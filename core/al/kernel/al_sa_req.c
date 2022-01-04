/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#include <iba/ib_al.h>
#include <complib/cl_timer.h>

#include "al.h"
#include "al_ca.h"
#include "al_common.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_sa_req.tmh"
#endif
#include "al_mgr.h"
#include "al_query.h"
#include "ib_common.h"


/* Global SA request manager */
typedef struct _sa_req_mgr
{
	al_obj_t					obj;		/* Child of gp_al_mgr */
	ib_pnp_handle_t				h_pnp;		/* Handle for CA PnP events */

}	sa_req_mgr_t;


static sa_req_mgr_t				*gp_sa_req_mgr = NULL;



/*
 * Function prototypes.
 */
void
destroying_sa_req_mgr(
	IN				al_obj_t*					p_obj );

void
free_sa_req_mgr(
	IN				al_obj_t*					p_obj );

ib_api_status_t
sa_req_mgr_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec );

ib_api_status_t
create_sa_req_svc(
	IN				ib_pnp_port_rec_t*			p_pnp_rec );

void
destroying_sa_req_svc(
	IN				al_obj_t*					p_obj );

void
free_sa_req_svc(
	IN				al_obj_t*					p_obj );

ib_api_status_t
init_sa_req_svc(
	IN				sa_req_svc_t*				p_sa_req_svc,
	IN		const	ib_pnp_port_rec_t			*p_pnp_rec );

void
sa_req_send_comp_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad );

void
sa_req_recv_comp_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad );

void
sa_req_svc_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec );


/*
 * Create the sa_req manager.
 */
