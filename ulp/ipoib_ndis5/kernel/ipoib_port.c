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



#include "ipoib_port.h"
#include "ipoib_adapter.h"
#include "ipoib_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_port.tmh"
#endif
#include <offload.h>


/* Amount of physical memory to register. */
#define MEM_REG_SIZE	0xFFFFFFFFFFFFFFFF

/* Number of work completions to chain for send and receive polling. */
#define MAX_SEND_WC		8
#define MAX_RECV_WC		16


ib_gid_t	bcast_mgid_template = {
	0xff,								/* multicast field */
	0x12,								/* scope (to be filled in) */
	0x40, 0x1b,							/* IPv4 signature */
	0xff, 0xff,							/* 16 bits of P_Key (to be filled in) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 48 bits of zeros */
	0xff, 0xff, 0xff, 0xff,				/* 32 bit IPv4 broadcast address */
};


#ifdef _DEBUG_
/* Handy pointer for debug use. */
ipoib_port_t	*gp_ipoib_port;
#endif

static void __port_mcast_garbage_dpc(KDPC *p_gc_dpc,void *context,void *s_arg1, void *s_arg2);
static void __port_do_mcast_garbage(ipoib_port_t* const	p_port );


/******************************************************************************
*
* Declarations
*
******************************************************************************/
static void
__port_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__port_init(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_adapter_t* const		p_adapter,
	IN				ib_pnp_port_rec_t* const	p_pnp_rec );

static void
__port_destroying(
	IN				cl_obj_t* const				p_obj );

static void
__port_cleanup(
	IN				cl_obj_t* const				p_obj );

static void
__port_free(
	IN				cl_obj_t* const				p_obj );


/******************************************************************************
*
* IB resource manager operations
*
******************************************************************************/
static void
__ib_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__ib_mgr_init(
	IN				ipoib_port_t* const			p_port );

static void
__ib_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

static void
__qp_event(
	IN				ib_async_event_rec_t		*p_event_rec );

static void
__cq_event(
	IN				ib_async_event_rec_t		*p_event_rec );

static ib_api_status_t
__ib_mgr_activate(
	IN				ipoib_port_t* const			p_port );

/******************************************************************************
*
* Buffer manager operations.
*
******************************************************************************/
static void
__buf_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__buf_mgr_init(
	IN				ipoib_port_t* const			p_port );

static void
__buf_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

static cl_status_t
__recv_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item );

#if !IPOIB_INLINE_RECV
static void
__recv_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void						*context );
#endif	/* IPOIB_INLINE_RECV */

static inline ipoib_send_desc_t*
__buf_mgr_get_send(
	IN				ipoib_port_t* const			p_port );

static inline void
__buf_mgr_put_send(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc );

static inline ipoib_recv_desc_t*
__buf_mgr_get_recv(
	IN				ipoib_port_t* const			p_port );

static inline void
__buf_mgr_put_recv(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
	IN				NDIS_PACKET* const			p_packet OPTIONAL );

static inline void
__buf_mgr_put_recv_list(
	IN				ipoib_port_t* const			p_port,
	IN				cl_qlist_t* const			p_list );

static inline NDIS_PACKET*
__buf_mgr_get_ndis_pkt(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc );


/******************************************************************************
*
* Receive manager operations.
*
******************************************************************************/
static void
__recv_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__recv_mgr_init(
	IN				ipoib_port_t* const			p_port );

static void
__recv_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

/* Posts receive buffers to the receive queue. */
static ib_api_status_t
__recv_mgr_repost(
	IN				ipoib_port_t* const			p_port );

static void
__recv_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

static void
__recv_get_endpts(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
	IN				ib_wc_t* const				p_wc,
		OUT			ipoib_endpt_t** const		pp_src,
		OUT			ipoib_endpt_t** const		pp_dst );

static int32_t
__recv_mgr_filter(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_done_wc_list,
		OUT			cl_qlist_t* const			p_done_list,
		OUT			cl_qlist_t* const			p_bad_list );

static ib_api_status_t
__recv_gen(
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst );

static ib_api_status_t
__recv_dhcp(
	IN				ipoib_port_t* const			p_port,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst );

static ib_api_status_t
__recv_arp(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_wc,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t** const		p_src,
	IN				ipoib_endpt_t* const		p_dst );

static ib_api_status_t
__recv_mgr_prepare_pkt(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
		OUT			NDIS_PACKET** const			pp_packet );

static uint32_t
__recv_mgr_build_pkt_array(
	IN				ipoib_port_t* const			p_port,
	IN				int32_t						shortage,
		OUT			cl_qlist_t* const			p_done_list,
		OUT			int32_t* const				p_discarded );

/******************************************************************************
*
* Send manager operations.
*
******************************************************************************/
static void
__send_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static void
__send_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

static NDIS_STATUS
__send_gen(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc,
	IN				INT 						lso_data_index);

static NDIS_STATUS
__send_mgr_filter_ip(
	IN				ipoib_port_t* const			p_port,
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc );

static NDIS_STATUS
__send_mgr_filter_igmp_v2(
	IN				ipoib_port_t* const			p_port,
    IN		const	ip_hdr_t* const				p_ip_hdr,
	IN				size_t						iph_options_size,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len );

static NDIS_STATUS
__send_mgr_filter_udp(
	IN				ipoib_port_t* const			p_port,
	IN		const	ip_hdr_t* const				p_ip_hdr,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc );

static NDIS_STATUS
__send_mgr_filter_dhcp(
	IN				ipoib_port_t* const			p_port,
	IN		const	udp_hdr_t* const			p_udp_hdr,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc );

static NDIS_STATUS
__send_mgr_filter_arp(
	IN				ipoib_port_t* const			p_port,
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc );

static void
__process_failed_send(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc,
	IN		const	NDIS_STATUS					status );

static void
__send_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

static NDIS_STATUS GetLsoHeaderSize(
	IN      ipoib_port_t* const pPort,
	IN      PNDIS_BUFFER  CurrBuffer,
	IN      LsoData *pLsoData,
	OUT     uint16_t *pSize,
	OUT     INT  *IndexOfData,
	IN		ipoib_hdr_t *ipoib_hdr
	);

/******************************************************************************
*
* Endpoint manager operations
*
******************************************************************************/
static void
__endpt_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__endpt_mgr_init(
	IN				ipoib_port_t* const			p_port );

static void
__endpt_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

/****f* IPoIB/__endpt_mgr_remove_all
* NAME
*	__endpt_mgr_remove_all
*
* DESCRIPTION
*	Removes all enpoints from the port, dereferencing them to initiate
*	destruction.
*
* SYNOPSIS
*/
static void
__endpt_mgr_remove_all(
	IN				ipoib_port_t* const			p_port );
/*
********/

static void
__endpt_mgr_remove(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_endpt_t* const		p_endpt );

static void
__endpt_mgr_reset_all(
	IN				ipoib_port_t* const			p_port );

static inline NDIS_STATUS
__endpt_mgr_ref(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ipoib_endpt_t** const		pp_endpt );

static inline NDIS_STATUS
__endpt_mgr_get_gid_qpn(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_gid_t* const				p_gid,
		OUT			UNALIGNED net32_t* const	p_qpn );

static inline ipoib_endpt_t*
__endpt_mgr_get_by_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_gid_t* const				p_gid );

static inline ipoib_endpt_t*
__endpt_mgr_get_by_lid(
	IN				ipoib_port_t* const			p_port,
	IN		const	net16_t						lid );

static inline ib_api_status_t
__endpt_mgr_insert_locked(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
	IN				ipoib_endpt_t* const		p_endpt );

static inline ib_api_status_t
__endpt_mgr_insert(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
	IN				ipoib_endpt_t* const		p_endpt );

static ib_api_status_t
__endpt_mgr_add_local(
	IN				ipoib_port_t* const			p_port,
	IN				ib_port_info_t* const		p_port_info );

static ib_api_status_t
__endpt_mgr_add_bcast(
	IN				ipoib_port_t* const			p_port,
	IN				ib_mcast_rec_t				*p_mcast_rec );

/******************************************************************************
*
* MCast operations.
*
******************************************************************************/
static ib_api_status_t
__port_get_bcast(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__port_join_bcast(
	IN				ipoib_port_t* const			p_port,
	IN				ib_member_rec_t* const		p_member_rec );

static ib_api_status_t
__port_create_bcast(
	IN				ipoib_port_t* const			p_port );



static void
__bcast_get_cb(
	IN				ib_query_rec_t				*p_query_rec );


static void
__bcast_cb(
	IN				ib_mcast_rec_t				*p_mcast_rec );


static void
__mcast_cb(
	IN				ib_mcast_rec_t				*p_mcast_rec );

void
__leave_error_mcast_cb(
	IN				void				*context );


static int
__gid_cmp(
	IN		const	void* const					p_key1,
	IN		const	void* const					p_key2 )
{
	return cl_memcmp( p_key1, p_key2, sizeof(ib_gid_t) );
}


inline void ipoib_port_ref( ipoib_port_t * p_port, int type )
{
	cl_obj_ref( &p_port->obj );
#if DBG
	cl_atomic_inc( &p_port->ref[type % ref_mask] );
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
		("ref type %d ref_cnt %d\n", type, p_port->obj.ref_cnt) );
#else
	UNREFERENCED_PARAMETER(type);
#endif
}


inline void ipoib_port_deref(ipoib_port_t * p_port, int type)
{
#if DBG
	cl_atomic_dec( &p_port->ref[type % ref_mask] );
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
		("deref type %d ref_cnt %d\n", type, p_port->obj.ref_cnt) );
#else
	UNREFERENCED_PARAMETER(type);
#endif
	cl_obj_deref( &p_port->obj );

}

/* function returns pointer to payload that is going after IP header.
*  asssuming that payload and IP header are in the same buffer
*/
static void* GetIpPayloadPtr(const	ip_hdr_t* const	p_ip_hdr)
{
	return (void*)((uint8_t*)p_ip_hdr + 4*(p_ip_hdr->ver_hl & 0xf));
}

/******************************************************************************
*
* Implementation
*
******************************************************************************/
ib_api_status_t
ipoib_create_port(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				ib_pnp_port_rec_t* const	p_pnp_rec,
		OUT			ipoib_port_t** const		pp_port )
{
	ib_api_status_t		status;
	ipoib_port_t		*p_port;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( !p_adapter->p_port );

	p_port = cl_zalloc( sizeof(ipoib_port_t) +
		(sizeof(ipoib_hdr_t) * (p_adapter->params.sq_depth - 1)) );
	if( !p_port )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate ipoib_port_t (%d bytes)\n",
			sizeof(ipoib_port_t)) );
		return IB_INSUFFICIENT_MEMORY;
	}

#ifdef _DEBUG_
	gp_ipoib_port = p_port;
#endif

	__port_construct( p_port );

	status = __port_init( p_port, p_adapter, p_pnp_rec );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_port_init returned %s.\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		__port_cleanup( &p_port->obj );
		__port_free( &p_port->obj );
		return status;
	}

	*pp_port = p_port;
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


void
ipoib_port_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_port );
	CL_ASSERT( p_port->p_adapter );
	CL_ASSERT( !p_port->p_adapter->p_port );

	cl_obj_destroy( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__port_construct(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_port->state = IB_QPS_RESET;

	cl_obj_construct( &p_port->obj, IPOIB_OBJ_PORT );
	cl_spinlock_construct( &p_port->send_lock );
	cl_spinlock_construct( &p_port->recv_lock );
	__ib_mgr_construct( p_port );
	__buf_mgr_construct( p_port );

	__recv_mgr_construct( p_port );
	__send_mgr_construct( p_port );

	__endpt_mgr_construct( p_port );

	p_port->pPoWorkItem = NULL;

	KeInitializeEvent( &p_port->sa_event, NotificationEvent, TRUE );
	KeInitializeEvent( &p_port->leave_mcast_event, NotificationEvent, TRUE );
	
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__port_init(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_adapter_t* const		p_adapter,
	IN				ib_pnp_port_rec_t* const	p_pnp_rec )
{
	cl_status_t			cl_status;
	ib_api_status_t		status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_port->port_num = p_pnp_rec->p_port_attr->port_num;
	p_port->p_adapter = p_adapter;

	p_port->pPoWorkItem = IoAllocateWorkItem(p_adapter->pdo);
	if( p_port->pPoWorkItem == NULL ) {
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("IoAllocateWorkItem returned NULL\n") );
		return IB_ERROR;
	}
	
	cl_status = cl_spinlock_init( &p_port->send_lock );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	cl_status = cl_spinlock_init( &p_port->recv_lock );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	/* Initialize the IB resource manager. */
	status = __ib_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__ib_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Initialize the buffer manager. */
	status = __buf_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__buf_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Initialize the receive manager. */
	status = __recv_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__recv_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Initialize the endpoint manager. */
	status = __endpt_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__endpt_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	 /* Initialize multicast garbage collector timer and DPC object */
	 KeInitializeDpc(&p_port->gc_dpc,(PKDEFERRED_ROUTINE)__port_mcast_garbage_dpc,p_port);
	 KeInitializeTimerEx(&p_port->gc_timer,SynchronizationTimer);

	/* We only ever destroy from the PnP callback thread. */
	cl_status = cl_obj_init( &p_port->obj, CL_DESTROY_SYNC,
		__port_destroying, __port_cleanup, __port_free );

#if DBG
	cl_atomic_inc( &p_port->ref[ref_init] );
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
		("ref type %d ref_cnt %d\n", ref_init, p_port->obj.ref_cnt) );
#endif

	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_obj_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	cl_status = cl_obj_insert_rel( &p_port->rel, &p_adapter->obj, &p_port->obj );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_obj_insert_rel returned %#x\n", cl_status) );
		cl_obj_destroy( &p_port->obj );
		return IB_ERROR;
	}

#if DBG
	cl_atomic_inc( &p_port->ref[ref_init] );
	IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_OBJ,
		("ref type %d ref_cnt %d\n", ref_init, p_port->obj.ref_cnt) );
#endif

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


static void
__port_destroying(
	IN				cl_obj_t* const				p_obj )
{
	ipoib_port_t	*p_port;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_obj );

	p_port = PARENT_STRUCT( p_obj, ipoib_port_t, obj );

	ipoib_port_down( p_port );

	__endpt_mgr_remove_all( p_port );

	ipoib_port_resume( p_port );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__port_cleanup(
	IN				cl_obj_t* const				p_obj )
{
	ipoib_port_t	*p_port;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_obj );

	p_port = PARENT_STRUCT( p_obj, ipoib_port_t, obj );

	/* Wait for all sends and receives to get flushed. */
	while( p_port->send_mgr.depth || p_port->recv_mgr.depth )
		cl_thread_suspend( 0 );

	/* Destroy the send and receive managers before closing the CA. */
	__ib_mgr_destroy( p_port );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__port_free(
	IN				cl_obj_t* const				p_obj )
{
	ipoib_port_t	*p_port;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_obj );

	p_port = PARENT_STRUCT( p_obj, ipoib_port_t, obj );

	KeCancelTimer(&p_port->gc_timer);
	KeFlushQueuedDpcs();
	__endpt_mgr_destroy( p_port );
	__recv_mgr_destroy( p_port );
	__send_mgr_destroy( p_port );
	__buf_mgr_destroy( p_port );

	cl_spinlock_destroy( &p_port->send_lock );
	cl_spinlock_destroy( &p_port->recv_lock );

	cl_obj_deinit( p_obj );

	IoFreeWorkItem( p_port->pPoWorkItem );
	cl_free( p_port );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}



/******************************************************************************
*
* IB resource manager implementation.
*
******************************************************************************/
static void
__ib_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	cl_memclr( &p_port->ib_mgr, sizeof(ipoib_ib_mgr_t) );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__ib_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		status;
	ib_cq_create_t		cq_create;
	ib_qp_create_t		qp_create;
	ib_phys_create_t	phys_create;
	ib_phys_range_t		phys_range;
	uint64_t			vaddr;
	net32_t				rkey;
	ib_qp_attr_t		qp_attr;
	ib_ca_attr_t *		p_ca_attr;
	uint32_t			attr_size;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Open the CA. */
	status = p_port->p_adapter->p_ifc->open_ca(
		p_port->p_adapter->h_al, p_port->p_adapter->guids.ca_guid,
		NULL, p_port, &p_port->ib_mgr.h_ca );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_OPEN_CA, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_open_ca returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Allocate the PD. */
	status = p_port->p_adapter->p_ifc->alloc_pd(
		p_port->ib_mgr.h_ca, IB_PDT_UD, p_port, &p_port->ib_mgr.h_pd );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_ALLOC_PD, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_alloc_pd returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Allocate receive CQ. */
	cq_create.size = p_port->p_adapter->params.rq_depth;
	cq_create.pfn_comp_cb = __recv_cb;
	cq_create.h_wait_obj = NULL;

	status = p_port->p_adapter->p_ifc->create_cq(
		p_port->ib_mgr.h_ca, &cq_create, p_port,
		__cq_event, &p_port->ib_mgr.h_recv_cq );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_CREATE_RECV_CQ, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_cq returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Allocate send CQ. */
	cq_create.size = p_port->p_adapter->params.sq_depth;
	cq_create.pfn_comp_cb = __send_cb;

	status = p_port->p_adapter->p_ifc->create_cq(
		p_port->ib_mgr.h_ca, &cq_create, p_port,
		__cq_event, &p_port->ib_mgr.h_send_cq );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_CREATE_SEND_CQ, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_cq returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	
	/* Allocate the QP. */
	cl_memclr( &qp_create, sizeof(qp_create) );
	qp_create.qp_type = IB_QPT_UNRELIABLE_DGRM;
	qp_create.rq_depth = p_port->p_adapter->params.rq_depth;
	qp_create.rq_sge = 2;	/* To support buffers spanning pages. */
	qp_create.h_rq_cq = p_port->ib_mgr.h_recv_cq;
	qp_create.sq_depth = p_port->p_adapter->params.sq_depth;
	
	//Figure out the right number of SGE entries for sends.
	/* Get the size of the CA attribute structure. */
	status = p_port->p_adapter->p_ifc->query_ca( p_port->ib_mgr.h_ca, NULL, &attr_size );
	if( status != IB_INSUFFICIENT_MEMORY )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_query_ca failed with status %s.\n", p_port->p_adapter->p_ifc->get_err_str(status)) );
		return status;
	}

	/* Allocate enough space to store the attribute structure. */
	p_ca_attr = cl_malloc( attr_size );
	if( !p_ca_attr )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_malloc failed to allocate p_ca_attr!\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Query the CA attributes. */
	status = p_port->p_adapter->p_ifc->query_ca(p_port->ib_mgr.h_ca, p_ca_attr, &attr_size );
	if( status != IB_SUCCESS )
	{
		cl_free( p_ca_attr );

		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_query_ca failed with status %s.\n", p_port->p_adapter->p_ifc->get_err_str(status)) );
		return status;
	}
#define UD_QP_USED_SGE 3
	qp_create.sq_sge = MAX_SEND_SGE < p_ca_attr->max_sges ? MAX_SEND_SGE  : (p_ca_attr->max_sges - UD_QP_USED_SGE);
	if (!p_ca_attr->ipoib_csum) { 
		//checksum is not supported by device
		//user must specify BYPASS to explicitly cancel checksum calculation
		if (p_port->p_adapter->params.send_chksum_offload == CSUM_ENABLED)
			p_port->p_adapter->params.send_chksum_offload = CSUM_DISABLED;
		if (p_port->p_adapter->params.recv_chksum_offload == CSUM_ENABLED)
			p_port->p_adapter->params.recv_chksum_offload = CSUM_DISABLED;
	}
	cl_free( p_ca_attr );
	
	qp_create.h_sq_cq = p_port->ib_mgr.h_send_cq;
	qp_create.sq_signaled = TRUE;
	status = p_port->p_adapter->p_ifc->create_qp(
		p_port->ib_mgr.h_pd, &qp_create, p_port,
		__qp_event, &p_port->ib_mgr.h_qp );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_CREATE_QP, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_qp returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	/* Query the QP so we can get our QPN. */
	status = p_port->p_adapter->p_ifc->query_qp(
		p_port->ib_mgr.h_qp, &qp_attr );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_QUERY_QP, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_query_qp returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	p_port->ib_mgr.qpn = qp_attr.num;

	/* Register all of physical memory */
	phys_create.length = MEM_REG_SIZE;
	phys_create.num_ranges = 1;
	phys_create.range_array = &phys_range;
	phys_create.buf_offset = 0;
	phys_create.hca_page_size = PAGE_SIZE;
	phys_create.access_ctrl = IB_AC_LOCAL_WRITE;
	phys_range.base_addr = 0;
	phys_range.size = MEM_REG_SIZE;
	vaddr = 0;
	status = p_port->p_adapter->p_ifc->reg_phys(
		p_port->ib_mgr.h_pd, &phys_create, &vaddr,
		&p_port->ib_mgr.lkey, &rkey, &p_port->ib_mgr.h_mr );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_REG_PHYS, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_reg_phys returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


static void
__ib_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t	status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	if( p_port->ib_mgr.h_ca )
	{
		status =
			p_port->p_adapter->p_ifc->close_ca( p_port->ib_mgr.h_ca, NULL );
		CL_ASSERT( status == IB_SUCCESS );
		p_port->ib_mgr.h_ca = NULL;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
}



/******************************************************************************
*
* Buffer manager implementation.
*
******************************************************************************/
static void
__buf_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	cl_qpool_construct( &p_port->buf_mgr.recv_pool );

	p_port->buf_mgr.h_packet_pool = NULL;
	p_port->buf_mgr.h_buffer_pool = NULL;

	ExInitializeNPagedLookasideList( &p_port->buf_mgr.send_buf_list,
		NULL, NULL, 0, MAX_XFER_BLOCK_SIZE, 'bipi', 0 );

	p_port->buf_mgr.h_send_pkt_pool = NULL;
	p_port->buf_mgr.h_send_buf_pool = NULL;

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__buf_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	cl_status_t		cl_status;
	NDIS_STATUS		ndis_status;
	ipoib_params_t	*p_params;

	IPOIB_ENTER(IPOIB_DBG_INIT );

	CL_ASSERT( p_port );
	CL_ASSERT( p_port->p_adapter );

	p_params = &p_port->p_adapter->params;

	/* Allocate the receive descriptor pool */
	cl_status = cl_qpool_init( &p_port->buf_mgr.recv_pool,
		p_params->rq_depth * p_params->recv_pool_ratio,
#if IPOIB_INLINE_RECV
		0, 0, sizeof(ipoib_recv_desc_t), __recv_ctor, NULL, p_port );
#else	/* IPOIB_INLINE_RECV */
		0, 0, sizeof(ipoib_recv_desc_t), __recv_ctor, __recv_dtor, p_port );
