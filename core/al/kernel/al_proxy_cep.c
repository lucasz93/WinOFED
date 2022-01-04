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


#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_proxy_cep.tmh"
#endif
#include "al_cm_cep.h"
#include "al_dev.h"
#include <iba/ib_al_ioctl.h>
#include "al_proxy.h"
#include "al.h"
#include "al_qp.h"


static cl_status_t
proxy_create_cep(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_create_cep_ioctl_t		*p_ioctl;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_create_cep_ioctl_t*)cl_ioctl_out_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_out_size( h_ioctl ) != sizeof(ual_create_cep_ioctl_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}
	/* We use IRPs as notification mechanism so the callback is NULL. */
	p_ioctl->status = kal_cep_alloc( p_context->h_al, &p_ioctl->cid );

	*p_ret_bytes = sizeof(ual_create_cep_ioctl_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static inline void
__complete_get_event_ioctl(
	IN				ib_al_handle_t				h_al,
	IN				IRP* const					p_irp,
	IN				NTSTATUS					status )
{
#pragma warning(push, 3)
	IoSetCancelRoutine( p_irp, NULL );
#pragma warning(pop)

	/* Complete the IRP. */
	p_irp->IoStatus.Status = status;
	p_irp->IoStatus.Information = 0;
	IoCompleteRequest( p_irp, IO_NETWORK_INCREMENT );

	deref_al_obj( &h_al->obj );
}


static cl_status_t
proxy_destroy_cep(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;

	AL_ENTER( AL_DBG_CM );

	UNUSED_PARAM( p_ret_bytes );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	al_destroy_cep( p_context->h_al,
		(net32_t*)cl_ioctl_in_buf( h_ioctl ), TRUE );

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_listen(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_listen_ioctl_t		*p_ioctl;
	ib_api_status_t				status;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_listen_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_cep_listen_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	/* Set the private data compare buffer to our kernel copy. */
	if( p_ioctl->cep_listen.p_cmp_buf )
		p_ioctl->cep_listen.p_cmp_buf = p_ioctl->compare;

	status =
		al_cep_listen( p_context->h_al, p_ioctl->cid, &p_ioctl->cep_listen );

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = status;

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_pre_req(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_req_ioctl_t			*p_ioctl;
	ib_qp_handle_t				h_qp;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_req_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(struct _ual_cep_req_ioctl_in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(struct _ual_cep_req_ioctl_out) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	*p_ret_bytes = sizeof(struct _ual_cep_req_ioctl_out);

	p_ioctl->in.cm_req.h_al = p_context->h_al;
	p_ioctl->in.cm_req.p_primary_path = &p_ioctl->in.paths[0];
	if( p_ioctl->in.cm_req.p_alt_path )
		p_ioctl->in.cm_req.p_alt_path = &p_ioctl->in.paths[1];
	if( p_ioctl->in.cm_req.p_compare_buffer )
		p_ioctl->in.cm_req.p_compare_buffer = p_ioctl->in.compare;
	if( p_ioctl->in.cm_req.p_req_pdata )
		p_ioctl->in.cm_req.p_req_pdata = p_ioctl->in.pdata;

	/* Get the kernel QP handle. */
	h_qp = (ib_qp_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.cm_req.h_qp_padding, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		p_ioctl->out.status = IB_INVALID_QP_HANDLE;
		goto done;
	}

	p_ioctl->in.cm_req.h_qp = h_qp;

	if(h_qp->type == IB_QPT_RELIABLE_CONN ||
			h_qp->type == IB_QPT_UNRELIABLE_CONN) 
		{
			((al_conn_qp_t *)(h_qp))->cid = p_ioctl->in.cid;
		}

	p_ioctl->out.status = al_cep_pre_req( p_context->h_al, p_ioctl->in.cid,
		&p_ioctl->in.cm_req, &p_ioctl->out.init );

	deref_al_obj( &h_qp->obj );

	if( p_ioctl->out.status != IB_SUCCESS )
	{
done:
		cl_memclr( &p_ioctl->out.init, sizeof(ib_qp_mod_t) );
	}

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_send_req(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = al_cep_send_req(
		p_context->h_al, *(net32_t*)cl_ioctl_in_buf( h_ioctl ) );

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_pre_rep(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_rep_ioctl_t			*p_ioctl;
	ib_qp_handle_t				h_qp;
	net32_t						cid;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_rep_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(struct _ual_cep_rep_ioctl_in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(struct _ual_cep_rep_ioctl_out) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	*p_ret_bytes = sizeof(struct _ual_cep_rep_ioctl_out);

	if( p_ioctl->in.cm_rep.p_rep_pdata )
		p_ioctl->in.cm_rep.p_rep_pdata = p_ioctl->in.pdata;

	/* Get the kernel QP handle. */
	h_qp = (ib_qp_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.cm_rep.h_qp_padding, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		p_ioctl->out.status = IB_INVALID_QP_HANDLE;
		goto done;
	}

	p_ioctl->in.cm_rep.h_qp = h_qp;

	cid = AL_INVALID_CID;
	p_ioctl->out.status = al_cep_pre_rep( p_context->h_al, p_ioctl->in.cid,
		(void*)(ULONG_PTR)p_ioctl->in.context, NULL, &p_ioctl->in.cm_rep,
		&cid, &p_ioctl->out.init );

	deref_al_obj( &h_qp->obj );

	if( p_ioctl->out.status != IB_SUCCESS )
	{
done:
		cl_memclr( &p_ioctl->out.init, sizeof(ib_qp_mod_t) );
	}

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_send_rep(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = al_cep_send_rep(
		p_context->h_al, *(net32_t*)cl_ioctl_in_buf( h_ioctl ) );

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_get_rtr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_get_rtr_ioctl_t		*p_ioctl;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_get_rtr_ioctl_t*)cl_ioctl_out_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ual_cep_get_rtr_ioctl_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	*p_ret_bytes = sizeof(ual_cep_get_rtr_ioctl_t);

	p_ioctl->status = al_cep_get_rtr_attr( p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ), &p_ioctl->rtr );

	if( p_ioctl->status != IB_SUCCESS )
		cl_memclr( &p_ioctl->rtr, sizeof(ib_qp_mod_t) );

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_get_rts(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_get_rts_ioctl_t		*p_ioctl;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_get_rts_ioctl_t*)cl_ioctl_out_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ual_cep_get_rts_ioctl_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	*p_ret_bytes = sizeof(ual_cep_get_rts_ioctl_t);

	p_ioctl->status = al_cep_get_rts_attr( p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ), &p_ioctl->rts );

	if( p_ioctl->status != IB_SUCCESS )
		cl_memclr( &p_ioctl->rts, sizeof(ib_qp_mod_t) );

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_rtu(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_rtu_ioctl_t			*p_ioctl;
	ib_api_status_t				status;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_rtu_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_cep_rtu_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	status = al_cep_rtu( p_context->h_al,
		p_ioctl->cid, p_ioctl->pdata, p_ioctl->pdata_len );

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = status;

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_rej(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_rej_ioctl_t			*p_ioctl;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_rej_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_cep_rej_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = al_cep_rej(
		p_context->h_al, p_ioctl->cid, p_ioctl->rej_status, p_ioctl->ari,
		p_ioctl->ari_len, p_ioctl->pdata, p_ioctl->pdata_len );

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_mra(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_mra_ioctl_t			*p_ioctl;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_mra_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_cep_mra_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl->cm_mra.p_mra_pdata = p_ioctl->pdata;

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = al_cep_mra(
		p_context->h_al, p_ioctl->cid, &p_ioctl->cm_mra );

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_lap(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_lap_ioctl_t			*p_ioctl;
	ib_api_status_t				status;
	ib_qp_handle_t				h_qp;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_lap_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_cep_lap_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	*p_ret_bytes = sizeof(ib_api_status_t);

	p_ioctl->cm_lap.p_alt_path = &p_ioctl->alt_path;
	if( p_ioctl->cm_lap.p_lap_pdata )
		p_ioctl->pdata;

	/* Get the kernel QP handle. */
	h_qp = (ib_qp_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->cm_lap.h_qp_padding, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		status = IB_INVALID_QP_HANDLE;
		goto done;
	}

	p_ioctl->cm_lap.h_qp = h_qp;

	status = al_cep_lap( p_context->h_al, p_ioctl->cid, &p_ioctl->cm_lap );

	deref_al_obj( &h_qp->obj );

done:
	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = status;

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_pre_apr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_apr_ioctl_t			*p_ioctl;
	ib_qp_handle_t				h_qp;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_apr_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(struct _ual_cep_apr_ioctl_in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(struct _ual_cep_apr_ioctl_out) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	*p_ret_bytes = sizeof(struct _ual_cep_apr_ioctl_out);

	if( p_ioctl->in.cm_apr.p_info )
		p_ioctl->in.cm_apr.p_info = (ib_apr_info_t*)p_ioctl->in.apr_info;
	if( p_ioctl->in.cm_apr.p_apr_pdata )
		p_ioctl->in.cm_apr.p_apr_pdata = p_ioctl->in.pdata;

	/* Get the kernel QP handle. */
	h_qp = (ib_qp_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.cm_apr.h_qp_padding, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		p_ioctl->out.status = IB_INVALID_QP_HANDLE;
		goto done;
	}

	p_ioctl->in.cm_apr.h_qp = h_qp;

	p_ioctl->out.status = al_cep_pre_apr( p_context->h_al, p_ioctl->in.cid,
		&p_ioctl->in.cm_apr, &p_ioctl->out.apr );

	deref_al_obj( &h_qp->obj );

	if( p_ioctl->out.status != IB_SUCCESS )
	{
done:
		cl_memclr( &p_ioctl->out.apr, sizeof(ib_qp_mod_t) );
	}

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_send_apr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = al_cep_send_apr(
		p_context->h_al, *(net32_t*)cl_ioctl_in_buf( h_ioctl ) );

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_dreq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_dreq_ioctl_t			*p_ioctl;
	ib_api_status_t				status;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_dreq_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_cep_dreq_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	/* Set the private data compare buffer to our kernel copy. */
	status = al_cep_dreq( p_context->h_al,
		p_ioctl->cid, p_ioctl->pdata, p_ioctl->pdata_len );

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = status;

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_drep(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_drep_ioctl_t		*p_ioctl;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_drep_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_cep_drep_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ib_api_status_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	(*(ib_api_status_t*)cl_ioctl_out_buf( h_ioctl )) = al_cep_drep(
		p_context->h_al, p_ioctl->cid, p_ioctl->pdata, p_ioctl->pdata_len );

	*p_ret_bytes = sizeof(ib_api_status_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_get_timewait(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t			*p_context;
	ual_cep_get_timewait_ioctl_t	*p_ioctl;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_get_timewait_ioctl_t*)cl_ioctl_out_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ual_cep_get_timewait_ioctl_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl->status = al_cep_get_timewait( p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ), &p_ioctl->timewait_us );

	*p_ret_bytes = sizeof(ual_cep_get_timewait_ioctl_t);

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_poll(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_poll_ioctl_t		*p_ioctl;
	ib_mad_element_t			*p_mad = NULL;
	void*						dummy;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_poll_ioctl_t*)cl_ioctl_out_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ual_cep_poll_ioctl_t) )
	{
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	*p_ret_bytes = sizeof(ual_cep_poll_ioctl_t);

	p_ioctl->status = al_cep_poll( p_context->h_al,
		*(net32_t*)cl_ioctl_in_buf( h_ioctl ),
		&dummy,
		&p_ioctl->new_cid, &p_mad );

	if( p_ioctl->status == IB_SUCCESS )
	{
		/* Copy the MAD for user consumption and free the it. */
		CL_ASSERT( p_mad );
		p_ioctl->element = *p_mad;
		if( p_mad->grh_valid )
			p_ioctl->grh = *p_mad->p_grh;
		else
			cl_memclr( &p_ioctl->grh, sizeof(ib_grh_t) );
		cl_memcpy( p_ioctl->mad_buf, p_mad->p_mad_buf, MAD_BLOCK_SIZE );
		ib_put_mad( p_mad );
	}
	else
	{
		cl_memclr( &p_ioctl->mad_buf, sizeof(MAD_BLOCK_SIZE) );
		p_ioctl->new_cid = AL_INVALID_CID;
	}

	AL_EXIT( AL_DBG_CM );
	return CL_SUCCESS;
}


static cl_status_t
proxy_cep_get_event(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	NTSTATUS				status;
	IO_STACK_LOCATION		*p_io_stack;
	al_dev_open_context_t	*p_context;
	net32_t					cid;

	AL_ENTER( AL_DBG_CM );

	UNUSED_PARAM( p_ret_bytes );

	p_context = p_open_context;

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	if( (uintn_t)p_io_stack->FileObject->FsContext2 != AL_OBJ_TYPE_CM )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid file object type for request: %p\n",
			p_io_stack->FileObject->FsContext2) );
		return CL_INVALID_PARAMETER;
	}

	/* Check the size of the ioctl */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(net32_t) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid IOCTL input buffer.\n") );
		return CL_INVALID_PARAMETER;
	}

	cid = *(net32_t*)cl_ioctl_in_buf( h_ioctl );

	status = al_cep_queue_irp( p_context->h_al, cid, h_ioctl );
	if( status != STATUS_PENDING )
	{
		/* Invalid CID.  Complete the request. */
		AL_EXIT( AL_DBG_CM );
		return CL_INVALID_PARAMETER;
	}

	AL_EXIT( AL_DBG_CM );
	return CL_PENDING;
}



static cl_status_t
proxy_cep_get_pdata(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t		*p_context;
	ual_cep_get_pdata_ioctl_t	*p_ioctl;
	NTSTATUS                    status;
	net32_t						cid;

	AL_ENTER( AL_DBG_CM );

	p_context = (al_dev_open_context_t*)p_open_context;
	p_ioctl = (ual_cep_get_pdata_ioctl_t*)cl_ioctl_in_buf( h_ioctl );

	/* Validate user parameters. */
	if( cl_ioctl_in_size( h_ioctl ) != sizeof(struct _ual_cep_get_pdata_ioctl_in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(struct _ual_cep_get_pdata_ioctl_out) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("Incorrect sizes: in %d, out %d (expected - %d, %d)\n",
			cl_ioctl_in_size( h_ioctl ), cl_ioctl_out_size( h_ioctl ), 
			sizeof(struct _ual_cep_get_pdata_ioctl_in),
			sizeof(struct _ual_cep_get_pdata_ioctl_out) ) );
		return CL_INVALID_PARAMETER;
	}

	cid = p_ioctl->in.cid;
	p_ioctl->out.pdata_len = sizeof(p_ioctl->out.pdata);
	status = al_cep_get_pdata( p_context->h_al, cid,
        &p_ioctl->out.init_depth, &p_ioctl->out.resp_res,
		(uint8_t*)&p_ioctl->out.pdata_len, p_ioctl->out.pdata );

	if( NT_SUCCESS( status ) )
	{
		*p_ret_bytes = sizeof(struct _ual_cep_get_pdata_ioctl_out);
		AL_PRINT(TRACE_LEVEL_INFORMATION ,AL_DBG_CM ,
			("proxy_cep_get_pdata: get %d of pdata \n", (int)*p_ret_bytes ));
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


cl_status_t cep_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t				cl_status;
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
	case UAL_CREATE_CEP:
		cl_status = proxy_create_cep( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DESTROY_CEP:
		cl_status = proxy_destroy_cep( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_LISTEN:
		cl_status = proxy_cep_listen( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_PRE_REQ:
		cl_status = proxy_cep_pre_req( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_SEND_REQ:
		cl_status = proxy_cep_send_req( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_PRE_REP:
		cl_status = proxy_cep_pre_rep( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_SEND_REP:
		cl_status = proxy_cep_send_rep( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_GET_RTR:
		cl_status = proxy_cep_get_rtr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_GET_RTS:
		cl_status = proxy_cep_get_rts( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_RTU:
		cl_status = proxy_cep_rtu( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_REJ:
		cl_status = proxy_cep_rej( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_MRA:
		cl_status = proxy_cep_mra( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_LAP:
		cl_status = proxy_cep_lap( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_PRE_APR:
		cl_status = proxy_cep_pre_apr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_SEND_APR:
		cl_status = proxy_cep_send_apr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_DREQ:
		cl_status = proxy_cep_dreq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_DREP:
		cl_status = proxy_cep_drep( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_GET_TIMEWAIT:
		cl_status = proxy_cep_get_timewait( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_GET_EVENT:
		cl_status = proxy_cep_get_event( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_POLL:
		cl_status = proxy_cep_poll( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CEP_GET_PDATA:
		cl_status = proxy_cep_get_pdata( p_context, h_ioctl, p_ret_bytes );
		break;
	default:
		cl_status = CL_INVALID_PARAMETER;
		break;
	}

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}
