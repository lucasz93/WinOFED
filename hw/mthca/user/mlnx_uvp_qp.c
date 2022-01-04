/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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
#include "mlnx_uvp_doorbell.h"
#include "mthca_wqe.h"
#include "mlnx_ual_data.h"

#if defined(EVENT_TRACING)
#include "mlnx_uvp_qp.tmh"
#endif

static const uint8_t mthca_opcode[] = {
	MTHCA_OPCODE_RDMA_WRITE,
	MTHCA_OPCODE_RDMA_WRITE_IMM,
	MTHCA_OPCODE_SEND,
	MTHCA_OPCODE_SEND_IMM,
	MTHCA_OPCODE_RDMA_READ,
	MTHCA_OPCODE_ATOMIC_CS,
	MTHCA_OPCODE_ATOMIC_FA
};

static enum mthca_wr_opcode conv_ibal_wr_opcode(struct _ib_send_wr *wr)
{
	enum mthca_wr_opcode opcode = -1; //= wr->wr_type;

	switch (wr->wr_type) {
		case WR_SEND: 
			opcode = (wr->send_opt & IB_SEND_OPT_IMMEDIATE) ? MTHCA_OPCODE_SEND_IMM : MTHCA_OPCODE_SEND;
			break;
		case WR_RDMA_WRITE:	
			opcode = (wr->send_opt & IB_SEND_OPT_IMMEDIATE) ? MTHCA_OPCODE_RDMA_WRITE_IMM : MTHCA_OPCODE_RDMA_WRITE;
			break;
		case WR_RDMA_READ: 		opcode = MTHCA_OPCODE_RDMA_READ; break;
		case WR_COMPARE_SWAP: opcode = MTHCA_OPCODE_ATOMIC_CS; break;
		case WR_FETCH_ADD: 			opcode = MTHCA_OPCODE_ATOMIC_FA; break;
		default:						opcode = MTHCA_OPCODE_INVALID;break;
	}
	return opcode;
}


static void dump_wqe(uint32_t print_lvl, uint32_t *wqe_ptr , struct mthca_qp *qp_ptr, int size)
{
	net32_t *wqe = wqe_ptr;
	int i;

	(void) wqe;	/* avoid warning if mthca_dbg compiled away... */
	UVP_PRINT(print_lvl,UVP_DBG_QP,("WQE: QPN 0x%06x, buf %p , buf_sz %d, send_offset %d\n",
		qp_ptr->ibv_qp.qp_num, qp_ptr->buf, qp_ptr->buf_size, qp_ptr->send_wqe_offset ));
	for (i=0; i<size; ++i) {
		UVP_PRINT(print_lvl,UVP_DBG_QP,("  segment[%d] %08x %08x %08x %08x \n",i,
			cl_ntoh32(wqe[4*i+0]), cl_ntoh32(wqe[4*i+1]), 
			cl_ntoh32(wqe[4*i+2]), cl_ntoh32(wqe[4*i+3])));
	}

}
static void *get_recv_wqe(struct mthca_qp *qp, int n)
{
	return qp->buf + (n << qp->rq.wqe_shift);
}

static void *get_send_wqe(struct mthca_qp *qp, int n)
{
	void *wqe_addr = qp->buf + qp->send_wqe_offset + (n << qp->sq.wqe_shift);
	UVP_PRINT(TRACE_LEVEL_INFORMATION,UVP_DBG_QP,
		("wqe %p, qp_buf %p, offset %#x,  index %d, shift %d \n",
		 wqe_addr, qp->buf, qp->send_wqe_offset, n, 
		qp->sq.wqe_shift));
	
	return wqe_addr;
}

void mthca_init_qp_indices(struct mthca_qp *qp)
{
	qp->sq.next_ind  = 0;
	qp->sq.last_comp = qp->sq.max - 1;
	qp->sq.head    	 = 0;
	qp->sq.tail    	 = 0;
	qp->sq.last      = get_send_wqe(qp, qp->sq.max - 1);

	qp->rq.next_ind	 = 0;
	qp->rq.last_comp = qp->rq.max - 1;
	qp->rq.head    	 = 0;
	qp->rq.tail    	 = 0;
	qp->rq.last      = get_recv_wqe(qp, qp->rq.max - 1);
}

static inline int mthca_wq_overflow(struct mthca_wq *wq, int nreq, struct mthca_cq *cq)
{
	unsigned cur;

	cur = wq->head - wq->tail;
	if ((int)(cur + nreq) < wq->max)
		return 0;

	cl_spinlock_acquire(&cq->lock);
	cur = wq->head - wq->tail;
	cl_spinlock_release(&cq->lock);

	return (int)(cur + nreq) >= wq->max;
}


