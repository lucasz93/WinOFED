/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
 * Copyright (c) 2006 Voltaire Corporation.  All rights reserved.
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

#include "ib_common.h"
#include "al_common.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_smi.tmh"
#endif
#include "al_verbs.h"
#include "al_mgr.h"
#include "al_pnp.h"
#include "al_qp.h"
#include "al_smi.h"
#include "al_av.h"


extern char						node_desc[IB_NODE_DESCRIPTION_SIZE];

#define	SMI_POLL_INTERVAL			20000		/* Milliseconds */
#define	LOCAL_MAD_TIMEOUT			50			/* Milliseconds */
#define	DEFAULT_QP0_DEPTH			256
#define	DEFAULT_QP1_DEPTH			1024

uint32_t				g_smi_poll_interval =	SMI_POLL_INTERVAL;
spl_qp_mgr_t*			gp_spl_qp_mgr = NULL;


/*
 * Function prototypes.
 */
void
destroying_spl_qp_mgr(
	IN				al_obj_t*					p_obj );

void
free_spl_qp_mgr(
	IN				al_obj_t*					p_obj );

ib_api_status_t
spl_qp0_agent_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec );

ib_api_status_t
spl_qp1_agent_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec );

ib_api_status_t
spl_qp_agent_pnp(
	IN				ib_pnp_rec_t*				p_pnp_rec,
	IN				ib_qp_type_t				qp_type );

ib_api_status_t
create_spl_qp_svc(
	IN				ib_pnp_port_rec_t*			p_pnp_rec,
	IN		const	ib_qp_type_t				qp_type );

void
destroying_spl_qp_svc(
	IN				al_obj_t*					p_obj );

void
free_spl_qp_svc(
	IN				al_obj_t*					p_obj );

void
spl_qp_svc_lid_change(
	IN				al_obj_t*					p_obj,
	IN				ib_pnp_port_rec_t*			p_pnp_rec );

ib_api_status_t
remote_mad_send(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr );

static ib_api_status_t
local_mad_send(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr );

static ib_api_status_t
loopback_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr );

static ib_api_status_t
__process_subn_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr );

static ib_api_status_t
fwd_local_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr );

void
send_local_mad_cb(
	IN				cl_async_proc_item_t*		p_item );

void
spl_qp_send_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

#ifdef NTDDI_WIN8
KDEFERRED_ROUTINE spl_qp_send_dpc_cb;
#else
void
spl_qp_send_dpc_cb(
    IN              KDPC                        *p_dpc,
    IN              void                        *context,
    IN              void                        *arg1,
    IN              void                        *arg2
    );
#endif

#ifdef NTDDI_WIN8
KDEFERRED_ROUTINE spl_qp_recv_dpc_cb;
#else
void
spl_qp_recv_dpc_cb(
    IN              KDPC                        *p_dpc,
    IN              void                        *context,
    IN              void                        *arg1,
    IN              void                        *arg2
    );
#endif

void
spl_qp_recv_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

void
spl_qp_comp(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN		const	ib_cq_handle_t				h_cq,
	IN				ib_wc_type_t				wc_type );

ib_api_status_t
process_mad_recv(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				ib_mad_element_t*			p_mad_element );

static mad_route_t
route_recv_smp(
	IN				ib_mad_element_t*			p_mad_element );

static mad_route_t
route_recv_smp_attr(
	IN				ib_mad_element_t*			p_mad_element );

mad_route_t
route_recv_dm_mad(
	IN				ib_mad_element_t*			p_mad_element );

static mad_route_t
route_recv_bm(
	IN				ib_mad_element_t*			p_mad_element );

static mad_route_t
route_recv_perf(
	IN				ib_mad_element_t*			p_mad_element );

static mad_route_t
route_recv_vendor(
	IN				ib_mad_element_t*			p_mad_element );

ib_api_status_t
forward_sm_trap(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				ib_mad_element_t*			p_mad_element );

ib_api_status_t
recv_local_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				ib_mad_element_t*			p_mad_request );

void
spl_qp_alias_send_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element );

void
spl_qp_alias_recv_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_response );

static ib_api_status_t
spl_qp_svc_post_recvs(
	IN				spl_qp_svc_t*	const		p_spl_qp_svc );

void
spl_qp_svc_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec );

void
spl_qp_alias_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec );

void
spl_qp_svc_reset(
	IN				spl_qp_svc_t*				p_spl_qp_svc );

void
spl_qp_svc_reset_cb(
	IN				cl_async_proc_item_t*		p_item );

ib_api_status_t
acquire_svc_disp(
	IN		const	cl_qmap_t* const			p_svc_map,
	IN		const	ib_net64_t					port_guid,
		OUT			al_mad_disp_handle_t		*ph_mad_disp );

void
smi_poll_timer_cb(
	IN				void*						context );

void
smi_post_recvs(
	IN				cl_list_item_t*	const		p_list_item,
	IN				void*						context );

#if defined( CL_USE_MUTEX )
void
spl_qp_send_async_cb(
	IN				cl_async_proc_item_t*		p_item );

void
spl_qp_recv_async_cb(
	IN				cl_async_proc_item_t*		p_item );
#endif

/*
 * Create the special QP manager.
 */
