/*
 * Copyright (c) 2011 Intel Corporation.  All rights reserved.
 * Copyright (c) 2008 QLogic Corporation.  All rights reserved.
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

#include <precompile.h>

#include <complib/cl_math.h>	// for ROUNDUP
#include <inaddr.h>
#include <ip2string.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_cm.tmh"
#endif


static void
__cm_recv_mgr_reset(
	IN		ipoib_port_t* const		p_port,
	IN		ipoib_endpt_t* const	p_endpt );

static void
__cm_buf_mgr_put_recv(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_cm_recv_desc_t* const	p_desc,
	IN		BOOLEAN						update,
	IN		NET_BUFFER_LIST* const		p_net_buffer_list OPTIONAL );

static void
__cm_buf_mgr_put_recv_list(
	IN		ipoib_port_t* const		p_port,
	IN		cl_qlist_t* const		p_list );

static ib_api_status_t
__cm_post_srq_recv(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt );

static void
__cm_send_cb(
	IN		const	ib_cq_handle_t		h_cq,
	IN				void*				cq_context );

static int32_t
__cm_recv_mgr_filter(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt,
	IN		ib_wc_t* const				p_done_wc_list,
	OUT		cl_qlist_t* const			p_done_list,
	OUT		cl_qlist_t* const			p_bad_list );

static BOOLEAN
__cm_recv_internal(
	IN		const	ib_cq_handle_t		h_cq,
	IN				void*				cq_context,
	IN				uint32_t			*p_recv_cnt );

static void
__cm_recv_cb(
	IN		const	ib_cq_handle_t		h_cq,
	IN				void*				cq_context );

/* callback to connect reply */
static void
__conn_reply_cb(
	IN		ib_cm_rep_rec_t*		p_cm_rep );

/* callback on REQ arrival while listen() */
static void
__conn_req_cb(
	IN		ib_cm_req_rec_t*		p_cm_req );

/* MRA callback */
static void
__conn_mra_cb(
	IN		ib_cm_mra_rec_t*		p_mra_rec );

/* RTU callback*/
static void
__conn_rtu_cb(
	IN		ib_cm_rtu_rec_t*		p_rtu_rec );

/*REJ callback */
static void
__conn_rej_cb(
	IN		ib_cm_rej_rec_t*		p_rej_rec );

/* callback on DREQ (Disconnect Request) arrival */
static void
__active_conn_dreq_cb(
	 IN	ib_cm_dreq_rec_t*			p_dreq_rec );

static void
__passive_conn_dreq_cb(
	 IN	ib_cm_dreq_rec_t*			p_dreq_rec );

static ib_api_status_t
__conn_accept(
	IN		ipoib_port_t* const		p_port,
	IN		ipoib_endpt_t*			p_endpt,
	IN		ib_cm_req_rec_t*		p_cm_req,
	IN		ib_recv_wr_t*			p_recv_wr );

static void
__conn_reject(
	IN		ipoib_port_t* const		p_port,
	IN		ib_cm_handle_t			h_cm_handle,
	IN		ib_rej_status_t			rej_status );

static void
__conn_send_dreq(
	IN		ipoib_port_t*	const	p_port,
	IN		ipoib_endpt_t*	const	p_endpt );

static void
__cq_async_event_cb(
	IN			ib_async_event_rec_t		*p_event_rec );

static void
__srq_qp_async_event_cb(
	IN		ib_async_event_rec_t		*p_event_rec );

static void
__queue_tx_resource_free(
	IN		ipoib_port_t*		p_port,
	IN		ipoib_endpt_t*		p_endpt );


static void
__endpt_cm_buf_mgr_construct(
	IN		cm_buf_mgr_t * const		p_buf_mgr );

static cl_status_t
__cm_recv_desc_ctor(
	IN		void* const					p_object,
	IN		void*						context,
	OUT		cl_pool_item_t** const		pp_pool_item );

static void
__cm_recv_desc_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void						*context );

static boolean_t
__cm_recv_is_dhcp(
	IN		const ipoib_pkt_t* const	p_ipoib );

static ib_api_status_t
__endpt_cm_recv_arp(
	IN		ipoib_port_t* const			p_port,
	IN		const	ipoib_pkt_t* const	p_ipoib,
	OUT		eth_pkt_t* const			p_eth,
	IN		ipoib_endpt_t* const		p_src_endpt );

static ib_api_status_t
__endpt_cm_recv_udp(
	IN		ipoib_port_t* const			p_port,
	IN		ib_wc_t* const				p_wc,
	IN		const	ipoib_pkt_t* const	p_ipoib,
	OUT		eth_pkt_t* const			p_eth,
	IN		ipoib_endpt_t* const		p_src_endpt );

static void
cm_start_conn_destroy(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt,
	IN		int							which_res );

void
endpt_queue_cm_connection(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt );

void
cm_release_resources(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt,
	IN		int							which_res );


static BOOLEAN
cm_start_conn_destruction( 
	IN			ipoib_port_t* const			p_port,
	IN			ipoib_endpt_t* const		p_endpt );



char *get_eth_packet_type_str(net16_t pkt_type)
{
	static char what[28];

	switch( pkt_type )
	{
	  case ETH_PROT_TYPE_IP:  
		return "ETH_PROT_IP";

	  case ETH_PROT_TYPE_IPV6:  
		return "ETH_PROT_IPV6";

	  case ETH_PROT_TYPE_ARP:  
		return "ETH_PROT_ARP";

	  case ETH_PROT_TYPE_RARP:  
		return "ETH_PROT_RARP";

	  case ETH_PROT_VLAN_TAG:  
		return "ETH_PROT_VLAN_TAG";

	  default:
		break;
	}
	StringCchPrintf(what,sizeof(what),"Unknown Eth packet type 0x%x",pkt_type);
	return what;
}


char *cm_get_state_str( cm_state_t s )
{
	static char what[28];

	switch( s )
	{
	  case IPOIB_CM_DISCONNECTED:
		return "CM_DISCONNECTED";
	  case IPOIB_CM_QUEUED_TO_CONNECT:
		return "CM_QUEUED_TO_CONNECT";
	  case IPOIB_CM_CONNECTING:
		return "CM_CONNECTING";
	  case IPOIB_CM_CONNECTED:
		return "CM_CONNECTED";
	  case IPOIB_CM_LISTEN:
		return "CM_LISTEN";
	  case IPOIB_CM_DREP_SENT:
		return "CM_DREP_SENT";
	  case IPOIB_CM_DREQ_SENT:
		return "CM_DREQ_SENT";
	  case IPOIB_CM_DISCONNECT_CLEANUP:
		return "CM_DISCONNECT_CLEANUP";
	  case IPOIB_CM_DESTROY:
		return "CM_DESTROY";
	  default:
		break;
	}
	//_snprintf(what,sizeof(what),"Unknown CM state %d(%#x)",s,s);
	(void) StringCchPrintf(what,sizeof(what),"Unknown CM state %d(%#x)",s,s);
	return what;
}

#if EXTRA_DBG

void
decode_enet_pkt(char *preFix, void *hdr, int len, char *postFix)
{
	eth_hdr_t *eh=(eth_hdr_t*)hdr;
	ip_hdr_t *p_ip_hdr =(ip_hdr_t*)(eh + 1);
	char ipp[120];
	char ip_addrs[60];
	char ip_src[16], ip_dst[16];

#if !defined(DBG) || DBG == 0
	UNREFERENCED_PARAMETER(preFix);
	UNREFERENCED_PARAMETER(postFix);
	UNREFERENCED_PARAMETER(len);
#endif

	if (eh->type == ETH_PROT_TYPE_IP)
	{

		ip_addrs[0] = '\0';
		//if (p_ip_hdr->prot == IP_PROT_TCP)
		{
			RtlIpv4AddressToStringA( (IN_ADDR*)&p_ip_hdr->src_ip, ip_src );
			RtlIpv4AddressToStringA( (IN_ADDR*)&p_ip_hdr->dst_ip, ip_dst );

			StringCchPrintf( ip_addrs, sizeof(ip_addrs), " %s --> %s",
							 ip_src, ip_dst );
		}
		StringCchPrintf( ipp, sizeof(ipp), "IP_proto(len %d) %s%s",
				cl_ntoh16(p_ip_hdr->length),
				get_IP_protocol_str(p_ip_hdr->prot),
				(ip_addrs[0] ? ip_addrs : "") );
	}
	else
	{
		StringCchPrintf( ipp, sizeof(ipp), "?Unknown Eth proto %#x? ", eh->type);
	}

	cl_dbg_out("%sEnet hdr(calc pkt_len %d):\n\tsrc MAC: %s\n"
				"\tdst MAC: %s\n\tEnet-proto: %s\n\t%s%s",
				(preFix ? preFix:"\n"),
				len,
				mk_mac_str(&eh->src),
				mk_mac_str2(&eh->dst),
				get_eth_packet_type_str(eh->type),
				ipp,
				(postFix ? postFix:"\n") );
}
#endif


static ib_api_status_t
__cm_create_qp( 
	IN		ipoib_port_t*			p_port,
	IN		ipoib_endpt_t*	const	p_endpt,
	IN 		boolean_t				send_qp )
{
	ib_qp_create_t	create_qp;
	ib_cq_create_t	create_cq;
    ib_api_status_t	ib_status;
	ib_qp_handle_t	h_qp = NULL;

	IPOIB_ENTER( IPOIB_DBG_CM_CONN );

	if( send_qp == TRUE && !p_endpt->conn.h_send_cq )
	{
		memset( &create_cq, 0, sizeof( ib_cq_create_t ) );
		create_cq.size = p_port->p_adapter->params.sq_depth;
		create_cq.pfn_comp_cb = __cm_send_cb;

		ib_status = p_endpt->p_ifc->create_cq( p_port->ib_mgr.h_ca,
											   &create_cq, 
											   p_endpt, 
											   __cq_async_event_cb, 
											   &p_endpt->conn.h_send_cq );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ERR: create send CQ %s\n",
					p_endpt->p_ifc->get_err_str( ib_status )) );
			goto err_exit;
		}
		ipoib_endpt_ref( p_endpt );
	}

	/* Creating a Recv QP? */
	if( send_qp == FALSE && !p_endpt->conn.h_recv_cq )
	{
		memset( &create_cq, 0, sizeof( ib_cq_create_t ) );
		create_cq.size = p_port->p_adapter->params.rq_depth;
		create_cq.pfn_comp_cb = __cm_recv_cb;

		ib_status = p_endpt->p_ifc->create_cq( p_port->ib_mgr.h_ca,
											   &create_cq,
											   p_endpt,
											   __cq_async_event_cb,
											   &p_endpt->conn.h_recv_cq );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed Create RECV CQ %s\n",
					p_endpt->p_ifc->get_err_str( ib_status )) );
			goto err_exit;
		}
		ipoib_endpt_ref( p_endpt );
	}

	memset( &create_qp, 0, sizeof( ib_qp_create_t ) );

	create_qp.qp_type = IB_QPT_RELIABLE_CONN;

	if( send_qp == TRUE )
	{
		create_qp.sq_signaled	= TRUE;
		create_qp.h_sq_cq		= p_endpt->conn.h_send_cq;
		create_qp.sq_depth		= p_port->p_adapter->params.sq_depth;
		create_qp.sq_sge		= min( MAX_SEND_SGE, p_port->max_sq_sge_supported );

 		/* not used, IBAL requires a CQ */
		create_qp.h_rq_cq		= p_endpt->conn.h_send_cq;

		DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_CONN,
			("create_qp[Send]: sq_sge %u sq_depth %u\n",
				create_qp.sq_sge, create_qp.sq_depth) );
	}
	else
	{
		create_qp.sq_signaled	= TRUE;
		create_qp.h_rq_cq		= p_endpt->conn.h_recv_cq;
		/* QP create error if Recv Queue attributes set and SRQ attached */

		ASSERT( p_port->ib_mgr.h_srq );
		create_qp.h_srq			= p_port->ib_mgr.h_srq;

		/* not used, IBAL required */
		create_qp.h_sq_cq		= p_endpt->conn.h_recv_cq;

		DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_CONN,
			("create_qp[Recv]: rq_sge %u rq_depth %u\n",
				create_qp.rq_sge, create_qp.rq_depth) );
	}

	ib_status = p_endpt->p_ifc->create_qp( p_port->ib_mgr.h_pd, 
										   &create_qp, 
										   p_endpt,
											__srq_qp_async_event_cb,
										   &h_qp );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Create RC [%s] QP failed status %s\n",
				(send_qp ? "Send" : "Recv"),
				p_endpt->p_ifc->get_err_str( ib_status )) );
		goto err_exit;
	}
	else
	{
		IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_CM_CONN,
			("Created CM %s QP %p for EP %s\n",
				(send_qp ? "Send" : "Recv"), h_qp, p_endpt->tag) );
	}

	if( send_qp )
		p_endpt->conn.h_send_qp = h_qp;
	else
	{
		p_endpt->conn.h_recv_qp = h_qp;
		cl_atomic_inc( &p_port->ib_mgr.srq_qp_cnt );
	}

err_exit:
	if( ib_status != IB_SUCCESS )
	{
		if( !send_qp && p_endpt->conn.h_recv_cq )
		{
			p_endpt->p_ifc->destroy_cq( p_endpt->conn.h_recv_cq, NULL );
			p_endpt->conn.h_recv_cq = NULL;
			ipoib_endpt_deref( p_endpt );
		}

		if( send_qp && p_endpt->conn.h_send_cq )
		{
			p_endpt->p_ifc->destroy_cq( p_endpt->conn.h_send_cq, NULL );
			p_endpt->conn.h_send_cq = NULL;
			ipoib_endpt_deref( p_endpt );
		}
	}

	IPOIB_EXIT( IPOIB_DBG_CM_CONN );
	return ib_status;
}


/*
 * destroy Endpoint RC connection resources.
 * Returns:
 *	TRUE = caller can destroy the endpoint: all RC connection resources released.
 *	FALSE = RC connection release release will destroy the endpoint object later.
 */
BOOLEAN
cm_destroy_conn(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt )
{
	cm_state_t	cm_state;
	BOOLEAN		status;

	IPOIB_ENTER( IPOIB_DBG_CM_DCONN );
	
	cm_state = endpt_cm_get_state( p_endpt );

	IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_CM_DCONN,
		("EP %s %s\n", p_endpt->tag, cm_get_state_str(cm_state)) );

	if( cm_state == IPOIB_CM_QUEUED_TO_CONNECT || cm_state == IPOIB_CM_CONNECTING )
	{
		p_endpt->cm_ep_destroy++;
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port[%d] EP %s CM_CONNECTING - abort connect operation.\n",
				p_port->port_num, p_endpt->tag) );
		return FALSE;
	}

	if( cm_state == IPOIB_CM_DESTROY )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port[%d] EP %s CM_DESTROY - been here before, abort.\n",
				p_port->port_num, p_endpt->tag) );
		return TRUE;
	}

	if( cm_state == IPOIB_CM_DISCONNECTED && !p_endpt->cm_ep_destroy )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_DCONN,
			("Port[%d] EP %s CM_DISCONNECTED (!cm_ep_destroy) - nothing to do.\n",
				p_port->port_num, p_endpt->tag) );
		return TRUE;
	}

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_DCONN,
		("Port[%d] Destroying Endpoint %s MAC %s %s\n",
			p_port->port_num, p_endpt->tag, mk_mac_str(&p_endpt->mac),
			cm_get_state_str(cm_state)) );

	status = cm_start_conn_destruction( p_port, p_endpt );

	IPOIB_EXIT( IPOIB_DBG_CM_DCONN );
	return status;
}


