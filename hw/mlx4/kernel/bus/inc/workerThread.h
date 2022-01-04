/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    mp_WorkerThread.h

Abstract:
    This module contains decleration of RSS functions and data structures
    
Revision History:

Notes:

--*/


// values for the different RSS modes

const int MAX_CARDS = 64;
const int MAX_THREAD_USERS = MAX_CARDS * 6;

class WorkerThreadData;

class WorkerThreadData {


public:

    WorkerThreadData() {
        m_pWorkByte = NULL;
        m_pStartWorkEvent = NULL;
    }

    VOID StartPoll() {
        *m_pWorkByte = true;
        KeSetEvent(m_pStartWorkEvent ,IO_NETWORK_INCREMENT ,FALSE);
    }

    VOID StopPoll() {
        *m_pWorkByte = false;
    }

    virtual bool PollFunction() = 0;

    char *m_pWorkByte;
    KEVENT *m_pStartWorkEvent;
    

};

 


VOID RingThread(void *pContext);


struct mlx4_en_rx_ring;
struct mlx4_en_cq;


class WorkerThreadInfo {
public:

    NTSTATUS StartThread(ULONG CpuNumber); 

    VOID StopThread();

    VOID PauseThread() ;

    VOID ResumeThread();

    VOID ThreadFunction();
    

    NTSTATUS AddRing(IN WorkerThreadData* pWorkerThreadData);

    VOID RemoveRing(IN WorkerThreadData* pWorkerThreadData);

    WorkerThreadInfo() {
        memset(m_StartPoll, 0, sizeof m_StartPoll);
        memset(m_WorkerThreadData, 0, sizeof(m_WorkerThreadData));
        m_CpuNumber = 0;
        m_MaxNumberOfRings = 0;
        m_ThreadPause = false;
        m_ThreadExit = false;
        m_ThreadObject = NULL;        
        m_pWaitTimeOut = NULL;
    }
        
    bool CheckIfStarving();

private:    
    char m_StartPoll[MAX_THREAD_USERS];  // A boolean to indicate that we shuld poll on the ring

    WorkerThreadData *m_WorkerThreadData[MAX_THREAD_USERS];

    ULONG m_CpuNumber;
    int    m_MaxNumberOfRings;

    // Needed in order to pause the thread
    KEVENT                  m_ThreadWaitWhenPausedEvent;
    KEVENT                  m_ThreadIsPausedEvent;
    bool                    m_ThreadPause;

    // Needed in order to stop the thread

    KEVENT                  m_ThreadWaitEvent;
    bool                    m_ThreadExit;
    PVOID                   m_ThreadObject;

    LARGE_INTEGER           m_LastTickCount;
    PLARGE_INTEGER          m_pWaitTimeOut;
};

extern int g_MaximumWorkingThreads;

class WorkerThreads {
public:
    NTSTATUS Init();
    VOID ShutDown();

    NTSTATUS AddRing(
        IN ULONG CpuNumber, 
        IN WorkerThreadData* pWorkerThreadData
        );

    VOID RemoveRing(IN ULONG CpuNumber,IN WorkerThreadData* pWorkerThreadData);

    inline void StartPolling()
    {
        InterlockedIncrement(&m_lThreadNum);
        NTSTATUS Status = STATUS_SUCCESS;
        
        Status = KeWaitForSingleObject(
                    &m_sem,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                    );
        ASSERT(Status == STATUS_SUCCESS);
    }

    inline void StopPolling()
    {
        InterlockedDecrement(&m_lThreadNum);
        KeReleaseSemaphore(&m_sem, 0, 1, FALSE);
    }

    inline void ContinuePolling()
    {
        if (m_lThreadNum <= g_MaximumWorkingThreads )
        {
            return;
        }
        StopPolling();
        StartPolling();
    }


    inline bool CheckIfStarving(ULONG ulProcessorNumber)
    {
        return m_WorkerThreadsInfo[ulProcessorNumber].CheckIfStarving();
    }

private:

    ULONG m_NumberOfCpus;

    WorkerThreadInfo *m_WorkerThreadsInfo;

    KEVENT           m_Lock;
    KSEMAPHORE       m_sem;
    LONG             m_lThreadNum;

    VOID Lock() {
        NTSTATUS Status = STATUS_SUCCESS;
        Status = KeWaitForSingleObject(
                &m_Lock,
                Executive,
                KernelMode,
                FALSE,
                NULL
                );
        ASSERT(Status == STATUS_SUCCESS);

    }

    VOID Unlock() {
        KeSetEvent(&m_Lock, IO_NO_INCREMENT, FALSE);
    }
    
};


extern WorkerThreads *g_pWorkerThreads;

NTSTATUS mlx4_add_ring(IN ULONG CpuNumber, IN void* pWorkerThreadData);
VOID mlx4_remove_ring(IN ULONG CpuNumber,IN void* pWorkerThreadData);
bool mlx4_check_if_starving(ULONG ulProcessorNumber);

void mlx4_start_polling();
void mlx4_stop_polling();
void mlx4_continue_polling();

