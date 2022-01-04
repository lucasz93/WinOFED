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

#include <ndioctl.h>
#include "user.h"

// {697925A7-84F0-4ED9-A9E8-76A941E72EB1}
DEFINE_GUID(GUID_MLX4_DRIVER, 
0x697925a7, 0x84f0, 0x4ed9, 0xa9, 0xe8, 0x76, 0xa9, 0x41, 0xe7, 0x2e, 0xb1);

#pragma warning( disable : 4201)

/*
 * Make sure that all structs defined in this file remain laid out so
 * that they pack the same way on 32-bit and 64-bit architectures (to
 * avoid incompatibility between 32-bit userspace and 64-bit kernels).
 * Specifically:
 *  - Do not use pointer types -- pass pointers in UINT64 instead.
 *  - Make sure that any structure larger than 4 bytes is padded to a
 *    multiple of 8 bytes.  Otherwise the structure size will be
 *    different between 32-bit and 64-bit architectures.
 */

enum ibv_get_context_mappings {
    ibv_get_context_uar,
    ibv_get_context_bf,
    ibv_get_context_mapping_max
};

struct ibv_get_context_req {

    ND_MAPPING mappings[ibv_get_context_mapping_max];
};

struct ibv_get_context_resp {

	// mmap UAR and BF
    ND_MAPPING_RESULT mapping_results[ibv_get_context_mapping_max];
	
	// mmap Blue Flame
	int bf_buf_size;
	int bf_offset;
	
	// mlx4_query_device result 
	int max_qp_wr;
	int max_sge;
	int max_cqe;

	// general parameters
	UINT32 cqe_size;
	UINT32 vend_id;
	UINT16 dev_id;
	UINT16 bf_reg_size;
	UINT16 bf_regs_per_page;
	UINT16 reserved1;

	// ibv_cmd_get_context result 
	UINT32 qp_tab_size;

	UINT32 reserved2;
};

struct ibv_alloc_pd_resp {
	UINT64 pd_handle;
	UINT32 pdn;
	UINT32 reserved;
};

struct ibv_reg_mr {
	UINT64 start;
	UINT64 length;
	UINT64 hca_va;
	UINT32 access_flags;
	UINT32 pdn;
	UINT64 pd_handle;
};

struct ibv_reg_mr_resp {
	UINT64 mr_handle;
	UINT32 lkey;
	UINT32 rkey;
};


enum mlx4_ib_create_cq_mapping {
    mlx4_ib_create_cq_buf,
    mlx4_ib_create_cq_db,
    mlx4_ib_create_cq_arm_sn,   // Windows specific
    mlx4_ib_create_cq_mapping_max
};

struct ibv_create_cq {
    ND_MAPPING mappings[mlx4_ib_create_cq_mapping_max];
};

struct ibv_create_cq_resp {
    ND_MAPPING_RESULT mapping_results[mlx4_ib_create_cq_mapping_max];
    UINT32  cqn;
    UINT32  cqe;
};

enum mlx4_ib_create_srq_mappings {
    mlx4_ib_create_srq_buf,
    mlx4_ib_create_srq_db,
    mlx4_ib_create_srq_mappings_max
};

struct ibv_create_srq {
    ND_MAPPING mappings[mlx4_ib_create_srq_mappings_max];
};

struct ibv_create_srq_resp {
    ND_MAPPING_RESULT mapping_results[mlx4_ib_create_srq_mappings_max];
};

enum mlx4_ib_create_qp_mappings {
    mlx4_ib_create_qp_buf,
    mlx4_ib_create_qp_db,
    mlx4_ib_create_qp_mappings_max
};

struct ibv_create_qp {
    ND_MAPPING mappings[mlx4_ib_create_qp_mappings_max];
    UINT8	log_sq_bb_count;
    UINT8	log_sq_stride;
    UINT8	sq_no_prefetch;
    UINT8	reserved;
};

struct ibv_create_qp_resp {
    ND_MAPPING_RESULT mapping_results[mlx4_ib_create_qp_mappings_max];
	// struct ib_uverbs_create_qp_resp
	UINT64 qp_handle;
	UINT32 qpn;
	UINT32 max_send_wr;
	UINT32 max_recv_wr;
	UINT32 max_send_sge;
	UINT32 max_recv_sge;
	UINT32 max_inline_data;
};

struct ibv_modify_qp_resp {
	enum ibv_qp_attr_mask attr_mask;
	UINT8 qp_state;
	UINT8 reserved[3];
};

struct ibv_create_ah_resp {
	UINT64 start;
};

#pragma warning( default  : 4201)

#endif /* MX_ABI_H */

