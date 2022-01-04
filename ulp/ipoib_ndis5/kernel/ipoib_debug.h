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


#ifndef _IPOIB_DEBUG_H_
#define _IPOIB_DEBUG_H_


#define __MODULE__	"[IPoIB]"

#include <complib/cl_debug.h>


/* Object types for passing into complib. */
#define IPOIB_OBJ_INSTANCE		1
#define IPOIB_OBJ_PORT			2
#define IPOIB_OBJ_ENDPOINT		3


extern uint32_t		g_ipoib_dbg_level;
extern uint32_t		g_ipoib_dbg_flags;


#if defined(EVENT_TRACING)
//
// Software Tracing Definitions
//
#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID( \
		IPOIBCtlGuid,(3F9BC73D, EB03, 453a, B27B, 20F9A664211A), \
	WPP_DEFINE_BIT(IPOIB_DBG_ERROR) \
	WPP_DEFINE_BIT(IPOIB_DBG_INIT) \
	WPP_DEFINE_BIT(IPOIB_DBG_PNP) \
	WPP_DEFINE_BIT(IPOIB_DBG_SEND) \
	WPP_DEFINE_BIT(IPOIB_DBG_RECV) \
	WPP_DEFINE_BIT(IPOIB_DBG_ENDPT) \
	WPP_DEFINE_BIT(IPOIB_DBG_IB) \
	WPP_DEFINE_BIT(IPOIB_DBG_BUF) \
	WPP_DEFINE_BIT(IPOIB_DBG_MCAST) \
	WPP_DEFINE_BIT(IPOIB_DBG_ALLOC) \
	WPP_DEFINE_BIT(IPOIB_DBG_OID) \
	WPP_DEFINE_BIT(IPOIB_DBG_IOCTL) \
	WPP_DEFINE_BIT(IPOIB_DBG_STAT) \
	WPP_DEFINE_BIT(IPOIB_DBG_OBJ))



#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
	(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags) \
	(WPP_LEVEL_ENABLED(flags) && \
	WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)

// begin_wpp config
// IPOIB_ENTER(FLAG);
// IPOIB_EXIT(FLAG);
// USEPREFIX(IPOIB_PRINT, "%!STDPREFIX! [IPoIB] :%!FUNC!() :");
// USEPREFIX(IPOIB_PRINT_EXIT, "%!STDPREFIX! [IPoIB] :%!FUNC!() :");
// USESUFFIX(IPOIB_PRINT_EXIT, "[IpoIB] :%!FUNC!():]");
// USESUFFIX(IPOIB_ENTER, " [IPoIB] :%!FUNC!():[");
// USESUFFIX(IPOIB_EXIT, " [IPoIB] :%!FUNC!():]");
// end_wpp

#else

#include <evntrace.h>


/*
 * Debug macros
 */
#define IPOIB_DBG_ERR	(1 << 0)
#define IPOIB_DBG_INIT	(1 << 1)
#define IPOIB_DBG_PNP	(1 << 2)
#define IPOIB_DBG_SEND	(1 << 3)
#define IPOIB_DBG_RECV	(1 << 4)
#define IPOIB_DBG_ENDPT	(1 << 5)
#define IPOIB_DBG_IB	(1 << 6)
#define IPOIB_DBG_BUF	(1 << 7)
#define IPOIB_DBG_MCAST	(1 << 8)
#define IPOIB_DBG_ALLOC	(1 << 9)
#define IPOIB_DBG_OID	(1 << 10)
#define IPOIB_DBG_IOCTL	(1 << 11)
#define IPOIB_DBG_STAT	(1 << 12)
#define IPOIB_DBG_OBJ	(1 << 13)

#define IPOIB_DBG_ERROR	(CL_DBG_ERROR | IPOIB_DBG_ERR)
#define IPOIB_DBG_ALL	CL_DBG_ALL


#if DBG

// assignment of _level_ is need to to overcome warning C4127
#define IPOIB_PRINT(_level_,_flag_,_msg_) \
	{ \
		__pragma(warning(suppress:6326)) \
		if( g_ipoib_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_ipoib_dbg_flags, _msg_ ); \
	}

#define IPOIB_PRINT_EXIT(_level_,_flag_,_msg_) \
	{ \
		__pragma(warning(suppress:6326)) \
		if( g_ipoib_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_ipoib_dbg_flags, _msg_ );\
		IPOIB_EXIT(_flag_);\
	}

