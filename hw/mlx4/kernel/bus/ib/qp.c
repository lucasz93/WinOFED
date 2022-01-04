/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include "mlx4_ib.h"
#include "ib_cache.h"
#include "ib_pack.h"
#include "qp.h"
#include "mx_abi.h"
#include "mlx4_debug.h"
#include "mmintrin.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "qp.tmh"
#endif


void st_print_mlx_header( struct mlx4_dev *mdev, struct mlx4_ib_sqp *sqp, struct mlx4_wqe_mlx_seg *mlx );
void st_print_mlx_send(struct mlx4_dev *mdev, struct ib_qp *ibqp, ib_send_wr_t *wr);
void st_dump_mlx_wqe(struct mlx4_dev *mdev, void *wqe, int size_in_dwords, ib_send_wr_t *wr);

enum {
	MLX4_IB_ACK_REQ_FREQ	= 8,
};

enum {
	MLX4_IB_DEFAULT_SCHED_QUEUE	= 0x83,
	MLX4_IB_DEFAULT_QP0_SCHED_QUEUE	= 0x3f,
	MLX4_IB_LINK_TYPE_IB		= 0,
	MLX4_IB_LINK_TYPE_ETH		= 1
};

enum {
	MLX4_RDMAOE_ETHERTYPE = 0x8915
};

enum {
	MLX4_IB_MIN_SQ_STRIDE = 6
};

static const __be32 mlx4_ib_opcode[] = {
	__constant_cpu_to_be32(MLX4_OPCODE_RDMA_WRITE),		/* 	[IB_WR_RDMA_WRITE]			*/
	__constant_cpu_to_be32(MLX4_OPCODE_RDMA_WRITE_IMM),	/* 	[IB_WR_RDMA_WRITE_WITH_IMM] */
	__constant_cpu_to_be32(MLX4_OPCODE_SEND),			/* 	[IB_WR_SEND]				*/
	__constant_cpu_to_be32(MLX4_OPCODE_SEND_IMM),		/* 	[IB_WR_SEND_WITH_IMM]		*/
	__constant_cpu_to_be32(MLX4_OPCODE_RDMA_READ),		/* 	[IB_WR_RDMA_READ]			*/
	__constant_cpu_to_be32(MLX4_OPCODE_ATOMIC_CS),		/* 	[IB_WR_ATOMIC_CMP_AND_SWP]	*/
	__constant_cpu_to_be32(MLX4_OPCODE_ATOMIC_FA),		/* 	[IB_WR_ATOMIC_FETCH_AND_ADD]*/
	__constant_cpu_to_be32(MLX4_OPCODE_LSO | (1 << 6)),	/* 	[IB_WR_LSO]					*/


	__constant_cpu_to_be32(MLX4_OPCODE_SEND_INVAL),		/* 	[IB_WR_SEND_WITH_INV]	*/
	__constant_cpu_to_be32(MLX4_OPCODE_RDMA_READ),		/* 	[IB_WR_RDMA_READ_WITH_INV]	*/
	__constant_cpu_to_be32(MLX4_OPCODE_LOCAL_INVAL),	/* 	[IB_WR_LOCAL_INV]	*/
	__constant_cpu_to_be32(MLX4_OPCODE_FMR),			/* 	[IB_WR_FAST_REG_MR]	*/



	__constant_cpu_to_be32(MLX4_OPCODE_NOP)				/*	[IB_WR_NOP]					*/
};


//?????????????????	IB_WR_RDMA_READ_WITH_INV,  //???????????????

static struct mlx4_ib_sqp *to_msqp(struct mlx4_ib_qp *mqp)
{
	return container_of(mqp, struct mlx4_ib_sqp, qp);
}

static int is_tunnel_qp(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	return qp->mqp.qpn >= dev->dev->caps.tunnel_qpn &&
		qp->mqp.qpn <= dev->dev->caps.tunnel_qpn + 1; /* XXX use num_ports instead */
}

static int is_sqp(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	return qp->mqp.qpn >= dev->dev->caps.sqp_start &&
		qp->mqp.qpn <= dev->dev->caps.sqp_start + 3;
}

static int is_qp0(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	return qp->mqp.qpn >= dev->dev->caps.sqp_start &&
		qp->mqp.qpn <= dev->dev->caps.sqp_start + 1;
}

static inline void *get_wqe(struct mlx4_ib_qp *qp, int offset)
{
	if (qp->buf.is_direct)
		return qp->buf.direct.buf + offset;
	else
		return qp->buf.page_list[offset >> PAGE_SHIFT].buf +
			(offset & (PAGE_SIZE - 1));
}

static inline void *get_recv_wqe(struct mlx4_ib_qp *qp, int n)
{
	return get_wqe(qp, qp->rq.offset + (n << qp->rq.wqe_shift));
}

static inline void *get_send_wqe(struct mlx4_ib_qp *qp, int n)
{
	return get_wqe(qp, qp->sq.offset + (n << qp->sq.wqe_shift));
}

#ifdef WQE_SHRINK


/*
 * Stamp a SQ WQE so that it is invalid if prefetched by marking the
 * first four bytes of every 64 byte chunk with
 *     0x7FFFFFF | (invalid_ownership_value << 31).
 *
 * When the max work request size is less than or equal to the WQE
 * basic block size, as an optimization, we can stamp all WQEs with
 * 0xffffffff, and skip the very first chunk of each WQE.
 */
static void stamp_send_wqe(struct mlx4_ib_qp *qp, int n, int size)
{
	__be32 *wqe;
	int i;
	int s;
	int ind;
	u8 *buf;
	__be32 stamp;
	struct mlx4_wqe_ctrl_seg *ctrl;

	if (qp->sq_max_wqes_per_wr > 1) {
		s = roundup(size, 1U << qp->sq.wqe_shift);
		for (i = 0; i < s; i += 64) {
			ind = (i >> qp->sq.wqe_shift) + n;
			stamp = ind & qp->sq.wqe_cnt ? cpu_to_be32(0x7fffffff) :
						       cpu_to_be32(0xffffffff);
			buf = get_send_wqe(qp, ind & (qp->sq.wqe_cnt - 1));
			wqe = buf + (i & ((1 << qp->sq.wqe_shift) - 1));
			*wqe = stamp;
		}
	} else {
		ctrl = buf = get_send_wqe(qp, n & (qp->sq.wqe_cnt - 1));
		s = (ctrl->fence_size & 0x3f) << 4;
		for (i = 64; i < s; i += 64) {
			wqe = buf + i;
			*wqe = cpu_to_be32(0xffffffff);
		}
	}
}

static void post_nop_wqe(struct mlx4_ib_qp *qp, int n, int size)
{
	struct mlx4_wqe_ctrl_seg *ctrl;
	struct mlx4_wqe_inline_seg *inl;
	u8 *wqe;
	int s;

	ctrl = wqe = get_send_wqe(qp, n & (qp->sq.wqe_cnt - 1));
	s = sizeof(struct mlx4_wqe_ctrl_seg);

	if (qp->ibqp.qp_type == IB_QPT_UD) {
		struct mlx4_wqe_datagram_seg *dgram = wqe + sizeof *ctrl;
		struct mlx4_av *av = (struct mlx4_av *)dgram->av;
		memset(dgram, 0, sizeof *dgram);
		av->port_pd = cpu_to_be32((qp->port << 24) | to_mpd(qp->ibqp.pd)->pdn);
		s += sizeof(struct mlx4_wqe_datagram_seg);
	}

	/* Pad the remainder of the WQE with an inline data segment. */
	if (size > s) {
		inl = wqe + s;
		inl->byte_count = cpu_to_be32(1 << 31 | (size - s - sizeof *inl));
	}
	ctrl->srcrb_flags = 0;
	ctrl->fence_size = size / 16;
	/*
	 * Make sure descriptor is fully written before setting ownership bit
	 * (because HW can start executing as soon as we do).
	 */
	wmb();

	ctrl->owner_opcode = cpu_to_be32(MLX4_OPCODE_NOP | MLX4_WQE_CTRL_NEC) |
		(n & qp->sq.wqe_cnt ? cpu_to_be32(1 << 31) : 0);

	stamp_send_wqe(qp, n + qp->sq_spare_wqes, size);
}


/* Post NOP WQE to prevent wrap-around in the middle of WR */
static inline unsigned pad_wraparound(struct mlx4_ib_qp *qp, int ind)
{
	unsigned s = qp->sq.wqe_cnt - (ind & (qp->sq.wqe_cnt - 1));
	if (unlikely(s < qp->sq_max_wqes_per_wr)) {
		post_nop_wqe(qp, ind, s << qp->sq.wqe_shift);
		ind += s;
	}
	return ind;
}

#else

/*
 * Stamp a SQ WQE so that it is invalid if prefetched by marking the
 * first four bytes of every 64 byte chunk with 0xffffffff, except for
 * the very first chunk of the WQE.
 */
static void stamp_send_wqe(struct mlx4_ib_qp *qp, int n)
{
	u32 *wqe = (u32*)get_send_wqe(qp, n);
	int i;

	for (i = 16; i < 1 << (qp->sq.wqe_shift - 2); i += 16)
		wqe[i] = 0xffffffff;
}

#endif

static void mlx4_ib_qp_event(struct mlx4_qp *qp, enum mlx4_event type)
{
	ib_event_rec_t event;
	struct ib_qp *ibqp = &to_mibqp(qp)->ibqp;

	if (type == MLX4_EVENT_TYPE_PATH_MIG)
		to_mibqp(qp)->port = to_mibqp(qp)->alt_port;

	switch (type) {
	case MLX4_EVENT_TYPE_PATH_MIG:
		event.type = (ib_async_event_t)IB_EVENT_PATH_MIG;
		break;
	case MLX4_EVENT_TYPE_COMM_EST:
		event.type = (ib_async_event_t)IB_EVENT_COMM_EST;
		break;
	case MLX4_EVENT_TYPE_SQ_DRAINED:
		event.type = (ib_async_event_t)IB_EVENT_SQ_DRAINED;
		break;
	case MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE:
		event.type = (ib_async_event_t)IB_EVENT_QP_LAST_WQE_REACHED;
		break;
	case MLX4_EVENT_TYPE_WQ_CATAS_ERROR:
		event.type = (ib_async_event_t)IB_EVENT_QP_FATAL;
		break;
	case MLX4_EVENT_TYPE_PATH_MIG_FAILED:
		event.type = (ib_async_event_t)IB_EVENT_PATH_MIG_ERR;
		break;
	case MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
		event.type = (ib_async_event_t)IB_EVENT_QP_REQ_ERR;
		break;
	case MLX4_EVENT_TYPE_WQ_ACCESS_ERROR:
		event.type = (ib_async_event_t)IB_EVENT_QP_ACCESS_ERR;
		break;
	default:
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "mlx4_ib: Unexpected event type %d "
		       "on QP %06x\n", type, qp->qpn));
		return;
	}

	event.context = ibqp->qp_context;
	ibqp->event_handler(&event);
}

static int send_wqe_overhead(enum ib_qp_type type, u32 flags)
{
	/*
	 * UD WQEs must have a datagram segment.
	 * RC and UC WQEs might have a remote address segment.
	 * MLX WQEs need two extra inline data segments (for the UD
	 * header and space for the ICRC).
	 */
	switch (type) {
	case IB_QPT_UD:											// total - 96
		return sizeof (struct mlx4_wqe_ctrl_seg)  +
			sizeof (struct mlx4_wqe_datagram_seg) +
			((flags & MLX4_IB_QP_LSO) ? 64 : 0);
	case IB_QPT_UC:											// total - 32
		return sizeof (struct mlx4_wqe_ctrl_seg) +
			sizeof (struct mlx4_wqe_raddr_seg);
	case IB_QPT_RC:											// total - 48
		return sizeof (struct mlx4_wqe_ctrl_seg) +
			sizeof (struct mlx4_wqe_atomic_seg) +
			sizeof (struct mlx4_wqe_raddr_seg);
	case IB_QPT_SMI:
	case IB_QPT_GSI:										// total - 128
		return sizeof (struct mlx4_wqe_ctrl_seg) +			// 16
			ALIGN(MLX4_IB_UD_HEADER_SIZE +					// 96
			      DIV_ROUND_UP(MLX4_IB_UD_HEADER_SIZE,
					   MLX4_INLINE_ALIGN) *
			      sizeof (struct mlx4_wqe_inline_seg),
			      sizeof (struct mlx4_wqe_data_seg)) +
			ALIGN(4 +										// 16
			      sizeof (struct mlx4_wqe_inline_seg),
			      sizeof (struct mlx4_wqe_data_seg));
	default:												// total - 16
		return sizeof (struct mlx4_wqe_ctrl_seg);
	}
}

// inline data segment shouldn't cross 64-byte boundary
static int calculate_max_inline_data(int buf_size, int overhead)
{
	int max_inline = 0, segment, num_seg;
	int max_segment = MLX4_INLINE_ALIGN - sizeof (struct mlx4_wqe_inline_seg);

	// wqe w/o overhead
	buf_size -= overhead;

	// inline data in the first segment
	segment = max_segment - (overhead % MLX4_INLINE_ALIGN);
	max_inline += segment;
	buf_size -= segment + sizeof (struct mlx4_wqe_inline_seg);

	// number of full segments and their size
	num_seg = buf_size / MLX4_INLINE_ALIGN;
	max_inline += num_seg * max_segment;
	buf_size -= num_seg * MLX4_INLINE_ALIGN;

	// last segment
 	max_inline += buf_size - sizeof (struct mlx4_wqe_inline_seg);
	
	return max_inline;
}

