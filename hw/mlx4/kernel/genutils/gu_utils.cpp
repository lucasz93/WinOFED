/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    GenUtils.cpp

Abstract:
    This module contains all debug-related code.

Revision History:

Notes:

--*/

#include "gu_precomp.h"
#include <bugcodes.h>

#ifdef offsetof
#undef offsetof
#endif
#if defined(EVENT_TRACING)
#include "gu_utils.tmh"
#endif

#include <complib/cl_init.h>

ULONG g_QueryTimeIncrement;
DWORD g_AllocTag = GLOBAL_ALLOCATION_TAG;

LARGE_INTEGER  TimeFromLong(ULONG HandredNanos)
{
    LARGE_INTEGER  Timeout;
    Timeout.HighPart = 0xffffffff;
    Timeout.LowPart =  0xffffffff ^ HandredNanos;
    return Timeout;
}

//
// Sleep function must be running at IRQL <= APC_LEVEL
//
// NOTE: The input parameter is in 100 Nano Second units. Multiply by 10000 to specify Milliseconds.
//
NTSTATUS Sleep(ULONG HandredNanos)
{    
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    NTSTATUS rc = STATUS_SUCCESS;
    LARGE_INTEGER  Timeout = TimeFromLong(HandredNanos);

    rc = KeDelayExecutionThread( KernelMode, FALSE, &Timeout );
    ASSERT(rc == STATUS_SUCCESS);
    
    return rc;

}


// In units of ms
uint64_t MtnicGetTickCount()
{

    LARGE_INTEGER	Ticks;
    KeQueryTickCount(&Ticks);
    return Ticks.QuadPart * g_QueryTimeIncrement / 10000; // 10,000 moves from 100ns to ms
}

// In units of ms
uint64_t GetTickCountInMsec()
{
    LARGE_INTEGER   Ticks;
    KeQueryTickCount(&Ticks);
    return Ticks.QuadPart * g_QueryTimeIncrement / 10000; // 10,000 moves from 100ns to ms
}

// In units of nano-seconds
uint64_t GetTickCountInNsec()
{
    LARGE_INTEGER   Ticks;
    KeQueryTickCount(&Ticks);
    return Ticks.QuadPart * g_QueryTimeIncrement * 100;
}



uint64_t GetTimeStampStart()
{
    
    LARGE_INTEGER tick_count, frequency;
    tick_count = KeQueryPerformanceCounter( &frequency );    

    return tick_count.QuadPart;
}


uint64_t GetTimeStamp(uint64_t Start )
{

    LARGE_INTEGER tick_count, frequency;
    u64 result;
    
    tick_count = KeQueryPerformanceCounter( &frequency );
    
    tick_count.QuadPart -= Start;
    
    if (tick_count.QuadPart == 0) {
        return 0;
    }    
    
    if (frequency.QuadPart <50000) {
        ASSERT(FALSE);
        return 0;
    }

    s64 freq1 = frequency.QuadPart / 50000;
    s64 tick1 = 1000000000 / freq1;

    // Make sure there is no overflow
    ASSERT(tick1 * tick_count.QuadPart / tick_count.QuadPart == tick1);

    result = /*(s64)*/ (  tick1 * tick_count.QuadPart/ (frequency.QuadPart / freq1));

    return result;

}



u32 ROUNDUP_LOG2(u32 arg)
{
    if (arg <= 1)    return 0;
    if (arg <= 2)    return 1;
    if (arg <= 4)    return 2;
    if (arg <= 8)    return 3;
    if (arg <= 16)   return 4;
    if (arg <= 32)   return 5;
    if (arg <= 64)   return 6;
    if (arg <= 128)  return 7;
    if (arg <= 256)  return 8;
    if (arg <= 512)  return 9;
    if (arg <= 1024) return 10;
    if (arg <= 2048) return 11;
    if (arg <= 4096) return 12;
    if (arg <= 8192) return 13;
    if (arg <= 16384) return 14;
    if (arg <= 32768) return 15;
    if (arg <= 65536) return 16;
    ASSERT(FALSE);
    return 32;
}

NTSTATUS GenUtilsInit(DWORD AllocationTag)
{
    g_QueryTimeIncrement = KeQueryTimeIncrement();    
    g_AllocTag = AllocationTag;

    return STATUS_SUCCESS;
}

