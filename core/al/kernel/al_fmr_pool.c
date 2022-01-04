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




#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_fmr_pool.tmh"
#endif

#include "al_fmr_pool.h"
#include "al_mr.h"
#include "al_pd.h"

#define hash_mix(a, b, c) \
	{ \
		a -= b; a -= c; a ^= (c>>13); \
		b -= c; b -= a; b ^= (a<<8); \
		c -= a; c -= b; c ^= (b>>13); \
		a -= b; a -= c; a ^= (c>>12);  \
		b -= c; b -= a; b ^= (a<<16); \
		c -= a; c -= b; c ^= (b>>5); \
		a -= b; a -= c; a ^= (c>>3);  \
		b -= c; b -= a; b ^= (a<<10); \
		c -= a; c -= b; c ^= (b>>15); \
}

static inline uint32_t hash_2words(uint32_t a, uint32_t b, uint32_t c)
{
	a += 0x9e3779b9;
	b += 0x9e3779b9;
	hash_mix(a, b, c);
	return c;
}

enum {
	IB_FMR_MAX_REMAPS = 32,

	IB_FMR_HASH_BITS  = 8,
	IB_FMR_HASH_SIZE  = 1 << IB_FMR_HASH_BITS,
	IB_FMR_HASH_MASK  = IB_FMR_HASH_SIZE - 1
};


static inline uint32_t __fmr_hash(uint64_t first_page)
{
	return hash_2words((uint32_t) first_page, (uint32_t) (first_page >> 32), 0) &
		(IB_FMR_HASH_SIZE - 1);
}

/* Caller must hold pool_lock */
static inline mlnx_fmr_pool_element_t *__fmr_cache_lookup(
	mlnx_fmr_pool_t *p_pool,
	const	uint64_t* const page_list,
	int  page_list_len,
	uint64_t  io_virtual_address)
{
	cl_qlist_t *bucket;
	cl_list_item_t		*p_list_item;
	mlnx_fmr_pool_element_t	*p_fmr_el;

	if (!p_pool->cache_bucket)
		return NULL;

	bucket = p_pool->cache_bucket + __fmr_hash(*page_list);

	for( p_list_item = cl_qlist_head( bucket );
		 p_list_item != cl_qlist_end( bucket);
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		 p_fmr_el = PARENT_STRUCT( p_list_item, mlnx_fmr_pool_element_t, cache_node );
		 if (io_virtual_address == p_fmr_el->io_virtual_address &&
			 page_list_len == p_fmr_el->page_list_len &&
			 !memcmp(page_list, p_fmr_el->page_list, page_list_len * sizeof *page_list))
			 return p_fmr_el;
	}

	return NULL;
}


static void 
__fmr_pool_batch_release(mlnx_fmr_pool_t *p_pool)
{
	ib_api_status_t			status;
	mlnx_fmr_pool_element_t	*p_fmr_el;
	mlnx_fmr_handle_t			h_fmr = NULL;
	cl_qlist_t					unmap_list;
	cl_list_item_t				*p_list_item;
	cl_qlist_t *bucket;

	cl_qlist_init(&unmap_list);
	
	cl_spinlock_acquire(&p_pool->pool_lock);

	for( p_list_item = cl_qlist_head( &p_pool->dirty_list );
		 p_list_item != cl_qlist_end( &p_pool->dirty_list);
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_fmr_el = PARENT_STRUCT( p_list_item, mlnx_fmr_pool_element_t, list_item );
		if (p_fmr_el->in_cash)
		{
			p_fmr_el->in_cash = FALSE;
			bucket = p_pool->cache_bucket + __fmr_hash(p_fmr_el->page_list[0]);
			cl_qlist_remove_item( bucket, &p_fmr_el->cache_node );
		}
		p_fmr_el->remap_count = 0;
		p_fmr_el->h_fmr->p_next  = h_fmr;
		h_fmr = p_fmr_el->h_fmr;
		if (p_fmr_el->ref_count !=0) 
		{
			AL_PRINT(TRACE_LEVEL_WARNING, AL_DBG_FMR_POOL, ("Unmapping FMR 0x%p with ref count %d",
				p_fmr_el, p_fmr_el->ref_count));
		}
	}

	cl_qlist_insert_list_head(&unmap_list, &p_pool->dirty_list );
	cl_qlist_init(&p_pool->dirty_list);
	p_pool->dirty_len = 0;

	cl_spinlock_release( &p_pool->pool_lock );

	if (cl_is_qlist_empty(&unmap_list)) {
		return;
	}

	status = mlnx_unmap_fmr(h_fmr);
	if (status != IB_SUCCESS)
			AL_PRINT(TRACE_LEVEL_WARNING, AL_DBG_FMR_POOL, ("mlnx_unmap_fmr returned %s", ib_get_err_str(status)));


	cl_spinlock_acquire( &p_pool->pool_lock );
	cl_qlist_insert_list_head(&p_pool->free_list,&unmap_list);
	cl_spinlock_release( &p_pool->pool_lock );
}



