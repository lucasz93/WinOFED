/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    GenUtils.h


Notes:

--*/

#pragma once

// OS
#include <ntddk.h>
#include "shutter.h"

#define GU_SET_FLAG(_M, _F)         ((_M)->Flags |= (_F))   
#define GU_CLEAR_FLAG(_M, _F)       ((_M)->Flags &= ~(_F))
#define GU_CLEAR_FLAGS(_M)          ((_M)->Flags = 0)
#define GU_TEST_FLAG(_M, _F)        (((_M)->Flags & (_F)) != 0)
#define GU_TEST_FLAGS(_M, _F)       (((_M)->Flags & (_F)) == (_F))


// max. length of full pathname
#define MAX_PATH   260
#define MAX_LONG_VALUE 0x7FFFFFFF

#define BITS_PER_LONG       32

#define GLOBAL_ALLOCATION_TAG 'XtoC'
#define SIZE_OF(A) (sizeof(A)/sizeof(A[0]))
#define FLOOR_4_MASK 0xFFFFFFFC


#define BUFFER_SIZE   100

#define SIZE_OF(A) (sizeof(A)/sizeof(A[0]))
#define SIZEOF_IN_BITS(_type) (8 * sizeof(_type))


uint64_t MtnicGetTickCount();
uint64_t  GetTickCountInMsec();
unsigned __int64 GetTickCountInNsec();

uint64_t GetTimeStampStart();
uint64_t GetTimeStamp(uint64_t Start);

VOID inline CheckTimes(uint64_t t1, uint64_t t2){
    if (t1 > t2) {
        ASSERT(t2 * 101/100 > t1);
    } else {
        ASSERT(t1 * 101/100 > t2);
    }
}


LARGE_INTEGER TimeFromLong(ULONG HandredNanos);
NTSTATUS Sleep(ULONG HandredNanos);
NTSTATUS GenUtilsInit(DWORD AllocationTag);

void guid_to_str(__in u64 guid,__out WCHAR* pstr,__in DWORD BufLen);

extern DWORD g_AllocTag;
void* __cdecl operator new(size_t n,__in char *str ) throw();
void* __cdecl operator new(size_t n ) throw();
void __cdecl operator delete(void* p);

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CALLED_FROM __FILE__ ":" TOSTRING(__LINE__)

#define NEW    new(CALLED_FROM)

#ifdef _WIN64
#define sfence()	 	KeMemoryBarrierWithoutFence(); _mm_sfence()
#else
#define sfence()	 	KeMemoryBarrierWithoutFence(); KeMemoryBarrier()
#endif

FORCEINLINE const u32 H_TO_BE(const u32 src)
{
    return src << 24 | 
           ((src << 8 ) & 0xff0000) | 
           ((src >> 8 ) & 0xff00) |
           (src >> 24);
}

inline unsigned int Floor_4(unsigned int value)
{
    return value&FLOOR_4_MASK;
}

struct AllocateSharedMemoryDeleteInfo {
    ULONG  Length;
    BOOLEAN  Cached;
    PVOID  VirtualAddress;
    PHYSICAL_ADDRESS  PhysicalAddress;

#if NDIS_SUPPORT_NDIS620
    BOOLEAN     fVMQ;
    BOOLEAN     fNonVMQ;
    NDIS_HANDLE AllocationHandle;
    NDIS_HANDLE SharedMemoryHandle;
    ULONG       SharedMemoryOffset;
#endif
};

void 
DbgPrintIpAddress(
    LPCSTR str_description,
    u8 ipAddress[],
    unsigned int traceLevel
    );

void 
DbgPrintMacAddress(
    LPCSTR str_description,
    u8 macAddress[],
    unsigned int traceLevel
    );

bool 
Validate16bitValue(
    __be16  be16_currentValue,
    u16 expectedValue,
    LPCSTR valueName
    );

bool
Validate8BitValue(
    u8 value,
    u8 expectedValue,
    LPCSTR valueName
    );

