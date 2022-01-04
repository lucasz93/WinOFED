/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    gu_dbg.cpp

Abstract:
    This modulde contains all related dubug code
Notes:

--*/
#include "gu_precomp.h"
#include <ndis.h>
#include <stdarg.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "gu_dbg.tmh"
#endif

LONG g_dev_assert_enabled = 1;

#if DBG

//
// Bytes to appear on each line of dump output.
//
#define DUMP_BytesPerLine 16


CGUDebugFlags g_GUDbgFlagsDef[] = {
    { L"GU", TRACE_LEVEL_ERROR},
    { L"GU_INIT", TRACE_LEVEL_ERROR}
    };


VOID dbg_out( IN PCCH  format, ...)
{
    va_list  list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, format, list);
    va_end(list);
}

void DebugGUPrintInit(IN LPCWSTR pszRegistryPath, CGUDebugFlags* pDbgFlags, DWORD size)
{
    LONG resultFromRegistry = TRACE_LEVEL_ERROR;
    
    for (DWORD i = 0; i < size; ++i)
    {
        DWORD defaultVal = pDbgFlags[i].dbgLevel;
        NTSTATUS Status = ReadRegistryDword(
                                pszRegistryPath,
                                L"\\Parameters\\Debug",
                                pDbgFlags[i].pszName,
                                defaultVal,
                                &resultFromRegistry);        
        if (NT_SUCCESS(Status))  
        {
            pDbgFlags[i].dbgLevel = resultFromRegistry;
        }
    }
}


#if !defined(EVENT_TRACING)
VOID
TraceGUMessage(
    IN PCCHAR  func,
    IN PCCHAR  file,
    IN ULONG   line,
    IN ULONG   level,
    IN PCCHAR  format,
    ...
    )
/*++

Routine Description:

    Debug print for the sample driver.

Arguments:

    TraceEventsLevel - print level between 0 and 3, with 3 the most verbose

Return Value:

    None.

 --*/
 {
#if DBG

    va_list    list;
    NTSTATUS   status;
    
    va_start(list, format);

    char psPrefix[TEMP_BUFFER_SIZE];
    PCCHAR  fileName = strrchr(file, '\\');
    if (fileName != NULL)
    {
        fileName++;
    }
    
    if(level == TRACE_LEVEL_ERROR) 
    {
        status = RtlStringCchPrintfA(psPrefix, TEMP_BUFFER_SIZE, "***ERROR***  %s (%s:%d) ", func, fileName, line);
    }
    else
    {
        status = RtlStringCchPrintfA(psPrefix, TEMP_BUFFER_SIZE, "%s (%s:%d) ", func, fileName, line);
        level = TRACE_LEVEL_ERROR;
    }
    
    ASSERT(NT_SUCCESS(status));
    vDbgPrintExWithPrefix(psPrefix , DPFLTR_IHVNETWORK_ID, level, format, list);

    va_end(list);
    
#else

    UNREFERENCED_PARAMETER(TraceEventsLevel);
    UNREFERENCED_PARAMETER(TraceEventsFlag);
    UNREFERENCED_PARAMETER(DebugMessage);

#endif
}
#endif

// Hex dump 'cb' bytes starting at 'p' grouping 'ulGroup' bytes together.
// For example, with 'ulGroup' of 1, 2, and 4:
//
// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 |................|
// 0000 0000 0000 0000 0000 0000 0000 0000 |................|
// 00000000 00000000 00000000 00000000 |................|
//
// If 'fAddress' is true, the memory address dumped is prepended to each
// line.
//
VOID
Dump(
    __in_bcount(cb) IN CHAR*   p,
    IN ULONG   cb,
    IN BOOLEAN fAddress,
    IN ULONG   ulGroup 
    )
{
    INT cbLine;

    while (cb)
    {

        cbLine = (cb < DUMP_BytesPerLine) ? cb : DUMP_BytesPerLine;
#pragma prefast(suppress: __WARNING_POTENTIAL_BUFFER_OVERFLOW, "p is bounded by cb bytes");        
        DumpLine( p, cbLine, fAddress, ulGroup );
        cb -= cbLine;
        p += cbLine;
    }
}

