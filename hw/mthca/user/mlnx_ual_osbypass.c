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
#include "mlnx_ual_osbypass.tmh"
#endif

static ib_api_status_t __to_status(int err)
{
	ib_api_status_t status;
	
	switch (err) {
		case -ENOMEM: status = IB_INSUFFICIENT_RESOURCES; break;
		case -EINVAL: status = IB_INVALID_WR_TYPE; break;
		case -ERANGE: status = IB_INVALID_MAX_SGE; break;
		case -EBUSY: status = IB_INVALID_QP_STATE; break;
		case -E2BIG: status = IB_INVALID_PARAMETER; break;
		default: status = errno_to_iberr(err);
	}
	return status;
}

static ib_api_status_t
__post_send (
	IN		const	void*						h_qp,
	IN				ib_send_wr_t*	const		p_send_wr,
		OUT			ib_send_wr_t**				pp_send_failure )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct mthca_qp *qp = (struct mthca_qp *) ((void*)h_qp);

	UVP_ENTER(UVP_DBG_QP);

	CL_ASSERT (qp);

	CL_ASSERT( p_send_wr );

	err = qp->ibv_qp.context->ops.post_send(&qp->ibv_qp, p_send_wr, pp_send_failure );

	if (err) {
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP , ("mthca_post_send failed (%d)\n", err));
		status = __to_status(err);
	}
	

	UVP_EXIT(UVP_DBG_QP);
	return status;
}

static ib_api_status_t
__post_recv (
	IN		const	void*						h_qp,
	IN				ib_recv_wr_t*	const		p_recv_wr,
		OUT			ib_recv_wr_t**				pp_recv_failure )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct mthca_qp *qp = (struct mthca_qp *) ((void*)h_qp);

	UVP_ENTER(UVP_DBG_QP);

	CL_ASSERT (qp);

	CL_ASSERT( p_recv_wr );

	err = qp->ibv_qp.context->ops.post_recv(&qp->ibv_qp, p_recv_wr, pp_recv_failure );

	if (err) {
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP, ("mthca_post_recv failed (%d)\n", err));
		status = __to_status(err);
	}

	UVP_EXIT(UVP_DBG_QP);
	return status;
}


static ib_api_status_t
__post_srq_recv (
	IN		const	void*						h_srq,
	IN				ib_recv_wr_t*	const		p_recv_wr,
		OUT			ib_recv_wr_t**				pp_recv_failure )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct mthca_srq *srq = (struct mthca_srq *) ((void*)h_srq);

	UVP_ENTER(UVP_DBG_QP);

	CL_ASSERT (srq);

	CL_ASSERT( p_recv_wr );

	err = srq->ibv_srq.context->ops.post_srq_recv(&srq->ibv_srq, p_recv_wr, pp_recv_failure );
	if (err) {
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP, ("mthca_post_recv failed (%d)\n", err));
		status = __to_status(err);
	}

	UVP_EXIT(UVP_DBG_QP);
	return status;
}


static ib_api_status_t
__poll_cq (
	IN		const	void*						h_cq,
	IN	OUT			ib_wc_t**	const			pp_free_wclist,
		OUT			ib_wc_t**	const			pp_done_wclist )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct mthca_cq *cq = (struct mthca_cq *) ((void*)h_cq);

	UVP_ENTER(UVP_DBG_CQ);
	CL_ASSERT (cq);

	if (!pp_free_wclist || !*pp_free_wclist || !pp_done_wclist)
	{
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_CQ ,("Passed in bad params\n")); 
		status = IB_INVALID_PARAMETER;
		goto err_invalid_params;
	}

	err = cq->ibv_cq.context->ops.poll_cq_list(&cq->ibv_cq, pp_free_wclist, pp_done_wclist );
	if (err) {
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_CQ , ("mthca_poll_cq failed (%d)\n", err));
			status = errno_to_iberr(err);
	}else if (!*pp_done_wclist)
			status = IB_NOT_FOUND;
	

err_invalid_params:

	if (status != IB_NOT_FOUND){
		UVP_PRINT_EXIT(TRACE_LEVEL_ERROR ,UVP_DBG_CQ  ,("completes with ERROR status %s\n", ib_get_err_str(status)));
	}else
		UVP_EXIT(UVP_DBG_CQ);

    return status;
}


static int
__poll_cq_array (
	IN		const	void*						h_cq,
	IN		const	int							num_entries,
	IN	OUT			uvp_wc_t*	const			wc )
{
	int ne;
	struct mthca_cq *cq = (struct mthca_cq *) h_cq;

	UVP_ENTER(UVP_DBG_CQ);
	CL_ASSERT (cq);

	ne = cq->ibv_cq.context->ops.poll_cq(&cq->ibv_cq, num_entries, wc);

	UVP_EXIT(UVP_DBG_CQ);
	return ne;
}


static ib_api_status_t
__enable_cq_notify (
	IN		const	void*						h_cq,
	IN		const	boolean_t					solicited )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct mthca_cq *cq = (struct mthca_cq *) ((void*)h_cq);

	UVP_ENTER(UVP_DBG_CQ);
	CL_ASSERT (cq);

	err = cq->ibv_cq.context->ops.req_notify_cq(&cq->ibv_cq, (solicited) ? IB_CQ_SOLICITED : IB_CQ_NEXT_COMP );
	if (err) {
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_SHIM , ("mthca_enable_cq_notify failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto exit;
	}

exit:
		UVP_EXIT(UVP_DBG_CQ);
		return status;
}


static ib_api_status_t
__enable_ncomp_cq_notify (
	IN		const	void*						h_cq,
	IN		const	uint32_t					n_cqes )
{
	// Not yet implemented
	ib_api_status_t status = IB_UNSUPPORTED;
	UVP_ENTER(UVP_DBG_SHIM);
	UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_SHIM , ("__enable_ncomp_cq_notify is not implemented yet\n"));
	UVP_EXIT(UVP_DBG_SHIM);
	return status;
}


void
mlnx_get_osbypass_interface (
    IN OUT	uvp_interface_t		*p_uvp )
{
    CL_ASSERT(p_uvp);

    p_uvp->post_send = __post_send;
    p_uvp->post_recv = __post_recv;
    p_uvp->post_srq_recv = __post_srq_recv;
    p_uvp->poll_cq  = __poll_cq;
    p_uvp->rearm_cq = __enable_cq_notify;
    p_uvp->rearm_n_cq = NULL;
    p_uvp->peek_cq  = NULL;
    p_uvp->bind_mw = NULL;
	p_uvp->poll_cq_array = __poll_cq_array;
}

