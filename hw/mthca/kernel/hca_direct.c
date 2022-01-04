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


#include "hca_driver.h"
#include "hca_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_direct.tmh"
#endif
#include "mthca_dev.h"


/* Controls whether to use the VAPI entrypoints in THH, or the IBAL native ones. */
#define MLNX_SEND_NATIVE	1
#define MLNX_RECV_NATIVE	1
#define MLNX_POLL_NATIVE	1


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
	ib_api_status_t 	status;
	struct ib_qp *ib_qp_p = (struct ib_qp *)h_qp;
	struct ib_device *ib_dev = ib_qp_p->device;

	HCA_ENTER(HCA_DBG_QP);
	
	err = ib_dev->post_send(ib_qp_p, p_send_wr, pp_failed );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_QP,
			("post_send failed (%d)\n", err));
		if (err == -ENOMEM)
			status = IB_INSUFFICIENT_RESOURCES;
		else
			status = errno_to_iberr(err);
		goto err_post_send;
	}

	status = IB_SUCCESS;
		
err_post_send: 
		if (status != IB_SUCCESS) 
		{
			HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_QP,
				("completes with ERROR status %#x\n", status));
		}
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
	ib_api_status_t 	status;
	struct ib_qp *ib_qp_p = (struct ib_qp *)h_qp;
	struct ib_device *ib_dev = ib_qp_p->device;
	
	HCA_ENTER(HCA_DBG_QP);

	err = ib_dev->post_recv(ib_qp_p, p_recv_wr, pp_failed );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP,
			("post_recv failed (%d)\n", err));
		if (err == -ENOMEM)
			status = IB_INSUFFICIENT_RESOURCES;
		else
			status = errno_to_iberr(err);
		goto err_post_recv;
	}

	status = IB_SUCCESS;
		
err_post_recv: 
		if (status != IB_SUCCESS) 
		{
			HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_QP,
				("completes with ERROR status %#x\n", status));
		}
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
	ib_api_status_t 	status;
	struct ib_srq *ib_srq_p = (struct ib_srq *)h_srq;
	struct ib_device *ib_dev = ib_srq_p->device;
	
	HCA_ENTER(HCA_DBG_SRQ);

	err = ib_dev->post_srq_recv(ib_srq_p, p_recv_wr, pp_failed );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("post_srq_recv failed (%d)\n", err));
		if (err == -ENOMEM)
			status = IB_INSUFFICIENT_RESOURCES;
		else
			status = errno_to_iberr(err);
		goto err_post_recv;
	}

	status = IB_SUCCESS;
		
err_post_recv: 
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %#x\n", status));
	}
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
	UNREFERENCED_PARAMETER(h_cq);
	UNREFERENCED_PARAMETER(p_n_cqes);
	HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM  ,("mlnx_peek_cq not implemented\n"));
	return IB_INVALID_CA_HANDLE;
}

ib_api_status_t
mlnx_poll_cq (
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			ib_wc_t** const				pp_free_wclist,
		OUT			ib_wc_t** const				pp_done_wclist )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_cq *ib_cq_p = (struct ib_cq *)h_cq;

	HCA_ENTER(HCA_DBG_CQ);

	// sanity checks
	if (!pp_free_wclist || !pp_done_wclist || !*pp_free_wclist) {
		status = IB_INVALID_PARAMETER;
		goto err_invalid_params;
	}

	// poll CQ
	err = mthca_poll_cq_list(ib_cq_p, pp_free_wclist, pp_done_wclist );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_CQ,
			("mthca_poll_cq_list failed (%d)\n", err));
		status = errno_to_iberr(err);
	}else if (!*pp_done_wclist)
		status = IB_NOT_FOUND;
		
err_invalid_params:	
	if (status != IB_SUCCESS && status  != IB_NOT_FOUND)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_CQ);
	return status;
																			
}

ib_api_status_t
mlnx_enable_cq_notify (
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	boolean_t					solicited )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_cq *ib_cq_p = (struct ib_cq *)h_cq;

	HCA_ENTER(HCA_DBG_CQ);

	// REARM CQ
	err = ib_req_notify_cq(ib_cq_p, (solicited) ? IB_CQ_SOLICITED : IB_CQ_NEXT_COMP );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("ib_req_notify_cq failed (%d)\n", err));
		status = errno_to_iberr(err);
	}
		
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_enable_ncomp_cq_notify (
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	uint32_t					n_cqes )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_cq *ib_cq_p = (struct ib_cq *)h_cq;

	HCA_ENTER(HCA_DBG_CQ);

	err = ib_req_ncomp_notif(ib_cq_p, n_cqes );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("ib_req_ncomp_notif failed (%d)\n", err));
		status = errno_to_iberr(err);
	}
		
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("completes with ERROR status %#x\n", status));
	}
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
	UNREFERENCED_PARAMETER(h_mw);
	UNREFERENCED_PARAMETER(h_qp);
	UNREFERENCED_PARAMETER(p_mw_bind);
	UNREFERENCED_PARAMETER(p_rkey);
	HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,("mlnx_bind_mw not implemented\n"));
	return IB_INVALID_CA_HANDLE;
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
	p_interface->enable_cq_notify = mlnx_enable_cq_notify;

	p_interface->bind_mw = mlnx_bind_mw;
}