ib_api_status_t
create_spl_qp_mgr(
	IN				al_obj_t*	const			p_parent_obj )
{
	ib_pnp_req_t			pnp_req;
	ib_api_status_t			status;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_parent_obj );
	CL_ASSERT( !gp_spl_qp_mgr );

	gp_spl_qp_mgr = cl_zalloc( sizeof( spl_qp_mgr_t ) );
	if( !gp_spl_qp_mgr )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("IB_INSUFFICIENT_MEMORY\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the special QP manager. */
	construct_al_obj( &gp_spl_qp_mgr->obj, AL_OBJ_TYPE_SMI );
	cl_timer_construct( &gp_spl_qp_mgr->poll_timer );

	/* Initialize the lists. */
	cl_qmap_init( &gp_spl_qp_mgr->smi_map );
	cl_qmap_init( &gp_spl_qp_mgr->gsi_map );

	/* Initialize the global SMI/GSI manager object. */
	status = init_al_obj( &gp_spl_qp_mgr->obj, gp_spl_qp_mgr, TRUE,
		destroying_spl_qp_mgr, NULL, free_spl_qp_mgr );
	if( status != IB_SUCCESS )
	{
		free_spl_qp_mgr( &gp_spl_qp_mgr->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/* Attach the special QP manager to the parent object. */
	status = attach_al_obj( p_parent_obj, &gp_spl_qp_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_spl_qp_mgr->obj.pfn_destroy( &gp_spl_qp_mgr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Initialize the SMI polling timer. */
	cl_status = cl_timer_init( &gp_spl_qp_mgr->poll_timer, smi_poll_timer_cb,
		gp_spl_qp_mgr );
	if( cl_status != CL_SUCCESS )
	{
		gp_spl_qp_mgr->obj.pfn_destroy( &gp_spl_qp_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_timer_init failed, status 0x%x\n", cl_status ) );
		return ib_convert_cl_status( cl_status );
	}

	/*
	 * Note: PnP registrations for port events must be done
	 * when the special QP manager is created.  This ensures that
	 * the registrations are listed sequentially and the reporting
	 * of PnP events occurs in the proper order.
	 */

	/*
	 * Separate context is needed for each special QP.  Therefore, a
	 * separate PnP event registration is performed for QP0 and QP1.
	 */

	/* Register for port PnP events for QP0. */
	cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
	pnp_req.pnp_class	= IB_PNP_PORT;
	pnp_req.pnp_context = &gp_spl_qp_mgr->obj;
	pnp_req.pfn_pnp_cb	= spl_qp0_agent_pnp_cb;

	status = ib_reg_pnp( gh_al, &pnp_req, &gp_spl_qp_mgr->h_qp0_pnp );

	if( status != IB_SUCCESS )
	{
		gp_spl_qp_mgr->obj.pfn_destroy( &gp_spl_qp_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_reg_pnp QP0 failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/* Reference the special QP manager on behalf of the ib_reg_pnp call. */
	ref_al_obj( &gp_spl_qp_mgr->obj );

	/* Register for port PnP events for QP1. */
	cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
	pnp_req.pnp_class	= IB_PNP_PORT;
	pnp_req.pnp_context = &gp_spl_qp_mgr->obj;
	pnp_req.pfn_pnp_cb	= spl_qp1_agent_pnp_cb;

	status = ib_reg_pnp( gh_al, &pnp_req, &gp_spl_qp_mgr->h_qp1_pnp );

	if( status != IB_SUCCESS )
	{
		gp_spl_qp_mgr->obj.pfn_destroy( &gp_spl_qp_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_reg_pnp QP1 failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/*
	 * Note that we don't release the referende taken in init_al_obj
	 * because we need one on behalf of the ib_reg_pnp call.
	 */

	AL_EXIT( AL_DBG_SMI );
	return IB_SUCCESS;
}



/*
 * Pre-destroy the special QP manager.
 */
void
destroying_spl_qp_mgr(
	IN				al_obj_t*					p_obj )
{
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	CL_ASSERT( gp_spl_qp_mgr == PARENT_STRUCT( p_obj, spl_qp_mgr_t, obj ) );
	UNUSED_PARAM( p_obj );

	/* Deregister for port PnP events for QP0. */
	if( gp_spl_qp_mgr->h_qp0_pnp )
	{
		status = ib_dereg_pnp( gp_spl_qp_mgr->h_qp0_pnp,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}

	/* Deregister for port PnP events for QP1. */
	if( gp_spl_qp_mgr->h_qp1_pnp )
	{
		status = ib_dereg_pnp( gp_spl_qp_mgr->h_qp1_pnp,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}

	/* Destroy the SMI polling timer. */
	cl_timer_destroy( &gp_spl_qp_mgr->poll_timer );
}



/*
 * Free the special QP manager.
 */
void
free_spl_qp_mgr(
	IN				al_obj_t*					p_obj )
{
	CL_ASSERT( p_obj );
	CL_ASSERT( gp_spl_qp_mgr == PARENT_STRUCT( p_obj, spl_qp_mgr_t, obj ) );
	UNUSED_PARAM( p_obj );

	destroy_al_obj( &gp_spl_qp_mgr->obj );
	cl_free( gp_spl_qp_mgr );
	gp_spl_qp_mgr = NULL;
}



/*
 * Special QP0 agent PnP event callback.
 */
ib_api_status_t
spl_qp0_agent_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec )
{
	ib_api_status_t	status;
	AL_ENTER( AL_DBG_SMI );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );

	status = spl_qp_agent_pnp( p_pnp_rec, IB_QPT_QP0 );

	AL_EXIT( AL_DBG_SMI );
	return status;
}



/*
 * Special QP1 agent PnP event callback.
 */
ib_api_status_t
spl_qp1_agent_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec )
{
	ib_api_status_t	status;
	AL_ENTER( AL_DBG_SMI );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );

	status = spl_qp_agent_pnp( p_pnp_rec, IB_QPT_QP1 );

	AL_EXIT( AL_DBG_SMI );
	return status;
}



/*
 * Special QP agent PnP event callback.
 */
ib_api_status_t
spl_qp_agent_pnp(
	IN				ib_pnp_rec_t*				p_pnp_rec,
	IN				ib_qp_type_t				qp_type )
{
	ib_api_status_t			status;
	al_obj_t*				p_obj;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_pnp_rec );
	p_obj = p_pnp_rec->context;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_SMI,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );
	/* Dispatch based on the PnP event type. */
	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_PORT_ADD:
		CL_ASSERT( !p_obj );
		status = create_spl_qp_svc( (ib_pnp_port_rec_t*)p_pnp_rec, qp_type );
		break;

	case IB_PNP_PORT_REMOVE:
		CL_ASSERT( p_obj );
		ref_al_obj( p_obj );
		p_obj->pfn_destroy( p_obj, NULL );
		status = IB_SUCCESS;
		break;

	case IB_PNP_LID_CHANGE:
		CL_ASSERT( p_obj );
		spl_qp_svc_lid_change( p_obj, (ib_pnp_port_rec_t*)p_pnp_rec );
		status = IB_SUCCESS;
		break;

	default:
		/* All other events are ignored. */
		status = IB_SUCCESS;
		break;
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}



/*
 * Create a special QP service.
 */
ib_api_status_t
create_spl_qp_svc(
	IN				ib_pnp_port_rec_t*			p_pnp_rec,
	IN		const	ib_qp_type_t				qp_type )
{
	cl_status_t				cl_status;
	spl_qp_svc_t*			p_spl_qp_svc;
	ib_ca_handle_t			h_ca;
	ib_cq_create_t			cq_create;
	ib_qp_create_t			qp_create;
	ib_qp_attr_t			qp_attr;
	ib_mad_svc_t			mad_svc;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_pnp_rec );

	if( ( qp_type != IB_QPT_QP0 ) && ( qp_type != IB_QPT_QP1 ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	CL_ASSERT( p_pnp_rec->pnp_rec.pnp_context );
	CL_ASSERT( p_pnp_rec->p_ca_attr );
	CL_ASSERT( p_pnp_rec->p_port_attr );

	p_spl_qp_svc = cl_zalloc( sizeof( spl_qp_svc_t ) );
	if( !p_spl_qp_svc )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("IB_INSUFFICIENT_MEMORY\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Tie the special QP service to the port by setting the port number. */
	p_spl_qp_svc->port_num = p_pnp_rec->p_port_attr->port_num;
	/* Store the port GUID to allow faster lookups of the dispatchers. */
	p_spl_qp_svc->port_guid = p_pnp_rec->p_port_attr->port_guid;

	/* Initialize the send and receive queues. */
	cl_qlist_init( &p_spl_qp_svc->send_queue );
	cl_qlist_init( &p_spl_qp_svc->recv_queue );
	cl_spinlock_init(&p_spl_qp_svc->cache_lock);

    /* Initialize the DPCs. */
    KeInitializeDpc( &p_spl_qp_svc->send_dpc, spl_qp_send_dpc_cb, p_spl_qp_svc );
    KeInitializeDpc( &p_spl_qp_svc->recv_dpc, spl_qp_recv_dpc_cb, p_spl_qp_svc );

    if( qp_type == IB_QPT_QP0 )
    {
        KeSetImportanceDpc( &p_spl_qp_svc->send_dpc, HighImportance );
        KeSetImportanceDpc( &p_spl_qp_svc->recv_dpc, HighImportance );
    }

#if defined( CL_USE_MUTEX )
	/* Initialize async callbacks and flags for send/receive processing. */
	p_spl_qp_svc->send_async_queued = FALSE;
	p_spl_qp_svc->send_async_cb.pfn_callback = spl_qp_send_async_cb;
	p_spl_qp_svc->recv_async_queued = FALSE;
	p_spl_qp_svc->recv_async_cb.pfn_callback = spl_qp_recv_async_cb;
#endif

	/* Initialize the async callback function to process local sends. */
	p_spl_qp_svc->send_async.pfn_callback = send_local_mad_cb;

	/* Initialize the async callback function to reset the QP on error. */
	p_spl_qp_svc->reset_async.pfn_callback = spl_qp_svc_reset_cb;

	/* Construct the special QP service object. */
	construct_al_obj( &p_spl_qp_svc->obj, AL_OBJ_TYPE_SMI );

	/* Initialize the special QP service object. */
	status = init_al_obj( &p_spl_qp_svc->obj, p_spl_qp_svc, TRUE,
		destroying_spl_qp_svc, NULL, free_spl_qp_svc );
	if( status != IB_SUCCESS )
	{
		free_spl_qp_svc( &p_spl_qp_svc->obj );
		return status;
	}

	/* Attach the special QP service to the parent object. */
	status = attach_al_obj(
		(al_obj_t*)p_pnp_rec->pnp_rec.pnp_context, &p_spl_qp_svc->obj );
	if( status != IB_SUCCESS )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	h_ca = acquire_ca( p_pnp_rec->p_ca_attr->ca_guid );
	CL_ASSERT( h_ca );
	if( !h_ca )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("acquire_ca failed.\n") );
		return IB_INVALID_GUID;
	}

	p_spl_qp_svc->obj.p_ci_ca = h_ca->obj.p_ci_ca;

	/* Determine the maximum queue depth of the QP and CQs. */
	p_spl_qp_svc->max_qp_depth =
		( p_pnp_rec->p_ca_attr->max_wrs <
		p_pnp_rec->p_ca_attr->max_cqes ) ?
		p_pnp_rec->p_ca_attr->max_wrs :
		p_pnp_rec->p_ca_attr->max_cqes;

	/* Compare this maximum to the default special queue depth. */
	if( ( qp_type == IB_QPT_QP0 ) &&
		( p_spl_qp_svc->max_qp_depth > DEFAULT_QP0_DEPTH ) )
		  p_spl_qp_svc->max_qp_depth = DEFAULT_QP0_DEPTH;
	if( ( qp_type == IB_QPT_QP1 ) &&
		( p_spl_qp_svc->max_qp_depth > DEFAULT_QP1_DEPTH ) )
		  p_spl_qp_svc->max_qp_depth = DEFAULT_QP1_DEPTH;

	/* Create the send CQ. */
	cl_memclr( &cq_create, sizeof( ib_cq_create_t ) );
	cq_create.size = p_spl_qp_svc->max_qp_depth;
	cq_create.pfn_comp_cb = spl_qp_send_comp_cb;

	status = ib_create_cq( p_spl_qp_svc->obj.p_ci_ca->h_ca, &cq_create,
		p_spl_qp_svc, spl_qp_svc_event_cb, &p_spl_qp_svc->h_send_cq );

	if( status != IB_SUCCESS )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_create_cq send CQ failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/* Reference the special QP service on behalf of ib_create_cq. */
	ref_al_obj( &p_spl_qp_svc->obj );

	/* Check the result of the creation request. */
	if( cq_create.size < p_spl_qp_svc->max_qp_depth )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_create_cq allocated insufficient send CQ size\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Create the receive CQ. */
	cl_memclr( &cq_create, sizeof( ib_cq_create_t ) );
	cq_create.size = p_spl_qp_svc->max_qp_depth;
	cq_create.pfn_comp_cb = spl_qp_recv_comp_cb;

	status = ib_create_cq( p_spl_qp_svc->obj.p_ci_ca->h_ca, &cq_create,
		p_spl_qp_svc, spl_qp_svc_event_cb, &p_spl_qp_svc->h_recv_cq );

	if( status != IB_SUCCESS )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_create_cq recv CQ failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/* Reference the special QP service on behalf of ib_create_cq. */
	ref_al_obj( &p_spl_qp_svc->obj );

	/* Check the result of the creation request. */
	if( cq_create.size < p_spl_qp_svc->max_qp_depth )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_create_cq allocated insufficient recv CQ size\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Create the special QP. */
	cl_memclr( &qp_create, sizeof( ib_qp_create_t ) );
	qp_create.qp_type = qp_type;
	qp_create.sq_depth = p_spl_qp_svc->max_qp_depth;
	qp_create.rq_depth = p_spl_qp_svc->max_qp_depth;
	qp_create.sq_sge = 3;	/* Three entries are required for segmentation. */
	qp_create.rq_sge = 1;
	qp_create.h_sq_cq = p_spl_qp_svc->h_send_cq;
	qp_create.h_rq_cq = p_spl_qp_svc->h_recv_cq;
	qp_create.sq_signaled = TRUE;

	status = ib_get_spl_qp( p_spl_qp_svc->obj.p_ci_ca->h_pd,
		p_pnp_rec->p_port_attr->port_guid, &qp_create,
		p_spl_qp_svc, spl_qp_svc_event_cb, NULL, &p_spl_qp_svc->h_qp );

	if( status != IB_SUCCESS )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_get_spl_qp failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/* Reference the special QP service on behalf of ib_get_spl_qp. */
	ref_al_obj( &p_spl_qp_svc->obj );

	/* Check the result of the creation request. */
	status = ib_query_qp( p_spl_qp_svc->h_qp, &qp_attr );
	if( status != IB_SUCCESS )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_query_qp failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	if( ( qp_attr.rq_depth < p_spl_qp_svc->max_qp_depth ) ||
		( qp_attr.sq_depth < p_spl_qp_svc->max_qp_depth ) ||
		( qp_attr.sq_sge < 3 ) || ( qp_attr.rq_sge < 1 ) )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_get_spl_qp allocated attributes are insufficient\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Initialize the QP for use. */
	status = ib_init_dgrm_svc( p_spl_qp_svc->h_qp, NULL );
	if( status != IB_SUCCESS )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_init_dgrm_svc failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/* Post receive buffers. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	status = spl_qp_svc_post_recvs( p_spl_qp_svc );
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );
	if( status != IB_SUCCESS )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("spl_qp_svc_post_recvs failed, %s\n",
			ib_get_err_str( status ) ) );
		return status;
	}

	/* Create the MAD dispatcher. */
	status = create_mad_disp( &p_spl_qp_svc->obj, p_spl_qp_svc->h_qp,
		&p_spl_qp_svc->h_mad_disp );
	if( status != IB_SUCCESS )
	{
		p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("create_mad_disp failed, %s\n", ib_get_err_str( status ) ) );
		return status;
	}

	/*
	 * Add this service to the special QP manager lookup lists.
	 * The service must be added to allow the creation of a QP alias.
	 */
	cl_spinlock_acquire( &gp_spl_qp_mgr->obj.lock );
	if( qp_type == IB_QPT_QP0 )
	{
		cl_qmap_insert( &gp_spl_qp_mgr->smi_map, p_spl_qp_svc->port_guid,
			&p_spl_qp_svc->map_item );
	}
	else
	{
		cl_qmap_insert( &gp_spl_qp_mgr->gsi_map, p_spl_qp_svc->port_guid,
			&p_spl_qp_svc->map_item );
	}
	cl_spinlock_release( &gp_spl_qp_mgr->obj.lock );

	/*
	 * If the CA does not support HW agents, create a QP alias and register
	 * a MAD service for sending responses from the local MAD interface.
	 */
	if( check_local_mad( p_spl_qp_svc->h_qp ) )
	{
		/* Create a QP alias. */
		cl_memclr( &qp_create, sizeof( ib_qp_create_t ) );
		qp_create.qp_type =
			( qp_type == IB_QPT_QP0 ) ? IB_QPT_QP0_ALIAS : IB_QPT_QP1_ALIAS;
		qp_create.sq_depth		= p_spl_qp_svc->max_qp_depth;
		qp_create.sq_sge		= 1;
		qp_create.sq_signaled	= TRUE;

		status = ib_get_spl_qp( p_spl_qp_svc->obj.p_ci_ca->h_pd_alias,
			p_pnp_rec->p_port_attr->port_guid, &qp_create,
			p_spl_qp_svc, spl_qp_alias_event_cb, &p_spl_qp_svc->pool_key,
			&p_spl_qp_svc->h_qp_alias );

		if (status != IB_SUCCESS)
		{
			p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_get_spl_qp alias failed, %s\n",
				ib_get_err_str( status ) ) );
			return status;
		}

		/* Reference the special QP service on behalf of ib_get_spl_qp. */
		ref_al_obj( &p_spl_qp_svc->obj );

		/* Register a MAD service for sends. */
		cl_memclr( &mad_svc, sizeof( ib_mad_svc_t ) );
		mad_svc.mad_svc_context = p_spl_qp_svc;
		mad_svc.pfn_mad_send_cb = spl_qp_alias_send_cb;
		mad_svc.pfn_mad_recv_cb = spl_qp_alias_recv_cb;

		status = ib_reg_mad_svc( p_spl_qp_svc->h_qp_alias, &mad_svc,
			&p_spl_qp_svc->h_mad_svc );

		if( status != IB_SUCCESS )
		{
			p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_reg_mad_svc failed, %s\n", ib_get_err_str( status ) ) );
			return status;
		}
	}

	/* Set the context of the PnP event to this child object. */
	p_pnp_rec->pnp_rec.context = &p_spl_qp_svc->obj;

	/* The QP is ready.  Change the state. */
	p_spl_qp_svc->state = SPL_QP_ACTIVE;

	/* Force a completion callback to rearm the CQs. */
	spl_qp_recv_comp_cb( p_spl_qp_svc->h_recv_cq, p_spl_qp_svc );
	spl_qp_send_comp_cb( p_spl_qp_svc->h_send_cq, p_spl_qp_svc );

	/* Start the polling thread timer. */
	if( g_smi_poll_interval )
	{
		cl_status =
			cl_timer_trim( &gp_spl_qp_mgr->poll_timer, g_smi_poll_interval );

		if( cl_status != CL_SUCCESS )
		{
			p_spl_qp_svc->obj.pfn_destroy( &p_spl_qp_svc->obj, NULL );
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("cl_timer_start failed, status 0x%x\n", cl_status ) );
			return ib_convert_cl_status( cl_status );
		}
	}

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_spl_qp_svc->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_SMI );
	return IB_SUCCESS;
}



/*
 * Return a work completion to the MAD dispatcher for the specified MAD.
 */
static void
__complete_send_mad(
	IN		const	al_mad_disp_handle_t		h_mad_disp,
	IN				al_mad_wr_t* const			p_mad_wr,
	IN		const	ib_wc_status_t				wc_status )
{
	ib_wc_t			wc;

	/* Construct a send work completion. */
	cl_memclr( &wc, sizeof( ib_wc_t ) );
	if (p_mad_wr) {
		// Handling the special race where p_mad_wr that comes from spl_qp can be NULL
		wc.wr_id	= p_mad_wr->send_wr.wr_id;
	}
	wc.wc_type	= IB_WC_SEND;
	wc.status	= wc_status;

	/* Set the send size if we were successful with the send. */
	if( wc_status == IB_WCS_SUCCESS )
		wc.length = MAD_BLOCK_SIZE;

	mad_disp_send_done( h_mad_disp, p_mad_wr, &wc );
}



/*
 * Pre-destroy a special QP service.
 */
