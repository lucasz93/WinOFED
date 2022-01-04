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

 * $Id: fip_debug.h 1936 2007-02-06 16:04:33Z sleybo $
 */


#ifndef  _FIP_DEBUG_H_
#define _FIP_DEBUG_H_

#include <ntddk.h>
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

extern unsigned int		g_fip_dbg_level;
extern unsigned int		g_fip_dbg_flags;
#define MAX_LOG_BUF_LEN		512
extern WCHAR g_wlog_buf[ MAX_LOG_BUF_LEN ]; 
extern UCHAR g_slog_buf[ MAX_LOG_BUF_LEN ];  

#pragma warning(disable:4505) //unreferenced local function
static void _build_str( const char *	format, ... )
{
	NTSTATUS status;
	va_list p_arg;
	va_start(p_arg, format);
//	vsprintf((char *)g_slog_buf, format , p_arg);
//	swprintf(g_wlog_buf, L"%S", g_slog_buf);
	status = RtlStringCbVPrintfA((char *)g_slog_buf, sizeof(g_slog_buf), format , p_arg);
	if (status)
		goto end;
	status = RtlStringCchPrintfW(g_wlog_buf, sizeof(g_wlog_buf)/sizeof(g_wlog_buf[0]), L"%S", g_slog_buf);
	if (status)
		goto end;
//	vsnprintf_s((char *)g_slog_buf, sizeof(g_slog_buf), _TRUNCATE, format , p_arg);
//	swprintf_s(g_wlog_buf, sizeof(g_wlog_buf), L"%S", g_slog_buf);
end:
	va_end(p_arg);
}

#define FIP_PRINT_TO_EVENT_LOG(_obj_,_level_,_flag_,_msg_)  \
	{ \
		NTSTATUS event_id; \
		int __lvl = _level_; \
		switch (__lvl) { \
			case TRACE_LEVEL_FATAL: case TRACE_LEVEL_ERROR: event_id = EVENT_FIP_ANY_ERROR; break; \
			case TRACE_LEVEL_WARNING: event_id = EVENT_FIP_ANY_WARN; break; \
			default: event_id = EVENT_FIP_ANY_INFO; break; \
		} \
		_build_str _msg_; \
		WriteEventLogEntryStr( _obj_, (ULONG)event_id, 0, 0, g_wlog_buf, 0, 0 ); \
	}

#define FIP_PRINT_EV_MDEV(_level_,_flag_,_msg_)  \
	FIP_PRINT_TO_EVENT_LOG(mdev->pdev->p_self_do,_level_,_flag_,_msg_)


#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//

#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(Mlx4BusCtlGuid,(E51BB6E2,914A,4e21,93C0,192F4801BBFF),  \
	WPP_DEFINE_BIT( FIP_DBG_DRV) \
	WPP_DEFINE_BIT( FIP_DBG_PNP) \
	WPP_DEFINE_BIT( FIP_DBG_INIT)\
	WPP_DEFINE_BIT( FIP_SEND_RECV)\
	WPP_DEFINE_BIT( FIP_PORT)\
	WPP_DEFINE_BIT( FIP_GW)\
	WPP_DEFINE_BIT( FIP_VNIC)\
	WPP_DEFINE_BIT( FIP_VHUB_TABLE))





#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// FIP_ENTER(FLAG);
// FIP_EXIT(FLAG);
// USESUFFIX(FIP_PRINT, "%!STDPREFIX! %!FUNC!");
// FIP_PRINT(LEVEL,FLAGS,MSG,...)
// FIP_PRINT_EXIT(LEVEL,FLAGS,MSG,...)
// USESUFFIX(FIP_ENTER, "====>>> %!FUNC! ");
// USESUFFIX(FIP_EXIT, "<<<====== %!FUNC!]");
// end_wpp





#else


/*
 * Debug macros
 */


#define FIP_DBG_DRV     (1 << 0)
#define FIP_DBG_PNP     (1 << 1)
#define FIP_DBG_INIT    (1 << 2)
#define FIP_SEND_RECV   (1 << 3)
#define FIP_PORT        (1 << 4)
#define FIP_GW          (1 << 5)
#define FIP_VNIC        (1 << 6)
#define FIP_VHUB_TABLE  (1 << 7)


#define TRACE_LEVEL_CRITICAL    DPFLTR_ERROR_LEVEL        
#define TRACE_LEVEL_FATAL       DPFLTR_ERROR_LEVEL        
#define TRACE_LEVEL_ERROR       DPFLTR_ERROR_LEVEL       
#define TRACE_LEVEL_WARNING     DPFLTR_WARNING_LEVEL      
#define TRACE_LEVEL_INFORMATION DPFLTR_TRACE_LEVEL   
#define TRACE_LEVEL_VERBOSE     DPFLTR_INFO_LEVEL   


VOID
TraceMessage(
    IN PCCHAR  func,
    IN PCCHAR  file,
    IN ULONG   line,
    IN ULONG   level,
    IN PCCHAR  format,
    ...
    );

#pragma warning(disable:4296)  // expression is always true/false          


#if DBG


#define FIP_PRINT(_level_,_flag_, _format_, ...)                               \
    if (g_fip_dbg_level >= (uint32_t)(_level_) && \
        (g_fip_dbg_flags & (_flag_))) { \
        TraceMessage(__FUNCTION__, __FILE__, __LINE__, _level_, _format_, __VA_ARGS__);  \
    }

#else

#define FIP_PRINT(lvl ,flags, msg) 

#endif

#define FIP_PRINT_EV(_level_,_flag_,_msg_)  \
    { \
	    MLX4_PRINT(_level_,_flag_,_msg_) \
	    MLX4_PRINT_EV_MDEV(_level_,_flag_,_msg_) \
	}

#define FIP_ENTER(flags)\
	FIP_PRINT(TRACE_LEVEL_VERBOSE, flags,("[\n"));

#define FIP_EXIT(flags)\
	FIP_PRINT(TRACE_LEVEL_VERBOSE, flags, ("]\n" ));


#endif //EVENT_TRACING


#endif	/*_FIP_DEBUG_H_ */