#endif	/* IPOIB_INLINE_RECV */
	if( cl_status != CL_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_POOL, 1, cl_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_qpool_init for recvs returned %#x\n",
			cl_status) );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Allocate the NDIS buffer and packet pools for receive indication. */
	NdisAllocatePacketPool( &ndis_status, &p_port->buf_mgr.h_packet_pool,
		p_params->rq_depth, PROTOCOL_RESERVED_SIZE_IN_PACKET );
	if( ndis_status != NDIS_STATUS_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_PKT_POOL, 1, ndis_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisAllocatePacketPool returned %08X\n", ndis_status) );
		return IB_INSUFFICIENT_RESOURCES;
	}

	NdisAllocateBufferPool( &ndis_status, &p_port->buf_mgr.h_buffer_pool,
		p_params->rq_depth );
	if( ndis_status != NDIS_STATUS_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_BUF_POOL, 1, ndis_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisAllocateBufferPool returned %08X\n", ndis_status) );
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Allocate the NDIS buffer and packet pools for send formatting. */
	NdisAllocatePacketPool( &ndis_status, &p_port->buf_mgr.h_send_pkt_pool,
		1, PROTOCOL_RESERVED_SIZE_IN_PACKET );
	if( ndis_status != NDIS_STATUS_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_SEND_PKT_POOL, 1, ndis_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisAllocatePacketPool returned %08X\n", ndis_status) );
		return IB_INSUFFICIENT_RESOURCES;
	}

	NdisAllocateBufferPool( &ndis_status,
		&p_port->buf_mgr.h_send_buf_pool, 1 );
	if( ndis_status != NDIS_STATUS_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_SEND_BUF_POOL, 1, ndis_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisAllocateBufferPool returned %08X\n", ndis_status) );
		return IB_INSUFFICIENT_RESOURCES;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


static void
__buf_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER(IPOIB_DBG_INIT );

	CL_ASSERT( p_port );

	/* Destroy the send packet and buffer pools. */
	if( p_port->buf_mgr.h_send_buf_pool )
		NdisFreeBufferPool( p_port->buf_mgr.h_send_buf_pool );
	if( p_port->buf_mgr.h_send_pkt_pool )
		NdisFreePacketPool( p_port->buf_mgr.h_send_pkt_pool );

	/* Destroy the receive packet and buffer pools. */
	if( p_port->buf_mgr.h_buffer_pool )
		NdisFreeBufferPool( p_port->buf_mgr.h_buffer_pool );
	if( p_port->buf_mgr.h_packet_pool )
		NdisFreePacketPool( p_port->buf_mgr.h_packet_pool );

	/* Free the receive and send descriptors. */
	cl_qpool_destroy( &p_port->buf_mgr.recv_pool );

	/* Free the lookaside list of scratch buffers. */
	ExDeleteNPagedLookasideList( &p_port->buf_mgr.send_buf_list );

	IPOIB_EXIT(  IPOIB_DBG_INIT );
}


static cl_status_t
__recv_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	ipoib_recv_desc_t	*p_desc;
	ipoib_port_t		*p_port;
	uint32_t			ds0_len;

	IPOIB_ENTER( IPOIB_DBG_ALLOC );

	CL_ASSERT( p_object );
	CL_ASSERT( context );

	p_desc = (ipoib_recv_desc_t*)p_object;
	p_port = (ipoib_port_t*)context;

	/* Setup the work request. */
	p_desc->wr.ds_array = p_desc->local_ds;
	p_desc->wr.wr_id = (uintn_t)p_desc;

#if IPOIB_INLINE_RECV
	/* Sanity check on the receive buffer layout */
	CL_ASSERT( (void*)&p_desc->buf.eth.pkt.type ==
		(void*)&p_desc->buf.ib.pkt.type );
	CL_ASSERT( sizeof(recv_buf_t) == sizeof(ipoib_pkt_t) + sizeof(ib_grh_t) );

	/* Setup the local data segment. */
	p_desc->local_ds[0].vaddr = cl_get_physaddr( &p_desc->buf );
	p_desc->local_ds[0].lkey = p_port->ib_mgr.lkey;
	ds0_len =
		PAGE_SIZE - ((uint32_t)p_desc->local_ds[0].vaddr & (PAGE_SIZE - 1));
	if( ds0_len >= sizeof(recv_buf_t) )
	{
		/* The whole buffer is within a page. */
		p_desc->local_ds[0].length = ds0_len;
		p_desc->wr.num_ds = 1;
	}
	else
	{
		/* The buffer crosses page boundaries. */
		p_desc->local_ds[0].length = ds0_len;
		p_desc->local_ds[1].vaddr = cl_get_physaddr( 
			((uint8_t*)&p_desc->buf) + ds0_len );
		p_desc->local_ds[1].lkey = p_port->ib_mgr.lkey;
		p_desc->local_ds[1].length = sizeof(recv_buf_t) - ds0_len;
		p_desc->wr.num_ds = 2;
	}
#else	/* IPOIB_INLINE_RECV */
	/* Allocate the receive buffer. */
	p_desc->p_buf = (recv_buf_t*)cl_zalloc( sizeof(recv_buf_t) );
	if( !p_desc->p_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate receive buffer.\n") );
		return CL_INSUFFICIENT_MEMORY;
	}

	/* Sanity check on the receive buffer layout */
	CL_ASSERT( (void*)&p_desc->p_buf->eth.pkt.type ==
		(void*)&p_desc->p_buf->ib.pkt.type );

	/* Setup the local data segment. */
	p_desc->local_ds[0].vaddr = cl_get_physaddr( p_desc->p_buf );
	p_desc->local_ds[0].length = sizeof(ipoib_pkt_t) + sizeof(ib_grh_t);
	p_desc->local_ds[0].lkey = p_port->ib_mgr.lkey;
#endif	/* IPOIB_INLINE_RECV */

	*pp_pool_item = &p_desc->item;

	IPOIB_EXIT( IPOIB_DBG_ALLOC );
	return CL_SUCCESS;
}


#if !IPOIB_INLINE_RECV
static void
__recv_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void						*context )
{
	ipoib_recv_desc_t	*p_desc;

	IPOIB_ENTER(  IPOIB_DBG_ALLOC );

	UNUSED_PARAM( context );

	p_desc = PARENT_STRUCT( p_pool_item, ipoib_recv_desc_t, item );

	if( p_desc->p_buf )
		cl_free( p_desc->p_buf );

	IPOIB_EXIT( IPOIB_DBG_ALLOC );
}
#endif


static inline ipoib_recv_desc_t*
__buf_mgr_get_recv(
	IN				ipoib_port_t* const			p_port )
{
	ipoib_recv_desc_t	*p_desc;
	IPOIB_ENTER( IPOIB_DBG_RECV );
	p_desc = (ipoib_recv_desc_t*)cl_qpool_get( &p_port->buf_mgr.recv_pool );
	/* Reference the port object for the send. */
	if( p_desc )
	{
		ipoib_port_ref( p_port, ref_get_recv );
		CL_ASSERT( p_desc->wr.wr_id == (uintn_t)p_desc );
#if IPOIB_INLINE_RECV
		CL_ASSERT( p_desc->local_ds[0].vaddr ==
			cl_get_physaddr( &p_desc->buf ) );
#else	/* IPOIB_INLINE_RECV */
		CL_ASSERT( p_desc->local_ds[0].vaddr ==
			cl_get_physaddr( p_desc->p_buf ) );
		CL_ASSERT( p_desc->local_ds[0].length ==
			(sizeof(ipoib_pkt_t) + sizeof(ib_grh_t)) );
#endif	/* IPOIB_INLINE_RECV */
		CL_ASSERT( p_desc->local_ds[0].lkey == p_port->ib_mgr.lkey );
	}
	IPOIB_EXIT( IPOIB_DBG_RECV );
	return p_desc;
}


static inline void
__buf_mgr_put_recv(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
	IN				NDIS_PACKET* const			p_packet OPTIONAL )
{
	NDIS_BUFFER		*p_buf;

	IPOIB_ENTER(IPOIB_DBG_RECV );

	if( p_packet )
	{
		/* Unchain the NDIS buffer. */
		NdisUnchainBufferAtFront( p_packet, &p_buf );
		CL_ASSERT( p_buf );
		/* Return the NDIS packet and NDIS buffer to their pools. */
		NdisDprFreePacketNonInterlocked( p_packet );
		NdisFreeBuffer( p_buf );
	}

	/* Return the descriptor to its pools. */
	cl_qpool_put( &p_port->buf_mgr.recv_pool, &p_desc->item );

	/*
	 * Dereference the port object since the receive is no longer outstanding.
	 */
	ipoib_port_deref( p_port, ref_get_recv );
	IPOIB_EXIT(  IPOIB_DBG_RECV );
}


static inline void
__buf_mgr_put_recv_list(
	IN				ipoib_port_t* const			p_port,
	IN				cl_qlist_t* const			p_list )
{
	//IPOIB_ENTER(  IPOIB_DBG_RECV );
	cl_qpool_put_list( &p_port->buf_mgr.recv_pool, p_list );
	//IPOIB_EXIT(  IPOIB_DBG_RECV );
}


static inline NDIS_PACKET*
__buf_mgr_get_ndis_pkt(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc )
{
	NDIS_STATUS				status;
	NDIS_PACKET				*p_packet;
	NDIS_BUFFER				*p_buffer;

	IPOIB_ENTER(  IPOIB_DBG_RECV );

	NdisDprAllocatePacketNonInterlocked( &status, &p_packet,
		p_port->buf_mgr.h_packet_pool );
	if( status != NDIS_STATUS_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate NDIS_PACKET: %08x\n", status) );
		return NULL;
	}

	IPOIB_PORT_FROM_PACKET( p_packet ) = p_port;
	IPOIB_RECV_FROM_PACKET( p_packet ) = p_desc;

	NdisAllocateBuffer( &status, &p_buffer,
#if IPOIB_INLINE_RECV
		p_port->buf_mgr.h_buffer_pool, &p_desc->buf.eth.pkt, p_desc->len );
#else	/* IPOIB_INLINE_RECV */
		p_port->buf_mgr.h_buffer_pool, &p_desc->p_buf->eth.pkt, p_desc->len );
#endif	/* IPOIB_INLINE_RECV */
	if( status != NDIS_STATUS_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate NDIS_BUFFER: %08x\n", status) );
		NdisDprFreePacketNonInterlocked( p_packet );
		return NULL;
	}

	NdisChainBufferAtFront( p_packet, p_buffer );
	NDIS_SET_PACKET_HEADER_SIZE( p_packet, sizeof(eth_hdr_t) );

	IPOIB_EXIT(  IPOIB_DBG_RECV );
	return p_packet;
}

/******************************************************************************
*
* Receive manager implementation.
*
******************************************************************************/
static void
__recv_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	cl_qlist_init( &p_port->recv_mgr.done_list );

	p_port->recv_mgr.recv_pkt_array = NULL;

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__recv_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Allocate the NDIS_PACKET pointer array for indicating receives. */
	p_port->recv_mgr.recv_pkt_array = cl_malloc(
		sizeof(NDIS_PACKET*) * p_port->p_adapter->params.rq_depth );
	if( !p_port->recv_mgr.recv_pkt_array )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_PKT_ARRAY, 0 );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_malloc for PNDIS_PACKET array failed.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


static void
__recv_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( cl_is_qlist_empty( &p_port->recv_mgr.done_list ) );
	CL_ASSERT( !p_port->recv_mgr.depth );

	if( p_port->recv_mgr.recv_pkt_array )
		cl_free( p_port->recv_mgr.recv_pkt_array );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


/*
 * Posts receive buffers to the receive queue and returns the number
 * of receives needed to bring the RQ to its low water mark.  Note
 * that the value is signed, and can go negative.  All tests must
 * be for > 0.
 */
static int32_t
__recv_mgr_repost(
	IN				ipoib_port_t* const			p_port )
{
	ipoib_recv_desc_t	*p_head = NULL, *p_tail = NULL, *p_next;
	ib_api_status_t		status;
	ib_recv_wr_t		*p_failed;
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

	while( p_port->recv_mgr.depth < p_port->p_adapter->params.rq_depth )
	{
		/* Pull receives out of the pool and chain them up. */
		cl_perf_start( GetRecv );
		p_next = __buf_mgr_get_recv( p_port );
		cl_perf_stop( &p_port->p_adapter->perf, GetRecv );
		if( !p_next )
		{
			IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("Out of receive descriptors! recv queue depath 0x%x\n",p_port->recv_mgr.depth) );
			break;
		}

		if( !p_tail )
		{
			p_tail = p_next;
			p_next->wr.p_next = NULL;
		}
		else
		{
			p_next->wr.p_next = &p_head->wr;
		}

		p_head = p_next;

		p_port->recv_mgr.depth++;
	}

	if( p_head )
	{
		cl_perf_start( PostRecv );
		status = p_port->p_adapter->p_ifc->post_recv(
			p_port->ib_mgr.h_qp, &p_head->wr, &p_failed );
		cl_perf_stop( &p_port->p_adapter->perf, PostRecv );

		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ip_post_recv returned %s\n", 
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			/* return the descriptors to the pool */
			while( p_failed )
			{
				p_head = PARENT_STRUCT( p_failed, ipoib_recv_desc_t, wr );
				p_failed = p_failed->p_next;

				__buf_mgr_put_recv( p_port, p_head, NULL );
				p_port->recv_mgr.depth--;
			}
		}
	}

	ipoib_port_deref( p_port, ref_repost );
	IPOIB_EXIT( IPOIB_DBG_RECV );
	return p_port->p_adapter->params.rq_low_watermark - p_port->recv_mgr.depth;
}


void
ipoib_return_packet(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_PACKET					*p_packet )
{
	cl_list_item_t		*p_item;
	ipoib_port_t		*p_port;
	ipoib_recv_desc_t	*p_desc;
	ib_api_status_t		status = IB_NOT_DONE;
	int32_t				shortage;
	PERF_DECLARE( ReturnPacket );
	PERF_DECLARE( ReturnPutRecv );
	PERF_DECLARE( ReturnRepostRecv );
	PERF_DECLARE( ReturnPreparePkt );
	PERF_DECLARE( ReturnNdisIndicate );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	UNUSED_PARAM( adapter_context );
	CL_ASSERT( p_packet );

	cl_perf_start( ReturnPacket );

	/* Get the port and descriptor from the packet. */
	p_port = IPOIB_PORT_FROM_PACKET( p_packet );
	p_desc = IPOIB_RECV_FROM_PACKET( p_packet );

	cl_spinlock_acquire( &p_port->recv_lock );

	cl_perf_start( ReturnPutRecv );
	__buf_mgr_put_recv( p_port, p_desc, p_packet );
	cl_perf_stop( &p_port->p_adapter->perf, ReturnPutRecv );

	/* Repost buffers. */
	cl_perf_start( ReturnRepostRecv );
	shortage = __recv_mgr_repost( p_port );
	cl_perf_stop( &p_port->p_adapter->perf, ReturnRepostRecv );

	for( p_item = cl_qlist_remove_head( &p_port->recv_mgr.done_list );
		p_item != cl_qlist_end( &p_port->recv_mgr.done_list );
		p_item = cl_qlist_remove_head( &p_port->recv_mgr.done_list ) )
	{
		p_desc = (ipoib_recv_desc_t*)p_item;

		cl_perf_start( ReturnPreparePkt );
		status = __recv_mgr_prepare_pkt( p_port, p_desc, &p_packet );
		cl_perf_stop( &p_port->p_adapter->perf, ReturnPreparePkt );
		if( status == IB_SUCCESS )
		{
			if( shortage > 0 )
				NDIS_SET_PACKET_STATUS( p_packet, NDIS_STATUS_RESOURCES );
			else
				NDIS_SET_PACKET_STATUS( p_packet, NDIS_STATUS_SUCCESS );

			cl_spinlock_release( &p_port->recv_lock );
			cl_perf_start( ReturnNdisIndicate );
			NdisMIndicateReceivePacket( p_port->p_adapter->h_adapter,
				&p_packet, 1 );
			cl_perf_stop( &p_port->p_adapter->perf, ReturnNdisIndicate );
			cl_spinlock_acquire( &p_port->recv_lock );

			if( shortage > 0 )
			{
				cl_perf_start( ReturnPutRecv );
				__buf_mgr_put_recv( p_port, p_desc, p_packet );
				cl_perf_stop( &p_port->p_adapter->perf, ReturnPutRecv );

				/* Repost buffers. */
				cl_perf_start( ReturnRepostRecv );
				shortage = __recv_mgr_repost( p_port );
				cl_perf_stop( &p_port->p_adapter->perf, ReturnRepostRecv );
			}
		}
		else if( status != IB_NOT_DONE )
		{
			IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("__recv_mgr_prepare_pkt returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			/* Return the item to the head of the list. */
			cl_qlist_insert_head( &p_port->recv_mgr.done_list, p_item );
			break;
		}
	}
	cl_spinlock_release( &p_port->recv_lock );
	cl_perf_stop( &p_port->p_adapter->perf, ReturnPacket );

	IPOIB_EXIT( IPOIB_DBG_RECV );
}

static BOOLEAN
__recv_cb_internal(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context,
	IN				uint32_t					 *p_recv_cnt
	);


static void
__iopoib_WorkItem(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				void*						context )
{

	ipoib_port_t *p_port = ( ipoib_port_t* ) context;
	BOOLEAN WorkToDo = TRUE;
	KIRQL irql;
	uint32_t recv_cnt = 0;
	uint32_t total_recv_cnt = 0;

	UNREFERENCED_PARAMETER(p_dev_obj);

	while (WorkToDo && total_recv_cnt < 512) {
		irql = KeRaiseIrqlToDpcLevel();
		WorkToDo = __recv_cb_internal(NULL, p_port, &recv_cnt);
		KeLowerIrql(irql);
		total_recv_cnt += recv_cnt;
	}

	if (WorkToDo) {
		IoQueueWorkItem( p_port->pPoWorkItem, __iopoib_WorkItem, DelayedWorkQueue, p_port);
	} else {
		// Release the reference count that was incremented when queued the work item.
		ipoib_port_deref( p_port, ref_recv_cb );
	}
}


static BOOLEAN
__recv_cb_internal(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context,
	IN				uint32_t*					 p_recv_cnt)
{
	ipoib_port_t		*p_port;
	ib_api_status_t		status;
	ib_wc_t				wc[MAX_RECV_WC], *p_free, *p_wc;
	int32_t				pkt_cnt, recv_cnt = 0, shortage, discarded;
	cl_qlist_t			done_list, bad_list;
	size_t				i;
	BOOLEAN 			WorkToDo = FALSE;
	
	PERF_DECLARE( RecvCompBundle );
	PERF_DECLARE( RecvCb );
	PERF_DECLARE( PollRecv );
	PERF_DECLARE( RepostRecv );
	PERF_DECLARE( FilterRecv );
	PERF_DECLARE( BuildPktArray );
	PERF_DECLARE( RecvNdisIndicate );
	PERF_DECLARE( RearmRecv );
	PERF_DECLARE( PutRecvList );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	cl_perf_clr( RecvCompBundle );

	cl_perf_start( RecvCb );

	p_port = (ipoib_port_t*)cq_context;

	cl_qlist_init( &done_list );
	cl_qlist_init( &bad_list );

	ipoib_port_ref( p_port, ref_recv_cb );
	for( i = 0; i < MAX_RECV_WC; i++ )
		wc[i].p_next = &wc[i + 1];
	wc[MAX_RECV_WC - 1].p_next = NULL;

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

		cl_perf_start( PollRecv );
		status = p_port->p_adapter->p_ifc->poll_cq(
			p_port->ib_mgr.h_recv_cq, &p_free, &p_wc );
		cl_perf_stop( &p_port->p_adapter->perf, PollRecv );
		CL_ASSERT( status == IB_SUCCESS || status == IB_NOT_FOUND );

		/* Look at the payload now and filter ARP and DHCP packets. */
		cl_perf_start( FilterRecv );
		recv_cnt += __recv_mgr_filter( p_port, p_wc, &done_list, &bad_list );
		cl_perf_stop( &p_port->p_adapter->perf, FilterRecv );

	} while( (!p_free) && (recv_cnt < 128));

	*p_recv_cnt = (uint32_t)recv_cnt;

	/* We're done looking at the endpoint map, release the reference. */
	cl_atomic_dec( &p_port->endpt_rdr );

	cl_perf_log( &p_port->p_adapter->perf, RecvCompBundle, recv_cnt );

	cl_spinlock_acquire( &p_port->recv_lock );

	/* Update our posted depth. */
	p_port->recv_mgr.depth -= recv_cnt;

	/* Return any discarded receives to the pool */
	cl_perf_start( PutRecvList );
	__buf_mgr_put_recv_list( p_port, &bad_list );
	cl_perf_stop( &p_port->p_adapter->perf, PutRecvList );

	do
	{
		/* Repost ASAP so we don't starve the RQ. */
		cl_perf_start( RepostRecv );
		shortage = __recv_mgr_repost( p_port );
		cl_perf_stop( &p_port->p_adapter->perf, RepostRecv );

		cl_perf_start( BuildPktArray );
		/* Notify NDIS of any and all possible receive buffers. */
		pkt_cnt = __recv_mgr_build_pkt_array(
			p_port, shortage, &done_list, &discarded );
		cl_perf_stop( &p_port->p_adapter->perf, BuildPktArray );

		/* Only indicate receives if we actually had any. */
		if( discarded && shortage > 0 )
		{
			/* We may have thrown away packets, and have a shortage */
			cl_perf_start( RepostRecv );
			__recv_mgr_repost( p_port );
			cl_perf_stop( &p_port->p_adapter->perf, RepostRecv );
		}

		if( !pkt_cnt )
			break;

		cl_spinlock_release( &p_port->recv_lock );

		cl_perf_start( RecvNdisIndicate );
		NdisMIndicateReceivePacket( p_port->p_adapter->h_adapter,
			p_port->recv_mgr.recv_pkt_array, pkt_cnt );
		cl_perf_stop( &p_port->p_adapter->perf, RecvNdisIndicate );

		/*
		 * Cap the number of receives to put back to what we just indicated
		 * with NDIS_STATUS_RESOURCES.
		 */
		if( shortage > 0 )
		{
			if( pkt_cnt < shortage )
				shortage = pkt_cnt;

			/* Return all but the last packet to the pool. */
			cl_spinlock_acquire( &p_port->recv_lock );
			while( shortage-- > 1 )
			{
				__buf_mgr_put_recv( p_port,
					IPOIB_RECV_FROM_PACKET( p_port->recv_mgr.recv_pkt_array[shortage] ),
					p_port->recv_mgr.recv_pkt_array[shortage] );
			}
			cl_spinlock_release( &p_port->recv_lock );

			/*
			 * Return the last packet as if NDIS returned it, so that we repost
			 * and report any other pending receives.
			 */
			ipoib_return_packet( NULL, p_port->recv_mgr.recv_pkt_array[0] );
		}
		cl_spinlock_acquire( &p_port->recv_lock );

	} while( pkt_cnt );
	cl_spinlock_release( &p_port->recv_lock );

	if (p_free ) {
		/*
		 * Rearm after filtering to prevent contention on the enpoint maps
		 * and eliminate the possibility of having a call to
		 * __endpt_mgr_insert find a duplicate.
		 */
		ASSERT(WorkToDo == FALSE);
		cl_perf_start( RearmRecv );
		status = p_port->p_adapter->p_ifc->rearm_cq(
			p_port->ib_mgr.h_recv_cq, FALSE );
		cl_perf_stop( &p_port->p_adapter->perf, RearmRecv );
		CL_ASSERT( status == IB_SUCCESS );

	} else {
		if (h_cq) {
			// increment reference to ensure no one release the object while work iteam is queued
			ipoib_port_ref( p_port, ref_recv_cb );
			IoQueueWorkItem( p_port->pPoWorkItem, __iopoib_WorkItem, DelayedWorkQueue, p_port);
			WorkToDo = FALSE;
		} else {
			WorkToDo = TRUE;
		}
	}
	ipoib_port_deref( p_port, ref_recv_cb );
	cl_perf_stop( &p_port->p_adapter->perf, RecvCb );

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return WorkToDo;
}


static void
__recv_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	uint32_t recv_cnt;
	
	__recv_cb_internal(h_cq, cq_context, &recv_cnt);
}


static void
__recv_get_endpts(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
	IN				ib_wc_t* const				p_wc,
		OUT			ipoib_endpt_t** const		pp_src,
		OUT			ipoib_endpt_t** const		pp_dst )
{
	ib_api_status_t		status;
	mac_addr_t			mac;
	PERF_DECLARE( GetEndptByGid );
	PERF_DECLARE( GetEndptByLid );
	PERF_DECLARE( EndptInsert );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	/* Setup our shortcut pointers based on whether GRH is valid. */
	if( p_wc->recv.ud.recv_opt & IB_RECV_OPT_GRH_VALID )
	{
		/* Lookup the source endpoints based on GID. */
		cl_perf_start( GetEndptByGid );
		*pp_src =
#if IPOIB_INLINE_RECV
			__endpt_mgr_get_by_gid( p_port, &p_desc->buf.ib.grh.src_gid );
#else	/* IPOIB_INLINE_RECV */
			__endpt_mgr_get_by_gid( p_port, &p_desc->p_buf->ib.grh.src_gid );
#endif	/* IPOIB_INLINE_RECV */
		cl_perf_stop( &p_port->p_adapter->perf, GetEndptByGid );

		/*
		 * Lookup the destination endpoint based on GID.
		 * This is used along with the packet filter to determine
		 * whether to report this to NDIS.
		 */
		cl_perf_start( GetEndptByGid );
		*pp_dst =
#if IPOIB_INLINE_RECV
			__endpt_mgr_get_by_gid( p_port, &p_desc->buf.ib.grh.dest_gid );
#else	/* IPOIB_INLINE_RECV */
			__endpt_mgr_get_by_gid( p_port, &p_desc->p_buf->ib.grh.dest_gid );
#endif	/* IPOIB_INLINE_RECV */
		cl_perf_stop( &p_port->p_adapter->perf, GetEndptByGid );

		/*
		 * Create the source endpoint if it does not exist.  Note that we
		 * can only do this for globally routed traffic since we need the
		 * information from the GRH to generate the MAC.
		 */
		if( !*pp_src )
		{
			status = ipoib_mac_from_guid(
#if IPOIB_INLINE_RECV
				p_desc->buf.ib.grh.src_gid.unicast.interface_id, p_port->p_adapter->params.guid_mask, &mac );
#else	/* IPOIB_INLINE_RECV */
				p_desc->p_buf->ib.grh.src_gid.unicast.interface_id, p_port->p_adapter->params.guid_mask, &mac );
#endif	/* IPOIB_INLINE_RECV */
			if( status != IB_SUCCESS )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("ipoib_mac_from_guid returned %s\n",
					p_port->p_adapter->p_ifc->get_err_str( status )) );
				return;
			}

			/* Create the endpoint. */