ib_api_status_t
endpt_cm_connect(
	 IN			ipoib_endpt_t* const		p_endpt )
{
	ib_api_status_t		ib_status = IB_SUCCESS;
	ib_cm_req_t			creq;
	ipoib_port_t*		p_port;
	ib_path_rec_t		path_rec;

	IPOIB_ENTER( IPOIB_DBG_CM );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("RC Connecting to MAC %s via EP %s\n",
			mk_mac_str(&p_endpt->mac), p_endpt->tag) );
			
	p_port = ipoib_endpt_parent( p_endpt );

	if( !p_port->p_adapter->params.cm_enabled )
		return IB_UNSUPPORTED;

	if( p_port->p_adapter->state != IB_PNP_PORT_ACTIVE )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("EP %s Port[%d] NOT active\n", p_endpt->tag, p_port->port_num ) );
		return IB_INVALID_STATE;
	}
	if( p_endpt->cm_ep_destroy )
		return IB_INVALID_STATE;

	ib_status = __cm_create_qp( p_port, p_endpt, TRUE );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Endpt %p CM create send QP/CQ failed %s\n",
				p_endpt, p_endpt->p_ifc->get_err_str( ib_status )) );
		return ib_status;
	}
	if( p_endpt->cm_ep_destroy )
		return IB_INVALID_STATE;

	memset( &creq, 0, sizeof(ib_cm_req_t) );
	memset( &path_rec, 0, sizeof(ib_path_rec_t) );

	p_endpt->conn.private_data.ud_qpn = p_port->ib_mgr.qpn;
	p_endpt->conn.private_data.recv_mtu =
		cl_hton32( p_port->p_adapter->params.cm_payload_mtu + sizeof(ipoib_hdr_t) ); 

	creq.svc_id			= p_endpt->conn.service_id;
	creq.max_cm_retries	= 5;
	
	if( ipoib_mac_to_path(p_port, p_endpt->mac, &path_rec) != STATUS_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_mac_to_path failed\n" ) );
		return IB_INVALID_PARAMETER;
	}
	if( p_endpt->cm_ep_destroy )
		return IB_INVALID_STATE;

	creq.p_primary_path = (ib_path_rec_t*)&path_rec;
	creq.p_req_pdata	= (uint8_t *)&p_endpt->conn.private_data;
	creq.req_length		= (uint8_t) sizeof( cm_private_data_t );

	creq.qp_type		= IB_QPT_RELIABLE_CONN;
	creq.h_qp			= p_endpt->conn.h_send_qp;

	//creq.resp_res		= 1;
	//creq.init_depth	= 1;

	creq.remote_resp_timeout = ib_path_rec_pkt_life(&path_rec) + 1;
	creq.flow_ctrl			 = FALSE;	// srq attached qp does not support FC
	creq.local_resp_timeout  = ib_path_rec_pkt_life(&path_rec) + 1;
	creq.rnr_nak_timeout	 = 7;
	creq.rnr_retry_cnt		 = 1; /* IPoIB CM RFC draft warns against retries */
	creq.retry_cnt			 = 1; /* IPoIB CM RFC draft warns against retries */

	//creq.pfn_cm_req_cb	= (ib_pfn_cm_req_cb_t)NULL; no peer connections
	creq.pfn_cm_rep_cb	= __conn_reply_cb;
	creq.pfn_cm_mra_cb	= __conn_mra_cb;
	creq.pfn_cm_rej_cb	= __conn_rej_cb;

	creq.h_al			= p_port->p_adapter->h_al;
	creq.pkey			= path_rec.pkey;

	ib_status = p_endpt->p_ifc->cm_req( &creq );
	if( ib_status != IB_SUCCESS && ib_status != IB_PENDING )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_cm_req failed status %s\n",
				p_endpt->p_ifc->get_err_str( ib_status )) );
	}
#if DBG
	else
	{
		IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_CM,
			("CM REQ sent to EP %s UD QPN: %#x SID: %#I64x\n",
				p_endpt->tag, cl_ntoh32(p_endpt->qpn), p_endpt->conn.service_id) );
	}
#endif

	IPOIB_EXIT( IPOIB_DBG_CM );
	return ib_status;
}

ib_api_status_t
ipoib_port_listen(
	 IN			ipoib_port_t* const		p_port )
{
	ib_api_status_t		ib_status;
	ib_cm_listen_t		cm_listen;

	IPOIB_ENTER( IPOIB_DBG_CM );

	if( !p_port->p_adapter->params.cm_enabled )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
				(" CONNECTED MODE IS NOT ENABLED\n" ) );
		return IB_UNSUPPORTED;
	}

	if( p_port->p_adapter->state != IB_PNP_PORT_ACTIVE )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
				(" Port state IS NOT ACTIVE\n" ) );
		return IB_INVALID_STATE;
	}

	if( !p_port->ib_mgr.h_srq )
	{
		ib_status = ipoib_port_srq_init( p_port );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("SRQ Init failed status %s\n",
			p_port->p_adapter->p_ifc->get_err_str( ib_status )) );
			return ib_status;
		}
	}

	endpt_cm_set_state( p_port->p_local_endpt , IPOIB_CM_LISTEN );

	memset( &cm_listen, 0, sizeof( ib_cm_listen_t ) );
	
	ipoib_addr_set_sid( &cm_listen.svc_id, p_port->ib_mgr.qpn );

	cm_listen.qp_type = IB_QPT_RELIABLE_CONN;
	cm_listen.ca_guid = p_port->p_adapter->guids.ca_guid;
	cm_listen.port_guid = p_port->p_adapter->guids.port_guid.guid;
	cm_listen.lid =  IB_ALL_LIDS;
	cm_listen.pkey = p_port->p_adapter->guids.port_guid.pkey;

	cm_listen.pfn_cm_req_cb = __conn_req_cb;

	p_port->p_local_endpt->conn.service_id = cm_listen.svc_id;

	ib_status = p_port->p_adapter->p_ifc->cm_listen(
										p_port->p_adapter->h_al,
										&cm_listen,
										(const void *)p_port,
										&p_port->p_local_endpt->conn.h_cm_listen );
// listen_done: see above ref.
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("cm_listen failed status %#x\n", ib_status ) );
		
		endpt_cm_buf_mgr_destroy( p_port );
		ipoib_port_srq_destroy( p_port );
	}
#if DBG
	else
	{
		IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_CM,
			("\n\tPort[%d] CREATED LISTEN CEP. SID: %#I64x\n", 
				p_port->port_num, p_port->p_local_endpt->conn.service_id ) );
	}
#endif

	IPOIB_EXIT( IPOIB_DBG_CM );
	return ib_status;
}


ib_api_status_t
ipoib_port_cancel_listen(
	IN	ipoib_endpt_t*	const	p_endpt )
{
	ib_api_status_t	 ibs = IB_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_CM );

	if( p_endpt->conn.h_cm_listen )
	{
		ibs = p_endpt->p_ifc->cm_cancel( p_endpt->conn.h_cm_listen, NULL );
		
		endpt_cm_set_state( p_endpt, IPOIB_CM_DISCONNECTED );

		p_endpt->conn.h_cm_listen = NULL;
	}

	IPOIB_EXIT( IPOIB_DBG_CM );

	return ibs;
}


/*
 * received a connection request (REQ) while listening.
 */
static void
__conn_req_cb(
	IN			ib_cm_req_rec_t				*p_cm_req )
{
	ib_api_status_t		ib_status = IB_ERROR;
	ipoib_endpt_t*		p_endpt;
	ipoib_port_t*		p_port;
	cm_private_data_t	private_data;
	uint32_t			mtu;
	ib_rej_status_t		rej_status = IB_REJ_INSUFFICIENT_RESP_RES;
	cm_state_t			cm_state;
	ib_recv_wr_t*		p_recv_wr=NULL;

	IPOIB_ENTER( IPOIB_DBG_CM );

	CL_ASSERT( p_cm_req );

	p_port = (ipoib_port_t*) p_cm_req->context;
	p_endpt = ipoib_endpt_get_by_gid( p_port, &p_cm_req->primary_path.dgid );
	
	if( !p_endpt )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("No matching Endpoint by gid?\n") );
		return;
	}

	cm_state = endpt_cm_get_state(p_endpt);
	if ( cm_state > IPOIB_CM_LISTEN )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("REQ while EP %s being destroyed, Reject.\n", p_endpt->tag) );
		rej_status = IB_REJ_STALE_CONN;
		goto conn_exit;
	}

	IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_CM,
		("Recv'ed conn REQ in listen() from EP %s\n", p_endpt->tag) );

	if( p_endpt->conn.h_recv_qp )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("EP %s conn.h_recv_qp != null? - Rejecting.\n", p_endpt->tag) );
		// XXX no REJ_CONSUMER defined per spec?
		rej_status = IB_REJ_STALE_CONN;
		goto conn_exit;
	}

	/* copy private data and parse */
	private_data = (*(cm_private_data_t *)p_cm_req->p_req_pdata);

	if( private_data.ud_qpn != p_endpt->qpn )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("EP %s BAD Private_Data, INVALID REMOTE QPN %#x EXPECT %#x, Rejected\n",
				p_endpt->tag, cl_ntoh32( private_data.ud_qpn ),
				cl_ntoh32( p_endpt->qpn ) ));
		rej_status = IB_REJ_INVALID_COMM_ID;
		goto conn_exit;
	}

	if( !p_endpt->conn.service_id )
	{
		p_endpt->cm_flag = IPOIB_CM_FLAG_RC;
		ipoib_addr_set_sid( &p_endpt->conn.service_id, private_data.ud_qpn ); 
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("EP %s service_id set %#I64x QPN %#x\n",
				p_endpt->tag, p_endpt->conn.service_id,
				cl_ntoh32( private_data.ud_qpn )) );
	}

	mtu = p_port->p_adapter->params.cm_payload_mtu + sizeof(ipoib_hdr_t);

	if( cl_ntoh32( private_data.recv_mtu ) > mtu )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("EP %s INVALID REMOTE MTU: %u. MAX EXPECT: %u, Rejected\n", 
				p_endpt->tag, cl_ntoh32( private_data.recv_mtu ), mtu) );
		rej_status = IB_REJ_INVALID_MTU;
		goto conn_exit;
	}

	DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("EP %s MTU: REMOTE %u. Local %u\n", 
			p_endpt->tag, cl_ntoh32( private_data.recv_mtu ), mtu) );

	ib_status = __cm_create_qp( p_port, p_endpt, FALSE );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("EP %s CM create recv QP failed, Reject\n", p_endpt->tag ) );
		rej_status = IB_REJ_INSUF_RESOURCES;
		goto conn_exit;
	}

	ib_status = __cm_post_srq_recv( p_port, p_endpt );

	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("failed to Post recv WRs\n" ) );
		goto conn_exit;
	}

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("Rx CM REQ(Accepting) port[%d] EP %s %s\n",
			p_port->port_num, p_endpt->tag,
			cm_get_state_str(endpt_cm_get_state(p_endpt))) );

	ib_status = __conn_accept( p_port, p_endpt, p_cm_req, p_recv_wr );

	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("EP %s CM accept failed '%s'\n",
				p_endpt->tag, p_endpt->p_ifc->get_err_str(ib_status)) );
		goto conn_exit2; /* IBAL has already rejected REQ */
	}

	ib_status = p_endpt->p_ifc->rearm_cq( p_endpt->conn.h_recv_cq, FALSE );

	if( ib_status == IB_SUCCESS )
	{
		IPOIB_EXIT( IPOIB_DBG_CM );
		return;
	}

	IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
		("rearm Recv CQ failed status %s\n",
			p_endpt->p_ifc->get_err_str(ib_status)) );

conn_exit:

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("REJECTING CM Rx connection port[%d] Endpoint %s\n",
			p_port->port_num, p_endpt->tag) );

	__conn_reject( p_port, p_cm_req->h_cm_req, rej_status );

conn_exit2:
	cm_release_resources( p_port, p_endpt, 2 ); // release Rx resources.

	IPOIB_EXIT( IPOIB_DBG_CM );
}


static ib_api_status_t
__conn_accept(
	IN		ipoib_port_t* const		p_port,
	IN		ipoib_endpt_t*			p_endpt,
	IN		ib_cm_req_rec_t			*p_cm_req,
	IN		ib_recv_wr_t*			p_recv_wr )
{
	ib_api_status_t		ib_status;
	ib_cm_rep_t			cm_reply;
	cm_private_data_t	private_data;
	ib_recv_wr_t*		p_failed_wc=NULL;

	IPOIB_ENTER( IPOIB_DBG_CM );

	memset( &cm_reply, 0, sizeof( cm_reply ) );
	
	private_data.ud_qpn = p_port->ib_mgr.qpn;
	private_data.recv_mtu = 
		cl_hton32( p_port->p_adapter->params.cm_payload_mtu + sizeof(ipoib_hdr_t) );

	cm_reply.p_rep_pdata = (uint8_t*)&private_data;
	cm_reply.rep_length = (uint8_t) sizeof( private_data );

	cm_reply.h_qp = p_endpt->conn.h_recv_qp;
	cm_reply.qp_type = IB_QPT_RELIABLE_CONN;

	cm_reply.access_ctrl = IB_AC_LOCAL_WRITE;
	cm_reply.target_ack_delay	= 10;
	cm_reply.failover_accepted = IB_FAILOVER_ACCEPT_UNSUPPORTED;
	cm_reply.flow_ctrl			= p_cm_req->flow_ctrl;
	cm_reply.rnr_nak_timeout	= 7;
	cm_reply.rnr_retry_cnt		= p_cm_req->rnr_retry_cnt;

	cm_reply.pfn_cm_rej_cb		= __conn_rej_cb;
	cm_reply.pfn_cm_mra_cb		= __conn_mra_cb;
	cm_reply.pfn_cm_rtu_cb		= __conn_rtu_cb;
	cm_reply.pfn_cm_dreq_cb		= __passive_conn_dreq_cb;

	if( p_recv_wr )
	{
		cm_reply.p_recv_wr		= p_recv_wr;
		cm_reply.pp_recv_failure = &p_failed_wc;
	}

	ib_status = p_endpt->p_ifc->cm_rep( p_cm_req->h_cm_req, &cm_reply );
	
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("EP %s sending conn-REP, private_data.recv_mtu %d status %#x\n",
			p_endpt->tag, cl_hton32(private_data.recv_mtu), ib_status) );

	IPOIB_EXIT( IPOIB_DBG_CM );
	return ib_status;
}


/* received a CM REPLY in response to our sending a connection REQ, next send RTU. */

