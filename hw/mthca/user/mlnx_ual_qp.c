/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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

#include "mt_l2w.h"
#include "mlnx_uvp.h"
#include "mx_abi.h"

#include "mlnx_ual_main.h"
#if defined(EVENT_TRACING)
#include "mlnx_ual_qp.tmh"
#endif

static void
__nd_modify_qp(
	IN		const	ib_qp_handle_t				h_uvp_qp,
	OUT				void**						pp_outbuf,
	OUT				DWORD*					p_size
	)
{
	struct ibv_qp *ibv_qp = (struct ibv_qp *)h_uvp_qp;
	UVP_ENTER(UVP_DBG_QP);
	*(uint32_t**)pp_outbuf = (uint32_t*)&ibv_qp->state;
	*p_size = sizeof(ibv_qp->state);
	UVP_EXIT(UVP_DBG_QP);
}

static ib_qp_state_t __qp_state_to_ibal(enum ibv_qp_state state)
{
	switch ( state ) {
		case IBV_QPS_RESET: return IB_QPS_RESET;
		case IBV_QPS_INIT: return IB_QPS_INIT;
		case IBV_QPS_RTR: return IB_QPS_RTR;
		case IBV_QPS_RTS: return IB_QPS_RTS;
		case IBV_QPS_SQD: return IB_QPS_SQD;
		case IBV_QPS_SQE: return IB_QPS_SQERR;
		case IBV_QPS_ERR: return IB_QPS_ERROR;
		default: return IB_QPS_TIME_WAIT;
	};
}

static uint32_t
__nd_get_qp_state(
	IN		const	ib_qp_handle_t				h_uvp_qp
	)
{
	struct ibv_qp *ibv_qp = (struct ibv_qp *)h_uvp_qp;
	UVP_ENTER(UVP_DBG_QP);
	return __qp_state_to_ibal( ibv_qp->state );
	UVP_EXIT(UVP_DBG_QP);
}

static ib_api_status_t
__pre_create_qp (
	IN		const ib_pd_handle_t		h_uvp_pd,
	IN		const ib_qp_create_t		*p_create_attr,
	IN OUT	ci_umv_buf_t			*p_umv_buf,
	   OUT	ib_qp_handle_t				*ph_uvp_qp)
{
	int err;
	struct ibv_qp *ibv_qp;
	struct ibv_qp_init_attr attr;
	struct ibv_create_qp *p_create_qp;
	ib_api_status_t status = IB_SUCCESS;
	size_t size = max( sizeof(struct ibv_create_qp), sizeof(struct ibv_create_qp_resp) );
	struct ibv_pd *ibv_pd = h_uvp_pd->ibv_pd;

	UVP_ENTER(UVP_DBG_QP);

	CL_ASSERT(p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_zalloc( size );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = sizeof(struct ibv_create_qp);
	p_umv_buf->output_size = sizeof(struct ibv_create_qp_resp);
	p_umv_buf->command = TRUE;

	/* convert attributes */
	attr.send_cq				= (struct ibv_cq *)p_create_attr->h_sq_cq;
	attr.recv_cq				= (struct ibv_cq *)p_create_attr->h_rq_cq;
	attr.srq					= (struct ibv_srq*)p_create_attr->h_srq;
	attr.cap.max_send_wr		= p_create_attr->sq_depth;
	attr.cap.max_recv_wr		= p_create_attr->rq_depth;
	attr.cap.max_send_sge		= p_create_attr->sq_sge;
	attr.cap.max_recv_sge		= p_create_attr->rq_sge;
	attr.cap.max_inline_data	= p_create_attr->sq_max_inline;
	attr.qp_type							= p_create_attr->qp_type;
	attr.sq_sig_all						= p_create_attr->sq_signaled;
	
	/* allocate ibv_qp */
	p_create_qp = (struct ibv_create_qp *)p_umv_buf->p_inout_buf;
	ibv_qp = ibv_pd->context->ops.create_qp_pre(ibv_pd, &attr, p_create_qp);
	if (IS_ERR(ibv_qp)) {
		err = PTR_ERR(ibv_qp);
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("mthca_create_qp_pre failed (%d)\n", err));
		if(err == -ENOMEM && (attr.cap.max_send_sge == 0 ||attr.cap.max_recv_sge == 0|| 
			attr.cap.max_send_wr == 0 || attr.cap.max_recv_wr == 0))
			status = IB_INVALID_SETTING;
		else
			status = errno_to_iberr(err);
		goto err_alloc_qp;
	}

	*ph_uvp_qp = (ib_qp_handle_t) ibv_qp;
	goto end;
		
err_alloc_qp:
	cl_free((void*)(ULONG_PTR)p_umv_buf->p_inout_buf);
err_memory:
end:
		UVP_EXIT(UVP_DBG_QP);
		return status;
}

