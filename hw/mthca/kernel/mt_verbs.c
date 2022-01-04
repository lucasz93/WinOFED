/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
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

#include <ib_verbs.h>
#include <ib_cache.h>
#include "mthca_dev.h"
#include "mx_abi.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mt_verbs.tmh"
#endif


void ibv_um_close(	struct ib_ucontext * h_um_ca )
{
	int err;
	ib_api_status_t		status;
	struct ib_ucontext *context_p = (struct ib_ucontext *)h_um_ca;

	HCA_ENTER(HCA_DBG_SHIM);

	context_p->is_removing = TRUE;

	if (atomic_read(&context_p->usecnt)) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("resources are not released (cnt %d)\n", context_p->usecnt));
		status = IB_RESOURCE_BUSY;
		goto err_usage;
	}
	
	err = ibv_dealloc_pd( context_p->pd );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("ibv_dealloc_pd failed (%d)\n", err));
		status = errno_to_iberr(err);
	}

	err = mthca_dealloc_ucontext(context_p);
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

/* Protection domains */

struct ib_pd *ibv_alloc_pd(struct ib_device *device,
	struct ib_ucontext *context, ci_umv_buf_t* const p_umv_buf)
{
	struct ib_pd *pd;

	// direct call is a must, because "lifefish" devices doesn't fill driver i/f table
	pd = mthca_alloc_pd(device, context, p_umv_buf);

	if (!IS_ERR(pd)) {
		pd->device  = device;
		pd->ucontext = context;
		atomic_set(&pd->usecnt, 0);
		KeInitializeMutex( &pd->mutex, 0 );
		INIT_LIST_HEAD( &pd->list );
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_CQ ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));
	}

	return pd;
}

int ibv_dealloc_pd(struct ib_pd *pd)
{
	// we need first to release list of AV MRs to decrease pd->usecnt
	if (pd->ucontext) {
		struct ib_mr *ib_mr, *tmp;
		down(&pd->mutex );
		list_for_each_entry_safe(ib_mr, tmp, &pd->list, list,struct ib_mr,struct ib_mr) {
			ibv_dereg_mr( ib_mr );
		}
		up(&pd->mutex );
	}

	if (atomic_read(&pd->usecnt)) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_CQ,("resources are not released (cnt %d)\n", pd->usecnt));
		return -EBUSY;
	}		

	HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_CQ ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
		((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));
	// direct call is a must, because "lifefish" devices doesn't fill driver i/f table
	return mthca_dealloc_pd(pd);
}

/* Address handles */

struct ib_ah *ibv_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr,
	struct ib_ucontext *context, ci_umv_buf_t* const p_umv_buf)
{
	int err;
	struct ib_ah *ah;
	struct ib_mr *ib_mr = NULL;
	u64 start = 0;
	u64 user_handle = 0;
	struct ibv_create_ah_resp *create_ah_resp = 0;

	// for user call we need also allocate MR
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct ibv_create_ah *create_ah = (struct ibv_create_ah *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		
		// create region; destroy will be done on dealloc_pd
		ib_mr	= ibv_reg_mr( 
			pd, 
			create_ah->mr.access_flags, 
			(void*)(ULONG_PTR)create_ah->mr.start,
			create_ah->mr.length, create_ah->mr.hca_va, TRUE, FALSE );
		if (IS_ERR(ib_mr)) {
			err = PTR_ERR(ib_mr);
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV	,("ibv_reg_mr failed (%d)\n", err));
			goto err_alloc_mr;
		}

		start = create_ah->mr.start;
		user_handle = create_ah->user_handle;

