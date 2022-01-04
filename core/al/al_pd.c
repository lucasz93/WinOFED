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

#include "al.h"
#include "al_av.h"
#include "al_ca.h"
#include "al_cq.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_pd.tmh"
#endif
#include "al_mgr.h"
#include "al_mr.h"
#include "al_mw.h"
#include "al_pd.h"
#include "al_qp.h"
#include "al_srq.h"
#include "al_verbs.h"

#include "ib_common.h"


void
destroying_pd(
	IN				struct _al_obj				*p_obj );


void
cleanup_pd(
	IN				struct _al_obj				*p_obj );


void
free_pd(
	IN				al_obj_t					*p_obj );



ib_api_status_t
alloc_pd(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_pd_type_t				pd_type,
	IN		const	void * const				pd_context,
		OUT			ib_pd_handle_t* const		ph_pd,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_pd_handle_t			h_pd;
	ib_api_status_t			status;
	al_obj_type_t			obj_type = AL_OBJ_TYPE_H_PD;

	CL_ASSERT( h_ca );

	if( !ph_pd )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Allocate a protection domain. */
	h_pd = (ib_pd_handle_t)cl_zalloc( sizeof( ib_pd_t ) );
	if( !h_pd )
	{
		return IB_INSUFFICIENT_MEMORY;
	}

	if( p_umv_buf )
		obj_type |= AL_OBJ_SUBTYPE_UM_EXPORT;

	/* Construct the PD. */
	construct_al_obj( &h_pd->obj, obj_type );
	cl_qlist_init( &h_pd->mw_list );

	status = init_al_obj( &h_pd->obj, pd_context, TRUE,
		destroying_pd, cleanup_pd, free_pd );
	if( status != IB_SUCCESS )
	{
		free_pd( &h_pd->obj );
		return status;
	}

	status = attach_al_obj( &h_ca->obj, &h_pd->obj );
	if( status != IB_SUCCESS )
	{
		h_pd->obj.pfn_destroy( &h_pd->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	h_pd->type = pd_type;
	switch( h_pd->type )
	{
	case IB_PDT_ALIAS:
		status = allocate_pd_alias( h_ca, h_pd );
		break;

	case IB_PDT_NORMAL:
	case IB_PDT_SQP:
	case IB_PDT_UD:
		/* Allocate the protection domain. */
		status = verbs_allocate_pd( h_ca, h_pd, p_umv_buf );
		break;

	default:
		status = IB_INVALID_PARAMETER;
	}

	if( status != IB_SUCCESS )
	{
		h_pd->obj.pfn_destroy( &h_pd->obj, NULL );
		return status;
	}

	*ph_pd = h_pd;

	return status;
}



ib_api_status_t
ib_dealloc_pd(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_PD );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	ref_al_obj( &h_pd->obj );
	h_pd->obj.pfn_destroy( &h_pd->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_PD );
	return IB_SUCCESS;
}



/*
 * Pre-destroy the protection domain.
 */
void
destroying_pd(
	IN				al_obj_t					*p_obj )
{
	ib_pd_handle_t			h_pd;
	ib_mw_handle_t			h_mw;
	cl_list_item_t			*p_list_item;
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	h_pd = PARENT_STRUCT( p_obj, ib_pd_t, obj );
	CL_ASSERT( h_pd );

	/*
	 * Deallocate all MW's before proceeding with destruction.  This ensures
	 * that all MW's have been destroyed before any MR's are.
	 */
	p_list_item = cl_qlist_head( &h_pd->mw_list );
	while( p_list_item != cl_qlist_end( &h_pd->mw_list ) )
	{
		h_mw = PARENT_STRUCT( p_list_item, ib_mw_t, pd_list_item );
		status = ib_destroy_mw( h_mw );
		CL_ASSERT( status == IB_SUCCESS );

		CL_ASSERT( p_list_item != cl_qlist_head( &h_pd->mw_list ) );
		p_list_item = cl_qlist_head( &h_pd->mw_list );
	}
}



void
cleanup_pd(
	IN				struct _al_obj				*p_obj )
{
	ib_pd_handle_t			h_pd;
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	h_pd = PARENT_STRUCT( p_obj, ib_pd_t, obj );

	/* Release the HW resources. */
	if( verbs_check_pd(h_pd))
	{
		if( h_pd->type != IB_PDT_ALIAS )
		{
			/* Deallocate the CI PD. */
			status = verbs_deallocate_pd(h_pd);
			CL_ASSERT( status == IB_SUCCESS );
		}
		else
		{
			deallocate_pd_alias( h_pd );
		}
	}
}



/*
 * Release all resources associated with the protection domain.
 */
void
free_pd(
	IN				al_obj_t					*p_obj )
{
	ib_pd_handle_t			h_pd;

	CL_ASSERT( p_obj );
	h_pd = PARENT_STRUCT( p_obj, ib_pd_t, obj );

	destroy_al_obj( p_obj );
	cl_free( h_pd );
}

ib_api_status_t
ib_create_srq(
	IN		const	ib_pd_handle_t			h_pd,
	IN		const	ib_srq_attr_t* const		p_srq_attr,
	IN		const	void* const					srq_context,
	IN		const	ib_pfn_event_cb_t			pfn_srq_event_cb OPTIONAL,
		OUT			ib_srq_handle_t* const		ph_srq )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SRQ );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	if( !p_srq_attr || !ph_srq)
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	if( !p_srq_attr->max_wr)
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MAX_WRS\n") );
		return IB_INVALID_MAX_WRS;
	}

	if (h_pd->obj.p_ci_ca && h_pd->obj.p_ci_ca->p_pnp_attr)
	{
		if (p_srq_attr->max_wr > h_pd->obj.p_ci_ca->p_pnp_attr->max_srq_wrs)
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MAX_WRS\n") );
			return IB_INVALID_MAX_WRS;
		}
		if (p_srq_attr->max_sge > h_pd->obj.p_ci_ca->p_pnp_attr->max_srq_sges)
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MAX_SGE\n") );
			return IB_INVALID_MAX_SGE;
		}
	}
	
	status = create_srq(
		h_pd, p_srq_attr, srq_context, pfn_srq_event_cb, ph_srq, NULL );

	/* Release the reference taken in init_al_obj (init_base_srq). */
	if( status == IB_SUCCESS )
		deref_ctx_al_obj( &(*ph_srq)->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_SRQ );
	return status;
}