void
destroying_spl_qp_svc(
	IN				al_obj_t*					p_obj )
{
	spl_qp_svc_t*			p_spl_qp_svc;
	cl_list_item_t*			p_list_item;
	al_mad_wr_t*			p_mad_wr;

	ib_api_status_t			status;
	KIRQL irql;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_obj );
	p_spl_qp_svc = PARENT_STRUCT( p_obj, spl_qp_svc_t, obj );

	/* Change the state to prevent processing new send requests. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	p_spl_qp_svc->state = SPL_QP_DESTROYING;
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	/* Wait here until the special QP service is no longer in use. */
	while( p_spl_qp_svc->in_use_cnt )
	{
		cl_thread_suspend( 0 );
	}

	/* Destroy the special QP. */
	if( p_spl_qp_svc->h_qp )
	{
		/* If present, remove the special QP service from the tracking map. */
		cl_spinlock_acquire( &gp_spl_qp_mgr->obj.lock );
		if( p_spl_qp_svc->h_qp->type == IB_QPT_QP0 )
		{
			cl_qmap_remove( &gp_spl_qp_mgr->smi_map, p_spl_qp_svc->port_guid );
		}
		else
		{
			cl_qmap_remove( &gp_spl_qp_mgr->gsi_map, p_spl_qp_svc->port_guid );
		}
		cl_spinlock_release( &gp_spl_qp_mgr->obj.lock );

		status = ib_destroy_qp( p_spl_qp_svc->h_qp,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );

		irql = KeRaiseIrqlToDpcLevel();
		cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );

		/* Complete any outstanding MAD sends operations as "flushed". */
		for( p_list_item = cl_qlist_remove_head( &p_spl_qp_svc->send_queue );
			 p_list_item != cl_qlist_end( &p_spl_qp_svc->send_queue );
			 p_list_item = cl_qlist_remove_head( &p_spl_qp_svc->send_queue ) )
		{
			cl_spinlock_release( &p_spl_qp_svc->obj.lock );
			p_mad_wr = PARENT_STRUCT( p_list_item, al_mad_wr_t, list_item );
			__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr,
				IB_WCS_WR_FLUSHED_ERR );
			cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
		}

		cl_spinlock_release( &p_spl_qp_svc->obj.lock );
		KeLowerIrql(irql);
		/* Receive MAD elements are returned to the pool by the free routine. */
	}

	/* Destroy the special QP alias and CQs. */
	if( p_spl_qp_svc->h_qp_alias )
	{
		status = ib_destroy_qp( p_spl_qp_svc->h_qp_alias,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}
	if( p_spl_qp_svc->h_send_cq )
	{
		status = ib_destroy_cq( p_spl_qp_svc->h_send_cq,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}
	if( p_spl_qp_svc->h_recv_cq )
	{
		status = ib_destroy_cq( p_spl_qp_svc->h_recv_cq,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Free a special QP service.
 */
void
free_spl_qp_svc(
	IN				al_obj_t*					p_obj )
{
	spl_qp_svc_t*			p_spl_qp_svc;
	cl_list_item_t*			p_list_item;
	al_mad_element_t*		p_al_mad;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_obj );
	p_spl_qp_svc = PARENT_STRUCT( p_obj, spl_qp_svc_t, obj );

	/* Dereference the CA. */
	if( p_spl_qp_svc->obj.p_ci_ca )
		deref_al_obj( &p_spl_qp_svc->obj.p_ci_ca->h_ca->obj );

	/* Return receive MAD elements to the pool. */
	for( p_list_item = cl_qlist_remove_head( &p_spl_qp_svc->recv_queue );
		 p_list_item != cl_qlist_end( &p_spl_qp_svc->recv_queue );
		 p_list_item = cl_qlist_remove_head( &p_spl_qp_svc->recv_queue ) )
	{
		p_al_mad = PARENT_STRUCT( p_list_item, al_mad_element_t, list_item );

		status = ib_put_mad( &p_al_mad->element );
		CL_ASSERT( status == IB_SUCCESS );
	}

	CL_ASSERT( cl_is_qlist_empty( &p_spl_qp_svc->send_queue ) );

	destroy_al_obj( &p_spl_qp_svc->obj );
	cl_free( p_spl_qp_svc );

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Update the base LID of a special QP service.
 */
void
spl_qp_svc_lid_change(
	IN				al_obj_t*					p_obj,
	IN				ib_pnp_port_rec_t*			p_pnp_rec )
{
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_obj );
	CL_ASSERT( p_pnp_rec );
	CL_ASSERT( p_pnp_rec->p_port_attr );

	p_spl_qp_svc = PARENT_STRUCT( p_obj, spl_qp_svc_t, obj );

	p_spl_qp_svc->base_lid = p_pnp_rec->p_port_attr->lid;
	p_spl_qp_svc->lmc = p_pnp_rec->p_port_attr->lmc;

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Route a send work request.
 */
mad_route_t
route_mad_send(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				ib_send_wr_t* const			p_send_wr )
{
	al_mad_wr_t*			p_mad_wr;
	al_mad_send_t*			p_mad_send;
	ib_mad_t*				p_mad;
	ib_smp_t*				p_smp;
	ib_av_handle_t			h_av;
	mad_route_t				route;
	boolean_t				local, loopback, discard;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_send_wr );

	/* Initialize a pointers to the MAD work request and the MAD. */
	p_mad_wr = PARENT_STRUCT( p_send_wr, al_mad_wr_t, send_wr );
	p_mad_send = PARENT_STRUCT( p_mad_wr, al_mad_send_t, mad_wr );
	p_mad = get_mad_hdr_from_wr( p_mad_wr );
	p_smp = (ib_smp_t*)p_mad;

	/* Check if the CA has a local MAD interface. */
	local = loopback = discard = FALSE;
	if( check_local_mad( p_spl_qp_svc->h_qp ) )
	{
		/*
		 * If the MAD is a locally addressed Subnet Management, Performance
		 * Management, or Connection Management datagram, process the work
		 * request locally.
		 */
		h_av = p_send_wr->dgrm.ud.h_av;
		switch( p_mad->mgmt_class )
		{
		case IB_MCLASS_SUBN_DIR:
			/* Perform special checks on directed route SMPs. */
			if( ib_smp_is_response( p_smp ) ) //TODO ib_mad_is_response( p_mad );
			{
				/*
				 * This node is the originator of the response.  Discard
				 * if the hop count or pointer is zero, an intermediate hop,
				 * out of bounds hop, or if the first port of the directed
				 * route retrun path is not this port.
				 */
				if( ( p_smp->hop_count == 0 ) || ( p_smp->hop_ptr == 0 ) )
				{
					AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
						("hop cnt or hop ptr set to 0...discarding\n") );
					discard = TRUE;
				}
				else if( p_smp->hop_count != ( p_smp->hop_ptr - 1 ) )
				{
					AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
						("hop cnt != (hop ptr - 1)...discarding\n") );
					discard = TRUE;
				}
				else if( p_smp->hop_count >= IB_SUBNET_PATH_HOPS_MAX )
				{
					AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
						("hop cnt > max hops...discarding\n") );
					discard = TRUE;
				}
				else if( ( p_smp->dr_dlid == IB_LID_PERMISSIVE ) &&
						 ( p_smp->return_path[ p_smp->hop_ptr - 1 ] !=
							p_spl_qp_svc->port_num ) )
				{
					AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
						("return path[hop ptr - 1] != port num...discarding\n") );
					discard = TRUE;
				}
			}
			else
			{
				/* The SMP is a request. */
				if( ( p_smp->hop_count >= IB_SUBNET_PATH_HOPS_MAX ) ||
					( p_smp->hop_ptr >= IB_SUBNET_PATH_HOPS_MAX ) )
				{
					discard = TRUE;
				}
				else if( ( p_smp->hop_count == 0 ) && ( p_smp->hop_ptr == 0 ) )
				{
					/* Self Addressed: Sent locally, routed locally. */
					local = TRUE;
					discard = ( p_smp->dr_slid != IB_LID_PERMISSIVE ) ||
							  ( p_smp->dr_dlid != IB_LID_PERMISSIVE );
				}
				else if( ( p_smp->hop_count != 0 ) &&
						 ( p_smp->hop_count == ( p_smp->hop_ptr - 1 ) ) )
				{
					/* End of Path: Sent remotely, routed locally. */
					local = TRUE;
				}
				else if( ( p_smp->hop_count != 0 ) &&
						 ( p_smp->hop_ptr	== 0 ) )
				{
					/* Beginning of Path: Sent locally, routed remotely. */
					if( p_smp->dr_slid == IB_LID_PERMISSIVE )
					{
						discard =
							( p_smp->initial_path[ p_smp->hop_ptr + 1 ] !=
							  p_spl_qp_svc->port_num );
					}
				}
				else
				{
					/* Intermediate hop. */
					discard = TRUE;
				}
			}
			/* Loopback locally addressed SM to SM "heartbeat" messages. */
			loopback = (p_mad->attr_id == IB_MAD_ATTR_SM_INFO);
			break;

		case IB_MCLASS_SUBN_LID:
			/* Loopback locally addressed SM to SM "heartbeat" messages. */
			loopback = (p_mad->attr_id == IB_MAD_ATTR_SM_INFO);

			/* Fall through to check for a local MAD. */

		case IB_MCLASS_PERF:
		case IB_MCLASS_BM:
		case IB_MLX_VENDOR_CLASS1:
		case IB_MLX_VENDOR_CLASS2:
			local = !(p_mad->method & IB_MAD_METHOD_RESP_MASK) && ( h_av &&
				( h_av->av_attr.dlid ==
				( h_av->av_attr.path_bits | p_spl_qp_svc->base_lid ) ) );
			break;
		}
	}

	route = ( p_mad_send->p_send_mad->send_opt & IB_SEND_OPT_LOCAL ) ?
		ROUTE_LOCAL : ROUTE_REMOTE;
	if( local ) route = ROUTE_LOCAL;
	if( loopback && local ) route = ROUTE_LOOPBACK;
	if( discard ) route = ROUTE_DISCARD;

	AL_EXIT( AL_DBG_SMI );
	return route;
}



/*
 * Send a work request on the special QP.
 */
ib_api_status_t
spl_qp_svc_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t* const			p_send_wr )
{
	spl_qp_svc_t*			p_spl_qp_svc;
	al_mad_wr_t*			p_mad_wr;
	mad_route_t				route;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( h_qp );
	CL_ASSERT( p_send_wr );

	/* Get the special QP service. */
	p_spl_qp_svc = (spl_qp_svc_t*)h_qp->obj.context;
	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_spl_qp_svc->h_qp == h_qp );

	/* Determine how to route the MAD. */
	route = route_mad_send( p_spl_qp_svc, p_send_wr );

	/*
	 * Check the QP state and guard against error handling.  Also,
	 * to maintain proper order of work completions, delay processing
	 * a local MAD until any remote MAD work requests have completed,
	 * and delay processing a remote MAD until local MAD work requests
	 * have completed.
	 */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	if( (p_spl_qp_svc->state != SPL_QP_ACTIVE) || p_spl_qp_svc->local_mad_wr ||
		(is_local(route) && !cl_is_qlist_empty( &p_spl_qp_svc->send_queue )) ||
		( cl_qlist_count( &p_spl_qp_svc->send_queue ) >=
			p_spl_qp_svc->max_qp_depth ) )
	{
		/*
		 * Return busy status.
		 * The special QP will resume sends at this point.
		 */
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );

		AL_EXIT( AL_DBG_SMI );
		return IB_RESOURCE_BUSY;
	}

	p_mad_wr = PARENT_STRUCT( p_send_wr, al_mad_wr_t, send_wr );

	if( is_local( route ) )
	{
		/* Save the local MAD work request for processing. */
		p_spl_qp_svc->local_mad_wr = p_mad_wr;

		/* Flag the service as in use by the asynchronous processing thread. */
		cl_atomic_inc( &p_spl_qp_svc->in_use_cnt );

		cl_spinlock_release( &p_spl_qp_svc->obj.lock );

		status = local_mad_send( p_spl_qp_svc, p_mad_wr );
	}
	else
	{
		/* Process a remote MAD send work request. */
		status = remote_mad_send( p_spl_qp_svc, p_mad_wr );

		cl_spinlock_release( &p_spl_qp_svc->obj.lock );
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}



/*
 * Process a remote MAD send work request.  Called holding the spl_qp_svc lock.
 */
ib_api_status_t
remote_mad_send(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_smp_t*				p_smp;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Initialize a pointers to the MAD work request and outbound MAD. */
	p_smp = (ib_smp_t*)get_mad_hdr_from_wr( p_mad_wr );

	/* Perform outbound MAD processing. */

	/* Adjust directed route SMPs as required by IBA. */
	if( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR )
	{
		if( ib_smp_is_response( p_smp ) )
		{
			if( p_smp->dr_dlid == IB_LID_PERMISSIVE )
				p_smp->hop_ptr--;
		}
		else if( p_smp->dr_slid == IB_LID_PERMISSIVE )
		{
			/*
			 * Only update the pointer if the hw_agent is not implemented.
			 * Fujitsu implements SMI in hardware, so the following has to
			 * be passed down to the hardware SMI.
			 */
			ci_ca_lock_attr( p_spl_qp_svc->obj.p_ci_ca );
			if( !p_spl_qp_svc->obj.p_ci_ca->p_pnp_attr->hw_agents )
				p_smp->hop_ptr++;
			ci_ca_unlock_attr( p_spl_qp_svc->obj.p_ci_ca );
		}
	}

	/* Always generate send completions. */
	p_mad_wr->send_wr.send_opt |= IB_SEND_OPT_SIGNALED;

	/* Queue the MAD work request on the service tracking queue. */
	cl_qlist_insert_tail( &p_spl_qp_svc->send_queue, &p_mad_wr->list_item );

	status = ib_post_send( p_spl_qp_svc->h_qp, &p_mad_wr->send_wr, NULL );

	if( status != IB_SUCCESS )
	{
		cl_qlist_remove_item( &p_spl_qp_svc->send_queue, &p_mad_wr->list_item );

		/* Reset directed route SMPs as required by IBA. */
		if( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR )
		{
			if( ib_smp_is_response( p_smp ) )
			{
				if( p_smp->dr_dlid == IB_LID_PERMISSIVE )
					p_smp->hop_ptr++;
			}
			else if( p_smp->dr_slid == IB_LID_PERMISSIVE )
			{
				/* Only update if the hw_agent is not implemented. */
				ci_ca_lock_attr( p_spl_qp_svc->obj.p_ci_ca );
				if( p_spl_qp_svc->obj.p_ci_ca->p_pnp_attr->hw_agents == FALSE )
					p_smp->hop_ptr--;
				ci_ca_unlock_attr( p_spl_qp_svc->obj.p_ci_ca );
			}
		}
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}


/*
 * Handle a MAD destined for the local CA, using cached data
 * as much as possible.
 */
static ib_api_status_t
local_mad_send(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	mad_route_t				route;
	ib_api_status_t			status = IB_SUCCESS;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Determine how to route the MAD. */
	route = route_mad_send( p_spl_qp_svc, &p_mad_wr->send_wr );

	/* Check if this MAD should be discarded. */
	if( is_discard( route ) )
	{
		/* Deliver a "work completion" to the dispatcher. */
		__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr,
			IB_WCS_LOCAL_OP_ERR );
		status = IB_INVALID_SETTING;
	}
	else if( is_loopback( route ) )
	{
		/* Loopback local SM to SM "heartbeat" messages. */
		status = loopback_mad( p_spl_qp_svc, p_mad_wr );
	}
	else
	{
		switch( get_mad_hdr_from_wr( p_mad_wr )->mgmt_class )
		{
		case IB_MCLASS_SUBN_DIR:
		case IB_MCLASS_SUBN_LID:
			//DO not use the cache in order to force Mkey  check
			status = __process_subn_mad( p_spl_qp_svc, p_mad_wr );
			//status = IB_NOT_DONE;
			break;

		default:
			status = IB_NOT_DONE;
		}
	}

	if( status == IB_NOT_DONE )
	{
		/* Queue an asynchronous processing item to process the local MAD. */
		cl_async_proc_queue( gp_async_proc_mgr, &p_spl_qp_svc->send_async );
	}
	else
	{
		/*
		 * Clear the local MAD pointer to allow processing of other MADs.
		 * This is done after polling for attribute changes to ensure that
		 * subsequent MADs pick up any changes performed by this one.
		 */
		cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
		p_spl_qp_svc->local_mad_wr = NULL;
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );

		/* No longer in use by the asynchronous processing thread. */
		cl_atomic_dec( &p_spl_qp_svc->in_use_cnt );

		/* Special QP operations will resume by unwinding. */
	}

	AL_EXIT( AL_DBG_SMI );
	return IB_SUCCESS;
}


