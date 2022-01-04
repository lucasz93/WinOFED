/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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

#include "al.h"
#include "al_ci_ca.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_mad_pool.tmh"
#endif

#include "al_mad_pool.h"
#include "al_pd.h"
#include "al_verbs.h"
#include "ib_common.h"


typedef struct _mad_send
{
	al_mad_send_t			mad_send;
	ib_pool_handle_t		h_pool;

}	mad_send_t;




typedef struct _mad_rmpp
{
	al_mad_rmpp_t			mad_rmpp;
	ib_pool_handle_t		h_pool;

}	mad_rmpp_t;


#if DBG

typedef struct _mad_elem_change
{
	const al_mad_element_t*		p_mad_elem;
	change_type_t				change;
	char*						file_name;
	uint32_t					line_num;
}	mad_elem_change_t;


typedef struct _mad_trace_db
{
	mad_elem_change_t 		trace[POOL_TRACE_ENTRIES];
	LONG 					trace_index;
} mad_trace_db_t;


mad_trace_db_t 		g_mad_elem_trace;
#endif


/*
 * Function prototypes.
 */
static void
__destroying_pool(
	IN				al_obj_t*					p_obj );

static void
__free_pool(
	IN				al_obj_t*					p_obj );

static void
__destroying_pool_key(
	IN				al_obj_t*					p_obj );

static void
__cleanup_pool_key(
	IN				al_obj_t*					p_obj );

static void
__free_pool_key(
	IN				al_obj_t*					p_obj );

static cl_status_t
__mad_send_init(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item );

static cl_status_t
__mad_rmpp_init(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item );



/*
 * Create a MAD pool.
 */
