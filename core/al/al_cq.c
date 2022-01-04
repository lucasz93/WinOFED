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

#include "al_cq.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_cq.tmh"
#endif

#include "al_ca.h"
#include "al_pd.h"
#include "al_qp.h"
#include "al_verbs.h"
#ifdef CL_KERNEL
#include "al_proxy_ndi.h"
#endif

/*
 * Function prototypes.
 */
void
destroying_cq(
	IN				struct _al_obj				*p_obj );

void
cleanup_cq(
	IN				al_obj_t					*p_obj );

void
free_cq(
	IN				al_obj_t					*p_obj );




/*
 * Initializes the CQ information structure.
 */
ib_api_status_t
create_cq(
	IN		const	ib_ca_handle_t				h_ca,
	IN	OUT			ib_cq_create_t* const		p_cq_create,
	IN		const	void* const					cq_context,
	IN		const	ib_pfn_event_cb_t			pfn_cq_event_cb,
		OUT			ib_cq_handle_t* const		ph_cq,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_cq_handle_t			h_cq;
	ib_api_status_t			status;
	al_obj_type_t			obj_type = AL_OBJ_TYPE_H_CQ;

	if( !p_cq_create || !ph_cq )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}
	if( (p_cq_create->pfn_comp_cb && p_cq_create->h_wait_obj) ||
		(!p_cq_create->pfn_comp_cb && !p_cq_create->h_wait_obj) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SETTING\n") );
		return IB_INVALID_SETTING;
	}

	h_cq = cl_zalloc( sizeof( ib_cq_t ) );
	if( !h_cq )
	{
		return IB_INSUFFICIENT_MEMORY;
	}

#ifdef CL_KERNEL
	if( !NT_SUCCESS( ndi_cq_init( h_cq ) ) )
	{
		free_cq( &h_cq->obj );
		return IB_ERROR;
	}
#endif
	
	if( p_umv_buf )
		obj_type |= AL_OBJ_SUBTYPE_UM_EXPORT;

	/* Construct the CQ. */
	construct_al_obj( &h_cq->obj, obj_type );

	cl_qlist_init( &h_cq->qp_list );

	/* Initialize the CQ. */
	status = init_al_obj( &h_cq->obj, cq_context, TRUE,
		destroying_cq, cleanup_cq, free_cq );
	if( status != IB_SUCCESS )
	{
		free_cq( &h_cq->obj );
		return status;
	}
	status = attach_al_obj( &h_ca->obj, &h_cq->obj );
	if( status != IB_SUCCESS )
	{
		h_cq->obj.pfn_destroy( &h_cq->obj, NULL );
		return status;
	}

	/*
	 * Record which completion routine will be used to notify the CQ of
	 * a completion.
	 */
	h_cq->pfn_event_cb = pfn_cq_event_cb;
	if( p_cq_create->pfn_comp_cb )
	{
		CL_ASSERT( !p_cq_create->h_wait_obj );
		h_cq->pfn_user_comp_cb = p_cq_create->pfn_comp_cb;
	}
	else
	{
		CL_ASSERT( p_cq_create->h_wait_obj );
		h_cq->h_wait_obj = p_cq_create->h_wait_obj;
	}

	/*
	 * Note:
	 * Because an extra reference is not held on the object during creation,
	 * the h_cq handle may be destroryed by the client's asynchronous event
	 * callback routine before call to verbs returns.
	 */
	status = verbs_create_cq( h_ca, p_cq_create, h_cq, p_umv_buf );
	if( status != IB_SUCCESS )
	{
		h_cq->obj.pfn_destroy( &h_cq->obj, NULL );
		return status;
	}

	*ph_cq = h_cq;

	return IB_SUCCESS;
}