static int calculate_buf_size_for_inline_data(int inline_data, int overhead)
{
	int buf_size = 0, segment, num_seg;
	int max_segment = MLX4_INLINE_ALIGN - sizeof (struct mlx4_wqe_inline_seg);

	// inline data in the first segment
	segment = max_segment - (overhead % MLX4_INLINE_ALIGN);
	buf_size += segment + sizeof (struct mlx4_wqe_inline_seg);
	inline_data -= segment;
	
	// number of full segments and their size
	num_seg = inline_data / max_segment;
	buf_size += num_seg * MLX4_INLINE_ALIGN;
	inline_data -= num_seg * max_segment;

	// last segment
 	buf_size += inline_data + sizeof (struct mlx4_wqe_inline_seg);

	return buf_size;
}

static int set_rq_size(struct mlx4_ib_dev *dev, struct ib_qp_cap *cap,
		       int is_user, int has_srq, struct mlx4_ib_qp *qp)
{
	/* Sanity check RQ size before proceeding */
	if ((int)cap->max_recv_wr  > dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE ||
	    (int)cap->max_recv_sge > min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg)) {
	    if ((int)cap->max_recv_wr  > dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE)
			cap->max_recv_wr	= (u32)(dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE);
		if ((int)cap->max_recv_sge > min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg))
			cap->max_recv_sge 	= (u32)min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg);
		return -ERANGE;
	}

	if (has_srq) {
		/* QPs attached to an SRQ should have no RQ */
		if (cap->max_recv_wr)
			return -EINVAL;

		qp->rq.wqe_cnt = qp->rq.max_gs = 0;
	} else {
		/* HW requires >= 1 RQ entry with >= 1 gather entry */
		if (is_user && (!cap->max_recv_wr || !cap->max_recv_sge))
			return -EINVAL;

		qp->rq.wqe_cnt	 = roundup_pow_of_two(max(1U, cap->max_recv_wr));
		qp->rq.max_gs	 = roundup_pow_of_two(max(1U, cap->max_recv_sge));
		qp->rq.wqe_shift = ilog2(qp->rq.max_gs * sizeof (struct mlx4_wqe_data_seg));
	}

	/* leave userspace return values as they were, so as not to break ABI */
	if (is_user) {
		cap->max_recv_wr  = qp->rq.max_post = qp->rq.wqe_cnt;
		cap->max_recv_sge = qp->rq.max_gs;
	} else {
		cap->max_recv_wr  = qp->rq.max_post =
			min(dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE, qp->rq.wqe_cnt);
		cap->max_recv_sge = min(qp->rq.max_gs,
					min(dev->dev->caps.max_sq_sg,
				    	dev->dev->caps.max_rq_sg));
	}
	/* We don't support inline sends for kernel QPs (yet) */

	return 0;
}

static int set_kernel_sq_size(struct mlx4_ib_dev *dev, struct ib_qp_cap *cap,
			      enum ib_qp_type type, struct mlx4_ib_qp *qp)
{
	int overhead = send_wqe_overhead(type, qp->flags);
	int max_inline_data = calculate_max_inline_data(dev->dev->caps.max_sq_desc_sz, overhead);
	int buf_size_for_inline_data = calculate_buf_size_for_inline_data(cap->max_inline_data, overhead);
	int s;

	
	/* Sanity check SQ size before proceeding */
	if ((int)cap->max_send_wr	> dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE  ||
	    (int)cap->max_send_sge > min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg) ||
	    (int)cap->max_inline_data > max_inline_data 
	    ) {
	    if ((int)cap->max_send_wr	> dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE)
		    cap->max_send_wr		= (u32)(dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE);
		if ((int)cap->max_send_sge > min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg))
			cap->max_send_sge		= (u32)min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg);
		if ((int)cap->max_inline_data > max_inline_data)
			cap->max_inline_data	= (u32)max_inline_data;
		return -ERANGE;
	}

	 
	// max_inline_data for RC for Cx: 1008 -4 -48 = 956 B
	// max size for BF (blue flame) in kernel is caps.bf_reg_size/2 = 256 B
	// so max_inline_data for RC for Cx for BF: 256 - 4 - 48 = 204 B

	/*
	 * For MLX transport we need 2 extra S/G entries:
	 * one for the header and one for the checksum at the end
	 */
	if ((type == IB_QPT_SMI || type == IB_QPT_GSI) &&
	    (int)cap->max_send_sge + 2 > dev->dev->caps.max_sq_sg)
		return -EINVAL;

	s = max(cap->max_send_sge * sizeof (struct mlx4_wqe_data_seg), 
		(u32)buf_size_for_inline_data) + overhead;
	if (s > dev->dev->caps.max_sq_desc_sz)
		return -EINVAL;

	qp->sq.wqe_shift = ilog2(roundup_pow_of_two(s));
	qp->sq.wqe_shift = max(MLX4_IB_SQ_MIN_WQE_SHIFT, qp->sq.wqe_shift);
	qp->sq.max_gs    = ((1 << qp->sq.wqe_shift) - overhead) /  
		sizeof (struct mlx4_wqe_data_seg);

	/*
	 * We need to leave 2 KB + 1 WQE of headroom in the SQ to
	 * allow HW to prefetch.
	 */
	qp->sq_spare_wqes = MLX4_IB_SQ_HEADROOM(qp->sq.wqe_shift);
	qp->sq.wqe_cnt = roundup_pow_of_two(cap->max_send_wr + qp->sq_spare_wqes);

	qp->buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
		(qp->sq.wqe_cnt << qp->sq.wqe_shift);
	if (qp->rq.wqe_shift > qp->sq.wqe_shift) {
		qp->rq.offset = 0;
		qp->sq.offset = qp->rq.wqe_cnt << qp->rq.wqe_shift;
	} else {
		qp->rq.offset = qp->sq.wqe_cnt << qp->sq.wqe_shift;
		qp->sq.offset = 0;
	}

	cap->max_send_wr = qp->sq.max_post =
		min(qp->sq.wqe_cnt - qp->sq_spare_wqes,
			dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE);
	cap->max_send_sge = min(qp->sq.max_gs,
				min(dev->dev->caps.max_sq_sg,
					dev->dev->caps.max_rq_sg));
	qp->max_inline_data = cap->max_inline_data;

	return 0;
}

static int set_user_sq_size(struct mlx4_ib_dev *dev,
			    struct mlx4_ib_qp *qp,
			    struct ibv_create_qp *ucmd)
{
	/* Sanity check SQ size before proceeding */
	if ((1 << ucmd->log_sq_bb_count) > dev->dev->caps.max_wqes	 ||
	    ucmd->log_sq_stride >
		ilog2(roundup_pow_of_two(dev->dev->caps.max_sq_desc_sz)) ||
	    ucmd->log_sq_stride < MLX4_IB_MIN_SQ_STRIDE)
		return -EINVAL;

	qp->sq.wqe_cnt   = 1 << ucmd->log_sq_bb_count;
	qp->sq.wqe_shift = ucmd->log_sq_stride;

	qp->buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
		(qp->sq.wqe_cnt << qp->sq.wqe_shift);

	return 0;
}

static int create_qp_common(struct mlx4_ib_dev *dev, struct ib_pd *pd,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata, int sqpn, struct mlx4_ib_qp *qp)
{
	int err;
	int qpn;
	BOOLEAN range_allocated = FALSE;
	enum ib_qp_type qp_type = init_attr->qp_type;

	/* When tunneling special qps, we use a plain UD qp */
	if (mlx4_is_mfunc(dev->dev) && !dev->dev->caps.sqp_demux &&
		(qp_type == IB_QPT_SMI || qp_type == IB_QPT_GSI))
		qp_type = IB_QPT_UD;

	mutex_init(&qp->mutex);
	spin_lock_init(&qp->sq.lock);
	spin_lock_init(&qp->rq.lock);

	qp->state	 = XIB_QPS_RESET;
	qp->atomic_rd_en = 0;
	qp->resp_depth   = 0;

	qp->rq.head	    = 0;
	qp->rq.tail	    = 0;
	qp->sq.head	    = 0;
	qp->sq.tail	    = 0;
	qp->sq_next_wqe = 0;

	qp->sq_post_sync_by_client = ((init_attr->create_flags & IB_QP_CREATE_SQ_ACCESS_CLIENT_SYNC) != 0);
	qp->rq_post_sync_by_client = ((init_attr->create_flags & IB_QP_CREATE_RQ_ACCESS_CLIENT_SYNC) != 0);	
	qp->no_wq_overflow_by_client = ((init_attr->create_flags & IB_QP_CREATE_NO_WQ_OVERFLOW_BY_CLIENT) != 0);

	err = set_rq_size(dev, &init_attr->cap, !!pd->p_uctx, !!init_attr->srq, qp);
	if (err) {
		// there is some problem in parameters. set_rq_size() returns allowable parameters
		// now call set_kernel_sq_size to check and correct SQ parameters
		if (!pd->p_uctx) {
			if (init_attr->create_flags & IB_QP_CREATE_IPOIB_UD_LSO)
				qp->flags |= MLX4_IB_QP_LSO;
			set_kernel_sq_size(dev, &init_attr->cap, qp_type, qp);
		}
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: set_rq_size() failed, invalid params, error:%d.\n",
				pd->device->dma_device->pdev->name, err));	
		
		goto err;
	}

	if (pd->p_uctx) {
		struct ibv_create_qp ucmd;

        NT_ASSERT( udata != NULL);

		if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: ib_copy_from_udata() failed, trying to copy more mem from user than availiable.\n", 
					pd->device->dma_device->pdev->name));	

			err = -EFAULT;
			goto err;
		}

		qp->sq_no_prefetch = ucmd.sq_no_prefetch;

		err = set_user_sq_size(dev, qp, &ucmd);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: set_user_sq_size() failed, invalid params.\n",
					pd->device->dma_device->pdev->name));	

			goto err;
		}
		
        if( NdValidateMemoryMapping( &ucmd.mappings[mlx4_ib_create_qp_buf],
                                     NdModifyAccess, qp->buf_size ) != STATUS_SUCCESS )
        {
            err = -EFAULT;
            goto err;
        }

        if( NdValidateCoallescedMapping( &ucmd.mappings[mlx4_ib_create_qp_db],
                                         NdWriteAccess, sizeof(UINT32) ) != STATUS_SUCCESS )
        {
            err = -EFAULT;
            goto err;
        }

		qp->umem = ib_umem_get(pd->p_uctx, ucmd.mappings[mlx4_ib_create_qp_buf].MapMemory.Address,
				       ucmd.mappings[mlx4_ib_create_qp_buf].MapMemory.CbLength, IB_ACCESS_NO_SECURE);
		if (IS_ERR(qp->umem)) {
			err = PTR_ERR(qp->umem);
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: ib_umem_get() failed with error:%d.\n", 
					pd->device->dma_device->pdev->name, PTR_ERR(qp->umem)));	

			goto err;
		}

		err = mlx4_mtt_init(dev->dev, ib_umem_page_count(qp->umem),
				    ilog2(qp->umem->page_size), &qp->mtt);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_mtt_init() failed - mem alloc related.\n", 
					pd->device->dma_device->pdev->name));

			goto err_buf;
		}
		
		err = mlx4_ib_umem_write_mtt(dev, &qp->mtt, qp->umem);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_ib_umem_write_mtt() failed with error:%d.\n",
					pd->device->dma_device->pdev->name, err));	
		
			goto err_mtt;
		}

		if (!init_attr->srq) {
			err = mlx4_ib_db_map_user(to_mucontext(pd->p_uctx),
						  ucmd.mappings[mlx4_ib_create_qp_db].MapMemory.Address, &qp->db);
			if (err){
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
					( "%s: mlx4_ib_db_map_user() failed with error:%d.\n", 
						pd->device->dma_device->pdev->name, err));	
				
				goto err_mtt;
			}
		}
	} else {
		qp->sq_no_prefetch = 0;
		
		if (init_attr->create_flags & IB_QP_CREATE_IPOIB_UD_LSO)
			qp->flags |= MLX4_IB_QP_LSO;
		
		err = set_kernel_sq_size(dev, &init_attr->cap, qp_type, qp);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: set_kernel_sq_size() failed, invalid params, error:%d.\n", 
					pd->device->dma_device->pdev->name, err));	

			goto err;
		}
		
		if (!init_attr->srq) {
			err = mlx4_ib_db_alloc(dev, &qp->db, 0);
			if (err){
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
					( "%s: mlx4_ib_db_alloc() failed to alloc mem for qp->db.\n", 
						pd->device->dma_device->pdev->name));

				goto err;
			}
			
			*qp->db.db = 0;
		}

		if (qp->max_inline_data) {
			err = mlx4_bf_alloc(dev->dev, &qp->bf);
			if (err) {
				// it can fail, because max_bf_regs < max_qps !
				qp->bf.uar = &dev->priv_uar;
				// BF is optional so we suppress the error
				MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV,
					( "%s: mlx4_bf_alloc() failed to alloc mem for qp->bf. This is optional, error suppressed.\n",
						pd->device->dma_device->pdev->name));

				err = 0;
			}
		} else
			qp->bf.uar = &dev->priv_uar;

		if (mlx4_buf_alloc(dev->dev, qp->buf_size, MAX_DIRECT_ALLOC_SIZE, &qp->buf)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_buf_alloc() failed to alloc mem for qp->buf.\n", 
					pd->device->dma_device->pdev->name));
			
			err = -ENOMEM;
			goto err_db;
		}

		err = mlx4_mtt_init(dev->dev, qp->buf.npages, qp->buf.page_shift,
				    &qp->mtt);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_mtt_init() failed - mem alloc related.\n",
					pd->device->dma_device->pdev->name));
			
			goto err_buf;
		}
		err = mlx4_buf_write_mtt(dev->dev, &qp->mtt, &qp->buf);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_ib_umem_write_mtt() failed with error:%d.\n", 
				pd->device->dma_device->pdev->name, err));	
			
			goto err_mtt;
		}
		if (qp->sq.wqe_cnt) {
			qp->sq.wrid  = (u64*)kmalloc(qp->sq.wqe_cnt * sizeof (u64), GFP_KERNEL);
			if (!qp->sq.wrid) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
					( "%s: mem alloc failed for qp->sq.wrid.\n",
						pd->device->dma_device->pdev->name));
				
				err = -ENOMEM;
				goto err_wrid;
			}
		}			

		if (qp->rq.wqe_cnt) {
			qp->rq.wrid  = (u64*)kmalloc(qp->rq.wqe_cnt * sizeof (u64), GFP_KERNEL);
			if (!qp->rq.wrid) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
					( "%s: mem alloc failed for qp->rq.wrid.\n",
						pd->device->dma_device->pdev->name));
				
				err = -ENOMEM;
				goto err_wrid;
			}
		}
	}

	if (!sqpn) {
		err = mlx4_qp_reserve_range(dev->dev, 1, 1, &sqpn);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_qp_reserve_range() failed for sqpn with error:%d.\n",
					pd->device->dma_device->pdev->name, err));
			
			goto err_wrid;
		}
		range_allocated = TRUE;
	}

	if (sqpn) {
		qpn = sqpn;
	} else if (init_attr->create_flags & MLX4_IB_QP_TUNNEL) {
		qpn = dev->dev->caps.tunnel_qpn + init_attr->port_num - 1;
	} else {
		err = mlx4_qp_reserve_range(dev->dev, 1, 1, &qpn);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_qp_reserve_range() failed for qpn with error:%d.\n",
					pd->device->dma_device->pdev->name, err));
			
			goto err_range;
		}
		range_allocated = TRUE;
	}

	err = mlx4_qp_alloc(dev->dev, qpn, &qp->mqp);
	if (err){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: mem alloc failed for qp->mqp.\n",
				pd->device->dma_device->pdev->name));
		
		goto err_range;
	}

	/*
	 * Hardware wants QPN written in big-endian order (after
	 * shifting) for send doorbell.  Precompute this value to save
	 * a little bit when posting sends.
	 */
	qp->doorbell_qpn = swab32(qp->mqp.qpn << 8);

	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		qp->sq_signal_bits = cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE);
	else
		qp->sq_signal_bits = 0;

	qp->mqp.event = mlx4_ib_qp_event;

	return 0;

