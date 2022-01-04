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


#include <complib/comp_lib.h>
#include <iba/ib_al.h>
#include <iba/ib_al_ioctl.h>

#include "al.h"
#include "al_av.h"
#include "al_ca.h"
#include "al_cq.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_proxy_subnet.tmh"
#endif
#include "al_dev.h"
#include "al_mad_pool.h"
#include "al_mr.h"
#include "al_mw.h"
#include "al_pd.h"
#include "al_qp.h"
#include "ib_common.h"
#include "al_proxy.h"


extern	ib_pool_handle_t		gh_mad_pool;



static
cl_status_t
proxy_reg_svc(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	UNUSED_PARAM( p_open_context );
	UNUSED_PARAM( h_ioctl );
	UNUSED_PARAM( p_ret_bytes );
	return CL_ERROR;
}
static
cl_status_t
proxy_dereg_svc(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	UNUSED_PARAM( p_open_context );
	UNUSED_PARAM( h_ioctl );
	UNUSED_PARAM( p_ret_bytes );
	return CL_ERROR;
}


static void
__proxy_sa_req_cb(
	IN				al_sa_req_t					*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response )
{
	IRP						*p_irp;
	IO_STACK_LOCATION		*p_io_stack;
	ual_send_sa_req_ioctl_t	*p_ioctl;
	al_dev_open_context_t	*p_context;
	uint64_t				hdl;

	AL_ENTER( AL_DBG_QUERY );

	p_irp = (IRP*)p_sa_req->user_context;
	CL_ASSERT( p_irp );

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	p_ioctl = cl_ioctl_out_buf( p_irp );

	p_context = p_io_stack->FileObject->FsContext;
	ASSERT( p_context );
#pragma warning(push, 3)
	IoSetCancelRoutine( p_irp, NULL );
#pragma warning(pop)
	/* Clear the pointer to the query to prevent cancelation. */
	hdl = (size_t)InterlockedExchangePointer(
		&p_irp->Tail.Overlay.DriverContext[0], AL_INVALID_HANDLE );

	cl_spinlock_acquire( &p_context->h_al->obj.lock );
	if( hdl != AL_INVALID_HANDLE )
	{
		CL_ASSERT( p_sa_req ==
			al_hdl_chk( p_context->h_al, hdl, AL_OBJ_TYPE_H_SA_REQ ) );
		al_hdl_free( p_context->h_al, hdl );
	}

	p_ioctl->out.status = p_sa_req->status;
	if( p_mad_response )
	{
		/* Insert an item to track the MAD until the user fetches it. */
		hdl = al_hdl_insert( p_context->h_al,
			p_mad_response, AL_OBJ_TYPE_H_MAD );
		if( hdl != AL_INVALID_HANDLE )
		{
			p_ioctl->out.h_resp = hdl;
			p_ioctl->out.resp_size = p_mad_response->size;
		}
		else
		{
			p_ioctl->out.h_resp = AL_INVALID_HANDLE;
			p_ioctl->out.resp_size = 0;
			p_ioctl->out.status = IB_TIMEOUT;
			ib_put_mad( p_sa_req->p_mad_response );
		}
	}
	else
	{
		p_ioctl->out.h_resp = AL_INVALID_HANDLE;
		p_ioctl->out.resp_size = 0;
	}
	cl_spinlock_release( &p_context->h_al->obj.lock );

	p_irp->IoStatus.Status = STATUS_SUCCESS;
	p_irp->IoStatus.Information = sizeof(p_ioctl->out);
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	/* Release the reference taken when the query was initiated. */
	proxy_context_deref( p_context );

	cl_free( p_sa_req );

	AL_EXIT( AL_DBG_QUERY );
}

