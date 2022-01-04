/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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


#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "direct.tmh"
#endif


/*
* Work Request Processing Verbs.
*/


ib_api_status_t
mlnx_post_send (
	IN	const	ib_qp_handle_t					h_qp,
	IN			ib_send_wr_t					*p_send_wr,
		OUT		ib_send_wr_t					**pp_failed )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_qp *p_ib_qp = (struct ib_qp *)h_qp;
	int acc = 0;

	HCA_ENTER(HCA_DBG_QP);

	if (p_send_wr->wr_type == WR_FAST_REG_MR)
	{
		acc = p_send_wr->fast_reg.access_flags;
		p_send_wr->fast_reg.access_flags = to_qp_acl(p_send_wr->fast_reg.access_flags);
	}
	
	err = p_ib_qp->device->post_send(p_ib_qp, p_send_wr, pp_failed );
	if (err) {
		if (err == -ENOMEM)
			status = IB_INSUFFICIENT_RESOURCES;
		else
			status = errno_to_iberr(err);
	}

	if (p_send_wr->wr_type == WR_FAST_REG_MR)
	{
		p_send_wr->fast_reg.access_flags = acc;
	}

	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_QP,
			("post_send failed with status %x\n", status));
	HCA_EXIT(HCA_DBG_QP);
	return status;
																			
}


ib_api_status_t 
mlnx_post_recv (
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_recv_wr_t				*p_recv_wr,
		OUT			ib_recv_wr_t				**pp_failed OPTIONAL )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_qp *p_ib_qp = (struct ib_qp *)h_qp;
	
	HCA_ENTER(HCA_DBG_QP);

	err = p_ib_qp->device->post_recv(p_ib_qp, p_recv_wr, pp_failed );
	if (err) {
		if (err == -ENOMEM)
			status = IB_INSUFFICIENT_RESOURCES;
		else
			status = errno_to_iberr(err);
	}

	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_QP,
			("post_recv failed with status %x\n", status));
	HCA_EXIT(HCA_DBG_QP);
	return status;
																			
}

ib_api_status_t 
mlnx_post_srq_recv (
	IN		const	ib_srq_handle_t				h_srq,
	IN				ib_recv_wr_t				*p_recv_wr,
		OUT			ib_recv_wr_t				**pp_failed OPTIONAL )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_srq *p_ib_srq = (struct ib_srq *)h_srq;
	
	HCA_ENTER(HCA_DBG_SRQ);

	err = p_ib_srq->device->post_srq_recv(p_ib_srq, p_recv_wr, pp_failed );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("post_srq_recv failed (%d)\n", err));
		if (err == -ENOMEM)
			status = IB_INSUFFICIENT_RESOURCES;
		else
			status = errno_to_iberr(err);
	}

	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("post_srq_recv failed with status %x\n", status));
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}

/*
* Completion Processing and Completion Notification Request Verbs.
*/

ib_api_status_t
mlnx_peek_cq(
	IN		const	ib_cq_handle_t				h_cq,
	OUT				uint32_t* const				p_n_cqes )
{
	int err;
	ib_api_status_t status;
	struct ib_cq *p_ib_cq = (struct ib_cq *)h_cq;

	HCA_ENTER(HCA_DBG_CQ);

	err = p_ib_cq->device->peek_cq ?
		p_ib_cq->device->peek_cq(p_ib_cq, *p_n_cqes) : -ENOSYS;
	status = errno_to_iberr(err);
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("ib_peek_cq failed with status %x\n", status));

	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_poll_cq (
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			ib_wc_t** const				pp_free_wclist,
		OUT			ib_wc_t** const				pp_done_wclist )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_cq *p_ib_cq = (struct ib_cq *)h_cq;

	HCA_ENTER(HCA_DBG_CQ);

	// sanity checks
	if (!pp_free_wclist || !pp_done_wclist || !*pp_free_wclist) {
		status = IB_INVALID_PARAMETER;
		goto err_invalid_params;
	}

	// poll CQ
	err = p_ib_cq->device->poll_cq(p_ib_cq, pp_free_wclist, pp_done_wclist);
	if (err < 0)
		status = errno_to_iberr(err);
	else if (!*pp_done_wclist)
		status = IB_NOT_FOUND;
		