err_range:
	if (!sqpn)
		mlx4_qp_release_range(dev->dev, sqpn, 1);

err_wrid:
	if (pd->p_uctx) {
		if (!init_attr->srq)
			mlx4_ib_db_unmap_user(to_mucontext(pd->p_uctx),
					      &qp->db);
	} else {
		if (qp->sq.wrid)
			kfree(qp->sq.wrid);
		if (qp->rq.wrid)
			kfree(qp->rq.wrid);
	}

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &qp->mtt);

err_buf:
	if (pd->p_uctx)
		ib_umem_release(qp->umem);
	else
		mlx4_buf_free(dev->dev, qp->buf_size, &qp->buf);

err_db:
	if (!pd->p_uctx && !init_attr->srq)
		mlx4_ib_db_free(dev, &qp->db);

	if (qp->max_inline_data)
		mlx4_bf_free(dev->dev, &qp->bf);

err:
	return err;
}

static enum mlx4_qp_state to_mlx4_state(enum ib_qp_state state)
{
	switch (state) {
	case XIB_QPS_RESET:	return MLX4_QP_STATE_RST;
	case XIB_QPS_INIT:	return MLX4_QP_STATE_INIT;
	case XIB_QPS_RTR:	return MLX4_QP_STATE_RTR;
	case XIB_QPS_RTS:	return MLX4_QP_STATE_RTS;
	case XIB_QPS_SQD:	return MLX4_QP_STATE_SQD;
	case XIB_QPS_SQE:	return MLX4_QP_STATE_SQER;
	case XIB_QPS_ERR:	return MLX4_QP_STATE_ERR;
	default:		return (mlx4_qp_state)-1;
	}
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
static void mlx4_ib_lock_cqs(struct mlx4_ib_cq *send_cq, struct mlx4_ib_cq *recv_cq)
{
	if (send_cq == recv_cq)
		spin_lock_irq(&send_cq->lock);
	else if (send_cq->mcq.cqn < recv_cq->mcq.cqn) {
		spin_lock_irq(&send_cq->lock);
		spin_lock_nested(&recv_cq->lock, SINGLE_DEPTH_NESTING);
	} else {
		spin_lock_irq(&recv_cq->lock);
		spin_lock_nested(&send_cq->lock, SINGLE_DEPTH_NESTING);
	}
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
static void mlx4_ib_unlock_cqs(struct mlx4_ib_cq *send_cq, struct mlx4_ib_cq *recv_cq)
{
	if (send_cq == recv_cq)
		spin_unlock_irq(&send_cq->lock);
	else if (send_cq->mcq.cqn < recv_cq->mcq.cqn) {
		spin_unlock(&recv_cq->lock);
		spin_unlock_irq(&send_cq->lock);
	} else {
		spin_unlock(&send_cq->lock);
		spin_unlock_irq(&recv_cq->lock);
	}
}

static void destroy_qp_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp,
			      int is_user)
{
	struct mlx4_ib_cq *send_cq, *recv_cq;
	int zombi = 0;

	if (qp->state != XIB_QPS_RESET)
		if (mlx4_qp_modify(dev->dev, NULL, to_mlx4_state((ib_qp_state)qp->state),
				MLX4_QP_STATE_RST, NULL, (mlx4_qp_optpar)0, 0, &qp->mqp, FALSE /* RST - doesn't matter is RDMA QP or not*/)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "mlx4_ib: modify QP %06x to RESET failed.\n",
				qp->mqp.qpn));
			zombi = 1;
		}

	send_cq = to_mcq(qp->ibqp.send_cq);
	recv_cq = to_mcq(qp->ibqp.recv_cq);

	mlx4_ib_lock_cqs(send_cq, recv_cq);

	if (!is_user) {
		__mlx4_ib_cq_clean(recv_cq, qp->mqp.qpn,
				 qp->ibqp.srq ? to_msrq(qp->ibqp.srq): NULL);
		if (send_cq != recv_cq)
			__mlx4_ib_cq_clean(send_cq, qp->mqp.qpn, NULL);
	}

	mlx4_qp_remove(dev->dev, &qp->mqp);

	mlx4_ib_unlock_cqs(send_cq, recv_cq);

	mlx4_qp_free(dev->dev, &qp->mqp);

	if (!is_sqp(dev, qp) && !zombi && !is_tunnel_qp(dev, qp))		
		mlx4_qp_release_range(dev->dev, qp->mqp.qpn, 1);

	mlx4_mtt_cleanup(dev->dev, &qp->mtt);

	if (is_user) {
		if (!qp->ibqp.srq)
			mlx4_ib_db_unmap_user(to_mucontext(qp->ibqp.p_uctx),
					      &qp->db);
		ib_umem_release(qp->umem);
	} else {
		kfree(qp->sq.wrid);
		kfree(qp->rq.wrid);
		mlx4_buf_free(dev->dev, qp->buf_size, &qp->buf);
		if (qp->max_inline_data)
			mlx4_bf_free(dev->dev, &qp->bf);
		if (!qp->ibqp.srq)
			mlx4_ib_db_free(dev, &qp->db);
	}
}

struct ib_qp *mlx4_ib_create_qp(struct ib_pd *pd,
				struct ib_qp_init_attr *init_attr,
				struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_sqp *sqp;
	struct mlx4_ib_qp *qp;
	int err;

	/* TODO: suggest to remove :We only support LSO, vendor flag1, and only for kernel UD QPs. */
	/* if (init_attr->create_flags & ~(IB_QP_CREATE_IPOIB_UD_LSO |
					IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK |
					IB_QP_CREATE_VENDOR_FLAG1))
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: QP creation failed.\n", 
				pd->device->dma_device->pdev->name));		

		return ERR_PTR(-EINVAL);
	if (init_attr->create_flags & IB_QP_CREATE_IPOIB_UD_LSO &&
		(pd->uobject || init_attr->qp_type != IB_QPT_UD))
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: QP creation failed. line: %d\n", 
				pd->device->dma_device->pdev->name, __LINE__));	

		return ERR_PTR(-EINVAL);*/

	if (mlx4_is_barred(pd->device->dma_device)){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: MLX4_FLAG_RESET_DRIVER (mlx4_is_barred) flag is set.\n", 
				pd->device->dma_device->pdev->name));

		return (ib_qp *)ERR_PTR(-EFAULT);
	}
	
	switch (init_attr->qp_type) {
	case IB_QPT_RC:
	case IB_QPT_UC:
	case IB_QPT_UD:
	{
		qp = (mlx4_ib_qp *)kzalloc(sizeof *qp, GFP_KERNEL);
		if (!qp){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mem alloc failed for qp.\n", 
					pd->device->dma_device->pdev->name));	

			return (ib_qp *)ERR_PTR(-ENOMEM);
		}
		
		err = create_qp_common(dev, pd, init_attr, udata, 0, qp);
		if (err) {
			kfree(qp);
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: create_qp_common() for qp failed with error:%d\n", 
					pd->device->dma_device->pdev->name, err));	

			return (ib_qp *)ERR_PTR(err);
		}

		qp->ibqp.qp_num = qp->mqp.qpn;

		break;
	}
	case IB_QPT_SMI:
	case IB_QPT_GSI:
	{
		/* Userspace is not allowed to create special QPs: */
		if (pd->p_uctx){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: failed, tried to create a special QP with user context - not allowed.\n", 
					pd->device->dma_device->pdev->name));	

			return (ib_qp *)ERR_PTR(-EINVAL);
		}
		
		sqp = (mlx4_ib_sqp *)kzalloc(sizeof *sqp, GFP_KERNEL);
		if (!sqp){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mem alloc failed for sqp.\n", 
					pd->device->dma_device->pdev->name));

			return (ib_qp *)ERR_PTR(-ENOMEM);
		}
		
		qp = &sqp->qp;

		err = create_qp_common(dev, pd, init_attr, udata,
				       dev->dev->caps.sqp_start +
				       (init_attr->qp_type == IB_QPT_SMI ? 0 : 2) +
				       init_attr->port_num - 1,
				       qp);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: create_qp_common() for sqp failed with error:%d\n", 
					pd->device->dma_device->pdev->name, err));	

			kfree(sqp);
			return (ib_qp *)ERR_PTR(err);
		}

		qp->port	= init_attr->port_num;
		qp->ibqp.qp_num = init_attr->qp_type == IB_QPT_SMI ? 0 : 1;

		break;
	}
	default:
		/* Don't support raw QPs */
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: Tried to create a raw QP - not supported.\n", 
				pd->device->dma_device->pdev->name));	

		return (ib_qp *)ERR_PTR(-EINVAL);
	}

	return &qp->ibqp;
}

int mlx4_ib_destroy_qp(struct ib_qp *qp)
{
	struct mlx4_ib_dev *dev = to_mdev(qp->device);
	struct mlx4_ib_qp *mqp = to_mqp(qp);

	if (!mlx4_is_barred(dev->dev) && is_qp0(dev, mqp) && !mlx4_is_roce_port(dev->dev,mqp->port))
		mlx4_CLOSE_PORT(dev->dev, mqp->port);

	destroy_qp_common(dev, mqp, !!qp->pd->p_uctx);

	if (is_sqp(dev, mqp))
		kfree(to_msqp(mqp));
	else
		kfree(mqp);

	return 0;
}

static int to_mlx4_st(struct mlx4_ib_dev *dev, enum ib_qp_type type)
{
	switch (type) {
	case IB_QPT_RC:		return MLX4_QP_ST_RC;
	case IB_QPT_UC:		return MLX4_QP_ST_UC;
	case IB_QPT_UD:		return MLX4_QP_ST_UD;
	case IB_QPT_SMI:
	case IB_QPT_GSI:	
		return mlx4_is_mfunc(dev->dev) &&
			!dev->dev->caps.sqp_demux ? MLX4_QP_ST_UD : MLX4_QP_ST_MLX;
	default:		return -1;
	}
}

