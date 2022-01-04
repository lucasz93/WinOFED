/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved. 
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	   Redistribution and use in source and binary forms, with or
 *	   without modification, are permitted provided that the following
 *	   conditions are met:
 *
 *		- Redistributions of source code must retain the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer.
 *
 *		- Redistributions in binary form must reproduce the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer in the documentation and/or other materials
 *		  provided with the distribution.
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

#include <ib_verbs.h>
#include <ib_cache.h>
#include <ib_pack.h>

#include "mthca_dev.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mthca_qp.tmh"
#endif
#include "mthca_cmd.h"
#include "mthca_memfree.h"
#include "mthca_wqe.h"


enum {
	MTHCA_MAX_DIRECT_QP_SIZE = 4 * PAGE_SIZE,
	MTHCA_ACK_REQ_FREQ		 = 10,
	MTHCA_FLIGHT_LIMIT		 = 9,
	MTHCA_UD_HEADER_SIZE	 = 72, /* largest UD header possible */
	MTHCA_INLINE_HEADER_SIZE = 4,  /* data segment overhead for inline */
	MTHCA_INLINE_CHUNK_SIZE  = 16  /* inline data segment chunk */
};

enum {
	MTHCA_QP_STATE_RST	= 0,
	MTHCA_QP_STATE_INIT = 1,
	MTHCA_QP_STATE_RTR	= 2,
	MTHCA_QP_STATE_RTS	= 3,
	MTHCA_QP_STATE_SQE	= 4,
	MTHCA_QP_STATE_SQD	= 5,
	MTHCA_QP_STATE_ERR	= 6,
	MTHCA_QP_STATE_DRAINING = 7
};

enum {
	MTHCA_QP_ST_RC	= 0x0,
	MTHCA_QP_ST_UC	= 0x1,
	MTHCA_QP_ST_RD	= 0x2,
	MTHCA_QP_ST_UD	= 0x3,
	MTHCA_QP_ST_MLX = 0x7
};

enum {
	MTHCA_QP_PM_MIGRATED = 0x3,
	MTHCA_QP_PM_ARMED	 = 0x0,
	MTHCA_QP_PM_REARM	 = 0x1
};

enum {
	/* qp_context flags */
	MTHCA_QP_BIT_DE  = 1 <<  8,
	/* params1 */
	MTHCA_QP_BIT_SRE = 1 << 15,
	MTHCA_QP_BIT_SWE = 1 << 14,
	MTHCA_QP_BIT_SAE = 1 << 13,
	MTHCA_QP_BIT_SIC = 1 <<  4,
	MTHCA_QP_BIT_SSC = 1 <<  3,
	/* params2 */
	MTHCA_QP_BIT_RRE = 1 << 15,
	MTHCA_QP_BIT_RWE = 1 << 14,
	MTHCA_QP_BIT_RAE = 1 << 13,
	MTHCA_QP_BIT_RIC = 1 <<  4,
	MTHCA_QP_BIT_RSC = 1 <<  3
};

#pragma pack(push,1)
struct mthca_qp_path {
	__be32 port_pkey;
	u8	   rnr_retry;
	u8	   g_mylmc;
	__be16 rlid;
	u8	   ackto;
	u8	   mgid_index;
	u8	   static_rate;
	u8	   hop_limit;
	__be32 sl_tclass_flowlabel;
	u8	   rgid[16];
} ;

struct mthca_qp_context {
	__be32 flags;
	__be32 tavor_sched_queue; /* Reserved on Arbel */
	u8	   mtu_msgmax;
	u8	   rq_size_stride;	/* Reserved on Tavor */
	u8	   sq_size_stride;	/* Reserved on Tavor */
	u8	   rlkey_arbel_sched_queue; /* Reserved on Tavor */
	__be32 usr_page;
	__be32 local_qpn;
	__be32 remote_qpn;
	u32    reserved1[2];
	struct mthca_qp_path pri_path;
	struct mthca_qp_path alt_path;
	__be32 rdd;
	__be32 pd;
	__be32 wqe_base;
	__be32 wqe_lkey;
	__be32 params1;
	__be32 reserved2;
	__be32 next_send_psn;
	__be32 cqn_snd;
	__be32 snd_wqe_base_l;	/* Next send WQE on Tavor */
	__be32 snd_db_index;	/* (debugging only entries) */
	__be32 last_acked_psn;
	__be32 ssn;
	__be32 params2;
	__be32 rnr_nextrecvpsn;
	__be32 ra_buff_indx;
	__be32 cqn_rcv;
	__be32 rcv_wqe_base_l;	/* Next recv WQE on Tavor */
	__be32 rcv_db_index;	/* (debugging only entries) */
	__be32 qkey;
	__be32 srqn;
	__be32 rmsn;
	__be16 rq_wqe_counter;	/* reserved on Tavor */
	__be16 sq_wqe_counter;	/* reserved on Tavor */
	u32    reserved3[18];
} ;

struct mthca_qp_param {
	__be32 opt_param_mask;
	u32    reserved1;
	struct mthca_qp_context context;
	u32    reserved2[62];
} ;
#pragma pack(pop)

enum {
	MTHCA_QP_OPTPAR_ALT_ADDR_PATH	  = 1 << 0,
	MTHCA_QP_OPTPAR_RRE 			  = 1 << 1,
	MTHCA_QP_OPTPAR_RAE 			  = 1 << 2,
	MTHCA_QP_OPTPAR_RWE 			  = 1 << 3,
	MTHCA_QP_OPTPAR_PKEY_INDEX		  = 1 << 4,
	MTHCA_QP_OPTPAR_Q_KEY			  = 1 << 5,
	MTHCA_QP_OPTPAR_RNR_TIMEOUT 	  = 1 << 6,
	MTHCA_QP_OPTPAR_PRIMARY_ADDR_PATH = 1 << 7,
	MTHCA_QP_OPTPAR_SRA_MAX 		  = 1 << 8,
	MTHCA_QP_OPTPAR_RRA_MAX 		  = 1 << 9,
	MTHCA_QP_OPTPAR_PM_STATE		  = 1 << 10,
	MTHCA_QP_OPTPAR_PORT_NUM		  = 1 << 11,
	MTHCA_QP_OPTPAR_RETRY_COUNT 	  = 1 << 12,
	MTHCA_QP_OPTPAR_ALT_RNR_RETRY	  = 1 << 13,
	MTHCA_QP_OPTPAR_ACK_TIMEOUT 	  = 1 << 14,
	MTHCA_QP_OPTPAR_RNR_RETRY		  = 1 << 15,
	MTHCA_QP_OPTPAR_SCHED_QUEUE 	  = 1 << 16
};

static const u8 mthca_opcode[] = {
	MTHCA_OPCODE_RDMA_WRITE,
	MTHCA_OPCODE_RDMA_WRITE_IMM,
	MTHCA_OPCODE_SEND,
	MTHCA_OPCODE_SEND_IMM,
	MTHCA_OPCODE_RDMA_READ,
	MTHCA_OPCODE_ATOMIC_CS,
	MTHCA_OPCODE_ATOMIC_FA
};


enum { RC, UC, UD, RD, RDEE, MLX, NUM_TRANS };

static struct _state_table {
	int trans;
	u32 req_param[NUM_TRANS];
	u32 opt_param[NUM_TRANS];
} state_table[IBQPS_ERR + 1][IBQPS_ERR + 1]= {0};

static void fill_state_table()
{
	struct _state_table *t;
	RtlZeroMemory( state_table, sizeof(state_table) );

	/* IBQPS_RESET */	
	t = &state_table[IBQPS_RESET][0];
	t[IBQPS_RESET].trans					= MTHCA_TRANS_ANY2RST;
	t[IBQPS_ERR].trans						= MTHCA_TRANS_ANY2ERR;

	t[IBQPS_INIT].trans 						= MTHCA_TRANS_RST2INIT;
	t[IBQPS_INIT].req_param[UD] 	= IB_QP_PKEY_INDEX |IB_QP_PORT |IB_QP_QKEY;
	t[IBQPS_INIT].req_param[UC] 	= IB_QP_PKEY_INDEX |IB_QP_PORT |IB_QP_ACCESS_FLAGS;
	t[IBQPS_INIT].req_param[RC] 	= IB_QP_PKEY_INDEX |IB_QP_PORT |IB_QP_ACCESS_FLAGS;
	t[IBQPS_INIT].req_param[MLX]	= IB_QP_PKEY_INDEX |IB_QP_QKEY;
	t[IBQPS_INIT].opt_param[MLX]	= IB_QP_PORT;

	/* IBQPS_INIT */	
	t = &state_table[IBQPS_INIT][0];
	t[IBQPS_RESET].trans					= MTHCA_TRANS_ANY2RST;
	t[IBQPS_ERR].trans						= MTHCA_TRANS_ANY2ERR;

	t[IBQPS_INIT].trans 						= MTHCA_TRANS_INIT2INIT;
	t[IBQPS_INIT].opt_param[UD] 	= IB_QP_PKEY_INDEX |IB_QP_PORT |IB_QP_QKEY;
	t[IBQPS_INIT].opt_param[UC] 	= IB_QP_PKEY_INDEX |IB_QP_PORT |IB_QP_ACCESS_FLAGS;
	t[IBQPS_INIT].opt_param[RC] 	= IB_QP_PKEY_INDEX |IB_QP_PORT |IB_QP_ACCESS_FLAGS;
	t[IBQPS_INIT].opt_param[MLX]	= IB_QP_PKEY_INDEX |IB_QP_QKEY;

	t[IBQPS_RTR].trans						= MTHCA_TRANS_INIT2RTR;
	t[IBQPS_RTR].req_param[UC]		= 
		IB_QP_AV |IB_QP_PATH_MTU |IB_QP_DEST_QPN |IB_QP_RQ_PSN;
	t[IBQPS_RTR].req_param[RC]		= 
		IB_QP_AV |IB_QP_PATH_MTU |IB_QP_DEST_QPN |IB_QP_RQ_PSN |IB_QP_MAX_DEST_RD_ATOMIC |IB_QP_MIN_RNR_TIMER;
	t[IBQPS_RTR].opt_param[UD]		= IB_QP_PKEY_INDEX |IB_QP_QKEY;
	t[IBQPS_RTR].opt_param[UC]		= IB_QP_PKEY_INDEX |IB_QP_ALT_PATH |IB_QP_ACCESS_FLAGS;
	t[IBQPS_RTR].opt_param[RC]		= IB_QP_PKEY_INDEX |IB_QP_ALT_PATH |IB_QP_ACCESS_FLAGS;
	t[IBQPS_RTR].opt_param[MLX] 	= IB_QP_PKEY_INDEX |IB_QP_QKEY;

/* IBQPS_RTR */ 
	t = &state_table[IBQPS_RTR][0];
	t[IBQPS_RESET].trans					= MTHCA_TRANS_ANY2RST;
	t[IBQPS_ERR].trans						= MTHCA_TRANS_ANY2ERR;

	t[IBQPS_RTS].trans						= MTHCA_TRANS_RTR2RTS;
	t[IBQPS_RTS].req_param[UD]		= IB_QP_SQ_PSN;
	t[IBQPS_RTS].req_param[UC]		= IB_QP_SQ_PSN;
	t[IBQPS_RTS].req_param[RC]		= 
		IB_QP_TIMEOUT |IB_QP_RETRY_CNT |IB_QP_RNR_RETRY |IB_QP_SQ_PSN |IB_QP_MAX_QP_RD_ATOMIC;
	t[IBQPS_RTS].req_param[MLX] 	= IB_QP_SQ_PSN;
	t[IBQPS_RTS].opt_param[UD]		= IB_QP_CUR_STATE |IB_QP_QKEY;
	t[IBQPS_RTS].opt_param[UC]		= 
		IB_QP_CUR_STATE |IB_QP_ALT_PATH |IB_QP_ACCESS_FLAGS |IB_QP_PATH_MIG_STATE;
	t[IBQPS_RTS].opt_param[RC]		=	IB_QP_CUR_STATE |IB_QP_ALT_PATH |
		IB_QP_ACCESS_FLAGS |IB_QP_MIN_RNR_TIMER |IB_QP_PATH_MIG_STATE;
	t[IBQPS_RTS].opt_param[MLX] 	= IB_QP_CUR_STATE |IB_QP_QKEY;

	/* IBQPS_RTS */ 
	t = &state_table[IBQPS_RTS][0];
	t[IBQPS_RESET].trans					= MTHCA_TRANS_ANY2RST;
	t[IBQPS_ERR].trans						= MTHCA_TRANS_ANY2ERR;

	t[IBQPS_RTS].trans						= MTHCA_TRANS_RTS2RTS;
	t[IBQPS_RTS].opt_param[UD]		= IB_QP_CUR_STATE |IB_QP_QKEY;
	t[IBQPS_RTS].opt_param[UC]		= IB_QP_ACCESS_FLAGS |IB_QP_ALT_PATH |IB_QP_PATH_MIG_STATE;
	t[IBQPS_RTS].opt_param[RC]		=	IB_QP_ACCESS_FLAGS |
		IB_QP_ALT_PATH |IB_QP_PATH_MIG_STATE |IB_QP_MIN_RNR_TIMER;
	t[IBQPS_RTS].opt_param[MLX] 	= IB_QP_CUR_STATE |IB_QP_QKEY;

	t[IBQPS_SQD].trans						= MTHCA_TRANS_RTS2SQD;
	t[IBQPS_SQD].opt_param[UD]		= IB_QP_EN_SQD_ASYNC_NOTIFY;
	t[IBQPS_SQD].opt_param[UC]		= IB_QP_EN_SQD_ASYNC_NOTIFY;
	t[IBQPS_SQD].opt_param[RC]		=	IB_QP_EN_SQD_ASYNC_NOTIFY;
	t[IBQPS_SQD].opt_param[MLX] 	= IB_QP_EN_SQD_ASYNC_NOTIFY;

	/* IBQPS_SQD */ 
	t = &state_table[IBQPS_SQD][0];
	t[IBQPS_RESET].trans					= MTHCA_TRANS_ANY2RST;
	t[IBQPS_ERR].trans						= MTHCA_TRANS_ANY2ERR;

	t[IBQPS_RTS].trans						= MTHCA_TRANS_SQD2RTS;
	t[IBQPS_RTS].opt_param[UD]		= IB_QP_CUR_STATE |IB_QP_QKEY;
	t[IBQPS_RTS].opt_param[UC]		= IB_QP_CUR_STATE |
		IB_QP_ALT_PATH |IB_QP_ACCESS_FLAGS |IB_QP_PATH_MIG_STATE;
	t[IBQPS_RTS].opt_param[RC]		=	IB_QP_CUR_STATE |IB_QP_ALT_PATH |
		IB_QP_ACCESS_FLAGS |IB_QP_MIN_RNR_TIMER |IB_QP_PATH_MIG_STATE;
	t[IBQPS_RTS].opt_param[MLX] 	= IB_QP_CUR_STATE |IB_QP_QKEY;

	t[IBQPS_SQD].trans						= MTHCA_TRANS_SQD2SQD;
	t[IBQPS_SQD].opt_param[UD]		= IB_QP_PKEY_INDEX |IB_QP_QKEY;
	t[IBQPS_SQD].opt_param[UC]		= IB_QP_AV |	IB_QP_CUR_STATE |
		IB_QP_ALT_PATH |IB_QP_ACCESS_FLAGS |IB_QP_PKEY_INDEX |IB_QP_PATH_MIG_STATE;
	t[IBQPS_SQD].opt_param[RC]		=	IB_QP_AV |IB_QP_TIMEOUT |IB_QP_RETRY_CNT |IB_QP_RNR_RETRY |
		IB_QP_MAX_QP_RD_ATOMIC |IB_QP_MAX_DEST_RD_ATOMIC |IB_QP_CUR_STATE |IB_QP_ALT_PATH |
		IB_QP_ACCESS_FLAGS |IB_QP_PKEY_INDEX |IB_QP_MIN_RNR_TIMER |IB_QP_PATH_MIG_STATE;
	t[IBQPS_SQD].opt_param[MLX] 	= IB_QP_PKEY_INDEX |IB_QP_QKEY;

	/* IBQPS_SQE */ 
	t = &state_table[IBQPS_SQE][0];
	t[IBQPS_RESET].trans					= MTHCA_TRANS_ANY2RST;
	t[IBQPS_ERR].trans						= MTHCA_TRANS_ANY2ERR;

	t[IBQPS_RTS].trans						= MTHCA_TRANS_SQERR2RTS;
	t[IBQPS_RTS].opt_param[UD]		= IB_QP_CUR_STATE |IB_QP_QKEY;
	t[IBQPS_RTS].opt_param[UC]		= IB_QP_CUR_STATE | IB_QP_ACCESS_FLAGS;
//	t[IBQPS_RTS].opt_param[RC]		=	IB_QP_CUR_STATE |IB_QP_MIN_RNR_TIMER;
	t[IBQPS_RTS].opt_param[MLX] 	= IB_QP_CUR_STATE |IB_QP_QKEY;

	/* IBQPS_ERR */ 
	t = &state_table[IBQPS_ERR][0];
	t[IBQPS_RESET].trans					= MTHCA_TRANS_ANY2RST;
	t[IBQPS_ERR].trans						= MTHCA_TRANS_ANY2ERR;

};


