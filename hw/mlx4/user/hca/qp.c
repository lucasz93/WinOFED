/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
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
#include "doorbell.h"
#include "wqe.h"
#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#include "qp.tmh"
#endif

static enum mlx4_opcode_type __to_opcode(ib_send_wr_t *wr)
{

	enum mlx4_opcode_type opcode = MLX4_OPCODE_INVALID;

	switch (wr->wr_type) {
	case WR_SEND: 
		opcode = (wr->send_opt & IB_SEND_OPT_IMMEDIATE) ? 
					MLX4_OPCODE_SEND_IMM : MLX4_OPCODE_SEND;
		break;
	case WR_RDMA_WRITE:	
		opcode = (wr->send_opt & IB_SEND_OPT_IMMEDIATE) ?
					MLX4_OPCODE_RDMA_WRITE_IMM : MLX4_OPCODE_RDMA_WRITE;
		break;
	case WR_RDMA_READ:
		opcode = MLX4_OPCODE_RDMA_READ;
		break;
	case WR_COMPARE_SWAP:
		opcode = MLX4_OPCODE_ATOMIC_CS;
		break;
	case WR_FETCH_ADD:
		opcode = MLX4_OPCODE_ATOMIC_FA;
		break;
    case WR_NOP:
        opcode = MLX4_OPCODE_NOP;
        break;
	default:
		opcode = MLX4_OPCODE_INVALID;
		break;
	}

	return opcode;
}

static void *get_recv_wqe(struct mlx4_qp *qp, int n)
{
	return qp->buf.buf + qp->rq.offset + (n << qp->rq.wqe_shift);
}

static void *get_send_wqe(struct mlx4_qp *qp, int n)
{
	return qp->buf.buf + qp->sq.offset + (n << qp->sq.wqe_shift);
}

/*
 * Stamp a SQ WQE so that it is invalid if prefetched by marking the
 * first four bytes of every 64 byte chunk with 0xffffffff, except for
 * the very first chunk of the WQE.
 */
static void stamp_send_wqe(struct mlx4_qp *qp, int n)
{
	uint32_t *wqe = get_send_wqe(qp, n);
	int i;

	for (i = 16; i < 1 << (qp->sq.wqe_shift - 2); i += 16)
		wqe[i] = 0xffffffff;
}

void mlx4_init_qp_indices(struct mlx4_qp *qp)
{
	qp->sq.head	 = 0;
	qp->sq.tail	 = 0;
	qp->rq.head	 = 0;
	qp->rq.tail	 = 0;
}

void mlx4_qp_init_sq_ownership(struct mlx4_qp *qp)
{
	struct mlx4_wqe_ctrl_seg *ctrl;
	int i;

	for (i = 0; i < qp->sq.wqe_cnt; ++i) {
		ctrl = get_send_wqe(qp, i);
		ctrl->owner_opcode = htonl((uint32_t)1 << 31);

		stamp_send_wqe(qp, i);
	}
}

static int wq_overflow(struct mlx4_wq *wq, int nreq, struct mlx4_cq *cq)
{
	int cur;

	cur = wq->head - wq->tail;
	if (cur + nreq < wq->max_post)
		return 0;

	pthread_spin_lock(&cq->lock);
	cur = wq->head - wq->tail;
	pthread_spin_unlock(&cq->lock);

	return cur + nreq >= wq->max_post;
}

static inline void set_raddr_seg(struct mlx4_wqe_raddr_seg *rseg,
				 uint64_t remote_addr, uint32_t rkey)
{
	rseg->raddr    = cl_hton64(remote_addr);
	rseg->rkey     = rkey;
	rseg->reserved = 0;
}

static void set_atomic_seg(struct mlx4_wqe_atomic_seg *aseg, ib_send_wr_t *wr)
{
	if (wr->wr_type == WR_COMPARE_SWAP) {
		aseg->swap_add = wr->remote_ops.atomic2;
		aseg->compare  = wr->remote_ops.atomic1;
	} else {
		aseg->swap_add = wr->remote_ops.atomic1;
		aseg->compare  = 0;
	}
}

