/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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


#ifndef _IPOIB_ENDPOINT_H_
#define _IPOIB_ENDPOINT_H_


#include <iba/ib_al.h>
#include <complib/cl_qlist.h>
#include <complib/cl_qmap.h>
#include <complib/cl_fleximap.h>
#include <complib/cl_obj.h>
#include "iba/ipoib_ifc.h"
#include <ip_packet.h>
#include "ipoib_debug.h"


typedef struct _cm_buf_mgr
{
	cl_qpool_t			recv_pool;
	NDIS_HANDLE			h_nbl_pool;
	cl_spinlock_t		lock;
	cl_qlist_t			oop_list;
	long				posted;
	int32_t				recv_pool_depth;
	boolean_t			pool_init;
} cm_buf_mgr_t;
/*
* FIELDS
*	recv_pool
*		recv descriptor pool - shared by all endpoints & posted to SRQ.
*
*	h_nbl_pool
*		handle to the pool of NDIS NETWORK_BUFFER_LISTs
*
*	lock
*		serialize access to the recv_pool & oop_list.
*
*	oop_list
*		list of recv pool buffers which are Out-Of-Pool.
*		(SRQ bound QPs do not flush buffers on transition to QP Error state, this
*		list tracks recv pool buffers by hand).
*
*	posted
*		Number of outstanding recv pool elements on the oop_list.
*		Normally these pool elements are posted to the SRQ (for all endpoints)
*		CM recv code wants to keep (posted == params.rq_depth).
*
*	recv_pool_depth
*		Total number of recv descriptor pool elements.
*
*	pool_init
*		boolean: TRUE == recv pool has been initialized.
*/

typedef struct _endpt_recv_mgr
{
	NET_BUFFER_LIST	*NBL;
	cl_qlist_t		done_list;

}	endpt_recv_mgr_t;
/*
* FIELDS
*	NBL
*		Linked list of one or more NBL's (Network Buffer Lists) chained together.
*		Each NBL points @ a ipoib_cm_desc_t->p_buf of RC received packet data.
*
*	done_list
*		list of completed WR (work Requests) which have passed recv filtering.
*		Passed in that the WRs can be converted into NBLs and passed up to NDIS.
*/


typedef enum _cm_state 
{
	IPOIB_CM_DISCONNECTED,
	IPOIB_CM_QUEUED_TO_CONNECT,
	IPOIB_CM_CONNECTING,
	IPOIB_CM_CONNECTED,
	IPOIB_CM_LISTEN,
	IPOIB_CM_DREP_SENT,
	IPOIB_CM_DREQ_SENT,
	IPOIB_CM_DISCONNECT_CLEANUP,
	IPOIB_CM_DESTROY

} cm_state_t;

typedef struct _cm_private_data 
{
	ib_net32_t		ud_qpn;
	ib_net32_t		recv_mtu;
} cm_private_data_t;

typedef struct _endpt_conn 
{
	ib_net64_t			service_id;
	cm_private_data_t	private_data;
	ib_qp_handle_t		h_send_qp;
	ib_qp_handle_t		h_send_qp_err;
	ib_cq_handle_t		h_send_cq;
	ib_qp_handle_t		h_recv_qp;
	ib_cq_handle_t		h_recv_cq;
	ib_listen_handle_t  h_cm_listen;
	cm_state_t			state;

} endpt_conn_t;

/*
* FIELDS
*	service_id
*		listen() on this service ID
*
*	private_data
*		private data received from remote side.
*
*	h_send_qp
*		RC qp for send
*
*	h_send_qp_err
*		If !null, then copy of h_send_qp prior to setting h_send_qp == NULL.
*		QP is in error state, awaiting destroy.
*
*	h_recv_qp
*		Rx RC qp handle
*
*	h_send_cq
*		Tx CQ handle
*
*	h_recv_cq
*		Rx CQ handle
*
*	h_cm_listen
*		listen()ing CM handle.
*
*	state
*		connection state for the active connection only.
*
* NOTES
*	IpoIB Connect Mode (CM) connection protocol in a nutshell.
*	An IPoIB interface encodes it CM capability in the hardware address it publishes.
*	Once NDIS hands a Network Buffer to IPoIB for transmission (unicast), IPoIB needs
*	to resolve the hardware address to IB adress information (LID, SL etc.).
*	Once resolution is complete, an RC connection is forged with the remote host
*	using IBAL CM.
*	The connection process is symetrical - in that, for two communicating hosts
*	there are two RC connections such that each connection works half duplex. 
*	CM state variable is that of the 'active' connection (h_send_qp), the passive 
*	side (h_recv_qp) setup does not alter the CM state variable.
*
*********/

