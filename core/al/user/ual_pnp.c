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

/*
 * Handles all PnP-related interaction for user-mode:
 */
#include <iba/ib_al.h>

#include "al.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_pnp.tmh"
#endif

#include "al_mgr.h"
#include "al_pnp.h"
#include "ib_common.h"
#include "al_ioc_pnp.h"


#define PNP_CA_VECTOR_MIN		0
#define PNP_CA_VECTOR_GROW		10


/* PnP Manager structure. */
typedef struct _ual_pnp_mgr
{
	al_obj_t				obj;

	/* File handle on which to issue asynchronous PnP IOCTLs. */
	HANDLE					h_file;
	HANDLE					h_destroy_file;

}	ual_pnp_mgr_t;


/*
 * PnP Manager instance, creation, destruction.
 */

/* Global instance of the PnP manager. */
ual_pnp_mgr_t	*gp_pnp = NULL;


/*
 * Declarations.
 */
static void
__pnp_free(
	IN				al_obj_t					*p_obj );

static void
__pnp_async_cb(
	IN				cl_async_proc_item_t		*p_item );


ib_api_status_t
create_pnp(
	IN				al_obj_t* const			p_parent_obj )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( gp_pnp == NULL );

	gp_pnp = (ual_pnp_mgr_t*)cl_zalloc( sizeof(ual_pnp_mgr_t) );
	if( !gp_pnp )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("Failed to allocate PnP manager.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	construct_al_obj( &gp_pnp->obj, AL_OBJ_TYPE_PNP_MGR );
	gp_pnp->h_file = INVALID_HANDLE_VALUE;
	gp_pnp->h_destroy_file = INVALID_HANDLE_VALUE;

	status = init_al_obj( &gp_pnp->obj, NULL, TRUE, NULL, NULL, __pnp_free );
	if( status != IB_SUCCESS )
	{
		__pnp_free( &gp_pnp->obj );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("init_al_obj() failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}
	status = attach_al_obj( p_parent_obj, &gp_pnp->obj );
	if( status != IB_SUCCESS )
	{
		gp_pnp->obj.pfn_destroy( &gp_pnp->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Create a file object on which to issue all PNP requests. */
	gp_pnp->h_file = ual_create_async_file( UAL_BIND_PNP );
	if( gp_pnp->h_file == INVALID_HANDLE_VALUE )
	{
		gp_pnp->obj.pfn_destroy( &gp_pnp->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ual_create_async_file for UAL_BIND_PNP returned %d.\n",
			GetLastError()) );
		return IB_ERROR;
	}

	/* Create a file object on which to issue all dereg request. */
	gp_pnp->h_destroy_file = ual_create_async_file( UAL_BIND_DESTROY );
	if( gp_pnp->h_destroy_file == INVALID_HANDLE_VALUE )
	{
		gp_pnp->obj.pfn_destroy( &gp_pnp->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ual_create_async_file for UAL_BIND_DESTROY returned %d.\n",
			GetLastError()) );
		return IB_ERROR;
	}

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &gp_pnp->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_PNP );
	return( IB_SUCCESS );
}


static void
__pnp_free(
	IN				al_obj_t					*p_obj )
{
	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( PARENT_STRUCT( p_obj, ual_pnp_mgr_t, obj ) == gp_pnp );
	UNUSED_PARAM( p_obj );

	if( gp_pnp->h_file != INVALID_HANDLE_VALUE )
		CloseHandle( gp_pnp->h_file );
	if( gp_pnp->h_destroy_file != INVALID_HANDLE_VALUE )
		CloseHandle( gp_pnp->h_destroy_file );

	destroy_al_obj( &gp_pnp->obj );
	cl_free( gp_pnp );
	gp_pnp = NULL;

	AL_EXIT( AL_DBG_PNP );
}


static void
__pnp_reg_destroying(
	IN				al_obj_t					*p_obj )
{
	al_pnp_t				*p_reg;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_obj, al_pnp_t, obj );

	/* Reference the registration entry while we queue it to our PnP thread. */
	ref_al_obj( &p_reg->obj );

	/*
	 * Store the pointer to the object so we can dereference it
	 * in the completion callback.
	 */
	p_reg->destroy_ov.Pointer = &p_reg->obj;

	if( !DeviceIoControl( gp_pnp->h_destroy_file, UAL_DEREG_PNP,
		&p_reg->obj.hdl, sizeof(uint64_t), NULL, 0,
		NULL, &p_reg->destroy_ov ) )
	{
		if( GetLastError() != ERROR_IO_PENDING )
			deref_al_obj( &p_reg->obj );
	}
	else
	{
		CL_ASSERT( GetLastError() == ERROR_IO_PENDING );
		deref_al_obj( &p_reg->obj );
	}

	AL_EXIT( AL_DBG_PNP );
}