NTSTATUS 
ReadRegistryDword(
    LPCWSTR pszRegistryPath,
    LPCWSTR pszSuffix,
    LPCWSTR pszValueName,
    ULONG DefaultVal,
    LONG *pVal
    );

NTSTATUS 
ReadRegStrRegistryValueInNonPagedMemory(
    __in LPCWSTR pszRegistryPath,
    __in LPCWSTR pszSuffix,
    __in LPCWSTR pszValueName,
    __in unsigned int flags,
    __out LPWSTR* pWstr
    );

NTSTATUS ReadRegistryValue(
    IN LPCWSTR pszRegistryPath,
    IN LPCWSTR pszSuffix,
    IN LPCWSTR pszValueName,
    IN ULONG DefaultValueType,
    IN PVOID DefaultVal,
    IN ULONG DefaultValLength,
    IN ULONG Flags,
    OUT PVOID pVal
    );

u32 ROUNDUP_LOG2(u32 arg);



// This is simply a wrapper to the LIST_ENTRY class that allows 
// easier work with this list
class LinkedList {

public:
    LinkedList() {
       size = 0;
       InitializeListHead(&m_Data);
    }

    // Only used when the constructor can not be used.
    VOID Init() {
       size = 0;
       InitializeListHead(&m_Data);
    }

    DWORD Size() {return size;}

    LIST_ENTRY *RemoveHeadList() {
        LIST_ENTRY *pTemp;
        ASSERT(size > 0);
        ASSERT(!IsListEmpty(&m_Data));
        pTemp = ::RemoveHeadList(&m_Data);
        size--;
        return pTemp;        
    }

    LIST_ENTRY *RemoveTailList() {
        LIST_ENTRY *pTemp;
        ASSERT(size > 0);
        ASSERT(!IsListEmpty(&m_Data));
        pTemp = ::RemoveTailList(&m_Data);
        size--;
        return pTemp;        
    }

    
    VOID InsertTailList (LIST_ENTRY *Item) {
#if DBG        
        // Before we insert, we have to verify that the object is not in the list
        LIST_ENTRY *current = m_Data.Flink;
        while (current != & m_Data) {
            ASSERT(current != Item);
            current = current->Flink;
        }        
#endif        
        ::InsertTailList(&m_Data, Item);
        size++;
    }

    VOID InsertHeadList (LIST_ENTRY *Item) {
#if DBG        
        // Before we insert, we have to verify that the object is not in the list
        LIST_ENTRY *current = m_Data.Flink;
        while (current != & m_Data) {
            ASSERT(current != Item);
            current = current->Flink;
        }        
#endif        
        ::InsertHeadList(&m_Data, Item);
        size++;
    }

    LIST_ENTRY *Head() {
        ASSERT(size > 0);
        ASSERT(!IsListEmpty(&m_Data));
        return m_Data.Flink;
    }

    LIST_ENTRY *Tail() {
        ASSERT(size > 0);
        ASSERT(!IsListEmpty(&m_Data));
        return m_Data.Blink;
    }


    LIST_ENTRY *RawHead() {
        // Return the head of the list without any checks, 
        // needed in order to use it as in iterator
        return m_Data.Flink;
    }
    

    bool IsAfterTheLast(LIST_ENTRY *pEntry) {
        if (size == 0) {
            return true;
        }
        return &m_Data == pEntry;
    }

    VOID RemoveEntryList(LIST_ENTRY *Item) {
        ASSERT(size > 0);
        ASSERT(!IsListEmpty(&m_Data));        
#if DBG
        // Verify that this item is indeed in the list
        LIST_ENTRY *current = m_Data.Flink;
        while (current != Item) {
            if (current == & m_Data) {
                ASSERT(FALSE);
                //SDP_PRINT(TRACE_LEVEL_ERROR ,SDP_BUFFER_POOL ,("Object is not in the list\n"));
            }
            current = current->Flink;
        }
        
#endif
        ::RemoveEntryList(Item);
        size--;
    }
    
private:
    DWORD size;
    LIST_ENTRY m_Data;
};