static __be32 convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ? cpu_to_be32(MLX4_WQE_FMR_PERM_ATOMIC)       : 0) |
	       (acc & IB_ACCESS_REMOTE_WRITE  ? cpu_to_be32(MLX4_WQE_FMR_PERM_REMOTE_WRITE) : 0) |
	       (acc & IB_ACCESS_REMOTE_READ   ? cpu_to_be32(MLX4_WQE_FMR_PERM_REMOTE_READ)  : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE   ? cpu_to_be32(MLX4_WQE_FMR_PERM_LOCAL_WRITE)  : 0) |
		cpu_to_be32(MLX4_WQE_FMR_PERM_LOCAL_READ);
}


static __be32 to_mlx4_access_flags(struct mlx4_ib_qp *qp, const struct ib_qp_attr *attr,
				   int attr_mask)
{
	u8 dest_rd_atomic;
	u32 access_flags;
	u32 hw_access_flags = 0;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		dest_rd_atomic = attr->max_dest_rd_atomic;
	else
		dest_rd_atomic = qp->resp_depth;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		access_flags = attr->qp_access_flags;
	else
		access_flags = qp->atomic_rd_en;

	if (!dest_rd_atomic)
		access_flags &= IB_ACCESS_REMOTE_WRITE;

	if (access_flags & IB_ACCESS_REMOTE_READ)
		hw_access_flags |= MLX4_QP_BIT_RRE;
	if (access_flags & IB_ACCESS_REMOTE_ATOMIC)
		hw_access_flags |= MLX4_QP_BIT_RAE;
	if (access_flags & IB_ACCESS_REMOTE_WRITE)
		hw_access_flags |= MLX4_QP_BIT_RWE;

	return cpu_to_be32(hw_access_flags);
}

static void store_sqp_attrs(struct mlx4_ib_sqp *sqp, const struct ib_qp_attr *attr,
			    int attr_mask)
{
	if (attr_mask & IB_QP_PKEY_INDEX)
		sqp->pkey_index = attr->pkey_index;
	if (attr_mask & IB_QP_QKEY)
		sqp->qkey = attr->qkey;
	if (attr_mask & IB_QP_SQ_PSN)
		sqp->send_psn = attr->sq_psn;
}

static void mlx4_set_sched(struct mlx4_qp_path *path, u8 port)
{
	path->sched_queue = (path->sched_queue & 0xbf) | ((port - 1) << 6);
}

static int mlx4_set_path(struct mlx4_ib_dev *dev, ib_qp_type qp_type, const struct ib_ah_attr *ah,
			 struct mlx4_qp_path *path, u8 port, u8 generate_prio_tag)
{
	int err;
	int is_eth = rdma_port_get_transport(&dev->ib_dev, port) ==
		RDMA_TRANSPORT_RDMAOE ? 1 : 0;
	u8 mac[6];
	int is_mcast;

	path->grh_mylmc     = ah->src_path_bits & 0x7f;
	path->rlid	    = cpu_to_be16(ah->dlid);
	if (ah->static_rate) {
		path->static_rate = (u8)(ah->static_rate + MLX4_STAT_RATE_OFFSET);
		while (path->static_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << path->static_rate & dev->dev->caps.stat_rate_support))
			--path->static_rate;
	} else
		path->static_rate = 0;
	path->if_counter_index = 0xff;

	if (ah->ah_flags & IB_AH_GRH) {
		if (ah->grh.sgid_index >= dev->dev->caps.gid_table_len[port]) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "sgid_index (%u) too large. max is %d\n",
			       ah->grh.sgid_index, dev->dev->caps.gid_table_len[port] - 1));
			return -1;
		}

		path->grh_mylmc |= 1 << 7;
		if (mlx4_is_mfunc(dev->dev)) {
			if (ah->grh.sgid_index) {
				/* XXX currently deny access to non-gid0 in mfunc mode.
				 * TODO: add support for multiple gids per function */
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: cannot modify qp with gid != 0 "
							   "in mfunc mode\n", dev->dev->pdev->name ));
				return -1;
			}
			path->mgid_index = (u8)(slave_gid_index(dev) & 0x7f);
		} else
			path->mgid_index = ah->grh.sgid_index;
		path->hop_limit  = ah->grh.hop_limit;
		path->tclass_flowlabel =
			cpu_to_be32((ah->grh.traffic_class << 20) |
				    (ah->grh.flow_label));
		memcpy(path->rgid, ah->grh.dgid.raw, 16);
	}

	path->sched_queue = (u8)(MLX4_IB_DEFAULT_SCHED_QUEUE |
		((port - 1) << 6) | ((ah->sl & 0xf) << 2));

	if (is_eth) {
		if (!(ah->ah_flags & IB_AH_GRH))
			return -1;

		err = mlx4_ib_resolve_grh(dev, ah, mac, &is_mcast);
		if (err)
			return err;

		memcpy(path->dmac, mac, 6);
		path->ackto = MLX4_IB_LINK_TYPE_ETH;
		/* use index 0 into MAC table for RDMAoE */
		path->grh_mylmc &= 0x80;

		if(qp_type == IB_QPT_RC && mlx4_is_qos_supported(dev->dev, port) && generate_prio_tag)
		{
			struct mlx4_dev *mlx4_dev = to_mdev(&dev->ib_dev)->dev;
			int vidx;
			
			path->sched_queue = (u8)(MLX4_IB_DEFAULT_SCHED_QUEUE |
				((port - 1) << 6) | ((ah->sl & 0x7) << 3) | ((ah->sl & 0x8) >> 1));
			
			err = mlx4_find_vlan_index(mlx4_dev, port, 0, &vidx);
			if(err)
			{
				return err; // incldes error code
			}
			
			path->vlan_index = (u8) vidx;
			path->fl = 1 << 6; // add vlan tag
		}
	}

	return 0;
}

static int __mlx4_ib_modify_qp(struct ib_qp *ibqp,
			       const struct ib_qp_attr *attr, int attr_mask,
			       enum ib_qp_state cur_state, enum ib_qp_state new_state)
{
	struct mlx4_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	struct mlx4_qp_context *context;
	enum mlx4_qp_optpar optpar = (mlx4_qp_optpar)0;
	int sqd_event;
	int err = -EINVAL;

	context = (mlx4_qp_context *)kzalloc(sizeof *context, GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	context->flags = cpu_to_be32((to_mlx4_state(new_state) << 28) |
				     (to_mlx4_st(dev, ibqp->qp_type) << 16));
	context->flags     |= cpu_to_be32(1 << 8); /* DE? */

	if (!(attr_mask & IB_QP_PATH_MIG_STATE))
		context->flags |= cpu_to_be32(MLX4_QP_PM_MIGRATED << 11);
	else {
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_PM_STATE);
		switch (attr->path_mig_state) {
		case IB_MIG_MIGRATED:
			context->flags |= cpu_to_be32(MLX4_QP_PM_MIGRATED << 11);
			break;
		case IB_MIG_REARM:
			context->flags |= cpu_to_be32(MLX4_QP_PM_REARM << 11);
			break;
		case IB_MIG_ARMED:
			context->flags |= cpu_to_be32(MLX4_QP_PM_ARMED << 11);
			break;
		}
	}

	if (ibqp->qp_type == IB_QPT_GSI || ibqp->qp_type == IB_QPT_SMI )
		context->mtu_msgmax = (IB_MTU_4096 << 5) | 12;
	else if (ibqp->qp_type == IB_QPT_UD) {
		if (qp->flags & MLX4_IB_QP_LSO)
		{		
			if(mlx4_is_roce_port(dev->dev, qp->port))
			{// ROCE port - adjust mtu
				context->mtu_msgmax = 
					(u8)((dev->dev->dev_params.roce_mtu[qp->port] << 5) | 
					ilog2(dev->dev->caps.max_gso_sz));
			}
			else
			{
				context->mtu_msgmax = (u8)((IB_MTU_4096 << 5) |
						ilog2(dev->dev->caps.max_gso_sz));
			}
		}
		else
		{
			if(mlx4_is_roce_port(dev->dev, qp->port))
			{// ROCE port - adjust mtu
				context->mtu_msgmax = 
					(u8)((dev->dev->dev_params.roce_mtu[qp->port] << 5) | 12);
			}
			else
			{
				context->mtu_msgmax = (IB_MTU_4096 << 5) | 12;
			}
		}
	} else if (attr_mask & IB_QP_PATH_MTU) {
		enum ib_mtu mtu;
		
		if (attr->path_mtu < IB_MTU_256 || attr->path_mtu > IB_MTU_4096) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "path MTU (%u) is invalid\n",
				attr->path_mtu));
			goto out;
		}

		mtu = attr->path_mtu;
		if(mlx4_is_roce_port(dev->dev, qp->port))
		{// ROCE port - adjust mtu
			mtu = min(mtu, dev->dev->dev_params.roce_mtu[qp->port]);
		}
		context->mtu_msgmax = (u8)((mtu << 5) |
			ilog2(dev->dev->caps.max_msg_sz));
	}

	if (qp->rq.wqe_cnt)
		context->rq_size_stride = (u8)(ilog2(qp->rq.wqe_cnt) << 3);
	context->rq_size_stride |= qp->rq.wqe_shift - 4;

	if (qp->sq.wqe_cnt)
		context->sq_size_stride = (u8)(ilog2(qp->sq.wqe_cnt) << 3);
	context->sq_size_stride |= qp->sq.wqe_shift - 4;

	if (cur_state == XIB_QPS_RESET && new_state == XIB_QPS_INIT)
		context->sq_size_stride |= !!qp->sq_no_prefetch << 7;

	if (qp->ibqp.p_uctx)
		context->usr_page = cpu_to_be32(to_mucontext(ibqp->p_uctx)->uar.index);
	else
		context->usr_page = cpu_to_be32(qp->bf.uar->index);

	if (attr_mask & IB_QP_DEST_QPN)
		context->remote_qpn = cpu_to_be32(attr->dest_qp_num);

	if (attr_mask & IB_QP_PORT) {
		if (cur_state == XIB_QPS_SQD && new_state == XIB_QPS_SQD &&
		    !(attr_mask & IB_QP_AV)) {
			mlx4_set_sched(&context->pri_path, attr->port_num);
			optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_SCHED_QUEUE);
		}
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		context->pri_path.pkey_index = (u8)attr->pkey_index;
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_PKEY_INDEX);
	}

	if (attr_mask & IB_QP_AV) {
		if (mlx4_set_path(dev, qp->ibqp.qp_type, &attr->ah_attr, &context->pri_path,
				  attr_mask & IB_QP_PORT ? attr->port_num : qp->port, ((attr_mask & IB_QP_GENERATE_PRIO_TAG) != 0)))
			goto out;

		optpar = (mlx4_qp_optpar)((int)optpar | (MLX4_QP_OPTPAR_PRIMARY_ADDR_PATH |
			   MLX4_QP_OPTPAR_SCHED_QUEUE));
	}

	if (attr_mask & IB_QP_TIMEOUT) {
		context->pri_path.ackto = attr->timeout << 3;
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_ACK_TIMEOUT);
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		if (attr->alt_port_num == 0 ||
		    attr->alt_port_num > dev->dev->caps.num_ports)
			goto out;

		if (attr->alt_pkey_index >=
		    dev->dev->caps.pkey_table_len[attr->alt_port_num])
			goto out;

		if (mlx4_set_path(dev, qp->ibqp.qp_type, &attr->alt_ah_attr, &context->alt_path,
				  attr->alt_port_num, ((attr_mask & IB_QP_GENERATE_PRIO_TAG) != 0)))
			goto out;

		context->alt_path.pkey_index = (u8)attr->alt_pkey_index;
		context->alt_path.ackto = attr->alt_timeout << 3;
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_ALT_ADDR_PATH);
	}

	context->pd	    = cpu_to_be32(to_mpd(ibqp->pd)->pdn);
	context->params1    = cpu_to_be32(MLX4_IB_ACK_REQ_FREQ << 28);

	/* Set "fast registration enabled" for all kernel QPs */
	if (!qp->ibqp.pd->p_uctx)
		context->params1 |= cpu_to_be32(1 << 11);

	if (attr_mask & IB_QP_RNR_RETRY) {
		context->params1 |= cpu_to_be32(attr->rnr_retry << 13);
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_RNR_RETRY);
	}

	if (attr_mask & IB_QP_RETRY_CNT) {
		context->params1 |= cpu_to_be32(attr->retry_cnt << 16);
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_RETRY_COUNT);
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		if (attr->max_rd_atomic)
			context->params1 |=
				cpu_to_be32(fls(attr->max_rd_atomic - 1) << 21);
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_SRA_MAX);
	}

	if (attr_mask & IB_QP_SQ_PSN)
		context->next_send_psn = cpu_to_be32(attr->sq_psn);

	context->cqn_send = cpu_to_be32(to_mcq(ibqp->send_cq)->mcq.cqn);

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		if (attr->max_dest_rd_atomic)
			context->params2 |=
				cpu_to_be32(fls(attr->max_dest_rd_atomic - 1) << 21);
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_RRA_MAX);
	}

	if (attr_mask & (IB_QP_ACCESS_FLAGS | IB_QP_MAX_DEST_RD_ATOMIC)) {
		context->params2 |= to_mlx4_access_flags(qp, attr, attr_mask);
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_RWE | MLX4_QP_OPTPAR_RRE | MLX4_QP_OPTPAR_RAE);
	}

	if (ibqp->srq)
		context->params2 |= cpu_to_be32(MLX4_QP_BIT_RIC);

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		context->rnr_nextrecvpsn |= cpu_to_be32(attr->min_rnr_timer << 24);
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_RNR_TIMEOUT);
	}
	if (attr_mask & IB_QP_RQ_PSN)
		context->rnr_nextrecvpsn |= cpu_to_be32(attr->rq_psn);

	context->cqn_recv = cpu_to_be32(to_mcq(ibqp->recv_cq)->mcq.cqn);

	if (attr_mask & IB_QP_QKEY) {
		context->qkey = cpu_to_be32(attr->qkey);
		optpar = (mlx4_qp_optpar)((int)optpar | MLX4_QP_OPTPAR_Q_KEY);
	}

	if (ibqp->srq)
		context->srqn = cpu_to_be32(1 << 24 | to_msrq(ibqp->srq)->msrq.srqn);

	if (!ibqp->srq && cur_state == XIB_QPS_RESET && new_state == XIB_QPS_INIT)
		context->db_rec_addr = cpu_to_be64(qp->db.dma.da);

	if (cur_state == XIB_QPS_INIT &&
	    new_state == XIB_QPS_RTR  &&
	    (ibqp->qp_type == IB_QPT_GSI || ibqp->qp_type == IB_QPT_SMI ||
	     ibqp->qp_type == IB_QPT_UD)) {
		context->pri_path.sched_queue = (qp->port - 1) << 6;
		if (is_qp0(dev, qp))
			context->pri_path.sched_queue |= MLX4_IB_DEFAULT_QP0_SCHED_QUEUE;
		else
			context->pri_path.sched_queue |= MLX4_IB_DEFAULT_SCHED_QUEUE;
	}

	if (cur_state == XIB_QPS_RTS && new_state == XIB_QPS_SQD	&&
	    attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY && attr->en_sqd_async_notify)
		sqd_event = 1;
	else
		sqd_event = 0;

	/*
	 * Before passing a kernel QP to the HW, make sure that the
	 * ownership bits of the send queue are set and the SQ
	 * headroom is stamped so that the hardware doesn't start
	 * processing stale work requests.
	 */
	if (!ibqp->p_uctx && cur_state == XIB_QPS_RESET && new_state == XIB_QPS_INIT) {
		struct mlx4_wqe_ctrl_seg *ctrl;
		int i;

		for (i = 0; i < qp->sq.wqe_cnt; ++i) {
			ctrl = (mlx4_wqe_ctrl_seg *)get_send_wqe(qp, i);
			ctrl->owner_opcode = cpu_to_be32(1 << 31);

			stamp_send_wqe(qp, i);
		}
	}

	err = mlx4_qp_modify(dev->dev, &qp->mtt, to_mlx4_state(cur_state),
			     to_mlx4_state(new_state), context, optpar,
			     sqd_event, &qp->mqp, TRUE /* RDMA QP */);
	if (err)
		goto out;

	qp->state = (u8)new_state;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		qp->atomic_rd_en = (u8)attr->qp_access_flags;
	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		qp->resp_depth = attr->max_dest_rd_atomic;
	if (attr_mask & IB_QP_PORT)
		qp->port = attr->port_num;
	if (attr_mask & IB_QP_ALT_PATH)
		qp->alt_port = attr->alt_port_num;

	if (is_sqp(dev, qp))
		store_sqp_attrs(to_msqp(qp), attr, attr_mask);

	/*
	 * If we moved QP0 to RTR, bring the IB link up; if we moved
	 * QP0 to RESET or ERROR, bring the link back down.
	 */
	if (is_qp0(dev, qp) && !mlx4_is_roce_port(dev->dev,qp->port)) {
		if (cur_state != XIB_QPS_RTR && new_state == XIB_QPS_RTR)
			if (mlx4_INIT_PORT(dev->dev, qp->port))
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "INIT_PORT failed for port %d\n",
				       qp->port));

		if (cur_state != XIB_QPS_RESET && cur_state != XIB_QPS_ERR &&
		    (new_state == XIB_QPS_RESET || new_state == XIB_QPS_ERR))
			mlx4_CLOSE_PORT(dev->dev, qp->port);
	}

	/*
	 * If we moved a kernel QP to RESET, clean up all old CQ
	 * entries and reinitialize the QP.
	 */
	if (new_state == XIB_QPS_RESET && !ibqp->p_uctx) {
		mlx4_ib_cq_clean(to_mcq(ibqp->recv_cq), qp->mqp.qpn,
				 ibqp->srq ? to_msrq(ibqp->srq): NULL);
		if (ibqp->send_cq != ibqp->recv_cq)
			mlx4_ib_cq_clean(to_mcq(ibqp->send_cq), qp->mqp.qpn, NULL);

		qp->rq.head = 0;
		qp->rq.tail = 0;
		qp->sq.head = 0;
		qp->sq.tail = 0;
		qp->sq_next_wqe = 0;

		if (!ibqp->srq)
			*qp->db.db  = 0;
	}

