/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    gu_wpptrace.h

Abstract:
    This module contains all debug-related code.

Revision History:

Notes:

--*/

#pragma once


#if defined(EVENT_TRACING)

#define WPP_CONTROL_GUIDS \
        WPP_DEFINE_CONTROL_GUID(EthrnetGuid,(684E068C, 3FDC, 4bce, 89C3, CDB77A8B75A4),  \
        WPP_DEFINE_BIT(L2W)                    \
        WPP_DEFINE_BIT(L2W_INIT))               \
        
#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)

// begin_wpp config
// L2W_ENTER();
// L2W_EXIT();
// USESUFFIX(L2W_PRINT, "%!STDPREFIX! %!FUNC!");
// L2W_PRINT(LEVEL,FLAGS,MSG,...)
// USESUFFIX(L2W_ENTER, "====>>> %!FUNC! ");
// USESUFFIX(L2W_EXIT, "<<<====== %!FUNC!]");
// end_wpp


#else //defined(EVENT_TRACING)

// Debug toppics
#define L2W           		0x000001
#define L2W_INIT     	 	0x000020

#define TRACE_LEVEL_CRITICAL    DPFLTR_ERROR_LEVEL        
#define TRACE_LEVEL_FATAL       DPFLTR_ERROR_LEVEL        
#define TRACE_LEVEL_ERROR       DPFLTR_ERROR_LEVEL       
#define TRACE_LEVEL_WARNING     DPFLTR_WARNING_LEVEL      
#define TRACE_LEVEL_INFORMATION DPFLTR_TRACE_LEVEL   
#define TRACE_LEVEL_VERBOSE     DPFLTR_INFO_LEVEL   

#define DBG_LEVEL_THRESH 	TRACE_LEVEL_ERROR
#define DBG_FLAGS			0xffff


void
TraceL2WMessage(
    __in char*  func,
    __in char*  file,
    __in unsigned long   line,
    __in unsigned long   level,
    __in char*  format,
    ...
    );

#pragma warning(disable:4296)  // expression is always true/false   
#pragma warning(disable:4127) //conditional expression is constant
#define L2W_PRINT(_level_,_flag_, _format_, ...)                         \
    if ((DBG_FLAGS & (_flag_)) && (DBG_LEVEL_THRESH >= (_level_)))       \
    {                                                                    \
        TraceL2WMessage(__FUNCTION__, __FILE__, __LINE__, _level_, _format_, __VA_ARGS__);  \
    }
    
#define L2W_ENTER()\
	L2W_PRINT(TRACE_LEVEL_VERBOSE, L2W, "===>\n");

#define L2W_EXIT()\
	L2W_PRINT(TRACE_LEVEL_VERBOSE, L2W, "<===\n");

#endif //defined(EVENT_TRACING)