int mthca_tavor_post_send(struct ibv_qp *ibqp, struct _ib_send_wr *wr,
			  struct _ib_send_wr **bad_wr)
{
	struct mthca_qp *qp = to_mqp(ibqp);
	uint8_t *wqe;
	uint8_t *prev_wqe;
	int ret = 0;
	int nreq;
	int i;
	int size;
	int size0 = 0;
	uint32_t f0 = unlikely(wr->send_opt & IB_SEND_OPT_FENCE) ? MTHCA_SEND_DOORBELL_FENCE : 0;
	int ind;
	int op0 = 0;
	enum ib_wr_opcode opcode;
	
	UVP_ENTER(UVP_DBG_QP);
	cl_spinlock_acquire(&qp->sq.lock);

	/* XXX check that state is OK to post send */

	ind = qp->sq.next_ind;

	if(ibqp->state == IBV_QPS_RESET) {
		ret = -EBUSY;
		if (bad_wr)
			*bad_wr = wr;
		goto err_busy;
	}

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {

		if (mthca_wq_overflow(&qp->sq, nreq, to_mcq(qp->ibv_qp.send_cq))) {
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("SQ %06x full (%u head, %u tail,"
					" %d max, %d nreq)\n", ibqp->qp_num,
					qp->sq.head, qp->sq.tail,
					qp->sq.max, nreq));
			ret = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_send_wqe(qp, ind);
		prev_wqe = qp->sq.last;
		qp->sq.last = wqe;
		opcode = conv_ibal_wr_opcode(wr);
		if (opcode == MTHCA_OPCODE_INVALID) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_QP ,("SQ %06x opcode invalid\n",ibqp->qp_num));
			ret = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}


		((struct mthca_next_seg *) wqe)->nda_op = 0;
		((struct mthca_next_seg *) wqe)->ee_nds = 0;
		((struct mthca_next_seg *) wqe)->flags =
			((wr->send_opt & IB_SEND_OPT_SIGNALED) ?
			 cl_hton32(MTHCA_NEXT_CQ_UPDATE) : 0) |
			((wr->send_opt & IB_SEND_OPT_SOLICITED) ?
			 cl_hton32(MTHCA_NEXT_SOLICIT) : 0)   |
			cl_hton32(1);
		if (opcode == MTHCA_OPCODE_SEND_IMM||
		    opcode == MTHCA_OPCODE_RDMA_WRITE_IMM)
			((struct mthca_next_seg *) wqe)->imm = wr->immediate_data;

		wqe += sizeof (struct mthca_next_seg);
		size = sizeof (struct mthca_next_seg) / 16;


		switch (ibqp->qp_type) {
		case IB_QPT_RELIABLE_CONN:
			switch (opcode) {
			case MTHCA_OPCODE_ATOMIC_CS:
			case MTHCA_OPCODE_ATOMIC_FA:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;

				wqe += sizeof (struct mthca_raddr_seg);

				if (opcode == MTHCA_OPCODE_ATOMIC_CS) {
					((struct mthca_atomic_seg *) wqe)->swap_add =
						(wr->remote_ops.atomic2);
					((struct mthca_atomic_seg *) wqe)->compare =
						(wr->remote_ops.atomic1);
				} else {
					((struct mthca_atomic_seg *) wqe)->swap_add =
						(wr->remote_ops.atomic1);
					((struct mthca_atomic_seg *) wqe)->compare = 0;
				}

				wqe += sizeof (struct mthca_atomic_seg);
				size += (sizeof (struct mthca_raddr_seg) +
					 sizeof (struct mthca_atomic_seg)) / 16;
				break;

			case MTHCA_OPCODE_RDMA_WRITE:
			case MTHCA_OPCODE_RDMA_WRITE_IMM:
			case MTHCA_OPCODE_RDMA_READ:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;
				wqe += sizeof (struct mthca_raddr_seg);
				size += sizeof (struct mthca_raddr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}

			break;

		case IB_QPT_UNRELIABLE_CONN:
			switch (opcode) {
			case MTHCA_OPCODE_RDMA_WRITE:
			case MTHCA_OPCODE_RDMA_WRITE_IMM:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;
				wqe += sizeof (struct mthca_raddr_seg);
				size += sizeof (struct mthca_raddr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}

			break;

		case IB_QPT_UNRELIABLE_DGRM:
			{
				struct mthca_ah *ah = ((struct mthca_ah *)wr->dgrm.ud.h_av);
				((struct mthca_tavor_ud_seg *) wqe)->lkey =
					cl_hton32(ah->key);
				((struct mthca_tavor_ud_seg *) wqe)->av_addr =
					cl_hton64((ULONG_PTR)ah->av);
				((struct mthca_tavor_ud_seg *) wqe)->dqpn = wr->dgrm.ud.remote_qp;
				((struct mthca_tavor_ud_seg *) wqe)->qkey = wr->dgrm.ud.remote_qkey;

				wqe += sizeof (struct mthca_tavor_ud_seg);
				size += sizeof (struct mthca_tavor_ud_seg) / 16;
				break;
			}

		default:
			break;
		}

		if ((int)(int)wr->num_ds > qp->sq.max_gs) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_QP ,("SQ %06x too many gathers\n",ibqp->qp_num));
			ret = -ERANGE;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}
