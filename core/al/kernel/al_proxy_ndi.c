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
#include "al_qp.h"
#include "al_debug.h"
#include "al_cm_cep.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_proxy_ndi.tmh"
#endif

#include "al_dev.h"
/* Get the internal definitions of apis for the proxy */
#include "al_ca.h"
#include "ib_common.h"
#include "al_proxy_ndi.h"
#include "al_ndi_cm.h"

#if WINVER <= 0x501
#include "csq.h"
#endif

/*******************************************************************
 *
 * IOCTLS
 *
 ******************************************************************/

/*
 * Process the ioctl UAL_NDI_CREATE_CQ:
 */
static cl_status_t
__ndi_create_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_create_cq_ioctl_t	*p_ioctl =
		(ual_create_cq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;
	ib_cq_handle_t			h_cq;
	ib_cq_create_t			cq_create;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;
	ib_pfn_event_cb_t		pfn_ev;

	AL_ENTER( AL_DBG_NDI );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		status = CL_INVALID_PARAMETER;
		goto exit;
	}

	/* Validate CA handle */
	h_ca = (ib_ca_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_ca, AL_OBJ_TYPE_H_CA );
	if( !h_ca )
	{
		status = IB_INVALID_CA_HANDLE;
        goto proxy_create_cq_err;
	}

	cq_create.size = p_ioctl->in.size;

	/* Override with proxy's cq callback */
	cq_create.pfn_comp_cb = ndi_cq_compl_cb;
	cq_create.h_wait_obj = NULL;
	pfn_ev = ndi_cq_error_cb;

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
        goto proxy_create_cq_err;

	status = create_cq( h_ca, &cq_create,
		(void*)(ULONG_PTR)p_ioctl->in.context, pfn_ev, &h_cq, p_umv_buf );

	if( status != IB_SUCCESS )
        goto proxy_create_cq_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.size = cq_create.size;
		p_ioctl->out.h_cq = h_cq->obj.hdl;
		h_cq->obj.hdl_valid = TRUE;
		deref_ctx_al_obj( &h_cq->obj, E_REF_INIT );
	}
	else
	{
		h_cq->obj.pfn_destroy( &h_cq->obj, NULL );

proxy_create_cq_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_cq = AL_INVALID_HANDLE;
		p_ioctl->out.size = 0;
	}
	free_umvbuf( p_umv_buf );

	if( h_ca )
		deref_al_obj( &h_ca->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

exit:
	AL_EXIT( AL_DBG_NDI );
	return CL_SUCCESS;
}


static cl_status_t
__ndi_notify_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t cl_status;
	ual_ndi_notify_cq_ioctl_in_t *p_ioctl;
	al_dev_open_context_t *p_context;
	ib_cq_handle_t h_cq;
	UNUSED_PARAM(p_ret_bytes);
	
	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_ndi_notify_cq_ioctl_in_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_ndi_notify_cq_ioctl_in_t) )
	{
		cl_status = CL_INVALID_PARAMETER;
		goto exit;
	}

	/* Validate CQ handle */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->h_cq, AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		cl_status = CL_INVALID_HANDLE;
		goto exit;
	}

	/* enqueue the IRP (h_cq is referenced in al_hdl_ref) */
	if (p_ioctl->notify_comps)
		IoCsqInsertIrp( &h_cq->compl.csq, h_ioctl, NULL );
	else
		IoCsqInsertIrp( &h_cq->error.csq, h_ioctl, NULL );

	cl_status = CL_PENDING;

exit:
	AL_EXIT( AL_DBG_NDI );
	return cl_status;
}

static cl_status_t
__ndi_cancel_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t cl_status;
	ib_cq_handle_t h_cq = NULL;
	al_dev_open_context_t *p_context;
	UNUSED_PARAM(p_ret_bytes);
	
	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(uint64_t) )
	{
		cl_status = CL_INVALID_PARAMETER;
		goto exit;
	}

	/* Validate CQ handle */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, 
			*(uint64_t*)cl_ioctl_in_buf( h_ioctl ), AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		cl_status = CL_INVALID_HANDLE;
		goto exit;
	}

	/* flush IRP queues */
	ndi_cq_flush_ques( h_cq );

	cl_status = CL_SUCCESS;
	deref_al_obj( &h_cq->obj );

exit:
	AL_EXIT( AL_DBG_NDI );
	return cl_status;
}

