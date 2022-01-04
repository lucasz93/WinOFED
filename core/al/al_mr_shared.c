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
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_mr_shared.tmh"
#endif
#include "al_mr.h"
#include "al_pd.h"
#include "al_res_mgr.h"
#include "al_verbs.h"

#include "ib_common.h"


static void
__cleanup_mr(
	IN				struct _al_obj				*p_obj );

static void
__return_mr(
	IN				al_obj_t					*p_obj );


cl_status_t
mr_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	ib_api_status_t			status;
	ib_mr_handle_t			h_mr;

	UNUSED_PARAM( context );

	h_mr = (ib_mr_handle_t)p_object;
	cl_memclr( h_mr, sizeof( ib_mr_t ) );

	construct_al_obj( &h_mr->obj, AL_OBJ_TYPE_H_MR );
	status = init_al_obj( &h_mr->obj, NULL, FALSE, NULL,
		__cleanup_mr, __return_mr );
	if( status != IB_SUCCESS )
	{
		return CL_ERROR;
	}

	*pp_pool_item = &((ib_mr_handle_t)p_object)->obj.pool_item;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_mr->obj, E_REF_INIT );

	return CL_SUCCESS;
}



void
mr_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void*						context )
{
	al_obj_t				*p_obj;

	UNUSED_PARAM( context );

	p_obj = PARENT_STRUCT( p_pool_item, al_obj_t, pool_item );

	/*
	 * The MR is being totally destroyed.  Modify the free_cb to destroy the
	 * AL object.
	 */
	p_obj->pfn_free = (al_pfn_free_t)destroy_al_obj;
	ref_al_obj( p_obj );
	p_obj->pfn_destroy( p_obj, NULL );
}



static void
__cleanup_mr(
	IN				struct _al_obj				*p_obj )
{
	ib_api_status_t			status;
	ib_mr_handle_t			h_mr;

	CL_ASSERT( p_obj );
	h_mr = PARENT_STRUCT( p_obj, ib_mr_t, obj );

	/* Dereference any shared memory registrations. */
	verbs_release_shmid(h_mr);

	/* Deregister the memory. */
	if( verbs_check_mr(h_mr) )
	{
		status = verbs_deregister_mr(h_mr);

		/*
		 * This was our last chance to deregister the MR.  All MW's should
		 * be destroyed by now.
		 */
		CL_ASSERT( status == IB_SUCCESS );
		h_mr->h_ci_mr = NULL;
#ifndef CL_KERNEL
		h_mr->obj.hdl = AL_INVALID_HANDLE;
#endif
	}
}



static void
__return_mr(
	IN				al_obj_t					*p_obj )
{
	ib_mr_handle_t			h_mr;

	h_mr = PARENT_STRUCT( p_obj, ib_mr_t, obj );
	reset_al_obj( p_obj );
	put_mr( h_mr );
}



ib_api_status_t
ib_reg_mem(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t* const		p_mr_create,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	if( !p_mr_create )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = reg_mem( h_pd, p_mr_create, (ULONG_PTR)p_mr_create->vaddr,
					  p_lkey, p_rkey, ph_mr, FALSE );

	/* Release the reference taken in alloc_mr for initialization. */
	if( status == IB_SUCCESS )
		deref_al_obj( &(*ph_mr)->obj );

	AL_EXIT( AL_DBG_MR );
	return status;
}



