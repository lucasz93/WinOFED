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
#include "al_ca.h"
#include "al_cm_cep.h"
#include "al_common.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al.tmh"
#endif

#include "al_mad_pool.h"
#include "al_mgr.h"
#include "al_verbs.h"
#include "ib_common.h"


void
destroying_al(
	IN				al_obj_t					*p_obj );


void
free_al(
	IN				al_obj_t					*p_obj );



/*
 * Destroy an instance of the access layer.
 */
#ifdef CL_KERNEL
ib_api_status_t
ib_close_al(
	IN		const	ib_al_handle_t				h_al )
#else
ib_api_status_t
do_close_al(
	IN		const	ib_al_handle_t				h_al )
#endif
{
	AL_ENTER( AL_DBG_MGR );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}

	ref_al_obj( &h_al->obj );
	h_al->obj.pfn_destroy( &h_al->obj, NULL );

	AL_EXIT( AL_DBG_MGR );
	return IB_SUCCESS;
}



void
destroying_al(
	IN				al_obj_t					*p_obj )
{
	ib_al_handle_t			h_al;
	cl_list_item_t			*p_list_item;
	al_sa_req_t				*p_sa_req;

	CL_ASSERT( p_obj );
	h_al = PARENT_STRUCT( p_obj, ib_al_t, obj );

	cl_spinlock_acquire( &p_obj->lock );

	/* Cancel all outstanding queries. */
	for( p_list_item = cl_qlist_head( &h_al->query_list );
		 p_list_item != cl_qlist_end( &h_al->query_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_sa_req = PARENT_STRUCT( p_list_item, al_sa_req_t, list_item );
		al_cancel_sa_req( p_sa_req );
	}

	cl_spinlock_release( &p_obj->lock );

	/* Cleanup any left-over connections. */
	al_cep_cleanup_al( h_al );
}



static void
__free_mads(
	IN				const	ib_al_handle_t		h_al )
{
	cl_list_item_t			*p_list_item;
	al_mad_element_t		*p_mad_element;
	ib_api_status_t			status;

	/* Return all outstanding MADs to their MAD pools. */
	for( p_list_item = cl_qlist_head( &h_al->mad_list );
		 p_list_item != cl_qlist_end( &h_al->mad_list );
		 p_list_item = cl_qlist_head( &h_al->mad_list ) )
	{
		p_mad_element = PARENT_STRUCT( p_list_item, al_mad_element_t, al_item );
		p_mad_element->element.p_next = NULL;

		status = ib_put_mad( &p_mad_element->element );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_put_mad failed with status %s, continuing.\n",
				ib_get_err_str(status)) );
		}
	}
}

void
free_outstanding_mads(
	IN				const	ib_al_handle_t		h_al )
{
	cl_list_item_t			*p_list_item;
	al_mad_element_t		*p_mad_element;
	ib_api_status_t			status;
	int 					mad_count = 0;

	/* Return all outstanding MADs to the MAD pools. */
	cl_spinlock_acquire( &h_al->mad_lock);
	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
				("We have %lu outstanding MADs left. First ones are:\n", (unsigned long) h_al->mad_list.count));
	
	for( p_list_item = cl_qlist_head( &h_al->mad_list );
		 p_list_item != cl_qlist_end( &h_al->mad_list );
		 p_list_item = cl_qlist_head( &h_al->mad_list ) )
	{
		ib_mad_t* mad_buf;
		
		p_mad_element = PARENT_STRUCT( p_list_item, al_mad_element_t, al_item );
		p_mad_element->element.p_next = NULL;

		mad_buf = p_mad_element->element.p_mad_buf;

		if (mad_count < 10)
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("LEFT MAD: mgmt_class = %#x attr_id = %#x method = %#x \n",
				mad_buf->mgmt_class, mad_buf->attr_id, mad_buf->method) );
			mad_count++;
		}

		cl_spinlock_release( &h_al->mad_lock); // the lock is taken in ib_put_mad
		status = ib_put_mad( &p_mad_element->element );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_put_mad failed with status %s, continuing.\n",
				ib_get_err_str(status)) );
		}
		cl_spinlock_acquire( &h_al->mad_lock);
	}
	cl_spinlock_release( &h_al->mad_lock);
}



