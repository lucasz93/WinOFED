#include "gu_precomp.h"
#include "gu_timer.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "gu_timer.tmh"
#endif

LONG IGUWorkItem::AddRef(LPCSTR str)
{
    ASSERT(RefCount >= 0);
    
    LONG rc = InterlockedIncrement(&RefCount);
    GU_PRINT(TRACE_LEVEL_VERBOSE, GU, "AddRef (%s): WI %p: new count %d\n", str, this, rc);

    return rc;
}

LONG IGUWorkItem::Release(LPCSTR str)
{
    ASSERT(RefCount > 0);

    ULONG rc = InterlockedDecrement(&RefCount);
    GU_PRINT(TRACE_LEVEL_VERBOSE, GU, "Release (%s): WI %p: new count %d\n", str, this, rc);

    if (rc == 0)
    {
        //
        // Free memory if there is no outstanding reference.
        //
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "Free WI %p\n", this);
        delete this;
    }   

    return rc;
}

IGUWorkItem::~IGUWorkItem()
{
    //
    //  The work item must be executed
    //
    ASSERT(m_pWorkerThread == NULL);
    
    if(m_pWorkerThread != NULL)
    {
        m_pWorkerThread->DequeueWorkItem(this);
    }
}

#ifdef NTDDI_WIN8
KSTART_ROUTINE GUThreadFunc;
#endif
VOID GUThreadFunc(void *pContext)
{
    class CGUWorkerThread *pWorkerThread = (CGUWorkerThread*) pContext;
    pWorkerThread->Run();
}

CGUWorkerThread::CGUWorkerThread() : 
    m_bIsStarted(false),
    m_bExit(false),
    m_ThreadObject( NULL)
{
    m_WorkItems.Init();
    KeInitializeEvent(&m_Event, SynchronizationEvent, FALSE);
    KeInitializeEvent(&m_StartEvent, SynchronizationEvent, FALSE);

}

CGUWorkerThread::~CGUWorkerThread()
{
    if(m_bIsStarted && ! m_bExit)
    {
        ASSERT(FALSE);
        Stop();
    }
}

NTSTATUS CGUWorkerThread::Start() 
{
    NTSTATUS Status = STATUS_SUCCESS;
    OBJECT_ATTRIBUTES   attr;
    HANDLE  ThreadHandle;

    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "====>CGUWorkerThread::Start: thread %p\n", this);

    InitializeObjectAttributes( &attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL );

    Status = PsCreateSystemThread(
                                &ThreadHandle, 
                                THREAD_ALL_ACCESS,
                                &attr,
                                NULL,
                                NULL,
                                ::GUThreadFunc,
                                this
                                );
    if (!NT_SUCCESS(Status)) 
    {
        GU_PRINT(TRACE_LEVEL_VERBOSE, GU, "PsCreateSystemThread failed\n");
        goto Cleanup;
    }

    Status = KeWaitForSingleObject(&m_StartEvent, Executive, KernelMode, FALSE, NULL);
    ASSERT(Status == STATUS_SUCCESS);
    
    
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

    m_bIsStarted = true;
    
Cleanup:
    
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "<====CGUWorkerThread::Stop: thread %p\n", this);
    return Status;

}

void CGUWorkerThread::Stop() 
{
    ASSERT(m_bIsStarted == true);
    
    NTSTATUS Status = STATUS_SUCCESS;
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "====>CGUWorkerThread::Stop: thread %p\n", this);

    if(! m_bExit && m_ThreadObject)
    {
        m_bExit = true;
        KeSetEvent(&m_Event, IO_NO_INCREMENT, FALSE);
        Status = KeWaitForSingleObject(
                m_ThreadObject,
                Executive,
                KernelMode,
                FALSE,
                NULL
                );
    }
    
    ASSERT(Status == STATUS_SUCCESS);

    ASSERT(m_WorkItems.Size() == 0);
    
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "====>CGUWorkerThread::Stop: thread %p\n", this);
}