static ib_api_status_t
get_resp_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr,
		OUT			ib_mad_element_t** const	pp_mad_resp )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );
	CL_ASSERT( pp_mad_resp );

	/* Get a MAD element from the pool for the response. */
	status = ib_get_mad( p_spl_qp_svc->h_qp->obj.p_ci_ca->pool_key,
		MAD_BLOCK_SIZE, pp_mad_resp );
	if( status != IB_SUCCESS )
	{
		__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr,
			IB_WCS_LOCAL_OP_ERR );
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}


static ib_api_status_t
complete_local_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr,
	IN				ib_mad_element_t* const		p_mad_resp )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );
	CL_ASSERT( p_mad_resp );

	/* Construct the receive MAD element. */
	p_mad_resp->status		= IB_WCS_SUCCESS;
	p_mad_resp->remote_qp	= p_mad_wr->send_wr.dgrm.ud.remote_qp;
	p_mad_resp->remote_lid	= p_spl_qp_svc->base_lid;
	if( p_mad_wr->send_wr.send_opt & IB_RECV_OPT_IMMEDIATE )
	{
		p_mad_resp->immediate_data = p_mad_wr->send_wr.immediate_data;
		p_mad_resp->recv_opt |= IB_RECV_OPT_IMMEDIATE;
	}

	/*
	 * Hand the receive MAD element to the dispatcher before completing
	 * the send.  This guarantees that the send request cannot time out.
	 */
	status = mad_disp_recv_done( p_spl_qp_svc->h_mad_disp, p_mad_resp );

	/* Forward the send work completion to the dispatcher. */
	__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr, IB_WCS_SUCCESS );

	AL_EXIT( AL_DBG_SMI );
	return status;
}


static ib_api_status_t
loopback_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_mad_t				*p_mad;
	ib_mad_element_t		*p_mad_resp;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Get a MAD element from the pool for the response. */
	status = get_resp_mad( p_spl_qp_svc, p_mad_wr, &p_mad_resp );
	if( status == IB_SUCCESS )
	{
		/* Initialize a pointers to the MAD work request and outbound MAD. */
		p_mad = get_mad_hdr_from_wr( p_mad_wr );

		/* Simulate a send/receive between local managers. */
		cl_memcpy( p_mad_resp->p_mad_buf, p_mad, MAD_BLOCK_SIZE );

		/* Construct the receive MAD element. */
		p_mad_resp->status		= IB_WCS_SUCCESS;
		p_mad_resp->remote_qp	= p_mad_wr->send_wr.dgrm.ud.remote_qp;
		p_mad_resp->remote_lid	= p_spl_qp_svc->base_lid;
		if( p_mad_wr->send_wr.send_opt & IB_RECV_OPT_IMMEDIATE )
		{
			p_mad_resp->immediate_data = p_mad_wr->send_wr.immediate_data;
			p_mad_resp->recv_opt |= IB_RECV_OPT_IMMEDIATE;
		}

		/*
		 * Hand the receive MAD element to the dispatcher before completing
		 * the send.  This guarantees that the send request cannot time out.
		 */
		status = mad_disp_recv_done( p_spl_qp_svc->h_mad_disp, p_mad_resp );

		/* Forward the send work completion to the dispatcher. */
		__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr, IB_WCS_SUCCESS );

	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}


static void
__update_guid_info(
	IN				spl_qp_cache_t* const			p_cache,
	IN		const	ib_smp_t* const				p_mad )
{
	uint32_t			idx;

	/* Get the table selector from the attribute */
	idx = cl_ntoh32( p_mad->attr_mod );

	/*
	 * We only get successful MADs here, so invalid settings
	 * shouldn't happen.
	 */
	CL_ASSERT( idx <= 31 );

	cl_memcpy( &p_cache->guid_block[idx].tbl,
		ib_smp_get_payload_ptr( p_mad ),
		sizeof(ib_guid_info_t) );
	p_cache->guid_block[idx].valid = TRUE;
}


static  void
__update_pkey_table(
	IN				spl_qp_cache_t* const			p_cache,
	IN		const	ib_smp_t* const				p_mad )
{
	uint16_t			idx;

	/* Get the table selector from the attribute */
	idx = ((uint16_t)cl_ntoh32( p_mad->attr_mod ));

	CL_ASSERT( idx <= 2047 );

	cl_memcpy( &p_cache->pkey_tbl[idx].tbl,
		ib_smp_get_payload_ptr( p_mad ),
		sizeof(ib_pkey_table_t) );
	p_cache->pkey_tbl[idx].valid = TRUE;
}


static void
__update_sl_vl_table(
	IN				spl_qp_cache_t* const			p_cache,
	IN		const	ib_smp_t* const				p_mad )
{
	cl_memcpy( &p_cache->sl_vl.tbl,
		ib_smp_get_payload_ptr( p_mad ),
		sizeof(ib_slvl_table_t) );
	p_cache->sl_vl.valid = TRUE;
}


static void
__update_vl_arb_table(
	IN				spl_qp_cache_t* const			p_cache,
	IN		const	ib_smp_t* const				p_mad )
{
	uint16_t			idx;

	/* Get the table selector from the attribute */
	idx = ((uint16_t)(cl_ntoh32( p_mad->attr_mod ) >> 16)) - 1;

	CL_ASSERT( idx <= 3 );

	cl_memcpy( &p_cache->vl_arb[idx].tbl,
		ib_smp_get_payload_ptr( p_mad ),
		sizeof(ib_vl_arb_table_t) );
	p_cache->vl_arb[idx].valid = TRUE;
}



void
spl_qp_svc_update_cache(
	IN				spl_qp_svc_t				*p_spl_qp_svc,
	IN				ib_smp_t					*p_mad )
{



	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad );
	CL_ASSERT( p_mad->mgmt_class == IB_MCLASS_SUBN_DIR ||
				 p_mad->mgmt_class == IB_MCLASS_SUBN_LID);
	CL_ASSERT(ib_smp_get_status(p_mad) == 0);

	cl_spinlock_acquire(&p_spl_qp_svc->cache_lock);
	
	switch( p_mad->attr_id )
	{
	case IB_MAD_ATTR_GUID_INFO:
		__update_guid_info(
			&p_spl_qp_svc->cache, p_mad );
		break;

	case IB_MAD_ATTR_P_KEY_TABLE:
		__update_pkey_table(
			&p_spl_qp_svc->cache, p_mad );
		break;

	case IB_MAD_ATTR_SLVL_TABLE:
		__update_sl_vl_table(
			&p_spl_qp_svc->cache, p_mad );
		break;

	case IB_MAD_ATTR_VL_ARBITRATION:
		__update_vl_arb_table(
			&p_spl_qp_svc->cache, p_mad );
		break;

	default:
		break;
	}
	
	cl_spinlock_release(&p_spl_qp_svc->cache_lock);
}



static ib_api_status_t
__process_node_info(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_mad_t				*p_mad;
	ib_mad_element_t		*p_mad_resp;
	ib_smp_t				*p_smp;
	ib_node_info_t			*p_node_info;
	ib_ca_attr_t			*p_ca_attr;
	ib_port_attr_t			*p_port_attr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Initialize a pointers to the MAD work request and outbound MAD. */
	p_mad = get_mad_hdr_from_wr( p_mad_wr );
	if( p_mad->method != IB_MAD_METHOD_GET )
	{
		/* Node description is a GET-only attribute. */
		__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr,
			IB_WCS_LOCAL_OP_ERR );
		AL_EXIT( AL_DBG_SMI );
		return IB_INVALID_SETTING;
	}

	/* Get a MAD element from the pool for the response. */
	status = get_resp_mad( p_spl_qp_svc, p_mad_wr, &p_mad_resp );
	if( status == IB_SUCCESS )
	{
		p_smp = (ib_smp_t*)p_mad_resp->p_mad_buf;
		cl_memcpy( p_smp, p_mad, MAD_BLOCK_SIZE );
		p_smp->method = (IB_MAD_METHOD_RESP_MASK | IB_MAD_METHOD_GET);
		if( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR )
			p_smp->status = IB_SMP_DIRECTION;
		else
			p_smp->status = 0;

		p_node_info = (ib_node_info_t*)ib_smp_get_payload_ptr( p_smp );

		/*
		 * Fill in the node info, protecting against the
		 * attributes being changed by PnP.
		 */
		cl_spinlock_acquire( &p_spl_qp_svc->obj.p_ci_ca->obj.lock );

		p_ca_attr = p_spl_qp_svc->obj.p_ci_ca->p_pnp_attr;
		p_port_attr = &p_ca_attr->p_port_attr[p_spl_qp_svc->port_num - 1];

		p_node_info->base_version = 1;
		p_node_info->class_version = 1;
		p_node_info->node_type = IB_NODE_TYPE_CA;
		p_node_info->num_ports = p_ca_attr->num_ports;
		p_node_info->sys_guid = p_ca_attr->system_image_guid;
		p_node_info->node_guid = p_ca_attr->ca_guid;
		p_node_info->port_guid = p_port_attr->port_guid;
		p_node_info->partition_cap = cl_hton16( p_port_attr->num_pkeys );
		p_node_info->device_id = cl_hton16( p_ca_attr->dev_id );
		p_node_info->revision = cl_hton32( p_ca_attr->revision );
		p_node_info->port_num_vendor_id =
			cl_hton32( p_ca_attr->vend_id & 0x00FFFFFF ) | p_port_attr->port_num;
		cl_spinlock_release( &p_spl_qp_svc->obj.p_ci_ca->obj.lock );

		status = complete_local_mad( p_spl_qp_svc, p_mad_wr, p_mad_resp );
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}


static ib_api_status_t
__process_node_desc(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_mad_t				*p_mad;
	ib_mad_element_t		*p_mad_resp;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Initialize a pointers to the MAD work request and outbound MAD. */
	p_mad = get_mad_hdr_from_wr( p_mad_wr );
	if( p_mad->method != IB_MAD_METHOD_GET )
	{
		/* Node info is a GET-only attribute. */
		__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr,
			IB_WCS_LOCAL_OP_ERR );
		AL_EXIT( AL_DBG_SMI );
		return IB_INVALID_SETTING;
	}

	/* Get a MAD element from the pool for the response. */
	status = get_resp_mad( p_spl_qp_svc, p_mad_wr, &p_mad_resp );
	if( status == IB_SUCCESS )
	{
		cl_memcpy( p_mad_resp->p_mad_buf, p_mad, MAD_BLOCK_SIZE );
		p_mad_resp->p_mad_buf->method =
			(IB_MAD_METHOD_RESP_MASK | IB_MAD_METHOD_GET);
		if( p_mad_resp->p_mad_buf->mgmt_class == IB_MCLASS_SUBN_DIR )
			p_mad_resp->p_mad_buf->status = IB_SMP_DIRECTION;
		else
			p_mad_resp->p_mad_buf->status = 0;
		/* Set the node description to the machine name. */
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("__process_node_desc copy into %p node_desc of size %d\n",
				((ib_smp_t*)p_mad_resp->p_mad_buf)->data, sizeof(node_desc)) );
		cl_memcpy( ((ib_smp_t*)p_mad_resp->p_mad_buf)->data, 
			node_desc, sizeof(node_desc) );

		status = complete_local_mad( p_spl_qp_svc, p_mad_wr, p_mad_resp );
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}