static void set_datagram_seg(struct mlx4_wqe_datagram_seg *dseg, ib_send_wr_t *wr)
{
	memcpy(dseg->av, &to_mah((struct ibv_ah *)wr->dgrm.ud.h_av)->av, sizeof (struct mlx4_av));
	dseg->dqpn = wr->dgrm.ud.remote_qp;
	dseg->qkey = wr->dgrm.ud.remote_qkey;
	dseg->vlan = to_mah((struct ibv_ah *)wr->dgrm.ud.h_av)->av.eth.vlan;
	memcpy(dseg->mac_0_1, to_mah((struct ibv_ah *)wr->dgrm.ud.h_av)->av.eth.mac_0_1, 6);
}

static void __set_data_seg(struct mlx4_wqe_data_seg *dseg, ib_local_ds_t *ds)
{
	dseg->byte_count = cl_hton32(ds->length);
	dseg->lkey       = cl_hton32(ds->lkey);
	dseg->addr       = cl_hton64(ds->vaddr);
}

static void set_data_seg(struct mlx4_wqe_data_seg *dseg, ib_local_ds_t *ds)
{
	dseg->lkey       = cl_hton32(ds->lkey);
	dseg->addr       = cl_hton64(ds->vaddr);

	/*
	 * Need a barrier here before writing the byte_count field to
	 * make sure that all the data is visible before the
	 * byte_count field is set.  Otherwise, if the segment begins
	 * a new cacheline, the HCA prefetcher could grab the 64-byte
	 * chunk and get a valid (!= * 0xffffffff) byte count but
	 * stale data, and end up sending the wrong data.
	 */
	wmb();

	dseg->byte_count = cl_hton32(ds->length);
}

/*
 * Avoid using memcpy() to copy to BlueFlame page, since memcpy()
 * implementations may use move-string-buffer assembler instructions,
 * which do not guarantee order of copying.
 */
static void mlx4_bf_copy(unsigned long *dst, unsigned long *src, unsigned bytecnt)
{
#ifdef _WIN64
	uint64_t *d = (uint64_t *)dst;
	uint64_t *s = (uint64_t *)src;

	while (bytecnt > 0) {
		*d++ = *s++;
		*d++ = *s++;
		bytecnt -= 2 * sizeof (uint64_t);
	}
#else
	while (bytecnt > 0) {
		*dst++ = *src++;
		*dst++ = *src++;
		bytecnt -= 2 * sizeof (unsigned long);
	}
#endif	
}

