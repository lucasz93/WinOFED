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

#include <complib/cl_async_proc.h>
#include <complib/cl_memory.h>
#include <complib/cl_timer.h>

#include "al.h"
#include "al_av.h"
#include "al_ca.h"
#include "al_cm_cep.h"
#include "al_cq.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_qp.tmh"
#endif
#include "al_mad.h"
#include "al_mad_pool.h"
#include "al_mcast.h"
#include "al_mgr.h"
#include "al_mr.h"
#include "al_mw.h"
#include "al_pd.h"
#include "al_qp.h"
#include "al_query.h"
#ifdef CL_KERNEL
#include "al_smi.h"
#include "al_proxy_ndi.h"
#endif	/* CL_KERNEL */
#include "al_verbs.h"

#include "ib_common.h"


#define UNBOUND_PORT_GUID		0


extern ib_pool_handle_t			gh_mad_pool;


/*
 * Function prototypes.
 */
void
destroying_qp(
	IN				al_obj_t					*p_obj );

void
cleanup_qp(
	IN				al_obj_t					*p_obj );

void
free_qp(
	IN				al_obj_t					*p_obj );



ib_api_status_t
init_base_qp(
	IN				ib_qp_t* const				p_qp,
	IN		const	void* const					qp_context,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );

ib_api_status_t
init_raw_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid OPTIONAL,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );

ib_api_status_t
init_conn_qp(
	IN				al_conn_qp_t* const			p_conn_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );

ib_api_status_t
init_dgrm_qp(
	IN				al_dgrm_qp_t* const			p_dgrm_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );

ib_api_status_t
init_special_qp(
	IN				al_special_qp_t* const		p_special_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid,
	IN	OUT			ib_qp_create_t* const		p_qp_create );

ib_api_status_t
init_qp_alias(
	IN				al_qp_alias_t* const		p_qp_alias,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid,
	IN	OUT			ib_qp_create_t* const		p_qp_create );

ib_api_status_t
init_mad_qp(
	IN				al_mad_qp_t* const			p_mad_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb );

ib_api_status_t
init_mad_dgrm_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_dgrm_info_t* const		p_dgrm_info );


ib_api_status_t
al_modify_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
init_dgrm_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_dgrm_info_t* const		p_dgrm_info );

ib_api_status_t
mad_qp_post_recvs(
	IN				al_mad_qp_t*	const		p_mad_qp );

ib_api_status_t
ud_post_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t* const			p_send_wr,
		OUT			ib_send_wr_t				**pp_send_failure );

ib_api_status_t
special_qp_post_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t* const			p_send_wr,
		OUT			ib_send_wr_t				**pp_send_failure );

void
mad_qp_queue_mad(
	IN		const	ib_qp_handle_t				h_qp,
	IN				al_mad_wr_t* const			p_mad_wr );

void
mad_qp_resume_sends(
	IN				ib_qp_handle_t				h_qp );

void
mad_qp_flush_send(
	IN				al_mad_qp_t*				p_mad_qp,
	IN				al_mad_wr_t* const			p_mad_wr );

void
mad_recv_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

void
mad_send_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

void
mad_qp_comp(
	IN				al_mad_qp_t*				p_mad_qp,
	IN		const	ib_cq_handle_t				h_cq,
	IN				ib_wc_type_t				wc_type );

void
mad_qp_cq_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec );



/*
 * Allocates a structure to store QP information.
 */
ib_api_status_t
alloc_qp(
	IN		const	ib_qp_type_t				qp_type,
		OUT			ib_qp_handle_t* const		ph_qp )
{
	ib_qp_handle_t				h_qp;

	switch( qp_type )
	{
	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		h_qp = (ib_qp_handle_t)cl_zalloc( sizeof( al_conn_qp_t ) );
		break;

	case IB_QPT_UNRELIABLE_DGRM:
		h_qp = (ib_qp_handle_t)cl_zalloc( sizeof( al_dgrm_qp_t ) );
		break;

	case IB_QPT_QP0:
	case IB_QPT_QP1:
		h_qp = (ib_qp_handle_t)cl_zalloc( sizeof( al_special_qp_t ) );
		break;

	case IB_QPT_RAW_IPV6:
	case IB_QPT_RAW_ETHER:
		h_qp = (ib_qp_handle_t)cl_zalloc( sizeof( ib_qp_t ) );
		break;

	case IB_QPT_MAD:
		h_qp = (ib_qp_handle_t)cl_zalloc( sizeof( al_mad_qp_t ) );
		break;

	case IB_QPT_QP0_ALIAS:
	case IB_QPT_QP1_ALIAS:
		h_qp = (ib_qp_handle_t)cl_zalloc( sizeof( al_qp_alias_t ) );
		break;

	default:
		CL_ASSERT( qp_type == IB_QPT_RELIABLE_CONN ||
			qp_type == IB_QPT_UNRELIABLE_CONN ||
			qp_type == IB_QPT_UNRELIABLE_DGRM ||
			qp_type == IB_QPT_QP0 ||
			qp_type == IB_QPT_QP1 ||
			qp_type == IB_QPT_RAW_IPV6 ||
			qp_type == IB_QPT_RAW_ETHER ||
			qp_type == IB_QPT_MAD ||
			qp_type == IB_QPT_QP0_ALIAS ||
			qp_type == IB_QPT_QP1_ALIAS );
		return IB_INVALID_SETTING;
	}

	if( !h_qp )
	{
		return IB_INSUFFICIENT_MEMORY;
	}

	h_qp->type = qp_type;

	*ph_qp = h_qp;
	return IB_SUCCESS;
}



/*
 * Initializes the QP information structure.
 */