static void
__conn_reply_cb(
	IN		ib_cm_rep_rec_t			*p_cm_rep )
{
	ib_api_status_t	ib_status = IB_ERROR;
	ipoib_endpt_t*	p_endpt	;
	ipoib_port_t*	p_port;
	ib_cm_rtu_t		cm_rtu;

	IPOIB_ENTER( IPOIB_DBG_CM );
	CL_ASSERT( p_cm_rep );

	p_endpt = (ipoib_endpt_t* ) p_cm_rep->qp_context;
	if( ! p_endpt )
		return;

	p_port	= ipoib_endpt_parent( p_endpt );
	ASSERT( p_port );
	
	if( endpt_cm_get_state( p_endpt ) != IPOIB_CM_CONNECTING )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port[%d] EP %s Wrong state %s ?, reject - Active conn Abort.\n",
				p_port->port_num, p_endpt->tag,
				cm_get_state_str(endpt_cm_get_state(p_endpt))) );
		goto done;
	}

	ib_status = p_endpt->p_ifc->rearm_cq( p_endpt->conn.h_send_cq, FALSE );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("failed Rearm CM Send CQ %s\n",
				p_endpt->p_ifc->get_err_str(ib_status)) );
		goto done;
	}

	memset( &cm_rtu, 0, sizeof( ib_cm_rtu_t ) );
	cm_rtu.access_ctrl = IB_AC_LOCAL_WRITE;
	cm_rtu.pfn_cm_dreq_cb = __active_conn_dreq_cb;
	cm_rtu.p_rtu_pdata = (uint8_t*)&p_endpt->conn.private_data;
	cm_rtu.rtu_length = sizeof( cm_private_data_t );

	ib_status = p_endpt->p_ifc->cm_rtu(p_cm_rep->h_cm_rep, &cm_rtu );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Endpoint %s DLID %#x Connect failed (cm_rtu) status %s\n",
				p_endpt->tag, cl_ntoh16(p_endpt->dlid),
				p_endpt->p_ifc->get_err_str(ib_status)) );
		goto done;
	}

	/* somebody else want this EP to go away? */
	if( endpt_cm_get_state( p_endpt ) != IPOIB_CM_CONNECTING )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Endpoint %s Connect Aborted\n", p_endpt->tag) );
		ib_status = IB_INVALID_STATE;
		goto done;
	}

	cl_obj_lock( &p_port->obj );
	cl_fmap_insert( &p_port->endpt_mgr.conn_endpts, 
					&p_endpt->dgid, 
					&p_endpt->conn_item );
	cl_obj_unlock( &p_port->obj );

	p_endpt->tx_mtu = p_port->p_adapter->params.cm_payload_mtu + sizeof(ipoib_hdr_t); 
	endpt_cm_set_state(p_endpt, IPOIB_CM_CONNECTED);

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("Active RC CONNECTED to EP %s\n", p_endpt->tag) );
done:
	if( ib_status != IB_SUCCESS )
	{
		__conn_reject( p_port, p_cm_rep->h_cm_rep, IB_REJ_INSUF_RESOURCES );
		if( !p_endpt->cm_ep_destroy )
		{
			cm_release_resources( p_port, p_endpt, 1 );
			endpt_cm_set_state( p_endpt, IPOIB_CM_DISCONNECTED );
		}
	}

	IPOIB_EXIT( IPOIB_DBG_CM );
}


static void
__conn_mra_cb(
	IN		ib_cm_mra_rec_t			*p_mra_rec )
{
	IPOIB_ENTER( IPOIB_DBG_CM );
	UNUSED_PARAM( p_mra_rec );
	IPOIB_EXIT( IPOIB_DBG_CM );
}

/* RTU (Ready To Use) CM message arrived for passive/listen() connection, after this
 * this side has sent a CM Reply message.
 */

static void
__conn_rtu_cb(
	IN		ib_cm_rtu_rec_t			*p_rtu_rec )
{
	ipoib_endpt_t*	p_endpt;
	ipoib_port_t*	p_port;

	IPOIB_ENTER( IPOIB_DBG_CM );
	
	CL_ASSERT( p_rtu_rec );
	p_endpt = (ipoib_endpt_t *)p_rtu_rec->qp_context;
	CL_ASSERT( p_endpt );
	p_port = ipoib_endpt_parent( p_endpt );
	CL_ASSERT( p_port );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("RTU arrived: Passive RC Connected to EP %s posted %d Active RC %s\n",
			p_endpt->tag,
			p_port->cm_buf_mgr.posted,
			cm_get_state_str(endpt_cm_get_state(p_endpt))) );

	if ( endpt_cm_get_state(p_endpt) == IPOIB_CM_DISCONNECTED )
		endpt_queue_cm_connection( p_port, p_endpt );

	IPOIB_EXIT( IPOIB_DBG_CM );
}


void
endpt_queue_cm_connection(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt )
{
	if( p_endpt->cm_flag != IPOIB_CM_FLAG_RC )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Unable to queue EP %s for RC connection: EP not CM capable?\n",
				p_endpt->tag) );
		return;
	}

	if( !p_endpt->conn.service_id )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Unable to queue EP %s for RC connection: service_id not set?\n",
				p_endpt->tag) );
		return;
	}

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
		("Queue EP %s for Active RC connection\n", p_endpt->tag) );

	endpt_cm_set_state( p_endpt, IPOIB_CM_QUEUED_TO_CONNECT );

	/* Queue for endpt mgr to RC connect */
	NdisInterlockedInsertTailList( &p_port->endpt_mgr.pending_conns, 
								   &p_endpt->list_item, 
								   &p_port->endpt_mgr.conn_lock );

	cl_event_signal( &p_port->endpt_mgr.event );
}


/*
 * Queue endpt for CM Tx resource release, endpoint remains UD valid.
 */
static void
__queue_tx_resource_free(
	IN		ipoib_port_t*		p_port,
	IN		ipoib_endpt_t*		p_endpt )
{
	cm_state_t	old_state;
	
	ASSERT( p_port );
	p_endpt->tx_mtu = p_port->p_adapter->params.payload_mtu;
	old_state = endpt_cm_set_state( p_endpt, IPOIB_CM_DISCONNECT_CLEANUP );
	if( old_state == IPOIB_CM_CONNECTED )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("EP %s previous stat %s \n",
				p_endpt->tag, cm_get_state_str(old_state)) );
	}

	if( !p_port->endpt_mgr.thread_is_done ) 
	{
		DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("Queue EP %s for CM Tx resource cleanup\n", p_endpt->tag) );

		NdisInterlockedInsertTailList( &p_port->endpt_mgr.remove_conns, 
									   &p_endpt->list_item,
									   &p_port->endpt_mgr.remove_lock );
		cl_event_signal( &p_port->endpt_mgr.event );
	}
	else
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("EP %s for CM Tx resource cleanup, EP thread not running?\n",
				p_endpt->tag) );
	}
}


static void
__conn_rej_cb(
	IN		ib_cm_rej_rec_t			*p_rej_rec )
{
	ipoib_endpt_t*	p_endpt;
	ipoib_port_t*	p_port;

	IPOIB_ENTER( IPOIB_DBG_CM );

	CL_ASSERT( p_rej_rec );

	p_endpt	= (ipoib_endpt_t* )p_rej_rec->qp_context;
	p_port = ipoib_endpt_parent( p_endpt );

	CL_ASSERT( p_endpt->conn.h_send_qp == p_rej_rec->h_qp ||
				p_endpt->conn.h_recv_qp == p_rej_rec->h_qp );

	if( p_rej_rec->rej_status == IB_REJ_USER_DEFINED )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Connect REQ Rejected User defined ARI: %d\n",
				((uint16_t)(*(p_rej_rec->p_ari+1)))) );
	}
	else
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
		("Connect REQ Rejected Status: %d\n", cl_ntoh16( p_rej_rec->rej_status )) );
	}

	/* endpt not RC connected, release active (Tx) resources */
	cm_release_resources( p_port, p_endpt, 1 );
	endpt_cm_set_state( p_endpt, IPOIB_CM_DISCONNECTED );

	IPOIB_EXIT( IPOIB_DBG_CM );
}


/* received a conn-DREQ, send conn-DREP */

static void
__active_conn_dreq_cb(
	 IN	ib_cm_dreq_rec_t			*p_dreq_rec )
{
	ib_api_status_t		ib_status;
	ib_cm_drep_t		cm_drep;
	ipoib_endpt_t*		p_endpt	;
	ipoib_port_t*		p_port;
	cm_state_t			cm_state;

	IPOIB_ENTER( IPOIB_DBG_CM );

	CL_ASSERT( p_dreq_rec );

	p_endpt	= (ipoib_endpt_t *)p_dreq_rec->qp_context;
	
	if( !p_endpt )
		return;

	cm_state = endpt_cm_get_state( p_endpt );

	DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("Received DREQ for EP %s %s, return to CM_DISCONNECTED.\n",
			p_endpt->tag, cm_get_state_str(cm_state) ) );

	p_port = ipoib_endpt_parent( p_endpt );
	ASSERT( p_port );
	p_endpt->tx_mtu = p_port->p_adapter->params.payload_mtu;

	if( cm_state == IPOIB_CM_CONNECTED )
	{
		cm_state = endpt_cm_set_state( p_endpt, IPOIB_CM_DREP_SENT );
	
		cm_drep.drep_length = 0;
		cm_drep.p_drep_pdata = NULL;

		ib_status = p_endpt->p_ifc->cm_drep( p_dreq_rec->h_cm_dreq, &cm_drep );
		if ( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed DREP send EP %s %s\n",
					p_endpt->tag, p_endpt->p_ifc->get_err_str(ib_status)) );
		}
#if DBG
		else
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
				("DREP sent to EP %s prev-cstate %s\n",
					p_endpt->tag, cm_get_state_str(cm_state)) );
		}
#endif
		cl_obj_lock( &p_port->obj );
		endpt_unmap_conn_dgid( p_port, p_endpt);
		cl_obj_unlock( &p_port->obj );
	}
	cm_release_resources( p_port, p_endpt, 1 );
	endpt_cm_set_state( p_endpt, IPOIB_CM_DISCONNECTED );

	IPOIB_EXIT( IPOIB_DBG_CM );
}


/* CM conn state is ONLY for an Active connection item. Passive/listen() connections
 * are identified by a non-null endpt->conn.h_recv_qp.
 */
static void
__passive_conn_dreq_cb(
	 IN	ib_cm_dreq_rec_t			*p_dreq_rec )
{
	ib_api_status_t		ib_status;
	ib_cm_drep_t		cm_drep;
	ipoib_endpt_t*		p_endpt	;
	ipoib_port_t*		p_port;

	IPOIB_ENTER( IPOIB_DBG_CM );

	CL_ASSERT( p_dreq_rec );

	p_endpt	= (ipoib_endpt_t *)p_dreq_rec->qp_context;
	
	if( !p_endpt )
		return;

	DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("Received DREQ for EP %s Passive conn [%s], release Rx resources.\n",
			p_endpt->tag, cm_get_state_str(endpt_cm_get_state(p_endpt))) );

	p_port = ipoib_endpt_parent( p_endpt );
	ASSERT( p_port );

	cm_drep.drep_length = 0;
	cm_drep.p_drep_pdata = NULL;

	ib_status = p_endpt->p_ifc->cm_drep( p_dreq_rec->h_cm_dreq, &cm_drep );
	if ( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed DREP send %s\n",
				p_endpt->p_ifc->get_err_str(ib_status)) );
	}
	cm_release_resources( p_port, p_endpt, 2 );	// release Rx resources.

	IPOIB_EXIT( IPOIB_DBG_CM );
}

/*
 * If send & recv QPs are present they are in the ERROR state.
 */
void
cm_destroy_recv_resources(
	IN				ipoib_port_t* const		p_port,
	IN				ipoib_endpt_t* const	p_endpt )
{
	ib_api_status_t			ib_status = IB_SUCCESS;
	ib_wc_t					*p_done_wc;
	ib_wc_t					wc[MAX_CM_RECV_WC];
	ib_wc_t					*p_free_wc;
	ib_wc_t					*p_wc;
	ipoib_cm_recv_desc_t	*p_desc;
	int						flush_cnt=0;
	int						loops;

	IPOIB_ENTER( IPOIB_DBG_CM_DCONN );

	p_endpt->cm_rx_flushing = FALSE;

	if( p_endpt->conn.h_recv_qp )	
	{
		BOOLEAN dispatch;

		for( p_free_wc=wc; p_free_wc < &wc[MAX_CM_RECV_WC - 1]; p_free_wc++ )
			p_free_wc->p_next = p_free_wc + 1;
		p_free_wc->p_next = NULL;

		dispatch = (KeGetCurrentIrql() == DISPATCH_LEVEL); 
		loops = p_port->p_adapter->params.rq_depth;
		do
		{
			p_free_wc = wc;
			ib_status = p_endpt->p_ifc->poll_cq( p_endpt->conn.h_recv_cq, 
												 &p_free_wc,
												 &p_done_wc );

			if( ib_status != IB_SUCCESS && ib_status != IB_NOT_FOUND )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Poll Recv CQ failed: %s\n",
						p_endpt->p_ifc->get_err_str( ib_status )) );
				loops = 1;
				break;
			}
			if( ib_status == IB_SUCCESS )
			{
				cl_spinlock_acquire( &p_port->recv_lock );
				for( p_wc = p_done_wc; p_wc; p_wc = p_wc->p_next )
				{
					p_desc = (ipoib_cm_recv_desc_t *)(uintn_t)p_wc->wr_id;
					__cm_buf_mgr_put_recv( p_port, p_desc, TRUE, NULL );
					flush_cnt++;
				}
				cl_spinlock_release( &p_port->recv_lock );
			}
			else if( !dispatch )
					cl_thread_suspend(0);

		} while( --loops > 0 );

		if( flush_cnt > 0 )
		{
			IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
				("Flushed %d Passive RC recv buffers, destroying recv QP\n",
					flush_cnt) );
		}

		ib_status = p_endpt->p_ifc->destroy_qp( p_endpt->conn.h_recv_qp, NULL );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Destroy Recv QP failed %s\n",
					p_endpt->p_ifc->get_err_str( ib_status )) );
		}
		p_endpt->conn.h_recv_qp = NULL;
		cl_atomic_dec( &p_port->ib_mgr.srq_qp_cnt );
	}

	if( p_endpt->conn.h_recv_cq )
	{
		ib_status = p_endpt->p_ifc->destroy_cq( p_endpt->conn.h_recv_cq, NULL );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Destroy Recv CQ failed: %s\n",
					p_endpt->p_ifc->get_err_str( ib_status )) );
		}
		p_endpt->conn.h_recv_cq = NULL;
		ipoib_endpt_deref( p_endpt );
	}

	dmp_ipoib_port_refs( p_port, "cm_destroy_recv_resources()" );

	DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_DCONN,
		("Port[%u] EP %s Rx resources released.\n",
			p_port->port_num, p_endpt->tag) );

	IPOIB_EXIT( IPOIB_DBG_CM_DCONN );
}


/* Transition QP into ERR state
 * which_res:
 * -1 == both Tx & Rx QPs
 *	0 == both Tx & Rx QPs resources
 *	1 == only Tx QP
 *	2 == only Rx QP
 *
 * Side Effects:
 *  cm_rx_flushing == TRUE, expect async error callback to destroy Rx resouces.
 */