#if IPOIB_INLINE_RECV
			*pp_src = ipoib_endpt_create( &p_desc->buf.ib.grh.src_gid,
#else	/* IPOIB_INLINE_RECV */
			*pp_src = ipoib_endpt_create( &p_desc->p_buf->ib.grh.src_gid,
#endif	/* IPOIB_INLINE_RECV */
				p_wc->recv.ud.remote_lid, p_wc->recv.ud.remote_qp );
			if( !*pp_src )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("ipoib_endpt_create failed\n") );
				return;
			}
			cl_perf_start( EndptInsert );
			cl_obj_lock( &p_port->obj );
			status = __endpt_mgr_insert( p_port, mac, *pp_src );
			if( status != IB_SUCCESS )
			{
				cl_obj_unlock( &p_port->obj );
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("__endpt_mgr_insert returned %s\n",
					p_port->p_adapter->p_ifc->get_err_str( status )) );
				*pp_src = NULL;
				return;
			}
			cl_obj_unlock( &p_port->obj );
			cl_perf_stop( &p_port->p_adapter->perf, EndptInsert );
		}
	}
	else
	{
		/*
		 * Lookup the remote endpoint based on LID.  Note that only
		 * unicast traffic can be LID routed.
		 */
		cl_perf_start( GetEndptByLid );
		*pp_src = __endpt_mgr_get_by_lid( p_port, p_wc->recv.ud.remote_lid );
		cl_perf_stop( &p_port->p_adapter->perf, GetEndptByLid );
		*pp_dst = p_port->p_local_endpt;
		CL_ASSERT( *pp_dst );
	}

	if( *pp_src && !ipoib_is_voltaire_router_gid( &(*pp_src)->dgid ) &&
		(*pp_src)->qpn != p_wc->recv.ud.remote_qp )
	{
		/* Update the QPN for the endpoint. */
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Updating QPN for MAC: %02X-%02X-%02X-%02X-%02X-%02X\n",
			(*pp_src )->mac.addr[0], (*pp_src )->mac.addr[1],
			(*pp_src )->mac.addr[2], (*pp_src )->mac.addr[3],
			(*pp_src )->mac.addr[4], (*pp_src )->mac.addr[5]) );
		(*pp_src)->qpn = p_wc->recv.ud.remote_qp;
	}

	if( *pp_src && *pp_dst )
	{
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Recv:\n"
			"\tsrc MAC: %02X-%02X-%02X-%02X-%02X-%02X\n"
			"\tdst MAC: %02X-%02X-%02X-%02X-%02X-%02X\n",
			(*pp_src )->mac.addr[0], (*pp_src )->mac.addr[1],
			(*pp_src )->mac.addr[2], (*pp_src )->mac.addr[3],
			(*pp_src )->mac.addr[4], (*pp_src )->mac.addr[5],
			(*pp_dst )->mac.addr[0], (*pp_dst )->mac.addr[1],
			(*pp_dst )->mac.addr[2], (*pp_dst )->mac.addr[3],
			(*pp_dst )->mac.addr[4], (*pp_dst )->mac.addr[5]) );
	}

	IPOIB_EXIT( IPOIB_DBG_RECV );
}


static int32_t
__recv_mgr_filter(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_done_wc_list,
		OUT			cl_qlist_t* const			p_done_list,
		OUT			cl_qlist_t* const			p_bad_list )
{
	ipoib_recv_desc_t		*p_desc;
	ib_wc_t					*p_wc;
	ipoib_pkt_t				*p_ipoib;
	eth_pkt_t				*p_eth;
	ipoib_endpt_t			*p_src, *p_dst;
	ib_api_status_t			status;
	uint32_t				len;
	int32_t					recv_cnt = 0;
	PERF_DECLARE( GetRecvEndpts );
	PERF_DECLARE( RecvGen );
	PERF_DECLARE( RecvTcp );
	PERF_DECLARE( RecvUdp );
	PERF_DECLARE( RecvDhcp );
	PERF_DECLARE( RecvArp );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	for( p_wc = p_done_wc_list; p_wc; p_wc = p_wc->p_next )
	{
		CL_ASSERT( p_wc->status != IB_WCS_SUCCESS || p_wc->wc_type == IB_WC_RECV );
		p_desc = (ipoib_recv_desc_t*)(uintn_t)p_wc->wr_id;
		recv_cnt++;

		if( p_wc->status != IB_WCS_SUCCESS )
		{
			if( p_wc->status != IB_WCS_WR_FLUSHED_ERR )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Failed completion %s  (vendor specific %#x)\n",
					p_port->p_adapter->p_ifc->get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific) );
				ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0 );
			}
			else
			{
				IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
					("Flushed completion %s\n",
					p_port->p_adapter->p_ifc->get_wc_status_str( p_wc->status )) );
				ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_DROPPED, 0 );
			}
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			/* Dereference the port object on behalf of the failed receive. */
			ipoib_port_deref( p_port, ref_failed_recv_wc );
			continue;
		}

		len = p_wc->length - sizeof(ib_grh_t);

		if( len < sizeof(ipoib_hdr_t) )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received ETH packet < min size\n") );
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			ipoib_port_deref( p_port, ref_recv_inv_len );
			continue;
		}

		if((len - sizeof(ipoib_hdr_t)) > p_port->p_adapter->params.payload_mtu)
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received ETH packet > payload MTU (%d)\n",
				p_port->p_adapter->params.payload_mtu) );
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			ipoib_port_deref( p_port, ref_recv_inv_len );
			continue;
			
		}
		/* Successful completion.  Get the receive information. */
		p_desc->ndis_csum.Value = ( (p_wc->recv.ud.recv_opt & IB_RECV_OPT_CSUM_MASK ) >> 8 );
		cl_perf_start( GetRecvEndpts );
		__recv_get_endpts( p_port, p_desc, p_wc, &p_src, &p_dst );
		cl_perf_stop( &p_port->p_adapter->perf, GetRecvEndpts );

#if IPOIB_INLINE_RECV
		p_ipoib = &p_desc->buf.ib.pkt;
		p_eth = &p_desc->buf.eth.pkt;
#else	/* IPOIB_INLINE_RECV */
		p_ipoib = &p_desc->p_buf->ib.pkt;
		p_eth = &p_desc->p_buf->eth.pkt;
#endif	/*IPOIB_INLINE_RECV */

		if( p_src )
		{
			/* Don't report loopback traffic - we requested SW loopback. */
			if( !cl_memcmp( &p_port->p_adapter->params.conf_mac,
				&p_src->mac, sizeof(p_port->p_adapter->params.conf_mac) ) )
			{
				/*
				 * "This is not the packet you're looking for" - don't update
				 * receive statistics, the packet never happened.
				 */
				cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
				/* Dereference the port object on behalf of the failed recv. */
				ipoib_port_deref( p_port, ref_recv_loopback );
				continue;
			}
		}

		switch( p_ipoib->hdr.type )
		{
		case ETH_PROT_TYPE_IP:
			if( len < (sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received IP packet < min size\n") );
				status = IB_INVALID_SETTING;
				break;
			}

			if( p_ipoib->type.ip.hdr.offset ||
				p_ipoib->type.ip.hdr.prot != IP_PROT_UDP )
			{
				/* Unfiltered.  Setup the ethernet header and report. */
				cl_perf_start( RecvTcp );
				status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
				cl_perf_stop( &p_port->p_adapter->perf, RecvTcp );
				break;
			}

			/* First packet of a UDP transfer. */
			if( len <
				(sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t) + sizeof(udp_hdr_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received UDP packet < min size\n") );
				status = IB_INVALID_SETTING;
				break;
			}

			/* Check if DHCP conversion is required. */
			if( (p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_SERVER &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_CLIENT) ||
				(p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_CLIENT &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_SERVER) ||
				(p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_PROXY_SERVER &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_CLIENT) ||
				(p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_CLIENT &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_PROXY_SERVER) )
			{
				if( len < (sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t) +
					sizeof(udp_hdr_t) + DHCP_MIN_SIZE) )
				{
					IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Received DHCP < min size\n") );
					status = IB_INVALID_SETTING;
					break;
				}
				if ((p_ipoib->type.ip.hdr.ver_hl & 0x0f) != 5 ) {
					// If there are IP options in this message, we are in trouble in any case
					status = IB_INVALID_SETTING;
					break;					
				}
				/* UDP packet with BOOTP ports in src/dst port numbers. */
				cl_perf_start( RecvDhcp );
				status = __recv_dhcp( p_port, p_ipoib, p_eth, p_src, p_dst );
				cl_perf_stop( &p_port->p_adapter->perf, RecvDhcp );
			}
			else
			{
				/* Unfiltered.  Setup the ethernet header and report. */
				cl_perf_start( RecvUdp );
				status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
				cl_perf_stop( &p_port->p_adapter->perf, RecvUdp );
			}
			break;

		case ETH_PROT_TYPE_ARP:
			if( len < (sizeof(ipoib_hdr_t) + sizeof(ipoib_arp_pkt_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received ARP < min size\n") );
				status = IB_INVALID_SETTING;
				break;
			}
			cl_perf_start( RecvArp );
			status = __recv_arp( p_port, p_wc, p_ipoib, p_eth, &p_src, p_dst );
			cl_perf_stop( &p_port->p_adapter->perf, RecvArp );
			len = sizeof(ipoib_hdr_t) + sizeof(arp_pkt_t);
			break;

		default:
			/* Unfiltered.  Setup the ethernet header and report. */
			cl_perf_start( RecvGen );
			status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
			cl_perf_stop( &p_port->p_adapter->perf, RecvGen );
		}

		if( status != IB_SUCCESS )
		{
			/* Update stats. */
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			/* Dereference the port object on behalf of the failed receive. */
			ipoib_port_deref( p_port, ref_recv_filter );
		}
		else
		{
			p_desc->len =
				len + sizeof(eth_hdr_t) - sizeof(ipoib_hdr_t);
			if( p_dst->h_mcast)
			{
				if( p_dst->dgid.multicast.raw_group_id[10] == 0xFF &&
					p_dst->dgid.multicast.raw_group_id[11] == 0xFF &&
					p_dst->dgid.multicast.raw_group_id[12] == 0xFF &&
					p_dst->dgid.multicast.raw_group_id[13] == 0xFF )
				{
					p_desc->type = PKT_TYPE_BCAST;
				}
				else
				{
					p_desc->type = PKT_TYPE_MCAST;
				}
			}
			else
			{
				p_desc->type = PKT_TYPE_UCAST;
				
			}
			cl_qlist_insert_tail( p_done_list, &p_desc->item.list_item );
		}
	}

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return recv_cnt;
}


static ib_api_status_t
__recv_gen(
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst )
{
	IPOIB_ENTER( IPOIB_DBG_RECV );

	if( !p_src || !p_dst )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Received packet with no matching endpoints.\n") );
		return IB_NOT_DONE;
	}

	/*
	 * Fill in the ethernet header.  Note that doing so will overwrite
	 * the IPoIB header, so start by moving the information from the IPoIB
	 * header.
	 */
	p_eth->hdr.type = p_ipoib->hdr.type;
	p_eth->hdr.src = p_src->mac;
	p_eth->hdr.dst = p_dst->mac;

	if ( p_eth->hdr.dst.addr[0] == 1 && 
		 p_eth->hdr.type == ETH_PROT_TYPE_IP &&
		 p_eth->hdr.dst.addr[2] == 0x5E)  
	{
		p_eth->hdr.dst.addr[1] = 0;
		p_eth->hdr.dst.addr[3] = p_eth->hdr.dst.addr[3] & 0x7f;
	}
	if (p_dst->h_mcast)
		p_dst->is_in_use = TRUE;

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return IB_SUCCESS;
}


static ib_api_status_t
__recv_dhcp(
	IN				ipoib_port_t* const			p_port,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst )
{
	ib_api_status_t		status;
	dhcp_pkt_t			*p_dhcp;
	uint8_t				*p_option;
	uint8_t				*p_cid = NULL;
	uint8_t				msg = 0;

	IPOIB_ENTER( IPOIB_DBG_RECV );

	UNUSED_PARAM( p_port );

	/* Create the ethernet header. */
	status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__recv_gen returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Fixup the payload. */
	p_dhcp = &p_eth->type.ip.prot.udp.dhcp;
	if( p_dhcp->op != DHCP_REQUEST && p_dhcp->op != DHCP_REPLY )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid DHCP op code.\n") );
		return IB_INVALID_SETTING;
	}

	/*
	 * Find the client identifier option, making sure to skip
	 * the "magic cookie".
	 */
	p_option = &p_dhcp->options[0];
	if ( *(uint32_t *)p_option != DHCP_COOKIE )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("DHCP cookie corrupted.\n") );
		return IB_INVALID_PARAMETER;
	}

	p_option = &p_dhcp->options[DHCP_COOKIE_SIZE];
	while( *p_option != DHCP_OPT_END && p_option < &p_dhcp->options[312] )
	{
		switch( *p_option )
		{
		case DHCP_OPT_PAD:
			p_option++;
			break;

		case DHCP_OPT_MSG:
			msg = p_option[2];
			p_option += 3;
			break;

		case DHCP_OPT_CLIENT_ID:
			p_cid = p_option;
			/* Fall through. */

		default:
			/*
			 * All other options have a length byte following the option code.
			 * Offset by the length to get to the next option.
			 */
			p_option += (p_option[1] + 2);
		}
	}

	switch( msg )
	{
	/* message from client */
	case DHCPDISCOVER:
	case DHCPREQUEST:
	case DHCPDECLINE:
	case DHCPRELEASE:
	case DHCPINFORM:
		if( !p_cid )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to find required Client-identifier option.\n") );
			return IB_INVALID_SETTING;
		}
		if( p_dhcp->htype != DHCP_HW_TYPE_IB )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Invalid hardware address type.\n") );
			return IB_INVALID_SETTING;
		}
		break;
	/* message from DHCP server */
	case DHCPOFFER:
	case DHCPACK:
	case DHCPNAK:
		break;

	default:
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalide message type.\n") );
		return IB_INVALID_PARAMETER;
	}
	p_eth->type.ip.prot.udp.hdr.chksum = 0;
	p_dhcp->htype = DHCP_HW_TYPE_ETH;
	p_dhcp->hlen = HW_ADDR_LEN;

	if( p_cid ) /* from client */
	{
		int i;
		/* Validate that the length and type of the option is as required. */
		IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
					("DHCP CID received is:"));
		for ( i=0; i < coIPoIB_CID_TotalLen; ++i) {
			IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
				("[%d] 0x%x: \n",i, p_cid[i]));
		}
		if( p_cid[1] != coIPoIB_CID_Len )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Client-identifier length is not equal to %d as required.\n",coIPoIB_CID_Len) );
			return IB_INVALID_SETTING;
		}
		if( p_cid[2] != coIPoIB_HwTypeIB)
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Client-identifier type is %d <> %d and wrong \n", p_cid[2], coIPoIB_HwTypeIB) );
			return IB_INVALID_SETTING;
		}
		/*
		 * Copy the GID value from the option so that we can make aligned
		 * accesses to the contents.
		 * Recover CID to standard type.
		 */
		p_cid[1] =  sizeof (ib_net64_t) + 1;// CID length 
		p_cid[2] =  DHCP_HW_TYPE_ETH;// CID type
		//Copy the GUID to the 3-d byte of CID
		RtlMoveMemory( &p_cid[3], &p_cid[coIPoIB_CID_TotalLen - sizeof (ib_net64_t)], sizeof (ib_net64_t) );
		// Clear the rest
		RtlFillMemory(&p_cid[3+sizeof (ib_net64_t)],coIPoIB_CID_TotalLen - 3 -sizeof (ib_net64_t), 0);

		RtlCopyMemory( p_dhcp->chaddr, &p_src->mac, sizeof(p_src->mac) );
		RtlFillMemory( &p_dhcp->chaddr[sizeof(p_src->mac)],
			       ( sizeof(p_dhcp->chaddr) - sizeof(p_src->mac) ), 0 );
	}
	IPOIB_EXIT( IPOIB_DBG_RECV );
	return status;
}


static ib_api_status_t
__recv_arp(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_wc,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t** const		pp_src,
	IN				ipoib_endpt_t* const		p_dst )
{
	ib_api_status_t			status;
	arp_pkt_t				*p_arp;
	const ipoib_arp_pkt_t	*p_ib_arp;
	ib_gid_t				gid;
	mac_addr_t				mac;
	ipoib_hw_addr_t			null_hw = {0};

	IPOIB_ENTER( IPOIB_DBG_RECV );

	if( !p_dst )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Unknown destination endpoint\n") );
		return IB_INVALID_SETTING;
	}

	p_ib_arp = &p_ipoib->type.arp;
	p_arp = &p_eth->type.arp;

	if( p_ib_arp->hw_type != ARP_HW_TYPE_IB )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ARP hardware type is not IB\n") );
		return IB_INVALID_SETTING;
	}

	if( p_ib_arp->hw_size != sizeof(ipoib_hw_addr_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ARP hardware address size is not sizeof(ipoib_hw_addr_t)\n") );
		return IB_INVALID_SETTING;
	}

	if( p_ib_arp->prot_type != ETH_PROT_TYPE_IP )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ARP protocal type not IP\n") );
		return IB_INVALID_SETTING;
	}

	/*
	 * If we don't have a source, lookup the endpoint specified in the payload.
	 */
	if( !*pp_src )
		*pp_src = __endpt_mgr_get_by_gid( p_port, &p_ib_arp->src_hw.gid );

	/*
	 * If the endpoint exists for the GID, make sure
	 * the dlid and qpn match the arp.
	 */
	if( *pp_src )
	{
		if( cl_memcmp( &(*pp_src)->dgid, &p_ib_arp->src_hw.gid,
			sizeof(ib_gid_t) ) )
		{
			/*
			 * GIDs for the endpoint are different.  The ARP must
			 * have been proxied.  Dereference it.
			 */
			*pp_src = NULL;
		}
		else if( (*pp_src)->dlid &&
			(*pp_src)->dlid != p_wc->recv.ud.remote_lid )
		{
			/* Out of date!  Destroy the endpoint and replace it. */
			__endpt_mgr_remove( p_port, *pp_src );
			*pp_src = NULL;
		}
		else if ( ! ((*pp_src)->dlid)) {
			/* Out of date!  Destroy the endpoint and replace it. */
			__endpt_mgr_remove( p_port, *pp_src );
			*pp_src = NULL;
		}
		else if( ipoib_is_voltaire_router_gid( &(*pp_src)->dgid ) )
		{
			if( (*pp_src)->qpn !=
				(p_ib_arp->src_hw.flags_qpn & CL_HTON32(0x00FFFFFF)) &&
				p_wc->recv.ud.remote_qp !=
				(p_ib_arp->src_hw.flags_qpn & CL_HTON32(0x00FFFFFF)) )
			{
				/* Out of date!  Destroy the endpoint and replace it. */
				__endpt_mgr_remove( p_port, *pp_src );
				*pp_src = NULL;
			}
		}
		else if( (*pp_src)->qpn != p_wc->recv.ud.remote_qp )
		{
			/* Out of date!  Destroy the endpoint and replace it. */
			__endpt_mgr_remove( p_port, *pp_src );
			*pp_src = NULL;
		}
	}

	/* Do we need to create an endpoint for this GID? */
	if( !*pp_src )
	{
		/* Copy the src GID to allow aligned access */
		cl_memcpy( &gid, &p_ib_arp->src_hw.gid, sizeof(ib_gid_t) );
		status = ipoib_mac_from_guid( gid.unicast.interface_id, p_port->p_adapter->params.guid_mask, &mac );
		if (status == IB_INVALID_GUID_MASK)
		{
			IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_ERROR,
				("Invalid GUID mask received, rejecting it") );
			ipoib_create_log(p_port->p_adapter->h_adapter, GUID_MASK_LOG_INDEX, EVENT_IPOIB_WRONG_PARAMETER_WRN);
		}
		else if( status != IB_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ipoib_mac_from_guid returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			return status;
		}
		/*
		 * Create the endpoint.
		 */
		*pp_src = ipoib_endpt_create( &p_ib_arp->src_hw.gid,
			p_wc->recv.ud.remote_lid, (p_ib_arp->src_hw.flags_qpn & CL_HTON32(0x00FFFFFF)) );

		if( !*pp_src )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ipoib_endpt_create failed\n") );
			return status;
		}

		cl_obj_lock( &p_port->obj );
		status = __endpt_mgr_insert( p_port, mac, *pp_src );
		if( status != IB_SUCCESS )
		{
			cl_obj_unlock( &p_port->obj );
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__endpt_mgr_insert return %s \n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			return status;
		}

		cl_obj_unlock( &p_port->obj );
	}

	CL_ASSERT( !cl_memcmp(
		&(*pp_src)->dgid, &p_ib_arp->src_hw.gid, sizeof(ib_gid_t) ) );
	CL_ASSERT( ipoib_is_voltaire_router_gid( &(*pp_src)->dgid ) ||
		(*pp_src)->qpn ==
		(p_ib_arp->src_hw.flags_qpn & CL_HTON32(0x00FFFFFF)) );
	/* Now swizzle the data. */
	p_arp->hw_type = ARP_HW_TYPE_ETH;
	p_arp->hw_size = sizeof(mac_addr_t);
	p_arp->src_hw = (*pp_src)->mac;
	p_arp->src_ip = p_ib_arp->src_ip;

	if( cl_memcmp( &p_ib_arp->dst_hw, &null_hw, sizeof(ipoib_hw_addr_t) ) )
	{
		if( cl_memcmp( &p_dst->dgid, &p_ib_arp->dst_hw.gid, sizeof(ib_gid_t) ) )
		{
			/*
			 * We received bcast ARP packet that means
			 * remote port lets everyone know it was changed IP/MAC
			 * or just activated
			 */

			/* Guy: TODO: Check why this check fails in case of Voltaire IPR */

			if ( !ipoib_is_voltaire_router_gid( &(*pp_src)->dgid ) &&
				 !ib_gid_is_multicast( (const ib_gid_t*)&p_dst->dgid ) )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("ARP: is not ARP MCAST\n") );
				return IB_INVALID_SETTING;
			}

			p_arp->dst_hw = p_port->p_local_endpt->mac;
			p_dst->mac = p_port->p_local_endpt->mac;
			/*
			 * we don't care what receiver ip addr is,
			 * as long as OS' ARP table is global  ???
			 */
			p_arp->dst_ip = (net32_t)0;
		}
		else /* we've got reply to our ARP request */
		{
			p_arp->dst_hw = p_dst->mac;
			p_arp->dst_ip = p_ib_arp->dst_ip;
			CL_ASSERT( p_dst->qpn == 
				(p_ib_arp->dst_hw.flags_qpn & CL_HTON32(0x00FFFFFF)) );
		}
	}
	else /* we got ARP reqeust */
	{
		cl_memclr( &p_arp->dst_hw, sizeof(mac_addr_t) );
		p_arp->dst_ip = p_ib_arp->dst_ip;
	}

	/*
	 * Create the ethernet header.  Note that this is done last so that
	 * we have a chance to create a new endpoint.
	 */
	status = __recv_gen( p_ipoib, p_eth, *pp_src, p_dst );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__recv_gen returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return IB_SUCCESS;
}


