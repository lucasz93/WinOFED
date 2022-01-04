/*
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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

#include "mlx4.h"
#include "verbs.h"
#include "mx_abi.h"
#include "wqe.h"
#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#include "verbs.tmh"
#endif

static
ib_api_status_t mlx4_ib_resolve_grh(__in const struct ibv_ah_attr *ah_attr,
			__in char *mac,__in int *is_mcast);

ib_api_status_t
mlx4_pre_open_ca (
	IN		const	ib_net64_t				ca_guid,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_ca_handle_t			*ph_uvp_ca )
{
	struct ibv_context	*context;
    struct ibv_get_context_req *req;
	ib_api_status_t status = IB_SUCCESS;

	UNREFERENCED_PARAMETER(ca_guid);

	context = mlx4_alloc_context();
	if (!context) {
		status = IB_INSUFFICIENT_MEMORY;		
		goto end;
	}
	
	if( p_umv_buf )
	{
		req = (struct ibv_get_context_req*)cl_zalloc(
            max(sizeof(struct ibv_get_context_req), sizeof(struct ibv_get_context_resp)) );
		if( req == NULL )
		{
			status = IB_INSUFFICIENT_MEMORY;
			goto end;
		}

        req->mappings[ibv_get_context_uar].MapType = NdMapIoSpace;
        req->mappings[ibv_get_context_uar].MapIoSpace.CacheType = NdNonCached;
        req->mappings[ibv_get_context_uar].MapIoSpace.CbLength = 4096;

        req->mappings[ibv_get_context_bf].MapType = NdMapIoSpace;
        req->mappings[ibv_get_context_bf].MapIoSpace.CacheType = NdWriteCombined;
        req->mappings[ibv_get_context_bf].MapIoSpace.CbLength = 4096;

        p_umv_buf->p_inout_buf = (ULONG_PTR)req;
		p_umv_buf->input_size = sizeof(struct ibv_get_context_req);
		p_umv_buf->output_size = sizeof(struct ibv_get_context_resp);
		p_umv_buf->command = TRUE;
	}

	*ph_uvp_ca = (ib_ca_handle_t)context;

end:	
	return status;
}

ib_api_status_t
mlx4_post_open_ca (
	IN		const	ib_net64_t				ca_guid,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_ca_handle_t			*ph_uvp_ca,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_get_context_resp *p_resp;
	struct ibv_context *context = (struct ibv_context *)*ph_uvp_ca;
	ib_api_status_t status = IB_SUCCESS;

	UNREFERENCED_PARAMETER(ca_guid);

	CL_ASSERT(p_umv_buf && p_umv_buf->p_inout_buf);
	
	p_resp = (struct ibv_get_context_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status)
	{
		if (!mlx4_fill_context(context, p_resp))
		{
			status = IB_INSUFFICIENT_RESOURCES;
			*ph_uvp_ca = NULL;
			goto end;
		}
	}

end:
	cl_free(p_resp);
	return status;
}

ib_api_status_t
mlx4_post_close_ca (
	IN			ib_ca_handle_t				h_uvp_ca,
	IN			ib_api_status_t				ioctl_status )
{
	struct ibv_context *context = (struct ibv_context *)h_uvp_ca;

	CL_ASSERT(context);

	if (IB_SUCCESS == ioctl_status)
		
		mlx4_free_context(context);

	return IB_SUCCESS;
}

ib_api_status_t
mlx4_pre_alloc_pd (
	IN		const	ib_ca_handle_t			h_uvp_ca,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_pd_handle_t			*ph_uvp_pd )
{
	struct mlx4_pd *pd;
	struct ibv_context *context = (struct ibv_context *)h_uvp_ca;
	ib_api_status_t status = IB_SUCCESS;

	CL_ASSERT(context && p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_malloc( sizeof(struct ibv_alloc_pd_resp) );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto end;
	}
	p_umv_buf->input_size = 0;
	p_umv_buf->output_size = sizeof(struct ibv_alloc_pd_resp);
	p_umv_buf->command = TRUE;

	// Mlx4 code:

	pd = cl_malloc(sizeof *pd);
	if (!pd) {
		status = IB_INSUFFICIENT_MEMORY;		
		goto end;
	}

	pd->ibv_pd.context = context;

	*ph_uvp_pd = (ib_pd_handle_t)&pd->ibv_pd;
	
end:
	return status;
}

void
mlx4_post_alloc_pd (
	IN				ib_ca_handle_t			h_uvp_ca,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_pd_handle_t			*ph_uvp_pd,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_pd 			*pd = (struct ibv_pd *)*ph_uvp_pd;
	struct ibv_alloc_pd_resp	*p_resp;


	UNREFERENCED_PARAMETER(h_uvp_ca);
	
	CL_ASSERT(p_umv_buf && p_umv_buf->p_inout_buf);

	p_resp = (struct ibv_alloc_pd_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status)
	{
		// Mlx4 code:
		
		pd->handle = p_resp->pd_handle;
		to_mpd(pd)->pdn = p_resp->pdn;
	}
	else
	{
		cl_free(to_mpd(pd));
		*ph_uvp_pd = NULL;
	}
	
	cl_free(p_resp);
	return;
}

void
mlx4_post_free_pd (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN				ib_api_status_t			ioctl_status )
{
	struct ibv_pd *pd = (struct ibv_pd *)h_uvp_pd;

	CL_ASSERT(pd);

	if (IB_SUCCESS == ioctl_status)
		cl_free(to_mpd(pd));
}

static int __align_queue_size(int req)
{
	int nent;

	for (nent = 1; nent < req; nent <<= 1)
		; /* nothing */

	return nent;
}

