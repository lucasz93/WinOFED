/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#ifndef INFINIBAND_VERBS_H
#define INFINIBAND_VERBS_H

#include "iba\ib_uvp.h"


#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

union ibv_gid {
	uint8_t			raw[16];
	struct {
		uint64_t	subnet_prefix;
		uint64_t	interface_id;
	} global;
};

enum ibv_rate {
	IBV_RATE_MAX      = 0,
	IBV_RATE_2_5_GBPS = 2,
	IBV_RATE_5_GBPS   = 5,
	IBV_RATE_10_GBPS  = 3,
	IBV_RATE_20_GBPS  = 6,
	IBV_RATE_30_GBPS  = 4,
	IBV_RATE_40_GBPS  = 7,
	IBV_RATE_60_GBPS  = 8,
	IBV_RATE_80_GBPS  = 9,
	IBV_RATE_120_GBPS = 10
};

struct ibv_global_route {
	union ibv_gid		dgid;
	uint32_t		flow_label;
	uint8_t			sgid_index;
	uint8_t			hop_limit;
	uint8_t			traffic_class;
};

struct ibv_grh {
	uint32_t		version_tclass_flow;
	uint16_t		paylen;
	uint8_t			next_hdr;
	uint8_t			hop_limit;
	union ibv_gid		sgid;
	union ibv_gid		dgid;
};

struct ibv_ah_attr {
	struct ibv_global_route	grh;
	uint16_t		dlid;
	uint8_t			sl;
	uint8_t			src_path_bits;
	uint8_t			static_rate;
	uint8_t			is_global;
	uint8_t			port_num;
};

struct ibv_xrc_domain {
	struct ibv_context     *context;
	uint64_t		handle;
};

struct ibv_srq_attr {
	uint32_t		max_wr;
	uint32_t		max_sge;
	uint32_t		srq_limit;
};

enum ibv_qp_type {
	IBV_QPT_RC = 2,
	IBV_QPT_UC,
	IBV_QPT_UD,
	IBV_QPT_XRC
};

struct ibv_qp_cap {
	uint32_t		max_send_wr;
	uint32_t		max_recv_wr;
	uint32_t		max_send_sge;
	uint32_t		max_recv_sge;
	uint32_t		max_inline_data;
};

struct ibv_qp_init_attr {
	void		       *qp_context;
	struct ibv_cq	       *send_cq;
	struct ibv_cq	       *recv_cq;
	struct ibv_srq	       *srq;
	struct ibv_qp_cap	cap;
	enum ibv_qp_type qp_type;
	int			sq_sig_all;
	struct ibv_xrc_domain  *xrc_domain;
};

enum ibv_qp_attr_mask {
	IBV_QP_STATE			= 1 << 	0,
	IBV_QP_CUR_STATE		= 1 << 	1,
	IBV_QP_EN_SQD_ASYNC_NOTIFY	= 1 << 	2,
	IBV_QP_ACCESS_FLAGS		= 1 << 	3,
	IBV_QP_PKEY_INDEX		= 1 << 	4,
	IBV_QP_PORT			= 1 << 	5,
	IBV_QP_QKEY			= 1 << 	6,
	IBV_QP_AV			= 1 << 	7,
	IBV_QP_PATH_MTU			= 1 << 	8,
	IBV_QP_TIMEOUT			= 1 << 	9,
	IBV_QP_RETRY_CNT		= 1 << 10,
	IBV_QP_RNR_RETRY		= 1 << 11,
	IBV_QP_RQ_PSN			= 1 << 12,
	IBV_QP_MAX_QP_RD_ATOMIC		= 1 << 13,
	IBV_QP_ALT_PATH			= 1 << 14,
	IBV_QP_MIN_RNR_TIMER		= 1 << 15,
	IBV_QP_SQ_PSN			= 1 << 16,
	IBV_QP_MAX_DEST_RD_ATOMIC	= 1 << 17,
	IBV_QP_PATH_MIG_STATE		= 1 << 18,
	IBV_QP_CAP			= 1 << 19,
	IBV_QP_DEST_QPN			= 1 << 20
};

enum ibv_qp_state {
	IBV_QPS_RESET,
	IBV_QPS_INIT,
	IBV_QPS_RTR,
	IBV_QPS_RTS,
	IBV_QPS_SQD,
	IBV_QPS_SQE,
	IBV_QPS_ERR
};