static ib_api_status_t
__process_guid_info(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	
	ib_mad_t				*p_mad;
	ib_mad_element_t		*p_mad_resp;
	ib_smp_t				*p_smp;
	ib_guid_info_t			*p_guid_info;
	uint16_t				idx;
	ib_api_status_t		status;


	/* Initialize a pointers to the MAD work request and outbound MAD. */
	p_mad = get_mad_hdr_from_wr( p_mad_wr );

	/* Get the table selector from the attribute */
	idx = ((uint16_t)cl_ntoh32( p_mad->attr_mod ));
	
	/*
	 * TODO : Setup the response to fail the MAD instead of sending
	 * it down to the HCA.
	 */
	if( idx > 31 )
	{
		AL_EXIT( AL_DBG_SMI );
		return IB_NOT_DONE;
	}
	if( !p_spl_qp_svc->cache.guid_block[idx].valid )
	{
		AL_EXIT( AL_DBG_SMI );
		return IB_NOT_DONE;
	}

	/*
	 * If a SET, see if the set is identical to the cache,
	 * in which case it's a no-op.
	 */
	if( p_mad->method == IB_MAD_METHOD_SET )
	{
		if( cl_memcmp( ib_smp_get_payload_ptr( (ib_smp_t*)p_mad ),
			&p_spl_qp_svc->cache.guid_block[idx].tbl, sizeof(ib_guid_info_t) ) )
		{
			/* The set is requesting a change. */
			return IB_NOT_DONE;
		}
	}
	
	/* Get a MAD element from the pool for the response. */
	status = get_resp_mad( p_spl_qp_svc, p_mad_wr, &p_mad_resp );
	if( status == IB_SUCCESS )
	{
		p_smp = (ib_smp_t*)p_mad_resp->p_mad_buf;

		/* Setup the response mad. */
		cl_memcpy( p_smp, p_mad, MAD_BLOCK_SIZE );
		p_smp->method = (IB_MAD_METHOD_RESP_MASK | IB_MAD_METHOD_GET);
		if( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR )
			p_smp->status = IB_SMP_DIRECTION;
		else
			p_smp->status = 0;

		p_guid_info = (ib_guid_info_t*)ib_smp_get_payload_ptr( p_smp );

		// TODO: do we need lock on the cache ?????

		
		/* Copy the cached data. */
		cl_memcpy( p_guid_info,
			&p_spl_qp_svc->cache.guid_block[idx].tbl, sizeof(ib_guid_info_t) );

		status = complete_local_mad( p_spl_qp_svc, p_mad_wr, p_mad_resp );
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}


static ib_api_status_t
__process_pkey_table(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{

	ib_mad_t				*p_mad;
	ib_mad_element_t		*p_mad_resp;
	ib_smp_t				*p_smp;
	ib_pkey_table_t		*p_pkey_table;
	uint16_t				idx;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Initialize a pointers to the MAD work request and outbound MAD. */
	p_mad = get_mad_hdr_from_wr( p_mad_wr );

	/* Get the table selector from the attribute */
	idx = ((uint16_t)cl_ntoh32( p_mad->attr_mod ));
	
	/*
	 * TODO : Setup the response to fail the MAD instead of sending
	 * it down to the HCA.
	 */
	if( idx > 2047 )
	{
		AL_EXIT( AL_DBG_SMI );
		return IB_NOT_DONE;
	}


	if( !p_spl_qp_svc->cache.pkey_tbl[idx].valid )
	{
		AL_EXIT( AL_DBG_SMI );
		return IB_NOT_DONE;
	}

	/*
	 * If a SET, see if the set is identical to the cache,
	 * in which case it's a no-op.
	 */
	if( p_mad->method == IB_MAD_METHOD_SET )
	{
		if( cl_memcmp( ib_smp_get_payload_ptr( (ib_smp_t*)p_mad ),
			&p_spl_qp_svc->cache.pkey_tbl[idx].tbl, sizeof(ib_pkey_table_t) ) )
		{
			/* The set is requesting a change. */
			AL_EXIT( AL_DBG_SMI );
			return IB_NOT_DONE;
		}
	}
	
	/* Get a MAD element from the pool for the response. */
	status = get_resp_mad( p_spl_qp_svc, p_mad_wr, &p_mad_resp );
	if( status == IB_SUCCESS )
	{
		p_smp = (ib_smp_t*)p_mad_resp->p_mad_buf;

		/* Setup the response mad. */
		cl_memcpy( p_smp, p_mad, MAD_BLOCK_SIZE );
		p_smp->method = (IB_MAD_METHOD_RESP_MASK | IB_MAD_METHOD_GET);
		if( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR )
			p_smp->status = IB_SMP_DIRECTION;
		else
			p_smp->status = 0;

		p_pkey_table = (ib_pkey_table_t*)ib_smp_get_payload_ptr( p_smp );

		// TODO: do we need lock on the cache ?????

		
		/* Copy the cached data. */
		cl_memcpy( p_pkey_table,
			&p_spl_qp_svc->cache.pkey_tbl[idx].tbl, sizeof(ib_pkey_table_t) );

		status = complete_local_mad( p_spl_qp_svc, p_mad_wr, p_mad_resp );
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}


static ib_api_status_t
__process_slvl_table(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{


	ib_mad_t				*p_mad;
	ib_mad_element_t		*p_mad_resp;
	ib_smp_t				*p_smp;
	ib_slvl_table_t			*p_slvl_table;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Initialize a pointers to the MAD work request and outbound MAD. */
	p_mad = get_mad_hdr_from_wr( p_mad_wr );

	if( !p_spl_qp_svc->cache.sl_vl.valid )
	{
		AL_EXIT( AL_DBG_SMI );
		return IB_NOT_DONE;
	}

	/*
	 * If a SET, see if the set is identical to the cache,
	 * in which case it's a no-op.
	 */
	if( p_mad->method == IB_MAD_METHOD_SET )
	{
		if( cl_memcmp( ib_smp_get_payload_ptr( (ib_smp_t*)p_mad ),
			&p_spl_qp_svc->cache.sl_vl.tbl, sizeof(ib_slvl_table_t) ) )
		{
			/* The set is requesting a change. */
			AL_EXIT( AL_DBG_SMI );
			return IB_NOT_DONE;
		}
	}
	
	/* Get a MAD element from the pool for the response. */
	status = get_resp_mad( p_spl_qp_svc, p_mad_wr, &p_mad_resp );
	if( status == IB_SUCCESS )
	{
		p_smp = (ib_smp_t*)p_mad_resp->p_mad_buf;

		/* Setup the response mad. */
		cl_memcpy( p_smp, p_mad, MAD_BLOCK_SIZE );
		p_smp->method = (IB_MAD_METHOD_RESP_MASK | IB_MAD_METHOD_GET);
		if( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR )
			p_smp->status = IB_SMP_DIRECTION;
		else
			p_smp->status = 0;

		p_slvl_table = (ib_slvl_table_t*)ib_smp_get_payload_ptr( p_smp );

		// TODO: do we need lock on the cache ?????

		
		/* Copy the cached data. */
		cl_memcpy( p_slvl_table,
			&p_spl_qp_svc->cache.sl_vl.tbl, sizeof(ib_slvl_table_t) );

		status = complete_local_mad( p_spl_qp_svc, p_mad_wr, p_mad_resp );
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}



static ib_api_status_t
__process_vl_arb_table(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{

	ib_mad_t				*p_mad;
	ib_mad_element_t		*p_mad_resp;
	ib_smp_t				*p_smp;
	ib_vl_arb_table_t		*p_vl_arb_table;
	uint16_t				idx;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Initialize a pointers to the MAD work request and outbound MAD. */
	p_mad = get_mad_hdr_from_wr( p_mad_wr );

	/* Get the table selector from the attribute */
	idx = ((uint16_t)(cl_ntoh32( p_mad->attr_mod ) >> 16)) - 1;
	
	/*
	 * TODO : Setup the response to fail the MAD instead of sending
	 * it down to the HCA.
	 */
	if( idx > 3 )
	{
		AL_EXIT( AL_DBG_SMI );
		return IB_NOT_DONE;
	}


	if( !p_spl_qp_svc->cache.vl_arb[idx].valid )
	{
		AL_EXIT( AL_DBG_SMI );
		return IB_NOT_DONE;
	}

	/*
	 * If a SET, see if the set is identical to the cache,
	 * in which case it's a no-op.
	 */
	if( p_mad->method == IB_MAD_METHOD_SET )
	{
		if( cl_memcmp( ib_smp_get_payload_ptr( (ib_smp_t*)p_mad ),
			&p_spl_qp_svc->cache.vl_arb[idx].tbl, sizeof(ib_vl_arb_table_t) ) )
		{
			/* The set is requesting a change. */
			AL_EXIT( AL_DBG_SMI );
			return IB_NOT_DONE;
		}
	}
	
	/* Get a MAD element from the pool for the response. */
	status = get_resp_mad( p_spl_qp_svc, p_mad_wr, &p_mad_resp );
	if( status == IB_SUCCESS )
	{
		p_smp = (ib_smp_t*)p_mad_resp->p_mad_buf;

		/* Setup the response mad. */
		cl_memcpy( p_smp, p_mad, MAD_BLOCK_SIZE );
		p_smp->method = (IB_MAD_METHOD_RESP_MASK | IB_MAD_METHOD_GET);
		if( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR )
			p_smp->status = IB_SMP_DIRECTION;
		else
			p_smp->status = 0;

		p_vl_arb_table = (ib_vl_arb_table_t*)ib_smp_get_payload_ptr( p_smp );

		// TODO: do we need lock on the cache ?????

		
		/* Copy the cached data. */
		cl_memcpy( p_vl_arb_table,
			&p_spl_qp_svc->cache.pkey_tbl[idx].tbl, sizeof(ib_vl_arb_table_t) );

		status = complete_local_mad( p_spl_qp_svc, p_mad_wr, p_mad_resp );
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}




/*
 * Process subnet administration MADs using cached data if possible.
 */
static ib_api_status_t
__process_subn_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_api_status_t		status;
	ib_smp_t			*p_smp;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	p_smp = (ib_smp_t*)get_mad_hdr_from_wr( p_mad_wr );

	CL_ASSERT( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR ||
		p_smp->mgmt_class == IB_MCLASS_SUBN_LID );

	/* simple m-key check */
	if( p_smp->m_key == p_spl_qp_svc->m_key )
	{
		if(!p_spl_qp_svc->cache_en )
		{
			p_spl_qp_svc->cache_en = TRUE;
			AL_EXIT( AL_DBG_SMI );
			return IB_NOT_DONE;
		}
	}
	else
	{
		AL_PRINT(TRACE_LEVEL_WARNING, AL_DBG_SMI, ("Mkey check failed \n"));
		AL_PRINT(TRACE_LEVEL_WARNING, AL_DBG_SMI, ("Mkey check SMP= 0x%08x:%08x  SVC = 0x%08x:%08x \n",
									((uint32_t*)&p_smp->m_key)[0],((uint32_t*)&p_smp->m_key)[1],
									((uint32_t*)&p_spl_qp_svc->m_key)[0],((uint32_t*)&p_spl_qp_svc->m_key)[1]));

		p_spl_qp_svc->cache_en = FALSE;
		AL_EXIT( AL_DBG_SMI );
		return IB_NOT_DONE;
	}

	cl_spinlock_acquire(&p_spl_qp_svc->cache_lock);
	
	switch( p_smp->attr_id )
	{
	case IB_MAD_ATTR_NODE_INFO:
		status = __process_node_info( p_spl_qp_svc, p_mad_wr );
		break;

	case IB_MAD_ATTR_NODE_DESC:
		status = __process_node_desc( p_spl_qp_svc, p_mad_wr );
		break;

	case IB_MAD_ATTR_GUID_INFO:
		status = __process_guid_info( p_spl_qp_svc, p_mad_wr );
		break;

	case IB_MAD_ATTR_P_KEY_TABLE:
		status = __process_pkey_table( p_spl_qp_svc, p_mad_wr );
		break;
		
	case IB_MAD_ATTR_SLVL_TABLE:
		status = __process_slvl_table( p_spl_qp_svc, p_mad_wr );
		break;
		
	case IB_MAD_ATTR_VL_ARBITRATION:
		status = __process_vl_arb_table( p_spl_qp_svc, p_mad_wr );
		break;
		
	default:
		status = IB_NOT_DONE;
		break;
	}

	cl_spinlock_release(&p_spl_qp_svc->cache_lock);

	AL_EXIT( AL_DBG_SMI );
	return status;
}


/*
 * Process a local MAD send work request.
 */
static ib_api_status_t
fwd_local_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_mad_t*				p_mad;
	ib_smp_t*				p_smp;
	al_mad_send_t*			p_mad_send;
	ib_mad_element_t*		p_send_mad;
	ib_mad_element_t*		p_mad_response = NULL;
	ib_mad_t*				p_mad_response_buf;
	ib_api_status_t			status = IB_SUCCESS;
	boolean_t				smp_is_set;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_wr );

	/* Initialize a pointers to the MAD work request and outbound MAD. */
	p_mad = get_mad_hdr_from_wr( p_mad_wr );
	p_smp = (ib_smp_t*)p_mad;

	smp_is_set = (p_smp->method == IB_MAD_METHOD_SET);

	/* Get a MAD element from the pool for the response. */
	p_mad_send = PARENT_STRUCT( p_mad_wr, al_mad_send_t, mad_wr );
	if( p_mad_send->p_send_mad->resp_expected )
	{
		status = get_resp_mad( p_spl_qp_svc, p_mad_wr, &p_mad_response );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_SMI );
			return status;
		}
		p_mad_response_buf = p_mad_response->p_mad_buf;
		/* Copy MAD to dispatch locally in case CA doesn't handle it. */
		*p_mad_response_buf = *p_mad;
	}
	else
	{
			p_mad_response_buf = NULL;
	}

	/* Adjust directed route SMPs as required by IBA. */
	if( p_mad->mgmt_class == IB_MCLASS_SUBN_DIR )
	{
		CL_ASSERT( !ib_smp_is_response( p_smp ) );

		/*
		 * If this was a self addressed, directed route SMP, increment
		 * the hop pointer in the request before delivery as required
		 * by IBA.  Otherwise, adjustment for remote requests occurs
		 * during inbound processing.
		 */
		if( p_smp->hop_count == 0 )
			p_smp->hop_ptr++;
	}

	/* Forward the locally addressed MAD to the CA interface. */
	status = al_local_mad( p_spl_qp_svc->h_qp->obj.p_ci_ca->h_ca,
		p_spl_qp_svc->port_num, &p_mad_wr->send_wr.dgrm.ud.h_av->av_attr, p_mad, p_mad_response_buf );

	/* Reset directed route SMPs as required by IBA. */
	if( p_mad->mgmt_class == IB_MCLASS_SUBN_DIR )
	{
		/*
		 * If this was a self addressed, directed route SMP, decrement
		 * the hop pointer in the response before delivery as required
		 * by IBA.  Otherwise, adjustment for remote responses occurs
		 * during outbound processing.
		 */
		if( p_smp->hop_count == 0 )
		{
			/* Adjust the request SMP. */
			p_smp->hop_ptr--;

			/* Adjust the response SMP. */
			if( p_mad_response_buf )
			{
				p_smp = (ib_smp_t*)p_mad_response_buf;
				p_smp->hop_ptr--;
			}
		}
	}

	if( status != IB_SUCCESS )
	{
		if( p_mad_response )
			ib_put_mad( p_mad_response );

		__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr,
			IB_WCS_LOCAL_OP_ERR );
		AL_EXIT( AL_DBG_SMI );
		return status;
	}

	/* Check the completion status of this simulated send. */
	if( p_mad_send->p_send_mad->resp_expected )
	{
		/*
		 * The SMI is uses PnP polling to refresh the base_lid and lmc.
		 * Polling takes time, so we update the values here to prevent
		 * the failure of LID routed MADs sent immediately following this
		 * assignment.  Check the response to see if the port info was set.
		 */
		if( smp_is_set )
		{
			ib_smp_t*		p_smp_response = NULL;

			switch( p_mad_response_buf->mgmt_class )
			{
			case IB_MCLASS_SUBN_DIR:
				if( ib_smp_get_status( p_smp ) == IB_SA_MAD_STATUS_SUCCESS ) 
				{
					p_smp_response = p_smp;
					//p_port_info =
					//	(ib_port_info_t*)ib_smp_get_payload_ptr( p_smp );
				}
				break;

			case IB_MCLASS_SUBN_LID:
				if( p_mad_response_buf->status == IB_SA_MAD_STATUS_SUCCESS )
				{
					p_smp_response = (ib_smp_t*)p_mad_response_buf;
					//p_port_info =
					//	(ib_port_info_t*)ib_smp_get_payload_ptr((ib_smp_t*)p_mad_response_buf);
				}
				break;

			default:
				break;
			}

			if( p_smp_response )
			{
				switch( p_smp_response->attr_id )
				{
					case IB_MAD_ATTR_PORT_INFO:
						{
							ib_port_info_t		*p_port_info =
								(ib_port_info_t*)ib_smp_get_payload_ptr(p_smp_response);
							p_spl_qp_svc->base_lid = p_port_info->base_lid;
							p_spl_qp_svc->lmc = ib_port_info_get_lmc( p_port_info );
							p_spl_qp_svc->sm_lid = p_port_info->master_sm_base_lid;
							p_spl_qp_svc->sm_sl = ib_port_info_get_sm_sl( p_port_info );

							if(p_port_info->m_key)
								p_spl_qp_svc->m_key = p_port_info->m_key;
							if (p_port_info->subnet_timeout & 0x80)
							{
								AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
									("Client reregister event, setting sm_lid to 0.\n"));
								ci_ca_lock_attr(p_spl_qp_svc->obj.p_ci_ca);
								p_spl_qp_svc->obj.p_ci_ca->p_pnp_attr->
									p_port_attr[p_port_info->local_port_num - 1].sm_lid= 0;
								ci_ca_unlock_attr(p_spl_qp_svc->obj.p_ci_ca);
							}
						}
						break;
					case IB_MAD_ATTR_P_KEY_TABLE:
					case IB_MAD_ATTR_GUID_INFO:
					case IB_MAD_ATTR_SLVL_TABLE:
					case IB_MAD_ATTR_VL_ARBITRATION:
						spl_qp_svc_update_cache( p_spl_qp_svc, p_smp_response);
						break;
					default :
						break;
				}
			}
		}
		

		/* Construct the receive MAD element. */
		p_send_mad = p_mad_send->p_send_mad;
		p_mad_response->status = IB_WCS_SUCCESS;
		p_mad_response->grh_valid = p_send_mad->grh_valid;
		if( p_mad_response->grh_valid )
			*p_mad_response->p_grh  = *p_send_mad->p_grh;
		p_mad_response->path_bits   = p_send_mad->path_bits;
		p_mad_response->pkey_index  = p_send_mad->pkey_index;
		p_mad_response->remote_lid  = p_send_mad->remote_lid;
		p_mad_response->remote_qkey = p_send_mad->remote_qkey;
		p_mad_response->remote_qp   = p_send_mad->remote_qp;
		p_mad_response->remote_sl   = p_send_mad->remote_sl;
		if( p_mad_wr->send_wr.send_opt & IB_RECV_OPT_IMMEDIATE )
		{
			p_mad_response->immediate_data = p_mad_wr->send_wr.immediate_data;
			p_mad_response->recv_opt |= IB_RECV_OPT_IMMEDIATE;
		}

		/*
		 * Hand the receive MAD element to the dispatcher before completing
		 * the send.  This guarantees that the send request cannot time out.
		 */
		status = mad_disp_recv_done( p_spl_qp_svc->h_mad_disp, p_mad_response );
		if( status != IB_SUCCESS )
			ib_put_mad( p_mad_response );
	}
	
	__complete_send_mad( p_spl_qp_svc->h_mad_disp, p_mad_wr,IB_WCS_SUCCESS);

	
	
	/* If the SMP was a Get, no need to trigger a PnP poll. */
	if( status == IB_SUCCESS && !smp_is_set )
		status = IB_NOT_DONE;

	AL_EXIT( AL_DBG_SMI );
	return status;
}



