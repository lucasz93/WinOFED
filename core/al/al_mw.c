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
#include "al_mw.tmh"
#endif
#include "al_mw.h"
#include "al_pd.h"
#include "al_verbs.h"



void
destroying_mw(
	IN				struct _al_obj				*p_obj );

void
cleanup_mw(
	IN				struct _al_obj				*p_obj );

void
free_mw(
	IN				al_obj_t					*p_obj );



ib_api_status_t
create_mw(
	IN		const	ib_pd_handle_t				h_pd,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mw_handle_t* const		ph_mw,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_mw_handle_t			h_mw;
	ib_api_status_t			status;
	al_obj_type_t			obj_type = AL_OBJ_TYPE_H_MW;

	if( !p_rkey || !ph_mw )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Allocate a MW tracking structure. */
	h_mw = cl_zalloc( sizeof( ib_mw_t) );
	if( !h_mw )
	{
		return IB_INSUFFICIENT_MEMORY;
	}

	if( p_umv_buf )
		obj_type |= AL_OBJ_SUBTYPE_UM_EXPORT;

	/* Construct the mw. */
	construct_al_obj( &h_mw->obj, obj_type );

	status = init_al_obj( &h_mw->obj, NULL, FALSE,
		destroying_mw, NULL, free_mw );
	if( status != IB_SUCCESS )
	{
		free_mw( &h_mw->obj );
		return status;
	}
	status = attach_al_obj( &h_pd->obj, &h_mw->obj );
	if( status != IB_SUCCESS )
	{
		h_mw->obj.pfn_destroy( &h_mw->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Insert the MW into the PD's MW list used to order destruction. */
	pd_insert_mw( h_mw );

	/* Allocate the protection domain. */
	status = verbs_create_mw( h_pd, p_rkey, h_mw );
	if( status != IB_SUCCESS )
	{
		h_mw->obj.pfn_destroy( &h_mw->obj, NULL );
		return status;
	}

	*ph_mw = h_mw;

	return IB_SUCCESS;
}



ib_api_status_t
ib_destroy_mw(
	IN		const	ib_mw_handle_t				h_mw )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MW );

	if( AL_OBJ_INVALID_HANDLE( h_mw, AL_OBJ_TYPE_H_MW ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MW_HANDLE\n") );
		return IB_INVALID_MW_HANDLE;
	}

	ref_al_obj( &h_mw->obj );

	status = destroy_mw( h_mw );

	if( status != IB_SUCCESS )
		deref_al_obj( &h_mw->obj );

	AL_EXIT( AL_DBG_MW );
	return status;
}



ib_api_status_t
destroy_mw(
	IN		const	ib_mw_handle_t				h_mw )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MW );

	if( !verbs_check_mw( h_mw ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MW_HANDLE\n") );
		return IB_INVALID_MW_HANDLE;
	}

	/*
	 * MW's are destroyed synchronously.  Go ahead and try to destroy it now.
	 * If we fail, then report the failure to the user.
	 */
	status = verbs_destroy_mw( h_mw );

	if( status == IB_SUCCESS )
		h_mw->obj.pfn_destroy( &h_mw->obj, NULL );

	AL_EXIT( AL_DBG_MW );
	return status;
}



void
destroying_mw(
	IN				struct _al_obj				*p_obj )
{
	ib_mw_handle_t			h_mw;

	CL_ASSERT( p_obj );
	h_mw = PARENT_STRUCT( p_obj, ib_mw_t, obj );
	
	/* Remove the MW from the PD's MW list. */
	pd_remove_mw( h_mw );
}



/*
 * Release all resources associated with the protection domain.
 */
void
free_mw(
	IN				al_obj_t					*p_obj )
{
	ib_mw_handle_t			h_mw;

	CL_ASSERT( p_obj );
	h_mw = PARENT_STRUCT( p_obj, ib_mw_t, obj );

	destroy_al_obj( p_obj );
	cl_free( h_mw );
}



ib_api_status_t
ib_query_mw(
	IN		const	ib_mw_handle_t				h_mw,
		OUT			ib_pd_handle_t* const		ph_pd,
		OUT			net32_t* const				p_rkey )
{
	return query_mw( h_mw, ph_pd, p_rkey, NULL );
}


ib_api_status_t
query_mw(
	IN		const	ib_mw_handle_t				h_mw,
		OUT			ib_pd_handle_t* const		ph_pd,
		OUT			net32_t* const				p_rkey,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_pd_handle_t			h_ci_pd;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MW );

	if( AL_OBJ_INVALID_HANDLE( h_mw, AL_OBJ_TYPE_H_MW ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_MW_HANDLE\n") );
		return IB_INVALID_MW_HANDLE;
	}
	if( !ph_pd || !p_rkey )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = verbs_query_mw(h_mw, &h_ci_pd, p_rkey);

	/* Get the PD for AL associated with this memory window. */
	if( status == IB_SUCCESS )
	{
		*ph_pd = PARENT_STRUCT( h_mw->obj.p_parent_obj, ib_pd_t, obj );
		CL_ASSERT( (*ph_pd)->h_ci_pd == h_ci_pd );
	}

	AL_EXIT( AL_DBG_MW );
	return status;
}