//--------------------------------------
// Queue structure and macros
//--------------------------------------
typedef struct _QUEUE_ENTRY
{
    struct _QUEUE_ENTRY *Next;
} QUEUE_ENTRY, *PQUEUE_ENTRY;

typedef struct _QUEUE_HEADER
{
    PQUEUE_ENTRY Head;
    PQUEUE_ENTRY Tail;
} QUEUE_HEADER, *PQUEUE_HEADER;

#define InitializeQueueHeader(QueueHeader)                 \
    {                                                      \
        (QueueHeader)->Head = (QueueHeader)->Tail = NULL;  \
    }

#define IsQueueEmpty(QueueHeader) ((QueueHeader)->Head == NULL)

#define RemoveHeadQueue(QueueHeader)                  \
    (QueueHeader)->Head;                              \
    {                                                 \
        PQUEUE_ENTRY pNext;                           \
        ASSERT((QueueHeader)->Head);                  \
        pNext = (QueueHeader)->Head->Next;            \
        (QueueHeader)->Head = pNext;                  \
        if (pNext == NULL)                            \
            (QueueHeader)->Tail = NULL;               \
    }

#define InsertHeadQueue(QueueHeader, QueueEntry)                \
    {                                                           \
        ((PQUEUE_ENTRY)QueueEntry)->Next = (QueueHeader)->Head; \
        (QueueHeader)->Head = (PQUEUE_ENTRY)(QueueEntry);       \
        if ((QueueHeader)->Tail == NULL)                        \
            (QueueHeader)->Tail = (PQUEUE_ENTRY)(QueueEntry);   \
    }

#define InsertTailQueue(QueueHeader, QueueEntry)                     \
    {                                                                \
        ((PQUEUE_ENTRY)QueueEntry)->Next = NULL;                     \
        if ((QueueHeader)->Tail)                                     \
            (QueueHeader)->Tail->Next = (PQUEUE_ENTRY)(QueueEntry);  \
        else                                                         \
            (QueueHeader)->Head = (PQUEUE_ENTRY)(QueueEntry);        \
        (QueueHeader)->Tail = (PQUEUE_ENTRY)(QueueEntry);            \
    }



#define ETH_IS_LOCALLY_ADMINISTERED(Address) \
    (BOOLEAN)(((PUCHAR)(Address))[0] & ((UCHAR)0x02))
    



// A simpale static array (for now)
class OArray {
public:
    NTSTATUS Init(int MaxNumberofPackets);

    VOID Shutdown() {
        delete[]m_pData;
    }
    
    OArray() {
        m_Count = 0;
    }
    void Add(void *ptr) {
        ASSERT(m_Count < (int)m_Size);
        m_pData[m_Count++] = ptr;
    }

    int GetCount() {return m_Count;}

    void *GetPtr(int Place) {
        ASSERT(Place < m_Count);
        return m_pData[Place];
    }
    void Reset() {
        m_Count = 0;
    }

private:
    int m_Count;
    void **m_pData;
    unsigned int m_Size; // For Debug only

};


const int NO_FREE_LOCATION = -1;

template <typename obj>
class Array {
public:

    NTSTATUS 
    Init(int MaxObjects, bool IsPtr) {
        NTSTATUS Status = STATUS_SUCCESS;
        m_IsPtr = IsPtr;
        m_Size = MaxObjects;
        m_pData = NEW obj[MaxObjects];
        if (m_pData == NULL) {
//            GU_PRINT(TRACE_LEVEL_ERROR, GU,"new failed \n");
            return STATUS_NO_MEMORY;
        }
        if (IsPtr) {
            memset(m_pData, 0, MaxObjects * sizeof obj);
        }
        return Status;
    }

    Array(){
        m_pData = NULL;
        m_Size = 0;
    }


