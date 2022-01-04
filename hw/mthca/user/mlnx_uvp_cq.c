/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
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
#include <opcode.h>
#include "mlnx_uvp.h"
#include "mlnx_uvp_doorbell.h"
#include <iba\ib_uvp.h>

#if defined(EVENT_TRACING)
#include "mlnx_uvp_cq.tmh"
#endif


enum {
	MTHCA_CQ_DOORBELL	= 0x20
};

enum {
	CQ_OK		=  0,
	CQ_EMPTY	= -1,
	CQ_POLL_ERR	= -2
};

#define MTHCA_TAVOR_CQ_DB_INC_CI       (1 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT      (2 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT_SOL  (3 << 24)
#define MTHCA_TAVOR_CQ_DB_SET_CI       (4 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT_MULT (5 << 24)

#define MTHCA_ARBEL_CQ_DB_REQ_NOT_SOL  (1 << 24)
#define MTHCA_ARBEL_CQ_DB_REQ_NOT      (2 << 24)
#define MTHCA_ARBEL_CQ_DB_REQ_NOT_MULT (3 << 24)

enum {
	MTHCA_CQ_ENTRY_OWNER_SW     = 0x00,
	MTHCA_CQ_ENTRY_OWNER_HW     = 0x80,
	MTHCA_ERROR_CQE_OPCODE_MASK = 0xfe
};

enum {
	SYNDROME_LOCAL_LENGTH_ERR 	 = 0x01,
	SYNDROME_LOCAL_QP_OP_ERR  	 = 0x02,
	SYNDROME_LOCAL_EEC_OP_ERR 	 = 0x03,
	SYNDROME_LOCAL_PROT_ERR   	 = 0x04,
	SYNDROME_WR_FLUSH_ERR     	 = 0x05,
	SYNDROME_MW_BIND_ERR      	 = 0x06,
	SYNDROME_BAD_RESP_ERR     	 = 0x10,
	SYNDROME_LOCAL_ACCESS_ERR 	 = 0x11,
	SYNDROME_REMOTE_INVAL_REQ_ERR 	 = 0x12,
	SYNDROME_REMOTE_ACCESS_ERR 	 = 0x13,
	SYNDROME_REMOTE_OP_ERR     	 = 0x14,
	SYNDROME_RETRY_EXC_ERR 		 = 0x15,
	SYNDROME_RNR_RETRY_EXC_ERR 	 = 0x16,
	SYNDROME_LOCAL_RDD_VIOL_ERR 	 = 0x20,
	SYNDROME_REMOTE_INVAL_RD_REQ_ERR = 0x21,
	SYNDROME_REMOTE_ABORTED_ERR 	 = 0x22,
	SYNDROME_INVAL_EECN_ERR 	 = 0x23,
	SYNDROME_INVAL_EEC_STATE_ERR 	 = 0x24
};

struct mthca_cqe {
	uint32_t	my_qpn;
	uint32_t	my_ee;
	uint32_t	rqpn;
	uint16_t	sl_g_mlpath;
	uint16_t	rlid;
	uint32_t	imm_etype_pkey_eec;
	uint32_t	byte_cnt;
	uint32_t	wqe;
	uint8_t		opcode;
	uint8_t		is_send;
	uint8_t		reserved;
	uint8_t		owner;
};

struct mthca_err_cqe {
	uint32_t	my_qpn;
	uint32_t	reserved1[3];
	uint8_t		syndrome;
	uint8_t		vendor_err;
	uint16_t	db_cnt;
	uint32_t	reserved2;
	uint32_t	wqe;
	uint8_t		opcode;
	uint8_t		reserved3[2];
	uint8_t		owner;
};

static inline struct mthca_cqe *get_cqe(struct mthca_cq *cq, int entry)
{
	return (struct mthca_cqe *)((uint8_t*)cq->buf + entry * MTHCA_CQ_ENTRY_SIZE);
}

