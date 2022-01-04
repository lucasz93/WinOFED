/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
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

#ifndef MTHCA_H
#define MTHCA_H

#include <cl_spinlock.h>
#include <mlnx_uvp_verbs.h>
#include <arch.h>
#include "mlnx_uvp_debug.h"

#define PFX		"mthca: "

enum mthca_hca_type {
	MTHCA_TAVOR,
	MTHCA_ARBEL,
	MTHCA_LIVEFISH
};

enum {
	MTHCA_CQ_ENTRY_SIZE = 0x20,
	MTHCA_BYTES_PER_ATOMIC_COMPL = 0x8
};

enum {
	MTHCA_QP_TABLE_BITS = 8,
	MTHCA_QP_TABLE_SIZE = 1 << MTHCA_QP_TABLE_BITS,
	MTHCA_QP_TABLE_MASK = MTHCA_QP_TABLE_SIZE - 1
};

enum {
	MTHCA_DB_REC_PAGE_SIZE = 4096,
	MTHCA_DB_REC_PER_PAGE  = MTHCA_DB_REC_PAGE_SIZE / 8
};

enum mthca_db_type {
	MTHCA_DB_TYPE_INVALID   = 0x0,
	MTHCA_DB_TYPE_CQ_SET_CI = 0x1,
	MTHCA_DB_TYPE_CQ_ARM    = 0x2,
	MTHCA_DB_TYPE_SQ        = 0x3,
	MTHCA_DB_TYPE_RQ        = 0x4,
	MTHCA_DB_TYPE_SRQ       = 0x5,
	MTHCA_DB_TYPE_GROUP_SEP = 0x7
};

enum mthca_wr_opcode {
	MTHCA_OPCODE_NOP            = 0x00,
	MTHCA_OPCODE_RDMA_WRITE     = 0x08,
	MTHCA_OPCODE_RDMA_WRITE_IMM = 0x09,
	MTHCA_OPCODE_SEND           = 0x0a,
	MTHCA_OPCODE_SEND_IMM       = 0x0b,
	MTHCA_OPCODE_RDMA_READ      = 0x10,
	MTHCA_OPCODE_ATOMIC_CS      = 0x11,
	MTHCA_OPCODE_ATOMIC_FA      = 0x12,
	MTHCA_OPCODE_BIND_MW        = 0x18,
	MTHCA_OPCODE_INVALID        = 0xff
};

struct mthca_ah_page;

struct mthca_db_table;

struct mthca_context {
	struct ibv_context     ibv_ctx;
	void                  *uar;
	cl_spinlock_t     uar_lock;
	struct mthca_db_table *db_tab;
	struct ibv_pd         *pd;
	struct {
		struct mthca_qp	**table;
		int		  refcnt;
	}		       qp_table[MTHCA_QP_TABLE_SIZE];
	HANDLE        qp_table_mutex;
	int                    num_qps;
	int		       qp_table_shift;
	int		       qp_table_mask;
	enum mthca_hca_type hca_type;
};

struct mthca_pd {
	struct ibv_pd         ibv_pd;
	struct mthca_ah_page *ah_list;
	HANDLE       ah_mutex;
	uint32_t              pdn;
};

struct mthca_cq {
	struct ibv_cq  	   ibv_cq;
	void           	  *buf;
	cl_spinlock_t lock;
	struct ibv_mr  	  mr;
	uint32_t       	   cqn;
	uint32_t       	   cons_index;

	/* Next fields are mem-free only */
	int                set_ci_db_index;
	uint32_t          *set_ci_db;
	int                arm_db_index;
	uint32_t          *arm_db;
	int                u_arm_db_index;
	volatile uint32_t *p_u_arm_sn;
};

struct mthca_srq {
	struct ibv_srq     ibv_srq;
	void              *buf;
	void           	  *last;
	cl_spinlock_t lock;
	struct ibv_mr 	  mr;
	uint64_t      	  *wrid;
	uint32_t       	   srqn;
	int            	   max;
	int            	   max_gs;
	int            	   wqe_shift;
	int            	   first_free;
	int            	   last_free;
	int                buf_size;