static ib_api_status_t
__recv_mgr_prepare_pkt(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
		OUT			NDIS_PACKET** const			pp_packet )
{
	NDIS_STATUS							status;
	uint32_t							pkt_filter;
	ip_stat_sel_t						type;
	NDIS_TCP_IP_CHECKSUM_PACKET_INFO	chksum;

	PERF_DECLARE( GetNdisPkt );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	pkt_filter = p_port->p_adapter->packet_filter;
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
			type = IP_STAT_UCAST_BYTES;
			status = NDIS_STATUS_SUCCESS;
		}
		else
		{
			type = IP_STAT_DROPPED;
			status = NDIS_STATUS_FAILURE;
		}
		break;
	case PKT_TYPE_BCAST:
		if( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS ||
			pkt_filter & NDIS_PACKET_TYPE_BROADCAST )
		{
			/* OK to report. */
			type = IP_STAT_BCAST_BYTES;
			status = NDIS_STATUS_SUCCESS;
		}
		else
		{
			type = IP_STAT_DROPPED;
			status = NDIS_STATUS_FAILURE;
		}
		break;
	case PKT_TYPE_MCAST:
		if( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS ||
			pkt_filter & NDIS_PACKET_TYPE_ALL_MULTICAST ||
			pkt_filter & NDIS_PACKET_TYPE_MULTICAST )
		{
			/* OK to report. */
			type = IP_STAT_MCAST_BYTES;
			status = NDIS_STATUS_SUCCESS;
		}
		else
		{
			type = IP_STAT_DROPPED;
			status = NDIS_STATUS_FAILURE;
		}
		break;
	}

	if( status != NDIS_STATUS_SUCCESS )
	{
		ipoib_inc_recv_stat( p_port->p_adapter, type, 0 );
		/* Return the receive descriptor to the pool. */
		__buf_mgr_put_recv( p_port, p_desc, NULL );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Packet filter doesn't match receive.  Dropping.\n") );
		/*
		 * Return IB_NOT_DONE since the packet has been completed,
		 * but has not consumed an array entry.
		 */
		return IB_NOT_DONE;
	}

	cl_perf_start( GetNdisPkt );
	*pp_packet = __buf_mgr_get_ndis_pkt( p_port, p_desc );
	cl_perf_stop( &p_port->p_adapter->perf, GetNdisPkt );
	if( !*pp_packet )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__buf_mgr_get_ndis_pkt failed\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	chksum.Value = 0;
	switch (p_port->p_adapter->params.recv_chksum_offload) {
	  case CSUM_DISABLED:
		NDIS_PER_PACKET_INFO_FROM_PACKET( *pp_packet, TcpIpChecksumPacketInfo ) =
		(void*)(uintn_t)chksum.Value;
		break;
	  case CSUM_ENABLED:
		/* Get the checksums directly from packet information. */
		/* In this case, no one of cheksum's cat get false value */
		/* If hardware checksum failed or wasn't calculated, NDIS will recalculate it again */
		NDIS_PER_PACKET_INFO_FROM_PACKET( *pp_packet, TcpIpChecksumPacketInfo ) = 
			(void*)(uintn_t)(p_desc->ndis_csum.Value);
		break;
	  case CSUM_BYPASS:
		/* Flag the checksums as having been calculated. */
		chksum.Receive.NdisPacketTcpChecksumSucceeded = TRUE;
		chksum.Receive.NdisPacketUdpChecksumSucceeded = TRUE;
		chksum.Receive.NdisPacketIpChecksumSucceeded = TRUE;
		NDIS_PER_PACKET_INFO_FROM_PACKET( *pp_packet, TcpIpChecksumPacketInfo ) =
		(void*)(uintn_t)chksum.Value;
		break;
	  default:
		ASSERT(FALSE);
		NDIS_PER_PACKET_INFO_FROM_PACKET( *pp_packet, TcpIpChecksumPacketInfo ) =
		(void*)(uintn_t)chksum.Value;
	}
	ipoib_inc_recv_stat( p_port->p_adapter, type, p_desc->len );

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return IB_SUCCESS;
}


static uint32_t
__recv_mgr_build_pkt_array(
	IN				ipoib_port_t* const			p_port,
	IN				int32_t						shortage,
		OUT			cl_qlist_t* const			p_done_list,
		OUT			int32_t* const				p_discarded )
{
	cl_list_item_t			*p_item;
	ipoib_recv_desc_t		*p_desc;
	uint32_t				i = 0;
	ib_api_status_t			status;
	PERF_DECLARE( PreparePkt );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	*p_discarded = 0;

	/* Move any existing receives to the head to preserve ordering. */
	cl_qlist_insert_list_head( p_done_list, &p_port->recv_mgr.done_list );
	p_item = cl_qlist_remove_head( p_done_list );
	while( p_item != cl_qlist_end( p_done_list ) )
	{
		p_desc = (ipoib_recv_desc_t*)p_item;

		cl_perf_start( PreparePkt );
		status = __recv_mgr_prepare_pkt( p_port, p_desc,
			&p_port->recv_mgr.recv_pkt_array[i] );
		cl_perf_stop( &p_port->p_adapter->perf, PreparePkt );
		if( status == IB_SUCCESS )
		{
			CL_ASSERT( p_port->recv_mgr.recv_pkt_array[i] );
			if( shortage-- > 0 )
			{
				NDIS_SET_PACKET_STATUS(
					p_port->recv_mgr.recv_pkt_array[i], NDIS_STATUS_RESOURCES );
			}
			else
			{
				NDIS_SET_PACKET_STATUS(
					p_port->recv_mgr.recv_pkt_array[i], NDIS_STATUS_SUCCESS );
			}
			i++;
		}
		else if( status == IB_NOT_DONE )
		{
			(*p_discarded)++;
		}
		else
		{
			IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
				("__recv_mgr_prepare_pkt returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			/* Put all completed receives on the port's done list. */
			cl_qlist_insert_tail( &p_port->recv_mgr.done_list, p_item );
			cl_qlist_insert_list_tail( &p_port->recv_mgr.done_list, p_done_list );
			break;
		}

		p_item = cl_qlist_remove_head( p_done_list );
	}

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return i;
}




/******************************************************************************
*
* Send manager implementation.
*
******************************************************************************/
static void
__send_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_SEND );
	p_port->send_mgr.depth = 0;
	cl_qlist_init( &p_port->send_mgr.pending_list );
	IPOIB_EXIT( IPOIB_DBG_SEND );
}


static void 
__pending_list_destroy(
	IN				ipoib_port_t* const			p_port )
{
	cl_list_item_t	*p_item;
	NDIS_PACKET		*p_packet;
	
	cl_spinlock_acquire( &p_port->send_lock );
	/* Complete any pending packets. */
	for( p_item = cl_qlist_remove_head( &p_port->send_mgr.pending_list );
		p_item != cl_qlist_end( &p_port->send_mgr.pending_list );
		p_item = cl_qlist_remove_head( &p_port->send_mgr.pending_list ) )
	{
		p_packet = IPOIB_PACKET_FROM_LIST_ITEM( p_item );
		NdisMSendCompleteX( p_port->p_adapter->h_adapter, p_packet,
			NDIS_STATUS_RESET_IN_PROGRESS );
	}
	cl_spinlock_release( &p_port->send_lock );
}

static void
__send_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_SEND );
	__pending_list_destroy(p_port);

	IPOIB_EXIT( IPOIB_DBG_SEND );
}


static NDIS_STATUS
__send_mgr_filter(
	IN				ipoib_port_t* const			p_port,
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				NDIS_BUFFER* const			p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc )
{
	NDIS_STATUS		status;

	PERF_DECLARE( FilterIp );
	PERF_DECLARE( FilterArp );
	PERF_DECLARE( SendGen );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	/*
	 * We already checked the ethernet header length, so we know it's safe
	 * to decrement the buf_len without underflowing.
	 */
	buf_len -= sizeof(eth_hdr_t);

	switch( p_eth_hdr->type )
	{
	case ETH_PROT_TYPE_IP:
		cl_perf_start( FilterIp );
		status = __send_mgr_filter_ip(
			p_port, p_eth_hdr, p_buf, buf_len, p_desc );
		cl_perf_stop( &p_port->p_adapter->perf, FilterIp );
		break;

	case ETH_PROT_TYPE_ARP:
		cl_perf_start( FilterArp );
		status = __send_mgr_filter_arp(
			p_port, p_eth_hdr, p_buf, buf_len, p_desc );
		cl_perf_stop( &p_port->p_adapter->perf, FilterArp );
		break;

	default:
		/*
		 * The IPoIB spec doesn't define how to send non IP or ARP packets.
		 * Just send the payload and hope for the best.
		 */
		cl_perf_start( SendGen );
		status = __send_gen( p_port, p_desc, 0 );
		cl_perf_stop( &p_port->p_adapter->perf, SendGen );
		break;
	}

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}


static NDIS_STATUS
__send_copy(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc )
{
	NDIS_PACKET				*p_packet;
	NDIS_BUFFER				*p_buf;
	NDIS_STATUS				status;
	UINT					tot_len, bytes_copied = 0;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	p_desc->p_buf = 
		ExAllocateFromNPagedLookasideList( &p_port->buf_mgr.send_buf_list );
	if( !p_desc->p_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate buffer for packet copy.\n") );
		return NDIS_STATUS_RESOURCES;
	}

	NdisAllocatePacket( &status, &p_packet, p_port->buf_mgr.h_send_pkt_pool );
	if( status != NDIS_STATUS_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
			("Failed to allocate NDIS_PACKET for copy.\n") );
		return status;
	}

	NdisAllocateBuffer( &status, &p_buf, p_port->buf_mgr.h_send_buf_pool,
		p_desc->p_buf, p_port->p_adapter->params.xfer_block_size );
	if( status != NDIS_STATUS_SUCCESS )
	{
		NdisFreePacket( p_packet );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
			("Failed to allocate NDIS_BUFFER for copy.\n") );
		return status;
	}

	NdisChainBufferAtFront( p_packet, p_buf );

	NdisQueryPacketLength( p_desc->p_pkt, &tot_len );

	/* Setup the work request. */
	p_desc->local_ds[1].vaddr = cl_get_physaddr(
		((uint8_t*)p_desc->p_buf) + sizeof(eth_hdr_t) );
	p_desc->local_ds[1].length = tot_len - sizeof(eth_hdr_t);
	p_desc->local_ds[1].lkey = p_port->ib_mgr.lkey;
	p_desc->wr.num_ds = 2;

	/* Copy the packet. */
	NdisCopyFromPacketToPacketSafe( p_packet, bytes_copied, tot_len,
		p_desc->p_pkt, bytes_copied, &bytes_copied,
		NormalPagePriority );

	/* Free our temp packet now that the data is copied. */
	NdisUnchainBufferAtFront( p_packet, &p_buf );
	NdisFreeBuffer( p_buf );
	NdisFreePacket( p_packet );

	if( bytes_copied != tot_len )
	{
		/* Something went wrong.  Drop the packet. */
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to copy full packet: %d of %d bytes copied.\n",
			bytes_copied, tot_len) );
		return NDIS_STATUS_RESOURCES;
	}

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}


#if !IPOIB_USE_DMA
/* Send using the MDL's page information rather than the SGL. */
static ib_api_status_t
__send_gen(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc )
{
	uint32_t				i, j = 1;
	ULONG					offset;
	MDL						*p_mdl;
	UINT					num_pages, tot_len;
	ULONG					buf_len;
	PPFN_NUMBER				page_array;
	boolean_t				hdr_done = FALSE;
	ib_api_status_t			status;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	NdisQueryPacket( p_desc->p_pkt, &num_pages, NULL, &p_mdl,
		&tot_len );

	if( !p_mdl )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("No buffers associated with packet.\n") );
		return IB_ERROR;
	}

	/* Remember that one of the DS entries is reserved for the IPoIB header. */
	if( num_pages >= MAX_SEND_SGE )
	{
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Too many buffers to fit in WR ds_array.  Copying data.\n") );
		status = __send_copy( p_port, p_desc );
		IPOIB_EXIT( IPOIB_DBG_SEND );
		return status;
	}

	CL_ASSERT( tot_len > sizeof(eth_hdr_t) );
	CL_ASSERT( tot_len <= p_port->p_adapter->params.xfer_block_size );
	/*
	 * Assume that the ethernet header is always fully contained
	 * in the first page of the first MDL.  This makes for much
	 * simpler code.
	 */
	offset = MmGetMdlByteOffset( p_mdl ) + sizeof(eth_hdr_t);
	CL_ASSERT( offset <= PAGE_SIZE );

	while( tot_len )
	{
		buf_len = MmGetMdlByteCount( p_mdl );
		page_array = MmGetMdlPfnArray( p_mdl );
		CL_ASSERT( page_array );
		i = 0;
		if( !hdr_done )
		{
			CL_ASSERT( buf_len >= sizeof(eth_hdr_t) );
			/* Skip the ethernet header. */
			buf_len -= sizeof(eth_hdr_t);
			CL_ASSERT( buf_len <= p_port->p_adapter->params.payload_mtu );
			if( buf_len )
			{
				/* The ethernet header is a subset of this MDL. */
				CL_ASSERT( i == 0 );
				if( offset < PAGE_SIZE )
				{
					p_desc->local_ds[j].lkey = p_port->ib_mgr.lkey;
					p_desc->local_ds[j].vaddr = (page_array[i] << PAGE_SHIFT);
					/* Add the byte offset since we're on the 1st page. */
					p_desc->local_ds[j].vaddr += offset;
					if( offset + buf_len > PAGE_SIZE )
					{
						p_desc->local_ds[j].length = PAGE_SIZE - offset;
						buf_len -= p_desc->local_ds[j].length;
					}
					else
					{
						p_desc->local_ds[j].length = buf_len;
						buf_len = 0;
					}
					/* This data segment is done.  Move to the next. */
					j++;
				}
				/* This page is done.  Move to the next. */
				i++;
			}
			/* Done handling the ethernet header. */
			hdr_done = TRUE;
		}

		/* Finish this MDL */
		while( buf_len )
		{
			p_desc->local_ds[j].lkey = p_port->ib_mgr.lkey;
			p_desc->local_ds[j].vaddr = (page_array[i] << PAGE_SHIFT);
			/* Add the first page's offset if we're on the first page. */
			if( i == 0 )
				p_desc->local_ds[j].vaddr += MmGetMdlByteOffset( p_mdl );

			if( i == 0 && (MmGetMdlByteOffset( p_mdl ) + buf_len) > PAGE_SIZE )
			{
				/* Buffers spans pages. */
				p_desc->local_ds[j].length =
					PAGE_SIZE - MmGetMdlByteOffset( p_mdl );
				buf_len -= p_desc->local_ds[j].length;
				/* This page is done.  Move to the next. */
				i++;
			}
			else
			{
				/* Last page of the buffer. */
				p_desc->local_ds[j].length = buf_len;
				buf_len = 0;
			}
			/* This data segment is done.  Move to the next. */
			j++;
		}

		tot_len -= MmGetMdlByteCount( p_mdl );
		if( !tot_len )
			break;

		NdisGetNextBuffer( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get next buffer.\n") );
			return IB_ERROR;
		}
	}

	/* Set the number of data segments. */
	p_desc->wr.num_ds = j;

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return IB_SUCCESS;
}

#else

static NDIS_STATUS
__send_gen(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc,
	IN				INT lso_data_index)
{
	ib_api_status_t			status;
	SCATTER_GATHER_LIST		*p_sgl;
	uint32_t				i, j = 1;
	uint32_t				offset = sizeof(eth_hdr_t);
	PERF_DECLARE( SendCopy );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	p_sgl = NDIS_PER_PACKET_INFO_FROM_PACKET( p_desc->p_pkt,
		ScatterGatherListPacketInfo );
	if( !p_sgl )
	{
		ASSERT( p_sgl );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to get SGL from packet.\n") );
		return NDIS_STATUS_FAILURE;
	}

	/* Remember that one of the DS entries is reserved for the IPoIB header. */
	if( ( p_sgl->NumberOfElements >= MAX_SEND_SGE &&
		p_sgl->Elements[0].Length > sizeof(eth_hdr_t)) ||
		( p_sgl->NumberOfElements > MAX_SEND_SGE &&
		p_sgl->Elements[0].Length <= sizeof(eth_hdr_t)) )
	{
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Too many buffers to fit in WR ds_array.  Copying data.\n") );
		cl_perf_start( SendCopy );
		status = __send_copy( p_port, p_desc );
		cl_perf_stop( &p_port->p_adapter->perf, SendCopy );
		IPOIB_EXIT( IPOIB_DBG_SEND );
		return status;
	}

	/*
	 * Skip the ethernet header.  It is either the first element,
	 * or part of it.
	 */
	i = 0;
	if (lso_data_index) { //we have an LSO packet
		i = lso_data_index;
		j = 0;
	}
	else while( offset )
	{
		if( p_sgl->Elements[i].Length <= offset )
		{
			offset -= p_sgl->Elements[i++].Length;
		}
		else
		{
			p_desc->local_ds[j].vaddr =
				p_sgl->Elements[i].Address.QuadPart + offset;
			p_desc->local_ds[j].length =
				p_sgl->Elements[i].Length - offset;
			p_desc->local_ds[j].lkey = p_port->ib_mgr.lkey;
			i++;
			j++;
			break;
		}
	}
	/* Now fill in the rest of the local data segments. */
	while( i < p_sgl->NumberOfElements )
	{
		p_desc->local_ds[j].vaddr = p_sgl->Elements[i].Address.QuadPart;
		p_desc->local_ds[j].length = p_sgl->Elements[i].Length;
		p_desc->local_ds[j].lkey = p_port->ib_mgr.lkey;
		i++;
		j++;
	}

	/* Set the number of data segments. */
	p_desc->wr.num_ds = j;

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}
#endif