static inline struct mthca_cqe *cqe_sw(struct mthca_cq *cq, int i)
{
	struct mthca_cqe *cqe = get_cqe(cq, i);
	return MTHCA_CQ_ENTRY_OWNER_HW & cqe->owner ? NULL : cqe;
}

static inline struct mthca_cqe *next_cqe_sw(struct mthca_cq *cq)
{
	return cqe_sw(cq, cq->cons_index & cq->ibv_cq.cqe);
}

static inline void set_cqe_hw(struct mthca_cqe *cqe)
{
	cqe->owner = MTHCA_CQ_ENTRY_OWNER_HW;
}

/*
 * incr is ignored in native Arbel (mem-free) mode, so cq->cons_index
 * should be correct before calling update_cons_index().
 */
static inline void update_cons_index(struct mthca_cq *cq, int incr)
{
	uint32_t doorbell[2];

	if (mthca_is_memfree(cq->ibv_cq.context)) {
		*cq->set_ci_db = cl_hton32(cq->cons_index);
		mb();
	} else {
		doorbell[0] = cl_hton32(MTHCA_TAVOR_CQ_DB_INC_CI | cq->cqn);
		doorbell[1] = cl_hton32(incr - 1);

		mthca_write64(doorbell, to_mctx(cq->ibv_cq.context), MTHCA_CQ_DOORBELL);
	}
}


static void dump_cqe(uint32_t print_lvl, void *cqe_ptr)
{
	uint32_t *cqe = cqe_ptr;
	int i;
	(void) cqe;	/* avoid warning if mthca_dbg compiled away... */

	UVP_PRINT(print_lvl,UVP_DBG_CQ,("CQE content \n"));
	UVP_PRINT(print_lvl,UVP_DBG_CQ,(" [%2x] %08x %08x %08x %08x \n",0
		, cl_ntoh32(cqe[0]), cl_ntoh32(cqe[1]), cl_ntoh32(cqe[2]), cl_ntoh32(cqe[3])));
	UVP_PRINT(print_lvl,UVP_DBG_CQ,(" [%2x] %08x %08x %08x %08x\n",16
		, cl_ntoh32(cqe[4]), cl_ntoh32(cqe[5]), cl_ntoh32(cqe[6]), cl_ntoh32(cqe[7])));
	
}

static int handle_error_cqe(struct mthca_cq *cq,
			    struct mthca_qp *qp, int wqe_index, int is_send,
			    struct mthca_err_cqe *cqe,
			    struct _ib_wc *entry, int *free_cqe)
{
	int err;
	int dbd;
	uint32_t new_wqe;

	if (cqe->syndrome == SYNDROME_LOCAL_QP_OP_ERR) {
		UVP_PRINT(TRACE_LEVEL_ERROR , UVP_DBG_CQ,("local QP operation err "
		       "(QPN %06x, WQE @ %08x, CQN %06x, index %d, vendor_err %d)\n",
		       cl_ntoh32(cqe->my_qpn), cl_ntoh32(cqe->wqe),
		       cq->cqn, cq->cons_index, cqe->vendor_err));
		dump_cqe(TRACE_LEVEL_VERBOSE, cqe);
	}