//TODO sleybo:
		if (wr->send_opt & IB_SEND_OPT_INLINE) {
			if (wr->num_ds) {
				struct mthca_inline_seg *seg = (struct mthca_inline_seg *)wqe;
				uint32_t s = 0;

				wqe += sizeof *seg;
				for (i = 0; i < (int)wr->num_ds; ++i) {
					struct _ib_local_ds *sge = &wr->ds_array[i];

					s += sge->length;

					if (s > (uint32_t)qp->max_inline_data) {
						ret = -E2BIG;
						if (bad_wr)
							*bad_wr = wr;
						goto out;
					}

					memcpy(wqe, (void *) (ULONG_PTR) sge->vaddr,
					       sge->length);
					wqe += sge->length;
				}

				seg->byte_count = cl_hton32(MTHCA_INLINE_SEG | s);
				size += align(s + sizeof *seg, 16) / 16;
			}
		} else {
			for (i = 0; i < (int)wr->num_ds; ++i) {
				((struct mthca_data_seg *) wqe)->byte_count =
					cl_hton32(wr->ds_array[i].length);
				((struct mthca_data_seg *) wqe)->lkey =
					cl_hton32(wr->ds_array[i].lkey);
				((struct mthca_data_seg *) wqe)->addr =
					cl_hton64(wr->ds_array[i].vaddr);
				wqe += sizeof (struct mthca_data_seg);
				size += sizeof (struct mthca_data_seg) / 16;
			}
		}

		qp->wrid[ind + qp->rq.max] = wr->wr_id;

		((struct mthca_next_seg *) prev_wqe)->nda_op =
			cl_hton32(((ind << qp->sq.wqe_shift) +
			qp->send_wqe_offset) |opcode);
		
		wmb();
		
		((struct mthca_next_seg *) prev_wqe)->ee_nds =
			cl_hton32((size0 ? 0 : MTHCA_NEXT_DBD) | size |
			((wr->send_opt& IB_SEND_OPT_FENCE) ?
			 MTHCA_NEXT_FENCE : 0));

		if (!size0) {
			size0 = size;
			op0   = opcode;
		}
		
		dump_wqe( TRACE_LEVEL_VERBOSE, (uint32_t*)qp->sq.last,qp, size);
		
		++ind;
		if (unlikely(ind >= qp->sq.max))
			ind -= qp->sq.max;

	}

out:
	if (likely(nreq)) {
		uint32_t doorbell[2];

		doorbell[0] = cl_hton32(((qp->sq.next_ind << qp->sq.wqe_shift) +
			qp->send_wqe_offset) | f0 | op0);
		doorbell[1] = cl_hton32((ibqp->qp_num << 8) | size0);

		wmb();

		mthca_write64(doorbell, to_mctx(ibqp->pd->context), MTHCA_SEND_DOORBELL);
	}

	qp->sq.next_ind = ind;
	qp->sq.head    += nreq;

err_busy:
	cl_spinlock_release(&qp->sq.lock);
	
	UVP_EXIT(UVP_DBG_QP);
	return ret;
}


int mthca_tavor_post_recv(struct ibv_qp *ibqp, struct _ib_recv_wr *wr,
			  struct _ib_recv_wr **bad_wr)
{
	struct mthca_qp *qp = to_mqp(ibqp);
	uint32_t doorbell[2];
	int ret = 0;
	int nreq;
	int i;
	int size;
	int size0 = 0;
	int ind;
	uint8_t *wqe;
	uint8_t *prev_wqe;
	
	UVP_ENTER(UVP_DBG_QP);
	
