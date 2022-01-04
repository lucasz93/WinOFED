/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include <mt_l2w.h>

#include "mlnx_uvp.h"
#include "mx_abi.h"
#include "mthca_wqe.h"


#if defined(EVENT_TRACING)
#include "mlnx_uvp_verbs.tmh"
#endif

struct ibv_pd *mthca_alloc_pd(struct ibv_context *context, struct ibv_alloc_pd_resp *resp)
{
	struct mthca_pd           *pd;

	pd = cl_zalloc(sizeof *pd);
	if (!pd)
		goto err_malloc;

	if (!mthca_is_memfree(context)) {
		pd->ah_list = NULL;
		pd->ah_mutex = CreateMutex( NULL, FALSE, NULL );
		if (!pd->ah_mutex) 
			goto err_mutex;
	}

	/* fill response fields */
	pd->ibv_pd.context = context;	
	pd->ibv_pd.handle = resp->pd_handle;
	pd->pdn = resp->pdn;

	return &pd->ibv_pd;

err_mutex:
	cl_free(pd);
err_malloc:
	return NULL;
}

int mthca_free_pd(struct ibv_pd *ibv_pd)
{
	struct mthca_pd *pd = to_mpd(ibv_pd);
	if (!mthca_is_memfree(ibv_pd->context)) {
		struct mthca_ah_page *page, *next_page;
		WaitForSingleObject( pd->ah_mutex, INFINITE );
		for (page = pd->ah_list; page; page = next_page) {
			next_page = page->next;
			posix_memfree(page->buf);
			cl_free(page);
		}
		ReleaseMutex( pd->ah_mutex );
		CloseHandle(pd->ah_mutex);
	}
	cl_free(pd);
	return 0;
}

/* allocate create_cq infrastructure  and fill it's request parameters structure */
struct ibv_cq *mthca_create_cq_pre(struct ibv_context *context, int *p_cqe,
			       struct ibv_create_cq *req)
{
	struct mthca_cq      	   *cq;
	int                  	    nent;
	int                  	    ret;

	/* Sanity check CQ size before proceeding */
	if ((unsigned)*p_cqe > 131072)
		goto exit;

	cq = cl_zalloc(sizeof *cq);
	if (!cq)
		goto exit;

	cl_spinlock_construct(&cq->lock);
	if (cl_spinlock_init(&cq->lock))
		goto err;

	for (nent = 1; nent <= *p_cqe; nent <<= 1)
		; /* nothing */

	if (posix_memalign(&cq->buf, g_page_size,
			align(nent * MTHCA_CQ_ENTRY_SIZE, g_page_size)))
		goto err_memalign;

	mthca_init_cq_buf(cq, nent);

	if (mthca_is_memfree(context)) {
		cq->set_ci_db_index = mthca_alloc_db(to_mctx(context)->db_tab,
						     MTHCA_DB_TYPE_CQ_SET_CI,
						     &cq->set_ci_db);
		if (cq->set_ci_db_index < 0)
			goto err_unreg;

		cq->arm_db_index    = mthca_alloc_db(to_mctx(context)->db_tab,
						     MTHCA_DB_TYPE_CQ_ARM,
						     &cq->arm_db);
		if (cq->arm_db_index < 0)
			goto err_set_db;

		cq->u_arm_db_index    = mthca_alloc_db(to_mctx(context)->db_tab,
						     MTHCA_DB_TYPE_CQ_ARM,
						     &cq->p_u_arm_sn);
		if (cq->u_arm_db_index < 0)
			goto err_arm_db;

		*cq->p_u_arm_sn = 1;

		req->arm_db_page  = db_align(cq->arm_db);
		req->set_db_page  = db_align(cq->set_ci_db);
		req->u_arm_db_page  = (uint64_t)(ULONG_PTR)cq->p_u_arm_sn;
		req->arm_db_index = cq->arm_db_index;
		req->set_db_index = cq->set_ci_db_index;
		req->u_arm_db_index = cq->u_arm_db_index;
	}

