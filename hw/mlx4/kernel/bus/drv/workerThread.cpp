/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    mp_WorkerThread.cpp

Abstract:
    This module contains Implmentation of RSS functions and data structures
    
Revision History:

Notes:

--*/
/*
extern "C" {
#include <ndis.h>
}
*/
#include "precomp.h"
#include "workerThread.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "workerThread.tmh"
#endif

LARGE_INTEGER  g_WorkerThreadTO ;//in 100-nanosecond units
ULONG g_WorkerThresholdInTicks = 0;

VOID RingThread(void *pContext)
{
    class WorkerThreadInfo *pWorkerThreadInfo = (WorkerThreadInfo*)pContext;
    pWorkerThreadInfo->ThreadFunction();
}

NTSTATUS WorkerThreadInfo::StartThread(ULONG CpuNumber) 
{
    NTSTATUS Status = STATUS_SUCCESS;
    OBJECT_ATTRIBUTES   attr;
    HANDLE  ThreadHandle;

    KeInitializeEvent(&m_ThreadWaitEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&m_ThreadWaitWhenPausedEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&m_ThreadIsPausedEvent, SynchronizationEvent, FALSE);

    
    InitializeObjectAttributes( &attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL );

    m_CpuNumber = CpuNumber;
    KeQueryTickCount(&m_LastTickCount);
   
    Status = PsCreateSystemThread(
                                &ThreadHandle, 
                                THREAD_ALL_ACCESS,
                                &attr,
                                NULL,
                                NULL,
                                ::RingThread,
                                this
                                );
    if (!NT_SUCCESS(Status)) 
    {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("PsCreateSystemThread failed\n"));
        goto Cleanup;
    }
    
    // Convert the thread into a handle
    Status = ObReferenceObjectByHandle(
          ThreadHandle,
          THREAD_ALL_ACCESS,
          NULL,
          KernelMode,
          &m_ThreadObject,
          NULL
          );
    
    ASSERT(Status == STATUS_SUCCESS); // According to MSDN, must succeed if I set the params
    
    Status = ZwClose(ThreadHandle);
    ASSERT(NT_SUCCESS(Status)); // Should always succeed
Cleanup:
    return Status;

}


VOID WorkerThreadInfo::StopThread() {
    NTSTATUS Status;
    int i;

    for (i=0; i < MAX_THREAD_USERS; i++) {
        if(m_WorkerThreadData[i]) {
            ASSERT(FALSE);            
        }
    }
    
    m_ThreadExit = true;
    KeSetEvent(&m_ThreadWaitEvent, IO_NO_INCREMENT, FALSE);
    Status = KeWaitForSingleObject(
            m_ThreadObject,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
    ASSERT(Status == STATUS_SUCCESS);
}

VOID WorkerThreadInfo::PauseThread() 
{
    m_ThreadPause = true;

    KeSetEvent(&m_ThreadWaitEvent, IO_NO_INCREMENT, FALSE);

    NTSTATUS Status;
    Status = KeWaitForSingleObject(
            &m_ThreadIsPausedEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
    ASSERT(Status == STATUS_SUCCESS);        
}

VOID WorkerThreadInfo::ResumeThread()
{
    ASSERT(m_ThreadPause == true);
    m_ThreadPause = false;

    KeSetEvent(&m_ThreadWaitWhenPausedEvent, IO_NO_INCREMENT, FALSE);
}    

NTSTATUS WorkerThreadInfo::AddRing(IN WorkerThreadData* pWorkerThreadData)
{

    int i;
    bool found = false;
    NTSTATUS Status = STATUS_SUCCESS;
    PauseThread();
    for (i=0; i < MAX_THREAD_USERS; i++) {
        if (m_WorkerThreadData[i] == NULL) {
            // We have found our empty entry
            m_WorkerThreadData[i] = pWorkerThreadData;
            found = true;
            break;
        }
    }

    if (found == false) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
            ("Too many rings? You must have a bug somewhere else\n"));
        ASSERT(FALSE);
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }

    if (i == m_MaxNumberOfRings) {
        // We have just added a new record to our table.
        m_MaxNumberOfRings = i+1;
    }

    m_WorkerThreadData[i]->m_pStartWorkEvent = &m_ThreadWaitEvent;
    m_WorkerThreadData[i]->m_pWorkByte = &m_StartPoll[i];
    m_StartPoll[i] = false;
    m_pWaitTimeOut = &g_WorkerThreadTO;
        
Cleanup:
    ResumeThread();
    return Status;

}