	cl_spinlock_acquire(&qp->rq.lock);

	/* XXX check that state is OK to post receive */
	
	ind = qp->rq.next_ind;
	if(ibqp->state == IBV_QPS_RESET) {
		ret = -EBUSY;
		if (bad_wr)
			*bad_wr = wr;
		goto err_busy;
	}
	
	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (unlikely(nreq == MTHCA_TAVOR_MAX_WQES_PER_RECV_DB)) {
			nreq = 0;

			doorbell[0] = cl_hton32((qp->rq.next_ind << qp->rq.wqe_shift) | size0);
			doorbell[1] = cl_hton32(ibqp->qp_num << 8); //TODO sleybo: add qpn to qp struct 

			/*
			 * Make sure that descriptors are written
			 * before doorbell is rung.
			 */
			mb();

			mthca_write64(doorbell, to_mctx(ibqp->pd->context), MTHCA_RECV_DOORBELL);

			qp->rq.head += MTHCA_TAVOR_MAX_WQES_PER_RECV_DB;
			size0 = 0;
		}

		if (mthca_wq_overflow(&qp->rq, nreq, to_mcq(qp->ibv_qp.recv_cq))) {
			UVP_PRINT(TRACE_LEVEL_ERROR,UVP_DBG_QP,("RQ %06x full (%u head, %u tail,"
					" %d max, %d nreq)\n", ibqp->qp_num,
					qp->rq.head, qp->rq.tail,
					qp->rq.max, nreq));
			ret = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_recv_wqe(qp, ind);
		prev_wqe = qp->rq.last;
		qp->rq.last = wqe;

		((struct mthca_next_seg *) wqe)->nda_op = 0;
		((struct mthca_next_seg *) wqe)->ee_nds =
			cl_hton32(MTHCA_NEXT_DBD);
		((struct mthca_next_seg *) wqe)->flags =
			cl_hton32(MTHCA_NEXT_CQ_UPDATE);

		wqe += sizeof (struct mthca_next_seg);
		size = sizeof (struct mthca_next_seg) / 16;

		if (unlikely((int)wr->num_ds  > qp->rq.max_gs)) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_QP ,("RQ %06x too many gathers\n",ibqp->qp_num));
			ret = -ERANGE;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		for (i = 0; i < (int)wr->num_ds; ++i) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cl_hton32(wr->ds_array[i].length);
			((struct mthca_data_seg *) wqe)->lkey =
				cl_hton32(wr->ds_array[i].lkey);
			((struct mthca_data_seg *) wqe)->addr =
				cl_hton64(wr->ds_array[i].vaddr);
			wqe += sizeof (struct mthca_data_seg);
			size += sizeof (struct mthca_data_seg) / 16;
		}

		qp->wrid[ind] = wr->wr_id;

		((struct mthca_next_seg *) prev_wqe)->nda_op =
			cl_hton32((ind << qp->rq.wqe_shift) | 1);
		wmb();
		((struct mthca_next_seg *) prev_wqe)->ee_nds =
			cl_hton32(MTHCA_NEXT_DBD | size);

		if (!size0)
			size0 = size;

		++ind;
		if (unlikely(ind >= qp->rq.max))
			ind -= qp->rq.max;
	}

out:
	if (likely(nreq)) {
		doorbell[0] = cl_hton32((qp->rq.next_ind << qp->rq.wqe_shift) | size0);
		doorbell[1] = cl_hton32((ibqp->qp_num << 8) | (nreq & 255));

		/*
		 * Make sure that descriptors are written before
		 * doorbell is rung.
		 */
		mb();

		mthca_write64(doorbell, to_mctx(ibqp->pd->context), MTHCA_RECV_DOORBELL);
	}

	qp->rq.next_ind = ind;
	qp->rq.head    += nreq;

err_busy:
	cl_spinlock_release(&qp->rq.lock);
	UVP_EXIT(UVP_DBG_QP);
	return ret;
}

int mthca_arbel_post_send(struct ibv_qp *ibqp, struct _ib_send_wr *wr,
			  struct _ib_send_wr **bad_wr)
{
	struct mthca_qp *qp = to_mqp(ibqp);
	uint32_t doorbell[2];
	uint8_t *wqe;
	uint8_t *prev_wqe;
	int ret = 0;
	int nreq;	
	int i;
	int size;
	int size0 = 0;
	uint32_t f0 = unlikely(wr->send_opt & IB_SEND_OPT_FENCE) ? MTHCA_SEND_DOORBELL_FENCE : 0;
	int ind;
	uint8_t op0 = 0;
	enum ib_wr_opcode opcode;
	