static cl_status_t
__ndi_modify_qp(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t cl_status;
	ib_api_status_t status;
	ib_qp_handle_t h_qp = NULL;
	al_dev_open_context_t *p_context;
	ual_ndi_modify_qp_ioctl_in_t *p_req = 
		(ual_ndi_modify_qp_ioctl_in_t*)cl_ioctl_in_buf( h_ioctl );

	UNUSED_PARAM(p_ret_bytes);
	
	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) < sizeof(ual_ndi_modify_qp_ioctl_in_t))
	{
		cl_status = CL_INVALID_PARAMETER;
		goto exit;
	}

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)al_hdl_ref( p_context->h_al, p_req->h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		cl_status = CL_INVALID_HANDLE;
		goto exit;
	}

	/* Check QP type */
	if( h_qp->type != IB_QPT_RELIABLE_CONN )
	{
		cl_status = CL_INVALID_HANDLE;
		goto err;
	}

	/* perform the ioctl */
	status = ndi_modify_qp( h_qp, &p_req->qp_mod, 
		cl_ioctl_out_size( h_ioctl ), cl_ioctl_out_buf( h_ioctl ) );
	if ( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ndi_modify_qp returned %s.\n", ib_get_err_str(status) ) );
		cl_status = CL_ERROR;
	}
	else
	{
		cl_status = CL_SUCCESS;
		*p_ret_bytes = cl_ioctl_out_size( h_ioctl );
	}

err:
	deref_al_obj( &h_qp->obj );

exit:
	AL_EXIT( AL_DBG_NDI );
	return cl_status;
}

static cl_status_t
__ndi_req_cm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t cl_status;
	ib_qp_handle_t h_qp = NULL;
	al_dev_open_context_t *p_context;
	ual_ndi_req_cm_ioctl_in_t *p_req = 
		(ual_ndi_req_cm_ioctl_in_t*)cl_ioctl_in_buf( h_ioctl );
	UNUSED_PARAM(p_ret_bytes);
	
	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) < sizeof(ual_ndi_req_cm_ioctl_in_t) )
	{
		cl_status = CL_INVALID_PARAMETER;
		goto exit;
	}

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)al_hdl_ref( p_context->h_al, p_req->h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		cl_status = CL_INVALID_HANDLE;
		goto exit;
	}

	/* Check QP type */
	if( h_qp->type != IB_QPT_RELIABLE_CONN )
	{
		cl_status = CL_INVALID_HANDLE;
		goto err;
	}

	/* Check psize */
	if ( p_req->pdata_size > sizeof(p_req->pdata) )
	{
		cl_status = CL_INVALID_PARAMETER;
		goto err;
	}

	/* perform the ioctl */
	cl_status = ndi_req_cm( p_context->h_al, h_ioctl );

err:
	deref_al_obj( &h_qp->obj );

exit:
	AL_EXIT( AL_DBG_NDI );
	return cl_status;
}

static cl_status_t
__ndi_rep_cm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t cl_status;
	ib_qp_handle_t h_qp = NULL;
	al_dev_open_context_t *p_context;
	ual_ndi_rep_cm_ioctl_in_t *p_rep = 
		(ual_ndi_rep_cm_ioctl_in_t*)cl_ioctl_in_buf( h_ioctl );
	UNUSED_PARAM(p_ret_bytes);

	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( (cl_ioctl_in_size( h_ioctl ) < sizeof(ual_ndi_rep_cm_ioctl_in_t)) )
	{
		cl_status = CL_INVALID_PARAMETER;
		goto exit;
	}

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI,
		("CID = %d\n", p_rep->cid) );

	/* Get and validate QP handle */
	h_qp = (ib_qp_handle_t)al_hdl_ref( p_context->h_al, p_rep->h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		cl_status = CL_INVALID_HANDLE;
		goto exit;
	}

	if( h_qp->type != IB_QPT_RELIABLE_CONN )
	{
		cl_status = CL_INVALID_HANDLE;
		goto err;
	}

	/* Check psize */
	if ( p_rep->pdata_size >= sizeof(p_rep->pdata) )
	{
		cl_status = CL_INVALID_PARAMETER;
		goto err;
	}

	/* perform the ioctls */
	cl_status = ndi_rep_cm( p_context->h_al, h_ioctl );

err:
	deref_al_obj( &h_qp->obj );

exit:
	AL_EXIT( AL_DBG_NDI );
	return cl_status;
}