    ~Array() {
        if(m_pData) {
            delete[]m_pData;
            m_pData = NULL;
            m_Size = 0;
        }
    }

    VOID Shutdown() {
        if(m_pData) {
            delete[]m_pData;
            m_pData = NULL;
            m_Size = 0;
        }
    }
    

    obj &operator [ ](size_t i){ 
        ASSERT((int)i>=0 && (int)i< m_Size);
        return m_pData[i];
    }
        
    int GetMaxSize() {return m_Size;}


    NTSTATUS Reserve(int MaxObjects) {

    if (MaxObjects <= m_Size) {
        return STATUS_SUCCESS;
    }
    obj * pTemp = NEW obj[MaxObjects];
    if (pTemp == NULL) {
        //        GU_PRINT(TRACE_LEVEL_ERROR, GU,"new failed \n");
        return STATUS_NO_MEMORY;
    }
    memcpy(pTemp, m_pData,m_Size * sizeof (obj));
    delete m_pData;

    m_pData = pTemp;
    if (m_IsPtr) {
        memset(m_pData + m_Size, 0, (MaxObjects - m_Size)* sizeof obj);
    }

    m_Size = MaxObjects;

    return STATUS_SUCCESS;

}


    /*
        * This function will return a free location
        * in case the array is full it will be responsible to
        * allocate more space.
        * in any case of failure it will return -1
        */
    int GetFreeLocation( VOID )
    {
        int FreeLocation = NO_FREE_LOCATION;

        if (!m_IsPtr) {
            ASSERT(FALSE);  // TODO: No way to know if the data is valid
            return FreeLocation;
        }

        for (int i = 0; i < m_Size; i++) {
            if (m_pData[i] == NULL) {
                FreeLocation = i;
                break;
            }
        }

        if (FreeLocation == NO_FREE_LOCATION) {
            FreeLocation = m_Size;
            if (Reserve(2 * m_Size) != STATUS_SUCCESS) {
                FreeLocation = NO_FREE_LOCATION;
                }
        }

        return FreeLocation;
    }

private:
    obj *m_pData;
    int m_Size; 
    bool m_IsPtr;
    
};



#if DBG  
class VERIFY_DISPATCH_LEVEL {
public:
    VERIFY_DISPATCH_LEVEL(KIRQL irql = -1) {
        if (irql != (KIRQL)-1) {
            ASSERT(KeGetCurrentIrql() ==  irql);
        }

        StartLevel = KeGetCurrentIrql();

    }

    ~VERIFY_DISPATCH_LEVEL() {
        ASSERT(KeGetCurrentIrql() ==  StartLevel);
    }
private:
    KIRQL StartLevel;
};
#else
class VERIFY_DISPATCH_LEVEL {
public:
#pragma warning(push)
#pragma warning(disable:4100)   // unreferenced formal parameter
    VERIFY_DISPATCH_LEVEL(KIRQL irql = -1) {}
#pragma warning(pop)
};
#endif // DBG

template <class T>
class FIFO {
public:

    NTSTATUS Init(int MaxSize);

    FIFO() {
        m_pData = NULL;
        m_Head = 0;
        m_Tail = 0;
        m_Size = 0;
        m_Count = 0;
    }

    ~FIFO() {
        Shutdown();
    }

    VOID Shutdown() {
        if(m_pData != NULL) {
            delete []m_pData;
            m_pData = NULL;
        }
    }

    VOID Push(T pNewItem) {
        ASSERT(m_Count < m_Size);
        m_pData[m_Head++] = pNewItem;
        if(m_Head == m_Size) {
            m_Head = 0;
        }
        m_Count++;
        ASSERT(m_Count <= m_Size);
    }

    T Pop() {
        VOID *pRet = m_pData[m_Tail++];
        ASSERT(m_Count > 0);
        if(m_Tail == m_Size) {
            m_Tail = 0;
        }
        m_Count--;
        ASSERT(m_Count >= 0);
        return pRet;
    }
    int Count() {
        return m_Count;
    }

