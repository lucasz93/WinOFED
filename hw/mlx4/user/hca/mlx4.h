/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
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

#ifndef MLX4_H
#define MLX4_H


#include <iba\ib_types.h>
#include <iba\ib_uvp.h>
#include <ndioctl.h>

#include "verbs.h"
#include "mx_abi.h"

#include "l2w.h"

#define PFX		"mlx4: "

#ifndef max
#define max(a,b) \
	({ typeof (a) _a = (a); \
	   typeof (b) _b = (b); \
	   _a > _b ? _a : _b; })
#endif

#ifndef min
#define min(a,b) \
	({ typeof (a) _a = (a); \
	   typeof (b) _b = (b); \
	   _a < _b ? _a : _b; })
#endif

enum {
	MLX4_CQ_ENTRY_SIZE		= 0x20
};

enum {
	MLX4_STAT_RATE_OFFSET		= 5
};

enum {
	MLX4_QP_TABLE_BITS		= 8,
	MLX4_QP_TABLE_SIZE		= 1 << MLX4_QP_TABLE_BITS,
	MLX4_QP_TABLE_MASK		= MLX4_QP_TABLE_SIZE - 1
};

#ifdef XRC_SUPPORT
enum {
	MLX4_XRC_SRQ_TABLE_BITS		= 8,
	MLX4_XRC_SRQ_TABLE_SIZE		= 1 << MLX4_XRC_SRQ_TABLE_BITS,
	MLX4_XRC_SRQ_TABLE_MASK		= MLX4_XRC_SRQ_TABLE_SIZE - 1
};

enum {
	MLX4_XRC_QPN_BIT		= (1 << 23)
};
#endif

enum mlx4_db_type {
	MLX4_DB_TYPE_CQ,
	MLX4_DB_TYPE_RQ,
	MLX4_NUM_DB_TYPE
};

enum mlx4_opcode_type {
	MLX4_OPCODE_NOP			= 0x00,
	MLX4_OPCODE_SEND_INVAL		= 0x01,
	MLX4_OPCODE_RDMA_WRITE		= 0x08,
	MLX4_OPCODE_RDMA_WRITE_IMM	= 0x09,
	MLX4_OPCODE_SEND		= 0x0a,
	MLX4_OPCODE_SEND_IMM		= 0x0b,
	MLX4_OPCODE_LSO			= 0x0e,
	MLX4_OPCODE_RDMA_READ		= 0x10,
	MLX4_OPCODE_ATOMIC_CS		= 0x11,
	MLX4_OPCODE_ATOMIC_FA		= 0x12,
	MLX4_OPCODE_ATOMIC_MASK_CS	= 0x14,
	MLX4_OPCODE_ATOMIC_MASK_FA	= 0x15,
	MLX4_OPCODE_BIND_MW		= 0x18,
	MLX4_OPCODE_FMR			= 0x19,
	MLX4_OPCODE_LOCAL_INVAL		= 0x1b,
	MLX4_OPCODE_CONFIG_CMD		= 0x1f,

	MLX4_RECV_OPCODE_RDMA_WRITE_IMM	= 0x00,
	MLX4_RECV_OPCODE_SEND		= 0x01,
	MLX4_RECV_OPCODE_SEND_IMM	= 0x02,
	MLX4_RECV_OPCODE_SEND_INVAL	= 0x03,

	MLX4_CQE_OPCODE_ERROR		= 0x1e,
	MLX4_CQE_OPCODE_RESIZE		= 0x16,

	MLX4_OPCODE_INVALID			= 0xff
};

struct mlx4_db_page;

struct mlx4_context {
	struct ibv_context		ibv_ctx;

	uint8_t			       *uar;
	pthread_spinlock_t		uar_lock;

	uint8_t			       *bf_page;
	uint32_t			   cqe_size;
	int				bf_buf_size;
	int				bf_offset;
	pthread_spinlock_t		bf_lock;