class ZeroMemoryClass {
} zmClass;


void* __cdecl operator new(size_t n, char *str ) throw() 
{
#ifndef CL_TRACK_MEM
	UNREFERENCED_PARAMETER(str);
#endif

    //From WinDDK: "Avoid calling ExAllocatePoolWithTag with memory size == 0. Doing so will result in pool header wastage"
    // Verifier with low mem simulation will crash with  memory size == 0
    //TODO throw exception
    if (n ==0) {
        return &zmClass;
    }
    
#pragma prefast(suppress:6014, "The object is released in delete")
#pragma prefast(suppress:28197, "The object is released in delete")

#ifdef CL_TRACK_MEM
	void * p = cl_zalloc_ex( n, str );
#else
    void * p = ExAllocatePoolWithTag(NonPagedPool , n, g_AllocTag);
#endif
    if (p) {
        RtlZeroMemory(p , n);
    }
    return p;
}

void* __cdecl operator new(size_t n ) throw() 
{

    //From WinDDK: "Avoid calling ExAllocatePoolWithTag with memory size == 0. Doing so will result in pool header wastage"
    // Verifier with low mem simulation will crash with  memory size == 0
    //TODO throw exception
    if (n ==0) {
        return &zmClass;
    }
    
#pragma prefast(suppress:6014, "The object is released in delete")
#pragma prefast(suppress:28197, "The object is released in delete")

#ifdef CL_TRACK_MEM
	void * p = cl_zalloc( n );
#else
    void * p = ExAllocatePoolWithTag(NonPagedPool , n, g_AllocTag);
#endif
    if (p) {
        RtlZeroMemory(p , n);
    }
    return p;
}

void __cdecl operator delete(void* p) 
{
    if ((p != &zmClass) && (p != NULL))
    {
#ifdef CL_TRACK_MEM
		cl_free( p );
#else
        ExFreePoolWithTag(p, g_AllocTag);
#endif
    }
}

void* __cdecl operator new(size_t n, void *addr ) throw()
{
    return addr;
}


NTSTATUS 
OArray::Init(int MaxNumberofPackets) {
    NTSTATUS Status = STATUS_SUCCESS;
    m_Size = MaxNumberofPackets;
    m_pData = new("Array Init() ") void*[MaxNumberofPackets];
    if (m_pData == NULL) {
        GU_PRINT(TRACE_LEVEL_ERROR, GU,"new failed \n");
        return STATUS_NO_MEMORY;
    }
    return Status;
}

#if 0
NTSTATUS
    
