/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems.  All rights reserved.
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

#include "mlx4.h"
#include "doorbell.h"
#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#include "cq.tmh"
#endif

enum {
	MLX4_CQ_DOORBELL			= 0x20
};

enum {
	CQ_OK					=  0,
	CQ_EMPTY				= -1,
	CQ_POLL_ERR				= -2
};

#define MLX4_CQ_DB_REQ_NOT_SOL			(1 << 24)
#define MLX4_CQ_DB_REQ_NOT			(2 << 24)

enum {
	MLX4_CQE_OWNER_MASK			= 0x80,
	MLX4_CQE_IS_SEND_MASK			= 0x40,
	MLX4_CQE_OPCODE_MASK			= 0x1f
};

enum {
	MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR		= 0x01,
	MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR		= 0x02,
	MLX4_CQE_SYNDROME_LOCAL_PROT_ERR		= 0x04,
	MLX4_CQE_SYNDROME_WR_FLUSH_ERR			= 0x05,
	MLX4_CQE_SYNDROME_MW_BIND_ERR			= 0x06,
	MLX4_CQE_SYNDROME_BAD_RESP_ERR			= 0x10,
	MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR		= 0x11,
	MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR		= 0x12,
	MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR		= 0x13,
	MLX4_CQE_SYNDROME_REMOTE_OP_ERR			= 0x14,
	MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR	= 0x15,
	MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR		= 0x16,
	MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR		= 0x22,
};

struct mlx4_cqe {
	uint32_t	my_qpn;
	uint32_t	immed_rss_invalid;
	uint32_t	g_mlpath_rqpn;
	uint8_t		sl;
	uint8_t		reserved1;
	uint16_t	rlid;
	uint32_t	reserved2;
	uint32_t	byte_cnt;
	uint16_t	wqe_index;
	uint16_t	checksum;
	uint8_t		reserved3[3];
	uint8_t		owner_sr_opcode;
};

struct mlx4_err_cqe {
	uint32_t	my_qpn;
	uint32_t	reserved1[5];
	uint16_t	wqe_index;
	uint8_t		vendor_err;
	uint8_t		syndrome;
	uint8_t		reserved2[3];
	uint8_t		owner_sr_opcode;
};

static struct mlx4_cqe *get_cqe(struct mlx4_cq *cq, int entry)
{
	return (struct mlx4_cqe *)(cq->buf.buf + entry * cq->buf.entry_size);
}

static void *get_sw_cqe(struct mlx4_cq *cq, int n)
{
	struct mlx4_cqe *cqe = get_cqe(cq, n & cq->ibv_cq.cqe);
	struct mlx4_cqe *tcqe = ((cq->buf.entry_size == 64) ? (cqe + 1) : cqe);

	return (!!(tcqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK) ^
		!!(n & (cq->ibv_cq.cqe + 1))) ? NULL : cqe;
}

static struct mlx4_cqe *next_cqe_sw(struct mlx4_cq *cq)
{
	return get_sw_cqe(cq, cq->cons_index);
}

static void update_cons_index(struct mlx4_cq *cq)
{
	*cq->set_ci_db = htonl(cq->cons_index & 0xffffff);
}

static void mlx4_handle_error_cqe(struct mlx4_err_cqe *cqe, ib_wc_t *wc)
{
	if (cqe->syndrome == MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR)
		printf(PFX "local QP operation err "
		       "(QPN %06x, WQE index %x, vendor syndrome %02x, "
		       "opcode = %02x)\n",
		       htonl(cqe->my_qpn), htons(cqe->wqe_index),
		       cqe->vendor_err,
		       cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);

	switch (cqe->syndrome) {
	case MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		wc->status = IB_WCS_LOCAL_LEN_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		wc->status = IB_WCS_LOCAL_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_PROT_ERR:
		wc->status = IB_WCS_LOCAL_PROTECTION_ERR;
		break;
	case MLX4_CQE_SYNDROME_WR_FLUSH_ERR:
		wc->status = IB_WCS_WR_FLUSHED_ERR;
		break;
	case MLX4_CQE_SYNDROME_MW_BIND_ERR:
		wc->status = IB_WCS_MEM_WINDOW_BIND_ERR;
		break;
	case MLX4_CQE_SYNDROME_BAD_RESP_ERR:
		wc->status = IB_WCS_BAD_RESP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		wc->status = IB_WCS_LOCAL_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		wc->status = IB_WCS_REM_INVALID_REQ_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		wc->status = IB_WCS_REM_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_OP_ERR:
		wc->status = IB_WCS_REM_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IB_WCS_TIMEOUT_RETRY_ERR;
		break;
	case MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		wc->status = IB_WCS_RNR_RETRY_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		wc->status = IB_WCS_REM_ABORT_ERR;
		break;
	}

	wc->vendor_specific = cqe->vendor_err;
}

