/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
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

#pragma once

#include "ib_verbs.h"

struct ib_mr *ibv_reg_mr(struct ib_pd *pd, 
	u64 start, u64 length,
	u64 virt_addr,
	int mr_access_flags);

struct ib_mr *ibv_reg_mdl(struct ib_pd *pd,
    MDL *mdl, u64 length,
    int mr_access_flags);

struct ib_mr *ib_alloc_fast_reg_mr(struct ib_pd *pd, int max_page_list_len);

ib_fast_reg_page_list_t *ib_alloc_fast_reg_page_list(struct ib_device *device,
			  int max_page_list_len);

struct ib_cq *ibv_create_cq(struct ib_device *p_ibdev,
			   ib_comp_handler comp_handler,
			   void (*event_handler)(ib_event_rec_t *),
			   void *cq_context, int cqe, 
			   struct ib_ucontext *p_uctx, ci_umv_buf_t* const p_umv_buf);

struct ib_cq *ibv_create_cq_ex(struct ib_device *p_ibdev,
			   ib_comp_handler comp_handler,
			   void (*event_handler)(ib_event_rec_t *),
			   void *cq_context, ib_group_affinity_t *affinity, int cqe, 
			   struct ib_ucontext *p_uctx, ci_umv_buf_t* const p_umv_buf);

struct ib_qp *ibv_create_qp(struct ib_pd *pd,
	struct ib_qp_init_attr *qp_init_attr,
	struct ib_ucontext *context, ci_umv_buf_t* const p_umv_buf);

struct ib_srq *ibv_create_srq(struct ib_pd *pd,
	struct ib_srq_init_attr *srq_init_attr,
	struct ib_ucontext *context, ci_umv_buf_t* const p_umv_buf);

ib_api_status_t ibv_um_open(	
	IN 			struct ib_device 		*	p_ibdev,
	IN			KPROCESSOR_MODE				mode,
	IN	OUT		ci_umv_buf_t* const			p_umv_buf,
	OUT			struct ib_ucontext 		**	pp_uctx );

void ibv_um_close(	struct ib_ucontext * h_um_ca );


