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



#include "ipoib_endpoint.h"
#include "ipoib_port.h"
#include "ipoib_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_endpoint.tmh"
#endif
#include <complib/cl_atomic.h>


static void
__endpt_destroying(
	IN				cl_obj_t*					p_obj );

static void
__endpt_cleanup(
	IN				cl_obj_t*					p_obj );

static void
__endpt_free(
	IN				cl_obj_t*					p_obj );

static ib_api_status_t
__create_mcast_av(
	IN				ib_pd_handle_t				h_pd,
	IN				uint8_t						port_num,
	IN				ib_member_rec_t* const		p_member_rec,
		OUT			ib_av_handle_t* const		ph_av );

static inline ipoib_port_t*
__endpt_parent(
	IN				ipoib_endpt_t* const		p_endpt );

static void
__path_query_cb(
	IN				ib_query_rec_t				*p_query_rec );

static void
__endpt_resolve(
	IN				ipoib_endpt_t* const		p_endpt );


ipoib_endpt_t*
ipoib_endpt_create(
	IN		const	ib_gid_t* const				p_dgid,
	IN		const	net16_t						dlid,
	IN		const	net32_t						qpn )
{
	ipoib_endpt_t	*p_endpt;
	cl_status_t		status;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_endpt = cl_zalloc( sizeof(ipoib_endpt_t) );
	if( !p_endpt )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate endpoint (%d bytes)\n",
			sizeof(ipoib_endpt_t)) );
		return NULL;
	}

	cl_obj_construct( &p_endpt->obj, IPOIB_OBJ_ENDPOINT );

	status = cl_obj_init( &p_endpt->obj, CL_DESTROY_ASYNC,
		__endpt_destroying, __endpt_cleanup, __endpt_free );

	p_endpt->dgid = *p_dgid;
	p_endpt->dlid = dlid;
	p_endpt->qpn = qpn;

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return p_endpt;
}


static ib_api_status_t
__create_mcast_av(
	IN				ib_pd_handle_t				h_pd,
	IN				uint8_t						port_num,
	IN				ib_member_rec_t* const		p_member_rec,
		OUT			ib_av_handle_t* const		ph_av )
{
	ib_av_attr_t	av_attr;
	uint32_t		flow_lbl;
	uint8_t			hop_lmt;
	ib_api_status_t	status;
	ipoib_endpt_t	*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	p_endpt = PARENT_STRUCT(ph_av, ipoib_endpt_t, h_av );

	cl_memclr( &av_attr, sizeof(ib_av_attr_t) );
	av_attr.port_num = port_num;
	ib_member_get_sl_flow_hop( p_member_rec->sl_flow_hop,
		&av_attr.sl, &flow_lbl, &hop_lmt );
	av_attr.dlid = p_member_rec->mlid;
	av_attr.grh_valid = TRUE;
	av_attr.grh.hop_limit = hop_lmt;
	av_attr.grh.dest_gid = p_member_rec->mgid;
	av_attr.grh.src_gid = p_member_rec->port_gid;
	av_attr.grh.ver_class_flow =
		ib_grh_set_ver_class_flow( 6, p_member_rec->tclass, flow_lbl );
	av_attr.static_rate = p_member_rec->rate & IB_PATH_REC_BASE_MASK;
	av_attr.path_bits = 0;
	/* port is not attached to endpoint at this point, so use endpt ifc reference */
	status = p_endpt->p_ifc->create_av( h_pd, &av_attr, ph_av );

	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_av returned %s\n",
			p_endpt->p_ifc->get_err_str( status )) );
	}

	IPOIB_EXIT( IPOIB_DBG_MCAST );
	return status;
}


ib_api_status_t
ipoib_endpt_set_mcast(
	IN				ipoib_endpt_t* const		p_endpt,
	IN				ib_pd_handle_t				h_pd,
	IN				uint8_t						port_num,
	IN				ib_mcast_rec_t* const		p_mcast_rec )
{
	ib_api_status_t	status;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
		("Create av for MAC: %02X-%02X-%02X-%02X-%02X-%02X\n",
		p_endpt->mac.addr[0], p_endpt->mac.addr[1],
		p_endpt->mac.addr[2], p_endpt->mac.addr[3],
		p_endpt->mac.addr[4], p_endpt->mac.addr[5]) );
		
	status = __create_mcast_av( h_pd, port_num, p_mcast_rec->p_member_rec,
		&p_endpt->h_av );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__create_mcast_av returned %s\n", 
			p_endpt->p_ifc->get_err_str( status )) );
		return status;
	}
	p_endpt->h_mcast = p_mcast_rec->h_mcast;
	CL_ASSERT(p_endpt->dlid == 0);

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return IB_SUCCESS;
}


