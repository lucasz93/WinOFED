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
 *
 */


#ifndef  _MLX4_DEBUG_H_
#define _MLX4_DEBUG_H_

#include <complib/cl_debug.h>

extern uint32_t		g_mlx4_dbg_level;
extern uint32_t		g_mlx4_dbg_flags;


#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//
//

#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(HCACtlGuid,(1752F07C,7E5C,402c,9C5F,AD21E572F852),  \
	WPP_DEFINE_BIT( MLX4_DBG_DEV) \
	WPP_DEFINE_BIT( MLX4_DBG_PNP) \
	WPP_DEFINE_BIT( MLX4_DBG_MAD) \
	WPP_DEFINE_BIT( MLX4_DBG_PO) \
	WPP_DEFINE_BIT( MLX4_DBG_CQ) \
	WPP_DEFINE_BIT( MLX4_DBG_QP) \
	WPP_DEFINE_BIT( MLX4_DBG_MEMORY) \
	WPP_DEFINE_BIT( MLX4_DBG_SRQ) \
	WPP_DEFINE_BIT( MLX4_DBG_AV) \
	WPP_DEFINE_BIT( MLX4_DBG_SEND) \
	WPP_DEFINE_BIT( MLX4_DBG_RECV) \
	WPP_DEFINE_BIT( MLX4_DBG_LOW))


#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// MLX4_ENTER(FLAG);
// MLX4_EXIT(FLAG);
// USEPREFIX(MLX4_PRINT, "%!FUNC!()  ");
// USESUFFIX(MLX4_ENTER, "%!FUNC!===>");
// USESUFFIX(MLX4_EXIT, "%!FUNC!<===");
// end_wpp


#else

#include <wmistr.h>
#include <evntrace.h>

/*
 * Debug macros
 */


#define MLX4_DBG_DEV	(1 << 0)
#define MLX4_DBG_PNP	(1 << 1)
#define MLX4_DBG_MAD	(1 << 2)
#define MLX4_DBG_PO	(1 << 3)
#define MLX4_DBG_QP	(1 << 4)
#define MLX4_DBG_CQ	(1 << 5)
#define MLX4_DBG_MEMORY	(1 << 6)
#define MLX4_DBG_SRQ	(1 << 7)
#define MLX4_DBG_AV	(1 << 8)
#define MLX4_DBG_SEND	(1 << 9)
#define MLX4_DBG_RECV	(1 << 10)
#define MLX4_DBG_LOW	(1 << 11)


VOID
	_MLX4_PRINT(
	__in char* msg,
	...);

#if DBG


extern const int MLX4_PRINT_HELPER;

#define MLX4_PRINT(_level_,_flags_,_msg_)  \
	if ((_level_) <= g_mlx4_dbg_level && (_flags_) & g_mlx4_dbg_flags) {\
                _MLX4_PRINT("[MLX4] %s():",__FUNCTION__);\
                if((_level_ | MLX4_PRINT_HELPER) == TRACE_LEVEL_ERROR) _MLX4_PRINT ("***ERROR***  ");\
		_MLX4_PRINT _msg_  ;	\
	}
	

#else

#define MLX4_PRINT(lvl ,flags, msg) 

#endif


#define MLX4_ENTER(flags)\
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, flags,("===>\n"));

#define MLX4_EXIT(flags)\
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, flags,("<===\n"));

#define MLX4_PRINT_EXIT(_level_,_flag_,_msg_)	\
	{\
		if (status != IB_SUCCESS) {\
			MLX4_PRINT(_level_,_flag_,_msg_);\
		}\
		MLX4_EXIT(_flag_);\
	}

#endif //EVENT_TRACING

#endif	/*_MLNX_MLX4_DEBUG_H_ */

