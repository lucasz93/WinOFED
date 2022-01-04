/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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

#ifndef MTHCA_PROVIDER_H
#define MTHCA_PROVIDER_H

#include <ib_verbs.h>
#include <ib_pack.h>
#include <iba/ib_ci.h>

typedef uint32_t mthca_mpt_access_t;
#define MTHCA_MPT_FLAG_ATOMIC        (1 << 14)
#define MTHCA_MPT_FLAG_REMOTE_WRITE  (1 << 13)
#define MTHCA_MPT_FLAG_REMOTE_READ   (1 << 12)
#define MTHCA_MPT_FLAG_LOCAL_WRITE   (1 << 11)
#define MTHCA_MPT_FLAG_LOCAL_READ    (1 << 10)

union mthca_buf {
	struct scatterlist direct;
	struct scatterlist *page_list;
};

struct mthca_uar {
	PFN_NUMBER pfn;
	int           index;
};

struct mthca_user_db_table;

struct mthca_ucontext {
	struct ib_ucontext          ibucontext;
	struct mthca_uar            uar;
	struct mthca_user_db_table *db_tab;
	// for user UAR 
	PMDL	mdl;
	PVOID	kva;
	SIZE_T uar_size;	
};

struct mthca_mtt;

struct mthca_mr {
	//NB: the start of this structure is to be equal to mlnx_mro_t !
	//NB: the structure was not inserted here for not to mix driver and provider structures
	struct ib_mr      ibmr;
	struct mthca_mtt *mtt;
	int			iobuf_used;
	mt_iobuf_t	iobuf;
	void *secure_handle;
};

struct mthca_fmr {
	struct ib_fmr      ibfmr;
	struct ib_fmr_attr attr;
	struct mthca_mtt  *mtt;
	int                maps;
	union {
		struct {
			struct mthca_mpt_entry __iomem *mpt;
			u64 __iomem *mtts;
		} tavor;
		struct {
			struct mthca_mpt_entry *mpt;
			__be64 *mtts;
		} arbel;
	} mem;
};

struct mthca_pd {
	struct ib_pd    ibpd;
	u32             pd_num;
	atomic_t        sqp_count;
	struct mthca_mr ntmr;
	int             privileged;
};

struct mthca_eq {
	struct mthca_dev      *dev;
	int                    eqn;
	int                    eq_num;
	u32                    eqn_mask;
	u32                    cons_index;
	u16                    msi_x_vector;
	u16                    msi_x_entry;
	int                    have_irq;
	int                    nent;
	struct scatterlist *page_list;
	struct mthca_mr        mr;
	KDPC				dpc;			/* DPC for MSI-X interrupts */
	spinlock_t  lock;			/* spinlock for simult DPCs */
};

struct mthca_av;

enum mthca_ah_type {
	MTHCA_AH_ON_HCA,
	MTHCA_AH_PCI_POOL,
	MTHCA_AH_KMALLOC
};

struct mthca_ah {
	struct ib_ah       ibah;
	enum mthca_ah_type type;
	u32                key;
	struct mthca_av   *av;
	dma_addr_t         avdma;
};

/*
 * Quick description of our CQ/QP locking scheme:
 *
 * We have one global lock that protects dev->cq/qp_table.  Each
 * struct mthca_cq/qp also has its own lock.  An individual qp lock
 * may be taken inside of an individual cq lock.  Both cqs attached to
 * a qp may be locked, with the send cq locked first.  No other
 * nesting should be done.
 *
 * Each struct mthca_cq/qp also has an atomic_t ref count.  The
 * pointer from the cq/qp_table to the struct counts as one reference.
 * This reference also is good for access through the consumer API, so
 * modifying the CQ/QP etc doesn't need to take another reference.
 * Access because of a completion being polled does need a reference.
 *
 * Finally, each struct mthca_cq/qp has a wait_queue_head_t for the
 * destroy function to sleep on.
 *
 * This means that access from the consumer API requires nothing but
 * taking the struct's lock.
 *
 * Access because of a completion event should go as follows:
 * - lock cq/qp_table and look up struct
 * - increment ref count in struct
 * - drop cq/qp_table lock
 * - lock struct, do your thing, and unlock struct
 * - decrement ref count; if zero, wake up waiters
 *
 * To destroy a CQ/QP, we can do the following:
 * - lock cq/qp_table, remove pointer, unlock cq/qp_table lock
 * - decrement ref count
 * - wait_event until ref count is zero
 *
 * It is the consumer's responsibilty to make sure that no QP
 * operations (WQE posting or state modification) are pending when the
 * QP is destroyed.  Also, the consumer must make sure that calls to
 * qp_modify are serialized.
 *
 * Possible optimizations (wait for profile data to see if/where we
 * have locks bouncing between CPUs):
 * - split cq/qp table lock into n separate (cache-aligned) locks,
 *   indexed (say) by the page in the table
 * - split QP struct lock into three (one for common info, one for the
 *   send queue and one for the receive queue)
 */
//TODO: check correctness of the above requirement: "It is the consumer's responsibilty to make sure that no QP
// operations (WQE posting or state modification) are pending when the QP is destroyed"

struct mthca_cq {
	struct ib_cq           ibcq;
	spinlock_t             lock;
	atomic_t               refcount;
	int                    cqn;
	u32                    cons_index;
	int                    is_direct;
	int                    is_kernel;

	/* Next fields are Arbel only */
	int                    set_ci_db_index;
	__be32                *set_ci_db;
	int                    arm_db_index;
	__be32                *arm_db;
	int                    arm_sn;
	int                    u_arm_db_index;
	volatile u32          *p_u_arm_sn;