		// chain this MR to PD list
		down(&pd->mutex );
		list_add_tail(&ib_mr->list, &pd->list);
		up(&pd->mutex );
	}

	ah = pd->device->create_ah(pd, ah_attr);

	/* fill obligatory fields */
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		create_ah_resp = (struct ibv_create_ah_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		create_ah_resp->user_handle = user_handle;
	}

	if (IS_ERR(ah)) {
		err = PTR_ERR(ah);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV ,("create_ah failed (%d)\n", err));
		goto err_create_ah;
	}

	// fill results
	ah->device  = pd->device;
	ah->pd      = pd;
	ah->ucontext = context;
	atomic_inc(&pd->usecnt);
	HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_AV	,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
		((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));

	// fill results for user
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct ibv_create_ah_resp *create_ah_resp = (struct ibv_create_ah_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		create_ah_resp->start = start;
		create_ah_resp->mr.lkey = ib_mr->lkey;
		create_ah_resp->mr.rkey = ib_mr->rkey;
		create_ah_resp->mr.mr_handle = (u64)(ULONG_PTR)ib_mr;
		p_umv_buf->output_size = sizeof(struct ibv_create_ah_resp);
	}

	return ah;
	
err_create_ah:
	if (ib_mr)
		ibv_dereg_mr(ib_mr);
err_alloc_mr:
	if( p_umv_buf && p_umv_buf->command ) 
		p_umv_buf->status = IB_ERROR;
	return ERR_PTR(ib_mr);
}

struct ib_ah *ibv_create_ah_from_wc(struct ib_pd *pd, struct _ib_wc *wc,
				   struct ib_grh *grh, u8 port_num)
{
	struct ib_ah_attr ah_attr;
	u32 flow_class;
	u16 gid_index;
	int ret;

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.dlid = wc->recv.ud.remote_lid;
	ah_attr.sl = wc->recv.ud.remote_sl;
	ah_attr.src_path_bits = wc->recv.ud.path_bits;
	ah_attr.port_num = port_num;

	if (wc->recv.ud.recv_opt & IB_RECV_OPT_GRH_VALID) {
		ah_attr.ah_flags = IB_AH_GRH;
		ah_attr.grh.dgid = grh->dgid;

		ret = ib_find_cached_gid(pd->device, &grh->sgid, &port_num,
					 &gid_index);
		if (ret)
			return ERR_PTR(ret);

		ah_attr.grh.sgid_index = (u8) gid_index;
		flow_class = cl_ntoh32(grh->version_tclass_flow);
		ah_attr.grh.flow_label = flow_class & 0xFFFFF;
		ah_attr.grh.traffic_class = (u8)((flow_class >> 20) & 0xFF);
		ah_attr.grh.hop_limit = grh->hop_limit;
	}

	return ibv_create_ah(pd, &ah_attr, NULL, NULL);
}

int ibv_modify_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr)
{
	return ah->device->modify_ah ?
		ah->device->modify_ah(ah, ah_attr) :
		-ENOSYS;
}

int ibv_query_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr)
{
	return ah->device->query_ah ?
		ah->device->query_ah(ah, ah_attr) :
		-ENOSYS;
}


static void release_user_cq_qp_resources(
	struct ib_ucontext	*ucontext,
	struct ib_mr * ib_mr)
{
	if (ucontext) {
		ibv_dereg_mr( ib_mr );
		atomic_dec(&ucontext->usecnt);
		if (!atomic_read(&ucontext->usecnt) && ucontext->is_removing) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_SHIM	,("User resources are released. Removing context\n"));
			ibv_um_close(ucontext);
		}
	}
}

int ibv_destroy_ah(struct ib_ah *ah)
{
	struct ib_pd *pd;
	int ret;

	HCA_ENTER(HCA_DBG_AV);
	pd = ah->pd;

	ret = ah->device->destroy_ah(ah);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_AV	,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));
	}
	HCA_EXIT(HCA_DBG_AV);
	return ret;
}

/* Shared receive queues */

struct ib_srq *ibv_create_srq(struct ib_pd *pd,
	struct ib_srq_init_attr *srq_init_attr,
	struct ib_ucontext *context, ci_umv_buf_t* const p_umv_buf)
{
	int err;
	struct ib_srq *ib_srq;
	struct ib_mr *ib_mr = NULL;
	u64 user_handle = 0;
	struct ibv_create_srq_resp *create_srq_resp = 0;