	/*
	 * For completions in error, only work request ID, status, vendor error
	 * (and freed resource count for RD) have to be set.
	 */
	switch (cqe->syndrome) {
	case SYNDROME_LOCAL_LENGTH_ERR:
		entry->status = IB_WCS_LOCAL_LEN_ERR;
		break;
	case SYNDROME_LOCAL_QP_OP_ERR:
		entry->status = IB_WCS_LOCAL_OP_ERR;
		break;
	case SYNDROME_LOCAL_PROT_ERR:
		entry->status = IB_WCS_LOCAL_PROTECTION_ERR;
		break;
	case SYNDROME_WR_FLUSH_ERR:
		entry->status = IB_WCS_WR_FLUSHED_ERR;
		break;
	case SYNDROME_MW_BIND_ERR:
		entry->status = IB_WCS_MEM_WINDOW_BIND_ERR;
		break;
	case SYNDROME_BAD_RESP_ERR:
		entry->status = IB_WCS_BAD_RESP_ERR;
		break;
	case SYNDROME_LOCAL_ACCESS_ERR:
		entry->status = IB_WCS_LOCAL_ACCESS_ERR;
		break;
	case SYNDROME_REMOTE_INVAL_REQ_ERR:
		entry->status = IB_WCS_REM_INVALID_REQ_ERR;
		break;
	case SYNDROME_REMOTE_ACCESS_ERR:
		entry->status = IB_WCS_REM_ACCESS_ERR;
		break;
	case SYNDROME_REMOTE_OP_ERR:
		entry->status = IB_WCS_REM_OP_ERR;
		break;
	case SYNDROME_RETRY_EXC_ERR:
		entry->status = IB_WCS_TIMEOUT_RETRY_ERR;
		break;
	case SYNDROME_RNR_RETRY_EXC_ERR:
		entry->status = IB_WCS_RNR_RETRY_ERR;
		break;
	case SYNDROME_LOCAL_EEC_OP_ERR:
	case SYNDROME_LOCAL_RDD_VIOL_ERR:
	case SYNDROME_REMOTE_INVAL_RD_REQ_ERR:
	case SYNDROME_REMOTE_ABORTED_ERR:
	case SYNDROME_INVAL_EECN_ERR:
	case SYNDROME_INVAL_EEC_STATE_ERR:
	default:
		entry->status = IB_WCS_GENERAL_ERR;
		break;
	}

	entry->vendor_specific = cqe->vendor_err;
	
	/*
	 * Mem-free HCAs always generate one CQE per WQE, even in the
	 * error case, so we don't have to check the doorbell count, etc.
	 */
	if (mthca_is_memfree(cq->ibv_cq.context))
		return 0;

	err = mthca_free_err_wqe(qp, is_send, wqe_index, &dbd, &new_wqe);
	if (err)
		return err;

	/*
	 * If we're at the end of the WQE chain, or we've used up our
	 * doorbell count, free the CQE.  Otherwise just update it for
	 * the next poll operation.
	 * 
	 * This doesn't apply to mem-free HCAs, which never use the
	 * doorbell count field.  In that case we always free the CQE.
	 */
	if (mthca_is_memfree(cq->ibv_cq.context) ||
	    !(new_wqe & cl_hton32(0x3f)) || (!cqe->db_cnt && dbd))
		return 0;

	cqe->db_cnt   = cl_hton16(cl_ntoh16(cqe->db_cnt) - dbd);
	cqe->wqe      = new_wqe;
	cqe->syndrome = SYNDROME_WR_FLUSH_ERR;

	*free_cqe = 0;

	return 0;
}

static inline int mthca_poll_one(struct mthca_cq *cq,
				 struct mthca_qp **cur_qp,
				 int *freed,
				 struct _ib_wc *entry)
{
	struct mthca_wq *wq;
	struct mthca_cqe *cqe;
	uint32_t qpn;
	int wqe_index;
	int is_error;
	int is_send;
	int free_cqe = 1;
	int err = 0;

	UVP_ENTER(UVP_DBG_CQ);
	
	cqe = next_cqe_sw(cq);
	if (!cqe)
		return -EAGAIN;

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	{ // debug print
		UVP_PRINT(TRACE_LEVEL_VERBOSE,UVP_DBG_CQ,("%x/%d: CQE -> QPN %06x, WQE @ %08x\n",
			  cq->cqn, cq->cons_index, cl_ntoh32(cqe->my_qpn),
			  cl_ntoh32(cqe->wqe)));
		dump_cqe(TRACE_LEVEL_VERBOSE,cqe);
	}
	
	qpn = cl_ntoh32(cqe->my_qpn);