ib_api_status_t
mlx4_pre_create_cq (
	IN		const 	ib_ca_handle_t			h_uvp_ca,
	IN	OUT 		uint32_t* const			p_size,
	IN	OUT 		ci_umv_buf_t				*p_umv_buf,
		OUT			ib_cq_handle_t			*ph_uvp_cq )
{
	struct mlx4_cq		*cq;
	struct ibv_create_cq	*p_create_cq;
	struct ibv_context		*context = (struct ibv_context *)h_uvp_ca;
	ib_api_status_t		status = IB_SUCCESS;
	int size = max( sizeof(struct ibv_create_cq), sizeof(struct ibv_create_cq_resp) );
	int cqe_size = to_mctx(context)->cqe_size;
    uint32_t cqe;

	CL_ASSERT(h_uvp_ca && p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_malloc( size );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_umv_buf;
	}
	p_umv_buf->input_size = sizeof(struct ibv_create_cq);
	p_umv_buf->output_size = sizeof(struct ibv_create_cq_resp);
	p_umv_buf->command = TRUE;

	p_create_cq = (struct ibv_create_cq*)(ULONG_PTR)p_umv_buf->p_inout_buf;

	// Mlx4 code:
	
	/* Sanity check CQ size before proceeding */
	if (*p_size > 0x3fffff) {
		status = IB_INVALID_CQ_SIZE;
		goto err_cqe_size;
	}

	cq = cl_malloc(sizeof *cq);
	if (!cq) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_cq;
	}

	if (cl_spinlock_init(&cq->lock)) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_lock;
	}

	cqe = __align_queue_size(*p_size + 1);

	if (mlx4_alloc_buf(&cq->buf, cqe * cqe_size, 
						context->page_size))
    {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_buf;
	}

	cq->buf.entry_size = cqe_size;

	cq->ibv_cq.context = context;
	cq->cons_index = 0;
		
	cq->set_ci_db  = mlx4_alloc_db(to_mctx(context), MLX4_DB_TYPE_CQ);
	if (!cq->set_ci_db){
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_db;
	}


	cq->arm_db = cq->set_ci_db + 1;
	*cq->arm_db = 0;
	cq->arm_sn = 1;
	*cq->set_ci_db = 0;

	p_create_cq->mappings[mlx4_ib_create_cq_buf].MapMemory.MapType = NdMapMemory;
	p_create_cq->mappings[mlx4_ib_create_cq_buf].MapMemory.AccessType = NdModifyAccess;
	p_create_cq->mappings[mlx4_ib_create_cq_buf].MapMemory.Address = (uintptr_t) cq->buf.buf;
	p_create_cq->mappings[mlx4_ib_create_cq_buf].MapMemory.CbLength = cqe * cqe_size;

	p_create_cq->mappings[mlx4_ib_create_cq_db].MapMemory.MapType = NdMapMemoryCoallesce;
	p_create_cq->mappings[mlx4_ib_create_cq_db].MapMemory.AccessType = NdWriteAccess;
	p_create_cq->mappings[mlx4_ib_create_cq_db].MapMemory.Address = (uintptr_t) cq->set_ci_db;
	p_create_cq->mappings[mlx4_ib_create_cq_db].MapMemory.CbLength = sizeof(*cq->set_ci_db) + sizeof(*cq->arm_db);
	
	p_create_cq->mappings[mlx4_ib_create_cq_arm_sn].MapMemory.MapType = NdMapMemory;
	p_create_cq->mappings[mlx4_ib_create_cq_arm_sn].MapMemory.AccessType = NdModifyAccess;
	p_create_cq->mappings[mlx4_ib_create_cq_arm_sn].MapMemory.Address = (uintptr_t) &cq->arm_sn;
	p_create_cq->mappings[mlx4_ib_create_cq_arm_sn].MapMemory.CbLength = sizeof(cq->arm_sn);

    *p_size = --cqe;

	*ph_uvp_cq = (ib_cq_handle_t)&cq->ibv_cq;
	goto end;

err_alloc_db:
	mlx4_free_buf(&cq->buf);
err_alloc_buf:
	cl_spinlock_destroy(&cq->lock);
err_lock:
	cl_free(cq);
err_cq:
err_cqe_size:
	cl_free( (void*)(ULONG_PTR)p_umv_buf->p_inout_buf );
err_umv_buf:
end:
	return status;
}

void
mlx4_post_create_cq (
	IN		const	ib_ca_handle_t			h_uvp_ca,
	IN				ib_api_status_t			ioctl_status,
	IN		const	uint32_t					size,
	IN	OUT			ib_cq_handle_t			*ph_uvp_cq,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_cq				*cq = (struct ibv_cq *)*ph_uvp_cq;
	struct ibv_create_cq_resp	*p_resp;

	UNREFERENCED_PARAMETER(h_uvp_ca);
	UNREFERENCED_PARAMETER(size);
	
	CL_ASSERT(p_umv_buf && p_umv_buf->p_inout_buf);

	p_resp = (struct ibv_create_cq_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status)
	{
		// Mlx4 code:
		
		to_mcq(cq)->cqn	= p_resp->cqn;
		cq->cqe			= p_resp->cqe;
	}
	else
	{
		mlx4_post_destroy_cq (*ph_uvp_cq, IB_SUCCESS);
		*ph_uvp_cq = NULL;
	}
	
	cl_free(p_resp);
	return;
}

ib_api_status_t
mlx4_pre_query_cq (
	IN		const	ib_cq_handle_t			h_uvp_cq,
		OUT			uint32_t* const			p_size,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_cq *cq = (struct ibv_cq *)h_uvp_cq;

	UNREFERENCED_PARAMETER(p_umv_buf);
	
	*p_size = cq->cqe;

	return IB_VERBS_PROCESSING_DONE;
}

void
mlx4_post_destroy_cq (
	IN		const	ib_cq_handle_t			h_uvp_cq,
	IN				ib_api_status_t			ioctl_status )
{
	struct ibv_cq *cq = (struct ibv_cq *)h_uvp_cq;

	CL_ASSERT(cq);

	if (IB_SUCCESS == ioctl_status) {
		mlx4_free_db(to_mctx(cq->context), MLX4_DB_TYPE_CQ, to_mcq(cq)->set_ci_db);
		mlx4_free_buf(&to_mcq(cq)->buf);

		cl_spinlock_destroy(&to_mcq(cq)->lock);
		cl_free(to_mcq(cq));
	}
}

