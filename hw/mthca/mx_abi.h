/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 PathScale, Inc.  All rights reserved.
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

#ifndef MX_ABI_H
#define MX_ABI_H

#include <complib/cl_types_osd.h>

/*
 * Make sure that all structs defined in this file remain laid out so
 * that they pack the same way on 32-bit and 64-bit architectures (to
 * avoid incompatibility between 32-bit userspace and 64-bit kernels).
 * Specifically:
 *  - Do not use pointer types -- pass pointers in uint64_t instead.
 *  - Make sure that any structure larger than 4 bytes is padded to a
 *    multiple of 8 bytes.  Otherwise the structure size will be
 *    different between 32-bit and 64-bit architectures.
 */

struct ibv_get_context_resp {
	uint64_t uar_addr;
	uint64_t pd_handle;
	uint32_t pdn;
	uint32_t qp_tab_size;
	uint32_t uarc_size;
	uint32_t vend_id;
	uint16_t dev_id;
	uint16_t reserved[3];
};

struct ibv_alloc_pd_resp {
	uint64_t pd_handle;
	uint32_t pdn;
	uint32_t reserved;
};

struct ibv_reg_mr {
	uint64_t start;
	uint64_t length;
	uint64_t hca_va;
	uint32_t access_flags;
	uint32_t pdn;
	uint64_t pd_handle;
};

struct ibv_reg_mr_resp {
	uint64_t mr_handle;
	uint32_t lkey;
	uint32_t rkey;
};

struct ibv_create_cq {
	struct ibv_reg_mr mr;	
	uint64_t arm_db_page;
	uint64_t set_db_page;
	uint64_t u_arm_db_page;
	uint64_t user_handle;
	uint32_t arm_db_index;
	uint32_t set_db_index;
	uint32_t u_arm_db_index;
	uint32_t cqe;
	uint32_t lkey;		/* used only by kernel */
	uint32_t reserved;
};

struct ibv_create_cq_resp {
	uint64_t user_handle;
	uint64_t cq_handle;
	struct ibv_reg_mr_resp mr;
	uint32_t cqe;
	uint32_t cqn;
};

struct ibv_create_srq {
	uint64_t user_handle;
	struct ibv_reg_mr mr;
	uint32_t lkey;	/* used only in kernel */
	uint32_t db_index;
	uint64_t db_page;
};

struct ibv_create_srq_resp {
	struct ibv_reg_mr_resp mr;
	uint64_t srq_handle;
	uint64_t user_handle;
	uint32_t max_wr;
	uint32_t max_sge;
	uint32_t srqn;
	uint32_t reserved;
};

struct ibv_create_qp {
	uint64_t sq_db_page;
	uint64_t rq_db_page;
	uint32_t sq_db_index;
	uint32_t rq_db_index;
	struct ibv_reg_mr mr;
	uint64_t user_handle;
	uint64_t send_cq_handle;
	uint64_t recv_cq_handle;
	uint64_t srq_handle;
	uint32_t max_send_wr;
	uint32_t max_recv_wr;
	uint32_t max_send_sge;
	uint32_t max_recv_sge;
	uint32_t max_inline_data;
	uint32_t lkey;	/* used only in kernel */
	uint8_t  sq_sig_all;
	uint8_t  qp_type;
	uint8_t  is_srq;
	uint8_t  reserved[5];
};

struct ibv_create_qp_resp {
	struct ibv_reg_mr_resp mr;
	uint64_t user_handle;
	uint64_t qp_handle;
	uint32_t qpn;
	uint32_t max_send_wr;
	uint32_t max_recv_wr;
	uint32_t max_send_sge;
	uint32_t max_recv_sge;
	uint32_t max_inline_data;
};

struct ibv_modify_qp_resp {
	enum ibv_qp_attr_mask attr_mask;
	uint8_t qp_state;
	uint8_t reserved[3];
};

struct ibv_create_ah {
	uint64_t user_handle;
	struct ibv_reg_mr mr;	
};

struct ibv_create_ah_resp {
	uint64_t user_handle;
	uint64_t start;
	struct ibv_reg_mr_resp mr;
};


#endif /* MX_ABI_H */

