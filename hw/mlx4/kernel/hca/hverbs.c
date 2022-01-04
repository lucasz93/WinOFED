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


#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hverbs.tmh"
#endif


/* Memory regions */

struct ib_mr *ib_get_dma_mr(struct ib_pd *pd, enum ib_access_flags mr_access_flags)
{
	struct ib_mr *mr;

 	mr = pd->device->get_dma_mr(pd, mr_access_flags);
 
	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		mr->p_uctx = pd->p_uctx;
		atomic_inc(&pd->usecnt);
		atomic_set(&mr->usecnt, 0);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("pdn %d, usecnt %d \n", 
			((struct mlx4_ib_pd*)pd)->pdn, pd->usecnt));
	}

	return mr;
}

struct ib_mr *ib_reg_phys_mr(struct ib_pd *pd,
				  struct ib_phys_buf *phys_buf_array,
				  int num_phys_buf,
				  enum ib_access_flags mr_access_flags,
				  u64 *iova_start)
{
	struct ib_mr *mr;

	if ( pd->device->reg_phys_mr ) {
		mr = pd->device->reg_phys_mr(pd, phys_buf_array, num_phys_buf,
			mr_access_flags, iova_start);
    }
	else {
		mr = ERR_PTR(-ENOSYS);
    }

	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd 	 = pd;
		mr->p_uctx = pd->p_uctx;
		atomic_inc(&pd->usecnt);
		atomic_set(&mr->usecnt, 0);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mlx4_ib_pd*)pd)->pdn, pd->usecnt));
	}

	return mr;
}


static inline void commit_mr(struct ib_pd *pd, struct ib_mr *ib_mr)
{
	ib_mr->device  = pd->device;
	ib_mr->pd      = pd;
	atomic_inc(&pd->usecnt);
	atomic_set(&ib_mr->usecnt, 0);
	HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
		((struct mlx4_ib_pd*)pd)->pdn, pd->usecnt, pd, pd->p_uctx));
}


 struct ib_mr *ibv_reg_mr(struct ib_pd *pd, 
	u64 start, u64 length,
	u64 virt_addr,
	int mr_access_flags)
{
	struct ib_mr *ib_mr;
	HCA_ENTER(HCA_DBG_MEMORY);

		ib_mr = pd->device->reg_user_mr(pd, start, length, virt_addr, mr_access_flags);

	if (IS_ERR(ib_mr)) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_MEMORY ,("ibv_reg_mr failed (%d)\n", PTR_ERR(ib_mr)));
	}
	else {
        commit_mr(pd, ib_mr);
	}

	HCA_EXIT(HCA_DBG_MEMORY);
	return ib_mr;
}


struct ib_mr *ibv_reg_mdl(struct ib_pd *pd,
    MDL *mdl, u64 length,
    int mr_access_flags)
{
	struct ib_mr *ib_mr;
	HCA_ENTER(HCA_DBG_MEMORY);

	ib_mr = pd->device->x.reg_krnl_mr(pd, mdl, length, mr_access_flags);

	if (IS_ERR(ib_mr)) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_MEMORY ,("ibv_reg_mdl failed (%d)\n", PTR_ERR(ib_mr)));
	}
    else {
        commit_mr(pd, ib_mr);
	}

	HCA_EXIT(HCA_DBG_MEMORY);
	return ib_mr;
}


struct ib_mr *ib_alloc_fast_reg_mr(struct ib_pd *pd, int max_page_list_len)
{
	struct ib_mr *mr;

	mr = pd->device->x.alloc_fast_reg_mr(pd, max_page_list_len);

	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		atomic_inc(&pd->usecnt);
		atomic_set(&mr->usecnt, 0);
	}

	return mr;
}

ib_fast_reg_page_list_t *ib_alloc_fast_reg_page_list(struct ib_device *device,
							  int max_page_list_len)
{
	ib_fast_reg_page_list_t *page_list;

	page_list = device->x.alloc_fast_reg_page_list(device, max_page_list_len);

	if (!IS_ERR(page_list)) {
		page_list->device = device;
		page_list->max_page_list_len = max_page_list_len;
	}

	return page_list;
}