static cl_status_t
__ndi_rej_cm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t *p_context;
	ib_api_status_t status;
	ual_ndi_rej_cm_ioctl_in_t *p_rej = 
		(ual_ndi_rej_cm_ioctl_in_t*)cl_ioctl_in_buf( h_ioctl );
	NTSTATUS ntstatus;
	UNUSED_PARAM(p_ret_bytes);

	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Check psize */
	if ( p_rej->pdata_size >= sizeof(p_rej->pdata) )
	{
		ntstatus = CL_INVALID_PARAMETER;
		goto exit;
	}

	/* perform the ioctl */
	status = al_cep_rej( p_context->h_al, p_rej->cid, IB_REJ_USER_DEFINED,
		NULL, 0, p_rej->pdata, p_rej->pdata_size);
	if (status != IB_SUCCESS)
	{
		ntstatus = CL_INVALID_HANDLE;
		goto exit;
	}

	ntstatus = STATUS_SUCCESS;

exit:
	AL_EXIT( AL_DBG_NDI );
	return ntstatus;
}

static cl_status_t
__ndi_rtu_cm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t cl_status;
	nd_csq_t* p_csq;
	al_dev_open_context_t *p_context;

	UNUSED_PARAM(p_ret_bytes);
	
	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) < sizeof(net32_t) )
	{
		cl_status = CL_INVALID_PARAMETER;
		goto exit;
	}

	p_csq = kal_cep_get_context(
		p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ),
		nd_cm_handler,
		nd_csq_ref
		);
	if( p_csq == NULL )
	{
		cl_status = CL_INVALID_HANDLE;
		goto exit;
	}

	/* perform the ioctl */
	cl_status = ndi_rtu_cm( p_csq, h_ioctl );

	nd_csq_release( p_csq );

exit:
	AL_EXIT( AL_DBG_NDI );
	return cl_status;
}

static cl_status_t
__ndi_dreq_cm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t cl_status;
	nd_csq_t *p_csq;
	al_dev_open_context_t *p_context;

	UNUSED_PARAM(p_ret_bytes);
	
	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) < sizeof(net32_t) )
	{
		cl_status = CL_INVALID_PARAMETER;
		goto exit;
	}

	/* Validate CID */
	p_csq = (nd_csq_t*)kal_cep_get_context(
		p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ),
		nd_cm_handler,
		nd_csq_ref
		);

	if( p_csq == NULL )
	{
		cl_status = CL_CONNECTION_INVALID;
		goto exit;
	}

	/* perform the ioctl */
	cl_status = ndi_dreq_cm( p_csq, h_ioctl );

	nd_csq_release( p_csq );

exit:
	AL_EXIT( AL_DBG_NDI );
	return cl_status;
}

static NTSTATUS
__ndi_notify_dreq_cm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	NTSTATUS status;
	nd_csq_t *p_csq;
	al_dev_open_context_t *p_context;

	UNUSED_PARAM(p_ret_bytes);
	
	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) < sizeof(net32_t) )
	{
		status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

	/* Validate CID */
	p_csq = (nd_csq_t*)kal_cep_get_context(
		p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ),
		nd_cm_handler,
		nd_csq_ref
		);

	if( p_csq == NULL )
	{
		status = STATUS_CONNECTION_INVALID;
		goto exit;
	}

	/* perform the ioctl */
	status = IoCsqInsertIrpEx(
		&p_csq->csq,
		h_ioctl,
		NULL,
		(VOID*)(ULONG_PTR)NDI_CM_CONNECTED_DREQ_RCVD
		);

	nd_csq_release( p_csq );

exit:
	AL_EXIT( AL_DBG_NDI );
	return status;
}

static cl_status_t
__ndi_cancel_cm_irps(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	nd_csq_t *p_csq;
	al_dev_open_context_t *p_context;

	UNUSED_PARAM(p_ret_bytes);
	
	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) < sizeof(net32_t) )
	{
		AL_EXIT( AL_DBG_NDI );
		return STATUS_INVALID_PARAMETER;
	}

	/* Validate CID */
	p_csq = (nd_csq_t*)kal_cep_get_context(
		p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ),
		nd_cm_handler,
		nd_csq_ref
		);

	if( p_csq == NULL )
	{
		AL_EXIT( AL_DBG_NDI );
		return STATUS_UNSUCCESSFUL;
	}

	/* perform the ioctl */
	ndi_cancel_cm_irps( p_csq );
	nd_csq_release( p_csq );

	AL_EXIT( AL_DBG_NDI );
	return STATUS_SUCCESS;
}

