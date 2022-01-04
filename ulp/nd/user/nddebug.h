/*
 * Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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

#pragma once

#ifdef __MODULE__
#undef __MODULE__
#endif
#define __MODULE__	"[ND]"


#include <complib/cl_debug.h>

extern uint32_t			g_nd_dbg_level;
extern uint32_t			g_nd_dbg_flags;

#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//


#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(NDCtlGuid1,(1463B4CE,7A66,47a4,ABDB,09EE7AD9E698),  \
	WPP_DEFINE_BIT( ND_DBG_ERROR)\
	WPP_DEFINE_BIT( ND_DBG_NDI))
	


#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// ND_ENTER( FLAG );
// ND_EXIT( FLAG );
// USEPREFIX(ND_PRINT, "%!STDPREFIX! [ND] :%!FUNC!() :");
// USESUFFIX(ND_ENTER, " [ND] :%!FUNC!():[");
// USESUFFIX(ND_EXIT, " [ND] :%!FUNC!():]");
// end_wpp



#else

#include <wmistr.h>
#include <evntrace.h>

/*
 * Debug macros
 */


/* Debug message source */
#define ND_DBG_ERR	(1 << 0)
#define ND_DBG_NDI	(1 << 1)

#define ND_DBG_ERROR	(CL_DBG_ERROR | ND_DBG_ERR)

#if DBG

// assignment of _level_ is need to to overcome warning C4127
#define ND_PRINT( _level_,_flag_,_msg_)  \
	{ \
		if( g_nd_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_nd_dbg_flags, _msg_ ); \
	}


#define ND_PRINT_EXIT( _level_,_flag_,_msg_) \
	{ \
		if( g_nd_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_nd_dbg_flags, _msg_ );\
		ND_EXIT( _flag_ );\
	}

#define ND_ENTER( _flag_) \
	{ \
		if( g_nd_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_ENTER( _flag_, g_nd_dbg_flags ); \
	}

#define ND_EXIT( _flag_)\
	{ \
		if( g_nd_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_EXIT( _flag_, g_nd_dbg_flags ); \
	}


#else

#define ND_PRINT( lvl, flags, msg)

#define ND_PRINT_EXIT( _level_,_flag_,_msg_)

#define ND_ENTER( _flag_)

#define ND_EXIT( _flag_)


#endif

#endif //EVENT_TRACING


#if DBG
struct dbg_data
{	
	int64_t rcv_cnt;
	int64_t rcv_pkts;
	int64_t rcv_bytes;
	int64_t rcv_pkts_err;
	int64_t rcv_pkts_zero;
	int64_t snd_cnt;
	int64_t snd_pkts;
	int64_t snd_bytes;
	int64_t snd_pkts_err;
	int64_t snd_pkts_zero;
	int64_t c_cnt;
	int64_t c_rcv_pkts;
	int64_t c_rcv_bytes;
	int64_t c_rcv_pkts_err;
	int64_t c_snd_pkts;
	int64_t c_snd_bytes;
	int64_t c_snd_pkts_err;
};

extern dbg_data g;

#endif