ib_api_status_t  
mlx4_pre_create_srq (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	ib_srq_attr_t				*p_srq_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_srq_handle_t			*ph_uvp_srq )
{
	struct mlx4_srq *srq;
	struct ibv_create_srq *p_create_srq;
	struct ibv_pd *pd = (struct ibv_pd *)h_uvp_pd;
	ib_api_status_t status = IB_SUCCESS;
	size_t size = max( sizeof(struct ibv_create_srq), sizeof(struct ibv_create_srq_resp) );

	CL_ASSERT(p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_malloc( size ); 
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = sizeof(struct ibv_create_srq);
	p_umv_buf->output_size = sizeof(struct ibv_create_srq_resp);
	p_umv_buf->command = TRUE;

	p_create_srq = (struct ibv_create_srq*)(ULONG_PTR)p_umv_buf->p_inout_buf;
	
	// Mlx4 code:

	/* Sanity check SRQ size before proceeding */
	if (p_srq_attr->max_wr > 1 << 16 || p_srq_attr->max_sge > 64)
	{
		status = IB_INVALID_PARAMETER;
		goto err_params;
	}

	srq = cl_malloc(sizeof *srq);
	if (!srq) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_srq;
	}

	if (cl_spinlock_init(&srq->lock)) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_lock;
	}

	srq->ibv_srq.pd 		= pd;
	srq->ibv_srq.context	= pd->context;
	
	srq->max     	= __align_queue_size(p_srq_attr->max_wr + 1);
	srq->max_gs  = p_srq_attr->max_sge;
	srq->counter	= 0;

	if (mlx4_alloc_srq_buf(pd, (struct ibv_srq_attr *)p_srq_attr, srq))
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_buf;
	}

	srq->db = mlx4_alloc_db(to_mctx(pd->context), MLX4_DB_TYPE_RQ);
	if (!srq->db){
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_db;
	}
	*srq->db = 0;
	
	// fill the parameters for ioctl
	p_create_srq->mappings[mlx4_ib_create_srq_buf].MapMemory.MapType = NdMapMemory;
	p_create_srq->mappings[mlx4_ib_create_srq_buf].MapMemory.AccessType = NdModifyAccess;
	p_create_srq->mappings[mlx4_ib_create_srq_buf].MapMemory.Address = (uintptr_t) srq->buf.buf;
	p_create_srq->mappings[mlx4_ib_create_srq_buf].MapMemory.CbLength = srq->max << srq->wqe_shift;

	p_create_srq->mappings[mlx4_ib_create_srq_db].MapMemory.MapType = NdMapMemoryCoallesce;
	p_create_srq->mappings[mlx4_ib_create_srq_db].MapMemory.AccessType = NdWriteAccess;
	p_create_srq->mappings[mlx4_ib_create_srq_db].MapMemory.Address = (uintptr_t) srq->db;
	p_create_srq->mappings[mlx4_ib_create_srq_db].MapMemory.CbLength = sizeof(*srq->db);

	*ph_uvp_srq = (ib_srq_handle_t)&srq->ibv_srq;
	goto end;

err_alloc_db:
	cl_free(srq->wrid);
	mlx4_free_buf(&srq->buf);
err_alloc_buf:
	cl_spinlock_destroy(&srq->lock);
err_lock:
	cl_free(srq);
err_alloc_srq:
	cl_free( (void*)(ULONG_PTR)p_umv_buf->p_inout_buf );
err_params: err_memory:
end:
	return status;
}

void
mlx4_post_create_srq (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_srq_handle_t			*ph_uvp_srq,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_create_srq_resp *p_resp;

	UNREFERENCED_PARAMETER(h_uvp_pd);
	
	CL_ASSERT(p_umv_buf && p_umv_buf->p_inout_buf);
	
	p_resp = (struct ibv_create_srq_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS != ioctl_status)
	{
		mlx4_post_destroy_srq (*ph_uvp_srq, IB_SUCCESS);
		*ph_uvp_srq = NULL;
	}

	cl_free(p_resp);
	return;
}

ib_api_status_t
mlx4_pre_destroy_srq (
	IN		const	ib_srq_handle_t			h_uvp_srq )
{
#ifdef XRC_SUPPORT
	struct ibv_srq *ibsrq = (struct ibv_srq *)h_uvp_srq;
	struct mlx4_srq *srq = to_msrq(ibsrq);
	struct mlx4_cq *mcq = NULL;
	
	if (ibsrq->xrc_cq)
	{
		/* is an xrc_srq */
		mcq = to_mcq(ibsrq->xrc_cq);
		mlx4_cq_clean(mcq, 0, srq);
		cl_spinlock_acquire(&mcq->lock);
		mlx4_clear_xrc_srq(to_mctx(ibsrq->context), srq->srqn);
		cl_spinlock_release(&mcq->lock);
	}
#else
	UNUSED_PARAM(h_uvp_srq);
#endif	
	return IB_SUCCESS;
}

void
mlx4_post_destroy_srq (
	IN		const	ib_srq_handle_t			h_uvp_srq,
	IN				ib_api_status_t			ioctl_status )
{
	struct ibv_srq		*ibsrq = (struct ibv_srq *)h_uvp_srq;
	struct mlx4_srq	*srq = to_msrq(ibsrq);
	
	CL_ASSERT(srq);

	if (IB_SUCCESS == ioctl_status)
	{
		mlx4_free_db(to_mctx(ibsrq->context), MLX4_DB_TYPE_RQ, srq->db);
		cl_free(srq->wrid);
		mlx4_free_buf(&srq->buf);
		cl_spinlock_destroy(&srq->lock);
		cl_free(srq);
	}
	else
	{
#ifdef XRC_SUPPORT
		if (ibsrq->xrc_cq) {
			/* is an xrc_srq */
			struct mlx4_cq	*mcq = to_mcq(ibsrq->xrc_cq);
			cl_spinlock_acquire(&mcq->lock);
			mlx4_store_xrc_srq(to_mctx(ibsrq->context), srq->srqn, srq);
			cl_spinlock_release(&mcq->lock);
		}
#endif		
	}
}

static enum ibv_qp_type
__to_qp_type(ib_qp_type_t type)
{
	switch (type) {
	case IB_QPT_RELIABLE_CONN: return IBV_QPT_RC;
	case IB_QPT_UNRELIABLE_CONN: return IBV_QPT_UC;
	case IB_QPT_UNRELIABLE_DGRM: return IBV_QPT_UD;
#ifdef XRC_SUPPORT
	//case IB_QPT_XRC_CONN: return IBV_QPT_XRC;
#endif	
	default: return IBV_QPT_RC;
	}
}

