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
#include "ipoib_xfr_mgr.h"
#include "ipoib_endpoint.h"


/*
 * Define to place receive buffer inline in receive descriptor.
 */
#define IPOIB_INLINE_RECV	1

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

//Used in RECV flow
#define IPOIB_PORT_FROM_NBL( P )	\
	(((ipoib_port_t**)NET_BUFFER_LIST_MINIPORT_RESERVED(P))[1])

//Used in RECV flow
#define IPOIB_RECV_FROM_NBL( P )	\
	(((ipoib_recv_desc_t**)NET_BUFFER_LIST_MINIPORT_RESERVED(P))[0])
	
//Used in CM RECV flow
#define IPOIB_CM_RECV_FROM_NBL( P )	\
	(((ipoib_cm_recv_desc_t**)NET_BUFFER_LIST_MINIPORT_RESERVED(P))[0])
	
//Used in CM SEND flow - to update
#define IPOIB_LIST_ITEM_FROM_NBL( P ) \
	((cl_list_item_t*)NET_BUFFER_LIST_MINIPORT_RESERVED(P))

//Used in SEND flow
#define IPOIB_INFO_FROM_NB( P )	\
	(((ipoib_send_NB_SG **)NET_BUFFER_MINIPORT_RESERVED(P))[0])

//#define IPOIB_TCP_PAYLOAD_FROM_NB( P )	\
//	(((UINT *)NET_BUFFER_MINIPORT_RESERVED(P))[1])


//Used in SEND flow
#define IPOIB_GET_NET_BUFFER_LIST_REF_COUNT(_NetBufferList)	\
	((_NetBufferList)->MiniportReserved[0])

#define IPOIB_DEC_NET_BUFFER_LIST_REF_COUNT(_NetBufferList)	\
	(*(PULONG)&(_NetBufferList)->MiniportReserved[0])--



typedef struct _ipoib_ib_mgr
{
	ib_ca_handle_t			h_ca;
	ib_pd_handle_t			h_pd;
	ib_cq_handle_t			h_recv_cq;
	ib_cq_handle_t			h_send_cq;
	ib_qp_handle_t			h_qp;
	ib_query_handle_t		h_query;
	ib_srq_handle_t			h_srq;
	atomic32_t				srq_qp_cnt;
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
*		QP handle for UD data transfers.
*
*	h_query
*		Query handle for cancelling SA queries.
*
*	h_srq
*		SRQ (Shared Receive Queue) handle
*
*	srq_qp_cnt
*		number of QPs bound to the SRQ
*
*	qpn
*		local QP number in net byte-order.
*
*	h_mr
*		Registration handle for all of physical memory.  Used for
*		send/receive buffers to simplify growing the receive pool.
*
*	lkey
*		LKey for the memory region.
*
*	rate
*		port rate
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

typedef struct _ipoib_prealloc_hdr:public ipoib_hdr_t
{
	uint64_t	phys_addr;
} PACK_SUFFIX ipoib_prealloc_hdr_t;
	
static const uint32_t	EthHeaderOffset 		= sizeof(eth_hdr_t);

//We reuse eth header to put there IPoIB header for LSO Net Buffers.
// Thus, when copying such NB one need to jump over the appropriate offset
static const uint32_t 	EthIPoIBHeaderOffset 	= EthHeaderOffset - sizeof(ipoib_hdr_t);

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
		uint8_t			data[MAX_UD_PAYLOAD_MTU];
		ipoib_arp_pkt_t	arp;
		ip_pkt_t		ip;
		ipv6_pkt_t		ipv6;

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
	uint8_t			data[MAX_LSO_PAYLOAD_MTU];
	ipoib_arp_pkt_t	arp;
	ip_pkt_t		ip;
	ipv6_pkt_t		ipv6;

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

	NPAGED_LOOKASIDE_LIST	send_buf_list;
	uint32_t				send_buf_len;
	
	NDIS_HANDLE			h_send_pkt_pool;

}	ipoib_buf_mgr_t;
/*
* FIELDS
*	recv_pool
*		Pool of ipoib_recv_desc_t structures.
*
*	h_packet_pool
*		NDIS packet pool, used to indicate receives to NDIS.
*
*	send_buf_list
*		Lookaside list for dynamically allocating send buffers for send
*		operations which require copies (ARP, DHCP, IP fragmentation and any with more
*		physical pages than can fit in the local data segments).
*********/

