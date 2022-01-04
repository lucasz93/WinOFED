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



#ifndef _IPOIB_PORT_H_
#define _IPOIB_PORT_H_


#include <iba/ib_al.h>
#include <complib/cl_obj.h>
#include <complib/cl_qmap.h>
#include <complib/cl_fleximap.h>
#include <ip_packet.h>
#include "ipoib_endpoint.h"
#include "ipoib_xfr_mgr.h"


/*
 * Define to place receive buffer inline in receive descriptor.
 */
#define IPOIB_INLINE_RECV	1


/* Max send data segment list size. */
#define MAX_SEND_SGE	30 //TODO optimize this value


/* 
 *  Invalid pkey index
 */
#define PKEY_INVALID_INDEX	0xFFFF

/*
 * Define to control how transfers are done.  When defined as 1, causes
 * packets to be sent using NDIS DMA facilities (getting the SGL from the
 * packet).  When defined as 0, uses the NDIS_BUFFER structures as MDLs
 * to get the physical page mappings of the buffers.
 */
#define IPOIB_USE_DMA	1


#define IPOIB_PORT_FROM_PACKET( P )	\
	(((ipoib_port_t**)P->MiniportReservedEx)[0])
#define IPOIB_ENDPT_FROM_PACKET( P )	\
	(((ipoib_endpt_t**)P->MiniportReservedEx)[1])
#define IPOIB_RECV_FROM_PACKET( P )	\
	(((ipoib_recv_desc_t**)P->MiniportReservedEx)[1])
#define IPOIB_SEND_FROM_PACKET( P )		\
	(((send_buf_t**)P->MiniportReservedEx)[2])
#define IPOIB_PACKET_FROM_LIST_ITEM( I ) \
	(PARENT_STRUCT( I, NDIS_PACKET, MiniportReservedEx ))
#define IPOIB_LIST_ITEM_FROM_PACKET( P ) \
	((cl_list_item_t*)P->MiniportReservedEx)


typedef struct _ipoib_ib_mgr
{
	ib_ca_handle_t			h_ca;
	ib_pd_handle_t			h_pd;
	ib_cq_handle_t			h_recv_cq;
	ib_cq_handle_t			h_send_cq;
	ib_qp_handle_t			h_qp;
	ib_query_handle_t		h_query;
	net32_t					qpn;

	ib_mr_handle_t			h_mr;
	net32_t					lkey;

	uint8_t					rate;
	ib_member_rec_t			bcast_rec;

}	ipoib_ib_mgr_t;
/*
* FIELDS
*	h_ca
*		CA handle for all IB resources.
*
*	h_pd
*		PD handle for all IB resources.
*
*	h_recv_cq
*		Recv CQ handle.
*
*	h_send_cq
*		Send CQ handle.
*
*	h_qp
*		QP handle for data transfers.
*
*	h_query
*		Query handle for cancelling SA queries.
*
*	h_mr
*		Registration handle for all of physical memory.  Used for
*		send/receive buffers to simplify growing the receive pool.
*
*	lkey
*		LKey for the memory region.
*
*	bcast_rec
*		Cached information about the broadcast group, used to specify
*		parameters used to join other multicast groups.
*********/


#include <complib/cl_packon.h>
/****s* IPoIB Driver/ipoib_hdr_t
* NAME
*	ipoib_hdr_t
*
* DESCRIPTION
*	IPoIB packet header.
*
* SYNOPSIS
*/
typedef struct _ipoib_hdr
{
	net16_t			type;
	net16_t			resv;

}	PACK_SUFFIX ipoib_hdr_t;
/*
* FIELDS
*	type
*		Protocol type.
*
*	resv
*		Reserved portion of IPoIB header.
*********/

typedef struct _ipoib_arp_pkt
{
	net16_t			hw_type;
	net16_t			prot_type;
	uint8_t			hw_size;
	uint8_t			prot_size;
	net16_t			op;
	ipoib_hw_addr_t	src_hw;
	net32_t			src_ip;
	ipoib_hw_addr_t	dst_hw;
	net32_t			dst_ip;

}	PACK_SUFFIX ipoib_arp_pkt_t;