static int 
__fmr_cleanup_thread(void * p_pool_ptr)
{
	mlnx_fmr_pool_t *p_pool = p_pool_ptr;
	atomic32_t flush_req;
	int forever = 1;

	do {
		flush_req = 0;
		if (p_pool->flush_req || p_pool->dirty_len >= p_pool->dirty_watermark)
		{
			__fmr_pool_batch_release(p_pool);

			if (p_pool->flush_req) 
			{
				cl_event_signal(&p_pool->flush_done_event);
				flush_req = cl_atomic_dec( &p_pool->flush_req );
			}
		
			if (p_pool->flush_function)
				p_pool->flush_function( (mlnx_fmr_pool_handle_t)p_pool, p_pool->flush_arg);
		}

		if (!flush_req)
		{
			if (p_pool->should_stop)
				break;
			cl_event_wait_on(&p_pool->do_flush_event, EVENT_NO_TIMEOUT, TRUE);
		}
	} while (forever);

	return 0;
}

/*
 * Destroying  the pool.
 */
static void
__destroying_fmr_pool(
	IN				al_obj_t*					p_obj )
{
	mlnx_fmr_pool_t*		p_pool;

	CL_ASSERT( p_obj );
	p_pool = PARENT_STRUCT( p_obj, mlnx_fmr_pool_t, obj );
	AL_PRINT(TRACE_LEVEL_ERROR, AL_DBG_FMR_POOL, ("pool %p\n", p_pool));

	// notify cleaning thread to exit
	cl_atomic_inc( &p_pool->should_stop );
	cl_event_signal(&p_pool->do_flush_event);
	cl_thread_destroy(&p_pool->thread);
}

/*
 * Cleanup the pool.
 */
static void
__cleanup_fmr_pool(
	IN				al_obj_t*					p_obj )
{
	int						i=0;
	ib_api_status_t			status = IB_SUCCESS;
	mlnx_fmr_pool_t*		p_pool;
	mlnx_fmr_pool_element_t	*p_fmr_el;
	cl_list_item_t				*p_list_item;
	cl_qlist_t *bucket;

	CL_ASSERT( p_obj );
	p_pool = PARENT_STRUCT( p_obj, mlnx_fmr_pool_t, obj );
	AL_PRINT(TRACE_LEVEL_ERROR, AL_DBG_FMR_POOL, ("pool %p\n", p_pool));

	// cleanup the dirty list stuff
	__fmr_pool_batch_release(p_pool);

	cl_spinlock_acquire(&p_pool->pool_lock);

	// merge the rest with free list
	for( p_list_item = cl_qlist_head( &p_pool->rest_list );
		 p_list_item != cl_qlist_end( &p_pool->rest_list );
		 p_list_item = cl_qlist_head( &p_pool->rest_list ) )
	{
		p_fmr_el = PARENT_STRUCT( p_list_item, mlnx_fmr_pool_element_t, list_item );
		if (p_fmr_el->in_cash)
		{
			p_fmr_el->in_cash = FALSE;
			bucket = p_pool->cache_bucket + __fmr_hash(p_fmr_el->page_list[0]);
			cl_qlist_remove_item( bucket, &p_fmr_el->cache_node );
		}
		cl_qlist_remove_item(&p_pool->rest_list, p_list_item);
		cl_qlist_insert_tail(&p_pool->free_list, &p_fmr_el->list_item);
		p_fmr_el->p_cur_list = &p_pool->free_list;
	}

	// cleanup the free list
	for( p_list_item = cl_qlist_head( &p_pool->free_list );
		 p_list_item != cl_qlist_end( &p_pool->free_list );
		 p_list_item = cl_qlist_head( &p_pool->free_list ) )
	{
		p_fmr_el = PARENT_STRUCT( p_list_item, mlnx_fmr_pool_element_t, list_item);
		cl_spinlock_release( &p_pool->pool_lock );
		if (p_fmr_el->remap_count)
		{
			p_fmr_el->h_fmr->p_next  = NULL;
			status = mlnx_unmap_fmr(p_fmr_el->h_fmr);
			if (status != IB_SUCCESS)
				AL_PRINT(TRACE_LEVEL_WARNING, AL_DBG_FMR_POOL, ("mlnx_unmap_fmr returned %s\n", ib_get_err_str(status)));

		}
		status = mlnx_destroy_fmr(p_fmr_el->h_fmr);
		if (status != IB_SUCCESS)
			AL_PRINT(TRACE_LEVEL_WARNING, AL_DBG_FMR_POOL, ("mlnx_destroy_fmr returned %s\n", ib_get_err_str(status)));

		cl_spinlock_acquire(&p_pool->pool_lock);
		cl_qlist_remove_item(&p_pool->free_list, p_list_item);
		cl_free(p_fmr_el);
		++i;
	}

	cl_spinlock_release( &p_pool->pool_lock );

	if (i < p_pool->pool_size)
		AL_PRINT(TRACE_LEVEL_ERROR, AL_DBG_FMR_POOL, ("pool still has %d regions registered\n",
			p_pool->pool_size - i));
}