void CGUWorkerThread::Run()
{    
    NTSTATUS Status;

    m_CurrentThread = KeGetCurrentThread();
    KeSetEvent(&m_StartEvent, IO_NO_INCREMENT, FALSE);

    while(! m_bExit)
    {
        ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
        Status = KeWaitForSingleObject(
            &m_Event,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
        ASSERT(Status == STATUS_SUCCESS);
        GU_PRINT(TRACE_LEVEL_VERBOSE, GU, "Thread wake up\n");
        
        m_Lock.Lock();
        
        while (m_WorkItems.Size() != 0)
        {
            PLIST_ENTRY p = m_WorkItems.RemoveHeadList();

            IGUWorkItem* pWorkItem = CONTAINING_RECORD(p, IGUWorkItem, m_Link);
            pWorkItem->m_pWorkerThread = NULL;
            m_Lock.Unlock();

            pWorkItem->Execute();
            ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
            pWorkItem->Release("CGUWorkerThread::Run");
            
            m_Lock.Lock();
        }
            
        m_Lock.Unlock();
    }
    
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS CGUWorkerThread::EnqueueWorkItem(IGUWorkItem *pWorkItem)
{    
    BOOLEAN IsEmpty = false;
    
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "====>CGUWorkerThread::EnqueueWorkItem: thread %p\n", this);
    if(! m_bExit)
    {
        m_Lock.Lock();
        IsEmpty = m_WorkItems.Size() == 0;
        m_WorkItems.InsertTailList(&pWorkItem->m_Link);
        pWorkItem->AddRef("CGUWorkerThread::EnqueueWorkItem");
        pWorkItem->m_pWorkerThread = this;
        m_Lock.Unlock();

        if(IsEmpty)
            KeSetEvent(&m_Event, IO_NO_INCREMENT, FALSE);
        
        GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "<====CGUWorkerThread::EnqueueWorkItem: thread %p SUCCESS\n", this);
        return STATUS_SUCCESS;
    }
    
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "<====CGUWorkerThread::EnqueueWorkItem: thread %p - NOT ACCEPTED\n", this);
    return STATUS_REQUEST_NOT_ACCEPTED;
}

NTSTATUS CGUWorkerThread::DequeueWorkItem(IGUWorkItem *pWorkItem)
{
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "====>CGUWorkerThread::DequeueWorkItem: thread %p\n", this);
    m_Lock.Lock();
    
    if ( pWorkItem->m_pWorkerThread )
    {
        m_WorkItems.RemoveEntryList(&pWorkItem->m_Link);
        pWorkItem->m_pWorkerThread = NULL;
        pWorkItem->Release("CGUWorkerThread::DequeueWorkItem");
    }
    m_Lock.Unlock();
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "<====CGUWorkerThread::DequeueWorkItem: thread %p\n", this);

    return STATUS_SUCCESS;
}

VOID
 GUTimerFunc(
    IN struct _KDPC  *Dpc,
    IN PVOID  DeferredContext,
    IN PVOID  SystemArgument1,
    IN PVOID  SystemArgument2
    )
{
    CGUTimer* pTimer = (CGUTimer *) DeferredContext;
    pTimer->Run();
}

__drv_floatUsed
CGUTimer::CGUTimer(
    CGUWorkerThread *pThread, 
    PKDEFERRED_ROUTINE CbFunc,
    IGUWorkItem *pWorkItem, 
    ULONG TimerIntervalMillis,
    bool  IsPeriodic
    ) : 
    m_pThread(pThread),
    m_pWorkItem(pWorkItem),
    m_bExit(false),
    m_IsPeriodic(IsPeriodic),
    m_TimerIntervalMillis(TimerIntervalMillis)
{
    m_TimerIntervalLimit = TimerIntervalMillis * 9 / 10;

    KeInitializeTimer(&m_Timer);
    KeInitializeDpc(&m_Dpc, CbFunc, this);

    m_TimerWorkItemEnqueued = false;
    m_CallingThread = pThread->GetCurrentThread();
    
    shutter_init(&m_cancel);
     
    m_TimerWorkItem.Init(this);
     
    AddRef("CGUTimer::CGUTimer");
}