	req->mr.start = (uint64_t)(ULONG_PTR)cq->buf;
	req->mr.length = nent * MTHCA_CQ_ENTRY_SIZE;
	req->mr.hca_va = 0;
	req->mr.pd_handle    = to_mctx(context)->pd->handle;
	req->mr.pdn = to_mpd(to_mctx(context)->pd)->pdn;
	req->mr.access_flags = MTHCA_ACCESS_LOCAL_WRITE;
	req->user_handle = (uint64_t)(ULONG_PTR)cq;
#if 1	
	req->cqe = *p_cqe;
	*p_cqe = nent-1;
//	*p_cqe = *p_cqe;	// return the same value
//	cq->ibv_cq.cqe = nent -1;
#else
	req->cqe = nent;
	*p_cqe = *p_cqe;	// return the same value
#endif
	return &cq->ibv_cq;

err_arm_db:
	if (mthca_is_memfree(context))
		mthca_free_db(to_mctx(context)->db_tab, MTHCA_DB_TYPE_CQ_SET_CI,
			cq->arm_db_index);

err_set_db:
	if (mthca_is_memfree(context))
		mthca_free_db(to_mctx(context)->db_tab, MTHCA_DB_TYPE_CQ_SET_CI,
			cq->set_ci_db_index);

err_unreg:
	posix_memfree(cq->buf);

err_memalign:
	cl_spinlock_destroy(&cq->lock);

err:
	cl_free(cq);
	
exit:
	return ERR_PTR(-ENOMEM);
}

struct ibv_cq *mthca_create_cq_post(struct ibv_context *context, 
			       struct ibv_create_cq_resp *resp)
{
	struct mthca_cq   *cq;
	int                  	    ret;

	cq = (struct mthca_cq *)(ULONG_PTR)resp->user_handle;

	cq->cqn = resp->cqn;
	cq->mr.handle = resp->mr.mr_handle;
	cq->mr.lkey = resp->mr.lkey;
	cq->mr.rkey = resp->mr.rkey;
	cq->mr.pd = to_mctx(context)->pd;
	cq->mr.context = context;
	cq->ibv_cq.cqe = resp->cqe;
	cq->ibv_cq.handle = resp->cq_handle;
	cq->ibv_cq.context = context;

	if (mthca_is_memfree(context)) {
		mthca_set_db_qn(cq->set_ci_db, MTHCA_DB_TYPE_CQ_SET_CI, cq->cqn);
		mthca_set_db_qn(cq->arm_db,    MTHCA_DB_TYPE_CQ_ARM,    cq->cqn);
	}

	return &cq->ibv_cq;

}

int mthca_destroy_cq(struct ibv_cq *cq)
{
	int ret;

	if (mthca_is_memfree(cq->context)) {
		mthca_free_db(to_mctx(cq->context)->db_tab, MTHCA_DB_TYPE_CQ_SET_CI,
			      to_mcq(cq)->u_arm_db_index);
		mthca_free_db(to_mctx(cq->context)->db_tab, MTHCA_DB_TYPE_CQ_SET_CI,
			      to_mcq(cq)->set_ci_db_index);
		mthca_free_db(to_mctx(cq->context)->db_tab, MTHCA_DB_TYPE_CQ_ARM,
			      to_mcq(cq)->arm_db_index);
	}

	posix_memfree(to_mcq(cq)->buf);
	
	cl_spinlock_destroy(&((struct mthca_cq *)cq)->lock);
	cl_free(to_mcq(cq));

	return 0;
}

int align_queue_size(struct ibv_context *context, int size, int spare)
{
	int ret;

	/* Enable creating zero-sized QPs */
	if (!size)
		return 1;

	if (mthca_is_memfree(context)) {
		for (ret = 1; ret < size + spare; ret <<= 1)
			; /* nothing */

		return ret;
	} else
		return size + spare;
}

struct ibv_qp *mthca_create_qp_pre(struct ibv_pd *pd, 
	struct ibv_qp_init_attr *attr, struct ibv_create_qp *req)
{
	struct mthca_qp       *qp;
	struct ibv_context *context = pd->context;
	int                    ret = -ENOMEM;