/*
 * Free the pool.
 */
static void
__free_fmr_pool(
	IN				al_obj_t*					p_obj )
{
	mlnx_fmr_pool_t*		p_pool;

	CL_ASSERT( p_obj );
	p_pool = PARENT_STRUCT( p_obj, mlnx_fmr_pool_t, obj );

	cl_spinlock_destroy(&p_pool->pool_lock);
	destroy_al_obj( &p_pool->obj );
	if (p_pool->cache_bucket)
		cl_free( p_pool->cache_bucket );
	cl_free( p_pool );
	AL_PRINT(TRACE_LEVEL_ERROR, AL_DBG_FMR_POOL, ("__free_pool: pool %p\n", p_pool));
}



ib_api_status_t
mlnx_create_fmr_pool(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	mlnx_fmr_pool_create_t		*p_fmr_pool_attr,
	OUT		mlnx_fmr_pool_handle_t* const			ph_pool )
{
	ib_api_status_t		status = IB_SUCCESS;
	mlnx_fmr_pool_t		*p_pool;
	int					i;
	int					max_remaps;
	cl_status_t			cl_status;
	mlnx_fmr_pool_element_t *p_fmr_el;


	AL_ENTER( AL_DBG_FMR_POOL );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		status =  IB_INVALID_AL_HANDLE;
		goto end;
	}

	if( !ph_pool )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		status = IB_INVALID_PARAMETER;
		goto end;
	}

	if( !p_fmr_pool_attr || !p_fmr_pool_attr->dirty_watermark)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		status = IB_INVALID_PARAMETER;
		goto end;
	}

	if (!h_pd->obj.p_ci_ca || !h_pd->obj.p_ci_ca->p_pnp_attr) 
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_STATE\n") );
		status = IB_INVALID_STATE;
		goto end;
	}
	
	// check whether the device support FMR
	if (!h_pd->obj.p_ci_ca->verbs.alloc_mlnx_fmr|| !h_pd->obj.p_ci_ca->verbs.dealloc_mlnx_fmr  ||
		!h_pd->obj.p_ci_ca->verbs.map_phys_mlnx_fmr || !h_pd->obj.p_ci_ca->verbs.unmap_mlnx_fmr) {
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Device does not support fast memory regions"));
		status = IB_UNSUPPORTED;
		goto end;
	}

	if (!h_pd->obj.p_ci_ca->p_pnp_attr->max_map_per_fmr)
	{
		max_remaps = IB_FMR_MAX_REMAPS;
	}
	else
	{
		max_remaps = h_pd->obj.p_ci_ca->p_pnp_attr->max_map_per_fmr;
	}

	// allocate pool object
	p_pool = cl_zalloc( sizeof( mlnx_fmr_pool_t ) );
	if( !p_pool )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Couldn't allocate pool struct"));
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_pool_obj;
	}

	// construct  pool objects
	cl_spinlock_construct( &p_pool->pool_lock);
	cl_thread_construct(&p_pool->thread);
	cl_event_construct(&p_pool->do_flush_event);
	cl_event_construct(&p_pool->flush_done_event);


	// init pool objects
	p_pool->pool_size       = 0;
	p_pool->max_pages       = p_fmr_pool_attr->max_pages_per_fmr;
	p_pool->max_remaps      = max_remaps;
	p_pool->dirty_watermark = p_fmr_pool_attr->dirty_watermark;
	p_pool->dirty_len       = 0;
	p_pool->cache_bucket   = NULL;
	p_pool->flush_function = p_fmr_pool_attr->flush_function;
	p_pool->flush_arg      = p_fmr_pool_attr->flush_arg;
	cl_qlist_init(&p_pool->dirty_list);
	cl_qlist_init(&p_pool->free_list);
	cl_qlist_init(&p_pool->rest_list);

	if (p_fmr_pool_attr->cache) {
		p_pool->cache_bucket =
			cl_zalloc(IB_FMR_HASH_SIZE * sizeof *p_pool->cache_bucket);
		if (!p_pool->cache_bucket) {
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed to allocate cache in pool"));
			status = IB_INSUFFICIENT_MEMORY;
			goto err_alloc_cache;
		}

		for (i = 0; i < IB_FMR_HASH_SIZE; ++i)
			cl_qlist_init(p_pool->cache_bucket + i);
	}

	cl_status = cl_spinlock_init( &p_pool->pool_lock );
	if( cl_status != CL_SUCCESS ) 
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed cl_spinlock_init"));
		status = IB_ERROR;
		goto err_pool_init;
	}

	cl_event_init(&p_pool->do_flush_event,FALSE);
	if( cl_status != CL_SUCCESS ) 
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed cl_event_init"));
		status = IB_ERROR;
		goto err_pool_init;
	}

	cl_event_init(&p_pool->flush_done_event,FALSE);
	if( cl_status != CL_SUCCESS ) 
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed cl_event_init"));
		status = IB_ERROR;
		goto err_pool_init;
	}

	cl_thread_init(&p_pool->thread ,__fmr_cleanup_thread,p_pool,"fmr_cleanup");
	if( cl_status != CL_SUCCESS ) 
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed cl_thread_init"));
		status = IB_ERROR;
		goto err_pool_init;
	}

	{
		mlnx_fmr_create_t			fmr_attr;
		
		fmr_attr.max_pages = p_fmr_pool_attr->max_pages_per_fmr,
		fmr_attr.max_maps = p_pool->max_remaps,
		fmr_attr.page_size = p_fmr_pool_attr->page_size;
		fmr_attr.access_ctrl = p_fmr_pool_attr->access_ctrl;


		for (i = 0; i < p_fmr_pool_attr->pool_size; ++i)
		{
			p_fmr_el = cl_zalloc(sizeof (mlnx_fmr_pool_element_t) + p_fmr_pool_attr->max_pages_per_fmr * sizeof (uint64_t));
			if (!p_fmr_el)
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, (" failed to allocate struct for FMR %d \n",i));
				status = IB_INSUFFICIENT_MEMORY;
				goto err_alloc_cache_el;
			}

			p_fmr_el->h_pool = (mlnx_fmr_pool_handle_t)p_pool;
			p_fmr_el->remap_count = 0;
			p_fmr_el->ref_count = 0;

			status = mlnx_create_fmr(h_pd, &fmr_attr,&p_fmr_el->h_fmr);
			if (status != IB_SUCCESS)
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("mlnx_create_fmr failed for FMR %d with status %s.\n",i,ib_get_err_str(status)));
				cl_free(p_fmr_el);
				goto err_alloc_cache_el;
			}

			cl_qlist_insert_tail(&p_pool->free_list, &p_fmr_el->list_item);
			p_fmr_el->p_cur_list = &p_pool->free_list;
			++p_pool->pool_size;
		}

	}

	/* Do IBAL stuff for creating and iniitializing the object */
	construct_al_obj( &p_pool->obj, AL_OBJ_TYPE_H_FMR_POOL);

	status = init_al_obj( &p_pool->obj, p_pool, FALSE, __destroying_fmr_pool, __cleanup_fmr_pool, __free_fmr_pool );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		goto err_init_al_obj;
	}

	/* Attach the pool to the AL object. */
	status = attach_al_obj( &h_pd->obj, &p_pool->obj );
	if( status != IB_SUCCESS )
	{
		ref_al_obj( &p_pool->obj );
		p_pool->obj.pfn_destroy( &p_pool->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		goto end;
	}


	/* Release the reference taken in init_al_obj */
	deref_ctx_al_obj( &p_pool->obj, E_REF_INIT );

	*ph_pool = p_pool;
	status = IB_SUCCESS;
	goto end;

err_init_al_obj:
	destroy_al_obj( &p_pool->obj );

err_alloc_cache_el:
	__destroying_fmr_pool( &p_pool->obj );
	__cleanup_fmr_pool( &p_pool->obj );

err_pool_init:
	if (p_pool->cache_bucket)
		cl_free( p_pool->cache_bucket );

err_alloc_cache:	
	cl_free( p_pool );

err_alloc_pool_obj:
end:
	AL_EXIT( AL_DBG_FMR_POOL );
	return status;
}