	is_error = (cqe->opcode & MTHCA_ERROR_CQE_OPCODE_MASK) ==
		MTHCA_ERROR_CQE_OPCODE_MASK;
	is_send  = is_error ? cqe->opcode & 0x01 : cqe->is_send & 0x80;

	if (!*cur_qp || cl_ntoh32(cqe->my_qpn) != (*cur_qp)->ibv_qp.qp_num) {
		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		*cur_qp = mthca_find_qp(to_mctx(cq->ibv_cq.context), cl_ntoh32(cqe->my_qpn));
		if (!*cur_qp) {
			UVP_PRINT(TRACE_LEVEL_WARNING,UVP_DBG_CQ, ("CQ entry for unknown QP %06x\n",
				   cl_ntoh32(cqe->my_qpn) & 0xffffff));
			err = -EINVAL;
			goto out;
		}
	}

	if (is_send) {
		wq = &(*cur_qp)->sq;
		wqe_index = ((cl_ntoh32(cqe->wqe) - (*cur_qp)->send_wqe_offset) >> wq->wqe_shift);
		entry->wr_id = (*cur_qp)->wrid[wqe_index + (*cur_qp)->rq.max];
	} else if ((*cur_qp)->ibv_qp.srq) {
		struct mthca_srq * srq = to_msrq((*cur_qp)->ibv_qp.srq);
		uint32_t wqe = cl_hton32(cqe->wqe);
		wq = NULL;
		wqe_index = wqe >> srq->wqe_shift;
		entry->wr_id = srq->wrid[wqe_index];
		mthca_free_srq_wqe(srq, wqe_index);
	} else {
		wq = &(*cur_qp)->rq;
		wqe_index = cl_ntoh32(cqe->wqe) >> wq->wqe_shift;
		entry->wr_id = (*cur_qp)->wrid[wqe_index];
	}

	if (wq) {
		if ((int)wq->last_comp < wqe_index)
			wq->tail += wqe_index - wq->last_comp;
		else
			wq->tail += wqe_index + wq->max - wq->last_comp;

		wq->last_comp = wqe_index;
	}

	if (is_send) {
		entry->recv.ud.recv_opt = 0;
		switch (cqe->opcode) {
		case MTHCA_OPCODE_RDMA_WRITE:
			entry->wc_type    = IB_WC_RDMA_WRITE;
			break;
		case MTHCA_OPCODE_RDMA_WRITE_IMM:
			entry->wc_type    = IB_WC_RDMA_WRITE;
			entry->recv.ud.recv_opt |= IB_RECV_OPT_IMMEDIATE;
			break;
		case MTHCA_OPCODE_SEND:
			entry->wc_type    = IB_WC_SEND;
			break;
		case MTHCA_OPCODE_SEND_IMM:
			entry->wc_type    = IB_WC_SEND;
			entry->recv.ud.recv_opt |= IB_RECV_OPT_IMMEDIATE;
			break;
		case MTHCA_OPCODE_RDMA_READ:
			entry->wc_type    = IB_WC_RDMA_READ;
			entry->length  = cl_ntoh32(cqe->byte_cnt);
			break;
		case MTHCA_OPCODE_ATOMIC_CS:
			entry->wc_type    = IB_WC_COMPARE_SWAP;
			entry->length  = MTHCA_BYTES_PER_ATOMIC_COMPL;
			break;
		case MTHCA_OPCODE_ATOMIC_FA:
			entry->wc_type    = IB_WC_FETCH_ADD;
			entry->length  = MTHCA_BYTES_PER_ATOMIC_COMPL;
			break;
		case MTHCA_OPCODE_BIND_MW:
			entry->wc_type    = IB_WC_MW_BIND;
			break;
		default:
			/* assume it's a send completion */
			entry->wc_type    = IB_WC_SEND;
			break;
		}
	} else {
		entry->length = cl_ntoh32(cqe->byte_cnt);
		switch (cqe->opcode & 0x1f) {
		case IBV_OPCODE_SEND_LAST_WITH_IMMEDIATE:
		case IBV_OPCODE_SEND_ONLY_WITH_IMMEDIATE:
			entry->recv.ud.recv_opt  = IB_RECV_OPT_IMMEDIATE;
			entry->recv.ud.immediate_data = cqe->imm_etype_pkey_eec;
			entry->wc_type = IB_WC_RECV;
			break;
		case IBV_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE:
		case IBV_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE:
			entry->recv.ud.recv_opt  = IB_RECV_OPT_IMMEDIATE;
			entry->recv.ud.immediate_data = cqe->imm_etype_pkey_eec;
			entry->wc_type = IB_WC_RECV;
			break;
		default:
			entry->recv.ud.recv_opt  = 0;
			entry->wc_type = IB_WC_RECV;
			break;
		}
		entry->recv.ud.remote_lid = cqe->rlid;
		entry->recv.ud.remote_qp = cqe->rqpn & 0xffffff00;
		entry->recv.ud.pkey_index     = (uint16_t)(cl_ntoh32(cqe->imm_etype_pkey_eec) >> 16);
		entry->recv.ud.remote_sl   	   = cl_ntoh16(cqe->sl_g_mlpath) >> 12;
		entry->recv.ud.path_bits = cl_ntoh16(cqe->sl_g_mlpath) & 0x7f;
		entry->recv.ud.recv_opt      |= cl_ntoh16(cqe->sl_g_mlpath) & 0x80 ?
			IB_RECV_OPT_GRH_VALID : 0;
	}