ib_api_status_t
mlx4_pre_create_qp (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	ib_qp_create_t			*p_create_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_qp_handle_t			*ph_uvp_qp )
{
	struct ibv_pd			*pd = (struct ibv_pd *)h_uvp_pd;
	struct mlx4_context	*context = to_mctx(pd->context);
	struct mlx4_qp		*qp;
	struct ibv_create_qp	*p_create_qp;
	struct ibv_qp_init_attr	attr;
	ib_api_status_t 		status = IB_SUCCESS;
	int size = max( sizeof(struct ibv_create_qp), sizeof(struct ibv_create_qp_resp) );

	CL_ASSERT(p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_malloc(size);
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = sizeof(struct ibv_create_qp);
	p_umv_buf->output_size = sizeof(struct ibv_create_qp_resp);
	p_umv_buf->command = TRUE;

	p_create_qp = (struct ibv_create_qp*)(ULONG_PTR)p_umv_buf->p_inout_buf;
	
	/* convert attributes */
	memset( &attr, 0, sizeof(attr) );
	attr.send_cq				= (struct ibv_cq *)p_create_attr->h_sq_cq;
	attr.recv_cq				= (struct ibv_cq *)p_create_attr->h_rq_cq;
	attr.srq					= (struct ibv_srq*)p_create_attr->h_srq;
	attr.cap.max_send_wr		= p_create_attr->sq_depth;
	attr.cap.max_recv_wr		= p_create_attr->rq_depth;
	attr.cap.max_send_sge		= p_create_attr->sq_sge;
	attr.cap.max_recv_sge		= p_create_attr->rq_sge;
	attr.cap.max_inline_data	= p_create_attr->sq_max_inline;
	attr.qp_type				= __to_qp_type(p_create_attr->qp_type);
	attr.sq_sig_all				= p_create_attr->sq_signaled;

	// Mlx4 code:
	
	/* Sanity check QP size before proceeding */
	if (attr.cap.max_send_wr    > (uint32_t) context->max_qp_wr ||
	    attr.cap.max_recv_wr     > (uint32_t) context->max_qp_wr ||
	    attr.cap.max_send_sge   > (uint32_t) context->max_sge   ||
	    attr.cap.max_recv_sge   > (uint32_t) context->max_sge   ||
	    attr.cap.max_inline_data > 1024)
	{
		status = IB_INVALID_PARAMETER;
		goto end;
	}

	qp = cl_malloc(sizeof *qp);
	if (!qp) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_qp;
	}

	mlx4_calc_sq_wqe_size(&attr.cap, attr.qp_type, qp);

	/*
	 * We need to leave 2 KB + 1 WQE of headroom in the SQ to
	 * allow HW to prefetch.
	 */
	qp->sq_spare_wqes = (2048 >> qp->sq.wqe_shift) + 1;
	qp->sq.wqe_cnt = __align_queue_size(attr.cap.max_send_wr + qp->sq_spare_wqes);
	qp->rq.wqe_cnt = __align_queue_size(attr.cap.max_recv_wr);

	if (attr.srq || attr.qp_type == IBV_QPT_XRC)
		attr.cap.max_recv_wr = qp->rq.wqe_cnt = 0;
	else 
	{
		if (attr.cap.max_recv_sge < 1)
			attr.cap.max_recv_sge = 1;
		if (attr.cap.max_recv_wr < 1)
			attr.cap.max_recv_wr = 1;
	}

	if (mlx4_alloc_qp_buf(pd, &attr.cap, attr.qp_type, qp)){
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_qp_buff;
	}

	mlx4_init_qp_indices(qp);

	if (cl_spinlock_init(&qp->sq.lock)) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_spinlock_sq;
	}
	if (cl_spinlock_init(&qp->rq.lock)) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_spinlock_rq;
	}

	// fill qp fields
	if (!attr.srq && attr.qp_type != IBV_QPT_XRC) {
		qp->db = mlx4_alloc_db(context, MLX4_DB_TYPE_RQ);
		if (!qp->db) {
			status = IB_INSUFFICIENT_MEMORY;
			goto err_db;
		}

		*qp->db = 0;
	}
	if (attr.sq_sig_all)
		qp->sq_signal_bits = cl_hton32(MLX4_WQE_CTRL_CQ_UPDATE);
	else
		qp->sq_signal_bits = 0;

	// fill the rest of qp fields
	qp->ibv_qp.pd = pd;
	qp->ibv_qp.context= pd->context;
	qp->ibv_qp.send_cq = attr.send_cq;
	qp->ibv_qp.recv_cq = attr.recv_cq;
	qp->ibv_qp.srq = attr.srq;
	qp->ibv_qp.state = IBV_QPS_RESET;
	qp->ibv_qp.qp_type = attr.qp_type;

	// fill request fields
	p_create_qp->mappings[mlx4_ib_create_qp_buf].MapMemory.MapType = NdMapMemory;
	p_create_qp->mappings[mlx4_ib_create_qp_buf].MapMemory.AccessType = NdModifyAccess;
	p_create_qp->mappings[mlx4_ib_create_qp_buf].MapMemory.Address = (uintptr_t) qp->buf.buf;
	p_create_qp->mappings[mlx4_ib_create_qp_buf].MapMemory.CbLength = qp->buf_size;

	p_create_qp->mappings[mlx4_ib_create_qp_db].MapMemory.MapType = NdMapMemoryCoallesce;
	p_create_qp->mappings[mlx4_ib_create_qp_db].MapMemory.AccessType = NdWriteAccess;
	if (!attr.srq && attr.qp_type != IBV_QPT_XRC) {
		p_create_qp->mappings[mlx4_ib_create_qp_db].MapMemory.Address = (uintptr_t) qp->db;
		p_create_qp->mappings[mlx4_ib_create_qp_db].MapMemory.CbLength = sizeof(*qp->db);
	} else {
		p_create_qp->mappings[mlx4_ib_create_qp_db].MapMemory.Address = 0;
		p_create_qp->mappings[mlx4_ib_create_qp_db].MapMemory.CbLength = 0;
	}

	p_create_qp->log_sq_stride   = (uint8_t)qp->sq.wqe_shift;
	for (p_create_qp->log_sq_bb_count = 0;
	     qp->sq.wqe_cnt > 1 << p_create_qp->log_sq_bb_count;
	     ++p_create_qp->log_sq_bb_count)
    {
		; /* nothing */
    }
	p_create_qp->sq_no_prefetch = 0;

	*ph_uvp_qp = (ib_qp_handle_t)&qp->ibv_qp;
	goto end;

err_db:
	cl_spinlock_destroy(&qp->rq.lock);
err_spinlock_rq:
	cl_spinlock_destroy(&qp->sq.lock);
err_spinlock_sq:
	cl_free(qp->sq.wrid);
	if (qp->rq.wqe_cnt)
		free(qp->rq.wrid);
	mlx4_free_buf(&qp->buf);
err_alloc_qp_buff:
	cl_free(qp);	
err_alloc_qp:
	cl_free( (void*)(ULONG_PTR)p_umv_buf->p_inout_buf );
err_memory:
end:
	return status;
}

ib_api_status_t
mlx4_wv_pre_create_qp (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	uvp_qp_create_t			*p_create_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_qp_handle_t			*ph_uvp_qp )
{
	struct mlx4_qp *qp;
	ib_api_status_t	status;

	status = mlx4_pre_create_qp(h_uvp_pd, &p_create_attr->qp_create,
								p_umv_buf, ph_uvp_qp);
	if (status == IB_SUCCESS) {
		qp = (struct mlx4_qp *) *ph_uvp_qp;
		qp->ibv_qp.qp_context = p_create_attr->context;
	}
	return status;
}

