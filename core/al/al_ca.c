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

#include <complib/cl_atomic.h>
#include <iba/ib_al.h>

#include "al.h"
#include "al_av.h"
#include "al_ca.h"
#include "al_cq.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_ca.tmh"
#endif

#include "al_mgr.h"
#include "al_mr.h"
#include "al_mw.h"
#include "al_pd.h"
#include "al_qp.h"
#include "al_verbs.h"
#include "ib_common.h"


static void
__destroying_ca(
	IN				struct _al_obj				*p_obj );

static void
__cleanup_ca(
	IN				struct _al_obj				*p_obj );

static void
__free_ca(
	IN				struct _al_obj				*p_obj );



ib_api_status_t
ib_open_ca(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_net64_t					ca_guid,
	IN		const	ib_pfn_event_cb_t			pfn_ca_event_cb OPTIONAL,
	IN		const	void* const					ca_context,
		OUT			ib_ca_handle_t* const		ph_ca )
{
	ib_api_status_t	status;

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}

	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("ib_open_ca: open  CA\n"));
	status = open_ca( h_al, ca_guid, pfn_ca_event_cb, ca_context,
#if defined(CL_KERNEL)
					  KernelMode,
#endif
					  ph_ca, NULL );
	
	/* Release the reference taken in init_al_obj. */
	if( status == IB_SUCCESS )
		deref_ctx_al_obj( &(*ph_ca)->obj, E_REF_INIT );

	return status;
}


ib_api_status_t
open_ca(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_net64_t					ca_guid,
	IN		const	ib_pfn_event_cb_t			pfn_ca_event_cb OPTIONAL,
	IN		const	void* const					ca_context,
#if defined(CL_KERNEL)
	IN				KPROCESSOR_MODE				mode,
#endif
		OUT			ib_ca_handle_t* const		ph_ca,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf OPTIONAL )
{
	ib_ca_handle_t			h_ca;
	ib_api_status_t			status;
	al_obj_type_t			obj_type = AL_OBJ_TYPE_H_CA;

	AL_ENTER( AL_DBG_CA );
	if( !ph_ca )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Allocate a CA instance. */
	h_ca = (ib_ca_handle_t)cl_zalloc( sizeof( ib_ca_t ) );
	if( !h_ca )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("IB_INSUFFICIENT_MEMORY\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the CA. */
	if( p_umv_buf )
		obj_type |= AL_OBJ_SUBTYPE_UM_EXPORT;
	construct_al_obj( &h_ca->obj, obj_type );
	h_ca->pfn_event_cb = pfn_ca_event_cb;

	status = init_al_obj( &h_ca->obj, ca_context, TRUE,
		NULL, __cleanup_ca, __free_ca );
	if( status != IB_SUCCESS )
	{
		__free_ca( &h_ca->obj );
		AL_EXIT( AL_DBG_CA );
		return status;
	}

	status = attach_al_obj( &h_al->obj, &h_ca->obj );
	if( status != IB_SUCCESS )
	{
		h_ca->obj.pfn_destroy( &h_ca->obj, NULL );
		AL_EXIT( AL_DBG_CA );
		return status;
	}

	/* Obtain a reference to the correct CI CA. */
	h_ca->obj.p_ci_ca = acquire_ci_ca( ca_guid, h_ca );
	if( !h_ca->obj.p_ci_ca )
	{
		h_ca->obj.pfn_destroy( &h_ca->obj, NULL );
		AL_EXIT( AL_DBG_CA );
		return IB_INVALID_GUID;
	}

#if defined(CL_KERNEL)
	/* If a UM open, pass to the VPD to establish the UM CA context. */
	if( p_umv_buf )
	{
		status = h_ca->obj.p_ci_ca->verbs.um_open_ca(
			h_ca->obj.p_ci_ca->h_ci_ca, mode, p_umv_buf, &h_ca->h_um_ca );
		if( status != IB_SUCCESS )
		{
			h_ca->obj.pfn_destroy( &h_ca->obj, NULL );
			AL_EXIT( AL_DBG_CA );
			return status;
		}
	}
#endif	/* defined(CL_KERNEL) */

	*ph_ca = h_ca;

	AL_EXIT( AL_DBG_CA );
	return IB_SUCCESS;
}


/*
 * Destroy an instance of an AL channel adapter.
 */
ib_api_status_t
ib_close_ca(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_CA );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}

	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("ib_close_ca: close  CA\n"));
	ref_al_obj( &h_ca->obj );
	h_ca->obj.pfn_destroy( &h_ca->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_CA );
	return IB_SUCCESS;
}


/*
 * Release all resources associated with the CA.
 */
static void
__cleanup_ca(
	IN				struct _al_obj				*p_obj )
{
#if defined(CL_KERNEL)
	ib_ca_handle_t			h_ca;

	CL_ASSERT( p_obj );
	h_ca = PARENT_STRUCT( p_obj, ib_ca_t, obj );
	if( h_ca->h_um_ca )
	{
		h_ca->obj.p_ci_ca->verbs.um_close_ca(
			h_ca->obj.p_ci_ca->h_ci_ca, h_ca->h_um_ca );
	}
#endif

	/* It is now safe to release the CI CA. */
	if( p_obj->p_ci_ca )
		release_ci_ca( PARENT_STRUCT( p_obj, ib_ca_t, obj ) );
}