ib_api_status_t
ib_create_mad_pool(
	IN		const	ib_al_handle_t				h_al,
	IN		const	size_t						min,
	IN		const	size_t						max,
	IN		const	size_t						grow_size,
		OUT			ib_pool_handle_t* const		ph_pool )
{
	ib_pool_handle_t		h_pool;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MAD_POOL );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !ph_pool )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Validate the min and max parameters. */
	if( (min > 0) && (max > 0) && (min > max) )
		return IB_INVALID_SETTING;

	h_pool = cl_zalloc( sizeof( al_pool_t ) );
	if( !h_pool )
		return IB_INSUFFICIENT_MEMORY;

	/* Initialize the pool lists. */
	cl_qlist_init( &h_pool->key_list );
	ExInitializeNPagedLookasideList( &h_pool->mad_stack, NULL, NULL,
		0, sizeof(mad_item_t), 'dmla', 0 );
	ExInitializeNPagedLookasideList( &h_pool->mad_send_pool, NULL, NULL,
		0, sizeof(mad_send_t), 'dmla', 0 );
	ExInitializeNPagedLookasideList( &h_pool->mad_rmpp_pool, NULL, NULL,
		0, sizeof(mad_rmpp_t), 'dmla', 0 );

	/* Initialize the pool object. */
	construct_al_obj( &h_pool->obj, AL_OBJ_TYPE_H_MAD_POOL );
	status = init_al_obj( &h_pool->obj, h_pool, TRUE,
		__destroying_pool, NULL, __free_pool );
	if( status != IB_SUCCESS )
	{
		__free_pool( &h_pool->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Attach the pool to the AL object. */
	status = attach_al_obj( &h_al->obj, &h_pool->obj );
	if( status != IB_SUCCESS )
	{
		h_pool->obj.pfn_destroy( &h_pool->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Save the pool parameters.  Set grow_size to min for initialization. */
	h_pool->max = max;
	h_pool->grow_size = min;

	/* Save the grow_size for subsequent allocations. */
	h_pool->grow_size = grow_size;

	/* Return the pool handle. */
	*ph_pool = h_pool;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_pool->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_MAD_POOL );
	return IB_SUCCESS;
}



/*
 * Pre-destory the pool.
 */
static void
__destroying_pool(
	IN				al_obj_t*					p_obj )
{
	ib_pool_handle_t		h_pool;
	ib_al_handle_t			h_al;

	AL_ENTER( AL_DBG_MAD_POOL );

	CL_ASSERT( p_obj );
	h_pool = PARENT_STRUCT( p_obj, al_pool_t, obj );

	/* Get the AL instance of this MAD pool. */
	p_obj = h_pool->obj.p_parent_obj;
	h_al = PARENT_STRUCT( p_obj, ib_al_t, obj );

	/* Deregister this MAD pool from all protection domains. */
	al_dereg_pool( h_al, h_pool );

	AL_EXIT( AL_DBG_MAD_POOL );
}



/*
 * Free the pool.
 */
static void
__free_pool(
	IN				al_obj_t*					p_obj )
{
	ib_pool_handle_t		h_pool;

	CL_ASSERT( p_obj );
	h_pool = PARENT_STRUCT( p_obj, al_pool_t, obj );

	ExDeleteNPagedLookasideList( &h_pool->mad_send_pool );
	ExDeleteNPagedLookasideList( &h_pool->mad_rmpp_pool );
	ExDeleteNPagedLookasideList( &h_pool->mad_stack );
	destroy_al_obj( &h_pool->obj );
	cl_free( h_pool );
}



/*
 * Destory a MAD pool.
 */
ib_api_status_t
ib_destroy_mad_pool(
	IN		const	ib_pool_handle_t			h_pool )
{
	cl_list_item_t*			p_array_item;
	al_obj_t*				p_obj;
	boolean_t				busy;

	AL_ENTER( AL_DBG_MAD_POOL );

	if( AL_OBJ_INVALID_HANDLE( h_pool, AL_OBJ_TYPE_H_MAD_POOL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	/* Verify that all send handles and MAD elements are in pool. */
	cl_spinlock_acquire( &h_pool->obj.lock );
	busy = ( h_pool->obj.ref_cnt > 1 );
	for( p_array_item = cl_qlist_head( &h_pool->obj.obj_list );
		 p_array_item != cl_qlist_end( &h_pool->obj.obj_list ) && !busy;
		 p_array_item = cl_qlist_next( p_array_item ) )
	{
		p_obj = PARENT_STRUCT( p_array_item, al_obj_t, pool_item );
		busy = ( p_obj->ref_cnt > 1 );
	}
	cl_spinlock_release( &h_pool->obj.lock );

	/* Return an error if the pool is busy. */
	if( busy )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("h_pool (0x%p) is busy!.\n", h_pool) );
		return IB_RESOURCE_BUSY;
	}

	ref_al_obj( &h_pool->obj );
	h_pool->obj.pfn_destroy( &h_pool->obj, NULL );

	AL_EXIT( AL_DBG_MAD_POOL );
	return IB_SUCCESS;
}



/*
 * Register a MAD pool with a protection domain.
 */
ib_api_status_t
ib_reg_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
	IN		const	ib_pd_handle_t				h_pd,
		OUT			ib_pool_key_t* const		pp_pool_key )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MAD_POOL );

	if( AL_OBJ_INVALID_HANDLE( h_pool, AL_OBJ_TYPE_H_MAD_POOL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}
	/* Alias keys require an alias PD. */
	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	status = reg_mad_pool( h_pool, h_pd, pp_pool_key );
	/* Release the reference taken in init_al_obj. */
	if( status == IB_SUCCESS )
		deref_ctx_al_obj( &(*pp_pool_key)->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_MAD_POOL );
	return status;
}