	// for user call we need also allocate MR
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct ibv_create_srq *create_srp = (struct ibv_create_srq *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		
		// create region
		ib_mr = ibv_reg_mr( 
			(struct ib_pd *)(ULONG_PTR)create_srp->mr.pd_handle, 
			create_srp->mr.access_flags, 
			(void*)(ULONG_PTR)create_srp->mr.start,
			create_srp->mr.length, create_srp->mr.hca_va, TRUE, FALSE );
		if (IS_ERR(ib_mr)) {
			err = PTR_ERR(ib_mr);
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP ,("ibv_reg_mr failed (%d)\n", err));
			goto err_alloc_mr;
		}
		create_srp->lkey = ib_mr->lkey;
		user_handle = create_srp->user_handle;
	}

	ib_srq = pd->device->create_srq(pd, srq_init_attr, p_umv_buf);

	/* fill obligatory fields */
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		create_srq_resp = (struct ibv_create_srq_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		create_srq_resp->user_handle = user_handle;
	}

	if (IS_ERR(ib_srq)) {
		err = PTR_ERR(ib_srq);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP ,("create_srq failed (%d)\n", err));
		goto err_create_srq;
	}

	// fill results
	ib_srq->device     	  		= pd->device;
	ib_srq->pd         	  		= pd;
	ib_srq->ucontext 			= context;
	ib_srq->event_handler 		= srq_init_attr->event_handler;
	ib_srq->srq_context    		= srq_init_attr->srq_context;
	atomic_inc(&pd->usecnt);
	atomic_set(&ib_srq->usecnt, 0);
	if (context)
		atomic_inc(&context->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_QP ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));

	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_SRQ ,
		("uctx %p, qhndl %p, qnum %#x \n", 
		pd->ucontext, ib_srq, ((struct mthca_srq*)ib_srq)->srqn ) );

	// fill results for user
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct mthca_srq *srq = (struct mthca_srq *)ib_srq;
		ib_srq->ib_mr = ib_mr;
		create_srq_resp->mr.lkey = ib_mr->lkey;
		create_srq_resp->mr.rkey = ib_mr->rkey;
		create_srq_resp->mr.mr_handle = (u64)(ULONG_PTR)ib_mr;
		create_srq_resp->srq_handle = (__u64)(ULONG_PTR)srq;
		create_srq_resp->max_wr = (mthca_is_memfree(to_mdev(pd->device))) ? srq->max - 1 : srq->max;
		create_srq_resp->max_sge = srq->max_gs;
		create_srq_resp->srqn= srq->srqn;
		p_umv_buf->output_size = sizeof(struct ibv_create_srq_resp);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_QP ,("PD%d use cnt %d \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt));
	}

	return ib_srq;
	
err_create_srq:
	if (ib_mr)
		ibv_dereg_mr(ib_mr);
err_alloc_mr:
	if( p_umv_buf && p_umv_buf->command ) 
		p_umv_buf->status = IB_ERROR;
	HCA_EXIT(HCA_DBG_QP);
	return ERR_PTR(err);
}

int ibv_modify_srq(struct ib_srq *srq,
	ib_srq_attr_t *srq_attr,
	ib_srq_attr_mask_t srq_attr_mask)
{
	return srq->device->modify_srq(srq, srq_attr, srq_attr_mask);
}

int ibv_query_srq(struct ib_srq *srq,
	ib_srq_attr_t *srq_attr)
{
	return srq->device->query_srq(srq, srq_attr);
}

int ibv_destroy_srq(struct ib_srq *srq)
{
	int ret;
	struct ib_pd *pd = srq->pd;
	struct ib_ucontext	*ucontext = pd->ucontext;
	struct ib_mr * ib_mr = srq->ib_mr;

	ret = srq->device->destroy_srq(srq);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_SRQ ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));
		release_user_cq_qp_resources(ucontext, ib_mr);
	}

	return ret;
}

/* Queue pairs */