static int is_sqp(struct mthca_dev *dev, struct mthca_qp *qp)
{
	return qp->qpn >= (u32)dev->qp_table.sqp_start &&
		qp->qpn <= (u32)dev->qp_table.sqp_start + 3;
}

static int is_qp0(struct mthca_dev *dev, struct mthca_qp *qp)
{
	return qp->qpn >= (u32)dev->qp_table.sqp_start &&
		qp->qpn <= (u32)(dev->qp_table.sqp_start + 1);
}


static void dump_wqe(u32 print_lvl, u32 *wqe_ptr , struct mthca_qp *qp_ptr)
{
	__be32 *wqe = wqe_ptr;

	UNUSED_PARAM_WOWPP(qp_ptr);
	UNUSED_PARAM_WOWPP(print_lvl);

	(void) wqe; /* avoid warning if mthca_dbg compiled away... */
	HCA_PRINT(print_lvl,HCA_DBG_QP,("WQE contents  QPN 0x%06x \n",qp_ptr->qpn));
	HCA_PRINT(print_lvl,HCA_DBG_QP,("WQE contents [%02x] %08x %08x %08x %08x \n",0
		, cl_ntoh32(wqe[0]), cl_ntoh32(wqe[1]), cl_ntoh32(wqe[2]), cl_ntoh32(wqe[3])));
	HCA_PRINT(print_lvl,HCA_DBG_QP,("WQE contents [%02x] %08x %08x %08x %08x \n",4
		, cl_ntoh32(wqe[4]), cl_ntoh32(wqe[5]), cl_ntoh32(wqe[6]), cl_ntoh32(wqe[7])));
	HCA_PRINT(print_lvl,HCA_DBG_QP,("WQE contents [%02x] %08x %08x %08x %08x \n",8
		, cl_ntoh32(wqe[8]), cl_ntoh32(wqe[9]), cl_ntoh32(wqe[10]), cl_ntoh32(wqe[11])));
	HCA_PRINT(print_lvl,HCA_DBG_QP,("WQE contents [%02x] %08x %08x %08x %08x \n",12
		, cl_ntoh32(wqe[12]), cl_ntoh32(wqe[13]), cl_ntoh32(wqe[14]), cl_ntoh32(wqe[15])));

}


static void *get_recv_wqe(struct mthca_qp *qp, int n)
{
	if (qp->is_direct)
		return (u8*)qp->queue.direct.page + (n << qp->rq.wqe_shift);
	else
		return (u8*)qp->queue.page_list[(n << qp->rq.wqe_shift) >> PAGE_SHIFT].page +
			((n << qp->rq.wqe_shift) & (PAGE_SIZE - 1));
}

static void *get_send_wqe(struct mthca_qp *qp, int n)
{
	if (qp->is_direct)
		return (u8*)qp->queue.direct.page + qp->send_wqe_offset +
			(n << qp->sq.wqe_shift);
	else
		return (u8*)qp->queue.page_list[(qp->send_wqe_offset +
						(n << qp->sq.wqe_shift)) >>
					   PAGE_SHIFT].page +
			((qp->send_wqe_offset + (n << qp->sq.wqe_shift)) &
			 (PAGE_SIZE - 1));
}

static void mthca_wq_init(struct mthca_wq *wq)
{	
	spin_lock_init(&wq->lock);	
	wq->next_ind  = 0;	
	wq->last_comp = wq->max - 1;	
	wq->head	  = 0;	
	wq->tail	  = 0;	
}

void mthca_qp_event(struct mthca_dev *dev, u32 qpn,
			enum ib_event_type event_type, u8 vendor_code)
{
	struct mthca_qp *qp;
	ib_event_rec_t event;
	SPIN_LOCK_PREP(lh);

	spin_lock(&dev->qp_table.lock, &lh);
	qp = mthca_array_get(&dev->qp_table.qp, qpn & (dev->limits.num_qps - 1));
	if (qp)
		atomic_inc(&qp->refcount);
	spin_unlock(&lh);

	if (!qp) {
		HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_QP,("QP %06x Async event for bogus \n", qpn));
		return;
	}

	event.type = event_type;
	event.context = qp->ibqp.qp_context;
	event.vendor_specific = vendor_code;
	HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_QP,("QP %06x Async event	event_type 0x%x vendor_code 0x%x\n",
		qpn,event_type,vendor_code));
	qp->ibqp.event_handler(&event);

	if (atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);
}

static int to_mthca_state(enum ib_qp_state ib_state)
{
	switch (ib_state) {
	case IBQPS_RESET: return MTHCA_QP_STATE_RST;
	case IBQPS_INIT:  return MTHCA_QP_STATE_INIT;
	case IBQPS_RTR:   return MTHCA_QP_STATE_RTR;
	case IBQPS_RTS:   return MTHCA_QP_STATE_RTS;
	case IBQPS_SQD:   return MTHCA_QP_STATE_SQD;
	case IBQPS_SQE:   return MTHCA_QP_STATE_SQE;
	case IBQPS_ERR:   return MTHCA_QP_STATE_ERR;
	default:				return -1;
	}
}

static int to_mthca_st(int transport)
{
	switch (transport) {
	case RC:  return MTHCA_QP_ST_RC;
	case UC:  return MTHCA_QP_ST_UC;
	case UD:  return MTHCA_QP_ST_UD;
	case RD:  return MTHCA_QP_ST_RD;
	case MLX: return MTHCA_QP_ST_MLX;
	default:  return -1;
	}
}

static inline enum ib_qp_state to_ib_qp_state(int mthca_state)
{
	switch (mthca_state) {
	case MTHCA_QP_STATE_RST: return IBQPS_RESET;
	case MTHCA_QP_STATE_INIT:  return IBQPS_INIT;
	case MTHCA_QP_STATE_RTR:   return IBQPS_RTR;
	case MTHCA_QP_STATE_RTS:   return IBQPS_RTS;
	case MTHCA_QP_STATE_SQD:   return IBQPS_SQD;
	case MTHCA_QP_STATE_DRAINING:	return IBQPS_SQD;
	case MTHCA_QP_STATE_SQE:   return IBQPS_SQE;
	case MTHCA_QP_STATE_ERR:   return IBQPS_ERR;
	default:				return -1;
	}
}

static inline enum ib_mig_state to_ib_mig_state(int mthca_mig_state)
{
	switch (mthca_mig_state) {
	case 0:  return IB_MIG_ARMED;
	case 1:  return IB_MIG_REARM;
	case 3:  return IB_MIG_MIGRATED;
	default: return -1;
	}
}

static int to_ib_qp_access_flags(int mthca_flags)
{
	int ib_flags = 0;

	if (mthca_flags & MTHCA_QP_BIT_RRE)
		ib_flags |= MTHCA_ACCESS_REMOTE_READ;
	if (mthca_flags & MTHCA_QP_BIT_RWE)
		ib_flags |= MTHCA_ACCESS_REMOTE_WRITE;
	if (mthca_flags & MTHCA_QP_BIT_RAE)
		ib_flags |= MTHCA_ACCESS_REMOTE_ATOMIC;

	return ib_flags;
}

static void to_ib_ah_attr(struct mthca_dev *dev, struct ib_ah_attr *ib_ah_attr,
				struct mthca_qp_path *path)
{
	memset(ib_ah_attr, 0, sizeof *ib_ah_attr);
	ib_ah_attr->port_num	  = (u8)((cl_ntoh32(path->port_pkey) >> 24) & 0x3);

	if (ib_ah_attr->port_num == 0 || ib_ah_attr->port_num > dev->limits.num_ports)
		return;