static void
__free_ca(
	IN				struct _al_obj				*p_obj )
{
	ib_ca_handle_t			h_ca;

	CL_ASSERT( p_obj );
	h_ca = PARENT_STRUCT( p_obj, ib_ca_t, obj );

	destroy_al_obj( p_obj );
	cl_free( h_ca );
}



ib_api_status_t
ib_query_ca(
	IN		const	ib_ca_handle_t				h_ca,
		OUT			ib_ca_attr_t* const			p_ca_attr OPTIONAL,
	IN	OUT			uint32_t* const				p_size )
{
	return query_ca( h_ca, p_ca_attr, p_size, NULL );
}



ib_api_status_t
query_ca(
	IN		const	ib_ca_handle_t				h_ca,
		OUT			ib_ca_attr_t* const			p_ca_attr OPTIONAL,
	IN	OUT			uint32_t* const				p_size,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CA );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}
	if( !p_size )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_query_ca( h_ca, p_ca_attr, p_size );

	AL_EXIT( AL_DBG_CA );
	return status;
}



ib_api_status_t
ib_modify_ca(
	IN		const ib_ca_handle_t				h_ca,
	IN		const uint8_t						port_num,
	IN		const ib_ca_mod_t					ca_mod,
	IN		const ib_port_attr_mod_t* const		p_port_attr_mod )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CA );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}
	if( !p_port_attr_mod || (ca_mod & IB_CA_MOD_RESERVED_MASK) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_modify_ca(h_ca, port_num, ca_mod, p_port_attr_mod);

	AL_EXIT( AL_DBG_CA );
	return status;
}



/*
 * Allocate a new protection domain.
 */
ib_api_status_t
ib_alloc_pd(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_pd_type_t				pd_type,
	IN		const	void * const				pd_context,
		OUT			ib_pd_handle_t* const		ph_pd )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_PD );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}

	status = alloc_pd( h_ca, pd_type, pd_context, ph_pd, NULL );

	/* Release the reference taken in init_al_obj. */
	if( status == IB_SUCCESS )
		deref_al_obj( &(*ph_pd)->obj );

	AL_EXIT( AL_DBG_PD );
	return status;
}



ib_api_status_t
ib_create_cq(
	IN		const	ib_ca_handle_t				h_ca,
	IN	OUT			ib_cq_create_t* const		p_cq_create,
	IN		const	void* const					cq_context,
	IN		const	ib_pfn_event_cb_t			pfn_cq_event_cb OPTIONAL,
		OUT			ib_cq_handle_t* const		ph_cq )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}

	status = create_cq( h_ca, p_cq_create, cq_context, pfn_cq_event_cb,
		ph_cq, NULL );

	/* Release the reference taken in init_al_obj. */
	if( status == IB_SUCCESS )
		deref_ctx_al_obj( &(*ph_cq)->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_CQ );
	return status;
}


ib_api_status_t
al_convert_to_ci_handles(
	IN				void**				const	dst_handle_array,
	IN		const	void**				const	src_handle_array,
	IN				uint32_t					num_handles )
{
	uint32_t		i;
	al_obj_t		*p_al_obj;

	for( i = 0; i < num_handles; i++ )
	{
		p_al_obj = (al_obj_t*)(const void*)src_handle_array[i];
		switch( p_al_obj->type )
		{
		case AL_OBJ_TYPE_H_PD:
			dst_handle_array[i] = ((ib_pd_t*)p_al_obj)->h_ci_pd;
			break;
		case AL_OBJ_TYPE_H_CQ:
			dst_handle_array[i] = ((ib_cq_t*)p_al_obj)->h_ci_cq;
			break;
		case AL_OBJ_TYPE_H_AV:
			dst_handle_array[i] = ((ib_av_t*)p_al_obj)->h_ci_av;
			break;
		case AL_OBJ_TYPE_H_QP:
			dst_handle_array[i] = ((ib_qp_t*)p_al_obj)->h_ci_qp;
			break;
		case AL_OBJ_TYPE_H_MR:
			dst_handle_array[i] = ((ib_mr_t*)p_al_obj)->h_ci_mr;
			break;
		case AL_OBJ_TYPE_H_MW:
			dst_handle_array[i] = ((ib_mw_t*)p_al_obj)->h_ci_mw;
			break;
		default:
			/* Bad handle type. */
			CL_ASSERT( p_al_obj->type == AL_OBJ_TYPE_H_PD ||
				p_al_obj->type == AL_OBJ_TYPE_H_CQ ||
				p_al_obj->type == AL_OBJ_TYPE_H_AV ||
				p_al_obj->type == AL_OBJ_TYPE_H_QP ||
				p_al_obj->type == AL_OBJ_TYPE_H_MR ||
				p_al_obj->type == AL_OBJ_TYPE_H_MW );
			return IB_INVALID_HANDLE;
		}
	}

	return IB_SUCCESS;
}
