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
#include "al_ca.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_srq.tmh"
#endif
#include "al_mgr.h"
#include "al_mr.h"
#include "al_pd.h"
#include "al_srq.h"
#include "al_verbs.h"

#include "ib_common.h"

/*
 * Function prototypes.
 */
void
destroying_srq(
	IN				struct _al_obj				*p_obj );

void
cleanup_srq(
	IN				al_obj_t					*p_obj );

void
free_srq(
	IN				al_obj_t					*p_obj );


ib_destroy_srq(
	IN		const	ib_srq_handle_t				h_srq,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_SRQ );

	if( AL_OBJ_INVALID_HANDLE( h_srq, AL_OBJ_TYPE_H_SRQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SRQ_HANDLE\n") );
		return IB_INVALID_SRQ_HANDLE;
	}

	/* Don't destroy while there are bound QPs. */
	cl_spinlock_acquire( &h_srq->obj.lock );
	if (!cl_is_qlist_empty( &h_srq->qp_list ))
	{
		cl_spinlock_release( &h_srq->obj.lock );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_RESOURCE_BUSY\n") );
		return IB_RESOURCE_BUSY;
	}
	cl_spinlock_release( &h_srq->obj.lock );

	ref_al_obj( &h_srq->obj );
	h_srq->obj.pfn_destroy( &h_srq->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_SRQ );
	return IB_SUCCESS;
}


void
destroying_srq(
	IN				struct _al_obj				*p_obj )
{
	ib_srq_handle_t		h_srq;
	cl_list_item_t		*p_item;
	cl_obj_rel_t		*p_rel;
	ib_qp_handle_t		h_qp;

	CL_ASSERT( p_obj );
	h_srq = PARENT_STRUCT( p_obj, ib_srq_t, obj );

	/* Initiate destruction of all bound QPs. */
	cl_spinlock_acquire( &h_srq->obj.lock );
	for( p_item = cl_qlist_remove_tail( &h_srq->qp_list );
		p_item != cl_qlist_end( &h_srq->qp_list );
		p_item = cl_qlist_remove_tail( &h_srq->qp_list ) )
	{
		p_rel = PARENT_STRUCT( p_item, cl_obj_rel_t, pool_item.list_item );
		p_rel->p_parent_obj = NULL;
		h_qp = (ib_qp_handle_t)p_rel->p_child_obj;
		if( h_qp )
		{
			/* Take a reference to prevent the QP from being destroyed. */
			ref_al_obj( &h_qp->obj );
			cl_spinlock_release( &h_srq->obj.lock );
			h_qp->obj.pfn_destroy( &h_qp->obj, NULL );
			cl_spinlock_acquire( &h_srq->obj.lock );
		}
	}
	cl_spinlock_release( &h_srq->obj.lock );
}

void
cleanup_srq(
	IN				struct _al_obj				*p_obj )
{
	ib_srq_handle_t			h_srq;
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	h_srq = PARENT_STRUCT( p_obj, ib_srq_t, obj );

	/* Deallocate the CI srq. */
	if( verbs_check_srq( h_srq ) )
	{
		status = verbs_destroy_srq( h_srq );
		CL_ASSERT( status == IB_SUCCESS );
	}
}


/*
 * Release all resources associated with the completion queue.
 */
void
free_srq(
	IN				al_obj_t					*p_obj )
{
	ib_srq_handle_t			h_srq;

	CL_ASSERT( p_obj );
	h_srq = PARENT_STRUCT( p_obj, ib_srq_t, obj );

	destroy_al_obj( &h_srq->obj );
	cl_free( h_srq );
}


void
srq_attach_qp(
	IN		const	ib_srq_handle_t				h_srq,
	IN				cl_obj_rel_t* const			p_qp_rel )
{
	p_qp_rel->p_parent_obj = (cl_obj_t*)h_srq;
	ref_al_obj( &h_srq->obj );
	cl_spinlock_acquire( &h_srq->obj.lock );
	cl_qlist_insert_tail( &h_srq->qp_list, &p_qp_rel->pool_item.list_item );
	cl_spinlock_release( &h_srq->obj.lock );
}