struct ib_qp *ibv_create_qp(struct ib_pd *pd,
	struct ib_qp_init_attr *qp_init_attr,
	struct ib_ucontext *context, ci_umv_buf_t* const p_umv_buf)
{
	int err;
	struct ib_qp *ib_qp;
	struct ib_mr *ib_mr = NULL;
	u64 user_handle = 0;

	HCA_ENTER(HCA_DBG_QP);

	// for user call we need also allocate MR
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct ibv_create_qp *create_qp = (struct ibv_create_qp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		
		// create region
		ib_mr	= ibv_reg_mr( 
			(struct ib_pd *)(ULONG_PTR)create_qp->mr.pd_handle, 
			create_qp->mr.access_flags, 
			(void*)(ULONG_PTR)create_qp->mr.start,
			create_qp->mr.length, create_qp->mr.hca_va, TRUE, FALSE );
		if (IS_ERR(ib_mr)) {
			err = PTR_ERR(ib_mr);
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP ,("ibv_reg_mr failed (%d)\n", err));
			goto err_alloc_mr;
		}
		create_qp->lkey = ib_mr->lkey;
		user_handle = create_qp->user_handle;
	}

	ib_qp = pd->device->create_qp(pd, qp_init_attr, p_umv_buf);

	if (IS_ERR(ib_qp)) {
		err = PTR_ERR(ib_qp);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP ,("create_qp failed (%d)\n", err));
		goto err_create_qp;
	}

	// fill results
	ib_qp->device     	  		= pd->device;
	ib_qp->pd         	  			= pd;
	ib_qp->send_cq    	  		= qp_init_attr->send_cq;
	ib_qp->recv_cq    	  		= qp_init_attr->recv_cq;
	ib_qp->srq	       	  		= qp_init_attr->srq;
	ib_qp->ucontext 			= context;
	ib_qp->event_handler 	= qp_init_attr->event_handler;
	ib_qp->qp_context    	= qp_init_attr->qp_context;
	ib_qp->qp_type	  			= qp_init_attr->qp_type;
	atomic_inc(&pd->usecnt);
	atomic_inc(&qp_init_attr->send_cq->usecnt);
	atomic_inc(&qp_init_attr->recv_cq->usecnt);
	if (qp_init_attr->srq)
		atomic_inc(&qp_init_attr->srq->usecnt);
	if (context)
		atomic_inc(&context->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_QP ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));

	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_QP ,
		("uctx %p, qhndl %p, qnum %#x, q_num  %#x, scq %#x:%#x, rcq %#x:%#x \n",
		pd->ucontext, ib_qp, ((struct mthca_qp*)ib_qp)->qpn, ib_qp->qp_num,
		((struct mthca_cq*)ib_qp->send_cq)->cqn, ib_qp->send_cq->cqe,
		((struct mthca_cq*)ib_qp->recv_cq)->cqn, ib_qp->recv_cq->cqe ) );

	// fill results for user
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct mthca_qp *qp = (struct mthca_qp *)ib_qp;
		struct ibv_create_qp_resp *create_qp_resp = (struct ibv_create_qp_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		ib_qp->ib_mr = ib_mr;
		create_qp_resp->qpn = ib_qp->qp_num;
		create_qp_resp->user_handle = user_handle;
		create_qp_resp->mr.lkey = ib_mr->lkey;
		create_qp_resp->mr.rkey = ib_mr->rkey;
		create_qp_resp->mr.mr_handle = (u64)(ULONG_PTR)ib_mr;
		create_qp_resp->qp_handle = (__u64)(ULONG_PTR)qp;
		create_qp_resp->max_send_wr = qp->sq.max;
		create_qp_resp->max_recv_wr = qp->rq.max;
		create_qp_resp->max_send_sge = qp->sq.max_gs;
		create_qp_resp->max_recv_sge = qp->rq.max_gs;
		create_qp_resp->max_inline_data = qp->max_inline_data;
		p_umv_buf->output_size = sizeof(struct ibv_create_qp_resp);
	}

	return ib_qp;