ib_api_status_t
mlx4_post_create_qp (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN				ib_api_status_t 			ioctl_status,
	IN	OUT 		ib_qp_handle_t			*ph_uvp_qp,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	struct mlx4_qp 			*qp = (struct mlx4_qp *)*ph_uvp_qp;
	struct ibv_pd 			*pd = (struct ibv_pd *)h_uvp_pd;
	struct ibv_context			*context = pd->context;
	struct ibv_create_qp_resp	*p_resp;
	ib_api_status_t status = IB_SUCCESS;
		
	CL_ASSERT(p_umv_buf && p_umv_buf->p_inout_buf);
	
	p_resp = (struct ibv_create_qp_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status)
	{
		// Mlx4 code:
		
		struct ibv_qp_cap	cap;
		
		cap.max_recv_sge		= p_resp->max_recv_sge;
		cap.max_send_sge		= p_resp->max_send_sge;
		cap.max_recv_wr		= p_resp->max_recv_wr;
		cap.max_send_wr		= p_resp->max_send_wr;
		cap.max_inline_data	= p_resp->max_inline_data;
		
		qp->ibv_qp.handle		= p_resp->qp_handle;
		qp->ibv_qp.qp_num	= p_resp->qpn;
		
		qp->rq.wqe_cnt	= cap.max_recv_wr;
		qp->rq.max_gs	= cap.max_recv_sge;

		/* adjust rq maxima to not exceed reported device maxima */
		cap.max_recv_wr	= min((uint32_t) to_mctx(context)->max_qp_wr, cap.max_recv_wr);
		cap.max_recv_sge = min((uint32_t) to_mctx(context)->max_sge, cap.max_recv_sge);

		qp->rq.max_post = cap.max_recv_wr;
		//qp->rq.max_gs = cap.max_recv_sge;  - RIB : add this ?
		mlx4_set_sq_sizes(qp, &cap, qp->ibv_qp.qp_type);

		qp->doorbell_qpn    = cl_hton32(qp->ibv_qp.qp_num << 8);

		if (mlx4_store_qp(to_mctx(context), qp->ibv_qp.qp_num, qp))
		{
			mlx4_post_destroy_qp(*ph_uvp_qp, IB_SUCCESS);
			status = IB_INSUFFICIENT_MEMORY;
		}
		MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_QP, 
			("qpn %#x, buf %p, db_rec %p, sq %d:%d, rq %d:%d\n", 
			qp->ibv_qp.qp_num, qp->buf.buf, qp->db,
			qp->sq.head, qp->sq.tail, qp->rq.head, qp->rq.tail )); 
	}
	else
	{
		mlx4_post_destroy_qp(*ph_uvp_qp, IB_SUCCESS);
		*ph_uvp_qp = NULL;
	}

	cl_free(p_resp);
	return status;
}

ib_api_status_t
mlx4_pre_modify_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp,
	IN		const	ib_qp_mod_t				*p_modify_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t status = IB_SUCCESS;

	UNREFERENCED_PARAMETER(h_uvp_qp);
	UNREFERENCED_PARAMETER(p_modify_attr);

	CL_ASSERT(p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_malloc(sizeof(struct ibv_modify_qp_resp));
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = 0;
	p_umv_buf->output_size = sizeof(struct ibv_modify_qp_resp);
	p_umv_buf->command = TRUE;
	
err_memory:
	return status;
}

void
mlx4_post_query_qp (
	IN				ib_qp_handle_t				h_uvp_qp,
	IN				ib_api_status_t				ioctl_status,
	IN	OUT			ib_qp_attr_t				*p_query_attr,
	IN	OUT			ci_umv_buf_t					*p_umv_buf )
{
	struct mlx4_qp *qp = (struct mlx4_qp *)h_uvp_qp;

	UNREFERENCED_PARAMETER(p_umv_buf);

	if(IB_SUCCESS == ioctl_status)
	{
		p_query_attr->sq_max_inline = qp->max_inline_data;
		p_query_attr->sq_sge		= qp->sq.max_gs;
		p_query_attr->sq_depth		= qp->sq.max_post;
		p_query_attr->rq_sge		= qp->rq.max_gs;
		p_query_attr->rq_depth		= qp->rq.max_post;
	}
}

void
mlx4_post_modify_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_qp				*qp = (struct ibv_qp *)h_uvp_qp;
	struct ibv_modify_qp_resp	*p_resp;

	CL_ASSERT(p_umv_buf && p_umv_buf->p_inout_buf);

	p_resp = (struct ibv_modify_qp_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status) 
	{
		// Mlx4 code:
		
		if (qp->state == IBV_QPS_RESET &&
	    	    p_resp->attr_mask & IBV_QP_STATE &&
	    	    p_resp->qp_state == IBV_QPS_INIT)
	    	{
			mlx4_qp_init_sq_ownership(to_mqp(qp));
		}

		if (p_resp->attr_mask & IBV_QP_STATE) {
			qp->state = p_resp->qp_state;
		}

		if (p_resp->attr_mask & IBV_QP_STATE &&
		    p_resp->qp_state == IBV_QPS_RESET)
		{
			mlx4_cq_clean(to_mcq(qp->recv_cq), qp->qp_num,
		       				qp->srq ? to_msrq(qp->srq) : NULL);
			if (qp->send_cq != qp->recv_cq)
				mlx4_cq_clean(to_mcq(qp->send_cq), qp->qp_num, NULL);

			mlx4_init_qp_indices(to_mqp(qp));
			if (!qp->srq && qp->qp_type != IBV_QPT_XRC)
				*to_mqp(qp)->db = 0;
		}
	}

	cl_free (p_resp);
	return;
}

static void
__mlx4_lock_cqs(struct ibv_qp *qp)
{
	struct mlx4_cq *send_cq = to_mcq(qp->send_cq);
	struct mlx4_cq *recv_cq = to_mcq(qp->recv_cq);

	if (send_cq == recv_cq)
		cl_spinlock_acquire(&send_cq->lock);
	else if (send_cq->cqn < recv_cq->cqn) {
		cl_spinlock_acquire(&send_cq->lock);
		cl_spinlock_acquire(&recv_cq->lock);
	} else {
		cl_spinlock_acquire(&recv_cq->lock);
		cl_spinlock_acquire(&send_cq->lock);
	}
}

static void
__mlx4_unlock_cqs(struct ibv_qp *qp)
{
	struct mlx4_cq *send_cq = to_mcq(qp->send_cq);
	struct mlx4_cq *recv_cq = to_mcq(qp->recv_cq);

	if (send_cq == recv_cq)
		cl_spinlock_release(&send_cq->lock);
	else if (send_cq->cqn < recv_cq->cqn) {
		cl_spinlock_release(&recv_cq->lock);
		cl_spinlock_release(&send_cq->lock);
	} else {
		cl_spinlock_release(&send_cq->lock);
		cl_spinlock_release(&recv_cq->lock);
	}
}

ib_api_status_t
mlx4_pre_destroy_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp )
{
    UNREFERENCED_PARAMETER(h_uvp_qp);
	return IB_SUCCESS;
}