int ib_dereg_mr(struct ib_mr *mr)
{
	int ret;
	struct ib_pd *pd;
	struct ib_device *p_ibdev;

	if (atomic_read(&mr->usecnt))
		return -EBUSY;

	p_ibdev = mr->device;
	pd = mr->pd;
	ret = p_ibdev->dereg_mr(mr);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_MEMORY ,("pdn %d, usecnt %d, pd_handle %p, ctx %p \n", 
			((struct mlx4_ib_pd*)pd)->pdn, pd->usecnt, pd, pd->p_uctx));
	}

	return ret;
}

static void release_user_cq_qp_resources(
	struct ib_ucontext	*p_uctx)
{
	if (p_uctx) {
		atomic_dec(&p_uctx->x.usecnt);
		if (!atomic_read(&p_uctx->x.usecnt) && p_uctx->closing) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_SHIM	,("User resources are released. Removing context\n"));
			ibv_um_close(p_uctx);
		}
	}
}

//
// Completion queues
//

struct ib_cq *ibv_create_cq(struct ib_device *p_ibdev,
			   ib_comp_handler comp_handler,
			   void (*event_handler)(ib_event_rec_t *),
			   void *cq_context, int cqe, 
			   struct ib_ucontext *p_uctx, ci_umv_buf_t* const p_umv_buf)
{
	int err;
	struct ib_cq *cq;
	struct ib_udata udata, *p_udata = &udata;
	struct ibv_create_cq *p_req;
	struct ibv_create_cq_resp *p_resp = NULL;

	if ( p_uctx && p_umv_buf && p_umv_buf->p_inout_buf ) {
		// prepare user parameters
		p_req = (struct ibv_create_cq*)(ULONG_PTR)p_umv_buf->p_inout_buf;
		p_resp = (struct ibv_create_cq_resp*)(ULONG_PTR)
			p_umv_buf->p_inout_buf;
		INIT_UDATA(&udata, p_req, p_resp, 
			sizeof(struct ibv_create_cq), sizeof(struct ibv_create_cq_resp));
	}
	else {
		p_udata = NULL;
    }

	// create cq
	cq = p_ibdev->create_cq(p_ibdev, cqe, 0, p_uctx, p_udata);
	if (IS_ERR(cq)) {
		err = PTR_ERR(cq);
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_CQ ,("create_cq failed (%d)\n", err));
		goto err_create_cq;
	}

	cq->device        = p_ibdev;
	cq->p_uctx        = p_uctx;
	cq->comp_handler  = comp_handler;
	cq->event_handler = event_handler;
	cq->cq_context    = cq_context;
	atomic_set(&cq->usecnt, 0);
	if (p_uctx)
		atomic_inc(&p_uctx->x.usecnt);

	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_CQ ,
		("created CQ: cqn %#x:%#x \n", ((struct mlx4_ib_cq*)cq)->mcq.cqn, cq->cqe ));

	// fill results
	if (p_umv_buf) {
		p_resp->cqe = cq->cqe;
		p_umv_buf->output_size = sizeof(struct ibv_create_cq_resp);
	}
	
	return cq;

err_create_cq:
	if( p_umv_buf && p_umv_buf->command ) 
		p_umv_buf->status = IB_ERROR;
	return ERR_PTR(err);
}

struct ib_cq *ibv_create_cq_ex(struct ib_device *p_ibdev,
			   ib_comp_handler comp_handler,
			   void (*event_handler)(ib_event_rec_t *),
			   void *cq_context, ib_group_affinity_t *affinity, int cqe, 
			   struct ib_ucontext *p_uctx, ci_umv_buf_t* const p_umv_buf)
{
	int err;
	struct ib_cq *cq;
	struct ib_udata udata, *p_udata = &udata;
	struct ibv_create_cq *p_req;
	struct ibv_create_cq_resp *p_resp = NULL;