static DRIVER_CANCEL __proxy_cancel_sa_req;
static void
__proxy_cancel_sa_req(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				IRP*						p_irp )
{
	al_dev_open_context_t	*p_context;
	PIO_STACK_LOCATION		p_io_stack;
	uint64_t				hdl;
	al_sa_req_t				*p_sa_req;

	AL_ENTER( AL_DBG_DEV );

	UNUSED_PARAM( p_dev_obj );

	/* Get the stack location. */
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	p_context = (al_dev_open_context_t *)p_io_stack->FileObject->FsContext;
	ASSERT( p_context );

	hdl = (size_t)InterlockedExchangePointer(
		&p_irp->Tail.Overlay.DriverContext[0], NULL );
	if( hdl != AL_INVALID_HANDLE )
	{
#pragma warning(push, 3)
		IoSetCancelRoutine( p_irp, NULL );
#pragma warning(pop)
		cl_spinlock_acquire( &p_context->h_al->obj.lock );
		p_sa_req = al_hdl_chk( p_context->h_al, hdl, AL_OBJ_TYPE_H_SA_REQ );
		CL_ASSERT( p_sa_req );
		al_cancel_sa_req( p_sa_req );
		al_hdl_free( p_context->h_al, hdl );
		cl_spinlock_release( &p_context->h_al->obj.lock );
	}

	IoReleaseCancelSpinLock( p_irp->CancelIrql );
}


static cl_status_t
proxy_send_sa_req(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_send_sa_req_ioctl_t	*p_ioctl;
	cl_status_t				status;
	ib_api_status_t			ib_status, *p_usr_status;
	IO_STACK_LOCATION		*p_io_stack;
	al_dev_open_context_t	*p_context;
	al_sa_req_t				*p_sa_req;
	uint64_t				hdl, *p_usr_hdl;

	AL_ENTER( AL_DBG_QUERY );

	UNUSED_PARAM( p_ret_bytes );

	p_context = p_open_context;

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	/*
	 * We support SA requests coming in either through the main file object
	 * or the async file handle.
	 */
	if( p_io_stack->FileObject->FsContext2 &&
		(uintn_t)p_io_stack->FileObject->FsContext2 != AL_OBJ_TYPE_SA_REQ_SVC )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid file object type for request: %p\n",
			p_io_stack->FileObject->FsContext2) );
		return CL_INVALID_PARAMETER;
	}

	/* Check the size of the ioctl */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid IOCTL buffers.\n") );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = cl_ioctl_in_buf( h_ioctl );
	CL_ASSERT( p_ioctl );

	/* Must save user's pointers in case req completes before call returns. */
	p_usr_status = (ib_api_status_t*)(ULONG_PTR)p_ioctl->in.p_status;
	p_usr_hdl = (uint64_t*)(ULONG_PTR)p_ioctl->in.ph_sa_req;

	if( p_ioctl->in.sa_req.attr_size > IB_SA_DATA_SIZE )
	{
		ib_status = IB_INVALID_SETTING;
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid SA data size: %d\n",
			p_ioctl->in.sa_req.attr_size) );
		goto proxy_send_sa_req_err1;
	}

	p_sa_req = (al_sa_req_t*)cl_zalloc( sizeof(al_sa_req_t) );
	if( !p_sa_req )
	{
		ib_status = IB_INSUFFICIENT_MEMORY;
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed to allocate SA req.\n") );
		goto proxy_send_sa_req_err1;
	}

	/* Synchronize with callbacks. */
	cl_spinlock_acquire( &p_context->h_al->obj.lock );

	/* Track the request. */
	hdl = al_hdl_insert( p_context->h_al, p_sa_req, AL_OBJ_TYPE_H_SA_REQ );
	if( hdl == AL_INVALID_HANDLE )
	{
		ib_status = IB_INSUFFICIENT_MEMORY;
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed to create handle.\n") );
		goto proxy_send_sa_req_err2;
	}

	/*
	 * Store the handle in the IRP's driver context so we can cancel it.
	 * Note that the handle is really a size_t variable, but is cast to a
	 * uint64_t to provide constant size in mixed 32- and 64-bit environments.
	 */
	h_ioctl->Tail.Overlay.DriverContext[0] = (void*)(size_t)hdl;

	/* Format the SA request */
	p_sa_req->user_context = h_ioctl;
	p_sa_req->pfn_sa_req_cb = __proxy_sa_req_cb;

	p_ioctl->in.sa_req.p_attr = p_ioctl->in.attr;

	/*
	 * We never pass the user-mode flag when sending SA requests - the
	 * I/O manager will perform all synchronization to make this IRP sync
	 * if it needs to.
	 */
	ib_status = al_send_sa_req( p_sa_req, p_ioctl->in.port_guid,
		p_ioctl->in.timeout_ms, p_ioctl->in.retry_cnt,
		&p_ioctl->in.sa_req, 0 );
	if( ib_status == IB_SUCCESS )
	{
		/* Hold a reference on the proxy context until the request completes. */
		proxy_context_ref( p_context );
#pragma warning(push, 3)
		IoSetCancelRoutine( h_ioctl, __proxy_cancel_sa_req );
#pragma warning(pop)
		IoMarkIrpPending( h_ioctl );

		cl_spinlock_release( &p_context->h_al->obj.lock );

		cl_copy_to_user( p_usr_hdl, &hdl, sizeof(hdl) );
		status = CL_PENDING;
	}
	else
	{
		al_hdl_free( p_context->h_al, hdl );

proxy_send_sa_req_err2:
		cl_spinlock_release( &p_context->h_al->obj.lock );
		cl_free( p_sa_req );

proxy_send_sa_req_err1:
		status = CL_INVALID_PARAMETER;
	}

	cl_copy_to_user( p_usr_status, &ib_status, sizeof(ib_api_status_t) );

	AL_EXIT( AL_DBG_QUERY );
	return status;
}

