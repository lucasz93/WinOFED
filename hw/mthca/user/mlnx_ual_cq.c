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
#include "mlnx_ual_main.h"
#include "mlnx_uvp.h"
#include "mx_abi.h"

#if defined(EVENT_TRACING)
#include "mlnx_ual_cq.tmh"
#endif


extern uint32_t	mlnx_dbg_lvl;

static ib_api_status_t
__pre_create_cq (
		IN		const ib_ca_handle_t				h_uvp_ca,
		IN	OUT		uint32_t*			const		p_size,
		IN	OUT 		ci_umv_buf_t				*p_umv_buf,
			OUT			ib_cq_handle_t				*ph_uvp_cq)
{
	struct ibv_cq *ibv_cq;
	ib_api_status_t status = IB_SUCCESS;
	size_t size = max( sizeof(struct ibv_create_cq), sizeof(struct ibv_create_cq_resp) );
	mlnx_ual_hobul_t *p_hobul = (mlnx_ual_hobul_t *)((void *)h_uvp_ca);
	struct ibv_create_cq *p_create_cq;
	int err;

	UVP_ENTER(UVP_DBG_CQ);

	CL_ASSERT(p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_zalloc( size );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = sizeof(struct ibv_create_cq);
	p_umv_buf->output_size = sizeof(struct ibv_create_cq_resp);
	p_umv_buf->command = TRUE;

	/* allocate ibv_cq */
	p_create_cq = (struct ibv_create_cq *)p_umv_buf->p_inout_buf;
	ibv_cq = p_hobul->ibv_ctx->ops.create_cq_pre(p_hobul->ibv_ctx, p_size, p_create_cq);
	if (IS_ERR(ibv_cq)) {
		err = PTR_ERR(ibv_cq);
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_CQ , ("mthca_alloc_cq_pre failed (%d)\n", err));
		status = (err == -ENOMEM) ? IB_INVALID_CQ_SIZE : errno_to_iberr(err);
		goto err_alloc_cq;
	}

	*ph_uvp_cq = (ib_cq_handle_t)(ULONG_PTR)p_create_cq->user_handle;
	goto end;
		
err_alloc_cq:
	cl_free((void*)(ULONG_PTR)p_umv_buf->p_inout_buf);
err_memory:
end:
	UVP_EXIT(UVP_DBG_CQ);
	return status;
}


static void
__post_create_cq (
	IN		const	ib_ca_handle_t				h_uvp_ca,
	IN				ib_api_status_t				ioctl_status,
	IN		const	uint32_t					size,
	IN	OUT			ib_cq_handle_t				*ph_uvp_cq,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ibv_create_cq_resp *p_resp;
	struct ibv_cq *ibv_cq;
	mlnx_ual_hobul_t *p_hobul = (mlnx_ual_hobul_t *)((void *)h_uvp_ca);


	UVP_ENTER(UVP_DBG_CQ);

	CL_ASSERT(p_hobul);
	CL_ASSERT(p_umv_buf);
	p_resp = (struct ibv_create_cq_resp *)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status) {

		/* allocate ibv_cq */
		ibv_cq = p_hobul->ibv_ctx->ops.create_cq_post(p_hobul->ibv_ctx, p_resp);
		if (IS_ERR(ibv_cq)) {
			err = PTR_ERR(ibv_cq);
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_CQ , ("mthca_create_cq failed (%d)\n", err));
			status = errno_to_iberr(err);
			goto err_create_cq;
		}

		*ph_uvp_cq = (ib_cq_handle_t)ibv_cq;
	}
	else {
		ibv_cq = (struct ibv_cq *)*ph_uvp_cq;
		ibv_cq->context = h_uvp_ca->ibv_ctx;
		ibv_cq->context->ops.destroy_cq( ibv_cq );
		*ph_uvp_cq = NULL;
	}

err_create_cq:
	if (p_resp)
		cl_free( p_resp );
	UVP_EXIT(UVP_DBG_CQ);
	return;
}


static ib_api_status_t
__pre_query_cq (
	IN		const	ib_cq_handle_t		h_uvp_cq,
		OUT			uint32_t* const		p_size,
	IN	OUT			ci_umv_buf_t		*p_umv_buf)
{
	struct ibv_cq *ibv_cq = (struct ibv_cq *)h_uvp_cq;

	UVP_ENTER(UVP_DBG_CQ);

	*p_size = ibv_cq->cqe;

	UVP_EXIT(UVP_DBG_CQ);
	return IB_VERBS_PROCESSING_DONE;
}


static ib_api_status_t
__pre_destroy_cq (
	IN		const ib_cq_handle_t			h_uvp_cq)
{
	UVP_ENTER(UVP_DBG_CQ);
	UVP_EXIT(UVP_DBG_CQ);
	return IB_SUCCESS;
}

static void
__post_destroy_cq (
	IN		const ib_cq_handle_t	h_uvp_cq,
	IN		ib_api_status_t			ioctl_status)
{
	int err;
	struct ibv_cq *ibv_cq = (struct ibv_cq *)h_uvp_cq;
	UNREFERENCED_PARAMETER(ioctl_status);

	UVP_ENTER(UVP_DBG_CQ);

	CL_ASSERT(ibv_cq);

	if (IB_SUCCESS == ioctl_status) {
		err = ibv_cq->context->ops.destroy_cq( ibv_cq );
		if (err) 
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_CQ, ("mthca_destroy_cq failed (%d)\n", err));
		//cl_free (p_cq_info);
	}

	UVP_EXIT(UVP_DBG_CQ);
}

void
mlnx_get_cq_interface (
	IN OUT	uvp_interface_t		*p_uvp )
{
	UVP_ENTER(UVP_DBG_DEV);

	CL_ASSERT(p_uvp);

	/*
	 * Completion Queue Management Verbs
	 */
	p_uvp->pre_create_cq  = __pre_create_cq;
	p_uvp->post_create_cq = __post_create_cq;

	p_uvp->pre_query_cq  = __pre_query_cq;
	p_uvp->post_query_cq = NULL;

	p_uvp->pre_resize_cq  = NULL; /* __pre_resize_cq: not supported in kernel */
	p_uvp->post_resize_cq = NULL;	/* __post_resize_cq:not supported in kernel */ 

	p_uvp->pre_destroy_cq  = __pre_destroy_cq;
	p_uvp->post_destroy_cq = __post_destroy_cq;

	UVP_EXIT(UVP_DBG_DEV);
}