	if ( p_uctx && p_umv_buf && p_umv_buf->p_inout_buf ) {
		// prepare user parameters
		p_req = (struct ibv_create_cq*)(ULONG_PTR)p_umv_buf->p_inout_buf;
		p_resp = (struct ibv_create_cq_resp*)(ULONG_PTR)
			p_umv_buf->p_inout_buf;
		INIT_UDATA(&udata, p_req, p_resp, 
			sizeof(struct ibv_create_cq), sizeof(struct ibv_create_cq_resp));
	}
	else 
		p_udata = NULL;

	// create cq
	cq = p_ibdev->x.create_cq_ex(p_ibdev, cqe, affinity, p_uctx, p_udata);
	if (IS_ERR(cq)) {
		err = PTR_ERR(cq);
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_CQ ,("create_cq failed (%d)\n", err));
		goto err_create_cq;
	}

	cq->device        = p_ibdev;
	cq->p_uctx        = p_uctx;
	cq->comp_handler  = comp_handler;
	cq->event_handler = event_handler;
	cq->cq_context    = cq_context;
	atomic_set(&cq->usecnt, 0);
	if (p_uctx)
		atomic_inc(&p_uctx->x.usecnt);

	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_CQ ,
		("created CQ: cqn %#x:%#x \n", ((struct mlx4_ib_cq*)cq)->mcq.cqn, cq->cqe ));

	// fill results
	if (p_umv_buf) {
		p_resp->cqe = cq->cqe;
		p_umv_buf->output_size = sizeof(struct ibv_create_cq_resp);
	}
	
	return cq;

err_create_cq:
	if( p_umv_buf && p_umv_buf->command ) 
		p_umv_buf->status = IB_ERROR;
	return ERR_PTR(err);
}

int ib_destroy_cq(struct ib_cq *cq)
{
	int ret;
	struct ib_ucontext	*p_uctx = cq->p_uctx;
	
	if (atomic_read(&cq->usecnt))
		return -EBUSY;

	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_CQ ,
		("destroying CQ: cqn %#x:%#x \n", ((struct mlx4_ib_cq*)cq)->mcq.cqn, cq->cqe ));

	ret = cq->device->destroy_cq(cq);
	release_user_cq_qp_resources(p_uctx);
	return ret;
}

//
// Queue pairs 
//

static char *__print_qtype(enum ib_qp_type qtype)
{
	char *str = NULL;
	switch (qtype) {
		case IB_QPT_SMI: str = "SMI"; break;
		case IB_QPT_GSI: str = "GSI"; break;
		case IB_QPT_RC: str = "RC"; break;
		case IB_QPT_UC: str = "UC"; break;
		case IB_QPT_UD: str = "UD"; break;
		case IB_QPT_RAW_IP_V6: str = "IP_V6"; break;
		case IB_QPT_RAW_ETY: str = "ETY"; break;
		default: str = "UKNWN"; break;
	}
	return str;
}

struct ib_qp *ibv_create_qp(struct ib_pd *pd,
	struct ib_qp_init_attr *qp_init_attr,
	struct ib_ucontext *p_uctx, ci_umv_buf_t* const p_umv_buf)
{
	int err;
	struct ib_qp *p_ib_qp;
	struct ib_udata udata, *p_udata = &udata;
	struct ibv_create_qp *p_req = NULL;
	struct ibv_create_qp_resp *p_resp= NULL;

	HCA_ENTER(HCA_DBG_QP);

	if ( p_uctx && p_umv_buf && p_umv_buf->command ) {
		// prepare user parameters
		p_req = (struct ibv_create_qp*)(ULONG_PTR)p_umv_buf->p_inout_buf;
		p_resp = (struct ibv_create_qp_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;
		INIT_UDATA(&udata, p_req, NULL, 
			sizeof(struct ibv_create_qp), 0);
	}
	else {
		p_udata = NULL;
    }

	p_ib_qp = pd->device->create_qp( pd, qp_init_attr, p_udata );

	if (IS_ERR(p_ib_qp)) {
		err = PTR_ERR(p_ib_qp);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP ,("create_qp failed (%d)\n", err));
		goto err_create_qp;
	}