ib_api_status_t
create_qp(
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN		const	void* const					qp_context,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb,
		OUT			ib_qp_handle_t* const		ph_qp,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;
	ib_qp_handle_t			h_qp;

	if( !p_qp_create || !ph_qp )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	if (p_qp_create->h_srq && 
		AL_OBJ_INVALID_HANDLE( p_qp_create->h_srq, AL_OBJ_TYPE_H_SRQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SRQ_HANDLE\n") );
		return IB_INVALID_SRQ_HANDLE;
	}

	switch( p_qp_create->qp_type )
	{
	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		if( AL_OBJ_INVALID_HANDLE( p_qp_create->h_sq_cq, AL_OBJ_TYPE_H_CQ ) ||
			AL_OBJ_INVALID_HANDLE( p_qp_create->h_rq_cq, AL_OBJ_TYPE_H_CQ ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
			return IB_INVALID_CQ_HANDLE;
		}
		break;

	case IB_QPT_UNRELIABLE_DGRM:
		if( AL_OBJ_INVALID_HANDLE( p_qp_create->h_sq_cq, AL_OBJ_TYPE_H_CQ ) ||
			AL_OBJ_INVALID_HANDLE( p_qp_create->h_rq_cq, AL_OBJ_TYPE_H_CQ ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
			return IB_INVALID_CQ_HANDLE;
		}
		break;

	case IB_QPT_MAD:
		if( p_qp_create->h_sq_cq || p_qp_create->h_rq_cq )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
			return IB_INVALID_CQ_HANDLE;
		}
		break;

	default:
		CL_ASSERT( p_qp_create->qp_type == IB_QPT_RELIABLE_CONN ||
			p_qp_create->qp_type == IB_QPT_UNRELIABLE_CONN ||
			p_qp_create->qp_type == IB_QPT_UNRELIABLE_DGRM ||
			p_qp_create->qp_type == IB_QPT_MAD );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SETTING\n") );
		return IB_INVALID_SETTING;
	}

	
	/* Allocate a QP. */
	status = alloc_qp( p_qp_create->qp_type, &h_qp );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Init the base QP first. */
	status = init_base_qp( h_qp, qp_context, pfn_qp_event_cb, p_umv_buf );
	if( status != IB_SUCCESS )
		return status;

	/* Initialize the QP based on its type. */
	switch( h_qp->type )
	{
	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		status = init_conn_qp( (al_conn_qp_t*)h_qp, h_pd, p_qp_create, p_umv_buf );
		break;

	case IB_QPT_UNRELIABLE_DGRM:
		status = init_dgrm_qp( (al_dgrm_qp_t*)h_qp, h_pd, p_qp_create, p_umv_buf );
		break;

	case IB_QPT_MAD:
		status = init_mad_qp( (al_mad_qp_t*)h_qp, h_pd, p_qp_create,
			pfn_qp_event_cb );
		break;
	}

	if( status != IB_SUCCESS )
	{
		h_qp->obj.pfn_destroy( &h_qp->obj, NULL );
		return status;
	}

	*ph_qp = h_qp;

	/*
	 * Note that we don't release the reference taken in init_al_obj here.
	 * For kernel clients, it is release in ib_create_qp.  For user-mode
	 * clients is is released by the proxy after the handle is extracted.
	 */
	return IB_SUCCESS;
}



ib_api_status_t
get_spl_qp(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN		const	void* const					qp_context,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb,
		OUT			ib_pool_key_t* const		p_pool_key OPTIONAL,
		OUT			ib_qp_handle_t* const		ph_qp,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;
	ib_qp_handle_t			h_qp;

	if( !p_qp_create || !ph_qp )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Only allow creation of the special QP types. */
	switch( p_qp_create->qp_type )
	{
#ifdef CL_KERNEL
	case IB_QPT_QP0:
	case IB_QPT_QP1:
#endif
	case IB_QPT_QP0_ALIAS:
	case IB_QPT_QP1_ALIAS:
	case IB_QPT_RAW_IPV6:
	case IB_QPT_RAW_ETHER:
		break;				/* The QP type is valid. */

	default:
		return IB_INVALID_SETTING;
	}

	/* Allocate a QP. */
	status = alloc_qp( p_qp_create->qp_type, &h_qp );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Init the base QP first. */
	status = init_base_qp( h_qp, qp_context, pfn_qp_event_cb, p_umv_buf );
	if( status != IB_SUCCESS )
		return status;

	/* Initialize the QP based on its type. */
	switch( h_qp->type )
	{
#ifdef CL_KERNEL
	case IB_QPT_QP0:
	case IB_QPT_QP1:
		if( AL_OBJ_INVALID_HANDLE( p_qp_create->h_sq_cq, AL_OBJ_TYPE_H_CQ ) ||
			AL_OBJ_INVALID_HANDLE( p_qp_create->h_rq_cq, AL_OBJ_TYPE_H_CQ ) )
		{
			status = IB_INVALID_CQ_HANDLE;
			break;
		}
		status = init_special_qp( (al_special_qp_t*)h_qp, h_pd, port_guid,
			p_qp_create );
		break;
#endif	/* CL_KERNEL */

	case IB_QPT_QP0_ALIAS:
	case IB_QPT_QP1_ALIAS:
		if( p_qp_create->h_sq_cq || p_qp_create->h_rq_cq )
		{
			status = IB_INVALID_CQ_HANDLE;
			break;
		}
		status = init_alias_qp( (al_qp_alias_t*)h_qp, h_pd, port_guid,
			p_qp_create );
		if( status == IB_SUCCESS && p_pool_key )
		{
			/* Create a pool_key to access to the global MAD pool. */
			status = ib_reg_mad_pool( gh_mad_pool, h_pd,
				&((al_qp_alias_t*)h_qp)->pool_key );
			if( status == IB_SUCCESS )
			{
				/*
				 * Take a reference on the pool key since we don't have a
				 * mechanism for the pool key to clear the QP's pointer to it.
				 */
				ref_al_obj( &((al_qp_alias_t*)h_qp)->pool_key->obj );
				*p_pool_key = ((al_qp_alias_t*)h_qp)->pool_key;
			}
		}
		break;

	case IB_QPT_RAW_IPV6:
	case IB_QPT_RAW_ETHER:
		if( AL_OBJ_INVALID_HANDLE( p_qp_create->h_sq_cq, AL_OBJ_TYPE_H_CQ ) ||
			AL_OBJ_INVALID_HANDLE( p_qp_create->h_rq_cq, AL_OBJ_TYPE_H_CQ ) )
		{
			status = IB_INVALID_CQ_HANDLE;
			break;
		}
		status = init_raw_qp( h_qp, h_pd, port_guid, p_qp_create, p_umv_buf );
		break;

	default:
		CL_ASSERT( h_qp->type == IB_QPT_QP0 ||
			h_qp->type == IB_QPT_QP1 ||
			h_qp->type == IB_QPT_QP0_ALIAS ||
			h_qp->type == IB_QPT_QP1_ALIAS ||
			h_qp->type == IB_QPT_RAW_IPV6 ||
			h_qp->type == IB_QPT_RAW_ETHER );

		status = IB_INVALID_SETTING;
		break;
	}

	if( status != IB_SUCCESS )
	{
		h_qp->obj.pfn_destroy( &h_qp->obj, NULL );
		return status;
	}

	*ph_qp = h_qp;

	return IB_SUCCESS;
}


static ib_api_status_t
al_bad_modify_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	UNUSED_PARAM( h_qp );
	UNUSED_PARAM( p_qp_mod );
	UNUSED_PARAM( p_umv_buf );
	return IB_INVALID_PARAMETER;
}


static ib_api_status_t
al_bad_post_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t* const			p_send_wr,
	IN				ib_send_wr_t				**pp_send_failure OPTIONAL )
{
	UNUSED_PARAM( h_qp );
	UNUSED_PARAM( p_send_wr );
	UNUSED_PARAM( pp_send_failure );
	return IB_INVALID_PARAMETER;
}


static ib_api_status_t
al_bad_post_recv(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_recv_wr_t* const			p_recv_wr,
	IN				ib_recv_wr_t				**p_recv_failure OPTIONAL )
{
	UNUSED_PARAM( h_qp );
	UNUSED_PARAM( p_recv_wr );
	UNUSED_PARAM( p_recv_failure );
	return IB_INVALID_PARAMETER;
}


static ib_api_status_t
al_bad_init_dgrm_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_dgrm_info_t* const		p_dgrm_info )
{
	UNUSED_PARAM( h_qp );
	UNUSED_PARAM( p_dgrm_info );
	return IB_INVALID_PARAMETER;
}


static ib_api_status_t
al_bad_reg_mad_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mad_svc_t* const			p_mad_svc,
		OUT			ib_mad_svc_handle_t* const	ph_mad_svc )
{
	UNUSED_PARAM( h_qp );
	UNUSED_PARAM( p_mad_svc );
	UNUSED_PARAM( ph_mad_svc );
	return IB_INVALID_PARAMETER;
}


static ib_api_status_t
al_bad_dereg_mad_svc(
	IN		const	ib_mad_svc_handle_t			h_mad_svc )
{
	UNUSED_PARAM( h_mad_svc );
	return IB_INVALID_PARAMETER;
}