CGUTimer::CGUTimer()
{
}


CGUTimer::~CGUTimer()
{
}

__drv_floatUsed
void CGUTimer::Init(
    CGUWorkerThread *pThread,
    PKDEFERRED_ROUTINE CbFunc,    
    IGUWorkItem *pWorkItem,    
    ULONG TimerIntervalMillis,
    bool  IsPeriodic
    )
{
    
    m_pThread = pThread;
    ASSERT(pWorkItem);
    m_pWorkItem = pWorkItem;
    m_bExit = false;
    m_IsPeriodic = IsPeriodic;
    m_TimerIntervalMillis = TimerIntervalMillis;

    m_TimerIntervalLimit = TimerIntervalMillis * 9 / 10;
    
    KeInitializeTimer(&m_Timer);
    KeInitializeDpc(&m_Dpc, CbFunc, this);
    m_TimerWorkItemEnqueued = false;
    m_CallingThread = pThread->GetCurrentThread();
    
    shutter_init(&m_cancel);
     
    m_TimerWorkItem.Init(this);
     
    AddRef("CGUTimer::CGUTimer");
}


LONG CGUTimer::AddRef(LPCSTR str)
{
    ASSERT(m_RefCount >= 0);
    
    LONG rc = InterlockedIncrement(&m_RefCount);
    GU_PRINT(TRACE_LEVEL_VERBOSE, GU, "AddRef (%s): Timer %p: new count %d\n", str, this, rc);

    return rc;
}

LONG CGUTimer::Release(LPCSTR str)
{
    ASSERT(m_RefCount > 0);

    UINT rc = InterlockedDecrement(&m_RefCount);
    GU_PRINT(TRACE_LEVEL_VERBOSE, GU, "Release (%s): Timer %p: new count %d\n", str, this, rc);

    if (rc == 0)
    {
        //
        // Free memory if there is no outstanding reference.
        //
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "Free Timer %p\n", this);
        delete this;
    }   

    return rc;
}

bool CGUTimer::Start()
{    
    return Start(m_TimerIntervalMillis);
}

bool CGUTimer::Start(DWORD dwTimerIntervalMillis)
{
    NTSTATUS Status = STATUS_SUCCESS;

    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "===>CGUTimer::Start\n");

    m_lock.Lock();

    ASSERT(m_cancel.cnt <= 1); // The timer is not intended to be started more than once. (actualy twice, since start can be called from the callback
    
    bool bret = true;
    if (shutter_use(&m_cancel) <= 0)
    {
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "Cancelling is in progress\n");
        bret = false;
        goto Exit;
    }
    
    BOOLEAN bPrevTimerWasCancelled = FALSE;

    AddRef("CGUTimer::Start");
    
    LARGE_INTEGER TimerInterval;
    if(dwTimerIntervalMillis == 0)
    {
        m_TimerWorkItemEnqueued = true;
        Status = m_pThread->EnqueueWorkItem(&m_TimerWorkItem);
        if(!NT_SUCCESS(Status)) {
            m_TimerWorkItemEnqueued = false;
            shutter_loose(&m_cancel);
            bret = false;
            goto Exit;
        }
        goto Exit;
    }
    else
    {
        TimerInterval.QuadPart = ((LONGLONG)-10000) * dwTimerIntervalMillis;
    }   

    bPrevTimerWasCancelled = KeSetTimer(&m_Timer, TimerInterval, &m_Dpc);
    if(bPrevTimerWasCancelled)
    {
        shutter_loose(&m_cancel);
        Release("CGUTimer::Start");
    }
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "<===CGUTimer::Start\n");