	ib_ah_attr->dlid		  = cl_ntoh16(path->rlid);
	ib_ah_attr->sl			  = (u8)(cl_ntoh32(path->sl_tclass_flowlabel) >> 28);
	ib_ah_attr->src_path_bits = path->g_mylmc & 0x7f;
	//TODO: work around: set always full speed	- really, it's much more complicate
	ib_ah_attr->static_rate   = 0;
	ib_ah_attr->ah_flags	  = (path->g_mylmc & (1 << 7)) ? IB_AH_GRH : 0;
	if (ib_ah_attr->ah_flags) {
		ib_ah_attr->grh.sgid_index = (u8)(path->mgid_index & (dev->limits.gid_table_len - 1));
		ib_ah_attr->grh.hop_limit  = path->hop_limit;
		ib_ah_attr->grh.traffic_class =
			(u8)((cl_ntoh32(path->sl_tclass_flowlabel) >> 20) & 0xff);
		ib_ah_attr->grh.flow_label =
			cl_ntoh32(path->sl_tclass_flowlabel) & 0xfffff;
		memcpy(ib_ah_attr->grh.dgid.raw,
			path->rgid, sizeof ib_ah_attr->grh.dgid.raw);
	}
}

int mthca_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		   struct ib_qp_init_attr *qp_init_attr)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	struct mthca_qp *qp = to_mqp(ibqp);
	int err = 0;
	struct mthca_mailbox *mailbox = NULL;
	struct mthca_qp_param *qp_param;
	struct mthca_qp_context *context;
	int mthca_state;
	u8 status;

	UNUSED_PARAM(qp_attr_mask);
	
	down( &qp->mutex );

	if (qp->state == IBQPS_RESET) {
		qp_attr->qp_state = IBQPS_RESET;
		goto done;
	}

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto out;
	}

	err = mthca_QUERY_QP(dev, qp->qpn, 0, mailbox, &status);
	if (err)
		goto out_mailbox;
	if (status) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_QP,
			("QUERY_QP returned status %02x\n", status));
		err = -EINVAL;
		goto out_mailbox;
	}

	qp_param	= mailbox->buf;
	context 	= &qp_param->context;
	mthca_state = cl_ntoh32(context->flags) >> 28;

	qp->state			 = to_ib_qp_state(mthca_state);
	qp_attr->qp_state		 = qp->state;
	qp_attr->path_mtu		 = context->mtu_msgmax >> 5;
	qp_attr->path_mig_state 	 =
		to_ib_mig_state((cl_ntoh32(context->flags) >> 11) & 0x3);
	qp_attr->qkey			 = cl_ntoh32(context->qkey);
	qp_attr->rq_psn 		 = cl_ntoh32(context->rnr_nextrecvpsn) & 0xffffff;
	qp_attr->sq_psn 		 = cl_ntoh32(context->next_send_psn) & 0xffffff;
	qp_attr->dest_qp_num		 = cl_ntoh32(context->remote_qpn) & 0xffffff;
	qp_attr->qp_access_flags	 =
		to_ib_qp_access_flags(cl_ntoh32(context->params2));

	if (qp->transport == RC || qp->transport == UC) {
		to_ib_ah_attr(dev, &qp_attr->ah_attr, &context->pri_path);
		to_ib_ah_attr(dev, &qp_attr->alt_ah_attr, &context->alt_path);
		qp_attr->alt_pkey_index =
			(u16)(cl_ntoh32(context->alt_path.port_pkey) & 0x7f);
		qp_attr->alt_port_num	= qp_attr->alt_ah_attr.port_num;
	}

	qp_attr->pkey_index = (u16)(cl_ntoh32(context->pri_path.port_pkey) & 0x7f);
	qp_attr->port_num	=
		(u8)((cl_ntoh32(context->pri_path.port_pkey) >> 24) & 0x3);

	/* qp_attr->en_sqd_async_notify is only applicable in modify qp */
	qp_attr->sq_draining = (u8)(mthca_state == MTHCA_QP_STATE_DRAINING);

	qp_attr->max_rd_atomic = (u8)(1 << ((cl_ntoh32(context->params1) >> 21) & 0x7));

	qp_attr->max_dest_rd_atomic =
		(u8)(1 << ((cl_ntoh32(context->params2) >> 21) & 0x7));
	qp_attr->min_rnr_timer		=
		(u8)((cl_ntoh32(context->rnr_nextrecvpsn) >> 24) & 0x1f);
	qp_attr->timeout		= context->pri_path.ackto >> 3;
	qp_attr->retry_cnt		= (u8)((cl_ntoh32(context->params1) >> 16) & 0x7);
	qp_attr->rnr_retry		= context->pri_path.rnr_retry >> 5;
	qp_attr->alt_timeout		= context->alt_path.ackto >> 3;

done:
	qp_attr->cur_qp_state		 = qp_attr->qp_state;
	qp_attr->cap.max_send_wr	 = qp->sq.max;
	qp_attr->cap.max_recv_wr	 = qp->rq.max;
	qp_attr->cap.max_send_sge	 = qp->sq.max_gs;
	qp_attr->cap.max_recv_sge	 = qp->rq.max_gs;
	qp_attr->cap.max_inline_data = qp->max_inline_data;

	qp_init_attr->cap			 = qp_attr->cap;
	qp_init_attr->sq_sig_type	 = qp->sq_policy;

out_mailbox:
	mthca_free_mailbox(dev, mailbox);

out:
	up(&qp->mutex);
	return err;
}

static void store_attrs(struct mthca_sqp *sqp, struct ib_qp_attr *attr,
			int attr_mask)
{
	if (attr_mask & IB_QP_PKEY_INDEX)
		sqp->pkey_index = attr->pkey_index;
	if (attr_mask & IB_QP_QKEY)
		sqp->qkey = attr->qkey;
	if (attr_mask & IB_QP_SQ_PSN)
		sqp->send_psn = attr->sq_psn;
}

static void init_port(struct mthca_dev *dev, int port)
{
	int err;
	u8 status;
	struct mthca_init_ib_param param;

	RtlZeroMemory(&param, sizeof param);

	param.port_width	= dev->limits.port_width_cap;
	param.vl_cap	= dev->limits.vl_cap;
	param.mtu_cap	= dev->limits.mtu_cap;
	param.gid_cap	= (u16)dev->limits.gid_table_len;
	param.pkey_cap	= (u16)dev->limits.pkey_table_len;

	err = mthca_INIT_IB(dev, &param, port, &status);
	if (err)
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP  ,("INIT_IB failed, return code %d.\n", err));
	if (status)
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP  ,("INIT_IB returned status %02x.\n", status));
}


static __be32 get_hw_access_flags(struct mthca_qp *qp, struct ib_qp_attr *attr,
				  int attr_mask)
{
	u8 dest_rd_atomic;
	u32 access_flags;
	u32 hw_access_flags = 0;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		dest_rd_atomic = attr->max_dest_rd_atomic;
	else
		dest_rd_atomic = qp->resp_depth;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		access_flags = attr->qp_access_flags;
	else
		access_flags = qp->atomic_rd_en;

	if (!dest_rd_atomic)
		access_flags &= MTHCA_ACCESS_REMOTE_WRITE;

	if (access_flags & MTHCA_ACCESS_REMOTE_READ)
		hw_access_flags |= MTHCA_QP_BIT_RRE;
	if (access_flags & MTHCA_ACCESS_REMOTE_ATOMIC)
		hw_access_flags |= MTHCA_QP_BIT_RAE;
	if (access_flags & MTHCA_ACCESS_REMOTE_WRITE)
		hw_access_flags |= MTHCA_QP_BIT_RWE;

	return cl_hton32(hw_access_flags);
}