out:
	kfree(context);
	return err;
}

static struct ib_qp_attr mlx4_ib_qp_attr;
static int mlx4_ib_qp_attr_mask_table[IB_QPT_UD + 1];

void mlx4_ib_qp_init()
{
	memset( &mlx4_ib_qp_attr, 0, sizeof(mlx4_ib_qp_attr) );
	mlx4_ib_qp_attr.port_num = 1;

	memset( &mlx4_ib_qp_attr_mask_table, 0, sizeof(mlx4_ib_qp_attr_mask_table) );
	mlx4_ib_qp_attr_mask_table[IB_QPT_UD]  = (IB_QP_PKEY_INDEX		|
				IB_QP_PORT			|
				IB_QP_QKEY);
	mlx4_ib_qp_attr_mask_table[IB_QPT_UC]  = (IB_QP_PKEY_INDEX		|
				IB_QP_PORT			|
				IB_QP_ACCESS_FLAGS);
	mlx4_ib_qp_attr_mask_table[IB_QPT_RC]  = (IB_QP_PKEY_INDEX		|
				IB_QP_PORT			|
				IB_QP_ACCESS_FLAGS);
	mlx4_ib_qp_attr_mask_table[IB_QPT_SMI] = (IB_QP_PKEY_INDEX		|
				IB_QP_QKEY);
	mlx4_ib_qp_attr_mask_table[IB_QPT_GSI] = (IB_QP_PKEY_INDEX		|
				IB_QP_QKEY);
}

int mlx4_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	enum ib_qp_state cur_state, new_state;
	int err = -EINVAL;

	UNUSED_PARAM(udata);
	
	if (mlx4_is_barred(dev->dev))
		return -EFAULT;	

	mutex_lock(&qp->mutex);

	cur_state = (ib_qp_state)(attr_mask & IB_QP_CUR_STATE ? attr->cur_qp_state : qp->state);
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type, (ib_qp_attr_mask)attr_mask))
		goto out;

	if ((attr_mask & IB_QP_PORT) &&
	    (attr->port_num == 0 || attr->port_num > dev->dev->caps.num_ports)) {
		goto out;
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		int p = attr_mask & IB_QP_PORT ? attr->port_num : qp->port;
		if (attr->pkey_index >= dev->dev->caps.pkey_table_len[p])
			goto out;
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
	    attr->max_rd_atomic > dev->dev->caps.max_qp_init_rdma) {
		goto out;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
	    attr->max_dest_rd_atomic > dev->dev->caps.max_qp_dest_rdma) {
		goto out;
	}

	if (cur_state == new_state && cur_state == XIB_QPS_RESET) {
		err = 0;
		goto out;
	}

	if (cur_state == XIB_QPS_RESET && new_state == XIB_QPS_ERR) {
		err = __mlx4_ib_modify_qp(ibqp, &mlx4_ib_qp_attr,
					  mlx4_ib_qp_attr_mask_table[ibqp->qp_type],
					  XIB_QPS_RESET, XIB_QPS_INIT);
		if (err)
			goto out;
		cur_state = XIB_QPS_INIT;
	}

	err = __mlx4_ib_modify_qp(ibqp, attr, attr_mask, cur_state, new_state);

out:
	mutex_unlock(&qp->mutex);
	return err;
}

static enum ib_wr_opcode to_wr_opcode(struct _ib_send_wr *wr)
{

	enum ib_wr_opcode opcode = (ib_wr_opcode)-1; //= wr->wr_type;

	switch (wr->wr_type) {
		case WR_SEND: 
			opcode = (wr->send_opt & IB_SEND_OPT_IMMEDIATE) ? IB_WR_SEND_WITH_IMM : IB_WR_SEND;
			break;
		case WR_LSO:
			opcode = IB_WR_LSO;
			break;
		case WR_RDMA_WRITE:	
			opcode = (wr->send_opt & IB_SEND_OPT_IMMEDIATE) ? IB_WR_RDMA_WRITE_WITH_IMM : IB_WR_RDMA_WRITE;
			break;
		case WR_RDMA_READ:
			opcode = IB_WR_RDMA_READ;
			break;
		case WR_COMPARE_SWAP:
			opcode = IB_WR_ATOMIC_CMP_AND_SWP;
			break;
		case WR_FETCH_ADD:
			opcode = IB_WR_ATOMIC_FETCH_AND_ADD;
			break;
		case WR_NOP:
			opcode = IB_WR_NOP;
			break;
		case WR_LOCAL_INV:
			opcode = IB_WR_LOCAL_INV;
			break;
		case WR_FAST_REG_MR:
			opcode = IB_WR_FAST_REG_MR;
			break;
	}
	return opcode;
}

static int build_mlx_header(struct mlx4_ib_sqp *sqp, ib_send_wr_t *wr,
	void *wqe, unsigned *mlx_seg_len)
{
	enum ib_wr_opcode opcode = to_wr_opcode(wr);
	struct ib_device *ib_dev = &to_mdev(sqp->qp.ibqp.device)->ib_dev;
	struct mlx4_wqe_mlx_seg *mlx = (mlx4_wqe_mlx_seg *)wqe;
	struct mlx4_wqe_inline_seg *inl = (mlx4_wqe_inline_seg *)((u8*)wqe + sizeof *mlx);
	struct mlx4_ib_ah *ah = to_mah((struct ib_ah *)wr->dgrm.ud.h_av);
	u16 pkey;
	int send_size;
	int header_size;
	int spc;
	u16 i;
	struct ib_ud_header *ib = NULL;
	struct eth_ud_header *eth = NULL;
	struct ib_unpacked_grh *grh;
	struct ib_unpacked_bth  *bth;
	struct ib_unpacked_deth *deth;
	u8 *tmp;
	u8 mac[6];

	send_size = 0;
	for (i = 0; i < wr->num_ds; ++i)
		send_size += wr->ds_array[i].length;

	if (rdma_port_get_transport(sqp->qp.ibqp.device, sqp->qp.port) == RDMA_TRANSPORT_IB) {

		ib = &sqp->hdr.ib;
		grh = &ib->grh;
		bth = &ib->bth;
		deth = &ib->deth;
		ib_ud_header_init(send_size, mlx4_ib_ah_grh_present(ah), ib);
		ib->lrh.service_level   =
			(u8)(be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 28);
		ib->lrh.destination_lid = ah->av.ib.dlid;
		ib->lrh.source_lid      = cpu_to_be16(ah->av.ib.g_slid & 0x7f);
	} else {
		eth = &sqp->hdr.eth;
		grh = &eth->grh;
		bth = &eth->bth;
		deth = &eth->deth;
		ib_rdmaoe_ud_header_init(send_size, mlx4_ib_ah_grh_present(ah), eth);
	}

	if (mlx4_ib_ah_grh_present(ah)) {
		grh->traffic_class =
			(u8)((be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 20) & 0xff);
		grh->flow_label    =
			ah->av.ib.sl_tclass_flowlabel & cpu_to_be32(0xfffff);
		grh->hop_limit     = ah->av.ib.hop_limit;
		ib_get_cached_gid(ib_dev, (u8)(be32_to_cpu(ah->av.ib.port_pd) >> 24),
				  ah->av.ib.gid_index, &grh->source_gid);
		memcpy(grh->destination_gid.raw,
			   ah->av.ib.dgid, 16);
		if (ah->ex)
			memcpy(&grh->source_gid, ah->ex->sgid, 16);
		else {
			if (mlx4_is_mfunc(to_mdev(ib_dev)->dev)) {
				/* When multi-function is enabled, the ib_core gid
				 * indexes don't necessarily match the hw ones, so
				 * we must use our own cache */
				grh->source_gid.global.subnet_prefix =
					to_mdev(ib_dev)->sriov.demux[sqp->qp.port - 1].
							       subnet_prefix;
				grh->source_gid.global.interface_id =
					to_mdev(ib_dev)->sriov.demux[sqp->qp.port - 1].
							       guid_cache[ah->av.ib.gid_index];
			} 
			else
				ib_get_cached_gid(ib_dev, (u8)(be32_to_cpu(ah->av.ib.port_pd) >> 24),
					ah->av.ib.gid_index, &grh->source_gid);
		}
		memcpy(grh->destination_gid.raw, ah->av.ib.dgid, 16);
	}

	mlx->flags &= cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE);

	if (ib) {
		mlx->flags |= cpu_to_be32((!sqp->qp.ibqp.qp_num ? MLX4_WQE_MLX_VL15 : 0) |
			((ib->lrh.destination_lid == XIB_LID_PERMISSIVE || ah->ex) ? 
			MLX4_WQE_MLX_SLR : 0) | (ib->lrh.service_level << 8));
		if (ah->av.ib.port_pd & cpu_to_be32(0x80000000)) {
			mlx->flags |= cpu_to_be32(0x1); /* force loopback */
		}
		mlx->rlid   = ib->lrh.destination_lid;
	}

	switch (opcode) {
	case IB_WR_SEND:
		bth->opcode	 = IB_OPCODE_UD_SEND_ONLY;
		if (ib)
			ib->immediate_present = 0;
		else
			eth->immediate_present = 0;
		break;
	case IB_WR_SEND_WITH_IMM:
		bth->opcode	 = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
		if (ib) {
			ib->immediate_present = 1;
			ib->immediate_data    = wr->immediate_data;
		} else {
			eth->immediate_present = 1;
			eth->immediate_data    = wr->immediate_data;
		}
		break;
	default:
		return -EINVAL;
	}

	if (ib) {
		ib->lrh.virtual_lane    = !sqp->qp.ibqp.qp_num ? 15 : 0;
		if (ib->lrh.destination_lid == IB_LID_PERMISSIVE)
			ib->lrh.source_lid = IB_LID_PERMISSIVE;
	} else {
		memcpy(eth->eth.dmac_h, ah->av.eth.mac_0_1, 2);
		memcpy(eth->eth.dmac_h + 2, ah->av.eth.mac_2_5, 2);
		memcpy(eth->eth.dmac_l, ah->av.eth.mac_2_5 + 2, 2);
		rdma_get_ll_mac(&grh->source_gid, mac);

		tmp = mac;
		memcpy(eth->eth.smac_h, tmp, 2);
		memcpy(eth->eth.smac_l, tmp + 2, 4);
		if (!memcmp(eth->eth.smac_h, eth->eth.dmac_h, 6))
			mlx->flags |= cpu_to_be32(MLX4_WQE_CTRL_FORCE_LOOPBACK);
        
		eth->eth.type = cpu_to_be16(MLX4_RDMAOE_ETHERTYPE);
	}

	bth->solicited_event = (u8)(!!(wr->send_opt & IB_SEND_SOLICITED));

	if (!sqp->qp.ibqp.qp_num)
		ib_get_cached_pkey(ib_dev, sqp->qp.port, sqp->pkey_index, &pkey);
	else
		ib_get_cached_pkey(ib_dev, sqp->qp.port, wr->dgrm.ud.pkey_index, &pkey);
	bth->pkey = pkey;
	bth->destination_qpn = wr->dgrm.ud.remote_qp;
	bth->psn = cpu_to_be32((sqp->send_psn++) & ((1 << 24) - 1));
	deth->qkey = wr->dgrm.ud.remote_qkey & 0x80000000 ?
		cpu_to_be32(sqp->qkey) : wr->dgrm.ud.remote_qkey;
	deth->source_qpn = ah->ex ? cpu_to_be32(ah->ex->sqpn) :
		cpu_to_be32(sqp->qp.ibqp.qp_num);

	if (ib)
		header_size = ib_ud_header_pack(ib, sqp->header_buf);
	else
		header_size = rdmaoe_ud_header_pack(eth, sqp->header_buf);

