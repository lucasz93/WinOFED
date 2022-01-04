/*
 * Copyright (c) 2005 Mellanox Technologies LTD.  All rights reserved.
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
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation. Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property. If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
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

// Author: Uri Habusha  

#pragma once

#if defined(EVENT_TRACING)


#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)

// begin_wpp config
// TRACE_FUNC_ENTER(FLAG);
// TRACE_FUNC_EXIT(FLAG);
// TRACE_PRINT(LEVEL,FLAGS,MSG,...)
// USESUFFIX(TRACE_FUNC_ENTER, "====>>> %!FUNC! ");
// USESUFFIX(TRACE_FUNC_EXIT, "<<<====== %!FUNC!]");
// end_wpp

#else //defined(EVENT_TRACING)

#include <evntrace.h>

// Debug toppics
#define BUS_DRIVER            0x000001
#define BUS_SS                0x000002
#define BUS_PNP               0x000004
#define BUS_IOCTL             0x000008
#define BUS_POWER             0x000010
#define BUS_WMI               0x000020

#if DBG

extern const unsigned int g_SdpDbgLevel;
extern const unsigned int g_SdpDbgFlags;

//                                                                      
//BUGBUG: need to protect against context switch otherwise there can    
//  be mismatched of trace messages. We can't use a simple spinlock     
//  since some of the printing occours in IRQ level and the spinlock    
//  can be alreardy use.                                                
//                                                                      
#define TRACE_PRINT(_level_,_flag_,_msg_)                                       \
    if (g_SdpDbgLevel >= (_level_) && (g_SdpDbgFlags & (_flag_)))               \
    {                                                                           \
        if(_level_ == TRACE_LEVEL_ERROR)                                        \
            cl_dbg_out ("***ERROR***  ");                                         \
        cl_dbg_out ("%s(): ",__FUNCTION__);                                       \
        cl_dbg_out _msg_;                                                         \
    }

#else

#define TRACE_PRINT(lvl ,flags, msg) 

#endif



#define TRACE_FUNC_ENTER(flags)\
	ETH_PRINT(TRACE_LEVEL_VERBOSE, flags,("===>\n"));

#define TRACE_FUNC_EXIT(flags)\
	ETH_PRINT(TRACE_LEVEL_VERBOSE, flags, ("<===\n" ));



#endif //defined(EVENT_TRACING)