struct ibv_pd {
	struct ibv_context     *context;
	uint64_t		handle;
};

struct ibv_srq {
	struct ibv_context     *context;
	struct ibv_pd	       *pd;
	uint32_t		xrc_srq_num;
	struct ibv_xrc_domain  *xrc_domain;
	struct ibv_cq	       *xrc_cq;
};

struct ibv_qp {
	struct ibv_context     *context;
	void				   *qp_context;
	struct ibv_pd	       *pd;
	struct ibv_cq	       *send_cq;
	struct ibv_cq	       *recv_cq;
	struct ibv_srq	       *srq;
	uint64_t		handle;
	uint32_t		qp_num;
	enum ibv_qp_state       state;
	enum ibv_qp_type	qp_type;
	struct ibv_xrc_domain  *xrc_domain;
};

struct ibv_cq {
	struct ibv_context     *context;
	int			cqe;
};

struct ibv_ah {
	struct ibv_context     *context;
	struct ibv_pd	       *pd;
	ib_av_attr_t		av_attr;
};

struct ibv_context {
	int				page_size;
};


/************* CA operations *************************/
ib_api_status_t
mlx4_pre_open_ca (
	IN		const	ib_net64_t				ca_guid,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_ca_handle_t			*ph_uvp_ca );

ib_api_status_t
mlx4_post_open_ca (
	IN		const	ib_net64_t				ca_guid,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_ca_handle_t			*ph_uvp_ca,
	IN				ci_umv_buf_t				*p_umv_buf );

ib_api_status_t
mlx4_post_close_ca (
	IN			ib_ca_handle_t				h_uvp_ca,
	IN			ib_api_status_t				ioctl_status );

/************* PD Management ***********************/
extern ib_api_status_t
mlx4_pre_alloc_pd (
	IN		const	ib_ca_handle_t			h_uvp_ca,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_pd_handle_t			*ph_uvp_pd );

void
mlx4_post_alloc_pd (
	IN				ib_ca_handle_t			h_uvp_ca,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_pd_handle_t			*ph_uvp_pd,
	IN				ci_umv_buf_t				*p_umv_buf );

void
mlx4_post_free_pd (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN				ib_api_status_t			ioctl_status );

/************* CQ Management ***********************/
ib_api_status_t
mlx4_pre_create_cq (
	IN		const 	ib_ca_handle_t			h_uvp_ca,
	IN	OUT 		uint32_t* const			p_size,
	IN	OUT 		ci_umv_buf_t				*p_umv_buf,
		OUT			ib_cq_handle_t			*ph_uvp_cq );

void
mlx4_post_create_cq (
	IN		const	ib_ca_handle_t			h_uvp_ca,
	IN				ib_api_status_t			ioctl_status,
	IN		const	uint32_t					size,
	IN	OUT			ib_cq_handle_t			*ph_uvp_cq,
	IN				ci_umv_buf_t				*p_umv_buf );

ib_api_status_t
mlx4_pre_query_cq (
	IN		const	ib_cq_handle_t			h_uvp_cq,
		OUT			uint32_t* const			p_size,
	IN	OUT			ci_umv_buf_t				*p_umv_buf );

void
mlx4_post_destroy_cq (
	IN		const	ib_cq_handle_t			h_uvp_cq,
	IN				ib_api_status_t			ioctl_status );

ib_api_status_t
mlx4_arm_cq (
	IN		const	void*						h_cq,
	IN		const	boolean_t					solicited);

int mlx4_poll_cq_array(const void* h_cq,
			const int num_entries, uvp_wc_t* const wc);

/************* SRQ Management **********************/
ib_api_status_t  
mlx4_pre_create_srq (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	ib_srq_attr_t				*p_srq_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_srq_handle_t			*ph_uvp_srq );

void
mlx4_post_create_srq (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_srq_handle_t			*ph_uvp_srq,
	IN				ci_umv_buf_t				*p_umv_buf );

ib_api_status_t
mlx4_pre_destroy_srq (
	IN		const	ib_srq_handle_t			h_uvp_srq );

void
mlx4_post_destroy_srq (
	IN		const	ib_srq_handle_t			h_uvp_srq,
	IN				ib_api_status_t			ioctl_status );

ib_api_status_t mlx4_post_srq_recv(const void* h_srq,
			ib_recv_wr_t* const wr,
			ib_recv_wr_t** bad_wr);

