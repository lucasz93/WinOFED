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
#include "mlnx_ual_srq.tmh"
#endif


extern uint32_t	mlnx_dbg_lvl;

static void __free_srq(struct mthca_srq *srq)
{
	/* srq may be NULL, when ioctl returned with some kind of error, e.g. IB_INVALID_PARAM */
	if (!srq)
		return;
	
	if (mthca_is_memfree(srq->ibv_srq.context)) {
		mthca_free_db(to_mctx(srq->ibv_srq.context)->db_tab, MTHCA_DB_TYPE_SRQ,
		srq->db_index);
	}

	if (srq->buf) {
		posix_memfree(srq->buf);
	}

	if (srq->wrid) 
		cl_free(srq->wrid);

	cl_spinlock_destroy(&srq->lock);
	cl_free (srq);
}

static ib_api_status_t  
__pre_create_srq (
	IN		const	ib_pd_handle_t		h_uvp_pd,// Fix me: if needed
	IN		const	ib_srq_attr_t		*p_srq_attr,
	IN OUT	ci_umv_buf_t				*p_umv_buf,
	    OUT	ib_srq_handle_t				*ph_uvp_srq)
{
	struct mthca_srq *srq;
	ib_api_status_t status = IB_SUCCESS;
	size_t size = max( sizeof(struct ibv_create_srq), sizeof(struct ibv_create_srq_resp) );
	mlnx_ual_pd_info_t *p_pd = (mlnx_ual_pd_info_t *)h_uvp_pd;
	struct ibv_pd *ibv_pd = p_pd->ibv_pd;
	struct ibv_create_srq *p_create_srq;
	int err;

	UNREFERENCED_PARAMETER(ph_uvp_srq);
	
	UVP_ENTER(UVP_DBG_SRQ);

	CL_ASSERT(p_umv_buf);

	/* Sanity check SRQ size before proceeding */
	if (p_srq_attr->max_wr > 1 << 16 || p_srq_attr->max_sge > 64)
	{
		status = IB_INVALID_PARAMETER;
		goto err_params;
	}

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_zalloc( size );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = sizeof(struct ibv_create_srq);
	p_umv_buf->output_size = sizeof(struct ibv_create_srq_resp);
	p_umv_buf->command = TRUE;

	/* allocate srq */
	srq = cl_zalloc(sizeof *srq);
	if (!srq)
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_srq;
	}

	/* init fields */
	cl_spinlock_construct(&srq->lock);
	if (cl_spinlock_init(&srq->lock))
		goto err_lock;

	srq->ibv_srq.pd = ibv_pd;
	srq->ibv_srq.context			= ibv_pd->context;
	srq->max     = align_queue_size(ibv_pd->context, p_srq_attr->max_wr, 1);
	srq->max_gs  = p_srq_attr->max_sge;
	srq->counter = 0;

	if (mthca_alloc_srq_buf(ibv_pd, (void*)p_srq_attr, srq))
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_buf;
	}

	// fill the parameters for ioctl
	p_create_srq = (struct ibv_create_srq *)p_umv_buf->p_inout_buf;
	p_create_srq->user_handle = (uint64_t)(ULONG_PTR)srq;
	p_create_srq->mr.start = (uint64_t)(ULONG_PTR)srq->buf;
	p_create_srq->mr.length = srq->buf_size;
	p_create_srq->mr.hca_va = 0;
	p_create_srq->mr.pd_handle	 = p_pd->ibv_pd->handle;
	p_create_srq->mr.pdn = to_mpd(p_pd->ibv_pd)->pdn;
	p_create_srq->mr.access_flags = 0;	//local read

	if (mthca_is_memfree(ibv_pd->context)) {
		srq->db_index = mthca_alloc_db(to_mctx(ibv_pd->context)->db_tab,
			MTHCA_DB_TYPE_SRQ, &srq->db);
		if (srq->db_index < 0)
			goto err_alloc_db;

		p_create_srq->db_page  = db_align(srq->db);
		p_create_srq->db_index = srq->db_index;
	}

	status = IB_SUCCESS;
	goto end;