/****s* IPoIB Driver/ipoib_pkt_t
* NAME
*	ipoib_pkt_t
*
* DESCRIPTION
*	Represents an IPoIB packet with no GRH.
*
* SYNOPSIS
*/
typedef struct _ipoib_pkt
{
	ipoib_hdr_t		hdr;
	union _payload
	{
		uint8_t			data[MAX_PAYLOAD_MTU];
		ipoib_arp_pkt_t	arp;
		ip_pkt_t		ip;

	}	PACK_SUFFIX type;

}	PACK_SUFFIX ipoib_pkt_t;
/*
* FIELDS
*	hdr
*		IPoIB header.
*
*	type
*		Union for different types of payloads.
*
*	type.data
*		raw packet.
*
*	type.ib_arp
*		IPoIB ARP packet.
*
*	type.arp
*		Ethernet ARP packet.
*
*	type.ip
*		IP packet.
*********/


/****s* IPoIB Driver/recv_buf_t
* NAME
*	recv_buf_t
*
* DESCRIPTION
*	Represents a receive buffer, including the ethernet header
*	used to indicate the receive to the OS.
*
* SYNOPSIS
*/
typedef union _recv_buf
{
	struct _recv_buf_type_eth
	{
		uint8_t			pad[sizeof(ib_grh_t) +
							sizeof(ipoib_hdr_t) -
							sizeof(eth_hdr_t)];
		eth_pkt_t		pkt;	/* data starts at sizeof(grh)+sizeof(eth_hdr) */

	}	PACK_SUFFIX eth;

	struct _recv_buf_type_ib
	{
		ib_grh_t		grh;	/* Must be same offset as lcl_rt.ib.pkt */
		ipoib_pkt_t		pkt;	/* data starts at 10+grh+4 */

	}	PACK_SUFFIX ib;

}	PACK_SUFFIX recv_buf_t;
/*
* FIELDS
*	eth.pkt
*		Ethernet packet, used to indicate the receive to the OS.
*
*	ib.grh
*		GRH for a globally routed received packet.
*
*	ib.pkt
*		IPOIB packet representing a globally routed received packet.
*
* NOTES
*	When posting the work request, the address of ib.grh is used.
*
*	TODO: Do we need a pad to offset the header so that the data ends up
*	aligned on a pointer boundary?
*********/

/****s* IPoIB Driver/send_buf_t
* NAME
*	send_buf_t
*
* DESCRIPTION
*	Represents a send buffer, used to convert packets to IPoIB format.
*
* SYNOPSIS
*/
typedef union _send_buf
{
	uint8_t			data[MAX_PAYLOAD_MTU];
	ipoib_arp_pkt_t	arp;
	ip_pkt_t		ip;

}	PACK_SUFFIX send_buf_t;
/*
* FIELDS
*	data
*		IP/ARP packet.
*
* NOTES
*	TODO: Do we need a pad to offset the header so that the data ends up
*	aligned on a pointer boundary?
*********/
#include <complib/cl_packoff.h>


typedef struct _ipoib_buf_mgr
{
	cl_qpool_t			recv_pool;

	NDIS_HANDLE			h_packet_pool;
	NDIS_HANDLE			h_buffer_pool;

	NPAGED_LOOKASIDE_LIST	send_buf_list;
	NDIS_HANDLE			h_send_pkt_pool;
	NDIS_HANDLE			h_send_buf_pool;

}	ipoib_buf_mgr_t;
/*
* FIELDS
*	recv_pool
*		Pool of ipoib_recv_desc_t structures.
*
*	h_packet_pool
*		NDIS packet pool, used to indicate receives to NDIS.
*
*	h_buffer_pool
*		NDIS buffer pool, used to indicate receives to NDIS.
*
*	send_buf_list
*		Lookaside list for dynamically allocating send buffers for send
*		that require copies (ARP, DHCP, and any with more physical pages
*		than can fit in the local data segments).
*********/