ib_api_status_t
reg_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
	IN		const	ib_pd_handle_t				h_pd,
		OUT			ib_pool_key_t* const		pp_pool_key )
{
	al_pool_key_t*			p_pool_key;
	ib_al_handle_t			h_al;
	ib_api_status_t			status;
	al_key_type_t			key_type;

	AL_ENTER( AL_DBG_MAD_POOL );

	if( !pp_pool_key )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Set the type of key to create. */
	if( h_pd->type != IB_PDT_ALIAS )
		key_type = AL_KEY_NORMAL;
	else
		key_type = AL_KEY_ALIAS;

	/* Allocate a pool key structure. */
	p_pool_key = cl_zalloc( sizeof( al_pool_key_t ) );
	if( !p_pool_key )
		return IB_INSUFFICIENT_MEMORY;

	/* Initialize the pool key. */
	construct_al_obj( &p_pool_key->obj, AL_OBJ_TYPE_H_POOL_KEY );
	p_pool_key->type = key_type;
	p_pool_key->h_pool = h_pool;

	/* Initialize the pool key object. */
	status = init_al_obj( &p_pool_key->obj, p_pool_key, TRUE,
		__destroying_pool_key, __cleanup_pool_key, __free_pool_key );
	if( status != IB_SUCCESS )
	{
		__free_pool_key( &p_pool_key->obj );

		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	//add ref to h_pool which is dereferenced in __cleanup_pool_key 
	//in both success and error flow
	ref_al_obj( &h_pool->obj );

	/* Register the pool on the protection domain. */
	if( key_type == AL_KEY_NORMAL )
	{
		ib_phys_create_t		phys_create;
		ib_phys_range_t			phys_range;
		uint64_t				vaddr;
		net32_t					rkey;

		/* Register all of physical memory. */
		phys_create.length = 0xFFFFFFFFFFFFFFFF;
		phys_create.num_ranges = 1;
		phys_create.range_array = &phys_range;
		phys_create.buf_offset = 0;
		phys_create.hca_page_size = PAGE_SIZE;
		phys_create.access_ctrl = IB_AC_LOCAL_WRITE;
		phys_range.base_addr = 0;
		phys_range.size = 0xFFFFFFFFFFFFFFFF;
		vaddr = 0;
		status = ib_reg_phys( h_pd, &phys_create, &vaddr,
			&p_pool_key->lkey, &rkey, &p_pool_key->h_mr );
		if( status != IB_SUCCESS )
		{
			p_pool_key->obj.pfn_destroy( &p_pool_key->obj, NULL );
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_reg_phys returned %s\n", ib_get_err_str( status )) );
			return status;
		}

		/* Chain the pool key onto the pool. */
		cl_spinlock_acquire( &h_pool->obj.lock );
		cl_qlist_insert_tail( &h_pool->key_list, &p_pool_key->pool_item );
		cl_spinlock_release( &h_pool->obj.lock );
	}

	/*
	 * Attach to the pool after we register the memory so that PD destruction
	 * will cleanup the pool key before its memory region.
	 */
	status = attach_al_obj( &h_pd->obj, &p_pool_key->obj );
	if( status != IB_SUCCESS )
	{
		p_pool_key->obj.pfn_destroy( &p_pool_key->obj, NULL );

		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s\n", ib_get_err_str(status)) );
		return status;
	}

	/* From the PD, get the AL handle of the pool_key. */
	h_al = h_pd->obj.h_al;

	/* Add this pool_key to the AL instance. */
	al_insert_key( h_al, p_pool_key );

	/*
	 * Take a reference on the global pool_key for this CA, if it exists.
	 * Note that the pool_key does not exist for the global MAD pool in
	 * user-mode, as that MAD pool never registers memory on a PD.
	 */
	/* TODO: Is the pool_key check here needed since this is a kernel-only implementation? */
	if( key_type == AL_KEY_ALIAS && h_pd->obj.p_ci_ca->pool_key )
	{
		ref_al_obj( &h_pd->obj.p_ci_ca->pool_key->obj );
		p_pool_key->pool_key = h_pd->obj.p_ci_ca->pool_key;
	}

	/* Return the pool key. */
	*pp_pool_key = (ib_pool_key_t)p_pool_key;

	AL_EXIT( AL_DBG_MAD_POOL );
	return IB_SUCCESS;
}


/*
 * The destroying callback releases the memory registration.  This is needed
 * to maintain the destroy semantics, where the pool key's destruction is
 * async, but the MAD registrations are sync.  This means that all memory
 * registered on a pool key is deregistered before the pool key leaves the
 * destroy call.
 */
static void
__destroying_pool_key(
	IN				al_obj_t*					p_obj )
{
	al_pool_key_t*			p_pool_key;

	CL_ASSERT( p_obj );
	p_pool_key = PARENT_STRUCT( p_obj, al_pool_key_t, obj );

	/* Remove this pool_key from the AL instance. */
	al_remove_key( p_pool_key );
	if( p_pool_key->h_mr )
		ib_dereg_mr( p_pool_key->h_mr );

	p_pool_key->lkey = 0;
}


/*
 * Release all references on objects that were needed by the pool key.
 */
static void
__cleanup_pool_key(
	IN				al_obj_t*					p_obj )
{
	cl_list_item_t			*p_list_item, *p_next_item;
	ib_mad_element_t		*p_mad_element_list, *p_last_mad_element;
	al_mad_element_t		*p_mad;
	ib_api_status_t			status;
	al_pool_key_t*			p_pool_key;

	CL_ASSERT( p_obj );
	p_pool_key = PARENT_STRUCT( p_obj, al_pool_key_t, obj );

	CL_ASSERT( !p_pool_key->mad_cnt );

	/* Search for any outstanding MADs associated with the given pool key. */
	if( p_pool_key->mad_cnt )
	{
		p_mad_element_list = p_last_mad_element = NULL;

		cl_spinlock_acquire( &p_pool_key->obj.h_al->mad_lock );
		for( p_list_item = cl_qlist_head( &p_pool_key->obj.h_al->mad_list );
			 p_list_item != cl_qlist_end( &p_pool_key->obj.h_al->mad_list );
			 p_list_item = p_next_item )
		{
			p_next_item = cl_qlist_next( p_list_item );
			p_mad = PARENT_STRUCT( p_list_item, al_mad_element_t, al_item );

			if( p_mad->pool_key != p_pool_key ) continue;

			/* Build the list of MADs to be returned to pool. */
			if( p_last_mad_element )
				p_last_mad_element->p_next = &p_mad->element;
			else
				p_mad_element_list = &p_mad->element;

			p_last_mad_element = &p_mad->element;
			p_last_mad_element->p_next = NULL;
		}
		cl_spinlock_release( &p_pool_key->obj.h_al->mad_lock );

		/* Return any outstanding MADs to the pool. */
		if( p_mad_element_list )
		{
			status = ib_put_mad( p_mad_element_list );
			if( status != IB_SUCCESS )
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("ib_put_mad failed with status %s, continuing.\n",
					ib_get_err_str(status)) );
			}
		}
	}

	/*
	 * Remove the pool key from the pool to prevent further registrations
	 * against this pool.
	 */
	if( p_pool_key->type == AL_KEY_NORMAL && p_pool_key->pool_item.p_next != NULL)
	{
		cl_spinlock_acquire( &p_pool_key->h_pool->obj.lock );
		cl_qlist_remove_item( &p_pool_key->h_pool->key_list,
			&p_pool_key->pool_item );
		cl_spinlock_release( &p_pool_key->h_pool->obj.lock );
	}

	deref_al_obj( &p_pool_key->h_pool->obj );
	p_pool_key->h_pool = NULL;
	if( p_pool_key->pool_key )
		deref_al_obj( &p_pool_key->pool_key->obj );
}