/*
 * Asynchronous processing thread callback to send a local MAD.
 */
void
send_local_mad_cb(
	IN				cl_async_proc_item_t*		p_item )
{
	spl_qp_svc_t*			p_spl_qp_svc;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_item );
	p_spl_qp_svc = PARENT_STRUCT( p_item, spl_qp_svc_t, send_async );

	/* Process a local MAD send work request. */
	CL_ASSERT( p_spl_qp_svc->local_mad_wr );
	status = fwd_local_mad( p_spl_qp_svc, p_spl_qp_svc->local_mad_wr );

	/*
	 * If we successfully processed a local MAD, which could have changed
	 * something (e.g. the LID) on the HCA.  Scan for changes.
	 */
	if( status == IB_SUCCESS )
		pnp_poll();

	/*
	 * Clear the local MAD pointer to allow processing of other MADs.
	 * This is done after polling for attribute changes to ensure that
	 * subsequent MADs pick up any changes performed by this one.
	 */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	p_spl_qp_svc->local_mad_wr = NULL;
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	/* Continue processing any queued MADs on the QP. */
	special_qp_resume_sends( p_spl_qp_svc->h_qp );

	/* No longer in use by the asynchronous processing thread. */
	cl_atomic_dec( &p_spl_qp_svc->in_use_cnt );

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Special QP send completion callback.
 */
void
spl_qp_send_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void*						cq_context )
{
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	UNREFERENCED_PARAMETER( h_cq );

	CL_ASSERT( cq_context );
	p_spl_qp_svc = cq_context;

#if defined( CL_USE_MUTEX )

	/* Queue an asynchronous processing item to process sends. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	if( !p_spl_qp_svc->send_async_queued )
	{
		p_spl_qp_svc->send_async_queued = TRUE;
		ref_al_obj( &p_spl_qp_svc->obj );
		cl_async_proc_queue( gp_async_proc_mgr, &p_spl_qp_svc->send_async_cb );
	}
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

#else
    cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	if( p_spl_qp_svc->state != SPL_QP_ACTIVE )
	{
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );
        AL_EXIT( AL_DBG_SMI );
		return;
	}
	cl_atomic_inc( &p_spl_qp_svc->in_use_cnt );
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

    /* Queue the DPC. */
	CL_ASSERT( h_cq == p_spl_qp_svc->h_send_cq );
    KeInsertQueueDpc( &p_spl_qp_svc->send_dpc, NULL, NULL );
#endif

	AL_EXIT( AL_DBG_SMI );
}


void
spl_qp_send_dpc_cb(
    IN              KDPC                        *p_dpc,
    IN              void                        *context,
    IN              void                        *arg1,
    IN              void                        *arg2
    )
{
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( context );
	p_spl_qp_svc = context;

    UNREFERENCED_PARAMETER( p_dpc );
    UNREFERENCED_PARAMETER( arg1 );
    UNREFERENCED_PARAMETER( arg2 );

	spl_qp_comp( p_spl_qp_svc, p_spl_qp_svc->h_send_cq, IB_WC_SEND );

	/* Continue processing any queued MADs on the QP. */
	special_qp_resume_sends( p_spl_qp_svc->h_qp );

    cl_atomic_dec( &p_spl_qp_svc->in_use_cnt );

    AL_EXIT( AL_DBG_SMI );
}


#if defined( CL_USE_MUTEX )
void
spl_qp_send_async_cb(
	IN				cl_async_proc_item_t*		p_item )
{
	spl_qp_svc_t*			p_spl_qp_svc;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_item );
	p_spl_qp_svc = PARENT_STRUCT( p_item, spl_qp_svc_t, send_async_cb );

	/* Reset asynchronous queue flag. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	p_spl_qp_svc->send_async_queued = FALSE;
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	spl_qp_comp( p_spl_qp_svc, p_spl_qp_svc->h_send_cq, IB_WC_SEND );

	/* Continue processing any queued MADs on the QP. */
	status = special_qp_resume_sends( p_spl_qp_svc->h_qp );
	CL_ASSERT( status == IB_SUCCESS );

	deref_al_obj( &p_spl_qp_svc->obj );

	AL_EXIT( AL_DBG_SMI );
}
#endif



/*
 * Special QP receive completion callback.
 */
void
spl_qp_recv_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void*						cq_context )
{
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	UNREFERENCED_PARAMETER( h_cq );

	CL_ASSERT( cq_context );
	p_spl_qp_svc = cq_context;

#if defined( CL_USE_MUTEX )

	/* Queue an asynchronous processing item to process receives. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	if( !p_spl_qp_svc->recv_async_queued )
	{
		p_spl_qp_svc->recv_async_queued = TRUE;
		ref_al_obj( &p_spl_qp_svc->obj );
		cl_async_proc_queue( gp_async_proc_mgr, &p_spl_qp_svc->recv_async_cb );
	}
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

#else
    cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	if( p_spl_qp_svc->state != SPL_QP_ACTIVE )
	{
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );
        AL_EXIT( AL_DBG_SMI );
		return;
	}
	cl_atomic_inc( &p_spl_qp_svc->in_use_cnt );
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

    /* Queue the DPC. */
	CL_ASSERT( h_cq == p_spl_qp_svc->h_recv_cq );
    KeInsertQueueDpc( &p_spl_qp_svc->recv_dpc, NULL, NULL );
#endif

	AL_EXIT( AL_DBG_SMI );
}

void
spl_qp_recv_dpc_cb(
    IN              KDPC                        *p_dpc,
    IN              void                        *context,
    IN              void                        *arg1,
    IN              void                        *arg2
    )
{
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( context );
	p_spl_qp_svc = context;

    UNREFERENCED_PARAMETER( p_dpc );
    UNREFERENCED_PARAMETER( arg1 );
    UNREFERENCED_PARAMETER( arg2 );

	spl_qp_comp( p_spl_qp_svc, p_spl_qp_svc->h_recv_cq, IB_WC_RECV );

    cl_atomic_dec( &p_spl_qp_svc->in_use_cnt );

    AL_EXIT( AL_DBG_SMI );
}


#if defined( CL_USE_MUTEX )
void
spl_qp_recv_async_cb(
	IN				cl_async_proc_item_t*		p_item )
{
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_item );
	p_spl_qp_svc = PARENT_STRUCT( p_item, spl_qp_svc_t, recv_async_cb );

	/* Reset asynchronous queue flag. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	p_spl_qp_svc->recv_async_queued = FALSE;
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	spl_qp_comp( p_spl_qp_svc, p_spl_qp_svc->h_recv_cq, IB_WC_RECV );

	deref_al_obj( &p_spl_qp_svc->obj );

	AL_EXIT( AL_DBG_SMI );
}
#endif


#define SPL_QP_MAX_POLL 16
/*
 * Special QP completion handler.
 */
void
spl_qp_comp(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN		const	ib_cq_handle_t				h_cq,
	IN				ib_wc_type_t				wc_type )
{
	ib_wc_t					wc;
	ib_wc_t*				p_free_wc = &wc;
	ib_wc_t*				p_done_wc;
	al_mad_wr_t*			p_mad_wr;
	al_mad_element_t*		p_al_mad;
	ib_mad_element_t*		p_mad_element;
	ib_smp_t*				p_smp;
	ib_api_status_t			status;
    int                     max_poll = SPL_QP_MAX_POLL;

	AL_ENTER( AL_DBG_SMI_CB );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( h_cq );

	/* Check the QP state and guard against error handling. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	if( p_spl_qp_svc->state != SPL_QP_ACTIVE )
	{
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );
		return;
	}
	cl_atomic_inc( &p_spl_qp_svc->in_use_cnt );
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	wc.p_next = NULL;
	/* Process work completions. */
	while( max_poll && ib_poll_cq( h_cq, &p_free_wc, &p_done_wc ) == IB_SUCCESS )
	{
		/* Process completions one at a time. */
		CL_ASSERT( p_done_wc );

		/* Flushed completions are handled elsewhere. */
		if( wc.status == IB_WCS_WR_FLUSHED_ERR )
		{
			p_free_wc = &wc;
			continue;
		}

		/*
		 * Process the work completion.  Per IBA specification, the
		 * wc.wc_type is undefined if wc.status is not IB_WCS_SUCCESS.
		 * Use the wc_type parameter.
		 */
		switch( wc_type )
		{
		case IB_WC_SEND:
			/* Get a pointer to the MAD work request. */
			p_mad_wr = (al_mad_wr_t*)((uintn_t)wc.wr_id);

			/* Remove the MAD work request from the service tracking queue. */
			cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
			cl_qlist_remove_item( &p_spl_qp_svc->send_queue,
				&p_mad_wr->list_item );
			cl_spinlock_release( &p_spl_qp_svc->obj.lock );

			/* Reset directed route SMPs as required by IBA. */
			p_smp = (ib_smp_t*)get_mad_hdr_from_wr( p_mad_wr );
			if( p_smp->mgmt_class == IB_MCLASS_SUBN_DIR )
			{
				if( ib_smp_is_response( p_smp ) )
					p_smp->hop_ptr++;
				else
					p_smp->hop_ptr--;
			}

			/* Report the send completion to the dispatcher. */
			mad_disp_send_done( p_spl_qp_svc->h_mad_disp, p_mad_wr, &wc );
			break;

		case IB_WC_RECV:

			/* Initialize pointers to the MAD element. */
			p_al_mad = (al_mad_element_t*)((uintn_t)wc.wr_id);
			p_mad_element = &p_al_mad->element;

			/* Remove the AL MAD element from the service tracking list. */
			cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );

			cl_qlist_remove_item( &p_spl_qp_svc->recv_queue,
				&p_al_mad->list_item );

			/* Replenish the receive buffer. */
			spl_qp_svc_post_recvs( p_spl_qp_svc );
			cl_spinlock_release( &p_spl_qp_svc->obj.lock );

			/* Construct the MAD element from the receive work completion. */
			build_mad_recv( p_mad_element, &wc );

			/* Process the received MAD. */
			status = process_mad_recv( p_spl_qp_svc, p_mad_element );

			/* Discard this MAD on error. */
			if( status != IB_SUCCESS )
			{
				status = ib_put_mad( p_mad_element );
				CL_ASSERT( status == IB_SUCCESS );
			}
			break;

		default:
			CL_ASSERT( wc_type == IB_WC_SEND || wc_type == IB_WC_RECV );
			break;
		}

		if( wc.status != IB_WCS_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("special QP completion error: %s! internal syndrome 0x%I64x\n",
				ib_get_wc_status_str( wc.status ), wc.vendor_specific) );

			/* Reset the special QP service and return. */
			spl_qp_svc_reset( p_spl_qp_svc );
		}
		p_free_wc = &wc;
        --max_poll;
	}

    if( max_poll == 0 )
    {
        /* We already have an in_use_cnt reference - use it to queue the DPC. */
        if( wc_type == IB_WC_SEND )
            KeInsertQueueDpc( &p_spl_qp_svc->send_dpc, NULL, NULL );
        else
            KeInsertQueueDpc( &p_spl_qp_svc->recv_dpc, NULL, NULL );
    }
    else
    {
	    /* Rearm the CQ. */
	    status = ib_rearm_cq( h_cq, FALSE );
	    CL_ASSERT( status == IB_SUCCESS );

	    cl_atomic_dec( &p_spl_qp_svc->in_use_cnt );
    }
	AL_EXIT( AL_DBG_SMI_CB );
}



