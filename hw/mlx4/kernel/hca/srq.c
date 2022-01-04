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
#include "srq.tmh"
#endif


ib_api_status_t
mlnx_create_srq (
	IN		const	ib_pd_handle_t			h_pd,
	IN		const	void						*srq_context,
	IN				ci_async_event_cb_t			event_handler,
	IN		const	ib_srq_attr_t * const		p_srq_attr,
		OUT			ib_srq_handle_t			*ph_srq,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status;
	struct ib_srq *p_ib_srq;
	struct ib_srq_init_attr srq_init_attr;
	struct ib_ucontext *p_uctx = NULL;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;

	HCA_ENTER(HCA_DBG_SRQ);

	if( p_umv_buf  && p_umv_buf->command) {

		// sanity checks 
		if (p_umv_buf->input_size < sizeof(struct ibv_create_srq) ||
			p_umv_buf->output_size < sizeof(struct ibv_create_srq_resp) ||
			!p_umv_buf->p_inout_buf) {
			status = IB_INVALID_PARAMETER;
			goto err_inval_params;
		}
		p_uctx = p_ib_pd->p_uctx;
	}

	// prepare the parameters
	RtlZeroMemory(&srq_init_attr, sizeof(srq_init_attr));
	srq_init_attr.event_handler = event_handler;
	srq_init_attr.srq_context = (void*)srq_context;
	srq_init_attr.attr.max_wr = p_srq_attr->max_wr;
	srq_init_attr.attr.max_sge = p_srq_attr->max_sge;
	srq_init_attr.attr.srq_limit = p_srq_attr->srq_limit;

	// allocate srq	
	p_ib_srq = ibv_create_srq(p_ib_pd, &srq_init_attr, p_uctx, p_umv_buf );
	if (IS_ERR(p_ib_srq)) {
		err = PTR_ERR(p_ib_srq);
		HCA_PRINT (TRACE_LEVEL_ERROR ,HCA_DBG_SRQ, ("ibv_create_srq failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_create_srq;
	}

	// return the result
	if (ph_srq) *ph_srq = (ib_srq_handle_t)p_ib_srq;

	status = IB_SUCCESS;
	
err_create_srq:
err_inval_params:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}


ib_api_status_t
mlnx_modify_srq (
		IN		const	ib_srq_handle_t 			h_srq,
		IN		const	ib_srq_attr_t* const			p_srq_attr,
		IN		const	ib_srq_attr_mask_t			srq_attr_mask,
		IN	OUT 		ci_umv_buf_t				*p_umv_buf OPTIONAL )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_srq *p_ib_srq = (struct ib_srq *)h_srq;
	UNUSED_PARAM(p_umv_buf);

	HCA_ENTER(HCA_DBG_SRQ);

	err = p_ib_srq->device->modify_srq(p_ib_srq, (void*)p_srq_attr, srq_attr_mask, NULL);
	status = errno_to_iberr(err);

	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}

ib_api_status_t
mlnx_query_srq (
	IN		const	ib_srq_handle_t				h_srq,
		OUT			ib_srq_attr_t* const			p_srq_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf OPTIONAL )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_srq *p_ib_srq = (struct ib_srq *)h_srq;
	UNUSED_PARAM(p_umv_buf);

	HCA_ENTER(HCA_DBG_SRQ);

	err = p_ib_srq->device->query_srq(p_ib_srq, (void*)p_srq_attr);
	status = errno_to_iberr(err);

	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}

ib_api_status_t
mlnx_destroy_srq (
	IN	const	ib_srq_handle_t		h_srq )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_srq *p_ib_srq = (struct ib_srq *)h_srq;

	HCA_ENTER(HCA_DBG_SRQ);

	err = ib_destroy_srq(p_ib_srq);
	status = errno_to_iberr(err);

	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}

void
mlnx_srq_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->create_srq = mlnx_create_srq;
	p_interface->modify_srq = mlnx_modify_srq;
	p_interface->query_srq = mlnx_query_srq;
	p_interface->destroy_srq = mlnx_destroy_srq;
}