err_create_qp:
	if (ib_mr)
		ibv_dereg_mr(ib_mr);
err_alloc_mr:
	if( p_umv_buf && p_umv_buf->command ) 
		p_umv_buf->status = IB_ERROR;
	HCA_EXIT(HCA_DBG_QP);
	return ERR_PTR(err);
}

int ibv_modify_qp(struct ib_qp *qp,
		 struct ib_qp_attr *qp_attr,
		 int qp_attr_mask)
{
	return qp->device->modify_qp(qp, qp_attr, qp_attr_mask);
}

int ibv_query_qp(struct ib_qp *qp,
		struct ib_qp_attr *qp_attr,
		int qp_attr_mask,
		struct ib_qp_init_attr *qp_init_attr)
{
	return qp->device->query_qp ?
		qp->device->query_qp(qp, qp_attr, qp_attr_mask, qp_init_attr) :
		-ENOSYS;
}
	
int ibv_destroy_qp(struct ib_qp *qp)
{
	struct ib_pd *pd;
	struct ib_cq *scq, *rcq;
	struct ib_srq *srq;
	int ret;
	struct ib_ucontext	*ucontext;
	struct ib_mr * ib_mr;

	pd  = qp->pd;
	scq = qp->send_cq;
	rcq = qp->recv_cq;
	srq = qp->srq;
	ucontext = pd->ucontext;
	ib_mr = qp->ib_mr;

	ret = qp->device->destroy_qp(qp);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		atomic_dec(&scq->usecnt);
		atomic_dec(&rcq->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_QP ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));
		if (srq)
			atomic_dec(&srq->usecnt);
		release_user_cq_qp_resources(ucontext, ib_mr);
	}

	return ret;
}

/* Completion queues */

struct ib_cq *ibv_create_cq(struct ib_device *device,
			   ib_comp_handler comp_handler,
			   void (*event_handler)(ib_event_rec_t *),
			   void *cq_context, int cqe, 
			   struct ib_ucontext *context, ci_umv_buf_t* const p_umv_buf)
{
	int err;
	struct ib_cq *cq;
	struct ib_mr *ib_mr = NULL;
	u64 user_handle = 0;

	// for user call we need also allocate MR
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct ibv_create_cq *create_cq = (struct ibv_create_cq *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		
		// create region
		ib_mr	= ibv_reg_mr( 
			(struct ib_pd *)(ULONG_PTR)create_cq->mr.pd_handle, 
			create_cq->mr.access_flags, 
			(void*)(ULONG_PTR)create_cq->mr.start,
			create_cq->mr.length, create_cq->mr.hca_va, TRUE, FALSE );
		if (IS_ERR(ib_mr)) {
			err = PTR_ERR(ib_mr);
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_CQ ,("ibv_reg_mr failed (%d)\n", err));
			goto err_alloc_mr;
		}
		user_handle = create_cq->user_handle;
		create_cq->lkey = ib_mr->lkey;
		cqe = create_cq->cqe;
	}
	
	// create cq
	cq = device->create_cq(device, cqe, context, p_umv_buf);
	if (IS_ERR(cq)) {
		err = PTR_ERR(cq);
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_CQ ,("create_cq failed (%d)\n", err));
		goto err_create_cq;
	}

	cq->device        = device;
	cq->ucontext = context;
	cq->comp_handler  = comp_handler;
	cq->event_handler = event_handler;
	cq->cq_context    = cq_context;
	atomic_set(&cq->usecnt, 0);
	if (context)
		atomic_inc(&context->usecnt);

	// fill results
	if (context && p_umv_buf && p_umv_buf->p_inout_buf) {
		struct ibv_create_cq_resp *create_cq_resp = (struct ibv_create_cq_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		cq->ib_mr = ib_mr;
		create_cq_resp->user_handle = user_handle;
		create_cq_resp->mr.lkey = ib_mr->lkey;
		create_cq_resp->mr.rkey = ib_mr->rkey;
		create_cq_resp->mr.mr_handle = (u64)(ULONG_PTR)ib_mr;
		create_cq_resp->cq_handle = (u64)(ULONG_PTR)cq;
		create_cq_resp->cqe = cq->cqe;
		p_umv_buf->output_size = sizeof(struct ibv_create_cq_resp);
	}
	
	return cq;

err_create_cq:
	if (ib_mr)
		ibv_dereg_mr(ib_mr);
err_alloc_mr:
	if( p_umv_buf && p_umv_buf->command ) 
		p_umv_buf->status = IB_ERROR;
	return ERR_PTR(err);
}