VOID WorkerThreadInfo::RemoveRing(IN WorkerThreadData* pWorkerThreadData)
{
    int i;
    PauseThread();

    int NumOfEntries = 0;
    bool found  = false;
    
    for (i=0; i < MAX_THREAD_USERS; i++) {
        if (m_WorkerThreadData[i] == pWorkerThreadData) {
            // We have found our entry
            pWorkerThreadData->m_pStartWorkEvent = NULL;
            m_WorkerThreadData[i] = NULL;
            m_StartPoll[i] = false;
            found = true;
        }
        else if(m_WorkerThreadData[i]) {
            NumOfEntries++;            
        }
    }
    ASSERT(found);

    if(NumOfEntries == 0)
    {
        m_pWaitTimeOut = NULL;
    }

    ResumeThread();
}

VOID WorkerThreadInfo::ThreadFunction() {
    NTSTATUS Status = STATUS_SUCCESS;
    int i;
    
#if !(NDIS_SUPPORT_NDIS620)
    ASSERT((1 << 14) > m_CpuNumber);
    #pragma prefast(suppress:6297, "former assert prevents overflow")
    KeSetSystemAffinityThread(1 << m_CpuNumber);
#else
    PROCESSOR_NUMBER ProcNum = {0};
    Status = KeGetProcessorNumberFromIndex(m_CpuNumber, &ProcNum);
    ASSERT(Status == STATUS_SUCCESS);
    
    GROUP_AFFINITY  Affinity = {0};
    Affinity.Group = ProcNum.Group;
    Affinity.Mask = (KAFFINITY)(1 << ProcNum.Number);
    KeSetSystemGroupAffinityThread(&Affinity,NULL);        
#endif

    MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV, ("RingThread called \n"));

    for(; ;) {
        ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
        Status = KeWaitForSingleObject(
            &m_ThreadWaitEvent,
            Executive,
            KernelMode,
            FALSE,
            m_pWaitTimeOut
            );
                    
        KeQueryTickCount(&m_LastTickCount);
        if(Status == STATUS_TIMEOUT)
        {
            continue;
        }
        
        ASSERT(Status == STATUS_SUCCESS);
        MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, ("Thread wake up\n"));

        if (m_ThreadExit) {
            // the driver going down
            break;
        }
        if (m_ThreadPause) {
#pragma prefast(suppress:28160, "Ignore this prefast error")
            KeSetEvent(&m_ThreadIsPausedEvent, IO_NO_INCREMENT, TRUE);
            Status = KeWaitForSingleObject(
                    &m_ThreadWaitWhenPausedEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                    );
            ASSERT(Status == STATUS_SUCCESS);

        }
        // Here is where we actually start polling
        bool ContineToPoll = true;
        g_pWorkerThreads->StartPolling();
        while(ContineToPoll && m_ThreadPause == false && m_ThreadExit == false) {
            ContineToPoll = false;                
            for (i=0; i < m_MaxNumberOfRings; i++) {
                if(m_StartPoll[i]) {
                    bool CurrentPoll = m_WorkerThreadData[i]->PollFunction();
                    ContineToPoll |= CurrentPoll;
                }      
            }

            g_pWorkerThreads->ContinuePolling();

        }   

        g_pWorkerThreads->StopPolling();
    
    }

    PsTerminateSystemThread(STATUS_SUCCESS);

}

bool WorkerThreadInfo::CheckIfStarving()
{    
    LARGE_INTEGER CurrentTickCount;
    KeQueryTickCount(&CurrentTickCount);

    if(CurrentTickCount.QuadPart-m_LastTickCount.QuadPart > g_WorkerThresholdInTicks)
    {
        return true;
    }
    
    return false;
}

#define WORKER_THREAD_TIME_OUT_MSEC  100
#define WORKER_THREAD_THRESHOLD_MSEC 500

