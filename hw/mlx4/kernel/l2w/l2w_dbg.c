/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    gu_dbg.cpp

Abstract:
    This modulde contains all related dubug code
Notes:

--*/

#include "l2w_precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "l2w_dbg.tmh"
#endif

#if DBG

#define TEMP_BUFFER_SIZE 128

#if 0
VOID cl_dbg_out( IN PCCH  format, ...)
{
	va_list  list;
	va_start(list, format);
	vDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, format, list);
	va_end(list);
}
#endif

#if !defined(EVENT_TRACING)
void
TraceL2WMessage(
    char*  func,
    char*  file,
    unsigned long   line,
    unsigned long   level,
    char*  format,
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
    long   status;
    
    char psPrefix[TEMP_BUFFER_SIZE];
    char*  fileName = strrchr(file, '\\');
	
    va_start(list, format);

    if (fileName)
    {
        fileName++;
    }
    else
    {
    	fileName = file;
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
    
    ASSERT(status >= 0);
    vDbgPrintExWithPrefix(psPrefix , DPFLTR_IHVNETWORK_ID, level, format, list);

    va_end(list);
    
#else

    UNREFERENCED_PARAMETER(TraceEventsLevel);
    UNREFERENCED_PARAMETER(TraceEventsFlag);
    UNREFERENCED_PARAMETER(DebugMessage);

#endif
}
#endif

#endif // DBG