typedef enum _ipoib_pkt_type
{
	PKT_TYPE_UCAST,
	PKT_TYPE_BCAST,
	PKT_TYPE_MCAST,
	PKT_TYPE_CM_UCAST

}	ipoib_pkt_type_t;


typedef enum __ib_recv_mode
{
	RECV_UD = 1,
	RECV_RC = 2

}	ib_recv_mode_t;



typedef struct _ipoib_cm_recv_desc
{
	cl_pool_item_t			item;	/* Must be first. */
	uint32_t				len;
	ipoib_pkt_type_t		type;
	ib_recv_mode_t			recv_mode;/* matches ipoib_recv_desc_t to this offset */
	ib_recv_wr_t			wr;
	ib_local_ds_t			local_ds[MAX_CM_RECV_SGE];	/* 1 ds_local / phys page */
	cl_list_item_t			list_item;
	ipoib_endpt_t*			p_endpt;
	uint8_t*				p_alloc_buf;
	uint8_t*				p_buf;
	uint32_t				alloc_buf_size;
	uint32_t				buf_size;
	NET_BUFFER_LIST*		p_NBL;
	MDL*					p_mdl;	
	NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO csum;

}	ipoib_cm_recv_desc_t;
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
*	recv_mode
*		IB connection mode RC(reliable connection) or UD(datagram)
*
*	wr
*		Receive work request.
*
*	local_ds
*		Local data segments.  The second segment is only used if a buffer
*		spans physical pages.
*
*	list_item
*		used when this recv desc is on the recv list (valid data)
*
*	p_endpt
*		remote endpoint struct
*
*	p_alloc_buf
*		allocated receive buffer start address; also start of Ethernet header.
*
*	p_buf
*		Buffer for the CM receive as p_aloc_buf+OFFSET == p_buf, due to adding Enet
*		header in front of received ipoib CM data. ipoib_hdeader_t is overlayed
*		by the synthesized ethernet header prior to passing buffer to NDIS receive
*		routine.
*
*	p_alloc_buf_size
*		actual byte size of allocated buffer.
*
*	buf_size
*		byte size from p_buf to end of allocated buffer.
*
*	p_NBL
*		allocated NBL (NetworkBufferList)
*
*	p_MDL
*		allocated MDL (Memory Descriptor List) for p_buf - sizeof(eth_hdr)
*
*	csum
*		hardware utilized checksum status block.
*/