#ifdef NTDDI_WIN8
static DRIVER_CANCEL __proxy_cancel_sa_req;
#endif
static cl_status_t
proxy_cancel_sa_req(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_cancel_sa_req_ioctl_t	*p_ioctl;
	al_dev_open_context_t		*p_context;
	al_sa_req_t					*p_sa_req;

	AL_ENTER( AL_DBG_QUERY );

	UNUSED_PARAM( p_ret_bytes );

	p_context = p_open_context;

	/* Check the size of the ioctl */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_cancel_sa_req_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid input buffer.\n") );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = cl_ioctl_in_buf( h_ioctl );
	CL_ASSERT( p_ioctl );

	cl_spinlock_acquire( &p_context->h_al->obj.lock );
	p_sa_req =
		al_hdl_chk( p_context->h_al, p_ioctl->h_sa_req, AL_OBJ_TYPE_H_SA_REQ );
	if( p_sa_req )
		al_cancel_sa_req( p_sa_req );
	cl_spinlock_release( &p_context->h_al->obj.lock );

	AL_EXIT( AL_DBG_QUERY );
	return CL_SUCCESS;
}


static cl_status_t
proxy_send_mad(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_send_mad_ioctl_t	*p_ioctl =
		(ual_send_mad_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_mad_svc_handle_t		h_mad_svc;
	ib_pool_key_t			pool_key = NULL;
	ib_av_handle_t			h_av = NULL;
	ib_mad_element_t		*p_mad_el;
	al_mad_element_t		*p_al_el;
	ib_mad_t				*p_mad_buf, *p_usr_buf;
	ib_grh_t				*p_grh, *p_usr_grh;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MAD );
	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MAD );
		return CL_INVALID_PARAMETER;
	}

	/* Validate mad svc handle. */
	h_mad_svc = (ib_mad_svc_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.h_mad_svc, AL_OBJ_TYPE_H_MAD_SVC );
	if( !h_mad_svc )
	{
		status = IB_INVALID_HANDLE;
		goto proxy_send_mad_err1;
	}

	/* Validate the pool key */
	pool_key = (ib_pool_key_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.pool_key, AL_OBJ_TYPE_H_POOL_KEY );
	if( !pool_key )
	{
		status = IB_INVALID_HANDLE;
		goto proxy_send_mad_err1;
	}

	/* Validate the AV handle in the mad element if it is not NULL. */
	if( p_ioctl->in.h_av )
	{
		h_av = (ib_av_handle_t)
			al_hdl_ref( p_context->h_al, p_ioctl->in.h_av, AL_OBJ_TYPE_H_AV );
		if( !h_av )
		{
			status = IB_INVALID_AV_HANDLE;
			goto proxy_send_mad_err1;
		}
	}

	/*
	 * Get a mad element from kernel MAD pool
	 * This should not fail since the pool is set to grow
	 * dynamically
	 */
	status = ib_get_mad( pool_key, p_ioctl->in.size, &p_mad_el );
	if( status != IB_SUCCESS )
		goto proxy_send_mad_err1;

	/* Store the MAD and GRH buffers pointers. */
	p_mad_buf = p_mad_el->p_mad_buf;
	p_grh = p_mad_el->p_grh;

	/* Now copy the mad element with all info */
	status = ib_convert_cl_status( cl_copy_from_user( p_mad_el,
		(void*)(ULONG_PTR)p_ioctl->in.p_mad_element,
		sizeof(ib_mad_element_t) ) );
	if( status != IB_SUCCESS )
		goto proxy_send_mad_err2;

	/* Store the UM pointers. */
	p_usr_buf = p_mad_el->p_mad_buf;
	p_usr_grh = p_mad_el->p_grh;
	/* Restore the MAD and GRH buffer pointers. */
	p_mad_el->p_mad_buf = p_mad_buf;
	p_mad_el->p_grh = p_grh;
	/* Clear the next pointer. */
	p_mad_el->p_next = NULL;
	/*
	 * Override the send context so that a response's MAD has a way
	 * of getting back to the associated send.  This is needed because a
	 * MAD receive completion could fail to be delivered to the app even though
	 * the response was properly received in the kernel.
	 */
	p_mad_el->context1 = (void*)(ULONG_PTR)p_ioctl->in.p_mad_element;

	/* Set the kernel AV handle. This is either NULL or a valid KM handle. */
	p_mad_el->h_av = h_av;

	/* Copy the GRH, if valid. */
	if( p_mad_el->grh_valid )
	{
		status = ib_convert_cl_status(
			cl_copy_from_user( p_grh, p_usr_grh, sizeof(ib_grh_t) ) );
		if( status != IB_SUCCESS )
			goto proxy_send_mad_err2;
	}

	/* Copy the mad payload. */
	status = ib_convert_cl_status(
		cl_copy_from_user( p_mad_buf, p_usr_buf, p_ioctl->in.size ) );
	if( status != IB_SUCCESS )
		goto proxy_send_mad_err2;

	/* Copy the handle to UM to allow cancelling. */
	status = ib_convert_cl_status( cl_copy_to_user(
		(void*)(ULONG_PTR)p_ioctl->in.ph_proxy,
		&p_mad_el, sizeof(ib_mad_element_t*) ) );
	if( status != IB_SUCCESS )
		goto proxy_send_mad_err2;

	/*
	 * Copy the UM element pointer to the kernel's AL element
	 * for use in completion generation.
	 */
	p_al_el = PARENT_STRUCT( p_mad_el, al_mad_element_t, element );
	p_al_el->h_proxy_element = p_ioctl->in.p_mad_element;

	/* Post the element. */
	status = ib_send_mad( h_mad_svc, p_mad_el, NULL );

	if( status != IB_SUCCESS )
	{
proxy_send_mad_err2:
		ib_put_mad( p_mad_el );
	}
