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

#include "al_ci_ca.h"
#include "al_verbs.h"
#include "al_cq.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_ci_ca.tmh"
#endif
#include "al_mad_pool.h"
#include "al_mgr.h"
#include "al_mr.h"
#include "al_pnp.h"
#include "al_mad_pool.h"
#include "al.h"

#include "ib_common.h"


#define	EVENT_POOL_MIN			4
#define	EVENT_POOL_MAX			0
#define	EVENT_POOL_GROW			1


void
destroying_ci_ca(
	IN				al_obj_t*					p_obj );

void
cleanup_ci_ca(
	IN				al_obj_t*					p_obj );

void
free_ci_ca(
	IN				al_obj_t*					p_obj );

void
ci_ca_comp_cb(
	IN				void						*cq_context );

void
ci_ca_async_proc_cb(
	IN				struct _cl_async_proc_item	*p_item );

void
ci_ca_async_event_cb(
	IN				ib_event_rec_t*				p_event_record );



ib_api_status_t
create_ci_ca(
	IN				al_obj_t					*p_parent_obj,
	IN		const	ci_interface_t*				p_ci,
	IN		const	PDEVICE_OBJECT				p_hca_dev,
	IN		const	PDEVICE_OBJECT				p_fdo
	)
{
	ib_api_status_t			status;
	cl_status_t				cl_status;
	al_ci_ca_t				*p_ci_ca;

	AL_ENTER( AL_DBG_CA );

	CL_ASSERT( p_ci );

	/* Allocate the CI CA. */
	p_ci_ca = (al_ci_ca_t*)cl_zalloc( sizeof( al_ci_ca_t ) );
	if( !p_ci_ca )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_zalloc failed\n") );
		return	IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the CI CA. */
	construct_al_obj( &p_ci_ca->obj, AL_OBJ_TYPE_CI_CA );
	cl_spinlock_construct( &p_ci_ca->attr_lock );
	cl_qlist_init( &p_ci_ca->ca_list );
	cl_qlist_init( &p_ci_ca->shmid_list );
	cl_qpool_construct( &p_ci_ca->event_pool );
	p_ci_ca->verbs = *p_ci;
	cl_event_construct( &p_ci_ca->event );
	cl_event_init( &p_ci_ca->event, FALSE );

	cl_status = cl_spinlock_init( &p_ci_ca->attr_lock );
	if( cl_status != CL_SUCCESS )
	{
		free_ci_ca( &p_ci_ca->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_spinlock_init failed, status = 0x%x.\n",
			ib_convert_cl_status( cl_status ) ) );
		return ib_convert_cl_status( cl_status );
	}

	/* Create a pool of items to report asynchronous events. */
	cl_status = cl_qpool_init( &p_ci_ca->event_pool, EVENT_POOL_MIN,
		EVENT_POOL_MAX, EVENT_POOL_GROW, sizeof( event_item_t ), NULL,
		NULL, p_ci_ca );
	if( cl_status != CL_SUCCESS )
	{
		free_ci_ca( &p_ci_ca->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_qpool_init failed, status = 0x%x.\n", 
			ib_convert_cl_status( cl_status ) ) );
		return ib_convert_cl_status( cl_status );
	}

	status = init_al_obj( &p_ci_ca->obj, p_ci_ca, FALSE,
		destroying_ci_ca, cleanup_ci_ca, free_ci_ca );
	if( status != IB_SUCCESS )
	{
		free_ci_ca( &p_ci_ca->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed, status = 0x%x.\n", status) );
		return status;
	}
	status = attach_al_obj( p_parent_obj, &p_ci_ca->obj );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	p_ci_ca->dereg_async_item.pfn_callback = ci_ca_async_proc_cb;

	/* Open the CI CA. */
	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("create_ci_ca: open  CA\n"));
	status = p_ci_ca->verbs.open_ca( p_ci_ca->verbs.guid,
		ci_ca_async_event_cb, p_ci_ca, &p_ci_ca->h_ci_ca );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("open_ca failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Increase the max timeout for the CI CA to handle driver unload. */
	set_al_obj_timeout( &p_ci_ca->obj, AL_MAX_TIMEOUT_MS );

	/*
	 * Register ourselves with the AL manager, so that the open call below
	 * will succeed.
	 */
	add_ci_ca( p_ci_ca );

	/* Open the AL CA. */
	status = ib_open_ca( gh_al, p_ci_ca->verbs.guid, ca_event_cb, p_ci_ca,
		&p_ci_ca->h_ca );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_open_ca failed, status = 0x%x.\n", status) );
		return status;
	}


	/* store HCA device object into CA object */
	p_ci_ca->h_ca->p_hca_dev = p_hca_dev;
	p_ci_ca->h_ca->p_fdo = p_fdo;
	
	/* Get a list of the port GUIDs on this CI CA. */
	status = get_port_info( p_ci_ca );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("get_port_guids failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Allocate a PD for use by AL itself.	*/
	status = ib_alloc_pd( p_ci_ca->h_ca, IB_PDT_SQP, p_ci_ca,
		&p_ci_ca->h_pd );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_alloc_pd failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Allocate a PD for use by AL itself.	*/
	status = ib_alloc_pd( p_ci_ca->h_ca, IB_PDT_ALIAS, p_ci_ca,
		&p_ci_ca->h_pd_alias );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_alloc_pd alias failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Register the global MAD pool on this CA. */
	status = ib_reg_mad_pool( gh_mad_pool, p_ci_ca->h_pd, &p_ci_ca->pool_key );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_reg_mad_pool failed, status = 0x%x.\n", status) );
		return status;
	}

	/*
	 * Notify the PnP manager that a CA has been added.
	 * NOTE: PnP Manager must increment the CA reference count.
	 */
	status = pnp_ca_event( p_ci_ca, IB_PNP_CA_ADD );
	if( status != IB_SUCCESS )
	{
		/* Destroy the CA */
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_pnp_add_ca failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_ci_ca->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_CA );
	return IB_SUCCESS;
}