void
mlx4_post_destroy_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp,
	IN				ib_api_status_t			ioctl_status )
{
	struct ibv_qp* ibqp = (struct ibv_qp *)h_uvp_qp;
	struct mlx4_qp* qp = to_mqp(ibqp);
	
	CL_ASSERT(h_uvp_qp);

	if (IB_SUCCESS == ioctl_status)
	{
		mlx4_cq_clean(to_mcq(ibqp->recv_cq), ibqp->qp_num,
					ibqp->srq ? to_msrq(ibqp->srq) : NULL);
		if (ibqp->send_cq != ibqp->recv_cq)
			mlx4_cq_clean(to_mcq(ibqp->send_cq), ibqp->qp_num, NULL);

		__mlx4_lock_cqs(ibqp);
		mlx4_clear_qp(to_mctx(ibqp->context), ibqp->qp_num);
		__mlx4_unlock_cqs(ibqp);

		if (!ibqp->srq && ibqp->qp_type != IBV_QPT_XRC)
			mlx4_free_db(to_mctx(ibqp->context), MLX4_DB_TYPE_RQ, qp->db);

		cl_spinlock_destroy(&qp->sq.lock);
		cl_spinlock_destroy(&qp->rq.lock);

		MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_QP, 
			("qpn %#x, buf %p, sq %d:%d, rq %d:%d\n", qp->ibv_qp.qp_num, qp->buf.buf, 
			qp->sq.head, qp->sq.tail, qp->rq.head, qp->rq.tail )); 
		cl_free(qp->sq.wrid);
		if (qp->rq.wqe_cnt)
			cl_free(qp->rq.wrid);
		mlx4_free_buf(&qp->buf);
		cl_free(qp);
	}
}

void
mlx4_nd_modify_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp,
		OUT			void**					pp_outbuf,
		OUT			DWORD*					p_size )
{
	struct ibv_qp *ibv_qp = (struct ibv_qp *)h_uvp_qp;

	*(uint32_t**)pp_outbuf = (uint32_t*)&ibv_qp->state;
	*p_size = sizeof(ibv_qp->state);
}

