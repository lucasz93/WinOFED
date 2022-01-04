/*++

Copyright (c) 2005-2010 Mellanox Technologies. All rights reserved.

Module Name:
    gu_dbg.h

Abstract:
    This modulde contains all related dubug code
Notes:

--*/
#pragma once

#ifdef _PREFAST_
#define CONDITION_ASSUMED(X) __analysis_assume((X))
#else
#define CONDITION_ASSUMED(X) 
#endif // _PREFAST_

#if DBG

#define TEMP_BUFFER_SIZE 128


struct CGUDebugFlags{
    LPCWSTR pszName;
    DWORD dbgLevel;
    };

extern LONG g_dev_assert_enabled;
extern CGUDebugFlags g_GUDbgFlagsDef[];
const unsigned int g_GUDbgFlags= 0xffff;

#define DEV_ASSERT(x) \
    if(g_dev_assert_enabled) { \
        ASSERT(x); \
    } \

#undef ASSERT
#define ASSERT(x) if(!(x)) { \
    DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, "Assertion failed: %s:%d %s\n", __FILE__, __LINE__, #x);\
    DbgBreakPoint(); }\
    CONDITION_ASSUMED(x);

#define ASSERT_ALWAYS(x) ASSERT(x)

VOID dbg_out(PCCH  format, ...);
void DevAssertInit(LPCWSTR pszRegistryPath);

void DebugGUPrintInit(
    LPCWSTR pszRegistryPath, 
    CGUDebugFlags* pDbgFlags, 
    DWORD size
    );

VOID
Dump(
    __in_bcount(cb) CHAR* p,
    ULONG cb,
    BOOLEAN fAddress,
    ULONG ulGroup );

VOID
DumpLine(
    __in_bcount(cb) CHAR* p,
    ULONG cb,
    BOOLEAN  fAddress,
    ULONG ulGroup );

#else   // !DBG

#undef ASSERT
#define ASSERT(x)
#define DEV_ASSERT(x)

#define ASSERT_ALWAYS(x) if(!(x)) { \
    DbgBreakPoint(); }

#define Dump(p,cb,fAddress,ulGroup)
#define DumpLine(p,cb,fAddress,ulGroup)
#define DevAssertInit(pszRegistryPath)

#endif  // DBG

VOID print(PCCH  Format, ...);

inline unsigned long align(unsigned long val, unsigned long align)
{
    return (val + align - 1) & ~(align - 1);
}

/*
	Function: PrintToEventLog
*/

void
PrintToEventLog(
    PVOID LogHandle,
    NTSTATUS EventCode,
    LPCWSTR psAdapterName,
    LPCSTR psFunctionName,    
    IN ULONG  DataSize,
    PVOID  Data = NULL,
    int Count = 0,
    ...
);