ib_api_status_t
ib_create_qp(
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN		const	void* const					qp_context,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb OPTIONAL,
		OUT			ib_qp_handle_t* const		ph_qp )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}
	if( !p_qp_create || !ph_qp )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}
	if (h_pd->obj.p_ci_ca && h_pd->obj.p_ci_ca->p_pnp_attr)
	{
		if ((p_qp_create->rq_depth > h_pd->obj.p_ci_ca->p_pnp_attr->max_wrs) ||
			(p_qp_create->sq_depth > h_pd->obj.p_ci_ca->p_pnp_attr->max_wrs))
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MAX_WRS\n") );
			return IB_INVALID_MAX_WRS;
		}
		if ((p_qp_create->rq_sge > h_pd->obj.p_ci_ca->p_pnp_attr->max_sges) ||
			(p_qp_create->sq_sge > h_pd->obj.p_ci_ca->p_pnp_attr->max_sges))
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MAX_SGE\n") );
			return IB_INVALID_MAX_SGE;
		}
	}
	status = create_qp(
		h_pd, p_qp_create, qp_context, pfn_qp_event_cb, ph_qp, NULL );

	/* Release the reference taken in init_al_obj (init_base_qp). */
	if( status == IB_SUCCESS )
		deref_ctx_al_obj( &(*ph_qp)->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_QP );
	return status;
}



ib_api_status_t
ib_get_spl_qp(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN		const	void* const					qp_context,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb OPTIONAL,
		OUT			ib_pool_key_t* const		p_pool_key OPTIONAL,
		OUT			ib_qp_handle_t* const		ph_qp )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	status = get_spl_qp( h_pd, port_guid, p_qp_create, qp_context,
		pfn_qp_event_cb, p_pool_key, ph_qp, NULL );

	/* Release the reference taken in init_al_obj. */
	if( status == IB_SUCCESS )
		deref_ctx_al_obj( &(*ph_qp)->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_QP );
	return status;
}



ib_api_status_t
ib_create_av(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t* const			p_av_attr,
		OUT			ib_av_handle_t* const		ph_av )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_AV );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	status = create_av( h_pd, p_av_attr, ph_av, NULL );

	/* Release the reference taken in alloc_av. */
	if( status == IB_SUCCESS )
	{
		// temporary assert. There is a chance that LLD can return STATUS_SUCCESS with NULL handle, which is prohibit
		CL_ASSERT( (*ph_av)->h_ci_av );
		deref_al_obj( &(*ph_av)->obj );
	}

	AL_EXIT( AL_DBG_AV );
	return status;
}


#pragma warning(disable:4100) //unreferenced formal parameter
ib_api_status_t
ib_create_av_ctx(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t* const			p_av_attr,
	IN		uint16_t	ctx,
		OUT			ib_av_handle_t* const		ph_av)
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_AV );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	status = create_av( h_pd, p_av_attr, ph_av, NULL );

	/* Release the reference taken in alloc_av. */
	if( status == IB_SUCCESS )
	{
		// temporary assert. There is a chance that LLD can return STATUS_SUCCESS with NULL handle, which is prohibit
		CL_ASSERT( (*ph_av)->h_ci_av );
		deref_al_obj( &(*ph_av)->obj );

#if DBG
		insert_pool_trace(&g_av_trace, *ph_av, POOL_GET, ctx);
#endif
	}


	AL_EXIT( AL_DBG_AV );
	return status;
}



ib_api_status_t
ib_create_mw(
	IN		const	ib_pd_handle_t				h_pd,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mw_handle_t* const		ph_mw )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MW );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	status = create_mw( h_pd, p_rkey, ph_mw, NULL );

	if( status == IB_SUCCESS )
		deref_ctx_al_obj( &(*ph_mw)->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_MW );
	return status;
}



void
pd_insert_mw(
	IN		const	ib_mw_handle_t				h_mw )
{
	ib_pd_handle_t		h_pd;

	CL_ASSERT( h_mw );
	h_pd = PARENT_STRUCT( h_mw->obj.p_parent_obj, ib_pd_t, obj );

	cl_spinlock_acquire( &h_pd->obj.lock );
	cl_qlist_insert_tail( &h_pd->mw_list, &h_mw->pd_list_item );
	cl_spinlock_release( &h_pd->obj.lock );
}



void
pd_remove_mw(
	IN		const	ib_mw_handle_t				h_mw )
{
	ib_pd_handle_t		h_pd;

	CL_ASSERT( h_mw );
	h_pd = PARENT_STRUCT( h_mw->obj.p_parent_obj, ib_pd_t, obj );

	cl_spinlock_acquire( &h_pd->obj.lock );
	cl_qlist_remove_item( &h_pd->mw_list, &h_mw->pd_list_item );
	cl_spinlock_release( &h_pd->obj.lock );
}