static int mlx4_poll_one(struct mlx4_cq *cq, struct mlx4_qp **cur_qp, ib_wc_t *wc)
{
	struct mlx4_wq *wq;
	struct mlx4_cqe *cqe;
	struct mlx4_srq *srq = NULL;
	uint32_t qpn;
	uint16_t wqe_index;
	int is_error;
	int is_send;
#ifdef XRC_SUPPORT
	int is_xrc_recv = 0;
#endif

	cqe = next_cqe_sw(cq);
	if (!cqe)
		return CQ_EMPTY;

	if (cq->buf.entry_size == 64)
		cqe++;
	
	++cq->cons_index;

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	qpn = ntohl(cqe->my_qpn);

	is_send  = cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK;
	is_error = (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
		MLX4_CQE_OPCODE_ERROR;

#ifdef XRC_SUPPORT
	if (qpn & MLX4_XRC_QPN_BIT && !is_send) {
		uint32_t srqn = ntohl(cqe->g_mlpath_rqpn) & 0xffffff;
		/*
		* We do not have to take the XRC SRQ table lock here,
		* because CQs will be locked while XRC SRQs are removed
		* from the table.
		*/
		srq = mlx4_find_xrc_srq(to_mctx(cq->ibv_cq.context), srqn);
		if (!srq)
			return CQ_POLL_ERR;
		is_xrc_recv = 1;
	} else 
#endif
	if (!*cur_qp || (qpn & 0xffffff) != (*cur_qp)->ibv_qp.qp_num) {
		struct mlx4_qp *tmp_qp;
		/*
		* We do not have to take the QP table lock here,
		* because CQs will be locked while QPs are removed
		* from the table.
		*/
		tmp_qp = mlx4_find_qp(to_mctx(cq->ibv_cq.context), qpn & 0xffffff);
			if (!tmp_qp) {
					MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_CQ, (
						"cqe_qpn %#x, wr_id %#I64x, ix %d, cons_index %d, asked_qpn %#x \n", 
						qpn, wc->wr_id, ntohs(cqe->wqe_index), cq->cons_index - 1,
						(*cur_qp) ? (*cur_qp)->ibv_qp.qp_num : 0 )); 
			return CQ_POLL_ERR;
			}
			*cur_qp = tmp_qp;
		}

	if (is_send) {
		wq = &(*cur_qp)->sq;
		wqe_index = ntohs(cqe->wqe_index);
		wq->tail += (uint16_t) (wqe_index - (uint16_t) wq->tail);
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	} else 
#ifdef XRC_SUPPORT
	if (is_xrc_recv) {
		wqe_index = htons(cqe->wqe_index);
		wc->wr_id = srq->wrid[wqe_index];
		mlx4_free_srq_wqe(srq, wqe_index);
	} else 
#endif	
	if ((*cur_qp)->ibv_qp.srq) {
		srq = to_msrq((*cur_qp)->ibv_qp.srq);
		wqe_index = htons(cqe->wqe_index);
		wc->wr_id = srq->wrid[wqe_index];
		mlx4_free_srq_wqe(srq, wqe_index);
	} else {
		wq = &(*cur_qp)->rq;
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	}

	if (is_send) {
		wc->recv.ud.recv_opt = 0;
		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_OPCODE_RDMA_WRITE_IMM:
			wc->recv.ud.recv_opt |= IB_RECV_OPT_IMMEDIATE;
            __fallthrough;
		case MLX4_OPCODE_RDMA_WRITE:
			wc->wc_type   = IB_WC_RDMA_WRITE;
			break;
		case MLX4_OPCODE_SEND_IMM:
			wc->recv.ud.recv_opt |= IB_RECV_OPT_IMMEDIATE;
            __fallthrough;
		case MLX4_OPCODE_SEND:
			wc->wc_type   = IB_WC_SEND;
			break;
		case MLX4_OPCODE_RDMA_READ:
			wc->wc_type   = IB_WC_RDMA_READ;
			wc->length      = ntohl(cqe->byte_cnt);
			break;
		case MLX4_OPCODE_ATOMIC_CS:
			wc->wc_type   = IB_WC_COMPARE_SWAP;
			wc->length      = 8;
			break;
		case MLX4_OPCODE_ATOMIC_FA:
			wc->wc_type   = IB_WC_FETCH_ADD;
			wc->length      = 8;
			break;
		case MLX4_OPCODE_BIND_MW:
			wc->wc_type   = IB_WC_MW_BIND;
			break;
        case MLX4_OPCODE_NOP:
            wc->wc_type   = IB_WC_NOP;
            break;
		default:
			/* assume it's a send completion */
			wc->wc_type   = IB_WC_SEND;
			break;
		}
	} else {
		wc->length = ntohl(cqe->byte_cnt);

		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_RECV_OPCODE_RDMA_WRITE_IMM:
			wc->wc_type   = IB_WC_RECV;
			wc->recv.ud.recv_opt  = IB_RECV_OPT_IMMEDIATE;
			wc->recv.ud.immediate_data = cqe->immed_rss_invalid;
			break;
		case MLX4_RECV_OPCODE_SEND:
			wc->wc_type   = IB_WC_RECV;
			wc->recv.ud.recv_opt = 0;
			break;
		case MLX4_RECV_OPCODE_SEND_IMM:
			wc->wc_type   = IB_WC_RECV;
			wc->recv.ud.recv_opt  = IB_RECV_OPT_IMMEDIATE;
			wc->recv.ud.immediate_data = cqe->immed_rss_invalid;
			break;
		default:
			/* assume it's a recv completion */
			wc->recv.ud.recv_opt  = 0;
			wc->wc_type = IB_WC_RECV;
			break;
		}

		wc->recv.ud.remote_lid	= cqe->rlid;
		wc->recv.ud.remote_sl		= cqe->sl >> 4;
		wc->recv.ud.remote_qp	= cqe->g_mlpath_rqpn & 0xffffff00;
		wc->recv.ud.path_bits		= (uint8_t)(cqe->g_mlpath_rqpn & 0x7f);
		wc->recv.ud.recv_opt		|= cqe->g_mlpath_rqpn & 0x080 ? IB_RECV_OPT_GRH_VALID : 0;
		wc->recv.ud.pkey_index	= (uint16_t)(ntohl(cqe->immed_rss_invalid) & 0x7f);
	}

	if (is_error) 
		mlx4_handle_error_cqe((struct mlx4_err_cqe *) cqe, wc);
	else
		wc->status = IB_WCS_SUCCESS;

	MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_CQ, ("qpn %#x, wr_id %#I64x, ix %d, cons_index %d, is_error %d \n", 
		qpn, wc->wr_id, ntohs(cqe->wqe_index), cq->cons_index - 1, is_error )); 

	return CQ_OK;
}