	// fill results
	p_ib_qp->device     	  		= pd->device;
	p_ib_qp->pd         	  		= pd;
	p_ib_qp->send_cq    	  		= qp_init_attr->send_cq;
	p_ib_qp->recv_cq    	  		= qp_init_attr->recv_cq;
	p_ib_qp->srq	       	  		= qp_init_attr->srq;
	p_ib_qp->p_uctx   				= p_uctx;
	p_ib_qp->event_handler 			= qp_init_attr->event_handler;
	p_ib_qp->qp_context    			= qp_init_attr->qp_context;
	p_ib_qp->qp_type	  			= qp_init_attr->qp_type;
	atomic_inc(&pd->usecnt);
	atomic_inc(&qp_init_attr->send_cq->usecnt);
	atomic_inc(&qp_init_attr->recv_cq->usecnt);
	if (qp_init_attr->srq)
		atomic_inc(&qp_init_attr->srq->usecnt);
	if (p_uctx)
		atomic_inc(&p_uctx->x.usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_QP ,("pdn %d, usecnt %d, pd_handle %p, ctx %p \n", 
			((struct mlx4_ib_pd*)pd)->pdn, pd->usecnt, pd, pd->p_uctx));

	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_QP ,
		("qtype %s (%d), qnum %#x, q_num  %#x, ssz %d, rsz %d, scq %#x:%#x, rcq %#x:%#x, port_num %d \n",
		__print_qtype(p_ib_qp->qp_type), p_ib_qp->qp_type,
		((struct mlx4_ib_qp*)p_ib_qp)->mqp.qpn, p_ib_qp->qp_num, 
		qp_init_attr->cap.max_send_wr, qp_init_attr->cap.max_recv_wr,
		((struct mlx4_ib_cq*)p_ib_qp->send_cq)->mcq.cqn, p_ib_qp->send_cq->cqe,
		((struct mlx4_ib_cq*)p_ib_qp->recv_cq)->mcq.cqn, p_ib_qp->recv_cq->cqe,
		qp_init_attr->port_num
		) );

	// fill results for user
	if (p_uctx && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct mlx4_ib_qp *p_mib_qp = (struct mlx4_ib_qp *)p_ib_qp;
		p_resp->qp_handle = (__u64)(ULONG_PTR)p_ib_qp;
		p_resp->qpn = p_mib_qp->mqp.qpn;
		p_resp->max_send_wr = p_mib_qp->sq.max_post;
		p_resp->max_recv_wr = p_mib_qp->rq.max_post;
		p_resp->max_send_sge = p_mib_qp->sq.max_gs;
		p_resp->max_recv_sge = p_mib_qp->rq.max_gs;
		p_resp->max_inline_data = p_mib_qp->max_inline_data;
		p_umv_buf->output_size = sizeof(struct ibv_create_qp_resp);
	}

	return p_ib_qp;

err_create_qp:
	if( p_umv_buf && p_umv_buf->command ) 
		p_umv_buf->status = IB_ERROR;
	HCA_EXIT(HCA_DBG_QP);
	return ERR_PTR(err);
}

int ib_destroy_qp(struct ib_qp *qp)
{
	struct ib_pd *p_ib_pd;
	struct ib_cq *scq, *rcq;
	struct ib_srq *srq;
	struct ib_ucontext	*p_uctx;
	int ret;

	p_ib_pd  = qp->pd;
	scq = qp->send_cq;
	rcq = qp->recv_cq;
	srq = qp->srq;
	p_uctx = p_ib_pd->p_uctx;

	ret = qp->device->destroy_qp(qp);
	if (!ret) {
		atomic_dec(&p_ib_pd->usecnt);
		atomic_dec(&scq->usecnt);
		atomic_dec(&rcq->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_QP ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mlx4_ib_pd*)p_ib_pd)->pdn, p_ib_pd->usecnt, p_ib_pd, p_ib_pd->p_uctx));
		if (srq)
			atomic_dec(&srq->usecnt);
		release_user_cq_qp_resources(p_uctx);
	}

	return ret;
}

//
// Shared receive queues
//


/* Shared receive queues */