static NDIS_STATUS
__send_mgr_filter_ip(
	IN				ipoib_port_t* const			p_port,
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc )
{
	NDIS_STATUS		status;
	ip_hdr_t		*p_ip_hdr;

	PERF_DECLARE( QueryIp );
	PERF_DECLARE( SendTcp );
	PERF_DECLARE( FilterUdp );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	if( !buf_len )
	{
		cl_perf_start( QueryIp );
		NdisGetNextBuffer( p_buf, &p_buf );
		if( !p_buf )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get IP header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryBufferSafe( p_buf, &p_ip_hdr, &buf_len, NormalPagePriority );
		if( !p_ip_hdr )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query IP header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		cl_perf_stop( &p_port->p_adapter->perf, QueryIp );
	}
	else
	{
		p_ip_hdr = (ip_hdr_t*)(p_eth_hdr + 1);
	}
	if( buf_len < sizeof(ip_hdr_t) )
	{
		/* This buffer is done for.  Get the next buffer. */
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer too small for IP packet.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	if( p_ip_hdr->offset ||
		p_ip_hdr->prot != IP_PROT_UDP )
	{
		/* Check if this packet is IGMP */
		if ( p_ip_hdr->prot == IP_PROT_IGMP ) 
		{
			/*
			    In igmp packet I saw that iph arrive in 2 NDIS_BUFFERs:
				1. iph
				2. ip options
				So to get the IGMP packet we need to skip the ip options NDIS_BUFFER
			*/
			size_t iph_size_in_bytes = (p_ip_hdr->ver_hl & 0xf) * 4;
			size_t iph_options_size = iph_size_in_bytes - buf_len;
			buf_len -= sizeof(ip_hdr_t);//without ipheader

			/*
			    Could be a case that arrived igmp packet not from type IGMPv2 ,
				but IGMPv1 or IGMPv3.
				We anyway pass it to __send_mgr_filter_igmp_v2().
			*/
			__send_mgr_filter_igmp_v2(p_port, p_ip_hdr, iph_options_size, p_buf, buf_len);
		}
		/* Not a UDP packet. */
		cl_perf_start( SendTcp );
		status = __send_gen( p_port, p_desc,0 );
		cl_perf_stop( &p_port->p_adapter->perf, SendTcp );
		IPOIB_EXIT( IPOIB_DBG_SEND );
		return status;
	}

	buf_len -= sizeof(ip_hdr_t);

	cl_perf_start( FilterUdp );
	status = __send_mgr_filter_udp(
		p_port, p_ip_hdr, p_buf, buf_len, p_desc );
	cl_perf_stop( &p_port->p_adapter->perf, FilterUdp );

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}

static NDIS_STATUS
__send_mgr_filter_igmp_v2(
	IN				ipoib_port_t* const			p_port,
	IN		const	ip_hdr_t* const				p_ip_hdr,
	IN				size_t						iph_options_size,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len )
{
	igmp_v2_hdr_t		*p_igmp_v2_hdr = NULL;
	NDIS_STATUS			endpt_status;
	ipoib_endpt_t* 		p_endpt = NULL;
	mac_addr_t			fake_mcast_mac;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			 ("buf_len = %d,iph_options_size = %d\n",(int)buf_len,(int)iph_options_size ) );

	if( !buf_len )
	{
		// To get the IGMP packet we need to skip the ip options NDIS_BUFFER (if exists)
		while ( iph_options_size )
		{
			NdisGetNextBuffer( p_buf, &p_buf );
			if( !p_buf )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Failed to get IGMPv2 header buffer.\n") );
				return NDIS_STATUS_FAILURE;
			}
			NdisQueryBufferSafe( p_buf, &p_igmp_v2_hdr, &buf_len, NormalPagePriority );
			if( !p_igmp_v2_hdr )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Failed to query IGMPv2 header buffer.\n") );
				return NDIS_STATUS_FAILURE;
			}
			iph_options_size-=buf_len;
		}
        
		NdisGetNextBuffer( p_buf, &p_buf );
		if( !p_buf )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get IGMPv2 header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryBufferSafe( p_buf, &p_igmp_v2_hdr, &buf_len, NormalPagePriority );
		if( !p_igmp_v2_hdr )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query IGMPv2 header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
	}
	else
	{
		/* assuming ip header and options are in the same packet */
		p_igmp_v2_hdr = GetIpPayloadPtr(p_ip_hdr);
	}
	/* Get the IGMP header length. */
	if( buf_len < sizeof(igmp_v2_hdr_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer not large enough for IGMPv2 packet.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	// build fake mac from igmp packet group address
	fake_mcast_mac.addr[0] = 1;
	fake_mcast_mac.addr[1] = ((unsigned char*)&p_igmp_v2_hdr->group_address)[0] & 0x0f;
	fake_mcast_mac.addr[2] = 0x5E;
	fake_mcast_mac.addr[3] = ((unsigned char*)&p_igmp_v2_hdr->group_address)[1];
	fake_mcast_mac.addr[4] = ((unsigned char*)&p_igmp_v2_hdr->group_address)[2];
	fake_mcast_mac.addr[5] = ((unsigned char*)&p_igmp_v2_hdr->group_address)[3];

	switch ( p_igmp_v2_hdr->type )
	{
	case IGMP_V2_MEMBERSHIP_REPORT:
		/* 
			This mean that some body open listener on this group 
			Change type of mcast endpt to SEND_RECV endpt. So mcast garbage collector 
			will not delete this mcast endpt.
		*/
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			("Catched IGMP_V2_MEMBERSHIP_REPORT message\n") );
		endpt_status = __endpt_mgr_ref( p_port, fake_mcast_mac, &p_endpt );
		if ( p_endpt )
		{
			cl_obj_lock( &p_port->obj );
			p_endpt->is_mcast_listener = TRUE;
			cl_obj_unlock( &p_port->obj );
            ipoib_endpt_deref( p_endpt );
		}
		break;

	case IGMP_V2_LEAVE_GROUP:
		/* 
			This mean that somebody CLOSE listener on this group .
		    Change type of mcast endpt to SEND_ONLY endpt. So mcast 
			garbage collector will delete this mcast endpt next time.
		*/
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			     ("Catched IGMP_V2_LEAVE_GROUP message\n") );
		endpt_status = __endpt_mgr_ref( p_port, fake_mcast_mac, &p_endpt );
		if ( p_endpt )
		{
			cl_obj_lock( &p_port->obj );
			p_endpt->is_mcast_listener = FALSE;
			p_endpt->is_in_use = FALSE;
			cl_obj_unlock( &p_port->obj );
			ipoib_endpt_deref( p_endpt );
		}

		__port_do_mcast_garbage(p_port);

		break;

	default:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			     ("Send Unknown IGMP message: 0x%x \n", p_igmp_v2_hdr->type ) );
		break;
	}

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
__send_mgr_filter_udp(
	IN				ipoib_port_t* const			p_port,
	IN		const	ip_hdr_t* const				p_ip_hdr,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc )
{
	ib_api_status_t		status;
	udp_hdr_t			*p_udp_hdr;
	PERF_DECLARE( QueryUdp );
	PERF_DECLARE( SendUdp );
	PERF_DECLARE( FilterDhcp );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	if (p_ip_hdr->offset > 0) {
		/* This is a fragmented part of UDP packet
		 * Only first packet will contain UDP header in such case
		 * So, return if offset > 0
		 */
		cl_perf_start( SendUdp );
		status = __send_gen( p_port, p_desc,0 );
		cl_perf_stop( &p_port->p_adapter->perf, SendUdp );
		IPOIB_EXIT( IPOIB_DBG_SEND );
		return status;
	}
	
	if( !buf_len )
	{
		cl_perf_start( QueryUdp );
		NdisGetNextBuffer( p_buf, &p_buf );
		if( !p_buf )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get UDP header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryBufferSafe( p_buf, &p_udp_hdr, &buf_len, NormalPagePriority );
		if( !p_udp_hdr )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query UDP header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		cl_perf_stop( &p_port->p_adapter->perf, QueryUdp );
	}
	else
	{
		p_udp_hdr = (udp_hdr_t*)GetIpPayloadPtr(p_ip_hdr);
	}
	
	if( buf_len < sizeof(udp_hdr_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer not large enough for UDP packet.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	/* Get the UDP header and check the destination port numbers. */
	if( (p_udp_hdr->src_port != DHCP_PORT_CLIENT ||
		p_udp_hdr->dst_port != DHCP_PORT_SERVER) &&
		(p_udp_hdr->src_port != DHCP_PORT_SERVER ||
		p_udp_hdr->dst_port != DHCP_PORT_CLIENT) )
	{
		/* Not a DHCP packet. */
		cl_perf_start( SendUdp );
		status = __send_gen( p_port, p_desc,0 );
		cl_perf_stop( &p_port->p_adapter->perf, SendUdp );
		IPOIB_EXIT( IPOIB_DBG_SEND );
		return status;
	}

	buf_len -= sizeof(udp_hdr_t);

	/* Allocate our scratch buffer. */
	p_desc->p_buf = (send_buf_t*)
		ExAllocateFromNPagedLookasideList( &p_port->buf_mgr.send_buf_list );
	if( !p_desc->p_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to query DHCP packet buffer.\n") );
		return NDIS_STATUS_RESOURCES;
	}
	/* Copy the IP and UDP headers. */
	cl_memcpy( &p_desc->p_buf->ip.hdr, p_ip_hdr , sizeof(ip_hdr_t) );
	cl_memcpy(
		&p_desc->p_buf->ip.prot.udp.hdr, p_udp_hdr, sizeof(udp_hdr_t) );

	cl_perf_start( FilterDhcp );
	status = __send_mgr_filter_dhcp(
		p_port, p_udp_hdr, p_buf, buf_len, p_desc );
	cl_perf_stop( &p_port->p_adapter->perf, FilterDhcp );

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}

unsigned short ipchksum(unsigned short *ip, int len)
{
    unsigned long sum = 0;

    len >>= 1;
    while (len--) {
        sum += *(ip++);
        if (sum > 0xFFFF)
            sum -= 0xFFFF;
    }
    return (unsigned short)((~sum) & 0x0000FFFF);
}

static NDIS_STATUS
__send_mgr_filter_dhcp(
	IN				ipoib_port_t* const			p_port,
	IN		const	udp_hdr_t* const			p_udp_hdr,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc )
{
	dhcp_pkt_t			*p_dhcp;
	dhcp_pkt_t			*p_ib_dhcp;
	uint8_t				*p_option, *p_cid = NULL;
	uint8_t				msg = 0;
	size_t				len;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	if( !buf_len )
	{
		NdisGetNextBuffer( p_buf, &p_buf );
		if( !p_buf )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get DHCP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryBufferSafe( p_buf, &p_dhcp, &buf_len, NormalPagePriority );
		if( !p_dhcp )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query DHCP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
	}
	else
	{
		p_dhcp = (dhcp_pkt_t*)(p_udp_hdr + 1);
	}

	if( buf_len < DHCP_MIN_SIZE )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer not large enough for DHCP packet.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	p_ib_dhcp = &p_desc->p_buf->ip.prot.udp.dhcp;
	cl_memcpy( p_ib_dhcp, p_dhcp, buf_len );

	/* Now scan through the options looking for the client identifier. */
	p_option = &p_ib_dhcp->options[4];
	while( *p_option != DHCP_OPT_END && p_option < &p_ib_dhcp->options[312] )
	{
		switch( *p_option )
		{
		case DHCP_OPT_PAD:
			p_option++;
			break;

		case DHCP_OPT_MSG:
			msg = p_option[2];
			p_option += 3;
			break;

		case DHCP_OPT_CLIENT_ID:
			p_cid = p_option;
			/* Fall through. */

		default:
			/*
			 * All other options have a length byte following the option code.
			 * Offset by the length to get to the next option.
			 */
			p_option += (p_option[1] + 2);
		}
	}

	switch( msg )
	{
	/* Client messages */
	case DHCPDISCOVER:
	case DHCPREQUEST:
			p_ib_dhcp->flags |= DHCP_FLAGS_BROADCAST;
		/* Fall through */
	case DHCPDECLINE:
	case DHCPRELEASE:
	case DHCPINFORM:
		/* Fix up the client identifier option */
		if( p_cid )
		{
			/* The length of client identifier should be equal to  ETH MAC size */
			if( p_cid[1] == HW_ADDR_LEN+1 ) {

				/* MAC should be mine except the case below */
				if ( cl_memcmp( &p_cid[3], 
					&p_port->p_adapter->params.conf_mac.addr, HW_ADDR_LEN ) )
				
				{
					/* According to http://support.microsoft.com/kb/945948
					 * This behavior occurs because the server sends a Dynamic Host Configuration Protocol (DHCP) 
					 * INFORM message to the network. This DHCP INFORM message contains a MAC address that is 
					 *  unrelated to the addresses to which the physical network adapters are assigned. 
					 * The packets are expected. Therefore, the packets are not seen as malicious.
					 *  IPoIB will replace this demo MAC address by its GUID as for regular DHCP_INFORM packet
					 */
					 IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
							("NDIS sends client message with other than mine MAC ADDRESS to search other DHCP servers\n") );
				}
			
				/* Make sure there's room to extend it.  22 is the size of
				 * the CID option for IPoIB. (20 is the length, one byte for type and the second for lenght field)
				 */
				if( buf_len + coIPoIB_CID_TotalLen - p_cid[1] > sizeof(dhcp_pkt_t) )
				{
					IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Can't convert CID to IPoIB format.\n") );
					return NDIS_STATUS_RESOURCES;
				}
				/* Move the existing options down, and add a new CID option */
				len = p_option - ( p_cid + p_cid[1] + 2 );
				p_option = p_cid + p_cid[1] + 2;
				RtlMoveMemory( p_cid, p_option, len );
				
				p_cid += len;
				p_cid[0] = DHCP_OPT_CLIENT_ID;
				p_cid[1] = coIPoIB_CID_Len;
			} 
			else
			{
				ASSERT( FALSE );
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					(" Invalid Client Identifier Format\n") );
				return NDIS_STATUS_INVALID_DATA;
			}
				
				
		}
		else
		{
			/*
			 * Make sure there's room to extend it.  23 is the size of
			 * the CID option for IPoIB.
			 */
			if( buf_len + coIPoIB_CID_TotalLen > sizeof(dhcp_pkt_t) )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Can't convert CID to IPoIB format.\n") );
				return NDIS_STATUS_RESOURCES;
			}

			p_cid = p_option;
			p_cid[0] = DHCP_OPT_CLIENT_ID;
			p_cid[1] = coIPoIB_CID_Len;
		}

		CL_ASSERT( p_cid[1] == coIPoIB_CID_Len);
		p_cid[coIPoIB_CID_TotalLen]= DHCP_OPT_END;
		
		// Copy the default prefix for ALL DHCP messages
		cl_memcpy( &p_cid[2], &coIBDefaultDHCPPrefix[0], sizeof coIBDefaultDHCPPrefix);
		// Copy the GUID into the last 8 bytes of the CID field
		cl_memcpy( &p_cid[2+ sizeof(coIBDefaultDHCPPrefix)],&p_port->p_adapter->guids.port_guid.guid , 
			sizeof(p_port->p_adapter->guids.port_guid.guid) );
		
		p_ib_dhcp->htype = DHCP_HW_TYPE_IB;

		break;

	/* Server messages. */
	case DHCPOFFER:
	case DHCPACK:
	case DHCPNAK:
		/* don't touch server messages */
		break;

	default:
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalide message type.\n") );
		return NDIS_STATUS_INVALID_DATA;
	}

	/* update lengths to include any change we made */
	p_desc->p_buf->ip.hdr.length = cl_ntoh16( sizeof(ip_hdr_t) + sizeof(udp_hdr_t) + sizeof(dhcp_pkt_t) );
	p_desc->p_buf->ip.prot.udp.hdr.length = cl_ntoh16( sizeof(udp_hdr_t) + sizeof(dhcp_pkt_t) );

	/* update crc in ip header */
	p_desc->p_buf->ip.hdr.chksum = 0;
	p_desc->p_buf->ip.hdr.chksum = ipchksum((unsigned short*) &p_desc->p_buf->ip.hdr, sizeof(ip_hdr_t));

	/* no chksum for udp */
	p_desc->p_buf->ip.prot.udp.hdr.chksum = 0;
	p_desc->local_ds[1].vaddr = cl_get_physaddr( p_desc->p_buf );
	p_desc->local_ds[1].length = sizeof(ip_hdr_t) 
								+ sizeof(udp_hdr_t) 
								+ sizeof(dhcp_pkt_t);
	p_desc->local_ds[1].lkey = p_port->ib_mgr.lkey;
	p_desc->wr.num_ds = 2;
	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}


static NDIS_STATUS
__send_mgr_filter_arp(
	IN				ipoib_port_t* const			p_port,
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				NDIS_BUFFER*				p_buf,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc )
{
	arp_pkt_t			*p_arp;
	ipoib_arp_pkt_t		*p_ib_arp;
	NDIS_STATUS			status;
	mac_addr_t			null_hw = {0};

	IPOIB_ENTER( IPOIB_DBG_SEND );

	if( !buf_len )
	{
		NdisGetNextBuffer( p_buf, &p_buf );
		if( !p_buf )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get ARP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryBufferSafe( p_buf, &p_arp, &buf_len, NormalPagePriority );
		if( !p_arp )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get query ARP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
	}
	else
	{
		p_arp = (arp_pkt_t*)(p_eth_hdr + 1);
	}

	/* Single buffer ARP packet. */
	if( buf_len < sizeof(arp_pkt_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer too short for ARP.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	if( p_arp->prot_type != ETH_PROT_TYPE_IP )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Unsupported protocol type.\n") );
		return NDIS_STATUS_INVALID_DATA;
	}

	/* Allocate our scratch buffer. */
	p_desc->p_buf = (send_buf_t*)
		ExAllocateFromNPagedLookasideList( &p_port->buf_mgr.send_buf_list );
	if( !p_desc->p_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to query ARP packet buffer.\n") );
		return NDIS_STATUS_RESOURCES;
	}
	p_ib_arp = (ipoib_arp_pkt_t*)p_desc->p_buf;

	/* Convert the ARP payload. */
	p_ib_arp->hw_type = ARP_HW_TYPE_IB;
	p_ib_arp->prot_type = p_arp->prot_type;
	p_ib_arp->hw_size = sizeof(ipoib_hw_addr_t);
	p_ib_arp->prot_size = p_arp->prot_size;
	p_ib_arp->op = p_arp->op;
	p_ib_arp->src_hw.flags_qpn = p_port->ib_mgr.qpn;
	ib_gid_set_default( &p_ib_arp->src_hw.gid,
		p_port->p_adapter->guids.port_guid.guid );
	p_ib_arp->src_ip = p_arp->src_ip;
	if( cl_memcmp( &p_arp->dst_hw, &null_hw, sizeof(mac_addr_t) ) )
	{
		/* Get the endpoint referenced by the dst_hw address. */
		status = __endpt_mgr_get_gid_qpn( p_port, p_arp->dst_hw,
			&p_ib_arp->dst_hw.gid, &p_ib_arp->dst_hw.flags_qpn );
		if( status != NDIS_STATUS_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed lookup of destination HW address\n") );
			return status;
		}
	}
	else
	{
		cl_memclr( &p_ib_arp->dst_hw, sizeof(ipoib_hw_addr_t) );
	}
	p_ib_arp->dst_ip = p_arp->dst_ip;

	p_desc->local_ds[1].vaddr = cl_get_physaddr( p_ib_arp );
	p_desc->local_ds[1].length = sizeof(ipoib_arp_pkt_t);
	p_desc->local_ds[1].lkey = p_port->ib_mgr.lkey;
	p_desc->wr.num_ds = 2;

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}


static inline NDIS_STATUS
__send_mgr_get_eth_hdr(
	IN				NDIS_PACKET* const			p_packet,
		OUT			NDIS_BUFFER** const			pp_buf,
		OUT			eth_hdr_t** const			pp_eth_hdr,
		OUT			UINT*						p_buf_len )
{
	UINT				tot_len;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	NdisGetFirstBufferFromPacketSafe(
		p_packet, pp_buf, pp_eth_hdr, p_buf_len, &tot_len, NormalPagePriority );

	if( !*pp_eth_hdr )
	{
		/* Failed to get first buffer. */
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisMGetFirstBufferSafe failed.\n") );
		return NDIS_STATUS_FAILURE;
	}

	if( *p_buf_len < sizeof(eth_hdr_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("First buffer in packet smaller than eth_hdr_t: %d.\n",
			*p_buf_len) );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
		("Ethernet header:\n"
		"\tsrc MAC: %02X-%02X-%02X-%02X-%02X-%02X\n"
		"\tdst MAC: %02X-%02X-%02X-%02X-%02X-%02X\n"
		"\tprotocol type: %04X\n",
		(*pp_eth_hdr)->src.addr[0], (*pp_eth_hdr)->src.addr[1],
		(*pp_eth_hdr)->src.addr[2], (*pp_eth_hdr)->src.addr[3],
		(*pp_eth_hdr)->src.addr[4], (*pp_eth_hdr)->src.addr[5],
		(*pp_eth_hdr)->dst.addr[0], (*pp_eth_hdr)->dst.addr[1],
		(*pp_eth_hdr)->dst.addr[2], (*pp_eth_hdr)->dst.addr[3],
		(*pp_eth_hdr)->dst.addr[4], (*pp_eth_hdr)->dst.addr[5],
		cl_ntoh16( (*pp_eth_hdr)->type )) );

	return NDIS_STATUS_SUCCESS;
}


static inline NDIS_STATUS
__send_mgr_queue(
	IN				ipoib_port_t* const			p_port,
	IN				eth_hdr_t* const			p_eth_hdr,
		OUT			ipoib_endpt_t** const		pp_endpt )
{
	NDIS_STATUS			status;

	PERF_DECLARE( GetEndpt );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	/* Check the send queue and pend the request if not empty. */
	if( cl_qlist_count( &p_port->send_mgr.pending_list ) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
			("Pending list not empty.\n") );
		return NDIS_STATUS_PENDING;
	}

	/* Check the send queue and pend the request if not empty. */
	if( p_port->send_mgr.depth == p_port->p_adapter->params.sq_depth )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
			("No available WQEs.\n") );
		return NDIS_STATUS_PENDING;
	}

	cl_perf_start( GetEndpt );
	status = __endpt_mgr_ref( p_port, p_eth_hdr->dst, pp_endpt );
	cl_perf_stop( &p_port->p_adapter->perf, GetEndpt );

	if( status == NDIS_STATUS_NO_ROUTE_TO_DESTINATION &&
		ETH_IS_MULTICAST( p_eth_hdr->dst.addr ) )
	{
		if( ipoib_port_join_mcast( p_port, p_eth_hdr->dst, 
			IB_MC_REC_STATE_FULL_MEMBER) == IB_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
				("Multicast Mac - trying to join.\n") );
			return NDIS_STATUS_PENDING;
		}
	}
	else if ( status == NDIS_STATUS_SUCCESS && 
			  ETH_IS_MULTICAST( p_eth_hdr->dst.addr ) &&  
			  !ETH_IS_BROADCAST( p_eth_hdr->dst.addr ) )
	{
		CL_ASSERT( (*pp_endpt) );
		CL_ASSERT((*pp_endpt)->h_mcast != NULL);
		(*pp_endpt)->is_in_use = TRUE;
	}

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}


static NDIS_STATUS
__build_send_desc(
	IN				ipoib_port_t* const			p_port,
	IN				eth_hdr_t* const			p_eth_hdr,
	IN				NDIS_BUFFER* const			p_buf,
	IN		const	size_t						buf_len,
	IN	OUT			ipoib_send_desc_t* const	p_desc )
{
	NDIS_STATUS			status;
	int32_t				hdr_idx;
	PNDIS_PACKET_EXTENSION				PktExt;
	PNDIS_TCP_IP_CHECKSUM_PACKET_INFO 	pChecksumPktInfo; //NDIS 5.1
	ULONG								mss;
	LsoData								TheLsoData;
	INT									IndexOfData = 0;
	ULONG 								PhysBufCount;
	ULONG 								PacketLength;
	PNDIS_BUFFER 						FirstBuffer;
	uint16_t							lso_header_size;


	PERF_DECLARE( SendMgrFilter );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	/* Format the send descriptor. */
	cl_perf_start( SendMgrFilter );

	PktExt = NDIS_PACKET_EXTENSION_FROM_PACKET(p_desc->p_pkt);
	pChecksumPktInfo = (PNDIS_TCP_IP_CHECKSUM_PACKET_INFO)&PktExt->NdisPacketInfo[TcpIpChecksumPacketInfo];
	mss = PtrToUlong(PktExt->NdisPacketInfo[TcpLargeSendPacketInfo]);
	//TODO: optimization: we already got total length from NdisGetFirstBufferFromPacketSafe before
	NdisQueryPacket(p_desc->p_pkt, (PUINT)&PhysBufCount, NULL, &FirstBuffer,(PUINT)&PacketLength);

	/* Format the send descriptor. */
	hdr_idx = cl_atomic_inc( &p_port->hdr_idx );
	hdr_idx &= (p_port->p_adapter->params.sq_depth - 1);
	ASSERT( hdr_idx < p_port->p_adapter->params.sq_depth );
	p_port->hdr[hdr_idx].type = p_eth_hdr->type;
	p_port->hdr[hdr_idx].resv = 0;

	if (mss)
	{
		memset(&TheLsoData, 0, sizeof TheLsoData );
		status = GetLsoHeaderSize(
			p_port,
			FirstBuffer, 
			&TheLsoData, 
			&lso_header_size,
			&IndexOfData,
			&p_port->hdr[hdr_idx]
			
		);
		if ((status != NDIS_STATUS_SUCCESS ) || 
			(TheLsoData.FullBuffers != TheLsoData.UsedBuffers)) {
			ASSERT(FALSE);

			IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("<-- Throwing this packet\n"));

			//NdisReleaseSpinLock(&Port->SendLock);
			//MP_ASSERT_NDIS_PACKET_TYPE(Packet);
			//SendComplete(Port, Packet, NDIS_STATUS_INVALID_PACKET);
			//NdisAcquireSpinLock(&Port->SendLock);
			//IPOIB_PRINT_EXIT
			return status;
		}
		ASSERT(lso_header_size > 0);
		p_desc->wr.dgrm.ud.mss = mss;
		p_desc->wr.dgrm.ud.header = TheLsoData.LsoBuffers[0].pData;
		p_desc->wr.dgrm.ud.hlen = lso_header_size; 
		// Tell NDIS how much we will send.
		PktExt->NdisPacketInfo[TcpLargeSendPacketInfo] = UlongToPtr(PacketLength);
		p_desc->wr.send_opt |= (IB_SEND_OPT_TX_IP_CSUM | IB_SEND_OPT_TX_TCP_UDP_CSUM) | IB_SEND_OPT_SIGNALED;
		__send_gen(p_port, p_desc, IndexOfData);
		p_desc->wr.wr_type = WR_LSO;
	} else {

		/* Setup the first local data segment (used for the IPoIB header). */
		p_desc->local_ds[0].vaddr = cl_get_physaddr( &p_port->hdr[hdr_idx] );
		p_desc->local_ds[0].length = sizeof(ipoib_hdr_t);
		p_desc->local_ds[0].lkey = p_port->ib_mgr.lkey;

		status = __send_mgr_filter(
			p_port, p_eth_hdr, p_buf, buf_len, p_desc);
		cl_perf_stop( &p_port->p_adapter->perf, SendMgrFilter );
		if( status != NDIS_STATUS_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__send_mgr_filter returned 0x%08X.\n", status) );
			return status;
		}
		p_desc->wr.wr_type = WR_SEND;
		p_desc->wr.send_opt = IB_SEND_OPT_SIGNALED;
	}



	/* Setup the work request. */
	p_desc->wr.p_next = NULL;
	p_desc->wr.wr_id = (uintn_t)p_desc->p_pkt;

	if(p_port->p_adapter->params.send_chksum_offload && 
		(pChecksumPktInfo->Transmit.NdisPacketChecksumV4 || pChecksumPktInfo->Transmit.NdisPacketChecksumV6))
	{
		// Set transimition checksum offloading 
		if (pChecksumPktInfo->Transmit.NdisPacketIpChecksum) 
		{
			p_desc->wr.send_opt |= IB_SEND_OPT_TX_IP_CSUM;
		}
		if(pChecksumPktInfo->Transmit.NdisPacketTcpChecksum ||
		   pChecksumPktInfo->Transmit.NdisPacketUdpChecksum ) 
		{      
			p_desc->wr.send_opt |= IB_SEND_OPT_TX_TCP_UDP_CSUM;
		}
	}
	
	p_desc->wr.ds_array = p_desc->local_ds;

	p_desc->wr.dgrm.ud.remote_qp = p_desc->p_endpt1->qpn;
	p_desc->wr.dgrm.ud.remote_qkey = p_port->ib_mgr.bcast_rec.qkey;
	p_desc->wr.dgrm.ud.h_av = p_desc->p_endpt1->h_av;
	p_desc->wr.dgrm.ud.pkey_index = p_port->pkey_index;
	p_desc->wr.dgrm.ud.rsvd = NULL;

	/* Store context in our reserved area of the packet. */
	IPOIB_PORT_FROM_PACKET( p_desc->p_pkt ) = p_port;
	IPOIB_ENDPT_FROM_PACKET( p_desc->p_pkt ) = p_desc->p_endpt1;
	IPOIB_SEND_FROM_PACKET( p_desc->p_pkt ) = p_desc->p_buf;

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}


static inline void
__process_failed_send(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc,
	IN		const	NDIS_STATUS					status )
{
	IPOIB_ENTER( IPOIB_DBG_SEND );

	/* Complete the packet. */
	NdisMSendCompleteX( p_port->p_adapter->h_adapter,
		p_desc->p_pkt, status );
	ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_ERROR, 0 );
	/* Deref the endpoint. */
	if( p_desc->p_endpt1 )
		ipoib_endpt_deref( p_desc->p_endpt1 );

	if( p_desc->p_buf )
	{
		ExFreeToNPagedLookasideList(
			&p_port->buf_mgr.send_buf_list, p_desc->p_buf );
	}

	IPOIB_EXIT( IPOIB_DBG_SEND );
}