ib_api_status_t
create_sa_req_mgr(
	IN				al_obj_t*	const			p_parent_obj )
{
	ib_pnp_req_t			pnp_req;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SA_REQ );
	CL_ASSERT( p_parent_obj );
	CL_ASSERT( gp_sa_req_mgr == NULL );

	gp_sa_req_mgr = cl_zalloc( sizeof( sa_req_mgr_t ) );
	if( gp_sa_req_mgr == NULL )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_zalloc failed\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the sa_req manager. */
	construct_al_obj( &gp_sa_req_mgr->obj, AL_OBJ_TYPE_SA_REQ_SVC );

	/* Initialize the global sa_req manager object. */
	status = init_al_obj( &gp_sa_req_mgr->obj, gp_sa_req_mgr, TRUE,
		destroying_sa_req_mgr, NULL, free_sa_req_mgr );
	if( status != IB_SUCCESS )
	{
		free_sa_req_mgr( &gp_sa_req_mgr->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_spinlock_init failed\n") );
		return status;
	}
	status = attach_al_obj( p_parent_obj, &gp_sa_req_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_sa_req_mgr->obj.pfn_destroy( &gp_sa_req_mgr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Register for CA PnP events. */
	cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
	pnp_req.pnp_class = IB_PNP_PORT;
	pnp_req.pnp_context = &gp_sa_req_mgr->obj;
	pnp_req.pfn_pnp_cb = sa_req_mgr_pnp_cb;

	status = ib_reg_pnp( gh_al, &pnp_req, &gp_sa_req_mgr->h_pnp );
	if (status != IB_SUCCESS)
	{
		gp_sa_req_mgr->obj.pfn_destroy( &gp_sa_req_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_reg_pnp failed: %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/*
	 * Note that we don't release the reference from init_al_obj because
	 * we need a reference on behalf of the ib_reg_pnp call.  This avoids
	 * a call to ref_al_obj and deref_al_obj.
	 */

	AL_EXIT( AL_DBG_SA_REQ );
	return IB_SUCCESS;
}



/*
 * Pre-destroy the sa_req manager.
 */
void
destroying_sa_req_mgr(
	IN				al_obj_t*					p_obj )
{
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	CL_ASSERT( gp_sa_req_mgr == PARENT_STRUCT( p_obj, sa_req_mgr_t, obj ) );
	UNUSED_PARAM( p_obj );

	/* Deregister for PnP events. */
	if( gp_sa_req_mgr->h_pnp )
	{
		status = ib_dereg_pnp( gp_sa_req_mgr->h_pnp,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}
}



/*
 * Free the sa_req manager.
 */
void
free_sa_req_mgr(
	IN				al_obj_t*					p_obj )
{
	CL_ASSERT( p_obj );
	CL_ASSERT( gp_sa_req_mgr == PARENT_STRUCT( p_obj, sa_req_mgr_t, obj ) );
	UNUSED_PARAM( p_obj );

	destroy_al_obj( &gp_sa_req_mgr->obj );
	cl_free( gp_sa_req_mgr );
	gp_sa_req_mgr = NULL;
}



/*
 * SA request manager PnP event callback.
 */
ib_api_status_t
sa_req_mgr_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec )
{
	sa_req_svc_t				*p_sa_req_svc;
	ib_av_attr_t				av_attr;
	ib_pd_handle_t				h_pd;
	ib_api_status_t				status = IB_SUCCESS;
	ib_pnp_port_rec_t			*p_port_rec = (ib_pnp_port_rec_t*)p_pnp_rec;

	AL_ENTER( AL_DBG_SA_REQ );
	CL_ASSERT( p_pnp_rec );
	CL_ASSERT( p_pnp_rec->pnp_context == &gp_sa_req_mgr->obj );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );

	/* Dispatch based on the PnP event type. */
	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_PORT_ADD:
		if ( p_port_rec->p_port_attr->transport == RDMA_TRANSPORT_RDMAOE )
		{	// RoCE port
			AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_ERROR,
				("create_sa_req_svc is not called for RoCE port %d\n", p_port_rec->p_port_attr->port_num ) );
		}
		else
		{
			status = create_sa_req_svc( p_port_rec );
			if( status != IB_SUCCESS )
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("create_sa_req_svc for port %d failed: %s\n", 
					p_port_rec->p_port_attr->port_num, ib_get_err_str(status)) );
			}
		}
		break;

	case IB_PNP_PORT_REMOVE:
		// context will be NULL for RoCE port
		if ( !p_pnp_rec->context )
			break;
		p_sa_req_svc = p_pnp_rec->context;
		ref_al_obj( &p_sa_req_svc->obj );
		p_sa_req_svc->obj.pfn_destroy( &p_sa_req_svc->obj, NULL );
		p_pnp_rec->context = NULL;
		status = IB_SUCCESS;
		break;

	case IB_PNP_PORT_ACTIVE:
	case IB_PNP_SM_CHANGE:
		// context will be NULL for RoCE port
		if ( !p_pnp_rec->context )
			break;
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_SA_REQ,
			("updating SM information\n") );

		p_sa_req_svc = p_pnp_rec->context;
		p_sa_req_svc->sm_lid = p_port_rec->p_port_attr->sm_lid;
		p_sa_req_svc->sm_sl = p_port_rec->p_port_attr->sm_sl;

		/* Update the address vector. */
		status = ib_query_av( p_sa_req_svc->h_av, &av_attr, &h_pd );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("AV query failed: %s\n", ib_get_err_str(status)) );
			status = IB_SUCCESS;
			break;
		}

		av_attr.dlid = p_sa_req_svc->sm_lid;
		av_attr.sl = p_sa_req_svc->sm_sl;
		status = ib_modify_av( p_sa_req_svc->h_av, &av_attr );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("modify AV failed: %s\n", ib_get_err_str(status) ) );
			status = IB_SUCCESS;
			break;
		}
		break;

	case IB_PNP_PORT_INIT:
	case IB_PNP_PORT_ARMED:
	case IB_PNP_PORT_DOWN:
		// context will be NULL for RoCE port
		if ( !p_pnp_rec->context )
			break;
		p_sa_req_svc = p_pnp_rec->context;
		p_sa_req_svc->sm_lid = 0;
		p_sa_req_svc->sm_sl = 0;

	default:
		status = IB_SUCCESS;
		break;
	}
	AL_EXIT( AL_DBG_SA_REQ );
	return status;
}



/*
 * Create an sa_req service.
 */