proxy_send_mad_err1:

	if( h_av )
		deref_al_obj( &h_av->obj );
	if( pool_key )
		deref_al_obj( &pool_key->obj );
	if( h_mad_svc )
		deref_al_obj( &h_mad_svc->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MAD );
	return CL_SUCCESS;
}



/*
 * Process the ioctl to retrieve a received MAD.
 */
static cl_status_t
proxy_mad_comp(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_mad_recv_ioctl_t	*p_ioctl;
	al_dev_open_context_t	*p_context;
	ib_mad_element_t		*p_mad;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MAD );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MAD );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = (ual_mad_recv_ioctl_t*)cl_ioctl_in_buf( h_ioctl );
	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate the MAD handle and remove it from the handle manager. */
	p_mad = al_hdl_get_mad( p_context->h_al, p_ioctl->in.h_mad );
	if( !p_mad )
	{
		status = IB_INVALID_HANDLE;
		goto proxy_mad_comp_err1;
	}

	/*
	 * Return the MAD to the user.  The user-mode library is responsible
	 * for correcting all pointers.
	 */
	status = ib_convert_cl_status( cl_copy_to_user(
		(void*)(ULONG_PTR)p_ioctl->in.p_user_mad,
		p_mad, sizeof(ib_mad_element_t) ) );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Unable to copy element to user's MAD\n") );
		goto proxy_mad_comp_err2;
	}

	/* Copy the MAD buffer. */
	status = ib_convert_cl_status( cl_copy_to_user(
		(void*)(ULONG_PTR)p_ioctl->in.p_mad_buf, p_mad->p_mad_buf, p_mad->size ) );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Unable to copy buffer to user's MAD\n") );
		goto proxy_mad_comp_err2;
	}

	/* Copy the GRH if it is valid. */
	if( p_mad->grh_valid )
	{
		status = ib_convert_cl_status( cl_copy_to_user(
			(void*)(ULONG_PTR)p_ioctl->in.p_grh, p_mad->p_grh, sizeof(ib_grh_t) ) );
		if( status != IB_SUCCESS )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Unable to copy GRH to user's MAD\n") );
			goto proxy_mad_comp_err2;
		}
	}

	if( status == IB_SUCCESS )
	{
		ib_put_mad( p_mad );
	}
	else
	{
proxy_mad_comp_err2:
		ib_put_mad( p_mad );
proxy_mad_comp_err1:
		cl_memclr( &p_ioctl->out, sizeof(p_ioctl->out) );
	}

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MAD );
	return CL_SUCCESS;
}