err_alloc_db:
	posix_memfree(srq->buf);
	cl_free(srq->wrid);
err_alloc_buf:
	cl_spinlock_destroy(&srq->lock);
err_lock:
	cl_free(srq);
err_alloc_srq:
	cl_free((void*)(ULONG_PTR)p_umv_buf->p_inout_buf);
err_memory:
err_params:
end:
	UVP_EXIT(UVP_DBG_SRQ);
	return status;
}


static void
__post_create_srq (
	IN		const	ib_pd_handle_t				h_uvp_pd,
	IN				ib_api_status_t				ioctl_status,
	IN	OUT			ib_srq_handle_t				*ph_uvp_srq,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	int err;
	struct mthca_srq *srq;
	struct ibv_create_srq_resp *p_resp;
	mlnx_ual_pd_info_t *p_pd = (mlnx_ual_pd_info_t *)h_uvp_pd;
	struct ibv_pd *ibv_pd = p_pd->ibv_pd;
	ib_api_status_t status = IB_SUCCESS;

	UVP_ENTER(UVP_DBG_SRQ);

	CL_ASSERT(p_umv_buf);
	p_resp = (struct ibv_create_srq_resp *)p_umv_buf->p_inout_buf;
	srq = (struct mthca_srq *)(ULONG_PTR)p_resp->user_handle;

	if (IB_SUCCESS == ioctl_status) {

		/* complete filling SRQ object */
		srq->ibv_srq.handle			= p_resp->srq_handle;
		srq->srqn					= p_resp->srqn;
		srq->max					= p_resp->max_wr;
		srq->max_gs					= p_resp->max_sge;
		srq->mr.handle = p_resp->mr.mr_handle;
		srq->mr.lkey = p_resp->mr.lkey;
		srq->mr.rkey = p_resp->mr.rkey;
		srq->mr.pd = ibv_pd;
		srq->mr.context = ibv_pd->context;

		if (mthca_is_memfree(ibv_pd->context))
			mthca_set_db_qn(srq->db, MTHCA_DB_TYPE_SRQ, srq->srqn);
		
		*ph_uvp_srq = (ib_srq_handle_t)srq;
	}
	else
		__free_srq(srq);

	if (p_resp)
		cl_free( p_resp );
	UVP_EXIT(UVP_DBG_SRQ);
	return;
}

static void
__post_destroy_srq (
	IN		const ib_srq_handle_t	h_uvp_srq,
	IN		ib_api_status_t			ioctl_status)
{
	int err;
	struct mthca_srq *srq = (struct mthca_srq *) ((void*)h_uvp_srq);

	UVP_ENTER(UVP_DBG_CQ);

	CL_ASSERT(srq);

	if (IB_SUCCESS == ioctl_status) 
		__free_srq(srq);

	UVP_EXIT(UVP_DBG_CQ);
}

void
mlnx_get_srq_interface (
	IN OUT	uvp_interface_t		*p_uvp )
{
	UVP_ENTER(UVP_DBG_DEV);

	CL_ASSERT(p_uvp);

	/*
	 * Completion Queue Management Verbs
	 */
	p_uvp->pre_create_srq  = __pre_create_srq;
	p_uvp->post_create_srq = __post_create_srq;

	p_uvp->pre_query_srq  = NULL; /* __pre_query_srq; */
	p_uvp->post_query_srq = NULL; /*__post_query_srq;*/

	p_uvp->pre_modify_srq  = NULL; /* __modify_srq;*/
	p_uvp->post_modify_srq = NULL; /*__post_modify_srq;*/

	p_uvp->pre_destroy_srq  = NULL; /* __pre_destroy_srq; */
	p_uvp->post_destroy_srq = __post_destroy_srq;

	UVP_EXIT(UVP_DBG_DEV);
}