ib_api_status_t
create_sa_req_svc(
	IN				ib_pnp_port_rec_t*			p_pnp_rec )
{
	sa_req_svc_t*			p_sa_req_svc;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SA_REQ );
	CL_ASSERT( p_pnp_rec );
	CL_ASSERT( p_pnp_rec->p_ca_attr );
	CL_ASSERT( p_pnp_rec->p_port_attr );

	p_sa_req_svc = cl_zalloc( sizeof( sa_req_svc_t ) );
	if( p_sa_req_svc == NULL )
		return IB_INSUFFICIENT_MEMORY;

	/* Construct the sa_req service object. */
	construct_al_obj( &p_sa_req_svc->obj, AL_OBJ_TYPE_SA_REQ_SVC );

	/* Initialize the sa_req service object. */
	status = init_al_obj( &p_sa_req_svc->obj, p_sa_req_svc, TRUE,
		destroying_sa_req_svc, NULL, free_sa_req_svc );
	if( status != IB_SUCCESS )
	{
		free_sa_req_svc( &p_sa_req_svc->obj );
		return status;
	}

	/* Attach to the sa_req_mgr. */
	status = attach_al_obj( &gp_sa_req_mgr->obj, &p_sa_req_svc->obj );
	if( status != IB_SUCCESS )
	{
		p_sa_req_svc->obj.pfn_destroy( &p_sa_req_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Allocate a QP alias and MAD service to send queries on. */
	status = init_sa_req_svc( p_sa_req_svc, p_pnp_rec );
	if( status != IB_SUCCESS )
	{
		p_sa_req_svc->obj.pfn_destroy( &p_sa_req_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_sa_req_svc failed: %s\n", ib_get_err_str(status) ) );
		return status;
	}

	/* Set the context of the PnP event to this child object. */
	p_pnp_rec->pnp_rec.context = p_sa_req_svc;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_sa_req_svc->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_SA_REQ );
	return IB_SUCCESS;
}



/*
 * Pre-destroy a sa_req service.
 */
void
destroying_sa_req_svc(
	IN				al_obj_t*					p_obj )
{
	sa_req_svc_t*			p_sa_req_svc;
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	p_sa_req_svc = PARENT_STRUCT( p_obj, sa_req_svc_t, obj );

	/* Destroy the AV. */
	if( p_sa_req_svc->h_av )
		ib_destroy_av_ctx( p_sa_req_svc->h_av, 4 );

	/* Destroy the QP. */
	if( p_sa_req_svc->h_qp )
	{
		status = ib_destroy_qp( p_sa_req_svc->h_qp,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}
}



/*
 * Free a sa_req service.
 */
void
free_sa_req_svc(
	IN				al_obj_t*					p_obj )
{
	sa_req_svc_t*			p_sa_req_svc;

	CL_ASSERT( p_obj );
	p_sa_req_svc = PARENT_STRUCT( p_obj, sa_req_svc_t, obj );

	destroy_al_obj( p_obj );
	cl_free( p_sa_req_svc );
}



/*
 * Initialize an sa_req service.
 */
ib_api_status_t
init_sa_req_svc(
	IN				sa_req_svc_t				*p_sa_req_svc,
	IN		const	ib_pnp_port_rec_t			*p_pnp_rec )
{
	ib_qp_create_t			qp_create;
	ib_mad_svc_t			mad_svc;
	ib_api_status_t			status;
	ib_ca_handle_t			h_ca;
	ib_av_attr_t			av_attr;

	AL_ENTER( AL_DBG_SA_REQ );
	CL_ASSERT( p_sa_req_svc && p_pnp_rec );

	/* Acquire the correct CI CA for this port. */
	h_ca = acquire_ca( p_pnp_rec->p_ca_attr->ca_guid );
	if( !h_ca )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_SA_REQ, ("Failed to acquire CA\n") );
		return IB_INVALID_GUID;
	}
	p_sa_req_svc->obj.p_ci_ca = h_ca->obj.p_ci_ca;

	/* Record which port this service operates on. */
	p_sa_req_svc->port_guid = p_pnp_rec->p_port_attr->port_guid;
	p_sa_req_svc->port_num = p_pnp_rec->p_port_attr->port_num;
	p_sa_req_svc->sm_lid = p_pnp_rec->p_port_attr->sm_lid;
	p_sa_req_svc->sm_sl = p_pnp_rec->p_port_attr->sm_sl;
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_SA_REQ,
		("using port: 0x%x\tsm lid: 0x%x\tsm sl: 0x%x\n",
		p_sa_req_svc->port_num, p_sa_req_svc->sm_lid, p_sa_req_svc->sm_sl) );

	/* Create the QP. */
	cl_memclr( &qp_create, sizeof( ib_qp_create_t ) );
	qp_create.qp_type = IB_QPT_QP1_ALIAS;
	qp_create.sq_depth = p_pnp_rec->p_ca_attr->max_wrs;
	qp_create.rq_depth = 0;
	qp_create.sq_sge = 1;
	qp_create.rq_sge = 0;
	qp_create.h_sq_cq = NULL;
	qp_create.h_rq_cq = NULL;
	qp_create.sq_signaled = TRUE;

	status = ib_get_spl_qp( h_ca->obj.p_ci_ca->h_pd_alias,
		p_sa_req_svc->port_guid, &qp_create, p_sa_req_svc,
		sa_req_svc_event_cb, &p_sa_req_svc->pool_key, &p_sa_req_svc->h_qp );

	/*
	 * Release the CI CA once we've allocated the QP.  The CI CA will not
	 * go away while we hold the QP.
	 */
	deref_al_obj( &h_ca->obj );

	/* Check for failure allocating the QP. */
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("failed to create QP1 alias: %s\n", ib_get_err_str(status) ) );
		return status;
	}

	/* Reference the sa_req service on behalf of QP alias. */
	ref_al_obj( &p_sa_req_svc->obj );

	/* Create a MAD service. */
	cl_memclr( &mad_svc, sizeof( ib_mad_svc_t ) );
	mad_svc.mad_svc_context = p_sa_req_svc;
	mad_svc.pfn_mad_send_cb = sa_req_send_comp_cb;
	mad_svc.pfn_mad_recv_cb = sa_req_recv_comp_cb;

	status = ib_reg_mad_svc( p_sa_req_svc->h_qp, &mad_svc,
		&p_sa_req_svc->h_mad_svc );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("failed to register MAD service: %s\n", ib_get_err_str(status) ) );
		return status;
	}

	/* Create an address vector for the SA. */
	av_attr.port_num = p_sa_req_svc->port_num;
	av_attr.sl = p_sa_req_svc->sm_sl;
	av_attr.dlid = cl_hton16(1);
	av_attr.grh_valid = FALSE;
	av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
	av_attr.path_bits = 0;

	status = ib_create_av_ctx( p_sa_req_svc->obj.p_ci_ca->h_pd_alias,
		&av_attr, 3, &p_sa_req_svc->h_av );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("failed to create AV: %s\n", ib_get_err_str(status) ) );
		return status;
	}

	AL_EXIT( AL_DBG_SA_REQ );
	return IB_SUCCESS;
}