static ib_qp_state_t __from_qp_state(enum ibv_qp_state state)
{
	switch (state) {
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

uint32_t
mlx4_nd_get_qp_state (
	IN		const	ib_qp_handle_t			h_uvp_qp )
{
	struct ibv_qp *ibv_qp = (struct ibv_qp *)h_uvp_qp;

	return __from_qp_state(ibv_qp->state);
}

static enum ibv_rate __to_rate(uint8_t rate)
{
	if (rate == IB_PATH_RECORD_RATE_2_5_GBS) return IBV_RATE_2_5_GBPS;
	if (rate == IB_PATH_RECORD_RATE_5_GBS) return IBV_RATE_5_GBPS;
	if (rate == IB_PATH_RECORD_RATE_10_GBS) return IBV_RATE_10_GBPS;
	if (rate == IB_PATH_RECORD_RATE_20_GBS) return IBV_RATE_20_GBPS;
	if (rate == IB_PATH_RECORD_RATE_30_GBS) return IBV_RATE_30_GBPS;
	if (rate == IB_PATH_RECORD_RATE_40_GBS) return IBV_RATE_40_GBPS;
	if (rate == IB_PATH_RECORD_RATE_60_GBS) return IBV_RATE_60_GBPS;
	if (rate == IB_PATH_RECORD_RATE_80_GBS) return IBV_RATE_80_GBPS;
	if (rate == IB_PATH_RECORD_RATE_120_GBS) return IBV_RATE_120_GBPS;
	return IBV_RATE_MAX;
}

inline void 
__grh_get_ver_class_flow(
	IN		const	ib_net32_t					ver_class_flow,
		OUT			uint8_t* const				p_ver OPTIONAL,
		OUT			uint8_t* const				p_tclass OPTIONAL,
		OUT			net32_t* const				p_flow_lbl OPTIONAL )
{
	ib_net32_t tmp_ver_class_flow;

	tmp_ver_class_flow = cl_ntoh32( ver_class_flow );

	if (p_ver)
		*p_ver = (uint8_t)(tmp_ver_class_flow >> 28);

	if (p_tclass)
		*p_tclass = (uint8_t)(tmp_ver_class_flow >> 20);

	if (p_flow_lbl)
		*p_flow_lbl = (ver_class_flow & CL_HTON32( 0x000FFFFF ));
}

static ib_api_status_t
__to_ah (
	IN		const	ib_av_attr_t				*p_av_attr,
		OUT			struct ibv_ah_attr			*p_attr )
{
	p_attr->port_num = p_av_attr->port_num;
	p_attr->sl = p_av_attr->sl;
	p_attr->dlid = cl_ntoh16 (p_av_attr->dlid);
	p_attr->static_rate = __to_rate(p_av_attr->static_rate);
	p_attr->src_path_bits = p_av_attr->path_bits;
			
	/* For global destination or Multicast address:*/
	if (p_av_attr->grh_valid)
	{
		p_attr->is_global 		= TRUE;
		p_attr->grh.hop_limit	= p_av_attr->grh.hop_limit;
		__grh_get_ver_class_flow( p_av_attr->grh.ver_class_flow, NULL,
								&p_attr->grh.traffic_class, &p_attr->grh.flow_label );
		p_attr->grh.sgid_index	= p_av_attr->grh.resv2;
		cl_memcpy (p_attr->grh.dgid.raw, p_av_attr->grh.dest_gid.raw, 16);
	}
	else
	{
		p_attr->is_global = FALSE;
	}
	return IB_SUCCESS;
} 

static ib_api_status_t
__set_av_params(struct mlx4_ah *ah, struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
	ah->av.ib.port_pd = cl_hton32(to_mpd(pd)->pdn | (attr->port_num << 24));
	ah->av.ib.g_slid  = attr->src_path_bits;
	ah->av.ib.dlid    = cl_hton16(attr->dlid);
	if (attr->static_rate) {
		ah->av.ib.stat_rate = (uint8_t)(attr->static_rate + MLX4_STAT_RATE_OFFSET);
		/* XXX check rate cap? */
	}
	ah->av.ib.sl_tclass_flowlabel = cl_hton32(attr->sl << 28);
	if (attr->is_global)
	{
		ah->av.ib.g_slid |= 0x80;
		ah->av.ib.gid_index = attr->grh.sgid_index;
		ah->av.ib.hop_limit = attr->grh.hop_limit;
		ah->av.ib.sl_tclass_flowlabel |=
			cl_hton32((attr->grh.traffic_class << 20) |
				    attr->grh.flow_label);
		cl_memcpy(ah->av.ib.dgid, attr->grh.dgid.raw, 16);
	}

	if(ah->av.ib.dlid == 0)
	{// RoCE: only for RoCE LID can be zero
		char mac[6];
		ib_api_status_t err;
		int is_mcast;
		
		err = mlx4_ib_resolve_grh(attr, mac, &is_mcast);
		if (err != IB_SUCCESS)
			return err;
		
		memcpy(ah->av.eth.mac_0_1, mac, 2);
		memcpy(ah->av.eth.mac_2_5, mac + 2, 4);
		ah->av.eth.vlan = 0;
	}
    return IB_SUCCESS;
}

ib_api_status_t
mlx4_pre_create_ah (
	IN		const 	ib_pd_handle_t			h_uvp_pd,
	IN		const 	ib_av_attr_t				*p_av_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
	   	OUT			ib_av_handle_t			*ph_uvp_av )
{
	struct mlx4_ah *ah;
	struct ibv_ah_attr attr;
	struct ibv_pd *pd = (struct ibv_pd *)h_uvp_pd;
	ib_api_status_t status = IB_SUCCESS;
	
	UNREFERENCED_PARAMETER(p_umv_buf);
	
	ah = cl_malloc(sizeof *ah);
	if (!ah) {
		status = IB_INSUFFICIENT_MEMORY;
		goto end;
	}

	// convert parameters 
	cl_memset(&attr, 0, sizeof(attr));
	__to_ah(p_av_attr, &attr);

	ah->ibv_ah.pd = pd;
	ah->ibv_ah.context = pd->context;
	cl_memcpy(&ah->ibv_ah.av_attr, p_av_attr, sizeof (ib_av_attr_t));

	cl_memset(&ah->av.ib, 0, sizeof ah->av.ib);
	status = __set_av_params(ah, pd, &attr);
	if (status)
		goto end;

	*ph_uvp_av = (ib_av_handle_t)&ah->ibv_ah;
	status = IB_VERBS_PROCESSING_DONE;

end:
	return status;
}

ib_api_status_t
mlx4_pre_query_ah (
	IN		const	ib_av_handle_t			h_uvp_av,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	UNREFERENCED_PARAMETER(h_uvp_av);
	UNREFERENCED_PARAMETER(p_umv_buf);
	
	return IB_VERBS_PROCESSING_DONE;
}

void
mlx4_post_query_ah (
	IN		const	ib_av_handle_t			h_uvp_av,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_av_attr_t				*p_addr_vector,
	IN	OUT			ib_pd_handle_t			*ph_pd,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_ah *ah = (struct ibv_ah *)h_uvp_av;

	UNREFERENCED_PARAMETER(p_umv_buf);

	CL_ASSERT(h_uvp_av && p_addr_vector);

	if (ioctl_status == IB_SUCCESS)
	{
		cl_memcpy(p_addr_vector, &ah->av_attr, sizeof(ib_av_attr_t));
		if (ph_pd)
			*ph_pd = (ib_pd_handle_t)ah->pd;
	}
}

ib_api_status_t
mlx4_pre_modify_ah (
	IN		const	ib_av_handle_t			h_uvp_av,
	IN		const	ib_av_attr_t				*p_addr_vector,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_ah *ah = (struct ibv_ah *)h_uvp_av;
	struct ibv_ah_attr attr;
	ib_api_status_t status;

	UNREFERENCED_PARAMETER(p_umv_buf);
	
	CL_ASSERT (h_uvp_av);

	status = __to_ah(p_addr_vector, &attr);
	if (status)
		return status;

	status = __set_av_params(to_mah(ah), ah->pd, &attr);
	if (status)
		return status;
    
	cl_memcpy(&ah->av_attr, p_addr_vector, sizeof(ib_av_attr_t));
	
	return IB_VERBS_PROCESSING_DONE;
}

ib_api_status_t
mlx4_pre_destroy_ah (
	IN		const	ib_av_handle_t			h_uvp_av )
{
	struct ibv_ah *ah = (struct ibv_ah *)h_uvp_av;
	
	CL_ASSERT(ah);
	
	cl_free(to_mah(ah));
	
	return IB_VERBS_PROCESSING_DONE;
}

static inline int rdma_link_local_addr(union ibv_gid *addr)
{
	uint32_t addr0, addr1;

	addr0 = *((uint32_t *) addr->raw);
	addr1 = *(((uint32_t *) addr->raw) + 1);
	
	if (addr0 == htonl(0xfe800000) &&
	    addr1 == 0)
		return 1;
	else
		return 0;
}

inline void rdma_get_ll_mac(__in union ibv_gid *addr,__out char *mac)
{
	memcpy(mac, &addr->raw[8], 3);
	memcpy(mac + 3, &addr->raw[13], 3);
	mac[0] ^= 2;   
}

static inline int rdma_is_multicast_addr(union ibv_gid *addr)
{
	return addr->raw[0] == 0xff ? 1 : 0;
}

static inline void rdma_get_mcast_mac(__in union ibv_gid *addr,__out char *mac)
{
	int i;

	mac[0] = 0x33;
	mac[1] = 0x33;
	for (i = 2; i < 6; ++i)
		mac[i] = addr->raw[i + 10];

}

ib_api_status_t mlx4_ib_resolve_grh(__in const struct ibv_ah_attr *ah_attr,
			__out char *mac,__out int *is_mcast)
{
	ib_api_status_t err = IB_SUCCESS;
	union ibv_gid dst;

	*is_mcast = 0;
	memcpy(dst.raw, ah_attr->grh.dgid.raw, sizeof(ah_attr->grh.dgid.raw));

	if (rdma_link_local_addr(&dst))
		rdma_get_ll_mac(&dst, mac);
	else if (rdma_is_multicast_addr(&dst)) {
		rdma_get_mcast_mac(&dst, mac);
		*is_mcast = 1;
	} else {
		err = IB_INVALID_PARAMETER; //jyang:todo
	}
	return err;
}

#ifdef XRC_SUPPORT
ib_api_status_t  
mlx4_pre_create_xrc_srq (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	ib_xrcd_handle_t 			h_uvp_xrcd,
	IN		const	ib_srq_attr_t				*p_srq_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_srq_handle_t			*ph_uvp_srq )
{
	struct mlx4_srq *srq;
	struct ibv_create_srq *p_create_srq;
	struct ibv_pd *pd = (struct ibv_pd *)h_uvp_pd;
	struct ibv_xrc_domain *xrc_domain = (struct ibv_xrc_domain *)h_uvp_xrcd;
	ib_api_status_t status = IB_SUCCESS;
	size_t size = max( sizeof(struct ibv_create_srq), sizeof(struct ibv_create_srq_resp) );

	CL_ASSERT(p_umv_buf);

	p_umv_buf->p_inout_buf = cl_malloc( size ); 
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = sizeof(struct ibv_create_srq);
	p_umv_buf->output_size = sizeof(struct ibv_create_srq_resp);
	p_umv_buf->command = TRUE;

	p_create_srq = p_umv_buf->p_inout_buf;
	
	// Mlx4 code:

	/* Sanity check SRQ size before proceeding */
	if (p_srq_attr->max_wr > 1 << 16 || p_srq_attr->max_sge > 64)
	{
		status = IB_INVALID_PARAMETER;
		goto err_params;
	}

	srq = cl_malloc(sizeof *srq);
	if (!srq) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_srq;
	}

	if (cl_spinlock_init(&srq->lock)) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_lock;
	}

	srq->ibv_srq.pd 		= pd;
	srq->ibv_srq.context	= pd->context;
	
	srq->max     	= __align_queue_size(p_srq_attr->max_wr + 1);
	srq->max_gs  = p_srq_attr->max_sge;
	srq->counter	= 0;

	if (mlx4_alloc_srq_buf(pd, (struct ibv_srq_attr *)p_srq_attr, srq))
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_buf;
	}

	srq->db = mlx4_alloc_db(to_mctx(pd->context), MLX4_DB_TYPE_RQ);
	if (!srq->db) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_db;
	}
	*srq->db = 0;
	
	// fill the parameters for ioctl
	p_create_srq->buf_addr = (uintptr_t) srq->buf.buf;
	p_create_srq->db_addr  = (uintptr_t) srq->db;
	p_create_srq->pd_handle = pd->handle;
	p_create_srq->max_wr = p_srq_attr->max_wr;
	p_create_srq->max_sge = p_srq_attr->max_sge;
	p_create_srq->srq_limit = p_srq_attr->srq_limit;

	*ph_uvp_srq = (ib_srq_handle_t)&srq->ibv_srq;
	goto end;