static void
cm_start_conn_teardown(
	IN			ipoib_port_t* const			p_port,
	IN			ipoib_endpt_t* const		p_endpt,
	IN			int							which_res )
{
	ib_api_status_t	ib_status;
	ib_qp_mod_t		mod_attr;

	ASSERT( p_port );
	ASSERT( p_endpt );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_DCONN,
		("Port[%d] EP %s which_res %d\n",
			p_port->port_num, p_endpt->tag, which_res) );

	cl_obj_lock( &p_endpt->obj );

	if( (which_res == 0 || which_res == 1) && p_endpt->conn.h_send_qp )
	{
		ib_qp_handle_t h_send_qp = p_endpt->conn.h_send_qp;

		p_endpt->conn.h_send_qp = NULL;	// prevent Tx on invalid QP
		p_endpt->conn.h_send_qp_err = h_send_qp;	// save for later destroy.

		memset( &mod_attr, 0, sizeof( mod_attr ) );
		mod_attr.req_state = IB_QPS_ERROR;
		ib_status = p_endpt->p_ifc->modify_qp( h_send_qp, &mod_attr );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("modify_qp(send: IB_QPS_ERROR) %s\n",
					p_endpt->p_ifc->get_err_str( ib_status )) );
		}
	}

	if( (which_res == 0 || which_res == 2) && p_endpt->conn.h_recv_qp )	
	{
		//if( !p_endpt->cm_rx_flushing )
		{
			p_endpt->cm_rx_flushing++;
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_CM_DCONN,
				("EP %s FLUSHING\n", p_endpt->tag) );
			memset( &mod_attr, 0, sizeof( mod_attr ) );
			mod_attr.req_state = IB_QPS_ERROR;
			ib_status = p_endpt->p_ifc->modify_qp( p_endpt->conn.h_recv_qp,
												   &mod_attr );
			if( ib_status != IB_SUCCESS )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("modify_qp(recv:IB_QPS_ERROR) %s\n",
						p_endpt->p_ifc->get_err_str( ib_status )) );
			}
			/* QP async error handler will finish the Rx QP destruction task */
		}
	}
	cl_obj_unlock( &p_endpt->obj );
}

/*
 * begin RC connection destruction.
 * If the Rx Qp exists, then the endpoint object destruction is delayed until
 * the QP's async event callback fires on flushed CQ; then destroy.
 * Returns:
 *  TRUE - caller can destroy the endpoint obj.
 *  FALSE - Rx CQ async callback will destroy endpoint object.
 */
static BOOLEAN
cm_start_conn_destruction( 
	IN			ipoib_port_t* const			p_port,
	IN			ipoib_endpt_t* const		p_endpt )
{
	BOOLEAN		status=TRUE;

	IPOIB_ENTER( IPOIB_DBG_CM_DCONN );

	cl_obj_lock( &p_port->obj );
	endpt_unmap_conn_dgid( p_port, p_endpt );
	cl_obj_unlock( &p_port->obj );

	cm_release_resources( p_port, p_endpt, 1 );	// release Tx now.

	ASSERT( !p_endpt->conn.h_send_qp );
	ASSERT( !p_endpt->conn.h_send_qp_err );
	ASSERT( !p_endpt->conn.h_send_cq );

	if( p_endpt->conn.h_recv_qp )
	{
		// flag endpoint object is to be destroyed in Rx qp async error handler.
		p_endpt->cm_ep_destroy++;
		status = FALSE;
		cm_start_conn_teardown( p_port, p_endpt, 2 );	// release Rx 
	}
	else
	{
		cm_destroy_recv_resources( p_port, p_endpt );
		ASSERT( !p_endpt->conn.h_recv_qp );
		ASSERT( !p_endpt->conn.h_recv_cq );
	}
	IPOIB_EXIT( IPOIB_DBG_CM_DCONN );
	return status;
}


/* release/free an EP's CM resources:
 * which_res: (<0 implies rouine called from async error handler).
 * -2 == only Rx/Passive resources, Rx QP in ERROR state.
 * -1 == both Tx & Rx CM resources, TX & Rx QPs in ERROR state.
 *	0 == both Tx & Rx CM resources
 *	1 == only Tx/Active resources
 *	2 == only Rx/Passive resources
 */
void
cm_release_resources(
	IN				ipoib_port_t* const		p_port,
	IN				ipoib_endpt_t* const	p_endpt,
	IN				int						which_res )
{
	ib_api_status_t	ib_status = IB_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_CM_DCONN );

	ASSERT( p_port );
	ASSERT( p_endpt );

	if( which_res >= 0 )
	{
		cm_start_conn_teardown( p_port, p_endpt, which_res);
		cl_obj_lock( &p_endpt->obj );
	}

	if( which_res == -1 || which_res == 1 )
	{
		ib_qp_handle_t h_send_qp = (p_endpt->conn.h_send_qp
									? p_endpt->conn.h_send_qp
									: p_endpt->conn.h_send_qp_err);
		if( h_send_qp )
		{
			ib_status = p_endpt->p_ifc->destroy_qp( h_send_qp, NULL );
			if( ib_status != IB_SUCCESS )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Destroy RC Send QP failed: %s\n",
						p_endpt->p_ifc->get_err_str( ib_status )) );
			}
			p_endpt->conn.h_send_qp = p_endpt->conn.h_send_qp_err = NULL;
		}
		p_endpt->tx_mtu = p_port->p_adapter->params.payload_mtu;

		if( p_endpt->conn.h_send_cq )
		{
			ib_status = p_endpt->p_ifc->destroy_cq( p_endpt->conn.h_send_cq, NULL );
			if( ib_status != IB_SUCCESS )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Destroy Send CQ failed status %s\n",
						p_endpt->p_ifc->get_err_str( ib_status )) );
			}
			p_endpt->conn.h_send_cq = NULL;
			ipoib_endpt_deref( p_endpt );
			DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_DCONN,
				("Port[%u] EP %s Tx resources released.\n",
					p_port->port_num, p_endpt->tag) );
		}
	}

	/* see srq_async */
	if( which_res < 0 )
		cm_destroy_recv_resources( p_port, p_endpt );
	else
		cl_obj_unlock( &p_endpt->obj );

	IPOIB_EXIT( IPOIB_DBG_CM_DCONN );
}


static void
__cm_send_cb(
	IN		const	ib_cq_handle_t			h_cq,
	IN				void					*cq_context )
{
	ipoib_port_t		*p_port;
	ib_api_status_t		ib_status;
	ib_wc_t				wc[MAX_SEND_WC], *p_wc, *p_free;
	cl_qlist_t			done_list;
	ipoib_endpt_t		*p_endpt;
	ib_api_status_t		send_failed = IB_SUCCESS;
	ip_stat_sel_t		type;
	NET_BUFFER			*p_netbuffer = NULL;
	ipoib_send_NB_SG	*s_buf;
	cl_perf_t			*perf;
	ib_al_ifc_t			*p_ibal;
	NDIS_STATUS			status = NDIS_STATUS_FAILURE;
	cl_qlist_t			complete_list;

	PERF_DECLARE( CMSendCompBundle );
	PERF_DECLARE( CMSendCb );
	PERF_DECLARE( CMPollSend );
	PERF_DECLARE( CMFreeSendBuf );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	cl_perf_clr( CMSendCompBundle );
	cl_perf_start( CMSendCb );
	cl_qlist_init( &done_list );
	cl_qlist_init( &complete_list );

	p_endpt = (ipoib_endpt_t *)cq_context;
	ASSERT( p_endpt );
	ipoib_endpt_ref( p_endpt );

#if DBG
	if( h_cq ) { ASSERT( h_cq == p_endpt->conn.h_send_cq ); }
#else
	UNUSED_PARAM( h_cq );
#endif

	p_port = ipoib_endpt_parent( p_endpt );
	perf = &p_port->p_adapter->perf;
	p_ibal = p_port->p_adapter->p_ifc;

	for( p_free=wc; p_free < &wc[MAX_SEND_WC - 1]; p_free++ )
		p_free->p_next = p_free + 1;
	p_free->p_next = NULL;

	cl_spinlock_acquire( &p_port->send_lock );

	do
	{
		p_free = wc;
		cl_perf_start( CMPollSend );
		ib_status = p_ibal->poll_cq( h_cq, &p_free, &p_wc );
		cl_perf_stop( perf, CMPollSend );
		CL_ASSERT( ib_status == IB_SUCCESS || ib_status == IB_NOT_FOUND );

		while( p_wc )
		{
			CL_ASSERT(p_wc->status != IB_WCS_SUCCESS || p_wc->wc_type == IB_WC_SEND);
			s_buf = (ipoib_send_NB_SG*)(uintn_t)p_wc->wr_id;
			CL_ASSERT( s_buf );

			DIPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
				("RC send completion: s_buf %p s_buf->p_nbl %p endpt %s\n",
					s_buf, s_buf->p_nbl, s_buf->p_endpt->tag) );

			ASSERT( p_endpt == s_buf->p_endpt );

			status = NDIS_STATUS_FAILURE;

			switch( p_wc->status )
			{
			case IB_WCS_SUCCESS:
				if( p_endpt->h_mcast )
				{
					if( p_endpt->dgid.multicast.raw_group_id[11] == 0xFF &&
						p_endpt->dgid.multicast.raw_group_id[10] == 0xFF &&
						p_endpt->dgid.multicast.raw_group_id[12] == 0xFF &&
						p_endpt->dgid.multicast.raw_group_id[13] == 0xFF )
					{
						type = IP_STAT_BCAST_BYTES;
					}
					else
						type = IP_STAT_MCAST_BYTES;
				}
				else
					type = IP_STAT_UCAST_BYTES;

				p_netbuffer = s_buf->p_curr_nb;
				ipoib_inc_send_stat( p_port->p_adapter, type,
										NET_BUFFER_DATA_LENGTH(p_netbuffer) );	
				status = NDIS_STATUS_SUCCESS;
				break;

			case IB_WCS_WR_FLUSHED_ERR:
				IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
					("RC Flushed send completion.\n") );
					ipoib_inc_send_stat(p_port->p_adapter, IP_STAT_DROPPED, 0);
				status = NDIS_STATUS_RESET_IN_PROGRESS;
				if( !send_failed )
					send_failed = (ib_api_status_t)p_wc->status;
				break;

			default:
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("RC Send failed with %s (vendor specific %#x)\n",
					p_ibal->get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific) );
				ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_ERROR, 0 );
				status = NDIS_STATUS_FAILURE;
				send_failed = (ib_api_status_t)p_wc->status;
			}
			
			cl_perf_start( CMFreeSendBuf );
			__send_complete_add_to_list( &complete_list ,s_buf, status );
			cl_perf_stop( perf, CMFreeSendBuf );
			
			cl_atomic_dec( &p_port->send_mgr.depth );

			p_wc = p_wc->p_next;
			cl_perf_inc( CMSendCompBundle );
		}
		/* If we didn't use up every WC, break out. */
	} while( !p_free );

	if ( send_failed )
	{
		/* revert to UD only */
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port[%d] start CM Tx resources release EP %s\n",
				p_port->port_num, p_endpt->tag) );
		p_endpt->conn.h_send_qp_err = p_endpt->conn.h_send_qp; // for later destroy.
		p_endpt->conn.h_send_qp = NULL;	// prevent Tx on invalid QP
		__queue_tx_resource_free( p_port, p_endpt );
		endpt_cm_set_state( p_endpt, IPOIB_CM_DISCONNECT_CLEANUP );
	}
	else
	{
		/* Rearm the CQ. */
		ib_status = p_ibal->rearm_cq( h_cq, FALSE );
		CL_ASSERT( ib_status == IB_SUCCESS );
	}

	ipoib_endpt_deref( p_endpt );

	cl_perf_stop( perf, CMSendCb );

	cl_perf_update_ctr( perf, CMSendCompBundle );

	cl_spinlock_release( &p_port->send_lock );
	send_complete_list_complete ( &complete_list,
									NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL );

	IPOIB_EXIT( IPOIB_DBG_SEND );
}


static inline NET_BUFFER_LIST*
__cm_buf_mgr_get_NBL(
		IN				ipoib_port_t* const			p_port,
		IN				ipoib_cm_recv_desc_t* const	p_desc )
{
	NET_BUFFER_LIST		*p_net_buffer_list;
	MDL					*p_mdl;


	PNET_BUFFER			NetBuffer;
#if !DBG
	UNUSED_PARAM( p_port );
#endif
	
	CL_ASSERT( p_desc->p_NBL );
	CL_ASSERT( p_desc->p_mdl );
	CL_ASSERT( p_port == IPOIB_PORT_FROM_NBL( p_desc->p_NBL ) );

	p_net_buffer_list = p_desc->p_NBL;

	NetBuffer = NET_BUFFER_LIST_FIRST_NB( p_net_buffer_list );
	p_mdl = NET_BUFFER_FIRST_MDL( NetBuffer );	

	CL_ASSERT( p_mdl == p_desc->p_mdl );
	CL_ASSERT( NET_BUFFER_CURRENT_MDL( NetBuffer ) == p_mdl );

	NET_BUFFER_DATA_LENGTH( NetBuffer ) = p_desc->len;
	NdisAdjustMdlLength( p_mdl, p_desc->len );

	return p_net_buffer_list;
}


