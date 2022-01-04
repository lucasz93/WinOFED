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


#include "al.h"
#include "al_av.h"
#include "al_ci_ca.h"
#include "al_cq.h"
#include "al_pd.h"
#include "al_qp.h"
#include "al_srq.h"
#include "ual_mad.h"
#include "ual_support.h"


#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_qp.tmh"
#endif
ib_api_status_t
ual_post_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t*	const		p_send_wr,
		OUT			ib_send_wr_t				**pp_send_failure OPTIONAL )
{
	uintn_t					failed_index;
	uintn_t					bytes_ret;
	uint32_t				num_wr		= 0;
	uint32_t				num_ds		= 0;
	ib_send_wr_t			*p_wr;
	ib_local_ds_t			*p_ds;
	ual_post_send_ioctl_t	*p_qp_ioctl;
	size_t					ioctl_buf_sz;
	cl_status_t				cl_status;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	/*
	 * Since the work request is a link list and we need to pass this
	 * to the kernel as a array of work requests.  So first walk through
	 * the list and find out how much memory we need to allocate.
	 */
	for( p_wr = p_send_wr; p_wr; p_wr = p_wr->p_next )
	{
		num_wr++;

		/* Check for overflow */
		if( !num_wr )
			break;
		if( num_ds > num_ds + p_wr->num_ds )
		{
			num_wr = 0;
			break;
		}

		num_ds += p_wr->num_ds;
	}
	if( !num_wr )
	{
		AL_EXIT( AL_DBG_QP );
		return IB_INVALID_PARAMETER;
	}

	ioctl_buf_sz = sizeof(ual_post_send_ioctl_t);
	ioctl_buf_sz += sizeof(ib_send_wr_t) * (num_wr - 1);
	ioctl_buf_sz += sizeof(ib_local_ds_t) * num_ds;

	p_qp_ioctl = (ual_post_send_ioctl_t*)cl_zalloc( ioctl_buf_sz );
	if( !p_qp_ioctl )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("Failed to allocate IOCTL buffer.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}
	p_ds = (ib_local_ds_t*)&p_qp_ioctl->in.send_wr[num_wr];

	/* Now populate the ioctl buffer and send down the ioctl */
	p_qp_ioctl->in.h_qp = h_qp->obj.hdl;
	p_qp_ioctl->in.num_wr = num_wr;
	p_qp_ioctl->in.num_ds = num_ds;
	num_wr = 0;
	for( p_wr = p_send_wr; p_wr; p_wr = p_wr->p_next )
	{
		/* pNext and pDs pointer is set by the kernel proxy. */
		p_qp_ioctl->in.send_wr[num_wr] = *p_wr;
		if( h_qp->type == IB_QPT_UNRELIABLE_DGRM )
		{
			p_qp_ioctl->in.send_wr[num_wr].dgrm.ud.h_av =
				(ib_av_handle_t) (ULONG_PTR) p_wr->dgrm.ud.h_av->obj.hdl;
		}
		num_wr++;
		cl_memcpy(
			p_ds, p_wr->ds_array, sizeof(ib_local_ds_t) * p_wr->num_ds );
		p_ds += p_wr->num_ds;
	}

	cl_status = do_al_dev_ioctl( UAL_POST_SEND,
		&p_qp_ioctl->in, ioctl_buf_sz,
		&p_qp_ioctl->out, sizeof(p_qp_ioctl->out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(p_qp_ioctl->out) )
	{
		if( pp_send_failure )
			*pp_send_failure = p_send_wr;
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_POST_SEND IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = p_qp_ioctl->out.status;

		if( status != IB_SUCCESS && pp_send_failure )
		{
			/* Get the failed index */
			failed_index = num_wr - p_qp_ioctl->out.failed_cnt;
			p_wr = p_send_wr;
			while( failed_index-- )
				p_wr = p_wr->p_next;

			*pp_send_failure = p_wr;
		}
	}

	cl_free( p_qp_ioctl );
	AL_EXIT( AL_DBG_QP );
	return status;
}


ib_api_status_t
ual_post_recv(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_recv_wr_t*	const		p_recv_wr,
		OUT			ib_recv_wr_t				**pp_recv_failure OPTIONAL )
{
	uintn_t					failed_index;
	uintn_t					bytes_ret;
	uint32_t				num_wr		= 0;
	uint32_t				num_ds		= 0;
	ib_recv_wr_t*			p_wr;
	ib_local_ds_t*			p_ds;
	ual_post_recv_ioctl_t	*p_qp_ioctl;
	size_t					ioctl_buf_sz;
	cl_status_t				cl_status;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	/*
	 * Since the work request is a link list and we need to pass this
	 * to the kernel as a array of work requests.  So first walk through
	 * the list and find out how much memory we need to allocate.
	 */
	for( p_wr = p_recv_wr; p_wr; p_wr = p_wr->p_next )
	{
		num_wr++;

		/* Check for overflow */
		if( !num_wr )
			break;
		if( num_ds > num_ds + p_wr->num_ds )
		{
			num_wr = 0;
			break;
		}

		num_ds += p_wr->num_ds;
	}
	if( !num_wr )
	{
		AL_EXIT( AL_DBG_QP );
		return IB_INVALID_PARAMETER;
	}

	ioctl_buf_sz = sizeof(ual_post_recv_ioctl_t);
	ioctl_buf_sz += sizeof(ib_recv_wr_t) * (num_wr - 1);
	ioctl_buf_sz += sizeof(ib_local_ds_t) * num_ds;

	p_qp_ioctl = (ual_post_recv_ioctl_t*)cl_zalloc( ioctl_buf_sz );
	if( !p_qp_ioctl )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("Failed to allocate IOCTL buffer.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}
	p_ds = (ib_local_ds_t*)&p_qp_ioctl->in.recv_wr[num_wr];

	/* Now populate the ioctl buffer and send down the ioctl */
	p_qp_ioctl->in.h_qp = h_qp->obj.hdl;
	p_qp_ioctl->in.num_wr = num_wr;
	p_qp_ioctl->in.num_ds = num_ds;
	num_wr = 0;
	for( p_wr = p_recv_wr; p_wr; p_wr = p_wr->p_next )
	{
		/* pNext and pDs pointer is set by the kernel proxy. */
		p_qp_ioctl->in.recv_wr[num_wr++] = *p_wr;
		cl_memcpy(
			p_ds, p_wr->ds_array, sizeof(ib_local_ds_t) * p_wr->num_ds );
		p_ds += p_wr->num_ds;
	}

	cl_status = do_al_dev_ioctl( UAL_POST_RECV,
		&p_qp_ioctl->in, ioctl_buf_sz,
		&p_qp_ioctl->out, sizeof(p_qp_ioctl->out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(p_qp_ioctl->out) )
	{
		if( pp_recv_failure )
			*pp_recv_failure = p_recv_wr;

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_POST_RECV IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = p_qp_ioctl->out.status;

		if( status != IB_SUCCESS && pp_recv_failure )
		{
			/* Get the failed index */
			failed_index = num_wr - p_qp_ioctl->out.failed_cnt;
			p_wr = p_recv_wr;
			while( failed_index-- )
				p_wr = p_wr->p_next;

			*pp_recv_failure = p_wr;
		}
	}

	cl_free( p_qp_ioctl );
	AL_EXIT( AL_DBG_QP );
	return status;
}



ib_api_status_t
ual_create_qp(
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_handle_t				h_qp,
	IN		const	ib_qp_create_t* const		p_qp_create,
	IN				ib_qp_attr_t*				p_qp_attr )
{
	/* The first argument is probably not needed */
	ual_create_qp_ioctl_t	qp_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	ib_api_status_t			uvp_status = IB_SUCCESS;
	uvp_interface_t			uvp_intf = h_qp->obj.p_ci_ca->verbs.user_verbs;
	ib_qp_create_t			qp_create;

	AL_ENTER( AL_DBG_QP );
	UNUSED_PARAM( p_qp_attr );

	/* Clear the qp_ioctl */
	cl_memclr( &qp_ioctl, sizeof(qp_ioctl) );

	/* Pre call to the UVP library */
	if( h_pd->h_ci_pd && uvp_intf.pre_create_qp )
	{
		/* The post call MUST exist as it sets the UVP QP handle. */
		CL_ASSERT( uvp_intf.post_create_qp );
		/* Convert the handles to UVP handles */
		qp_create = *p_qp_create;
		qp_create.h_rq_cq = qp_create.h_rq_cq->h_ci_cq;
		qp_create.h_sq_cq = qp_create.h_sq_cq->h_ci_cq;
		if (qp_create.h_srq)
			qp_create.h_srq = qp_create.h_srq->h_ci_srq;
		status = uvp_intf.pre_create_qp( h_pd->h_ci_pd,
			&qp_create, &qp_ioctl.in.umv_buf, &h_qp->h_ci_qp );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_QP );
			return status;
		}
	}
	/*
	 * Convert the handles to KAL handles once again starting
	 * from the input qp attribute
	 */
	qp_ioctl.in.h_pd = h_pd->obj.hdl;
	qp_ioctl.in.qp_create = *p_qp_create;
	qp_ioctl.in.qp_create.h_rq_cq_padding = p_qp_create->h_rq_cq->obj.hdl;
	qp_ioctl.in.qp_create.h_sq_cq_padding = p_qp_create->h_sq_cq->obj.hdl;
	if (p_qp_create->h_srq)
	{
		qp_ioctl.in.qp_create.h_srq_padding = p_qp_create->h_srq->obj.hdl;
	}
	qp_ioctl.in.context = (ULONG_PTR)h_qp;
	qp_ioctl.in.ev_notify = (h_qp->pfn_event_cb != NULL) ? TRUE : FALSE;

	cl_status = do_al_dev_ioctl( UAL_CREATE_QP,
		&qp_ioctl.in, sizeof(qp_ioctl.in), &qp_ioctl.out, sizeof(qp_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(qp_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_CREATE_QP IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = qp_ioctl.out.status;
		
		if( status == IB_SUCCESS )
		{
			h_qp->obj.hdl = qp_ioctl.out.h_qp;
			*p_qp_attr = qp_ioctl.out.attr;
		}
	}

	/* Post uvp call */
	if( h_pd->h_ci_pd && uvp_intf.post_create_qp )
	{
		uvp_status = uvp_intf.post_create_qp( h_pd->h_ci_pd,
			status, &h_qp->h_ci_qp, &qp_ioctl.out.umv_buf );

		if( uvp_intf.post_recv )
		{
			h_qp->h_recv_qp = h_qp->h_ci_qp;
			h_qp->pfn_post_recv = uvp_intf.post_recv;
		}
		else
		{
			h_qp->h_recv_qp = h_qp;
			h_qp->pfn_post_recv = ual_post_recv;
		}

		if( uvp_intf.post_send )
		{
			h_qp->h_send_qp = h_qp->h_ci_qp;
			h_qp->pfn_post_send = uvp_intf.post_send;
		}
		else
		{
			h_qp->h_send_qp = h_qp;
			h_qp->pfn_post_send = ual_post_send;
		}
	}
	else
	{
		h_qp->h_recv_qp = h_qp;
		h_qp->pfn_post_recv = ual_post_recv;
		h_qp->h_send_qp = h_qp;
		h_qp->pfn_post_send = ual_post_send;
	}

	if( (status == IB_SUCCESS) && (uvp_status != IB_SUCCESS) )
		status = uvp_status;

	AL_EXIT( AL_DBG_QP );
	return status;
}



ib_api_status_t
ual_destroy_qp(
	IN			ib_qp_handle_t				h_qp )
{
	ual_destroy_qp_ioctl_t	qp_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_qp->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_QP );

	/* Call the uvp pre call if the vendor library provided a valid QP handle */
	if( h_qp->h_ci_qp && uvp_intf.pre_destroy_qp )
	{
		status = uvp_intf.pre_destroy_qp( h_qp->h_ci_qp );
		if (status != IB_SUCCESS)
		{
			AL_EXIT( AL_DBG_QP );
			return status;
		}
	}

	cl_memclr( &qp_ioctl, sizeof(qp_ioctl) );
	qp_ioctl.in.h_qp = h_qp->obj.hdl;
	cl_status = do_al_dev_ioctl( UAL_DESTROY_QP,
		&qp_ioctl.in, sizeof(qp_ioctl.in), &qp_ioctl.out, sizeof(qp_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(qp_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DESTROY_QP IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = qp_ioctl.out.status;
	}

	/* Call vendor's post_destroy_qp */
	if( h_qp->h_ci_qp && uvp_intf.post_destroy_qp )
		uvp_intf.post_destroy_qp( h_qp->h_ci_qp, status );

	AL_EXIT( AL_DBG_QP );
	return status;
}


ib_api_status_t
ual_modify_qp(
	IN					ib_qp_handle_t				h_qp,
	IN		const		ib_qp_mod_t*		const	p_qp_mod,
	IN					ib_qp_attr_t*				p_qp_attr)
{
	ual_modify_qp_ioctl_t	qp_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_qp->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_QP );

	/* Clear the qp_ioctl */
	cl_memclr( &qp_ioctl, sizeof(qp_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid QP handle */
	if( h_qp->h_ci_qp && uvp_intf.pre_modify_qp )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_modify_qp( h_qp->h_ci_qp,
			p_qp_mod, &qp_ioctl.in.umv_buf );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_QP );
			return status;
		}
	}

	qp_ioctl.in.h_qp = h_qp->obj.hdl;
	qp_ioctl.in.modify_attr = *p_qp_mod;

	cl_status = do_al_dev_ioctl( UAL_MODIFY_QP,
		&qp_ioctl.in, sizeof(qp_ioctl.in), &qp_ioctl.out, sizeof(qp_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(qp_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_MODIFY_QP IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = qp_ioctl.out.status;
	}

	/* Post uvp call */
	if( h_qp->h_ci_qp && uvp_intf.post_modify_qp )
	{
		uvp_intf.post_modify_qp( h_qp->h_ci_qp, status,
			&qp_ioctl.out.umv_buf );
	}

	UNUSED_PARAM( p_qp_attr );
	//if( status == IB_SUCCESS )
	//{
	//	*p_qp_attr = qp_ioctl.out.qp_attr;
	//}

	AL_EXIT( AL_DBG_QP );
	return status;
}


ib_api_status_t
ual_query_qp(
	IN			ib_qp_handle_t				h_qp,
		OUT		ib_qp_attr_t*				p_qp_attr )
{
	ual_query_qp_ioctl_t	qp_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_qp->obj.p_ci_ca->verbs.user_verbs;
	ib_qp_attr_t*			p_attr;
	ib_pd_handle_t			h_ual_pd;

	AL_ENTER( AL_DBG_QP );

	/* Clear the qp_ioctl */
	cl_memclr( &qp_ioctl, sizeof(qp_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( h_qp->h_ci_qp && uvp_intf.pre_query_qp )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_query_qp( h_qp->h_ci_qp, &qp_ioctl.in.umv_buf );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_QP );
			return status;
		}
	}

	qp_ioctl.in.h_qp = h_qp->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_QUERY_QP,
		&qp_ioctl.in, sizeof(qp_ioctl.in), &qp_ioctl.out, sizeof(qp_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(qp_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_QUERY_QP IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = qp_ioctl.out.status;
	}

	p_attr = &qp_ioctl.out.attr;
	/*
	 * Convert the handles in qp_attr to UVP handles
	 */
	h_ual_pd = PARENT_STRUCT( h_qp->obj.p_parent_obj, ib_pd_t, obj );
	p_attr->h_pd = h_ual_pd->h_ci_pd;
	if( h_qp->h_recv_cq )
		p_attr->h_rq_cq = h_qp->h_recv_cq->h_ci_cq;
	if( h_qp->h_send_cq )
		p_attr->h_sq_cq = h_qp->h_send_cq->h_ci_cq;
	if( h_qp->h_srq )
		p_attr->h_srq = h_qp->h_srq->h_ci_srq;

	/* Post uvp call */
	if( h_qp->h_ci_qp && uvp_intf.post_query_qp )
	{
		uvp_intf.post_query_qp( h_qp->h_ci_qp, status,
			p_attr, &qp_ioctl.out.umv_buf );
	}

	if( IB_SUCCESS == status )
	{
		/* UVP handles in qp_attr will be converted to UAL's handles
		 * by the common code
		 */
		*p_qp_attr = *p_attr;
	}

	AL_EXIT( AL_DBG_QP );
	return status;
}


ib_api_status_t
ual_init_qp_alias(
	IN				al_qp_alias_t* const		p_qp_alias,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid,
	IN		const	ib_qp_create_t* const		p_qp_create )
{
	ual_spl_qp_ioctl_t		qp_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_QP );

	CL_ASSERT( p_qp_alias );

	if( h_pd->type != IB_PDT_ALIAS )
	{
		AL_EXIT( AL_DBG_QP );
		return IB_INVALID_PD_HANDLE;
	}

	attach_al_obj( &h_pd->obj, &p_qp_alias->qp.obj );

	switch( p_qp_alias->qp.type )
	{
	case IB_QPT_QP0_ALIAS:
	case IB_QPT_QP1_ALIAS:
		/* Send an ioctl to kernel to get the alias qp */
		cl_memclr( &qp_ioctl, sizeof(qp_ioctl) );
		qp_ioctl.in.h_pd = h_pd->obj.hdl;
		qp_ioctl.in.port_guid = port_guid;
		qp_ioctl.in.qp_create = *p_qp_create;
		qp_ioctl.in.context = (ULONG_PTR)&p_qp_alias->qp;

		cl_status = do_al_dev_ioctl( UAL_GET_SPL_QP_ALIAS,
			&qp_ioctl.in, sizeof(qp_ioctl.in),
			&qp_ioctl.out, sizeof(qp_ioctl.out),
			&bytes_ret );

		if( cl_status != CL_SUCCESS || bytes_ret != sizeof(qp_ioctl.out) )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("UAL_GET_SPL_QP_ALIAS IOCTL returned %s.\n",
				CL_STATUS_MSG(cl_status)) );
			return IB_ERROR;
		}
		else if( qp_ioctl.out.status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_QP );
			return qp_ioctl.out.status;
		}
		p_qp_alias->qp.obj.hdl = qp_ioctl.out.h_qp;
		p_qp_alias->h_mad_disp = NULL;
		break;

	case IB_QPT_MAD:
		/* The MAD QP should have created the MAD dispatcher. */
		CL_ASSERT( p_qp_alias->h_mad_disp );
		break;

	default:
		CL_ASSERT( p_qp_alias->qp.type == IB_QPT_QP0_ALIAS ||
			p_qp_alias->qp.type == IB_QPT_QP1_ALIAS ||
			p_qp_alias->qp.type == IB_QPT_MAD );
		AL_EXIT( AL_DBG_QP );
		return IB_ERROR;
	}


	/* Override function pointers. */
	p_qp_alias->qp.pfn_reg_mad_svc = ual_reg_mad_svc;
	p_qp_alias->qp.pfn_dereg_mad_svc = ual_dereg_mad_svc;

	AL_EXIT( AL_DBG_QP );
	return IB_SUCCESS;
}
