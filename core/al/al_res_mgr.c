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

#include <complib/cl_memory.h>

#include "al_mgr.h"
#include "ib_common.h"
#include "al_res_mgr.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_res_mgr.tmh"
#endif


#define AL_MR_POOL_SIZE			(((4096 / sizeof( ib_mr_t )) == 0) ? 32 : (4096 / sizeof( ib_mr_t )))
#define AL_AV_POOL_SIZE			(((4096 / sizeof( ib_av_t )) == 0) ? 32 : (4096 / sizeof( ib_av_t )))
#ifdef CL_KERNEL
#define AL_FMR_POOL_SIZE		(((4096 / sizeof( mlnx_fmr_t )) == 0) ? 32 : (4096 / sizeof( mlnx_fmr_t )))
#endif

al_res_mgr_t			*gp_res_mgr;


void
free_res_mgr(
	IN				al_obj_t					*p_obj );



ib_api_status_t
create_res_mgr(
	IN				al_obj_t					*p_parent_obj )
{
	ib_api_status_t				status;
	cl_status_t					cl_status;

	gp_res_mgr = (al_res_mgr_t*)cl_zalloc( sizeof( al_res_mgr_t ) );
	if( !gp_res_mgr )
	{
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the resource manager. */
	cl_qpool_construct( &gp_res_mgr->av_pool );
	cl_qpool_construct( &gp_res_mgr->mr_pool );
#ifdef CL_KERNEL
	cl_qpool_construct( &gp_res_mgr->fmr_pool );
#endif

	construct_al_obj( &gp_res_mgr->obj, AL_OBJ_TYPE_RES_MGR );
	status = init_al_obj( &gp_res_mgr->obj, gp_res_mgr, TRUE,
		NULL, NULL, free_res_mgr );
	if( status != IB_SUCCESS )
	{
		free_res_mgr( &gp_res_mgr->obj );
		return status;
	}

	status = attach_al_obj( p_parent_obj, &gp_res_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_res_mgr->obj.pfn_destroy( &gp_res_mgr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Initialize the pool of address vectors. */
	cl_status = cl_qpool_init( &gp_res_mgr->av_pool,
		AL_AV_POOL_SIZE, 0, AL_AV_POOL_SIZE, sizeof( ib_av_t ),
		av_ctor, av_dtor, gp_res_mgr );
	if( cl_status != CL_SUCCESS )
	{
		gp_res_mgr->obj.pfn_destroy( &gp_res_mgr->obj, NULL );
		return ib_convert_cl_status( cl_status );
	}

	/* Initialize the pool of memory regions. */
	cl_status = cl_qpool_init( &gp_res_mgr->mr_pool,
		AL_MR_POOL_SIZE, 0, AL_MR_POOL_SIZE, sizeof( ib_mr_t ),
		mr_ctor, mr_dtor, gp_res_mgr );
	if( cl_status != CL_SUCCESS )
	{
		gp_res_mgr->obj.pfn_destroy( &gp_res_mgr->obj, NULL );
		return ib_convert_cl_status( cl_status );
	}

#ifdef CL_KERNEL
	/* Initialize the pool of fast memory regions. */
	cl_status = cl_qpool_init( &gp_res_mgr->fmr_pool,
		AL_FMR_POOL_SIZE, 0, AL_FMR_POOL_SIZE, sizeof(mlnx_fmr_t),
		mlnx_fmr_ctor, mlnx_fmr_dtor, gp_res_mgr );
	if( cl_status != CL_SUCCESS )
	{
		gp_res_mgr->obj.pfn_destroy( &gp_res_mgr->obj, NULL );
		return ib_convert_cl_status( cl_status );
	}
#endif

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &gp_res_mgr->obj, E_REF_INIT );

	return IB_SUCCESS;
}



/*
 * Destroy the resource manager.
 */
void
free_res_mgr(
	IN				al_obj_t					*p_obj )
{
	CL_ASSERT( p_obj == &gp_res_mgr->obj );

	cl_qpool_destroy( &gp_res_mgr->av_pool );
	cl_qpool_destroy( &gp_res_mgr->mr_pool );
#ifdef CL_KERNEL
	cl_qpool_destroy( &gp_res_mgr->fmr_pool );
#endif

	destroy_al_obj( p_obj );
	cl_free ( gp_res_mgr );
	gp_res_mgr = NULL;
}



/*
 * Get a memory region structure to track registration requests.
 */
ib_mr_handle_t
alloc_mr()
{
	al_obj_t				*p_obj;
	cl_pool_item_t			*p_pool_item;

	cl_spinlock_acquire( &gp_res_mgr->obj.lock );
	p_pool_item = cl_qpool_get( &gp_res_mgr->mr_pool );
	cl_spinlock_release( &gp_res_mgr->obj.lock );

	if( !p_pool_item )
		return NULL;

	ref_al_obj( &gp_res_mgr->obj );
	p_obj = PARENT_STRUCT( p_pool_item, al_obj_t, pool_item );

	/*
	 * Hold an extra reference on the object until creation is complete.
	 * This prevents a client's destruction of the object during asynchronous
	 * event callback processing from deallocating the object before the
	 * creation is complete.
	 */
	ref_al_obj( p_obj );

	return PARENT_STRUCT( p_obj, ib_mr_t, obj );
}



/*
 * Return a memory region structure to the available pool.
 */
void
put_mr(
	IN				ib_mr_handle_t				h_mr )
{
	cl_spinlock_acquire( &gp_res_mgr->obj.lock );
	cl_qpool_put( &gp_res_mgr->mr_pool, &h_mr->obj.pool_item );
	cl_spinlock_release( &gp_res_mgr->obj.lock );
	deref_al_obj( &gp_res_mgr->obj );
}


#ifdef CL_KERNEL
/*
 * Get a fast memory region structure to track registration requests.
 */
mlnx_fmr_handle_t
alloc_mlnx_fmr()
{
	al_obj_t				*p_obj;
	cl_pool_item_t			*p_pool_item;

	cl_spinlock_acquire( &gp_res_mgr->obj.lock );
	p_pool_item = cl_qpool_get( &gp_res_mgr->fmr_pool );
	cl_spinlock_release( &gp_res_mgr->obj.lock );

	if( !p_pool_item )
		return NULL;

	ref_al_obj( &gp_res_mgr->obj );
	p_obj = PARENT_STRUCT( p_pool_item, al_obj_t, pool_item );

	/*
	 * Hold an extra reference on the object until creation is complete.
	 * This prevents a client's destruction of the object during asynchronous
	 * event callback processing from deallocating the object before the
	 * creation is complete.
	 */
	ref_al_obj( p_obj );

	return PARENT_STRUCT( p_obj, mlnx_fmr_t, obj );
}



/*
 * Return a memory region structure to the available pool.
 */
void
put_mlnx_fmr(
	IN				mlnx_fmr_handle_t			h_fmr )
{
	cl_spinlock_acquire( &gp_res_mgr->obj.lock );
	cl_qpool_put( &gp_res_mgr->fmr_pool, &h_fmr->obj.pool_item );
	cl_spinlock_release( &gp_res_mgr->obj.lock );
	deref_al_obj( &gp_res_mgr->obj );
}
#endif


/*
 * Get an address vector from the available pool.
 */
ib_av_handle_t
alloc_av()
{
	al_obj_t				*p_obj;
	cl_pool_item_t			*p_pool_item;

	cl_spinlock_acquire( &gp_res_mgr->obj.lock );
	p_pool_item = cl_qpool_get( &gp_res_mgr->av_pool );
#if DBG
	insert_pool_trace(&g_av_trace, p_pool_item, POOL_GET, 0);
#endif
	cl_spinlock_release( &gp_res_mgr->obj.lock );

	if( !p_pool_item )
		return NULL;

	ref_al_obj( &gp_res_mgr->obj );
	p_obj = PARENT_STRUCT( p_pool_item, al_obj_t, pool_item );

	/*
	 * Hold an extra reference on the object until creation is complete.
	 * This prevents a client's destruction of the object during asynchronous
	 * event callback processing from deallocating the object before the
	 * creation is complete.
	 */
	ref_al_obj( p_obj );
	return PARENT_STRUCT( p_obj, ib_av_t, obj );
}



/*
 * Return an address vector to the available pool.
 */
void
put_av(
	IN				ib_av_handle_t				h_av )
{
	cl_spinlock_acquire( &gp_res_mgr->obj.lock );
	cl_qpool_put( &gp_res_mgr->av_pool, &h_av->obj.pool_item );
#if DBG
	insert_pool_trace(&g_av_trace, &h_av->obj.pool_item, POOL_PUT, 0);
#endif
	cl_spinlock_release( &gp_res_mgr->obj.lock );
	deref_al_obj( &gp_res_mgr->obj );
}
