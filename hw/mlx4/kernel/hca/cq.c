/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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
#include "cq.tmh"
#endif

ib_api_status_t
mlnx_create_cq (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	void						*cq_context,
	IN				ci_async_event_cb_t			event_handler,
	IN				ci_completion_cb_t			cq_comp_handler,
	IN	OUT			uint32_t					*p_size,
		OUT			ib_cq_handle_t				*ph_cq,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status;
	struct ib_cq *p_ib_cq;
	mlnx_hca_t *p_hca;
	struct ib_device *p_ibdev;
	struct ib_ucontext *p_uctx;

	HCA_ENTER(HCA_DBG_CQ);

	if( p_umv_buf ) {

		p_uctx = (struct ib_ucontext *)h_ca;
		p_ibdev = p_uctx->device;
		p_hca = ibdev2hca(p_ibdev);

		if( p_umv_buf  && p_umv_buf->command) {
			// sanity checks 
			if (p_umv_buf->input_size < sizeof(struct ibv_create_cq) ||
				p_umv_buf->output_size < sizeof(struct ibv_create_cq_resp) ||
				!p_umv_buf->p_inout_buf) {
				status = IB_INVALID_PARAMETER;
				goto err_inval_params;
			}
		}
	}
	else {
		p_uctx = NULL;
		p_hca = (mlnx_hca_t *)h_ca;
		p_ibdev = hca2ibdev(p_hca);
	}

	/* sanity check */
	if (!*p_size || (*p_size & ~IB_CQ_PRINT_CQE_FLAG) > (uint32_t)hca2mdev(p_hca)->caps.max_cqes) {
		status = IB_INVALID_CQ_SIZE;
		goto err_cqe;
	}

	// allocate cq	
	p_ib_cq = ibv_create_cq(p_ibdev, 
		cq_comp_handler, event_handler,
		(void*)cq_context, *p_size, p_uctx, p_umv_buf );
	if (IS_ERR(p_ib_cq)) {
		err = PTR_ERR(p_ib_cq);
		HCA_PRINT (TRACE_LEVEL_ERROR ,HCA_DBG_CQ, ("ibv_create_cq failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_create_cq;
	}

	*ph_cq = (ib_cq_handle_t)p_ib_cq;

	status = IB_SUCCESS;
	
err_create_cq:
err_inval_params:
err_cqe:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("completes with ERROR status %x\n", status));
	}
	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_create_cq_ex (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	void						*cq_context,
	IN				ci_async_event_cb_t			event_handler,
	IN				ci_completion_cb_t			cq_comp_handler,
	IN				ib_group_affinity_t         *affinity,
	IN	OUT			uint32_t					*p_size,
		OUT			ib_cq_handle_t				*ph_cq,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status;
	struct ib_cq *p_ib_cq;
	mlnx_hca_t *p_hca;
	struct ib_device *p_ibdev;
	struct ib_ucontext *p_uctx;

	HCA_ENTER(HCA_DBG_CQ);

	if( p_umv_buf ) {

		p_uctx = (struct ib_ucontext *)h_ca;
		p_ibdev = p_uctx->device;
		p_hca = ibdev2hca(p_ibdev);

		if( p_umv_buf  && p_umv_buf->command) {
			// sanity checks 
			if (p_umv_buf->input_size < sizeof(struct ibv_create_cq) ||
				p_umv_buf->output_size < sizeof(struct ibv_create_cq_resp) ||
				!p_umv_buf->p_inout_buf) {
				status = IB_INVALID_PARAMETER;
				goto err_inval_params;
			}
		}
	}
	else {
		p_uctx = NULL;
		p_hca = (mlnx_hca_t *)h_ca;
		p_ibdev = hca2ibdev(p_hca);
	}

	/* sanity check */
	if (!*p_size || (*p_size & ~IB_CQ_PRINT_CQE_FLAG) > (uint32_t)hca2mdev(p_hca)->caps.max_cqes) {
		status = IB_INVALID_CQ_SIZE;
		goto err_cqe;
	}

	// allocate cq	
	p_ib_cq = ibv_create_cq_ex(p_ibdev, 
		cq_comp_handler, event_handler,
		(void*)cq_context, affinity, *p_size, p_uctx, p_umv_buf );
	if (IS_ERR(p_ib_cq)) {
		err = PTR_ERR(p_ib_cq);
		HCA_PRINT (TRACE_LEVEL_ERROR ,HCA_DBG_CQ, ("ibv_create_cq failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_create_cq;
	}

	// return the result
	*p_size = p_ib_cq->cqe;

	if (ph_cq) *ph_cq = (ib_cq_handle_t)p_ib_cq;

	status = IB_SUCCESS;
	
err_create_cq:
err_inval_params:
err_cqe:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("completes with ERROR status %x\n", status));
	}
	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_resize_cq (
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			uint32_t					*p_size,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_cq *p_ib_cq = (struct ib_cq *)h_cq;
	struct ib_device *p_ibdev = p_ib_cq->device;

	UNUSED_PARAM(p_umv_buf);
	
	HCA_ENTER(HCA_DBG_CQ);

	if (p_ibdev->resize_cq) {
		err = p_ibdev->resize_cq(p_ib_cq, *p_size, NULL);
		if (err) {
			HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM,
				("ib_resize_cq failed (%d)\n", err));
			status = errno_to_iberr(err);
		}
	}
	else
		status = IB_UNSUPPORTED;
	
	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_modify_cq (
	IN		const	ib_cq_handle_t				h_cq,
	IN 		uint16_t 							moder_cnt,
	IN      uint16_t 							moder_time,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_cq *p_ib_cq = (struct ib_cq *)h_cq;
	struct ib_device *p_ibdev = p_ib_cq->device;

	UNUSED_PARAM(p_umv_buf);
	
	HCA_ENTER(HCA_DBG_CQ);

	if (p_ibdev->modify_cq) {
		err = p_ibdev->modify_cq(p_ib_cq, moder_cnt, moder_time);
		if (err) {
			HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM,
				("ib_modify_cq failed (%d)\n", err));
			status = errno_to_iberr(err);
		}
	}
	else
		status = IB_UNSUPPORTED;
	
	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_query_cq (
	IN		const	ib_cq_handle_t				h_cq,
		OUT			uint32_t					*p_size,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	UNREFERENCED_PARAMETER(h_cq);
	UNREFERENCED_PARAMETER(p_size);
	if (p_umv_buf && p_umv_buf->command) {
		p_umv_buf->status = IB_UNSUPPORTED;
	}
	HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,("mlnx_query_cq not supported\n"));
	return IB_UNSUPPORTED;
}

ib_api_status_t
mlnx_destroy_cq (
	IN		const	ib_cq_handle_t				h_cq)
{
																				
	ib_api_status_t 	status;
	int err;
	struct ib_cq *p_ib_cq = (struct ib_cq *)h_cq;

	HCA_ENTER( HCA_DBG_QP);

	HCA_PRINT(TRACE_LEVEL_INFORMATION,HCA_DBG_CQ,
		("cqn %#x, pcs %p\n", ((struct mlx4_ib_cq*)p_ib_cq)->mcq.cqn, PsGetCurrentProcess()) );

	// destroy CQ
	err = ib_destroy_cq( p_ib_cq );
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR ,HCA_DBG_SHIM,
			("ibv_destroy_cq failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_destroy_cq;
	}

	status = IB_SUCCESS;

err_destroy_cq:
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_CQ);
	return status;
}


	

void
mlnx_cq_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->create_cq = mlnx_create_cq;
	p_interface->create_cq_ex = mlnx_create_cq_ex;
	p_interface->resize_cq = mlnx_resize_cq;
	p_interface->modify_cq = mlnx_modify_cq;
	p_interface->query_cq = mlnx_query_cq;
	p_interface->destroy_cq = mlnx_destroy_cq;
}

