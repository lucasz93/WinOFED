/*
 * Copyright (c) 2011 Intel Corporation.  All rights reserved.
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

#include <precompile.h>

#if PERF_TRACK_ON

// must match enum ipoib_perf_counters in ipoib_debug.h

char *PerfCounterName[MaxPerf] =
{
	"SendBundle",
	"SendPackets",
		"PortSend",
			"GetEthHdr",
			"SendMgrQueue",
				"GetEndpt",
					"EndptQueue",
			"QueuePacket",
			"BuildSendDesc",
				"SendMgrFilter",
					"FilterIp",
					"FilterIpV6",
						"QueryIp",
						"SendTcp",
						"FilterUdp",
							"QueryUdp",
							"SendUdp",
							"FilterDhcp",
					"FilterArp",
					"SendGen",
						"SendCopy",
			"PostSend",
			"ProcessFailedSends",
	"SendCompBundle",
	"CMSendCompBundle",
	"SendCb",
		"PollSend",
		"SendComp",
		"FreeSendBuf",
		"RearmSend",
		"PortResume",
	"CMSendCb",
		"CMPollSend",
		"CMFreeSendBuf",
	"RecvCompBundle",
	"RecvCb",
		"PollRecv",
		"FilterRecv",
			"GetRecvEndpts",
				"GetEndptByGid",
				"GetEndptByLid",
				"EndptInsert",
			"RecvTcp",
			"RecvUdp",
			"RecvDhcp",
			"RecvArp",
			"RecvGen",
	"CMRecvCb",
		"CMPollRecv",
		"CMFilterRecv",
		"CMRepostRecv",
	"CMBuildNBLArray",
		"CMPreparePkt",
	"BuildNBLArray",
		"PreparePkt",
			"GetNdisPkt",
	"CMRecvNdisIndicate",
	"RecvNdisIndicate",
	"PutRecvList",
	"RepostRecv",
		"GetRecv",
		"PostRecv",
	"RearmRecv",
		"ReturnPacket",
		"ReturnPutRecv",
		"ReturnRepostRecv",
		"ReturnPreparePkt",
		"ReturnNdisIndicate"
};

/*
 * Display the captured performance data.
 * Overrides complib versions for local control.
 */

#define perf_out cl_dbg_out
// #define perf_out cl_msg_out
// 'cl_msg_out' requires dbgview.exe to set verbose kernel output in order
// to see cl_msg_out() display.

void
ipoib_cl_perf_display(
	IN	const cl_perf_t* const	p_perf )
{
	uintn_t	i;

	CL_ASSERT( p_perf );
	CL_ASSERT( p_perf->state == CL_INITIALIZED );

	perf_out( "Perf:Counter Update Times\n" );
	perf_out( "Perf:  Locked    Unlocked    Calibration Loops\n" );
	perf_out( "Perf:%8"PRIu64"    %8"PRIu64"         %u\n",
						p_perf->locked_calibration_time,
						p_perf->normal_calibration_time,
						PERF_CALIBRATION_TESTS );

	perf_out( "Perf:IPoIB Performance Counters\n" );
	perf_out( "Perf:Name                TotalTime  MinTime    Count     Ave\n" );
	for( i = 0; i < p_perf->size; i++ )
	{
		if( p_perf->data_array[i].count > 50 )
		{
			perf_out( "Perf:%-18s %8"PRIu64"   %8"PRIu64" %8"PRIu64"%8"PRIu64"\n",
				PerfCounterName[i], p_perf->data_array[i].total_time,
				p_perf->data_array[i].min_time, p_perf->data_array[i].count,
				(p_perf->data_array[i].total_time > 0 ?
					p_perf->data_array[i].total_time / p_perf->data_array[i].count
					: 0) );
		}
	}
	perf_out( "Perf:End of IPoIB Performance Counters\n" );
}

/*
 * Destroy the performance tracker.
 * Overrides complib version ala macros in ipoib_debug.h
 */
void
ipoib_cl_perf_destroy(
	IN	cl_perf_t* const	p_perf,
	IN	const boolean_t		display )
{
	uintn_t	i;

	CL_ASSERT( cl_is_state_valid( p_perf->state ) );

	if( !p_perf->data_array )
		return;

	/* Display the performance data as requested. */
	if( display && p_perf->state == CL_INITIALIZED )
		ipoib_cl_perf_display( p_perf );

	/* Destroy the user's counters. */
	for( i = 0; i < p_perf->size; i++ )
		cl_spinlock_destroy( &p_perf->data_array[i].lock );

	cl_free( p_perf->data_array );
	p_perf->data_array = NULL;

	p_perf->state = CL_UNINITIALIZED;
}
#endif /* PERF_TRACK_ON */


#if DBG

