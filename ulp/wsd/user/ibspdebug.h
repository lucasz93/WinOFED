/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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

#ifndef _IBSP_DEBUG_H_
#define _IBSP_DEBUG_H_


#include "ibspdll.h"
#include <complib/cl_atomic.h>
#include <complib/cl_debug.h>

#ifndef __MODULE__
#define __MODULE__	"[IBSP]"
#endif




extern uint32_t			g_ibsp_dbg_level;
extern uint32_t			g_ibsp_dbg_flags;

#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//


#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(IBSPCtlGuid,(156A98A5,8FDC,4d00,A673,0638123DF336), \
	WPP_DEFINE_BIT( IBSP_DBG_ERROR) \
	WPP_DEFINE_BIT( IBSP_DBG_DLL) \
	WPP_DEFINE_BIT( IBSP_DBG_SI) \
	WPP_DEFINE_BIT( IBSP_DBG_INIT) \
	WPP_DEFINE_BIT( IBSP_DBG_WQ) \
	WPP_DEFINE_BIT( IBSP_DBG_EP) \
	WPP_DEFINE_BIT( IBSP_DBG_MEM) \
	WPP_DEFINE_BIT( IBSP_DBG_CM) \
	WPP_DEFINE_BIT( IBSP_DBG_CONN) \
	WPP_DEFINE_BIT( IBSP_DBG_OPT) \
	WPP_DEFINE_BIT( IBSP_DBG_NEV) \
	WPP_DEFINE_BIT( IBSP_DBG_HW) \
	WPP_DEFINE_BIT( IBSP_DBG_IO) \
	WPP_DEFINE_BIT( IBSP_DBG_DUP) \
	WPP_DEFINE_BIT( IBSP_DBG_PERFMON) \
	WPP_DEFINE_BIT( IBSP_DBG_APM))


#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// IBSP_ENTER( FLAG );
// IBSP_EXIT( FLAG );
// USEPREFIX(IBSP_PRINT, "%!STDPREFIX! %!FUNC!() :");
// USEPREFIX(IBSP_PRINT_EXIT, "%!STDPREFIX! %!FUNC!() :");
// USEPREFIX(IBSP_ERROR, "%!STDPREFIX! %!FUNC!() :ERR***");
// USEPREFIX(IBSP_ERROR_EXIT, "%!STDPREFIX! %!FUNC!() :ERR***");
// USESUFFIX(IBSP_ENTER, " %!FUNC!():[");
// USESUFFIX(IBSP_EXIT, " %!FUNC!():]");
// end_wpp



#define STAT_INC(name)
#define STAT_DEC(name)
#define BREAKPOINT(x)
#define DebugPrintIBSPIoctlParams(a,b,c,d,e,f,g,h,i)
#define DebugPrintSockAddr(a,b)
#define fzprint(a)
#define STATS(expr)

#else

#include <wmistr.h>
#include <evntrace.h>

/*
 * Debug macros
 */



#define IBSP_DBG_ERR		0x00000001	/* error */
#define IBSP_DBG_DLL		0x00000002	/* DLL */
#define IBSP_DBG_SI			0x00000004	/* socket info */
#define IBSP_DBG_INIT		0x00000008	/* initialization code */
#define IBSP_DBG_WQ		0x00000010	/* WQ related functions */
#define IBSP_DBG_EP			0x00000020	/* Enpoints related functions */
#define IBSP_DBG_MEM		0x00000040	/* memory registration */
#define IBSP_DBG_CM			0x00000080	/* CM */
#define IBSP_DBG_CONN		0x00000100	/* connections */
#define IBSP_DBG_OPT		0x00000200	/* socket options */
#define IBSP_DBG_NEV		0x00000400	/* network events */
#define IBSP_DBG_HW			0x00000800	/* Hardware */
#define IBSP_DBG_IO			0x00001000	/* Overlapped I/O request */
#define IBSP_DBG_DUP		0x00002000	/* Socket Duplication */
#define IBSP_DBG_PERFMON	0x00004000	/* Performance Monitoring */
#define IBSP_DBG_APM		0x00008000	/* APM handeling */