int mlx4_poll_cq_array(const void* h_cq,
			const int num_entries, uvp_wc_t* const wc)
{
	struct mlx4_cq *cq = to_mcq((struct ibv_cq *)/*Ptr64ToPtr(*/ h_cq /*)*/);
	struct mlx4_qp *qp = NULL;
	int ne;
	int err = CQ_EMPTY;

	pthread_spin_lock(&cq->lock);
 	for (ne = 0; ne < num_entries; ne++) {
		err = mlx4_poll_one(cq, &qp, (ib_wc_t *) &wc[ne]);
		if (err != CQ_OK)
			break;
		wc[ne].qp_context = qp->ibv_qp.qp_context;
	}

	if (ne)
		update_cons_index(cq);
	pthread_spin_unlock(&cq->lock);

	return (err == CQ_OK || err == CQ_EMPTY) ? ne : err;
}

ib_api_status_t
mlx4_poll_cq_list(
	IN		const	void*						h_cq,
	IN	OUT			ib_wc_t**	const			pp_free_wclist,
		OUT			ib_wc_t**	const			pp_done_wclist)
{
	struct mlx4_cq *cq = to_mcq((struct ibv_cq *)/*Ptr64ToPtr(*/ h_cq /*)*/);
	struct mlx4_qp *qp = NULL;
	ib_wc_t *wc_p, **next_pp;
	int npolled = 0;
	int err = CQ_OK;
	ib_api_status_t status = IB_SUCCESS;

	pthread_spin_lock(&cq->lock);
 
	// loop through CQ
	next_pp = pp_done_wclist;
	wc_p = *pp_free_wclist;
	while( wc_p ) {
		err = mlx4_poll_one(cq, &qp, wc_p);
		if (err != CQ_OK)
			break;

		// prepare for the next step
		*next_pp = wc_p;
		next_pp = &wc_p->p_next;
		wc_p = wc_p->p_next;
		++npolled;
	}

	// prepare the results
	*pp_free_wclist = wc_p;		/* Set the head of the free list. */
	*next_pp = NULL;				/* Clear the tail of the done list. */

	if (npolled)
		update_cons_index(cq);

	pthread_spin_unlock(&cq->lock);

	if (err == CQ_POLL_ERR)
		status = IB_ERROR;
	else if (err == CQ_EMPTY && npolled == 0 )
		status = IB_NOT_FOUND;
	
	return status;
}