struct ib_srq *ibv_create_srq(struct ib_pd *pd,
	struct ib_srq_init_attr *srq_init_attr,
	struct ib_ucontext *p_uctx, ci_umv_buf_t* const p_umv_buf)
{
	int err;
	struct ib_srq *p_ib_srq;
	struct ib_udata udata, *p_udata = &udata;
	struct ibv_create_srq *p_req = NULL;
	struct ibv_create_srq_resp *p_resp= NULL;

	if ( p_uctx && p_umv_buf && p_umv_buf->p_inout_buf) {
		// prepare user parameters
		p_req = (struct ibv_create_srq*)(ULONG_PTR)p_umv_buf->p_inout_buf;
		p_resp = (struct ibv_create_srq_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;
		INIT_UDATA(&udata, p_req, p_resp, 
			sizeof(struct ibv_create_srq), sizeof(struct ibv_create_srq_resp));
	}
	else {
		p_udata = NULL;
    }

	p_ib_srq = pd->device->create_srq( pd, srq_init_attr, p_udata );
	if (IS_ERR(p_ib_srq)) {
		err = PTR_ERR(p_ib_srq);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP ,("create_srq failed (%d)\n", err));
		goto err_create_srq;
	}

	// fill results
	p_ib_srq->device     	  		= pd->device;
	p_ib_srq->pd         	  		= pd;
	p_ib_srq->p_uctx 				= p_uctx;
	p_ib_srq->event_handler 		= srq_init_attr->event_handler;
	p_ib_srq->srq_context    		= srq_init_attr->srq_context;
	atomic_inc(&pd->usecnt);
	atomic_set(&p_ib_srq->usecnt, 0);
	if (p_uctx)
		atomic_inc(&p_uctx->x.usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_QP ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mlx4_ib_pd*)pd)->pdn, pd->usecnt, pd, pd->p_uctx));

	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_SRQ ,
		("uctx %p, qhndl %p, qnum %#x \n", 
		pd->p_uctx, p_ib_srq, ((struct mlx4_ib_srq*)p_ib_srq)->msrq.srqn ) );

	// fill results for user
	if (p_uctx && p_umv_buf && p_umv_buf->p_inout_buf) {
		p_umv_buf->output_size = sizeof(struct ibv_create_srq_resp);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_QP ,("PD%d use cnt %d \n", 
			((struct mlx4_ib_pd*)pd)->pdn, pd->usecnt));
	}

	return p_ib_srq;
	
err_create_srq:
	if( p_umv_buf && p_umv_buf->command ) 
		p_umv_buf->status = IB_ERROR;
	HCA_EXIT(HCA_DBG_QP);
	return ERR_PTR(err);
}

int ib_destroy_srq(struct ib_srq *srq)
{
	int ret;
	struct ib_pd *p_ib_pd = srq->pd;
	struct ib_ucontext	*p_uctx = p_ib_pd->p_uctx;

	ret = srq->device->destroy_srq(srq);
	if (!ret) {
		atomic_dec(&p_ib_pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_SRQ ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mlx4_ib_pd*)p_ib_pd)->pdn, p_ib_pd->usecnt, p_ib_pd, p_ib_pd->p_uctx));
		release_user_cq_qp_resources(p_uctx);
	}

	return ret;
}

//
// User context
//
static NTSTATUS __map_memory_for_user(
	IN		io_addr_t	addr,
	IN		SIZE_T		size,
	IN		MEMORY_CACHING_TYPE mem_type,
	IN		KPROCESSOR_MODE mode,
	OUT		umap_t	*	p_map
	)
{
	NTSTATUS status;

	HCA_ENTER(HCA_DBG_SHIM);

	// map UAR to kernel 
	p_map->kva = ioremap(addr, size, mem_type);
	if (!p_map->kva) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_LOW ,
			("Couldn't map kernel access region, aborting.\n") );
		status = IB_INSUFFICIENT_MEMORY;
		goto err_ioremap;
	}

	if( mode == KernelMode )
	{
		p_map->mdl = NULL;
		p_map->uva = p_map->kva;
		status = STATUS_SUCCESS;
		goto done;
	}

	// build MDL 
	p_map->mdl = IoAllocateMdl( p_map->kva, (ULONG)size,
		FALSE, TRUE, NULL );
	if( !p_map->mdl ) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_mdl;
	}
	MmBuildMdlForNonPagedPool( p_map->mdl );

	/* Map the memory into the calling process's address space. */
	__try	{
		p_map->uva = MmMapLockedPagesSpecifyCache( p_map->mdl,
			UserMode, mem_type, NULL, FALSE, NormalPagePriority );
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		status = IB_INVALID_PERMISSION;
		goto err_map;
	}

	status = STATUS_SUCCESS;
	goto done;