static void
al_bad_queue_mad(
	IN		const	ib_qp_handle_t				h_qp,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	UNUSED_PARAM( h_qp );
	UNUSED_PARAM( p_mad_wr );
}


static void
al_bad_resume_mad(
	IN		const	ib_qp_handle_t				h_qp )
{
	UNUSED_PARAM( h_qp );
	return;
}


static ib_api_status_t
al_bad_join_mcast(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mcast_req_t* const		p_mcast_req,
	OUT		ib_mcast_handle_t*					p_h_mcast	)
{
	UNUSED_PARAM( h_qp );
	UNUSED_PARAM( p_mcast_req );
	UNUSED_PARAM( p_h_mcast );
	return IB_INVALID_PARAMETER;
}


ib_api_status_t
init_base_qp(
	IN				ib_qp_t* const				p_qp,
	IN		const	void* const					qp_context,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;
	al_obj_type_t			obj_type = AL_OBJ_TYPE_H_QP;

	CL_ASSERT( p_qp );

	if( p_umv_buf )
		obj_type |= AL_OBJ_SUBTYPE_UM_EXPORT;

	construct_al_obj( &p_qp->obj, obj_type );
	status = init_al_obj( &p_qp->obj, qp_context, TRUE,
		destroying_qp, cleanup_qp, free_qp );
	if( status != IB_SUCCESS )
	{
		free_qp( &p_qp->obj );
		return status;
	}

	p_qp->pfn_event_cb = pfn_qp_event_cb;

	/*
	 * All function pointers should be invalid.  They will be set by
	 * derived QP types where appropriate.
	 */
	p_qp->pfn_modify_qp = al_bad_modify_qp;
	p_qp->pfn_post_recv = al_bad_post_recv;
	p_qp->pfn_post_send = al_bad_post_send;
	p_qp->pfn_reg_mad_svc = al_bad_reg_mad_svc;
	p_qp->pfn_dereg_mad_svc = al_bad_dereg_mad_svc;
	p_qp->pfn_queue_mad = al_bad_queue_mad;
	p_qp->pfn_resume_mad = al_bad_resume_mad;
	p_qp->pfn_init_dgrm_svc = al_bad_init_dgrm_svc;
	p_qp->pfn_join_mcast = al_bad_join_mcast;

	if( p_qp->type == IB_QPT_RELIABLE_CONN ||
		p_qp->type == IB_QPT_UNRELIABLE_CONN )
	{
		((al_conn_qp_t*)p_qp)->cid = AL_INVALID_CID;
	}

	return status;
}



ib_api_status_t
init_raw_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid OPTIONAL,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t				status;
	ib_qp_create_t				qp_create;
	ib_qp_attr_t				qp_attr;
	uint8_t						port_num;

	status = attach_al_obj( &h_pd->obj, &h_qp->obj );
	if( status != IB_SUCCESS )
		return status;

	/* Convert AL handles to CI handles. */
	qp_create = *p_qp_create;
	convert_qp_handle( qp_create );

	/* Clear the QP attributes to ensure non-set values are 0. */
	cl_memclr( &qp_attr, sizeof( ib_qp_attr_t ) );

	h_qp->port_guid = port_guid;

	/*
	 * Allocate a QP from the channel adapter.  Note that these calls
	 * set the send and receive pointers appropriately for posting
	 * work requests.
	 */
	if( port_guid == UNBOUND_PORT_GUID )
	{
		status =
			verbs_create_qp( h_pd, h_qp, &qp_create, &qp_attr, p_umv_buf );
	}
	else
	{
		status = get_port_num( h_pd->obj.p_ci_ca, port_guid, &port_num );
		if( status == IB_SUCCESS )
		{
			status = verbs_get_spl_qp( h_pd, port_num, h_qp,
				&qp_create, &qp_attr );
		}
	}

	if( status == IB_INVALID_SETTING )
	{
		p_qp_create->rq_depth		= qp_create.rq_depth;
		p_qp_create->sq_depth		= qp_create.sq_depth;
		p_qp_create->rq_sge			= qp_create.rq_sge;
		p_qp_create->sq_sge			= qp_create.sq_sge;
		p_qp_create->sq_max_inline	= qp_create.sq_max_inline;
	}	
	
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Override function pointers. */
	h_qp->pfn_modify_qp = al_modify_qp;

	if( h_qp->type == IB_QPT_UNRELIABLE_DGRM ||
		h_qp->type == IB_QPT_QP0 ||
		h_qp->type == IB_QPT_QP1 )
	{
		/* We have to mess with the AV handles. */
		h_qp->pfn_ud_post_send = h_qp->pfn_post_send;
		h_qp->h_ud_send_qp = h_qp->h_send_qp;

		h_qp->pfn_post_send = ud_post_send;
		h_qp->h_send_qp = h_qp;
	}

	h_qp->h_recv_cq = p_qp_create->h_rq_cq;
	h_qp->h_send_cq = p_qp_create->h_sq_cq;

	h_qp->recv_cq_rel.p_child_obj = (cl_obj_t*)h_qp;
	h_qp->send_cq_rel.p_child_obj = (cl_obj_t*)h_qp;

	cq_attach_qp( h_qp->h_recv_cq, &h_qp->recv_cq_rel );
	cq_attach_qp( h_qp->h_send_cq, &h_qp->send_cq_rel );

	h_qp->h_srq = p_qp_create->h_srq;
	h_qp->srq_rel.p_child_obj = (cl_obj_t*)h_qp;
	if (h_qp->h_srq)
		srq_attach_qp( h_qp->h_srq, &h_qp->srq_rel );

	h_qp->num = qp_attr.num;

	return IB_SUCCESS;
}



ib_api_status_t
init_conn_qp(
	IN				al_conn_qp_t* const			p_conn_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t				status;
	CL_ASSERT( p_conn_qp );

	/* Initialize the inherited QP first. */
	status = init_raw_qp( &p_conn_qp->qp, h_pd, UNBOUND_PORT_GUID,
		p_qp_create, p_umv_buf );

	return status;
}



ib_api_status_t
init_dgrm_qp(
	IN				al_dgrm_qp_t* const			p_dgrm_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t				status;
	CL_ASSERT( p_dgrm_qp );

	/* Initialize the inherited QP first. */
	status = init_raw_qp( p_dgrm_qp, h_pd, UNBOUND_PORT_GUID,
		p_qp_create, p_umv_buf );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Override function pointers. */
	p_dgrm_qp->pfn_init_dgrm_svc = init_dgrm_svc;
	p_dgrm_qp->pfn_join_mcast = al_join_mcast;

	return IB_SUCCESS;
}


#ifdef CL_KERNEL
ib_api_status_t
init_special_qp(
	IN				al_special_qp_t* const		p_special_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid,
	IN	OUT			ib_qp_create_t* const		p_qp_create )
{
	ib_api_status_t				status;
	CL_ASSERT( p_special_qp );

	/* Construct the special QP. */
	cl_qlist_init( &p_special_qp->to_send_queue );

	/* Initialize the inherited QP first. */
	status =
		init_raw_qp( &p_special_qp->qp, h_pd, port_guid, p_qp_create, NULL );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Override function pointers. */
	p_special_qp->qp.pfn_init_dgrm_svc = init_dgrm_svc;
	p_special_qp->qp.pfn_queue_mad = special_qp_queue_mad;
	p_special_qp->qp.pfn_resume_mad = special_qp_resume_sends;

	return IB_SUCCESS;
}


