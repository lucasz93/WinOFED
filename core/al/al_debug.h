/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#if !defined(__AL_DEBUG_H__)
#define __AL_DEBUG_H__

#ifdef __MODULE__
#undef __MODULE__
#endif
#define __MODULE__	"[AL]"


#include <complib/cl_debug.h>
#include <complib/cl_perf.h>

extern uint32_t			g_al_dbg_level;
extern uint32_t			g_al_dbg_flags;

#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//

#ifndef CL_KERNEL

#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(ALCtlGuid1,(B199CE55,F8BF,4147,B119,DACD1E5987A6),  \
	WPP_DEFINE_BIT( AL_DBG_ERROR) \
	WPP_DEFINE_BIT( AL_DBG_PNP) \
	WPP_DEFINE_BIT( AL_DBG_HDL) \
	WPP_DEFINE_BIT( AL_DBG_AL_OBJ) \
	WPP_DEFINE_BIT( AL_DBG_SMI) \
	WPP_DEFINE_BIT( AL_DBG_SMI_CB) \
	WPP_DEFINE_BIT( AL_DBG_RES1) \
	WPP_DEFINE_BIT( AL_DBG_MAD_POOL) \
	WPP_DEFINE_BIT( AL_DBG_MAD_SVC) \
	WPP_DEFINE_BIT( AL_DBG_RES2) \
	WPP_DEFINE_BIT( AL_DBG_CM) \
	WPP_DEFINE_BIT( AL_DBG_CA) \
	WPP_DEFINE_BIT( AL_DBG_MR) \
	WPP_DEFINE_BIT( AL_DBG_MGR)\
	WPP_DEFINE_BIT( AL_DBG_DEV)\
	WPP_DEFINE_BIT( AL_DBG_MCAST)\
	WPP_DEFINE_BIT( AL_DBG_PD)\
	WPP_DEFINE_BIT( AL_DBG_AV)\
	WPP_DEFINE_BIT( AL_DBG_CQ)\
	WPP_DEFINE_BIT( AL_DBG_QP)\
	WPP_DEFINE_BIT( AL_DBG_SRQ)\
	WPP_DEFINE_BIT( AL_DBG_MW)\
	WPP_DEFINE_BIT( AL_DBG_NDI) \
	WPP_DEFINE_BIT( AL_DBG_PROXY_CB)\
	WPP_DEFINE_BIT( AL_DBG_UAL)\
	WPP_DEFINE_BIT( AL_DBG_QUERY)\
	WPP_DEFINE_BIT( AL_DBG_SA_REQ)\
	WPP_DEFINE_BIT( AL_DBG_IOC)\
	WPP_DEFINE_BIT( AL_DBG_SUB)\
	WPP_DEFINE_BIT( AL_DBG_MAD))

#else

#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(ALCtlGuid2,(99DC84E3,B106,431e,88A6,4DD20C9BBDE3),  \
	WPP_DEFINE_BIT( AL_DBG_ERROR) \
	WPP_DEFINE_BIT( AL_DBG_PNP) \
	WPP_DEFINE_BIT( AL_DBG_HDL) \
	WPP_DEFINE_BIT( AL_DBG_AL_OBJ) \
	WPP_DEFINE_BIT( AL_DBG_SMI) \
	WPP_DEFINE_BIT( AL_DBG_SMI_CB) \
	WPP_DEFINE_BIT( AL_DBG_FMR_POOL) \
	WPP_DEFINE_BIT( AL_DBG_MAD_POOL) \
	WPP_DEFINE_BIT( AL_DBG_MAD_SVC) \
	WPP_DEFINE_BIT( AL_DBG_RES2) \
	WPP_DEFINE_BIT( AL_DBG_CM) \
	WPP_DEFINE_BIT( AL_DBG_CA) \
	WPP_DEFINE_BIT( AL_DBG_MR) \
	WPP_DEFINE_BIT( AL_DBG_MGR)\
	WPP_DEFINE_BIT( AL_DBG_DEV)\
	WPP_DEFINE_BIT( AL_DBG_MCAST)\
	WPP_DEFINE_BIT( AL_DBG_PD)\
	WPP_DEFINE_BIT( AL_DBG_AV)\
	WPP_DEFINE_BIT( AL_DBG_CQ)\
	WPP_DEFINE_BIT( AL_DBG_QP)\
	WPP_DEFINE_BIT( AL_DBG_SRQ)\
	WPP_DEFINE_BIT( AL_DBG_MW)\
	WPP_DEFINE_BIT( AL_DBG_NDI) \
	WPP_DEFINE_BIT( AL_DBG_PROXY_CB)\
	WPP_DEFINE_BIT( AL_DBG_UAL)\
	WPP_DEFINE_BIT( AL_DBG_QUERY)\
	WPP_DEFINE_BIT( AL_DBG_SA_REQ)\
	WPP_DEFINE_BIT( AL_DBG_IOC)\
	WPP_DEFINE_BIT( AL_DBG_SUB)\
	WPP_DEFINE_BIT( AL_DBG_MAD))