/**
 * ib_destroy_fmr_pool - Free FMR pool
 * @pool:FMR pool to free
 *
 * Destroy an FMR pool and free all associated resources.
 */
ib_api_status_t
mlnx_destroy_fmr_pool(
	IN		const	mlnx_fmr_pool_handle_t	 h_pool)
{
	mlnx_fmr_pool_t			*p_pool = (mlnx_fmr_pool_t*)h_pool;

	AL_ENTER( AL_DBG_FMR_POOL );

	if( AL_OBJ_INVALID_HANDLE( h_pool, AL_OBJ_TYPE_H_FMR_POOL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	ref_al_obj( &p_pool->obj );
	p_pool->obj.pfn_destroy( &p_pool->obj, NULL );

	AL_EXIT( AL_DBG_FMR_POOL );
	return IB_SUCCESS;
}



ib_api_status_t
mlnx_flush_fmr_pool(mlnx_fmr_pool_handle_t	h_pool)
{

	ib_api_status_t			status = IB_SUCCESS;
	mlnx_fmr_pool_t			*p_pool = (mlnx_fmr_pool_t*)h_pool;

	if( AL_OBJ_INVALID_HANDLE( h_pool, AL_OBJ_TYPE_H_FMR_POOL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	ref_al_obj( &p_pool->obj );

	cl_atomic_inc( &p_pool->flush_req );
	cl_event_signal(&p_pool->do_flush_event);
	if (cl_event_wait_on(&p_pool->flush_done_event, EVENT_NO_TIMEOUT, TRUE))
		status = IB_ERROR;

	deref_al_obj( &p_pool->obj );

	return status;
}

ib_api_status_t
mlnx_map_phys_fmr_pool(
	IN		const	mlnx_fmr_pool_handle_t		h_pool ,
	IN		const	uint64_t* const				page_list,
	IN		const	int							list_len,
	IN	OUT			uint64_t* const				p_vaddr,
	OUT		net32_t* const					p_lkey,
	OUT		net32_t* const					p_rkey,
	OUT				mlnx_fmr_pool_el_t		*pp_fmr_el)
{

	ib_api_status_t			status = IB_SUCCESS;
	mlnx_fmr_pool_t			*p_pool = (mlnx_fmr_pool_t*)h_pool;
	mlnx_fmr_pool_element_t	*p_fmr_el;
	cl_qlist_t *bucket;

	if( AL_OBJ_INVALID_HANDLE( h_pool, AL_OBJ_TYPE_H_FMR_POOL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	if (list_len < 1 || list_len > p_pool->max_pages)
		return IB_INVALID_PARAMETER;

	ref_al_obj( &p_pool->obj );

	cl_spinlock_acquire(&p_pool->pool_lock);

	p_fmr_el = __fmr_cache_lookup( p_pool, page_list, list_len, *p_vaddr );
	if (p_fmr_el) {
		/* found in cache */
		++p_fmr_el->ref_count;
		if (p_fmr_el->ref_count == 1) {
			cl_qlist_remove_item( p_fmr_el->p_cur_list, &p_fmr_el->list_item );
			cl_qlist_insert_tail(&p_pool->rest_list, &p_fmr_el->list_item);
			p_fmr_el->p_cur_list = &p_pool->rest_list;
		}

		cl_spinlock_release(&p_pool->pool_lock);
		goto end;
	}
	
	if (cl_is_qlist_empty(&p_pool->free_list)) {
		cl_spinlock_release(&p_pool->pool_lock);
		status = IB_RESOURCE_BUSY;
		goto exit;
	}

	p_fmr_el = PARENT_STRUCT(cl_qlist_remove_head(&p_pool->free_list),mlnx_fmr_pool_element_t,list_item);
	if (p_fmr_el->in_cash)
	{
		p_fmr_el->in_cash = FALSE;
		bucket = p_pool->cache_bucket + __fmr_hash(p_fmr_el->page_list[0]);
		cl_qlist_remove_item( bucket, &p_fmr_el->cache_node );
	}
	cl_spinlock_release(&p_pool->pool_lock);

	status = mlnx_map_phys_fmr(p_fmr_el->h_fmr, page_list,
		list_len, p_vaddr, p_lkey, p_rkey);

	if (status != IB_SUCCESS) {
		cl_spinlock_acquire(&p_pool->pool_lock);
		cl_qlist_insert_tail(&p_pool->free_list, &p_fmr_el->list_item);
		p_fmr_el->p_cur_list = &p_pool->free_list;
		cl_spinlock_release(&p_pool->pool_lock);
		goto exit;
	}

	++p_fmr_el->remap_count;
	p_fmr_el->ref_count = 1;
	p_fmr_el->lkey = *p_lkey;
	p_fmr_el->rkey = *p_rkey;
	p_fmr_el->io_virtual_address = *p_vaddr;
	cl_spinlock_acquire(&p_pool->pool_lock);
	cl_qlist_insert_tail(&p_pool->rest_list, &p_fmr_el->list_item);
	p_fmr_el->p_cur_list = &p_pool->rest_list;
	cl_spinlock_release(&p_pool->pool_lock);

	if (p_pool->cache_bucket) {
		p_fmr_el->io_virtual_address = *p_vaddr;
		p_fmr_el->page_list_len      = list_len;
		memcpy(p_fmr_el->page_list, page_list, list_len * sizeof(*page_list));

		cl_spinlock_acquire(&p_pool->pool_lock);
		bucket = p_pool->cache_bucket + __fmr_hash(p_fmr_el->page_list[0]);
		cl_qlist_insert_head( bucket, &p_fmr_el->cache_node );
		p_fmr_el->in_cash = TRUE;
		cl_spinlock_release(&p_pool->pool_lock);
	}

end:
	*pp_fmr_el = (mlnx_fmr_pool_el_t)p_fmr_el;
	*p_lkey = p_fmr_el->lkey;
	*p_rkey = p_fmr_el->rkey;
	*p_vaddr = p_fmr_el->io_virtual_address;
	
exit:
	deref_al_obj( &p_pool->obj );
	return status;
}



ib_api_status_t
mlnx_unmap_fmr_pool(
	IN		mlnx_fmr_pool_el_t			p_fmr_el )
{
	mlnx_fmr_pool_t			*p_pool;

	p_pool = (mlnx_fmr_pool_t*)p_fmr_el->h_pool;

	if( AL_OBJ_INVALID_HANDLE( (mlnx_fmr_pool_handle_t)p_pool, AL_OBJ_TYPE_H_FMR_POOL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	ref_al_obj( &p_pool->obj );
		
	cl_spinlock_acquire(&p_pool->pool_lock);

	--p_fmr_el->ref_count;
	if (!p_fmr_el->ref_count) 
	{
		if (p_fmr_el->p_cur_list == &p_pool->rest_list)
			cl_qlist_remove_item( p_fmr_el->p_cur_list, &p_fmr_el->list_item );

		if (p_fmr_el->remap_count < p_pool->max_remaps) 
		{
			cl_qlist_insert_tail(&p_pool->free_list,&p_fmr_el->list_item);
			p_fmr_el->p_cur_list = &p_pool->free_list;
		}
		else
		{
			cl_qlist_insert_tail(&p_pool->dirty_list, &p_fmr_el->list_item);
			p_fmr_el->p_cur_list = &p_pool->dirty_list;
			++p_pool->dirty_len;
			cl_event_signal(&p_pool->do_flush_event);
		}
	}

	if (p_fmr_el->ref_count < 0)
	{
		AL_PRINT(TRACE_LEVEL_WARNING, AL_DBG_FMR_POOL, ("FMR %p has ref count %d < 0\n",p_fmr_el, p_fmr_el->ref_count));
	}
	cl_spinlock_release( &p_pool->pool_lock );

	deref_al_obj( &p_pool->obj );
	return IB_SUCCESS;
}