/*
 * Process a received MAD.
 */
ib_api_status_t
process_mad_recv(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				ib_mad_element_t*			p_mad_element )
{
	ib_smp_t*				p_smp;
	mad_route_t				route;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_element );

	/*
	 * If the CA has a HW agent then this MAD should have been
	 * consumed below verbs.  The fact that it was received here
	 * indicates that it should be forwarded to the dispatcher
	 * for delivery to a class manager.  Otherwise, determine how
	 * the MAD should be routed.
	 */
	route = ROUTE_DISPATCHER;
	if( check_local_mad( p_spl_qp_svc->h_qp ) )
	{
		/*
		 * SMP and GMP processing is branched here to handle overlaps
		 * between class methods and attributes.
		 */
		switch( p_mad_element->p_mad_buf->mgmt_class )
		{
		case IB_MCLASS_SUBN_DIR:
			/* Perform special checks on directed route SMPs. */
			p_smp = (ib_smp_t*)p_mad_element->p_mad_buf;

			if( ( p_smp->hop_count >= IB_SUBNET_PATH_HOPS_MAX ) ||
				( p_smp->hop_ptr >= IB_SUBNET_PATH_HOPS_MAX ) )
			{
				route = ROUTE_DISCARD;
			}
			else if( ib_smp_is_response( p_smp ) )
			{
				/*
				 * This node is the destination of the response.  Discard
				 * the source LID or hop pointer are incorrect.
				 */
				if( p_smp->dr_slid == IB_LID_PERMISSIVE )
				{
					if( p_smp->hop_ptr == 1 )
					{
						p_smp->hop_ptr--;		/* Adjust ptr per IBA spec. */
					}
					else
					{
						route = ROUTE_DISCARD;
					}
				}
				else if( ( p_smp->dr_slid <  p_spl_qp_svc->base_lid ) ||
						 ( p_smp->dr_slid >= p_spl_qp_svc->base_lid +
							( 1 << p_spl_qp_svc->lmc ) ) )
				{
						route = ROUTE_DISCARD;
				}
			}
			else
			{
				/*
				 * This node is the destination of the request.  Discard
				 * the destination LID or hop pointer are incorrect.
				 */
				if( p_smp->dr_dlid == IB_LID_PERMISSIVE )
				{
					if( p_smp->hop_count == p_smp->hop_ptr )
					{
						p_smp->return_path[ p_smp->hop_ptr++ ] =
							p_spl_qp_svc->port_num;	/* Set path per IBA spec. */
					}
					else
					{
						route = ROUTE_DISCARD;
					}
				}
				else if( ( p_smp->dr_dlid <  p_spl_qp_svc->base_lid ) ||
						 ( p_smp->dr_dlid >= p_spl_qp_svc->base_lid +
							( 1 << p_spl_qp_svc->lmc ) ) )
				{
					route = ROUTE_DISCARD;
				}
			}

			if( route == ROUTE_DISCARD ) break;
			/* else fall through next case */

		case IB_MCLASS_SUBN_LID:
			route = route_recv_smp( p_mad_element );
			break;

		case IB_MCLASS_PERF:
			route = route_recv_perf( p_mad_element );
			break;

		case IB_MCLASS_BM:
			route = route_recv_bm( p_mad_element );
			break;

		case IB_MLX_VENDOR_CLASS1:
		case IB_MLX_VENDOR_CLASS2:
			route = route_recv_vendor( p_mad_element );
			break;

		default:
			break;
		}
	}

	/* Route the MAD. */
	if( is_discard( route ) )
		status = IB_ERROR;
	else if( is_dispatcher( route ) )
		status = mad_disp_recv_done( p_spl_qp_svc->h_mad_disp, p_mad_element );
	else if( is_remote( route ) )
		status = forward_sm_trap( p_spl_qp_svc, p_mad_element );
	else
		status = recv_local_mad( p_spl_qp_svc, p_mad_element );

	AL_EXIT( AL_DBG_SMI );
	return status;
}



/*
 * Route a received SMP.
 */
static mad_route_t
route_recv_smp(
	IN				ib_mad_element_t*			p_mad_element )
{
	mad_route_t				route;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_mad_element );

	/* Process the received SMP. */
	switch( p_mad_element->p_mad_buf->method )
	{
	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_SET:
		route = route_recv_smp_attr( p_mad_element );
		break;

	case IB_MAD_METHOD_TRAP:
		/*
		 * Special check to route locally generated traps to the remote SM.
		 * Distinguished from other receives by the p_wc->recv.ud.recv_opt
		 * IB_RECV_OPT_FORWARD flag.
		 *
		 * Note that because forwarded traps use AL MAD services, the upper
		 * 32-bits of the TID are reserved by the access layer.  When matching
		 * a Trap Repress MAD, the SMA must only use the lower 32-bits of the
		 * TID.
		 */
		AL_PRINT(TRACE_LEVEL_INFORMATION, AL_DBG_SMI, ("Trap TID = 0x%08x:%08x \n",
			((uint32_t*)&p_mad_element->p_mad_buf->trans_id)[0],
			((uint32_t*)&p_mad_element->p_mad_buf->trans_id)[1]));

		route = ( p_mad_element->recv_opt & IB_RECV_OPT_FORWARD ) ?
			ROUTE_REMOTE : ROUTE_DISPATCHER;
		break;

	case IB_MAD_METHOD_TRAP_REPRESS:
		/*
		 * Note that because forwarded traps use AL MAD services, the upper
		 * 32-bits of the TID are reserved by the access layer.  When matching
		 * a Trap Repress MAD, the SMA must only use the lower 32-bits of the
		 * TID.
		 */
		AL_PRINT(TRACE_LEVEL_INFORMATION, AL_DBG_SMI, ("TrapRepress TID = 0x%08x:%08x \n",
			((uint32_t*)&p_mad_element->p_mad_buf->trans_id)[0],
			((uint32_t*)&p_mad_element->p_mad_buf->trans_id)[1]));

		route = ROUTE_LOCAL;
		break;

	default:
		route = ROUTE_DISPATCHER;
		break;
	}

	AL_EXIT( AL_DBG_SMI );
	return route;
}



/*
 * Route received SMP attributes.
 */
static mad_route_t
route_recv_smp_attr(
	IN				ib_mad_element_t*			p_mad_element )
{
	mad_route_t				route;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_mad_element );

	/* Process the received SMP attributes. */
	switch( p_mad_element->p_mad_buf->attr_id )
	{
	case IB_MAD_ATTR_NODE_DESC:
	case IB_MAD_ATTR_NODE_INFO:
	case IB_MAD_ATTR_GUID_INFO:
	case IB_MAD_ATTR_PORT_INFO:
	case IB_MAD_ATTR_P_KEY_TABLE:
	case IB_MAD_ATTR_SLVL_TABLE:
	case IB_MAD_ATTR_VL_ARBITRATION:
	case IB_MAD_ATTR_VENDOR_DIAG:
	case IB_MAD_ATTR_LED_INFO:
	case IB_MAD_ATTR_SWITCH_INFO:
		route = ROUTE_LOCAL;
		break;

	default:
		route = ROUTE_DISPATCHER;
		break;
	}

	AL_EXIT( AL_DBG_SMI );
	return route;
}


static mad_route_t
route_recv_bm(
	IN				ib_mad_element_t*			p_mad_element )
{
	switch( p_mad_element->p_mad_buf->method )
	{
	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_SET:
		if( p_mad_element->p_mad_buf->attr_id == IB_MAD_ATTR_CLASS_PORT_INFO )
			return ROUTE_LOCAL;
		break;
	default:
		break;
	}
	return ROUTE_DISPATCHER;
}

static mad_route_t
route_recv_perf(
	IN				ib_mad_element_t*			p_mad_element )
{
	switch( p_mad_element->p_mad_buf->method )
	{
	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_SET:
		return ROUTE_LOCAL;
	default:
		break;
	}
	return ROUTE_DISPATCHER;
}

static mad_route_t
route_recv_vendor(
	IN				ib_mad_element_t*			p_mad_element )
{
	return ( p_mad_element->p_mad_buf->method & IB_MAD_METHOD_RESP_MASK ) ?
		ROUTE_DISPATCHER : ROUTE_LOCAL;
}

/*
 * Forward a locally generated Subnet Management trap.
 */
ib_api_status_t
forward_sm_trap(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				ib_mad_element_t*			p_mad_element )
{
	ib_av_attr_t			av_attr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_element );

	/* Check the SMP class. */
	if( p_mad_element->p_mad_buf->mgmt_class != IB_MCLASS_SUBN_LID )
	{
		/*
		 * Per IBA Specification Release 1.1 Section 14.2.2.1,
		 * "C14-5: Only a SM shall originate a directed route SMP."
		 * Therefore all traps should be LID routed; drop this one.
		 */
		AL_EXIT( AL_DBG_SMI );
		return IB_ERROR;
	}

	if(p_spl_qp_svc->sm_lid == p_spl_qp_svc->base_lid)
		return mad_disp_recv_done(p_spl_qp_svc->h_mad_disp,p_mad_element);
	
	/* Create an address vector for the SM. */
	cl_memclr( &av_attr, sizeof( ib_av_attr_t ) );
	av_attr.port_num = p_spl_qp_svc->port_num;
	av_attr.sl = p_spl_qp_svc->sm_sl;
	av_attr.dlid = p_spl_qp_svc->sm_lid;
	av_attr.grh_valid = FALSE;

	status = ib_create_av_ctx( p_spl_qp_svc->h_qp->obj.p_ci_ca->h_pd_alias,
		&av_attr, 4, &p_mad_element->h_av );

	if( status != IB_SUCCESS )
	{
		AL_EXIT( AL_DBG_SMI );
		return status;
	}

	/* Complete the initialization of the MAD element. */
	p_mad_element->p_next = NULL;
	p_mad_element->remote_qkey = IB_QP_PRIVILEGED_Q_KEY;
	p_mad_element->resp_expected = FALSE;

	/* Clear context1 for proper send completion callback processing. */
	p_mad_element->context1 = NULL;

	/*
	 * Forward the trap.  Note that because forwarded traps use AL MAD
	 * services, the upper 32-bits of the TID are reserved by the access
	 * layer.  When matching a Trap Repress MAD, the SMA must only use
	 * the lower 32-bits of the TID.
	 */
	status = ib_send_mad( p_spl_qp_svc->h_mad_svc, p_mad_element, NULL );

	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_send_mad in forward_sm_trap returned %s.\n", ib_get_err_str(status)) );
		ib_destroy_av_ctx( p_mad_element->h_av, 5 );
		p_mad_element->h_av = NULL;
	}

	AL_EXIT( AL_DBG_SMI );
	return status;
}


/*
 * Process a locally routed MAD received from the special QP.
 */
ib_api_status_t
recv_local_mad(
	IN				spl_qp_svc_t*				p_spl_qp_svc,
	IN				ib_mad_element_t*			p_mad_request )
{
	ib_mad_t*				p_mad_hdr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_spl_qp_svc );
	CL_ASSERT( p_mad_request );

	/* Initialize the MAD element. */
	p_mad_hdr = ib_get_mad_buf( p_mad_request );
	p_mad_request->context1	= p_mad_request;

	/* Save the TID. */
	p_mad_request->context2 =
		(void*)(uintn_t)al_get_al_tid( p_mad_hdr->trans_id );
/*
 * Disable warning about passing unaligned 64-bit value.
 * The value is always aligned given how buffers are allocated
 * and given the layout of a MAD.
 */
#pragma warning( push, 3 )
	al_set_al_tid( &p_mad_hdr->trans_id, 0 );
#pragma warning( pop )

	/*
	 * We need to get a response from the local HCA to this MAD only if this
	 * MAD is not itself a response.
	 */
	p_mad_request->resp_expected = !ib_mad_is_response( p_mad_hdr );
	p_mad_request->timeout_ms = LOCAL_MAD_TIMEOUT;
	p_mad_request->send_opt	= IB_SEND_OPT_LOCAL;

	/* Send the locally addressed MAD request to the CA for processing. */
	status = ib_send_mad( p_spl_qp_svc->h_mad_svc, p_mad_request, NULL );

	AL_EXIT( AL_DBG_SMI );
	return status;
}



/*
 * Special QP alias send completion callback.
 */
void
spl_qp_alias_send_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void*						mad_svc_context,
	IN				ib_mad_element_t*			p_mad_element )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	UNUSED_PARAM( h_mad_svc );
	UNUSED_PARAM( mad_svc_context );
	CL_ASSERT( p_mad_element );

	if( p_mad_element->h_av )
	{
		status = ib_destroy_av_ctx( p_mad_element->h_av, 6 );
		p_mad_element->h_av = NULL;
		CL_ASSERT( status == IB_SUCCESS );
	}

	status = ib_put_mad( p_mad_element );
	CL_ASSERT( status == IB_SUCCESS );

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Special QP alias receive completion callback.
 */
void
spl_qp_alias_recv_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void*						mad_svc_context,
	IN				ib_mad_element_t*			p_mad_response )
{
	spl_qp_svc_t*			p_spl_qp_svc;
	ib_mad_element_t*		p_mad_request;
	ib_mad_t*				p_mad_hdr;
	ib_av_attr_t			av_attr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( mad_svc_context );
	CL_ASSERT( p_mad_response );
	
	
	if ( !p_mad_response->send_context1 ) {
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("p_mad_response->send_context1 == NULL\n") );
		ib_put_mad( p_mad_response );
		AL_EXIT( AL_DBG_SMI );
		return;
	}

	/* Initialize pointers. */
	p_spl_qp_svc = mad_svc_context;
	p_mad_request = p_mad_response->send_context1;
	p_mad_hdr = ib_get_mad_buf( p_mad_response );

	/* Restore the TID, so it will match on the remote side. */