ProcessorArray::Init(int MaxNumberofPackets) {
    NTSTATUS Status = STATUS_SUCCESS;
    u32 i = 0,j=0;
    m_NumberOfProcessors = NdisSystemProcessorCount();
    m_Arrays = new Array[m_NumberOfProcessors];
    if (m_Arrays == NULL) {
        GU_PRINT(TRACE_LEVEL_ERROR, GU,"new failed \n");
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    for (i=0; i < m_NumberOfProcessors; i++) {
        Status = m_Arrays[i].Init(MaxNumberofPackets);
        if (!NT_SUCCESS(Status)) {
            GU_PRINT(TRACE_LEVEL_ERROR, GU,"Array[i].Init failed \n");
            goto Cleanup;
        }
    }

Cleanup:
    if (!NT_SUCCESS(Status)) {
        if (m_Arrays) {
            for (j=0; j< i; j++) {
                m_Arrays[i].Shutdown();
            }
            delete []m_Arrays;
            m_Arrays = NULL;
        }
        m_NumberOfProcessors = 0;

    }
    return Status;
}
#endif

NTSTATUS ReadRegistryDword(
    IN LPCWSTR pszRegistryPath,
    IN LPCWSTR pszSuffix,
    IN LPCWSTR pszValueName,
    IN ULONG DefaultVal,
    OUT LONG *pVal
    )
{
    NTSTATUS    status;
    /* Remember the terminating entry in the table below. */
    RTL_QUERY_REGISTRY_TABLE    table[2];
    UNICODE_STRING              ParamPath;

    ASSERT(NULL != pszRegistryPath);
    ASSERT(NULL != pszValueName);
    ASSERT(NULL != pVal);

    USHORT suffixLength = 0;

    if (NULL != pszSuffix)
    {
        suffixLength = (USHORT)wcslen(pszSuffix) ;
    }

    RtlInitUnicodeString( &ParamPath, NULL );
    USHORT length = (USHORT)wcslen(pszRegistryPath) + suffixLength + 1;
    ParamPath.Length = (length -1) * sizeof(WCHAR);       // length in bytes, of the Buffer, not including the terminating NULL character
    ParamPath.MaximumLength = length * sizeof(WCHAR);     // total size in bytes, of memory allocated for Buffer
    ParamPath.Buffer = new("ReadRegistryDword() ") WCHAR[length];
    if( !ParamPath.Buffer ) 
    {
        GU_PRINT(TRACE_LEVEL_ERROR, GU,"Failed to allocate parameters path buffer\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlStringCchCopyW(ParamPath.Buffer, length, pszRegistryPath);

    if (NULL != pszSuffix)
    {
        #pragma prefast(suppress:6053, "The ParamPath.Buffer is preallocated to the required length, and the assert checks for this assumption")
        RtlStringCchCatW(ParamPath.Buffer, length, pszSuffix);
    }
    
    //
    //Clear the table.  This clears all the query callback pointers,
    // and sets up the terminating table entry.
    //
    memset(table, 0, sizeof(table));

    //
    // Setup the table entries. 
    //
    table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    table[0].Name = const_cast <LPWSTR>(pszValueName);
    table[0].EntryContext = pVal;
    table[0].DefaultType = REG_DWORD;
    table[0].DefaultData = &DefaultVal;
    table[0].DefaultLength = sizeof(ULONG);
    
    status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, ParamPath.Buffer, table, NULL, NULL );
    if (!NT_SUCCESS(status)) 
    {
        GU_PRINT(TRACE_LEVEL_ERROR, GU,"RtlQueryRegistryValues failed status =0x%x\n", status);
        *pVal = DefaultVal;
        status = STATUS_SUCCESS;
    }

    GU_PRINT(TRACE_LEVEL_INFORMATION, GU, " status %#x, path %S, name %S \n", 
        status, ParamPath.Buffer, table[0].Name );

    delete [] ParamPath.Buffer;
    return status;
}

NTSTATUS 
ReadRegStrRegistryValueInNonPagedMemory(
    IN LPCWSTR pszRegistryPath,
    IN LPCWSTR pszSuffix,
    IN LPCWSTR pszValueName,
    IN unsigned int flags,
    OUT LPWSTR * pWstr
    )
{
    //
    // NDIS Query Unicode in ReadRegistryValue allocates PAGED memory
    // Hence this function using our customized operator new for Non paged allocation
    //
    
    UCHAR* pWcharTemp = NULL;
    *pWstr = NULL;
    
    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);
    
    UNICODE_STRING tempString = { 0, 0, NULL};
    
    NTSTATUS ntStatus = ReadRegistryValue(
        pszRegistryPath,
        pszSuffix,
        pszValueName,
        REG_NONE,
        NULL,
        0,
        flags,
        &tempString
        );

    if(tempString.Buffer == NULL)
    {
        ntStatus = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    
    if(ntStatus != STATUS_SUCCESS)
    {
        switch (ntStatus)
        {
            case STATUS_OBJECT_NAME_NOT_FOUND:
                GU_PRINT(TRACE_LEVEL_ERROR, GU,  "Resitry string failed to read: Suffix =  %S, Name = %S not found\n",pszSuffix, pszValueName);
                break;                
            default:
                GU_PRINT(TRACE_LEVEL_ERROR, GU,  "Resitry string failed to read with NTSTATUS 0x%X\n",ntStatus);
        }
        return ntStatus;
    }


    GU_PRINT(TRACE_LEVEL_INFORMATION, GU,  "Read string value from registry: %S\n", tempString.Buffer);
    const ULONG c_NdisStringMaxLength = tempString.MaximumLength;
    pWcharTemp = new("ReadRegStrRegistryValueInNonPagedMemory() ") UCHAR[c_NdisStringMaxLength];
    if (NULL == pWcharTemp)
    {
        //
        // Allocaton failed
        //
        RtlFreeUnicodeString(&tempString);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(pWcharTemp,tempString.Buffer, c_NdisStringMaxLength);
    *pWstr = (LPWSTR)pWcharTemp;

    RtlFreeUnicodeString(&tempString);

    return STATUS_SUCCESS;
}


NTSTATUS ReadRegistryValue(
    IN LPCWSTR pszRegistryPath,
    IN LPCWSTR pszSuffix,
    IN LPCWSTR pszValueName,
    IN ULONG DefaultValueType,
    IN PVOID DefaultVal,
    IN ULONG DefaultValLength,
    IN ULONG Flags,
    OUT PVOID pVal
    )
{
    NTSTATUS    status;
    /* Remember the terminating entry in the table below. */
    RTL_QUERY_REGISTRY_TABLE    table[2];
    UNICODE_STRING              ParamPath;

    ASSERT(NULL != pszRegistryPath);
    ASSERT(NULL != pszValueName);
    ASSERT(NULL != pVal);

    USHORT suffixLength = 0;

    if (NULL != pszSuffix)
    {
        suffixLength = (USHORT)wcslen(pszSuffix) ;
    }

    RtlInitUnicodeString( &ParamPath, NULL );
    USHORT length = (USHORT)wcslen(pszRegistryPath) + suffixLength + 1;
    ParamPath.Length = (length -1) * sizeof(WCHAR);       // length in bytes, of the Buffer, not including the terminating NULL character
    ParamPath.MaximumLength = length * sizeof(WCHAR);     // total size in bytes, of memory allocated for Buffer
    ParamPath.Buffer = new("ReadRegistryValue() ") WCHAR[length];    
    if( !ParamPath.Buffer ) 
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlStringCchCopyW(ParamPath.Buffer, length, pszRegistryPath);

    if (NULL != pszSuffix)
    {
        #pragma prefast(suppress:6053, "The ParamPath.Buffer is preallocated to the required length, and the assert checks for this assumption")
        RtlStringCchCatW(ParamPath.Buffer, length, pszSuffix);
    }
    
    //
    //Clear the table.  This clears all the query callback pointers,
    // and sets up the terminating table entry.
    //
    memset(table, 0, sizeof(table));

    //
    // Setup the table entries. 
    //
    table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | Flags;
    table[0].Name = const_cast <LPWSTR>(pszValueName);
    table[0].EntryContext = pVal;
    table[0].DefaultType = DefaultValueType;
    table[0].DefaultData = DefaultVal;
    table[0].DefaultLength = DefaultValLength;
    
    status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, ParamPath.Buffer, table, NULL, NULL );
    if (!NT_SUCCESS(status) && DefaultVal != NULL) 
    {
        GU_PRINT(TRACE_LEVEL_WARNING, GU,  "RtlQueryRegistryValues failed to read %S\\%S. status =0x%x. Use deafault value.\n", 
                            ParamPath.Buffer, table[0].Name, status);
        RtlMoveMemory(pVal, DefaultVal, DefaultValLength);
        status = STATUS_SUCCESS;
    }

    GU_PRINT(TRACE_LEVEL_WARNING, GU,   " status 0x%x, path %S, name %S \n", 
                            status, ParamPath.Buffer, table[0].Name);

    delete [] ParamPath.Buffer;    
    return status;
}


void 
DbgPrintMacAddress(
    LPCSTR str_description, 
    u8 macAddress[],
    unsigned int traceLevel
    )
{
    ASSERT(NULL != macAddress);
    ASSERT(NULL != str_description);

        GU_PRINT(traceLevel, GU, 
                        "%s%.2X-%.2X-%.2X-%.2X-%.2X-%.2X\n",
                        str_description,
                        macAddress[0], macAddress[1],macAddress[2],
                        macAddress[3],macAddress[4],macAddress[5]
                        );
}

void 
DbgPrintIpAddress(
    LPCSTR str_description,
    u8 ipAddress[],
    unsigned int traceLevel
    )
{
    ASSERT(NULL != ipAddress);
    ASSERT(NULL != str_description);

    GU_PRINT(traceLevel, GU, 
                        "%s%d.%d.%d.%d\n",
                        str_description,
                        ipAddress[0], ipAddress[1],ipAddress[2],ipAddress[3]
                        );
}

bool
Validate16bitValue(
    __be16  be16_currentValue,
    u16 expectedValue,
    LPCSTR valueName)
{
    ASSERT(NULL != valueName);
    u16 valueByHardwareBytesOrder = be16_to_cpu(be16_currentValue);
    if (valueByHardwareBytesOrder != expectedValue)
    {
        GU_PRINT(TRACE_LEVEL_VERBOSE, GU, 
            "ARP detection: %s field; Expected Value = %0xX, current Value = %0xX\n",
            valueName,expectedValue,valueByHardwareBytesOrder
            );
        return false;
    }
    return true;
}

bool
Validate8BitValue(
    u8 value,
    u8 expectedValue,
    LPCSTR valueName)
{
    ASSERT(NULL != valueName);
    if (value != expectedValue)
    {
        GU_PRINT(TRACE_LEVEL_VERBOSE, GU, 
            "ARP detection: %s field; Expected Value = %0xX, current Value = %0xX\n",
            valueName,expectedValue,value
            );
        return false;
    }
    return true;
}

void guid_to_str(u64 guid, WCHAR * pWstr, DWORD BufLen)
{
    PUCHAR pGuid = (UCHAR*)&guid;

    char temp[BUFFER_SIZE] = {0};

    HRESULT hr = RtlStringCbPrintfA(temp, BUFFER_SIZE , "%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x",
                                                    pGuid[7],
                                                    pGuid[6],
                                                    pGuid[5],
                                                    pGuid[4],
                                                    pGuid[3],
                                                    pGuid[2],
                                                    pGuid[1],
                                                    pGuid[0]
                                                    );
    ASSERT(hr == STATUS_SUCCESS)
    UNREFERENCED_PARAMETER(hr);

    mbstowcs(pWstr, temp, BUFFER_SIZE);
}


template <class T>
NTSTATUS  FIFO<T>::Init(int MaxSize) {
    ASSERT(m_pData == NULL);
    m_pData = new("FIFO<T>::Init() ") VOID *[MaxSize];
    if (m_pData == NULL) {
        GU_PRINT(TRACE_LEVEL_ERROR, GU,"new failed\n");
        return STATUS_NO_MEMORY;
    }
    m_Head = m_Tail = 0;
    m_Size = MaxSize;
    m_Count = 0;
    return STATUS_SUCCESS;
}


// This is needed in order to force the compiler to create the function  
void FIFO_DUMMY_FUNCTION(){
    FIFO <VOID *> m_DataToSend;

    m_DataToSend.Init(5);
}

///////////////////////////////////////////////////////////////////////////////
// Tracer Class                                                              //
///////////////////////////////////////////////////////////////////////////////

VOID Tracer::Init() {
    m_CurrentLocation = 0;
}
#if 0
__drv_floatUsed
VOID Tracer::AddEvent(EventType Event, int ExtraData) {
    if (m_CurrentLocation >= MAX_EVENTS) {
        return;
    }
    int Location = m_CurrentLocation++;
    if ((Location > 0) && ( Event == PROCESS_RX_INTERNAL_START)) {
        if (m_data[Location-1].Event == PROCESS_RX_INTERNAL_START) {
            ExtraData = 0;
            Event = PROCESS_RX_INTERNAL_START_SKIPING;
        } else if (m_data[Location-1].Event == PROCESS_RX_INTERNAL_START_SKIPING) {
        Location--;
            ExtraData = m_data[Location].ExtraData+1;
            Event = PROCESS_RX_INTERNAL_START_SKIPING;
        }


    }
    m_data[Location].TimeStamp = GetTimeStamp(); the function has changed, dont enter floating point, please
    m_data[Location].Event = Event;
    m_data[Location].ExtraData = ExtraData;

}
#endif
void Tracer::Printxx() {
    int i;
    for(i=0; i < m_CurrentLocation; i++) {
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "Time = %I64d: ", m_data[i].TimeStamp / 1000);
        switch(m_data[i].Event) {
            case PROCESS_RX_START:
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "PROCESS_RX_START\n");
                break;
            case PROCESS_RX_END:
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "PROCESS_RX_END handeled %d packets\n", m_data[i].ExtraData);
                break;
            case PROCESS_RX_INTERNAL_START:
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "PROCESS_RX_INTERNAL_START\n");
                break;
            case PROCESS_RX_INTERNAL_START_SKIPING:
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "PROCESS_RX_INTERNAL_START_SKIPING pooled %d times\n", m_data[i].ExtraData);
                break;

            case MP_PORT_SEND_PACKETS:
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "MP_PORT_SEND_PACKETS \n");
                break;

            case COMPLEATD_INDICATING:
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "COMPLEATD_INDICATING handeled %d packets\n", m_data[i].ExtraData);
                break;
            default:
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "iligal event %d\n", m_data[i].Event);
        }
    }
    m_CurrentLocation = 0;
}