ib_api_status_t
ib_destroy_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_CQ );

	if( AL_OBJ_INVALID_HANDLE( h_cq, AL_OBJ_TYPE_H_CQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
		return IB_INVALID_CQ_HANDLE;
	}

	ref_al_obj( &h_cq->obj );
	h_cq->obj.pfn_destroy( &h_cq->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_CQ );
	return IB_SUCCESS;
}



void
destroying_cq(
	IN				struct _al_obj				*p_obj )
{
	ib_cq_handle_t		h_cq;
	cl_list_item_t		*p_item;
	cl_obj_rel_t		*p_rel;
	ib_qp_handle_t		h_qp;

	CL_ASSERT( p_obj );
	h_cq = PARENT_STRUCT( p_obj, ib_cq_t, obj );

	/* Initiate destruction of all bound QPs. */
	cl_spinlock_acquire( &h_cq->obj.lock );
	for( p_item = cl_qlist_remove_tail( &h_cq->qp_list );
		p_item != cl_qlist_end( &h_cq->qp_list );
		p_item = cl_qlist_remove_tail( &h_cq->qp_list ) )
	{
		p_rel = PARENT_STRUCT( p_item, cl_obj_rel_t, pool_item.list_item );
		p_rel->p_parent_obj = NULL;
		h_qp = (ib_qp_handle_t)p_rel->p_child_obj;
		if( h_qp )
		{
			/* Take a reference to prevent the QP from being destroyed. */
			ref_al_obj( &h_qp->obj );
			cl_spinlock_release( &h_cq->obj.lock );
			h_qp->obj.pfn_destroy( &h_qp->obj, NULL );
			cl_spinlock_acquire( &h_cq->obj.lock );
		}
	}

	cl_spinlock_release( &h_cq->obj.lock );

#ifdef CL_KERNEL
	/* cancel pending IRPS for NDI type CQ */
	ndi_cq_flush_ques( h_cq );
#endif

}


void
cleanup_cq(
	IN				struct _al_obj				*p_obj )
{
	ib_cq_handle_t			h_cq;
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	h_cq = PARENT_STRUCT( p_obj, ib_cq_t, obj );

	/* Deallocate the CI cq. */
	if( verbs_check_cq( h_cq ) )
	{
		status = verbs_destroy_cq( h_cq );
		CL_ASSERT( status == IB_SUCCESS );
	}
}



/*
 * Release all resources associated with the completion queue.
 */
void
free_cq(
	IN				al_obj_t					*p_obj )
{
	ib_cq_handle_t			h_cq;

	CL_ASSERT( p_obj );
	h_cq = PARENT_STRUCT( p_obj, ib_cq_t, obj );

	destroy_al_obj( &h_cq->obj );
	cl_free( h_cq );
}


void
cq_attach_qp(
	IN		const	ib_cq_handle_t				h_cq,
	IN				cl_obj_rel_t* const			p_qp_rel )
{
	p_qp_rel->p_parent_obj = (cl_obj_t*)h_cq;
	ref_al_obj( &h_cq->obj );
	cl_spinlock_acquire( &h_cq->obj.lock );
	cl_qlist_insert_tail( &h_cq->qp_list, &p_qp_rel->pool_item.list_item );
	cl_spinlock_release( &h_cq->obj.lock );
}


void
cq_detach_qp(
	IN		const	ib_cq_handle_t				h_cq,
	IN				cl_obj_rel_t* const			p_qp_rel )
{
	if( p_qp_rel->p_parent_obj )
	{
		CL_ASSERT( p_qp_rel->p_parent_obj == (cl_obj_t*)h_cq );
		p_qp_rel->p_parent_obj = NULL;
		cl_spinlock_acquire( &h_cq->obj.lock );
		cl_qlist_remove_item( &h_cq->qp_list, &p_qp_rel->pool_item.list_item );
		cl_spinlock_release( &h_cq->obj.lock );
	}
}


ib_api_status_t
ib_modify_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			uint32_t* const				p_size )
{
	return modify_cq( h_cq, p_size, NULL );
}