void
ipoib_port_send(
	IN				ipoib_port_t* const			p_port,
	IN				NDIS_PACKET					**p_packet_array,
	IN				uint32_t					num_packets )
{
	NDIS_STATUS			status;
	ib_api_status_t		ib_status;
	ipoib_send_desc_t	desc;
	uint32_t			i;
	eth_hdr_t			*p_eth_hdr;
	NDIS_BUFFER			*p_buf;
	UINT				buf_len;

	PERF_DECLARE( GetEthHdr );
	PERF_DECLARE( BuildSendDesc );
	PERF_DECLARE( QueuePacket );
	PERF_DECLARE( SendMgrQueue );
	PERF_DECLARE( PostSend );
	PERF_DECLARE( ProcessFailedSends );

	IPOIB_ENTER( IPOIB_DBG_SEND );


	cl_obj_lock( &p_port->obj );
	if( p_port->state != IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		for( i = 0; i < num_packets; ++i )
		{
			ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_DROPPED, 0 );
			/* Complete the packet. */
			NdisMSendCompleteX( p_port->p_adapter->h_adapter,
				p_packet_array[i], NDIS_STATUS_ADAPTER_NOT_READY );
			
		}

		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Invalid state - Aborting.\n") );
		return;
	}
	cl_obj_unlock( &p_port->obj );

	
	cl_spinlock_acquire( &p_port->send_lock );
	for( i = 0; i < num_packets; i++ )
	{
		desc.p_pkt = p_packet_array[i];
		desc.p_endpt1 = NULL;
		desc.p_buf = NULL;

		/* Get the ethernet header so we can find the endpoint. */
		cl_perf_start( GetEthHdr );
		status = __send_mgr_get_eth_hdr(
			p_packet_array[i], &p_buf, &p_eth_hdr, &buf_len );
		cl_perf_stop( &p_port->p_adapter->perf, GetEthHdr );
		if( status != NDIS_STATUS_SUCCESS )
		{
			cl_perf_start( ProcessFailedSends );
			__process_failed_send( p_port, &desc, status );
			cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
			continue;
		}

		cl_perf_start( SendMgrQueue );

		if ( ETH_IS_MULTICAST( p_eth_hdr->dst.addr ) && 
			 p_eth_hdr->type == ETH_PROT_TYPE_IP &&
			 !ETH_IS_BROADCAST( p_eth_hdr->dst.addr ) ) 
		{
			ip_hdr_t			*p_ip_hdr;
			NDIS_BUFFER			*p_ip_hdr_buf;
			UINT				ip_hdr_buf_len;

			// Extract the ip hdr 
			if(buf_len >= sizeof(ip_hdr_t)+ sizeof(eth_hdr_t))
			{
				p_ip_hdr = (ip_hdr_t*)(p_eth_hdr + 1);
				ip_hdr_buf_len = sizeof(ip_hdr_t);
			}
			else
			{
				NdisGetNextBuffer( p_buf, &p_ip_hdr_buf );
				if( !p_ip_hdr_buf )
				{
					IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Failed to get IP header buffer.\n") );
					goto h_end;
				}
		
				NdisQueryBufferSafe( p_ip_hdr_buf, &p_ip_hdr, &ip_hdr_buf_len, NormalPagePriority );
				if( !p_ip_hdr )
				{
					IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Failed to query IP header buffer.\n") );
					goto h_end;
				}
			}

			if( ip_hdr_buf_len < sizeof(ip_hdr_t) )
			{
				/* This buffer is done for.  Get the next buffer. */
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Buffer too small for IP packet.\n") );
				goto h_end;
			}
		
			p_eth_hdr->dst.addr[1] = ((unsigned char*)&p_ip_hdr->dst_ip)[0] & 0x0f;
			p_eth_hdr->dst.addr[3] = ((unsigned char*)&p_ip_hdr->dst_ip)[1];
		}
h_end:
		status = __send_mgr_queue( p_port, p_eth_hdr, &desc.p_endpt1 );
		cl_perf_stop( &p_port->p_adapter->perf, SendMgrQueue );
		if( status == NDIS_STATUS_PENDING )
		{
			/* Queue all remaining packets. */
			cl_perf_start( QueuePacket );
			while( i < num_packets )
			{
				cl_qlist_insert_tail( &p_port->send_mgr.pending_list,
					IPOIB_LIST_ITEM_FROM_PACKET( p_packet_array[i++] ) );
			}
			cl_perf_stop( &p_port->p_adapter->perf, QueuePacket );
			break;
		}
		if( status != NDIS_STATUS_SUCCESS )
		{
			ASSERT( status == NDIS_STATUS_NO_ROUTE_TO_DESTINATION );
			/*
			 * Complete the send as if we sent it - WHQL tests don't like the
			 * sends to fail.
			 */
			cl_perf_start( ProcessFailedSends );
			__process_failed_send( p_port, &desc, NDIS_STATUS_SUCCESS );
			cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
			continue;
		}

		cl_perf_start( BuildSendDesc );
		status = __build_send_desc( p_port, p_eth_hdr, p_buf, buf_len, &desc );
		cl_perf_stop( &p_port->p_adapter->perf, BuildSendDesc );
		if( status != NDIS_STATUS_SUCCESS )
		{
			cl_perf_start( ProcessFailedSends );
			__process_failed_send( p_port, &desc, status );
			cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
			continue;
		}

		/* Post the WR. */
		cl_perf_start( PostSend );
		ib_status = p_port->p_adapter->p_ifc->post_send( p_port->ib_mgr.h_qp, &desc.wr, NULL );
		cl_perf_stop( &p_port->p_adapter->perf, PostSend );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ib_post_send returned %s\n", 
				p_port->p_adapter->p_ifc->get_err_str( ib_status )) );
			cl_perf_start( ProcessFailedSends );
			__process_failed_send( p_port, &desc, NDIS_STATUS_FAILURE );
			cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
			/* Flag the adapter as hung since posting is busted. */
			p_port->p_adapter->hung = TRUE;
			continue;
		}

		cl_atomic_inc( &p_port->send_mgr.depth );
	}
	cl_spinlock_release( &p_port->send_lock );

	IPOIB_EXIT( IPOIB_DBG_SEND );
}


void
ipoib_port_resume(
	IN				ipoib_port_t* const			p_port )
{
	NDIS_STATUS			status;
	ib_api_status_t		ib_status;
	cl_list_item_t		*p_item;
	ipoib_send_desc_t	desc;
	eth_hdr_t			*p_eth_hdr;
	NDIS_BUFFER			*p_buf;
	UINT				buf_len;

	PERF_DECLARE( GetEndpt );
	PERF_DECLARE( BuildSendDesc );
	PERF_DECLARE( ProcessFailedSends );
	PERF_DECLARE( PostSend );

	IPOIB_ENTER( IPOIB_DBG_SEND );


	cl_obj_lock( &p_port->obj );
	if( p_port->state != IB_QPS_RTS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
			("Invalid state - Aborting.\n") );
		cl_obj_unlock( &p_port->obj );
		return;
	}
	cl_obj_unlock( &p_port->obj );

	cl_spinlock_acquire( &p_port->send_lock );

	for( p_item = cl_qlist_head( &p_port->send_mgr.pending_list );
		p_item != cl_qlist_end( &p_port->send_mgr.pending_list );
		p_item = cl_qlist_head( &p_port->send_mgr.pending_list ) )
	{
		/* Check the send queue and pend the request if not empty. */
		if( p_port->send_mgr.depth == p_port->p_adapter->params.sq_depth )
		{
			IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
				("No available WQEs.\n") );
			break;
		}

		desc.p_pkt = IPOIB_PACKET_FROM_LIST_ITEM(
			cl_qlist_remove_head( &p_port->send_mgr.pending_list ) );
		desc.p_endpt1 = NULL;
		desc.p_buf = NULL;

		/* Get the ethernet header so we can find the endpoint. */
		status = __send_mgr_get_eth_hdr(
			desc.p_pkt, &p_buf, &p_eth_hdr, &buf_len );
		if( status != NDIS_STATUS_SUCCESS )
		{
			cl_perf_start( ProcessFailedSends );
			__process_failed_send( p_port, &desc, status );
			cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
			continue;
		}

		cl_perf_start( GetEndpt );
		status = __endpt_mgr_ref( p_port, p_eth_hdr->dst, &desc.p_endpt1 );
		cl_perf_stop( &p_port->p_adapter->perf, GetEndpt );
		if( status == NDIS_STATUS_PENDING )
		{
			CL_ASSERT(desc.p_endpt1 == NULL);
			cl_qlist_insert_head( &p_port->send_mgr.pending_list,
				IPOIB_LIST_ITEM_FROM_PACKET( desc.p_pkt ) );
			break;
		}
		else if( status != NDIS_STATUS_SUCCESS )
		{
			ASSERT( status == NDIS_STATUS_NO_ROUTE_TO_DESTINATION );
			CL_ASSERT(desc.p_endpt1 == NULL);

			if( ETH_IS_MULTICAST( p_eth_hdr->dst.addr ) )
			{
				if( ipoib_port_join_mcast( p_port, p_eth_hdr->dst,
					IB_MC_REC_STATE_FULL_MEMBER) == IB_SUCCESS )
				{
					IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
						("Multicast Mac - trying to join.\n") );
					cl_qlist_insert_head( &p_port->send_mgr.pending_list,
						IPOIB_LIST_ITEM_FROM_PACKET( desc.p_pkt ) );
					break;
				}
			}

			/*
			 * Complete the send as if we sent it - WHQL tests don't like the
			 * sends to fail.
			 */
			cl_perf_start( ProcessFailedSends );
			__process_failed_send( p_port, &desc, NDIS_STATUS_SUCCESS );
			cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
			continue;
		}

		cl_perf_start( BuildSendDesc );
		status = __build_send_desc( p_port, p_eth_hdr, p_buf, buf_len, &desc );
		cl_perf_stop( &p_port->p_adapter->perf, BuildSendDesc );
		if( status != NDIS_STATUS_SUCCESS )
		{
			cl_perf_start( ProcessFailedSends );
			__process_failed_send( p_port, &desc, status );
			cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
			continue;
		}

		/* Post the WR. */
		cl_perf_start( PostSend );
		ib_status = p_port->p_adapter->p_ifc->post_send( p_port->ib_mgr.h_qp, &desc.wr, NULL );
		cl_perf_stop( &p_port->p_adapter->perf, PostSend );
		if( ib_status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ib_post_send returned %s\n", 
				p_port->p_adapter->p_ifc->get_err_str( ib_status )) );
			cl_perf_start( ProcessFailedSends );
			__process_failed_send( p_port, &desc, NDIS_STATUS_FAILURE );
			cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
			/* Flag the adapter as hung since posting is busted. */
			p_port->p_adapter->hung = TRUE;
			continue;
		}

		cl_atomic_inc( &p_port->send_mgr.depth );
	}
	cl_spinlock_release( &p_port->send_lock );

	IPOIB_EXIT( IPOIB_DBG_SEND );
}


static void
__send_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	ipoib_port_t		*p_port;
	ib_api_status_t		status;
	ib_wc_t				wc[MAX_SEND_WC], *p_wc, *p_free;
	cl_qlist_t			done_list;
	NDIS_PACKET			*p_packet;
	uint32_t			length;
	ipoib_endpt_t		*p_endpt;
	send_buf_t			*p_send_buf;
	ip_stat_sel_t		type;
	size_t				i;
	PERF_DECLARE( SendCompBundle );
	PERF_DECLARE( SendCb );
	PERF_DECLARE( PollSend );
	PERF_DECLARE( SendComp );
	PERF_DECLARE( FreeSendBuf );
	PERF_DECLARE( RearmSend );
	PERF_DECLARE( PortResume );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	cl_perf_clr( SendCompBundle );

	cl_perf_start( SendCb );

	UNUSED_PARAM( h_cq );

	cl_qlist_init( &done_list );

	p_port = (ipoib_port_t*)cq_context;

	ipoib_port_ref( p_port, ref_send_cb );

	for( i = 0; i < MAX_SEND_WC; i++ )
		wc[i].p_next = &wc[i + 1];
	wc[MAX_SEND_WC - 1].p_next = NULL;

	do
	{
		p_free = wc;
		cl_perf_start( PollSend );
		status = p_port->p_adapter->p_ifc->poll_cq( p_port->ib_mgr.h_send_cq, &p_free, &p_wc );
		cl_perf_stop( &p_port->p_adapter->perf, PollSend );
		CL_ASSERT( status == IB_SUCCESS || status == IB_NOT_FOUND );

		while( p_wc )
		{
			cl_perf_start( SendComp );
			CL_ASSERT( p_wc->status != IB_WCS_SUCCESS 
									|| p_wc->wc_type == IB_WC_SEND
									|| p_wc->wc_type == IB_WC_LSO);
			p_packet = (NDIS_PACKET*)(uintn_t)p_wc->wr_id;
			CL_ASSERT( p_packet );
			CL_ASSERT( IPOIB_PORT_FROM_PACKET( p_packet ) == p_port );

			p_endpt = IPOIB_ENDPT_FROM_PACKET( p_packet );
			p_send_buf = IPOIB_SEND_FROM_PACKET( p_packet );
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
					{
						type = IP_STAT_MCAST_BYTES;
					}
				}
				else
				{
					type = IP_STAT_UCAST_BYTES;
				}
				NdisQueryPacketLength( p_packet, &length );
				ipoib_inc_send_stat( p_port->p_adapter, type, length );
				NdisMSendComplete( p_port->p_adapter->h_adapter,
					p_packet, NDIS_STATUS_SUCCESS );
				break;

			case IB_WCS_WR_FLUSHED_ERR:
				IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
					("Flushed send completion.\n") );
				ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_DROPPED, 0 );
				NdisMSendCompleteX( p_port->p_adapter->h_adapter,
					p_packet, NDIS_STATUS_RESET_IN_PROGRESS );
				break;

			default:
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Send failed with %s (vendor specific %#x)\n",
					p_port->p_adapter->p_ifc->get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific) );
				ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_ERROR, 0 );
				NdisMSendCompleteX( p_port->p_adapter->h_adapter,
					p_packet, NDIS_STATUS_FAILURE );
				break;
			}
			cl_perf_stop( &p_port->p_adapter->perf, SendComp );
			/* Dereference the enpoint used for the transfer. */
			ipoib_endpt_deref( p_endpt );

			if( p_send_buf )
			{
				cl_perf_start( FreeSendBuf );
				ExFreeToNPagedLookasideList( &p_port->buf_mgr.send_buf_list,
					p_send_buf );
				cl_perf_stop( &p_port->p_adapter->perf, FreeSendBuf );
			}

			cl_atomic_dec( &p_port->send_mgr.depth );

			p_wc = p_wc->p_next;
			cl_perf_inc( SendCompBundle );
		}
		/* If we didn't use up every WC, break out. */
	} while( !p_free );

	/* Rearm the CQ. */
	cl_perf_start( RearmSend );
	status = p_port->p_adapter->p_ifc->rearm_cq( p_port->ib_mgr.h_send_cq, FALSE );
	cl_perf_stop( &p_port->p_adapter->perf, RearmSend );
	CL_ASSERT( status == IB_SUCCESS );

	/* Resume any sends awaiting resources. */
	cl_perf_start( PortResume );
	ipoib_port_resume( p_port );
	cl_perf_stop( &p_port->p_adapter->perf, PortResume );
	
	ipoib_port_deref( p_port, ref_send_cb );

	cl_perf_stop( &p_port->p_adapter->perf, SendCb );
	cl_perf_update_ctr( &p_port->p_adapter->perf, SendCompBundle );

	IPOIB_EXIT( IPOIB_DBG_SEND );
}


/******************************************************************************
*
* Endpoint manager implementation
*
******************************************************************************/
static void
__endpt_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );
	cl_qmap_init( &p_port->endpt_mgr.mac_endpts );
	cl_qmap_init( &p_port->endpt_mgr.lid_endpts );
	cl_fmap_init( &p_port->endpt_mgr.gid_endpts, __gid_cmp );
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__endpt_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );
	UNUSED_PARAM( p_port );
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


static void
__endpt_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );
	CL_ASSERT( cl_is_qmap_empty( &p_port->endpt_mgr.mac_endpts ) );
	CL_ASSERT( cl_is_qmap_empty( &p_port->endpt_mgr.lid_endpts ) );
	CL_ASSERT( cl_is_fmap_empty( &p_port->endpt_mgr.gid_endpts ) );
	UNUSED_PARAM( p_port );
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__endpt_mgr_remove_all(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_ENDPT );
	
	cl_obj_lock( &p_port->obj );
	/* Wait for all readers to complete. */
	while( p_port->endpt_rdr )
		;
	/*
	 * We don't need to initiate destruction - this is called only
	 * from the __port_destroying function, and destruction cascades
	 * to all child objects.  Just clear all the maps.
	 */
	cl_qmap_remove_all( &p_port->endpt_mgr.mac_endpts );
	cl_qmap_remove_all( &p_port->endpt_mgr.lid_endpts );
	cl_fmap_remove_all( &p_port->endpt_mgr.gid_endpts );
	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


static void
__endpt_mgr_reset_all(
	IN				ipoib_port_t* const			p_port )
{
	cl_map_item_t	*p_item;
	ipoib_endpt_t		*p_endpt;
	cl_qlist_t			mc_list;
	uint32_t			local_exist = 0;


	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	cl_qlist_init( &mc_list );
	
	cl_obj_lock( &p_port->obj );
	/* Wait for all readers to complete. */
	while( p_port->endpt_rdr )
		;

#if 0
			__endpt_mgr_remove_all(p_port);
#else
	
		NdisMIndicateStatus( p_port->p_adapter->h_adapter,
						NDIS_STATUS_MEDIA_DISCONNECT, NULL, 0 );
		NdisMIndicateStatusComplete( p_port->p_adapter->h_adapter );
		
		NdisMIndicateStatus( p_port->p_adapter->h_adapter,
						NDIS_STATUS_MEDIA_CONNECT, NULL, 0 );
		NdisMIndicateStatusComplete( p_port->p_adapter->h_adapter );
		
				//	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
					//	("Link DOWN!\n") );

	if( p_port->p_local_endpt )
	{
		cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts,
			&p_port->p_local_endpt->gid_item );
		cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts,
			&p_port->p_local_endpt->mac_item );
		if( p_port->p_local_endpt->dlid ) {
			cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts,
				&p_port->p_local_endpt->lid_item );
			p_port->p_local_endpt->dlid = 0;
		}
		
		cl_qlist_insert_head(
			&mc_list, &p_port->p_local_endpt->mac_item.pool_item.list_item );
		local_exist = 1;

		p_port->p_local_endpt = NULL;
	}

	p_item = cl_qmap_head( &p_port->endpt_mgr.mac_endpts );
	while( p_item != cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
		p_item = cl_qmap_next( p_item );
		if( p_endpt->h_mcast )
		{
			/*
			 * We destroy MC endpoints since they will get recreated
			 * when the port comes back up and we rejoin the MC groups.
			 */
			cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts,
				&p_endpt->mac_item );
			cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts,
				&p_endpt->gid_item );

			cl_qlist_insert_tail(
				&mc_list, &p_endpt->mac_item.pool_item.list_item );
		}
		else if( p_endpt->h_av )
		{
			/* Destroy the AV for all other endpoints. */
			p_port->p_adapter->p_ifc->destroy_av( p_endpt->h_av );
			p_endpt->h_av = NULL;
		}
		
		if( p_endpt->dlid )
		{
			cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts,
				&p_endpt->lid_item );
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
				("<__endptr_mgr_reset_all: setting p_endpt->dlid to 0\n"));
			p_endpt->dlid = 0;
		}
		
	}
#endif
	cl_obj_unlock( &p_port->obj );


	if(cl_qlist_count( &mc_list ) - local_exist)
	{
		p_port->mcast_cnt =  (uint32_t)cl_qlist_count( &mc_list ) - local_exist;
	}
	else
	{
		p_port->mcast_cnt = 0;
		KeSetEvent( &p_port->leave_mcast_event, EVENT_INCREMENT, FALSE );
	}	

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,("p_port->mcast_cnt = %d\n", p_port->mcast_cnt - local_exist));

	/* Destroy all multicast endpoints now that we have released the lock. */
	while( cl_qlist_count( &mc_list ) )
	{
		cl_list_item_t	*p_item;
		p_item = cl_qlist_remove_head( &mc_list );
		p_endpt = PARENT_STRUCT(p_item, ipoib_endpt_t, mac_item.pool_item.list_item);
		cl_obj_destroy( &p_endpt->obj);
	}

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


/*
 * Called when updating an endpoint entry in response to an ARP.
 * Because receive processing is serialized, and holds a reference
 * on the endpoint reader, we wait for all *other* readers to exit before
 * removing the item.
 */
static void
__endpt_mgr_remove(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_endpt_t* const		p_endpt )
{
	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	/* This function must be called from the receive path */
	CL_ASSERT(p_port->endpt_rdr > 0);

	cl_obj_lock( &p_port->obj );
	/* Wait for all readers to complete. */    
	while( p_port->endpt_rdr > 1 )
		;

	/* Remove the endpoint from the maps so further requests don't find it. */
	cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts, &p_endpt->mac_item );
	/*
	 * The enpoints are *ALWAYS* in both the MAC and GID maps.  They are only
	 * in the LID map if the GID has the same subnet prefix as us.
	 */
	cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts, &p_endpt->gid_item );

	if( p_endpt->dlid )
	{
		cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts,
			&p_endpt->lid_item );
	}

	cl_obj_unlock( &p_port->obj );

	cl_obj_destroy( &p_endpt->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


NTSTATUS
ipoib_mac_to_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_gid_t*					p_gid )
{
	ipoib_endpt_t*	p_endpt;
	cl_map_item_t	*p_item;
	uint64_t		key = 0;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	cl_memcpy( &key, &mac, sizeof(mac_addr_t) );

	cl_obj_lock( &p_port->obj );

	p_item = cl_qmap_get( &p_port->endpt_mgr.mac_endpts, key );
	if( p_item == cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed endpoint lookup.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
	*p_gid = p_endpt->dgid;

	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return STATUS_SUCCESS;
}


NTSTATUS
ipoib_mac_to_path(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_path_rec_t*				p_path )
{
	ipoib_endpt_t*	p_endpt;
	cl_map_item_t	*p_item;
	uint64_t		key = 0;
	uint8_t			sl;
	net32_t			flow_lbl;
	uint8_t			hop_limit;
	uint8_t			pkt_life;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	cl_memcpy( &key, &mac, sizeof(mac_addr_t) );

	cl_obj_lock( &p_port->obj );

	if( p_port->p_local_endpt == NULL )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("No local endpoint.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	if( mac.addr[0] == 0 && mac.addr[1] == 0 && mac.addr[2] == 0 &&
		mac.addr[3] == 0 && mac.addr[4] == 0 && mac.addr[5] == 0 )
	{
		p_endpt = p_port->p_local_endpt;
	}
	else
	{
		p_item = cl_qmap_get( &p_port->endpt_mgr.mac_endpts, key );
		if( p_item == cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
		{
			cl_obj_unlock( &p_port->obj );
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed endpoint lookup.\n") );
			return STATUS_INVALID_PARAMETER;
		}

		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
	}

	p_path->service_id = 0;
	p_path->dgid = p_endpt->dgid;
	p_path->sgid = p_port->p_local_endpt->dgid;
	p_path->dlid = p_endpt->dlid;
	p_path->slid = p_port->p_local_endpt->dlid;

	ib_member_get_sl_flow_hop(
		p_port->ib_mgr.bcast_rec.sl_flow_hop,
		&sl,
		&flow_lbl,
		&hop_limit
		);

	if( p_path->slid == p_path->dlid )
		pkt_life = 0;
	else
		pkt_life = p_port->ib_mgr.bcast_rec.pkt_life;

	ib_path_rec_init_local(
		p_path,
		&p_endpt->dgid,
		&p_port->p_local_endpt->dgid,
		p_endpt->dlid,
		p_port->p_local_endpt->dlid,
		1,
		p_port->ib_mgr.bcast_rec.pkey,
		sl, 0,
		IB_PATH_SELECTOR_EXACTLY, p_port->ib_mgr.bcast_rec.mtu,
		IB_PATH_SELECTOR_EXACTLY, p_port->ib_mgr.bcast_rec.rate,
		IB_PATH_SELECTOR_EXACTLY, pkt_life,
		0 );

	/* Set global routing information. */
	ib_path_rec_set_hop_flow_raw( p_path, hop_limit, flow_lbl, FALSE );
	p_path->tclass = p_port->ib_mgr.bcast_rec.tclass;

	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return STATUS_SUCCESS;
}


static inline NDIS_STATUS
__endpt_mgr_ref(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ipoib_endpt_t** const		pp_endpt )
{
	NDIS_STATUS		status;
	cl_map_item_t	*p_item;
	uint64_t		key;

	PERF_DECLARE( EndptQueue );

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	if( !cl_memcmp( &mac, &p_port->p_adapter->params.conf_mac, sizeof(mac) ) )
	{
		/* Discard loopback traffic. */
		IPOIB_PRINT(TRACE_LEVEL_WARNING, IPOIB_DBG_ENDPT,
			("Discarding loopback traffic\n") );
		IPOIB_EXIT( IPOIB_DBG_ENDPT );
		return NDIS_STATUS_NO_ROUTE_TO_DESTINATION;
	}

	key = 0;
	cl_memcpy( &key, &mac, sizeof(mac_addr_t) );

	cl_obj_lock( &p_port->obj );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
		("Look for :\t  MAC: %02X-%02X-%02X-%02X-%02X-%02X\n",
		mac.addr[0], mac.addr[1], mac.addr[2],
		mac.addr[3], mac.addr[4], mac.addr[5]) );

	p_item = cl_qmap_get( &p_port->endpt_mgr.mac_endpts, key );
	if( p_item == cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("Failed endpoint lookup.\n") );
		return NDIS_STATUS_NO_ROUTE_TO_DESTINATION;
	}

	*pp_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
	ipoib_endpt_ref( *pp_endpt );

	cl_obj_unlock( &p_port->obj );

	cl_perf_start( EndptQueue );
	status = ipoib_endpt_queue( *pp_endpt );
	cl_perf_stop( &p_port->p_adapter->perf, EndptQueue );
	if( status != NDIS_STATUS_SUCCESS )
		*pp_endpt = NULL;

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return status;
}


