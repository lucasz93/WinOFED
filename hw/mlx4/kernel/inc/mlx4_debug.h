/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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


#ifndef  _MLX4_DEBUG_H_
#define _MLX4_DEBUG_H_

#include <ntddk.h>
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>


#include "ev_log.h"

extern unsigned int		g_mlx4_dbg_level;
extern unsigned int		g_mlx4_dbg_flags;
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

#define MLX4_PRINT_TO_EVENT_LOG(_obj_,_level_,_flag_,_msg_)  \
	{ \
		NTSTATUS event_id; \
		int __lvl = _level_; \
		switch (__lvl) { \
			case TRACE_LEVEL_FATAL: case TRACE_LEVEL_ERROR: event_id = EVENT_MLX4_ANY_ERROR; break; \
			case TRACE_LEVEL_WARNING: event_id = EVENT_MLX4_ANY_WARN; break; \
			default: event_id = EVENT_MLX4_ANY_INFO; break; \
		} \
		_build_str _msg_; \
		WriteEventLogEntryStr( _obj_, (ULONG)event_id, 0, 0, g_wlog_buf, 0, 0 ); \
	}

#define MLX4_PRINT_EV_MDEV(_level_,_flag_,_msg_)  \
	MLX4_PRINT_TO_EVENT_LOG(mdev->pdev->p_self_do,_level_,_flag_,_msg_)


#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//

//TODO: for mlx4_hca.lib use the same guid as in mlx4_debug.h 
#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(Mlx4BusCtlGuid,(E51BB6E2,914A,4e21,93C0,192F4801BBFF),  \
	WPP_DEFINE_BIT( MLX4_DBG_DEV) \
	WPP_DEFINE_BIT( MLX4_DBG_PNP) \
	WPP_DEFINE_BIT( MLX4_DBG_INIT) \
	WPP_DEFINE_BIT( MLX4_DBG_MAD) \
	WPP_DEFINE_BIT( MLX4_DBG_PO) \
	WPP_DEFINE_BIT( MLX4_DBG_PD) \
	WPP_DEFINE_BIT( MLX4_DBG_CQ) \
	WPP_DEFINE_BIT( MLX4_DBG_QP) \
	WPP_DEFINE_BIT( MLX4_DBG_MEMORY) \
	WPP_DEFINE_BIT( MLX4_DBG_AV) \
	WPP_DEFINE_BIT( MLX4_DBG_SRQ) \
	WPP_DEFINE_BIT( MLX4_DBG_MCAST) \
	WPP_DEFINE_BIT( MLX4_DBG_LOW) \
	WPP_DEFINE_BIT( MLX4_DBG_SHIM) \
	WPP_DEFINE_BIT( MLX4_DBG_DRV) \
	WPP_DEFINE_BIT( MLX4_DBG_COMM) \
	WPP_DEFINE_BIT( MLX4_DBG_MSIX) \
    WPP_DEFINE_BIT(BUS_SS)     \
    WPP_DEFINE_BIT(BUS_PNP)     \
    WPP_DEFINE_BIT(BUS_IOCTL)     \
    WPP_DEFINE_BIT(BUS_POWER)     \
    WPP_DEFINE_BIT(BUS_WMI))


//#define WPP_GLOBALLOGGER

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// MLX4_ENTER(FLAG);
// MLX4_EXIT(FLAG);
// USEPREFIX(MLX4_PRINT, "%!STDPREFIX! [MLX4_BUS] :%!FUNC!() :");
// USESUFFIX(MLX4_ENTER, " [MLX4_BUS] :%!FUNC!()[");
// USESUFFIX(MLX4_EXIT, " [MLX4_BUS] :%!FUNC!()]");
// end_wpp



#define MLX4_PRINT_EV(_level_,_flag_,_msg_)  \
    { \
	    MLX4_PRINT_EV_MDEV(_level_,_flag_,_msg_) \
	}


#else


#include <evntrace.h>

/*
 * Debug macros
 */


#define MLX4_DBG_DEV	(1 << 0)
#define MLX4_DBG_PNP	(1<<1)
#define MLX4_DBG_INIT	(1 << 2)
#define MLX4_DBG_MAD	(1 << 3)
#define MLX4_DBG_PO		(1 << 4)
#define MLX4_DBG_PD		(1<<5)
#define MLX4_DBG_QP		(1 << 6)
#define MLX4_DBG_CQ		(1 << 7)
#define MLX4_DBG_MEMORY	(1 << 8)
#define MLX4_DBG_AV		(1<<9)
#define MLX4_DBG_SRQ	(1 << 10)
#define MLX4_DBG_MCAST	(1<<11)
#define MLX4_DBG_LOW	(1 << 12)
#define MLX4_DBG_SHIM	(1 << 13)
#define MLX4_DBG_DRV	(1 << 14)
#define MLX4_DBG_COMM	(1 << 15)
#define MLX4_DBG_MSIX	(1 << 16)


#if DBG

// assignment of _level_ is need to to overcome warning C4127
#define MLX4_PRINT(_level_,_flag_,_msg_)  \
	{ \
		uint32_t __lvl = _level_; \
		if (g_mlx4_dbg_level >= (uint32_t)(__lvl) && \
			(g_mlx4_dbg_flags & (_flag_))) { \
				cl_dbg_out ("~%d:[MLX4_BUS] %s() :", KeGetCurrentProcessorNumber(), __FUNCTION__); \
			if(__lvl == TRACE_LEVEL_ERROR) cl_dbg_out ("***ERROR***  "); \
				if(__lvl == TRACE_LEVEL_WARNING) cl_dbg_out ("***WARN****  "); \
				if(__lvl == TRACE_LEVEL_INFORMATION) cl_dbg_out ("***INFO****  "); \
				if(__lvl == TRACE_LEVEL_VERBOSE) cl_dbg_out ("**VERBOSE**  "); \
				cl_dbg_out _msg_; \
		} \
	}

#else

#define MLX4_PRINT(lvl ,flags, msg) 

#endif

#define MLX4_PRINT_EV(_level_,_flag_,_msg_)  \
    { \
	    MLX4_PRINT(_level_,_flag_,_msg_) \
	    MLX4_PRINT_EV_MDEV(_level_,_flag_,_msg_) \
	}

#define MLX4_ENTER(flags)\
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, flags,("[\n"));

#define MLX4_EXIT(flags)\
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, flags, ("]\n" ));

#endif //EVENT_TRACING


void  debug_busy_wait(struct mlx4_dev *dev);


#endif	/*_MLX4_DEBUG_H_ */