static cl_status_t
proxy_init_dgrm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	UNUSED_PARAM( p_open_context );
	UNUSED_PARAM( h_ioctl );
	UNUSED_PARAM( p_ret_bytes );
	return CL_ERROR;
}



static void
__proxy_mad_send_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element )
{
	misc_cb_ioctl_info_t	cb_info;
	al_dev_open_context_t	*p_context;
	al_mad_element_t		*p_al_el;

	AL_ENTER( AL_DBG_MAD );

	CL_ASSERT( p_mad_element );
	CL_ASSERT( !p_mad_element->p_next );
	p_context = h_mad_svc->obj.h_al->p_context;
	p_al_el = PARENT_STRUCT( p_mad_element, al_mad_element_t, element );

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( proxy_context_ref( p_context ) )
	{
		/* Set up context and callback record type appropriate for UAL */
		cb_info.rec_type = MAD_SEND_REC;
		cb_info.ioctl_rec.mad_send_cb_ioctl_rec.wc_status =
			p_mad_element->status;
		cb_info.ioctl_rec.mad_send_cb_ioctl_rec.p_um_mad =
			p_al_el->h_proxy_element;
		cb_info.ioctl_rec.mad_send_cb_ioctl_rec.mad_svc_context =
			(ULONG_PTR)mad_svc_context;

		/* Queue this mad completion notification for the user. */
		proxy_queue_cb_buf( UAL_GET_MISC_CB_INFO, p_context, &cb_info,
			&h_mad_svc->obj );
	}

	/* Return the MAD. */
	ib_put_mad( p_mad_element );

	proxy_context_deref( p_context );
	AL_EXIT( AL_DBG_MAD );
}