ib_api_status_t
init_qp_alias(
	IN				al_qp_alias_t* const		p_qp_alias,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid,
	IN	OUT			ib_qp_create_t* const		p_qp_create )
{
	ib_api_status_t				status;

	CL_ASSERT( p_qp_alias );
	UNUSED_PARAM( p_qp_create );

	if( h_pd->type != IB_PDT_ALIAS )
	{
		return IB_INVALID_PD_HANDLE;
	}

	status = attach_al_obj( &h_pd->obj, &p_qp_alias->qp.obj );
	if( status != IB_SUCCESS )
		return status;

	switch( p_qp_alias->qp.type )
	{
	case IB_QPT_QP0_ALIAS:
		status = acquire_smi_disp( port_guid, &p_qp_alias->h_mad_disp );
		break;

	case IB_QPT_QP1_ALIAS:
		status = acquire_gsi_disp( port_guid, &p_qp_alias->h_mad_disp );
		break;

	default:
		CL_ASSERT( p_qp_alias->qp.type == IB_QPT_QP0_ALIAS ||
			p_qp_alias->qp.type == IB_QPT_QP1_ALIAS );
		return IB_ERROR;
	}

	if( status != IB_SUCCESS )
		return status;

	/* Get a copy of the QP used by the MAD dispatcher. */
	ref_al_obj( &p_qp_alias->h_mad_disp->h_qp->obj );
	p_qp_alias->qp.h_ci_qp = p_qp_alias->h_mad_disp->h_qp->h_ci_qp;

	/* Override function pointers. */
	p_qp_alias->qp.pfn_reg_mad_svc = reg_mad_svc;

	return IB_SUCCESS;
}
#endif	/* CL_KERNEL */



ib_api_status_t
init_mad_qp(
	IN				al_mad_qp_t* const			p_mad_qp,
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb )
{
	ib_cq_create_t				cq_create;
	ib_qp_create_t				qp_create;
	ib_al_handle_t				h_al;
	ib_ca_handle_t				h_ca;
	ib_api_status_t				status;

	CL_ASSERT( p_mad_qp );

	/* Initialize the send and receive tracking queues. */
	cl_qlist_init( &p_mad_qp->to_send_queue );
	cl_qlist_init( &p_mad_qp->send_queue );
	cl_qlist_init( &p_mad_qp->recv_queue );

	/* The CQ handles must be NULL when creating a MAD queue pair. */
	if( p_qp_create->h_sq_cq || p_qp_create->h_rq_cq )
	{
		return IB_INVALID_SETTING;
	}

	/* Initialize the CQs used with the MAD QP. */
	cl_memclr( &cq_create, sizeof( ib_cq_create_t ) );

	/* Create the send CQ. */
	cq_create.size = p_qp_create->sq_depth;
	cq_create.pfn_comp_cb = mad_send_comp_cb;

	status = ib_create_cq( h_pd->obj.p_ci_ca->h_ca, &cq_create,
		p_mad_qp, mad_qp_cq_event_cb, &p_mad_qp->h_send_cq );

	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Reference the MAD QP on behalf of ib_create_cq. */
	ref_al_obj( &p_mad_qp->qp.obj );

	/* Create the receive CQ. */
	cq_create.size = p_qp_create->rq_depth;
	cq_create.pfn_comp_cb = mad_recv_comp_cb;

	h_ca = PARENT_STRUCT( h_pd->obj.p_parent_obj, ib_ca_t, obj );
	status = ib_create_cq( h_ca, &cq_create, p_mad_qp, mad_qp_cq_event_cb,
		&p_mad_qp->h_recv_cq );

	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Reference the MAD QP on behalf of ib_create_cq. */
	ref_al_obj( &p_mad_qp->qp.obj );

	/* Save the requested receive queue depth.  This is used to post MADs. */
	p_mad_qp->max_rq_depth = p_qp_create->rq_depth;

	/* Allocate a datagram QP for the MAD QP. */
	qp_create = *p_qp_create;
	qp_create.qp_type = IB_QPT_UNRELIABLE_DGRM;
	qp_create.sq_sge = 1;
	qp_create.rq_sge = 1;
	qp_create.h_rq_cq = p_mad_qp->h_recv_cq;
	qp_create.h_sq_cq = p_mad_qp->h_send_cq;

	status = ib_create_qp( h_pd, &qp_create, p_mad_qp, pfn_qp_event_cb,
		&p_mad_qp->h_dgrm_qp );

	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Reference the MAD QP on behalf of ib_create_qp. */
	ref_al_obj( &p_mad_qp->qp.obj );

	/* Create the MAD dispatch service. */
	status = create_mad_disp( &p_mad_qp->qp.obj, &p_mad_qp->qp,
		&p_mad_qp->h_mad_disp );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Override function pointers. */
	p_mad_qp->qp.pfn_init_dgrm_svc = init_mad_dgrm_svc;
	p_mad_qp->qp.pfn_queue_mad = mad_qp_queue_mad;
	p_mad_qp->qp.pfn_resume_mad = mad_qp_resume_sends;
	p_mad_qp->qp.pfn_reg_mad_svc = reg_mad_svc;

	/* The client's AL handle is the grandparent of the PD. */
	h_al = PARENT_STRUCT( h_pd->obj.p_parent_obj->p_parent_obj, ib_al_t, obj );

	/* Create a receive MAD pool. */
	status = ib_create_mad_pool( h_al, p_mad_qp->max_rq_depth + 16,	0, 16,
		&p_mad_qp->h_pool );

	if (status != IB_SUCCESS)
	{
		return status;
	}

	/*
	 * The MAD pool is a child of the client's AL instance.  If the client
	 * closes AL, the MAD pool will be destroyed before the MAD queue pair.
	 * Therefore, we hold a reference on the MAD pool to keep it from being
	 * destroyed until the MAD queue pair is destroyed.  Refer to the MAD
	 * queue pair cleanup code.
	 */
	ref_al_obj( &p_mad_qp->h_pool->obj );

	/* Register the MAD pool with the PD. */
	status = ib_reg_mad_pool( p_mad_qp->h_pool, h_pd, &p_mad_qp->pool_key );

	if (status != IB_SUCCESS)
	{
		return status;
	}

	/*
	 * Attach the MAD queue pair to the protection domain.  This must be
	 * done after creating the datagram queue pair and the MAD pool to set
	 * the correct order of object destruction.
	 */
	status = attach_al_obj( &h_pd->obj, &p_mad_qp->qp.obj );
	
	/* Get a copy of the CI datagram QP for ib_query_qp. */
	p_mad_qp->qp.h_ci_qp = p_mad_qp->h_dgrm_qp->h_ci_qp;

	return status;
}



ib_api_status_t
ib_destroy_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_QP );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}

	ref_al_obj( &h_qp->obj );
	h_qp->obj.pfn_destroy( &h_qp->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_QP );
	return IB_SUCCESS;
}



/*
 * Release any resources that must be cleaned up immediately, such as
 * any AL resources acquired by calling through the main API.
 */