ib_api_status_t
mlx4_arm_cq (
	IN		const	void*						h_cq,
	IN		const	boolean_t					solicited)
{
	struct ibv_cq *ibvcq = (struct ibv_cq *)/*Ptr64ToPtr(*/ h_cq /*)*/;
	struct mlx4_cq *cq = to_mcq(ibvcq);
	uint32_t doorbell[2];
	uint32_t sn;
	uint32_t ci;
	uint32_t cmd;

	sn  = cq->arm_sn & 3;
	ci  = cq->cons_index & 0xffffff;
	cmd = solicited ? MLX4_CQ_DB_REQ_NOT_SOL : MLX4_CQ_DB_REQ_NOT;

	*cq->arm_db = htonl(sn << 28 | cmd | ci);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	doorbell[0] = htonl(sn << 28 | cmd | cq->cqn);
	doorbell[1] = htonl(ci);

	mlx4_write64(doorbell, to_mctx(ibvcq->context), MLX4_CQ_DOORBELL);

	return IB_SUCCESS;
}

#if 0
// this function could be called in Windows
// we do it in kernel
void mlx4_cq_event(struct ibv_cq *cq)
{
	to_mcq(cq)->arm_sn++;
}
#endif

void mlx4_cq_clean(struct mlx4_cq *cq, uint32_t qpn, struct mlx4_srq *srq)
{
	struct mlx4_cqe *cqe, *dest, *tcqe, *tdest;
	uint32_t prod_index;
	uint8_t owner_bit;
	int nfreed = 0;
#ifdef XRC_SUPPORT
	int is_xrc_srq = 0;

	if (srq && srq->ibv_srq.xrc_cq)
		is_xrc_srq = 1;
#endif	

	pthread_spin_lock(&cq->lock);

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->cons_index; get_sw_cqe(cq, prod_index); ++prod_index)
		if (prod_index == cq->cons_index + cq->ibv_cq.cqe)
			break;

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibv_cq.cqe);
		tcqe = (cq->buf.entry_size == 64) ? (cqe + 1) : cqe;
#ifdef XRC_SUPPORT
		if (is_xrc_srq &&
		    (ntohl(tcqe->g_mlpath_rqpn & 0xffffff) == srq->srqn) &&
		    !(tcqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK)) {
			mlx4_free_srq_wqe(srq, ntohs(tcqe->wqe_index));
			++nfreed;
		} else 
#endif		
		if ((ntohl(tcqe->my_qpn) & 0xffffff) == qpn) {
			if (srq && !(tcqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK))
				mlx4_free_srq_wqe(srq, ntohs(tcqe->wqe_index));
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(cq, (prod_index + nfreed) & cq->ibv_cq.cqe);
			tdest = (cq->buf.entry_size == 64) ? (dest + 1) : dest;
			owner_bit =  (uint8_t)(tdest->owner_sr_opcode & MLX4_CQE_OWNER_MASK);
			memcpy(dest, cqe, cq->buf.entry_size);
			tdest->owner_sr_opcode = (uint8_t)(owner_bit |
				(tdest->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK));
		}
	}

	if (nfreed) {
		cq->cons_index += nfreed;
		/*
		 * Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		wmb();
		update_cons_index(cq);
	}

	pthread_spin_unlock(&cq->lock);
}

void mlx4_cq_resize_copy_cqes(struct mlx4_cq *cq, void *buf, int old_cqe)
{
	UNREFERENCED_PARAMETER(cq);
	UNREFERENCED_PARAMETER(buf);
	UNREFERENCED_PARAMETER(old_cqe);
}