typedef enum _ipoib_pkt_type
{
	PKT_TYPE_UCAST,
	PKT_TYPE_BCAST,
	PKT_TYPE_MCAST

}	ipoib_pkt_type_t;


typedef struct _ipoib_recv_desc
{
	cl_pool_item_t		item;	/* Must be first. */
	uint32_t			len;
	ipoib_pkt_type_t	type;
	ib_recv_wr_t		wr;
	ib_local_ds_t		local_ds[2];
	NDIS_TCP_IP_CHECKSUM_PACKET_INFO	ndis_csum;
#if IPOIB_INLINE_RECV
	recv_buf_t			buf;
#else
	recv_buf_t			*p_buf;
#endif

}	ipoib_recv_desc_t;
/*
* FIELDS
*	item
*		Pool item for storing descriptors in a pool.
*
*	len
*		Length to indicate to NDIS.  This is different than the length of the
*		received data as some data is IPoIB specific and filtered out.
*
*	type
*		Type of packet, used in filtering received packets against the packet
*		filter.  Also used to update stats.
*
*	wr
*		Receive work request.
*
*	local_ds
*		Local data segments.  The second segment is only used if a buffer
*		spans physical pages.
*
*	buf
*		Buffer for the receive.
*
* NOTES
*	The pool item is always first to allow casting form a cl_pool_item_t or
*	cl_list_item_t to the descriptor.
*********/


typedef struct _ipoib_send_desc
{
	NDIS_PACKET			*p_pkt;
	ipoib_endpt_t		*p_endpt1;
	send_buf_t			*p_buf;
	ib_send_wr_t		wr;
	ipoib_hdr_t			pkt_hdr;
	ib_local_ds_t		local_ds[MAX_SEND_SGE];	/* Must be last. */

}	ipoib_send_desc_t;
/*
* FIELDS
*	p_pkt
*		Pointer to the NDIS_PACKET associated with the send operation.
*
*	p_endpt
*		Endpoint for this send.
*
*	p_buf
*		Buffer for the send, if allocated.
*
*	wr
*		Send work request.
*
*	pkt_hdr
*		IPoIB packet header, pointed to by the first local datasegment.
*
*	local_ds
*		Local data segment array.  Placed last to allow allocating beyond the
*		end of the descriptor for additional datasegments.
*
* NOTES
*	The pool item is always first to allow casting form a cl_pool_item_t or
*	cl_list_item_t to the descriptor.
*********/


typedef struct _ipoib_recv_mgr
{
	int32_t			depth;

	NDIS_PACKET		**recv_pkt_array;

	cl_qlist_t		done_list;

}	ipoib_recv_mgr_t;
/*
* FIELDS
*	depth
*		Current number of WRs posted.
*
*	p_head
*		Pointer to work completion in descriptor at the head of the QP.
*
*	p_tail
*		Pointer to the work completion in the descriptor at the tail of the QP.
*
*	recv_pkt_array
*		Array of pointers to NDIS_PACKET used to indicate receives.
*
*	done_list
*		List of receive descriptors that need to be indicated to NDIS.
*********/


typedef struct _ipoib_send_mgr
{
	atomic32_t		depth;
	cl_qlist_t		pending_list;

}	ipoib_send_mgr_t;
/*
* FIELDS
*	depth
*		Current number of WRs posted, used to queue pending requests.
*
*	pending_list
*		List of NDIS_PACKET structures that are awaiting available WRs to send.
*********/


typedef struct _ipoib_endpt_mgr
{
	cl_qmap_t				mac_endpts;
	cl_fmap_t				gid_endpts;
	cl_qmap_t				lid_endpts;

}	ipoib_endpt_mgr_t;
/*
* FIELDS
*	mac_endpts
*		Map of enpoints, keyed by MAC address.
*
*	gid_endpts
*		Map of enpoints, keyed by GID.
*
*	lid_endpts
*		Map of enpoints, keyed by LID.  Only enpoints on the same subnet
*		are inserted in the LID map.
*********/