void
destroying_qp(
	IN				al_obj_t					*p_obj )
{
	ib_qp_handle_t			h_qp;
	al_mad_qp_t				*p_mad_qp;
	al_qp_alias_t			*p_qp_alias;

	CL_ASSERT( p_obj );
	h_qp = PARENT_STRUCT( p_obj, ib_qp_t, obj );

	switch( h_qp->type )
	{
	case IB_QPT_MAD:
		/* Destroy QP and CQ services required for MAD QP support. */
		p_mad_qp = PARENT_STRUCT( h_qp, al_mad_qp_t, qp );

		if( p_mad_qp->h_dgrm_qp )
		{
			ib_destroy_qp( p_mad_qp->h_dgrm_qp,
				(ib_pfn_destroy_cb_t)deref_al_obj_cb );
			p_mad_qp->qp.h_ci_qp = NULL;
		}

		if( p_mad_qp->h_recv_cq )
		{
			ib_destroy_cq( p_mad_qp->h_recv_cq,
				(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		}

		if( p_mad_qp->h_send_cq )
		{
			ib_destroy_cq( p_mad_qp->h_send_cq,
				(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		}
		break;

	case IB_QPT_QP0_ALIAS:
	case IB_QPT_QP1_ALIAS:
		p_qp_alias = PARENT_STRUCT( h_qp, al_qp_alias_t, qp );

		if( p_qp_alias->pool_key )
		{
			ib_api_status_t		status;
			/* Deregister the pool_key. */
			status = dereg_mad_pool( p_qp_alias->pool_key, AL_KEY_ALIAS );
			if( status != IB_SUCCESS )
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("dereg_mad_pool returned %s.\n",
					ib_get_err_str(status)) );
				/* Release the reference taken when we created the pool key. */
				deref_al_obj( &p_qp_alias->pool_key->obj );
			}
			p_qp_alias->pool_key = NULL;
		}

		if( p_qp_alias->qp.h_ci_qp )
		{
			deref_al_obj( &p_qp_alias->h_mad_disp->h_qp->obj );
			p_qp_alias->qp.h_ci_qp = NULL;
		}

		/*
		 * If the pool_key still exists here, then the QP is being destroyed
		 * by destroying its parent (the PD).  Destruction of the PD will also
		 * destroy the pool_key.
		 */

		if( p_qp_alias->h_mad_disp )
			deref_al_obj( &p_qp_alias->h_mad_disp->obj );
		break;

	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		al_destroy_cep( h_qp->obj.h_al, &((al_conn_qp_t*)h_qp)->cid, FALSE );

		/* Fall through. */
	case IB_QPT_UNRELIABLE_DGRM:
	default:
		/* Multicast membership gets cleaned up by object hierarchy. */
		cq_detach_qp( h_qp->h_recv_cq, &h_qp->recv_cq_rel );
		cq_detach_qp( h_qp->h_send_cq, &h_qp->send_cq_rel );
		if (h_qp->h_srq)
			srq_detach_qp( h_qp->h_srq, &h_qp->srq_rel );
	}
}



/*
 * Release any HW resources.
 */
void
cleanup_qp(
	IN				al_obj_t					*p_obj )
{
	ib_qp_handle_t			h_qp;
	al_mad_qp_t*			p_mad_qp;
	al_mad_wr_t*			p_mad_wr;
	cl_list_item_t*			p_list_item;
	al_mad_element_t*		p_al_mad;
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	h_qp = PARENT_STRUCT( p_obj, ib_qp_t, obj );

	if( verbs_check_qp( h_qp ) )
	{
		status = verbs_destroy_qp( h_qp );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("verbs_destroy_qp failed with status %s.\n",
				ib_get_err_str(status)) );
		}
		h_qp->h_ci_qp = NULL;
	}

	if( h_qp->type == IB_QPT_MAD )
	{
		/* All MAD queue pair operations are complete. */
		p_mad_qp = PARENT_STRUCT( h_qp, al_mad_qp_t, qp );

		/* Append the pending MAD send queue to the posted MAD send queue. */
		cl_qlist_insert_list_tail( &p_mad_qp->send_queue,
			&p_mad_qp->to_send_queue );

		/* Complete all MAD sends as "flushed". */
		for( p_list_item = cl_qlist_remove_head( &p_mad_qp->send_queue );
			 p_list_item != cl_qlist_end( &p_mad_qp->send_queue );
			 p_list_item = cl_qlist_remove_head( &p_mad_qp->send_queue ) )
		{
			p_mad_wr = PARENT_STRUCT( p_list_item, al_mad_wr_t, list_item );
			mad_qp_flush_send( p_mad_qp, p_mad_wr );
		}

		/* Return any posted receive MAD elements to the pool. */
		for( p_list_item = cl_qlist_remove_head( &p_mad_qp->recv_queue );
			 p_list_item != cl_qlist_end( &p_mad_qp->recv_queue );
			 p_list_item = cl_qlist_remove_head( &p_mad_qp->recv_queue ) )
		{
			p_al_mad = PARENT_STRUCT( p_list_item, al_mad_element_t,
				list_item );

			status = ib_put_mad( &p_al_mad->element );
			CL_ASSERT( status == IB_SUCCESS );
		}

		if( p_mad_qp->h_pool )
		{
			/*
			 * Destroy the receive MAD pool.  If the client has closed the
			 * AL instance, the MAD pool should already be destroying.  In
			 * this case, we simply release our reference on the pool to
			 * allow it to cleanup and deallocate.  Otherwise, we initiate
			 * the destruction of the MAD pool and release our reference.
			 */
			cl_spinlock_acquire( &p_mad_qp->h_pool->obj.lock );
			if( p_mad_qp->h_pool->obj.state == CL_DESTROYING )
			{
				cl_spinlock_release( &p_mad_qp->h_pool->obj.lock );
			}
			else
			{
				cl_spinlock_release( &p_mad_qp->h_pool->obj.lock );
				ib_destroy_mad_pool( p_mad_qp->h_pool );
			}
			deref_al_obj( &p_mad_qp->h_pool->obj );
		}
	}
	else
	{
		if( h_qp->h_recv_cq )
			deref_al_obj( &h_qp->h_recv_cq->obj );
		if( h_qp->h_send_cq )
			deref_al_obj( &h_qp->h_send_cq->obj );
		if( h_qp->h_srq )
			deref_al_obj( &h_qp->h_srq->obj );
	}
}



void
free_qp(
	IN				al_obj_t					*p_obj )
{
	ib_qp_handle_t			h_qp;

	CL_ASSERT( p_obj );
	h_qp = PARENT_STRUCT( p_obj, ib_qp_t, obj );

	destroy_al_obj( p_obj );
	cl_free( h_qp );
}



ib_api_status_t
ib_query_qp(
	IN		const	ib_qp_handle_t				h_qp,
		OUT			ib_qp_attr_t* const			p_qp_attr )
{
	return query_qp( h_qp, p_qp_attr, NULL );
}


ib_api_status_t
query_qp(
	IN		const	ib_qp_handle_t				h_qp,
		OUT			ib_qp_attr_t* const			p_qp_attr,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}
	if( !p_qp_attr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_query_qp( h_qp, p_qp_attr );
	if( status != IB_SUCCESS )
	{
		AL_EXIT( AL_DBG_QP );
		return status;
	}

	/* Convert to using AL's handles. */
	p_qp_attr->h_pd = PARENT_STRUCT( h_qp->obj.p_parent_obj, ib_pd_t, obj );
	p_qp_attr->h_rq_cq = h_qp->h_recv_cq;
	p_qp_attr->h_sq_cq = h_qp->h_send_cq;
	p_qp_attr->qp_type = h_qp->type;
	p_qp_attr->h_srq = h_qp->h_srq;

	AL_EXIT( AL_DBG_QP );
	return IB_SUCCESS;
}



ib_api_status_t
ib_modify_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod )
{
	return modify_qp( h_qp, p_qp_mod, NULL );
}