    bool IsFull() {
        return m_Size == m_Count;
    }

    bool IsEmpty() {
        return m_Count == 0;
    }

private:
    T     *m_pData;
    int    m_Head;
    int    m_Tail;
    int    m_Size;
    int    m_Count;
};

#define SIZEOF_IN_BITS(_type) (8 * sizeof(_type))

class Bitmap
{
public:

    static bool Set(ULONG* pData, ULONG BitIndex)
    {
        if(pData == NULL)
        {
            return false;
        }
        ULONG Offset = BitIndex / SIZEOF_IN_BITS(ULONG);
        ULONG Bit = (BitIndex % SIZEOF_IN_BITS(ULONG));

        if(pData[Offset] & (1 << Bit))
        {// already set
            return false;
        }
        pData[Offset] |= 1 << Bit;
        return true;
    }
    static bool Clear(ULONG* pData, ULONG BitIndex)
    {
        if(pData == NULL)
        {
            return false;
        }
        ULONG Offset = BitIndex / SIZEOF_IN_BITS(ULONG);
        ULONG Bit = (BitIndex % SIZEOF_IN_BITS(ULONG));

        if((pData[Offset] & (1 << Bit)) == 0)
        {// already clear
            return false;
        }
        pData[Offset] &= ~(1 << Bit);
        return true;
    }
    static bool Test(ULONG* pData, ULONG BitIndex)
    {
        if(pData == NULL)
        {// out of range
            return false;
        }
        ULONG Offset = BitIndex / SIZEOF_IN_BITS(ULONG);
        ULONG Bit = (BitIndex % SIZEOF_IN_BITS(ULONG));

        return (pData[Offset] & (1 << Bit)) != 0;
    }
};


//
//  Class Timer 
//
typedef void (CPassiveTimerFunc )(VOID *Context);

class CPassiveTimer
{
public:    
    NTSTATUS Initialize(IN PDEVICE_OBJECT  DeviceObject, CPassiveTimerFunc WorkerRoutine, PVOID  pContext);
    void Shutdown();
    
    bool Start(ULONG TimerIntervalMillis);
    bool Cancel();

private:
    
    void DPCRun();   
    void PassiveRun();
#ifdef NTDDI_WIN8
    static IO_WORKITEM_ROUTINE TimerPassive;
    static KDEFERRED_ROUTINE TimerFunc;
#else
    static VOID TimerPassive(PDEVICE_OBJECT pDevice, void *pContext);
    static VOID TimerFunc(struct _KDPC  *Dpc, PVOID  DeferredContext,PVOID  SystemArgument1,PVOID  SystemArgument2);
#endif


    KTIMER            m_Timer;
    KDPC              m_Dpc;
    shutter_t         m_cancel;

    PIO_WORKITEM      m_IoWorkItem;
    CPassiveTimerFunc    *m_WorkerRoutine;
    PVOID             m_pContext;


    KEVENT            m_GlobalEvent;
    
};


///////////////////////////////////////////////////////////////////////////////
// Tracer Class                                                              //
///////////////////////////////////////////////////////////////////////////////


enum EventType {
    PROCESS_RX_START,
    PROCESS_RX_END,
    PROCESS_RX_INTERNAL_START,
    PROCESS_RX_INTERNAL_START_SKIPING,
    COMPLEATD_INDICATING,
    MP_PORT_SEND_PACKETS

};

const int MAX_EVENTS = 10000;

class Tracer {
public:

    VOID Init();
    VOID AddEvent(EventType Event, int ExtraData);
    void Printxx() ;

private:

struct data {
    uint64_t  TimeStamp;
    EventType Event;
    int       ExtraData;
};

    data m_data[MAX_EVENTS];
    int m_CurrentLocation;


};

class CSpinLock 
{
public:

    CSpinLock()
    {
        KeInitializeSpinLock(&m_SpinLock);
#if DBG
        m_OldIrql = 0xff;
#endif
    }

    ~CSpinLock() 
    {
        ASSERT(m_OldIrql == 0xff);
    }

#ifdef NTDDI_WIN8   
    _IRQL_requires_max_(DISPATCH_LEVEL)
    _IRQL_raises_(DISPATCH_LEVEL)
    __drv_at(m_SpinLock, __drv_acquiresExclusiveResource(KSPIN_LOCK))
    __drv_at(m_OldIrql, __drv_savesIRQL)
#endif    
    void Lock() 
    {
        KeAcquireSpinLock(&m_SpinLock, &m_OldIrql);
    }

#ifdef NTDDI_WIN8   
        __drv_at(m_SpinLock, __drv_releasesExclusiveResource(KSPIN_LOCK))
        __drv_at(m_OldIrql, __drv_restoresIRQL)
#endif 
    void Unlock() 
    {
        KIRQL  OldIrql = m_OldIrql;
        ASSERT(m_OldIrql != 0xff);
#if DBG        
        m_OldIrql = 0xff;
#endif
#pragma prefast(suppress: 28151, "oldIrql can't get invalid IRQL value (0xFF) because it's always set in Lock")
        KeReleaseSpinLock(&m_SpinLock, OldIrql);
    }

private:
    KSPIN_LOCK m_SpinLock;
    KIRQL  m_OldIrql;
};

USHORT nthos(USHORT in);

NTSTATUS 
  MyKeWaitForSingleObject(
    IN PVOID  Object,
    IN KWAIT_REASON  WaitReason,
    IN KPROCESSOR_MODE  WaitMode,
    IN BOOLEAN  Alertable,
    IN PLARGE_INTEGER  Timeout  OPTIONAL,
    IN BOOLEAN ExceptApc = FALSE
    );


NTSTATUS
CopyFromUser(
    IN  void* const         p_dest,
    IN  const void* const   p_src,
    IN  const size_t        count 
    );

NTSTATUS
CopyToUser(
    IN  void* const         p_dest,
    IN  const void* const   p_src,
    IN  const size_t        count 
    );

VOID * MapUserMemory(
    IN  PVOID Address, 
    IN  ULONG size,
    OUT PMDL *ppMdl
    );

VOID UnMapMemory(
    IN  VOID *pKernelAddress,
    IN PMDL pMdl
    );

VOID UpdateRc(NTSTATUS *rc, NTSTATUS rc1);


/* Read registry defines & struct */
#define REG_OFFSET(base, field)   		((PULONG)&(base.field))
#define REG_OFFSET_INT(base, field)   	((int*)&(base.field))
#define REG_OFFSET_BWSD(base, field)	((PULONG)&(base.bwsd.field))
#define REG_SIZE(base, field)			sizeof(base.field)
#define REG_SIZE_BWSD(base, field)		sizeof(base.bwsd.field)
#define REG_STRING_CONST(x)				{sizeof(L##x)-2, sizeof(L##x), L##x}

typedef struct _REG_ENTRY
{
    UNICODE_STRING  RegName;     // variable name text
    PULONG          FieldOffset; // offset to the 'g' field
    ULONG           FieldSize;   // size (in bytes) of the field
    ULONG           Default;     // default value to use
    ULONG           Min;         // minimum value allowed
    ULONG           Max;         // maximum value allowed, -1 for unlimited
} REG_ENTRY, *pREG_ENTRY;
/* Read registry defines & struct */

NTSTATUS
ReadRegistry(
    IN LPCWSTR pRegistryPath,
    IN LPCWSTR pSuffixPath,
    IN pREG_ENTRY pRegEntry,
    IN ULONG RegEntrySize
    );

PVOID
GuCreateThread(
    PKSTART_ROUTINE ThreadFunc,
    PVOID Ctx
    );

LONG
RoundRecvRingSizeDown(LONG arg);