static void
__endpt_destroying(
	IN				cl_obj_t*					p_obj )
{
	ipoib_endpt_t	*p_endpt;
	ipoib_port_t	*p_port;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_endpt = PARENT_STRUCT( p_obj, ipoib_endpt_t, obj );
	p_port = __endpt_parent( p_endpt );

	cl_obj_lock( p_obj );
	if( p_endpt->h_query )
	{
		p_port->p_adapter->p_ifc->cancel_query(
			p_port->p_adapter->h_al, p_endpt->h_query );
		p_endpt->h_query = NULL;
	}

	/* Leave the multicast group if it exists. */
	if( p_endpt->h_mcast )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("Leaving MCast group\n") );
		ipoib_port_ref(p_port, ref_leave_mcast);
		p_port->p_adapter->p_ifc->leave_mcast( p_endpt->h_mcast, ipoib_leave_mcast_cb );
	}
	
	cl_obj_unlock( p_obj );
	
	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


static void
__endpt_cleanup(
	IN				cl_obj_t*					p_obj )
{
	ipoib_endpt_t	*p_endpt;
	ipoib_port_t	*p_port;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_endpt = PARENT_STRUCT( p_obj, ipoib_endpt_t, obj );
	p_port = __endpt_parent( p_endpt );

	/* Destroy the AV if it exists. */
	if( p_endpt->h_av )
		p_port->p_adapter->p_ifc->destroy_av( p_endpt->h_av );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


static void
__endpt_free(
	IN				cl_obj_t*					p_obj )
{
	ipoib_endpt_t	*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_endpt = PARENT_STRUCT( p_obj, ipoib_endpt_t, obj );

	cl_obj_deinit( p_obj );
	cl_free( p_endpt );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


static inline ipoib_port_t*
__endpt_parent(
	IN				ipoib_endpt_t* const		p_endpt )
{
	return PARENT_STRUCT( p_endpt->rel.p_parent_obj, ipoib_port_t, obj );
}


/*
 * This function is called with the port object's send lock held and
 * a reference held on the endpoint.  If we fail, we release the reference.
 */
NDIS_STATUS
ipoib_endpt_queue(
	IN				ipoib_endpt_t* const		p_endpt )
{
	ib_api_status_t	status;
	ipoib_port_t	*p_port;
	ib_av_attr_t	av_attr;
	net32_t			flow_lbl;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	if( p_endpt->h_av )
	{
		IPOIB_EXIT( IPOIB_DBG_ENDPT );
		return NDIS_STATUS_SUCCESS;
	}

	if( p_endpt->qpn == CL_HTON32(0x00FFFFFF) )
	{
		/*
		 * Handle a race between the mcast callback and a receive/send.  The QP
		 * is joined to the MC group before the MC callback is received, so it
		 * can receive packets, and NDIS can try to respond.  We need to delay
		 * a response until the MC callback runs and sets the AV.
		 */
		ipoib_endpt_deref( p_endpt );
		IPOIB_EXIT( IPOIB_DBG_ENDPT );
		return NDIS_STATUS_PENDING;
	}

	/* This is the first packet for this endpoint.  Create the AV. */
	p_port = __endpt_parent( p_endpt );

	cl_memclr( &av_attr, sizeof(ib_av_attr_t) );

	av_attr.port_num = p_port->port_num;

	ib_member_get_sl_flow_hop(
		p_port->ib_mgr.bcast_rec.sl_flow_hop,
		&av_attr.sl,
		&flow_lbl,
		&av_attr.grh.hop_limit
		);

	av_attr.dlid = p_endpt->dlid;

	/*
	 * We always send the GRH so that we preferably lookup endpoints
	 * by GID rather than by LID.  This allows certain WHQL tests
	 * such as the 2c_MediaCheck test to succeed since they don't use
	 * IP.  This allows endpoints to be created on the fly for requests
	 * for which there is no match, something that doesn't work when
	 * using LIDs only.
	 */
	av_attr.grh_valid = TRUE;
	av_attr.grh.ver_class_flow = ib_grh_set_ver_class_flow(
		6, p_port->ib_mgr.bcast_rec.tclass, flow_lbl );
	av_attr.grh.resv1 = 0;
	av_attr.grh.resv2 = 0;
	ib_gid_set_default( &av_attr.grh.src_gid, p_port->p_adapter->guids.port_guid.guid );
	av_attr.grh.dest_gid = p_endpt->dgid;

	av_attr.static_rate = p_port->ib_mgr.bcast_rec.rate;
	av_attr.path_bits = 0;

	/* Create the AV. */
	status = p_port->p_adapter->p_ifc->create_av(
		p_port->ib_mgr.h_pd, &av_attr, &p_endpt->h_av );
	if( status != IB_SUCCESS )
	{
		p_port->p_adapter->hung = TRUE;
		ipoib_endpt_deref( p_endpt );
		cl_obj_unlock( &p_endpt->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_av failed with %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return NDIS_STATUS_FAILURE;
	}

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return NDIS_STATUS_SUCCESS;
}