	struct {
		struct mlx4_qp	      **table;
		int			refcnt;
	}				qp_table[MLX4_QP_TABLE_SIZE];
	pthread_mutex_t			qp_table_mutex;
	int				num_qps;
	int				qp_table_shift;
	int				qp_table_mask;
	int				max_qp_wr;
	int				max_sge;
	int				max_cqe;

#ifdef XRC_SUPPORT
	struct {
		struct mlx4_srq       **table;
		int			refcnt;
	}				xrc_srq_table[MLX4_XRC_SRQ_TABLE_SIZE];
	pthread_mutex_t			xrc_srq_table_mutex;
	int				num_xrc_srqs;
	int				xrc_srq_table_shift;
	int				xrc_srq_table_mask;
#endif

	struct mlx4_db_page	       *db_list[MLX4_NUM_DB_TYPE];
	pthread_mutex_t			db_list_mutex;
};

struct mlx4_buf {
	uint8_t			    *buf;
	int					length;
	int					entry_size;
};

struct mlx4_pd {
	struct ibv_pd			ibv_pd;
	uint32_t			pdn;
};

struct mlx4_cq {
	struct ibv_cq			ibv_cq;
	struct mlx4_buf			buf;
	pthread_spinlock_t		lock;
	uint32_t			cqn;
	uint32_t			cons_index;
	uint32_t		       *set_ci_db;
	uint32_t		       *arm_db;
	int				arm_sn;
};

struct mlx4_srq {
	struct ibv_srq			ibv_srq;
	struct mlx4_buf			buf;
	pthread_spinlock_t		lock;
	uint64_t		       *wrid;
	int				max;
	int				max_gs;
	int				wqe_shift;
	int				head;
	int				tail;
	uint32_t		       *db;
	uint16_t			counter;
};

struct mlx4_wq {
	uint64_t		       *wrid;
	pthread_spinlock_t		lock;
	int				wqe_cnt;
	int				max_post;
	unsigned			head;
	unsigned			tail;
	int				max_gs;
	int				wqe_shift;
	int				offset;
};

struct mlx4_qp {
	struct ibv_qp			ibv_qp;
	struct mlx4_buf			buf;
	int				max_inline_data;
	int				buf_size;

	uint32_t			doorbell_qpn;
	uint32_t			sq_signal_bits;
	int				sq_spare_wqes;
	struct mlx4_wq			sq;

	uint32_t		       *db;
	struct mlx4_wq			rq;
};

struct mlx4_av {
	uint32_t			port_pd;
	uint8_t				reserved1;
	uint8_t				g_slid;
	uint16_t			dlid;
	uint8_t				reserved2;
	uint8_t				gid_index;
	uint8_t				stat_rate;
	uint8_t				hop_limit;
	uint32_t			sl_tclass_flowlabel;
	uint8_t				dgid[16];
};

struct mlx4_eth_av 
{	
	uint32_t			port_pd;	
	uint8_t				reserved1;	
	uint8_t				smac_idx;	
	uint16_t			reserved2;	
	uint8_t				reserved3;	
	uint8_t				gid_index;	
	uint8_t				stat_rate;	
	uint8_t				hop_limit;	
	uint32_t			sl_tclass_flowlabel;	
	uint8_t				dgid[16];	
	uint32_t			reserved4[2];	
	uint16_t			vlan;	
	uint8_t				mac_0_1[2];	
	uint8_t				mac_2_5[4];
};

union mlx4_ext_av {
	struct mlx4_av		ib;
	struct mlx4_eth_av	eth;
};

struct mlx4_ah {
	struct ibv_ah			ibv_ah;
	union  mlx4_ext_av		av;
};

#ifdef XRC_SUPPORT
struct mlx4_xrc_domain {
	struct ibv_xrc_domain		ibv_xrcd;
	uint32_t			xrcdn;
};
#endif

static inline unsigned long align(unsigned long val, unsigned long align)
{
	return (val + align - 1) & ~(align - 1);
}

static inline struct mlx4_context *to_mctx(struct ibv_context *ibctx)
{
	return CONTAINING_RECORD(ibctx, struct mlx4_context, ibv_ctx);
}

static inline struct mlx4_pd *to_mpd(struct ibv_pd *ibpd)
{
	return CONTAINING_RECORD(ibpd, struct mlx4_pd, ibv_pd);
}

static inline struct mlx4_cq *to_mcq(struct ibv_cq *ibcq)
{
	return CONTAINING_RECORD(ibcq, struct mlx4_cq, ibv_cq);
}