USHORT ntohs(USHORT in)
{
    return ((in & 0xff) << 8) | ((in & 0xff00) >> 8);
}

// BUGBUG: Understand how to reomove the 20 from the code.
// This function is a wrapper for the KeWaitForSingleObject that adds
// assertsions to the valuas returned by it
NTSTATUS 
  MyKeWaitForSingleObject(
    IN PVOID  Object,
    IN KWAIT_REASON  WaitReason,
    IN KPROCESSOR_MODE  WaitMode,
    IN BOOLEAN  Alertable,
    IN PLARGE_INTEGER  Timeout  OPTIONAL,
    IN BOOLEAN ExceptApc    
    )
{
    NTSTATUS rc = STATUS_SUCCESS;
    int i;
    for (i=0; i < 20; i++) {
        rc = KeWaitForSingleObject(
                Object,
                WaitReason,
                WaitMode,
                Alertable,
                Timeout
        );
        if (!NT_SUCCESS(rc)) {
            ASSERT(FALSE);
            GU_PRINT(TRACE_LEVEL_ERROR ,GU ,"KeWaitForSingleObject failed rc = 0x%x\n", rc );
            KeBugCheck(CRITICAL_SERVICE_FAILED);
        }
        ASSERT((rc == STATUS_SUCCESS ) ||
               (rc == STATUS_ALERTED  ) ||
               (rc == STATUS_USER_APC  ) ||
               (rc == STATUS_TIMEOUT  )); // This are simply all the return code from DDK
        
        ASSERT( (Timeout != NULL ) || rc != STATUS_TIMEOUT);
        if (rc != STATUS_USER_APC) {
            break;
        } else {
            // Currently we only expect to have an APC from the user threads call back
            if (ExceptApc == FALSE) {
                GU_PRINT(TRACE_LEVEL_WARNING ,GU ,("KeWaitForSingleObject was stoped because of STATUS_USER_APC\n" ));
                ASSERT(FALSE);
            } else {
                break;                
            }
        }
    }
    if (i == 20) {
        GU_PRINT(TRACE_LEVEL_ERROR ,GU ,("!!!! KeWaitForSingleObject was Exhausted STATUS_USER_APC\n" ));
        // This is probably fine if we are runnign for a user thread
        ASSERT((WaitReason == UserRequest) && (WaitMode == UserMode));
    }
    return rc;
}