static ib_api_status_t
__cm_recv_mgr_prepare_NBL(
	IN			ipoib_port_t* const				p_port,
	IN			ipoib_cm_recv_desc_t* const		p_desc,
	OUT			NET_BUFFER_LIST** const			pp_net_buffer_list )
{
	NDIS_STATUS		status;
	uint32_t		pkt_filter;
	PNET_BUFFER		NetBuffer;

	PERF_DECLARE( CMGetNdisPkt );

	XIPOIB_ENTER( IPOIB_DBG_RECV );

	pkt_filter = p_port->p_adapter->packet_filter;

	CL_ASSERT( p_desc->recv_mode == RECV_RC );

	/* Check the packet filter. */
	switch( p_desc->type )
	{
	default:
	case PKT_TYPE_UCAST:
		
		if( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS ||
			pkt_filter & NDIS_PACKET_TYPE_ALL_FUNCTIONAL ||
			pkt_filter & NDIS_PACKET_TYPE_SOURCE_ROUTING ||
			pkt_filter & NDIS_PACKET_TYPE_DIRECTED )
		{
			/* OK to report. */
			status = NDIS_STATUS_SUCCESS;
			IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
				("Received RC UCAST PKT.\n") );
		}
		else
		{
			status = NDIS_STATUS_FAILURE;
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_RECV,
				("Received UCAST PKT with ERROR !!!!\n"));
		}
		break;

	case PKT_TYPE_BCAST:
		if( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS ||
			pkt_filter & NDIS_PACKET_TYPE_BROADCAST )
		{
			/* OK to report. */
			status = NDIS_STATUS_SUCCESS;
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("Received BCAST PKT.\n"));
		}
		else
		{
			status = NDIS_STATUS_FAILURE;
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received BCAST PKT with ERROR !!!!\n"));
		}
		break;

	case PKT_TYPE_MCAST:
		if( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS ||
			pkt_filter & NDIS_PACKET_TYPE_ALL_MULTICAST ||
			pkt_filter & NDIS_PACKET_TYPE_MULTICAST )
		{
			/* OK to report. */
			status = NDIS_STATUS_SUCCESS;
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("Received MCAST PKT.\n"));
		}
		else
		{
			status = NDIS_STATUS_FAILURE;
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received MCAST PKT with ERROR !!!!\n"));
		}
		break;
	}

	if( status != NDIS_STATUS_SUCCESS )
	{
		/* Return the receive descriptor to the pool. */
		__cm_buf_mgr_put_recv( p_port, p_desc, FALSE, NULL );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Packet filter doesn't match receive.  Dropping.\n") );
		/*
		 * Return IB_NOT_DONE since the packet has been completed,
		 * but has not consumed an array entry.
		 */
		return IB_NOT_DONE;
	}

	cl_perf_start( CMGetNdisPkt );
	*pp_net_buffer_list = __cm_buf_mgr_get_NBL( p_port, p_desc );
	cl_perf_stop( &p_port->p_adapter->perf, CMGetNdisPkt );
	if( !*pp_net_buffer_list )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__cm_buf_mgr_get_NBL failed\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	NetBuffer = NET_BUFFER_LIST_FIRST_NB(*pp_net_buffer_list);
	NET_BUFFER_DATA_LENGTH(NetBuffer) = p_desc->len;
	
	switch( p_port->p_adapter->params.recv_chksum_offload )
	{
	  default:
		CL_ASSERT( FALSE );
		break;

	  case CSUM_DISABLED:
		NET_BUFFER_LIST_INFO(*pp_net_buffer_list, TcpIpChecksumNetBufferListInfo) = 
		(PVOID)p_desc->csum.Value = (PVOID)0;
		break;

	  case CSUM_ENABLED:
		/* Get the checksums directly from packet information.
		   In this case, no one of cheksum's can get false value 
		   If hardware checksum failed or wasn't calculated, NDIS will
		   recalculate it again */
		NET_BUFFER_LIST_INFO(*pp_net_buffer_list, TcpIpChecksumNetBufferListInfo) = 
		(PVOID)p_desc->csum.Value;
		break;

	  case CSUM_BYPASS:
		p_desc->csum.Value = 0;
		/* Flag the checksums as having been calculated. */
		p_desc->csum.Receive.TcpChecksumSucceeded = 
		p_desc->csum.Receive.UdpChecksumSucceeded = 
		p_desc->csum.Receive.IpChecksumSucceeded = TRUE;
		NET_BUFFER_LIST_INFO(*pp_net_buffer_list, TcpIpChecksumNetBufferListInfo) = 
		(PVOID)p_desc->csum.Value;
		break;
	}

	NET_BUFFER_LIST_STATUS( *pp_net_buffer_list ) = NDIS_STATUS_SUCCESS;

	XIPOIB_EXIT( IPOIB_DBG_RECV );
	return IB_SUCCESS;
}


static uint32_t
__cm_recv_mgr_build_NBL_list(
	IN		ipoib_port_t* const		p_port,
	IN		ipoib_endpt_t* const	p_endpt,
	IN		cl_qlist_t* 			p_done_list,
	IN OUT	int32_t* const			p_discarded,
	IN OUT	int32_t* const			p_bytes_recv,
	IN OUT	NET_BUFFER_LIST** const	p_NBL_head )
{
	cl_list_item_t			*p_item;
	ipoib_cm_recv_desc_t	*p_desc;
	uint32_t				i = 0;
	ib_api_status_t			status;
	NET_BUFFER_LIST			*p_NBL;
	NET_BUFFER_LIST			*p_tail=NULL;

	PERF_DECLARE( CMPreparePkt );

	XIPOIB_ENTER( IPOIB_DBG_RECV );

	*p_discarded = 0;
	*p_bytes_recv = 0;
	*p_NBL_head = NULL;

	/* Move any existing receives to the head of p_done_list to preserve ordering */
	if ( p_done_list ) {
		cl_qlist_insert_list_head( p_done_list, &p_endpt->cm_recv.done_list );
	} else {
		p_done_list = &p_endpt->cm_recv.done_list;
	}

	p_item = cl_qlist_remove_head( p_done_list );
	while( p_item != cl_qlist_end( p_done_list ) )
	{
		p_desc = (ipoib_cm_recv_desc_t*)p_item;

		CL_ASSERT( p_desc->recv_mode == RECV_RC );
		cl_perf_start( CMPreparePkt );
		status = __cm_recv_mgr_prepare_NBL( p_port, p_desc, &p_NBL );
		cl_perf_stop( &p_port->p_adapter->perf, CMPreparePkt );

		if( status == IB_SUCCESS )
		{
			if ( i == 0 )
				*p_NBL_head = p_tail = p_NBL;
			else
			{	// not 1st NBL, Link NBLs together
				NET_BUFFER_LIST_NEXT_NBL(p_tail) = p_NBL;
				p_tail = p_NBL;
			}
			i++;
			*p_bytes_recv += p_desc->len;
			IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_CM,
				("NBL[%d] len %d\n",(i-1),p_desc->len) );
		}
		else if( status == IB_NOT_DONE )
		{
			IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__recv_mgr_prepare_NBL returned IB_NOT_DONE, discard pkt.\n") );
			(*p_discarded)++;
		}
		else
		{
			IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__recv_mgr_prepare_NBL returned %s\n",
					p_endpt->p_ifc->get_err_str( status )) );

			/* Put all completed receives on the port's done list. */
			if ( p_done_list != &p_endpt->cm_recv.done_list)
			{
				cl_qlist_insert_tail( &p_endpt->cm_recv.done_list, p_item );
				cl_qlist_insert_list_tail( &p_endpt->cm_recv.done_list, p_done_list );
			} else
				cl_qlist_insert_head( &p_endpt->cm_recv.done_list, p_item );

			break;
		}
		p_item = cl_qlist_remove_head( p_done_list );
	}

	XIPOIB_EXIT( IPOIB_DBG_RECV );
	return i;
}


/* cm_buf_mgr.lock held */

static ipoib_cm_recv_desc_t*
__cm_buf_mgr_get_recv_locked(
	IN		 ipoib_port_t* const		p_port,
	IN		 ipoib_endpt_t* const		p_endpt )
{
	ipoib_cm_recv_desc_t	*p_desc;

	XIPOIB_ENTER( IPOIB_DBG_RECV );

	p_desc = (ipoib_cm_recv_desc_t*)cl_qpool_get( &p_port->cm_buf_mgr.recv_pool );
	if( p_desc )
	{
		p_desc->p_endpt = p_endpt;
		InterlockedIncrement( &p_port->cm_buf_mgr.posted );
		cl_qlist_insert_tail( &p_port->cm_buf_mgr.oop_list, &p_desc->list_item );
		CL_ASSERT( p_desc->wr.wr_id == (uintn_t)p_desc );
		CL_ASSERT( p_desc->local_ds[0].vaddr == cl_get_physaddr(p_desc->p_buf) );
		CL_ASSERT( p_desc->local_ds[0].length > 0 );
	}

	XIPOIB_EXIT( IPOIB_DBG_RECV );
	return p_desc;
}


static ib_api_status_t
__cm_post_srq_recv(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt )
{
	ib_api_status_t			ib_status = IB_SUCCESS;
	ipoib_cm_recv_desc_t	*p_head_desc = NULL;
	ipoib_cm_recv_desc_t	*p_tail_desc = NULL;
	ipoib_cm_recv_desc_t	*p_next_desc;
	ib_recv_wr_t			*p_failed_wc = NULL;
	int						wanted;
	int						rx_cnt;
	int						posted;

	IPOIB_ENTER( IPOIB_DBG_RECV );

	posted = p_port->cm_buf_mgr.posted;
	wanted = p_port->p_adapter->params.rq_depth - posted;

#if DBG
	IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
		("Port[%d] posting %d RC bufs of limit(rq_depth %d) posted %d max %d\n",
			p_port->port_num, wanted, p_port->p_adapter->params.rq_depth,
			posted, p_port->cm_buf_mgr.recv_pool_depth) );
#endif

	cl_spinlock_acquire( &p_port->cm_buf_mgr.lock);

	for( rx_cnt=posted; rx_cnt < p_port->p_adapter->params.rq_depth; rx_cnt++)
	{
		/* Pull receives out of the pool to chain them up. */
		p_next_desc = __cm_buf_mgr_get_recv_locked( p_port, p_endpt );
		if( !p_next_desc )
		{
			IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("RX descriptor pool exhausted! wanted %d provided %d\n",
					wanted, (rx_cnt - posted)) );
			break;
		}

		if( !p_tail_desc )
		{
			p_tail_desc = p_next_desc;
			p_next_desc->wr.p_next = NULL;
		}
		else
			p_next_desc->wr.p_next = &p_head_desc->wr;

		p_head_desc = p_next_desc;
	}
	cl_spinlock_release( &p_port->cm_buf_mgr.lock);

	if( p_head_desc )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Posting %d SRQ desc, oop %d\n", rx_cnt, posted) );

		ib_status = p_endpt->p_ifc->post_srq_recv( p_port->ib_mgr.h_srq,
												   &p_head_desc->wr,
												   &p_failed_wc );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("post_srq_recv() returned %s\n", 
				p_endpt->p_ifc->get_err_str( ib_status )) );
			
			/* put descriptors back to the pool */
			while( p_failed_wc )
			{
				p_head_desc = PARENT_STRUCT( p_failed_wc, ipoib_cm_recv_desc_t, wr );
				p_failed_wc = p_failed_wc->p_next;
				__cm_buf_mgr_put_recv( p_port, p_head_desc, TRUE, NULL );
			}
		}
	}

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return( ib_status );
}


/*
 * Posts receive buffers to the receive queue and returns the number
 * of receives needed to bring the RQ to its low water mark.  Note
 * that the value is signed, and can go negative.  All tests must
 * be for > 0.
 */
static int32_t
__cm_recv_mgr_repost(
	IN 		ipoib_port_t* const		p_port,
	IN 		ipoib_endpt_t* const	p_endpt )
{
	ipoib_cm_recv_desc_t	*p_head = NULL, *p_tail = NULL, *p_next;
	ib_api_status_t			status;
	ib_recv_wr_t			*p_failed;
	int						rx_cnt=0;
	int						rx_wanted;

	PERF_DECLARE( GetRecv );
	PERF_DECLARE( PostRecv );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	CL_ASSERT( p_port );

	cl_obj_lock( &p_port->obj );
	if( p_port->state != IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Port in invalid state.  Not reposting.\n") );
		return 0;
	}

	ipoib_port_ref( p_port, ref_repost );
	cl_obj_unlock( &p_port->obj );

	cl_spinlock_acquire( &p_port->cm_buf_mgr.lock);
	rx_wanted = p_port->p_adapter->params.rq_depth - p_port->cm_buf_mgr.posted;

	while( p_port->cm_buf_mgr.posted < p_port->p_adapter->params.rq_depth )
	{
		/* Pull receives out of the pool and chain them up. */
		cl_perf_start( GetRecv );
		p_next = __cm_buf_mgr_get_recv_locked( p_port, p_endpt );
		cl_perf_stop( &p_port->p_adapter->perf, GetRecv );
		if( !p_next )
		{
			IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
				("Out of receive descriptors! recv queue depth %d\n",
				p_port->cm_buf_mgr.posted) );
			break;
		}

		if( !p_tail )
		{
			p_tail = p_next;
			p_next->wr.p_next = NULL;
		}
		else
			p_next->wr.p_next = &p_head->wr;

		p_head = p_next;
		rx_cnt++;
	}
	cl_spinlock_release( &p_port->cm_buf_mgr.lock);

	if( p_head )
	{
		cl_perf_start( PostRecv );
		status = p_endpt->p_ifc->post_srq_recv( p_port->ib_mgr.h_srq,
											    &p_head->wr,
											    &p_failed );
		cl_perf_stop( &p_port->p_adapter->perf, PostRecv );

		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("posting %d recv desc returned %s\n", 
					rx_cnt, p_endpt->p_ifc->get_err_str( status )) );
			/* return the descriptors to the pool */
			while( p_failed )
			{
				p_head = PARENT_STRUCT( p_failed, ipoib_cm_recv_desc_t, wr );
				p_failed = p_failed->p_next;
				__cm_buf_mgr_put_recv( p_port, p_head, TRUE, NULL );
			}
		}
		else
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("CM RX bufs: wanted %d posted %d\n", rx_wanted, rx_cnt) ); 
		}
	}

	cl_spinlock_acquire( &p_port->cm_buf_mgr.lock);
	rx_cnt = p_port->p_adapter->params.rq_low_watermark - p_port->cm_buf_mgr.posted;
	cl_spinlock_release( &p_port->cm_buf_mgr.lock);

	ipoib_port_deref( p_port, ref_repost );

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return rx_cnt;
}


/*
 * Post specified number of receive buffers to the receive queue
 */

static int
__cm_recv_mgr_repost_grow(
	IN 		ipoib_port_t* const		p_port,
	IN 		ipoib_endpt_t* const	p_endpt,
	IN		int						grow_cnt )
{
	ipoib_cm_recv_desc_t	*p_head = NULL, *p_next;
	ib_api_status_t			status;
	ib_recv_wr_t			*p_failed;
	int						buf_cnt=0;
	int						buf_wanted=grow_cnt;

	PERF_DECLARE( GetRecv );
	PERF_DECLARE( PostRecv );

	IPOIB_ENTER( IPOIB_DBG_CM );

	CL_ASSERT( p_port );

	cl_obj_lock( &p_port->obj );
	if( p_port->state != IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port state invalid; Not reposting for Rx pool Growth.\n") );
		return grow_cnt;
	}
	ipoib_port_ref( p_port, ref_repost );
	cl_obj_unlock( &p_port->obj );

	cl_spinlock_acquire( &p_port->cm_buf_mgr.lock);

	while (grow_cnt > 0)
	{
		/* Pull receives out of the pool and chain them up. */
		cl_perf_start( GetRecv );
		p_next = __cm_buf_mgr_get_recv_locked( p_port, p_endpt );
		cl_perf_stop( &p_port->p_adapter->perf, GetRecv );
		if( !p_next )
		{
			IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Out of receive descriptors! recv queue depth %d\n",
					p_port->cm_buf_mgr.posted) );
			break;
		}

		if( !p_head )
		{
			p_head = p_next;
			p_head->wr.p_next = NULL;
		}
		else
			p_next->wr.p_next = &p_head->wr;

		p_head = p_next;
		grow_cnt--;
		buf_cnt++;
	}
	cl_spinlock_release( &p_port->cm_buf_mgr.lock);

	if( p_head )
	{
		cl_perf_start( PostRecv );
		status = p_endpt->p_ifc->post_srq_recv( p_port->ib_mgr.h_srq,
											    &p_head->wr,
											    &p_failed );
		cl_perf_stop( &p_port->p_adapter->perf, PostRecv );

		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("SRQ posting %d recv desc returned %s\n", 
					buf_cnt, p_endpt->p_ifc->get_err_str( status )) );
			/* return the descriptors to the pool */
			while( p_failed )
			{
				p_head = PARENT_STRUCT( p_failed, ipoib_cm_recv_desc_t, wr );
				p_failed = p_failed->p_next;
				__cm_buf_mgr_put_recv( p_port, p_head, TRUE, NULL );
			}
		}
		else
		{
			if( (buf_wanted - buf_cnt) != 0 )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("SRQ growth: wanted %d posted %d, shortage %d\n",
						buf_wanted, buf_cnt, (buf_wanted - buf_cnt)) ); 
			}
		}
	}
	ipoib_port_deref( p_port, ref_repost );

	IPOIB_EXIT( IPOIB_DBG_CM );
	return (buf_wanted - buf_cnt);
}