void
free_al(
	IN				al_obj_t					*p_obj )
{
	ib_al_handle_t			h_al;

	CL_ASSERT( p_obj );
	h_al = PARENT_STRUCT( p_obj, ib_al_t, obj );

	/* Free any MADs not returned by the user. */
	__free_mads( h_al );

#ifdef CL_KERNEL
	cl_vector_destroy( &h_al->hdl_vector );
#endif

	cl_spinlock_destroy( &h_al->mad_lock );
	destroy_al_obj( &h_al->obj );
	cl_free( h_al );
}


ib_api_status_t
ib_query_ca_by_guid(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_net64_t					ca_guid,
		OUT			ib_ca_attr_t* const			p_ca_attr OPTIONAL,
	IN	OUT			uint32_t* const				p_size )
{
	ib_ca_handle_t		h_ca;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CA );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_size )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	h_ca = acquire_ca( ca_guid );
	if( !h_ca )
	{
		return IB_INVALID_GUID;
	}
	status = ib_query_ca( h_ca, p_ca_attr, p_size );
	deref_al_obj( &h_ca->obj );

	AL_EXIT( AL_DBG_CA );
	return status;
}



void
al_insert_mad(
	IN		const	ib_al_handle_t				h_al,
	IN				al_mad_element_t*	const	p_mad )
{
	/* Assert that the MAD does not already have an owner. */
	CL_ASSERT( !p_mad->h_al );

	ref_ctx_al_obj( &h_al->obj , E_REF_MAD_ADD_REMOVE );
	cl_spinlock_acquire( &h_al->mad_lock );

	/*
	 * Initialize the h_al field.  This field is used to locate the AL
	 * instance that owns a given MAD.
	 */
	p_mad->h_al = h_al;
	cl_qlist_insert_tail( &h_al->mad_list, &p_mad->al_item );

	cl_spinlock_release( &h_al->mad_lock );
}



void
al_remove_mad(
	IN				al_mad_element_t*	const	p_mad )
{
	/* Return if the MAD is not in the AL instance MAD list. */
	if( !p_mad->h_al ) return;

	cl_spinlock_acquire( &p_mad->h_al->mad_lock );
	cl_qlist_remove_item( &p_mad->h_al->mad_list, &p_mad->al_item );
	cl_spinlock_release( &p_mad->h_al->mad_lock );

	deref_ctx_al_obj( &p_mad->h_al->obj , E_REF_MAD_ADD_REMOVE );
	p_mad->h_al = NULL;
}



void
al_handoff_mad(
	IN		const	ib_al_handle_t				h_al,
	IN				ib_mad_element_t*	const	p_mad_element )
{
	al_mad_element_t		*p_mad;

	p_mad = PARENT_STRUCT( p_mad_element, al_mad_element_t, element );

	/*
	 * See if we're handing off to the same AL instance.  This can happen if
	 * we hand off to an internal service that uses the global AL instance.
	 */
	if( p_mad->h_al == h_al )
		return;

	al_remove_mad( p_mad );
	al_insert_mad( h_al, p_mad );
}



void
al_insert_key(
	IN		const	ib_al_handle_t				h_al,
	IN				al_pool_key_t* const		p_pool_key )
{
	ref_ctx_al_obj( &h_al->obj , E_REF_KEY_ADD_REMOVE );
	p_pool_key->h_al = h_al;

	cl_spinlock_acquire( &h_al->obj.lock );
	p_pool_key->in_al_list = TRUE;
	cl_qlist_insert_tail( &h_al->key_list, &p_pool_key->al_item );
	cl_spinlock_release( &h_al->obj.lock );
}



/*
 * Remove the pool_key from AL's list.  This is called from the pool_key's
 * cleanup routine.
 */
void
al_remove_key(
	IN				al_pool_key_t* const		p_pool_key )
{
	/* Return if the pool key is not in the AL instance key list. */
	if( !p_pool_key->h_al ) return;

	cl_spinlock_acquire( &p_pool_key->h_al->obj.lock );
	if( p_pool_key->in_al_list )
	{
		cl_qlist_remove_item( &p_pool_key->h_al->key_list,
			&p_pool_key->al_item );
	}
	cl_spinlock_release( &p_pool_key->h_al->obj.lock );

	deref_ctx_al_obj( &p_pool_key->h_al->obj , E_REF_KEY_ADD_REMOVE );
	p_pool_key->h_al = NULL;
}



