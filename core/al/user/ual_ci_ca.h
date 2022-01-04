/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#if !defined(__UAL_CI_CA_H__)
#define __UAL_CI_CA_H__

#include <iba/ib_uvp.h>
#include "al_common.h"
/* #include "al_ci_ca.h" */
/* Dummy function declerations */
/* The arguments must be defined later */



ib_api_status_t
create_ci_ca(
	IN		ib_al_handle_t						h_al,
	IN		al_obj_t							*p_parent_obj,
	IN		ib_net64_t							ca_guid );

ib_api_status_t
ual_open_ca(
	IN		const	ib_net64_t					ca_guid,
	IN	OUT			struct _al_ci_ca* const		p_ci_ca );

#if 0
ib_api_status_t
ual_close_ca(
	IN		ib_ca_handle_t						h_ca);
#else
ib_api_status_t
ual_close_ca(
	IN		struct _al_ci_ca					*p_ci_ca );
#endif

ib_api_status_t
ual_modify_ca(
	IN		const ib_ca_handle_t				h_ca,
	IN		const uint8_t						port_num,
	IN		const ib_ca_mod_t					ca_mod,
	IN		const ib_port_attr_mod_t* const		p_port_attr_mod	);

ib_api_status_t
ual_query_ca(
	IN		const	ib_ca_handle_t				h_ca,
		OUT			ib_ca_attr_t* const			p_ca_attr OPTIONAL,
	IN	OUT			uint32_t* const				p_size );

ib_api_status_t
ual_allocate_pd(
	IN				ib_ca_handle_t				h_ca,
	IN		const	ib_pd_type_t				pd_type,
	IN	OUT			ib_pd_handle_t				h_pd );

ib_api_status_t
ual_deallocate_pd(
	IN				ib_pd_handle_t				h_pd );

ib_api_status_t
ual_create_av(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t* const			p_av_attr,
	IN	OUT			ib_av_handle_t				h_av );

ib_api_status_t
ual_pd_alias_create_av(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t* const			p_av_attr,
	IN	OUT			ib_av_handle_t				h_av );

ib_api_status_t
ual_destroy_av(
	IN				ib_av_handle_t				h_av );

ib_api_status_t
ual_modify_av(
	IN				ib_av_handle_t				h_av,
	IN		const	ib_av_attr_t* const			p_av_attr);

ib_api_status_t
ual_query_av(
	IN				ib_av_handle_t				h_av,
		OUT			ib_av_attr_t* const			p_av_attr,
		OUT			ib_pd_handle_t* const		ph_pd );

ib_api_status_t
ual_create_srq(
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_srq_handle_t				h_srq,
	IN		const	ib_srq_attr_t* const			p_srq_attr);

ib_api_status_t
ual_modify_srq(
	IN				ib_srq_handle_t				h_srq,
	IN		const	ib_srq_attr_t*	const			p_srq_attr,
	IN		const	ib_srq_attr_mask_t			srq_attr_mask );

ib_api_status_t
ual_query_srq(
	IN				ib_srq_handle_t				h_srq,
		OUT			ib_srq_attr_t*				p_srq_attr );

ib_api_status_t
ual_destroy_srq(
	IN				ib_srq_handle_t				h_srq );

ib_api_status_t
ual_create_qp(
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_handle_t				h_qp,
	IN		const	ib_qp_create_t* const		p_qp_create,
	IN				ib_qp_attr_t*				p_qp_attr );

ib_api_status_t
ual_modify_qp(
	IN				ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod,
	IN				ib_qp_attr_t*				p_qp_attr );

ib_api_status_t
ual_query_qp(
	IN				ib_qp_handle_t				h_qp,
		OUT			ib_qp_attr_t*				p_qp_attr );

ib_api_status_t
ual_destroy_qp(
	IN				ib_qp_handle_t				h_qp );

ib_api_status_t
ual_create_cq(
	IN				struct _al_ci_ca* const		p_ci_ca,
	IN				ib_cq_create_t* const		p_cq_create,
	IN	OUT			ib_cq_handle_t				h_cq );

ib_api_status_t
ual_modify_cq(
	IN				ib_cq_handle_t				h_cq,
	IN	OUT			uint32_t*					p_size );

ib_api_status_t
ual_query_cq(
	IN				ib_cq_handle_t				h_cq,
		OUT			uint32_t*					p_size );

ib_api_status_t
ual_destroy_cq(
	IN				ib_cq_handle_t				h_cq );

ib_api_status_t
ual_reg_mem(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t* const		p_mr_create,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN	OUT			ib_mr_handle_t				h_mr );

ib_api_status_t
ual_dereg_mr(
	IN				ib_mr_handle_t				h_mr );

ib_api_status_t
ual_modify_mr(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mr_mod_mask,
	IN		const	ib_mr_create_t* const		p_mr_create OPTIONAL,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL );

ib_api_status_t
ual_query_mr(
	IN				ib_mr_handle_t				h_mr,
		OUT			ib_mr_attr_t*				p_mr_attr );

ib_api_status_t
ual_reg_shared(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_access_t					access_ctrl,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN	OUT			ib_mr_handle_t				h_new_mr );

ib_api_status_t
ual_create_mw(
	IN		const	ib_pd_handle_t				h_pd,
		OUT			net32_t* const				p_rkey,
	IN	OUT			ib_mw_handle_t				h_mw );

ib_api_status_t
ual_destroy_mw(
	IN				ib_mw_handle_t				h_mw );

ib_api_status_t
ual_query_mw(
	IN				ib_mw_handle_t				h_mw,
		OUT			ib_pd_handle_t*				ph_pd,
		OUT			net32_t* const				p_rkey );

ib_api_status_t
ual_bind_mw(
	IN		const	ib_mw_handle_t				h_mw,
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_bind_wr_t*				p_mw_bind,
		OUT			net32_t* const				p_rkey );

ib_api_status_t
ual_post_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t* const			p_send_wr,
		OUT			ib_send_wr_t				**pp_send_failure );

ib_api_status_t
ual_post_recv(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_recv_wr_t* const			p_recv_wr,
		OUT			ib_recv_wr_t				**pp_recv_failure );

ib_api_status_t
ual_post_srq_recv(
	IN		const	ib_srq_handle_t				h_srq,
	IN				ib_recv_wr_t* const			p_recv_wr,
		OUT			ib_recv_wr_t				**pp_recv_failure );

ib_api_status_t
ual_peek_cq(
	IN		const	ib_cq_handle_t				h_cq,
	OUT				uint32_t* const				p_n_cqes );

ib_api_status_t
ual_poll_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			ib_wc_t** const				pp_free_wclist,
		OUT			ib_wc_t** const				pp_done_wclist );

ib_api_status_t
ual_rearm_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	boolean_t					solicited );

ib_api_status_t
ual_rearm_n_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	uint32_t					n_cqes );

typedef struct _ual_ci_interface
{
	uvp_interface_t			user_verbs;
	HMODULE					h_uvp_lib;		/* UVP Library Handle */
	ib_net64_t				guid;

} ual_ci_interface_t;

typedef ual_ci_interface_t		verbs_interface_t;

#endif		/* (__UAL_CI_CA_H__) */