static void
__proxy_mad_recv_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element )
{
	misc_cb_ioctl_info_t	cb_info;
	al_dev_open_context_t	*p_context;
	al_mad_element_t		*p_al_mad;
	uint64_t				hdl;

	AL_ENTER( AL_DBG_MAD );

	p_context = h_mad_svc->obj.h_al->p_context;

	p_al_mad = PARENT_STRUCT( p_mad_element, al_mad_element_t, element );

	/* Set up context and callback record type appropriate for UAL */
	cb_info.rec_type = MAD_RECV_REC;
	cb_info.ioctl_rec.mad_recv_cb_ioctl_rec.mad_svc_context = (ULONG_PTR)mad_svc_context;
	cb_info.ioctl_rec.mad_recv_cb_ioctl_rec.elem_size = p_mad_element->size;
	cb_info.ioctl_rec.mad_recv_cb_ioctl_rec.p_send_mad =
		(ULONG_PTR)p_mad_element->send_context1;

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( !proxy_context_ref( p_context ) )
	{
		ib_put_mad( p_mad_element );
		AL_EXIT( AL_DBG_MAD );
		return;
	}

	/* Insert an item to track the MAD until the user fetches it. */
	cl_spinlock_acquire( &p_context->h_al->obj.lock );
	hdl = al_hdl_insert( p_context->h_al, p_mad_element, AL_OBJ_TYPE_H_MAD );
	if( hdl == AL_INVALID_HANDLE )
		goto proxy_mad_recv_cb_err;

	cb_info.ioctl_rec.mad_recv_cb_ioctl_rec.h_mad = hdl;

	/* Queue this mad completion notification for the user. */
	if( !proxy_queue_cb_buf( UAL_GET_MISC_CB_INFO, p_context, &cb_info,
		&h_mad_svc->obj ) )
	{
		al_hdl_free( p_context->h_al, hdl );
proxy_mad_recv_cb_err:
		ib_put_mad( p_mad_element );
	}
	cl_spinlock_release( &p_context->h_al->obj.lock );

	proxy_context_deref( p_context );

	AL_EXIT( AL_DBG_MAD );
}



static cl_status_t
proxy_reg_mad_svc(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_reg_mad_svc_ioctl_t	*p_ioctl =
		(ual_reg_mad_svc_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_qp_handle_t			h_qp;
	ib_mad_svc_handle_t		h_mad_svc;

	AL_ENTER( AL_DBG_MAD );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MAD );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		p_ioctl->out.status = IB_INVALID_QP_HANDLE;
		p_ioctl->out.h_mad_svc = AL_INVALID_HANDLE;
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return CL_SUCCESS;
	}

	/* Now proxy's mad_svc overrides */
	p_ioctl->in.mad_svc.pfn_mad_send_cb = __proxy_mad_send_cb;
	p_ioctl->in.mad_svc.pfn_mad_recv_cb = __proxy_mad_recv_cb;

	p_ioctl->out.status = reg_mad_svc( h_qp,
		&p_ioctl->in.mad_svc, &h_mad_svc );
	if( p_ioctl->out.status == IB_SUCCESS )
	{
		p_ioctl->out.h_mad_svc = h_mad_svc->obj.hdl;
		h_mad_svc->obj.hdl_valid = TRUE;
		deref_al_obj( &h_mad_svc->obj );
	}
	else
	{
		p_ioctl->out.h_mad_svc = AL_INVALID_HANDLE;
	}

	deref_ctx_al_obj( &h_qp->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_MAD );
	return CL_SUCCESS;
}



/*
 * Deregister the MAD service.
 */