	UVP_ENTER(UVP_DBG_QP);
	/* Sanity check QP size before proceeding */
	if (attr->cap.max_send_wr     > 65536 ||
	    attr->cap.max_recv_wr     > 65536 ||
	    attr->cap.max_send_sge    > 64    ||
	    attr->cap.max_recv_sge    > 64    ||
	    attr->cap.max_inline_data > 1024) {
		ret = -EINVAL;
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("sanity checks  failed (%d)\n",ret));
		goto exit;
		}

	qp = cl_zalloc(sizeof *qp);
	if (!qp) {
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("cl_malloc  failed (%d)\n",ret));
		goto err_nomem;
	}	

	qp->sq.max = align_queue_size(context, attr->cap.max_send_wr, 0);
	qp->rq.max = align_queue_size(context, attr->cap.max_recv_wr, 0);

	if (mthca_alloc_qp_buf(pd, &attr->cap, attr->qp_type, qp)) {
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("mthca_alloc_qp_buf  failed (%d)\n",ret));
		goto err_nomem;
	} 

	mthca_init_qp_indices(qp);

	cl_spinlock_construct(&qp->sq.lock);
	if (cl_spinlock_init(&qp->sq.lock)) {
		ret = -EFAULT;
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("cl_spinlock_init failed for sq (%d)\n",ret));
		goto err_spinlock_sq;
	}

	cl_spinlock_construct(&qp->rq.lock);
	if (cl_spinlock_init(&qp->rq.lock)) {
		ret = -EFAULT;
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("cl_spinlock_init failed for rq (%d)\n",ret));
		goto err_spinlock_rq;
	}

	if (mthca_is_memfree(context)) {
		qp->sq.db_index = mthca_alloc_db(to_mctx(context)->db_tab,
						 MTHCA_DB_TYPE_SQ,
						 &qp->sq.db);
		if (qp->sq.db_index < 0)
			goto err_sq_db;

		qp->rq.db_index = mthca_alloc_db(to_mctx(context)->db_tab,
						 MTHCA_DB_TYPE_RQ,
						 &qp->rq.db);
		if (qp->rq.db_index < 0)
			goto err_rq_db;

		req->sq_db_page  = db_align(qp->sq.db);
		req->rq_db_page  = db_align(qp->rq.db);
		req->sq_db_index = qp->sq.db_index;
		req->rq_db_index = qp->rq.db_index;
	}

	// fill the rest qp fields
	qp->ibv_qp.pd = pd;
	qp->ibv_qp.context= pd->context;
	qp->ibv_qp.send_cq = attr->send_cq;
	qp->ibv_qp.recv_cq = attr->recv_cq;
	qp->ibv_qp.srq = attr->srq;
	qp->ibv_qp.state = IBV_QPS_RESET;
	qp->ibv_qp.qp_type = attr->qp_type;

	// fill the rest request fields
	req->mr.start = (uint64_t)(ULONG_PTR)qp->buf;
	req->mr.length = qp->buf_size;
	req->mr.hca_va = 0;
	req->mr.pd_handle    = pd->handle;
	req->mr.pdn = to_mpd(pd)->pdn;
	req->mr.access_flags = 0;	//local read
	req->user_handle = (uint64_t)(ULONG_PTR)qp;
	req->send_cq_handle = attr->send_cq->handle;
	req->recv_cq_handle = attr->recv_cq->handle;
	req->srq_handle = (attr->srq) ? attr->srq->handle : 0;
	req->max_send_wr = attr->cap.max_send_wr;
	req->max_recv_wr = attr->cap.max_recv_wr;
	req->max_send_sge = attr->cap.max_send_sge;
	req->max_recv_sge = attr->cap.max_recv_sge;
	req->max_inline_data = attr->cap.max_inline_data;
	req->sq_sig_all = (uint8_t)attr->sq_sig_all;
	req->qp_type = attr->qp_type;
	req->is_srq = !!attr->srq;


	UVP_EXIT(UVP_DBG_QP);
	return &qp->ibv_qp;

err_rq_db:
	if (mthca_is_memfree(context))
		mthca_free_db(to_mctx(context)->db_tab, 
			MTHCA_DB_TYPE_SQ, qp->sq.db_index);

err_sq_db:
	cl_spinlock_destroy(&qp->rq.lock);

err_spinlock_rq:
	cl_spinlock_destroy(&qp->sq.lock);
	
err_spinlock_sq:
	cl_free(qp->wrid);
	posix_memfree(qp->buf);

err_nomem:
	cl_free(qp);

exit:
	
	UVP_EXIT(UVP_DBG_QP);
	return ERR_PTR(ret);
}

struct ibv_qp *mthca_create_qp_post(struct ibv_pd *pd, 
	struct ibv_create_qp_resp *resp)
{
	struct mthca_qp       *qp;
	int                    ret;
	UVP_ENTER(UVP_DBG_QP);
	qp = (struct mthca_qp *)(ULONG_PTR)resp->user_handle;

	qp->ibv_qp.handle			= resp->qp_handle;
	qp->ibv_qp.qp_num		= resp->qpn;
	qp->sq.max 	    			= resp->max_send_wr;
	qp->rq.max 	    			= resp->max_recv_wr;
	qp->sq.max_gs 	    		= resp->max_send_sge;
	qp->rq.max_gs 	    		= resp->max_recv_sge;
	qp->max_inline_data 	= resp->max_inline_data;
	qp->mr.handle = resp->mr.mr_handle;
	qp->mr.lkey = resp->mr.lkey;
	qp->mr.rkey = resp->mr.rkey;
	qp->mr.pd = pd;
	qp->mr.context = pd->context;

