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
#include "al_av.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_av.tmh"
#endif

#include "al_pd.h"
#include "al_res_mgr.h"
#include "al_verbs.h"



static void
__cleanup_av(
	IN				struct _al_obj				*p_obj );


static void
__return_av(
	IN				al_obj_t					*p_obj );



cl_status_t
av_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	ib_api_status_t			status;
	ib_av_handle_t			h_av;

	UNUSED_PARAM( context );

	h_av = (ib_av_handle_t)p_object;
	cl_memclr( h_av, sizeof( ib_av_t ) );

	construct_al_obj( &h_av->obj, AL_OBJ_TYPE_H_AV );
	status = init_al_obj( &h_av->obj, NULL, FALSE, NULL,
		__cleanup_av, __return_av );
	if( status != IB_SUCCESS )
	{
		return CL_ERROR;
	}

	*pp_pool_item = &((ib_av_handle_t)p_object)->obj.pool_item;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_av->obj, E_REF_INIT);

	return CL_SUCCESS;
}


void
av_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void*						context )
{
	al_obj_t				*p_obj;

	UNUSED_PARAM( context );

	p_obj = PARENT_STRUCT( p_pool_item, al_obj_t, pool_item );

	/*
	 * The AV is being totally destroyed.  Modify the free_cb to destroy the
	 * AL object.
	 */
	p_obj->pfn_free = (al_pfn_free_t)destroy_al_obj;
	ref_al_obj( p_obj );
	p_obj->pfn_destroy( p_obj, NULL );
}


static ib_api_status_t
__check_av_port(
	IN		const	al_ci_ca_t*	const			p_ci_ca,
	IN		const	ib_av_attr_t* const			p_av_attr )
{
	ib_api_status_t status = IB_SUCCESS;

	if (p_av_attr->port_num == 0 || p_av_attr->port_num > p_ci_ca->num_ports)
	{
		AL_PRINT(TRACE_LEVEL_WARNING ,AL_DBG_AV,
			("invalid port number specified (%d)\n", p_av_attr->port_num) );
		status = IB_INVALID_PORT;
	}
	return status;
}


ib_api_status_t
create_av(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t* const			p_av_attr,
		OUT			ib_av_handle_t* const		ph_av,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;
	ib_av_handle_t			h_av;

	CL_ASSERT( h_pd );

	if( !p_av_attr || !ph_av )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}

	if((p_av_attr->dlid ==0) &&(p_av_attr->grh_valid == FALSE)) {
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("dlid ==0 and there is no grh for this AV\n") );
		return IB_INVALID_PARAMETER;
	}

	status = __check_av_port(h_pd->obj.p_ci_ca, p_av_attr);
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PORT\n") );
		return status;
	}
	
	/* Get an AV tracking structure. */
	h_av = alloc_av();
	if( !h_av )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("alloc_av failed\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	status = attach_al_obj( &h_pd->obj, &h_av->obj );
	if( status != IB_SUCCESS )
	{
		h_av->obj.pfn_destroy( &h_av->obj, NULL );
#if DBG
		insert_pool_trace(&g_av_trace, h_av, POOL_PUT, 8);
#endif
		return status;
	}

	/* Create the address vector. */
	status = verbs_create_av( h_pd, p_av_attr, h_av );
	if( status != IB_SUCCESS )
	{
		h_av->obj.pfn_destroy( &h_av->obj, NULL );
#if DBG
		insert_pool_trace(&g_av_trace, h_av, POOL_PUT, 9);
#endif
		return status;
	}

	/* keep a copy of the av for special qp access */
	h_av->av_attr = *p_av_attr;

#ifdef CL_KERNEL
	ref_al_obj( &h_av->obj );
#endif
	*ph_av = h_av;

	return IB_SUCCESS;
}