	/* Next fields are mem-free only */
	int           	   db_index;
	uint32_t      	  *db;
	uint16_t      	   counter;
};

struct mthca_wq {
	cl_spinlock_t lock;
	int            	   max;
	unsigned       	   next_ind;
	unsigned       	   last_comp;
	unsigned       	   head;
	unsigned       	   tail;
	void           	  *last;
	int            	   max_gs;
	int            	   wqe_shift;

	/* Next fields are mem-free only */
	int                db_index;
	uint32_t          *db;
};

struct mthca_qp {
	struct ibv_qp    ibv_qp;
	uint8_t            *buf;
	uint64_t        *wrid;
	int              send_wqe_offset;
	int              max_inline_data;
	int              buf_size;
	struct mthca_wq  sq;
	struct mthca_wq  rq;
	struct ibv_mr   mr;
	int              sq_sig_all;
};

struct mthca_av {
	uint32_t port_pd;
	uint8_t  reserved1;
	uint8_t  g_slid;
	uint16_t dlid;
	uint8_t  reserved2;
	uint8_t  gid_index;
	uint8_t  msg_sr;
	uint8_t  hop_limit;
	uint32_t sl_tclass_flowlabel;
	uint32_t dgid[4];
};

struct mthca_ah {
	struct mthca_av      *av;
	ib_av_attr_t		av_attr;
	ib_pd_handle_t	h_uvp_pd;
	struct mthca_ah_page *page;
	uint32_t              key;
	int	in_kernel;
};

#pragma warning( disable : 4200)
struct mthca_ah_page {
	struct mthca_ah_page *prev, *next;
	void           	     *buf;
	struct ibv_mr 	     mr;
	int           	      use_cnt;
	unsigned      	      free[0];
};
#pragma warning( default  : 4200)


static inline uintptr_t db_align(uint32_t *db)
{
	return (uintptr_t) db & ~((uintptr_t) MTHCA_DB_REC_PAGE_SIZE - 1);
}

