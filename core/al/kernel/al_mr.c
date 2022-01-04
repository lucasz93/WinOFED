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

#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_mr.tmh"
#endif
#include "al_mr.h"
#include "al_pd.h"
#include "al_res_mgr.h"
#include "al_verbs.h"

#include "ib_common.h"


static void
__cleanup_mlnx_fmr(
	IN				struct _al_obj				*p_obj );

static void
__return_mlnx_fmr(
	IN				al_obj_t					*p_obj );


static al_shmid_t*
__create_shmid(
	IN		const	int							shmid );

static void
__free_shmid(
	IN				struct _al_obj				*p_obj );


cl_status_t
mlnx_fmr_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	ib_api_status_t			status;
	mlnx_fmr_handle_t			h_fmr;

	UNUSED_PARAM( context );

	h_fmr = (mlnx_fmr_handle_t)p_object;
	cl_memclr( h_fmr, sizeof(mlnx_fmr_t) );

	construct_al_obj( &h_fmr->obj, AL_OBJ_TYPE_H_FMR );
	status = init_al_obj( &h_fmr->obj, NULL, FALSE, NULL,
		__cleanup_mlnx_fmr, __return_mlnx_fmr );
	if( status != IB_SUCCESS )
	{
		return CL_ERROR;
	}

	*pp_pool_item = &((mlnx_fmr_handle_t)p_object)->obj.pool_item;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_fmr->obj, E_REF_INIT );

	return CL_SUCCESS;
}



void
mlnx_fmr_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void*						context )
{
	al_obj_t				*p_obj;

	UNUSED_PARAM( context );

	p_obj = PARENT_STRUCT( p_pool_item, al_obj_t, pool_item );

	/*
	 * The FMR is being totally destroyed.  Modify the free_cb to destroy the
	 * AL object.
	 */
	p_obj->pfn_free = (al_pfn_free_t)destroy_al_obj;
	ref_al_obj( p_obj );
	p_obj->pfn_destroy( p_obj, NULL );
}



static void
__cleanup_mlnx_fmr(
	IN				struct _al_obj				*p_obj )
{
	ib_api_status_t			status;
	mlnx_fmr_handle_t			h_fmr;

	CL_ASSERT( p_obj );
	h_fmr = PARENT_STRUCT( p_obj, mlnx_fmr_t, obj );

	/* Deregister the memory. */
	if( verbs_check_mlnx_fmr( h_fmr ) )
	{
		status = verbs_destroy_mlnx_fmr( h_fmr );
		CL_ASSERT( status == IB_SUCCESS );

		h_fmr->h_ci_fmr = NULL;
		h_fmr->p_next = NULL;
	}
}



static void
__return_mlnx_fmr(
	IN				al_obj_t					*p_obj )
{
	mlnx_fmr_handle_t			h_fmr;

	h_fmr = PARENT_STRUCT( p_obj, mlnx_fmr_t, obj );
	reset_al_obj( p_obj );
	put_mlnx_fmr( h_fmr );
}