ib_api_status_t
ib_destroy_av(
	IN		const	ib_av_handle_t				h_av )
{
	AL_ENTER( AL_DBG_AV );

#ifdef CL_KERNEL
	ASSERT(AL_OBJ_IS_TYPE( h_av, AL_OBJ_TYPE_H_AV ));
	// Remove the reference taken in create_av
	deref_al_obj( &h_av->obj );
#endif

	if( AL_OBJ_INVALID_HANDLE( h_av, AL_OBJ_TYPE_H_AV ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AV_HANDLE\n") );
		return IB_INVALID_AV_HANDLE;
	}
	
	ref_al_obj( &h_av->obj );
	h_av->obj.pfn_destroy( &h_av->obj, NULL );
#if DBG
	insert_pool_trace(&g_av_trace, h_av, POOL_PUT, 10);
#endif


	AL_EXIT( AL_DBG_AV );
	return IB_SUCCESS;
}

#pragma warning(disable:4100) //unreferenced formal parameter
ib_api_status_t
ib_destroy_av_ctx(
	IN		const	ib_av_handle_t				h_av,
	IN 		uint16_t							ctx)
{
	AL_ENTER( AL_DBG_AV );

#ifdef CL_KERNEL
	ASSERT(AL_OBJ_IS_TYPE( h_av, AL_OBJ_TYPE_H_AV ));
	// Remove the reference taken in create_av
	deref_al_obj( &h_av->obj );
#endif

	if( AL_OBJ_INVALID_HANDLE( h_av, AL_OBJ_TYPE_H_AV ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AV_HANDLE\n") );
		return IB_INVALID_AV_HANDLE;
	}

	ref_al_obj( &h_av->obj );
	h_av->obj.pfn_destroy( &h_av->obj, NULL );

#if DBG
	insert_pool_trace(&g_av_trace, h_av, POOL_PUT, ctx);
#endif

	AL_EXIT( AL_DBG_AV );
	return IB_SUCCESS;
}



static void
__cleanup_av(
	IN				struct _al_obj				*p_obj )
{
	ib_api_status_t			status;
	ib_av_handle_t			h_av;

	CL_ASSERT( p_obj );
	h_av = PARENT_STRUCT( p_obj, ib_av_t, obj );

	/* Destroy the AV. */
	if( verbs_check_av( h_av ) )
	{
		status = verbs_destroy_av(h_av);
		CL_ASSERT( status == IB_SUCCESS );
#ifndef CL_KERNEL
		h_av->obj.hdl = AL_INVALID_HANDLE;
#endif
		h_av->h_ci_av = NULL;
	}
}



static void
__return_av(
	IN				al_obj_t					*p_obj )
{
	ib_av_handle_t			h_av;

	h_av = PARENT_STRUCT( p_obj, ib_av_t, obj );
	reset_al_obj( p_obj );
	put_av( h_av );
}



ib_api_status_t
ib_query_av(
	IN		const	ib_av_handle_t				h_av,
		OUT			ib_av_attr_t* const			p_av_attr,
		OUT			ib_pd_handle_t* const		ph_pd )
{
	return query_av( h_av, p_av_attr, ph_pd, NULL );
}



ib_api_status_t
query_av(
	IN		const	ib_av_handle_t				h_av,
		OUT			ib_av_attr_t* const			p_av_attr,
		OUT			ib_pd_handle_t* const		ph_pd,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_AV );

	if( AL_OBJ_INVALID_HANDLE( h_av, AL_OBJ_TYPE_H_AV ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AV_HANDLE\n") );
		return IB_INVALID_AV_HANDLE;
	}
	if( !p_av_attr || !ph_pd )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_query_av(h_av, p_av_attr, ph_pd);

	/* Record AL's PD handle. */
	if( status == IB_SUCCESS )
	{
		*ph_pd = PARENT_STRUCT( h_av->obj.p_parent_obj, ib_pd_t, obj );
		h_av->av_attr = *p_av_attr;
	}

	AL_EXIT( AL_DBG_AV );
	return status;
}



ib_api_status_t
ib_modify_av(
	IN		const	ib_av_handle_t				h_av,
	IN		const	ib_av_attr_t* const			p_av_mod )
{
	return modify_av( h_av, p_av_mod, NULL );
}


ib_api_status_t
modify_av(
	IN		const	ib_av_handle_t				h_av,
	IN		const	ib_av_attr_t* const			p_av_mod,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_AV );

	if( AL_OBJ_INVALID_HANDLE( h_av, AL_OBJ_TYPE_H_AV ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AV_HANDLE\n") );
		return IB_INVALID_AV_HANDLE;
	}
	if( !p_av_mod )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = __check_av_port(h_av->obj.p_ci_ca, p_av_mod);
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PORT\n") );
		return status;
	}

	status = verbs_modify_av(h_av, p_av_mod);

	/* Record av for special qp access */
	if( status == IB_SUCCESS )
	{
		h_av->av_attr = *p_av_mod;
	}

	AL_EXIT( AL_DBG_AV );
	return status;
}