/************* QP Management ***********************/
ib_api_status_t
mlx4_pre_create_qp (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	ib_qp_create_t			*p_create_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_qp_handle_t			*ph_uvp_qp );

ib_api_status_t
mlx4_wv_pre_create_qp (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	uvp_qp_create_t			*p_create_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_qp_handle_t			*ph_uvp_qp );

ib_api_status_t
mlx4_post_create_qp (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN				ib_api_status_t 			ioctl_status,
	IN	OUT 		ib_qp_handle_t			*ph_uvp_qp,
	IN				ci_umv_buf_t				*p_umv_buf );

ib_api_status_t
mlx4_pre_modify_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp,
	IN		const	ib_qp_mod_t				*p_modify_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf );

void
mlx4_post_modify_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ci_umv_buf_t				*p_umv_buf );

void
mlx4_post_query_qp (
	IN				ib_qp_handle_t			h_uvp_qp,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_qp_attr_t			*p_query_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf );

ib_api_status_t
mlx4_pre_destroy_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp );

void
mlx4_post_destroy_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp,
	IN				ib_api_status_t			ioctl_status );

void
mlx4_nd_modify_qp (
	IN		const	ib_qp_handle_t			h_uvp_qp,
		OUT			void**					pp_outbuf,
		OUT			DWORD*					p_size );

uint32_t
mlx4_nd_get_qp_state (
	IN		const	ib_qp_handle_t			h_uvp_qp );

ib_api_status_t mlx4_post_send(const void* h_qp,
			ib_send_wr_t* const wr,
			ib_send_wr_t** bad_wr);

ib_api_status_t mlx4_post_recv(const void* h_qp,
			ib_recv_wr_t* const wr,
			ib_recv_wr_t** bad_wr);

/************* AV Management ***********************/
ib_api_status_t
mlx4_pre_create_ah (
	IN		const 	ib_pd_handle_t			h_uvp_pd,
	IN		const 	ib_av_attr_t				*p_av_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
	   	OUT			ib_av_handle_t			*ph_uvp_av );

ib_api_status_t
mlx4_pre_query_ah (
	IN		const	ib_av_handle_t			h_uvp_av,
	IN	OUT			ci_umv_buf_t				*p_umv_buf );

void
mlx4_post_query_ah (
	IN		const	ib_av_handle_t			h_uvp_av,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_av_attr_t				*p_addr_vector,
	IN	OUT			ib_pd_handle_t			*ph_pd,
	IN	OUT			ci_umv_buf_t				*p_umv_buf );

ib_api_status_t
mlx4_pre_modify_ah (
	IN		const	ib_av_handle_t			h_uvp_av,
	IN		const	ib_av_attr_t				*p_addr_vector,
	IN	OUT			ci_umv_buf_t				*p_umv_buf );

ib_api_status_t
mlx4_pre_destroy_ah (
	IN		const	ib_av_handle_t			h_uvp_av );

#ifdef XRC_SUPPORT
/************* XRC Management **********************/
ib_api_status_t  
mlx4_pre_create_xrc_srq (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN		const	ib_xrcd_handle_t 			h_uvp_xrcd,
	IN		const	ib_srq_attr_t				*p_srq_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_srq_handle_t			*ph_uvp_srq );

ib_api_status_t  
mlx4_post_create_xrc_srq (
	IN		const	ib_pd_handle_t			h_uvp_pd,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_srq_handle_t			*ph_uvp_srq,
	IN				ci_umv_buf_t				*p_umv_buf );

ib_api_status_t
mlx4_pre_open_xrc_domain (
	IN		const 	ib_ca_handle_t			h_uvp_ca,
	IN		const	uint32_t					oflag,
	IN	OUT 		ci_umv_buf_t				*p_umv_buf,
		OUT			ib_xrcd_handle_t			*ph_uvp_xrcd );

void
mlx4_post_open_xrc_domain (
	IN		const	ib_ca_handle_t			h_uvp_ca,
	IN				ib_api_status_t			ioctl_status,
	IN	OUT			ib_xrcd_handle_t			*ph_uvp_xrcd,
	IN				ci_umv_buf_t				*p_umv_buf );

void
mlx4_post_close_xrc_domain (
	IN		const	ib_xrcd_handle_t			h_uvp_xrcd,
	IN				ib_api_status_t			ioctl_status );

#endif /* XRC_SUPPORT */

END_C_DECLS

#endif /* INFINIBAND_VERBS_H */