ib_api_status_t
modify_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			uint32_t* const				p_size,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	if( AL_OBJ_INVALID_HANDLE( h_cq, AL_OBJ_TYPE_H_CQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
		return IB_INVALID_CQ_HANDLE;
	}
	if( !p_size )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_modify_cq( h_cq, p_size );

	AL_EXIT( AL_DBG_CQ );
	return status;
}



ib_api_status_t
ib_query_cq(
	IN		const	ib_cq_handle_t				h_cq,
		OUT			uint32_t* const				p_size )
{
	return query_cq( h_cq, p_size, NULL );
}



ib_api_status_t
query_cq(
	IN		const	ib_cq_handle_t				h_cq,
		OUT			uint32_t* const				p_size,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	if( AL_OBJ_INVALID_HANDLE( h_cq, AL_OBJ_TYPE_H_CQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
		return IB_INVALID_CQ_HANDLE;
	}
	if( !p_size )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_query_cq( h_cq, p_size );

	AL_EXIT( AL_DBG_CQ );
	return status;
}



ib_api_status_t
ib_peek_cq(
	IN		const	ib_cq_handle_t				h_cq,
	OUT				uint32_t* const				p_n_cqes )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	if( AL_OBJ_INVALID_HANDLE( h_cq, AL_OBJ_TYPE_H_CQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
		return IB_INVALID_CQ_HANDLE;
	}
	if( !p_n_cqes )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_peek_cq( h_cq, p_n_cqes );

	AL_EXIT( AL_DBG_CQ );
	return status;
}



ib_api_status_t
ib_poll_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			ib_wc_t** const				pp_free_wclist,
		OUT			ib_wc_t** const				pp_done_wclist )
{
	ib_api_status_t			status;
	PERF_DECLARE( IbPollCq );
	PERF_DECLARE( VerbsPollCq );

	cl_perf_start( IbPollCq );
	AL_ENTER( AL_DBG_CQ );

	if( AL_OBJ_INVALID_HANDLE( h_cq, AL_OBJ_TYPE_H_CQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
		return IB_INVALID_CQ_HANDLE;
	}
	if( !pp_free_wclist || !pp_done_wclist )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	cl_perf_start( VerbsPollCq );
	status = verbs_poll_cq( h_cq, pp_free_wclist, pp_done_wclist );
	cl_perf_stop( &g_perf, VerbsPollCq );

	AL_EXIT( AL_DBG_CQ );
	cl_perf_stop( &g_perf, IbPollCq );
	return status;
}



ib_api_status_t
ib_rearm_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	boolean_t					solicited )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	if( AL_OBJ_INVALID_HANDLE( h_cq, AL_OBJ_TYPE_H_CQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
		return IB_INVALID_CQ_HANDLE;
	}

	status = verbs_rearm_cq( h_cq, solicited );

	AL_EXIT( AL_DBG_CQ );
	return status;
}



ib_api_status_t
ib_rearm_n_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	uint32_t					n_cqes )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	if( AL_OBJ_INVALID_HANDLE( h_cq, AL_OBJ_TYPE_H_CQ ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CQ_HANDLE\n") );
		return IB_INVALID_CQ_HANDLE;
	}
	if( !n_cqes )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_rearm_n_cq( h_cq, n_cqes );

	AL_EXIT( AL_DBG_CQ );
	return status;
}



/*
 * Process an asynchronous event on the CQ.  Notify the user of the event.
 */
void
cq_async_event_cb(
	IN				ib_async_event_rec_t* const	p_event_rec )
{
	ib_cq_handle_t			h_cq;

	CL_ASSERT( p_event_rec );
	h_cq = (ib_cq_handle_t)p_event_rec->context;

	p_event_rec->context = (void*)h_cq->obj.context;
	p_event_rec->handle.h_cq = h_cq;

	if( h_cq->pfn_event_cb )
		h_cq->pfn_event_cb( p_event_rec );
}
