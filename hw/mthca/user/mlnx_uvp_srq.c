/*
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
#include "mlnx_uvp_doorbell.h"
#include "mthca_wqe.h"

#if defined(EVENT_TRACING)
#include "mlnx_uvp_srq.tmh"
#endif

static void *get_wqe(struct mthca_srq *srq, int n)
{
	return (uint8_t*)srq->buf + (n << srq->wqe_shift);
}

/*
 * Return a pointer to the location within a WQE that we're using as a
 * link when the WQE is in the free list.  We use the imm field at an
 * offset of 12 bytes because in the Tavor case, posting a WQE may
 * overwrite the next segment of the previous WQE, but a receive WQE
 * will never touch the imm field.  This avoids corrupting our free
 * list if the previous WQE has already completed and been put on the
 * free list when we post the next WQE.
 */
static inline int *wqe_to_link(void *wqe)
{
	return (int *) ((uint8_t*)wqe + 12);
}

void mthca_free_srq_wqe(struct mthca_srq *srq, int ind)
{
	cl_spinlock_acquire(&srq->lock);

	if (srq->first_free >= 0)
		*wqe_to_link(get_wqe(srq, srq->last_free)) = ind;
	else
		srq->first_free = ind;

	*wqe_to_link(get_wqe(srq, ind)) = -1;
	srq->last_free = ind;

	cl_spinlock_release(&srq->lock);
}

int mthca_tavor_post_srq_recv(struct ibv_srq *ibsrq,
			      struct _ib_recv_wr *wr,
			      struct _ib_recv_wr **bad_wr)
{
	struct mthca_srq *srq = to_msrq(ibsrq);
	uint32_t doorbell[2];
	int err = 0;
	int first_ind;
	int ind;
	int next_ind;
	int nreq;
	int i;
	uint8_t *wqe;
	uint8_t *prev_wqe;

	cl_spinlock_acquire(&srq->lock);

	first_ind = srq->first_free;

	for (nreq = 0; wr; wr = wr->p_next) {
		ind = srq->first_free;

		if (ind < 0) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_QP ,("SRQ %06x full\n", srq->srqn));
			err = -1;
			*bad_wr = wr;
			break;
		}

		wqe       = get_wqe(srq, ind);
		next_ind  = *wqe_to_link(wqe);

		if (next_ind < 0) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_QP  ,("SRQ %06x full\n", srq->srqn));
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		prev_wqe  = srq->last;
		srq->last = wqe;

		((struct mthca_next_seg *) wqe)->nda_op = 0;
		((struct mthca_next_seg *) wqe)->ee_nds = 0;
		/* flags field will always remain 0 */

		wqe += sizeof (struct mthca_next_seg);

		if (unlikely((int)wr->num_ds > srq->max_gs)) {
			err = -1;
			*bad_wr = wr;
			srq->last = prev_wqe;
			break;
		}

		for (i = 0; i < (int)wr->num_ds; ++i) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cl_hton32(wr->ds_array[i].length);
			((struct mthca_data_seg *) wqe)->lkey =
				cl_hton32(wr->ds_array[i].lkey);
			((struct mthca_data_seg *) wqe)->addr =
				htonll(wr->ds_array[i].vaddr);
			wqe += sizeof (struct mthca_data_seg);
		}

		if (i < srq->max_gs) {
			((struct mthca_data_seg *) wqe)->byte_count = 0;
			((struct mthca_data_seg *) wqe)->lkey = cl_hton32(MTHCA_INVAL_LKEY);
			((struct mthca_data_seg *) wqe)->addr = 0;
		}

		((struct mthca_next_seg *) prev_wqe)->nda_op =
			cl_hton32((ind << srq->wqe_shift) | 1);
		mb();
		((struct mthca_next_seg *) prev_wqe)->ee_nds =
			cl_hton32(MTHCA_NEXT_DBD);

		srq->wrid[ind]  = wr->wr_id;
		srq->first_free = next_ind;

		if (++nreq == MTHCA_TAVOR_MAX_WQES_PER_RECV_DB) {
			nreq = 0;
		
			doorbell[0] = cl_hton32(first_ind << srq->wqe_shift);
			doorbell[1] = cl_hton32(srq->srqn << 8);
		
			/*
			 * Make sure that descriptors are written
			 * before doorbell is rung.
			 */
			wmb();
		
			mthca_write64(doorbell, to_mctx(ibsrq->context), MTHCA_RECV_DOORBELL);
		
			first_ind = srq->first_free;
		}
	}

	if (nreq) {
		doorbell[0] = cl_hton32(first_ind << srq->wqe_shift);
		doorbell[1] = cl_hton32((srq->srqn << 8) | nreq);

		/*
		 * Make sure that descriptors are written before
		 * doorbell is rung.
		 */
		wmb();

		mthca_write64(doorbell, to_mctx(ibsrq->context), MTHCA_RECV_DOORBELL);
	}

	cl_spinlock_release(&srq->lock);
	return err;
}