/*
 * SA request service asynchronous event callback.  Our QP is an alias,
 * so if we've received an error, the QP is unusable.
 */
void
sa_req_svc_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	sa_req_svc_t		*p_sa_req_svc;

	CL_ASSERT( p_event_rec );
	CL_ASSERT( p_event_rec->context );

	p_sa_req_svc = p_event_rec->context;
	ref_al_obj( &p_sa_req_svc->obj );
	p_sa_req_svc->obj.pfn_destroy( &p_sa_req_svc->obj, NULL );
}



/*
 * Acquire the sa_req service for the given port.
 */
static sa_req_svc_t*
acquire_sa_req_svc(
	IN		const	ib_net64_t					port_guid )
{
	cl_list_item_t		*p_list_item;
	sa_req_svc_t		*p_sa_req_svc;
	al_obj_t			*p_obj;

	CL_ASSERT( gp_sa_req_mgr );

	/* Search for the sa_req service for the given port. */
	cl_spinlock_acquire( &gp_sa_req_mgr->obj.lock );
	for( p_list_item = cl_qlist_head( &gp_sa_req_mgr->obj.obj_list );
		 p_list_item != cl_qlist_end( &gp_sa_req_mgr->obj.obj_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_obj = PARENT_STRUCT( p_list_item, al_obj_t, pool_item );
		p_sa_req_svc = PARENT_STRUCT( p_obj, sa_req_svc_t, obj );

		/* Make sure that the REQ service isn't being destroyed. */
		if( p_sa_req_svc->obj.state != CL_INITIALIZED || !p_sa_req_svc->sm_lid )
			continue;

		/* Check for a port match. */
		if( p_sa_req_svc->port_guid == port_guid )
		{
			/* Reference the service on behalf of the client. */
			ref_al_obj( &p_sa_req_svc->obj );
			cl_spinlock_release( &gp_sa_req_mgr->obj.lock );
			return p_sa_req_svc;
		}
	}
	cl_spinlock_release( &gp_sa_req_mgr->obj.lock );

	return NULL;
}



ib_api_status_t
al_send_sa_req(
	IN				al_sa_req_t					*p_sa_req,
	IN		const	net64_t						port_guid,
	IN		const	uint32_t					timeout_ms,
	IN		const	uint32_t					retry_cnt,
	IN		const	ib_user_query_t* const		p_sa_req_data,
	IN		const	ib_al_flags_t				flags )
{
	ib_api_status_t			status;
	sa_req_svc_t			*p_sa_req_svc;
	ib_mad_element_t		*p_mad_request;
	ib_mad_t				*p_mad_hdr;
	ib_sa_mad_t				*p_sa_mad;
	KEVENT					event;

	AL_ENTER( AL_DBG_SA_REQ );

	if( flags & IB_FLAGS_SYNC )
	{
		if( !cl_is_blockable() )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Thread context not blockable\n") );
			return IB_INVALID_SETTING;
		}

		KeInitializeEvent( &event, NotificationEvent, FALSE );
		p_sa_req->p_sync_event = &event;
	}
	else
	{
		p_sa_req->p_sync_event = NULL;
	}

	/* Locate the sa_req service to issue the sa_req on. */
	p_sa_req->p_sa_req_svc = acquire_sa_req_svc( port_guid );
	if( !p_sa_req->p_sa_req_svc )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("invalid port GUID %#I64x\n", port_guid) );
		return IB_INVALID_GUID;
	}

	/* Get a MAD element for the send request. */
	p_sa_req_svc = p_sa_req->p_sa_req_svc;
	status = ib_get_mad( p_sa_req_svc->pool_key, MAD_BLOCK_SIZE,
		&p_mad_request );
	if( status != IB_SUCCESS )
	{
		deref_al_obj( &p_sa_req_svc->obj );
		AL_EXIT( AL_DBG_SA_REQ );
		return status;
	}

	/* Store the MAD request so it can be cancelled. */
	p_sa_req->p_mad_request = p_mad_request;

	/* Initialize the MAD buffer for the send operation. */
	p_mad_hdr = p_sa_req->p_mad_request->p_mad_buf;
	p_sa_mad = (ib_sa_mad_t*)p_mad_hdr;

	/* Initialize the standard MAD header. */
	ib_mad_init_new( p_mad_hdr, IB_MCLASS_SUBN_ADM, (uint8_t)2,
		p_sa_req_data->method,
		cl_hton64( (uint64_t)cl_atomic_inc( &p_sa_req_svc->trans_id ) ),
		0, 0 );

	/* Set the query information. */
	p_sa_mad->attr_id = p_sa_req_data->attr_id;
	p_sa_mad->attr_offset = ib_get_attr_offset( p_sa_req_data->attr_size );
	p_sa_mad->comp_mask = p_sa_req_data->comp_mask;
	/*
	 * Most set operations don't use the component mask.
	 * Check the method and copy the attributes if it's a set too.
	 */
	if( p_sa_mad->comp_mask || p_sa_mad->method == IB_MAD_METHOD_SET )
	{
		cl_memcpy( p_sa_mad->data, p_sa_req_data->p_attr,
			p_sa_req_data->attr_size );
	}

	/* Set the MAD element information. */
	p_mad_request->context1 = p_sa_req;
	p_mad_request->send_context1 = p_sa_req;
	p_mad_request->remote_qp = IB_QP1;
	p_mad_request->h_av = p_sa_req_svc->h_av;
	p_mad_request->send_opt = 0;
	p_mad_request->remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
	p_mad_request->resp_expected = TRUE;
	p_mad_request->timeout_ms = timeout_ms;
	p_mad_request->retry_cnt = retry_cnt;

	status = ib_send_mad( p_sa_req_svc->h_mad_svc, p_mad_request, NULL );
	if( status != IB_SUCCESS )
	{
		p_sa_req->p_mad_request = NULL;
		ib_put_mad( p_mad_request );
		deref_al_obj( &p_sa_req->p_sa_req_svc->obj );
	}
	else if( flags & IB_FLAGS_SYNC )
	{
		/* Wait for the MAD completion. */
		KeWaitForSingleObject( &event, Executive, KernelMode, FALSE, NULL );
	}

	AL_EXIT( AL_DBG_SA_REQ );
	return status;
}



