/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
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

#include "l2w.h"
#include "ib_verbs.h"
#include <mlx4_debug.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "device.tmh"
#endif


// qp_state_table
static struct {
	int			valid;
	enum ib_qp_attr_mask	req_param[IB_QPT_RAW_ETY + 1];
	enum ib_qp_attr_mask	opt_param[IB_QPT_RAW_ETY + 1];
} qst[XIB_QPS_ERR + 1][XIB_QPS_ERR + 1];


void init_qp_state_tbl()
{
	memset( qst, 0, sizeof(qst) );

	//
	// XIB_QPS_RESET
	//

		// XIB_QPS_RESET
		qst[XIB_QPS_RESET][XIB_QPS_RESET].valid = 1;

		// XIB_QPS_ERR
		qst[XIB_QPS_RESET][XIB_QPS_ERR].valid = 1;

		// XIB_QPS_INIT

		qst[XIB_QPS_RESET][XIB_QPS_INIT].valid = 1;
		qst[XIB_QPS_RESET][XIB_QPS_INIT].req_param[IB_QPT_UD]  = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_QKEY);
		qst[XIB_QPS_RESET][XIB_QPS_INIT].req_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_ACCESS_FLAGS);
		qst[XIB_QPS_RESET][XIB_QPS_INIT].req_param[IB_QPT_RC]  = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_ACCESS_FLAGS);
		qst[XIB_QPS_RESET][XIB_QPS_INIT].req_param[IB_QPT_SMI] = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);
		qst[XIB_QPS_RESET][XIB_QPS_INIT].req_param[IB_QPT_GSI] = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);

	//
	// XIB_QPS_INIT
	//

		// XIB_QPS_RESET
		qst[XIB_QPS_INIT][XIB_QPS_RESET].valid = 1;

		// XIB_QPS_ERR
		qst[XIB_QPS_INIT][XIB_QPS_ERR].valid = 1;

		// XIB_QPS_INIT
		qst[XIB_QPS_INIT][XIB_QPS_INIT].valid = 1;

		qst[XIB_QPS_INIT][XIB_QPS_INIT].opt_param[IB_QPT_UD]  = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_QKEY);
		qst[XIB_QPS_INIT][XIB_QPS_INIT].opt_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_ACCESS_FLAGS);
		qst[XIB_QPS_INIT][XIB_QPS_INIT].opt_param[IB_QPT_RC]  = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_ACCESS_FLAGS);
		qst[XIB_QPS_INIT][XIB_QPS_INIT].opt_param[IB_QPT_SMI] = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);
		qst[XIB_QPS_INIT][XIB_QPS_INIT].opt_param[IB_QPT_GSI] = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);

		// XIB_QPS_RTR
		qst[XIB_QPS_INIT][XIB_QPS_RTR].valid = 1;

		qst[XIB_QPS_INIT][XIB_QPS_RTR].req_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_AV | IB_QP_PATH_MTU | IB_QP_DEST_QPN | IB_QP_RQ_PSN);
		qst[XIB_QPS_INIT][XIB_QPS_RTR].req_param[IB_QPT_RC]  = 
			(ib_qp_attr_mask)(IB_QP_AV | IB_QP_PATH_MTU | IB_QP_DEST_QPN |
			IB_QP_RQ_PSN | IB_QP_MAX_DEST_RD_ATOMIC | IB_QP_MIN_RNR_TIMER);
		
		qst[XIB_QPS_INIT][XIB_QPS_RTR].opt_param[IB_QPT_UD]  = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);
		qst[XIB_QPS_INIT][XIB_QPS_RTR].opt_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX);
		qst[XIB_QPS_INIT][XIB_QPS_RTR].opt_param[IB_QPT_RC]  = 
			(ib_qp_attr_mask)(IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX | IB_QP_GENERATE_PRIO_TAG);
		qst[XIB_QPS_INIT][XIB_QPS_RTR].opt_param[IB_QPT_SMI] = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);
		qst[XIB_QPS_INIT][XIB_QPS_RTR].opt_param[IB_QPT_GSI] = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);

	//
	// XIB_QPS_RTR
	//

		// XIB_QPS_RESET
		qst[XIB_QPS_RTR][XIB_QPS_RESET].valid = 1;

		// XIB_QPS_ERR
		qst[XIB_QPS_RTR][XIB_QPS_ERR].valid = 1;

		// XIB_QPS_RTS
		qst[XIB_QPS_RTR][XIB_QPS_RTS].valid = 1;

		qst[XIB_QPS_RTR][XIB_QPS_RTS].req_param[IB_QPT_UD]  = IB_QP_SQ_PSN;
		qst[XIB_QPS_RTR][XIB_QPS_RTS].req_param[IB_QPT_UC]  = IB_QP_SQ_PSN;
		qst[XIB_QPS_RTR][XIB_QPS_RTS].req_param[IB_QPT_RC]  = (ib_qp_attr_mask)(IB_QP_TIMEOUT | 
			IB_QP_RETRY_CNT | IB_QP_RNR_RETRY | IB_QP_SQ_PSN | IB_QP_MAX_QP_RD_ATOMIC);
		qst[XIB_QPS_RTR][XIB_QPS_RTS].req_param[IB_QPT_SMI] = IB_QP_SQ_PSN;
		qst[XIB_QPS_RTR][XIB_QPS_RTS].req_param[IB_QPT_GSI] = IB_QP_SQ_PSN;

		qst[XIB_QPS_RTR][XIB_QPS_RTS].opt_param[IB_QPT_UD]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);
		qst[XIB_QPS_RTR][XIB_QPS_RTS].opt_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS |
			IB_QP_PATH_MIG_STATE);
		qst[XIB_QPS_RTR][XIB_QPS_RTS].opt_param[IB_QPT_RC]  = 
			(ib_qp_attr_mask)(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS | 
			IB_QP_MIN_RNR_TIMER | IB_QP_PATH_MIG_STATE);
		qst[XIB_QPS_RTR][XIB_QPS_RTS].opt_param[IB_QPT_SMI] = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);
		qst[XIB_QPS_RTR][XIB_QPS_RTS].opt_param[IB_QPT_GSI] = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);

	//
	// XIB_QPS_RTS
	//

		// XIB_QPS_RESET
		qst[XIB_QPS_RTS][XIB_QPS_RESET].valid = 1;

		// XIB_QPS_ERR
		qst[XIB_QPS_RTS][XIB_QPS_ERR].valid = 1;

		// XIB_QPS_RTS
		qst[XIB_QPS_RTS][XIB_QPS_RTS].valid = 1;

		qst[XIB_QPS_RTS][XIB_QPS_RTS].opt_param[IB_QPT_UD]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);
		qst[XIB_QPS_RTS][XIB_QPS_RTS].opt_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_ACCESS_FLAGS | IB_QP_ALT_PATH |
			IB_QP_PATH_MIG_STATE);
		qst[XIB_QPS_RTS][XIB_QPS_RTS].opt_param[IB_QPT_RC]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_ACCESS_FLAGS | IB_QP_ALT_PATH |
			IB_QP_PATH_MIG_STATE | IB_QP_MIN_RNR_TIMER);
		qst[XIB_QPS_RTS][XIB_QPS_RTS].opt_param[IB_QPT_SMI] = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);
		qst[XIB_QPS_RTS][XIB_QPS_RTS].opt_param[IB_QPT_GSI] = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);

		// XIB_QPS_SQD
		qst[XIB_QPS_RTS][XIB_QPS_SQD].valid = 1;
		qst[XIB_QPS_RTS][XIB_QPS_SQD].opt_param[IB_QPT_UD] = IB_QP_EN_SQD_ASYNC_NOTIFY;
		qst[XIB_QPS_RTS][XIB_QPS_SQD].opt_param[IB_QPT_UC] = IB_QP_EN_SQD_ASYNC_NOTIFY;
		qst[XIB_QPS_RTS][XIB_QPS_SQD].opt_param[IB_QPT_RC] = IB_QP_EN_SQD_ASYNC_NOTIFY;
		qst[XIB_QPS_RTS][XIB_QPS_SQD].opt_param[IB_QPT_SMI] = IB_QP_EN_SQD_ASYNC_NOTIFY;
		qst[XIB_QPS_RTS][XIB_QPS_SQD].opt_param[IB_QPT_GSI] = IB_QP_EN_SQD_ASYNC_NOTIFY;

	//
	// XIB_QPS_SQD
	//

		// XIB_QPS_RESET
		qst[XIB_QPS_SQD][XIB_QPS_RESET].valid = 1;

		// XIB_QPS_ERR
		qst[XIB_QPS_SQD][XIB_QPS_ERR].valid = 1;

		// XIB_QPS_RTS
		qst[XIB_QPS_SQD][XIB_QPS_RTS].valid = 1;

		qst[XIB_QPS_SQD][XIB_QPS_RTS].opt_param[IB_QPT_UD]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);
		qst[XIB_QPS_SQD][XIB_QPS_RTS].opt_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS |
			IB_QP_PATH_MIG_STATE);
		qst[XIB_QPS_SQD][XIB_QPS_RTS].opt_param[IB_QPT_RC]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS |
			IB_QP_MIN_RNR_TIMER | IB_QP_PATH_MIG_STATE);
		qst[XIB_QPS_SQD][XIB_QPS_RTS].opt_param[IB_QPT_SMI] = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);
		qst[XIB_QPS_SQD][XIB_QPS_RTS].opt_param[IB_QPT_GSI] = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);

		// XIB_QPS_SQD
		qst[XIB_QPS_SQD][XIB_QPS_SQD].valid = 1;

		qst[XIB_QPS_SQD][XIB_QPS_SQD].opt_param[IB_QPT_UD]  = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);
		qst[XIB_QPS_SQD][XIB_QPS_SQD].opt_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_AV | IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS |
			IB_QP_PKEY_INDEX | IB_QP_PATH_MIG_STATE);
		qst[XIB_QPS_SQD][XIB_QPS_SQD].opt_param[IB_QPT_RC]  = 
			(ib_qp_attr_mask)(IB_QP_PORT | IB_QP_AV | IB_QP_TIMEOUT | IB_QP_RETRY_CNT |
			IB_QP_RNR_RETRY | IB_QP_MAX_QP_RD_ATOMIC | IB_QP_MAX_DEST_RD_ATOMIC |
			IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX |
			IB_QP_MIN_RNR_TIMER | IB_QP_PATH_MIG_STATE);
		qst[XIB_QPS_SQD][XIB_QPS_SQD].opt_param[IB_QPT_SMI] = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);
		qst[XIB_QPS_SQD][XIB_QPS_SQD].opt_param[IB_QPT_GSI] = 
			(ib_qp_attr_mask)(IB_QP_PKEY_INDEX | IB_QP_QKEY);

	//
	// XIB_QPS_SQE
	//

		// XIB_QPS_RESET
		qst[XIB_QPS_SQE][XIB_QPS_RESET].valid = 1;

		// XIB_QPS_ERR
		qst[XIB_QPS_SQE][XIB_QPS_ERR].valid = 1;

		// XIB_QPS_RTS
		qst[XIB_QPS_SQE][XIB_QPS_RTS].valid = 1;

		qst[XIB_QPS_SQE][XIB_QPS_RTS].opt_param[IB_QPT_UD]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);
		qst[XIB_QPS_SQE][XIB_QPS_RTS].opt_param[IB_QPT_UC]  = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_ACCESS_FLAGS);
		qst[XIB_QPS_SQE][XIB_QPS_RTS].opt_param[IB_QPT_SMI] = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);
		qst[XIB_QPS_SQE][XIB_QPS_RTS].opt_param[IB_QPT_GSI] = 
			(ib_qp_attr_mask)(IB_QP_CUR_STATE | IB_QP_QKEY);


	//
	// XIB_QPS_ERR
	//

		// XIB_QPS_RESET
		qst[XIB_QPS_ERR][XIB_QPS_RESET].valid = 1;

		// XIB_QPS_ERR
		qst[XIB_QPS_ERR][XIB_QPS_ERR].valid = 1;

}