	if (mthca_is_memfree(pd->context)) {
		mthca_set_db_qn(qp->sq.db, MTHCA_DB_TYPE_SQ, qp->ibv_qp.qp_num);
		mthca_set_db_qn(qp->rq.db, MTHCA_DB_TYPE_RQ, qp->ibv_qp.qp_num);
	}

	ret = mthca_store_qp(to_mctx(pd->context), qp->ibv_qp.qp_num, qp);
	if (ret)
		goto err_store_qp;

	UVP_EXIT(UVP_DBG_QP);
	return &qp->ibv_qp;

err_store_qp:
	UVP_EXIT(UVP_DBG_QP);
	return ERR_PTR(ret);
}


int mthca_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		    enum ibv_qp_attr_mask attr_mask)
{
	int ret = 0;

	if (attr_mask & IBV_QP_STATE)
		qp->state = attr->qp_state;

	if ((attr_mask & IBV_QP_STATE) &&
	    (attr->qp_state == IBV_QPS_RESET)) {
		mthca_cq_clean(to_mcq(qp->recv_cq), qp->qp_num,
			       qp->srq ? to_msrq(qp->srq) : NULL);
		if (qp->send_cq != qp->recv_cq)
			mthca_cq_clean(to_mcq(qp->send_cq), qp->qp_num, NULL);

		mthca_init_qp_indices(to_mqp(qp));

		if (mthca_is_memfree(qp->pd->context)) {
			*to_mqp(qp)->sq.db = 0;
			*to_mqp(qp)->rq.db = 0;
		}
	}

	return ret;
}


void mthca_destroy_qp_pre(struct ibv_qp *qp)
{
	int ret;

	mthca_cq_clean(to_mcq(qp->recv_cq), qp->qp_num,
		       qp->srq ? to_msrq(qp->srq) : NULL);
	if (qp->send_cq != qp->recv_cq)
		mthca_cq_clean(to_mcq(qp->send_cq), qp->qp_num, NULL);

	cl_spinlock_acquire(&to_mcq(qp->send_cq)->lock);
	if (qp->send_cq != qp->recv_cq)
		cl_spinlock_acquire(&to_mcq(qp->recv_cq)->lock);
	mthca_clear_qp(to_mctx(qp->pd->context), qp->qp_num);
	if (qp->send_cq != qp->recv_cq)
		cl_spinlock_release(&to_mcq(qp->recv_cq)->lock);
	cl_spinlock_release(&to_mcq(qp->send_cq)->lock);
}

void mthca_destroy_qp_post(struct ibv_qp *qp, int ret)
{
	if (ret) {
		cl_spinlock_acquire(&to_mcq(qp->send_cq)->lock);
		if (qp->send_cq != qp->recv_cq)
			cl_spinlock_acquire(&to_mcq(qp->recv_cq)->lock);
		mthca_store_qp(to_mctx(qp->pd->context), qp->qp_num, to_mqp(qp));
		if (qp->send_cq != qp->recv_cq)
			cl_spinlock_release(&to_mcq(qp->recv_cq)->lock);
		cl_spinlock_release(&to_mcq(qp->send_cq)->lock);
	}
	else {
		if (mthca_is_memfree(qp->pd->context)) {
			mthca_free_db(to_mctx(qp->pd->context)->db_tab, MTHCA_DB_TYPE_RQ,
				      to_mqp(qp)->rq.db_index);
			mthca_free_db(to_mctx(qp->pd->context)->db_tab, MTHCA_DB_TYPE_SQ,
				      to_mqp(qp)->sq.db_index);
		}

		cl_spinlock_destroy(&((struct mthca_qp *)qp)->sq.lock);
		cl_spinlock_destroy(&((struct mthca_qp *)qp)->rq.lock);

		posix_memfree(to_mqp(qp)->buf);
		cl_free(to_mqp(qp)->wrid);
		cl_free(to_mqp(qp));
	}

}

int mthca_attach_mcast(struct ibv_qp *qp, union ibv_gid *gid, uint16_t lid)
{
#ifdef WIN_TO_BE_CHANGED
	return ibv_cmd_attach_mcast(qp, gid, lid);
#else
	return -ENOSYS;
#endif
}

int mthca_detach_mcast(struct ibv_qp *qp, union ibv_gid *gid, uint16_t lid)
{
#ifdef WIN_TO_BE_CHANGED
	return ibv_cmd_detach_mcast(qp, gid, lid);
#else
	return -ENOSYS;
#endif
}