VOID
DumpLine(
    __in_bcount(cb) IN CHAR*   p,
    IN ULONG   cb,
    IN BOOLEAN fAddress,
    IN ULONG   ulGroup 
    )
{

    CHAR* pszDigits = "0123456789ABCDEF";
    CHAR szHex[ ((2 + 1) * DUMP_BytesPerLine) + 1 ];
    CHAR* pszHex = szHex;
    CHAR szAscii[ DUMP_BytesPerLine + 1 ];
    CHAR* pszAscii = szAscii;
    ULONG ulGrouped = 0;

    if (fAddress) 
    {
        dbg_out( "E100: %p: ", p );
    }
    else 
    {
        dbg_out( "E100: " );
    }

    while (cb)
    {
#pragma prefast(suppress: __WARNING_POTENTIAL_BUFFER_OVERFLOW, "pszHex accessed is always within bounds");    
        *pszHex++ = pszDigits[ ((UCHAR )*p) / 16 ];
        *pszHex++ = pszDigits[ ((UCHAR )*p) % 16 ];

        if (++ulGrouped >= ulGroup)
        {
            *pszHex++ = ' ';
            ulGrouped = 0;
        }

#pragma prefast(suppress: __WARNING_POTENTIAL_BUFFER_OVERFLOW, "pszAscii is bounded by cb bytes");
        *pszAscii++ = (*p >= 32 && *p < 128) ? *p : '.';

        ++p;
        --cb;
    }

    *pszHex = '\0';
    *pszAscii = '\0';

    dbg_out(
        "%-*s|%-*s|\n",
        (2 * DUMP_BytesPerLine) + (DUMP_BytesPerLine / ulGroup), szHex,
        DUMP_BytesPerLine, szAscii );
}

void DevAssertInit(IN LPCWSTR pszRegistryPath)
{
    LONG defaultVal = g_dev_assert_enabled;
    
    ReadRegistryDword(
            pszRegistryPath,
            L"\\Parameters\\Debug",
            L"DEV_ASSERT",
            defaultVal,
            &g_dev_assert_enabled);  
}

#endif // DBG

VOID print( IN PCCH  format, ...)
{
    va_list  list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, format, list);
    va_end(list);
}

//
//Convet the int number to WCHAR string and return the size in WCHAR string, the Caller function need to free the buffer.
//

void ConvertIntToWString(IN unsigned int input,WCHAR* res, OUT size_t *str_size)
{
    WCHAR temp[20] = {0};
    unsigned int reminder = 0 ;
    int i=-1;
    bool flag = true;
    
    while (flag)
    {
        i++;
        reminder = input %16 ;
        
        switch (reminder)
        {   
            case 0:
                temp[i] = L'0';
                break;
            case 1:
                temp[i] = L'1';
                break;
            case 2:
                temp[i] = L'2';
                break;
            case 3:
                temp[i] = L'3';
                break;
            case 4:
                temp[i] = L'4';
                break;
            case 5:
                temp[i] = L'5';
                break;
            case 6:
                temp[i] = L'6';
                break;
            case 7:
                temp[i] = L'7';
                break;
            case 8:
                temp[i] = L'8';
                break;
            case 9:
                temp[i] = L'9';
                break;
            case 10:
                temp[i] = L'a';
                break;
            case 11:
                temp[i] = L'b';
                break;
            case 12:
                temp[i] = L'c';
                break;
            case 13:
                temp[i] = L'd';
                break;
            case 14:
                temp[i] = L'e';
                break;
            case 15:
                temp[i] = L'f';
                break;
        }

        input = input/16;
        
        flag = input == 0 ? false : true;

    }

    *str_size = i+3;
    
    res[0] = L'0';
    res[1] = L'x';
    
    for(int j=2;i>-1; i--, j++)
    {
        res[j] = temp[i];
    }

    return;
    
}

//
// Calculate input size in WCHAR.
//
unsigned int CalculateWStrSizeW(LPCWSTR input)
{
    WCHAR* iter =(WCHAR*) input;
    unsigned int res = 0;

    while(*iter != L'\0')
    {
        res++;
        iter++;
    }

    return res;
}

//
// Convert input string  WCHAR string. and return the count of chars converted.
//
void CovertStringToWString(LPCSTR input,char* res,size_t count)
{
    
    CHAR* iter = (CHAR*) input;

    ASSERT(res != NULL);
    
    for(size_t i=0;i<count;i++ )
    {
        res[2*i] = iter[i];
        res[2*i+1] = '\0';
    }
    
    return;
}

//
// Function: PrintToEventLog
//    Prints a message to the Event log, the message format should be as follow:
//        %2 device reports about .... . Therefore, the HCA .... (The issue is reported in Function %N).
//    Note: the AdapterName, functionName must "not" be NULL .
//