/*
 * Free a pool key.
 */
static void
__free_pool_key(
	IN				al_obj_t*					p_obj )
{
	al_pool_key_t*			p_pool_key;

	CL_ASSERT( p_obj );
	p_pool_key = PARENT_STRUCT( p_obj, al_pool_key_t, obj );

	destroy_al_obj( &p_pool_key->obj );
	cl_free( p_pool_key );
}


/*
 * Deregister a MAD pool from a protection domain.  Only normal pool_keys
 * can be destroyed using this routine.
 */
ib_api_status_t
ib_dereg_mad_pool(
	IN		const	ib_pool_key_t				pool_key )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MAD_POOL );

	if( AL_OBJ_INVALID_HANDLE( pool_key, AL_OBJ_TYPE_H_POOL_KEY ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	ref_al_obj( &pool_key->obj );
	status = dereg_mad_pool( pool_key, AL_KEY_NORMAL );

	if( status != IB_SUCCESS )
		deref_al_obj( &pool_key->obj );

	AL_EXIT( AL_DBG_MAD_POOL );
	return status;
}



/*
 * Deregister a MAD pool from a protection domain.
 */
ib_api_status_t
dereg_mad_pool(
	IN		const	ib_pool_key_t				pool_key,
	IN		const	al_key_type_t				expected_type )
{
	AL_ENTER( AL_DBG_MAD_POOL );

	if( pool_key->type != expected_type )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Check mad_cnt to see if MADs are still outstanding. */
	//if( pool_key->mad_cnt )
	//{
	//	AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_MAD_POOL, ("IB_RESOURCE_BUSY\n") );
	//	return IB_RESOURCE_BUSY;
	//}

	pool_key->obj.pfn_destroy( &pool_key->obj, NULL );

	AL_EXIT( AL_DBG_MAD_POOL );
	return IB_SUCCESS;
}



/*
 * Obtain a MAD element from the pool.
 */
static ib_api_status_t
__get_mad_element(
	IN		const	ib_pool_key_t				pool_key,
		OUT			al_mad_element_t**			pp_mad_element )
{
	mad_item_t*				p_mad_item;
	net32_t					lkey;

	AL_ENTER( AL_DBG_MAD_POOL );

	CL_ASSERT( pool_key );
	CL_ASSERT( pp_mad_element );

	/* Obtain a MAD item from the stack. */
	p_mad_item = (mad_item_t*)ExAllocateFromNPagedLookasideList(
		&pool_key->h_pool->mad_stack );
	if( !p_mad_item )
		return IB_INSUFFICIENT_RESOURCES;

	p_mad_item->pool_key = pool_key;

	if( pool_key->type == AL_KEY_NORMAL )
		lkey = pool_key->lkey;
	else
		lkey = pool_key->pool_key->lkey;

	CL_ASSERT( ADDRESS_AND_SIZE_TO_SPAN_PAGES(
		p_mad_item->al_mad_element.mad_buf, MAD_BLOCK_GRH_SIZE ) == 1 );

	/* Clear the element. */
	cl_memclr( &p_mad_item->al_mad_element, sizeof(al_mad_element_t) );

	/* Initialize the receive data segment information. */
	p_mad_item->al_mad_element.grh_ds.vaddr =
		cl_get_physaddr( p_mad_item->al_mad_element.mad_buf );
	p_mad_item->al_mad_element.grh_ds.length = MAD_BLOCK_GRH_SIZE;
	p_mad_item->al_mad_element.grh_ds.lkey = lkey;

	/* Initialize the send data segment information. */
	p_mad_item->al_mad_element.mad_ds.vaddr =
		p_mad_item->al_mad_element.grh_ds.vaddr + sizeof(ib_grh_t);
	p_mad_item->al_mad_element.mad_ds.length = MAD_BLOCK_SIZE;
	p_mad_item->al_mad_element.mad_ds.lkey = lkey;

	/* Initialize grh */
	p_mad_item->al_mad_element.element.p_grh =
		(ib_grh_t*)p_mad_item->al_mad_element.mad_buf;

	/* Hold a reference on the pool key while a MAD element is removed. */
	ref_al_obj( &pool_key->obj );
	cl_atomic_inc( &pool_key->mad_cnt );

	p_mad_item->al_mad_element.pool_key = (ib_pool_key_t)pool_key;
	/* Return the MAD element. */
	*pp_mad_element = &p_mad_item->al_mad_element;

	AL_EXIT( AL_DBG_MAD_POOL );
	return IB_SUCCESS;
}



/*
 * Return a MAD element to the pool.
 */
static void
__put_mad_element(
	IN				al_mad_element_t*			p_mad_element )
{
	mad_item_t*				p_mad_item;
	ib_pool_key_t			pool_key;

	CL_ASSERT( p_mad_element );
	p_mad_item = PARENT_STRUCT( p_mad_element, mad_item_t, al_mad_element );
	pool_key = p_mad_item->pool_key;
	CL_ASSERT( pool_key );
	CL_ASSERT( pool_key->h_pool );

	/* Clear the MAD buffer. */
	cl_memclr( p_mad_element->mad_buf, MAD_BLOCK_GRH_SIZE );
	p_mad_element->element.p_next = NULL;

	/* Return the MAD element to the pool. */
	ExFreeToNPagedLookasideList( &pool_key->h_pool->mad_stack, p_mad_item );

	cl_atomic_dec( &pool_key->mad_cnt );
	deref_al_obj( &pool_key->obj );
}



ib_mad_send_handle_t
get_mad_send(
	IN		const	al_mad_element_t			*p_mad_element )
{
	mad_item_t*				p_mad_item;
	mad_send_t				*p_mad_send;

	CL_ASSERT( p_mad_element );

	/* Get a handle to the pool. */
	p_mad_item = PARENT_STRUCT( p_mad_element, mad_item_t, al_mad_element );
	CL_ASSERT( p_mad_item->pool_key );
	CL_ASSERT( p_mad_item->pool_key->h_pool );

	p_mad_send = ExAllocateFromNPagedLookasideList(
		&p_mad_item->pool_key->h_pool->mad_send_pool );
	if( !p_mad_send )
		return NULL;

#if DBG
	insert_pool_trace(&g_mad_trace, &p_mad_send->mad_send, POOL_GET, 0);
#endif

	p_mad_send->mad_send.canceled = FALSE;
	p_mad_send->mad_send.p_send_mad = NULL;
	p_mad_send->mad_send.p_resp_mad = NULL;
	p_mad_send->mad_send.h_av = NULL;
	p_mad_send->mad_send.retry_cnt = 0;
	p_mad_send->mad_send.retry_time = 0;
	p_mad_send->mad_send.delay = 0;
	p_mad_send->h_pool = p_mad_item->pool_key->h_pool;

	ref_al_obj( &p_mad_item->pool_key->h_pool->obj );
	return &p_mad_send->mad_send;
}



void
put_mad_send(
	IN				ib_mad_send_handle_t		h_mad_send )
{
	mad_send_t			*p_mad_send;
	ib_pool_handle_t	h_pool;

	p_mad_send = PARENT_STRUCT( h_mad_send, mad_send_t, mad_send );
	h_pool = p_mad_send->h_pool;

	ExFreeToNPagedLookasideList( &h_pool->mad_send_pool, p_mad_send );
	deref_al_obj( &h_pool->obj );
}



al_mad_rmpp_t*
get_mad_rmpp(
	IN		const	al_mad_element_t			*p_mad_element )
{
	mad_item_t		*p_mad_item;
	mad_rmpp_t		*p_mad_rmpp;

	CL_ASSERT( p_mad_element );

	/* Get a handle to the pool. */
	p_mad_item = PARENT_STRUCT( p_mad_element, mad_item_t, al_mad_element );
	CL_ASSERT( p_mad_item->pool_key );
	CL_ASSERT( p_mad_item->pool_key->h_pool );

	p_mad_rmpp = ExAllocateFromNPagedLookasideList(
		&p_mad_item->pool_key->h_pool->mad_rmpp_pool );
	if( !p_mad_rmpp )
		return NULL;

	p_mad_rmpp->h_pool = p_mad_item->pool_key->h_pool;

	ref_al_obj( &p_mad_item->pool_key->h_pool->obj );
	return &p_mad_rmpp->mad_rmpp;
}



void
put_mad_rmpp(
	IN				al_mad_rmpp_t*				h_mad_rmpp )
{
	mad_rmpp_t			*p_mad_rmpp;
	ib_pool_handle_t	h_pool;

	p_mad_rmpp = PARENT_STRUCT( h_mad_rmpp, mad_rmpp_t, mad_rmpp );

	h_pool = p_mad_rmpp->h_pool;

	ExFreeToNPagedLookasideList( &h_pool->mad_rmpp_pool, p_mad_rmpp );
	deref_al_obj( &h_pool->obj );
}



ib_api_status_t
ib_get_mad_inner(
	IN		const	ib_pool_key_t				pool_key,
	IN		const	size_t						buf_size,
		OUT			ib_mad_element_t			**pp_mad_element )
{
	al_mad_element_t*		p_mad;
	ib_api_status_t			status;
	al_obj_t*				p_ci_ca_obj;

	AL_ENTER( AL_DBG_MAD_POOL );

	if( AL_OBJ_INVALID_HANDLE( pool_key, AL_OBJ_TYPE_H_POOL_KEY ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}
	if( !buf_size || !pp_mad_element )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = __get_mad_element( pool_key, &p_mad );
	if( status != IB_SUCCESS )
	{
		AL_EXIT( AL_DBG_MAD_POOL );
		return status;
	}

	/* Set the user accessible buffer. */
	if( buf_size <= MAD_BLOCK_SIZE )
	{
		/* Use the send buffer for 256 byte MADs. */
		p_mad->element.p_mad_buf = (ib_mad_t*)(p_mad->mad_buf + sizeof(ib_grh_t));
	}
	else if( buf_size >= 0xFFFFFFFF )
	{
		__put_mad_element( p_mad );
		return IB_INVALID_SETTING;
	}
	else
	{
		/* Allocate a new buffer for the MAD. */
		p_mad->p_al_mad_buf = cl_zalloc( buf_size );
		if( !p_mad->p_al_mad_buf )
		{
			__put_mad_element( p_mad );
			AL_EXIT( AL_DBG_MAD_POOL );
			return IB_INSUFFICIENT_MEMORY;
		}
		p_mad->element.p_mad_buf = p_mad->p_al_mad_buf;
	}
	p_mad->element.size = (uint32_t)buf_size;

	/* Track the MAD element with the requesting AL instance. */
	p_ci_ca_obj = &(pool_key->obj.p_ci_ca->obj);
	cl_spinlock_acquire( &p_ci_ca_obj->lock );
	if( p_ci_ca_obj->state >= CL_DESTROYING )
	{
		cl_spinlock_release( &p_ci_ca_obj->lock );
		__put_mad_element( p_mad );
		AL_EXIT( AL_DBG_MAD_POOL );
		return IB_INVALID_STATE;
	}
	al_insert_mad( pool_key->h_al, p_mad );
	cl_spinlock_release( &p_ci_ca_obj->lock );

	/* Return the MAD element to the client. */
	*pp_mad_element = &p_mad->element;

	AL_EXIT( AL_DBG_MAD_POOL );
	return IB_SUCCESS;
}



ib_api_status_t
ib_put_mad_inner(
	IN		const	ib_mad_element_t*			p_mad_element_list )
{
	al_mad_element_t*		p_mad;

	if( !p_mad_element_list )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	while( p_mad_element_list )
	{
		p_mad = PARENT_STRUCT( p_mad_element_list, al_mad_element_t, element );
		p_mad_element_list = p_mad_element_list->p_next;

		/* Deallocate any buffers allocated for the user. */
		if( p_mad->p_al_mad_buf )
		{
			cl_free( p_mad->p_al_mad_buf );
			p_mad->p_al_mad_buf = NULL;
		}

		/* See if the MAD has already been returned to the MAD pool. */
		CL_ASSERT( p_mad->h_al );

		/* Remove the MAD element from the owning AL instance. */
		al_remove_mad( p_mad );

		/* Return the MAD element to the pool. */
		__put_mad_element( p_mad );
	}

	return IB_SUCCESS;
}

#pragma warning(disable:4100) //unreferenced formal parameter
ib_api_status_t
ib_get_mad_insert(
	IN 		char*				 				file,
	IN 		LONG 				 				line,
	IN		const	ib_pool_key_t				pool_key,
	IN		const	size_t						buf_size,
		OUT			ib_mad_element_t			**pp_mad_element )
{
	ib_api_status_t status = ib_get_mad_inner( pool_key, buf_size, pp_mad_element );

#if DBG
	al_mad_element_t* p_mad = PARENT_STRUCT( *pp_mad_element, al_mad_element_t, element );
	uint32_t	entry_num = InterlockedIncrement(&g_mad_elem_trace.trace_index) - 1;
		
	entry_num = entry_num & (POOL_TRACE_ENTRIES-1);

	g_mad_elem_trace.trace[entry_num].p_mad_elem = p_mad;
	g_mad_elem_trace.trace[entry_num].change = POOL_GET;
	g_mad_elem_trace.trace[entry_num].file_name = file;
	g_mad_elem_trace.trace[entry_num].line_num = line;

#endif

	return status;
}

#pragma warning(disable:4100) //unreferenced formal parameter
ib_api_status_t
ib_put_mad_insert(
	IN 		char*				 				file,
	IN 		LONG 				 				line,
	IN		const	ib_mad_element_t*			p_mad_element_list )
{
#if DBG
	const ib_mad_element_t* p_mad_element_dbg_list = p_mad_element_list;

	if( !p_mad_element_dbg_list )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	while( p_mad_element_dbg_list )
	{
		al_mad_element_t* p_mad = PARENT_STRUCT( p_mad_element_list, al_mad_element_t, element );
		uint32_t	entry_num = InterlockedIncrement(&g_mad_elem_trace.trace_index) - 1;
			
		entry_num = entry_num & (POOL_TRACE_ENTRIES-1);
		
		g_mad_elem_trace.trace[entry_num].p_mad_elem = p_mad;
		g_mad_elem_trace.trace[entry_num].change = POOL_PUT;
		g_mad_elem_trace.trace[entry_num].file_name = file;
		g_mad_elem_trace.trace[entry_num].line_num = line;
		
		p_mad_element_dbg_list = p_mad_element_dbg_list->p_next;
	}
#endif

	return ib_put_mad_inner(p_mad_element_list);

}


/*
 * Resize the data buffer associated with a MAD element.
 */
ib_api_status_t
al_resize_mad(
		OUT			ib_mad_element_t			*p_mad_element,
	IN		const	size_t						buf_size )
{
	al_mad_element_t		*p_al_element;
	ib_mad_t				*p_new_buf;

	CL_ASSERT( p_mad_element );

	/* We only support growing the buffer for now. */
	CL_ASSERT( buf_size > p_mad_element->size );

	/* Cap the size. */
	if( buf_size >= 0xFFFFFFFF )
		return IB_INVALID_SETTING;

	p_al_element = PARENT_STRUCT( p_mad_element, al_mad_element_t, element );

	/* Allocate a new buffer. */
	p_new_buf = cl_malloc( buf_size );
	if( !p_new_buf )
		return IB_INSUFFICIENT_MEMORY;

	/* Copy the existing buffer's data into the new buffer. */
	cl_memcpy( p_new_buf, p_mad_element->p_mad_buf, p_mad_element->size );
	cl_memclr( (uint8_t*)p_new_buf + p_mad_element->size,
		buf_size - p_mad_element->size );

	/* Update the MAD element to use the new buffer. */
	p_mad_element->p_mad_buf = p_new_buf;
	p_mad_element->size = (uint32_t)buf_size;

	/* Free any old buffer. */
	if( p_al_element->p_al_mad_buf )
		cl_free( p_al_element->p_al_mad_buf );
	p_al_element->p_al_mad_buf = p_new_buf;

	return IB_SUCCESS;
}

