/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
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


#ifndef _VNIC_DEBUG_H_
#define _VNIC_DEBUG_H_


#include <complib/cl_debug.h>

/*
 * Debug macros
 */
extern uint32_t		g_vnic_dbg_lvl;


#define VNIC_DBG_INIT		( ( VNIC_DBG_INFO ) | 0x00000001 )
#define VNIC_DBG_PNP		( ( VNIC_DBG_INFO ) | 0x00000002 )
#define VNIC_DBG_SEND		( ( VNIC_DBG_INFO ) | 0x00000004 )
#define VNIC_DBG_RECV		( ( VNIC_DBG_INFO ) | 0x00000008 )
#define VNIC_DBG_STATUS		( ( VNIC_DBG_INFO ) | 0x00000010 )
#define VNIC_DBG_IB			( ( VNIC_DBG_INFO ) | 0x00000020 )
#define VNIC_DBG_BUF		( ( VNIC_DBG_INFO ) | 0x00000040 )
#define VNIC_DBG_MCAST		( ( VNIC_DBG_INFO ) | 0x00000080 )
#define VNIC_DBG_ALLOC		( ( VNIC_DBG_INFO ) | 0x00000100 )
#define VNIC_DBG_OID		( ( VNIC_DBG_INFO ) | 0x00000200 )
#define VNIC_DBG_DATA		( ( VNIC_DBG_INFO ) | 0x00000400 )
#define VNIC_DBG_CTRL		( ( VNIC_DBG_INFO ) | 0x00000800 )
#define VNIC_DBG_CTRL_PKT	( ( VNIC_DBG_INFO ) | 0x00001000 ) 
#define VNIC_DBG_CONF		( ( VNIC_DBG_INFO ) | 0x00002000 )
#define VNIC_DBG_VIPORT		( ( VNIC_DBG_INFO ) | 0x00004000 )
#define VNIC_DBG_ADAPTER	( ( VNIC_DBG_INFO ) | 0x00008000 )
#define VNIC_DBG_NETPATH	( ( VNIC_DBG_INFO ) | 0x00010000 )

#define VNIC_DBG_FUNC		(0x10000000)	/* For function entry/exit */
#define VNIC_DBG_INFO		(0x20000000)	/* For verbose information */
#define VNIC_DBG_WARN		(0x40000000)	/* For warnings. */
#define VNIC_DBG_ERROR		CL_DBG_ERROR
#define VNIC_DBG_ALL		CL_DBG_ALL

#define VNIC_DEBUG_FLAGS ( VNIC_DBG_ERROR | VNIC_DBG_WARN |	VNIC_DBG_INFO )
/* Enter and exit macros automatically add VNIC_DBG_FUNC bit */
#define VNIC_ENTER( lvl )	\
	CL_ENTER( (lvl | VNIC_DBG_FUNC), g_vnic_dbg_lvl )

#define VNIC_EXIT( lvl )	\
	CL_EXIT( (lvl | VNIC_DBG_FUNC), g_vnic_dbg_lvl )

#if defined FREE_BUILD_DBG
#define VNIC_TRACE( lvl, msg )							\
	{													\
		switch( (lvl) & VNIC_DBG_ERROR )				\
		{												\
		case VNIC_DBG_ERROR:							\
			cl_msg_out msg;								\
		default:										\
			CL_TRACE( (lvl), g_vnic_dbg_lvl, msg );		\
		}												\
	}

#define VNIC_TRACE_EXIT( lvl, msg )						 \
	{													 \
		switch( (lvl) & VNIC_DBG_ERROR )				 \
		{												 \
		case VNIC_DBG_ERROR:							 \
			cl_msg_out msg;								 \
		default:										 \
			CL_TRACE_EXIT( (lvl), g_vnic_dbg_lvl, msg ); \
		}												 \
	}

#else // ! FREE_BUILD_DBG

#define VNIC_TRACE( lvl, msg )	\
	CL_TRACE( (lvl), g_vnic_dbg_lvl, msg )

#define VNIC_TRACE_EXIT( lvl, msg )	\
	CL_TRACE_EXIT( (lvl), g_vnic_dbg_lvl, msg )

#endif // FREE_BUILD_DBG

#define VNIC_PRINT( lvl, msg )	\
	CL_PRINT ( (lvl), g_vnic_dbg_lvl, msg )

#define VNIC_TRACE_BYTES( lvl, ptr, len )									\
	{																		\
		size_t _loop_;														\
		for (_loop_ = 0; _loop_ < (len); ++_loop_)							\
		{																	\
			CL_PRINT( (lvl), g_vnic_dbg_lvl, ("0x%.2X ", ((uint8_t*)(ptr))[_loop_]));	\
			if ((_loop_  + 1)% 16 == 0)											\
			{																\
				CL_PRINT( (lvl), g_vnic_dbg_lvl, ("\n") );					\
			}																\
			else if ((_loop_ % 4 + 1) == 0)										\
			{																\
				CL_PRINT( (lvl), g_vnic_dbg_lvl, ("  ") );					\
			}																\
		}																	\
		CL_PRINT( (lvl), g_vnic_dbg_lvl, ("\n") );							\
	}


#endif /* _VNIC_DEBUG_H_ */