int ExceptionFilter(unsigned int code, struct _EXCEPTION_POINTERS *ep) {
    // This filter currently only allows us to check the error, before we contiue
    ASSERT(FALSE);
    return EXCEPTION_EXECUTE_HANDLER;
}

NTSTATUS
CopyFromUser(
    IN  void* const         p_dest,
    IN  const void* const   p_src,
    IN  const size_t        count )
{
    /*
     * The memory copy must be done within a try/except block as the
     * memory could be changing while the buffer is copied.
     */
    __try
    {
        ProbeForRead( (void*)p_src, count, 1 );
#ifdef DONT_COPY_DATA        
        if (count < 1000){
            RtlCopyMemory( p_dest, p_src, count );
        }
#else
        RtlCopyMemory( p_dest, p_src, count );
#endif
        return STATUS_SUCCESS;
    }
    __except(ExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
        GU_PRINT(TRACE_LEVEL_ERROR ,GU ,("copying memory from user failed\n"));
        return STATUS_ACCESS_DENIED;
    }
}


NTSTATUS
CopyToUser(
    IN  void* const         p_dest,
    IN  const void* const   p_src,
    IN  const size_t        count 
    )
{
    /*
     * The memory copy must be done within a try/except block as the
     * memory could be changing while the buffer is copied.
     */
    __try
    {
        ProbeForWrite( p_dest, count, 1 );
#ifdef DONT_COPY_DATA        
        if (count < 1000){
            RtlCopyMemory( p_dest, p_src, count );
        }
#else
        RtlCopyMemory( p_dest, p_src, count );
#endif
        return STATUS_SUCCESS;
    }
    __except(ExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
        GU_PRINT(TRACE_LEVEL_ERROR ,GU ,("copying memory to user failed\n"));
        return STATUS_ACCESS_DENIED;
    }
}

