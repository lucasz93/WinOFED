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


#if !defined(__ALTS_DEBUG_H__)
#define __ALTS_DEBUG_H__

#ifndef __MODULE__
#define __MODULE__	"alts"
#endif

#include <complib/cl_debug.h>

#define	ALTS_DBG_NORMAL		(1 << 0)
#define ALTS_DBG_PNP		(1 << 1)
#define ALTS_DBG_INFO		(1 << 2)
#define ALTS_DBG_VERBOSE	(1 << 3)
#define ALTS_DBG_DEV		(1 << 4)
#define ALTS_DBG_STATUS		(1 << 5)
#define ALTS_DBG_ERROR		CL_DBG_ERROR

#define	ALTS_DBG_NONE		CL_DBG_DISABLE
#define ALTS_DBG_FULL		CL_DBG_ALL

extern uint32_t			alts_dbg_lvl;

/* Macros for simplifying CL_ENTER, CL_TRACE, etc. */
#define ALTS_ENTER( msg_lvl )	\
	CL_ENTER( msg_lvl, alts_dbg_lvl )

#define ALTS_EXIT( msg_lvl )				\
	CL_EXIT( msg_lvl, alts_dbg_lvl )

#define ALTS_TRACE( msg_lvl, msg )		\
	CL_TRACE( msg_lvl, alts_dbg_lvl, msg )

#define ALTS_TRACE_EXIT( msg_lvl, msg )	\
	CL_TRACE_EXIT( msg_lvl, alts_dbg_lvl, msg )

#ifndef CL_KERNEL

#define ALTS_PRINT( msg_lvl, msg )		\
	printf msg
#else

#define ALTS_PRINT( msg_lvl, msg )		\
	CL_PRINT( msg_lvl, alts_dbg_lvl, msg )
#endif	

#endif // __ALTS_DEBUG_H__