ib_api_status_t
mlx4_post_send(
	IN		const	void*						h_qp,
	IN				ib_send_wr_t*	const		p_wr,
		OUT			ib_send_wr_t**				bad_wr)
{
	struct ibv_qp *ibqp = (struct ibv_qp *)/*Ptr64ToPtr(*/h_qp/*)*/;
	struct mlx4_qp *qp = to_mqp(ibqp);
	struct mlx4_context *ctx;
	uint8_t *wqe;
	struct mlx4_wqe_ctrl_seg *ctrl = NULL;
	enum mlx4_opcode_type opcode;
	int ind;
	int nreq;
	int inl = 0;
	ib_api_status_t status = IB_SUCCESS;
	ib_send_wr_t *wr = p_wr;
	int size = 0;
	uint32_t i;

	pthread_spin_lock(&qp->sq.lock);

	ind = qp->sq.head;

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (wq_overflow(&qp->sq, nreq, to_mcq(qp->ibv_qp.send_cq))) {
			status = IB_INSUFFICIENT_RESOURCES;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		if (wr->num_ds > (uint32_t)qp->sq.max_gs) {
			status = IB_INVALID_MAX_SGE;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		opcode = __to_opcode(wr);
		if (opcode == MLX4_OPCODE_INVALID) {
			status = IB_INVALID_WR_TYPE;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_send_wqe(qp, ind & (qp->sq.wqe_cnt - 1));
		ctrl = (struct mlx4_wqe_ctrl_seg *)wqe;
		qp->sq.wrid[ind & (qp->sq.wqe_cnt - 1)] = wr->wr_id;

		ctrl->xrcrb_flags =
			(wr->send_opt & IB_SEND_OPT_SIGNALED ?
			 cl_hton32(MLX4_WQE_CTRL_CQ_UPDATE) : 0) |
			(wr->send_opt & IB_SEND_OPT_SOLICITED ?
			 cl_hton32(MLX4_WQE_CTRL_SOLICIT) : 0)   |
			qp->sq_signal_bits;

		if (opcode == MLX4_OPCODE_SEND_IMM ||
		    opcode == MLX4_OPCODE_RDMA_WRITE_IMM)
			ctrl->imm = wr->immediate_data;
		else
			ctrl->imm = 0;

		wqe += sizeof *ctrl;
		size = sizeof *ctrl / 16;

		switch (ibqp->qp_type) {
#ifdef XRC_SUPPORT
		case IBV_QPT_XRC:
			// TODO: why is the following line outcommented ?
			//ctrl->xrcrb_flags |= cl_hton32(wr->xrc_remote_srq_num << 8);
			/* fall thru */
#endif			
		case IBV_QPT_RC:
		case IBV_QPT_UC:
			switch (opcode) {
			case MLX4_OPCODE_ATOMIC_CS:
			case MLX4_OPCODE_ATOMIC_FA:
				set_raddr_seg((struct mlx4_wqe_raddr_seg *)wqe, wr->remote_ops.vaddr,
								wr->remote_ops.rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);

				set_atomic_seg((struct mlx4_wqe_atomic_seg *)wqe, wr);
				wqe  += sizeof (struct mlx4_wqe_atomic_seg);
				size += (sizeof (struct mlx4_wqe_raddr_seg) +
					 sizeof (struct mlx4_wqe_atomic_seg)) / 16;

				break;

			case MLX4_OPCODE_RDMA_READ:
				inl = 1;
				/* fall through */
			case MLX4_OPCODE_RDMA_WRITE:
			case MLX4_OPCODE_RDMA_WRITE_IMM:
				set_raddr_seg((struct mlx4_wqe_raddr_seg *)wqe, wr->remote_ops.vaddr,
								wr->remote_ops.rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);
				size += sizeof (struct mlx4_wqe_raddr_seg) / 16;

				break;

			default:
				/* No extra segments required for sends */
				break;
			}
			break;

		case IBV_QPT_UD:
			set_datagram_seg((struct mlx4_wqe_datagram_seg *)wqe, wr);
			wqe  += sizeof (struct mlx4_wqe_datagram_seg);
			size += sizeof (struct mlx4_wqe_datagram_seg) / 16;
			break;

		default:
			break;
		}

		if (wr->send_opt & IB_SEND_OPT_INLINE && wr->num_ds) {
			struct mlx4_wqe_inline_seg *seg;
			uint8_t *addr;
			int len, seg_len;
			int num_seg;
			int off, to_copy;

			inl = 0;

			seg = (struct mlx4_wqe_inline_seg *)wqe;
			wqe += sizeof *seg;
			off = (int)(((uintptr_t) wqe) & (MLX4_INLINE_ALIGN - 1));
			num_seg = 0;
			seg_len = 0;

			for (i = 0; i < wr->num_ds; ++i) {
				addr = (uint8_t *)(uintptr_t)wr->ds_array[i].vaddr;
				len  = wr->ds_array[i].length;
				inl += len;

				if ((uint32_t)inl > (uint32_t)qp->max_inline_data) {
					inl = 0;
					status = IB_INVALID_PARAMETER;
					*bad_wr = wr;
					goto out;
				}

				while (len >= MLX4_INLINE_ALIGN - off) {
					to_copy = MLX4_INLINE_ALIGN - off;
					memcpy(wqe, addr, to_copy);
					len -= to_copy;
					wqe += to_copy;
					addr += to_copy;
					seg_len += to_copy;
					wmb(); /* see comment below */
					seg->byte_count = htonl(MLX4_INLINE_SEG | seg_len);
					seg_len = 0;
					seg = (struct mlx4_wqe_inline_seg *)wqe;
					wqe += sizeof *seg;
					off = sizeof *seg;
					++num_seg;
				}

				memcpy(wqe, addr, len);
				wqe += len;
				seg_len += len;
				off += len;
			}

			if (seg_len) {
				++num_seg;
				/*
				 * Need a barrier here to make sure
				 * all the data is visible before the
				 * byte_count field is set.  Otherwise
				 * the HCA prefetcher could grab the
				 * 64-byte chunk with this inline
				 * segment and get a valid (!=
				 * 0xffffffff) byte count but stale
				 * data, and end up sending the wrong
				 * data.
				 */
				wmb();
				seg->byte_count = htonl(MLX4_INLINE_SEG | seg_len);
			}

			size += (inl + num_seg * sizeof * seg + 15) / 16;
		} else {
			struct mlx4_wqe_data_seg *seg = (struct mlx4_wqe_data_seg *)wqe;

			for (i = wr->num_ds; i > 0; --i)
				set_data_seg(seg + i - 1, wr->ds_array + i - 1);

			size += wr->num_ds * (sizeof *seg / 16);
		}

		ctrl->fence_size = (uint8_t)((wr->send_opt & IB_SEND_OPT_FENCE ?
				    					MLX4_WQE_CTRL_FENCE : 0) | size);

		/*
		 * Make sure descriptor is fully written before
		 * setting ownership bit (because HW can start
		 * executing as soon as we do).
		 */
		wmb();

		ctrl->owner_opcode = htonl(opcode) |
			(ind & qp->sq.wqe_cnt ? htonl((uint32_t)1 << 31) : 0);

		/*
		 * We can improve latency by not stamping the last
		 * send queue WQE until after ringing the doorbell, so
		 * only stamp here if there are still more WQEs to post.
		 */
		if (wr->p_next)
			stamp_send_wqe(qp, (ind + qp->sq_spare_wqes) &
				       (qp->sq.wqe_cnt - 1));

		++ind;

		MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_QP, ("qpn %#x, wr_id %#I64x, ix %d, solicited %d\n", 
			qp->ibv_qp.qp_num, wr->wr_id, ind - 1, wr->send_opt & IB_SEND_OPT_SOLICITED)); 
	}

out:
	ctx = to_mctx(ibqp->context);

	if (nreq == 1 && inl && size > 1 && size < ctx->bf_buf_size / 16) {
		ctrl->owner_opcode |= htonl((qp->sq.head & 0xffff) << 8);
		*(uint32_t *) ctrl->reserved |= qp->doorbell_qpn;
		/*
		 * Make sure that descriptor is written to memory
		 * before writing to BlueFlame page.
		 */
		wmb();

		++qp->sq.head;

		pthread_spin_lock(&ctx->bf_lock);

		mlx4_bf_copy((unsigned long *) (ctx->bf_page + ctx->bf_offset),
						(unsigned long *) ctrl, align(size * 16, 64));

		wc_wmb();

		ctx->bf_offset ^= ctx->bf_buf_size;

		pthread_spin_unlock(&ctx->bf_lock);
	}else if (nreq) {
		qp->sq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*(uint32_t *) (ctx->uar + MLX4_SEND_DOORBELL) = qp->doorbell_qpn;
	}

	if (nreq)
		stamp_send_wqe(qp, (ind + qp->sq_spare_wqes - 1) &
			       (qp->sq.wqe_cnt - 1));

	pthread_spin_unlock(&qp->sq.lock);

	return status;
}


ib_api_status_t
mlx4_post_recv(
	IN		const	void*						h_qp,
	IN				ib_recv_wr_t* 	const		p_wr,
		OUT			ib_recv_wr_t**				bad_wr)
{
	struct mlx4_qp *qp = to_mqp((struct ibv_qp *)/*Ptr64ToPtr(*/h_qp/*)*/);
	struct mlx4_wqe_data_seg *scat;
	ib_api_status_t status = IB_SUCCESS;
	ib_recv_wr_t *wr = p_wr;
	int nreq;
	int ind;
	uint32_t i;

	pthread_spin_lock(&qp->rq.lock);

	ind = qp->rq.head & (qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (wq_overflow(&qp->rq, nreq, to_mcq(qp->ibv_qp.recv_cq))) {
			status = IB_INSUFFICIENT_RESOURCES;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		if (wr->num_ds > (uint32_t)qp->rq.max_gs) {
			status = IB_INVALID_MAX_SGE;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		scat = get_recv_wqe(qp, ind);

		for (i = 0; i < wr->num_ds; ++i)
			__set_data_seg(scat + i, wr->ds_array + i);

		if (i < (uint32_t)qp->rq.max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = htonl(MLX4_INVALID_LKEY);
			scat[i].addr       = 0;
		}

		qp->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (qp->rq.wqe_cnt - 1);

		MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_QP, ("qpn %#x, wr_id %#I64x, ix %d, \n", 
			qp->ibv_qp.qp_num, wr->wr_id, ind - 1)); 
	}

out:
	if (nreq) {
		qp->rq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*qp->db = htonl(qp->rq.head & 0xffff);
	}

	pthread_spin_unlock(&qp->rq.lock);

	return status;
}

static int num_inline_segs(int data, enum ibv_qp_type type)
{
	/*
	 * Inline data segments are not allowed to cross 64 byte
	 * boundaries.  For UD QPs, the data segments always start
	 * aligned to 64 bytes (16 byte control segment + 48 byte
	 * datagram segment); for other QPs, there will be a 16 byte
	 * control segment and possibly a 16 byte remote address
	 * segment, so in the worst case there will be only 32 bytes
	 * available for the first data segment.
	 */
	if (type == IBV_QPT_UD)
		data += (sizeof (struct mlx4_wqe_ctrl_seg) +
			 sizeof (struct mlx4_wqe_datagram_seg)) %
			MLX4_INLINE_ALIGN;
	else
		data += (sizeof (struct mlx4_wqe_ctrl_seg) +
			 sizeof (struct mlx4_wqe_raddr_seg)) %
			MLX4_INLINE_ALIGN;

	return (int)(data + MLX4_INLINE_ALIGN - sizeof (struct mlx4_wqe_inline_seg) - 1) /
		(MLX4_INLINE_ALIGN - sizeof (struct mlx4_wqe_inline_seg));
}

void mlx4_calc_sq_wqe_size(struct ibv_qp_cap *cap, enum ibv_qp_type type,
			   struct mlx4_qp *qp)
{
	int size;
	unsigned max_sq_sge;

	max_sq_sge	 = align(cap->max_inline_data +
				 num_inline_segs(cap->max_inline_data, type) *
				 sizeof (struct mlx4_wqe_inline_seg),
				 sizeof (struct mlx4_wqe_data_seg)) /
		sizeof (struct mlx4_wqe_data_seg);
	if (max_sq_sge < cap->max_send_sge)
		max_sq_sge = cap->max_send_sge;

	size = max_sq_sge * sizeof (struct mlx4_wqe_data_seg);
	switch (type) {
	case IBV_QPT_UD:
		size += sizeof (struct mlx4_wqe_datagram_seg);
		break;

	case IBV_QPT_UC:
		size += sizeof (struct mlx4_wqe_raddr_seg);
		break;

#ifdef XRC_SUPPORT
	case IBV_QPT_XRC:
#endif		
	case IBV_QPT_RC:
		size += sizeof (struct mlx4_wqe_raddr_seg);
		/*
		 * An atomic op will require an atomic segment, a
		 * remote address segment and one scatter entry.
		 */
		if (size < (sizeof (struct mlx4_wqe_atomic_seg) +
			    sizeof (struct mlx4_wqe_raddr_seg) +
			    sizeof (struct mlx4_wqe_data_seg)))
			size = (sizeof (struct mlx4_wqe_atomic_seg) +
				sizeof (struct mlx4_wqe_raddr_seg) +
				sizeof (struct mlx4_wqe_data_seg));
		break;

	default:
		break;
	}

	/* Make sure that we have enough space for a bind request */
	if (size < sizeof (struct mlx4_wqe_bind_seg))
		size = sizeof (struct mlx4_wqe_bind_seg);

	size += sizeof (struct mlx4_wqe_ctrl_seg);

	for (qp->sq.wqe_shift = 6; 1 << qp->sq.wqe_shift < size;
	     qp->sq.wqe_shift++)
		; /* nothing */
}

int mlx4_alloc_qp_buf(struct ibv_pd *pd, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type, struct mlx4_qp *qp)
{
	UNREFERENCED_PARAMETER(type);
	
	qp->rq.max_gs	 = cap->max_recv_sge;

	qp->sq.wrid = malloc(qp->sq.wqe_cnt * sizeof (uint64_t));
	if (!qp->sq.wrid)
		return -1;

	if (qp->rq.wqe_cnt) {
		qp->rq.wrid = malloc(qp->rq.wqe_cnt * sizeof (uint64_t));
		if (!qp->rq.wrid) {
			free(qp->sq.wrid);
			return -1;
		}
	}

	for (qp->rq.wqe_shift = 4;
		(1 << qp->rq.wqe_shift) < qp->rq.max_gs * (int) sizeof (struct mlx4_wqe_data_seg);
		qp->rq.wqe_shift++)
		; /* nothing */

	qp->buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
		(qp->sq.wqe_cnt << qp->sq.wqe_shift);
	if (qp->rq.wqe_shift > qp->sq.wqe_shift) {
		qp->rq.offset = 0;
		qp->sq.offset = qp->rq.wqe_cnt << qp->rq.wqe_shift;
	} else {
		qp->rq.offset = qp->sq.wqe_cnt << qp->sq.wqe_shift;
		qp->sq.offset = 0;
	}

    qp->buf_size = align(qp->buf_size, pd->context->page_size);

	if (mlx4_alloc_buf(&qp->buf, qp->buf_size, pd->context->page_size)) {
		free(qp->sq.wrid);
		if (qp->rq.wqe_cnt)
			free(qp->rq.wrid);
		return -1;
	}

	mlx4_qp_init_sq_ownership(qp);

	return 0;
}

void mlx4_set_sq_sizes(struct mlx4_qp *qp, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type)
{
	int wqe_size;
	struct mlx4_context *ctx = to_mctx(qp->ibv_qp.context);

	wqe_size = (1 << qp->sq.wqe_shift) - (int) sizeof (struct mlx4_wqe_ctrl_seg);
	
	switch (type) {
	case IBV_QPT_UD:
		wqe_size -= sizeof (struct mlx4_wqe_datagram_seg);
		break;

	case IBV_QPT_UC:
	case IBV_QPT_RC:
#ifdef XRC_SUPPORT
	case IBV_QPT_XRC:
#endif		
		wqe_size -= sizeof (struct mlx4_wqe_raddr_seg);
		break;

	default:
		break;
	}

	qp->sq.max_gs	     = wqe_size / sizeof (struct mlx4_wqe_data_seg);
	cap->max_send_sge    = min(ctx->max_sge, qp->sq.max_gs);
	qp->sq.max_post	     = min(ctx->max_qp_wr,
				   qp->sq.wqe_cnt - qp->sq_spare_wqes);
	cap->max_send_wr     = qp->sq.max_post;

	/*
	 * Inline data segments can't cross a 64 byte boundary.  So
	 * subtract off one segment header for each 64-byte chunk,
	 * taking into account the fact that wqe_size will be 32 mod
	 * 64 for non-UD QPs.
	 */
	qp->max_inline_data  = wqe_size -
		(int) sizeof (struct mlx4_wqe_inline_seg) *
		(align(wqe_size, MLX4_INLINE_ALIGN) / MLX4_INLINE_ALIGN);
	cap->max_inline_data = qp->max_inline_data;
}

struct mlx4_qp *mlx4_find_qp(struct mlx4_context *ctx, uint32_t qpn)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	if (ctx->qp_table[tind].refcnt)
		return ctx->qp_table[tind].table[qpn & ctx->qp_table_mask];
	else
		return NULL;
}

int mlx4_store_qp(struct mlx4_context *ctx, uint32_t qpn, struct mlx4_qp *qp)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;
	int ret = 0;

	pthread_mutex_lock(&ctx->qp_table_mutex);

	if (!ctx->qp_table[tind].refcnt) {
		ctx->qp_table[tind].table = calloc(ctx->qp_table_mask + 1,
						   sizeof (struct mlx4_qp *));
		if (!ctx->qp_table[tind].table) {
			ret = -1;
			goto out;
		}
	}

	++ctx->qp_table[tind].refcnt;
	ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = qp;

out:
	pthread_mutex_unlock(&ctx->qp_table_mutex);
	return ret;
}

void mlx4_clear_qp(struct mlx4_context *ctx, uint32_t qpn)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

    if (ctx->qp_table[tind].table == NULL) {
        return;
    }

	pthread_mutex_lock(&ctx->qp_table_mutex);

	if (!--ctx->qp_table[tind].refcnt)
		free(ctx->qp_table[tind].table);
	else
		ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = NULL;

	pthread_mutex_unlock(&ctx->qp_table_mutex);
}