Exit:    
    m_lock.Unlock();
    return bret;  
}


void CGUTimer::Stop()
{
    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "====>CGUTimer::Stop: Timer %p, RefCount %d\n",
                this, m_RefCount);

    m_bExit = true;
    Cancel();
    
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "<====CGUTimer::Stop: Timer %p\n",
                this);
}

// true = timer was canceled and will not run
// false = event has just finished running
bool CGUTimer::Cancel()
{
    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);

    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "====>CGUTimer::cancel: Timer %p, RefCount %d\n",
                this, m_RefCount);

    bool bret = false;
    bool DequeueWI = false;

    m_lock.Lock();

    if(m_cancel.cnt == 0)
    {
        //
        //  Do not fail cancel call.
        //
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "Cancel called while timer is idle\n");
        m_lock.Unlock();
        return false;
    }

    BOOLEAN bTimerCancelled = KeCancelTimer(&m_Timer);
    if(bTimerCancelled)
    {
        shutter_loose(&m_cancel);
        Release("CGUTimer::Stop_cancel");
        bret = true;
    }

    // If we are being called by the same thread that runs the timer, and if have a workitem already scheduled, 
    // we can safely remove this work item and loose one of the cancel calls.

    if ( m_CallingThread == KeGetCurrentThread() && (m_TimerWorkItemEnqueued == true)) 
    {
        m_TimerWorkItemEnqueued = false;
        shutter_loose(&m_cancel);
        DequeueWI = true;
        bret = true;
    }


    
    m_lock.Unlock();

    if (DequeueWI) {
        m_pThread->DequeueWorkItem(&m_TimerWorkItem);

    }
    
    shutter_shut(&m_cancel);
    shutter_alive(&m_cancel);
    
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "<====CGUTimer::cancel: Timer %p\n", this);
    return bret;
}


// Runs at disatch level as the result of the DPC.
void CGUTimer::Run() 
{
    NTSTATUS Status = STATUS_SUCCESS;
    LARGE_INTEGER CurrentTime;
    VERIFY_DISPATCH_LEVEL(DISPATCH_LEVEL);
    
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "====>CGUTimer::Run: Timer %p\n", this);

    ASSERT(m_pWorkItem);
    ASSERT(m_TimerWorkItemEnqueued == false);

    m_lock.Lock();

    if(!m_bExit)
    {
        CurrentTime.QuadPart = GetTickCountInMsec();

        if (! m_IsPeriodic || (CurrentTime.QuadPart - m_LastRunTime.QuadPart) >= m_TimerIntervalLimit)
        {
            m_TimerWorkItemEnqueued = true;
            Status = m_pThread->EnqueueWorkItem(&m_TimerWorkItem);
            if(!NT_SUCCESS(Status)) {
                m_TimerWorkItemEnqueued = false;
                shutter_loose(&m_cancel);
            } 
            m_LastRunTime.QuadPart = CurrentTime.QuadPart;
        }
        else
        {
            ASSERT(FALSE);
            shutter_loose(&m_cancel);
            
            if(m_IsPeriodic)
            {
                Start(); 
            }
        }
    }
    else
    {
        shutter_loose(&m_cancel);
    }
    m_lock.Unlock();

    Release("CGUTimer::Run");
    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, "<====CGUTimer::Run: Timer %p\n", this);
}

void CGUTimer::PassiveRun()
{
    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);

    ASSERT(m_CallingThread == KeGetCurrentThread());

    ASSERT(m_TimerWorkItemEnqueued == true);
    m_TimerWorkItemEnqueued = false;

    ASSERT(m_pWorkItem);

    m_pWorkItem->Execute();

//    ASSERT(m_TimerWorkItemEnqueued == false);

    shutter_loose(&m_cancel);

    if(! m_bExit && m_IsPeriodic)
    {
        Start(); 
    }
}

// This is called by the timer thread
void CTimerWorkItem::Execute()
{
    m_pTimer->PassiveRun();
}