ib_api_status_t
reg_mem(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t* const		p_mr_create,
	IN		const	uint64_t					mapaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr,
	IN				boolean_t					um_call )
{
	ib_mr_handle_t			h_mr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	if( !p_lkey || !p_rkey || !ph_mr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Get a MR tracking structure. */
	h_mr = alloc_mr();
	if( !h_mr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to allocate memory handle\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	status = attach_al_obj( &h_pd->obj, &h_mr->obj );
	if( status != IB_SUCCESS )
	{
		h_mr->obj.pfn_destroy( &h_mr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Register the memory region. */
	status = verbs_register_mr( h_pd, p_mr_create, p_lkey, p_rkey, h_mr );
	if( status != IB_SUCCESS )
	{
		h_mr->obj.pfn_destroy( &h_mr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to register memory: %s\n", ib_get_err_str(status)) );
		return status;
	}

	*ph_mr = h_mr;

	AL_EXIT( AL_DBG_MR );
	return IB_SUCCESS;
}



ib_api_status_t
ib_reg_phys(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_phys_create_t* const		p_phys_create,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr )
{
	ib_mr_handle_t			h_mr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}
	if( !p_vaddr || !p_lkey || !p_rkey || !ph_mr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Get a MR tracking structure. */
	h_mr = alloc_mr();
	if( !h_mr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to allocate memory handle\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	status = attach_al_obj( &h_pd->obj, &h_mr->obj );
	if( status != IB_SUCCESS )
	{
		h_mr->obj.pfn_destroy( &h_mr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Register the memory region. */
	status = verbs_register_pmr( h_pd, p_phys_create, p_vaddr,
		p_lkey, p_rkey, h_mr );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to register memory: %s\n", ib_get_err_str(status)) );
		h_mr->obj.pfn_destroy( &h_mr->obj, NULL );
		return status;
	}

	*ph_mr = h_mr;

	/* Release the reference taken in alloc_mr for initialization. */
	deref_al_obj( &h_mr->obj );

	AL_EXIT( AL_DBG_MR );
	return IB_SUCCESS;
}



ib_api_status_t
ib_rereg_mem(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mr_mod_mask,
	IN		const	ib_mr_create_t* const		p_mr_create OPTIONAL,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL )
{
	if( AL_OBJ_INVALID_HANDLE( h_mr, AL_OBJ_TYPE_H_MR ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MR_HANDLE\n") );
		return IB_INVALID_MR_HANDLE;
	}

	return rereg_mem(
		h_mr, mr_mod_mask, p_mr_create, p_lkey, p_rkey, h_pd, FALSE );
}


ib_api_status_t
rereg_mem(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mr_mod_mask,
	IN		const	ib_mr_create_t* const		p_mr_create OPTIONAL,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL,
	IN				boolean_t					um_call )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MR );

	if( ( mr_mod_mask & IB_MR_MOD_PD ) )
	{
		if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
			return IB_INVALID_PD_HANDLE;
		}
		if( h_pd->obj.h_al != h_mr->obj.h_al )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
			return IB_INVALID_PD_HANDLE;
		}
	}
	if( !p_lkey || !p_rkey )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Modify the registered memory region. */
	status = verbs_modify_mr( h_mr, mr_mod_mask, p_mr_create,
		p_lkey, p_rkey, h_pd );

	/* If we're changing the PD, we need to update the object hierarchy. */
	if( h_pd && (status == IB_SUCCESS) )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MR, ("associating MR with new PD\n") );
		detach_al_obj( &h_mr->obj );
		deref_al_obj( h_mr->obj.p_parent_obj );
		status = attach_al_obj( &h_pd->obj, &h_mr->obj );
		CL_ASSERT( status );
	}

	AL_EXIT( AL_DBG_MR );
	return status;
}



ib_api_status_t
ib_rereg_phys(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mr_mod_mask,
	IN		const	ib_phys_create_t* const		p_phys_create OPTIONAL,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_mr, AL_OBJ_TYPE_H_MR ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MR_HANDLE\n") );
		return IB_INVALID_MR_HANDLE;
	}
	if( ( mr_mod_mask & IB_MR_MOD_PD ) )
	{
		if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
			return IB_INVALID_PD_HANDLE;
		}
		if( h_pd->obj.p_parent_obj != h_mr->obj.p_parent_obj->p_parent_obj )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
			return IB_INVALID_PD_HANDLE;
		}
	}
	if( !p_vaddr || !p_lkey || !p_rkey )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Modify the registered memory region. */
	status = verbs_modify_pmr( h_mr, mr_mod_mask, p_phys_create, p_vaddr,
		p_lkey, p_rkey, h_pd );

	/* If we're changing the PD, we need to update the object hierarchy. */
	if( h_pd && (status == IB_SUCCESS) )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MR, ("associating MR with new PD\n") );
		detach_al_obj( &h_mr->obj );
		deref_al_obj( h_mr->obj.p_parent_obj );
		status = attach_al_obj( &h_pd->obj, &h_mr->obj );
		CL_ASSERT( status );
	}

	AL_EXIT( AL_DBG_MR );
	return status;
}