void
srq_detach_qp(
	IN		const	ib_srq_handle_t				h_srq,
	IN				cl_obj_rel_t* const			p_qp_rel )
{
	if( p_qp_rel->p_parent_obj )
	{
		CL_ASSERT( p_qp_rel->p_parent_obj == (cl_obj_t*)h_srq );
		p_qp_rel->p_parent_obj = NULL;
		cl_spinlock_acquire( &h_srq->obj.lock );
		cl_qlist_remove_item( &h_srq->qp_list, &p_qp_rel->pool_item.list_item );
		cl_spinlock_release( &h_srq->obj.lock );
	}
}


ib_api_status_t
ib_modify_srq(
	IN		const	ib_srq_handle_t		h_srq,
	IN		const	ib_srq_attr_t* const		p_srq_attr,
	IN		const	ib_srq_attr_mask_t			srq_attr_mask )
{
	return modify_srq( h_srq, p_srq_attr, srq_attr_mask, NULL );
}


ib_api_status_t
modify_srq(
	IN		const	ib_srq_handle_t		h_srq,
	IN		const	ib_srq_attr_t* const		p_srq_attr,
	IN		const	ib_srq_attr_mask_t			srq_attr_mask,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;
    
	AL_ENTER( AL_DBG_SRQ );

	if( AL_OBJ_INVALID_HANDLE( h_srq, AL_OBJ_TYPE_H_SRQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SRQ_HANDLE\n") );
		return IB_INVALID_SRQ_HANDLE;
	}

	if( !p_srq_attr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	if( !( srq_attr_mask & (IB_SRQ_MAX_WR |IB_SRQ_LIMIT)) ||
		( srq_attr_mask & ~(IB_SRQ_MAX_WR |IB_SRQ_LIMIT)))
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_SETTING;
	}

	if((srq_attr_mask & IB_SRQ_LIMIT)  && h_srq->obj.p_ci_ca && h_srq->obj.p_ci_ca->p_pnp_attr )
	{
		if (p_srq_attr->srq_limit > h_srq->obj.p_ci_ca->p_pnp_attr->max_srq_wrs)
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SETTING\n") );
			return IB_INVALID_SETTING;
		}
	}

	if((srq_attr_mask & IB_SRQ_MAX_WR) &&  !p_srq_attr->max_wr)
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SETTING\n") );
		return IB_INVALID_SETTING;
	}

	if ((srq_attr_mask & IB_SRQ_MAX_WR) && h_srq->obj.p_ci_ca && h_srq->obj.p_ci_ca->p_pnp_attr)
	{
		if (p_srq_attr->max_wr > h_srq->obj.p_ci_ca->p_pnp_attr->max_srq_wrs)
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MAX_WRS\n") );
			return IB_INVALID_MAX_WRS;
		}
	}

	status = verbs_modify_srq( h_srq, p_srq_attr, srq_attr_mask );

	AL_EXIT( AL_DBG_SRQ );
	return status;
}



ib_api_status_t
ib_query_srq(
	IN		const	ib_srq_handle_t			h_srq,
		OUT 		ib_srq_attr_t* const			p_srq_attr )
{
	return query_srq( h_srq, p_srq_attr, NULL );
}