#if 0
	{
		printk(KERN_ERR "built UD header of size %d:\n", header_size);
		for (i = 0; i < header_size / 4; ++i) {
			if (i % 8 == 0)
				printk("  [%02x] ", i * 4);
			printk(" %08x",
			       be32_to_cpu(((__be32 *) sqp->header_buf)[i]));
			if ((i + 1) % 8 == 0)
				printk("\n");
		}
		printk("\n");
	}
#endif

	/*
	 * Inline data segments may not cross a 64 byte boundary.  If
	 * our UD header is bigger than the space available up to the
	 * next 64 byte boundary in the WQE, use two inline data
	 * segments to hold the UD header.
	 */
	spc = MLX4_INLINE_ALIGN -
		((u32)(ULONG_PTR)(inl + 1) & (MLX4_INLINE_ALIGN - 1));
	if (header_size <= spc) {
		inl->byte_count = cpu_to_be32(1 << 31 | header_size);
		memcpy(inl + 1, sqp->header_buf, header_size);
		i = 1;
	} else {
		inl->byte_count = cpu_to_be32(1 << 31 | spc);
		memcpy(inl + 1, sqp->header_buf, spc);

		inl = (mlx4_wqe_inline_seg *)((u8*)(inl + 1) + spc);
		memcpy(inl + 1, sqp->header_buf + spc, header_size - spc);
		/*
		 * Need a barrier here to make sure all the data is
		 * visible before the byte_count field is set.
		 * Otherwise the HCA prefetcher could grab the 64-byte
		 * chunk with this inline segment and get a valid (!=
		 * 0xffffffff) byte count but stale data, and end up
		 * generating a packet with bad headers.
		 *
		 * The first inline segment's byte_count field doesn't
		 * need a barrier, because it comes after a
		 * control/MLX segment and therefore is at an offset
		 * of 16 mod 64.
		 */
		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | (header_size - spc));
		i = 2;
	}

	if (mlx_seg_len)
		*mlx_seg_len = ALIGN(i * sizeof (struct mlx4_wqe_inline_seg) + header_size, 16);
	return 0;
}

static int mlx4_wq_overflow(struct mlx4_ib_wq *wq, int nreq, struct ib_cq *ib_cq)
{
	unsigned cur;
	struct mlx4_ib_cq *cq;

	cur = wq->head - wq->tail;
	if (likely((int)cur + nreq < wq->max_post))
		return 0;

	cq = to_mcq(ib_cq);
	spin_lock(&cq->lock);
	cur = wq->head - wq->tail;
	spin_unlock(&cq->lock);

	return (int)cur + nreq >= wq->max_post;
}

static void set_fmr_seg(struct mlx4_wqe_fmr_seg *fseg, ib_send_wr_t *wr)
{
	struct mlx4_ib_fast_reg_page_list *mfrpl = to_mfrpl(wr->fast_reg.page_list);
	unsigned int i;

	for (i = 0; i < wr->fast_reg.page_list_len; ++i)
		mfrpl->mapped_page_list[i] =
			cpu_to_be64(wr->fast_reg.page_list->page_list[i] |
				    MLX4_MTT_FLAG_PRESENT);

	fseg->flags		= convert_access(wr->fast_reg.access_flags);
	fseg->mem_key		= wr->fast_reg.rkey;
	fseg->buf_list		= cpu_to_be64(mfrpl->map.da);
	fseg->start_addr	= cpu_to_be64(wr->fast_reg.iova_start);
	fseg->reg_len		= cpu_to_be64(wr->fast_reg.length);
	fseg->offset		= cpu_to_be32(wr->fast_reg.fbo);
	fseg->page_size		= cpu_to_be32(wr->fast_reg.page_shift);
	fseg->reserved[0]	= 0;
	fseg->reserved[1]	= 0;
#if 0
	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
		( "set_fmr_seg: flags %x, mem_key %x, page_list_pa %I64x, va %I64x, reg_len %I64x, offset %x, page_size %x, page_list_len %d, page_list[0] %I64x\n",
		be32_to_cpu(fseg->flags), be32_to_cpu(fseg->mem_key), 
		be64_to_cpu(fseg->buf_list), be64_to_cpu(fseg->start_addr), 
		be64_to_cpu(fseg->reg_len), be32_to_cpu(fseg->offset), 
		be32_to_cpu(fseg->page_size), wr->fast_reg.page_list_len, 
		be64_to_cpu(mfrpl->mapped_page_list[0]) ));
#endif
}

static void set_local_inv_seg(struct mlx4_wqe_local_inval_seg *iseg, u32 rkey)
{
	iseg->flags	= 0;
	iseg->mem_key	= rkey;
	iseg->guest_id	= 0;
	iseg->pa	= 0;
#if 0
	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
		( "set_local_inv_seg: mem_key %x\n", be32_to_cpu(rkey) ));
#endif
}

static __always_inline void set_raddr_seg(struct mlx4_wqe_raddr_seg *rseg,
					  u64 remote_addr, __be32 rkey)
{
	rseg->raddr    = cpu_to_be64(remote_addr);
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

static void set_datagram_seg(struct mlx4_wqe_datagram_seg *dseg,
			     ib_send_wr_t *wr)
{

	memcpy(dseg->av, &to_mah((struct ib_ah *)wr->dgrm.ud.h_av)->av, sizeof (struct mlx4_av));
	dseg->dqpn = wr->dgrm.ud.remote_qp;
	dseg->qkey = wr->dgrm.ud.remote_qkey;
	dseg->vlan = to_mah((struct ib_ah *)wr->dgrm.ud.h_av)->av.eth.vlan;
	memcpy(dseg->mac_0_1, to_mah((struct ib_ah *)wr->dgrm.ud.h_av)->av.eth.mac_0_1, 6);

}

static void set_tunnel_datagram_seg(struct mlx4_ib_dev *dev,
				    struct mlx4_wqe_datagram_seg *dseg,
				    ib_send_wr_t *wr)
{
	struct mlx4_ib_ah *mah = to_mah((struct ib_ah *)wr->dgrm.ud.h_av);
	struct mlx4_av *av = &mah->av.ib;
	struct mlx4_av sqp_av = {0};
	int port = *((u8*) &av->port_pd) & 0x3;

	/* XXX see if we weren't better off creating an AH for qp1 in the SA... */
	sqp_av.port_pd = av->port_pd | cpu_to_be32(0x80000000); /* force loopback */
	sqp_av.g_slid = av->g_slid & 0x7f; /* no GRH */
	sqp_av.dlid = cpu_to_be16(dev->sriov.local_lid[port - 1]);
	sqp_av.sl_tclass_flowlabel = av->sl_tclass_flowlabel & cpu_to_be32(0xf0000000);

	memcpy(dseg->av, &sqp_av, sizeof (struct mlx4_av));
	dseg->dqpn = cpu_to_be32(dev->dev->caps.tunnel_qpn + port - 1);
	dseg->qkey = cpu_to_be32(0x80000000); /* use well-known qkey from the QPC */
}

static int set_tunnel_mlx_seg(struct mlx4_ib_sqp *sqp, ib_send_wr_t *wr,
			      void *wqe, unsigned *mlx_seg_len)
{
	struct mlx4_ib_dev *mdev = to_mdev(sqp->qp.ibqp.device);
	struct mlx4_ib_ah mah = *to_mah((struct ib_ah *)wr->dgrm.ud.h_av);
	ib_send_wr_t tunnel_wr = *wr;
	struct mlx4_ib_ah_ext ex;
	int port = *((u8*) &mah.av.ib.port_pd) & 0x3;
	int err;

	ex.slid = cpu_to_be16(mdev->sriov.local_lid[port - 1]);
	ex.sqpn = sqp->qp.mqp.qpn; /* use real qpn so the multiplex code will recognize it */

	mah.ex = &ex;
	mah.av.ib.port_pd = mah.av.ib.port_pd | cpu_to_be32(0x80000000); /* force loopback */
	mah.av.ib.g_slid = mah.av.ib.g_slid & 0x7f; /* no GRH */
	mah.av.ib.dlid = ex.slid;
	mah.av.ib.sl_tclass_flowlabel = mah.av.ib.sl_tclass_flowlabel & cpu_to_be32(0xf0000000);

	//TODO: is he cast right ?
	tunnel_wr.dgrm.ud.h_av = (ib_av_handle_t)&mah.ibah;
	tunnel_wr.dgrm.ud.remote_qp = mdev->dev->caps.tunnel_qpn + port - 1;
	tunnel_wr.dgrm.ud.remote_qkey = 0x80000000; 

	/* Temporarily add the size of the tunnel header so that build_mlx_header()
	 * will correctly calculate the tunnel packet length */
	tunnel_wr.ds_array[0].length += sizeof(struct mlx4_ib_tunnel_header);
	err = build_mlx_header(sqp, &tunnel_wr, wqe, mlx_seg_len);
	tunnel_wr.ds_array[0].length -= sizeof(struct mlx4_ib_tunnel_header);
	return err;
}

static void build_tunnel_header(ib_send_wr_t *wr, void *wqe, unsigned *mlx_seg_len)
{
	struct mlx4_wqe_inline_seg *inl = (mlx4_wqe_inline_seg *)wqe;
	struct mlx4_ib_tunnel_header hdr;
	struct mlx4_ib_ah *ah = to_mah((struct ib_ah *)wr->dgrm.ud.h_av);
	u32 byte_count;
	int spc;
	int i;

	memcpy(&hdr.av, &ah->av, sizeof hdr.av);
	hdr.remote_qpn = wr->dgrm.ud.remote_qp;
	hdr.pkey_index = wr->dgrm.ud.pkey_index;
	hdr.qkey = wr->dgrm.ud.remote_qkey;

	spc = MLX4_INLINE_ALIGN -
		((unsigned long)(ULONG_PTR)(inl + 1) & (MLX4_INLINE_ALIGN - 1));
	if (sizeof(hdr) <= spc) {
		memcpy(inl + 1, &hdr, sizeof(hdr));
		wmb();
		byte_count = 1 << 31 | (u32)sizeof(hdr);
		inl->byte_count = cpu_to_be32(byte_count);
		i = 1;
	} else {
		memcpy(inl + 1, &hdr, spc);
		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | spc);
		inl = (struct mlx4_wqe_inline_seg *)((char *) (inl + 1) + spc);
		memcpy(inl + 1, (char *) &hdr + spc, sizeof(hdr) - spc);
		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | (sizeof(hdr) - spc));
		i = 2;
	}

	*mlx_seg_len =
		ALIGN(i * sizeof (struct mlx4_wqe_inline_seg) + sizeof(hdr), 16);
}