ib_api_status_t
modify_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}
	if( !p_qp_mod )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = h_qp->pfn_modify_qp( h_qp, p_qp_mod, p_umv_buf );

	AL_EXIT( AL_DBG_QP );
	return status;
}



ib_api_status_t
al_modify_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;

	CL_ASSERT( h_qp );

#ifdef CL_KERNEL
	/* Only allow ERROR and RESET state changes during timewait. */
	if( (h_qp->type == IB_QPT_RELIABLE_CONN ||
		h_qp->type == IB_QPT_UNRELIABLE_CONN) &&
		p_qp_mod->req_state != IB_QPS_ERROR &&
		p_qp_mod->req_state != IB_QPS_RESET &&
		p_qp_mod->req_state != IB_QPS_INIT &&
		cl_get_time_stamp() < h_qp->timewait )
	{
		return IB_QP_IN_TIMEWAIT;
	}
#endif	/* CL_KERNEL */

	/* Modify the actual QP attributes. */
	status = verbs_modify_qp( h_qp, p_qp_mod, NULL );

	/* Record the QP state if the modify was successful. */
	if( status == IB_SUCCESS )
		h_qp->state = p_qp_mod->req_state;

	return status;
}


#ifdef CL_KERNEL

ib_api_status_t
ndi_modify_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod,
	IN		const	uint32_t					buf_size,
	IN				uint8_t* const				p_outbuf)
{
	ib_api_status_t			status;
	ib_qp_attr_t			qp_attr;

	CL_ASSERT( h_qp );

	/* Modify the actual QP attributes. */
	status = verbs_ndi_modify_qp( h_qp, p_qp_mod, qp_attr, buf_size, p_outbuf );

	/* Record the QP state if the modify was successful. */
	if( status == IB_SUCCESS )
		h_qp->state = p_qp_mod->req_state;

	return status;
}

#endif

ib_api_status_t
ib_init_dgrm_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_dgrm_info_t* const		p_dgrm_info OPTIONAL )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}

	switch( h_qp->type )
	{
	case IB_QPT_QP0:
	case IB_QPT_QP1:
	case IB_QPT_RAW_IPV6:
	case IB_QPT_RAW_ETHER:
		break;

	case IB_QPT_UNRELIABLE_DGRM:
	case IB_QPT_MAD:
		if( !p_dgrm_info )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("IB_INVALID_PARAMETER\n") );
			return IB_INVALID_PARAMETER;
		}
		break;

	default:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = h_qp->pfn_init_dgrm_svc( h_qp, p_dgrm_info );

	AL_EXIT( AL_DBG_QP );
	return status;
}



/*
 * Initialize a datagram QP to send and receive datagrams.
 */
ib_api_status_t
init_dgrm_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_dgrm_info_t* const		p_dgrm_info OPTIONAL )
{
	al_dgrm_qp_t			*p_dgrm_qp;
	ib_qp_mod_t				qp_mod;
	ib_api_status_t			status;

	CL_ASSERT( h_qp );

	p_dgrm_qp = (al_dgrm_qp_t*)h_qp;

	/* Change to the RESET state. */
	cl_memclr( &qp_mod, sizeof( ib_qp_mod_t ) );
	qp_mod.req_state = IB_QPS_RESET;

	status = ib_modify_qp( h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Change to the INIT state. */
	cl_memclr( &qp_mod, sizeof( ib_qp_mod_t ) );
	qp_mod.req_state = IB_QPS_INIT;
	if( p_dgrm_info )
	{
		qp_mod.state.init.qkey = p_dgrm_info->qkey;
		qp_mod.state.init.pkey_index = p_dgrm_info->pkey_index;
		status = get_port_num( h_qp->obj.p_ci_ca, p_dgrm_info->port_guid,
			&qp_mod.state.init.primary_port );
	}
	else
	{
		if( h_qp->type == IB_QPT_QP0 )
			qp_mod.state.init.qkey = 0;
		else
			qp_mod.state.init.qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		status = get_port_num( h_qp->obj.p_ci_ca, h_qp->port_guid,
			&qp_mod.state.init.primary_port );
	}
	if( status != IB_SUCCESS )
	{
		return status;
	}

	status = ib_modify_qp( h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Change to the RTR state. */
	cl_memclr( &qp_mod, sizeof( ib_qp_mod_t ) );
	qp_mod.req_state = IB_QPS_RTR;

	status = ib_modify_qp( h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Change to the RTS state. */
	cl_memclr( &qp_mod, sizeof( ib_qp_mod_t ) );
	qp_mod.req_state = IB_QPS_RTS;
	qp_mod.state.rts.sq_psn = CL_HTON32(cl_get_time_stamp_sec() & 0x00ffffff);
	status = ib_modify_qp( h_qp, &qp_mod );

	return status;
}



ib_api_status_t
init_mad_dgrm_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_dgrm_info_t* const		p_dgrm_info )
{
	al_mad_qp_t				*p_mad_qp;
	ib_api_status_t			status;

	CL_ASSERT( h_qp );

	p_mad_qp = (al_mad_qp_t*)h_qp;
	status = ib_init_dgrm_svc( p_mad_qp->h_dgrm_qp, p_dgrm_info );
	if( status != IB_SUCCESS )
	{
		return status;
	}

	/* Post receive buffers. */
	status = mad_qp_post_recvs( p_mad_qp );
	if (status != IB_SUCCESS)
	{
		return status;
	}

	/* Force a completion callback to rearm the CQs. */
	mad_send_comp_cb( p_mad_qp->h_send_cq, p_mad_qp );
	mad_recv_comp_cb( p_mad_qp->h_recv_cq, p_mad_qp );

	return status;
}



ib_api_status_t
ib_reg_mad_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mad_svc_t* const			p_mad_svc,
		OUT			ib_mad_svc_handle_t* const	ph_mad_svc )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MAD_SVC );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}

	status = h_qp->pfn_reg_mad_svc( h_qp, p_mad_svc, ph_mad_svc );

	/* Release the reference taken in init_al_obj. */
	if( status == IB_SUCCESS )
		deref_al_obj( &(*ph_mad_svc)->obj );

	AL_EXIT( AL_DBG_MAD_SVC );
	return status;
}


ib_api_status_t
ib_join_mcast(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mcast_req_t* const		p_mcast_req,
	OUT		ib_mcast_handle_t*					p_h_mcast	)
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MCAST );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}
	if( !p_mcast_req )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = h_qp->pfn_join_mcast( h_qp, p_mcast_req, p_h_mcast );

	AL_EXIT( AL_DBG_MCAST );
	return status;
}


ib_api_status_t
ib_join_mcast_no_qp(
	IN		const	ib_mcast_req_t* const		p_mcast_req,
	OUT		ib_mcast_handle_t*					p_h_mcast	)
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MCAST );

	if( !p_mcast_req )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = al_join_mcast_no_qp( p_mcast_req, p_h_mcast);

	AL_EXIT( AL_DBG_MCAST );
	return status;
}



/*
 * Post a work request to the send queue of the QP.
 */