ib_api_status_t
ib_reg_shared(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_access_t					access_ctrl,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr )
{
	ib_api_status_t			status;

	if( AL_OBJ_INVALID_HANDLE( h_mr, AL_OBJ_TYPE_H_MR ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MR_HANDLE\n") );
		return IB_INVALID_MR_HANDLE;
	}
	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	status = reg_shared( h_mr, h_pd, access_ctrl, p_vaddr, p_lkey, p_rkey,
		ph_mr, FALSE );

	/* Release the reference taken in alloc_mr for initialization. */
	if( status == IB_SUCCESS )
		deref_al_obj( &(*ph_mr)->obj );

	return status;
}



ib_api_status_t
reg_shared(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_access_t					access_ctrl,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr,
	IN				boolean_t					um_call )
{
	ib_mr_handle_t			h_new_mr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	if( !p_vaddr || !p_lkey || !p_rkey || !ph_mr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Get a MR tracking structure. */
	h_new_mr = alloc_mr();
	if( !h_new_mr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to allocate memory handle\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	status = attach_al_obj( &h_pd->obj, &h_new_mr->obj );
	if( status != IB_SUCCESS )
	{
		h_new_mr->obj.pfn_destroy( &h_new_mr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Register the memory region. */
	status = verbs_register_smr( h_mr, h_pd, access_ctrl, p_vaddr,
		p_lkey, p_rkey, h_new_mr );
	if( status != IB_SUCCESS )
	{
		h_new_mr->obj.pfn_destroy( &h_new_mr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to register memory: %s\n", ib_get_err_str(status)) );
		return status;
	}

	*ph_mr = h_new_mr;

	AL_EXIT( AL_DBG_MR );
	return IB_SUCCESS;
}



ib_api_status_t
ib_dereg_mr(
	IN		const	ib_mr_handle_t				h_mr )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_mr, AL_OBJ_TYPE_H_MR ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MR_HANDLE\n") );
		return IB_INVALID_MR_HANDLE;
	}

	ref_al_obj( &h_mr->obj );

	status = dereg_mr( h_mr );
	if( status != IB_SUCCESS )
		deref_al_obj( &h_mr->obj );

	AL_EXIT( AL_DBG_MR );
	return status;
}



ib_api_status_t
dereg_mr(
	IN		const	ib_mr_handle_t				h_mr )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MR );

	if( !verbs_check_mr(h_mr) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MR_HANDLE\n") );
		return IB_INVALID_MR_HANDLE;
	}

	/*
	 * MR's are destroyed synchronously.  Go ahead and try to destroy it now.
	 * If we fail, then report the failure to the user.  Failures could be
	 * a result of having a memory window bound to the region, which we cannot
	 * track in the kernel for user-mode clients.
	 */
	status = verbs_deregister_mr(h_mr);

	if( status == IB_SUCCESS )
	{
		h_mr->h_ci_mr = NULL;
#ifndef CL_KERNEL
		h_mr->obj.hdl = AL_INVALID_HANDLE;
#endif

		/* We're good to destroy the object. */
		h_mr->obj.pfn_destroy( &h_mr->obj, NULL );
	}

	AL_EXIT( AL_DBG_MR );
	return status;
}



ib_api_status_t
ib_query_mr(
	IN		const	ib_mr_handle_t				h_mr,
		OUT			ib_mr_attr_t* const			p_mr_attr )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_mr, AL_OBJ_TYPE_H_MR ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MR_HANDLE\n") );
		return IB_INVALID_MR_HANDLE;
	}
	if( !p_mr_attr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_query_mr(h_mr, p_mr_attr);

	/* Set AL's handles. */
	if( status == IB_SUCCESS )
	{
		p_mr_attr->h_pd = PARENT_STRUCT( h_mr->obj.p_parent_obj, ib_pd_t, obj );
	}
	else
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unable to query memory region: %s\n", ib_get_err_str(status)) );
	}

	AL_EXIT( AL_DBG_MR );
	return status;
}