static void set_mlx_icrc_seg(void *dseg)
{
	u32 *t = (u32*)dseg;
	struct mlx4_wqe_inline_seg *iseg = (mlx4_wqe_inline_seg *)dseg;

	t[1] = 0;

	/*
	 * Need a barrier here before writing the byte_count field to
	 * make sure that all the data is visible before the
	 * byte_count field is set.  Otherwise, if the segment begins
	 * a new cacheline, the HCA prefetcher could grab the 64-byte
	 * chunk and get a valid (!= * 0xffffffff) byte count but
	 * stale data, and end up sending the wrong data.
	 */
	wmb();

	iseg->byte_count = cpu_to_be32((1 << 31) | 4);
}

static inline void set_data_seg(struct mlx4_wqe_data_seg *dseg, ib_local_ds_t *sg)
{
	dseg->lkey       = cpu_to_be32(sg->lkey);
	dseg->addr       = cpu_to_be64(sg->vaddr);

	/*
	 * Need a barrier here before writing the byte_count field to
	 * make sure that all the data is visible before the
	 * byte_count field is set.  Otherwise, if the segment begins
	 * a new cacheline, the HCA prefetcher could grab the 64-byte
	 * chunk and get a valid (!= * 0xffffffff) byte count but
	 * stale data, and end up sending the wrong data.
	 */
	wmb();

	dseg->byte_count = cpu_to_be32(sg->length);
}

static inline void __set_data_seg(struct mlx4_wqe_data_seg *dseg, ib_local_ds_t *sg)
{
	dseg->byte_count = cpu_to_be32(sg->length);
	dseg->lkey       = cpu_to_be32(sg->lkey);
	dseg->addr       = cpu_to_be64(sg->vaddr);
}

static int build_lso_seg(struct mlx4_lso_seg *wqe, ib_send_wr_t *wr,
						 struct mlx4_ib_qp *qp, unsigned *lso_seg_len)
 {
 	unsigned halign = ALIGN(sizeof *wqe + wr->dgrm.ud.hlen, 16);
	void * ds;

	if (unlikely(!(qp->flags & MLX4_IB_QP_LSO) &&
		wr->num_ds > qp->sq.max_gs - (halign >> 4)))
		return -EINVAL;
	*lso_seg_len = halign;
	 ds =  (u8 *) (void *) wqe + halign;
	
	//TODO: use memcpy from physical/virtual addr we can get directly from the ipoib at first data segmentmemcpy(wqe->header, , );
	memcpy(wqe->header, wr->dgrm.ud.header, wr->dgrm.ud.hlen);
	
	/* make sure LSO header is written before overwriting stamping */
	wmb();

	wqe->mss_hdr_size = cpu_to_be32((wr->dgrm.ud.mss) << 16 |
									wr->dgrm.ud.hlen);
	
	return 0;
}