static inline struct mlx4_srq *to_msrq(struct ibv_srq *ibsrq)
{
	return CONTAINING_RECORD(ibsrq, struct mlx4_srq, ibv_srq);
}

static inline struct mlx4_qp *to_mqp(struct ibv_qp *ibqp)
{
	return CONTAINING_RECORD(ibqp, struct mlx4_qp, ibv_qp);
}

static inline struct mlx4_ah *to_mah(struct ibv_ah *ibah)
{
	return CONTAINING_RECORD(ibah, struct mlx4_ah, ibv_ah);
}

#ifdef XRC_SUPPORT
static inline struct mlx4_xrc_domain *to_mxrcd(struct ibv_xrc_domain *ibxrcd)
{
	return CONTAINING_RECORD(ibxrcd, struct mlx4_xrcd, ibv_xrcd);
}
#endif

struct ibv_context * mlx4_alloc_context();
struct ibv_context * mlx4_fill_context(struct ibv_context *ctx,
										struct ibv_get_context_resp *resp_p);
void mlx4_free_context(struct ibv_context *ctx);

int mlx4_alloc_buf(struct mlx4_buf *buf, int size, int page_size);
void mlx4_free_buf(struct mlx4_buf *buf);

uint32_t *mlx4_alloc_db(struct mlx4_context *context, enum mlx4_db_type type);
void mlx4_free_db(struct mlx4_context *context, enum mlx4_db_type type, uint32_t *db);

int mlx4_poll_cq_array(const void* h_cq,
			const int num_entries, uvp_wc_t* const wc);
ib_api_status_t mlx4_poll_cq_list(const void* h_cq,
			ib_wc_t** const pp_free_wclist,
			ib_wc_t** const pp_done_wclist);
ib_api_status_t mlx4_arm_cq(const	void* h_cq, const	boolean_t solicited);
void mlx4_cq_event(struct ibv_cq *cq);
void mlx4_cq_clean(struct mlx4_cq *cq, uint32_t qpn,
		    struct mlx4_srq *srq);
void mlx4_cq_resize_copy_cqes(struct mlx4_cq *cq, void *buf, int new_cqe);

int mlx4_alloc_srq_buf(struct ibv_pd *pd, struct ibv_srq_attr *attr,
			struct mlx4_srq *srq);
void mlx4_free_srq_wqe(struct mlx4_srq *srq, int ind);
ib_api_status_t mlx4_post_srq_recv(const void* h_srq,
			ib_recv_wr_t* const wr,
			ib_recv_wr_t** bad_wr);

#ifdef XRC_SUPPORT
struct mlx4_srq *mlx4_find_xrc_srq(struct mlx4_context *ctx, uint32_t xrc_srqn);
int mlx4_store_xrc_srq(struct mlx4_context *ctx, uint32_t xrc_srqn,
		       struct mlx4_srq *srq);
void mlx4_clear_xrc_srq(struct mlx4_context *ctx, uint32_t xrc_srqn);
#endif

void mlx4_init_qp_indices(struct mlx4_qp *qp);
void mlx4_qp_init_sq_ownership(struct mlx4_qp *qp);
ib_api_status_t mlx4_post_send(const void* h_qp,
			ib_send_wr_t* const wr,
			ib_send_wr_t** bad_wr);
ib_api_status_t mlx4_post_recv(const void* h_qp,
			ib_recv_wr_t* const wr,
			ib_recv_wr_t** bad_wr);

void mlx4_calc_sq_wqe_size(struct ibv_qp_cap *cap, enum ibv_qp_type type,
			   struct mlx4_qp *qp);
int mlx4_alloc_qp_buf(struct ibv_pd *pd, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type, struct mlx4_qp *qp);
void mlx4_set_sq_sizes(struct mlx4_qp *qp, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type);
struct mlx4_qp *mlx4_find_qp(struct mlx4_context *ctx, uint32_t qpn);
int mlx4_store_qp(struct mlx4_context *ctx, uint32_t qpn, struct mlx4_qp *qp);
void mlx4_clear_qp(struct mlx4_context *ctx, uint32_t qpn);

#endif /* MLX4_H */