ib_api_status_t
mlnx_create_fmr(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	mlnx_fmr_create_t*	const	p_fmr_create,
	OUT				mlnx_fmr_handle_t*	const	ph_fmr )
{
	mlnx_fmr_handle_t		h_fmr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	if( !p_fmr_create || !ph_fmr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Get a FMR tracking structure. */
	h_fmr = alloc_mlnx_fmr();
	if( !h_fmr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to allocate memory handle\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	status = attach_al_obj( &h_pd->obj, &h_fmr->obj );
	if( status != IB_SUCCESS )
	{
		h_fmr->obj.pfn_destroy( &h_fmr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Register the memory region. */
	status = verbs_create_mlnx_fmr( h_pd, p_fmr_create, h_fmr );
	if( status != IB_SUCCESS )
	{
		h_fmr->obj.pfn_destroy( &h_fmr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to register memory: %s\n", ib_get_err_str(status)) );
		return status;
	}

	*ph_fmr = h_fmr;
	/* Release the reference taken in alloc_mlnx_fmr for initialization. */
	deref_al_obj( &(*ph_fmr )->obj );

	AL_EXIT( AL_DBG_MR );
	return IB_SUCCESS;
}


ib_api_status_t
mlnx_map_phys_fmr(
	IN		const	mlnx_fmr_handle_t			h_fmr,
	IN		const	uint64_t* const				paddr_list,
	IN		const	int							list_len,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey)
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_fmr, AL_OBJ_TYPE_H_FMR ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_FMR_HANDLE\n") );
		return IB_INVALID_FMR_HANDLE;
	}

	if( !paddr_list || !p_vaddr  || !p_lkey || !p_rkey )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	ref_al_obj( &h_fmr->obj );

	/* Register the memory region. */
	status = verbs_map_phys_mlnx_fmr( h_fmr, paddr_list, list_len, p_vaddr, p_lkey, p_rkey);
	if( status != IB_SUCCESS )
	{
		//TODO: do we need to do something more about the error ?
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to map FMR: %s\n", ib_get_err_str(status)) );
	}

	deref_al_obj( &h_fmr->obj );

	AL_EXIT( AL_DBG_MR );
	return status;
}


ib_api_status_t
mlnx_unmap_fmr(
	IN		const	mlnx_fmr_handle_t				h_fmr )
{
	ib_api_status_t		status;
	mlnx_fmr_t			*p_fmr = (mlnx_fmr_t*)h_fmr;
	mlnx_fmr_t			*p_cur_fmr;
	mlnx_fmr_handle_t		*p_fmr_array;
	int					i;
	
	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_fmr, AL_OBJ_TYPE_H_FMR ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_FMR_HANDLE\n") );
		return IB_INVALID_FMR_HANDLE;
	}

	// calculate the list size
	for ( i=0, p_cur_fmr = p_fmr; p_cur_fmr; p_cur_fmr = p_cur_fmr->p_next)
		i++;
	
	// allocate the array
	p_fmr_array = cl_zalloc((i+1)*sizeof(mlnx_fmr_handle_t));
	if (!p_fmr_array)
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_FMR_HANDLE\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	// fill the array
	for ( i=0, p_cur_fmr = p_fmr; p_cur_fmr; p_cur_fmr = p_cur_fmr->p_next)
	{
		p_fmr_array[i++] = p_cur_fmr->h_ci_fmr;
		ref_al_obj( &p_cur_fmr->obj );
	}
	p_fmr_array[i] = NULL;

	// unmap the array of FMRs
	status = verbs_unmap_mlnx_fmr( h_fmr, p_fmr_array );

	// deref the objects
	for ( p_cur_fmr = p_fmr; p_cur_fmr; p_cur_fmr = p_cur_fmr->p_next)
		deref_al_obj( &p_cur_fmr->obj );

	cl_free( p_fmr_array );
	
	AL_EXIT( AL_DBG_MR );
	return status;
}


ib_api_status_t
mlnx_destroy_fmr(
	IN		const	mlnx_fmr_handle_t				h_fmr )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_fmr, AL_OBJ_TYPE_H_FMR ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_FMR_HANDLE\n") );
		return IB_INVALID_FMR_HANDLE;
	}

	if( !verbs_check_mlnx_fmr( h_fmr ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_FMR_HANDLE\n") );
		return IB_INVALID_FMR_HANDLE;
	}

	ref_al_obj( &h_fmr->obj );

	/* FMR's are destroyed synchronously */
	status = verbs_destroy_mlnx_fmr( h_fmr );

	if( status == IB_SUCCESS )
	{
		h_fmr->h_ci_fmr = NULL;
		/* We're good to destroy the object. 
		NOTE: No need to deref the al object , 
		we are resetting the fmr obj before inserting it back to the pool */
		
		h_fmr->obj.pfn_destroy( &h_fmr->obj, NULL );
	}
	else
	{
		deref_al_obj( &h_fmr->obj );
	}
	AL_EXIT( AL_DBG_MR );
	return status;
}



ib_api_status_t
ib_create_shmid(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	int							shmid,
	IN		const	ib_mr_create_t* const		p_mr_create,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;
	net32_t				lkey;
	net32_t				rkey;
	ib_mr_handle_t		h_mr;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}
	if( !p_mr_create || !p_lkey || !p_rkey || !ph_mr )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Register the memory region. */
	status = ib_reg_mem( h_pd, p_mr_create, &lkey, &rkey, &h_mr );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to register memory: %s\n", ib_get_err_str(status)) );
		return status;
	}

	/* Create the shmid tracking structure. */
	h_mr->p_shmid = __create_shmid( shmid );
	if( !h_mr->p_shmid )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to allocate shmid\n") );
		ib_dereg_mr( h_mr );
		return IB_INSUFFICIENT_MEMORY;
	}

	/*
	 * Record that the memory region is associated with this shmid.  The
	 * insertion should automatically succeed since the list has a minimum
	 * size of 1.
	 */
	ref_al_obj( &h_mr->p_shmid->obj );
	cl_status = cl_list_insert_head( &h_mr->p_shmid->mr_list, h_mr );
	CL_ASSERT( cl_status == CL_SUCCESS );

	/* Add the shmid to the CI CA for tracking. */
	add_shmid( h_pd->obj.p_ci_ca, h_mr->p_shmid );

	/* Return the results. */
	*p_lkey = lkey;
	*p_rkey = rkey;
	*ph_mr = h_mr;
	AL_EXIT( AL_DBG_MR );
	return IB_SUCCESS;
}



/*
 * Allocate a new structure to track memory registrations shared across
 * processes.
 */