	UVP_ENTER(UVP_DBG_QP);
	
	cl_spinlock_acquire(&qp->sq.lock);

	/* XXX check that state is OK to post send */

	ind = qp->sq.head & (qp->sq.max - 1);
	if(ibqp->state == IBV_QPS_RESET) {
		ret = -EBUSY;
		if (bad_wr)
			*bad_wr = wr;
		goto err_busy;
	}

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (unlikely(nreq == MTHCA_ARBEL_MAX_WQES_PER_SEND_DB)) {
			nreq = 0;

			doorbell[0] = cl_hton32((MTHCA_ARBEL_MAX_WQES_PER_SEND_DB << 24) |
					    ((qp->sq.head & 0xffff) << 8) | f0 | op0);
			doorbell[1] = cl_hton32((ibqp->qp_num << 8) | size0);
			qp->sq.head += MTHCA_ARBEL_MAX_WQES_PER_SEND_DB;
			size0 = 0;
			f0 = unlikely(wr->send_opt & IB_SEND_OPT_FENCE) ? MTHCA_SEND_DOORBELL_FENCE : 0;

			/*
			 * Make sure that descriptors are written before
			 * doorbell record.
			 */
			wmb();
			*qp->sq.db = cl_hton32(qp->sq.head & 0xffff);

			/*
			 * Make sure doorbell record is written before we
			 * write MMIO send doorbell.
			 */
			wmb();
			mthca_write64(doorbell, to_mctx(ibqp->pd->context), MTHCA_SEND_DOORBELL);

		}