struct _bucket_list
{
	int		val;
	char	*name;
};

#define BK(a) {a,#a}

/* must match ipoib_debug.h enum ref_cnt_buckets */

struct _bucket_list bucket_name[35] =
{
	BK(ref_init),
	BK(ref_refresh_mcast),	/* only used in refresh_mcast */
	BK(ref_send_packets),	/* only in send_packets */
	BK(ref_get_recv),
	BK(ref_repost),		/* only in __recv_mgr_repost */
	BK(ref_recv_cb),	/* only in __recv_cb */
	BK(ref_send_cb),	/* only in __send_cb */
	BK(ref_cm_recv_cb),	/* only in __recv_cm_cb */
	BK(ref_cm_send_cb),	/* only in __send_cm_cb */
	BK(ref_get_cm_recv),
	BK(ref_port_up),
	BK(ref_get_bcast),
	BK(ref_bcast),		/* join and create, used as base only */
	BK(ref_join_mcast),
	BK(ref_leave_mcast),
	BK(ref_endpt_track),	/* used when endpt is in port's child list. */
	BK(ref_failed_recv_wc),
	BK(ref_recv_inv_len),
	BK(ref_recv_loopback),
	BK(ref_recv_filter),
	BK(ref_bcast_get_cb),
	BK(ref_join_bcast),
	BK(ref_create_bcast),
	BK(ref_bcast_inv_state),
	BK(ref_bcast_req_failed),
	BK(ref_bcast_error),
	BK(ref_bcast_join_failed),
	BK(ref_bcast_create_failed),
	BK(ref_mcast_inv_state),
	BK(ref_mcast_req_failed),
	BK(ref_mcast_no_endpt),
	BK(ref_mcast_av_failed),
	BK(ref_mcast_join_failed),
	BK(ref_port_info_cb),
	{0,NULL}
};

char *ref_cnt_str(int type)
{
	int	i;
	char *cp;

	for(i=0,cp=NULL; bucket_name[i].name; i++)
	{
		if( bucket_name[i].val == type )
		{
			cp = bucket_name[i].name;
			break;
		}
	}
	if( cp == NULL )
	{
		static char what[24];
		StringCchPrintf(what,sizeof(what),"Unknown ref type %d",type);
		cp = what;
	}
	return cp;
}

void
dmp_ipoib_port_refs( ipoib_port_t *p_port, char *where OPTIONAL )
{
	int i;

#if _PORT_REFS
		cl_dbg_out("%s() %s Port[%d] refs %d\n",
			__FUNCTION__, (where ? where:""), p_port->port_num, p_port->obj.ref_cnt);
#else
	UNREFERENCED_PARAMETER(where);
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
		("Port[%d] refs %d\n", p_port->port_num, p_port->obj.ref_cnt) );
#endif

	for(i=0; i < ref_array_size; i++)
	{
		int32_t r = cl_atomic_add( &p_port->ref[i], 0 );

		if( r )
		{
#if _PORT_REFS
			cl_dbg_out("  %s %d\n", ref_cnt_str(i), r);
#else
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
				("  %s %d\n", ref_cnt_str(i), r) );
#endif
		}
	}
}

char *get_ib_recv_mode_str(ib_recv_mode_t m)
{
	char	*s;

	switch( m )
	{
	  case RECV_UD:
		s = "RECV_UD";
		break;
	  case RECV_RC:
		s = "RECV_RC";
		break;
	  default:
		s = "Unknown ib_recv_mode_t value?";
		break;
	}
	return s;
}
#endif // DBG


char *get_ipoib_pkt_type_str( IN ipoib_pkt_type_t t )
{
	char	*s;

	switch( t )
	{
	  case PKT_TYPE_UCAST:
		s = "PKT_TYPE_UCAST";
		break;
	  case PKT_TYPE_BCAST:
		s = "PKT_TYPE_BCAST";
		break;
	  case PKT_TYPE_MCAST:
		s = "PKT_TYPE_MCAST";
		break;
	  case PKT_TYPE_CM_UCAST:
		s = "PKT_TYPE_CM_UCAST";
		break;
	  default:
		s = "Unknown ipoib_pkt_type_t value?";
		break;
	}
	return s;
}


/* Generate an Ethernet MAC string from network order input. */
char *
_mk_mac_str(
	IN		char				*buf,
	IN 		const mac_addr_t	*ma )
{
	StringCchPrintf( buf, 20, "%02X-%02X-%02X-%02X-%02X-%02X",
								ma->addr[0], ma->addr[1],
								ma->addr[2], ma->addr[3],
								ma->addr[4], ma->addr[5] );
	return buf;
}


char *
mk_mac_str( IN const mac_addr_t *ma )
{
	static char buf[20];
	return _mk_mac_str( buf, ma );
}