int mthca_arbel_post_srq_recv(struct ibv_srq *ibsrq,
			      struct _ib_recv_wr *wr,
			      struct _ib_recv_wr **bad_wr)
{
	struct mthca_srq *srq = to_msrq(ibsrq);
	int err = 0;
	int ind;
	int next_ind;
	int nreq;
	int i;
	uint8_t *wqe;

	cl_spinlock_acquire(&srq->lock);

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		ind = srq->first_free;

		if (ind < 0) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_QP ,("SRQ %06x full\n", srq->srqn));
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		wqe       = get_wqe(srq, ind);
		next_ind  = *wqe_to_link(wqe);

		if (next_ind < 0) {
			UVP_PRINT(TRACE_LEVEL_ERROR  ,UVP_DBG_LOW  ,("SRQ %06x full\n", srq->srqn));
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		((struct mthca_next_seg *) wqe)->nda_op =
			cl_hton32((next_ind << srq->wqe_shift) | 1);
		((struct mthca_next_seg *) wqe)->ee_nds = 0;
		/* flags field will always remain 0 */

		wqe += sizeof (struct mthca_next_seg);

		if (unlikely((int)wr->num_ds > srq->max_gs)) {
			err = -1;
			*bad_wr = wr;
			break;
		}

		for (i = 0; i < (int)wr->num_ds; ++i) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cl_hton32(wr->ds_array[i].length);
			((struct mthca_data_seg *) wqe)->lkey =
				cl_hton32(wr->ds_array[i].lkey);
			((struct mthca_data_seg *) wqe)->addr =
				htonll(wr->ds_array[i].vaddr);
			wqe += sizeof (struct mthca_data_seg);
		}

		if (i < srq->max_gs) {
			((struct mthca_data_seg *) wqe)->byte_count = 0;
			((struct mthca_data_seg *) wqe)->lkey = cl_hton32(MTHCA_INVAL_LKEY);
			((struct mthca_data_seg *) wqe)->addr = 0;
		}

		srq->wrid[ind]  = wr->wr_id;
		srq->first_free = next_ind;
	}

	if (likely(nreq)) {
		srq->counter += (uint16_t)nreq;

		/*
		 * Make sure that descriptors are written before
		 * we write doorbell record.
		 */
		wmb();
		*srq->db = cl_hton32(srq->counter);
	}

	cl_spinlock_release(&srq->lock);
	return err;
}

int mthca_alloc_srq_buf(struct ibv_pd *pd, struct ibv_srq_attr *attr,
		       struct mthca_srq *srq)
{
	struct mthca_data_seg *scatter;
	uint8_t *wqe;
	int size;
	int i;

	srq->wrid = cl_malloc(srq->max * sizeof (uint64_t));
	if (!srq->wrid)
		return -1;

	size = sizeof (struct mthca_next_seg) +
		srq->max_gs * sizeof (struct mthca_data_seg);

	for (srq->wqe_shift = 6; 1 << srq->wqe_shift < size; ++srq->wqe_shift)
		; /* nothing */

	srq->buf_size = srq->max << srq->wqe_shift;

	if (posix_memalign(&srq->buf, g_page_size,
			align(srq->buf_size, g_page_size))) {
		cl_free(srq->wrid);
		return -1;
	}

	cl_memclr(srq->buf, srq->buf_size);

	/*
	 * Now initialize the SRQ buffer so that all of the WQEs are
	 * linked into the list of free WQEs.  In addition, set the
	 * scatter list L_Keys to the sentry value of 0x100.
	 */

	for (i = 0; i < srq->max; ++i) {
		wqe = get_wqe(srq, i);

		*wqe_to_link(wqe) = i < srq->max - 1 ? i + 1 : -1;

		for (scatter = (struct mthca_data_seg *)(wqe + sizeof (struct mthca_next_seg));
		     (void *) scatter < (void*)(wqe + (uint32_t)(1 << srq->wqe_shift));
		     ++scatter)
			scatter->lkey = cl_hton32(MTHCA_INVAL_LKEY);
	}

	srq->first_free = 0;
	srq->last_free  = srq->max - 1;
	srq->last       = get_wqe(srq, srq->max - 1);

	return 0;
}
