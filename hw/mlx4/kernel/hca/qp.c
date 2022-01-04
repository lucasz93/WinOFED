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

#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "qp.tmh"
#endif


ib_api_status_t
mlnx_query_qp (
	IN		const	ib_qp_handle_t				h_qp,
		OUT			ib_qp_attr_t				*p_qp_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t 	status;
	struct ib_qp *p_ib_qp = (struct ib_qp *)h_qp;
	struct ib_qp_attr qp_attr;
	struct ib_qp_init_attr qp_init_attr;
	int qp_attr_mask = 0;
	int err;

	UNREFERENCED_PARAMETER(p_umv_buf);
	
	HCA_ENTER( HCA_DBG_QP);

	// sanity checks
	if (!p_qp_attr) {
		status =  IB_INVALID_PARAMETER;
		goto err_parm;
	}

	// convert structures
	memset( &qp_attr, 0, sizeof(struct ib_qp_attr) );
	err = p_ib_qp->device->query_qp( p_ib_qp, &qp_attr, 
		qp_attr_mask, &qp_init_attr);
	if (err){
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_PD,
			("ib_query_qp failed (%#x)\n", status));
		goto err_query_qp;
	}

	status = from_qp_attr( p_ib_qp, &qp_attr, p_qp_attr );

err_query_qp:
err_parm:
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

static ib_api_status_t
__create_qp (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	uint8_t						port_num,
	IN		const	void						*qp_uctx,
	IN				ci_async_event_cb_t			event_handler,
	IN	OUT			ib_qp_create_t				*p_create_attr,
	IN				int							create_flags,
		OUT			ib_qp_attr_t				*p_qp_attr,
		OUT			ib_qp_handle_t				*ph_qp,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status;
	struct ib_qp * p_ib_qp;
	struct ib_qp_init_attr qp_init_attr;
	struct ib_ucontext *p_uctx = NULL;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;
	struct ibv_create_qp *p_req = NULL;
	
	HCA_ENTER(HCA_DBG_QP);

	if( p_umv_buf && p_umv_buf->command ) {
		// sanity checks 
		if (p_umv_buf->input_size < sizeof(struct ibv_create_qp) ||
			p_umv_buf->output_size < sizeof(struct ibv_create_qp_resp) ||
			!p_umv_buf->p_inout_buf) {
			status = IB_INVALID_PARAMETER;
			goto err_inval_params;
		}
		p_req = (struct ibv_create_qp*)(ULONG_PTR)p_umv_buf->p_inout_buf;
		p_uctx = p_ib_pd->p_uctx;
	}

	// prepare the parameters
	RtlZeroMemory(&qp_init_attr, sizeof(qp_init_attr));
	qp_init_attr.event_handler = event_handler;
	qp_init_attr.qp_context = (void*)qp_uctx;
	qp_init_attr.send_cq = (struct ib_cq *)p_create_attr->h_sq_cq;
	qp_init_attr.recv_cq = (struct ib_cq *)p_create_attr->h_rq_cq;
	qp_init_attr.srq = (struct ib_srq *)p_create_attr->h_srq;
		qp_init_attr.cap.max_recv_sge = p_create_attr->rq_sge;
		qp_init_attr.cap.max_send_sge = p_create_attr->sq_sge;
		qp_init_attr.cap.max_recv_wr = p_create_attr->rq_depth;
		qp_init_attr.cap.max_send_wr = p_create_attr->sq_depth;
		qp_init_attr.cap.max_inline_data = p_create_attr->sq_max_inline;
		
	qp_init_attr.sq_sig_type = (p_create_attr->sq_signaled) ? IB_SIGNAL_ALL_WR : IB_SIGNAL_REQ_WR;
	qp_init_attr.qp_type = to_qp_type(p_create_attr->qp_type);
	qp_init_attr.port_num = port_num;
	qp_init_attr.create_flags |= create_flags | IB_QP_CREATE_IPOIB_UD_LSO;

	// create qp		
	p_ib_qp = ibv_create_qp( p_ib_pd, &qp_init_attr, p_uctx, p_umv_buf );
	if (IS_ERR(p_ib_qp)) {
		err = PTR_ERR(p_ib_qp);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_QP,
			("ibv_create_qp failed (%d)\n", err));
		if ( err == -ERANGE ) {
			p_create_attr->rq_sge			= qp_init_attr.cap.max_recv_sge;
			p_create_attr->sq_sge			= qp_init_attr.cap.max_send_sge;
			p_create_attr->rq_depth			= qp_init_attr.cap.max_recv_wr;
			p_create_attr->sq_depth			= qp_init_attr.cap.max_send_wr;
			p_create_attr->sq_max_inline	= qp_init_attr.cap.max_inline_data;
		}
		status = errno_to_iberr(err);
		goto err_create_qp;
	}

	// Query QP to obtain requested attributes
	if (p_qp_attr) {
		status = mlnx_query_qp((ib_qp_handle_t)p_ib_qp, p_qp_attr, p_umv_buf);
		if (status != IB_SUCCESS)
			goto err_query_qp;
	}
	
	// return the results
	*ph_qp = (ib_qp_handle_t)p_ib_qp;

	status = IB_SUCCESS;
	goto end;