ib_api_status_t
query_srq(
	IN		const	ib_srq_handle_t			h_srq,
		OUT 		ib_srq_attr_t* const			p_srq_attr,
	IN	OUT 		ci_umv_buf_t* const 		p_umv_buf )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SRQ );

	if( AL_OBJ_INVALID_HANDLE( h_srq, AL_OBJ_TYPE_H_SRQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SRQ_HANDLE\n") );
		return IB_INVALID_SRQ_HANDLE;
	}
	if( !p_srq_attr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_query_srq( h_srq, p_srq_attr );

	AL_EXIT( AL_DBG_SRQ );
	return status;
}


/*
 * Initializes the QP information structure.
 */
ib_api_status_t
create_srq(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_srq_attr_t* const			p_srq_attr,
	IN		const	void* const					srq_context,
	IN		const	ib_pfn_event_cb_t				pfn_srq_event_cb,
		OUT			ib_srq_handle_t* const			ph_srq,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_srq_handle_t			h_srq;
	ib_api_status_t			status;
	al_obj_type_t			obj_type = AL_OBJ_TYPE_H_SRQ;

	h_srq = cl_zalloc( sizeof( ib_srq_t ) );
	if( !h_srq )
	{
		return IB_INSUFFICIENT_MEMORY;
	}
	
	if( p_umv_buf )
		obj_type |= AL_OBJ_SUBTYPE_UM_EXPORT;

	/* Construct the SRQ. */
	construct_al_obj( &h_srq->obj, obj_type );

	cl_qlist_init( &h_srq->qp_list );
	h_srq->pfn_event_cb = pfn_srq_event_cb;

	/* Initialize the SRQ. */
	status = init_al_obj( &h_srq->obj, srq_context, TRUE,
		destroying_srq, cleanup_srq, free_srq );
	if( status != IB_SUCCESS )
	{
		free_srq( &h_srq->obj );
		return status;
	}
	status = attach_al_obj( &h_pd->obj, &h_srq->obj );
	if( status != IB_SUCCESS )
	{
		h_srq->obj.pfn_destroy( &h_srq->obj, NULL );
		return status;
	}

	status = verbs_create_srq( h_pd, h_srq, p_srq_attr, p_umv_buf );
	if( status != IB_SUCCESS )
	{
		h_srq->obj.pfn_destroy( &h_srq->obj, NULL );
		return status;
	}

	*ph_srq = h_srq;

	/*
	 * Note that we don't release the reference taken in init_al_obj here.
	 * For kernel clients, it is release in ib_create_srq.  For user-mode
	 * clients is released by the proxy after the handle is extracted.
	 */
	return IB_SUCCESS;
}


/*
 * Process an asynchronous event on the QP.  Notify the user of the event.
 */
void
srq_async_event_cb(
	IN				ib_async_event_rec_t* const	p_event_rec )
{
	ib_srq_handle_t			h_srq;

	CL_ASSERT( p_event_rec );
	h_srq = (ib_srq_handle_t)p_event_rec->context;

#if defined(CL_KERNEL)
	switch( p_event_rec->code )
	{
	case IB_AE_SRQ_LIMIT_REACHED:
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_SRQ, 
			("IB_AE_SRQ_LIMIT_REACHED for srq %p \n", h_srq) );
		//TODO: handle this error.
		break;
	case IB_AE_SRQ_CATAS_ERROR:
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_SRQ, 
			("IB_AE_SRQ_CATAS_ERROR for srq %p \n", h_srq) );
		//TODO: handle this error.
		break;
	default:
		break;
	}
#endif

	p_event_rec->context = (void*)h_srq->obj.context;
	p_event_rec->handle.h_srq = h_srq;

	if( h_srq->pfn_event_cb )
		h_srq->pfn_event_cb( p_event_rec );
}

ib_api_status_t
ib_post_srq_recv(
	IN		const	ib_srq_handle_t				h_srq,
	IN				ib_recv_wr_t* const			p_recv_wr,
		OUT			ib_recv_wr_t				**pp_recv_failure OPTIONAL )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SRQ );

	if( AL_OBJ_INVALID_HANDLE( h_srq, AL_OBJ_TYPE_H_SRQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SRQ_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}
	if( !p_recv_wr || ( p_recv_wr->p_next && !pp_recv_failure ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status =
		h_srq->pfn_post_srq_recv( h_srq->h_recv_srq, p_recv_wr, pp_recv_failure );

	AL_EXIT( AL_DBG_SRQ );
	return status;
}



