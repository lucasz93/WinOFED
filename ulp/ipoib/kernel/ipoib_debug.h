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

#if defined __MODULE__
#undef __MODULE__
#endif

#define __MODULE__	"[IPoIB]"

#include <complib/cl_debug.h>

#include <kernel\ip_packet.h>
#include <netiodef.h>

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
	WPP_DEFINE_BIT(IPOIB_DBG_ERROR)		\
	WPP_DEFINE_BIT(IPOIB_DBG_INIT)		\
	WPP_DEFINE_BIT(IPOIB_DBG_PNP)		\
	WPP_DEFINE_BIT(IPOIB_DBG_SEND)		\
	WPP_DEFINE_BIT(IPOIB_DBG_RECV)		\
	WPP_DEFINE_BIT(IPOIB_DBG_ENDPT)		\
	WPP_DEFINE_BIT(IPOIB_DBG_IB)		\
	WPP_DEFINE_BIT(IPOIB_DBG_BUF)		\
	WPP_DEFINE_BIT(IPOIB_DBG_MCAST)		\
	WPP_DEFINE_BIT(IPOIB_DBG_ALLOC)		\
	WPP_DEFINE_BIT(IPOIB_DBG_OID)		\
	WPP_DEFINE_BIT(IPOIB_DBG_IOCTL)		\
	WPP_DEFINE_BIT(IPOIB_DBG_STAT)		\
	WPP_DEFINE_BIT(IPOIB_DBG_OBJ)		\
	WPP_DEFINE_BIT(IPOIB_DBG_SHUTTER)	\
	WPP_DEFINE_BIT(IPOIB_DBG_ALL)		\
	WPP_DEFINE_BIT(IPOIB_DBG_ARP)		\
	WPP_DEFINE_BIT(IPOIB_DBG_FRAG)		\
	WPP_DEFINE_BIT(IPOIB_DBG_IPV6)		\
	WPP_DEFINE_BIT(IPOIB_DBG_CM)		\
	WPP_DEFINE_BIT(IPOIB_DBG_CM_RECV)	\
	WPP_DEFINE_BIT(IPOIB_DBG_CM_SEND)	\
	WPP_DEFINE_BIT(IPOIB_DBG_CM_CONN)	\
	WPP_DEFINE_BIT(IPOIB_DBG_CM_DCONN)	\
	)


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

#else	// !EVENT_TRACING

#include <evntrace.h>

/*
 * Debug macros
 */
#define IPOIB_DBG_ERR		(1 << 0)
#define IPOIB_DBG_INIT		(1 << 1)
#define IPOIB_DBG_PNP		(1 << 2)
#define IPOIB_DBG_SEND		(1 << 3)
#define IPOIB_DBG_RECV		(1 << 4)
#define IPOIB_DBG_ENDPT		(1 << 5)
#define IPOIB_DBG_IB		(1 << 6)
#define IPOIB_DBG_BUF		(1 << 7)
#define IPOIB_DBG_MCAST		(1 << 8)
#define IPOIB_DBG_ALLOC		(1 << 9)
#define IPOIB_DBG_OID		(1 << 10)
#define IPOIB_DBG_IOCTL		(1 << 11)
#define IPOIB_DBG_STAT		(1 << 12)
#define IPOIB_DBG_OBJ		(1 << 13)
#define IPOIB_DBG_SHUTTER 	(1 << 14)
#define IPOIB_DBG_ARP		(1 << 15)
#define IPOIB_DBG_FRAG		(1 << 16)
#define IPOIB_DBG_IPV6		(1 << 17)
#define IPOIB_DBG_CM		(1 << 18)
#define IPOIB_DBG_CM_RECV	(1 << 19)
#define IPOIB_DBG_CM_SEND	(1 << 20)
#define IPOIB_DBG_CM_CONN	(1 << 21)
#define IPOIB_DBG_CM_DCONN	(1 << 22)

#define IPOIB_DBG_ERROR	(CL_DBG_ERROR | IPOIB_DBG_ERR)
#define IPOIB_DBG_ALL	CL_DBG_ALL

#endif //EVENT_TRACING

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
					cl_dbg_out("\n");										\
				else if( (_loop_ % 4 + 1) == 0 )							\
					cl_dbg_out("  ");										\
			}																\
			cl_dbg_out("\n");												\
		}																	\
	}

#define DIPOIB_PRINT IPOIB_PRINT

#else	// !DBG

#define IPOIB_PRINT(lvl, flags, msg)
#define IPOIB_PRINT_EXIT(_level_,_flag_,_msg_)
#define IPOIB_ENTER(_flag_)
#define IPOIB_EXIT(_flag_)
#define IPOIB_TRACE_BYTES( lvl, ptr, len )

#define DIPOIB_PRINT(_level_,_flag_,_msg_)

#endif

#define XIPOIB_ENTER(_flag_)
#define XIPOIB_EXIT(_flag_)
#define XIPOIB_PRINT(_level_,_flag_,_msg_)


#if	PERF_TRACK_ON

#include <complib/cl_perf.h>

#undef cl_perf_destroy
#define cl_perf_destroy( p_perf, display ) \
	ipoib_cl_perf_destroy( p_perf, display )

void
ipoib_cl_perf_destroy(
	IN	cl_perf_t* const	p_perf,
	IN	const boolean_t		display );

#undef cl_perf_display
#define cl_perf_display( p_perf ) \
	ipoib_cl_perf_display( p_perf )

void
ipoib_cl_perf_display(
	IN	const cl_perf_t* const	p_perf );

// if you add a Perf counter, make sure you also add to the counter name to the
// PerCounterName list defined in ipoib_debug.cpp.

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
					FilterIpV6,
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
	CMSendCompBundle,
	SendCb,
		PollSend,
		SendComp,
		FreeSendBuf,
		RearmSend,
		PortResume,
	CMSendCb,
		CMPollSend,
		CMFreeSendBuf,
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
	CMRecvCb,
		CMPollRecv,
		CMFilterRecv,
		CMRepostRecv,
	CMBuildNBLArray,
		CMPreparePkt,
			CMGetNdisPkt,
	BuildNBLArray,
		PreparePkt,
			GetNdisPkt,
	CMRecvNdisIndicate,
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
#endif	/* PERF_TRACK_ON */


enum ref_cnt_buckets
{
	ref_init = 0,
	ref_refresh_mcast,	/* only used in refresh_mcast */
	ref_send_packets,	/* only in send_packets */
	ref_get_recv,
	ref_repost,		/* only in __recv_mgr_repost */
	ref_recv_cb,	/* only in __recv_cb */
	ref_send_cb,	/* only in __send_cb */
#if IPOIB_CM
	ref_cm_recv_cb,	/* only in __recv_cm_cb */
	ref_cm_send_cb,	/* only in __send_cm_cb */
	ref_get_cm_recv,
#endif
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

#if DBG
void dmp_ipoib_port_refs( ipoib_port_t *p_port, char *where OPTIONAL );
#else
#define dmp_ipoib_port_refs( a, ... )
#endif

char *
_mk_mac_str(
	IN		char *buf, const mac_addr_t *ma);

char *
mk_mac_str(
	IN		const mac_addr_t			*ma);

char *
mk_mac_str2(
	IN		mac_addr_t					*ma);

#if EXTRA_DBG
char *
get_eth_packet_type_str(
	IN	net16_t);

char *
get_IP_protocol_str(
	IN	uint8_t);

void decode_enet_pkt( char *preFix, void *hdr, int len, char *postFix );
#endif

#endif	/* _IPOIB_DEBUG_H_ */