static al_shmid_t*
__create_shmid(
	IN		const	int							shmid )
{
	al_shmid_t			*p_shmid;
	ib_api_status_t		status;
	cl_status_t			cl_status;

	/* Allocate the shmid structure. */
	p_shmid = cl_zalloc( sizeof( al_shmid_t ) );
	if( !p_shmid )
	{
		return NULL;
	}

	/* Construct the shmid structure. */
	construct_al_obj( &p_shmid->obj, AL_OBJ_TYPE_H_MR );
	cl_list_construct( &p_shmid->mr_list );

	/* Initialize the shmid structure. */
	status = init_al_obj( &p_shmid->obj, p_shmid, TRUE,
		NULL, NULL, __free_shmid );
	if( status != IB_SUCCESS )
	{
		__free_shmid( &p_shmid->obj );
		return NULL;
	}

	cl_status = cl_list_init( &p_shmid->mr_list, 1 );
	if( cl_status != CL_SUCCESS )
	{
		p_shmid->obj.pfn_destroy( &p_shmid->obj, NULL );
		return NULL;
	}

	p_shmid->id = shmid;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_shmid->obj, E_REF_INIT );

	return p_shmid;
}



static void
__free_shmid(
	IN				struct _al_obj				*p_obj )
{
	al_shmid_t			*p_shmid;

	p_shmid = PARENT_STRUCT( p_obj, al_shmid_t, obj );

	CL_ASSERT( cl_is_list_empty( &p_shmid->mr_list ) );

	cl_list_destroy( &p_shmid->mr_list );
	destroy_al_obj( p_obj );
	cl_free( p_shmid );
}



ib_api_status_t
ib_reg_shmid(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_shmid_t					shmid,
	IN		const	ib_mr_create_t* const		p_mr_create,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr )
{
	return reg_shmid( h_pd, shmid, p_mr_create, p_vaddr, p_lkey, p_rkey, ph_mr );
}


ib_api_status_t
reg_shmid(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_shmid_t					shmid,
	IN		const	ib_mr_create_t* const		p_mr_create,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr )
{
	UNUSED_PARAM( h_pd );
	UNUSED_PARAM( shmid );
	UNUSED_PARAM( p_mr_create );
	UNUSED_PARAM( p_vaddr );
	UNUSED_PARAM( p_lkey );
	UNUSED_PARAM( p_rkey );
	UNUSED_PARAM( ph_mr );
	return IB_ERROR;
#if 0
	ib_api_status_t		status;
	cl_status_t			cl_status;
	al_shmid_t			*p_shmid;
	uint64_t			vaddr;
	net32_t				lkey;
	net32_t				rkey;
	ib_mr_handle_t		h_mr, h_reg_mr;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}
	if( !p_vaddr || !p_lkey || !p_rkey || !ph_mr )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Let's see if we can acquire the registered memory region. */
	status = acquire_shmid( h_pd->obj.p_ci_ca, shmid, &p_shmid );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("shmid not found: %s\n", ib_get_err_str(status)) );
		return IB_NOT_FOUND;
	}

	/* Lock down the shmid to prevent deregistrations while we register. */
	cl_spinlock_acquire( &p_shmid->obj.lock );

	/*
	 * There's a chance after we acquired the shmid, all current
	 * registrations were deregistered.
	 */
	if( cl_is_list_empty( &p_shmid->mr_list ) )
	{
		/* There are no registrations left to share. */
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("shmid not found\n") );
		cl_spinlock_release( &p_shmid->obj.lock );
		release_shmid( p_shmid );
		return IB_NOT_FOUND;
	}

	/* Get a handle to an existing registered memory region. */
	h_reg_mr = cl_list_obj( cl_list_head( &p_shmid->mr_list ) );

// BUGBUG: This release is not safe since the h_reg_mr can be deregistered.
	cl_spinlock_release( &p_shmid->obj.lock );

	/* Register the memory region. */
	vaddr = *p_vaddr;
	status = ib_reg_shared( h_reg_mr, h_pd, access_ctrl, &vaddr,
		&lkey, &rkey, &h_mr );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to register shared memory: 0x%0I64x %s\n",
				vaddr, ib_get_err_str(status)) );
		release_shmid( p_shmid );
		return status;
	}

	cl_spinlock_acquire( &p_shmid->obj.lock );

	/* Track the registration with the shmid structure. */
	cl_status = cl_list_insert_head( &p_shmid->mr_list, h_mr );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("insertion into shmid list failed\n") );
		cl_spinlock_release( &p_shmid->obj.lock );
		release_shmid( p_shmid );
		return ib_convert_cl_status( cl_status );
	}

	cl_spinlock_release( &p_shmid->obj.lock );

	/* Return the results. */
	h_mr->p_shmid = p_shmid;
	*p_vaddr = vaddr;
	*p_lkey = lkey;
	*p_rkey = rkey;
	*ph_mr = h_mr;
	AL_EXIT( AL_DBG_MR );
	return IB_SUCCESS;
#endif
}
