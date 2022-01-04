/*
 * 
 * Copyright (c) 2011-2012 Mellanox Technologies. All rights reserved.
 * 
 * This software is licensed under the OpenFabrics BSD license below:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *  conditions are met:
 *
 *       - Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *       - Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*

Module Name:
    mp_dbg.c

Abstract:
    This module contains all debug-related code.

Revision History:

Notes:

--*/

#include "precomp.h"
#include <stdarg.h>


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "fip_dbg.tmh"
#endif



/**
Constants
**/


#define TEMP_BUFFER_SIZE 128



unsigned int		g_fip_dbg_level = TRACE_LEVEL_ERROR;
unsigned int		g_fip_dbg_flags = 0xffffffff;




#if !defined(EVENT_TRACING)

VOID
TraceMessage(
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
        status = RtlStringCchPrintfA(psPrefix, TEMP_BUFFER_SIZE, "***ERROR***  FIP: %s (%s:%d) ", func, fileName, line);
    }
    else
    {
        status = RtlStringCchPrintfA(psPrefix, TEMP_BUFFER_SIZE, "FIP: %s (%s:%d) ", func, fileName, line);
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