char *
mk_mac_str2( IN mac_addr_t *ma )
{
	static char buf2[20];
	return _mk_mac_str( buf2, ma );
}


#if 0

static uint32_t 
get_sgl_size( IN  PSCATTER_GATHER_LIST	p_sgl )
{
	uint32_t idx, bytes;

	for( idx=0,bytes=0; idx < p_sgl->NumberOfElements; idx++ )
		bytes += p_sgl->Elements[idx].Length;
		
	return bytes;
}

static uint32_t
dump_sgl( IN  PSCATTER_GATHER_LIST	p_sgl, int verbose )
{
	char		buf[160];
	char		*cp=buf;
	uint32_t	i, total;

	if( !p_sgl )
		return 0;

	StringCchPrintfA(cp,(&buf[160] - cp),"SGL cnt(%u) ",
											p_sgl->NumberOfElements);
	cp = &buf[strlen(buf)];

	for( i=0,total=0; i < p_sgl->NumberOfElements; i++ )
	{
		StringCchPrintfA(cp,(&buf[160] - cp),"[%d].len %u ",
												i,p_sgl->Elements[i].Length);
		cp = &buf[strlen(buf)];
		total += p_sgl->Elements[i].Length;
	}
	StringCchPrintfA(cp,(&buf[160] - cp),"(Total %u)\n",total);

	if( verbose )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND, ("%s",buf) );
	}
	return total;
}

void
dump_wr( ipoib_send_wr_t *wr, uint32_t idx )
{
	char		buf[160];
	char		*cp;
	uint32_t	i, total;

	StringCchPrintfA(buf,sizeof(buf),"wr[%u] num_ds %u ",idx,wr->wr.num_ds);
	cp = &buf[strlen(buf)];
	for( i=0,total=0; i < wr->wr.num_ds; i++ )
	{
		StringCchPrintfA(cp,(&buf[160] - cp),"ds[%d].len %u ",
			i,wr->local_ds[i].length);
		cp = &buf[strlen(buf)];
		total += wr->local_ds[i].length;
	}
	StringCchPrintfA(cp,(&buf[160] - cp),"(Total %u)\n",total);
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND, ("%s",buf) );
}

void
dump_ib_wr( ib_send_wr_t *wr )
{
#define _BS 768
	char		buf[_BS];
	char		*cp;
	uint32_t	idx=0, i, total, gtotal=0;

	buf[0] = '\n';
	buf[1] = '\0';
	for(cp=&buf[1]; wr; idx++,wr=wr->p_next )
	{
		StringCchPrintfA(cp,(&buf[_BS] - cp),
			"wr[%u] wr_id %I64x next%c type %x opts %x imm %x\n  num_ds %u\n",
					idx, wr->wr_id, (wr->p_next ? '+':'0'),
					wr->wr_type, wr->send_opt, wr->immediate_data, wr->num_ds );

		cp = &buf[strlen(buf)];
		for( i=0,total=0; i < wr->num_ds; i++ )
		{
			StringCchPrintfA(cp,(&buf[_BS] - cp),
							"    ds[%d].len %4u @ %I64x key %#x\n",
							i,wr->ds_array[i].length,
							wr->ds_array[i].vaddr,
							wr->ds_array[i].lkey);
			cp = &buf[strlen(buf)];
			total += wr->ds_array[i].length;
		}
		StringCchPrintfA(cp,(&buf[_BS] - cp),"    (Total %u)\n",total);
		gtotal += total;
		cp = &buf[strlen(buf)];
	} 
	cp = &buf[strlen(buf)];
	StringCchPrintfA(cp,(&buf[_BS] - cp),"  Posted %u\n",gtotal);
	//IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND, ("%s",buf) );
	cl_dbg_out("%s() %s%",__FUNCTION__,buf);
#undef _BS
}

#endif

#if EXTRA_DBG

char *get_IP_protocol_str(uint8_t proto)
{
	static char what[28];

	switch( proto )
	{
	  case IPPROTO_HOPOPTS:
		return "IPPROTO_HOPOPTS";

	  case IPPROTO_IPV4:
		return "IPPROTO_IP";

	  case IPPROTO_IPV6:
		return "IPPROTO_ICMP";

	  case IPPROTO_TCP:  
		return "IPPROTO_TCP";

	  case IPPROTO_UDP:
		return "IPPROTO_UDP";

	  case IPPROTO_IGMP:
		return "IPPROTO_IGMP";

	  case IPPROTO_ICMP:
		return "IPPROTO_ICMP";

	  case IPPROTO_ICMPV6:
		return "IPPROTO_ICMPV6";

	  case IPPROTO_NONE:
		return "IPPROTO_NONE";

	  case IPPROTO_DSTOPTS:
		return "IPPROTO_DSTOPTS";

	  case IPPROTO_SCTP:
		return "IPPROTO_SCTP";

	  default:
		break;
	}
	StringCchPrintf(what,sizeof(what),"Unknown IP protocol %d",proto);
	return what;
}