int ibv_destroy_cq(struct ib_cq *cq)
{
	int ret;
	struct ib_ucontext	*ucontext = cq->ucontext;
	struct ib_mr * ib_mr = cq->ib_mr;
	
	if (atomic_read(&cq->usecnt))
		return -EBUSY;

	ret = cq->device->destroy_cq(cq);

	release_user_cq_qp_resources(ucontext, ib_mr);
	
	return ret;
}

int ibv_resize_cq(struct ib_cq *cq,
                 int           cqe)
{
	int ret;

	if (!cq->device->resize_cq)
		return -ENOSYS;

	ret = cq->device->resize_cq(cq, &cqe);
	if (!ret)
		cq->cqe = cqe;

	return ret;
}

/* Memory regions */

struct ib_mr *ibv_reg_mr(struct ib_pd *pd, 
	mthca_qp_access_t mr_access_flags,
	void*			vaddr,
	uint64_t				length,
	uint64_t 				hca_va,
	boolean_t				um_call,
	boolean_t				secure
	)
{
	struct ib_mr *ib_mr;
	int                          err;
	HCA_ENTER(HCA_DBG_MEMORY);

	ib_mr = pd->device->reg_virt_mr(pd, vaddr, length, hca_va, mr_access_flags, um_call, secure);
	if (IS_ERR(ib_mr)) {
		err = PTR_ERR(ib_mr);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_MEMORY ,("mthca_reg_user_mr failed (%d)\n", err));
		goto err_reg_user_mr;
	}

	ib_mr->device  = pd->device;
	ib_mr->pd      = pd;
	atomic_inc(&pd->usecnt);
	atomic_set(&ib_mr->usecnt, 0);
	HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
		((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));
	HCA_EXIT(HCA_DBG_MEMORY);
	return ib_mr;

err_reg_user_mr:
	HCA_EXIT(HCA_DBG_MEMORY);
	return ERR_PTR(err);
}

struct ib_mr *ibv_get_dma_mr(struct ib_pd *pd, mthca_qp_access_t mr_access_flags)
{
	struct ib_mr *mr;

	// direct call is a must, because "lifefish" devices doesn't fill driver i/f table
	mr = mthca_get_dma_mr(pd, mr_access_flags);

	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		atomic_inc(&pd->usecnt);
		atomic_set(&mr->usecnt, 0);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt));
	}

	return mr;
}

struct ib_mr *ibv_reg_phys_mr(struct ib_pd *pd,
			     struct ib_phys_buf *phys_buf_array,
			     int num_phys_buf,
			     mthca_qp_access_t mr_access_flags,
			     u64 *iova_start)
{
	struct ib_mr *mr;

	mr = pd->device->reg_phys_mr(pd, phys_buf_array, num_phys_buf,
				     mr_access_flags, iova_start);

	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		atomic_inc(&pd->usecnt);
		atomic_set(&mr->usecnt, 0);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt));
	}

	return mr;
}