static int lay_inline_data(struct mlx4_ib_qp *qp, ib_send_wr_t *wr,
			   u8 *wqe, int *sz)
{
	struct mlx4_wqe_inline_seg *seg;
	u8 *addr;
	int len, seg_len;
	int num_seg;
	int off, to_copy;
	u32 i;
	int inl = 0;

	seg = (mlx4_wqe_inline_seg *)wqe;
	wqe += sizeof *seg;
	off = (int)(((u64)(ULONG_PTR)wqe) & (u64)(MLX4_INLINE_ALIGN - 1));
	num_seg = 0;
	seg_len = 0;

	for (i = 0; i < wr->num_ds; ++i) {
		addr = (u8*)(ULONG_PTR)(wr->ds_array[i].vaddr);
		len  = wr->ds_array[i].length;
		inl += len;

		if (inl > qp->max_inline_data) {
			inl = 0;
			return -1;
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
			seg = (mlx4_wqe_inline_seg *)wqe;
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

	*sz = (inl + num_seg * sizeof *seg + 15) / 16;

	return 0;
}

/*
  * Avoid using memcpy() to copy to BlueFlame page, since memcpy()
  * implementations may use move-string-buffer assembler instructions,
  * which do not guarantee order of copying.
  */
static inline void mlx4_bf_copy(unsigned long *dst, unsigned long *src, unsigned bytecnt)
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

int mlx4_ib_post_send(struct ib_qp *ibqp, ib_send_wr_t *wr,
		      ib_send_wr_t **bad_wr)
{
	enum ib_wr_opcode opcode;// = to_wr_opcode(wr);
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	struct mlx4_dev	*dev = to_mdev(ibqp->device)->dev;
	u8 *wqe /*, *wqe_start*/;
	struct mlx4_wqe_ctrl_seg *ctrl = NULL;
	struct mlx4_wqe_data_seg *dseg;
	unsigned long flags;
	UNUSED_PARAM(flags);
	int nreq;
	int err = 0;
	int ind;
	int size = 0;
	unsigned seglen = 0;
	int i;
	int j = 0;
	int inl = 0;
	int wqe_cnt, wqe_cnt_mask, num_ds, sq_head, sq_spare_wqes;
	
	if (mlx4_is_barred(ibqp->device->dma_device))
		return -EFAULT;

	if(qp->state == IB_QPS_RESET ||
	   qp->state == IB_QPS_INIT ||
	   qp->state == IB_QPS_RTR)
	{
		return -ENODEV;
	}
	
	if(! qp->sq_post_sync_by_client)
	{
		spin_lock_irqsave(&qp->sq.lock, &flags);
	}
	
	ind = qp->sq_next_wqe;
	wqe_cnt = qp->sq.wqe_cnt;
	wqe_cnt_mask = wqe_cnt - 1;
	sq_head = qp->sq.head;
	sq_spare_wqes = qp->sq_spare_wqes;

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (! qp->no_wq_overflow_by_client && mlx4_wq_overflow(&qp->sq, nreq, qp->ibqp.send_cq)) {
			err = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		num_ds = wr->num_ds;

		if (unlikely(num_ds > qp->sq.max_gs)) {
			err = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		/*wqe_start = */
		wqe = (u8*)get_send_wqe(qp, ind & wqe_cnt_mask);
		ctrl = (mlx4_wqe_ctrl_seg *)wqe;
		*((u64 *) ctrl) = (ind & wqe_cnt ? 0 : __constant_cpu_to_be32(1 << 31));

		qp->sq.wrid[(sq_head + nreq) & wqe_cnt_mask] = wr->wr_id;
		opcode = to_wr_opcode(wr);

		*((u64 *)&ctrl->srcrb_flags) =
			(wr->send_opt & IB_SEND_OPT_SIGNALED ?
			 __constant_cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE) : 0) |
			(wr->send_opt & IB_SEND_OPT_SOLICITED ?
			 __constant_cpu_to_be32(MLX4_WQE_CTRL_SOLICITED) : 0) |
			(wr->send_opt & IB_SEND_OPT_TX_IP_CSUM ?
			 __constant_cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM) : 0) |
			(wr->send_opt & IB_SEND_OPT_TX_TCP_UDP_CSUM ?
			 __constant_cpu_to_be32(MLX4_WQE_CTRL_TCP_UDP_CSUM) : 0) |
			qp->sq_signal_bits;

		if (opcode == IB_WR_SEND_WITH_IMM ||
		    opcode == IB_WR_RDMA_WRITE_WITH_IMM)
			ctrl->imm = wr->immediate_data;
		//else
		//	ctrl->imm = 0;

		wqe += sizeof *ctrl;
		size = sizeof *ctrl / 16;

		switch (ibqp->qp_type) {
		case IB_QPT_RC:
		case IB_QPT_UC:
			switch (opcode) {
			case IB_WR_ATOMIC_CMP_AND_SWP:
			case IB_WR_ATOMIC_FETCH_AND_ADD:
				set_raddr_seg((mlx4_wqe_raddr_seg *)wqe, wr->remote_ops.vaddr,
					      wr->remote_ops.rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);

				set_atomic_seg((mlx4_wqe_atomic_seg *)wqe, wr);
				wqe  += sizeof (struct mlx4_wqe_atomic_seg);

				size += (sizeof (struct mlx4_wqe_raddr_seg) +
					 sizeof (struct mlx4_wqe_atomic_seg)) / 16;

				break;

			case IB_WR_RDMA_READ:
			case IB_WR_RDMA_WRITE:
			case IB_WR_RDMA_WRITE_WITH_IMM:
				set_raddr_seg((mlx4_wqe_raddr_seg *)wqe, wr->remote_ops.vaddr,
					      wr->remote_ops.rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);
				size += sizeof (struct mlx4_wqe_raddr_seg) / 16;
				break;

			case IB_WR_LOCAL_INV:
				ctrl->srcrb_flags |= __constant_cpu_to_be32(MLX4_WQE_CTRL_STRONG_ORDER);
				set_local_inv_seg((mlx4_wqe_local_inval_seg *)wqe, wr->invalidate_rkey);
				wqe  += sizeof (struct mlx4_wqe_local_inval_seg);
				size += sizeof (struct mlx4_wqe_local_inval_seg) / 16;
				break;

			case IB_WR_FAST_REG_MR:
				ctrl->srcrb_flags |= __constant_cpu_to_be32(MLX4_WQE_CTRL_STRONG_ORDER);
				set_fmr_seg((mlx4_wqe_fmr_seg *)wqe, wr);
				wqe  += sizeof (struct mlx4_wqe_fmr_seg);
				size += sizeof (struct mlx4_wqe_fmr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}
			break;

		case IB_QPT_UD:
			set_datagram_seg((mlx4_wqe_datagram_seg *)wqe, wr);
			wqe  += sizeof (struct mlx4_wqe_datagram_seg);
			size += sizeof (struct mlx4_wqe_datagram_seg) / 16;
			if (wr->wr_type == WR_LSO) {
				err = build_lso_seg((struct mlx4_lso_seg *)(void *)wqe, wr, qp, &seglen);
				if (unlikely(err)) {
					*bad_wr = wr;
					goto out;
				}
				wqe  += seglen;
				size += seglen / 16;
				j=1;
			}
			break;

		case IB_QPT_SMI:
		case IB_QPT_GSI:
			if (mlx4_is_mfunc(dev)) {
				if (!dev->caps.sqp_demux) {
					/* If we are tunneling special qps, this is a UD qp.
					 * In this case we first add a UD segment targeting
					 * the tunnel qp, and then add a header with address
					 * information */
					set_tunnel_datagram_seg(to_mdev(ibqp->device), 
						(struct mlx4_wqe_datagram_seg *)wqe, wr);
					wqe  += sizeof (struct mlx4_wqe_datagram_seg);
					size += sizeof (struct mlx4_wqe_datagram_seg) / 16;
					build_tunnel_header(wr, wqe, &seglen);
				} else if (ibqp->qp_type == IB_QPT_GSI &&
					   to_mah((struct ib_ah *)wr->dgrm.ud.h_av)->gsi_demux_lb) {
					/* On the egress path, demux functions initially
					 * loopback their qp1 traffic because it needs
					 * the same treatment as other functions */
					set_tunnel_mlx_seg(to_msqp(qp), wr, ctrl, &seglen);
					wqe  += seglen;
					size += seglen / 16;
					build_tunnel_header(wr, wqe, &seglen);
				}
			} else {
				err = build_mlx_header(to_msqp(qp), wr, ctrl, &seglen);
				if (err < 0) {
					if (bad_wr)
						*bad_wr = wr;
					goto out;
				}
			}
			
			wqe  += seglen;
			size += seglen / 16;
			break;

		default:
			break;
		}

		/*
		 * Write data segments in reverse order, so as to
		 * overwrite cacheline stamp last within each
		 * cacheline.  This avoids issues with WQE
		 * prefetching.
		 */

		dseg = (mlx4_wqe_data_seg *)wqe;
		dseg += num_ds - 1;

		/* Add one more inline data segment for ICRC for MLX sends */
		if (unlikely((ibqp->qp_type == IB_QPT_SMI ||
			     ibqp->qp_type == IB_QPT_GSI) &&
			     (!mlx4_is_mfunc(dev) || dev->caps.sqp_demux))) {
			set_mlx_icrc_seg(dseg + 1);
			size += sizeof (struct mlx4_wqe_data_seg) / 16;
		}

		if (wr->send_opt & IB_SEND_OPT_INLINE && num_ds) {
			int sz;
			err = lay_inline_data(qp, wr, wqe, &sz);
			if (!err) {
				inl = 1;
				size += sz;
			}
			else
				goto out;
		} else {
			size += num_ds * (sizeof (struct mlx4_wqe_data_seg) / 16);
			for (i = num_ds - 1; i >= 0; --i, --dseg)
				set_data_seg(dseg, wr->ds_array + i);
		}
		ctrl->fence_size = (u8)((wr->send_opt & IB_SEND_OPT_FENCE ?
				    MLX4_WQE_CTRL_FENCE : 0) | size);
		
		/*
		 * Make sure descriptor is fully written before
		 * setting ownership bit (because HW can start
		 * executing as soon as we do).
		 */
		wmb();

		if (opcode < 0 || opcode >= ARRAY_SIZE(mlx4_ib_opcode)) {
			err = -EINVAL;
			goto out;
		}

		ctrl->owner_opcode = mlx4_ib_opcode[opcode] |
			(ind & wqe_cnt ? __constant_cpu_to_be32(1 << 31) : 0);

		// statistics
		if ( ibqp->qp_type == IB_QPT_SMI || ibqp->qp_type == IB_QPT_GSI ) {
			st_print_mlx_send( dev, ibqp, wr);
			st_print_mlx_header( dev, to_msqp(qp), (mlx4_wqe_mlx_seg *)ctrl );
			st_dump_mlx_wqe( dev, ctrl, size*4, wr);
		}

		/*
		 * We can improve latency by not stamping the last
		 * send queue WQE until after ringing the doorbell, so
		 * only stamp here if there are still more WQEs to post.
		 */
		if (wr->p_next)
			stamp_send_wqe(qp, (ind + qp->sq_spare_wqes) &
				       wqe_cnt_mask);

		++ind;
	}

//printk("ctrl->srcrb_flags & MLX4_WQE_CTRL_TCP_UDP_CSUM =%d \n", ctrl->srcrb_flags & cpu_to_be32(MLX4_WQE_CTRL_TCP_UDP_CSUM ));

out:
//WQE printout
#if 0	
		if (j) {
			u32 *ds = (u32 *) wqe_start;
			printk("WQE DUMP:\n");cq.c.their
			for (j = 0; j < ctrl->fence_size*4; ++j) {
				printk("%d %08x\n", j,be32_to_cpu(*ds));
				++ds;
			}
		}
#endif	

	if (nreq == 1 && inl && size > 1 && size < qp->bf.buf_size / 16) {
		ctrl->owner_opcode |= htonl((qp->sq_next_wqe & 0xffff) << 8);
		*(u32 *) (&ctrl->vlan_tag) |= qp->doorbell_qpn;
		/*
		 * Make sure that descriptor is written to memory
		 * before writing to BlueFlame page.
		 */
		wmb();

		++qp->sq.head;

		mlx4_bf_copy((PULONG)(qp->bf.reg + qp->bf.offset), (PULONG) ctrl,
			     ALIGN(size * 16, 64));
		wmb();

		qp->bf.offset ^= qp->bf.buf_size;

	} else if (nreq) {
		qp->sq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		__fast_writel(qp->doorbell_qpn, qp->bf.uar->map + MLX4_SEND_DOORBELL);


#if 0
		if (qp->mqp.qpn == 0x41)
			cl_dbg_out( "[MLX4_BUS] mlx4_ib_post_send : qtype %d, qpn %#x, nreq %d, sq.head %#x, wqe_ix %d, db %p \n", 
				ibqp->qp_type, qp->mqp.qpn, nreq, qp->sq.head, ind, 
				(u8*)to_mdev(ibqp->device)->uar_map + MLX4_SEND_DOORBELL );
#endif		
		/*
		 * Make sure doorbells don't leak out of SQ spinlock
		 * and reach the HCA out of order.
		 */
		mmiowb();

	}

	stamp_send_wqe(qp, (ind + sq_spare_wqes - 1) &
	   wqe_cnt_mask);

#ifdef WQE_SHRINK
	// instead of above
	stamp_send_wqe(qp, stamp, size * 16);
	ind = pad_wraparound(qp, ind);
#endif

	qp->sq_next_wqe = ind;

	if(! qp->sq_post_sync_by_client)
	{
		spin_unlock_irqrestore(&qp->sq.lock, flags);
	}
	return err;
}

int mlx4_ib_post_recv(struct ib_qp *ibqp, ib_recv_wr_t *wr,
		      ib_recv_wr_t **bad_wr)
{
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	struct mlx4_wqe_data_seg *scat;
	unsigned long flags;
	UNUSED_PARAM(flags);
	int err = 0;
	int nreq;
	int ind;
	int i;

	if (mlx4_is_barred(ibqp->device->dma_device))
		return -EFAULT;

	if(! qp->rq_post_sync_by_client)
	{
		spin_lock_irqsave(&qp->rq.lock, &flags);
	}
	
	ind = qp->rq.head & (qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (! qp->no_wq_overflow_by_client && mlx4_wq_overflow(&qp->rq, nreq, qp->ibqp.recv_cq)) {
			err = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_ds > (u32)qp->rq.max_gs)) {
			err = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		scat = (mlx4_wqe_data_seg *)get_recv_wqe(qp, ind);

		for (i = 0; i < (int)wr->num_ds; ++i)
			__set_data_seg(scat + i, wr->ds_array + i);

		if (i < qp->rq.max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = __constant_cpu_to_be32(MLX4_INVALID_LKEY);
			scat[i].addr       = 0;
		}

		qp->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (qp->rq.wqe_cnt - 1);
	}

out:
	if (likely(nreq)) {
		qp->rq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*qp->db.db = cpu_to_be32(qp->rq.head & 0xffff);

#if 0
		if (qp->mqp.qpn == 0x41)
			cl_dbg_out( "[MLX4_BUS] mlx4_ib_post_recv : qtype %d, qpn %#x, nreq %d, rq.head %#x, wqe_ix %d, db_obj %p, db %p \n", 
				ibqp->qp_type, qp->mqp.qpn, nreq, qp->rq.head, ind, &qp->db, qp->db.db );
#endif		
	}

	if(! qp->rq_post_sync_by_client)
	{
		spin_unlock_irqrestore(&qp->rq.lock, flags);
	}
	
	return err;
}

static inline enum ib_qp_state to_ib_qp_state(enum mlx4_qp_state mlx4_state)
{
	switch (mlx4_state) {
	case MLX4_QP_STATE_RST:      return XIB_QPS_RESET;
	case MLX4_QP_STATE_INIT:     return XIB_QPS_INIT;
	case MLX4_QP_STATE_RTR:      return XIB_QPS_RTR;
	case MLX4_QP_STATE_RTS:      return XIB_QPS_RTS;
	case MLX4_QP_STATE_SQ_DRAINING:
	case MLX4_QP_STATE_SQD:      return XIB_QPS_SQD;
	case MLX4_QP_STATE_SQER:     return XIB_QPS_SQE;
	case MLX4_QP_STATE_ERR:      return XIB_QPS_ERR;
	default:		     return (ib_qp_state)-1;
	}
}

static inline enum ib_mig_state to_ib_mig_state(int mlx4_mig_state)
{
	switch (mlx4_mig_state) {
	case MLX4_QP_PM_ARMED:		return IB_MIG_ARMED;
	case MLX4_QP_PM_REARM:		return IB_MIG_REARM;
	case MLX4_QP_PM_MIGRATED:	return IB_MIG_MIGRATED;
	default: return (ib_mig_state)-1;
	}
}

static int to_ib_qp_access_flags(int mlx4_flags)
{
	int ib_flags = 0;

	if (mlx4_flags & MLX4_QP_BIT_RRE)
		ib_flags |= IB_ACCESS_REMOTE_READ;
	if (mlx4_flags & MLX4_QP_BIT_RWE)
		ib_flags |= IB_ACCESS_REMOTE_WRITE;
	if (mlx4_flags & MLX4_QP_BIT_RAE)
		ib_flags |= IB_ACCESS_REMOTE_ATOMIC;

	return ib_flags;
}

static void to_ib_ah_attr(struct mlx4_ib_dev *dev, struct ib_ah_attr *ib_ah_attr,
				struct mlx4_qp_path *path)
{
	int is_eth;
	
	memset(ib_ah_attr, 0, sizeof *ib_ah_attr);
	ib_ah_attr->port_num	  = path->sched_queue & 0x40 ? 2 : 1;

	if (ib_ah_attr->port_num == 0 || ib_ah_attr->port_num > dev->dev->caps.num_ports)
		return;

	is_eth = rdma_port_get_transport(&dev->ib_dev, ib_ah_attr->port_num) ==
		RDMA_TRANSPORT_RDMAOE ? 1 : 0;

	if(is_eth)
	{
		ib_ah_attr->sl		  = ((path->sched_queue >> 3) & 0x7) | 
			((path->sched_queue & 4) << 1);
	}
	else
	{
		ib_ah_attr->sl		  = (path->sched_queue >> 2) & 0xf;
	}
	
	ib_ah_attr->dlid	  = be16_to_cpu(path->rlid);
	ib_ah_attr->src_path_bits = path->grh_mylmc & 0x7f;
	ib_ah_attr->static_rate   = path->static_rate ? path->static_rate - 5 : 0;
	ib_ah_attr->ah_flags      = (u8)((path->grh_mylmc & (1 << 7)) ? IB_AH_GRH : 0);
	if (ib_ah_attr->ah_flags) {
		ib_ah_attr->grh.sgid_index = path->mgid_index;
		ib_ah_attr->grh.hop_limit  = path->hop_limit;
		ib_ah_attr->grh.traffic_class =
			(u8)((be32_to_cpu(path->tclass_flowlabel) >> 20) & 0xff);
		ib_ah_attr->grh.flow_label =
			be32_to_cpu(path->tclass_flowlabel) & 0xfffff;
		memcpy(ib_ah_attr->grh.dgid.raw,
			path->rgid, sizeof ib_ah_attr->grh.dgid.raw);
	}
}

int mlx4_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		     struct ib_qp_init_attr *qp_init_attr)
{
	struct mlx4_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	struct mlx4_qp_context context;
	int mlx4_state;
	int err = 0;

	UNUSED_PARAM(qp_attr_mask);

	if (mlx4_is_barred(dev->dev))
		return -EFAULT;

	mutex_lock(&qp->mutex);
	
	if (qp->state == XIB_QPS_RESET) {
		qp_attr->qp_state = XIB_QPS_RESET;
		goto done;
	}

	err = mlx4_qp_query(dev->dev, &qp->mqp, &context);
	if (err) {
		err = -EINVAL;
		goto out;
	}

	mlx4_state = be32_to_cpu(context.flags) >> 28;

	qp_attr->qp_state	     = to_ib_qp_state((mlx4_qp_state)mlx4_state);
	qp_attr->path_mtu	     = (ib_mtu)(context.mtu_msgmax >> 5);
	qp_attr->path_mig_state	     =
		to_ib_mig_state((be32_to_cpu(context.flags) >> 11) & 0x3);
	qp_attr->qkey		     = be32_to_cpu(context.qkey);
	qp_attr->rq_psn		     = be32_to_cpu(context.rnr_nextrecvpsn) & 0xffffff;
	qp_attr->sq_psn		     = be32_to_cpu(context.next_send_psn) & 0xffffff;
	qp_attr->dest_qp_num	     = be32_to_cpu(context.remote_qpn) & 0xffffff;
	qp_attr->qp_access_flags     =
		to_ib_qp_access_flags(be32_to_cpu(context.params2));

	if (qp->ibqp.qp_type == IB_QPT_RC || qp->ibqp.qp_type == IB_QPT_UC) {
		to_ib_ah_attr(dev, &qp_attr->ah_attr, &context.pri_path);
		to_ib_ah_attr(dev, &qp_attr->alt_ah_attr, &context.alt_path);
		qp_attr->alt_pkey_index = context.alt_path.pkey_index & 0x7f;
		qp_attr->alt_port_num	= qp_attr->alt_ah_attr.port_num;
	}

	qp_attr->pkey_index = context.pri_path.pkey_index & 0x7f;
	if (qp_attr->qp_state == XIB_QPS_INIT)
		qp_attr->port_num = qp->port;
	else
		qp_attr->port_num = context.pri_path.sched_queue & 0x40 ? 2 : 1;

	/* qp_attr->en_sqd_async_notify is only applicable in modify qp */
	qp_attr->sq_draining = (u8)(mlx4_state == MLX4_QP_STATE_SQ_DRAINING);

	qp_attr->max_rd_atomic = (u8)(1 << ((be32_to_cpu(context.params1) >> 21) & 0x7));

	qp_attr->max_dest_rd_atomic =
		(u8)(1 << ((be32_to_cpu(context.params2) >> 21) & 0x7));
	qp_attr->min_rnr_timer	    =
		(u8)((be32_to_cpu(context.rnr_nextrecvpsn) >> 24) & 0x1f);
	qp_attr->timeout	    = context.pri_path.ackto >> 3;
	qp_attr->retry_cnt	    = (u8)((be32_to_cpu(context.params1) >> 16) & 0x7);
	qp_attr->rnr_retry	    = (u8)((be32_to_cpu(context.params1) >> 13) & 0x7);
	qp_attr->alt_timeout	    = context.alt_path.ackto >> 3;

done:
	qp_attr->cur_qp_state	     = qp_attr->qp_state;
	qp_attr->cap.max_recv_wr     = qp->rq.wqe_cnt;
	qp_attr->cap.max_recv_sge    = qp->rq.max_gs;

	if (!ibqp->p_uctx) {
		qp_attr->cap.max_send_wr  = qp->sq.wqe_cnt;
		qp_attr->cap.max_send_sge = qp->sq.max_gs;
	} else {
		qp_attr->cap.max_send_wr  = 0;
		qp_attr->cap.max_send_sge = 0;
	}

	/*
	 * We don't support inline sends for kernel QPs (yet), and we
	 * don't know what userspace's value should be.
	 */
	qp_attr->cap.max_inline_data = qp->max_inline_data;

	qp_init_attr->cap	     = qp_attr->cap;

out:
	mutex_unlock(&qp->mutex);
	return err;
}