err_map:
	IoFreeMdl(p_map->mdl);
	p_map->mdl = NULL;

err_alloc_mdl:	
	iounmap(p_map->kva, PAGE_SIZE);
	p_map->kva = NULL;

err_ioremap:
done:	
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}

static void __unmap_memory_for_user(
	IN		umap_t	*	p_map
	)
{
	if (p_map->mdl != NULL) {
		MmUnmapLockedPages( p_map->uva, p_map->mdl );
		IoFreeMdl(p_map->mdl);
		p_map->mdl = NULL;
	}
	if( p_map->kva != NULL ){
		iounmap(p_map->kva, PAGE_SIZE);
		p_map->kva = NULL;
	}
}

ib_api_status_t ibv_um_open(	
	IN 			struct ib_device 		*	p_ibdev,
	IN			KPROCESSOR_MODE				mode,
	IN	OUT		ci_umv_buf_t* const			p_umv_buf,
	OUT			struct ib_ucontext 		**	pp_uctx )
{
	int err;
	ib_api_status_t		status;
	struct mlx4_ib_ucontext *p_muctx;
    struct ibv_get_context_req *p_ureq;
	struct ibv_get_context_resp *p_uresp;
	struct mlx4_ib_alloc_ucontext_resp ib_alloc_ucontext_resp;
	struct ib_ucontext 		*p_uctx;
	struct ib_udata udata;

	HCA_ENTER(HCA_DBG_SHIM);

    p_ureq = (struct ibv_get_context_req*)(ULONG_PTR)p_umv_buf->p_inout_buf;
    if( NdValidateIoSpaceMapping( &p_ureq->mappings[ibv_get_context_uar],
                                  NdNonCached, PAGE_SIZE ) != STATUS_SUCCESS )
    {
        status = IB_INVALID_PARAMETER;
        goto end;
    }
    if( NdValidateIoSpaceMapping( &p_ureq->mappings[ibv_get_context_bf],
                                  NdWriteCombined, PAGE_SIZE ) != STATUS_SUCCESS )
    {
        status = IB_INVALID_PARAMETER;
        goto end;
    }

	// create user context in kernel
	INIT_UDATA(&udata, NULL, &ib_alloc_ucontext_resp, 
		0, sizeof(struct mlx4_ib_alloc_ucontext_resp));

	p_uctx = p_ibdev->alloc_ucontext(p_ibdev, &udata);
	if (IS_ERR(p_uctx)) {
		err = PTR_ERR(p_uctx);
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_SHIM,
			("mthca_alloc_ucontext failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc_ucontext;
	}
	p_muctx = to_mucontext(p_uctx);
	p_uresp = (struct ibv_get_context_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;

	// fill the rest of ib_ucontext fields 
	p_uctx->device = p_ibdev;
	p_uctx->closing = 0;
	p_uctx->x.mode = mode;

	// map uar to user space
	status = __map_memory_for_user( 
		(io_addr_t)p_muctx->uar.pfn << PAGE_SHIFT, 
		PAGE_SIZE, MmNonCached, mode, &p_uctx->x.uar );
	if( !NT_SUCCESS(status) ) {
		goto err_map_uar;
	}
    p_uresp->mapping_results[ibv_get_context_uar].Id = 0;
	p_uresp->mapping_results[ibv_get_context_uar].Information = (u64)(ULONG_PTR)p_uctx->x.uar.uva;
	HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_SHIM,
		("UAR: pa 0x%I64x, size %#x, kva 0x%I64x, uva 0x%I64x, num_uars %#x \n", 
			(io_addr_t)p_muctx->uar.pfn << PAGE_SHIFT,
			PAGE_SIZE,
			(UINT64)(ULONG_PTR)p_uctx->x.uar.kva,
			(UINT64)(ULONG_PTR)p_uctx->x.uar.uva,
			to_mdev(p_ibdev)->dev->caps.num_uars
			));

	// map BF to user space
	p_uresp->mapping_results[ibv_get_context_bf].Id = 0;
	if (ib_alloc_ucontext_resp.bf_reg_size) {
		status = __map_memory_for_user( 
			(io_addr_t)(p_muctx->uar.pfn + 
			to_mdev(p_ibdev)->dev->caps.num_uars) << PAGE_SHIFT, 
			PAGE_SIZE, MmWriteCombined, mode, &p_uctx->x.bf );
		if( !NT_SUCCESS(status) ) {
			HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_SHIM,
				("BlueFlame available, but failed to be mapped (%#x)\n", status));
			p_uresp->mapping_results[ibv_get_context_bf].Information = 0;
			p_uresp->bf_buf_size = 0;
		} 
		else {
			p_uresp->mapping_results[ibv_get_context_bf].Information = (u64)(ULONG_PTR)p_uctx->x.bf.uva;
			p_uresp->bf_buf_size = ib_alloc_ucontext_resp.bf_reg_size / 2;
			p_uresp->bf_offset	 = 0;
			HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_SHIM,
				("BF:  pa 0x%I64x, size %#x, kva 0x%I64x, uva 0x%I64x, num_uars %#x \n", 
					(io_addr_t)(p_muctx->uar.pfn + to_mdev(p_ibdev)->dev->caps.num_uars) << PAGE_SHIFT,
					PAGE_SIZE,
					(UINT64)(ULONG_PTR)p_uctx->x.bf.kva,
					(UINT64)(ULONG_PTR)p_uctx->x.bf.uva,
					to_mdev(p_ibdev)->dev->caps.num_uars
					));
		}
	}
	else {
			p_uresp->mapping_results[ibv_get_context_bf].Information = 0;
			p_uresp->bf_buf_size = 0;
	}

	// fill the response
	p_uresp->bf_reg_size		 = ib_alloc_ucontext_resp.bf_reg_size;
	p_uresp->bf_regs_per_page	 = ib_alloc_ucontext_resp.bf_regs_per_page;
	p_uresp->qp_tab_size		 = ib_alloc_ucontext_resp.qp_tab_size;
	p_uresp->cqe_size			 = ib_alloc_ucontext_resp.cqe_size;

	*pp_uctx = p_uctx;
	status = IB_SUCCESS;
	goto end;