int mthca_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	struct mthca_qp *qp = to_mqp(ibqp);
	enum ib_qp_state cur_state, new_state;
	struct mthca_mailbox *mailbox;
	struct mthca_qp_param *qp_param;
	struct mthca_qp_context *qp_context;
	u32 req_param, opt_param;
	u32 sqd_event = 0;
	u8 status;
	int err = -EINVAL;
	SPIN_LOCK_PREP(lhs);
	SPIN_LOCK_PREP(lhr);

	down( &qp->mutex );

	if (attr_mask & IB_QP_CUR_STATE) {
		if (attr->cur_qp_state != IBQPS_RTR &&
			attr->cur_qp_state != IBQPS_RTS &&
			attr->cur_qp_state != IBQPS_SQD &&
			attr->cur_qp_state != IBQPS_SQE)
			goto out;
		else
			cur_state = attr->cur_qp_state;
	} else {
		spin_lock_irq(&qp->sq.lock, &lhs);
		spin_lock(&qp->rq.lock, &lhr);
		cur_state = qp->state;
		spin_unlock(&lhr);
		spin_unlock_irq(&lhs);
	}

	if (attr_mask & IB_QP_STATE) {
		if (attr->qp_state < 0 || attr->qp_state > IBQPS_ERR)
			goto out;
		new_state = attr->qp_state;
	} else
		new_state = cur_state;

	if (state_table[cur_state][new_state].trans == MTHCA_TRANS_INVALID) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("Illegal QP transition "
			  "%d->%d\n", cur_state, new_state));
		goto out;
	}

	req_param = state_table[cur_state][new_state].req_param[qp->transport];
	opt_param = state_table[cur_state][new_state].opt_param[qp->transport];

	if ((req_param & attr_mask) != req_param) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("QP transition "
			  "%d->%d missing req attr 0x%08x\n",
			  cur_state, new_state,
			  req_param & ~attr_mask));
		//NB: IBAL doesn't use all the fields, so we can miss some mandatory flags
		goto out;
	}

	if (attr_mask & ~(req_param | opt_param | IB_QP_STATE)) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("QP transition (transport %d) "
			  "%d->%d has extra attr 0x%08x\n",
			  qp->transport,
			  cur_state, new_state,
			  attr_mask & ~(req_param | opt_param |
						 IB_QP_STATE)));
		//NB: The old code sometimes uses optional flags that are not so in this code
		goto out;
	}

	if ((attr_mask & IB_QP_PKEY_INDEX) && 
		attr->pkey_index >= dev->limits.pkey_table_len) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("PKey index (%u) too large. max is %d\n",
			  attr->pkey_index,dev->limits.pkey_table_len-1)); 
		goto out;
	}

	if ((attr_mask & IB_QP_PORT) &&
		(attr->port_num == 0 || attr->port_num > dev->limits.num_ports)) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("Port number (%u) is invalid\n", attr->port_num));
		goto out;
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
		attr->max_rd_atomic > dev->limits.max_qp_init_rdma) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("Max rdma_atomic as initiator %u too large (max is %d)\n",
			  attr->max_rd_atomic, dev->limits.max_qp_init_rdma));
		goto out;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
		attr->max_dest_rd_atomic > 1 << dev->qp_table.rdb_shift) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("Max rdma_atomic as responder %u too large (max %d)\n",
			  attr->max_dest_rd_atomic, 1 << dev->qp_table.rdb_shift));
		goto out;
	}

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto out;
	}
	qp_param = mailbox->buf;
	qp_context = &qp_param->context;
	RtlZeroMemory(qp_param, sizeof *qp_param);

	qp_context->flags	   = cl_hton32((to_mthca_state(new_state) << 28) |
						 (to_mthca_st(qp->transport) << 16));
	qp_context->flags	  |= cl_hton32(MTHCA_QP_BIT_DE);
	if (!(attr_mask & IB_QP_PATH_MIG_STATE))
		qp_context->flags |= cl_hton32(MTHCA_QP_PM_MIGRATED << 11);
	else {
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_PM_STATE);
		switch (attr->path_mig_state) {
		case IB_APM_MIGRATED:
			qp_context->flags |= cl_hton32(MTHCA_QP_PM_MIGRATED << 11);
			break;
		case IB_APM_REARM:
			qp_context->flags |= cl_hton32(MTHCA_QP_PM_REARM << 11);
			break;
		case IB_APM_ARMED:
			qp_context->flags |= cl_hton32(MTHCA_QP_PM_ARMED << 11);
			break;
		}
	}

	/* leave tavor_sched_queue as 0 */

	if (qp->transport == MLX || qp->transport == UD)
		qp_context->mtu_msgmax = (IB_MTU_LEN_2048 << 5) | 11;
	else if (attr_mask & IB_QP_PATH_MTU) {
		if (attr->path_mtu < IB_MTU_LEN_256 || attr->path_mtu > IB_MTU_LEN_2048) {
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,
				("path MTU (%u) is invalid\n", attr->path_mtu));
			goto out_mailbox;
		}
		qp_context->mtu_msgmax = (u8)((attr->path_mtu << 5) | 31);
	}

	if (mthca_is_memfree(dev)) {
		if (qp->rq.max)
			qp_context->rq_size_stride = (u8)(long_log2(qp->rq.max) << 3);
		qp_context->rq_size_stride |= qp->rq.wqe_shift - 4;

		if (qp->sq.max)
			qp_context->sq_size_stride = (u8)(long_log2(qp->sq.max) << 3);
		qp_context->sq_size_stride |= qp->sq.wqe_shift - 4;
	}

	/* leave arbel_sched_queue as 0 */

	if (qp->ibqp.ucontext)
		qp_context->usr_page =
			cl_hton32(to_mucontext(qp->ibqp.ucontext)->uar.index);
	else
		qp_context->usr_page = cl_hton32(dev->driver_uar.index);
	qp_context->local_qpn  = cl_hton32(qp->qpn);
	if (attr_mask & IB_QP_DEST_QPN) {
		qp_context->remote_qpn = cl_hton32(attr->dest_qp_num);
	}

	if (qp->transport == MLX)
		qp_context->pri_path.port_pkey |=
			cl_hton32(to_msqp(qp)->port << 24);
	else {
		if (attr_mask & IB_QP_PORT) {
			qp_context->pri_path.port_pkey |=
				cl_hton32(attr->port_num << 24);
			qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_PORT_NUM);
		}
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		qp_context->pri_path.port_pkey |=
			cl_hton32(attr->pkey_index);
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_PKEY_INDEX);
	}

	if (attr_mask & IB_QP_RNR_RETRY) {
		qp_context->pri_path.rnr_retry = attr->rnr_retry << 5;
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_RNR_RETRY);
	}

	if (attr_mask & IB_QP_AV) {
		qp_context->pri_path.g_mylmc	 = attr->ah_attr.src_path_bits & 0x7f;
		qp_context->pri_path.rlid		 = cl_hton16(attr->ah_attr.dlid);
		//TODO: work around: set always full speed	- really, it's much more complicate
		qp_context->pri_path.static_rate = 0;
		if (attr->ah_attr.ah_flags & IB_AH_GRH) {
			qp_context->pri_path.g_mylmc |= 1 << 7;
			qp_context->pri_path.mgid_index = attr->ah_attr.grh.sgid_index;
			qp_context->pri_path.hop_limit = attr->ah_attr.grh.hop_limit;
			qp_context->pri_path.sl_tclass_flowlabel =
				cl_hton32((attr->ah_attr.sl << 28)				  |
						(attr->ah_attr.grh.traffic_class << 20) |
						(attr->ah_attr.grh.flow_label));
			memcpy(qp_context->pri_path.rgid,
				   attr->ah_attr.grh.dgid.raw, 16);
		} else {
			qp_context->pri_path.sl_tclass_flowlabel =
				cl_hton32(attr->ah_attr.sl << 28);
		}
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_PRIMARY_ADDR_PATH);
	}

	if (attr_mask & IB_QP_TIMEOUT) {
		qp_context->pri_path.ackto = attr->timeout << 3;
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_ACK_TIMEOUT);
	}

	/* XXX alt_path */

	/* leave rdd as 0 */
	qp_context->pd		   = cl_hton32(to_mpd(ibqp->pd)->pd_num);
	/* leave wqe_base as 0 (we always create an MR based at 0 for WQs) */
	qp_context->wqe_lkey   = cl_hton32(qp->mr.ibmr.lkey);
	qp_context->params1    = cl_hton32((unsigned long)(
		(MTHCA_ACK_REQ_FREQ << 28) |
		(MTHCA_FLIGHT_LIMIT << 24) |
		MTHCA_QP_BIT_SWE));
	if (qp->sq_policy == IB_SIGNAL_ALL_WR)
		qp_context->params1 |= cl_hton32(MTHCA_QP_BIT_SSC);
	if (attr_mask & IB_QP_RETRY_CNT) {
		qp_context->params1 |= cl_hton32(attr->retry_cnt << 16);
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_RETRY_COUNT);
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		if (attr->max_rd_atomic) {
			qp_context->params1 |=
				cl_hton32(MTHCA_QP_BIT_SRE |
						MTHCA_QP_BIT_SAE);
			qp_context->params1 |=
				cl_hton32(fls(attr->max_rd_atomic - 1) << 21);
		}
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_SRA_MAX);
	}

	if (attr_mask & IB_QP_SQ_PSN)
		qp_context->next_send_psn = cl_hton32(attr->sq_psn);
	qp_context->cqn_snd = cl_hton32(to_mcq(ibqp->send_cq)->cqn);

	if (mthca_is_memfree(dev)) {
		qp_context->snd_wqe_base_l = cl_hton32(qp->send_wqe_offset);
		qp_context->snd_db_index   = cl_hton32(qp->sq.db_index);
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {

		if (attr->max_dest_rd_atomic)
			qp_context->params2 |=
				cl_hton32(fls(attr->max_dest_rd_atomic - 1) << 21);

		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_RRA_MAX);

	}

	if (attr_mask & (IB_QP_ACCESS_FLAGS | IB_QP_MAX_DEST_RD_ATOMIC)) {
		qp_context->params2 	 |= get_hw_access_flags(qp, attr, attr_mask);
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_RWE |
							MTHCA_QP_OPTPAR_RRE |
							MTHCA_QP_OPTPAR_RAE);
	}

	qp_context->params2 |= cl_hton32(MTHCA_QP_BIT_RSC);

	if (ibqp->srq)
		qp_context->params2 |= cl_hton32(MTHCA_QP_BIT_RIC);

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		qp_context->rnr_nextrecvpsn |= cl_hton32(attr->min_rnr_timer << 24);
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_RNR_TIMEOUT);
	}
	if (attr_mask & IB_QP_RQ_PSN)
		qp_context->rnr_nextrecvpsn |= cl_hton32(attr->rq_psn);

	qp_context->ra_buff_indx =
		cl_hton32(dev->qp_table.rdb_base +
				((qp->qpn & (dev->limits.num_qps - 1)) * MTHCA_RDB_ENTRY_SIZE <<
				 dev->qp_table.rdb_shift));

	qp_context->cqn_rcv = cl_hton32(to_mcq(ibqp->recv_cq)->cqn);

	if (mthca_is_memfree(dev))
		qp_context->rcv_db_index   = cl_hton32(qp->rq.db_index);

	if (attr_mask & IB_QP_QKEY) {
		qp_context->qkey = cl_hton32(attr->qkey);
		qp_param->opt_param_mask |= cl_hton32(MTHCA_QP_OPTPAR_Q_KEY);
	}

	if (ibqp->srq)
		qp_context->srqn = cl_hton32(1 << 24 |
						   to_msrq(ibqp->srq)->srqn);

	if (cur_state == IBQPS_RTS && new_state == IBQPS_SQD	&&
		attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY		&&
		attr->en_sqd_async_notify)
		sqd_event = (u32)(1 << 31);

	err = mthca_MODIFY_QP(dev, state_table[cur_state][new_state].trans,
				  qp->qpn, 0, mailbox, sqd_event, &status);
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("mthca_MODIFY_QP returned error (qp-num = 0x%x) returned status %02x "
			"cur_state	= %d  new_state = %d attr_mask = %d req_param = %d opt_param = %d\n",
			ibqp->qp_num, status, cur_state, new_state, 
			attr_mask, req_param, opt_param));		  
		goto out_mailbox;
	}
	if (status) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("mthca_MODIFY_QP bad status(qp-num = 0x%x) returned status %02x "
			"cur_state	= %d  new_state = %d attr_mask = %d req_param = %d opt_param = %d\n",
			ibqp->qp_num, status, cur_state, new_state, 
			attr_mask, req_param, opt_param));
		err = -EINVAL;
		goto out_mailbox;
	}

	qp->state = new_state;
	if (attr_mask & IB_QP_ACCESS_FLAGS)
		qp->atomic_rd_en = (u8)attr->qp_access_flags;
	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		qp->resp_depth = attr->max_dest_rd_atomic;

	if (is_sqp(dev, qp))
		store_attrs(to_msqp(qp), attr, attr_mask);

	/*
	 * If we moved QP0 to RTR, bring the IB link up; if we moved
	 * QP0 to RESET or ERROR, bring the link back down.
	 */
	if (is_qp0(dev, qp)) {
		if (cur_state != IBQPS_RTR &&
			new_state == IBQPS_RTR)
			init_port(dev, to_msqp(qp)->port);

		if (cur_state != IBQPS_RESET &&
			cur_state != IBQPS_ERR &&
			(new_state == IBQPS_RESET ||
			new_state == IBQPS_ERR))
			mthca_CLOSE_IB(dev, to_msqp(qp)->port, &status);
	}

	/*
	 * If we moved a kernel QP to RESET, clean up all old CQ
	 * entries and reinitialize the QP.
	 */
	if (new_state == IBQPS_RESET && !qp->ibqp.ucontext) {
		mthca_cq_clean(dev, to_mcq(qp->ibqp.send_cq)->cqn, qp->qpn,
				   qp->ibqp.srq ? to_msrq(qp->ibqp.srq) : NULL);
		if (qp->ibqp.send_cq != qp->ibqp.recv_cq)
			mthca_cq_clean(dev, to_mcq(qp->ibqp.recv_cq)->cqn, qp->qpn,
					   qp->ibqp.srq ? to_msrq(qp->ibqp.srq) : NULL);

		mthca_wq_init(&qp->sq);
		qp->sq.last = get_send_wqe(qp, qp->sq.max - 1);
		mthca_wq_init(&qp->rq);
		qp->rq.last = get_recv_wqe(qp, qp->rq.max - 1);

		if (mthca_is_memfree(dev)) {
			*qp->sq.db = 0;
			*qp->rq.db = 0;
		}
	}

out_mailbox:
	mthca_free_mailbox(dev, mailbox);

out:
	up( &qp->mutex );
	return err;
}

static int mthca_max_data_size(struct mthca_dev *dev, struct mthca_qp *qp, int desc_sz)
{

	/*
	 * Calculate the maximum size of WQE s/g segments, excluding
	 * the next segment and other non-data segments.
	 */
	int max_data_size = desc_sz - sizeof (struct mthca_next_seg);

	switch (qp->transport) {
	case MLX:
		max_data_size -= 2 * sizeof (struct mthca_data_seg);
		break;

	case UD:
		if (mthca_is_memfree(dev))
			max_data_size -= sizeof (struct mthca_arbel_ud_seg);
		else
			max_data_size -= sizeof (struct mthca_tavor_ud_seg);
		break;

	default:
		max_data_size -= sizeof (struct mthca_raddr_seg);
		break;
	}
		return max_data_size;
}

static inline int mthca_max_inline_data(int max_data_size)
{
	return max_data_size - MTHCA_INLINE_HEADER_SIZE ;
}

static void mthca_adjust_qp_caps(struct mthca_dev *dev,
				 struct mthca_qp *qp)
{
	int max_data_size = mthca_max_data_size(dev, qp,
		min(dev->limits.max_desc_sz, 1 << qp->sq.wqe_shift));

	qp->max_inline_data = mthca_max_inline_data( max_data_size);

	qp->sq.max_gs = min(dev->limits.max_sg,
		(int)(max_data_size / sizeof (struct mthca_data_seg)));
	qp->rq.max_gs = min(dev->limits.max_sg,
		(int)((min(dev->limits.max_desc_sz, 1 << qp->rq.wqe_shift) -
		sizeof (struct mthca_next_seg)) / sizeof (struct mthca_data_seg))); 
}

/*
 * Allocate and register buffer for WQEs.  qp->rq.max, sq.max,
 * rq.max_gs and sq.max_gs must all be assigned.
 * mthca_alloc_wqe_buf will calculate rq.wqe_shift and
 * sq.wqe_shift (as well as send_wqe_offset, is_direct, and
 * queue)
 */
static int mthca_alloc_wqe_buf(struct mthca_dev *dev,
				   struct mthca_pd *pd,
				   struct mthca_qp *qp)
{
	int size;
	int err = -ENOMEM;
	
	HCA_ENTER(HCA_DBG_QP);
	size = sizeof (struct mthca_next_seg) +
		qp->rq.max_gs * sizeof (struct mthca_data_seg);

	if (size > dev->limits.max_desc_sz)
		return -EINVAL;

	for (qp->rq.wqe_shift = 6; 1 << qp->rq.wqe_shift < size;
		 qp->rq.wqe_shift++)
		; /* nothing */

