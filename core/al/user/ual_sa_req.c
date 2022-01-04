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

/*
 * Handles all SA-related interaction for user-mode:
 *	queries
 *	service registration
 *	multicast
 */


#include <iba/ib_al.h>
#include <complib/cl_timer.h>

#include "al.h"
#include "al_ca.h"
#include "al_common.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_sa_req.tmh"
#endif

#include "al_mgr.h"
#include "al_query.h"
#include "ib_common.h"
#include "ual_mad.h"


typedef struct _sa_req_mgr
{
	al_obj_t					obj;		/* Child of gp_al_mgr */

	/* File handle on which to issue query IOCTLs. */
	HANDLE						h_sa_dev;

}	sa_req_mgr_t;


/* Global SA request manager */
sa_req_mgr_t				*gp_sa_req_mgr = NULL;



/*
 * Function prototypes.
 */
static void
free_sa_req_mgr(
	IN				al_obj_t*					p_obj );

void
destroying_sa_req_svc(
	IN				al_obj_t*					p_obj );

void
free_sa_req_svc(
	IN				al_obj_t*					p_obj );


/*
 * Create the sa_req manager.
 */
ib_api_status_t
create_sa_req_mgr(
	IN				al_obj_t*	const			p_parent_obj )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SA_REQ );
	CL_ASSERT( p_parent_obj );
	CL_ASSERT( gp_sa_req_mgr == NULL );

	gp_sa_req_mgr = cl_zalloc( sizeof( sa_req_mgr_t ) );
	if( gp_sa_req_mgr == NULL )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("cl_zalloc failed\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the sa_req manager. */
	construct_al_obj( &gp_sa_req_mgr->obj, AL_OBJ_TYPE_SA_REQ_SVC );
	gp_sa_req_mgr->h_sa_dev = INVALID_HANDLE_VALUE;

	/* Initialize the global sa_req manager object. */
	status = init_al_obj( &gp_sa_req_mgr->obj, gp_sa_req_mgr, TRUE,
		NULL, NULL, free_sa_req_mgr );
	if( status != IB_SUCCESS )
	{
		free_sa_req_mgr( &gp_sa_req_mgr->obj );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("cl_spinlock_init failed\n") );
		return status;
	}
	status = attach_al_obj( p_parent_obj, &gp_sa_req_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_sa_req_mgr->obj.pfn_destroy( &gp_sa_req_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Create a file object on which to issue all SA requests. */
	gp_sa_req_mgr->h_sa_dev = ual_create_async_file( UAL_BIND_SA );
	if( gp_sa_req_mgr->h_sa_dev == INVALID_HANDLE_VALUE )
	{
		gp_sa_req_mgr->obj.pfn_destroy( &gp_sa_req_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ual_create_async_file returned %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	/* Release the reference from init_al_obj */
	deref_ctx_al_obj( &gp_sa_req_mgr->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_SA_REQ );
	return IB_SUCCESS;
}


/*
 * Free the sa_req manager.
 */
static void
free_sa_req_mgr(
	IN				al_obj_t*					p_obj )
{
	CL_ASSERT( p_obj );
	CL_ASSERT( gp_sa_req_mgr == PARENT_STRUCT( p_obj, sa_req_mgr_t, obj ) );
	UNUSED_PARAM( p_obj );

	if( gp_sa_req_mgr->h_sa_dev != INVALID_HANDLE_VALUE )
		CloseHandle( gp_sa_req_mgr->h_sa_dev );

	destroy_al_obj( &gp_sa_req_mgr->obj );
	cl_free( gp_sa_req_mgr );
	gp_sa_req_mgr = NULL;
}


ib_api_status_t
al_send_sa_req(
	IN				al_sa_req_t					*p_sa_req,
	IN		const	net64_t						port_guid,
	IN		const	uint32_t					timeout_ms,
	IN		const	uint32_t					retry_cnt,
	IN		const	ib_user_query_t* const		p_sa_req_data,
	IN		const	ib_al_flags_t				flags )
{
	ib_api_status_t			status;
	HANDLE					h_dev;
	DWORD					ret_bytes;

	AL_ENTER( AL_DBG_QUERY );

	CL_ASSERT( p_sa_req );
	CL_ASSERT( p_sa_req_data );

	/* Copy the query context information. */
	p_sa_req->status = IB_ERROR;

	/* Issue the query IOCTL */
	p_sa_req->ioctl.in.port_guid = port_guid;
	p_sa_req->ioctl.in.timeout_ms = timeout_ms;
	p_sa_req->ioctl.in.retry_cnt = retry_cnt;
	p_sa_req->ioctl.in.sa_req = *p_sa_req_data;
	cl_memcpy( p_sa_req->ioctl.in.attr,
		p_sa_req_data->p_attr, p_sa_req_data->attr_size );
	p_sa_req->ioctl.in.ph_sa_req = (ULONG_PTR)&p_sa_req->hdl;
	p_sa_req->ioctl.in.p_status = (ULONG_PTR)&p_sa_req->status;

	if( flags & IB_FLAGS_SYNC )
		h_dev = g_al_device;
	else
		h_dev = gp_sa_req_mgr->h_sa_dev;

	if( !DeviceIoControl( h_dev, UAL_SEND_SA_REQ,
		&p_sa_req->ioctl.in, sizeof(p_sa_req->ioctl.in),
		&p_sa_req->ioctl.out, sizeof(p_sa_req->ioctl.out),
		NULL, &p_sa_req->ov ) )
	{
		if( GetLastError() != ERROR_IO_PENDING )
		{
			status = p_sa_req->status;
			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("UAL_SEND_SA_REQ IOCTL returned %s\n",
				ib_get_err_str(status)) );
		}
		else
		{
			status = IB_SUCCESS;
		}
	}
	else
	{
		/* Completed synchronously. */
		if( GetOverlappedResult( h_dev, &p_sa_req->ov, &ret_bytes, FALSE ) )
		{
			status = IB_SUCCESS;
			/* Process the completion. */
			sa_req_cb( 0, ret_bytes, &p_sa_req->ov );
		}
		else
		{
			sa_req_cb( GetLastError(), 0, &p_sa_req->ov );
			status = IB_ERROR;
		}
	}

	AL_EXIT( AL_DBG_QUERY );
	return status;
}


void CALLBACK
sa_req_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov )
{
	al_sa_req_t			*p_sa_req;
	ib_mad_element_t	*p_mad_response = NULL;

	AL_ENTER( AL_DBG_QUERY );

	CL_ASSERT( p_ov );

	p_sa_req = PARENT_STRUCT( p_ov, al_sa_req_t, ov );

	if( error_code )
	{
		/* Some sort of failure. :( */
		p_sa_req->status = IB_ERROR;
		goto sa_req_cb_err;
	}
	else if( ret_bytes != sizeof(p_sa_req->ioctl.out) )
	{
		/* Check for expected returned data. */
		p_sa_req->status = IB_ERROR;
		goto sa_req_cb_err;
	}

	/* Retrieve the response */
	if( p_sa_req->ioctl.out.h_resp != AL_INVALID_HANDLE )
	{
		p_sa_req->status =
			ual_get_recv_mad( g_pool_key, p_sa_req->ioctl.out.h_resp,
			p_sa_req->ioctl.out.resp_size, &p_mad_response );

		if( p_sa_req->status != IB_SUCCESS )
			goto sa_req_cb_err;
	}

	p_sa_req->status = p_sa_req->ioctl.out.status;

sa_req_cb_err:
	p_sa_req->pfn_sa_req_cb( p_sa_req, p_mad_response );

	AL_EXIT( AL_DBG_QUERY );
}


void
al_cancel_sa_req(
	IN		const	al_sa_req_t					*p_sa_req )
{
	ual_cancel_sa_req_ioctl_t	ioctl;
	size_t						bytes_ret;

	AL_ENTER( AL_DBG_SA_REQ );

	ioctl.h_sa_req = p_sa_req->hdl;

	do_al_dev_ioctl(
		UAL_CANCEL_SA_REQ, &ioctl, sizeof(ioctl), NULL, 0, &bytes_ret );

	AL_EXIT( AL_DBG_SA_REQ );
}