VOID * MapUserMemory(
    IN  PVOID Address, 
    IN  ULONG size,
    OUT PMDL *ppMdl
    ) {
    // Create the MDL:
    PMDL pMdl = NULL;
    PVOID pKernelAddress;

    // Probe here for write
    __try
    {
        ProbeForWrite( Address, size, 1 );

        pMdl = IoAllocateMdl(Address, size, FALSE, FALSE, NULL);
        ASSERT(pMdl != NULL);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        GU_PRINT(TRACE_LEVEL_ERROR ,GU ,("copying memory to user failed\n"));
        ASSERT(FALSE);        
        return NULL;
    }
    if (pMdl == NULL) {
        ASSERT(FALSE);
        return NULL;
    }

    __try {
        MmProbeAndLockPages(pMdl, KernelMode , IoModifyAccess );
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ASSERT(FALSE);
        IoFreeMdl(pMdl);
        return NULL;
    }

    pKernelAddress = MmMapLockedPagesSpecifyCache(
        pMdl,
        KernelMode,
        MmCached , //??????????????
        NULL,
        FALSE,
        LowPagePriority 
        );

    // Copy the output data
    *ppMdl = pMdl;
    return pKernelAddress;
}

VOID UnMapMemory(
    IN  VOID *pKernelAddress,
    IN PMDL pMdl)
{
    MmUnmapLockedPages(pKernelAddress, pMdl);

    MmUnlockPages(pMdl);

    IoFreeMdl(pMdl);

}