typedef struct _ipoib_endpt
{
	cl_obj_t				obj;
	cl_obj_rel_t			rel;
	cl_map_item_t			mac_item;
	cl_fmap_item_t			gid_item;
	cl_map_item_t			lid_item;
	cl_fmap_item_t			conn_item;
	LIST_ENTRY				list_item;
	ib_query_handle_t		h_query;
	ib_mcast_handle_t		h_mcast;
	mac_addr_t				mac;
	net16_t					dlid;
	ib_gid_t				dgid;
	net32_t					qpn;
	ib_av_handle_t			h_av;
	ib_al_ifc_t				*p_ifc;
	boolean_t    			is_in_use;
	boolean_t				is_mcast_listener;
	endpt_recv_mgr_t		cm_recv;
	endpt_conn_t			conn;
	uint32_t				tx_mtu;
	uint8_t					cm_flag;
	uint8_t					cm_rx_flushing;
	uint8_t					cm_ep_destroy;
	char					tag[24]; // <(Broad/Multi)-cast or 0xLID> string

}	ipoib_endpt_t;
/*
* FIELDS
*	obj
*		Pointer to the EndPoint object proper.
*
*	rel
*		Object relations - used to convert from EndPoint to Port struct pointer.
*
*	gid_item
*		Map item for storing the endpoint in a map. key is destination GID.
*
*	lid_item
*		Map item for storing the endpoint in a map. key is destination LID.
*
*	conn_item
*		Map item for storing the endpoint in a connect map. key is hardware MAC.
*
*	mac_item
*		Map item for storing the endpoint in a map. key is destination MAC address.
*
*	list_item
*		used when emdpoint is on the connection list.
*
*	h_query
*		Query handle for cancelling SA queries.
*
*	h_mcast
*		For multicast endpoints, the multicast handle.
*
*	mac
*		MAC address; next 2 bytes make for even alignment.
*
*	dlid
*		Destination LID.  The destination LID is only set for endpoints
*		that are on the same subnet.  It is used as key in the LID map.
*
*	dgid
*		Destination GID.
*
*	qpn
*		Destination UD queue pair number.
*
*	h_av
*		Address vector for sending data.
*
*	p_ifc
*		Reference to transport functions, can be used
*		while endpoint is not attached to port yet.
*
*	is_in_use
*		Endpoint is a member of a mcast group.
*
*	is_mcast_listener
*
*	cm_recv
*		Manage NDIS NBLs (Network Buffer List) and completed recv work-requests.
*
*	conn
*		for connected mode endpoints, IB RC connection info; includes conn state.
*
*	tx_mtu
*		current MTU; starts as UD MTU, when CM connected then
*		tx_mtu = params.cm_payload_mtu, otherwise revert back to UD mtu. 
*
*	cm_flag
*		!= 0 implies CM capable.
*
*	cm_rx_flushing
*		!= 0, CM is flushing SRQ QPs; defer object destroy until done flushing.
*
*	cm_ep_destroy
*		SRQ async error routine should destroy the EP object.
*
*	tag
*		Endpoint tag string: asciz string 'lid 0xNNN'
*
* NOTES
*	If the h_mcast member is set, the endpoint is never expired.
*********/


ipoib_endpt_t*
ipoib_endpt_create(
	IN		const	ipoib_port_t* const			p_port,
	IN		const	ib_gid_t* const				p_dgid,
	IN		const	net16_t						dlid,
	IN		const	net32_t						qpn );


ib_api_status_t
ipoib_endpt_set_mcast(
	IN				ipoib_endpt_t* const		p_endpt,
	IN				ib_pd_handle_t				h_pd,
	IN				uint8_t						port_num,
	IN				ib_mcast_rec_t* const		p_mcast_rec );


static inline void
ipoib_endpt_ref(
	IN				ipoib_endpt_t* const		p_endpt )
{
	CL_ASSERT( p_endpt );

	cl_obj_ref( &p_endpt->obj );
#if DBG
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
		("Endpt %s ++refcount %d\n", p_endpt->tag, p_endpt->obj.ref_cnt));
#endif
	/*
	 * Anytime we reference the endpoint, we're either receiving data
	 * or trying to send data to that endpoint.  Clear the expired flag
	 * to prevent the AV from being flushed.
	 */
}


static inline void
ipoib_endpt_deref(
	IN				ipoib_endpt_t* const		p_endpt )
{
	CL_ASSERT(p_endpt->obj.ref_cnt);
	cl_obj_deref( &p_endpt->obj );
#if DBG
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
		("Endpt %s --refcount %d\n", p_endpt->tag, p_endpt->obj.ref_cnt));
#endif
}

void ipoib_endpt_cm_mgr_thread(
	IN			void*					p_context );

NDIS_STATUS
ipoib_endpt_queue(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt );

struct _ipoib_port *
ipoib_endpt_parent(
	IN		ipoib_endpt_t* const		p_endpt );

void
endpt_unmap_conn_dgid(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt );

static inline cm_state_t
endpt_cm_set_state(
	IN		ipoib_endpt_t* const		p_endpt,
	IN		cm_state_t					state )
{
	return (cm_state_t) InterlockedExchange( (volatile LONG *)&p_endpt->conn.state, 
											 (LONG)state );
}

static inline cm_state_t
endpt_cm_get_state(
	IN		ipoib_endpt_t* const		p_endpt )
{
	return( cm_state_t )InterlockedCompareExchange( 
				(volatile LONG *)&p_endpt->conn.state, 
				IPOIB_CM_DISCONNECTED, IPOIB_CM_DISCONNECTED );
}

ib_api_status_t
endpt_cm_connect(
	IN		ipoib_endpt_t* const		p_endpt );

void
endpt_queue_cm_connection(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt );

void
cm_release_resources(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt,
	IN		int							which_res );

void
cm_destroy_recv_resources(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt );


char *
cm_get_state_str(
	IN		cm_state_t );

#endif	/* _IPOIB_ENDPOINT_H_ */