int ibv_rereg_phys_mr(struct ib_mr *mr,
		     int mr_rereg_mask,
		     struct ib_pd *pd,
		     struct ib_phys_buf *phys_buf_array,
		     int num_phys_buf,
		     mthca_qp_access_t mr_access_flags,
		     u64 *iova_start)
{
	struct ib_pd *old_pd;
	int ret;

	if (!mr->device->rereg_phys_mr)
		return -ENOSYS;

	if (atomic_read(&mr->usecnt))
		return -EBUSY;

	old_pd = mr->pd;

	ret = mr->device->rereg_phys_mr(mr, mr_rereg_mask, pd,
					phys_buf_array, num_phys_buf,
					mr_access_flags, iova_start);

	if (!ret && (mr_rereg_mask & IB_MR_REREG_PD)) {
		atomic_dec(&old_pd->usecnt);
		atomic_inc(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt));
	}

	return ret;
}

int ibv_query_mr(struct ib_mr *mr, struct ib_mr_attr *mr_attr)
{
	return mr->device->query_mr ?
		mr->device->query_mr(mr, mr_attr) : -ENOSYS;
}

int ibv_dereg_mr(struct ib_mr *mr)
{
	int ret;
	struct ib_pd *pd;

	if (atomic_read(&mr->usecnt))
		return -EBUSY;

	pd = mr->pd;
	// direct call is a must, because "lifefish" devices doesn't fill driver i/f table
	ret = mthca_dereg_mr(mr);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_MEMORY ,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt, pd, pd->ucontext));
	}

	return ret;
}

/* Memory windows */

struct ib_mw *ibv_alloc_mw(struct ib_pd *pd)
{
	struct ib_mw *mw;

	if (!pd->device->alloc_mw)
		return ERR_PTR(-ENOSYS);

	mw = pd->device->alloc_mw(pd);
	if (!IS_ERR(mw)) {
		mw->device  = pd->device;
		mw->pd      = pd;
		atomic_inc(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt));
	}

	return mw;
}

int ibv_dealloc_mw(struct ib_mw *mw)
{
	struct ib_pd *pd;
	int ret;

	pd = mw->pd;
	ret = mw->device->dealloc_mw(mw);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt));
	}

	return ret;
}

/* "Fast" memory regions */

struct ib_fmr *ibv_alloc_fmr(struct ib_pd *pd,
			    mthca_qp_access_t mr_access_flags,
			    struct ib_fmr_attr *fmr_attr)
{
	struct ib_fmr *fmr;

	if (!pd->device->alloc_fmr)
		return ERR_PTR(-ENOSYS);

	fmr = pd->device->alloc_fmr(pd, mr_access_flags, fmr_attr);
	if (!IS_ERR(fmr)) {
		fmr->device = pd->device;
		fmr->pd     = pd;
		atomic_inc(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt));
	}

	return fmr;
}

int ibv_map_phys_fmr(struct ib_fmr *fmr,
				  u64 *page_list, int list_len,
				  u64 iova)
{
	return fmr->device->map_phys_fmr(fmr, page_list, list_len, iova);
}

int ibv_unmap_fmr(struct list_head *fmr_list)
{
	struct ib_fmr *fmr;

	if (list_empty(fmr_list))
		return 0;

	fmr = list_entry(fmr_list->Flink, struct ib_fmr, list);
	return fmr->device->unmap_fmr(fmr_list);
}

int ibv_dealloc_fmr(struct ib_fmr *fmr)
{
	struct ib_pd *pd;
	int ret;

	pd = fmr->pd;
	ret = fmr->device->dealloc_fmr(fmr);
	if (!ret) {
		atomic_dec(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mthca_pd*)pd)->pd_num, pd->usecnt));
	}

	return ret;
}

/* Multicast groups */

int ibv_attach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid)
{
	if (!qp->device->attach_mcast)
		return -ENOSYS;
	if (gid->raw[0] != 0xff || qp->qp_type != IB_QPT_UNRELIABLE_DGRM)
		return -EINVAL;

	return qp->device->attach_mcast(qp, gid, lid);
}

int ibv_detach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid)
{
	if (!qp->device->detach_mcast)
		return -ENOSYS;
	if (gid->raw[0] != 0xff || qp->qp_type != IB_QPT_UNRELIABLE_DGRM)
		return -EINVAL;

	return qp->device->detach_mcast(qp, gid, lid);
}
