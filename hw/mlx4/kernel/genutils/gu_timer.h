#pragma once 

#include "shutter.h"

class CGUWorkerThread;
class CSpinLock;

struct IGUWorkItem
{  
    IGUWorkItem() :
        m_pWorkerThread(NULL)
    {
        AddRef("IGUWorkItem::IGUWorkItem");
    }

    virtual ~IGUWorkItem();
    virtual void Execute() = 0;

    LONG AddRef(LPCSTR str);
    LONG Release(LPCSTR str);

    LIST_ENTRY          m_Link;
    CGUWorkerThread*    m_pWorkerThread;
    LONG                RefCount;
};

class CGUWorkerThread
{
    friend VOID GUThreadFunc(void *pContext);

    public:
        CGUWorkerThread();
        ~CGUWorkerThread();
        NTSTATUS Start();
        void Stop();

        NTSTATUS EnqueueWorkItem(IGUWorkItem *pWorkItem);
        NTSTATUS DequeueWorkItem(IGUWorkItem *pWorkItem);

        PRKTHREAD GetCurrentThread() {
            return m_CurrentThread;
        }

    private:

        void Run();

        LinkedList                  m_WorkItems;        
        CSpinLock                   m_Lock;
        KEVENT                      m_Event;
        bool                        m_bExit;
        PVOID                       m_ThreadObject;
        bool                        m_bIsStarted;
        PRKTHREAD                   m_CurrentThread;
        KEVENT                      m_StartEvent;
};

class CGUTimer;

#ifdef NTDDI_WIN8
KDEFERRED_ROUTINE GUTimerFunc;
#endif
VOID GUTimerFunc(
    IN struct _KDPC  *Dpc,
    IN PVOID  DeferredContext,
    IN PVOID  SystemArgument1,
    IN PVOID  SystemArgument2
    );

class CTimerWorkItem : public IGUWorkItem
{
public:
    void Init(CGUTimer* pTimer)
    {
        m_pTimer = pTimer;
    }
    
    ~CTimerWorkItem()
    {
    }
    
    void Execute();   
    
public:    
    CGUTimer* m_pTimer;
};

class CGUTimer
{
friend  VOID  GUTimerFunc(
    IN struct _KDPC  *Dpc,
    IN PVOID  DeferredContext,
    IN PVOID  SystemArgument1,
    IN PVOID  SystemArgument2
    );

friend class CTimerWorkItem;
public:
    
    CGUTimer(
        CGUWorkerThread *pThread,
        PKDEFERRED_ROUTINE CbFunc,        
        IGUWorkItem *pWorkItem, 
        ULONG TimerIntervalMillis = 0,
        bool  IsPeriodic = true);

    CGUTimer();
    ~CGUTimer();

    LONG AddRef(LPCSTR str);
    LONG Release(LPCSTR str);

    void Init(
        CGUWorkerThread *pThread,
        PKDEFERRED_ROUTINE CbFunc,
        IGUWorkItem *pWorkItem, 
        ULONG TimerIntervalMillis = 0,
        bool  IsPeriodic = true);
    

    bool Cancel();

    bool Start();
    bool Start(ULONG dwInterval);
    void Stop();

    void Run();
    
private:
    void PassiveRun();



    KTIMER              m_Timer;
    KDPC                m_Dpc;    
    CSpinLock           m_lock;
    LONG                m_RefCount;
    ULONG               m_TimerIntervalMillis;
    ULONG               m_TimerIntervalLimit;
    CGUWorkerThread*    m_pThread;
    IGUWorkItem*        m_pWorkItem;
    bool                m_bExit;
    bool                m_IsPeriodic;
    LARGE_INTEGER       m_LastRunTime;
    
    shutter_t           m_cancel;
    CTimerWorkItem      m_TimerWorkItem;
    bool                m_TimerWorkItemEnqueued;
    
    PRKTHREAD           m_CallingThread;
};


VOID
 GUTimerFunc(
    IN struct _KDPC  *Dpc,
    IN PVOID  DeferredContext,
    IN PVOID  SystemArgument1,
    IN PVOID  SystemArgument2
    );