VOID UpdateRc(NTSTATUS *rc, NTSTATUS rc1)
{
    // We want to keep the first errro
    if (NT_SUCCESS(*rc)) {
        *rc = rc1;
    }
}

VOID CPassiveTimer::TimerPassive(PDEVICE_OBJECT pDevice, void *pContext)
{
    CPassiveTimer *pNdisTimer = (CPassiveTimer *)pContext;
    pNdisTimer->PassiveRun();
}

VOID
CPassiveTimer::TimerFunc(
    struct _KDPC  *Dpc,
    PVOID  DeferredContext,
    PVOID  SystemArgument1,
    PVOID  SystemArgument2
    )
{
    CPassiveTimer* pTimer = (CPassiveTimer *) DeferredContext;
    pTimer->DPCRun();
}


NTSTATUS CPassiveTimer::Initialize(IN PDEVICE_OBJECT  DeviceObject, CPassiveTimerFunc WorkerRoutine, PVOID pContext)
{    

    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);
    m_IoWorkItem = IoAllocateWorkItem(DeviceObject);
    if (m_IoWorkItem == NULL) {
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "IoAllocateWorkItem failed \n");
        return STATUS_NO_MEMORY;
    }

    shutter_init(&m_cancel);
        
    KeInitializeTimer(&m_Timer);
    KeInitializeDpc(&m_Dpc, TimerFunc, this);

    KeInitializeEvent(&m_GlobalEvent, SynchronizationEvent, TRUE);    

    m_WorkerRoutine = WorkerRoutine;
    m_pContext = pContext;
    return STATUS_SUCCESS;
}


bool CPassiveTimer::Start(ULONG TimerIntervalMillis)
{
    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);
    KeWaitForSingleObject(&m_GlobalEvent, Executive, KernelMode , FALSE, NULL);
    bool ret = true;

    if (shutter_use(&m_cancel) <= 0) {
        ret = false;
        goto Exit;
    }
    
    BOOLEAN bPrevTimerWasCancelled = FALSE;
    LARGE_INTEGER TimerInterval;
    if(TimerIntervalMillis == 0)
    {
        IoQueueWorkItem(m_IoWorkItem, TimerPassive, DelayedWorkQueue, this);
        goto Exit;
    }
    else
    {
        TimerInterval.QuadPart = ((LONGLONG)-10000) * TimerIntervalMillis;
    }     
   
    bPrevTimerWasCancelled = KeSetTimer(&m_Timer, TimerInterval, &m_Dpc);
    ASSERT(bPrevTimerWasCancelled == FALSE);

Exit:    
    KeSetEvent(&m_GlobalEvent, IO_NO_INCREMENT, FALSE);
    return ret;
}