static inline NDIS_STATUS
__endpt_mgr_get_gid_qpn(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_gid_t* const				p_gid,
		OUT			UNALIGNED net32_t* const	p_qpn )
{
	UNALIGNED
	cl_map_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;
	uint64_t		key;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	cl_obj_lock( &p_port->obj );

	key = 0;
	cl_memcpy( &key, &mac, sizeof(mac_addr_t) );
	p_item = cl_qmap_get( &p_port->endpt_mgr.mac_endpts, key );
	if( p_item == cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("Failed endpoint lookup.\n") );
		return NDIS_STATUS_FAILURE;
	}

	p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );

	*p_gid = p_endpt->dgid;
	*p_qpn = p_endpt->qpn;

	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return NDIS_STATUS_SUCCESS;
}


static inline ipoib_endpt_t*
__endpt_mgr_get_by_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_gid_t* const				p_gid )
{
	cl_fmap_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_item = cl_fmap_get( &p_port->endpt_mgr.gid_endpts, p_gid );
	if( p_item == cl_fmap_end( &p_port->endpt_mgr.gid_endpts ) )
		p_endpt = NULL;
	else
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, gid_item );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return p_endpt;
}


static ipoib_endpt_t*
__endpt_mgr_get_by_lid(
	IN				ipoib_port_t* const			p_port,
	IN		const	net16_t						lid )
{
	cl_map_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_item = cl_qmap_get( &p_port->endpt_mgr.lid_endpts, lid );
	if( p_item == cl_qmap_end( &p_port->endpt_mgr.lid_endpts ) )
		p_endpt = NULL;
	else
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, lid_item );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return p_endpt;
}


inline ib_api_status_t
__endpt_mgr_insert_locked(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
	IN				ipoib_endpt_t* const		p_endpt )
{
	ib_api_status_t	status;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
		("insert  :\t  MAC: %02X-%02X-%02X-%02X-%02X-%02X\n",
		mac.addr[0], mac.addr[1], mac.addr[2],
		mac.addr[3], mac.addr[4], mac.addr[5]) );

	cl_obj_lock( &p_port->obj );
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}
	/* __endpt_mgr_insert expects *one* reference to be held when being called. */
	cl_atomic_inc( &p_port->endpt_rdr );
	status= __endpt_mgr_insert( p_port, mac, p_endpt );
	cl_atomic_dec( &p_port->endpt_rdr );
	cl_obj_unlock( &p_port->obj );

	return status;
}


inline ib_api_status_t
__endpt_mgr_insert(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
	IN				ipoib_endpt_t* const		p_endpt )
{
	uint64_t		key;
	cl_status_t		cl_status;
	cl_map_item_t	*p_qitem;
	cl_fmap_item_t	*p_fitem;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	/* Wait for all accesses to the map to complete. */
	while( p_port->endpt_rdr > 1 )
		;

	/* Link the endpoint to the port. */
	cl_status = cl_obj_insert_rel_parent_locked(
		&p_endpt->rel, &p_port->obj, &p_endpt->obj );

	if( cl_status != CL_SUCCESS )
	{
		cl_obj_destroy( &p_endpt->obj );
		return IB_INVALID_STATE;
	}

#if DBG
	cl_atomic_inc( &p_port->ref[ref_endpt_track] );
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
		("ref  type %d ref_cnt %d\n", ref_endpt_track, p_port->obj.ref_cnt) );
#endif

	p_endpt->mac = mac;
	key = 0;
	cl_memcpy( &key, &mac, sizeof(mac_addr_t) );
	p_qitem = cl_qmap_insert(
		&p_port->endpt_mgr.mac_endpts, key, &p_endpt->mac_item );
	CL_ASSERT( p_qitem == &p_endpt->mac_item );
	p_fitem = cl_fmap_insert(
		&p_port->endpt_mgr.gid_endpts, &p_endpt->dgid, &p_endpt->gid_item );
	CL_ASSERT( p_fitem == &p_endpt->gid_item );
	if( p_endpt->dlid )
	{
		p_qitem = cl_qmap_insert(
			&p_port->endpt_mgr.lid_endpts, p_endpt->dlid, &p_endpt->lid_item );
		CL_ASSERT( p_qitem == &p_endpt->lid_item );
		if (p_qitem != &p_endpt->lid_item) {
			// Since we failed to insert into the list, make sure it is not removed
			p_endpt->dlid =0;
		}
	}

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return IB_SUCCESS;
}


static ib_api_status_t
__endpt_mgr_add_bcast(
	IN				ipoib_port_t* const			p_port,
	IN				ib_mcast_rec_t				*p_mcast_rec )
{
	ib_api_status_t	status;
	ipoib_endpt_t	*p_endpt;
	mac_addr_t		bcast_mac;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/*
	 * Cache the broadcast group properties for creating future mcast groups.
	 */
	p_port->ib_mgr.bcast_rec = *p_mcast_rec->p_member_rec;

	/* Allocate the broadcast endpoint. */
	p_endpt = ipoib_endpt_create( &p_mcast_rec->p_member_rec->mgid,
		0 , CL_HTON32(0x00FFFFFF) );
	if( !p_endpt )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_endpt_create failed.\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}
	/* set reference to transport to be used while is not attached to the port */
	p_endpt->is_mcast_listener = TRUE;
	p_endpt->p_ifc = p_port->p_adapter->p_ifc;
	status = ipoib_endpt_set_mcast( p_endpt, p_port->ib_mgr.h_pd,
		p_port->port_num, p_mcast_rec );
	if( status != IB_SUCCESS )
	{
		cl_obj_destroy( &p_endpt->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_create_mcast_endpt returned %s\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Add the broadcast endpoint to the endpoint map. */
	cl_memset( &bcast_mac, 0xFF, sizeof(bcast_mac) );
	status = __endpt_mgr_insert_locked( p_port, bcast_mac, p_endpt );

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


void
ipoib_port_remove_endpt(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac )
{
	cl_map_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;
	uint64_t		key;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	key = 0;
	cl_memcpy( &key, &mac, sizeof(mac_addr_t) );

	/* Remove the endpoint from the maps so further requests don't find it. */
	cl_obj_lock( &p_port->obj );
	/* Wait for all readers to finish */
	while( p_port->endpt_rdr )
		;
	p_item = cl_qmap_remove( &p_port->endpt_mgr.mac_endpts, key );
	/*
	 * Dereference the endpoint.  If the ref count goes to zero, it
	 * will get freed.
	 */
	if( p_item != cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
		/*
		 * The enpoints are *ALWAYS* in both the MAC and GID maps.  They are only
		 * in the LID map if the GID has the same subnet prefix as us.
		 */
		cl_fmap_remove_item(
			&p_port->endpt_mgr.gid_endpts, &p_endpt->gid_item );

		if( p_endpt->dlid )
		{
			cl_qmap_remove_item(
				&p_port->endpt_mgr.lid_endpts, &p_endpt->lid_item );
			p_endpt->dlid = 0;
		}

		cl_obj_unlock( &p_port->obj );
		cl_obj_destroy( &p_endpt->obj );
#if DBG
		cl_atomic_dec( &p_port->ref[ref_endpt_track] );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("ref type %d ref_cnt %d\n", ref_endpt_track, p_port->obj.ref_cnt) );
#endif

	}
	else
	{
		cl_obj_unlock( &p_port->obj );
	}

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}

/*
 * The sequence for port up is as follows:
 *	1. The port goes active.  This allows the adapter to send SA queries
 *	and join the broadcast group (and other groups).
 *
 *	2. The adapter sends an SA query for the broadcast group.
 *
 *	3. Upon completion of the query, the adapter joins the broadcast group.
 */


/*
 * Query the SA for the broadcast group.
 */
void
ipoib_port_up(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_pnp_port_rec_t* const	p_pnp_rec )
{
	ib_port_info_t		*p_port_info;
	ib_mad_t			*mad_in = NULL;
	ib_mad_t			*mad_out = NULL;
	ib_api_status_t		status = IB_INSUFFICIENT_MEMORY;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	cl_obj_lock( &p_port->obj );
	if ( p_port->state == IB_QPS_INIT ) 
	{
		cl_obj_unlock( &p_port->obj );
		status = IB_SUCCESS;
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("p_port->state = %d - Aborting.\n", p_port->state) );        
		goto up_done;
	}
	else if ( p_port->state == IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->p_adapter->obj );
		if( p_port->p_adapter->state == IB_PNP_PORT_INIT )
		{
			p_port->p_adapter->state = IB_PNP_PORT_ACTIVE;
		}
		cl_obj_unlock( &p_port->p_adapter->obj );
		status = IB_SUCCESS;
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Port init is done. p_port->state = %d.\n", p_port->state ) );
		goto up_done;
	}
	p_port->state = IB_QPS_INIT;
	cl_obj_unlock( &p_port->obj );  


	/* Wait for all work requests to get flushed. */
	while( p_port->recv_mgr.depth || p_port->send_mgr.depth )
		cl_thread_suspend( 0 );

	KeResetEvent( &p_port->sa_event );

	mad_out = (ib_mad_t*)cl_zalloc(256);
	if(! mad_out)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("failed to allocate mad mad_out\n")); 
		goto up_done;
	}
	mad_in = (ib_mad_t*)cl_zalloc(256);
	if(! mad_in)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("failed to allocate mad mad_in\n")); 
		goto up_done;
	}

	mad_in->attr_id = IB_MAD_ATTR_PORT_INFO;
	mad_in->method = IB_MAD_METHOD_GET;
	mad_in->base_ver = 1;
	mad_in->class_ver =1;
	mad_in->mgmt_class = IB_MCLASS_SUBN_LID;
	
	status = p_port->p_adapter->p_ifc->local_mad(
		p_port->ib_mgr.h_ca ,p_port->port_num ,mad_in ,mad_out);

	if( status != IB_SUCCESS )
	{
		ipoib_set_inactive( p_port->p_adapter );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_local_mad returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		goto up_done;
	}

	p_port_info = (ib_port_info_t*)(((ib_smp_t*)mad_out)->data);
	p_port->base_lid = p_pnp_rec->p_port_attr->lid;
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Received port info: link width = %d.\n",
			p_port_info->link_width_active) );
	p_port->ib_mgr.rate =
		ib_port_info_compute_rate( p_port_info );
	
	ipoib_set_rate( p_port->p_adapter,
		p_port_info->link_width_active,
	ib_port_info_get_link_speed_active( p_port_info ) );

	status = __port_get_bcast( p_port );
	if (status != IB_SUCCESS)
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
		(" __port_get_bcast returned %s\n",p_port->p_adapter->p_ifc->get_err_str( status )));

up_done:
	if( status != IB_SUCCESS )
	{
		if( status != IB_CANCELED )
		{
			ipoib_set_inactive( p_port->p_adapter );
			__endpt_mgr_reset_all( p_port );
		}
		ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
		p_port->state = IB_QPS_ERROR;
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
	}

	if(mad_out)
		cl_free(mad_out);
	if(mad_in)
		cl_free(mad_in);

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__endpt_mgr_add_local(
	IN				ipoib_port_t* const			p_port,
	IN				ib_port_info_t* const		p_port_info )
{
	ib_api_status_t			status;
	ib_gid_t				gid;
	ipoib_endpt_t			*p_endpt;
	ib_av_attr_t			av_attr;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	ib_gid_set_default( &gid, p_port->p_adapter->guids.port_guid.guid );
	p_endpt = ipoib_endpt_create(
		&gid, p_port_info->base_lid, p_port->ib_mgr.qpn );
	if( !p_endpt )
	{
		p_port->p_adapter->hung = TRUE;
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to create local endpt\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	cl_memclr( &av_attr, sizeof(ib_av_attr_t) );
	av_attr.port_num = p_port->port_num;
	av_attr.sl = 0;
	IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
		("<__endpt_mgr_add_local>:  av_attr.dlid = p_port_info->base_lid = %d\n",p_port_info->base_lid));
	av_attr.dlid = p_port_info->base_lid;
	av_attr.static_rate = p_port->ib_mgr.rate;
	av_attr.path_bits = 0;
	status = p_port->p_adapter->p_ifc->create_av(
		p_port->ib_mgr.h_pd, &av_attr, &p_endpt->h_av );
	if( status != IB_SUCCESS )
	{
		cl_obj_destroy( &p_endpt->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_av for local endpoint returned %s\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* __endpt_mgr_insert expects *one* reference to be held. */
	cl_atomic_inc( &p_port->endpt_rdr );
	status = __endpt_mgr_insert( p_port, p_port->p_adapter->params.conf_mac, p_endpt );
	cl_atomic_dec( &p_port->endpt_rdr );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__endpt_mgr_insert for local endpoint returned %s\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	p_port->p_local_endpt = p_endpt;

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}




static ib_api_status_t
__port_get_bcast(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		status;
	ib_query_req_t		query;
	ib_user_query_t		info;
	ib_member_rec_t		member_rec;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	info.method = IB_MAD_METHOD_GETTABLE;
	info.attr_id = IB_MAD_ATTR_MCMEMBER_RECORD;
	info.attr_size = sizeof(ib_member_rec_t);
	info.comp_mask = IB_MCR_COMPMASK_MGID;
	info.p_attr = &member_rec;

	/* Query requires only the MGID. */
	cl_memclr( &member_rec, sizeof(ib_member_rec_t) );
	member_rec.mgid = bcast_mgid_template;

    member_rec.mgid.raw[4] = (uint8_t) (p_port->p_adapter->guids.port_guid.pkey >> 8) ;
	member_rec.mgid.raw[5] = (uint8_t) p_port->p_adapter->guids.port_guid.pkey;
	member_rec.pkey = cl_hton16(p_port->p_adapter->guids.port_guid.pkey);
	cl_memclr( &query, sizeof(ib_query_req_t) );
	query.query_type = IB_QUERY_USER_DEFINED;
	query.p_query_input = &info;
	query.port_guid = p_port->p_adapter->guids.port_guid.guid;
	query.timeout_ms = p_port->p_adapter->params.sa_timeout;
	query.retry_cnt = p_port->p_adapter->params.sa_retry_cnt;
	query.query_context = p_port;
	query.pfn_query_cb = __bcast_get_cb;

	/* reference the object for the multicast query. */
	ipoib_port_ref( p_port, ref_get_bcast );

	status = p_port->p_adapter->p_ifc->query(
		p_port->p_adapter->h_al, &query, &p_port->ib_mgr.h_query );
	if( status != IB_SUCCESS )
	{
		ipoib_port_deref( p_port, ref_get_bcast );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_query returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


/* Callback for the MCMemberRecord Get query for the IPv4 broadcast group. */
static void
__bcast_get_cb(
	IN				ib_query_rec_t				*p_query_rec )
{
	ipoib_port_t		*p_port;
	ib_member_rec_t		*p_mc_req;
	ib_api_status_t		status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_port = (ipoib_port_t*)p_query_rec->query_context;

	cl_obj_lock( &p_port->obj );
	p_port->ib_mgr.h_query = NULL;

	CL_ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
	if( p_port->state != IB_QPS_INIT )
	{
		status = IB_CANCELED;
		goto done;
	}
	
	status = p_query_rec->status;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
		("status of request %s\n", 
		p_port->p_adapter->p_ifc->get_err_str( status )) );

	switch( status )
	{
	case IB_SUCCESS:
		if( p_query_rec->result_cnt )
		{
			p_mc_req = (ib_member_rec_t*)
				ib_get_query_result( p_query_rec->p_result_mad, 0 );

			/* Join the broadcast group. */
			status = __port_join_bcast( p_port, p_mc_req );
			break;
		}
		/* Fall through. */

	case IB_REMOTE_ERROR:
		/* SA failed the query.  Broadcast group doesn't exist, create it. */
		status = __port_create_bcast( p_port );
		break;

	case IB_CANCELED:
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Instance destroying - Aborting.\n") );
		break;

	default:
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_BCAST_GET, 1, p_query_rec->status );
	}
done:
	cl_obj_unlock( &p_port->obj );

	if( status != IB_SUCCESS )
	{
		if( status != IB_CANCELED )
		{
			ipoib_set_inactive( p_port->p_adapter );
			__endpt_mgr_reset_all( p_port );
		}
		p_port->state = IB_QPS_ERROR;
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
	}

	/* Return the response MAD to AL. */
	if( p_query_rec->p_result_mad )
		p_port->p_adapter->p_ifc->put_mad( p_query_rec->p_result_mad );

	/* Release the reference taken when issuing the member record query. */
	ipoib_port_deref( p_port, ref_bcast_get_cb );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__port_join_bcast(
	IN				ipoib_port_t* const			p_port,
	IN				ib_member_rec_t* const		p_member_rec )
{
	ib_api_status_t		status;
	ib_mcast_req_t		mcast_req;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Check that the rate is realizable for our port. */
	if( p_port->ib_mgr.rate < (p_member_rec->rate & 0x3F) &&
		(g_ipoib.bypass_check_bcast_rate == 0))
	{
		/*
		 * The MC group rate is higher than our port's rate.  Log an error
		 * and stop.  A port transition will drive the retry.
		 */
		IPOIB_PRINT(TRACE_LEVEL_WARNING, IPOIB_DBG_INIT,
			("Unrealizable join due to rate mismatch.\n") );
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_BCAST_RATE, 2,
			(uint32_t)(p_member_rec->rate & 0x3F),
			(uint32_t)p_port->ib_mgr.rate );
		return IB_ERROR;
	}

	/* Join the broadcast group. */
	cl_memclr( &mcast_req, sizeof(mcast_req) );
	/* Copy the results of the Get to use as parameters. */
	mcast_req.member_rec = *p_member_rec;
	/* We specify our port GID for the join operation. */
	mcast_req.member_rec.port_gid.unicast.prefix = IB_DEFAULT_SUBNET_PREFIX;
	mcast_req.member_rec.port_gid.unicast.interface_id =
		p_port->p_adapter->guids.port_guid.guid;

	mcast_req.mcast_context = p_port;
	mcast_req.pfn_mcast_cb = __bcast_cb;
	mcast_req.timeout_ms = p_port->p_adapter->params.sa_timeout;
	mcast_req.retry_cnt = p_port->p_adapter->params.sa_retry_cnt;
	mcast_req.port_guid = p_port->p_adapter->guids.port_guid.guid;
	mcast_req.pkey_index = p_port->pkey_index;

	if( ib_member_get_state( mcast_req.member_rec.scope_state ) !=
		IB_MC_REC_STATE_FULL_MEMBER )
	{
		IPOIB_PRINT(TRACE_LEVEL_WARNING, IPOIB_DBG_INIT,
			("Incorrect MC member rec join state in query response.\n") );
		ib_member_set_state( &mcast_req.member_rec.scope_state,
			IB_MC_REC_STATE_FULL_MEMBER );
	}

	/* reference the object for the multicast join request. */
	ipoib_port_ref( p_port, ref_join_bcast );

	status = p_port->p_adapter->p_ifc->join_mcast(
		p_port->ib_mgr.h_qp, &mcast_req, NULL );
	if( status != IB_SUCCESS )
	{
		ipoib_port_deref( p_port, ref_bcast_join_failed );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_join_mcast returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
	}
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


static ib_api_status_t
__port_create_bcast(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		status;
	ib_mcast_req_t		mcast_req;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Join the broadcast group. */
	cl_memclr( &mcast_req, sizeof(mcast_req) );
	mcast_req.create = TRUE;
	/*
	 * Create requires pkey, qkey, SL, flow label, traffic class, joing state
	 * and port GID.
	 *
	 * We specify the MGID since we don't want the SA to generate it for us.
	 */
	mcast_req.member_rec.mgid = bcast_mgid_template;
	mcast_req.member_rec.mgid.raw[4] = (uint8_t) (p_port->p_adapter->guids.port_guid.pkey >> 8); 
	mcast_req.member_rec.mgid.raw[5] = (uint8_t) p_port->p_adapter->guids.port_guid.pkey;
	ib_gid_set_default( &mcast_req.member_rec.port_gid,
		p_port->p_adapter->guids.port_guid.guid );
	/*
	 * IPOIB spec requires that the QKEY have the MSb set so that the QKEY
	 * from the QP is used rather than the QKEY in the send WR.
	 */
	mcast_req.member_rec.qkey =
		(uint32_t)(uintn_t)p_port | IB_QP_PRIVILEGED_Q_KEY;
	mcast_req.member_rec.mtu =
		(IB_PATH_SELECTOR_EXACTLY << 6) | IB_MTU_LEN_2048;

	mcast_req.member_rec.pkey = cl_hton16(p_port->p_adapter->guids.port_guid.pkey);

	mcast_req.member_rec.sl_flow_hop = ib_member_set_sl_flow_hop( 0, 0, 0 );
	mcast_req.member_rec.scope_state =
		ib_member_set_scope_state( 2, IB_MC_REC_STATE_FULL_MEMBER );

	mcast_req.mcast_context = p_port;
	mcast_req.pfn_mcast_cb = __bcast_cb;
	mcast_req.timeout_ms = p_port->p_adapter->params.sa_timeout;
	mcast_req.retry_cnt = p_port->p_adapter->params.sa_retry_cnt;
	mcast_req.port_guid = p_port->p_adapter->guids.port_guid.guid;
	mcast_req.pkey_index = p_port->pkey_index;

	/* reference the object for the multicast join request. */
	ipoib_port_ref( p_port, ref_join_bcast );

	status = p_port->p_adapter->p_ifc->join_mcast( p_port->ib_mgr.h_qp, &mcast_req, NULL );
	if( status != IB_SUCCESS )
	{
		ipoib_port_deref( p_port, ref_bcast_create_failed );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_join_mcast returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
	}
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


void
ipoib_port_down(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		status;
	ib_qp_mod_t			qp_mod;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/*
	 * Mark our state.  This causes all callbacks to abort.
	 * Note that we hold the receive lock so that we synchronize
	 * with reposting.  We must take the receive lock before the
	 * object lock since that is the order taken when reposting.
	 */
	cl_spinlock_acquire( &p_port->recv_lock );
	cl_obj_lock( &p_port->obj );
	p_port->state = IB_QPS_ERROR;

	NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
		EVENT_IPOIB_PORT_DOWN, 0 );

	if( p_port->ib_mgr.h_query )
	{
		p_port->p_adapter->p_ifc->cancel_query(
			p_port->p_adapter->h_al, p_port->ib_mgr.h_query );
		p_port->ib_mgr.h_query = NULL;
	}
	cl_obj_unlock( &p_port->obj );
	cl_spinlock_release( &p_port->recv_lock );

	KeWaitForSingleObject(
		&p_port->sa_event, Executive, KernelMode, FALSE, NULL );

	/* garbage collector timer is not needed when link is down */
	KeCancelTimer(&p_port->gc_timer);
	KeFlushQueuedDpcs();

	/*
	 * Put the QP in the error state.  This removes the need to
	 * synchronize with send/receive callbacks.
	 */
	CL_ASSERT( p_port->ib_mgr.h_qp );
	cl_memclr( &qp_mod, sizeof(ib_qp_mod_t) );
	qp_mod.req_state = IB_QPS_ERROR;
	status = p_port->p_adapter->p_ifc->modify_qp( p_port->ib_mgr.h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_modify_qp to error state returned %s.\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		p_port->p_adapter->hung = TRUE;
		return;
	}

	KeResetEvent(&p_port->leave_mcast_event);

	/* Reset all endpoints so we don't flush our ARP cache. */
	__endpt_mgr_reset_all( p_port );

	KeWaitForSingleObject(
		&p_port->leave_mcast_event, Executive, KernelMode, FALSE, NULL );

	__pending_list_destroy(p_port);
	
	cl_obj_lock( &p_port->p_adapter->obj );
	ipoib_dereg_addrs( p_port->p_adapter );
	cl_obj_unlock( &p_port->p_adapter->obj );
	
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__bcast_cb(
	IN				ib_mcast_rec_t				*p_mcast_rec )
{
	ipoib_port_t	*p_port;
	ib_api_status_t	status;
	LARGE_INTEGER  	gc_due_time;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_port = (ipoib_port_t*)p_mcast_rec->mcast_context;

	cl_obj_lock( &p_port->obj );

	ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
	if( p_port->state != IB_QPS_INIT )
	{
		cl_obj_unlock( &p_port->obj );
		if( p_mcast_rec->status == IB_SUCCESS )
		{
			ipoib_port_ref(p_port, ref_leave_mcast);
			p_port->p_adapter->p_ifc->leave_mcast( p_mcast_rec->h_mcast, __leave_error_mcast_cb );
		}
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
		ipoib_port_deref( p_port, ref_bcast_inv_state );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Invalid state - Aborting.\n") );
		return;
	}
	status = p_mcast_rec->status;
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Multicast join for broadcast group returned %s.\n",
			p_port->p_adapter->p_ifc->get_err_str( p_mcast_rec->status )) );
		if( status == IB_REMOTE_ERROR )
		{
			/*
			 * Either:
			 *	- the join failed because the group no longer exists
			 *	- the create failed because the group already exists
			 *
			 * Kick off a new Get query to the SA to restart the join process
			 * from the top.  Note that as an optimization, it would be
			 * possible to distinguish between the join and the create.
			 * If the join fails, try the create.  If the create fails, start
			 * over with the Get.
			 */
			/* TODO: Assert is a place holder.  Can we ever get here if the
			state isn't IB_PNP_PORT_ADD or PORT_DOWN or PORT_INIT? */
			CL_ASSERT( p_port->p_adapter->state == IB_PNP_PORT_ADD ||
				p_port->p_adapter->state == IB_PNP_PORT_DOWN ||
				p_port->p_adapter->state == IB_PNP_PORT_INIT );
			if(++p_port->bc_join_retry_cnt < p_port->p_adapter->params.bc_join_retry)
			{
				status = __port_get_bcast( p_port );
			}
			else
			{
				NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
					EVENT_IPOIB_BCAST_JOIN, 1, p_mcast_rec->status );
				p_port->bc_join_retry_cnt = 0;
			}
		}
		else
		{
			NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
				EVENT_IPOIB_BCAST_JOIN, 1, p_mcast_rec->status );
		}

		cl_obj_unlock( &p_port->obj );
		if( status != IB_SUCCESS )
		{
			ipoib_set_inactive( p_port->p_adapter );
			__endpt_mgr_reset_all( p_port );
			ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
			p_port->state = IB_QPS_ERROR;
			KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
		}
		ipoib_port_deref( p_port, ref_bcast_req_failed );
		IPOIB_EXIT( IPOIB_DBG_INIT );
		return;
	}
	p_port->bc_join_retry_cnt = 0;

	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}

	if(! p_port->p_local_endpt)
	{
		ib_port_info_t	port_info;
		cl_memclr(&port_info, sizeof(port_info));
		port_info.base_lid = p_port->base_lid;
		status = __endpt_mgr_add_local( p_port, &port_info );
		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__endpt_mgr_add_local returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
				cl_obj_unlock( &p_port->obj );
			goto err;
		}
	}

	cl_obj_unlock( &p_port->obj );

	status = __endpt_mgr_add_bcast( p_port, p_mcast_rec );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__endpt_mgr_add_bcast returned %s\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		ipoib_port_ref(p_port, ref_leave_mcast);
		status = p_port->p_adapter->p_ifc->leave_mcast( p_mcast_rec->h_mcast, __leave_error_mcast_cb );
		CL_ASSERT( status == IB_SUCCESS );
		goto err;
	}

	/* Get the QP ready for action. */
	status = __ib_mgr_activate( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__ib_mgr_activate returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );

err:
		/* Flag the adapter as hung. */
		p_port->p_adapter->hung = TRUE;
		ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
		p_port->state = IB_QPS_ERROR;        
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
		ipoib_port_deref( p_port, ref_bcast_error );
		IPOIB_EXIT( IPOIB_DBG_INIT );
		return;
	}

	cl_obj_lock( &p_port->obj );
	/* Only change the state if we're still in INIT. */
	ASSERT( p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
	if (p_port->state == IB_QPS_INIT) {
		p_port->state = IB_QPS_RTS;
	}
	cl_obj_unlock( &p_port->obj );

	/* Prepost receives. */
	cl_spinlock_acquire( &p_port->recv_lock );
	__recv_mgr_repost( p_port );
	cl_spinlock_release( &p_port->recv_lock );

	/* Notify the adapter that we now have an active connection. */
	status = ipoib_set_active( p_port->p_adapter );
	if( status != IB_SUCCESS )
	{
		ib_qp_mod_t			qp_mod;
		ipoib_set_inactive( p_port->p_adapter );
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("ipoib_set_active returned %s.\n",p_port->p_adapter->p_ifc->get_err_str( status )));
		cl_spinlock_acquire( &p_port->recv_lock );
		cl_obj_lock( &p_port->obj );
		p_port->state = IB_QPS_ERROR;
		if( p_port->ib_mgr.h_query )
		{
			p_port->p_adapter->p_ifc->cancel_query(
				p_port->p_adapter->h_al, p_port->ib_mgr.h_query );
			p_port->ib_mgr.h_query = NULL;
		}
		cl_obj_unlock( &p_port->obj );
		cl_spinlock_release( &p_port->recv_lock );

		CL_ASSERT( p_port->ib_mgr.h_qp );
		cl_memclr( &qp_mod, sizeof(ib_qp_mod_t) );
		qp_mod.req_state = IB_QPS_ERROR;
		status = p_port->p_adapter->p_ifc->modify_qp( p_port->ib_mgr.h_qp, &qp_mod );
		__endpt_mgr_reset_all( p_port );

		ipoib_port_deref( p_port, ref_join_bcast );
		return;
	}

	/* garbage collector timer is needed when link is active */
	gc_due_time.QuadPart = -(int64_t)(((uint64_t)p_port->p_adapter->params.mc_leave_rescan * 2000000) * 10);
	KeSetTimerEx(&p_port->gc_timer,gc_due_time,
			    (LONG)p_port->p_adapter->params.mc_leave_rescan*1000,&p_port->gc_dpc);

	KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
	ipoib_port_deref( p_port, ref_join_bcast );
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__qp_event(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	UNUSED_PARAM( p_event_rec );
	CL_ASSERT( p_event_rec->context );
	((ipoib_port_t*)p_event_rec->context)->p_adapter->hung = TRUE;
}


static void
__cq_event(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	UNUSED_PARAM( p_event_rec );
	CL_ASSERT( p_event_rec->context );
	((ipoib_port_t*)p_event_rec->context)->p_adapter->hung = TRUE;
}


static ib_api_status_t
__ib_mgr_activate(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t	status;
	ib_dgrm_info_t	dgrm_info;
	ib_qp_mod_t		qp_mod;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	/*
	 * Move the QP to RESET.  This allows us to reclaim any
	 * unflushed receives.
	 */
	cl_memclr( &qp_mod, sizeof(ib_qp_mod_t) );
	qp_mod.req_state = IB_QPS_RESET;
	status = p_port->p_adapter->p_ifc->modify_qp( p_port->ib_mgr.h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_modify_qp returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Move the QP to RTS. */
	dgrm_info.port_guid = p_port->p_adapter->guids.port_guid.guid;
	dgrm_info.qkey = p_port->ib_mgr.bcast_rec.qkey;
	dgrm_info.pkey_index = p_port->pkey_index;
	status = p_port->p_adapter->p_ifc->init_dgrm_svc( p_port->ib_mgr.h_qp, &dgrm_info );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_init_dgrm_svc returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Rearm the CQs. */
	status = p_port->p_adapter->p_ifc->rearm_cq( p_port->ib_mgr.h_recv_cq, FALSE );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_rearm_cq for recv returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	status = p_port->p_adapter->p_ifc->rearm_cq( p_port->ib_mgr.h_send_cq, FALSE );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_rearm_cq for send returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


/* Transition to a passive level thread. */
ib_api_status_t
ipoib_port_join_mcast(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t				mac,
	IN		const	uint8_t					state)
{
	ib_api_status_t		status;
	ib_mcast_req_t		mcast_req;
	ipoib_endpt_t		*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	switch( __endpt_mgr_ref( p_port, mac, &p_endpt ) )
	{
	case NDIS_STATUS_NO_ROUTE_TO_DESTINATION:
		break;

	case NDIS_STATUS_SUCCESS:
		ipoib_endpt_deref( p_endpt );
		/* Fall through */

	case NDIS_STATUS_PENDING:
		return IB_SUCCESS;
	}

	/*
	 * Issue the mcast request, using the parameters of the broadcast group.
	 * This allows us to do a create request that should always succeed since
	 * the required parameters are known.
	 */
	cl_memclr( &mcast_req, sizeof(mcast_req) );
	mcast_req.create = TRUE;

	/* Copy the settings from the broadcast group. */
	mcast_req.member_rec = p_port->ib_mgr.bcast_rec;
	/* Clear fields that aren't specified in the join */
	mcast_req.member_rec.mlid = 0;
	ib_member_set_state( &mcast_req.member_rec.scope_state,state);

	if( (mac.addr[0] == 1) && (mac.addr[2] == 0x5E ))
	{
		/*
		 * Update the address portion of the MGID with the 28 lower bits of the
		 * IP address.  Since we're given a MAC address, we are using 
		 * 24 lower bits of that network-byte-ordered value (assuming MSb
		 * is zero) and 4 lsb bits of the first byte of IP address.
		 */
		mcast_req.member_rec.mgid.raw[12] = mac.addr[1];
		mcast_req.member_rec.mgid.raw[13] = mac.addr[3];
		mcast_req.member_rec.mgid.raw[14] = mac.addr[4];
		mcast_req.member_rec.mgid.raw[15] = mac.addr[5];
	}
	else
	{
		/* Handle non IP mutlicast MAC addresses. */
		/* Update the signature to use the lower 2 bytes of the OpenIB OUI. */
		mcast_req.member_rec.mgid.raw[2] = 0x14;
		mcast_req.member_rec.mgid.raw[3] = 0x05;
		/* Now copy the MAC address into the last 6 bytes of the GID. */
		cl_memcpy( &mcast_req.member_rec.mgid.raw[10], mac.addr, 6 );
	}

	mcast_req.mcast_context = p_port;
	mcast_req.pfn_mcast_cb = __mcast_cb;
	mcast_req.timeout_ms = p_port->p_adapter->params.sa_timeout;
	mcast_req.retry_cnt = p_port->p_adapter->params.sa_retry_cnt;
	mcast_req.port_guid = p_port->p_adapter->guids.port_guid.guid;
	mcast_req.pkey_index = p_port->pkey_index;
	mcast_req.member_rec.pkey = cl_hton16(p_port->p_adapter->guids.port_guid.pkey);
	/*
	 * Create the endpoint and insert it in the port.  Since we don't wait for
	 * the mcast SA operations to complete before returning from the multicast
	 * list set OID asynchronously, it is possible for the mcast entry to be
	 * cleared before the SA interaction completes.  In this case, when the
	 * mcast callback is invoked, it would not find the corresponding endpoint
	 * and would be undone.
	 */
	p_endpt = ipoib_endpt_create(
		&mcast_req.member_rec.mgid, 0, CL_HTON32(0x00FFFFFF) );
	if( !p_endpt )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_endpt_create failed.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	status = __endpt_mgr_insert_locked( p_port, mac, p_endpt );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__endpt_mgr_insert_locked returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* reference the object for the multicast join request. */
	ipoib_port_ref( p_port, ref_join_mcast );

	status = p_port->p_adapter->p_ifc->join_mcast( p_port->ib_mgr.h_qp, &mcast_req, NULL );
	if( status != IB_SUCCESS )
	{
		ipoib_port_deref( p_port, ref_mcast_join_failed );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_join_mcast returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
	}

	IPOIB_EXIT( IPOIB_DBG_MCAST );
	return status;
}


static void
__mcast_cb(
	IN				ib_mcast_rec_t				*p_mcast_rec )
{
	ib_api_status_t		status;
	ipoib_port_t		*p_port;
	cl_fmap_item_t		*p_item;
	ipoib_endpt_t		*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	p_port = (ipoib_port_t*)p_mcast_rec->mcast_context;

	cl_obj_lock( &p_port->obj );
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}
	if( p_port->state != IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		if( p_mcast_rec->status == IB_SUCCESS )

		{
			ipoib_port_ref(p_port, ref_leave_mcast);
			p_port->p_adapter->p_ifc->leave_mcast( p_mcast_rec->h_mcast, __leave_error_mcast_cb );
		}
		ipoib_port_deref( p_port, ref_mcast_inv_state );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Invalid state - Aborting.\n") );
		return;
	}

	if( p_mcast_rec->status != IB_SUCCESS )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Multicast join request failed with status %s.\n",
			p_port->p_adapter->p_ifc->get_err_str( p_mcast_rec->status )) );
		/* Flag the adapter as hung. */
		p_port->p_adapter->hung =TRUE;
		ipoib_port_deref( p_port, ref_mcast_req_failed );
		IPOIB_EXIT( IPOIB_DBG_MCAST );
		return;
	}

	p_item = cl_fmap_get(
		&p_port->endpt_mgr.gid_endpts, &p_mcast_rec->p_member_rec->mgid );
	if( p_item == cl_fmap_end( &p_port->endpt_mgr.gid_endpts ) )
	{
		/*
		 * The endpoint must have been flushed while the join request
		 * was outstanding.  Just leave the group and return.  This
		 * is not an error.
		 */
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT(TRACE_LEVEL_WARNING, IPOIB_DBG_ERROR,
			("Failed to find endpoint for update.\n") );

		ipoib_port_ref(p_port, ref_leave_mcast);
		p_port->p_adapter->p_ifc->leave_mcast( p_mcast_rec->h_mcast, __leave_error_mcast_cb );
		ipoib_port_deref( p_port, ref_mcast_no_endpt );
		IPOIB_EXIT( IPOIB_DBG_MCAST );
		return;
	}

	p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, gid_item );
	p_endpt->p_ifc = p_port->p_adapter->p_ifc;

	/* Setup the endpoint for use. */
	status = ipoib_endpt_set_mcast(
		p_endpt, p_port->ib_mgr.h_pd, p_port->port_num, p_mcast_rec );
	if( status != IB_SUCCESS )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_MCAST,
			("ipoib_endpt_set_mcast returned %s.\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		/* Flag the adapter as hung. */
		p_port->p_adapter->hung = TRUE;
		ipoib_port_deref( p_port, ref_mcast_av_failed );
		IPOIB_EXIT( IPOIB_DBG_MCAST );
		return;
	}

	/*
	 * The endpoint is already in the GID and MAC maps.
	 * mast endpoint are not used in the LID map.
	 */
	CL_ASSERT(p_endpt->dlid == 0);
	/* set flag that endpoint is use */
	p_endpt->is_in_use = TRUE;
	cl_obj_unlock( &p_port->obj );
	
	/* Try to send all pending sends. */
	ipoib_port_resume( p_port );

	ipoib_port_deref( p_port, ref_join_mcast );

	IPOIB_EXIT( IPOIB_DBG_MCAST );
}


void
ipoib_leave_mcast_cb(
	IN				void				*context )
{
	ipoib_port_t		*p_port;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	p_port = (ipoib_port_t*)context;

	IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_MCAST,("p_port->mcast_cnt = %d\n", p_port->mcast_cnt));
	
	ipoib_port_deref( p_port, ref_leave_mcast);
	cl_atomic_dec( &p_port->mcast_cnt);
	
	if(0 == p_port->mcast_cnt)
	{
		KeSetEvent( &p_port->leave_mcast_event, EVENT_INCREMENT, FALSE );
	}
	
	IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			("Leave mcast callback deref ipoib_port \n") );
	
	IPOIB_EXIT( IPOIB_DBG_MCAST );
}



void
__leave_error_mcast_cb(
	IN				void				*context )
{
	ipoib_port_t		*p_port;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	p_port = (ipoib_port_t*)context;

	ipoib_port_deref( p_port, ref_leave_mcast);
	IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			("Leave mcast callback deref ipoib_port \n") );
	
	IPOIB_EXIT( IPOIB_DBG_MCAST );
}


NDIS_STATUS GetLsoHeaderSize(
	IN      ipoib_port_t* const pPort,
	IN      PNDIS_BUFFER  CurrBuffer,
	IN      LsoData *pLsoData,
	OUT     uint16_t *pSize,
	OUT     INT  *IndexOfData,
	IN		ipoib_hdr_t *ipoib_hdr
	)
	{
	UINT    CurrLength;
	PUCHAR  pSrc;
	PUCHAR  pCopiedData = pLsoData->coppied_data;
	ip_hdr_t UNALIGNED  *IpHdr;
	tcp_hdr_t UNALIGNED *TcpHdr;
	uint16_t                 TcpHeaderLen;
	uint16_t                 IpHeaderLen;
	uint16_t IpOffset;
	INT                 FullBuffers = 0;
	NDIS_STATUS         status = NDIS_STATUS_INVALID_PACKET;    
	//
	// This Flag indicates the way we gets the headers
	// RegularFlow = we get the headers (ETH+IP+TCP) in the same Buffer 
	// in sequence.
	//
#define IP_OFFSET 14;
	boolean_t			IsRegularFlow = TRUE;
	const uint16_t ETH_OFFSET = IP_OFFSET; 
	*pSize = 0;
	UNUSED_PARAM(pPort);
	IpOffset = IP_OFFSET; //(uint16_t)pPort->EncapsulationFormat.EncapsulationHeaderSize;
	*IndexOfData = 0;
	NdisQueryBufferSafe( CurrBuffer, &pSrc, &CurrLength, NormalPagePriority );    
	if (pSrc == NULL) {
		IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("Error processing packets\n"));
		return status;
	}
	// We start by looking for the ethernet and the IP
	if (CurrLength < ETH_OFFSET) {
		IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("Error porcessing packets\n"));
		return status;
	}
	//*pSize = *pSize + ETH_OFFSET;
	if (CurrLength == ETH_OFFSET) {        
		ASSERT(FALSE);
		IsRegularFlow = FALSE;        
		memcpy(pCopiedData, pSrc, ETH_OFFSET);
		pCopiedData += ETH_OFFSET;        
		FullBuffers++;
		// First buffer was only ethernet
		NdisGetNextBuffer( CurrBuffer, &CurrBuffer);
		NdisQueryBufferSafe( CurrBuffer, &pSrc, &CurrLength, NormalPagePriority );
		if (pSrc == NULL) {
			IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_ERROR, ("Error porcessing packets\n"));
			return status;
	    }
	} else {
		// This is ETH + IP together (at least)
		pLsoData->LsoBuffers[0].pData = pSrc + (ETH_OFFSET - sizeof (ipoib_hdr_t));
		memcpy (pLsoData->LsoBuffers[0].pData, ipoib_hdr, sizeof (ipoib_hdr_t));
		CurrLength -= ETH_OFFSET;
		pSrc = pSrc + ETH_OFFSET;
		*pSize = *pSize + sizeof (ipoib_hdr_t);
	}
	// we should now be having at least the size of ethernet data
	if (CurrLength < sizeof (ip_hdr_t)) {
		IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_ERROR, ("Error porcessing packets\n"));
		return status;
	}
	IpHdr = (ip_hdr_t UNALIGNED*)pSrc;
	IpHeaderLen = (uint16_t)IP_HEADER_LENGTH(IpHdr);
	ASSERT(IpHdr->prot == PROTOCOL_TCP);
	if (CurrLength < IpHeaderLen) {
		IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_ERROR, ("Error processing packets\n"));
		return status;
	}
	*pSize = *pSize + IpHeaderLen;
	// We now start to find where the TCP header starts
	if (CurrLength == IpHeaderLen) {
		ASSERT(FALSE);
		// two options : 
		// if(IsRegularFlow = FALSE) ==> ETH and IP seperated in two buffers
		// if(IsRegularFlow = TRUE ) ==> ETH and IP in the same buffer 
		// TCP will start at next buffer
		if(IsRegularFlow){
			memcpy(pCopiedData, pSrc-ETH_OFFSET ,ETH_OFFSET+IpHeaderLen);
			pCopiedData += (ETH_OFFSET + IpHeaderLen);
		} else {
			memcpy(pCopiedData, pSrc,IpHeaderLen);
			pCopiedData += IpHeaderLen;
		}
	    
		FullBuffers++;
		IsRegularFlow = FALSE;
		NdisGetNextBuffer( CurrBuffer, &CurrBuffer);
		NdisQueryBufferSafe( CurrBuffer, &pSrc, &CurrLength, NormalPagePriority );

		if (pSrc == NULL) {
			IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_ERROR, ("Error porcessing packets\n"));
			return status;
		}
	} else {
		// if(IsRegularFlow = TRUE ) ==> the ETH and IP and TCP in the same buffer       
		// if(IsRegularFlow = FLASE ) ==> ETH in one buffer , IP+TCP together in the same buffer
		if (IsRegularFlow) {            
			pLsoData->LsoBuffers[0].Len += IpHeaderLen;
		} else {            
			memcpy(pCopiedData, pSrc, IpHeaderLen);
			pCopiedData += IpHeaderLen;
		}

		CurrLength -= IpHeaderLen;
		pSrc = pSrc + IpHeaderLen;
	}
	if (CurrLength < sizeof (tcp_hdr_t)) {
		IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_ERROR, ("Error porcessing packets\n"));
		return status;
	}
	// We have finaly found the TCP header
	TcpHdr = (tcp_hdr_t UNALIGNED *)pSrc;
	TcpHeaderLen = TCP_HEADER_LENGTH(TcpHdr);

	ASSERT(TcpHeaderLen == 20);
	
	if (CurrLength < TcpHeaderLen) {
		//IPOIB_PRINT(TRACE_LEVEL_VERBOSE, ETH, ("Error porcessing packets\n"));
		return status;
	}
	*pSize =  *pSize + TcpHeaderLen;
	if(IsRegularFlow){
		pLsoData->LsoBuffers[0].Len += TcpHeaderLen;            
	}
	else{
		memcpy(pCopiedData, pSrc, TcpHeaderLen);                    
		pCopiedData += TcpHeaderLen;                  
	}         
	if (CurrLength == TcpHeaderLen) {        
		FullBuffers++;
		pLsoData->UsedBuffers = FullBuffers;
		*IndexOfData = FullBuffers ;
	} else {
		pLsoData->UsedBuffers = FullBuffers + 1;
		*IndexOfData = FullBuffers - 1;
	}
	pLsoData->FullBuffers = FullBuffers; 
	if (!IsRegularFlow){
		pLsoData->LsoBuffers[0].pData = pLsoData->coppied_data;
		pLsoData->LsoBuffers[0].Len = ETH_OFFSET + IpHeaderLen + TcpHeaderLen;
		ASSERT(pLsoData->LsoBuffers[0].Len <= LSO_MAX_HEADER);
	}
	return NDIS_STATUS_SUCCESS;
}

static void __port_do_mcast_garbage(ipoib_port_t* const	p_port)
{
    const mac_addr_t DEFAULT_MCAST_GROUP = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
	/* Do garbage collecting... */

	cl_map_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;
	cl_qlist_t		destroy_mc_list;
	uint8_t			cnt;
	const static GC_MAX_LEAVE_NUM = 80;

	cl_qlist_init( &destroy_mc_list );

	cl_obj_lock( &p_port->obj );
	/* Wait for all readers to finish */
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}
	cnt = 0;
	p_item = cl_qmap_head( &p_port->endpt_mgr.mac_endpts );
	while( (p_item != cl_qmap_end( &p_port->endpt_mgr.mac_endpts )) && (cnt < GC_MAX_LEAVE_NUM))
	{
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
		p_item = cl_qmap_next( p_item );

		/* Check if the current endpoint is not a multicast listener */

		if( p_endpt->h_mcast && 
			(!p_endpt->is_mcast_listener) &&
			( cl_memcmp( &p_endpt->mac, &DEFAULT_MCAST_GROUP, sizeof(mac_addr_t) ) &&
			 (!p_endpt->is_in_use) ))
		{
			cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts,
				&p_endpt->mac_item );
			cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts,
				&p_endpt->gid_item );

			if( p_endpt->dlid )
			{
				cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts,
					&p_endpt->lid_item );
				p_endpt->dlid = 0;
			}

			cl_qlist_insert_tail(
				&destroy_mc_list, &p_endpt->mac_item.pool_item.list_item );
			cnt++;
		}
		else
			p_endpt->is_in_use = FALSE;
	}
	cl_obj_unlock( &p_port->obj );

	/* Destroy all multicast endpoints now that we have released the lock. */
	while( cl_qlist_count( &destroy_mc_list ) )
	{
		p_endpt = PARENT_STRUCT( cl_qlist_remove_head( &destroy_mc_list ),
								 ipoib_endpt_t, mac_item.pool_item.list_item );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("mcast garbage collector: destroying endpoint %02x:%02x:%02x:%02x:%02x:%02x \n", 
				 p_endpt->mac.addr[0],
				 p_endpt->mac.addr[1],
				 p_endpt->mac.addr[2],
				 p_endpt->mac.addr[3],
				 p_endpt->mac.addr[4],
				 p_endpt->mac.addr[5]) );
		cl_obj_destroy( &p_endpt->obj );
	}
}

static void __port_mcast_garbage_dpc(KDPC *p_gc_dpc,void *context,void *s_arg1, void *s_arg2)
{
	ipoib_port_t *p_port = context;

	UNREFERENCED_PARAMETER(p_gc_dpc);
	UNREFERENCED_PARAMETER(s_arg1);
	UNREFERENCED_PARAMETER(s_arg2);

	__port_do_mcast_garbage(p_port);
}