int ib_modify_qp_is_ok(enum ib_qp_state cur_state, enum ib_qp_state next_state,
		       enum ib_qp_type type, enum ib_qp_attr_mask mask)
{
	enum ib_qp_attr_mask req_param, opt_param;
		
	if (cur_state  < 0 || cur_state  > XIB_QPS_ERR ||
	    next_state < 0 || next_state > XIB_QPS_ERR)
		return 0;

	if (mask & IB_QP_CUR_STATE  &&
	    cur_state != XIB_QPS_RTR && cur_state != XIB_QPS_RTS &&
	    cur_state != XIB_QPS_SQD && cur_state != XIB_QPS_SQE)
		return 0;

	if (!qst[cur_state][next_state].valid)
		return 0;

	req_param = qst[cur_state][next_state].req_param[type];
	opt_param = qst[cur_state][next_state].opt_param[type];

	if ((mask & req_param) != req_param)
		return 0;

	if (mask & ~(req_param | opt_param | IB_QP_STATE))
		return 0;

	return 1;
}
EXPORT_SYMBOL(ib_modify_qp_is_ok);

struct ib_ah *ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr)
{
	struct ib_ah *ah;

	ah = pd->device->create_ah(pd, ah_attr);

	if (!IS_ERR(ah)) {
		ah->device  = pd->device;
		ah->pd      = pd;
		ah->p_uctx = NULL;
		atomic_inc(&pd->usecnt);
	}

	return ah;
}
EXPORT_SYMBOL(ib_create_ah);

int ib_destroy_ah(struct ib_ah *ah)
{
	int ret;
	struct ib_pd *pd = ah->pd;

	ret = ah->device->destroy_ah(ah);
	if (!ret)
		atomic_dec(&pd->usecnt);

	return ret;
}
EXPORT_SYMBOL(ib_destroy_ah);

enum rdma_transport_type
rdma_node_get_transport(enum rdma_node_type node_type)
{
	switch (node_type) {
	case RDMA_NODE_IB_CA:
	case RDMA_NODE_IB_SWITCH:
	case RDMA_NODE_IB_ROUTER:
		return RDMA_TRANSPORT_IB;
	case RDMA_NODE_RNIC:
		return RDMA_TRANSPORT_IWARP;
	default:
		ASSERT(FALSE);
		return (rdma_transport_type)0;
	}
}

enum rdma_transport_type rdma_port_get_transport(struct ib_device *device,
						 u8 port_num)
{
	return device->get_port_transport ?
		device->get_port_transport(device, port_num) :
		rdma_node_get_transport((rdma_node_type)device->node_type);
}
EXPORT_SYMBOL(rdma_port_get_transport);