ib_api_status_t
ib_post_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t* const			p_send_wr,
		OUT			ib_send_wr_t				**pp_send_failure OPTIONAL )
{
	ib_api_status_t			status;
	PERF_DECLARE( IbPostSend );
	PERF_DECLARE( PostSend );

	cl_perf_start( IbPostSend );
	AL_ENTER( AL_DBG_QP );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}
	if( !p_send_wr || ( p_send_wr->p_next && !pp_send_failure ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	cl_perf_start( PostSend );
	status =
		h_qp->pfn_post_send( h_qp->h_send_qp, p_send_wr, pp_send_failure );
	cl_perf_stop( &g_perf, PostSend );

	AL_EXIT( AL_DBG_QP );
	cl_perf_stop( &g_perf, IbPostSend );
	return status;
}



ib_api_status_t
ud_post_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t* const			p_send_wr,
		OUT			ib_send_wr_t				**pp_send_failure )
{
	ib_api_status_t			status;
	ib_send_wr_t			*p_wr;

	CL_ASSERT( h_qp );

	/* Convert all AV handles for verb provider usage. */
	for( p_wr = p_send_wr; p_wr; p_wr = p_wr->p_next )
	{
		CL_ASSERT( p_wr->dgrm.ud.h_av );
		p_wr->dgrm.ud.rsvd = p_wr->dgrm.ud.h_av;
		p_wr->dgrm.ud.h_av = convert_av_handle( h_qp, p_wr->dgrm.ud.h_av );
	}

	status = h_qp->pfn_ud_post_send(
		h_qp->h_ud_send_qp, p_send_wr, pp_send_failure );

	/* Restore all AV handles. */
	for( p_wr = p_send_wr; p_wr; p_wr = p_wr->p_next )
		p_wr->dgrm.ud.h_av = (ib_av_handle_t)p_wr->dgrm.ud.rsvd;

	return status;
}



#ifdef CL_KERNEL
/*
 * Post a work request to the send queue of a special QP.
 * The special QP is owned by the GSA or SMA, so care must be taken to prevent
 * overruning the QP by multiple owners.
 */
void
special_qp_queue_mad(
	IN		const	ib_qp_handle_t				h_qp,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	al_special_qp_t*		p_special_qp;

	CL_ASSERT( h_qp );
	CL_ASSERT( p_mad_wr );

	p_special_qp = (al_special_qp_t*)h_qp;

	/* Queue the send work request. */
	cl_spinlock_acquire( &h_qp->obj.lock );
	cl_qlist_insert_tail( &p_special_qp->to_send_queue, &p_mad_wr->list_item );
	cl_spinlock_release( &h_qp->obj.lock );
}



void
special_qp_resume_sends(
	IN		const	ib_qp_handle_t				h_qp )
{
	al_special_qp_t*		p_special_qp;
	cl_list_item_t*			p_list_item;
	al_mad_wr_t*			p_mad_wr;
	ib_api_status_t			status;

	CL_ASSERT( h_qp );
	p_special_qp = (al_special_qp_t*)h_qp;

	cl_spinlock_acquire( &p_special_qp->qp.obj.lock );

	for( p_list_item = cl_qlist_remove_head( &p_special_qp->to_send_queue );
		 p_list_item != cl_qlist_end( &p_special_qp->to_send_queue );
		 p_list_item = cl_qlist_remove_head( &p_special_qp->to_send_queue ) )
	{
		p_mad_wr = PARENT_STRUCT( p_list_item, al_mad_wr_t, list_item );

		cl_spinlock_release( &p_special_qp->qp.obj.lock );
		status = spl_qp_svc_send( &p_special_qp->qp, &p_mad_wr->send_wr );
		cl_spinlock_acquire( &p_special_qp->qp.obj.lock );

		if( status != IB_SUCCESS )
		{
			cl_qlist_insert_head( &p_special_qp->to_send_queue, p_list_item );
			break;
		}
	}

	cl_spinlock_release( &p_special_qp->qp.obj.lock );
}
#endif	/* CL_KERNEL */


void
mad_qp_queue_mad(
	IN		const	ib_qp_handle_t				h_qp,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	al_mad_qp_t				*p_mad_qp;

	CL_ASSERT( h_qp );
	p_mad_qp = (al_mad_qp_t*)h_qp;

	/* Queue the send work request on the to_send_queue. */
	cl_spinlock_acquire( &p_mad_qp->qp.obj.lock );
	cl_qlist_insert_tail( &p_mad_qp->to_send_queue, &p_mad_wr->list_item );
	cl_spinlock_release( &p_mad_qp->qp.obj.lock );
}



void
mad_qp_resume_sends(
	IN				ib_qp_handle_t				h_qp )
{
	al_mad_qp_t				*p_mad_qp;
	cl_list_item_t*			p_list_item;
	al_mad_wr_t*			p_mad_wr;
	ib_api_status_t			status;

	CL_ASSERT( h_qp );

	p_mad_qp = (al_mad_qp_t*)h_qp;

	cl_spinlock_acquire( &p_mad_qp->qp.obj.lock );

	/* Do not post sends if the MAD queue pair is being destroyed. */
	if( p_mad_qp->qp.obj.state == CL_DESTROYING )
	{
		cl_spinlock_release( &p_mad_qp->qp.obj.lock );
		return;
	}

	for( p_list_item = cl_qlist_remove_head( &p_mad_qp->to_send_queue );
		 p_list_item != cl_qlist_end( &p_mad_qp->to_send_queue );
		 p_list_item = cl_qlist_remove_head( &p_mad_qp->to_send_queue ) )
	{
		p_mad_wr = PARENT_STRUCT( p_list_item, al_mad_wr_t, list_item );

		/* Always generate send completions. */
		p_mad_wr->send_wr.send_opt |= IB_SEND_OPT_SIGNALED;

		status = ib_post_send( p_mad_qp->h_dgrm_qp, &p_mad_wr->send_wr, NULL );

		if( status == IB_SUCCESS )
		{
			/* Queue the MAD work request on the send tracking queue. */
			cl_qlist_insert_tail( &p_mad_qp->send_queue, &p_mad_wr->list_item );
		}
		else
		{
			/* Re-queue the send work request on the to_send_queue. */
			cl_qlist_insert_head( &p_mad_qp->to_send_queue, p_list_item );
			break;
		}
	}

	cl_spinlock_release( &p_mad_qp->qp.obj.lock );
}



void
mad_qp_flush_send(
	IN				al_mad_qp_t*				p_mad_qp,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_wc_t					wc;

	cl_memclr( &wc, sizeof( ib_wc_t ) );
	wc.wr_id = p_mad_wr->send_wr.wr_id;
	wc.wc_type = IB_WC_SEND;
	wc.status = IB_WCS_WR_FLUSHED_ERR;

	mad_disp_send_done( p_mad_qp->h_mad_disp, p_mad_wr, &wc );
}