// true = timer was canceled and will not run
// false = event has just finished running
bool CPassiveTimer::Cancel()
{
    bool ret = false;
    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);

    KeWaitForSingleObject(&m_GlobalEvent, Executive, KernelMode , FALSE, NULL);
                      
    BOOLEAN bTimerCancelled = KeCancelTimer(&m_Timer);
    if(bTimerCancelled)
    {
        shutter_loose(&m_cancel);
        ret = true;
    }    

    // the call is still running
    
    KeSetEvent(&m_GlobalEvent, IO_NO_INCREMENT, FALSE);
    shutter_shut(&m_cancel);
    return ret;
}


void CPassiveTimer::Shutdown()
{
    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);
    Cancel();
    if (m_IoWorkItem != NULL) {
        IoFreeWorkItem(m_IoWorkItem);
        m_IoWorkItem = NULL;
    }
}


void CPassiveTimer::DPCRun()
{
    IoQueueWorkItem(m_IoWorkItem, TimerPassive, DelayedWorkQueue, this);
}


void CPassiveTimer::PassiveRun()
{    
    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);
    m_WorkerRoutine(m_pContext);
    shutter_loose(&m_cancel);
}



NTSTATUS
ReadRegistry(
    IN LPCWSTR pRegistryPath,
    IN LPCWSTR pSuffixPath,
    IN pREG_ENTRY pRegEntry,
    IN ULONG RegEntrySize
    )
{
    ULONG value;
    NTSTATUS status = STATUS_SUCCESS;
    
    // read all the registry values 
    for (uint16_t i = 0; i < RegEntrySize; i++, pRegEntry++)
    {
        PULONG pointer = (PULONG)pRegEntry->FieldOffset;

        status = ReadRegistryDword(pRegistryPath, pSuffixPath, pRegEntry->RegName.Buffer, pRegEntry->Default, (LONG*)&value);
        if (!NT_SUCCESS(status)) {
            GU_PRINT(TRACE_LEVEL_ERROR, GU, "ReadRegistryDword failed 0x%x\n", status);
            goto end;
        }
        
        if (NT_SUCCESS (status)) {
            // Check for value limitation (Min-Max)
            if ((value >= pRegEntry->Min && value <= pRegEntry->Max) ||
                (pRegEntry->Max == -1))
            {
                *pointer = value;
                GU_PRINT(TRACE_LEVEL_ERROR, GU,
                    "Read registry: %S, value = 0x%x\n", pRegEntry->RegName.Buffer, value);
            }
            else {  // Value is illegal set default 
                GU_PRINT(TRACE_LEVEL_ERROR, GU,
                    "Illegal value is read for registry: %S. Use default value: 0x%x\n", pRegEntry->RegName.Buffer, value);
                *pointer = pRegEntry->Default;
            }
        }
        else {  // Failed to read registry value, will set default value
            GU_PRINT(TRACE_LEVEL_ERROR, GU,
                "Failed to Read registry: %S, using default value = 0x%x\n", pRegEntry->RegName.Buffer, pRegEntry->Default);
            *pointer = pRegEntry->Default;
        }
    }

    status = STATUS_SUCCESS;

end:
    return status;
}


PVOID
GuCreateThread(
    PKSTART_ROUTINE ThreadFunc,
    PVOID Ctx
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    OBJECT_ATTRIBUTES   attr;
    HANDLE  ThreadHandle;
    PVOID m_ThreadObject = NULL;

    InitializeObjectAttributes( &attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL );

    Status = PsCreateSystemThread(
                                &ThreadHandle, 
                                THREAD_ALL_ACCESS,
                                &attr,
                                NULL,
                                NULL,
                                ThreadFunc,
                                Ctx
                                );
    if (!NT_SUCCESS(Status)) 
    {
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "PsCreateSystemThread failed\n");
        goto Thread_Cleanup;
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

Thread_Cleanup:
    return m_ThreadObject;
}


LONG
RoundRecvRingSizeDown(LONG arg)
{
    if (arg >= 4096) return 4096;
    if (arg >= 2048) return 2048;
    if (arg >= 1024) return 1024;
    if (arg >= 512)  return 512;
    if (arg >= 256)  return 256;

    GU_PRINT(TRACE_LEVEL_WARNING, GU, "RecvRingSize Is Invalid.\n");
    ASSERT(FALSE);
    return 256;   // Min value for RecvRingSize
}