	size = qp->sq.max_gs * sizeof (struct mthca_data_seg);
	switch (qp->transport) {
		case MLX:
			size += 2 * sizeof (struct mthca_data_seg);
			break;

		case UD:
			size += mthca_is_memfree(dev) ?
				sizeof (struct mthca_arbel_ud_seg) :
				sizeof (struct mthca_tavor_ud_seg);
			break;
		
		case UC:
			size += sizeof (struct mthca_raddr_seg);
			break;
		
		case RC:
			size += sizeof (struct mthca_raddr_seg);
			/*
			 * An atomic op will require an atomic segment, a
			 * remote address segment and one scatter entry.
			 */
			size = max(size,
				 sizeof (struct mthca_atomic_seg) +
				 sizeof (struct mthca_raddr_seg) +
				 sizeof (struct mthca_data_seg));
			break;
			
		default:
			break;
	}
		
	/* Make sure that we have enough space for a bind request */
	size = max(size, sizeof (struct mthca_bind_seg));
	
	size += sizeof (struct mthca_next_seg);
	
	if (size > dev->limits.max_desc_sz)
		return -EINVAL;

	for (qp->sq.wqe_shift = 6; 1 << qp->sq.wqe_shift < size;
		 qp->sq.wqe_shift++)
		; /* nothing */

	qp->send_wqe_offset = ALIGN(qp->rq.max << qp->rq.wqe_shift,
					1 << qp->sq.wqe_shift);

	/*
	 * If this is a userspace QP, we don't actually have to
	 * allocate anything.  All we need is to calculate the WQE
	 * sizes and the send_wqe_offset, so we're done now.
	 */
	if (pd->ibpd.ucontext)
		return 0;

	size = (int)(LONG_PTR)NEXT_PAGE_ALIGN(qp->send_wqe_offset +
			  (qp->sq.max << qp->sq.wqe_shift));

	qp->wrid = kmalloc((qp->rq.max + qp->sq.max) * sizeof (u64),
			   GFP_KERNEL);
	if (!qp->wrid)
		goto err_out;

	err = mthca_buf_alloc(dev, size, MTHCA_MAX_DIRECT_QP_SIZE,
				  &qp->queue, &qp->is_direct, pd, 0, &qp->mr);
	if (err)
		goto err_out;
	
	HCA_EXIT(HCA_DBG_QP);
	return 0;

err_out:
	kfree(qp->wrid);
	return err;
}

static void mthca_free_wqe_buf(struct mthca_dev *dev,
				   struct mthca_qp *qp)
{
	mthca_buf_free(dev, (int)(LONG_PTR)NEXT_PAGE_ALIGN(qp->send_wqe_offset +
					   (qp->sq.max << qp->sq.wqe_shift)),
			   &qp->queue, qp->is_direct, &qp->mr);
	kfree(qp->wrid);
}

static int mthca_map_memfree(struct mthca_dev *dev,
				 struct mthca_qp *qp)
{
	int ret;

	if (mthca_is_memfree(dev)) {
		ret = mthca_table_get(dev, dev->qp_table.qp_table, qp->qpn);
		if (ret)
			return ret;

		ret = mthca_table_get(dev, dev->qp_table.eqp_table, qp->qpn);
		if (ret)
			goto err_qpc;

		ret = mthca_table_get(dev, dev->qp_table.rdb_table,
					  qp->qpn << dev->qp_table.rdb_shift);
		if (ret)
			goto err_eqpc;

	}

	return 0;

err_eqpc:
	mthca_table_put(dev, dev->qp_table.eqp_table, qp->qpn);

err_qpc:
	mthca_table_put(dev, dev->qp_table.qp_table, qp->qpn);

	return ret;
}

static void mthca_unmap_memfree(struct mthca_dev *dev,
				struct mthca_qp *qp)
{
	mthca_table_put(dev, dev->qp_table.rdb_table,
			qp->qpn << dev->qp_table.rdb_shift);
	mthca_table_put(dev, dev->qp_table.eqp_table, qp->qpn);
	mthca_table_put(dev, dev->qp_table.qp_table, qp->qpn);
}

static int mthca_alloc_memfree(struct mthca_dev *dev,
				   struct mthca_qp *qp)
{
	int ret = 0;

	if (mthca_is_memfree(dev)) {
		qp->rq.db_index = mthca_alloc_db(dev, MTHCA_DB_TYPE_RQ,
						 qp->qpn, &qp->rq.db);
		if (qp->rq.db_index < 0)
			return qp->rq.db_index;

		qp->sq.db_index = mthca_alloc_db(dev, MTHCA_DB_TYPE_SQ,
						 qp->qpn, &qp->sq.db);
		if (qp->sq.db_index < 0){
			mthca_free_db(dev, MTHCA_DB_TYPE_RQ, qp->rq.db_index);
			return qp->sq.db_index;
		}

	}

	return ret;
}

static void mthca_free_memfree(struct mthca_dev *dev,
				   struct mthca_qp *qp)
{
	if (mthca_is_memfree(dev)) {
		mthca_free_db(dev, MTHCA_DB_TYPE_SQ, qp->sq.db_index);
		mthca_free_db(dev, MTHCA_DB_TYPE_RQ, qp->rq.db_index);
	}
}

static int mthca_alloc_qp_common(struct mthca_dev *dev,
				 struct mthca_pd *pd,
				 struct mthca_cq *send_cq,
				 struct mthca_cq *recv_cq,
				 enum ib_sig_type send_policy,
				 struct mthca_qp *qp)
{
	int ret;
	int i;

	atomic_set(&qp->refcount, 1);
	init_waitqueue_head(&qp->wait);
	KeInitializeMutex(&qp->mutex, 0);

	qp->state		 = IBQPS_RESET;
	qp->atomic_rd_en = 0;
	qp->resp_depth	 = 0;
	qp->sq_policy	 = send_policy;
	mthca_wq_init(&qp->sq);
	mthca_wq_init(&qp->rq);

	UNREFERENCED_PARAMETER(send_cq);
	UNREFERENCED_PARAMETER(recv_cq);
	
	ret = mthca_map_memfree(dev, qp);
	if (ret)
		return ret;

	ret = mthca_alloc_wqe_buf(dev, pd, qp);
	if (ret) {
		mthca_unmap_memfree(dev, qp);
		return ret;
	}

	mthca_adjust_qp_caps(dev, qp);

	/*
	 * If this is a userspace QP, we're done now.  The doorbells
	 * will be allocated and buffers will be initialized in
	 * userspace.
	 */
	if (pd->ibpd.ucontext)
		return 0;

	ret = mthca_alloc_memfree(dev, qp);
	if (ret) {
		mthca_free_wqe_buf(dev, qp);
		mthca_unmap_memfree(dev, qp);
		return ret;
	}

	if (mthca_is_memfree(dev)) {
		struct mthca_next_seg *next;
		struct mthca_data_seg *scatter;
		int size = (sizeof (struct mthca_next_seg) +
				qp->rq.max_gs * sizeof (struct mthca_data_seg)) / 16;

		for (i = 0; i < qp->rq.max; ++i) {
			next = get_recv_wqe(qp, i);
			next->nda_op = cl_hton32(((i + 1) & (qp->rq.max - 1)) <<
						   qp->rq.wqe_shift);
			next->ee_nds = cl_hton32(size);

			for (scatter = (void *) (next + 1);
				 (void *) scatter < (void *) ((u8*)next + (u32)(1 << qp->rq.wqe_shift));
				 ++scatter)
				scatter->lkey = cl_hton32(MTHCA_INVAL_LKEY);
		}

		for (i = 0; i < qp->sq.max; ++i) {
			next = get_send_wqe(qp, i);
			next->nda_op = cl_hton32((((i + 1) & (qp->sq.max - 1)) <<
							qp->sq.wqe_shift) +
						   qp->send_wqe_offset);
		}
	}

	qp->sq.last = get_send_wqe(qp, qp->sq.max - 1);
	qp->rq.last = get_recv_wqe(qp, qp->rq.max - 1);

	return 0;
}

static int mthca_set_qp_size(struct mthca_dev *dev, struct ib_qp_cap *cap,
	struct mthca_qp *qp)
{
	int max_data_size = mthca_max_data_size(dev, qp, dev->limits.max_desc_sz);

	/* Sanity check QP size before proceeding */
	if (cap->max_send_wr	 > (u32)dev->limits.max_wqes ||
		cap->max_recv_wr	 > (u32)dev->limits.max_wqes ||
		cap->max_send_sge	 > (u32)dev->limits.max_sg	 ||
		cap->max_recv_sge	 > (u32)dev->limits.max_sg	 ||
		cap->max_inline_data > (u32)mthca_max_inline_data(max_data_size))
		return -EINVAL;

	/*
	 * For MLX transport we need 2 extra S/G entries:
	 * one for the header and one for the checksum at the end
	 */
	if (qp->transport == MLX && cap->max_recv_sge + 2 > (u32)dev->limits.max_sg)
		return -EINVAL;

	/* Enable creating zero-sized QPs */
	if (!cap->max_recv_wr)
		cap->max_recv_wr = 1;
	if (!cap->max_send_wr)
		cap->max_send_wr = 1;
	
	if (mthca_is_memfree(dev)) {
		qp->rq.max = cap->max_recv_wr ?
			roundup_pow_of_two(cap->max_recv_wr) : 0;
		qp->sq.max = cap->max_send_wr ?
			roundup_pow_of_two(cap->max_send_wr) : 0;
	} else {
		qp->rq.max = cap->max_recv_wr;
		qp->sq.max = cap->max_send_wr;
	}

	qp->rq.max_gs = cap->max_recv_sge;
	qp->sq.max_gs = MAX(cap->max_send_sge,
				  ALIGN(cap->max_inline_data + MTHCA_INLINE_HEADER_SIZE,
					MTHCA_INLINE_CHUNK_SIZE) /
				  (int)sizeof (struct mthca_data_seg));

	return 0;
}

int mthca_alloc_qp(struct mthca_dev *dev,
		   struct mthca_pd *pd,
		   struct mthca_cq *send_cq,
		   struct mthca_cq *recv_cq,
		   enum ib_qp_type_t type,
		   enum ib_sig_type send_policy,
		   struct ib_qp_cap *cap,
		   struct mthca_qp *qp)
{
	int err;
	SPIN_LOCK_PREP(lh);

	switch (type) {
	case IB_QPT_RELIABLE_CONN: qp->transport = RC; break;
	case IB_QPT_UNRELIABLE_CONN: qp->transport = UC; break;
	case IB_QPT_UNRELIABLE_DGRM: qp->transport = UD; break;
	default: return -EINVAL;
	}

	err = mthca_set_qp_size(dev, cap, qp);
	if (err)
		return err;

	qp->qpn = mthca_alloc(&dev->qp_table.alloc);
	if (qp->qpn == -1)
		return -ENOMEM;

	err = mthca_alloc_qp_common(dev, pd, send_cq, recv_cq,
					send_policy, qp);
	if (err) {
		mthca_free(&dev->qp_table.alloc, qp->qpn);
		return err;
	}

	spin_lock_irq(&dev->qp_table.lock, &lh);
	mthca_array_set(&dev->qp_table.qp,
			qp->qpn & (dev->limits.num_qps - 1), qp);
	spin_unlock_irq(&lh);

	return 0;
}

int mthca_alloc_sqp(struct mthca_dev *dev,
			struct mthca_pd *pd,
			struct mthca_cq *send_cq,
			struct mthca_cq *recv_cq,
			enum ib_sig_type send_policy,
			struct ib_qp_cap *cap,
			int qpn,
			int port,
			struct mthca_sqp *sqp)
{
	u32 mqpn = qpn * 2 + dev->qp_table.sqp_start + port - 1;
	int err;
	SPIN_LOCK_PREP(lhs);
	SPIN_LOCK_PREP(lhr);
	SPIN_LOCK_PREP(lht);

	err = mthca_set_qp_size(dev, cap, &sqp->qp);
	if (err)
		return err;

	alloc_dma_zmem_map(dev, 
		sqp->qp.sq.max * MTHCA_UD_HEADER_SIZE, 
		PCI_DMA_BIDIRECTIONAL,
		&sqp->sg);
	if (!sqp->sg.page)
		return -ENOMEM;

	spin_lock_irq(&dev->qp_table.lock, &lht);
	if (mthca_array_get(&dev->qp_table.qp, mqpn))
		err = -EBUSY;
	else
		mthca_array_set(&dev->qp_table.qp, mqpn, sqp);
	spin_unlock_irq(&lht);