#pragma warning( push, 3 )
	al_set_al_tid( &p_mad_hdr->trans_id,
		(uint32_t)(uintn_t)p_mad_response->send_context2 );
#pragma warning( pop )

	/* Set the remote QP. */
	p_mad_response->remote_qp	= p_mad_request->remote_qp;
	p_mad_response->remote_qkey = p_mad_request->remote_qkey;

	/* Prepare to create an address vector. */
	cl_memclr( &av_attr, sizeof( ib_av_attr_t ) );
	av_attr.port_num	= p_spl_qp_svc->port_num;
	av_attr.sl			= p_mad_request->remote_sl;
	av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
	av_attr.path_bits	= p_mad_request->path_bits;
	if( p_mad_request->grh_valid )
	{
		cl_memcpy( &av_attr.grh, p_mad_request->p_grh, sizeof( ib_grh_t ) );
		av_attr.grh.src_gid	 = p_mad_request->p_grh->dest_gid;
		av_attr.grh.dest_gid = p_mad_request->p_grh->src_gid;
		av_attr.grh_valid = TRUE;
	}
	if( ( p_mad_hdr->mgmt_class == IB_MCLASS_SUBN_DIR ) &&
		( ((ib_smp_t *)p_mad_hdr)->dr_dlid == IB_LID_PERMISSIVE ) )
		av_attr.dlid = IB_LID_PERMISSIVE;
	else
		av_attr.dlid = p_mad_request->remote_lid;

	/* Create an address vector. */
	status = ib_create_av_ctx( p_spl_qp_svc->h_qp->obj.p_ci_ca->h_pd_alias,
		&av_attr, 5, &p_mad_response->h_av );

	if( status != IB_SUCCESS )
	{
		ib_put_mad( p_mad_response );

		AL_EXIT( AL_DBG_SMI );
		return;
	}

	/* Send the response. */
	status = ib_send_mad( h_mad_svc, p_mad_response, NULL );

	if( status != IB_SUCCESS )
	{
		ib_destroy_av_ctx( p_mad_response->h_av, 7 );
		p_mad_response->h_av = NULL;
		ib_put_mad( p_mad_response );
	}

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Post receive buffers to a special QP.
 */
static ib_api_status_t
spl_qp_svc_post_recvs(
	IN				spl_qp_svc_t*	const		p_spl_qp_svc )
{
	ib_mad_element_t*		p_mad_element;
	al_mad_element_t*		p_al_element;
	ib_recv_wr_t			recv_wr;
	ib_api_status_t			status = IB_SUCCESS;

	/* Attempt to post receive buffers up to the max_qp_depth limit. */
	while( cl_qlist_count( &p_spl_qp_svc->recv_queue ) <
		(int32_t)p_spl_qp_svc->max_qp_depth )
	{
		/* Get a MAD element from the pool. */
		status = ib_get_mad( p_spl_qp_svc->obj.p_ci_ca->pool_key,
			MAD_BLOCK_SIZE, &p_mad_element );

		if( status != IB_SUCCESS ) break;

		p_al_element = PARENT_STRUCT( p_mad_element, al_mad_element_t,
			element );

		/* Build the receive work request. */
		recv_wr.p_next	 = NULL;
		recv_wr.wr_id	 = (uintn_t)p_al_element;
		recv_wr.num_ds = 1;
		recv_wr.ds_array = &p_al_element->grh_ds;

		/* Queue the receive on the service tracking list. */
		cl_qlist_insert_tail( &p_spl_qp_svc->recv_queue,
			&p_al_element->list_item );

		/* Post the receive. */
		status = ib_post_recv( p_spl_qp_svc->h_qp, &recv_wr, NULL );

		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Failed to post receive %p\n",
				p_al_element) );
			cl_qlist_remove_item( &p_spl_qp_svc->recv_queue,
				&p_al_element->list_item );

			ib_put_mad( p_mad_element );
			break;
		}
	}

	return status;
}



/*
 * Special QP service asynchronous event callback.
 */
void
spl_qp_svc_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_event_rec );
	CL_ASSERT( p_event_rec->context );

	if( p_event_rec->code == IB_AE_SQ_DRAINED )
	{
		AL_EXIT( AL_DBG_SMI );
		return;
	}

	p_spl_qp_svc = p_event_rec->context;

	spl_qp_svc_reset( p_spl_qp_svc );

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Special QP service reset.
 */
void
spl_qp_svc_reset(
	IN				spl_qp_svc_t*				p_spl_qp_svc )
{
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );

	if( p_spl_qp_svc->state != SPL_QP_ACTIVE )
	{
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );
		return;
	}

	/* Change the special QP service to the error state. */
	p_spl_qp_svc->state = SPL_QP_ERROR;

	/* Flag the service as in use by the asynchronous processing thread. */
	cl_atomic_inc( &p_spl_qp_svc->in_use_cnt );

	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	/* Queue an asynchronous processing item to reset the special QP. */
	cl_async_proc_queue( gp_async_proc_mgr, &p_spl_qp_svc->reset_async );
}



/*
 * Asynchronous processing thread callback to reset the special QP service.
 */
void
spl_qp_svc_reset_cb(
	IN				cl_async_proc_item_t*		p_item )
{
	spl_qp_svc_t*			p_spl_qp_svc;
	cl_list_item_t*			p_list_item;
	ib_wc_t					wc;
	ib_wc_t*				p_free_wc;
	ib_wc_t*				p_done_wc;
	al_mad_wr_t*			p_mad_wr;
	al_mad_element_t*		p_al_mad;
	ib_qp_mod_t				qp_mod;
	ib_api_status_t			status;
	cl_qlist_t				mad_wr_list;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_item );
	p_spl_qp_svc = PARENT_STRUCT( p_item, spl_qp_svc_t, reset_async );

	/* Wait here until the special QP service is only in use by this thread. */
	while( p_spl_qp_svc->in_use_cnt != 1 )
	{
		cl_thread_suspend( 0 );
	}

	/* Change the QP to the RESET state. */
	cl_memclr( &qp_mod, sizeof( ib_qp_mod_t ) );
	qp_mod.req_state = IB_QPS_RESET;

	status = ib_modify_qp( p_spl_qp_svc->h_qp, &qp_mod );
	CL_ASSERT( status == IB_SUCCESS );

	/* Return receive MAD elements to the pool. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	for( p_list_item = cl_qlist_remove_head( &p_spl_qp_svc->recv_queue );
		 p_list_item != cl_qlist_end( &p_spl_qp_svc->recv_queue );
		 p_list_item = cl_qlist_remove_head( &p_spl_qp_svc->recv_queue ) )
	{
		p_al_mad = PARENT_STRUCT( p_list_item, al_mad_element_t, list_item );

		status = ib_put_mad( &p_al_mad->element );
		CL_ASSERT( status == IB_SUCCESS );
	}
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	/* Re-initialize the QP. */
	status = ib_init_dgrm_svc( p_spl_qp_svc->h_qp, NULL );
	CL_ASSERT( status == IB_SUCCESS );

	/* Poll to remove any remaining send completions from the CQ. */
	do
	{
		cl_memclr( &wc, sizeof( ib_wc_t ) );
		p_free_wc = &wc;
		status = ib_poll_cq( p_spl_qp_svc->h_send_cq, &p_free_wc, &p_done_wc );

	} while( status == IB_SUCCESS );

	/* Post receive buffers. */
	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	spl_qp_svc_post_recvs( p_spl_qp_svc );

	/* Re-queue any outstanding MAD send operations. */
	cl_qlist_init( &mad_wr_list );
	cl_qlist_insert_list_tail( &mad_wr_list, &p_spl_qp_svc->send_queue );
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	for( p_list_item = cl_qlist_remove_head( &mad_wr_list );
		 p_list_item != cl_qlist_end( &mad_wr_list );
		 p_list_item = cl_qlist_remove_head( &mad_wr_list ) )
	{
		p_mad_wr = PARENT_STRUCT( p_list_item, al_mad_wr_t, list_item );
		special_qp_queue_mad( p_spl_qp_svc->h_qp, p_mad_wr );
	}

	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	if( p_spl_qp_svc->state == SPL_QP_ERROR )
	{
		/* The QP is ready.  Change the state. */
		p_spl_qp_svc->state = SPL_QP_ACTIVE;
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );

		/* Re-arm the CQs. */
		status = ib_rearm_cq( p_spl_qp_svc->h_recv_cq, FALSE );
		CL_ASSERT( status == IB_SUCCESS );
		status = ib_rearm_cq( p_spl_qp_svc->h_send_cq, FALSE );
		CL_ASSERT( status == IB_SUCCESS );

		/* Resume send processing. */
		special_qp_resume_sends( p_spl_qp_svc->h_qp );
	}
	else
	{
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );
	}

	/* No longer in use by the asynchronous processing thread. */
	cl_atomic_dec( &p_spl_qp_svc->in_use_cnt );

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Special QP alias asynchronous event callback.
 */
void
spl_qp_alias_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	UNUSED_PARAM( p_event_rec );
}



/*
 * Acquire the SMI dispatcher for the given port.
 */
ib_api_status_t
acquire_smi_disp(
	IN		const	ib_net64_t					port_guid,
		OUT			al_mad_disp_handle_t* const	ph_mad_disp )
{
	CL_ASSERT( gp_spl_qp_mgr );
	return acquire_svc_disp( &gp_spl_qp_mgr->smi_map, port_guid, ph_mad_disp );
}



/*
 * Acquire the GSI dispatcher for the given port.
 */
ib_api_status_t
acquire_gsi_disp(
	IN		const	ib_net64_t					port_guid,
		OUT			al_mad_disp_handle_t* const	ph_mad_disp )
{
	CL_ASSERT( gp_spl_qp_mgr );
	return acquire_svc_disp( &gp_spl_qp_mgr->gsi_map, port_guid, ph_mad_disp );
}



/*
 * Acquire the service dispatcher for the given port.
 */
ib_api_status_t
acquire_svc_disp(
	IN		const	cl_qmap_t* const			p_svc_map,
	IN		const	ib_net64_t					port_guid,
		OUT			al_mad_disp_handle_t		*ph_mad_disp )
{
	cl_map_item_t*			p_svc_item;
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_svc_map );
	CL_ASSERT( gp_spl_qp_mgr );

	/* Search for the SMI or GSI service for the given port. */
	cl_spinlock_acquire( &gp_spl_qp_mgr->obj.lock );
	p_svc_item = cl_qmap_get( p_svc_map, port_guid );
	cl_spinlock_release( &gp_spl_qp_mgr->obj.lock );
	if( p_svc_item == cl_qmap_end( p_svc_map ) )
	{
		/* The port does not have an active agent. */
		AL_EXIT( AL_DBG_SMI );
		return IB_INVALID_GUID;
	}

	p_spl_qp_svc = PARENT_STRUCT( p_svc_item, spl_qp_svc_t, map_item );

	/* Found a match.  Get MAD dispatcher handle. */
	*ph_mad_disp = p_spl_qp_svc->h_mad_disp;

	/* Reference the MAD dispatcher on behalf of the client. */
	ref_al_obj( &p_spl_qp_svc->h_mad_disp->obj );

	AL_EXIT( AL_DBG_SMI );
	return IB_SUCCESS;
}



/*
 * Force a poll for CA attribute changes.
 */
void
force_smi_poll(
	void )
{
	AL_ENTER( AL_DBG_SMI );

	/*
	 * Stop the poll timer.  Just invoke the timer callback directly to
	 * save the thread context switching.
	 */
	smi_poll_timer_cb( gp_spl_qp_mgr );

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Poll for CA port attribute changes.
 */
void
smi_poll_timer_cb(
	IN				void*						context )
{
	cl_status_t			cl_status;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( context );
	CL_ASSERT( gp_spl_qp_mgr == context );
	UNUSED_PARAM( context );

	/*
	 * Scan for changes on the local HCAs.  Since the PnP manager has its
	 * own thread for processing changes, we kick off that thread in parallel
	 * reposting receive buffers to the SQP agents.
	 */
	pnp_poll();

	/*
	 * To handle the case where force_smi_poll is called at the same time
	 * the timer expires, check if the asynchronous processing item is in
	 * use.  If it is already in use, it means that we're about to poll
	 * anyway, so just ignore this call.
	 */
	cl_spinlock_acquire( &gp_spl_qp_mgr->obj.lock );

	/* Perform port processing on the special QP agents. */
	cl_qlist_apply_func( &gp_spl_qp_mgr->obj.obj_list, smi_post_recvs,
		gp_spl_qp_mgr );

	/* Determine if there are any special QP agents to poll. */
	if( !cl_is_qlist_empty( &gp_spl_qp_mgr->obj.obj_list ) && g_smi_poll_interval )
	{
		/* Restart the polling timer. */
		cl_status =
			cl_timer_start( &gp_spl_qp_mgr->poll_timer, g_smi_poll_interval );
		CL_ASSERT( cl_status == CL_SUCCESS );
	}
	cl_spinlock_release( &gp_spl_qp_mgr->obj.lock );

	AL_EXIT( AL_DBG_SMI );
}



/*
 * Post receive buffers to a special QP.
 */
void
smi_post_recvs(
	IN				cl_list_item_t*	const		p_list_item,
	IN				void*						context )
{
	al_obj_t*				p_obj;
	spl_qp_svc_t*			p_spl_qp_svc;

	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_list_item );
	UNUSED_PARAM( context );

	p_obj = PARENT_STRUCT( p_list_item, al_obj_t, pool_item );
	p_spl_qp_svc = PARENT_STRUCT( p_obj, spl_qp_svc_t, obj );

	cl_spinlock_acquire( &p_spl_qp_svc->obj.lock );
	if( p_spl_qp_svc->state != SPL_QP_ACTIVE )
	{
		cl_spinlock_release( &p_spl_qp_svc->obj.lock );
		return;
	}

	spl_qp_svc_post_recvs( p_spl_qp_svc );
	cl_spinlock_release( &p_spl_qp_svc->obj.lock );

	AL_EXIT( AL_DBG_SMI );
}
