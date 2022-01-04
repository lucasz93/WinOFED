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



#ifndef _SRP_DEBUG_H_
#define _SRP_DEBUG_H_


#include <complib/cl_debug.h>


extern uint32_t		g_srp_dbg_level;
extern uint32_t		g_srp_dbg_flags;
extern uint32_t		g_srp_mode_flags;

// mode flags 
#define SRP_MODE_NO_FMR_POOL		(1 << 0)		/* don't use FMR_POOL - for tuning purposes */
#define SRP_MODE_SG_UNLIMITED		(1 << 1)		/* don't obey the limitation, stated in DDK, not to enlarge StorPort max SG */

#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//


#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(SRPCtlGuid,(5AF07B3C,D119,4233,9C81,C07EF481CBE6),  \
	WPP_DEFINE_BIT( SRP_DBG_ERROR) \
	WPP_DEFINE_BIT( SRP_DBG_PNP) \
	WPP_DEFINE_BIT( SRP_DBG_DATA) \
	WPP_DEFINE_BIT( SRP_DBG_SESSION) \
	WPP_DEFINE_BIT( SRP_DBG_DEBUG))



#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// SRP_ENTER(FLAG);
// SRP_EXIT(FLAG);
// USEPREFIX(SRP_PRINT, "%!STDPREFIX! [SRP] :%!FUNC!() :");
// USEPREFIX(SRP_PRINT_EXIT, "%!STDPREFIX! [SRP] :%!FUNC!() :");
// USESUFFIX(SRP_ENTER, " [SRP] :%!FUNC!():[");
// USESUFFIX(SRP_EXIT, " [SRP] :%!FUNC!():]");
// end_wpp


#else


#include <evntrace.h>

/*
 * Debug macros
 */


#define SRP_DBG_ERR			(1 << 0)
#define SRP_DBG_PNP			(1 << 1)
#define SRP_DBG_DATA		(1 << 2)
#define SRP_DBG_SESSION		(1 << 3)
#define SRP_DBG_DEBUG		(1 << 4)

#define SRP_DBG_ERROR	(CL_DBG_ERROR | SRP_DBG_ERR)
#define SRP_DBG_ALL	CL_DBG_ALL

#if DBG

// assignment of _level_ is need to to overcome warning C4127
#define SRP_PRINT(_level_,_flag_,_msg_) \
	{ \
		if( g_srp_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_srp_dbg_flags, _msg_ ); \
	}

#define SRP_PRINT_EXIT(_level_,_flag_,_msg_) \
	{ \
		if( g_srp_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_srp_dbg_flags, _msg_ );\
		SRP_EXIT(_flag_);\
	}

#define SRP_ENTER(_flag_) \
	{ \
		if( g_srp_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_ENTER( _flag_, g_srp_dbg_flags ); \
	}

#define SRP_EXIT(_flag_)\
	{ \
		if( g_srp_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_EXIT( _flag_, g_srp_dbg_flags ); \
	}


#else

#define SRP_PRINT(lvl, flags, msg)

#define SRP_PRINT_EXIT(_level_,_flag_,_msg_)

#define SRP_ENTER(_flag_)

#define SRP_EXIT(_flag_)


#endif


#endif //EVENT_TRACING

extern char         g_srb_function_name[][32];
extern char         g_srb_status_name[][32];
extern char         g_srb_scsi_status_name[][32];

#endif  /* _SRP_DEBUG_H_ */