	if (err)
		goto err_out;

	sqp->port = port;
	sqp->qp.qpn 	  = mqpn;
	sqp->qp.transport = MLX;

	err = mthca_alloc_qp_common(dev, pd, send_cq, recv_cq,
					send_policy, &sqp->qp);
	if (err)
		goto err_out_free;

	atomic_inc(&pd->sqp_count);

	return 0;

 err_out_free:
	/*
	 * Lock CQs here, so that CQ polling code can do QP lookup
	 * without taking a lock.
	 */
	spin_lock_irq(&send_cq->lock, &lhs);
	if (send_cq != recv_cq)
		spin_lock(&recv_cq->lock, &lhr);

	spin_lock(&dev->qp_table.lock, &lht);
	mthca_array_clear(&dev->qp_table.qp, mqpn);
	spin_unlock(&lht);

	if (send_cq != recv_cq)
		spin_unlock(&lhr);
	spin_unlock_irq(&lhs);

 err_out:
	free_dma_mem_map(dev, &sqp->sg, PCI_DMA_BIDIRECTIONAL);

	return err;
}

void mthca_free_qp(struct mthca_dev *dev,
		   struct mthca_qp *qp)
{
	u8 status;
	struct mthca_cq *send_cq;
	struct mthca_cq *recv_cq;
	int zombi = 0;
	SPIN_LOCK_PREP(lhs);
	SPIN_LOCK_PREP(lhr);
	SPIN_LOCK_PREP(lht);

	send_cq = to_mcq(qp->ibqp.send_cq);
	recv_cq = to_mcq(qp->ibqp.recv_cq);

	/*
	 * Lock CQs here, so that CQ polling code can do QP lookup
	 * without taking a lock.
	 */
	spin_lock_irq(&send_cq->lock, &lhs);
	if (send_cq != recv_cq)
		spin_lock(&recv_cq->lock, &lhr);

	spin_lock(&dev->qp_table.lock, &lht);
	mthca_array_clear(&dev->qp_table.qp,
			  qp->qpn & (dev->limits.num_qps - 1));
	spin_unlock(&lht);

	if (send_cq != recv_cq)
		spin_unlock(&lhr);
	spin_unlock_irq(&lhs);

	atomic_dec(&qp->refcount);
	wait_event(&qp->wait, !atomic_read(&qp->refcount));

	if (qp->state != IBQPS_RESET) {
		if (mthca_MODIFY_QP(dev, MTHCA_TRANS_ANY2RST, qp->qpn, 0, NULL, 0, &status)) {
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,("modify QP %06x to RESET failed.\n",
				qp->qpn));
			zombi = 1;
		}
	}

	/*
	 * If this is a userspace QP, the buffers, MR, CQs and so on
	 * will be cleaned up in userspace, so all we have to do is
	 * unref the mem-free tables and free the QPN in our table.
	 */
	if (!qp->ibqp.ucontext) {
		mthca_cq_clean(dev, to_mcq(qp->ibqp.send_cq)->cqn, qp->qpn,
				   qp->ibqp.srq ? to_msrq(qp->ibqp.srq) : NULL);
		if (qp->ibqp.send_cq != qp->ibqp.recv_cq)
			mthca_cq_clean(dev, to_mcq(qp->ibqp.recv_cq)->cqn, qp->qpn,
					   qp->ibqp.srq ? to_msrq(qp->ibqp.srq) : NULL);

		mthca_free_memfree(dev, qp);
		mthca_free_wqe_buf(dev, qp);
	}

	mthca_unmap_memfree(dev, qp);

	if (is_sqp(dev, qp)) {
		atomic_dec(&(to_mpd(qp->ibqp.pd)->sqp_count));
		free_dma_mem_map(dev, &to_msqp(qp)->sg, PCI_DMA_BIDIRECTIONAL);
	} else {
		if ( !zombi )
			mthca_free(&dev->qp_table.alloc, qp->qpn);
	}
}

static enum mthca_wr_opcode conv_ibal_wr_opcode(struct _ib_send_wr *wr)
{

	enum mthca_wr_opcode opcode = -1; //= wr->wr_type;

	switch (wr->wr_type) {
		case WR_SEND: 
			opcode = (wr->send_opt & IB_SEND_OPT_IMMEDIATE) ? MTHCA_OPCODE_SEND_IMM : MTHCA_OPCODE_SEND;
			break;
		case WR_RDMA_WRITE: 
			opcode = (wr->send_opt & IB_SEND_OPT_IMMEDIATE) ? MTHCA_OPCODE_RDMA_WRITE_IMM : MTHCA_OPCODE_RDMA_WRITE;
			break;
		case WR_RDMA_READ:		opcode = MTHCA_OPCODE_RDMA_READ; break;
		case WR_COMPARE_SWAP:		opcode = MTHCA_OPCODE_ATOMIC_CS; break;
		case WR_FETCH_ADD:			opcode = MTHCA_OPCODE_ATOMIC_FA; break;
		default:						opcode = MTHCA_OPCODE_INVALID;break;
	}
	return opcode;
}

/* Create UD header for an MLX send and build a data segment for it */
static int build_mlx_header(struct mthca_dev *dev, struct mthca_sqp *sqp,
				int ind, struct _ib_send_wr *wr,
				struct mthca_mlx_seg *mlx,
				struct mthca_data_seg *data)
{
	enum ib_wr_opcode opcode = conv_ibal_wr_opcode(wr);
	int header_size;
	int err;
	__be16 pkey;
	CPU_2_BE64_PREP;

	if (!wr->dgrm.ud.h_av) {
		HCA_PRINT(TRACE_LEVEL_ERROR , HCA_DBG_AV, 
			("absent AV in send wr %p\n", wr));
		return -EINVAL;
	}
		
	ib_ud_header_init(256, /* assume a MAD */
		mthca_ah_grh_present(to_mah((struct ib_ah *)wr->dgrm.ud.h_av)),
		&sqp->ud_header);

	err = mthca_read_ah(dev, to_mah((struct ib_ah *)wr->dgrm.ud.h_av), &sqp->ud_header);
	if (err){
		HCA_PRINT(TRACE_LEVEL_ERROR , HCA_DBG_AV, ("read av error%p\n",
			to_mah((struct ib_ah *)wr->dgrm.ud.h_av)->av));
		return err;
	}
	mlx->flags &= ~cl_hton32(MTHCA_NEXT_SOLICIT | 1);
	mlx->flags |= cl_hton32((!sqp->qp.ibqp.qp_num ? MTHCA_MLX_VL15 : 0) |
				  (sqp->ud_header.lrh.destination_lid ==
				   IB_LID_PERMISSIVE ? MTHCA_MLX_SLR : 0) |
				  (sqp->ud_header.lrh.service_level << 8));
	mlx->rlid = sqp->ud_header.lrh.destination_lid;
	mlx->vcrc = 0;

	switch (opcode) {
	case MTHCA_OPCODE_SEND:
		sqp->ud_header.bth.opcode = IB_OPCODE_UD_SEND_ONLY;
		sqp->ud_header.immediate_present = 0;
		break;
	case MTHCA_OPCODE_SEND_IMM:
		sqp->ud_header.bth.opcode = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
		sqp->ud_header.immediate_present = 1;
		sqp->ud_header.immediate_data = wr->immediate_data;
		break;
	default:
		return -EINVAL;
	}

	sqp->ud_header.lrh.virtual_lane    = !sqp->qp.ibqp.qp_num ? 15 : 0;
	if (sqp->ud_header.lrh.destination_lid == IB_LID_PERMISSIVE)
		sqp->ud_header.lrh.source_lid = IB_LID_PERMISSIVE;
	sqp->ud_header.bth.solicited_event = (u8)!!(wr->send_opt & IB_SEND_OPT_SOLICITED);
	if (!sqp->qp.ibqp.qp_num)
		ib_get_cached_pkey(&dev->ib_dev, (u8)sqp->port,
				   sqp->pkey_index, &pkey);
	else
		ib_get_cached_pkey(&dev->ib_dev, (u8)sqp->port,
				   wr->dgrm.ud.pkey_index, &pkey);
	sqp->ud_header.bth.pkey = pkey;
	sqp->ud_header.bth.destination_qpn = wr->dgrm.ud.remote_qp;
	sqp->ud_header.bth.psn = cl_hton32((sqp->send_psn++) & ((1 << 24) - 1));
	sqp->ud_header.deth.qkey = wr->dgrm.ud.remote_qkey & 0x00000080 ?
						   cl_hton32(sqp->qkey) : wr->dgrm.ud.remote_qkey;
	sqp->ud_header.deth.source_qpn = cl_hton32(sqp->qp.ibqp.qp_num);

	header_size = ib_ud_header_pack(&sqp->ud_header,
					(u8*)sqp->sg.page +
					ind * MTHCA_UD_HEADER_SIZE);

	data->byte_count = cl_hton32(header_size);
	data->lkey		 = cl_hton32(to_mpd(sqp->qp.ibqp.pd)->ntmr.ibmr.lkey);
	data->addr		 = CPU_2_BE64(sqp->sg.dma_address +
					   ind * MTHCA_UD_HEADER_SIZE);

	return 0;
}

static inline int mthca_wq_overflow(struct mthca_wq *wq, int nreq,
					struct ib_cq *ib_cq)
{
	unsigned cur;
	struct mthca_cq *cq;
	SPIN_LOCK_PREP(lh);

	cur = wq->head - wq->tail;
	if (likely((int)cur + nreq < wq->max))
		return 0;

	cq = to_mcq(ib_cq);
	spin_lock_dpc(&cq->lock, &lh);
	cur = wq->head - wq->tail;
	spin_unlock_dpc(&lh);

	return (int)cur + nreq >= wq->max;
}

