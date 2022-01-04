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


#ifndef  _HCA_DEBUG_H_
#define _HCA_DEBUG_H_

#include <ntstrsafe.h>

extern uint32_t		g_mthca_dbg_level;
extern uint32_t		g_mthca_dbg_flags;
#define MAX_LOG_BUF_LEN		512
extern WCHAR g_wlog_buf[ MAX_LOG_BUF_LEN ]; 
extern UCHAR g_slog_buf[ MAX_LOG_BUF_LEN ];  

static void _build_str( const char *	format, ... )
{
	NTSTATUS status;
	va_list p_arg;
	va_start(p_arg, format);
	status = RtlStringCbVPrintfA((char *)g_slog_buf, sizeof(g_slog_buf), format , p_arg);
	if (status)
		goto end;

	status = RtlStringCchPrintfW(g_wlog_buf, sizeof(g_wlog_buf)/sizeof(g_wlog_buf[0]), L"%S", g_slog_buf);
	if (status)
		goto end;

end:
	va_end(p_arg);
}

#define HCA_PRINT_TO_EVENT_LOG(_obj_,_level_,_flag_,_msg_)  \
	{ \
		NTSTATUS event_id; \
		__pragma(warning(suppress:6326)) \
		switch (_level_) { \
			case TRACE_LEVEL_FATAL: case TRACE_LEVEL_ERROR: event_id = EVENT_MTHCA_ANY_ERROR; break; \
			case TRACE_LEVEL_WARNING: event_id = EVENT_MTHCA_ANY_WARN; break; \
			default: event_id = EVENT_MTHCA_ANY_INFO; break; \
		} \
		_build_str _msg_; \
		WriteEventLogEntryStr( _obj_, (ULONG)event_id, 0, 0, g_wlog_buf, 0, 0 ); \
	}

#define HCA_PRINT_EV_MDEV(_level_,_flag_,_msg_)  \
{\
	if(mdev) {\
		HCA_PRINT_TO_EVENT_LOG(mdev->ext->cl_ext.p_self_do,_level_,_flag_,_msg_)\
	}\
}\


#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//

#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(HCACtlGuid,(8BF1F640,63FE,4743,B9EF,FA38C695BFDE),  \
	WPP_DEFINE_BIT( HCA_DBG_DEV) \
	WPP_DEFINE_BIT( HCA_DBG_PNP) \
	WPP_DEFINE_BIT( HCA_DBG_INIT) \
	WPP_DEFINE_BIT( HCA_DBG_MAD) \
	WPP_DEFINE_BIT( HCA_DBG_PO) \
	WPP_DEFINE_BIT( HCA_DBG_PD)\
	WPP_DEFINE_BIT( HCA_DBG_CQ) \
	WPP_DEFINE_BIT( HCA_DBG_QP) \
	WPP_DEFINE_BIT( HCA_DBG_MEMORY) \
	WPP_DEFINE_BIT( HCA_DBG_AV) \
	WPP_DEFINE_BIT( HCA_DBG_SRQ) \
	WPP_DEFINE_BIT( HCA_DBG_MCAST) \
	WPP_DEFINE_BIT( HCA_DBG_LOW) \
	WPP_DEFINE_BIT( HCA_DBG_SHIM))


#define WPP_GLOBALLOGGER


#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// HCA_ENTER(FLAG);
// HCA_EXIT(FLAG);
// USEPREFIX(HCA_PRINT, "%!STDPREFIX! [MTHCA] :%!FUNC!() :");
// USESUFFIX(HCA_ENTER, " [MTHCA] :%!FUNC!()[");
// USESUFFIX(HCA_EXIT, " [MTHCA] :%!FUNC!()]");
// end_wpp



#define HCA_PRINT_EV(_level_,_flag_,_msg_)  \
    { \
	    HCA_PRINT_EV_MDEV(_level_,_flag_,_msg_) \
	}


#else


#include <evntrace.h>

/*
 * Debug macros
 */


#define HCA_DBG_DEV	(1 << 0)
#define HCA_DBG_PNP	(1<<1)
#define HCA_DBG_INIT	(1 << 2)
#define HCA_DBG_MAD	(1 << 3)
#define HCA_DBG_PO		(1 << 4)
#define HCA_DBG_PD		(1<<5)
#define HCA_DBG_QP		(1 << 6)
#define HCA_DBG_CQ		(1 << 7)
#define HCA_DBG_MEMORY	(1 << 8)
#define HCA_DBG_AV		(1<<9)
#define HCA_DBG_SRQ	(1 << 10)
#define HCA_DBG_MCAST	(1<<11)
#define HCA_DBG_LOW	(1 << 12)
#define HCA_DBG_SHIM	(1 << 13)


#if DBG

// assignment of _level_ is need to to overcome warning C4127
#define HCA_PRINT(_level_,_flag_,_msg_)  \
	{ \
		int __lvl = _level_; \
		if (g_mthca_dbg_level >= (_level_) && \
			(g_mthca_dbg_flags & (_flag_))) { \
				cl_dbg_out ("~%d:[MTHCA] %s() :", KeGetCurrentProcessorNumber(), __FUNCTION__); \
				if(__lvl == TRACE_LEVEL_ERROR) cl_dbg_out ("***ERROR***  "); \
				cl_dbg_out _msg_; \
		} \
	}

#else

#define HCA_PRINT(lvl ,flags, msg) 

#endif

#define HCA_PRINT_EV(_level_,_flag_,_msg_)  \
    { \
	    HCA_PRINT(_level_,_flag_,_msg_) \
	    HCA_PRINT_EV_MDEV(_level_,_flag_,_msg_) \
	}

#define HCA_ENTER(flags)\
	HCA_PRINT(TRACE_LEVEL_VERBOSE, flags,("[\n"));

#define HCA_EXIT(flags)\
	HCA_PRINT(TRACE_LEVEL_VERBOSE, flags, ("]\n" ));


#endif //EVENT_TRACING


#endif	/*_HCA_DEBUG_H_ */