err_map_uar:
	p_ibdev->dealloc_ucontext(p_uctx);
err_alloc_ucontext: 
end:
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}


void ibv_um_close(	struct ib_ucontext * h_um_ca )
{
	int err;
	ib_api_status_t		status;
	struct ib_ucontext *p_uctx = (struct ib_ucontext *)h_um_ca;
	PHCA_FDO_DEVICE_DATA p_fdo = p_uctx->device->x.p_fdo;

	HCA_ENTER(HCA_DBG_SHIM);

	p_uctx->closing = 1;

	if (atomic_read(&p_uctx->x.usecnt)) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("resources are not released (cnt %d)\n", p_uctx->x.usecnt));
		status = IB_RESOURCE_BUSY;
		goto err_usage;
	}
	
	__unmap_memory_for_user( &p_uctx->x.bf );
	__unmap_memory_for_user( &p_uctx->x.uar );

	err = p_fdo->p_bus_ib_ifc->p_ibdev->dealloc_ucontext(p_uctx);
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("mthca_dealloc_ucontext failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_dealloc_ucontext;
	}

	HCA_PRINT(TRACE_LEVEL_INFORMATION,HCA_DBG_SHIM,
		("pcs %p\n", PsGetCurrentProcess()) );
	status = IB_SUCCESS;
	goto end;
	
err_dealloc_ucontext: 
err_usage:
end:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("completes with ERROR status %x\n", status));
	}
	HCA_EXIT(HCA_DBG_SHIM);
	return;
}