err_invalid_params:	
	if (status != IB_SUCCESS && status  != IB_NOT_FOUND)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("mthca_poll_cq_list failed with status %x\n", status));
	HCA_EXIT(HCA_DBG_CQ);
	return status;
																			
}

ib_api_status_t
mlnx_poll_cq_array (
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			int*						p_num_entries,
		OUT			ib_wc_t*	const			wc )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_cq *p_ib_cq = (struct ib_cq *)h_cq;

	HCA_ENTER(HCA_DBG_CQ);

	err = p_ib_cq->device->x.poll_cq_array(p_ib_cq, *p_num_entries, wc);
	if (err < 0)
		status = errno_to_iberr(err);
	else if (!err)
		status = IB_NOT_FOUND;
	else
		*p_num_entries = err;

	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_enable_cq_notify (
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	boolean_t					solicited )
{
	int err;
	ib_api_status_t status;
	struct ib_cq *p_ib_cq = (struct ib_cq *)h_cq;

	HCA_ENTER(HCA_DBG_CQ);

	err = ib_req_notify_cq(p_ib_cq, 
		(solicited) ? IB_CQ_SOLICITED : IB_CQ_NEXT_COMP );
	status = errno_to_iberr(err);
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("ib_req_notify_cq failed with status %x\n", status));

	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_enable_ncomp_cq_notify (
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	uint32_t					n_cqes )
{
	int err;
	ib_api_status_t status;
	struct ib_cq *p_ib_cq = (struct ib_cq *)h_cq;

	HCA_ENTER(HCA_DBG_CQ);

	err = ib_req_ncomp_notif(p_ib_cq, n_cqes);
	status = errno_to_iberr(err);
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("ib_req_ncomp_notif failed with status %x\n", status));

	HCA_EXIT(HCA_DBG_CQ);
	return status;
}


ib_api_status_t
mlnx_bind_mw (
	IN		const	ib_mw_handle_t				h_mw,
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_bind_wr_t* const			p_mw_bind,
		OUT			net32_t* const				p_rkey )
{
	int err;
	ib_api_status_t status;
	struct ib_mw *p_ib_mw = (struct ib_mw *)h_mw;
	struct ib_qp *p_ib_qp = (struct ib_qp *)h_qp;
	struct ib_mw_bind ib_mw_bind;

	UNUSED_PARAM(p_mw_bind);
	UNUSED_PARAM(p_rkey);
	
	HCA_ENTER(HCA_DBG_MEMORY);

	// TODO: convert ib_bind_wr_t to struct ib_mw_bind
		
	err = p_ib_qp->device->bind_mw ?
		p_ib_qp->device->bind_mw(p_ib_qp, p_ib_mw, &ib_mw_bind) : -ENOSYS;
	status = errno_to_iberr(err);

	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("ib_bind_mw failed with status %x\n", status));
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}


void
mlnx_direct_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->post_send = mlnx_post_send;
	p_interface->post_recv = mlnx_post_recv;
	p_interface->post_srq_recv = mlnx_post_srq_recv;

	p_interface->enable_ncomp_cq_notify = mlnx_enable_ncomp_cq_notify;
	p_interface->peek_cq =  NULL; /* mlnx_peek_cq: Not implemented */
	p_interface->poll_cq = mlnx_poll_cq;
	p_interface->poll_cq_array = mlnx_poll_cq_array;
	p_interface->enable_cq_notify = mlnx_enable_cq_notify;

	p_interface->bind_mw = mlnx_bind_mw;
}