#define IBSP_DBG_ERROR		(CL_DBG_ERROR | IBSP_DBG_ERR)



#if DBG

// assignment of _level_ is need to to overcome warning C4127
#define IBSP_PRINT( _level_,_flag_,_msg_)  \
	{ \
		if( g_ibsp_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_ibsp_dbg_flags, _msg_ ); \
	}


#define IBSP_PRINT_EXIT( _level_,_flag_,_msg_) \
	{ \
		if( g_ibsp_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_ibsp_dbg_flags, _msg_ );\
		IBSP_EXIT( _flag_ );\
	}

#define IBSP_ENTER( _flag_) \
	{ \
		if( g_ibsp_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_ENTER( _flag_, g_ibsp_dbg_flags ); \
	}

#define IBSP_EXIT( _flag_)\
	{ \
		if( g_ibsp_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_EXIT( _flag_, g_ibsp_dbg_flags ); \
	}


//#define fzprint(a) CL_PRINT(IBSP_DBG_USER, IBSP_DBG_USER, a)
#define fzprint(a)

//#define BREAKPOINT(x) if( gCurrentDebugLevel & x ) { DebugBreak(); }
void
DebugPrintIBSPIoctlParams(
					uint32_t						flags,
					DWORD						dwIoControlCode,
					LPVOID						lpvInBuffer,
					DWORD						cbInBuffer,
					LPVOID						lpvOutBuffer,
					DWORD						cbOutBuffer,
					LPWSAOVERLAPPED				lpOverlapped,
					LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
					LPWSATHREADID				lpThreadId );


void
DebugPrintSockAddr(
					uint32_t						flags,
					struct sockaddr_in			*sockaddr );

void
debug_dump_buffer(
					uint32_t						flags,
			const	char						*name,
					void						*buf,
					size_t						len );

void
debug_dump_overlapped(
					uint32_t						flags,
			const	char						*name,
					LPWSAOVERLAPPED				lpOverlapped );

/* Activate memory tracking debugging */
#define HeapAlloc(a,b,c) cl_zalloc(c)
#define HeapFree(a,b,c)  (cl_free(c), TRUE)
#define HeapCreate(a,b,c)  ((HANDLE)(-1))
#define HeapDestroy(a)

#define STAT_INC(name) cl_atomic_inc( &g_ibsp.name )
#define STAT_DEC(name) cl_atomic_dec( &g_ibsp.name )

#else


#define IBSP_PRINT( _level_,_flag_,_msg_)
#define IBSP_PRINT_EXIT( _level_,_flag_,_msg_)
#define IBSP_ENTER( _flag_)
#define IBSP_EXIT( _flag_)
#define fzprint(a)
#endif /* DBG */


#define IBSP_ERROR( _msg_) \
	IBSP_PRINT( TRACE_LEVEL_ERROR, IBSP_DBG_ERROR, _msg_)

#define IBSP_ERROR_EXIT( _msg_) \
	IBSP_PRINT_EXIT( TRACE_LEVEL_ERROR, IBSP_DBG_ERROR, _msg_)


#endif /* EVENT_TRACING */

/*
 * To enable logging of all Send/Receive data for each socket
 * uncomment the following line.
 */
//#define IBSP_LOGGING

#ifdef IBSP_LOGGING

typedef struct _DataLogger
{
	char				*BufferStart;
	size_t				TotalSize;
	char				*NextPrint;
	size_t				ToatalPrinted;
	BOOL				ShutdownClosed;
	HANDLE				hMapFile;

}	DataLogger;


VOID DataLogger_Init(
					DataLogger					*pLoger,
					char						*prefix,
					struct sockaddr_in			*addr1,
					struct sockaddr_in			*addr2 );


VOID DataLogger_WriteData(
					DataLogger					*pLoger,
					long						Idx,
					char						*Data,
					DWORD						Len );

VOID DataLogger_Shutdown(
					DataLogger					*pLoger );

#endif	/* IBSP_LOGGING */

#endif	/* _IBSP_DEBUG_H_ */