static ib_api_status_t
__wv_pre_create_qp (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	uvp_qp_create_t			*p_create_attr,
	IN OUT			ci_umv_buf_t			*p_umv_buf,
	    OUT 		ib_qp_handle_t			*ph_uvp_qp)
{
	struct ibv_qp *qp;
	ib_api_status_t status;

	status = __pre_create_qp(h_uvp_pd, &p_create_attr->qp_create,
							 p_umv_buf, ph_uvp_qp);
	if (status == IB_SUCCESS) {
		qp = (struct ibv_qp *) *ph_uvp_qp;
		qp->qp_context = p_create_attr->context;
	}
	return status;
}

static ib_api_status_t
__post_create_qp (
	IN		const ib_pd_handle_t				h_uvp_pd,
	IN				ib_api_status_t 			ioctl_status,
	IN	OUT			ib_qp_handle_t				*ph_uvp_qp,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	int err;
	struct ibv_qp *ibv_qp;
	struct ibv_create_qp_resp *p_resp;
	struct ibv_create_qp *p_create_qp;
	ib_api_status_t status = IB_SUCCESS;
	struct ibv_pd *ibv_pd = h_uvp_pd->ibv_pd;

	UVP_ENTER(UVP_DBG_QP);


	CL_ASSERT(p_umv_buf);
	p_resp = (struct ibv_create_qp_resp *)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status) {

		/* allocate ibv_qp */
		ibv_qp = ibv_pd->context->ops.create_qp_post(ibv_pd, p_resp);
		if (IS_ERR(ibv_qp)) {
			err = PTR_ERR(ibv_qp);
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP , ("mthca_create_qp_post failed (%d)\n", err));
			status = errno_to_iberr(err);
			goto err_create_cq;
		}
	}
	goto end;
	
	ibv_pd->context->ops.destroy_qp(ibv_qp);
err_create_cq:
end:	
	if (p_resp)
		cl_free( p_resp );
	UVP_EXIT(UVP_DBG_QP);
	return status;
}

static ib_api_status_t
__pre_modify_qp (
	IN		const ib_qp_handle_t	h_uvp_qp,
	IN		const ib_qp_mod_t		*p_modify_attr,
	IN OUT	ci_umv_buf_t			*p_umv_buf)
{
	ib_api_status_t status = IB_SUCCESS;
	UNREFERENCED_PARAMETER(h_uvp_qp);
	UNREFERENCED_PARAMETER(p_modify_attr);

	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_zalloc( sizeof(struct ibv_modify_qp_resp) );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = 0;
	p_umv_buf->output_size = sizeof(struct ibv_modify_qp_resp);
	p_umv_buf->command = TRUE;
	
err_memory:
	UVP_EXIT(UVP_DBG_SHIM);
	return status;
}


static void
__post_modify_qp (
	IN		const ib_qp_handle_t	h_uvp_qp,
	IN		ib_api_status_t			ioctl_status,
	IN OUT	ci_umv_buf_t			*p_umv_buf)
{
	int err;
	ib_api_status_t status;
	struct ibv_modify_qp_resp *p_resp; 
	struct ibv_qp_attr attr;
	struct ibv_qp *ibv_qp = (struct ibv_qp *)h_uvp_qp;

	UVP_ENTER(UVP_DBG_SHIM);
	CL_ASSERT(p_umv_buf);

	p_resp = (struct ibv_modify_qp_resp *)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status) 
	{
		memset( &attr, 0, sizeof(attr));
		attr.qp_state = p_resp->qp_state;
		if (ibv_qp) {
			err = ibv_qp->context->ops.modify_qp(	ibv_qp,
													&attr, p_resp->attr_mask);
			if (err) {
				UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_SHIM , ("mthca_modify_qp failed (%d)\n", err));
				status = errno_to_iberr(err);
				goto err_modify_qp;
			}
		}
		UVP_PRINT(TRACE_LEVEL_INFORMATION ,UVP_DBG_SHIM ,
		     	("Committed to modify QP to state %d\n", p_resp->qp_state));
	}