static IO_WORKITEM_ROUTINE __WorkItemCM;

static void
__WorkItemCM(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				void*						context )
{
	ipoib_port_t *p_port;
	ipoib_endpt_t *p_endpt;
	BOOLEAN WorkToDo = TRUE;
	KIRQL irql;
	uint32_t recv_cnt = 0;
	uint32_t total_recv_cnt = 0;

	UNREFERENCED_PARAMETER(p_dev_obj);
	IPOIB_ENTER( IPOIB_DBG_CM );

	p_endpt = (ipoib_endpt_t*)context;
	p_port = ipoib_endpt_parent( p_endpt );

	while (WorkToDo && total_recv_cnt < 512)
	{
		irql = KeRaiseIrqlToDpcLevel();
		WorkToDo = __cm_recv_internal( NULL, p_endpt, &recv_cnt );
		KeLowerIrql(irql);
		total_recv_cnt += recv_cnt;
	}

	if (WorkToDo)
	{
		IoQueueWorkItem( p_port->pPoWorkItemCM,
						(PIO_WORKITEM_ROUTINE) __WorkItemCM,
						 DelayedWorkQueue,
						 p_endpt );
	}
	else
	{
		// Release the reference count that was incremented when queued the work item
		ipoib_port_deref( p_port, ref_recv_cb );
	}
	IPOIB_EXIT( IPOIB_DBG_CM );
}


static BOOLEAN
__cm_recv_internal(
	IN		const	ib_cq_handle_t			h_cq,	// can be NULL
	IN				void					*cq_context,
	IN				uint32_t				*p_recv_cnt )
{
	ipoib_port_t		*p_port;
	ipoib_endpt_t		*p_endpt;
	ib_api_status_t		status;
	ib_wc_t				wc[MAX_CM_RECV_WC], *p_free, *p_done_wc;
	int32_t				NBL_cnt, recv_cnt = 0, shortage, discarded, bytes_received;
	cl_qlist_t			done_list, bad_list;
	size_t				bad_list_cnt; 
	ULONG				recv_complete_flags = 0;
	BOOLEAN				res;
	cl_perf_t			*p_perf;
	ib_al_ifc_t			*p_ibal;
	BOOLEAN				WorkToDo = FALSE;

	PERF_DECLARE( CMRecvCb );
	PERF_DECLARE( CMPollRecv );
	PERF_DECLARE( CMRepostRecv );
	PERF_DECLARE( CMFilterRecv );
	PERF_DECLARE( CMBuildNBLArray );
	PERF_DECLARE( CMRecvNdisIndicate );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	cl_perf_start( CMRecvCb );

	NDIS_SET_SEND_COMPLETE_FLAG(recv_complete_flags,
								NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);

	p_endpt = (ipoib_endpt_t*)cq_context;
	ASSERT( p_endpt );
#if DBG
	if( h_cq ) {ASSERT( h_cq == p_endpt->conn.h_recv_cq );}
#endif
	p_port = ipoib_endpt_parent( p_endpt );

	p_perf = &p_port->p_adapter->perf;
	p_ibal = p_port->p_adapter->p_ifc;

	cl_qlist_init( &done_list );
	cl_qlist_init( &bad_list );

	ipoib_port_ref( p_port, ref_cm_recv_cb );

	for( p_free=wc; p_free < &wc[MAX_CM_RECV_WC - 1]; p_free++ )
		p_free->p_next = p_free + 1;
	p_free->p_next = NULL;

	/*
	 * We'll be accessing the endpoint map so take a reference
	 * on it to prevent modifications.
	 */
	cl_obj_lock( &p_port->obj );
	cl_atomic_inc( &p_port->endpt_rdr );
	cl_obj_unlock( &p_port->obj );

	do
	{
		/* If we get here, then the list of WCs is intact. */
		p_free = wc;
		cl_perf_start( CMPollRecv );
		status = p_ibal->poll_cq( p_endpt->conn.h_recv_cq, &p_free, &p_done_wc );
		cl_perf_stop( p_perf, CMPollRecv );
		if( status != IB_SUCCESS && status != IB_NOT_FOUND )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Port[%d] EP %s poll_cq() %s\n",
					p_port->port_num, p_endpt->tag,
					p_port->p_adapter->p_ifc->get_err_str(status)) );
			break;
		}

		/* Look at the payload now and filter ARP and DHCP packets. */

		cl_perf_start( CMFilterRecv );
		recv_cnt += __cm_recv_mgr_filter( p_port,
										  p_endpt,
										  p_done_wc,
										  &done_list,
										  &bad_list );
		cl_perf_stop( p_perf, CMFilterRecv );

	} while( (!p_free) && (recv_cnt < 128) );

	/* We're done looking at the endpoint map, release the reference. */
	cl_atomic_dec( &p_port->endpt_rdr );

	bad_list_cnt = cl_qlist_count( &bad_list );
	if( bad_list_cnt > 0 )
		recv_cnt = (uint32_t) cl_qlist_count( &done_list );

	*p_recv_cnt = (uint32_t)recv_cnt;

	/* Return any discarded receives to the pool */
	if( bad_list_cnt > 0 )
	{
		/* RC connection - we are hosed. */
		IPOIB_PRINT(TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR,
			("bad_list %d done list %d\n", (int)bad_list_cnt, recv_cnt) );
		__cm_buf_mgr_put_recv_list( p_port, &bad_list );
	}

	if( recv_cnt == 0 )
	{
		IPOIB_PRINT_EXIT(TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR, ("recv_cnt == 0 ?\n") );
		ipoib_port_deref( p_port, ref_cm_recv_cb );
		IPOIB_EXIT( IPOIB_DBG_RECV );
		return FALSE;
	}

	cl_spinlock_acquire( &p_port->cm_buf_mgr.lock);

	if( recv_cnt > 0 )
		(void) InterlockedExchangeAdd( &p_port->cm_buf_mgr.posted, -recv_cnt );

	cl_spinlock_release( &p_port->cm_buf_mgr.lock);

	do
	{
 		/* Approximate the number of posted receive buffers needed in order to
		 * bring the SRQ above the low water mark; Normally a large negative number.
		 * Approximate, in that without proper locking, it's a guess, that's OK.
		 */
		shortage =
			p_port->p_adapter->params.rq_low_watermark - p_port->cm_buf_mgr.posted;

		if( shortage > 0 )
		{				
			cl_perf_start( CMRepostRecv );
			/* Repost ASAP so we don't starve the RQ. */
			shortage = __cm_recv_mgr_repost_grow( p_port, p_endpt, shortage );
			cl_perf_stop( p_perf, CMRepostRecv );

			if( shortage > 0 )
			{
				recv_complete_flags |= NDIS_RECEIVE_FLAGS_RESOURCES;
				cl_dbg_out("CM Rx SHORTAGE=%d\n",shortage);
			}
		}

		cl_perf_start( CMBuildNBLArray );
		/* Notify NDIS of any and all possible receive buffers. */
		NBL_cnt = __cm_recv_mgr_build_NBL_list( p_port,
												p_endpt,
												&done_list,
												&discarded,
												&bytes_received,
												&p_endpt->cm_recv.NBL );
		cl_perf_stop( p_perf, CMBuildNBLArray );

		/* Only indicate receives if we actually had any. */
		if( discarded && shortage > 0 )
		{
			/* We may have thrown away packets, and have a shortage */
			cl_perf_start( CMRepostRecv );
			__cm_recv_mgr_repost( p_port, p_endpt );
			cl_perf_stop( p_perf, CMRepostRecv );
		}

		if( !NBL_cnt )
		{
			/* normal all-done loop exit */
			break;
		}

		cl_perf_start( CMRecvNdisIndicate );
		
		if (shortage <= 0)	// normal case of posted RX > low water mark.
		{
			res = shutter_add( &p_port->p_adapter->recv_shutter, NBL_cnt );
			if (res)
			{
				ipoib_inc_recv_stat( p_port->p_adapter,
									 IP_STAT_UCAST_BYTES,
									 bytes_received,
									 NBL_cnt );

				NdisMIndicateReceiveNetBufferLists(
											p_port->p_adapter->h_adapter,
											p_endpt->cm_recv.NBL,
											NDIS_DEFAULT_PORT_NUMBER,
											NBL_cnt,
											recv_complete_flags );
			}
			else
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Port[%d] res == 0, free NBL\n",p_port->port_num) );
				cl_spinlock_acquire( &p_port->recv_lock );
				ipoib_free_received_NBL( p_port, p_endpt->cm_recv.NBL );
				cl_spinlock_release( &p_port->recv_lock );
			}
		}
		else
		{
			/* shortage > 0, we already set the status to
			   NDIS_RECEIVE_FLAGS_RESOURCES. That is, IPoIB driver regains
			   ownership of the NET_BUFFER_LIST structures immediately
			 */
			res = shutter_add( &p_port->p_adapter->recv_shutter, 1 );
			if (res)
			{
				ipoib_inc_recv_stat( p_port->p_adapter,
									 IP_STAT_UCAST_BYTES,
									 bytes_received,
									 NBL_cnt );

				NdisMIndicateReceiveNetBufferLists(
											p_port->p_adapter->h_adapter,
											p_endpt->cm_recv.NBL,
											NDIS_DEFAULT_PORT_NUMBER,
											NBL_cnt,
											recv_complete_flags );
				shutter_sub( &p_port->p_adapter->recv_shutter, -1 );
			}
			cl_perf_stop( p_perf, CMRecvNdisIndicate );

			/*
			 * Cap the number of receives to put back to what we just indicated
			 * with NDIS_STATUS_RESOURCES.
			 */
			cl_dbg_out("CM Rx SHORTAGE reposting all NBLs\n");
			/* repost all NBLs */
			cl_spinlock_acquire( &p_port->recv_lock );
			ipoib_free_received_NBL( p_port, p_endpt->cm_recv.NBL );
			cl_spinlock_release( &p_port->recv_lock );
		}

	} while( NBL_cnt );


	if (p_free )
	{
		/*
		 * Rearm after filtering to prevent contention on the end-point maps
		 * and eliminates the possibility of a call to __endpt_cm_mgr_insert
		 * finding a duplicate.
		 */
		ASSERT( WorkToDo == FALSE );
		status = p_ibal->rearm_cq( p_endpt->conn.h_recv_cq, FALSE );
		CL_ASSERT( status == IB_SUCCESS );
	}
	else
	{
		if( h_cq && bad_list_cnt == 0 )
		{
			// increment reference to ensure no one release the object while work
			// item is queued
			ipoib_port_ref( p_port, ref_recv_cb );
			IoQueueWorkItem( p_port->pPoWorkItemCM,
							 (PIO_WORKITEM_ROUTINE) __WorkItemCM,
							 DelayedWorkQueue,
							 p_endpt );
			WorkToDo = FALSE;
		}
		else
			WorkToDo = TRUE;
	}
	cl_perf_stop( p_perf, CMRecvCb );

	ipoib_port_deref( p_port, ref_cm_recv_cb );

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return WorkToDo;
}

static void
__cm_recv_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	uint32_t	recv_cnt;
	boolean_t	WorkToDo;
	
	do {
		WorkToDo = __cm_recv_internal(h_cq, cq_context, &recv_cnt);
	} while( WorkToDo );
}


static void
__conn_reject(
	IN		ipoib_port_t* const		p_port,
	IN		ib_cm_handle_t			h_cm_handle,
	IN		ib_rej_status_t			rej_status )
{
	ib_api_status_t		ib_status;
	ib_cm_rej_t			cm_rej;
	uint16_t			ari_info;
	cm_private_data_t	private_data;

	IPOIB_ENTER( IPOIB_DBG_CM );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("CM REJECT SEND with rej_status %d\n", cl_ntoh16( rej_status ) ) );

	memset( &cm_rej, 0, sizeof( ib_cm_rej_t ) );
	cm_rej.rej_status = IB_REJ_USER_DEFINED;
	cm_rej.ari_length = sizeof( uint16_t );
	ari_info = rej_status;
	cm_rej.p_ari = (ib_ari_t *)&ari_info;

	private_data.ud_qpn = p_port->ib_mgr.qpn;
	private_data.recv_mtu = 
		cl_hton32( p_port->p_adapter->params.cm_payload_mtu + sizeof(ipoib_hdr_t) );
	
	cm_rej.p_rej_pdata = (uint8_t *)&private_data;
	cm_rej.rej_length = sizeof( cm_private_data_t );

	ib_status = p_port->p_adapter->p_ifc->cm_rej( h_cm_handle, &cm_rej );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cm_rej failed status %s\n",
				p_port->p_adapter->p_ifc->get_err_str(ib_status)) );
	}

	IPOIB_EXIT( IPOIB_DBG_CM );
}


static void
__cq_async_event_cb(
	IN		ib_async_event_rec_t		*p_event_rec )
{
	ipoib_endpt_t*	p_endpt;
	ipoib_port_t*	p_port;

	p_endpt = (ipoib_endpt_t *)p_event_rec->context;
	p_port = ipoib_endpt_parent( p_endpt );

	IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
		("SRQ CQ AsyncEvent EP %s event '%s' vendor code %#I64d\n",
			p_endpt->tag, ib_get_async_event_str(p_event_rec->code),
			p_event_rec->vendor_specific) );
}



static void
__srq_qp_async_event_cb(
	IN		ib_async_event_rec_t		*p_event_rec )
{
	ipoib_endpt_t*	p_endpt;
	ipoib_port_t*	p_port;

	IPOIB_ENTER( IPOIB_DBG_CM );

	p_endpt = (ipoib_endpt_t *)p_event_rec->context;
	ASSERT( p_endpt );
	p_port = ipoib_endpt_parent( p_endpt );
	ASSERT( p_port );

	switch( p_event_rec->code )
	{
	  case IB_AE_SRQ_LIMIT_REACHED:
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("SRQ low-water mark reached(%d) EP %s grow posted by %d\n",
				SRQ_LOW_WATER, p_endpt->tag, SRQ_MIN_GROWTH) );
		__cm_recv_mgr_repost_grow( p_port, p_endpt, SRQ_MIN_GROWTH );
		// TODO: consider queuing to endpoint thread an SRQ grow request.
		break;

	  case IB_AE_SRQ_QP_LAST_WQE_REACHED:
		/* LAST_WEQ_REACHED is normal for SRQ, Endpoint CQ is flushed, destroy QP. */
		if( p_event_rec->handle.h_qp == p_endpt->conn.h_recv_qp )
		{
			int how = -2; // always flush - tidy bowl spirit.

			if( p_endpt->cm_ep_destroy )
				how = -1;

			cm_release_resources( p_port, p_endpt, how ); // QP already in ERR state.

			if( p_endpt->cm_ep_destroy )
			{
				endpt_cm_set_state( p_endpt, IPOIB_CM_DISCONNECTED ); 
				if( p_endpt->cm_ep_destroy == 1 )
				{
					DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
						("Destroy Obj EP %s\n", p_endpt->tag) );
					cl_obj_destroy( &p_endpt->obj );
				}
#if DBG
				else
				{
					IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
						("EP %s OBJ destroy cnt %u ?\n",
							p_endpt->tag, p_endpt->cm_ep_destroy) );
				}
#endif
			}
		}
		break;

	  default:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ERROR,
			("EP %s SRQ ASYNC EVENT(%d) '%s' vendor code %#I64d\n",
				p_endpt->tag, p_event_rec->code,
				ib_get_async_event_str(p_event_rec->code),
				p_event_rec->vendor_specific) );
		break;
	}
	IPOIB_EXIT( IPOIB_DBG_CM );
}