		if (mthca_wq_overflow(&qp->sq, nreq, to_mcq(qp->ibv_qp.send_cq))) {
			UVP_PRINT(TRACE_LEVEL_ERROR,UVP_DBG_QP,("SQ %06x full (%u head, %u tail,"
					" %d max, %d nreq)\n", ibqp->qp_num,
					qp->sq.head, qp->sq.tail,
					qp->sq.max, nreq));			
			ret = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_send_wqe(qp, ind);
		prev_wqe = qp->sq.last;
		qp->sq.last = wqe;
		opcode = conv_ibal_wr_opcode(wr);

		((struct mthca_next_seg *) wqe)->flags =
			((wr->send_opt & IB_SEND_OPT_SIGNALED) ?
			 cl_hton32(MTHCA_NEXT_CQ_UPDATE) : 0) |
			((wr->send_opt & IB_SEND_OPT_SOLICITED) ?
			 cl_hton32(MTHCA_NEXT_SOLICIT) : 0)   |
			cl_hton32(1);
		if (opcode == MTHCA_OPCODE_SEND_IMM||
			opcode == MTHCA_OPCODE_RDMA_WRITE_IMM)
			((struct mthca_next_seg *) wqe)->imm = wr->immediate_data;

		wqe += sizeof (struct mthca_next_seg);
		size = sizeof (struct mthca_next_seg) / 16;

		switch (ibqp->qp_type) {
		case IB_QPT_RELIABLE_CONN:
			switch (opcode) {
			case MTHCA_OPCODE_ATOMIC_CS:
			case MTHCA_OPCODE_ATOMIC_FA:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;

				wqe += sizeof (struct mthca_raddr_seg);

				if (opcode == MTHCA_OPCODE_ATOMIC_CS) {
					((struct mthca_atomic_seg *) wqe)->swap_add =
						(wr->remote_ops.atomic2);
					((struct mthca_atomic_seg *) wqe)->compare =
						(wr->remote_ops.atomic1);
				} else {
					((struct mthca_atomic_seg *) wqe)->swap_add =
						(wr->remote_ops.atomic1);
					((struct mthca_atomic_seg *) wqe)->compare = 0;
				}

				wqe += sizeof (struct mthca_atomic_seg);
				size += (sizeof (struct mthca_raddr_seg) +
					 sizeof (struct mthca_atomic_seg)) / 16;
				break;

			case MTHCA_OPCODE_RDMA_READ:
			case MTHCA_OPCODE_RDMA_WRITE:
			case MTHCA_OPCODE_RDMA_WRITE_IMM:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;
				wqe += sizeof (struct mthca_raddr_seg);
				size += sizeof (struct mthca_raddr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}

			break;

		case IB_QPT_UNRELIABLE_CONN:
			switch (opcode) {
			case MTHCA_OPCODE_RDMA_WRITE:
			case MTHCA_OPCODE_RDMA_WRITE_IMM:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;
				wqe += sizeof (struct mthca_raddr_seg);
				size += sizeof (struct mthca_raddr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}

			break;

		case IB_QPT_UNRELIABLE_DGRM:
			{
				struct mthca_ah *ah = ((struct mthca_ah *)wr->dgrm.ud.h_av);
				memcpy(((struct mthca_arbel_ud_seg *) wqe)->av,
				       ah->av, sizeof ( struct mthca_av));
				((struct mthca_arbel_ud_seg *) wqe)->dqpn = wr->dgrm.ud.remote_qp;
				((struct mthca_arbel_ud_seg *) wqe)->qkey = wr->dgrm.ud.remote_qkey;


				wqe += sizeof (struct mthca_arbel_ud_seg);
				size += sizeof (struct mthca_arbel_ud_seg) / 16;
				break;
			}

		default:
			break;
		}

		if ((int)wr->num_ds > qp->sq.max_gs) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_QP ,("SQ %06x full too many gathers\n",ibqp->qp_num));
			ret = -ERANGE;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		if (wr->send_opt & IB_SEND_OPT_INLINE) {
			if (wr->num_ds) {
				struct mthca_inline_seg *seg = (struct mthca_inline_seg *)wqe;
				uint32_t s = 0;

				wqe += sizeof *seg;
				for (i = 0; i < (int)wr->num_ds; ++i) {
					struct _ib_local_ds *sge = &wr->ds_array[i];

					s += sge->length;

					if (s > (uint32_t)qp->max_inline_data) {
						ret = -E2BIG;
						if (bad_wr)
							*bad_wr = wr;
						goto out;
					}

					memcpy(wqe, (void *) (uintptr_t) sge->vaddr,
					       sge->length);
					wqe += sge->length;
				}

				seg->byte_count = cl_hton32(MTHCA_INLINE_SEG | s);
				size += align(s + sizeof *seg, 16) / 16;
			}
		} else {

			for (i = 0; i < (int)wr->num_ds; ++i) {
				((struct mthca_data_seg *) wqe)->byte_count =
					cl_hton32(wr->ds_array[i].length);
				((struct mthca_data_seg *) wqe)->lkey =
					cl_hton32(wr->ds_array[i].lkey);
				((struct mthca_data_seg *) wqe)->addr =
					cl_hton64(wr->ds_array[i].vaddr);
				wqe += sizeof (struct mthca_data_seg);
				size += sizeof (struct mthca_data_seg) / 16;
			}
//TODO do this also in kernel
//			size += wr->num_ds * (sizeof *seg / 16);
		}

			qp->wrid[ind + qp->rq.max] = wr->wr_id;

		if (opcode == MTHCA_OPCODE_INVALID) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_QP ,("SQ %06x opcode invalid\n",ibqp->qp_num));
			ret = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		((struct mthca_next_seg *) prev_wqe)->nda_op =
			cl_hton32(((ind << qp->sq.wqe_shift) +
			       qp->send_wqe_offset) |
			      opcode);
		wmb();
		((struct mthca_next_seg *) prev_wqe)->ee_nds =
			cl_hton32(MTHCA_NEXT_DBD | size |
			  ((wr->send_opt & IB_SEND_OPT_FENCE) ?
						   MTHCA_NEXT_FENCE : 0));

		if (!size0) {
			size0 = size;
			op0   = opcode;
		}

		dump_wqe( TRACE_LEVEL_VERBOSE, (uint32_t*)qp->sq.last,qp, size);

		++ind;
		if (unlikely(ind >= qp->sq.max))
			ind -= qp->sq.max;
	}

out:
	if (likely(nreq)) {
		doorbell[0] = cl_hton32((nreq << 24) |
				    ((qp->sq.head & 0xffff) << 8) | f0 | op0);
		doorbell[1] = cl_hton32((ibqp->qp_num << 8) | size0);

		qp->sq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();
		*qp->sq.db = cl_hton32(qp->sq.head & 0xffff);

		/*
		 * Make sure doorbell record is written before we
		 * write MMIO send doorbell.
		 */
		wmb();
		mthca_write64(doorbell, to_mctx(ibqp->pd->context), MTHCA_SEND_DOORBELL);
	}

err_busy:
	cl_spinlock_release(&qp->sq.lock);

	UVP_EXIT(UVP_DBG_QP);
	
	return ret;
}

int mthca_arbel_post_recv(struct ibv_qp *ibqp, struct _ib_recv_wr *wr,
			  struct _ib_recv_wr **bad_wr)
{
	struct mthca_qp *qp = to_mqp(ibqp);
	int ret = 0;
	int nreq;
	int ind;
	int i;
	uint8_t *wqe;
	