static void
__pnp_reg_free(
	IN				al_obj_t					*p_obj )
{
	al_pnp_t		*p_reg;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_obj, al_pnp_t, obj );

	/* Dereference the PnP manager. */
	deref_al_obj( &gp_pnp->obj );

	/* Free the registration structure. */
	destroy_al_obj( &p_reg->obj );
	cl_free( p_reg );

	AL_EXIT( AL_DBG_PNP );
}


ib_api_status_t
ib_reg_pnp(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_pnp_req_t* const			p_pnp_req,
		OUT			ib_pnp_handle_t* const		ph_pnp )
{
	ib_api_status_t			status;
	al_pnp_t*				p_reg;
	ual_reg_pnp_ioctl_in_t	in;

	AL_ENTER( AL_DBG_PNP );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_pnp_req || !ph_pnp )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Allocate a new registration info structure. */
	p_reg = (al_pnp_t*)cl_zalloc( sizeof(al_pnp_t) );
	if( !p_reg )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("Failed to cl_zalloc al_pnp_t (%I64d bytes).\n",
			sizeof(al_pnp_t)) );
		return( IB_INSUFFICIENT_MEMORY );
	}

	/* Initialize the registration info. */
	construct_al_obj( &p_reg->obj, AL_OBJ_TYPE_H_PNP );
	p_reg->async_item.pfn_callback = __pnp_async_cb;

	status = init_al_obj( &p_reg->obj, p_pnp_req->pnp_context, TRUE,
		__pnp_reg_destroying, NULL, __pnp_reg_free );
	if( status != IB_SUCCESS )
	{
		__pnp_reg_free( &p_reg->obj );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("init_al_obj() failed with status %s.\n", ib_get_err_str(status)) );
		return( status );
	}
	status = attach_al_obj( &h_al->obj, &p_reg->obj );
	if( status != IB_SUCCESS )
	{
		p_reg->obj.pfn_destroy( &p_reg->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Reference the PnP Manager. */
	ref_al_obj( &gp_pnp->obj );

	/* Copy the request information. */
	p_reg->pfn_pnp_cb = p_pnp_req->pfn_pnp_cb;

	in.pnp_class = p_pnp_req->pnp_class;
	in.p_status = (ULONG_PTR)&status;
	in.p_hdl = (ULONG_PTR)&p_reg->obj.hdl;

	if( pnp_get_flag( p_pnp_req->pnp_class ) & IB_PNP_FLAG_REG_SYNC )
	{
		in.sync_event = HandleToHandle64( CreateEvent( NULL, FALSE, FALSE, NULL ) );
		if( !in.sync_event )
		{
			p_reg->obj.pfn_destroy( &p_reg->obj, NULL );
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("CreateEvent returned %d\n", GetLastError()) );
			return IB_ERROR;
		}
	}

	status = IB_ERROR;

	/* The IOCTL handler will update status as needed. */
	DeviceIoControl( gp_pnp->h_file, UAL_REG_PNP,
		&in, sizeof(in), &p_reg->rearm, sizeof(p_reg->rearm),
		NULL, &p_reg->ov );

	if( status == IB_SUCCESS )
	{
		/* Set the user handle. */
		*ph_pnp = p_reg;

		/*
		 * Note that we don't release the reference taken by init_al_obj while
		 * any IOCTLs are in progress.
		 */

		if( pnp_get_flag( p_pnp_req->pnp_class ) & IB_PNP_FLAG_REG_SYNC )
		{
			WaitForSingleObject( in.sync_event, INFINITE );
			CloseHandle( in.sync_event );
		}
	}
	else
	{
		p_reg->obj.pfn_destroy( &p_reg->obj, NULL );
	}

	AL_EXIT( AL_DBG_PNP );
	return status;
}


ib_api_status_t
ib_dereg_pnp(
	IN		const	ib_pnp_handle_t				h_pnp,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_PNP );

	if( AL_OBJ_INVALID_HANDLE( h_pnp, AL_OBJ_TYPE_H_PNP ) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	ref_al_obj( &h_pnp->obj );
	h_pnp->obj.pfn_destroy( &h_pnp->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_PNP );
	return( IB_SUCCESS );
}


ib_api_status_t
ib_reject_ioc(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_pnp_handle_t				h_event )
{
	AL_ENTER( AL_DBG_PNP );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !h_event )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	AL_EXIT( AL_DBG_PNP );
	return IB_UNSUPPORTED;
}


