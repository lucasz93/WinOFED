/*
 * Copyright (c) 2005 Mellanox Technologies.  All rights reserved.
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


#ifndef  _MLNX_UVP_DEBUG_H_
#define _MLNX_UVP_DEBUG_H_

#include <complib/cl_debug.h>

extern uint32_t		g_mlnx_dbg_level;
extern uint32_t		g_mlnx_dbg_flags;


#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//
//

#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(HCACtlGuid,(2C718E52,0D36,4bda,9E58,0FC601818D8F),  \
	WPP_DEFINE_BIT( UVP_DBG_DEV) \
	WPP_DEFINE_BIT( UVP_DBG_PNP) \
	WPP_DEFINE_BIT( UVP_DBG_MAD) \
	WPP_DEFINE_BIT( UVP_DBG_PO) \
	WPP_DEFINE_BIT( UVP_DBG_CQ) \
	WPP_DEFINE_BIT( UVP_DBG_QP) \
	WPP_DEFINE_BIT( UVP_DBG_MEMORY) \
	WPP_DEFINE_BIT( UVP_DBG_SRQ) \
	WPP_DEFINE_BIT( UVP_DBG_AV) \
	WPP_DEFINE_BIT( UVP_DBG_SEND) \
	WPP_DEFINE_BIT( UVP_DBG_RECV) \
	WPP_DEFINE_BIT( UVP_DBG_LOW) \
	WPP_DEFINE_BIT( UVP_DBG_SHIM))


#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// UVP_ENTER(FLAG);
// UVP_EXIT(FLAG);
// USEPREFIX(UVP_PRINT, "%!FUNC!()  ");
// USESUFFIX(UVP_ENTER, "%!FUNC!===>");
// USESUFFIX(UVP_EXIT, "%!FUNC!<===");
// end_wpp


#else

#include <wmistr.h>
#include <evntrace.h>

/*
 * Debug macros
 */


#define UVP_DBG_DEV	(1 << 0)
#define UVP_DBG_PNP	(1 << 1)
#define UVP_DBG_MAD	(1 << 2)
#define UVP_DBG_PO	(1 << 3)
#define UVP_DBG_QP	(1 << 4)
#define UVP_DBG_CQ	(1 << 5)
#define UVP_DBG_MEMORY	(1 << 6)
#define UVP_DBG_SRQ	(1 << 7)
#define UVP_DBG_AV	(1 << 8)
#define UVP_DBG_SEND	(1 << 9)
#define UVP_DBG_RECV	(1 << 10)
#define UVP_DBG_LOW	(1 << 11)
#define UVP_DBG_SHIM	(1 << 12)


VOID
	_UVP_PRINT(
	IN char* msg,
	...);

#if DBG

#define UVP_PRINT(_level_,_flags_,_msg_)  \
	if ((_level_) <= g_mlnx_dbg_level && (_flags_) & g_mlnx_dbg_flags) {\
                _UVP_PRINT("[UVP] %s():",__FUNCTION__);\
                if((_level_) == TRACE_LEVEL_ERROR) _UVP_PRINT ("***ERROR***  ");\
		_UVP_PRINT _msg_  ;	\
	}
	

//
#else

#define UVP_PRINT(lvl ,flags, msg) 

#endif


#define UVP_ENTER(flags)\
	UVP_PRINT(TRACE_LEVEL_VERBOSE, flags,("===>\n"));

#define UVP_EXIT(flags)\
	UVP_PRINT(TRACE_LEVEL_VERBOSE, flags,("<===\n"));

#define UVP_PRINT_EXIT(_level_,_flag_,_msg_)	\
	{\
		if (status != IB_SUCCESS) {\
			UVP_PRINT(_level_,_flag_,_msg_);\
		}\
		UVP_EXIT(_flag_);\
	}

#endif //EVENT_TRACING

#endif	/*_MLNX_UVP_DEBUG_H_ */