static cl_status_t
__ndi_listen_cm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t *p_context;
	ual_cep_listen_ioctl_t *p_listen = 
		(ual_cep_listen_ioctl_t*)cl_ioctl_in_buf( h_ioctl );
	net32_t* p_cid =
		(net32_t*)cl_ioctl_out_buf( h_ioctl );

	AL_ENTER( AL_DBG_NDI );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) < sizeof(*p_listen) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(*p_cid) )
	{
		AL_EXIT( AL_DBG_NDI );
		return CL_INVALID_PARAMETER;
	}

	/* Set the private data compare buffer to our kernel copy. */
	if( p_listen->cep_listen.p_cmp_buf )
		p_listen->cep_listen.p_cmp_buf = p_listen->compare;

	AL_EXIT( AL_DBG_NDI );
	return ndi_listen_cm( p_context->h_al, &p_listen->cep_listen, p_cid, p_ret_bytes );
}

static cl_status_t
__ndi_get_req_cm(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t *p_context;
	nd_csq_t *p_csq;
	NTSTATUS status;

	AL_ENTER( AL_DBG_NDI );

	UNREFERENCED_PARAMETER( p_ret_bytes );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(net32_t) )
	{
		AL_EXIT( AL_DBG_NDI );
		return CL_INVALID_PARAMETER;
	}

	/* Validate CID */
	p_csq = (nd_csq_t*)kal_cep_get_context(
		p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ),
		nd_cm_handler,
		nd_csq_ref
		);

	if( p_csq == NULL )
	{
		AL_EXIT( AL_DBG_NDI );
		return STATUS_UNSUCCESSFUL;
	}

	status = ndi_get_req_cm( p_csq, h_ioctl );
	nd_csq_release( p_csq );
	AL_EXIT( AL_DBG_NDI );
	return status;
}

cl_status_t
ndi_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t				cl_status;
	IO_STACK_LOCATION		*p_io_stack;
	void					*p_context;

	AL_ENTER( AL_DBG_NDI );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	p_context = p_io_stack->FileObject->FsContext;

	if( !p_context )
	{
		AL_EXIT( AL_DBG_DEV );
		return CL_INVALID_PARAMETER;
	}

	switch( cl_ioctl_ctl_code( h_ioctl ) )
	{
	case UAL_NDI_CREATE_CQ:
		cl_status = __ndi_create_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_NOTIFY_CQ:
		cl_status = __ndi_notify_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_CANCEL_CQ:
		cl_status = __ndi_cancel_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_MODIFY_QP:
		cl_status = __ndi_modify_qp( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_REQ_CM:
		cl_status = __ndi_req_cm( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_REP_CM:
		cl_status = __ndi_rep_cm( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_RTU_CM:
		cl_status = __ndi_rtu_cm( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_REJ_CM:
		cl_status = __ndi_rej_cm( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_DREQ_CM:
		cl_status = __ndi_dreq_cm( p_context, h_ioctl, p_ret_bytes );
		break;
    case UAL_NDI_NOOP:
        IoMarkIrpPending( h_ioctl );
        if( cl_ioctl_in_size( h_ioctl ) != 0 )
            h_ioctl->IoStatus.Status = STATUS_TIMEOUT;
        else
            h_ioctl->IoStatus.Status = CL_SUCCESS;
        h_ioctl->IoStatus.Information = 0;

		AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI,
			("UAL_NDI_NOOP completed with %08x\n", h_ioctl->IoStatus.Status) );
        IoCompleteRequest( h_ioctl, IO_NETWORK_INCREMENT );
        cl_status = CL_PENDING;
        break;
	case UAL_NDI_NOTIFY_DREQ:
		cl_status = __ndi_notify_dreq_cm( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_CANCEL_CM_IRPS:
		cl_status = __ndi_cancel_cm_irps( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_LISTEN_CM:
		cl_status = __ndi_listen_cm( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_NDI_GET_REQ_CM:
		cl_status = __ndi_get_req_cm( p_context, h_ioctl, p_ret_bytes );
		break;
	default:
		cl_status = CL_INVALID_PARAMETER;
		break;
	}

	AL_EXIT( AL_DBG_NDI );
	return cl_status;
}