void
destroy_outstanding_mads(	
	IN				al_obj_t*					p_obj )
{
	al_ci_ca_t			*p_ci_ca;
	cl_list_item_t		*p_ca_list_item;
	ib_al_t 			*p_al_element = NULL;

	CL_ASSERT( p_obj );
	p_ci_ca = PARENT_STRUCT( p_obj, al_ci_ca_t, obj );

	cl_spinlock_acquire(&(p_ci_ca->obj.lock));
	for( p_ca_list_item = cl_qlist_head( &p_ci_ca->ca_list );
		 p_ca_list_item != cl_qlist_end( &p_ci_ca->ca_list );
		 p_ca_list_item = cl_qlist_next( p_ca_list_item ) )
	{
		ib_ca_t *p_ca_element = PARENT_STRUCT( p_ca_list_item, ib_ca_t, list_item );
		if (p_al_element == p_ca_element->obj.h_al)
			continue;
		p_al_element = p_ca_element->obj.h_al;

		free_outstanding_mads(p_al_element);
	}
	cl_spinlock_release(&(p_ci_ca->obj.lock));
}

void
destroying_ci_ca(
	IN				al_obj_t*					p_obj )
{
	al_ci_ca_t			*p_ci_ca;

	CL_ASSERT( p_obj );
	p_ci_ca = PARENT_STRUCT( p_obj, al_ci_ca_t, obj );

	/*
	 * Notify the PnP manager that this CA is being removed.
	 * NOTE: PnP Manager must decrement the CA reference count.
	 */
	pnp_ca_event( p_ci_ca, IB_PNP_CA_REMOVE );

	/*
	 * We queue a request to the asynchronous processing manager to close
	 * the CA after the PNP remove CA event has been delivered.  This avoids
	 * the ib_close_ca() call from immediately removing resouces (PDs, QPs)
	 * that are in use by clients waiting on the remove CA event.
	 */
	if( p_ci_ca->h_ca )
		cl_async_proc_queue( gp_async_pnp_mgr, &p_ci_ca->dereg_async_item );
}



void
ci_ca_async_proc_cb(
	IN				struct _cl_async_proc_item	*p_item )
{
	al_ci_ca_t			*p_ci_ca;

	p_ci_ca = PARENT_STRUCT( p_item, al_ci_ca_t, dereg_async_item );

	/* Release all AL resources acquired by the CI CA. */
	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("ci_ca_async_proc_cb: close  CA\n"));
	ib_close_ca( p_ci_ca->h_ca, NULL );
}



void
cleanup_ci_ca(
	IN				al_obj_t*					p_obj )
{
	ib_api_status_t		status;
	al_ci_ca_t			*p_ci_ca;

	AL_ENTER( AL_DBG_CA );

	CL_ASSERT( p_obj );
	p_ci_ca = PARENT_STRUCT( p_obj, al_ci_ca_t, obj );

	CL_ASSERT( cl_is_qlist_empty( &p_ci_ca->shmid_list ) );

	if( p_ci_ca->h_ci_ca )
	{
		remove_ci_ca( p_ci_ca );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("cleanup_ci_ca: close HCA\n"));
		status = p_ci_ca->verbs.close_ca( p_ci_ca->h_ci_ca );
		CL_ASSERT( status == IB_SUCCESS );
	}

	AL_EXIT( AL_DBG_CA );
}



void
ci_ca_comp_cb(
	IN				void						*cq_context )
{
	ib_cq_handle_t			h_cq = (ib_cq_handle_t)cq_context;

	if( h_cq->h_wait_obj )
		KeSetEvent( h_cq->h_wait_obj, IO_NETWORK_INCREMENT, FALSE );
	else
		h_cq->pfn_user_comp_cb( h_cq, (void*)h_cq->obj.context );
}



/*
 * CI CA asynchronous event callback.
 */
