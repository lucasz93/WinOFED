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


typedef struct _mad_reg
{
	al_obj_t				obj;			/* Child of al_pool_key_t */
	ib_mr_handle_t			h_mr;
	net32_t					lkey;
	net32_t					rkey;
	mad_array_t*			p_mad_array;

}	mad_reg_t;



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
__cleanup_pool_key(
	IN				al_obj_t*					p_obj );

static void
__free_pool_key(
	IN				al_obj_t*					p_obj );

static ib_api_status_t
__reg_mad_array(
	IN				al_pool_key_t*	const	p_pool_key,
	IN				mad_array_t*	const	p_mad_array );

static void
__free_mad_reg(
	IN				al_obj_t*					p_obj );

static ib_api_status_t
__init_mad_element(
	IN		const	al_pool_key_t*				p_pool_key,
	IN	OUT			mad_item_t*					p_mad_item );

static cl_status_t
__locate_reg_cb(
	IN		const	cl_list_item_t* const		p_list_item,
	IN				void*						context );

static ib_api_status_t
__grow_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
		OUT			mad_item_t**				pp_mad_item OPTIONAL );

static void
__free_mad_array(
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
	cl_status_t				cl_status;

	AL_ENTER(AL_DBG_MAD_POOL);

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !ph_pool )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Validate the min and max parameters. */
	if( (min > 0) && (max > 0) && (min > max) )
		return IB_INVALID_SETTING;

	h_pool = cl_zalloc( sizeof( al_pool_t ) );
	if( !h_pool )
		return IB_INSUFFICIENT_MEMORY;

	/* Initialize the pool lists. */
	cl_qlist_init( &h_pool->mad_stack );
	cl_qlist_init( &h_pool->key_list );
	cl_qpool_construct( &h_pool->mad_send_pool );
	cl_qpool_construct( &h_pool->mad_rmpp_pool );

	/* Initialize the pool object. */
	construct_al_obj( &h_pool->obj, AL_OBJ_TYPE_H_MAD_POOL );
	status = init_al_obj( &h_pool->obj, h_pool, TRUE,
		__destroying_pool, NULL, __free_pool );
	if( status != IB_SUCCESS )
	{
		__free_pool( &h_pool->obj );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Attach the pool to the AL object. */
	attach_al_obj( &h_al->obj, &h_pool->obj );

	/* Save the pool parameters.  Set grow_size to min for initialization. */
	h_pool->max = max;
	if( min )
	{
		h_pool->grow_size = min;

		/* Grow the pool to the minimum size. */
		status = __grow_mad_pool( h_pool, NULL );
		if( status != IB_SUCCESS )
		{
			h_pool->obj.pfn_destroy( &h_pool->obj, NULL );
			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("grow_mad_pool failed with status %s.\n",
				ib_get_err_str(status)) );
			return status;
		}
	}

	/* Save the grow_size for subsequent allocations. */
	h_pool->grow_size = grow_size;

	/* Initialize the pool of mad send tracking structures. */
	cl_status = cl_qpool_init( &h_pool->mad_send_pool,
		min, max, grow_size, sizeof( mad_send_t ),
		__mad_send_init, NULL, h_pool );
	if( cl_status != CL_SUCCESS )
	{
		h_pool->obj.pfn_destroy( &h_pool->obj, NULL );
		status = ib_convert_cl_status( cl_status );
		h_pool->obj.pfn_destroy( &h_pool->obj, NULL );

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("cl_qpool_init failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Initialize the pool of mad send tracking structures. */
	cl_status = cl_qpool_init( &h_pool->mad_rmpp_pool,
		min, max, grow_size, sizeof( mad_rmpp_t ),
		__mad_rmpp_init, NULL, h_pool );
	if( cl_status != CL_SUCCESS )
	{
		h_pool->obj.pfn_destroy( &h_pool->obj, NULL );
		status = ib_convert_cl_status( cl_status );
		h_pool->obj.pfn_destroy( &h_pool->obj, NULL );

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("cl_qpool_init failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Return the pool handle. */
	*ph_pool = h_pool;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_pool->obj, E_REF_INIT );

	AL_EXIT(AL_DBG_MAD_POOL);
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

	AL_ENTER(AL_DBG_MAD_POOL);

	CL_ASSERT( p_obj );
	h_pool = PARENT_STRUCT( p_obj, al_pool_t, obj );

	/* Get the AL instance of this MAD pool. */
	p_obj = h_pool->obj.p_parent_obj;
	h_al = PARENT_STRUCT( p_obj, ib_al_t, obj );

	/* Deregister this MAD pool from all protection domains. */
	al_dereg_pool( h_al, h_pool );

	AL_EXIT(AL_DBG_MAD_POOL);
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

	cl_qpool_destroy( &h_pool->mad_send_pool );
	cl_qpool_destroy( &h_pool->mad_rmpp_pool );
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

	AL_ENTER(AL_DBG_MAD_POOL);

	if( AL_OBJ_INVALID_HANDLE( h_pool, AL_OBJ_TYPE_H_MAD_POOL ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_HANDLE\n") );
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
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("h_pool (0x%p) is busy!.\n", h_pool) );
		return IB_RESOURCE_BUSY;
	}

	ref_al_obj( &h_pool->obj );
	h_pool->obj.pfn_destroy( &h_pool->obj, NULL );

	AL_EXIT(AL_DBG_MAD_POOL);
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
	al_pool_key_t*			p_pool_key;
	cl_list_item_t*			p_array_item;
	al_obj_t*				p_obj;
	ib_al_handle_t			h_al;
	mad_array_t*			p_mad_array;
	ib_api_status_t			status;
	al_key_type_t			key_type;

	AL_ENTER(AL_DBG_MAD_POOL);

	if( AL_OBJ_INVALID_HANDLE( h_pool, AL_OBJ_TYPE_H_MAD_POOL ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}
	/* Alias keys require an alias PD. */
	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}
	if( !pp_pool_key )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
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
	p_pool_key->h_pd = h_pd;

	/* Initialize the pool key object. */
	status = init_al_obj( &p_pool_key->obj, p_pool_key, TRUE,
		NULL, __cleanup_pool_key, __free_pool_key );
	if( status != IB_SUCCESS )
	{
		__free_pool_key( &p_pool_key->obj );

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	status = attach_al_obj( &h_pd->obj, &p_pool_key->obj );
	if( status != IB_SUCCESS )
	{
		p_pool_key->obj.pfn_destroy( &p_pool_key->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* From the PD, get the AL handle of the pool_key. */
	p_obj = h_pd->obj.p_parent_obj->p_parent_obj;
	h_al = PARENT_STRUCT( p_obj, ib_al_t, obj );

	/* Add this pool_key to the AL instance. */
	al_insert_key( h_al, p_pool_key );

	ref_al_obj( &h_pd->obj );
	ref_al_obj( &h_pool->obj );

	/*
	 * Take a reference on the global pool_key for this CA, if it exists.
	 * Note that the pool_key does not exist for the global MAD pool in
	 * user-mode, as that MAD pool never registers memory on a PD.
	 */
	if( key_type == AL_KEY_ALIAS && h_pd->obj.p_ci_ca->pool_key )
	{
		ref_al_obj( &h_pd->obj.p_ci_ca->pool_key->obj );
		p_pool_key->pool_key = h_pd->obj.p_ci_ca->pool_key;
	}

	/* Register the pool on the protection domain. */
	if( key_type == AL_KEY_NORMAL )
	{
		/* Chain the pool key onto the pool. */
		cl_spinlock_acquire( &h_pool->obj.lock );
		cl_qlist_insert_tail( &h_pool->key_list, &p_pool_key->pool_item );

		/* Synchronize with growing the MAD pool. */
		for( p_array_item = cl_qlist_head( &h_pool->obj.obj_list );
			 p_array_item != cl_qlist_end( &h_pool->obj.obj_list );
			 p_array_item = cl_qlist_next( p_array_item ) )
		{
			p_obj = PARENT_STRUCT( p_array_item, al_obj_t, pool_item );
			p_mad_array = PARENT_STRUCT( p_obj, mad_array_t, obj );

			status = __reg_mad_array( p_pool_key, p_mad_array );
			
			if( status != IB_SUCCESS )
			{
				cl_spinlock_release( &h_pool->obj.lock );
				p_pool_key->obj.pfn_destroy( &p_pool_key->obj, NULL );

				AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
					("reg_mad_array failed with status %s.\n",
					ib_get_err_str(status)) );
				return status;
			}
		}
		cl_spinlock_release( &h_pool->obj.lock );
	}

	/*
	 * If the PD is of alias type, then we need to create/register an
	 * equivalent pool key in the kernel.
	 */
	if( h_pd->type == IB_PDT_ALIAS )
	{
		status = create_reg_mad_pool( h_pool, h_pd, p_pool_key );
		if( status != IB_SUCCESS )
		{
			p_pool_key->obj.pfn_destroy( &p_pool_key->obj, NULL );
			return status;
		}
	}

	/* Return the pool key. */
	*pp_pool_key = (ib_pool_key_t)p_pool_key;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_pool_key->obj, E_REF_INIT );

	AL_EXIT(AL_DBG_MAD_POOL);
	return IB_SUCCESS;
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

	/* Search for any outstanding MADs associated with the given pool key. */
	if( p_pool_key->mad_cnt )
	{
		p_mad_element_list = p_last_mad_element = NULL;

		cl_spinlock_acquire( &p_pool_key->h_al->mad_lock );
		for( p_list_item = cl_qlist_head( &p_pool_key->h_al->mad_list );
			 p_list_item != cl_qlist_end( &p_pool_key->h_al->mad_list );
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
		cl_spinlock_release( &p_pool_key->h_al->mad_lock );

		/* Return any outstanding MADs to the pool. */
		if( p_mad_element_list )
		{
			status = ib_put_mad( p_mad_element_list );
			if( status != IB_SUCCESS )
			{
				AL_PRINT(TRACE_LEVEL_ERROR , AL_DBG_ERROR ,
					("ib_put_mad failed with status %s, continuing.\n",
					ib_get_err_str(status)) );
			}
		}
	}

	/*
	 * Remove the pool key from the pool to prevent further registrations
	 * against this pool.
	 *
	 * Warning: There is a small window where a pool key can be destroyed
	 * while its associated pool is growing.  In this case, the pool key
	 * will receive a new registration after it has been destroyed.  This
	 * is a result of having to register memory with the HCA without holding
	 * a lock, making correct synchronization impossible.  One solution to
	 * this problem is to register all of physical memory, which avoids
	 * having to register more memory as a MAD pool grows.
	 */
	if( p_pool_key->type == AL_KEY_NORMAL )
	{
		cl_spinlock_acquire( &p_pool_key->h_pool->obj.lock );
		cl_qlist_remove_item( &p_pool_key->h_pool->key_list,
			&p_pool_key->pool_item );
		cl_spinlock_release( &p_pool_key->h_pool->obj.lock );
	}

	/* Remove this pool_key from the AL instance. */
	al_remove_key( p_pool_key );

	/* User-mode only: cleanup kernel resources. */
	dereg_destroy_mad_pool( p_pool_key );

	deref_al_obj( &p_pool_key->h_pool->obj );
	p_pool_key->h_pool = NULL;
	deref_al_obj( &p_pool_key->h_pd->obj );
	p_pool_key->h_pd = NULL;
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
 * Register a MAD array with a protection domain.
 */
static ib_api_status_t
__reg_mad_array(
	IN				al_pool_key_t*	const	p_pool_key,
	IN				mad_array_t*	const	p_mad_array )
{
	mad_reg_t*				p_reg;
	ib_mr_create_t			mr_create;
	ib_api_status_t			status;

	CL_ASSERT( p_pool_key );
	CL_ASSERT( p_mad_array );

	/* Determine if there is memory to register. */
	if( p_mad_array->sizeof_array == 0 )
		return IB_SUCCESS;

	p_reg = cl_zalloc( sizeof( mad_reg_t ) );
	if( p_reg == NULL )
		return IB_INSUFFICIENT_MEMORY;

	/*
	 * Initialize the registration object.  We use synchronous
	 * destruction to deregister memory immediately.  Otherwise, the
	 * memory will be automatically deregistered when destroying the
	 * PD, which can lead to trying to deregister the memory twice.
	 */
	construct_al_obj( &p_reg->obj, AL_OBJ_TYPE_MAD_POOL );
	status = init_al_obj( &p_reg->obj, p_reg, FALSE,
		NULL, NULL, __free_mad_reg );
	if( status != IB_SUCCESS )
	{
		__free_mad_reg( &p_reg->obj );
		return status;
	}

	/* Attach the registration to the pool key. */
	attach_al_obj( &p_pool_key->obj, &p_reg->obj );

	if( p_pool_key->h_pd->type != IB_PDT_ALIAS )
	{
		/* Register the MAD array on the protection domain. */
		cl_memclr( &mr_create, sizeof( ib_mr_create_t ) );
		mr_create.vaddr = p_mad_array->p_data;
		mr_create.length = p_mad_array->sizeof_array;
		mr_create.access_ctrl = IB_AC_LOCAL_WRITE;

		status = ib_reg_mem( p_pool_key->h_pd, &mr_create, &p_reg->lkey,
			&p_reg->rkey, &p_reg->h_mr );
	}

	if( status != IB_SUCCESS )
	{
		p_reg->obj.pfn_destroy( &p_reg->obj, NULL );
		return status;
	}

	/* Save p_mad_array to match the registration with the array. */
	p_reg->p_mad_array = p_mad_array;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_reg->obj, E_REF_INIT );

	return IB_SUCCESS;
}



/*
 * Free a MAD registration.
 */
static void
__free_mad_reg(
	IN				al_obj_t*					p_obj )
{
	mad_reg_t*				p_reg;
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	p_reg = PARENT_STRUCT( p_obj, mad_reg_t, obj );

	/* Deregister the MAD array if it was registered. */
	if( p_reg->h_mr )
	{
		status = ib_dereg_mr( p_reg->h_mr );
		CL_ASSERT( status == IB_SUCCESS );
	}

	destroy_al_obj( &p_reg->obj );
	cl_free( p_reg );
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

	AL_ENTER(AL_DBG_MAD_POOL);

	if( AL_OBJ_INVALID_HANDLE( pool_key, AL_OBJ_TYPE_H_POOL_KEY ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	ref_al_obj( &pool_key->obj );
	status = dereg_mad_pool( pool_key, AL_KEY_NORMAL );

	if( status != IB_SUCCESS )
		deref_al_obj( &pool_key->obj );

	AL_EXIT(AL_DBG_MAD_POOL);
	return status;
}



/*
 * Deregister a MAD pool from a protection domain.
 */
ib_api_status_t
dereg_mad_pool(
	IN		const	ib_pool_key_t				pool_key ,
	IN		const	al_key_type_t				expected_type )
{
	AL_ENTER(AL_DBG_MAD_POOL);

	if( pool_key->type != expected_type )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	///* Check mad_cnt to see if MADs are still outstanding. */
	//if( pool_key->mad_cnt )
	//{
	//	AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_MAD_POOL, ("IB_RESOURCE_BUSY\n") );
	//	return IB_RESOURCE_BUSY;
	//}

	pool_key->obj.pfn_destroy( &pool_key->obj, NULL );

	AL_EXIT(AL_DBG_MAD_POOL);
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
	al_pool_key_t*			p_pool_key;
	ib_pool_handle_t		h_pool;
	cl_list_item_t*			p_item;
	mad_item_t*				p_mad_item;
	ib_api_status_t			status;

	AL_ENTER(AL_DBG_MAD_POOL);

	CL_ASSERT( pool_key );
	CL_ASSERT( pp_mad_element );

	p_pool_key = (al_pool_key_t*)pool_key;
	h_pool = p_pool_key->h_pool;

	/* Obtain a MAD item from the stack. */
	cl_spinlock_acquire( &h_pool->obj.lock );
	p_item = cl_qlist_remove_head( &h_pool->mad_stack );
	p_mad_item = PARENT_STRUCT( p_item, mad_item_t, al_mad_element.list_item );
	if( p_item == cl_qlist_end( &h_pool->mad_stack ) )
	{
		/* The stack was empty.  Grow the pool and obtain a new item. */
		cl_spinlock_release( &h_pool->obj.lock );
		status = __grow_mad_pool( h_pool, &p_mad_item );
		if( status != IB_SUCCESS )
		{
			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("grow_mad_pool failed with status %s.\n",
				ib_get_err_str(status)) );
			return status;
		}
	}
	else
	{
		cl_spinlock_release( &h_pool->obj.lock );
	}

	/* Get the local data segment information for this pool key. */
	status = __init_mad_element( p_pool_key, p_mad_item );
	if( status != IB_SUCCESS )
	{
		cl_spinlock_acquire( &h_pool->obj.lock );
		cl_qlist_insert_head( &h_pool->mad_stack,
			&p_mad_item->al_mad_element.list_item );
		cl_spinlock_release( &h_pool->obj.lock );

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("init_mad_element failed with status %s.\n",
			ib_get_err_str(status)) );
		return status;
	}

	/* Hold a reference on the array while a MAD element is removed. */
	ref_al_obj( &p_mad_item->p_mad_array->obj );

	p_mad_item->al_mad_element.pool_key = (ib_pool_key_t)pool_key;
	/* Return the MAD element. */
	*pp_mad_element = &p_mad_item->al_mad_element;

	AL_EXIT(AL_DBG_MAD_POOL);
	return IB_SUCCESS;
}



static void
__setup_mad_element(
	IN	OUT			al_mad_element_t* const		p_al_mad_element,
	IN		const	uint32_t					lkey )
{
	/* Clear the MAD element. */
	cl_memclr( &p_al_mad_element->element, sizeof( ib_mad_element_t ) );

	/* Initialize the receive data segment information. */
	p_al_mad_element->grh_ds.lkey = lkey;

	/* Initialize the send data segment information. */
	p_al_mad_element->mad_ds.lkey = lkey;

	/* Initialize grh */
	p_al_mad_element->element.p_grh =
		(ib_grh_t*)(uintn_t)p_al_mad_element->grh_ds.vaddr;
}



/*
 * Initialize the MAD element local data segment for this pool key.
 */
static ib_api_status_t
__init_mad_element(
	IN		const	al_pool_key_t*				p_pool_key,
	IN	OUT			mad_item_t*					p_mad_item )
{
	cl_list_item_t			*p_item;
	cl_qlist_t				*p_list;
	al_obj_t				*p_obj;
	mad_reg_t				*p_reg;
	ib_pool_handle_t		h_pool;

	CL_ASSERT( p_pool_key );
	CL_ASSERT( p_mad_item != NULL );

	/* Find the MAD array registration entry. */
	if( p_pool_key->type == AL_KEY_NORMAL )
	{
		p_list = (cl_qlist_t*)&p_pool_key->obj.obj_list;
	}
	else
	{
#if defined( CL_KERNEL )
		/* Search the registrations on the actual pool key, not the alias. */
		p_list = (cl_qlist_t*)&p_pool_key->pool_key->obj.obj_list;
#else
		/*
		 * Note that MAD elements used by user-mode clients on special QPs
		 * are not registered on a user-mode PD.  The user-mode MAD elements
		 * must be copied into a kernel-mode MAD element before being sent.
		 */
		__setup_mad_element( &p_mad_item->al_mad_element, 0 );
		return IB_SUCCESS;
#endif
	}

	/* Prevent MAD array registrations. */
	h_pool = p_pool_key->h_pool;
	cl_spinlock_acquire( &h_pool->obj.lock );

	/* Search for the registration entry. */
	p_item = cl_qlist_find_from_head( p_list, __locate_reg_cb,
		p_mad_item->p_mad_array );
	if( p_item == cl_qlist_end( p_list ) )
	{
		cl_spinlock_release( &h_pool->obj.lock );
		return IB_NOT_FOUND;
	}

	/* Allow MAD array registrations. */
	cl_spinlock_release( &h_pool->obj.lock );

	/* Get a pointer to the registration. */
	p_obj = PARENT_STRUCT( p_item, al_obj_t, pool_item );
	p_reg = PARENT_STRUCT( p_obj, mad_reg_t, obj );
	__setup_mad_element( &p_mad_item->al_mad_element, p_reg->lkey );

	return IB_SUCCESS;
}



/*
 * Determine if a registration is for a given array.
 */
static cl_status_t
__locate_reg_cb(
	IN		const	cl_list_item_t* const		p_list_item,
	IN				void*						context )
{
	al_obj_t*				p_obj;
	mad_reg_t*				p_reg;
	mad_array_t*			p_mad_array;

	CL_ASSERT( p_list_item );
	CL_ASSERT( context );

	p_obj = PARENT_STRUCT( p_list_item,	al_obj_t, pool_item );
	p_reg = PARENT_STRUCT( p_obj, mad_reg_t, obj );
	p_mad_array = context;

	return ( p_reg->p_mad_array == p_mad_array ) ? CL_SUCCESS : CL_NOT_FOUND;
}



/*
 * Return a MAD element to the pool.
 */
static void
__put_mad_element(
	IN				al_mad_element_t*			p_mad_element )
{
	mad_item_t*				p_mad_item;
	ib_pool_handle_t		h_pool;

	CL_ASSERT( p_mad_element );
	p_mad_item = PARENT_STRUCT( p_mad_element, mad_item_t, al_mad_element );

	/* Get a handle to the pool. */
	h_pool = p_mad_item->p_mad_array->h_pool;

	/* Clear the MAD buffer. */
	cl_memclr(
		(uint8_t*)(uintn_t)p_mad_element->grh_ds.vaddr, MAD_BLOCK_GRH_SIZE );
	p_mad_element->element.p_next = NULL;

	/* Return the MAD element to the pool. */
	cl_spinlock_acquire( &h_pool->obj.lock );
	cl_qlist_insert_head( &h_pool->mad_stack,
		&p_mad_item->al_mad_element.list_item );
	cl_spinlock_release( &h_pool->obj.lock );

	/* Dereference the array when a MAD element is returned. */
	deref_al_obj( &p_mad_item->p_mad_array->obj );
}



/*
 * Grow a MAD pool.
 */
static ib_api_status_t
__grow_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
		OUT			mad_item_t**				pp_mad_item OPTIONAL )
{
	size_t					i;
	size_t					alloc_size;
	uint8_t*				p_data;
	mad_array_t*			p_mad_array;
	mad_item_t*				p_mad_item;
	mad_item_t*				p_mad_items;
	cl_list_item_t*			p_key_item;
	al_pool_key_t*			p_pool_key;
	ib_api_status_t			status;

	AL_ENTER(AL_DBG_MAD_POOL);

	CL_ASSERT( h_pool );

	/* Determine if the pool is allowed to grow. */
	if( h_pool->grow_size == 0 )
		return IB_INSUFFICIENT_RESOURCES;

	/* Lock the pool. */
	cl_spinlock_acquire( &h_pool->obj.lock );

	/* Determine if the pool has a maximum. */
	if( h_pool->max != 0 )
	{
		/* Determine if the pool maximum has been reached. */
		if( h_pool->actual >= h_pool->max )
		{
			cl_spinlock_release( &h_pool->obj.lock );

			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("h_pool's (0x%p) maximum has been reached.\n", h_pool) );
			return IB_INSUFFICIENT_RESOURCES;
		}

		/* Determine if growing the pool will exceed the maximum. */
		if( (h_pool->actual + h_pool->grow_size) > h_pool->max )
		{
			cl_spinlock_release( &h_pool->obj.lock );

			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("h_pool's (0x%p) will exceed maximum on grow.\n", h_pool) );
			return IB_INSUFFICIENT_RESOURCES;
		}
	}

	/* Calculate the allocation size. */
	alloc_size = sizeof( mad_item_t );
	alloc_size += MAD_BLOCK_GRH_SIZE;
	alloc_size *= h_pool->grow_size;
	alloc_size += sizeof( mad_array_t );

	/* Allocate a MAD data array and item structures. */
	p_data = cl_zalloc( alloc_size );
	if( p_data == NULL )
	{
		cl_spinlock_release( &h_pool->obj.lock );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Offset to the MAD array structure. */
	alloc_size -= sizeof( mad_array_t );
	p_mad_array = (mad_array_t*)(p_data + alloc_size);

	/* Offset to the array of MAD item structures. */
	alloc_size -= sizeof( mad_item_t ) * h_pool->grow_size;
	p_mad_items = (mad_item_t*)(p_data + alloc_size);

	/* Initialize the MAD array structure. */
	p_mad_array->h_pool = h_pool;
	p_mad_array->p_data = p_data;
	p_mad_array->sizeof_array = alloc_size;

	/* Initialize the MAD array object. */
	construct_al_obj( &p_mad_array->obj, AL_OBJ_TYPE_MAD_POOL );
	status = init_al_obj( &p_mad_array->obj, p_mad_array, TRUE,
		NULL, NULL, __free_mad_array );
	if( status != IB_SUCCESS )
	{
		cl_spinlock_release( &h_pool->obj.lock );
		__free_mad_array( &p_mad_array->obj );

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Register the MAD array on the existing pool protection domains. */
	for( p_key_item = cl_qlist_head( &h_pool->key_list );
		 p_key_item != cl_qlist_end( &h_pool->key_list );
		 p_key_item = cl_qlist_next( p_key_item ) )
	{
		p_pool_key = PARENT_STRUCT( p_key_item, al_pool_key_t, pool_item );
		ref_al_obj( &p_pool_key->obj );
		status = __reg_mad_array( p_pool_key, p_mad_array );
		deref_al_obj( &p_pool_key->obj );
		if( status != IB_SUCCESS )
			break;
	}

	if( status != IB_SUCCESS )
	{
		cl_spinlock_release( &h_pool->obj.lock );
		p_mad_array->obj.pfn_destroy( &p_mad_array->obj, NULL );
		return status;
	}

	/* The pool has been successfully grown.  Update the actual size. */
	h_pool->actual += h_pool->grow_size;

	/* Intialize the MAD stack item structures. */
	p_mad_item = p_mad_items;
	for( i = 0; i < h_pool->grow_size; i++ )
	{
		p_mad_item->p_mad_array = p_mad_array;

		p_mad_item->al_mad_element.grh_ds.vaddr = (uintn_t)p_data;
		p_mad_item->al_mad_element.grh_ds.length = MAD_BLOCK_GRH_SIZE;

		p_mad_item->al_mad_element.mad_ds.vaddr =
			(uintn_t)(p_data + sizeof( ib_grh_t ));
		p_mad_item->al_mad_element.mad_ds.length = MAD_BLOCK_SIZE;
		p_data += MAD_BLOCK_GRH_SIZE;
		p_mad_item++;
	}

	/* Return a MAD item to the caller if one was requested. */
	if( pp_mad_item != NULL )
	{
		*pp_mad_item = p_mad_items;
		p_mad_items++;
		i--;
	}

	/* Append the remaining MAD items to the existing stack. */
	cl_qlist_insert_array_tail( &h_pool->mad_stack,
		&p_mad_items->al_mad_element.list_item, i, sizeof( mad_item_t ) );

	/* Unlock the pool. */
	cl_spinlock_release( &h_pool->obj.lock );

	/* Attach the array object to the pool. */
	attach_al_obj( &h_pool->obj, &p_mad_array->obj );

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_mad_array->obj, E_REF_INIT );

	AL_EXIT(AL_DBG_MAD_POOL);
	return IB_SUCCESS;
}



/*
 * Free the MAD array structure.
 */
static void
__free_mad_array(
	IN				al_obj_t*					p_obj )
{
	mad_array_t*			p_mad_array;
	ib_pool_handle_t		h_pool;
	cl_list_item_t*			p_key_item;
	al_pool_key_t*			p_pool_key;
	cl_list_item_t*			p_reg_item;
	cl_list_item_t*			p_next_item;
	mad_reg_t*				p_reg;

	AL_ENTER(AL_DBG_MAD_POOL);

	CL_ASSERT( p_obj );
	p_mad_array = PARENT_STRUCT( p_obj, mad_array_t, obj );

	/* Destroy any registrations for this MAD array. */
	h_pool = p_mad_array->h_pool;
	cl_spinlock_acquire( &h_pool->obj.lock );

	/* Walk the pool key list. */
	p_key_item = cl_qlist_head( &h_pool->key_list );
	while( p_key_item != cl_qlist_end( &h_pool->key_list ) )
	{
		p_pool_key = PARENT_STRUCT( p_key_item, al_pool_key_t, pool_item );

		/* Walk the pool key registrations. */
		for( p_reg_item = cl_qlist_head( &p_pool_key->obj.obj_list );
			 p_reg_item != cl_qlist_end( &p_pool_key->obj.obj_list );
			 p_reg_item = p_next_item )
		{
			p_next_item = cl_qlist_next( p_reg_item );

			p_obj = PARENT_STRUCT( p_reg_item, al_obj_t, pool_item );
			p_reg = PARENT_STRUCT( p_obj, mad_reg_t, obj );

			/* Destroy registrations for this MAD array. */
			if( p_reg->p_mad_array == p_mad_array )
			{
				ref_al_obj( &p_reg->obj );
				p_reg->obj.pfn_destroy( &p_reg->obj, NULL );
			}
		}

		p_key_item = cl_qlist_next( p_key_item );
	}
	cl_spinlock_release( &h_pool->obj.lock );

	destroy_al_obj( &p_mad_array->obj );
	cl_free( p_mad_array->p_data );

	AL_EXIT(AL_DBG_MAD_POOL);
}



/*
 * Initialize a MAD send tracking structure to reference the pool from
 * whence it came.
 */
static cl_status_t
__mad_send_init(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	mad_send_t				*p_mad_send;

	p_mad_send = (mad_send_t*)p_object;
	p_mad_send->h_pool = context;
	*pp_pool_item = &p_mad_send->mad_send.pool_item;
	return CL_SUCCESS;
}



ib_mad_send_handle_t
get_mad_send(
	IN		const	al_mad_element_t			*p_mad_element )
{
	mad_item_t*				p_mad_item;
	ib_pool_handle_t		h_pool;
	cl_pool_item_t			*p_pool_item;
	ib_mad_send_handle_t	h_send;

	CL_ASSERT( p_mad_element );

	/* Get a handle to the pool. */
	p_mad_item = PARENT_STRUCT( p_mad_element, mad_item_t, al_mad_element );
	h_pool = p_mad_item->p_mad_array->h_pool;

	cl_spinlock_acquire( &h_pool->obj.lock );
	p_pool_item = cl_qpool_get( &h_pool->mad_send_pool );
	cl_spinlock_release( &h_pool->obj.lock );

	if( !p_pool_item )
		return NULL;

	ref_al_obj( &h_pool->obj );
	h_send = PARENT_STRUCT( p_pool_item, al_mad_send_t, pool_item );
	h_send->canceled = FALSE;
	h_send->p_send_mad = NULL;
	h_send->p_resp_mad = NULL;
	h_send->h_av = NULL;
	h_send->retry_cnt = 0;
	h_send->retry_time = 0;

	return h_send;
}



void
put_mad_send(
	IN				ib_mad_send_handle_t		h_mad_send )
{
	mad_send_t			*p_mad_send;

	p_mad_send = PARENT_STRUCT( h_mad_send, mad_send_t, mad_send );

	cl_spinlock_acquire( &p_mad_send->h_pool->obj.lock );
	cl_qpool_put( &p_mad_send->h_pool->mad_send_pool, &h_mad_send->pool_item );
	cl_spinlock_release( &p_mad_send->h_pool->obj.lock );
	deref_al_obj( &p_mad_send->h_pool->obj );
}



/*
 * Initialize a MAD RMPP tracking structure to reference the pool from
 * whence it came.
 */
static cl_status_t
__mad_rmpp_init(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	mad_rmpp_t				*p_mad_rmpp;

	p_mad_rmpp = (mad_rmpp_t*)p_object;
	p_mad_rmpp->h_pool = context;
	*pp_pool_item = &p_mad_rmpp->mad_rmpp.pool_item;
	return CL_SUCCESS;
}



al_mad_rmpp_t*
get_mad_rmpp(
	IN		const	al_mad_element_t			*p_mad_element )
{
	mad_item_t*				p_mad_item;
	ib_pool_handle_t		h_pool;
	cl_pool_item_t			*p_pool_item;

	CL_ASSERT( p_mad_element );

	/* Get a handle to the pool. */
	p_mad_item = PARENT_STRUCT( p_mad_element, mad_item_t, al_mad_element );
	h_pool = p_mad_item->p_mad_array->h_pool;

	cl_spinlock_acquire( &h_pool->obj.lock );
	p_pool_item = cl_qpool_get( &h_pool->mad_rmpp_pool );
	cl_spinlock_release( &h_pool->obj.lock );

	if( !p_pool_item )
		return NULL;

	ref_al_obj( &h_pool->obj );
	return PARENT_STRUCT( p_pool_item, al_mad_rmpp_t, pool_item );
}



void
put_mad_rmpp(
	IN				al_mad_rmpp_t*				h_mad_rmpp )
{
	mad_rmpp_t			*p_mad_rmpp;

	p_mad_rmpp = PARENT_STRUCT( h_mad_rmpp, mad_rmpp_t, mad_rmpp );

	cl_spinlock_acquire( &p_mad_rmpp->h_pool->obj.lock );
	cl_qpool_put( &p_mad_rmpp->h_pool->mad_rmpp_pool, &h_mad_rmpp->pool_item );
	cl_spinlock_release( &p_mad_rmpp->h_pool->obj.lock );
	deref_al_obj( &p_mad_rmpp->h_pool->obj );
}



ib_api_status_t
ib_get_mad(
	IN		const	ib_pool_key_t				pool_key,
	IN		const	size_t						buf_size,
		OUT			ib_mad_element_t			**pp_mad_element )
{
	al_pool_key_t*			p_pool_key;
	al_mad_element_t*		p_mad;
	ib_api_status_t			status;

	AL_ENTER(AL_DBG_MAD_POOL);

	if( AL_OBJ_INVALID_HANDLE( pool_key, AL_OBJ_TYPE_H_POOL_KEY ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}
	if( !buf_size || !pp_mad_element )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	p_pool_key = (al_pool_key_t*)pool_key;

	status = __get_mad_element( pool_key, &p_mad );
	if( status != IB_SUCCESS )
	{
		AL_EXIT(AL_DBG_MAD_POOL);
		return status;
	}

	/* Set the user accessible buffer. */
	if( buf_size <= MAD_BLOCK_SIZE )
	{
		/* Use the send buffer for 256 byte MADs. */
		p_mad->element.p_mad_buf = (ib_mad_t*)(uintn_t)p_mad->mad_ds.vaddr;
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
			AL_EXIT(AL_DBG_MAD_POOL);
			return IB_INSUFFICIENT_MEMORY;
		}
		p_mad->element.p_mad_buf = p_mad->p_al_mad_buf;
	}
	p_mad->element.size = (uint32_t)buf_size;

	/* Track the MAD element with the requesting AL instance. */
	al_insert_mad( p_pool_key->h_al, p_mad );

	ref_al_obj( &p_pool_key->obj );
	cl_atomic_inc( &p_pool_key->mad_cnt );

	/* Return the MAD element to the client. */
	*pp_mad_element = &p_mad->element;

	AL_EXIT(AL_DBG_MAD_POOL);
	return IB_SUCCESS;
}



ib_api_status_t
ib_put_mad(
	IN		const	ib_mad_element_t*			p_mad_element_list )
{
	al_mad_element_t*		p_mad;

	if( !p_mad_element_list )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
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
		if( p_mad->h_al )
		{
			/* Remove the MAD element from the owning AL instance. */
			al_remove_mad( p_mad );

			/* Return the MAD element to the pool. */
			cl_atomic_dec( &p_mad->pool_key->mad_cnt );
			deref_al_obj( &p_mad->pool_key->obj );
			__put_mad_element( p_mad );
		}
		else
		{
			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("MAD has already been returned to MAD pool.\n") );
		}
	}

	return IB_SUCCESS;
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

