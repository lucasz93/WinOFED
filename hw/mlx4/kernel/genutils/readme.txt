This library unifies in it general useful utilities.
To use it one needs to link with genutils.lib.

genutils provides:

//
// gu_utils.h - general utils file
//

Flags manipulation:
	(assuming M has a field named Flags)
	- GU_SET_FLAG(_M, _F)
	- GU_CLEAR_FLAG(_M, _F)
	- GU_CLEAR_FLAGS(_M)
	- GU_TEST_FLAG(_M, _F)
	- GU_TEST_FLAGS(_M, _F)

Time utilities:
	- GenUtilsInit()			- init the QueryTimeIncrement factor
	- GetTickCountInMsec()			- returns tick count in milliseconds
	- GetTickCountInNsec()			- returns tick count in nanoseconds	
	- GetTimeStamp()			- returns tick count divided by frequency in units of nanoseconds
	- TimeFromLong(ULONG HandredNanos)	- converts time from ULONG representation to LARGE_INTEGER representation
	- Sleep(ULONG HandredNanos)		- returns STATUS_SUCCESS after the specified time has passed.
						  Sleep function must be running at IRQL <= APC_LEVEL.
					  	  NOTE: The input parameter is in 100 Nano Second units. Multiply by 10000 to specify Milliseconds.
	- MyKeWaitForSingleObject 		- A wrapper for the KeWaitForSingleObject that adds assertions to the values returned by it

General utils:
	- ROUNDUP_LOG2(u32 arg)			- return the log2 of the given number rounded up
	- guid_to_str(u64 guid, WCHAR* pstr, DWORD BufLen)
	- H_TO_BE(const u32 src)
	- Floor_4(UINT value)
	- nthos(USHORT in)
	- DbgPrintIpAddress(LPCSTR str_description, u8 ipAddress[], unsigned int traceLevel)
	- DbgPrintMacAddress(LPCSTR str_description, u8 macAddress[], unsigned int traceLevel)
	- UpdateRc(NTSTATUS *rc, NTSTATUS rc1)	- set rc to be rc1 if rc was a success value (>0)

Memory utils:
	- AllocateSharedMemory	- allocate ndis shared memory according to Ndis.h _NDIS_SHARED_MEMORY_PARAMETERS
	- FreeSharedMemory
	- CopyFromUser		- copy from source buffer to destination buffer a given number of bytes. Checks that the source can be read
	- CopyToUser		- copy from source buffer to destination buffer a given number of bytes. Checks that the destination can be written to
	- MapUserMemory		- lock and map specified memory pages
	- UnMapMemory		- unmap and unlock specified memory pages

Registry values:
	- ReadRegistryDword
	- ReadRegStrRegistryValueInNonPagedMemory
	- ReadRegistryValue
	
VERIFY_DISPATCH_LEVEL:
	At the begining of the function one should call:
	VERIFY_DISPATCH_LEVEL(KIRQL irql), this call will verify that the current IRQL is the given IRQL.
	At the end of the function the distructor of the class will ASSERT that the level stayed the same 
	throughout the function.
	
CSpinLockWrapper:
   	- CSpinLockWrapper (KSPIN_LOCK &SpinLock) 	- Spinlock must already be initialized
	- Lock() 					- Uses KeAcquireSpinLock and saves the IRQL by itself
	- Unlock() 					- Uses KeReleaseSpinLock

LinkedList:
	- Init() 
	- Size()
	- RemoveHeadList()  			- returns LIST_ENTRY*
	- RemoveTailList()  			- returns LIST_ENTRY*
	- InsertTailList(LIST_ENTRY *Item)
	- InsertHeadList(LIST_ENTRY *Item)
	- Head()				- returns LIST_ENTRY*. ASSERTS that the list is not empty! 
	- Tail()				- returns LIST_ENTRY*. ASSERTS that the list is not empty! 
	- RawHead()				- returns LIST_ENTRY*. Return the head of the list without any checks, to be used as an iterator
    	- IsAfterTheLast(LIST_ENTRY *pEntry)  	- true if the list is empty or the entry is the raw head.
  	- RemoveEntryList(LIST_ENTRY *Item)   	- ASSERTS that the list is not empty!

Queue:
	- InitializeQueueHeader(QueueHeader)                 
    	- IsQueueEmpty(QueueHeader) 
	- RemoveHeadQueue(QueueHeader)                
	- InsertHeadQueue(QueueHeader, QueueEntry)    
	- InsertTailQueue(QueueHeader, QueueEntry)                     
    
Array (A simple static array):
	- Init(int MaxNumberofPackets)
	- Shutdown()   
    	- Array()
	- Add(void *ptr)	- add member to the current count (as indicated by GetCount())
	- GetCount()
	- GetPtr(int Place)	- get member from a given index
    	- Reset()		- after a call to this function the next add will be into the first index

ProcessorArray:
	This class is used for freeing the sent packets.
	It is based on the assumption that this happens at raised irql and therefore,
	if we allocate a data structure for each processor we should be fine
	- Init(int MaxNumberofPackets)
	- Shutdown() 
	- GetArray() 		- returns a reseted array of the current processor

FIFO:
	- Init(int MaxSize)
	- Shutdown() 
	- Push(T pNewItem)
	- Pop() 
	- Count() 
	- IsFull()
	- IsEmpty() 

Bitmap:
	- Set(ULONG* pData, ULONG BitIndex)	- returns true if the bit was set and false if it was already set or is out of range
	- Clear(ULONG* pData, ULONG BitIndex)	- returns true if the bit was cleared and false if it was already clear or is out of range
	- Test(ULONG* pData, ULONG BitIndex)	- returns true if the bit is set and false if it is clear or out of range

//
// gu_timer.h 
//

IGUWorkItem:
	The element that is queue for execution. It must have a void Execute() function.

CGUWorkerThread:
	- Start()		- start the thread. The thread will run its Run() function.
    	- Stop()		- signals the Run() function to stop its execution.
    	- Run()			- while Stop() was not called, wait for an item to be enqueued and execute all currently enqueued items
	- EnqueueWorkItem(IGUWorkItem *pWorkItem)	
   	- DequeueWorkItem(IGUWorkItem *pWorkItem)

CGUTimer:
	- Initialize(CGUWorkerThread *pThread, IGUWorkItem *pWorkItem, ULONG TimerIntervalMillis = 0, bool  IsPeriodic = true)
					- init a timer on an execution thread, a specific work item. 
					  The thread will enqueue the item after a given delay 
					  and will requeue it each interval if the timer is periodic.
	- Run()				- enqueue the item for execution. Called when the delay expires.
    	- Cancel()			- stops the wait for the delay to end, meaning the enqueue at the end of the delay will not take place.
					  returns true if timer was canceled and will not run or was idle
					  returns false if the event has just finished running and cannot be canceled anymore
	- Start()			- internally used to re-start the timer on the delay time, if the timer is periodic
	- Start(ULONG dwInterval)	- may be used to re-start the timer on a new delay time
    	- Stop()			- Cancel and release timer
	- PassiveRun()			- internally used to run the work item in PASSIVE_LEVEL
 