#if 0
static char *get_ipv6_nxt_hdr_str(UINT8 NH)
{
	static char what[28];

	switch( NH )
	{
	  case IPPROTO_HOPOPTS:
		return "Hop-by-Hop";

	  case IPPROTO_ICMP:
		return "ICMP";

	  case IPPROTO_IGMP:
		return "IGMP";

	  case IPPROTO_GGP:
		return "GGP";

	  case IPPROTO_IPV4:
		return "IPV4";

	  case IPPROTO_ST:
		return "ST";

	  case IPPROTO_TCP:
		return "TCP";

	  case IPPROTO_CBT:
		return "CBT";

	  case IPPROTO_EGP:
		return "EGP";

	  case IPPROTO_IGP:
		return "IGP";

	  case IPPROTO_PUP:
		return "PUP";

	  case IPPROTO_UDP:
		return "UDP";

	  case IPPROTO_IDP:
		return "IDP";

	  case IPPROTO_RDP:
		return "RDP";

	  case IPPROTO_IPV6:
		return "IPV6";

	  case IPPROTO_ROUTING:
		return "ROUTING";

	  case IPPROTO_FRAGMENT:
		return "FRAGMENT";

	  case IPPROTO_ESP:
		return "ESP";

	  case IPPROTO_AH:
		return "AH";

	  case IPPROTO_ICMPV6:
		return "ICMPV6";

	  case IPPROTO_NONE:
		return "NONE";

	  case IPPROTO_DSTOPTS:
		return "DSTOPTS";

	  case IPPROTO_ND:
		return "ND";

	  case IPPROTO_ICLFXBM:
		return "ICLFXBM";

	  case IPPROTO_PIM:
		return "PIM";

	  case IPPROTO_PGM:
		return "PGM";

	  case IPPROTO_L2TP:
		return "L2TP";

	  case IPPROTO_SCTP:
		return "SCTP";

	  default:
		break;
	}
	StringCchPrintf(what,sizeof(what),"Unknown Proto %u",NH);
	return what;
}
#endif // 0

#if 0

static void
decode_NBL(
	IN		char const				*preFix,
	IN		ipoib_port_t			*p_port,
	IN		NET_BUFFER_LIST 		*p_net_buffer_list ) 
{
	ipoib_cm_recv_desc_t	*p_desc;
	NET_BUFFER_LIST			*cur_NBL, *next_NBL;
	LONG					NBL_cnt = 0;
	PNET_BUFFER				NB;
	ULONG					len, off, i;

	IPOIB_ENTER(IPOIB_DBG_RECV);

	for (cur_NBL = p_net_buffer_list; cur_NBL != NULL; cur_NBL = next_NBL, NBL_cnt++)
	{
		next_NBL = NET_BUFFER_LIST_NEXT_NBL(cur_NBL);
		/* Get the port and descriptor from the NET_BUFFER_LIST. */
		CL_ASSERT(p_port == IPOIB_PORT_FROM_NBL(cur_NBL));
		p_desc = IPOIB_CM_RECV_FROM_NBL(cur_NBL);

		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("%s[%d] curNBL %p p_desc->len %d NblFlags %#x\n",
				preFix,
				NBL_cnt,
				cur_NBL,
				p_desc->len,
				NET_BUFFER_LIST_NBL_FLAGS(cur_NBL)) );

		NB = NET_BUFFER_LIST_FIRST_NB(cur_NBL);
		for(i = 1; NB;i++)
		{
			MDL		*p_mdl;
			PUCHAR	p_head;
			UINT	mdl_len;

			p_head=NULL;
			p_mdl = NET_BUFFER_FIRST_MDL(NB);	
			NdisQueryMdl( p_mdl, &p_head, &mdl_len, NormalPagePriority );
			if( p_head )
			{
				len = NET_BUFFER_DATA_LENGTH(NB);
				off = NET_BUFFER_DATA_OFFSET(NB);
				IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
					(" NB[%d] off %lu len %lu mdl_len %u\n",
						i,off,len,mdl_len) );
						//(p_head+off),
						//(p_desc->p_buf - DATA_OFFSET)) );
				CL_ASSERT( len == p_desc->len );
				CL_ASSERT( (p_head+off) == (p_desc->p_buf - DATA_OFFSET));
				decode_enet_pkt( "\nEdata:", (p_head + off), mdl_len, NULL );
			}
			NB=NET_BUFFER_NEXT_NB(NB);
		}
	}
}
#endif

#endif // EXTRA_DBG