err_modify_qp:
	if (p_resp)
		cl_free (p_resp);
	UVP_EXIT(UVP_DBG_SHIM);
	return;
	}


static ib_api_status_t
__pre_query_qp (
	IN		ib_qp_handle_t				h_uvp_qp,
	IN OUT	ci_umv_buf_t				*p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_qp);
	UVP_ENTER(UVP_DBG_SHIM);
	p_umv_buf->input_size = p_umv_buf->output_size = 0;
	p_umv_buf->command = FALSE;
	p_umv_buf->status = IB_SUCCESS;
	UVP_EXIT(UVP_DBG_SHIM);
	return IB_SUCCESS;
}


static void
__post_query_qp (
	IN				ib_qp_handle_t				h_uvp_qp,
	IN				ib_api_status_t				ioctl_status,
	IN	OUT			ib_qp_attr_t				*p_query_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf)
{
	struct mthca_qp *p_mthca_qp = (struct mthca_qp *)h_uvp_qp;
	UVP_ENTER(UVP_DBG_SHIM);

	UNREFERENCED_PARAMETER(p_umv_buf);
	if(IB_SUCCESS == ioctl_status)
	{
		p_query_attr->sq_max_inline = p_mthca_qp->max_inline_data;
		p_query_attr->sq_sge = p_mthca_qp->sq.max_gs;
		p_query_attr->sq_depth = p_mthca_qp->sq.max;
		p_query_attr->rq_sge = p_mthca_qp->rq.max_gs;
		p_query_attr->rq_depth = p_mthca_qp->rq.max;
	}
	UVP_EXIT(UVP_DBG_SHIM);
}


static ib_api_status_t
__pre_destroy_qp (
    IN		const ib_qp_handle_t		h_uvp_qp)
{
	int err;


	UVP_ENTER(UVP_DBG_SHIM);

	mthca_destroy_qp_pre((struct ibv_qp*)h_uvp_qp);

	UVP_EXIT(UVP_DBG_SHIM);
	return IB_SUCCESS;
}

static void
__post_destroy_qp (
	IN		const ib_qp_handle_t	h_uvp_qp,
    IN		ib_api_status_t			ioctl_status)
{
	int err;

	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(h_uvp_qp);

	mthca_destroy_qp_post((struct ibv_qp*)h_uvp_qp, (int)ioctl_status);
	if (ioctl_status != IB_SUCCESS) 
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_SHIM , ("mthca_destroy_qp_post failed (%d)\n", ioctl_status));

	UVP_EXIT(UVP_DBG_SHIM);
	return;
}


void
mlnx_get_qp_interface (
    IN OUT	uvp_interface_t		*p_uvp )
{
    UVP_ENTER(UVP_DBG_SHIM);

    CL_ASSERT(p_uvp);

    /*
     * QP Management Verbs
     */
    p_uvp->pre_create_qp   = __pre_create_qp;
    p_uvp->post_create_qp  = __post_create_qp;

    // !!! none for create_spl_qp, UAL will return error !!!

    p_uvp->pre_modify_qp   = __pre_modify_qp;
    p_uvp->post_modify_qp  = __post_modify_qp;
    p_uvp->pre_query_qp    = NULL;
    p_uvp->post_query_qp   = __post_query_qp;
    p_uvp->pre_destroy_qp  = __pre_destroy_qp;
    p_uvp->post_destroy_qp = __post_destroy_qp;

    p_uvp->nd_modify_qp   = __nd_modify_qp;
	p_uvp->nd_get_qp_state = __nd_get_qp_state;
	p_uvp->wv_pre_create_qp = __wv_pre_create_qp;

    UVP_EXIT(UVP_DBG_SHIM);
}