int mthca_tavor_post_send(struct ib_qp *ibqp, struct _ib_send_wr *wr,
			  struct _ib_send_wr **bad_wr)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	struct mthca_qp *qp = to_mqp(ibqp);
	u8 *wqe;
	u8 *prev_wqe;
	int err = 0;
	int nreq;
	int i;
	int size;
	int size0 = 0;
	u32 f0 = unlikely(wr->send_opt & IB_SEND_OPT_FENCE) ? MTHCA_SEND_DOORBELL_FENCE : 0;
	int ind;
	u8 op0 = 0;
	enum ib_wr_opcode opcode;
	SPIN_LOCK_PREP(lh);   

	spin_lock_irqsave(&qp->sq.lock, &lh);
	
	/* XXX check that state is OK to post send */

	ind = qp->sq.next_ind;

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (mthca_wq_overflow(&qp->sq, nreq, qp->ibqp.send_cq)) {
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,("SQ %06x full (%u head, %u tail,"
					" %d max, %d nreq)\n", qp->qpn,
					qp->sq.head, qp->sq.tail,
					qp->sq.max, nreq));
			err = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_send_wqe(qp, ind);
		prev_wqe = qp->sq.last;
		qp->sq.last = wqe;
		opcode = conv_ibal_wr_opcode(wr);

		((struct mthca_next_seg *) wqe)->nda_op = 0;
		((struct mthca_next_seg *) wqe)->ee_nds = 0;
		((struct mthca_next_seg *) wqe)->flags =
			((wr->send_opt & IB_SEND_OPT_SIGNALED) ?
			 cl_hton32(MTHCA_NEXT_CQ_UPDATE) : 0) |
			((wr->send_opt & IB_SEND_OPT_SOLICITED) ?
			 cl_hton32(MTHCA_NEXT_SOLICIT) : 0)   |
			cl_hton32(1);
		if (opcode == MTHCA_OPCODE_SEND_IMM||
			opcode == MTHCA_OPCODE_RDMA_WRITE_IMM)
			((struct mthca_next_seg *) wqe)->imm = wr->immediate_data;

		wqe += sizeof (struct mthca_next_seg);
		size = sizeof (struct mthca_next_seg) / 16;

		switch (qp->transport) {
		case RC:
			switch (opcode) {
			case MTHCA_OPCODE_ATOMIC_CS:
			case MTHCA_OPCODE_ATOMIC_FA:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;

				wqe += sizeof (struct mthca_raddr_seg);

				if (opcode == MTHCA_OPCODE_ATOMIC_CS) {
					((struct mthca_atomic_seg *) wqe)->swap_add =
						(wr->remote_ops.atomic2);
					((struct mthca_atomic_seg *) wqe)->compare =
						(wr->remote_ops.atomic1);
				} else {
					((struct mthca_atomic_seg *) wqe)->swap_add =
						(wr->remote_ops.atomic1);
					((struct mthca_atomic_seg *) wqe)->compare = 0;
				}

				wqe += sizeof (struct mthca_atomic_seg);
				size += (sizeof (struct mthca_raddr_seg) +
					sizeof (struct mthca_atomic_seg)) / 16 ;
				break;

			case MTHCA_OPCODE_RDMA_READ:
			case MTHCA_OPCODE_RDMA_WRITE:
			case MTHCA_OPCODE_RDMA_WRITE_IMM:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;
				wqe += sizeof (struct mthca_raddr_seg);
				size += sizeof (struct mthca_raddr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}

			break;

		case UC:
			switch (opcode) {
			case MTHCA_OPCODE_RDMA_WRITE:
			case MTHCA_OPCODE_RDMA_WRITE_IMM:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;
				wqe += sizeof (struct mthca_raddr_seg);
				size += sizeof (struct mthca_raddr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}

			break;

		case UD:
			((struct mthca_tavor_ud_seg *) wqe)->lkey =
				cl_hton32(to_mah((struct ib_ah *)wr->dgrm.ud.h_av)->key);
			((struct mthca_tavor_ud_seg *) wqe)->av_addr =
				cl_hton64(to_mah((struct ib_ah *)wr->dgrm.ud.h_av)->avdma);
			((struct mthca_tavor_ud_seg *) wqe)->dqpn = wr->dgrm.ud.remote_qp;
			((struct mthca_tavor_ud_seg *) wqe)->qkey = wr->dgrm.ud.remote_qkey;

			wqe += sizeof (struct mthca_tavor_ud_seg);
			size += sizeof (struct mthca_tavor_ud_seg) / 16;
			break;

		case MLX:
			err = build_mlx_header(dev, to_msqp(qp), ind, wr,
						   (void*)(wqe - sizeof (struct mthca_next_seg)),
						   (void*)wqe);
			if (err) {
				if (bad_wr)
					*bad_wr = wr;
				goto out;
			}
			wqe += sizeof (struct mthca_data_seg);
			size += sizeof (struct mthca_data_seg) / 16;
			break;
		}

		if ((int)(int)wr->num_ds > qp->sq.max_gs) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP ,("SQ %06x too many gathers\n",qp->qpn));
			err = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}
		if (wr->send_opt & IB_SEND_OPT_INLINE) {
			if (wr->num_ds) {
				struct mthca_inline_seg *seg = (struct mthca_inline_seg *)wqe;
				uint32_t s = 0;

				wqe += sizeof *seg;
				for (i = 0; i < (int)wr->num_ds; ++i) {
					struct _ib_local_ds *sge = &wr->ds_array[i];

					s += sge->length;

					if (s > (uint32_t)qp->max_inline_data) {
						err = -EINVAL;
						if (bad_wr)
							*bad_wr = wr;
						goto out;
					}

					memcpy(wqe, (void *) (ULONG_PTR) sge->vaddr,
						   sge->length);
					wqe += sge->length;
				}

				seg->byte_count = cl_hton32(MTHCA_INLINE_SEG | s);
				size += align(s + sizeof *seg, 16) / 16;
			}
		} else {
				
			for (i = 0; i < (int)wr->num_ds; ++i) {
				((struct mthca_data_seg *) wqe)->byte_count =
					cl_hton32(wr->ds_array[i].length);
				((struct mthca_data_seg *) wqe)->lkey =
					cl_hton32(wr->ds_array[i].lkey);
				((struct mthca_data_seg *) wqe)->addr =
					cl_hton64(wr->ds_array[i].vaddr);
				wqe += sizeof (struct mthca_data_seg);
				size += sizeof (struct mthca_data_seg) / 16;
				HCA_PRINT(TRACE_LEVEL_VERBOSE ,HCA_DBG_QP ,("SQ %06x [%02x]  lkey 0x%08x vaddr 0x%I64x 0x%x\n",qp->qpn,i,
					(wr->ds_array[i].lkey),(wr->ds_array[i].vaddr),wr->ds_array[i].length));
			}
		}

		/* Add one more inline data segment for ICRC */
		if (qp->transport == MLX) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cl_hton32((unsigned long)((1 << 31) | 4));
			((u32 *) wqe)[1] = 0;
			wqe += sizeof (struct mthca_data_seg);
			size += sizeof (struct mthca_data_seg) / 16;
		}

		qp->wrid[ind + qp->rq.max] = wr->wr_id;

		if (opcode == MTHCA_OPCODE_INVALID) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP ,("SQ %06x opcode invalid\n",qp->qpn));
			err = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		((struct mthca_next_seg *) prev_wqe)->nda_op =
			cl_hton32(((ind << qp->sq.wqe_shift) +	
			qp->send_wqe_offset) |opcode);
		wmb();
		((struct mthca_next_seg *) prev_wqe)->ee_nds =
			cl_hton32((size0 ? 0 : MTHCA_NEXT_DBD) | size |
				((wr->send_opt & IB_SEND_OPT_FENCE) ?
				MTHCA_NEXT_FENCE : 0));

		if (!size0) {
			size0 = size;
			op0   = opcode;
		}

		dump_wqe( TRACE_LEVEL_VERBOSE, (u32*)qp->sq.last,qp);

		++ind;
		if (unlikely(ind >= qp->sq.max))
			ind -= qp->sq.max;
	}

out:
	if (likely(nreq)) {
		__be32 doorbell[2];

		doorbell[0] = cl_hton32(((qp->sq.next_ind << qp->sq.wqe_shift) +
					   qp->send_wqe_offset) | f0 | op0);
		doorbell[1] = cl_hton32((qp->qpn << 8) | size0);

		wmb();

		mthca_write64(doorbell,
				  dev->kar + MTHCA_SEND_DOORBELL,
				  MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
	}

	qp->sq.next_ind = ind;
	qp->sq.head    += nreq;
	
	spin_unlock_irqrestore(&lh);   
	return err;
}

int mthca_tavor_post_recv(struct ib_qp *ibqp, struct _ib_recv_wr *wr,
				 struct _ib_recv_wr **bad_wr)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	struct mthca_qp *qp = to_mqp(ibqp);
	__be32 doorbell[2];
	int err = 0;
	int nreq;
	int i;
	int size;
	int size0 = 0;
	int ind;
	u8 *wqe;
	u8 *prev_wqe;
	SPIN_LOCK_PREP(lh);

	spin_lock_irqsave(&qp->rq.lock, &lh);

	/* XXX check that state is OK to post receive */

	ind = qp->rq.next_ind;

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (unlikely(nreq == MTHCA_TAVOR_MAX_WQES_PER_RECV_DB)) {
			nreq = 0;

			doorbell[0] = cl_hton32((qp->rq.next_ind << qp->rq.wqe_shift) | size0);
			doorbell[1] = cl_hton32(qp->qpn << 8);

			wmb();

			mthca_write64(doorbell, dev->kar + MTHCA_RECV_DOORBELL,
			  MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));

			qp->rq.head += MTHCA_TAVOR_MAX_WQES_PER_RECV_DB;
			size0 = 0;
		}
		if (mthca_wq_overflow(&qp->rq, nreq, qp->ibqp.recv_cq)) {
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,("RQ %06x full (%u head, %u tail,"
					" %d max, %d nreq)\n", qp->qpn,
					qp->rq.head, qp->rq.tail,
					qp->rq.max, nreq));
			err = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_recv_wqe(qp, ind);
		prev_wqe = qp->rq.last;
		qp->rq.last = wqe;

		((struct mthca_next_seg *) wqe)->nda_op = 0;
		((struct mthca_next_seg *) wqe)->ee_nds =
			cl_hton32(MTHCA_NEXT_DBD);
		((struct mthca_next_seg *) wqe)->flags = 0;

		wqe += sizeof (struct mthca_next_seg);
		size = sizeof (struct mthca_next_seg) / 16;

		if (unlikely((int)wr->num_ds > qp->rq.max_gs)) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP ,("RQ %06x too many gathers\n",qp->qpn));
			err = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		for (i = 0; i < (int)wr->num_ds; ++i) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cl_hton32(wr->ds_array[i].length);
			((struct mthca_data_seg *) wqe)->lkey =
				cl_hton32(wr->ds_array[i].lkey);
			((struct mthca_data_seg *) wqe)->addr =
				cl_hton64(wr->ds_array[i].vaddr);
			wqe += sizeof (struct mthca_data_seg);
			size += sizeof (struct mthca_data_seg) / 16;
//			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP ,("RQ %06x [%02x]	lkey 0x%08x vaddr 0x%I64x 0x %x 0x%08x\n",i,qp->qpn,
//				(wr->ds_array[i].lkey),(wr->ds_array[i].vaddr),wr->ds_array[i].length, wr->wr_id));
		}

		qp->wrid[ind] = wr->wr_id;

		((struct mthca_next_seg *) prev_wqe)->nda_op =
			cl_hton32((ind << qp->rq.wqe_shift) | 1);
		wmb();
		((struct mthca_next_seg *) prev_wqe)->ee_nds =
			cl_hton32(MTHCA_NEXT_DBD | size);

		if (!size0)
			size0 = size;

		dump_wqe(TRACE_LEVEL_VERBOSE,  (u32*)wqe ,qp);
		
		++ind;
		if (unlikely(ind >= qp->rq.max))
			ind -= qp->rq.max;
	}

out:
	if (likely(nreq)) {
		doorbell[0] = cl_hton32((qp->rq.next_ind << qp->rq.wqe_shift) | size0);
		doorbell[1] = cl_hton32((qp->qpn << 8) | (nreq & 255));

		wmb();

		mthca_write64(doorbell, dev->kar + MTHCA_RECV_DOORBELL,
		  MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
	}

	qp->rq.next_ind = ind;
	qp->rq.head    += nreq;

	spin_unlock_irqrestore(&lh);
	return err;
}

