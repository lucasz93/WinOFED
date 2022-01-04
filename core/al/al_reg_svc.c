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
#include "al_reg_svc.tmh"
#endif
#include "al_reg_svc.h"
#include "ib_common.h"
#include "al_mgr.h"



static void
__dereg_svc_cb(
	IN		al_sa_req_t			*p_sa_req,
	IN		ib_mad_element_t	*p_mad_response )
{
	ib_reg_svc_handle_t		h_reg_svc;

	/*
	 * Note that we come into this callback with a reference
	 * on the registration object.
	 */
	h_reg_svc = PARENT_STRUCT( p_sa_req, al_reg_svc_t, sa_req );

	if( p_mad_response )
		ib_put_mad( p_mad_response );

	h_reg_svc->obj.pfn_destroy( &h_reg_svc->obj, NULL );
}


static void
__sa_dereg_svc(
	IN		const	ib_reg_svc_handle_t			h_reg_svc )
{
	ib_user_query_t			sa_mad_data;

	ref_al_obj( &h_reg_svc->obj );

	/* Set the request information. */
	h_reg_svc->sa_req.pfn_sa_req_cb = __dereg_svc_cb;

	/* Set the MAD attributes and component mask correctly. */
	sa_mad_data.method = IB_MAD_METHOD_DELETE;
	sa_mad_data.attr_id = IB_MAD_ATTR_SERVICE_RECORD;
	sa_mad_data.attr_size = sizeof(ib_service_record_t);

	sa_mad_data.p_attr = &h_reg_svc->svc_rec;
	sa_mad_data.comp_mask = ~CL_CONST64(0);

	if( al_send_sa_req( &h_reg_svc->sa_req, h_reg_svc->port_guid,
		500, 0, &sa_mad_data, 0 ) != IB_SUCCESS )
	{
		/* Cleanup from the registration. */
		deref_al_obj( &h_reg_svc->obj );
	}
}


void
reg_svc_req_cb(
	IN				al_sa_req_t					*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response )
{
	ib_reg_svc_handle_t		h_reg_svc;
	ib_sa_mad_t				*p_sa_mad;
	ib_reg_svc_rec_t		reg_svc_rec;

	/*
	 * Note that we come into this callback with a reference
	 * on the registration object.
	 */
	h_reg_svc = PARENT_STRUCT( p_sa_req, al_reg_svc_t, sa_req );

	CL_ASSERT( h_reg_svc->pfn_reg_svc_cb );

	/* Record the status of the registration request. */
	h_reg_svc->req_status = p_sa_req->status;

	if( p_mad_response )
	{
		p_sa_mad = (ib_sa_mad_t *)p_mad_response->p_mad_buf;
		h_reg_svc->resp_status = p_sa_mad->status;

		if ( h_reg_svc->req_status == IB_SUCCESS )
		{
			/* Save the service registration results. */
			h_reg_svc->svc_rec = *((ib_service_record_t *)p_sa_mad->data);
		}

		/* We no longer need the response MAD. */
		ib_put_mad( p_mad_response );
	}

	/* Initialize the user's callback record. */
	cl_memclr( &reg_svc_rec, sizeof( ib_reg_svc_rec_t ) );
	reg_svc_rec.svc_context = h_reg_svc->sa_req.user_context;
	reg_svc_rec.req_status = h_reg_svc->req_status;
	reg_svc_rec.resp_status = h_reg_svc->resp_status;
	reg_svc_rec.svc_rec = h_reg_svc->svc_rec;

	cl_spinlock_acquire( &h_reg_svc->obj.lock );
	/* See if the registration was successful. */
	if( reg_svc_rec.req_status == IB_SUCCESS )
	{
		/* Ensure that the user wants the registration to proceed. */
		if( h_reg_svc->state == SA_REG_STARTING )
		{
			h_reg_svc->state = SA_REG_ACTIVE;
			reg_svc_rec.h_reg_svc = h_reg_svc;
		}
		else
		{
			CL_ASSERT( h_reg_svc->state == SA_REG_CANCELING );
			reg_svc_rec.req_status = IB_CANCELED;

			/* Notify the SA that we're deregistering. */
			__sa_dereg_svc( h_reg_svc );
		}
	}
	else
	{
		h_reg_svc->state = SA_REG_ERROR;
	}
	cl_spinlock_release( &h_reg_svc->obj.lock );

	h_reg_svc->pfn_reg_svc_cb( &reg_svc_rec );

	if( p_sa_req->status != IB_SUCCESS )
	{
		h_reg_svc->obj.pfn_destroy( &h_reg_svc->obj, NULL );
	}
	else
	{
		/* Release the reference taken when issuing the request. */
		deref_al_obj( &h_reg_svc->obj );
	}
}