err_alloc_db:
	cl_free(srq->wrid);
	mlx4_free_buf(&srq->buf);
err_alloc_buf:
	cl_spinlock_destroy(&srq->lock);
err_lock:
	cl_free(srq);
err_alloc_srq:
	cl_free(p_umv_buf->p_inout_buf);
err_params: err_memory:
end:
	return status;
}

ib_api_status_t  
mlx4_post_create_xrc_srq (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_srq_handle_t			*ph_uvp_srq,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	struct mlx4_srq *srq = (struct mlx4_srq *)*ph_uvp_srq;
	struct ibv_create_srq_resp *p_resp;
	ib_api_status_t status = IB_SUCCESS;

	UNREFERENCED_PARAMETER(h_uvp_pd);
	
	CL_ASSERT(p_umv_buf && p_umv_buf->p_inout_buf);
	
	p_resp = p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status)
	{
		// Mlx4 code:

		srq->ibv_srq.xrc_srq_num	= srq->srqn = p_resp->srqn;
		srq->ibv_srq.handle		= p_resp->srq_handle;

		srq->max		= p_resp->max_wr;
		srq->max_gs	= p_resp->max_sge;
		
		if (mlx4_store_xrc_srq(to_mctx(pd->context), srq->ibv_srq.xrc_srq_num, srq))
		{
			mlx4_post_destroy_srq(*ph_uvp_srq, IB_SUCCESS);
			status = IB_INSUFFICIENT_MEMORY;
		}	
	}
	else
	{
		mlx4_post_destroy_srq (*ph_uvp_srq, IB_SUCCESS);
		*ph_uvp_srq = NULL;
	}

	cl_free( p_resp );
	return status;
}

ib_api_status_t
mlx4_pre_open_xrc_domain (
	IN		const 	ib_ca_handle_t			h_uvp_ca,
	IN		const	uint32_t					oflag,
	IN	OUT 		ci_umv_buf_t				*p_umv_buf,
		OUT			ib_xrcd_handle_t			*ph_uvp_xrcd )
{
	struct mlx4_xrc_domain *xrcd;
	struct ibv_context * context = (struct ibv_context *)h_uvp_ca;
	struct ibv_open_xrc_domain	*p_open_xrcd;
	ib_api_status_t status = IB_SUCCESS;
	int size = max( sizeof(struct ibv_open_xrc_domain), sizeof(struct ibv_open_xrc_domain_resp) );

	CL_ASSERT(h_uvp_ca && p_umv_buf);

	p_umv_buf->p_inout_buf = cl_malloc( size );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_umv_buf;
	}
	p_umv_buf->input_size = sizeof(struct ibv_open_xrc_domain);
	p_umv_buf->output_size = sizeof(struct ibv_open_xrc_domain_resp);
	p_umv_buf->command = TRUE;

	p_open_xrcd = p_umv_buf->p_inout_buf;

	// Mlx4 code:

	xrcd = cl_malloc(sizeof *xrcd);
	if (!xrcd) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_xrc;
	}

	xrcd->ibv_xrcd.context = context;
	
	p_open_xrcd->oflags = oflag;

	*ph_uvp_xrcd = (struct ibv_xrc_domain *)&xrcd->ibv_xrcd;
	goto end;

err_xrc:
	cl_free(p_umv_buf->p_inout_buf);
err_umv_buf:
end:
	return status;
}

void
mlx4_post_open_xrc_domain (
	IN		const	ib_ca_handle_t			h_uvp_ca,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_xrcd_handle_t			*ph_uvp_xrcd,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	struct ibv_xrc_domain *xrcd = (struct ibv_xrc_domain *)*ph_uvp_xrcd;
	struct ibv_open_xrc_domain_resp *p_resp;

	UNREFERENCED_PARAMETER(h_uvp_ca);
	
	CL_ASSERT(p_umv_buf && p_umv_buf->p_inout_buf);

	p_resp = p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status)
	{
		// Mlx4 code:
		
		xrcd->handle = p_resp->xrcd_handle;
		to_mxrcd(xrcd)->xrcdn = p_resp->xrcdn;
	}
	else
	{
		cl_free(to_mxrcd(xrcd));
		*ph_uvp_xrcd = NULL;
	}
	
	cl_free(p_resp);
	return;
}

void
mlx4_post_close_xrc_domain (
	IN		const	ib_xrcd_handle_t			h_uvp_xrcd,
	IN				ib_api_status_t			ioctl_status )
{
	struct ibv_xrc_domain *xrdc = (struct ibv_xrc_domain *)h_uvp_xrcd;

	CL_ASSERT(xrdc);

	if (IB_SUCCESS == ioctl_status) {
		cl_free(to_mxrcd(xrdc));
	}
}
#endif