int mthca_arbel_post_send(struct ib_qp *ibqp, struct _ib_send_wr *wr,
			  struct _ib_send_wr **bad_wr)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	struct mthca_qp *qp = to_mqp(ibqp);
	__be32 doorbell[2];
	u8 *wqe;
	u8 *prev_wqe;
	int err = 0;
	int nreq;
	int i;
	int size;
	int size0 = 0;
	u32 f0 = unlikely(wr->send_opt & IB_SEND_OPT_FENCE) ? MTHCA_SEND_DOORBELL_FENCE : 0;
	int ind;
	u8 op0 = 0;
	enum ib_wr_opcode opcode;
	SPIN_LOCK_PREP(lh);

	spin_lock_irqsave(&qp->sq.lock, &lh);

	/* XXX check that state is OK to post send */

	ind = qp->sq.head & (qp->sq.max - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (unlikely(nreq == MTHCA_ARBEL_MAX_WQES_PER_SEND_DB)) {
			nreq = 0;
			doorbell[0] = cl_hton32((MTHCA_ARBEL_MAX_WQES_PER_SEND_DB << 24) |
				((qp->sq.head & 0xffff) << 8) |f0 | op0);
			doorbell[1] = cl_hton32((qp->qpn << 8) | size0);
			qp->sq.head += MTHCA_ARBEL_MAX_WQES_PER_SEND_DB;
			size0 = 0;
			f0 = unlikely(wr->send_opt & IB_SEND_OPT_FENCE) ? MTHCA_SEND_DOORBELL_FENCE : 0;

			/*
			 * Make sure that descriptors are written before
			 * doorbell record.
			 */
			wmb();
			*qp->sq.db = cl_hton32(qp->sq.head & 0xffff);

			/*
			 * Make sure doorbell record is written before we
			 * write MMIO send doorbell.
			 */
			wmb();
			mthca_write64(doorbell, dev->kar + MTHCA_SEND_DOORBELL,
				MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
		}

		if (mthca_wq_overflow(&qp->sq, nreq, qp->ibqp.send_cq)) {
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,("SQ %06x full (%u head, %u tail,"
					" %d max, %d nreq)\n", qp->qpn,
					qp->sq.head, qp->sq.tail,
					qp->sq.max, nreq));
			err = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_send_wqe(qp, ind);
		prev_wqe = qp->sq.last;
		qp->sq.last = wqe;
		opcode = conv_ibal_wr_opcode(wr);

		((struct mthca_next_seg *) wqe)->flags =
			((wr->send_opt & IB_SEND_OPT_SIGNALED) ?
			 cl_hton32(MTHCA_NEXT_CQ_UPDATE) : 0) |
			((wr->send_opt & IB_SEND_OPT_SOLICITED) ?
			 cl_hton32(MTHCA_NEXT_SOLICIT) : 0)   |
			((wr->send_opt & IB_SEND_OPT_TX_IP_CSUM) ?
			 cl_hton32(MTHCA_NEXT_IP_CSUM) : 0) |
			((wr->send_opt & IB_SEND_OPT_TX_TCP_UDP_CSUM) ?
			 cl_hton32(MTHCA_NEXT_TCP_UDP_CSUM) : 0) |
			 cl_hton32(1);
		if (opcode == MTHCA_OPCODE_SEND_IMM||
			opcode == MTHCA_OPCODE_RDMA_WRITE_IMM)
			((struct mthca_next_seg *) wqe)->imm = wr->immediate_data;

		wqe += sizeof (struct mthca_next_seg);
		size = sizeof (struct mthca_next_seg) / 16;

		switch (qp->transport) {
		case RC:
			switch (opcode) {
			case MTHCA_OPCODE_ATOMIC_CS:
			case MTHCA_OPCODE_ATOMIC_FA:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;

				wqe += sizeof (struct mthca_raddr_seg);

				if (opcode == MTHCA_OPCODE_ATOMIC_CS) {
					((struct mthca_atomic_seg *) wqe)->swap_add =
						(wr->remote_ops.atomic2);
					((struct mthca_atomic_seg *) wqe)->compare =
						(wr->remote_ops.atomic1);
				} else {
					((struct mthca_atomic_seg *) wqe)->swap_add =
						(wr->remote_ops.atomic1);
					((struct mthca_atomic_seg *) wqe)->compare = 0;
				}

				wqe += sizeof (struct mthca_atomic_seg);
				size += (sizeof (struct mthca_raddr_seg) +
					sizeof (struct mthca_atomic_seg)) / 16 ;
				break;

			case MTHCA_OPCODE_RDMA_READ:
			case MTHCA_OPCODE_RDMA_WRITE:
			case MTHCA_OPCODE_RDMA_WRITE_IMM:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;
				wqe += sizeof (struct mthca_raddr_seg);
				size += sizeof (struct mthca_raddr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}

			break;

		case UC:
			switch (opcode) {
			case MTHCA_OPCODE_RDMA_WRITE:
			case MTHCA_OPCODE_RDMA_WRITE_IMM:
				((struct mthca_raddr_seg *) wqe)->raddr =
					cl_hton64(wr->remote_ops.vaddr);
				((struct mthca_raddr_seg *) wqe)->rkey =
					wr->remote_ops.rkey;
				((struct mthca_raddr_seg *) wqe)->reserved = 0;
				wqe += sizeof (struct mthca_raddr_seg);
				size += sizeof (struct mthca_raddr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}

			break;

		case UD:
			memcpy(((struct mthca_arbel_ud_seg *) wqe)->av,
				   to_mah((struct ib_ah *)wr->dgrm.ud.h_av)->av, MTHCA_AV_SIZE);
			((struct mthca_arbel_ud_seg *) wqe)->dqpn = wr->dgrm.ud.remote_qp;
			((struct mthca_arbel_ud_seg *) wqe)->qkey = wr->dgrm.ud.remote_qkey;

			wqe += sizeof (struct mthca_arbel_ud_seg);
			size += sizeof (struct mthca_arbel_ud_seg) / 16;
			break;

		case MLX:
			err = build_mlx_header(dev, to_msqp(qp), ind, wr,
						   (void*)(wqe - sizeof (struct mthca_next_seg)),
						   (void*)wqe);
			if (err) {
				if (bad_wr)
					*bad_wr = wr;
				goto out;
			}
			wqe += sizeof (struct mthca_data_seg);
			size += sizeof (struct mthca_data_seg) / 16;
			break;
		}

		if ((int)wr->num_ds > qp->sq.max_gs) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP ,("SQ %06x full too many gathers\n",qp->qpn));
			err = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}
		if (wr->send_opt & IB_SEND_OPT_INLINE) {
			if (wr->num_ds) {
				struct mthca_inline_seg *seg = (struct mthca_inline_seg *)wqe;
				uint32_t s = 0;

				wqe += sizeof *seg;
				for (i = 0; i < (int)wr->num_ds; ++i) {
					struct _ib_local_ds *sge = &wr->ds_array[i];

					s += sge->length;

					if (s > (uint32_t)qp->max_inline_data) {
						err = -EINVAL;
						if (bad_wr)
							*bad_wr = wr;
						goto out;
					}

					memcpy(wqe, (void *) (uintptr_t) sge->vaddr,
						   sge->length);
					wqe += sge->length;
				}

				seg->byte_count = cl_hton32(MTHCA_INLINE_SEG | s);
				size += align(s + sizeof *seg, 16) / 16;
			}
		} else {
			for (i = 0; i < (int)wr->num_ds; ++i) {
				((struct mthca_data_seg *) wqe)->byte_count =
					cl_hton32(wr->ds_array[i].length);
				((struct mthca_data_seg *) wqe)->lkey =
					cl_hton32(wr->ds_array[i].lkey);
				((struct mthca_data_seg *) wqe)->addr =
					cl_hton64(wr->ds_array[i].vaddr);
				wqe += sizeof (struct mthca_data_seg);
				size += sizeof (struct mthca_data_seg) / 16;
			}
		}

		/* Add one more inline data segment for ICRC */
		if (qp->transport == MLX) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cl_hton32((unsigned long)((1 << 31) | 4));
			((u32 *) wqe)[1] = 0;
			wqe += sizeof (struct mthca_data_seg);
			size += sizeof (struct mthca_data_seg) / 16;
		}

		qp->wrid[ind + qp->rq.max] = wr->wr_id;

		if (opcode == MTHCA_OPCODE_INVALID) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP ,("SQ %06x opcode invalid\n",qp->qpn));
			err = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		((struct mthca_next_seg *) prev_wqe)->nda_op =
			cl_hton32(((ind << qp->sq.wqe_shift) +
			qp->send_wqe_offset) |opcode);
		wmb();
		((struct mthca_next_seg *) prev_wqe)->ee_nds =
			cl_hton32(MTHCA_NEXT_DBD | size |
			((wr->send_opt & IB_SEND_OPT_FENCE) ?
			MTHCA_NEXT_FENCE : 0));
		
		if (!size0) {
			size0 = size;
			op0   = opcode;
		}

		++ind;
		if (unlikely(ind >= qp->sq.max))
			ind -= qp->sq.max;
	}

out:
	if (likely(nreq)) {
		doorbell[0] = cl_hton32((nreq << 24) |
			((qp->sq.head & 0xffff) << 8) |f0 | op0);
		doorbell[1] = cl_hton32((qp->qpn << 8) | size0);
		qp->sq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();
		*qp->sq.db = cl_hton32(qp->sq.head & 0xffff);

		/*
		 * Make sure doorbell record is written before we
		 * write MMIO send doorbell.
		 */
		wmb();
		mthca_write64(doorbell,
				  dev->kar + MTHCA_SEND_DOORBELL,
				  MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
	}

	spin_unlock_irqrestore(&lh);
	return err;
}

int mthca_arbel_post_recv(struct ib_qp *ibqp, struct _ib_recv_wr *wr,
				 struct _ib_recv_wr **bad_wr)
{
	struct mthca_qp *qp = to_mqp(ibqp);
	int err = 0;
	int nreq;
	int ind;
	int i;
	u8 *wqe;
	SPIN_LOCK_PREP(lh);

	spin_lock_irqsave(&qp->rq.lock, &lh);

	/* XXX check that state is OK to post receive */

	ind = qp->rq.head & (qp->rq.max - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (mthca_wq_overflow(&qp->rq, nreq, qp->ibqp.recv_cq)) {
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,("RQ %06x full (%u head, %u tail,"
					" %d max, %d nreq)\n", qp->qpn,
					qp->rq.head, qp->rq.tail,
					qp->rq.max, nreq));
			err = -ENOMEM;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		wqe = get_recv_wqe(qp, ind);

		((struct mthca_next_seg *) wqe)->flags = 0;

		wqe += sizeof (struct mthca_next_seg);

		if (unlikely((int)wr->num_ds > qp->rq.max_gs)) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_QP ,("RQ %06x full too many scatter\n",qp->qpn));
			err = -EINVAL;
			if (bad_wr)
				*bad_wr = wr;
			goto out;
		}

		for (i = 0; i < (int)wr->num_ds; ++i) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cl_hton32(wr->ds_array[i].length);
			((struct mthca_data_seg *) wqe)->lkey =
				cl_hton32(wr->ds_array[i].lkey);
			((struct mthca_data_seg *) wqe)->addr =
				cl_hton64(wr->ds_array[i].vaddr);
			wqe += sizeof (struct mthca_data_seg);
		}

		if (i < qp->rq.max_gs) {
			((struct mthca_data_seg *) wqe)->byte_count = 0;
			((struct mthca_data_seg *) wqe)->lkey = cl_hton32(MTHCA_INVAL_LKEY);
			((struct mthca_data_seg *) wqe)->addr = 0;
		}

		qp->wrid[ind] = wr->wr_id;

		++ind;
		if (unlikely(ind >= qp->rq.max))
			ind -= qp->rq.max;
	}
out:
	if (likely(nreq)) {
		qp->rq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();
		*qp->rq.db = cl_hton32(qp->rq.head & 0xffff);
	}

	spin_unlock_irqrestore(&lh);
	return err;
}

void mthca_free_err_wqe(struct mthca_dev *dev, struct mthca_qp *qp, int is_send,
			   int index, int *dbd, __be32 *new_wqe)
{
	struct mthca_next_seg *next;

	UNREFERENCED_PARAMETER(dev);
	
	/*
	 * For SRQs, all WQEs generate a CQE, so we're always at the
	 * end of the doorbell chain.
	 */
	if (qp->ibqp.srq) {
		*new_wqe = 0;
		return;
	}

	if (is_send)
		next = get_send_wqe(qp, index);
	else
		next = get_recv_wqe(qp, index);

	*dbd = !!(next->ee_nds & cl_hton32(MTHCA_NEXT_DBD));
	if (next->ee_nds & cl_hton32(0x3f))
		*new_wqe = (next->nda_op & cl_hton32((unsigned long)~0x3f)) |
			(next->ee_nds & cl_hton32(0x3f));
	else
		*new_wqe = 0;
}

int mthca_init_qp_table(struct mthca_dev *dev)
{
	int err;
	u8 status;
	int i;

	spin_lock_init(&dev->qp_table.lock);
	fill_state_table();

	/*
	 * We reserve 2 extra QPs per port for the special QPs.  The
	 * special QP for port 1 has to be even, so round up.
	 */
	dev->qp_table.sqp_start = (dev->limits.reserved_qps + 1) & ~1UL;
	err = mthca_alloc_init(&dev->qp_table.alloc,
				   dev->limits.num_qps,
				   (1 << 24) - 1,
				   dev->qp_table.sqp_start +
				   MTHCA_MAX_PORTS * 2);
	if (err)
		return err;

	err = mthca_array_init(&dev->qp_table.qp,
				   dev->limits.num_qps);
	if (err) {
		mthca_alloc_cleanup(&dev->qp_table.alloc);
		return err;
	}

	for (i = 0; i < 2; ++i) {
		err = mthca_CONF_SPECIAL_QP(dev, i ? IB_QPT_QP1 : IB_QPT_QP0,
						dev->qp_table.sqp_start + i * 2,
						&status);
		if (err)
			goto err_out;
		if (status) {
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,("CONF_SPECIAL_QP returned "
				   "status %02x, aborting.\n",
				   status));
			err = -EINVAL;
			goto err_out;
		}
	}
	return 0;

 err_out:
	mthca_CONF_SPECIAL_QP(dev, IB_QPT_QP1, 0, &status);
	mthca_CONF_SPECIAL_QP(dev, IB_QPT_QP0, 0, &status);

	mthca_array_cleanup(&dev->qp_table.qp, dev->limits.num_qps);
	mthca_alloc_cleanup(&dev->qp_table.alloc);

	return err;
}

void mthca_cleanup_qp_table(struct mthca_dev *dev)
{
	u8 status;

	mthca_CONF_SPECIAL_QP(dev, IB_QPT_QP1, 0, &status);
	mthca_CONF_SPECIAL_QP(dev, IB_QPT_QP0, 0, &status);

	mthca_array_cleanup(&dev->qp_table.qp, dev->limits.num_qps);
	mthca_alloc_cleanup(&dev->qp_table.alloc);
}