/*
 * SA request send completion callback.
 */
void
sa_req_send_comp_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_request_mad )
{
	al_sa_req_t			*p_sa_req;
	sa_req_svc_t		*p_sa_req_svc;
	KEVENT				*p_sync_event;

	AL_ENTER( AL_DBG_SA_REQ );

	UNUSED_PARAM( h_mad_svc );
	UNUSED_PARAM( mad_svc_context );

	/*
	 * Check the result of the send operation.  If the send was successful,
	 * we will be getting a receive callback with the response.
	 */
	if( p_request_mad->status != IB_WCS_SUCCESS )
	{
		/* Notify the requestor of the result. */
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_QUERY,
			("request failed - notifying user\n") );

		p_sa_req = p_request_mad->send_context1;
		p_sa_req_svc = p_sa_req->p_sa_req_svc;
		p_sync_event = p_sa_req->p_sync_event;

		p_sa_req->status = convert_wc_status( p_request_mad->status );
		p_sa_req->pfn_sa_req_cb( p_sa_req, NULL );
		if( p_sync_event )
			KeSetEvent( p_sync_event, 0, FALSE );
		deref_al_obj( &p_sa_req_svc->obj );
	}

	/* Return the MAD. */
	ib_put_mad( p_request_mad );

	AL_EXIT( AL_DBG_SA_REQ );
}