	if (is_error) {
		err = handle_error_cqe(cq, *cur_qp, wqe_index, is_send,
				       (struct mthca_err_cqe *) cqe,
				       entry, &free_cqe);
	}
	else
		entry->status = IB_WCS_SUCCESS;

out:
	if (likely(free_cqe)) {
		set_cqe_hw(cqe);
		++(*freed);
		++cq->cons_index;
	}

	UVP_EXIT(UVP_DBG_CQ);
	return err;
}

int mthca_poll_cq(struct ibv_cq *ibcq, int num_entries, struct _uvp_wc *entry)
{
	struct mthca_cq *cq = to_mcq(ibcq);
	struct mthca_qp *qp = NULL;
	int err = CQ_OK;
	int freed = 0;
	int npolled;
	
	cl_spinlock_acquire(&cq->lock);

	for (npolled = 0; npolled < num_entries; ++npolled) {
		err = mthca_poll_one(cq, &qp, &freed, (struct _ib_wc *) (entry + npolled));
		if (err)
			break;
		entry[npolled].qp_context = qp->ibv_qp.qp_context;
	}

	if (freed) {
		wmb();
		update_cons_index(cq, freed);
	}

	cl_spinlock_release(&cq->lock);

	return (err == 0 || err == -EAGAIN) ? npolled : err;
}

int mthca_poll_cq_list(
	IN		struct ibv_cq *ibcq, 
	IN	OUT			struct _ib_wc** const				pp_free_wclist,
		OUT			struct _ib_wc** const				pp_done_wclist )
{
	struct mthca_cq *cq = to_mcq(ibcq);
	struct mthca_qp *qp = NULL;
	int err = CQ_OK;
	int freed = 0;
	ib_wc_t		*wc_p, **next_pp;
	uint32_t	wc_cnt = 0;

	cl_spinlock_acquire(&cq->lock);

	// loop through CQ
	next_pp = pp_done_wclist;
	wc_p = *pp_free_wclist;
	while( wc_p ) {
		// poll one CQE
		err = mthca_poll_one(cq, &qp, &freed, wc_p);
		if (err)
			break;

		// prepare for the next loop
		*next_pp = wc_p;
		next_pp = &wc_p->p_next;
		wc_p = wc_p->p_next;
	}

	// prepare the results
	*pp_free_wclist = wc_p;		/* Set the head of the free list. */
	*next_pp = NULL;						/* Clear the tail of the done list. */

	// update consumer index
	if (freed) {
		wmb();
		update_cons_index(cq, freed);
	}

	cl_spinlock_release(&cq->lock);
	return (err == 0 || err == -EAGAIN)? 0 : err; 
}