	UVP_ENTER(UVP_DBG_QP);
	
 	cl_spinlock_acquire(&qp->rq.lock);

	/* XXX check that state is OK to post receive */

	ind = qp->rq.head & (qp->rq.max - 1);
	if(ibqp->state == IBV_QPS_RESET) {
		ret = -EBUSY;
		if (bad_wr)
			*bad_wr = wr;
		goto err_busy;
	}
	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (mthca_wq_overflow(&qp->rq, nreq, to_mcq(qp->ibv_qp.recv_cq))) {//TODO sleybo: check the cq
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("RQ %06x full (%u head, %u tail,"
					" %d max, %d nreq)\n", ibqp->qp_num,
					qp->rq.head, qp->rq.tail,
					qp->rq.max, nreq));
			ret = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_recv_wqe(qp, ind);

		((struct mthca_next_seg *) wqe)->flags = 0;

		wqe += sizeof (struct mthca_next_seg);

		if (unlikely((int)wr->num_ds > qp->rq.max_gs)) {
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_QP ,("RQ %06x full too many scatter\n",ibqp->qp_num));
			ret = -ERANGE;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		for (i = 0; i < (int)wr->num_ds; ++i) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cl_hton32(wr->ds_array[i].length);
			((struct mthca_data_seg *) wqe)->lkey =
				cl_hton32(wr->ds_array[i].lkey);
			((struct mthca_data_seg *) wqe)->addr =
				cl_hton64(wr->ds_array[i].vaddr);
			wqe += sizeof (struct mthca_data_seg);
		}

		if (i < qp->rq.max_gs) {
			((struct mthca_data_seg *) wqe)->byte_count = 0;
			((struct mthca_data_seg *) wqe)->lkey = cl_hton32(MTHCA_INVAL_LKEY);
			((struct mthca_data_seg *) wqe)->addr = 0;
		}

			qp->wrid[ind] = wr->wr_id;

		++ind;
		if (unlikely(ind >= qp->rq.max))
			ind -= qp->rq.max;
	}
out:
	if (likely(nreq)) {
		qp->rq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		mb();
		*qp->rq.db = cl_hton32(qp->rq.head & 0xffff);
	}

err_busy:
	cl_spinlock_release(&qp->rq.lock);
	
	UVP_EXIT(UVP_DBG_QP);
	
	return ret;
}