void
endpt_cm_disconnect(
	IN		ipoib_endpt_t*	const	p_endpt )
{
	ib_api_status_t		ib_status;
	ib_cm_dreq_t		cm_dreq;
	cm_state_t			cm_state;
	
	IPOIB_ENTER( IPOIB_DBG_CM );

	cm_state = endpt_cm_get_state( p_endpt );

	if( cm_state != IPOIB_CM_CONNECTED && cm_state != IPOIB_CM_DESTROY )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("EP[%p] %s DREQ not sent, Incorrect conn state %s\n",
					p_endpt, p_endpt->tag, cm_get_state_str(cm_state)) );
		return;
	}

	endpt_cm_set_state( p_endpt, IPOIB_CM_DREQ_SENT );

	if( p_endpt->conn.h_send_qp )
	{
		cm_dreq.h_qp = p_endpt->conn.h_send_qp;
		cm_dreq.qp_type = IB_QPT_RELIABLE_CONN;
		cm_dreq.p_dreq_pdata = NULL;
		cm_dreq.dreq_length = 0;
		cm_dreq.flags = 0;

		ib_status = p_endpt->p_ifc->cm_dreq( &cm_dreq );

		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				(" SEND QP Disconnect ENDPT %s QP status %s\n",
					p_endpt->tag, p_endpt->p_ifc->get_err_str(ib_status)) );
		}
#if DBG
		else
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
				(" SEND QP disconnect(DREQ) to EP %s prev %s\n",
					p_endpt->tag, cm_get_state_str(cm_state)) );
		}
#endif
	}

	if( p_endpt->conn.h_recv_qp )
	{
		cm_dreq.h_qp = p_endpt->conn.h_recv_qp;
		ib_status = p_endpt->p_ifc->cm_dreq( &cm_dreq );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				(" RECV QP Disconnected(DREQ) EP %s QP status %s\n",
					p_endpt->tag, p_endpt->p_ifc->get_err_str(ib_status)) );
		}
#if DBG
		else
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				(" RECV QP disconnected(DREQ) from EP%s QP status %s\n",
					p_endpt->tag, p_endpt->p_ifc->get_err_str(ib_status)) );
		}
#endif
	}
	IPOIB_EXIT( IPOIB_DBG_CM );
}


#if IPOIB_CM

static void
__cm_buf_mgr_construct(
	IN		cm_buf_mgr_t * const		p_buf_mgr )
{
	cl_qpool_construct( &p_buf_mgr->recv_pool );

	cl_spinlock_construct( &p_buf_mgr->lock );

	p_buf_mgr->h_nbl_pool = NULL;
	p_buf_mgr->pool_init = FALSE;
}


ib_api_status_t	
endpt_cm_buf_mgr_init(
	IN		ipoib_port_t* const			p_port )
{
	cl_status_t		cl_status;
	ib_api_status_t	ib_status=IB_SUCCESS;
	NET_BUFFER_LIST_POOL_PARAMETERS pool_parameters;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	if( p_port->cm_buf_mgr.pool_init )
		return ib_status;

	__cm_buf_mgr_construct( &p_port->cm_buf_mgr );

	p_port->cm_buf_mgr.recv_pool_depth =
		min( (uint32_t) p_port->p_adapter->params.rq_depth * 8,
				p_port->p_ca_attrs->max_srq_wrs/2 );

	DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Port[%d] cm_recv_mgr.recv_pool_depth %d max_srq_wrs/2 %d\n",
			p_port->port_num,
			p_port->cm_buf_mgr.recv_pool_depth,
			p_port->p_ca_attrs->max_srq_wrs/2 ) );

	cl_qlist_init( &p_port->cm_buf_mgr.oop_list );
	cl_status = cl_spinlock_init( &p_port->cm_buf_mgr.lock );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}
	p_port->cm_buf_mgr.posted = 0;

	/* Allocate the NET BUFFER list pool for receive indication.
	 * In the recv_pool ctor routine a NBL & mdl are allocated and attached to the
	 * recv_desc.
	 */
	memset( &pool_parameters, 0, sizeof(NET_BUFFER_LIST_POOL_PARAMETERS) );
    pool_parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    pool_parameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    pool_parameters.Header.Size = sizeof(pool_parameters);
    pool_parameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    pool_parameters.ContextSize = 0;
    pool_parameters.fAllocateNetBuffer = TRUE;
    pool_parameters.PoolTag = 'PRPI';
	pool_parameters.DataSize = 0;

    p_port->cm_buf_mgr.h_nbl_pool = NdisAllocateNetBufferListPool(
													p_port->p_adapter->h_adapter,
													&pool_parameters ); 
	if( !p_port->cm_buf_mgr.h_nbl_pool )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_PKT_POOL, 1, NDIS_STATUS_RESOURCES );

		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisAllocatePacketPool(cm_packet_pool)\n") );
		
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Allocate the receive descriptors pool */
	cl_status = cl_qpool_init( &p_port->cm_buf_mgr.recv_pool,
							   p_port->cm_buf_mgr.recv_pool_depth,
							   0,
							   0,
							   sizeof( ipoib_cm_recv_desc_t ),
							   __cm_recv_desc_ctor,
							   __cm_recv_desc_dtor,
							   p_port );

	if( cl_status != CL_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
								EVENT_IPOIB_RECV_POOL, 1, cl_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_qpool_init(cm_buf_mgr.recv_pool) returned %#x\n", cl_status) );
		ib_status =  IB_INSUFFICIENT_MEMORY;
		goto pkt_pool_failed;
	}

	p_port->cm_buf_mgr.pool_init = TRUE;

	IPOIB_EXIT( IPOIB_DBG_INIT );

	return IB_SUCCESS;

pkt_pool_failed:

	NdisFreeNetBufferListPool( p_port->cm_buf_mgr.h_nbl_pool );
	p_port->cm_buf_mgr.h_nbl_pool = NULL;

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return ib_status;
}


static void
__cm_buf_mgr_reset(
	IN		ipoib_port_t* const		p_port )
{
	cl_list_item_t		*p_item;

	IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
		("Port[%d] pool elements outstanding %d oop_list count %d\n",
			p_port->port_num, p_port->cm_buf_mgr.posted,
			(int)cl_qlist_count( &p_port->cm_buf_mgr.oop_list)) );

	if( cl_qlist_count( &p_port->cm_buf_mgr.oop_list ) )
	{
		ipoib_cm_recv_desc_t*	p_desc;

		for( p_item = cl_qlist_remove_head( &p_port->cm_buf_mgr.oop_list );
			p_item != cl_qlist_end( &p_port->cm_buf_mgr.oop_list );
			p_item =  cl_qlist_remove_head( &p_port->cm_buf_mgr.oop_list ) )
		{
			p_desc = PARENT_STRUCT( p_item, ipoib_cm_recv_desc_t, list_item );
			cl_qpool_put( &p_port->cm_buf_mgr.recv_pool, &p_desc->item );
			InterlockedDecrement( &p_port->cm_buf_mgr.posted );
		}
	}

#if DBG
	if( p_port->cm_buf_mgr.posted )
	{
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Port[%d] CM Recv pool buffers outstanding %d?\n",
					p_port->port_num, p_port->cm_buf_mgr.posted) );
	}
#endif
}


void
endpt_cm_buf_mgr_destroy(
	IN		ipoib_port_t* const		p_port )
{
	IPOIB_ENTER(IPOIB_DBG_INIT );

	CL_ASSERT( p_port );
	
	/* Free the receive descriptors. */
	if( !p_port->cm_buf_mgr.pool_init )
		return;

	p_port->cm_buf_mgr.pool_init = FALSE;

	/* return CM recv pool elements (descriptors) to the recv pool */
	__cm_buf_mgr_reset( p_port );

	cl_qpool_destroy( &p_port->cm_buf_mgr.recv_pool );

	if( p_port->cm_buf_mgr.h_nbl_pool )
	{
		NdisFreeNetBufferListPool( p_port->cm_buf_mgr.h_nbl_pool );
		p_port->cm_buf_mgr.h_nbl_pool = NULL;
	}

	cl_spinlock_destroy( &p_port->cm_buf_mgr.lock );
	
	IPOIB_EXIT(  IPOIB_DBG_INIT );
}

static cl_status_t
__cm_recv_desc_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	ipoib_cm_recv_desc_t*	p_desc;
	ipoib_port_t*			p_port;
	int						lds, bytes;
	uint8_t*				kva;

	CL_ASSERT( p_object );
	CL_ASSERT( context );

	p_desc = (ipoib_cm_recv_desc_t*)p_object;
	p_port = (ipoib_port_t*)context;

	/*
	 * Allocate Rx buffer (PAGE_SIZE min).
	 * Extra space is allocated for the prefixed Ethernet header which is
	 * synthesized prior to NDIS receive indicate.
	 */
#define BUF_ALIGN 16
	p_desc->alloc_buf_size = p_port->p_adapter->params.cm_xfer_block_size + BUF_ALIGN;

	if( p_desc->alloc_buf_size < PAGE_SIZE )
		p_desc->alloc_buf_size = ROUNDUP( p_desc->alloc_buf_size, PAGE_SIZE );
	else if( p_desc->alloc_buf_size & (BUF_ALIGN - 1) )
		p_desc->alloc_buf_size = ROUNDUP( p_desc->alloc_buf_size, BUF_ALIGN );

	CL_ASSERT( (p_desc->alloc_buf_size / PAGE_SIZE) <= MAX_CM_RECV_SGE ); 

	p_desc->p_alloc_buf = (uint8_t *)ExAllocatePoolWithTag( NonPagedPool,
															p_desc->alloc_buf_size,
															'DOMC' );
	if( p_desc->p_alloc_buf == NULL )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate CM recv buffer size %d bytes.\n",
			p_desc->alloc_buf_size ) );
		return CL_INSUFFICIENT_MEMORY;
	}

	p_desc->p_buf = p_desc->p_alloc_buf + DATA_OFFSET;
	p_desc->buf_size = p_desc->alloc_buf_size - DATA_OFFSET;

	/* Setup RC recv local data segments, mapped by phys page.
	 * Use UD mem key which maps all phys mem, therefore map by phys page must be
	 * be page aligned; no memory register.
	 */
	ASSERT( (((uintn_t)p_desc->p_alloc_buf) & (PAGE_SIZE-1)) == 0 ); // Page aligned?
	kva = p_desc->p_alloc_buf;
	bytes = p_desc->alloc_buf_size;

	for( lds=0; bytes > 0; lds++ )
	{
		p_desc->local_ds[lds].vaddr = cl_get_physaddr( (void*)kva );
		p_desc->local_ds[lds].lkey = p_port->ib_mgr.lkey;
		if( lds == 0 )
		{
			/* get to next PAGE boundry for PAGE alignment as ds[0] short due
			 * to DATA_OFFSET space reservation for the synthesized Ethernet header.
			 */
			p_desc->local_ds[lds].vaddr += DATA_OFFSET;
			if( bytes >= PAGE_SIZE )
			{
				p_desc->local_ds[lds].length = PAGE_SIZE - DATA_OFFSET;
				bytes -= PAGE_SIZE;
			}
			else
			{
				p_desc->local_ds[lds].length = bytes;
				bytes = 0;
			}
		}
		else
		{
			if( bytes >= PAGE_SIZE )
			{
				p_desc->local_ds[lds].length = PAGE_SIZE;
				bytes -= PAGE_SIZE;
			}
			else
			{
				p_desc->local_ds[lds].length = bytes;
				bytes = 0;
			}
		}
		kva += PAGE_SIZE;
	}
	p_desc->wr.num_ds = lds;
	CL_ASSERT( lds <= MAX_CM_RECV_SGE );	// ,+ because lds ++ on for() loop exit.

	/* Setup the work request. */
	p_desc->wr.wr_id = (uintn_t)p_desc;
	p_desc->wr.ds_array = p_desc->local_ds;

	p_desc->type = PKT_TYPE_CM_UCAST;
	p_desc->recv_mode = RECV_RC;

	/* setup NDIS NetworkBufferList and MemoryDescriptorList */
	CL_ASSERT( p_port->p_adapter->h_adapter );

	p_desc->p_mdl = NdisAllocateMdl( p_port->p_adapter->h_adapter,
									 p_desc->p_alloc_buf,
									 p_desc->buf_size );
	if( !p_desc->p_mdl )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate MDL\n") );
		goto err1;
	}
	
	CL_ASSERT( p_port->cm_buf_mgr.h_nbl_pool );
	p_desc->p_NBL = NdisAllocateNetBufferAndNetBufferList(
													p_port->cm_buf_mgr.h_nbl_pool,
													0,
													0,
													p_desc->p_mdl,
													0,
													0 );
	if( !p_desc->p_NBL )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate NET_BUFFER_LIST\n") );
		goto err2;
	}

#if NDIS_HINTS
	NdisClearNblFlag( p_desc->p_NBL,
					  ( NDIS_NBL_FLAGS_IS_IPV4 | NDIS_NBL_FLAGS_IS_IPV6 
					  | NDIS_NBL_FLAGS_IS_TCP | NDIS_NBL_FLAGS_IS_UDP) ); 
#endif
	NET_BUFFER_LIST_NEXT_NBL(p_desc->p_NBL) = NULL;
	IPOIB_PORT_FROM_NBL( p_desc->p_NBL ) = p_port;
	IPOIB_CM_RECV_FROM_NBL( p_desc->p_NBL ) = p_desc;
	p_desc->p_NBL->SourceHandle = p_port->p_adapter->h_adapter;

	*pp_pool_item = &p_desc->item;

	return CL_SUCCESS;

err2:
	NdisFreeMdl( p_desc->p_mdl );
	p_desc->p_mdl = NULL;
err1:
	ExFreePoolWithTag( p_desc->p_alloc_buf, 'DOMC' );
	p_desc->p_alloc_buf = NULL;
	p_desc->p_buf = NULL;

	return CL_INSUFFICIENT_MEMORY;
}


static void
__cm_recv_desc_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void						*context )
{
	ipoib_cm_recv_desc_t	*p_desc;

	if( p_pool_item == NULL || context == NULL )
		return;

	p_desc = PARENT_STRUCT( p_pool_item, ipoib_cm_recv_desc_t, item );

	if( p_desc->p_mdl )
	{
		NdisFreeMdl( p_desc->p_mdl );
		p_desc->p_mdl = NULL;
	}

	if( p_desc->p_NBL)
	{
		NdisFreeNetBufferList(p_desc->p_NBL);
		p_desc->p_NBL = NULL;
	}

	if( p_desc->p_alloc_buf )
		ExFreePoolWithTag( p_desc->p_alloc_buf, 'DOMC' );

	p_desc->p_alloc_buf = NULL;
	p_desc->p_buf = NULL;
}