	union mthca_buf        queue;
	struct mthca_mr        mr;
	wait_queue_head_t      wait;
	KMUTEX                      mutex;
};

struct mthca_srq {
	struct ib_srq		ibsrq;
	spinlock_t		lock;
	atomic_t		refcount;
	int			srqn;
	int			max;
	int			max_gs;
	int			wqe_shift;
	int			first_free;
	int			last_free;
	u16			counter;  /* Arbel only */
	int			db_index; /* Arbel only */
	__be32		       *db;       /* Arbel only */
	void		       *last;

	int			is_direct;
	u64		       *wrid;
	union mthca_buf		queue;
	struct mthca_mr		mr;

	wait_queue_head_t	wait;
	KMUTEX			mutex;
};

struct mthca_wq {
	spinlock_t lock;
	int        max;
	unsigned   next_ind;
	unsigned   last_comp;
	unsigned   head;
	unsigned   tail;
	void      *last;
	int        max_gs;
	int        wqe_shift;

	int        db_index;	/* Arbel only */
	__be32    *db;
};

struct mthca_qp {
	struct ib_qp           ibqp;
	//TODO: added just because absense of ibv_query_qp
	// thereafter it may be worth to be replaced by struct ib_qp_attr qp_attr;
	struct ib_qp_init_attr qp_init_attr;	// leo: for query_qp
	atomic_t               refcount;
	u32                    qpn;
	int                    is_direct;
	u8                     transport;
	u8                     state;
	u8                     atomic_rd_en;
	u8                     resp_depth;

	struct mthca_mr        mr;

	struct mthca_wq        rq;
	struct mthca_wq        sq;
	enum ib_sig_type       sq_policy;
	int                    send_wqe_offset;
	int                    max_inline_data;

	u64                   *wrid;
	union mthca_buf	       queue;

	wait_queue_head_t      wait;
	KMUTEX                      mutex;
};

struct mthca_sqp {
	struct mthca_qp qp;
	int             port;
	int             pkey_index;
	u32             qkey;
	u32             send_psn;
	struct ib_ud_header ud_header;
	struct scatterlist sg;
};

static inline struct mthca_ucontext *to_mucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct mthca_ucontext, ibucontext);
}

static inline struct mthca_fmr *to_mfmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct mthca_fmr, ibfmr);
}

static inline struct mthca_mr *to_mmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct mthca_mr, ibmr);
}

static inline struct mthca_pd *to_mpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct mthca_pd, ibpd);
}

static inline struct mthca_ah *to_mah(struct ib_ah *ibah)
{
	return container_of(ibah, struct mthca_ah, ibah);
}

static inline struct mthca_cq *to_mcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct mthca_cq, ibcq);
}

static inline struct mthca_srq *to_msrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct mthca_srq, ibsrq);
}

static inline struct mthca_qp *to_mqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct mthca_qp, ibqp);
}

static inline struct mthca_sqp *to_msqp(struct mthca_qp *qp)
{
	return container_of(qp, struct mthca_sqp, qp);
}

static inline uint8_t start_port(struct ib_device *device)
{
	return device->node_type == IB_NODE_SWITCH ? 0 : 1;
}

static inline uint8_t end_port(struct ib_device *device)
{
	return device->node_type == IB_NODE_SWITCH ? 0 : device->phys_port_cnt;
}

static inline int ib_copy_from_umv_buf(void *dest, ci_umv_buf_t* const p_umv_buf, size_t len)
{
	RtlCopyMemory(dest, (void*)(ULONG_PTR)p_umv_buf->p_inout_buf,  len);
	return 0;
}

static inline int ib_copy_to_umv_buf(ci_umv_buf_t* const p_umv_buf, void *src, size_t len)
{
	if (p_umv_buf->output_size < len) {
		p_umv_buf->status = IB_INSUFFICIENT_MEMORY;
		p_umv_buf->output_size = 0;
		return -EFAULT;
	}
	RtlCopyMemory((void*)(ULONG_PTR)p_umv_buf->p_inout_buf, src, len);
	p_umv_buf->status = IB_SUCCESS;
	p_umv_buf->output_size = (uint32_t)len;
	return 0;
}



// API
int mthca_query_device(struct ib_device *ibdev,
			      struct ib_device_attr *props);

int mthca_query_port(struct ib_device *ibdev,
			    u8 port, struct ib_port_attr *props);

int mthca_modify_port(struct ib_device *ibdev,
			     u8 port, int port_modify_mask,
			     struct ib_port_modify *props);

struct ib_pd *mthca_alloc_pd(struct ib_device *ibdev,
				    struct ib_ucontext *context,
				    ci_umv_buf_t* const			p_umv_buf);

int mthca_dealloc_pd(struct ib_pd *pd);

int mthca_dereg_mr(struct ib_mr *mr);

int mthca_query_srq(struct ib_srq *ibsrq, ib_srq_attr_t *srq_attr);

struct ib_ucontext *mthca_alloc_ucontext(struct ib_device *ibdev,
						ci_umv_buf_t* const			p_umv_buf);

int mthca_dealloc_ucontext(struct ib_ucontext *context);

struct ib_mr *mthca_get_dma_mr(struct ib_pd *pd, mthca_qp_access_t acc);

int mthca_poll_cq_list(
	IN		struct ib_cq *ibcq, 
	IN	OUT			ib_wc_t** const				pp_free_wclist,
		OUT			ib_wc_t** const				pp_done_wclist );


#endif /* MTHCA_PROVIDER_H */