typedef struct _ipoib_recv_desc
{
	cl_pool_item_t		item;	/* Must be first. */
	uint32_t			len;
	ipoib_pkt_type_t	type;
	ib_recv_mode_t		recv_mode;	/* matches ipoib_cm_recv_desc_t to this offset */
	ib_recv_wr_t		wr;
	ib_local_ds_t		local_ds[2];
#if UD_NBL_IN_DESC
	NET_BUFFER_LIST		*p_NBL;
	MDL					*p_mdl;	
#endif
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
*	recv_mode
*		IB recv mode: RC(reliable connection) or UD(datagram)
*
*	wr
*		Receive work request.
*
*	local_ds
*		Local data segments.  The second segment is only used if a buffer
*		spans physical pages.
*
*	ndis_csum
*		hardware utilized checksum status block.
*
*	buf
*		Buffer for the receive.
*
* NOTES
*	The pool item is always first to allow casting form a cl_pool_item_t or
*	cl_list_item_t to the descriptor.
*********/


typedef struct __ipoib_send_wr
{
	ib_send_wr_t		wr;
	ib_local_ds_t		local_ds[MAX_SEND_SGE];	/* Must be last. */
} ipoib_send_wr_t;


typedef struct _ipoib_send_desc
{
	PNET_BUFFER_LIST    p_netbuf_list;
	ib_qp_handle_t		send_qp;
	uint32_t			num_wrs;
	ipoib_send_wr_t		send_wr[MAX_WRS_PER_MSG];

}	ipoib_send_desc_t;
/*
* FIELDS
*	p_netbuf_list
*		Pointer to the NET_BUFFER_LIST associated with the send operation.
*
*	send_qp
*		QP on which to Send work request.
*
*	num_wrs
*		count of work-requests (in send_wr) linked together.
*
*	send_wr
*		Vector of IB work requests
*
* NOTES
*	The pool item is always first to allow casting form a cl_pool_item_t or
*	cl_list_item_t to the descriptor.
*********/


typedef struct _ipoib_recv_mgr
{
	int32_t			depth;
	NET_BUFFER_LIST	**recv_NBL_array;
	cl_qlist_t		done_list;

}	ipoib_recv_mgr_t;
/*
* FIELDS
*	depth
*		Current number of RX WRs posted.
*
*	recv_NBL_array
*		Array of NBLs (NET_BUFFER_LIST) pointers used to indicate receives.
*
*	done_list
*		List of receive descriptors (ipoib_desc_t) polled from the RX CQ which
*		are used to construct the recv_NBL_array; array is then used to indicate
*		received packets to NDIS 6.
*********/

#if 0
class ItemListElement: public cl_list_item_t {
	public:
		ItemListElement() : p_port(NULL), p_nbl(NULL), p_curr_nbl(NULL) {};
		virtual ~ItemListElement() {};
		
		ipoib_port_t		*p_port;
		PNET_BUFFER_LIST	p_nbl;
		PNET_BUFFER			p_curr_nb;
}

class ItemList {
	//friend PendingListElement;
	public:
	
		ItemList(ULONG size): _size(size), _cnt(0) {
			item_array = new ItemListItem(size);
			RtlSecureZeroMemory(item_array, sizeof (item_array));
		}
		
		virtual ~PendingList() { free(item_array); } ;
		
		ItemListElement * 
		GetListElement() {
			if (_cnt == _size) {
				cl_dbg_out("Out of memory!\n");
				return NULL;
			}
			return item_array[cnt_++];
		}

		void
		PutListElement ( /*PendingListElement * list_elem*/) {
			//ASSERT(list_elem.p_list == list
			RtlSecureZeroMemory(&list_elem[_cnt], sizeof (ItemListElement));
			ASSERT(_cnt > 0);
			--_cnt;
			
		}


	private:
		ULONG 				_size;
		ULONG 				_cnt;
		ItemListItem		*item_array;
}
	

static const ULONG ItemListPoolSize(500);
#endif




typedef struct _ipoib_send_mgr
{
	atomic32_t			depth;
	cl_qlist_t			pending_list;
	cl_qpool_t			send_pool;
	cl_spinlock_t		send_pool_lock;
	cl_qpool_t			sg_pool;

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
	cl_fmap_t				conn_endpts;
	LIST_ENTRY  			pending_conns;
	LIST_ENTRY				remove_conns;
	NDIS_SPIN_LOCK  		conn_lock;
	NDIS_SPIN_LOCK			remove_lock;
	cl_thread_t				h_thread;
	cl_event_t				event;
	uint32_t				thread_is_done;
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
*
*	conn_endpts
*		Map of connected endpts, keyed by remote gid.
*********/

#pragma warning(disable:4324)   // structure padded due to align()
typedef struct _ipoib_port
{
	cl_obj_t				obj;
	cl_obj_rel_t			rel;

	ib_qp_state_t			state;

	cl_spinlock_t			recv_lock;
	cl_spinlock_t			send_lock;

	struct _ipoib_adapter	*p_adapter;
	uint8_t					port_num;
	uint32_t				max_sq_sge_supported; 

	KEVENT					sa_event;

	atomic32_t				mcast_cnt;
	KEVENT					leave_mcast_event;
	
	ipoib_ib_mgr_t			ib_mgr;

	ipoib_buf_mgr_t			buf_mgr;

	ipoib_recv_mgr_t		recv_mgr;
	ipoib_send_mgr_t		send_mgr;
	ipoib_send_desc_t *		p_desc;

	ipoib_endpt_mgr_t		endpt_mgr;
	cm_buf_mgr_t			cm_buf_mgr;

	ipoib_endpt_t			*p_local_endpt;
	ib_ca_attr_t			*p_ca_attrs;
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
	LONG					n_no_progress;
	PIO_WORKITEM			pPoWorkItem;
	PIO_WORKITEM			pPoWorkItemCM;
	ipoib_prealloc_hdr_t	hdr[1];	/* Must be last! */

}	ipoib_port_t;
#pragma warning(default:4324)

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
*		Spinlock to protect UD receive operations.
*
*	send_lock
*		Spinlock to protect UD/RC send operations from NDIS callback.
*
*	p_adapter
*		Parent adapter.  Used to get AL handle.
*
*	port_num
*		Port number of this adapter.
*
*	max_sq_sge_supported
*		The actual number of SGEs that will be used for UD QP
*
*	sa_event
*		Event signalled on SA query completion.
*
*	mcast_cnt
*		count of mcast group members
*
*	leave_mcast_event
*		event signalled upon leaving mcast group
*
*	ib_mgr
*		IB resource manager.
*
*	ipoib_buf_mgr
*		send/recv buffer pools
*
*	send_mgr
*		Send manager.
*
*	recv_mgr
*		Recv manager.
*
*	p_desc
*		send descriptor used by all send operations (serialization point).
*
*	endpt_mgr
*		Endpoint manager
*
*	cm_buf_mgr
*		Connected Mode SRQ recv buffer mgr.
*
*	p_local_endpoint
*		our local endpoint description
*	p_ca_attrs
*		HCA attributes for this port
*
*	ref
*		object reference count tracking
*	endpt_rdr
*		endpoint reader semaphore
*
*	hdr_idx
*		ipoib header array hdr[] index.
*
*	pkey_index
*
*	gc_dpc
*		garbage collector DPC thread.
*
*	gc_timer
*		timer which starts periodic running of gc thread.
*
*	bc_join_retry
*		broadcast join_retry_cnt
*
*	base_lid
*		port's base LID
*
*	n_no_progress
*
*	pPoWorkItem
*		DPC recv offload to worker thread
*
*	pPoWorkItemCM
*		DPC recv offload to worker thread for Connected Mode
*		
*	hdr
*		ipoib header array - 1 entry per outstanding send: UD or RC; see hdr_idx.
*********/
typedef struct _sgl_context
{
	MDL					*p_mdl;
	NET_BUFFER_LIST		*p_netbuffer_list;
	ipoib_port_t		*p_port;
}sgl_context_t;

#if 0

class ipoib_send_NB_SG: public cl_pool_item_t{
public:
	
	ipoib_send_NB_SG(): p_port(NULL), p_nbl (NULL), p_curr_nb(NULL), p_endpt(NULL), p_send_buf(NULL), p_sgl(NULL) {};
	virtual ~ipoib_send_NB_SG();
#endif

typedef struct ipoib_send_NB_SG_t {
//private: //TODO make data private
	cl_pool_item_t			pool_item;
	ipoib_port_t			*p_port;
	PNET_BUFFER_LIST		p_nbl;
	PNET_BUFFER				p_curr_nb;
	ipoib_endpt_t			*p_endpt;
	ipoib_send_desc_t 		*p_send_desc;
	send_buf_t				*p_send_buf;
	PSCATTER_GATHER_LIST	p_sgl;
	UINT					tcp_payload;
	PVOID					p_sg_buf;
	cl_list_item_t 			p_complete_list_item;
} ipoib_send_NB_SG;
/*
* FIELDS
*
*	pool_item
*		for storing descriptors in the send_mgr.send_pool.
*
*	p_port
*		HCA port which send is going out on.
*
*	p_nbl
*		Net buffer list - data passed down from NDIS to xmit
*
*	p_cur_nb
*		current list item in p_nbl list.
*
*	p_endpt
*		sending (aka local) endpoint & link to sending HCA port.
*
*	*p_desc
*		points to port->p_desc 
*		p_desc->p_endpt == destination EP.
*		
*	p_sgl
*		pointer to a scatter-gather list
*
*	tcp_payload
*		used in LSO processing
*		
*	p_sg_buf
*		scatter-gather buffer, used by p_sgl.
*
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
	IN		const	mac_addr_t					mac,
	IN		const	uint8_t						state );

void
ipoib_leave_mcast_cb(
	IN				void						*context );

void
ipoib_port_remove_endpt(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac );

void
ipoib_port_send(
	IN				ipoib_port_t* const			p_port,
	IN				NET_BUFFER_LIST				*net_buffer_list,
	IN				ULONG						send_flags );

void
ipoib_return_net_buffer_list(
	IN				NDIS_HANDLE					adapter_context,
	IN				PNET_BUFFER_LIST			p_netbuffer_lists,
	IN				ULONG						return_flags);

void
ipoib_port_resume(
	IN				ipoib_port_t* const			p_port,
	IN boolean_t								b_pending,
	IN	cl_qlist_t								*p_complete_list);

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

void 
ipoib_process_sg_list(
    IN  PDEVICE_OBJECT          pDO,
    IN  PVOID                   pIrp,
    IN  PSCATTER_GATHER_LIST    pSGList,
    IN  PVOID                   Context);
    
inline void ipoib_port_ref(
	IN				ipoib_port_t *			p_port, 
	IN				int						type);

inline void ipoib_port_deref(
	IN				ipoib_port_t *			p_port,
	IN				int						type);

#if 0
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
//#define NdisMSendCompleteX NdisMSendComplete
#endif

ipoib_endpt_t*
ipoib_endpt_get_by_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_gid_t* const				p_gid );

ipoib_endpt_t*
ipoib_endpt_get_by_lid(
	IN				ipoib_port_t* const			p_port,
	IN		const	net16_t						lid );

ib_api_status_t
ipoib_port_srq_init(
	IN		ipoib_port_t* const		p_port );

void
ipoib_port_srq_destroy( 
	IN		ipoib_port_t* const		p_port );

#if IPOIB_CM

ib_api_status_t
ipoib_port_listen(
	 IN		ipoib_port_t* const		p_port );

ib_api_status_t
ipoib_port_cancel_listen(
	IN		ipoib_endpt_t* const	p_endpt );

void 
ipoib_send_complete_net_buffer(
	IN		ipoib_send_NB_SG		*s_buf, 
	IN		ULONG					compl_flags );

#endif 

void
__send_complete_add_to_list(
	IN		cl_qlist_t				*p_complete_list,
	IN		ipoib_send_NB_SG		*s_buf,
	IN		NDIS_STATUS 			status);


ib_api_status_t
endpt_cm_buf_mgr_init(
	IN		ipoib_port_t* const			p_port );

void
endpt_cm_buf_mgr_destroy(
	IN		ipoib_port_t* const		p_port );

uint32_t
endpt_cm_recv_mgr_build_pkt_array(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt,
	IN		cl_qlist_t* const			p_done_list,
	IN OUT	uint32_t*					p_bytes_recv );


#if IPOIB_CM

void
ipoib_cm_buf_mgr_put_recv(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_cm_recv_desc_t* const	p_desc,
	IN		NET_BUFFER_LIST* const		p_NBL OPTIONAL );

ULONG ipoib_free_received_NBL(
	IN		ipoib_port_t			*p_port,
	IN		NET_BUFFER_LIST 		*p_net_buffer_lists );

BOOLEAN
cm_destroy_conn(
	IN		ipoib_port_t* const			p_port,
	IN		ipoib_endpt_t* const		p_endpt );

void
endpt_cm_disconnect(
	IN		ipoib_endpt_t*	const		p_endpt );

void
endpt_cm_release_resources(
	IN				ipoib_port_t* const		p_port,
	IN				ipoib_endpt_t* const	p_endpt );

char *
get_ipoib_pkt_type_str(
	IN				ipoib_pkt_type_t t );

#endif // IPOIB_CM

ib_api_status_t
ipoib_recv_dhcp(
	IN				ipoib_port_t* const			p_port,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst );

void
ipoib_port_cancel_xmit(
	IN				ipoib_port_t* const		p_port,
	IN				PVOID					 cancel_id );

static inline uint32_t
__port_attr_to_mtu_size(uint32_t value)
{
	switch (value) 
	{
	default:
	case IB_MTU_LEN_2048:
		return 2048;
	case IB_MTU_LEN_4096:
		return 4096;
	case IB_MTU_LEN_1024:
		return 1024;
	case IB_MTU_LEN_512:
		return  512;
	case IB_MTU_LEN_256:
		return  256;
	}
}

VOID
send_complete_list_complete(
	IN				cl_qlist_t					*p_complete_list,
	IN				ULONG						compl_flags);

#define IRQL_TO_COMPLETE_FLAG() KeGetCurrentIrql() == DISPATCH_LEVEL ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0




// This function is only used to monitor send failures
extern ULONG g_NBL_complete;
extern ULONG g_NBL;

static inline VOID NdisMSendNetBufferListsCompleteX(
	IN ipoib_adapter_t*		p_adapter,
	IN PNET_BUFFER_LIST		NetBufferLists,
	IN ULONG				SendCompleteFlags ) 
{
	++g_NBL_complete;
	ipoib_cnt_inc( &p_adapter->n_send_NBL_done );

#if DBG	
	ASSERT(NET_BUFFER_LIST_NEXT_NBL(NetBufferLists) == NULL);

	if (NET_BUFFER_LIST_STATUS(NetBufferLists) != NDIS_STATUS_SUCCESS) {
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ALL,
			("NBL completed with error %d to NDIS\n",
				NET_BUFFER_LIST_STATUS(NetBufferLists)));
	}
	IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
		("Completing NBL=%x, g_NBL=%d, g_NBL_completed=%d \n",
			NetBufferLists, g_NBL, g_NBL_complete) );
#endif

	NdisMSendNetBufferListsComplete( p_adapter->h_adapter,
									 NetBufferLists,
									 SendCompleteFlags );
}

#if DBG
char *get_ib_recv_mode_str(ib_recv_mode_t m);
#endif

#endif	/* _IPOIB_PORT_H_ */