static cl_status_t
proxy_dereg_mad_svc(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_dereg_mad_svc_ioctl_t	*p_ioctl;
	al_dev_open_context_t		*p_context;
	ib_mad_svc_handle_t			h_mad_svc;

	AL_ENTER( AL_DBG_MAD );

	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("IOCTL buffer is invalid\n") );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = (ual_dereg_mad_svc_ioctl_t*)cl_ioctl_in_buf( h_ioctl );
	p_context = (al_dev_open_context_t*)p_open_context;

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate MAD service. */
	h_mad_svc = (ib_mad_svc_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.h_mad_svc, AL_OBJ_TYPE_H_MAD_SVC );
	if( !h_mad_svc )
	{
		p_ioctl->out.status = IB_INVALID_HANDLE;
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return CL_SUCCESS;
	}

	/* Destroy the MAD service. */
	h_mad_svc->obj.pfn_destroy( &h_mad_svc->obj, ib_sync_destroy );
	p_ioctl->out.status = IB_SUCCESS;

	AL_EXIT( AL_DBG_MAD );
	return CL_SUCCESS;
}



/*
 * UAL only uses reg_mad_pool/dereg_mad_pool ioctls
 * create/destroy mad pool is implicit in these ioctls
 */
static
cl_status_t
proxy_reg_mad_pool(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_reg_mad_pool_ioctl_t	*p_ioctl =
		(ual_reg_mad_pool_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t		*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t				h_pd;
	ib_pool_key_t				pool_key;

	AL_ENTER( AL_DBG_MAD );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MAD );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate PD handle */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd )
	{
		p_ioctl->out.status = IB_INVALID_PD_HANDLE;
		p_ioctl->out.pool_key = AL_INVALID_HANDLE;
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PD_HANDLE\n") );
		return CL_SUCCESS;
	}

	/*
	 * If we're in the kernel, we are using the global MAD pool.  Other
	 * MAD pools remain entirely in user-mode.
	 */

	/* Register the PD with the MAD pool to obtain a pool_key. */
	p_ioctl->out.status = reg_mad_pool( gh_mad_pool, h_pd, &pool_key );
	if( p_ioctl->out.status == IB_SUCCESS )
	{
		/* Track the pool info with the process context. */
		p_ioctl->out.pool_key = pool_key->obj.hdl;
		pool_key->obj.hdl_valid = TRUE;
		deref_al_obj( &pool_key->obj );
	}
	else
	{
		p_ioctl->out.pool_key = AL_INVALID_HANDLE;
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("reg_mad_pool returned %s.\n",
			ib_get_err_str(p_ioctl->out.status)) );
	}

	deref_al_obj( &h_pd->obj );

	AL_EXIT( AL_DBG_MAD );
	return CL_SUCCESS;
}



/*
 * Deregister the pool_key with the MAD pool.  Destroy the MAD pool if we
 * created one.
 */
static
cl_status_t
proxy_dereg_mad_pool(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_dereg_mad_pool_ioctl_t	*p_ioctl =
		(ual_dereg_mad_pool_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t		*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pool_key_t				pool_key;

	AL_ENTER( AL_DBG_MAD );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("IOCTL buffer is invalid\n") );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate pool key */
	pool_key = (ib_pool_key_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.pool_key, AL_OBJ_TYPE_H_POOL_KEY );
	if( !pool_key )
	{
		p_ioctl->out.status = IB_INVALID_HANDLE;
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("User-mode provided pool key is invalid\n") );
		return CL_SUCCESS;
	}

	/* We should only have alias pool keys exported to user-mode. */
	p_ioctl->out.status = dereg_mad_pool( pool_key, AL_KEY_ALIAS );
	if( p_ioctl->out.status != IB_SUCCESS )
	{
		deref_al_obj( &pool_key->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("dereg_mad_pool failed: %s\n",
			ib_get_err_str( p_ioctl->out.status )) );
	}

	AL_EXIT( AL_DBG_MAD );
	return CL_SUCCESS;
}