void
PrintToEventLog(
    PVOID LogHandle,
    NTSTATUS EventCode,
    LPCWSTR psAdapterName,
    LPCSTR psFunctionName,    
    ULONG DataSize,
    PVOID  Data,
    int Count,
    ...
)
{
    PIO_ERROR_LOG_PACKET l_pErrorLogEntry; 
    va_list l_Argptr;    
    size_t StrSize;
    size_t ParamSize = 0;
    USHORT l_nDataItem=0;
    
    ASSERT(LogHandle != NULL);
    ASSERT(psAdapterName != NULL);
    ASSERT(psFunctionName != NULL);

    size_t BufSize = ERROR_LOG_MAXIMUM_SIZE -  sizeof(IO_ERROR_LOG_PACKET) ; 
    PUCHAR strBuf = new UCHAR[BufSize];

    if(strBuf == NULL)
    {   
        GU_PRINT(TRACE_LEVEL_WARNING, GU ,"PrintToEventLog Error Allocating Memorey For StrBuf.\n");
        return;
    }

    memset(strBuf, 0 ,BufSize);
    
    PWCHAR pBuf = (PWCHAR) strBuf;
    
    BufSize = BufSize / sizeof(WCHAR);

    StrSize = CalculateWStrSizeW(psAdapterName);

    if(StrSize > BufSize-1)
    {
        goto cleanup;
    }

    memcpy(strBuf, psAdapterName , StrSize * sizeof(WCHAR));
    l_nDataItem ++;

    StrSize ++; // Add Nul terminator
    ParamSize += StrSize;
    pBuf += StrSize;
    BufSize -= (int) StrSize ;

    
    va_start(l_Argptr,Count);
    
    for(int i = 0 ; i < Count && BufSize > 0 ; i++)
    {
        unsigned int val = va_arg(l_Argptr,unsigned int);
        
        WCHAR wchar_val[25] = {0}; 
        ConvertIntToWString(val, wchar_val ,&StrSize);
        
        if(StrSize >= BufSize)
        {   
            StrSize = BufSize-1;
        }

        memcpy(pBuf, wchar_val, StrSize * sizeof(WCHAR));

        l_nDataItem++;
        
        StrSize++; // Add Nul  string 
        ParamSize += StrSize;
        pBuf += StrSize ;
        BufSize -= (int)StrSize;
    }

    va_end(l_Argptr);

    //
    // Add function name, the function name is in CHAR we convert to WCHAR, so we allocate 2 * StrSize
    //
    StrSize = strlen(psFunctionName);

    if(BufSize != 0)
    {
        if(StrSize >= BufSize)
        {
            StrSize = BufSize-1;
        }
    }
    else
    {
        StrSize = 0;
    }
    if(StrSize != 0)
    {
        
        char function_converted[255] = {0};
        
        CovertStringToWString(psFunctionName, function_converted ,StrSize );

        memcpy(pBuf, function_converted , StrSize * sizeof(WCHAR));

        l_nDataItem++;

        StrSize++; // Add Nul  string 
        ParamSize += StrSize;
        pBuf += StrSize;
        BufSize -= (int)StrSize;
    }

    //
    // Allocate Event log object
    //
    size_t AllocSize = sizeof(IO_ERROR_LOG_PACKET) + ParamSize*sizeof(WCHAR);

    size_t AvailableDataSize = (ERROR_LOG_MAXIMUM_SIZE - AllocSize);
    USHORT dumpDataCopied = (USHORT) align(DataSize, sizeof(ULONG));

    if (dumpDataCopied > AvailableDataSize)
    {
        dumpDataCopied = (USHORT)((AvailableDataSize / sizeof(ULONG)) * sizeof(ULONG));
        DataSize = dumpDataCopied;
    }

    AllocSize += dumpDataCopied;
    
    ASSERT(ERROR_LOG_MAXIMUM_SIZE >= AllocSize);

    l_pErrorLogEntry = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(LogHandle,  (UCHAR)AllocSize);
    // Check allocation 
    if ( l_pErrorLogEntry == NULL) 
    {
        GU_PRINT(TRACE_LEVEL_WARNING, GU ,"PrintToEventLog Error Allocating Memorey For l_pErrorLogEntry.\n");
        ASSERT(FALSE);
        goto cleanup;
    }
   
    memset(l_pErrorLogEntry, 0, AllocSize);
    
    // Set the error log entry header 
    l_pErrorLogEntry->ErrorCode = EventCode ; 
    l_pErrorLogEntry->DumpDataSize = dumpDataCopied; 
    l_pErrorLogEntry->SequenceNumber = 0; 
    l_pErrorLogEntry->MajorFunctionCode = 0; 
    l_pErrorLogEntry->IoControlCode = 0; 
    l_pErrorLogEntry->RetryCount = 0; 
    l_pErrorLogEntry->UniqueErrorValue = 0; 
    l_pErrorLogEntry->FinalStatus = 0; 
    l_pErrorLogEntry->NumberOfStrings = l_nDataItem; 
    l_pErrorLogEntry->StringOffset = sizeof(IO_ERROR_LOG_PACKET) + dumpDataCopied;

    PUCHAR pLogBuf = (UCHAR*)l_pErrorLogEntry + sizeof(IO_ERROR_LOG_PACKET);
    memcpy(pLogBuf, Data, DataSize); 

    pLogBuf = (UCHAR*)l_pErrorLogEntry + l_pErrorLogEntry->StringOffset;
    memcpy( pLogBuf, strBuf, ParamSize*sizeof(WCHAR));

    IoWriteErrorLogEntry(l_pErrorLogEntry);
    
    cleanup:
        delete [] strBuf;
        return;
}