int mthca_tavor_arm_cq(struct ibv_cq *cq, enum ib_cq_notify notify)
{
	uint32_t doorbell[2];

	doorbell[0] = cl_hton32((notify == IB_CQ_SOLICITED ?
			     MTHCA_TAVOR_CQ_DB_REQ_NOT_SOL :
			     MTHCA_TAVOR_CQ_DB_REQ_NOT)      |
			    to_mcq(cq)->cqn);
	doorbell[1] = 0xffffffff;

	mthca_write64(doorbell, to_mctx(cq->context), MTHCA_CQ_DOORBELL);

	return 0;
}

int mthca_arbel_arm_cq(struct ibv_cq *ibvcq, enum ib_cq_notify notify)
{
	struct mthca_cq *cq = to_mcq(ibvcq);
	uint32_t doorbell[2];
	uint32_t sn;
	uint32_t ci;

	sn = *cq->p_u_arm_sn & 3;
	ci = cl_hton32(cq->cons_index);

	doorbell[0] = ci;
	doorbell[1] = cl_hton32((cq->cqn << 8) | (2 << 5) | (sn << 3) |
			    (notify == IB_CQ_SOLICITED ? 1 : 2));

	mthca_write_db_rec(doorbell, cq->arm_db);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	doorbell[0] = cl_hton32((sn << 28)                       |
			    (notify == IB_CQ_SOLICITED ?
			     MTHCA_ARBEL_CQ_DB_REQ_NOT_SOL :
			     MTHCA_ARBEL_CQ_DB_REQ_NOT)      |
			    cq->cqn);
	doorbell[1] = ci;

	mthca_write64(doorbell, to_mctx(ibvcq->context), MTHCA_CQ_DOORBELL);

	return 0;
}

static inline int is_recv_cqe(struct mthca_cqe *cqe)
{
	if ((cqe->opcode & MTHCA_ERROR_CQE_OPCODE_MASK) ==
	    MTHCA_ERROR_CQE_OPCODE_MASK)
		return !(cqe->opcode & 0x01);
	else
		return !(cqe->is_send & 0x80);
}

void mthca_cq_clean(struct mthca_cq *cq, uint32_t qpn, struct mthca_srq *srq)
{
	struct mthca_cqe *cqe;
	uint32_t prod_index;
	int nfreed = 0;

	cl_spinlock_acquire(&cq->lock);

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->cons_index;
	     cqe_sw(cq, prod_index & cq->ibv_cq.cqe);
	     ++prod_index)
		if (prod_index == cq->cons_index + cq->ibv_cq.cqe)
			break;

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibv_cq.cqe);
		if (cqe->my_qpn == cl_hton32(qpn)) {
			if (srq && is_recv_cqe(cqe))
				mthca_free_srq_wqe(srq,
						   cl_ntoh32(cqe->wqe) >> srq->wqe_shift);
			++nfreed;
		} else if (nfreed)
			memcpy(get_cqe(cq, (prod_index + nfreed) & cq->ibv_cq.cqe),
			       cqe, MTHCA_CQ_ENTRY_SIZE);
	}

	if (nfreed) {
		mb();
		cq->cons_index += nfreed;
		update_cons_index(cq, nfreed);
	}

	cl_spinlock_release(&cq->lock);
}

void mthca_init_cq_buf(struct mthca_cq *cq, int nent)
{
	int i;

	for (i = 0; i < nent; ++i)
		set_cqe_hw(get_cqe(cq, i));

	cq->cons_index = 0;
}