/*
 * SA request receive completion callback.
 */
void
sa_req_recv_comp_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_response )
{
	al_sa_req_t			*p_sa_req;
	sa_req_svc_t		*p_sa_req_svc;
	ib_sa_mad_t			*p_sa_mad;
	KEVENT				*p_sync_event;

	AL_ENTER( AL_DBG_SA_REQ );

	UNUSED_PARAM( h_mad_svc );
	UNUSED_PARAM( mad_svc_context );

	p_sa_req = p_mad_response->send_context1;
	p_sa_req_svc = p_sa_req->p_sa_req_svc;
	p_sync_event = p_sa_req->p_sync_event;

	//*** check for SA redirection...

	/* Record the results of the request. */
	p_sa_mad = (ib_sa_mad_t*)ib_get_mad_buf( p_mad_response );
	if( p_sa_mad->status == IB_SA_MAD_STATUS_SUCCESS )
		p_sa_req->status = IB_SUCCESS;
	else
		p_sa_req->status = IB_REMOTE_ERROR;

	/* Notify the requestor of the result. */
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_SA_REQ, ("notifying user\n") );
	p_sa_req->pfn_sa_req_cb( p_sa_req, p_mad_response );
	if( p_sync_event )
		KeSetEvent( p_sync_event, 0, FALSE );
	deref_al_obj( &p_sa_req_svc->obj );

	AL_EXIT( AL_DBG_SA_REQ );
}



ib_api_status_t
convert_wc_status(
	IN		const	ib_wc_status_t				wc_status )
{
	switch( wc_status )
	{
	case IB_WCS_SUCCESS:
		return IB_SUCCESS;

	case IB_WCS_TIMEOUT_RETRY_ERR:
		return IB_TIMEOUT;

	case IB_WCS_CANCELED:
		return IB_CANCELED;

	default:
		return IB_ERROR;
	}
}
