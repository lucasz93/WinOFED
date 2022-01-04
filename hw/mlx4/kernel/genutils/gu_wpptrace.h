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
        WPP_DEFINE_BIT(GU)                    \
        WPP_DEFINE_BIT(GU_INIT))               \
        
#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)

// begin_wpp config
// GU_ENTER();
// GU_EXIT();
// USESUFFIX(GU_PRINT, "%!STDPREFIX! %!FUNC!");
// GU_PRINT(LEVEL,FLAGS,MSG,...)
// USESUFFIX(GU_ENTER, "====>>> %!FUNC! ");
// USESUFFIX(GU_EXIT, "<<<====== %!FUNC!]");
// end_wpp


#else //defined(EVENT_TRACING)

// Debug toppics
#define GU            0x000001
#define GU_INIT       0x000020
// Each change to this flags requires additional change at Mp_dbg.cpp  g_DbgFlags[] variabl


#define TRACE_LEVEL_CRITICAL    DPFLTR_ERROR_LEVEL        
#define TRACE_LEVEL_FATAL       DPFLTR_ERROR_LEVEL        
#define TRACE_LEVEL_ERROR       DPFLTR_ERROR_LEVEL       
#define TRACE_LEVEL_WARNING     DPFLTR_WARNING_LEVEL      
#define TRACE_LEVEL_INFORMATION DPFLTR_TRACE_LEVEL   
#define TRACE_LEVEL_VERBOSE     DPFLTR_INFO_LEVEL   
   
VOID
TraceGUMessage(
    IN PCCHAR  func,
    IN PCCHAR  file,
    IN ULONG   line,
    IN ULONG   level,
    IN PCCHAR  format,
    ...
    );

#pragma warning(disable:4296)  // expression is always true/false     
#define GU_PRINT(_level_,_flag_, _format_, ...)                               \
    if ((g_GUDbgFlags & (_flag_)) &&                                          \
        (g_GUDbgFlagsDef[ROUNDUP_LOG2(_flag_)].dbgLevel  >= (_level_)))               \
    {                                                                          \
        TraceGUMessage(__FUNCTION__, __FILE__, __LINE__, _level_, _format_, __VA_ARGS__);  \
    }
    
#define GU_ENTER()\
	GU_PRINT(TRACE_LEVEL_VERBOSE, GU, "===>\n");

#define GU_EXIT()\
	GU_PRINT(TRACE_LEVEL_VERBOSE, GU, "<===\n");

#endif //defined(EVENT_TRACING)