#define IPOIB_ENTER(_flag_) \
	{ \
		__pragma(warning(suppress:6326)) \
		if( g_ipoib_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_ENTER( _flag_, g_ipoib_dbg_flags ); \
	}

#define IPOIB_EXIT(_flag_)\
	{ \
		__pragma(warning(suppress:6326)) \
		if( g_ipoib_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_EXIT( _flag_, g_ipoib_dbg_flags ); \
	}

#define IPOIB_TRACE_BYTES( lvl, ptr, len )									\
	{																		\
		__pragma(warning(suppress:6326)) 									\
		if( g_ipoib_dbg_level >= (_level_) &&								\
			(g_ipoib_dbg_flags & (_flag_)) )								\
		{																	\
			size_t _loop_;													\
			for( _loop_ = 0; _loop_ < (len); ++_loop_ )						\
			{																\
				cl_dbg_out( "0x%.2X ", ((uint8_t*)(ptr))[_loop_] );			\
				if( (_loop_ + 1)% 16 == 0 )									\
					cl_dbg_out("\n");											\
				else if( (_loop_ % 4 + 1) == 0 )							\
					cl_dbg_out("  ");											\
			}																\
			cl_dbg_out("\n");													\
		}																	\
	}

#else

#define IPOIB_PRINT(lvl, flags, msg)

#define IPOIB_PRINT_EXIT(_level_,_flag_,_msg_)

#define IPOIB_ENTER(_flag_)

#define IPOIB_EXIT(_flag_)

#define IPOIB_TRACE_BYTES( lvl, ptr, len )

#endif

#endif //EVENT_TRACING


enum ipoib_perf_counters
{
	SendBundle,
	SendPackets,
		PortSend,
			GetEthHdr,
			SendMgrQueue,
				GetEndpt,
					EndptQueue,
			QueuePacket,
			BuildSendDesc,
				SendMgrFilter,
					FilterIp,
						QueryIp,
						SendTcp,
						FilterUdp,
							QueryUdp,
							SendUdp,
							FilterDhcp,
					FilterArp,
					SendGen,
						SendCopy,
			PostSend,
			ProcessFailedSends,
	SendCompBundle,
	SendCb,
		PollSend,
		SendComp,
		FreeSendBuf,
		RearmSend,
		PortResume,
	RecvCompBundle,
	RecvCb,
		PollRecv,
		FilterRecv,
			GetRecvEndpts,
				GetEndptByGid,
				GetEndptByLid,
				EndptInsert,
			RecvTcp,
			RecvUdp,
			RecvDhcp,
			RecvArp,
			RecvGen,
	BuildPktArray,
		PreparePkt,
			GetNdisPkt,
	RecvNdisIndicate,
	PutRecvList,
	RepostRecv,
		GetRecv,
		PostRecv,
	RearmRecv,
		ReturnPacket,
		ReturnPutRecv,
		ReturnRepostRecv,
		ReturnPreparePkt,
		ReturnNdisIndicate,

	/* Must be last! */
	MaxPerf

};


enum ref_cnt_buckets
{
	ref_init = 0,
	ref_refresh_mcast,	/* only used in refresh_mcast */
	ref_send_packets,	/* only in send_packets */
	ref_get_recv,
	ref_repost,		/* only in __recv_mgr_repost */
	ref_recv_cb,	/* only in __recv_cb */
	ref_send_cb,	/* only in __send_cb */
	ref_port_up,
	ref_get_bcast,
	ref_bcast,		/* join and create, used as base only */
	ref_join_mcast,
	ref_leave_mcast,
	ref_endpt_track,	/* used when endpt is in port's child list. */

	ref_array_size,	/* Used to size the array of ref buckets. */
	ref_mask = 100,	/* Used to differentiate derefs. */

	ref_failed_recv_wc = 100 + ref_get_recv,
	ref_recv_inv_len = 200 + ref_get_recv,
	ref_recv_loopback = 300 + ref_get_recv,
	ref_recv_filter = 400 + ref_get_recv,

	ref_bcast_get_cb = 100 + ref_get_bcast,

	ref_join_bcast = 100 + ref_bcast,
	ref_create_bcast = 200 + ref_bcast,
	ref_bcast_inv_state = 300 + ref_bcast,
	ref_bcast_req_failed = 400 + ref_bcast,
	ref_bcast_error = 500 + ref_bcast,
	ref_bcast_join_failed = 600 + ref_bcast,
	ref_bcast_create_failed = 700 + ref_bcast,

	ref_mcast_inv_state = 100 + ref_join_mcast,
	ref_mcast_req_failed = 200 + ref_join_mcast,
	ref_mcast_no_endpt = 300 + ref_join_mcast,
	ref_mcast_av_failed = 400 + ref_join_mcast,
	ref_mcast_join_failed = 500 + ref_join_mcast,

	ref_port_info_cb = 100 + ref_port_up

};


#endif	/* _IPOIB_DEBUG_H_ */