void
ci_ca_async_event_cb(
	IN		ib_event_rec_t*		p_event_record )
{
	ib_async_event_rec_t	event_rec;

	AL_ENTER( AL_DBG_CA );

	CL_ASSERT( p_event_record );

	event_rec.code = p_event_record->type;
	event_rec.context = p_event_record->context;
	event_rec.vendor_specific = p_event_record->vendor_specific;
	event_rec.port_number = p_event_record->port_number;

	ci_ca_async_event( &event_rec );

	AL_EXIT( AL_DBG_CA );
}



/*
 * Insert a new shmid tracking structure into the CI CA's list.
 */
void
add_shmid(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN				struct _al_shmid			*p_shmid )
{
	CL_ASSERT( p_ci_ca && p_shmid );

	p_shmid->obj.p_ci_ca = p_ci_ca;

	/* Insert the shmid structure into the shmid list. */
	cl_spinlock_acquire( &p_ci_ca->obj.lock );
	cl_qlist_insert_head( &p_ci_ca->shmid_list, &p_shmid->list_item );
	cl_spinlock_release( &p_ci_ca->obj.lock );
}



ib_api_status_t
acquire_shmid(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN				int							shmid,
		OUT			struct _al_shmid			**pp_shmid )
{
	al_shmid_t			*p_shmid;
	cl_list_item_t		*p_list_item;

	/* Try to find the shmid. */
	cl_spinlock_acquire( &p_ci_ca->obj.lock );
	for( p_list_item = cl_qlist_head( &p_ci_ca->shmid_list );
		 p_list_item != cl_qlist_end( &p_ci_ca->shmid_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_shmid = PARENT_STRUCT( p_list_item, al_shmid_t, list_item );
		if( p_shmid->id == shmid )
		{
			ref_al_obj( &p_shmid->obj );
			*pp_shmid = p_shmid;
			break;
		}
	}
	cl_spinlock_release( &p_ci_ca->obj.lock );

	if( p_list_item == cl_qlist_end( &p_ci_ca->shmid_list ) )
		return IB_NOT_FOUND;
	else
		return IB_SUCCESS;
}



void
release_shmid(
	IN				struct _al_shmid			*p_shmid )
{
	al_ci_ca_t			*p_ci_ca;
	int32_t				ref_cnt;

	CL_ASSERT( p_shmid );

	p_ci_ca = p_shmid->obj.p_ci_ca;

	cl_spinlock_acquire( &p_ci_ca->obj.lock );

	/* Dereference the shmid. */
	ref_cnt = deref_al_obj( &p_shmid->obj );

	/* If the shmid is no longer in active use, remove it. */
	if( ref_cnt == 1 )
		cl_qlist_remove_item( &p_ci_ca->shmid_list, &p_shmid->list_item );

	cl_spinlock_release( &p_ci_ca->obj.lock );

	/* Destroy the shmid if it is not needed. */
	if( ref_cnt == 1 )
	{
		ref_al_obj( &p_shmid->obj );
		p_shmid->obj.pfn_destroy( &p_shmid->obj, NULL );
	}
}



ib_api_status_t
ib_ci_call(
	IN				ib_ca_handle_t				h_ca,
	IN		const	void**				const	handle_array	OPTIONAL,
	IN				uint32_t					num_handles,
	IN				ib_ci_op_t*			const	p_ci_op )
{
	return ci_call( h_ca, handle_array, num_handles, p_ci_op, NULL );
}



ib_api_status_t
ci_call(
	IN				ib_ca_handle_t				h_ca,
	IN		const	void**				const	handle_array	OPTIONAL,
	IN				uint32_t					num_handles,
	IN				ib_ci_op_t*			const	p_ci_op,
	IN				ci_umv_buf_t*		const	p_umv_buf OPTIONAL )
{
	void**			p_handle_array;
	ib_api_status_t	status;

	AL_ENTER( AL_DBG_CA );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}
	if( !p_ci_op )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}
	p_handle_array = NULL;
	if ( num_handles )
	{
		p_handle_array = cl_zalloc( sizeof(void*) * num_handles );
		if( !p_handle_array )
			return IB_INSUFFICIENT_MEMORY;

		status = al_convert_to_ci_handles( p_handle_array, handle_array,
			num_handles );

		if( status != IB_SUCCESS )
		{
			cl_free( p_handle_array );
			return status;
		}
	}

	if( h_ca->obj.p_ci_ca->verbs.vendor_call )
	{
		status = verbs_ci_call(
			h_ca, p_handle_array, num_handles, p_ci_op, p_umv_buf );
	}
	else
	{
		status = IB_UNSUPPORTED;
	}

	if ( num_handles )
		cl_free( p_handle_array );

	AL_EXIT( AL_DBG_QUERY );
	return status;
}


DEVICE_OBJECT*
get_ca_dev(
	IN		const	ib_ca_handle_t				h_ca )
{
	ASSERT( h_ca );

	ObReferenceObject( h_ca->p_hca_dev );
	return h_ca->p_hca_dev;
}