NTSTATUS WorkerThreads::Init() 
{

    NTSTATUS Status = STATUS_SUCCESS;
    USHORT i, Started = 0;    

    g_WorkerThreadTO = TimeFromLong(WORKER_THREAD_TIME_OUT_MSEC*10*1000);

    ULONG TimeIncrement = KeQueryTimeIncrement();

    g_WorkerThresholdInTicks = (WORKER_THREAD_THRESHOLD_MSEC*10*1000)/TimeIncrement;

#if WINVER >= 0x602
	m_NumberOfCpus = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
#else 
	m_NumberOfCpus = KeQueryActiveProcessorCount(NULL);
#endif
	

    m_WorkerThreadsInfo = new WorkerThreadInfo[m_NumberOfCpus];
    if (m_WorkerThreadsInfo == NULL) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("new failed \n"));
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }

    // Start all threads
    for (i=0; i < m_NumberOfCpus; i++) {
        Status = m_WorkerThreadsInfo[i].StartThread(i);
        if (! NT_SUCCESS(Status)) {            
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
                ("StartThread failed, Error=0x%x\n", Status));
            Started = i;
            goto Cleanup; 
        }
    }

    m_lThreadNum = 0;
    KeInitializeSemaphore(&m_sem, g_MaximumWorkingThreads, g_MaximumWorkingThreads);
    KeInitializeEvent(&m_Lock, SynchronizationEvent, TRUE);
    
Cleanup:
    if (!NT_SUCCESS(Status)) {
        for (i=0; i < Started; i++) {
            m_WorkerThreadsInfo[i].StopThread();
        }
        if(m_WorkerThreadsInfo) {
            delete []m_WorkerThreadsInfo;
            m_WorkerThreadsInfo = NULL;
        }
    }
    return Status;

}

VOID WorkerThreads::ShutDown() 
{
    ULONG i;
    for (i=0; i < m_NumberOfCpus; i++) {
        m_WorkerThreadsInfo[i].StopThread();
    }
    delete []m_WorkerThreadsInfo;
    m_WorkerThreadsInfo = NULL;
}

NTSTATUS WorkerThreads::AddRing(
    IN ULONG CpuNumber,
    IN WorkerThreadData* pWorkerThreadData
    ) {

    NTSTATUS Status = STATUS_SUCCESS;
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Lock();

    ASSERT(CpuNumber < m_NumberOfCpus);
    WorkerThreadInfo *pThreadInfo = &m_WorkerThreadsInfo[CpuNumber];

    Status = pThreadInfo->AddRing(pWorkerThreadData);
    if (! NT_SUCCESS(Status)) {            
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
            ("pThreadInfo->AddRing failed, Error=0x%x\n", Status));
    }
    Unlock();
    return Status;

}

VOID WorkerThreads::RemoveRing(IN ULONG CpuNumber, IN WorkerThreadData* pWorkerThreadData)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(CpuNumber < m_NumberOfCpus);
    WorkerThreadInfo *pThreadInfo = &m_WorkerThreadsInfo[CpuNumber];

    Lock();
    pThreadInfo->RemoveRing(pWorkerThreadData);
    Unlock();
}


WorkerThreads *g_pWorkerThreads = NULL;

NTSTATUS mlx4_add_ring(IN ULONG CpuNumber, IN void* pWorkerThreadData)
{
    return g_pWorkerThreads->AddRing(CpuNumber, (WorkerThreadData*)pWorkerThreadData);
}

VOID mlx4_remove_ring(IN ULONG CpuNumber,IN void* pWorkerThreadData)
{
    g_pWorkerThreads->RemoveRing(CpuNumber, (WorkerThreadData*)pWorkerThreadData);
}

bool mlx4_check_if_starving(ULONG ulProcessorNumber)
{
    return g_pWorkerThreads->CheckIfStarving(ulProcessorNumber);
}

void mlx4_start_polling()
{
    g_pWorkerThreads->StartPolling();
}

void mlx4_stop_polling()
{
    g_pWorkerThreads->StopPolling();
}

void mlx4_continue_polling()
{
    g_pWorkerThreads->ContinuePolling();
}