#endif


#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// AL_ENTER( FLAG );
// AL_EXIT( FLAG );
// USEPREFIX(AL_PRINT, "%!STDPREFIX! [AL] :%!FUNC!():");
// USESUFFIX(AL_ENTER, " [AL] :%!FUNC!():[");
// USESUFFIX(AL_EXIT, " [AL] :%!FUNC!():]");
// end_wpp



#else

#include <wmistr.h>
#include <evntrace.h>

/*
 * Debug macros
 */


/* Debug message source */
#define AL_DBG_ERR	(1 << 0)
#define AL_DBG_PNP		(1 << 1)
#define AL_DBG_HDL		(1 << 2)
#define AL_DBG_AL_OBJ	(1 << 3)
#define AL_DBG_SMI		(1 << 4)
#define AL_DBG_SMI_CB	(1 << 5)
#define AL_DBG_FMR_POOL	(1 << 6)
#define AL_DBG_MAD_POOL	(1 << 7)
#define AL_DBG_MAD_SVC	(1 << 8)
#define AL_DBG_CM		(1 << 10)
#define AL_DBG_CA		(1 << 11)
#define AL_DBG_MR		(1 << 12)
#define AL_DBG_MGR		(1 << 13)
#define AL_DBG_DEV		(1 << 14)
#define AL_DBG_MCAST	(1 << 15)
#define AL_DBG_PD		(1 << 16)
#define AL_DBG_AV		(1 << 17)
#define AL_DBG_CQ		(1 << 18)
#define AL_DBG_QP		(1 << 19)
#define AL_DBG_SRQ		(1 << 20)
#define AL_DBG_MW		(1 << 21)
#define AL_DBG_NDI		(1 << 22)
#define AL_DBG_PROXY_CB	(1 << 23)
#define AL_DBG_UAL		(1 << 24)
#define AL_DBG_QUERY	(1 << 25)
#define AL_DBG_SA_REQ	(1 << 26)
#define AL_DBG_IOC		(1 << 27)
#define AL_DBG_SUB		(1 << 28)
#define AL_DBG_MAD		(1 << 29) //TODO 

#define AL_DBG_ERROR	(CL_DBG_ERROR | AL_DBG_ERR)

#if DBG

// assignment of _level_ is need to to overcome warning C4127

//
// Code in DBG, has no impact on fre performance
// Hence the 6326 warning suppression
//

#define AL_PRINT( _level_,_flag_,_msg_)  \
	{ \
        __pragma(warning(suppress:6326)) \
		if( g_al_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_al_dbg_flags, _msg_ ); \
	}


#define AL_PRINT_EXIT( _level_,_flag_,_msg_) \
	{ \
        __pragma(warning(suppress:6326)) \
		if( g_al_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_al_dbg_flags, _msg_ );\
		AL_EXIT( _flag_ );\
	}

#define AL_ENTER( _flag_) \
	{ \
    __pragma(warning(suppress:6326)) \
		if( g_al_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_ENTER( _flag_, g_al_dbg_flags ); \
	}

#define AL_EXIT( _flag_)\
	{ \
    __pragma(warning(suppress:6326)) \
		if( g_al_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_EXIT( _flag_, g_al_dbg_flags ); \
	}


#else

#define AL_PRINT( lvl, flags, msg)

#define AL_PRINT_EXIT( _level_,_flag_,_msg_)

#define AL_ENTER( _flag_)

#define AL_EXIT( _flag_)


#endif

#endif //EVENT_TRACING



enum al_perf_counters
{
	IbPostSend,
		PostSend,
			VpPostSend,
			UalDoPostSend,

	IbPollCq,
		VerbsPollCq,
			VpPollCq,
			UalDoPollCq,


	AlMaxPerf

};

extern cl_perf_t	g_perf;


#endif /* __AL_DEBUG_H__ */