static void
__fix_port_attr(
	IN	OUT			ib_pnp_port_rec_t* const	p_port_rec )
{
	uintn_t		offset;

	if( !p_port_rec->p_ca_attr )
		return;

	offset = (uintn_t)(p_port_rec + 1) -
		(uintn_t)(ib_ca_attr_t*)p_port_rec->p_ca_attr;
	ib_fixup_ca_attr( (ib_ca_attr_t*)(p_port_rec + 1), p_port_rec->p_ca_attr );
	p_port_rec->p_ca_attr = (ib_ca_attr_t*)(size_t)(p_port_rec + 1);
	p_port_rec->p_port_attr = (ib_port_attr_t*)
		(((uint8_t*)p_port_rec->p_port_attr) + offset);
}


static void
__fix_ca_attr(
	IN	OUT			ib_pnp_ca_rec_t* const		p_ca_rec )
{
	if( !p_ca_rec->p_ca_attr )
		return;

	ib_fixup_ca_attr( (ib_ca_attr_t*)(p_ca_rec + 1), p_ca_rec->p_ca_attr );
	p_ca_rec->p_ca_attr = (ib_ca_attr_t*)(size_t)(p_ca_rec + 1);
}


static void
__pnp_async_cb(
	IN				cl_async_proc_item_t		*p_item )
{
	al_pnp_t					*p_reg;
	ual_rearm_pnp_ioctl_in_t	in;
	cl_status_t					status;
	size_t						bytes_ret;
	ib_pnp_rec_t				*p_pnp_rec;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_item, al_pnp_t, async_item );

	in.pnp_hdl = p_reg->obj.hdl;
	in.last_evt_hdl = p_reg->rearm.evt_hdl;
	in.last_evt_context = 0;

	if( p_reg->rearm.evt_size )
	{
		/* Retrieve the PnP event and report it to the client. */
		CL_ASSERT( p_reg->rearm.evt_size >= sizeof(ib_pnp_rec_t) );

		p_pnp_rec = (ib_pnp_rec_t*)cl_malloc( p_reg->rearm.evt_size );
		if( p_pnp_rec )
		{
			status = do_al_dev_ioctl( UAL_POLL_PNP,
				&p_reg->rearm.evt_hdl, sizeof(uint64_t),
				p_pnp_rec, p_reg->rearm.evt_size, &bytes_ret );

			if( status == CL_SUCCESS )
			{
				CL_ASSERT( bytes_ret == p_reg->rearm.evt_size );
				/* Fixup pointers. */
				switch( pnp_get_class( p_pnp_rec->pnp_event ) )
				{
				case IB_PNP_PORT:
					__fix_port_attr( (ib_pnp_port_rec_t*)p_pnp_rec );
					break;
				case IB_PNP_CA:
					__fix_ca_attr( (ib_pnp_ca_rec_t*)p_pnp_rec );
					break;
				default:
					break;
				}
				p_pnp_rec->pnp_context = (void*)p_reg->obj.context;
				in.last_evt_status = p_reg->pfn_pnp_cb( p_pnp_rec );
				in.last_evt_context = (ULONG_PTR)p_pnp_rec->context;
			}
			else
			{
				in.last_evt_status = IB_SUCCESS;
			}

			if( p_pnp_rec )
				cl_free( p_pnp_rec );
		}
		else
		{
			in.last_evt_status = IB_SUCCESS;
		}
	}
	else
	{
		in.last_evt_status = IB_SUCCESS;
	}

	/* Request the next PnP event. */
	DeviceIoControl( gp_pnp->h_file, UAL_REARM_PNP,
		&in, sizeof(in), &p_reg->rearm, sizeof(p_reg->rearm),
		NULL, &p_reg->ov );

	if( GetLastError() != ERROR_IO_PENDING )
	{
		/* Release the reference taken for the IOCTL. */
		deref_al_obj( &p_reg->obj );
	}

	CL_ASSERT( GetLastError() == ERROR_IO_PENDING ||
		GetLastError() == ERROR_CANCELLED ||
		GetLastError() == ERROR_OPERATION_ABORTED );

	AL_EXIT( AL_DBG_PNP );
}


void CALLBACK
pnp_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov )
{
	al_pnp_t					*p_reg;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_ov );

	p_reg = PARENT_STRUCT( p_ov, al_pnp_t, ov );
	if( error_code || ret_bytes != sizeof(p_reg->rearm) )
	{
		if( error_code == ERROR_CANCELLED ||
			error_code == ERROR_OPERATION_ABORTED ||
			p_reg->obj.state != CL_INITIALIZED )
		{
			/* Release the reference taken for the IOCTL. */
			deref_al_obj( &p_reg->obj );
			AL_EXIT( AL_DBG_PNP );
			return;
		}

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IOCTL failed with error code %d\n",
			error_code) );
		p_reg->rearm.evt_hdl = AL_INVALID_HANDLE;
		p_reg->rearm.evt_size = 0;
	}

	cl_async_proc_queue( gp_async_pnp_mgr, &p_reg->async_item );

	AL_EXIT( AL_DBG_PNP );
}