#define to_mxxx(xxx, type)						\
	((struct mthca_##type *)					\
	 ((uint8_t *) ib##xxx - offsetof(struct mthca_##type, ibv_##xxx)))

static inline struct mthca_context *to_mctx(struct ibv_context *ibctx)
{
	return to_mxxx(ctx, context);
}

static inline struct mthca_pd *to_mpd(struct ibv_pd *ibpd)
{
	return to_mxxx(pd, pd);
}

static inline struct mthca_cq *to_mcq(struct ibv_cq *ibcq)
{
	return to_mxxx(cq, cq);
}

static inline struct mthca_srq *to_msrq(struct ibv_srq *ibsrq)
{
	return to_mxxx(srq, srq);
}

static inline struct mthca_qp *to_mqp(struct ibv_qp *ibqp)
{
	return to_mxxx(qp, qp);
}

static inline int mthca_is_memfree(struct ibv_context *ibctx)
{
	return to_mctx(ibctx)->hca_type == MTHCA_ARBEL;
}

int mthca_alloc_db(struct mthca_db_table *db_tab, enum mthca_db_type type,
			  volatile uint32_t **db);
void mthca_set_db_qn(uint32_t *db, enum mthca_db_type type, uint32_t qn);
void mthca_free_db(struct mthca_db_table *db_tab, enum mthca_db_type type, int db_index);
struct mthca_db_table *mthca_alloc_db_tab(int uarc_size);
void mthca_free_db_tab(struct mthca_db_table *db_tab);

int mthca_query_device(struct ibv_context *context,
			      struct ibv_device_attr *attr);
int mthca_query_port(struct ibv_context *context, uint8_t port,
			    struct ibv_port_attr *attr);

	struct ibv_pd *mthca_alloc_pd(struct ibv_context *context, 
	struct ibv_alloc_pd_resp *resp_p);

int mthca_free_pd(struct ibv_pd *pd);

struct ibv_cq *mthca_create_cq_pre(struct ibv_context *context, int *cqe,
				 struct ibv_create_cq *req);
struct ibv_cq *mthca_create_cq_post(struct ibv_context *context, 
				 struct ibv_create_cq_resp *resp);
int mthca_destroy_cq(struct ibv_cq *cq);
int mthca_poll_cq(struct ibv_cq *cq, int ne, struct _uvp_wc *wc);
int mthca_poll_cq_list(struct ibv_cq *ibcq, 
	struct _ib_wc** const pp_free_wclist,
	struct _ib_wc** const pp_done_wclist );
int mthca_tavor_arm_cq(struct ibv_cq *cq, int solicited);
int mthca_arbel_arm_cq(struct ibv_cq *cq, int solicited);
void mthca_cq_clean(struct mthca_cq *cq, uint32_t qpn,
			   struct mthca_srq *srq);
void mthca_init_cq_buf(struct mthca_cq *cq, int nent);

struct ibv_srq *mthca_create_srq(struct ibv_pd *pd,
					struct ibv_srq_init_attr *attr);
int mthca_modify_srq(struct ibv_srq *srq,
			    struct ibv_srq_attr *attr,
			    enum ibv_srq_attr_mask mask);
int mthca_destroy_srq(struct ibv_srq *srq);
int mthca_alloc_srq_buf(struct ibv_pd *pd, struct ibv_srq_attr *attr,
			       struct mthca_srq *srq);
void mthca_free_srq_wqe(struct mthca_srq *srq, int ind);
int mthca_tavor_post_srq_recv(struct ibv_srq *ibsrq,
				     struct _ib_recv_wr *wr,
				     struct _ib_recv_wr **bad_wr);
int mthca_arbel_post_srq_recv(struct ibv_srq *ibsrq,
				     struct _ib_recv_wr *wr,
				     struct _ib_recv_wr **bad_wr);
struct ibv_qp *mthca_create_qp_pre(struct ibv_pd *pd, 
	struct ibv_qp_init_attr *attr, struct ibv_create_qp *req);
struct ibv_qp *mthca_create_qp_post(struct ibv_pd *pd, 
	struct ibv_create_qp_resp *resp);
int mthca_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
			   enum ibv_qp_attr_mask attr_mask);
void mthca_destroy_qp_pre(struct ibv_qp *qp);
void mthca_destroy_qp_post(struct ibv_qp *qp, int ret);
void mthca_init_qp_indices(struct mthca_qp *qp);
int mthca_tavor_post_send(struct ibv_qp *ibqp, struct _ib_send_wr *wr,
				 struct _ib_send_wr **bad_wr);
int mthca_tavor_post_recv(struct ibv_qp *ibqp, struct _ib_recv_wr *wr,
				 struct _ib_recv_wr **bad_wr);
int mthca_arbel_post_send(struct ibv_qp *ibqp, struct _ib_send_wr *wr,
				 struct _ib_send_wr **bad_wr);
int mthca_arbel_post_recv(struct ibv_qp *ibqp, struct _ib_recv_wr *wr,
				 struct _ib_recv_wr **bad_wr);
int mthca_alloc_qp_buf(struct ibv_pd *pd, struct ibv_qp_cap *cap,
			      ib_qp_type_t type, struct mthca_qp *qp);
struct mthca_qp *mthca_find_qp(struct mthca_context *ctx, uint32_t qpn);
int mthca_store_qp(struct mthca_context *ctx, uint32_t qpn, struct mthca_qp *qp);
void mthca_clear_qp(struct mthca_context *ctx, uint32_t qpn);
int mthca_free_err_wqe(struct mthca_qp *qp, int is_send,
			      int index, int *dbd, uint32_t *new_wqe);
int mthca_alloc_av(struct mthca_pd *pd, struct ibv_ah_attr *attr,
			  struct mthca_ah *ah, struct ibv_create_ah_resp *resp);
void mthca_free_av(struct mthca_ah *ah);
int mthca_attach_mcast(struct ibv_qp *qp, union ibv_gid *gid, uint16_t lid);
int mthca_detach_mcast(struct ibv_qp *qp, union ibv_gid *gid, uint16_t lid);
struct ibv_context *mthca_alloc_context(struct ibv_get_context_resp *resp_p);
void mthca_free_context(struct ibv_context *ibctx);

#endif /* MTHCA_H */