ib_api_status_t
ib_post_recv(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_recv_wr_t* const			p_recv_wr,
		OUT			ib_recv_wr_t				**pp_recv_failure OPTIONAL )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}
	if( !p_recv_wr || ( p_recv_wr->p_next && !pp_recv_failure ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status =
		h_qp->pfn_post_recv( h_qp->h_recv_qp, p_recv_wr, pp_recv_failure );

	AL_EXIT( AL_DBG_QP );
	return status;
}



/*
 * Post receive buffers to a MAD QP.
 */
ib_api_status_t
mad_qp_post_recvs(
	IN				al_mad_qp_t*	const		p_mad_qp )
{
	ib_mad_element_t*		p_mad_element;
	al_mad_element_t*		p_al_element;
	ib_recv_wr_t			recv_wr;
	ib_api_status_t			status = IB_SUCCESS;

	CL_ASSERT( p_mad_qp );

	/* Attempt to post receive buffers up to the max_rq_depth limit. */
	cl_spinlock_acquire( &p_mad_qp->qp.obj.lock );
	while( p_mad_qp->cur_rq_depth < (int32_t)p_mad_qp->max_rq_depth )
	{
		/* Get a MAD element from the pool. */
		status = ib_get_mad( p_mad_qp->pool_key, MAD_BLOCK_SIZE,
			&p_mad_element );

		if( status != IB_SUCCESS ) break;

		p_al_element = PARENT_STRUCT( p_mad_element, al_mad_element_t,
			element );

		/* Build the receive work request. */
		recv_wr.p_next	 = NULL;
		recv_wr.wr_id	 = (uintn_t)p_al_element;
		recv_wr.num_ds = 1;
		recv_wr.ds_array = &p_al_element->grh_ds;

		/* Queue the receive on the service tracking list. */
		cl_qlist_insert_tail( &p_mad_qp->recv_queue, &p_al_element->list_item );

		/* Post the receive. */
		status = ib_post_recv( p_mad_qp->h_dgrm_qp, &recv_wr, NULL );

		if( status != IB_SUCCESS )
		{
			cl_qlist_remove_item( &p_mad_qp->recv_queue,
				&p_al_element->list_item );

			ib_put_mad( p_mad_element );
			break;
		}

		cl_atomic_inc( &p_mad_qp->cur_rq_depth );
	}
	cl_spinlock_release( &p_mad_qp->qp.obj.lock );

	return status;
}



void
mad_recv_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	al_mad_qp_t				*p_mad_qp;

	CL_ASSERT( cq_context );
	p_mad_qp = (al_mad_qp_t*)cq_context;

	CL_ASSERT( h_cq == p_mad_qp->h_recv_cq );
	mad_qp_comp( p_mad_qp, h_cq, IB_WC_RECV );
}



void
mad_send_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	al_mad_qp_t				*p_mad_qp;

	CL_ASSERT( cq_context );
	p_mad_qp = (al_mad_qp_t*)cq_context;

	CL_ASSERT( h_cq == p_mad_qp->h_send_cq );
	mad_qp_comp( p_mad_qp, h_cq, IB_WC_SEND );

	/* Continue processing any queued MADs on the QP. */
	mad_qp_resume_sends( &p_mad_qp->qp );
}



void
mad_qp_comp(
	IN				al_mad_qp_t*				p_mad_qp,
	IN		const	ib_cq_handle_t				h_cq,
	IN				ib_wc_type_t				wc_type )
{
	ib_wc_t					wc;
	ib_wc_t*				p_free_wc = &wc;
	ib_wc_t*				p_done_wc;
	al_mad_wr_t*			p_mad_wr;
	al_mad_element_t*		p_al_mad;
	ib_mad_element_t*		p_mad_element;
	ib_api_status_t			status;

	CL_ASSERT( p_mad_qp );
	CL_ASSERT( h_cq );

	wc.p_next = NULL;
	/* Process work completions. */
	while( ib_poll_cq( h_cq, &p_free_wc, &p_done_wc ) == IB_SUCCESS )
	{
		/* Process completions one at a time. */

		/*
		 * Process the work completion.  Per IBA specification, the
		 * wc.wc_type is undefined if wc.status is not IB_WCS_SUCCESS.
		 * Use the wc_type function parameter instead of wc.wc_type.
		 */
		switch( wc_type )
		{
		case IB_WC_SEND:
			/* Get a pointer to the MAD work request. */
			p_mad_wr = (al_mad_wr_t*)((uintn_t)wc.wr_id);

			/* Remove the MAD work request from the send tracking queue. */
			cl_spinlock_acquire( &p_mad_qp->qp.obj.lock );
			cl_qlist_remove_item( &p_mad_qp->send_queue, &p_mad_wr->list_item );
			cl_spinlock_release( &p_mad_qp->qp.obj.lock );

			/* Report the send completion to the dispatcher. */
			mad_disp_send_done( p_mad_qp->h_mad_disp, p_mad_wr, &wc );
			break;

		case IB_WC_RECV:
			/* A receive buffer was consumed. */
			cl_atomic_dec( &p_mad_qp->cur_rq_depth );

			/* Replenish the receive buffer. */
			mad_qp_post_recvs( p_mad_qp );

			/* Initialize pointers to the MAD element. */
			p_al_mad = (al_mad_element_t*)((uintn_t)wc.wr_id);
			p_mad_element = &p_al_mad->element;

			/* Remove the AL MAD element from the receive tracking queue. */
			cl_spinlock_acquire( &p_mad_qp->qp.obj.lock );
			cl_qlist_remove_item( &p_mad_qp->recv_queue, &p_al_mad->list_item );
			cl_spinlock_release( &p_mad_qp->qp.obj.lock );

			/* Construct the MAD element from the receive work completion. */
			build_mad_recv( p_mad_element, &wc );

			/* Process the received MAD. */
			status = mad_disp_recv_done( p_mad_qp->h_mad_disp,
				p_mad_element );

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
		p_free_wc = &wc;
	}

	status = ib_rearm_cq( h_cq, FALSE );
	CL_ASSERT( status == IB_SUCCESS );
}



/*
 * Process an event on a CQ associated with a MAD QP.
 */
void
mad_qp_cq_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	al_mad_qp_t				*p_mad_qp;

	CL_ASSERT( p_event_rec );
	CL_ASSERT( p_event_rec->context );

	if( p_event_rec->code == IB_AE_SQ_DRAINED )
		return;

	p_mad_qp = (al_mad_qp_t*)p_event_rec->context;

	/* Nothing to do here. */
}



/*
 * Process an asynchronous event on the QP.  Notify the user of the event.
 */
void
qp_async_event_cb(
	IN				ib_async_event_rec_t* const	p_event_rec )
{
	ib_qp_handle_t			h_qp;

	CL_ASSERT( p_event_rec );
	h_qp = (ib_qp_handle_t)p_event_rec->context;

#if defined(CL_KERNEL)
	switch( p_event_rec->code )
	{
	case IB_AE_QP_COMM:
		al_cep_established( h_qp->obj.h_al, ((al_conn_qp_t*)h_qp)->cid );
		break;

	case IB_AE_QP_APM:
		al_cep_migrate( h_qp->obj.h_al, ((al_conn_qp_t*)h_qp)->cid );
		break;

	case IB_AE_QP_APM_ERROR:
		//***TODO: Figure out how to handle these errors.
		break;

	default:
		break;
	}
#endif

	p_event_rec->context = (void*)h_qp->obj.context;
	p_event_rec->handle.h_qp = h_qp;

	if( h_qp->pfn_event_cb )
		h_qp->pfn_event_cb( p_event_rec );
}



ib_api_status_t
ib_bind_mw(
	IN		const	ib_mw_handle_t				h_mw,
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_bind_wr_t * const		p_mw_bind,
		OUT			net32_t * const				p_rkey )
{
	ib_mr_handle_t			h_mr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MW );

	if( AL_OBJ_INVALID_HANDLE( h_mw, AL_OBJ_TYPE_H_MW ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MW_HANDLE\n") );
		return IB_INVALID_MW_HANDLE;
	}
	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}
	if( !p_mw_bind || !p_rkey )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Convert to the CI handles. */
	h_mr = p_mw_bind->h_mr;
	p_mw_bind->h_mr = convert_mr_handle( h_mr );

	status = verbs_bind_mw(h_mw, h_qp, p_mw_bind, p_rkey);

	p_mw_bind->h_mr = h_mr;

	AL_EXIT( AL_DBG_MW );
	return status;
}