static void
__destroying_sa_reg(
	IN				al_obj_t* const				p_obj )
{
	ib_reg_svc_handle_t		h_sa_reg;

	AL_ENTER( AL_DBG_SA_REQ );

	h_sa_reg = PARENT_STRUCT( p_obj, al_reg_svc_t, obj );

	cl_spinlock_acquire( &p_obj->lock );

	CL_ASSERT( h_sa_reg->state != SA_REG_HALTING );
	switch( h_sa_reg->state )
	{
	case SA_REG_STARTING:
		/*
		 * Cancel registration.  Note that there is a reference held until
		 * this completes.
		 */
		h_sa_reg->state = SA_REG_CANCELING;
		al_cancel_sa_req( &h_sa_reg->sa_req );
		break;

	case SA_REG_ERROR:
		/* Nothing to do. */
		break;
		
	default:
		h_sa_reg->state = SA_REG_HALTING;

		__sa_dereg_svc( h_sa_reg );
	}
	cl_spinlock_release( &p_obj->lock );

}


static void
__free_sa_reg(
	IN				al_obj_t* const				p_obj )
{
	ib_reg_svc_handle_t		h_sa_reg;

	AL_ENTER( AL_DBG_SA_REQ );

	h_sa_reg = PARENT_STRUCT( p_obj, al_reg_svc_t, obj );

	destroy_al_obj( p_obj );
	cl_free( h_sa_reg );

	AL_EXIT( AL_DBG_SA_REQ );
}


static ib_api_status_t
sa_reg_svc(
	IN		const	ib_reg_svc_handle_t			h_reg_svc,
	IN		const	ib_reg_svc_req_t* const		p_reg_svc_req )
{
	ib_user_query_t			sa_mad_data;

	/* Set the request information. */
	h_reg_svc->sa_req.pfn_sa_req_cb = reg_svc_req_cb;

	/* Set the MAD attributes and component mask correctly. */
	sa_mad_data.method = IB_MAD_METHOD_SET;
	sa_mad_data.attr_id = IB_MAD_ATTR_SERVICE_RECORD;
	sa_mad_data.attr_size = sizeof(ib_service_record_t);

	/* Initialize the component mask. */
	sa_mad_data.comp_mask = p_reg_svc_req->svc_data_mask;
	sa_mad_data.p_attr = &h_reg_svc->svc_rec;

	return al_send_sa_req( &h_reg_svc->sa_req, h_reg_svc->port_guid,
		p_reg_svc_req->timeout_ms, p_reg_svc_req->retry_cnt, &sa_mad_data,
		p_reg_svc_req->flags );
}


ib_api_status_t
ib_reg_svc(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_reg_svc_req_t* const		p_reg_svc_req,
		OUT			ib_reg_svc_handle_t* const	ph_reg_svc )
{
	ib_reg_svc_handle_t		h_sa_reg = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SA_REQ );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_reg_svc_req )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Allocate a new service registration request. */
	h_sa_reg = cl_zalloc( sizeof( al_reg_svc_t ) );
	if( !h_sa_reg )
	{
		AL_EXIT( AL_DBG_SA_REQ );
		return IB_INSUFFICIENT_MEMORY;
	}

	construct_al_obj( &h_sa_reg->obj, AL_OBJ_TYPE_H_SA_REG );

	status = init_al_obj( &h_sa_reg->obj, p_reg_svc_req->svc_context, TRUE,
		__destroying_sa_reg, NULL, __free_sa_reg );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj returned %s.\n", ib_get_err_str( status )) );
		__free_sa_reg( &h_sa_reg->obj );
		return status;
	}

	/* Track the registered service with the AL instance. */
	status = attach_al_obj( &h_al->obj, &h_sa_reg->obj );
	if( status != IB_SUCCESS )
	{
		h_sa_reg->obj.pfn_destroy( &h_sa_reg->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str( status )) );
		return status;
	}

	/* Store the port GUID on which to issue the request. */
	h_sa_reg->port_guid = p_reg_svc_req->port_guid;

	/* Copy the service registration information. */
	h_sa_reg->sa_req.user_context = p_reg_svc_req->svc_context;
	h_sa_reg->pfn_reg_svc_cb = p_reg_svc_req->pfn_reg_svc_cb;
	h_sa_reg->svc_rec = p_reg_svc_req->svc_rec;

	h_sa_reg->state = SA_REG_STARTING;

	/* Issue the MAD to the SA. */
	status = sa_reg_svc( h_sa_reg, p_reg_svc_req );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("sa_reg_svc failed: %s\n", ib_get_err_str(status) ) );
		h_sa_reg->state = SA_REG_ERROR;

		h_sa_reg->obj.pfn_destroy( &h_sa_reg->obj, NULL );
	}
	else
	{
		*ph_reg_svc = h_sa_reg;
	}

	AL_EXIT( AL_DBG_SA_REQ );
	return status;
}


ib_api_status_t
ib_dereg_svc(
	IN		const	ib_reg_svc_handle_t			h_reg_svc,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_SA_REQ );

	if( AL_OBJ_INVALID_HANDLE( h_reg_svc, AL_OBJ_TYPE_H_SA_REG) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	ref_al_obj( &h_reg_svc->obj );
	h_reg_svc->obj.pfn_destroy( &h_reg_svc->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_SA_REQ );
	return IB_SUCCESS;
}