cl_status_t
proxy_cancel_mad(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_cancel_mad_ioctl_t	*p_ioctl;
	al_dev_open_context_t	*p_context;
	ib_mad_svc_handle_t		h_mad_svc;

	AL_ENTER( AL_DBG_MAD );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MAD );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = (ual_cancel_mad_ioctl_t*)cl_ioctl_in_buf( h_ioctl );
	p_context = (al_dev_open_context_t*)p_open_context;

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate MAD service handle. */
	h_mad_svc = (ib_mad_svc_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.h_mad_svc, AL_OBJ_TYPE_H_MAD_SVC );
	if( !h_mad_svc )
	{
		p_ioctl->out.status = IB_INVALID_HANDLE;
		AL_EXIT( AL_DBG_MAD );
		return CL_SUCCESS;
	}

	p_ioctl->out.status = ib_cancel_mad( h_mad_svc,
		(ib_mad_element_t*)(ULONG_PTR)p_ioctl->in.h_proxy_element );

	/*
	 * The clean up of resources allocated for the sent mad will
	 * be handled in the send completion callback
	 */
	AL_EXIT( AL_DBG_MAD );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_LOCAL_MAD:
 */
static cl_status_t
proxy_local_mad(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
	OUT		size_t					*p_ret_bytes )
{
	ual_local_mad_ioctl_t	*p_ioctl =
			(ual_local_mad_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
							(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MAD );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MAD );
		return CL_INVALID_PARAMETER;
	}

	if( ((ib_mad_t*)p_ioctl->in.mad_in)->method != IB_MAD_METHOD_GET )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("invalid method %d\n", ((ib_mad_t*)p_ioctl->in.mad_in)->method) );
		status = IB_UNSUPPORTED;
		goto proxy_local_mad_err;
	}

	/* Validate CA handle */
	h_ca = (ib_ca_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_ca, AL_OBJ_TYPE_H_CA );
	if( !h_ca )
	{
		status = IB_INVALID_CA_HANDLE;
		goto proxy_local_mad_err;
	}
	
	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);
	
	status = ib_local_mad(
		h_ca, p_ioctl->in.port_num, p_ioctl->in.mad_in, p_ioctl->out.mad_out );

	deref_al_obj( &h_ca->obj );

proxy_local_mad_err:
	p_ioctl->out.status = status;

	AL_EXIT( AL_DBG_MAD );
	return CL_SUCCESS;
}


cl_status_t
subnet_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
	OUT		size_t					*p_ret_bytes )
{
	cl_status_t cl_status;
	IO_STACK_LOCATION		*p_io_stack;
	void					*p_context;

	AL_ENTER( AL_DBG_DEV );

	CL_ASSERT( h_ioctl && p_ret_bytes );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	p_context = p_io_stack->FileObject->FsContext;

	if( !p_context )
	{
		AL_EXIT( AL_DBG_DEV );
		return CL_INVALID_PARAMETER;
	}

	switch( cl_ioctl_ctl_code( h_ioctl ) )
	{
	case UAL_REG_SVC:
		cl_status = proxy_reg_svc( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_SEND_SA_REQ:
		cl_status = proxy_send_sa_req( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CANCEL_SA_REQ:
		cl_status = proxy_cancel_sa_req( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_MAD_SEND:
		cl_status = proxy_send_mad( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_INIT_DGRM_SVC:
		cl_status = proxy_init_dgrm( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_REG_MAD_SVC:
		cl_status = proxy_reg_mad_svc( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_REG_MAD_POOL:
		cl_status = proxy_reg_mad_pool( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CANCEL_MAD:
		cl_status = proxy_cancel_mad( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_MAD_RECV_COMP:
		cl_status = proxy_mad_comp( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DEREG_SVC:
		cl_status = proxy_dereg_svc( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DEREG_MAD_SVC:
		cl_status = proxy_dereg_mad_svc( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DEREG_MAD_POOL:
		cl_status = proxy_dereg_mad_pool( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_LOCAL_MAD:
		cl_status = proxy_local_mad( p_context, h_ioctl, p_ret_bytes );
		break;
	default:
		cl_status = CL_INVALID_PARAMETER;
		break;
	}

	return cl_status;
}