err_query_qp:
	ib_destroy_qp( p_ib_qp );
err_create_qp:
err_inval_params:
end:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_create_spl_qp (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	uint8_t						port_num,
	IN		const	void						*qp_uctx,
	IN				ci_async_event_cb_t			event_handler,
	IN	OUT			ib_qp_create_t				*p_create_attr,
		OUT			ib_qp_attr_t				*p_qp_attr,
		OUT			ib_qp_handle_t				*ph_qp )
{
	ib_api_status_t 	status;

	HCA_ENTER(HCA_DBG_SHIM);

	status = __create_qp( h_pd, port_num,
		qp_uctx, event_handler, p_create_attr, 0, p_qp_attr, ph_qp, NULL );
		
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_create_qp (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	void						*qp_uctx,
	IN				ci_async_event_cb_t			event_handler,
	IN	OUT			ib_qp_create_t				*p_create_attr,
		OUT			ib_qp_attr_t				*p_qp_attr,
		OUT			ib_qp_handle_t				*ph_qp,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t 	status;

	//NB: algorithm of mthca_alloc_sqp() requires port_num
	// PRM states, that special pares are created in couples, so
	// looks like we can put here port_num = 1 always
	uint8_t port_num = 1;

	HCA_ENTER(HCA_DBG_QP);

	status = __create_qp( h_pd, port_num,
		qp_uctx, event_handler, p_create_attr, 0, p_qp_attr, ph_qp, p_umv_buf );
		
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_create_qp_ex (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	void						*qp_uctx,
	IN				ci_async_event_cb_t			event_handler,
	IN	OUT			ib_qp_create_ex_t				*p_create_attr,
		OUT			ib_qp_attr_t				*p_qp_attr,
		OUT			ib_qp_handle_t				*ph_qp,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t 	status;

	//NB: algorithm of mthca_alloc_sqp() requires port_num
	// PRM states, that special pares are created in couples, so
	// looks like we can put here port_num = 1 always
	uint8_t port_num = 1;
	int create_flags = 0;
	
	HCA_ENTER(HCA_DBG_QP);

	create_flags |= (p_create_attr->create_flags & IB_QP_CREATE_FLAG_SQ_ACCESS_CLIENT_SYNC) ?
						IB_QP_CREATE_SQ_ACCESS_CLIENT_SYNC : 0;
	create_flags |= (p_create_attr->create_flags & IB_QP_CREATE_FLAG_RQ_ACCESS_CLIENT_SYNC) ?
						IB_QP_CREATE_RQ_ACCESS_CLIENT_SYNC : 0;
	create_flags |= (p_create_attr->create_flags & IB_QP_CREATE_FLAG_NO_WQ_OVERFLOW_BY_CLIENT) ?
						IB_QP_CREATE_NO_WQ_OVERFLOW_BY_CLIENT : 0;
	status = __create_qp( h_pd, port_num,
		qp_uctx, event_handler, &p_create_attr->qp_create, create_flags, p_qp_attr, ph_qp, p_umv_buf );
		
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_modify_qp (
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t					*p_modify_attr,
		OUT			ib_qp_attr_t				*p_qp_attr OPTIONAL,
	IN	OUT			ci_umv_buf_t				*p_umv_buf OPTIONAL )
{
	int err;
	ib_api_status_t 	status;
	struct ib_qp_attr qp_attr;
	int qp_attr_mask;
	struct ib_qp *p_ib_qp = (struct ib_qp *)h_qp;

	HCA_ENTER(HCA_DBG_QP);

	// sanity checks
	if( p_umv_buf && p_umv_buf->command ) {
		// sanity checks 
		if (p_umv_buf->output_size < sizeof(struct ibv_modify_qp_resp) ||
			!p_umv_buf->p_inout_buf) {
			status = IB_INVALID_PARAMETER;
			goto err_inval_params;
		}
	}
	
	// fill parameters 
	status = to_qp_attr( p_ib_qp, from_qp_type(p_ib_qp->qp_type), 
		p_modify_attr,  &qp_attr, &qp_attr_mask );
	if (status == IB_NOT_DONE)
		goto query_qp;
	if (status != IB_SUCCESS ) 
		goto err_mode_unsupported;

	// modify QP
	err = p_ib_qp->device->modify_qp( p_ib_qp, &qp_attr, qp_attr_mask, NULL);
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_QP,
			("ibv_modify_qp failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_modify_qp;
	}

	// Query QP to obtain requested attributes
query_qp:	
	if (p_qp_attr) {
		status = mlnx_query_qp ((ib_qp_handle_t)p_ib_qp, p_qp_attr, p_umv_buf);
		if (status != IB_SUCCESS)
				goto err_query_qp;
	}
	
	if( p_umv_buf && p_umv_buf->command ) {
		struct ibv_modify_qp_resp resp;
		resp.attr_mask = qp_attr_mask;
		resp.qp_state = qp_attr.qp_state;
		err = to_umv_buf(p_umv_buf, &resp, sizeof(struct ibv_modify_qp_resp));
		if (err) {
			HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM  ,("to_umv_buf failed (%d)\n", err));
			status = errno_to_iberr(err);
			goto err_copy;
		}
	}

	status = IB_SUCCESS;

err_copy:	
err_query_qp:
err_modify_qp:	
err_mode_unsupported:
err_inval_params:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %x\n", status));
	}
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_ndi_modify_qp (
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t					*p_modify_attr,
		OUT			ib_qp_attr_t				*p_qp_attr OPTIONAL,
	IN		const	uint32_t					buf_size,
	IN				uint8_t* const				p_outbuf)
{
	ci_umv_buf_t umv_buf;
	ib_api_status_t status;
	struct ibv_modify_qp_resp resp;

	HCA_ENTER(HCA_DBG_QP);

	if (buf_size != 0 && buf_size < sizeof(resp.qp_state)) {
		status = IB_INVALID_PARAMETER;
		goto out;
	}

	/* imitate umv_buf */
	umv_buf.command = TRUE;	/* special case for NDI. Usually it's TRUE */
	umv_buf.input_size = 0;
	umv_buf.output_size = sizeof(resp);
	umv_buf.p_inout_buf = (ULONG_PTR)&resp;

	status = mlnx_modify_qp ( h_qp, p_modify_attr, p_qp_attr, &umv_buf );

	if (status == IB_SUCCESS && buf_size != 0) {
		cl_memclr( p_outbuf, buf_size );
		*p_outbuf = resp.qp_state;
	}

out:
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_destroy_qp (
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	uint64_t					timewait )
{
	ib_api_status_t 	status;
	int err;
	struct ib_qp *p_ib_qp = (struct ib_qp *)h_qp;

	UNUSED_PARAM( timewait );

	HCA_ENTER( HCA_DBG_QP);

	HCA_PRINT(TRACE_LEVEL_INFORMATION 	,HCA_DBG_SHIM  ,
		("qpnum %#x, pcs %p\n", p_ib_qp->qp_num, PsGetCurrentProcess()) );

	err = ib_destroy_qp( p_ib_qp );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,
			("ibv_destroy_qp failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_destroy_qp;
	}

	status = IB_SUCCESS;

err_destroy_qp:
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

void
mlnx_qp_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->create_qp = mlnx_create_qp;
	p_interface->create_qp_ex = mlnx_create_qp_ex;
	p_interface->create_spl_qp = mlnx_create_spl_qp;
	p_interface->modify_qp = mlnx_modify_qp;
	p_interface->ndi_modify_qp = mlnx_ndi_modify_qp;
	p_interface->query_qp = mlnx_query_qp;
	p_interface->destroy_qp = mlnx_destroy_qp;
}