int mthca_alloc_qp_buf(struct ibv_pd *pd, struct ibv_qp_cap *cap,
		       ib_qp_type_t type, struct mthca_qp *qp)
{
	int size;
	int max_sq_sge;

	qp->rq.max_gs 	 = cap->max_recv_sge;
	qp->sq.max_gs 	 = cap->max_send_sge;
	max_sq_sge 	 = align(cap->max_inline_data + sizeof (struct mthca_inline_seg),
				 sizeof (struct mthca_data_seg)) / sizeof (struct mthca_data_seg);
	if (max_sq_sge < (int)cap->max_send_sge)
		max_sq_sge = cap->max_send_sge;

	qp->wrid = cl_malloc((qp->rq.max + qp->sq.max) * sizeof (uint64_t));
	if (!qp->wrid)
		return -1;

	size = sizeof (struct mthca_next_seg) +
		qp->rq.max_gs * sizeof (struct mthca_data_seg);

	for (qp->rq.wqe_shift = 6; 1 << qp->rq.wqe_shift < size;
	     qp->rq.wqe_shift++)
		; /* nothing */

	size = max_sq_sge * sizeof (struct mthca_data_seg);
	switch (type) {
	case IB_QPT_UNRELIABLE_DGRM:
		size += mthca_is_memfree(pd->context) ?
			sizeof (struct mthca_arbel_ud_seg) :
			sizeof (struct mthca_tavor_ud_seg);
		break;

	case IB_QPT_UNRELIABLE_CONN:
		size += sizeof (struct mthca_raddr_seg);
		break;

	case IB_QPT_RELIABLE_CONN:
		size += sizeof (struct mthca_raddr_seg);
		/*
		 * An atomic op will require an atomic segment, a
		 * remote address segment and one scatter entry.
		 */
		if (size < (sizeof (struct mthca_atomic_seg) +
			    sizeof (struct mthca_raddr_seg) +
			    sizeof (struct mthca_data_seg)))
			size = (sizeof (struct mthca_atomic_seg) +
				sizeof (struct mthca_raddr_seg) +
				sizeof (struct mthca_data_seg));
		break;

	default:
		break;
	}

	/* Make sure that we have enough space for a bind request */
	if (size < sizeof (struct mthca_bind_seg))
		size = sizeof (struct mthca_bind_seg);

	size += sizeof (struct mthca_next_seg);

	for (qp->sq.wqe_shift = 6; 1 << qp->sq.wqe_shift < size;
		qp->sq.wqe_shift++)
		; /* nothing */

		qp->send_wqe_offset = align(qp->rq.max << qp->rq.wqe_shift,
			1 << qp->sq.wqe_shift);

		qp->buf_size = qp->send_wqe_offset + (qp->sq.max << qp->sq.wqe_shift);

	if (posix_memalign(&qp->buf, g_page_size,
		align(qp->buf_size, g_page_size))) {
		cl_free(qp->wrid);
		return -1;
	}

	memset(qp->buf, 0, qp->buf_size);

	if (mthca_is_memfree(pd->context)) {
		struct mthca_next_seg *next;
		struct mthca_data_seg *scatter;
		int i;
		uint32_t sz;

		sz = cl_hton32((sizeof (struct mthca_next_seg) +
			    qp->rq.max_gs * sizeof (struct mthca_data_seg)) / 16);

		for (i = 0; i < qp->rq.max; ++i) {
			next = get_recv_wqe(qp, i);
			next->nda_op = cl_hton32(((i + 1) & (qp->rq.max - 1)) <<
					     qp->rq.wqe_shift);
			next->ee_nds = sz;

			for (scatter = (void *) (next + 1);
			     (void *) scatter < (void *) ((char *)next + (uint32_t)(1 << qp->rq.wqe_shift));
			     ++scatter)
				scatter->lkey = cl_hton32(MTHCA_INVAL_LKEY);
		}

		for (i = 0; i < qp->sq.max; ++i) {
			next = get_send_wqe(qp, i);
			next->nda_op = cl_hton32((((i + 1) & (qp->sq.max - 1)) <<
					      qp->sq.wqe_shift) +
					     qp->send_wqe_offset);
		}
	}

	qp->sq.last = get_send_wqe(qp, qp->sq.max - 1);
	qp->rq.last = get_recv_wqe(qp, qp->rq.max - 1);

	return 0;
}

struct mthca_qp *mthca_find_qp(struct mthca_context *ctx, uint32_t qpn)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	if (ctx->qp_table[tind].refcnt)
		return ctx->qp_table[tind].table[qpn & ctx->qp_table_mask];
	else
		return NULL;
}

int mthca_store_qp(struct mthca_context *ctx, uint32_t qpn, struct mthca_qp *qp)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;
	int ret = 0;

	WaitForSingleObject( ctx->qp_table_mutex, INFINITE );

	if (!ctx->qp_table[tind].refcnt) {
		ctx->qp_table[tind].table = cl_malloc(
			(ctx->qp_table_mask + 1) * sizeof (struct mthca_qp *));
		if (!ctx->qp_table[tind].table) {
			ret = -1;
			goto out;
		}
	}
	++ctx->qp_table[tind].refcnt;
	ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = qp;

out:
	ReleaseMutex( ctx->qp_table_mutex );
	return ret;
}

void mthca_clear_qp(struct mthca_context *ctx, uint32_t qpn)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	WaitForSingleObject( ctx->qp_table_mutex, INFINITE );

	if (!--ctx->qp_table[tind].refcnt)
		cl_free(ctx->qp_table[tind].table);
	else
		ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = NULL;
	
	ReleaseMutex( ctx->qp_table_mutex );
}

int mthca_free_err_wqe(struct mthca_qp *qp, int is_send,
		       int index, int *dbd, uint32_t *new_wqe)
{
	struct mthca_next_seg *next;

	/*
	 * For SRQs, all WQEs generate a CQE, so we're always at the
	 * end of the doorbell chain.
	 */
	if (qp->ibv_qp.srq) {
		*new_wqe = 0;
		return 0;
	}

	if (is_send)
		next = get_send_wqe(qp, index);
	else
		next = get_recv_wqe(qp, index);

	*dbd = !!(next->ee_nds & cl_hton32(MTHCA_NEXT_DBD));
	if (next->ee_nds & cl_hton32(0x3f))
		*new_wqe = (next->nda_op & cl_hton32(~0x3f)) |
			(next->ee_nds & cl_hton32(0x3f));
	else
		*new_wqe = 0;

	return 0;
}