static void
__cm_buf_mgr_put_recv(
	IN		ipoib_port_t * const		p_port,
	IN		ipoib_cm_recv_desc_t* const	p_desc,
	IN		BOOLEAN						update,
	IN		NET_BUFFER_LIST* const	p_net_buffer_list OPTIONAL )
{
	cm_buf_mgr_t * const	p_buf_mgr = &p_port->cm_buf_mgr;

	IPOIB_ENTER(IPOIB_DBG_RECV );

	if( p_net_buffer_list )
	{
		MDL			*p_mdl = NULL;
		NET_BUFFER	*p_nbuf = NULL;

		ASSERT( p_desc->p_NBL );
		ASSERT( p_desc->p_mdl );
		ASSERT( p_net_buffer_list == p_desc->p_NBL );

		NET_BUFFER_LIST_NEXT_NBL(p_net_buffer_list) = NULL;

		p_nbuf = NET_BUFFER_LIST_FIRST_NB( p_net_buffer_list );
		p_mdl = NET_BUFFER_FIRST_MDL( p_nbuf );	

		ASSERT( p_mdl == p_desc->p_mdl );
		ASSERT( NET_BUFFER_CURRENT_MDL( p_nbuf ) == p_mdl );

		/* reset Avail buffer lengths to full Rx size */
		NET_BUFFER_DATA_LENGTH( p_nbuf ) = p_desc->buf_size;
		NdisAdjustMdlLength( p_mdl, p_desc->buf_size );

#if NDIS_HINTS
		NdisClearNblFlag( p_desc->p_NBL,
						  ( NDIS_NBL_FLAGS_IS_IPV4 | NDIS_NBL_FLAGS_IS_IPV6 
						  | NDIS_NBL_FLAGS_IS_TCP | NDIS_NBL_FLAGS_IS_UDP) ); 
#endif
	}

	cl_spinlock_acquire( &p_buf_mgr->lock );

	/* Remove buffer from the posted list & return the descriptor to it's pool. */
	cl_qlist_remove_item( &p_buf_mgr->oop_list, &p_desc->list_item );
	p_desc->p_endpt = NULL;
	cl_qpool_put( &p_buf_mgr->recv_pool, &p_desc->item );

	/* case exists where .posted field is updated in cm_recv_cb() hence we skip the
	 * update here as the recv desc has finally come back to the pool after being
	 * freed up.
	 */
	if( update )
		InterlockedDecrement( &p_port->cm_buf_mgr.posted );

	cl_spinlock_release( &p_buf_mgr->lock );

	IPOIB_EXIT( IPOIB_DBG_RECV );
}

/*
 * routine called when NDIS is returning a receive NBL (Network Buffer List)
 * and the Rx buffer was determined via the p_desc field 'recv_mode' to be
 * a CM Rx buffer.
 * Repost the buffer to the SRQ.
 * see ipoib_port.cpp::__free_received_NBL()
 */
void
ipoib_cm_buf_mgr_put_recv(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_cm_recv_desc_t* const	p_desc,
	IN		NET_BUFFER_LIST* const		p_NBL OPTIONAL )
{
	ib_api_status_t	ib_status = IB_SUCCESS;
	ib_recv_wr_t	*p_failed_wc = NULL;

	/* free the Net Buffer List and MDL */
	if( p_NBL )
	{
		NET_BUFFER	*p_nbuf = NULL;
		MDL			*p_mdl = NULL;

		ASSERT( p_NBL == p_desc->p_NBL );
		NET_BUFFER_LIST_NEXT_NBL(p_NBL) = NULL;
		p_nbuf = NET_BUFFER_LIST_FIRST_NB( p_NBL );
		p_mdl = NET_BUFFER_FIRST_MDL( p_nbuf );	
		ASSERT( p_mdl == p_desc->p_mdl );
		ASSERT( NET_BUFFER_CURRENT_MDL( p_nbuf ) == p_mdl );

		/* reset Avail buffer lengths to full Rx size */
		NET_BUFFER_DATA_LENGTH( p_nbuf ) = p_desc->buf_size;
		NdisAdjustMdlLength( p_mdl, p_desc->buf_size );

#if NDIS_HINTS
		NdisClearNblFlag( p_desc->p_NBL,
						  ( NDIS_NBL_FLAGS_IS_IPV4 | NDIS_NBL_FLAGS_IS_IPV6 
						  | NDIS_NBL_FLAGS_IS_TCP | NDIS_NBL_FLAGS_IS_UDP) ); 
#endif
	}

	/* repost RC Rx desc */
	p_desc->wr.p_next = NULL; // just 1 buffer.

	ib_status = p_port->p_adapter->p_ifc->post_srq_recv( p_port->ib_mgr.h_srq,
														 &p_desc->wr,
														 &p_failed_wc );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("post_srq_recv() returned %s\n", 
				p_port->p_adapter->p_ifc->get_err_str( ib_status )) );
			
		/* return descriptor back to the CM RX pool, buffer accounted for in
		 * cm_recv_internal().
		 */
		__cm_buf_mgr_put_recv( p_port, p_desc, FALSE, NULL );
	}
	else
	{
		/* adjust buffer accounting as cm_recv_internal() did the decrement. */
		InterlockedIncrement( &p_port->cm_buf_mgr.posted );
	}
}


static void
__cm_buf_mgr_put_recv_list(
	IN		ipoib_port_t* const			p_port,
	IN		cl_qlist_t* const			p_list )
{
	cm_buf_mgr_t* 		p_buf_mgr = &p_port->cm_buf_mgr;
	ipoib_cm_recv_desc_t*	p_desc;
	cl_list_item_t*			p_item;

	IPOIB_ENTER( IPOIB_DBG_RECV );

	cl_spinlock_acquire( &p_buf_mgr->lock);
	p_item = cl_qlist_remove_head( p_list );

	while( p_item != cl_qlist_end( p_list ) )
	{
		p_desc = (ipoib_cm_recv_desc_t*)p_item;
		ASSERT( p_desc->p_endpt );
		cl_qlist_remove_item( &p_buf_mgr->oop_list, &p_desc->list_item );
		p_desc->p_endpt = NULL;
		/* Return the descriptor to it's global port buffer pool. */
		cl_qpool_put( &p_buf_mgr->recv_pool, &p_desc->item );
		InterlockedDecrement( &p_port->cm_buf_mgr.posted );
		p_item = cl_qlist_remove_head( p_list );
	}
	cl_spinlock_release( &p_buf_mgr->lock);

	IPOIB_EXIT( IPOIB_DBG_RECV );
}


static int32_t
__cm_recv_mgr_filter(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt,
	IN		ib_wc_t* const				p_done_wc_list,
	OUT		cl_qlist_t* const			p_done_list,
	OUT		cl_qlist_t* const			p_bad_list )
{
	ib_api_status_t			ib_status;
	ipoib_cm_recv_desc_t	*p_desc;
	ib_wc_t					*p_wc;
	ipoib_pkt_t				*p_ipoib;
	eth_pkt_t				*p_eth;
	int32_t					recv_cnt=0;
	uint32_t				len;

	IPOIB_ENTER( IPOIB_DBG_CM_RECV );

	ASSERT( ipoib_endpt_parent( p_endpt ) == p_port );

	for( p_wc = p_done_wc_list; p_wc; p_wc = p_wc->p_next )
	{
		p_desc = (ipoib_cm_recv_desc_t *)(uintn_t)p_wc->wr_id;
		p_desc->p_endpt = p_endpt; // use correct EndPoint pointer.
		recv_cnt++;
		if(  p_wc->status != IB_WCS_SUCCESS )
		{
			if( p_wc->status != IB_WCS_WR_FLUSHED_ERR )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("EP %s Failed RC completion %s (vendor specific %#x)\n",
					p_endpt->tag, p_endpt->p_ifc->get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific) );
			}
			else
			{
				IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
					("EP %s Flushed RC completion %s\n",
					p_endpt->tag, p_endpt->p_ifc->get_wc_status_str(p_wc->status)));
			}
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			continue;
		}

		if( p_wc->length < sizeof(ipoib_hdr_t) )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received ETH packet(%d) < min size(%d)\n",
					p_wc->length, sizeof(ipoib_hdr_t)) );
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			continue;
		}

		if( (p_wc->length - sizeof(ipoib_hdr_t)) >
					p_port->p_adapter->params.cm_payload_mtu )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received ETH packet len %d > CM payload MTU (%d)\n",
				(p_wc->length - sizeof(ipoib_hdr_t)),
				p_port->p_adapter->params.cm_payload_mtu) );
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			continue;
		}

		/*
		 * Successful RX completion.
		 * Setup the ethernet/ip/arp header and queue descriptor for NDIS report.
		 */
		ib_status = IB_SUCCESS;
		p_desc->csum.Value = (PVOID)(uintn_t)
					(( p_wc->recv.conn.recv_opt & IB_RECV_OPT_CSUM_MASK ) >> 8);	

		/* WARNING - ipoib header overwritten with Ethernet header for NDIS Rx.
		 * What p_eth points at is invalid; initialized further on.
		 */
#if 0 // TODO - IPoIB-CM RFC wants support for GRH?
		if( p_wc->recv.conn.recv_opt & IB_RECV_OPT_GRH_VALID )
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("RC Rx with GRH\n") );
			CL_ASSERT(0);
			p_ipoib = (ipoib_pkt_t *)(p_desc->p_buf + sizeof(ib_grh_t));
			p_eth = (eth_pkt_t *)
						((p_desc->p_buf + sizeof(ib_grh_t) + sizeof(ipoib_hdr_t))
							- sizeof(eth_pkt_t));
		}
		else
#endif
		{
			p_ipoib = (ipoib_pkt_t *) p_desc->p_buf;
			p_eth = (eth_pkt_t*) p_desc->p_alloc_buf;
		}

		//__debugbreak();
		
		CL_ASSERT( p_desc->recv_mode == RECV_RC );

		switch( p_ipoib->hdr.type )
		{
		case ETH_PROT_TYPE_ARP:
			if( p_wc->length < (sizeof(ipoib_hdr_t) + sizeof(ipoib_arp_pkt_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received ARP packet too short (wc_len %d)\n", p_wc->length) );
				ib_status = IB_ERROR;
				break;
			}
			ib_status = __endpt_cm_recv_arp( p_port, p_ipoib, p_eth, p_endpt );
			break;

		case ETH_PROT_TYPE_IP:
			if( p_wc->length < (sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received IP packet too short (wc_len %d)\n", p_wc->length) );
				ib_status = IB_ERROR;
				break;
			}

#if NDIS_HINTS
			NdisSetNblFlag( p_desc->p_NBL, NDIS_NBL_FLAGS_IS_IPV4 ); 
#endif

			if( p_ipoib->type.ip.hdr.prot == IP_PROT_UDP )
			{
#if NDIS_HINTS
				NdisSetNblFlag( p_desc->p_NBL, NDIS_NBL_FLAGS_IS_UDP ); 
#endif
				ib_status = __endpt_cm_recv_udp( p_port,
												 p_wc,
												 p_ipoib,
												 p_eth,
												 p_endpt );
			}
			else if( p_ipoib->type.ip.hdr.prot == IP_PROT_TCP )
			{
				len = sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t);
				if( p_wc->length < len )
				{
					IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Received short TCP packet wc_len %d < %d[ipoib+IP+TCP]\n",
							p_wc->length, len) );
					ib_status = IB_ERROR;
				}
#if NDIS_HINTS
				NdisSetNblFlag( p_desc->p_NBL, NDIS_NBL_FLAGS_IS_TCP ); 
#endif
			}
			break;

		case ETH_PROT_TYPE_IPV6:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_RECV,
				("RC-Received IPv6\n") );
#if NDIS_HINTS
			NdisSetNblFlag( p_desc->p_NBL, NDIS_NBL_FLAGS_IS_IPV6 ); 
#endif
			break;

		default:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("RC-Received ?? ipoib packet (%#x) %s desc->p_buf %p\n",
					p_ipoib->hdr.type, get_eth_packet_type_str(p_ipoib->hdr.type),
					p_desc->p_buf) );
			break;
		}

		if( ib_status != IB_SUCCESS )
		{
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			continue;
		}

		/*
		 * Synthesize an Ethernet header for NDIS recv.
		 * WARNING - Enet header overlays the ipoib header.
		 * Result is an Enet header + [ipoib packet - ipoib header] + wc_len payload.
		 */
		p_eth->hdr.type = p_ipoib->hdr.type;
		p_eth->hdr.src = p_endpt->mac;
		p_eth->hdr.dst = p_port->p_adapter->mac;

		/* set Ethernet frame length: (p_buf != p_eth)
		 *   wc_length + (sizeof(eth_hdr_t) - sizeof(ipoib_hdr_t))
		 */
		p_desc->len = p_wc->length + DATA_OFFSET;

		cl_qlist_insert_tail( p_done_list, &p_desc->item.list_item );
	}

	IPOIB_EXIT( IPOIB_DBG_CM_RECV );
	return recv_cnt;
}


static ib_api_status_t
__endpt_cm_recv_arp(
	IN		ipoib_port_t* const				p_port,
	IN		const	ipoib_pkt_t* const		p_ipoib,
	OUT		eth_pkt_t* const				p_eth,
	IN		ipoib_endpt_t* const			p_src_endpt )
{
	const ipoib_arp_pkt_t	*p_ib_arp;
	arp_pkt_t				*p_arp;
	
	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_ib_arp = &p_ipoib->type.arp;
	p_arp = &p_eth->type.arp;
	
	if( p_ib_arp->hw_type != ARP_HW_TYPE_IB ||
		p_ib_arp->hw_size != sizeof(ipoib_hw_addr_t) ||
		p_ib_arp->prot_type != ETH_PROT_TYPE_IP )
	{
		IPOIB_EXIT( IPOIB_DBG_ENDPT );
		return IB_ERROR;
	}
	
	p_arp->hw_type = ARP_HW_TYPE_ETH;
	p_arp->hw_size = sizeof(mac_addr_t);
	p_arp->src_hw = p_src_endpt->mac;
	p_arp->src_ip = p_ib_arp->src_ip;
	p_arp->dst_hw = p_port->p_local_endpt->mac;
	p_arp->dst_ip = p_ib_arp->dst_ip;

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return IB_SUCCESS;	
}

static ib_api_status_t
__endpt_cm_recv_udp(
	IN	ipoib_port_t* const	p_port,
	IN			ib_wc_t* const				p_wc,
	IN		const	ipoib_pkt_t* const		p_ipoib,
	OUT			eth_pkt_t* const			p_eth,
	IN			ipoib_endpt_t* const		p_src_endpt )
{
	ib_api_status_t ib_status = IB_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	if( p_wc->length <
		(sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t) + sizeof(udp_hdr_t)) )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Received UDP packet too short\n") );
		IPOIB_EXIT( IPOIB_DBG_ENDPT );
		return IB_ERROR;
	}
	if( __cm_recv_is_dhcp( p_ipoib ) )
	{
		ib_status = ipoib_recv_dhcp( p_port,
									 p_ipoib,
									 p_eth,
									 p_src_endpt,
									 p_port->p_local_endpt );
	}

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return ib_status;
}

static boolean_t
__cm_recv_is_dhcp(
	IN	const ipoib_pkt_t* const	p_ipoib )
{
	return( (p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_SERVER &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_CLIENT) ||
				(p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_CLIENT &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_SERVER) );
}

#endif /* IPOIB_CM */