typedef struct _ipoib_port
{
	cl_obj_t				obj;
	cl_obj_rel_t			rel;

	ib_qp_state_t			state;

	cl_spinlock_t			recv_lock;
	cl_spinlock_t			send_lock;

	struct _ipoib_adapter	*p_adapter;
	uint8_t					port_num;

	KEVENT					sa_event;

	atomic32_t				mcast_cnt;
	KEVENT					leave_mcast_event;
	
	ipoib_ib_mgr_t			ib_mgr;

	ipoib_buf_mgr_t			buf_mgr;

	ipoib_recv_mgr_t		recv_mgr;
	ipoib_send_mgr_t		send_mgr;

	ipoib_endpt_mgr_t		endpt_mgr;

	ipoib_endpt_t			*p_local_endpt;

#if DBG
	atomic32_t				ref[ref_array_size];
#endif

	atomic32_t				endpt_rdr;

	atomic32_t				hdr_idx;
	uint16_t				pkey_index;
	KDPC					gc_dpc;
	KTIMER					gc_timer;
	uint32_t				bc_join_retry_cnt;
	ib_net16_t				base_lid;
	PIO_WORKITEM			pPoWorkItem;
	ipoib_hdr_t				hdr[1];	/* Must be last! */

}	ipoib_port_t;
/*
* FIELDS
*	obj
*		Complib object for reference counting, relationships,
*		and destruction synchronization.
*
*	rel
*		Relationship to associate the port with the adapter.
*
*	state
*		State of the port object.  Tracks QP state fairly closely.
*
*	recv_lock
*		Spinlock to protect receive operations.
*
*	send_lock
*		Spinlock to protect send operations.
*
*	p_adapter
*		Parent adapter.  Used to get AL handle.
*
*	port_num
*		Port number of this adapter.
*
*	ib_mgr
*		IB resource manager.
*
*	recv_mgr
*		Receive manager.
*
*	send_mgr
*		Send manager.
*
*	endpt_mgr
*		Endpoint manager.
*********/


ib_api_status_t
ipoib_create_port(
	IN				struct _ipoib_adapter* const	p_adapter,
	IN				ib_pnp_port_rec_t* const	p_pnp_rec,
		OUT			ipoib_port_t** const		pp_port );

void
ipoib_port_destroy(
	IN				ipoib_port_t* const			p_port );

void
ipoib_port_up(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_pnp_port_rec_t* const	p_pnp_rec );

void
ipoib_port_down(
	IN				ipoib_port_t* const			p_port );

ib_api_status_t
ipoib_port_join_mcast(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t				mac,
	IN		const	uint8_t					state );


void
ipoib_leave_mcast_cb(
	IN				void				*context );


void
ipoib_port_remove_endpt(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac );

void
ipoib_port_send(
	IN				ipoib_port_t* const			p_port,
	IN				NDIS_PACKET					**p_packet_array,
	IN				uint32_t					num_packets );

void
ipoib_return_packet(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_PACKET					*p_packet );

void
ipoib_port_resume(
	IN				ipoib_port_t* const			p_port );

NTSTATUS
ipoib_mac_to_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_gid_t*					p_gid );

NTSTATUS
ipoib_mac_to_path(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_path_rec_t*				p_path );

inline void ipoib_port_ref(
	IN				ipoib_port_t *				p_port, 
	IN				int						type);

inline void ipoib_port_deref(
	IN				ipoib_port_t *				p_port,
	IN				int						type);

#if DBG
// This function is only used to monitor send failures
static inline VOID NdisMSendCompleteX(
	IN NDIS_HANDLE	MiniportAdapterHandle,
	IN PNDIS_PACKET  Packet,
	IN NDIS_STATUS	Status
	) {
	if (Status != NDIS_STATUS_SUCCESS) {
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Sending status other than Success to NDIS\n"));
	}
	NdisMSendComplete(MiniportAdapterHandle,Packet,Status);
}
#else
#define NdisMSendCompleteX NdisMSendComplete
#endif

#endif	/* _IPOIB_PORT_H_ */