void
al_dereg_pool(
	IN		const	ib_al_handle_t				h_al,
	IN				ib_pool_handle_t const		h_pool )
{
	cl_qlist_t				destroy_list;
	cl_list_item_t			*p_list_item, *p_next_item;
	al_pool_key_t			*p_pool_key;

	/*
	 * Deregister matching pool keys.  This may deregister memory, so we
	 * cannot do this while holding a lock.  So we need to move the pool
	 * keys to a destroy_list.
	 */
	cl_qlist_init( &destroy_list );

	/* Search for keys associated with the given PD or MAD pool. */
	cl_spinlock_acquire( &h_al->obj.lock );
	for( p_list_item = cl_qlist_head( &h_al->key_list );
		 p_list_item != cl_qlist_end( &h_al->key_list );
		 p_list_item = p_next_item )
	{
		/* Cache the next item in case we remove this one. */
		p_next_item = cl_qlist_next( p_list_item );
		p_pool_key = PARENT_STRUCT( p_list_item, al_pool_key_t, al_item );

		if( p_pool_key->h_pool == h_pool )
		{
			/*
			 * Destroy this pool key.  This only deregisters memory in
			 * user-mode since we use phys reg in kernel mode, so we
			 * can do this while holding a lock.
			 */
			ref_al_obj( &p_pool_key->obj );
			p_pool_key->in_al_list = FALSE;
			cl_qlist_remove_item( &h_al->key_list, &p_pool_key->al_item );
			cl_qlist_insert_tail( &destroy_list, &p_pool_key->al_item );
		}
	}
	cl_spinlock_release( &h_al->obj.lock );

	/* Destroy all pool_keys associated with the MAD pool. */
	for( p_list_item = cl_qlist_remove_head( &destroy_list );
		 p_list_item != cl_qlist_end( &destroy_list );
		 p_list_item = cl_qlist_remove_head( &destroy_list ) )
	{
		/* Mark that we've remove the item from the list. */
		p_pool_key = PARENT_STRUCT( p_list_item, al_pool_key_t, al_item );
		p_pool_key->obj.pfn_destroy( &p_pool_key->obj, NULL );
	}
}


void
al_insert_query(
	IN		const	ib_al_handle_t				h_al,
	IN				al_query_t* const			p_query )
{
	p_query->h_al = h_al;
	ref_al_obj( &h_al->obj );
	cl_spinlock_acquire( &h_al->obj.lock );
	cl_qlist_insert_tail( &h_al->query_list, &p_query->sa_req.list_item );
	cl_spinlock_release( &h_al->obj.lock );
}


void
al_remove_query(
	IN				al_query_t* const			p_query )
{
	cl_spinlock_acquire( &p_query->h_al->obj.lock );
	cl_qlist_remove_item( &p_query->h_al->query_list,
		&p_query->sa_req.list_item );
	cl_spinlock_release( &p_query->h_al->obj.lock );
	deref_al_obj( &p_query->h_al->obj );
}



static cl_status_t
__match_query(
	IN		const	cl_list_item_t* const		p_item,
	IN				void*						context )
{
	al_sa_req_t		*p_sa_req;

	p_sa_req = PARENT_STRUCT( p_item, al_sa_req_t, list_item );
	if( context == PARENT_STRUCT( p_item, al_query_t, sa_req ) )
		return IB_SUCCESS;

	return IB_NOT_FOUND;
}


void
ib_cancel_query(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_query_handle_t			h_query )
{
	cl_list_item_t	*p_item;

	AL_ENTER( AL_DBG_QUERY );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return;
	}

	cl_spinlock_acquire( &h_al->obj.lock );
	p_item =
		cl_qlist_find_from_head( &h_al->query_list, __match_query, h_query );
	if( p_item != cl_qlist_end( &h_al->query_list ) )
		al_cancel_sa_req( &h_query->sa_req );

	cl_spinlock_release( &h_al->obj.lock );

	AL_EXIT( AL_DBG_QUERY );
}
