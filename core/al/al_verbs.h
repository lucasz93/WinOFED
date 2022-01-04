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

#if !defined(__AL_VERBS_H__)
#define __AL_VERBS_H__

#include "al_ca.h"
#include "al_cq.h"
#include "al_pd.h"
#include "al_qp.h"
#include "al_srq.h"

#ifndef CL_KERNEL
#include "ual_mad.h"
#include "ual_qp.h"
#include "ual_mcast.h"
#endif

#ifdef CL_KERNEL

	/* Macros for kernel-mode only */
#define verbs_create_av(h_pd, p_av_attr, h_av) \
	h_av->obj.p_ci_ca->verbs.create_av( h_pd->h_ci_pd,\
		p_av_attr, &h_av->h_ci_av, p_umv_buf )

#define verbs_check_av(h_av)	((h_av)->h_ci_av)
#define convert_av_handle(h_qp, h_av)	((h_av)->h_ci_av)
#define verbs_destroy_av(h_av) \
	h_av->obj.p_ci_ca->verbs.destroy_av( h_av->h_ci_av )

#define verbs_query_av(h_av, p_av_attr, ph_pd) \
	h_av->obj.p_ci_ca->verbs.query_av( h_av->h_ci_av,\
		p_av_attr, ph_pd, p_umv_buf )

#define verbs_modify_av(h_av, p_av_mod) \
	h_av->obj.p_ci_ca->verbs.modify_av( h_av->h_ci_av, p_av_mod, p_umv_buf )

#define verbs_query_ca(h_ca, p_ca_attr, p_size) \
	h_ca->obj.p_ci_ca->verbs.query_ca( h_ca->obj.p_ci_ca->h_ci_ca,\
		p_ca_attr, p_size, p_umv_buf )

#define verbs_modify_ca(h_ca, port_num, ca_mod, p_port_attr_mod) \
	h_ca->obj.p_ci_ca->verbs.modify_ca( h_ca->obj.p_ci_ca->h_ci_ca,\
		port_num, ca_mod, p_port_attr_mod )

void ci_ca_comp_cb(void *cq_context);
void ci_ca_async_event_cb(ib_event_rec_t* p_event_record);

static inline ib_api_status_t
verbs_create_cq(
	IN		const	ib_ca_handle_t				h_ca,
	IN	OUT			ib_cq_create_t* const		p_cq_create,
	IN				ib_cq_handle_t				h_cq,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	return h_ca->obj.p_ci_ca->verbs.create_cq(
		(p_umv_buf) ? h_ca->h_um_ca : h_ca->obj.p_ci_ca->h_ci_ca,
		h_cq, ci_ca_async_event_cb, ci_ca_comp_cb, &p_cq_create->size,
		&h_cq->h_ci_cq, p_umv_buf );
}

#define verbs_check_cq(h_cq)	((h_cq)->h_ci_cq)
#define verbs_destroy_cq(h_cq) \
	h_cq->obj.p_ci_ca->verbs.destroy_cq( h_cq->h_ci_cq )

#define verbs_modify_cq(h_cq, p_size) \
	h_cq->obj.p_ci_ca->verbs.resize_cq( h_cq->h_ci_cq, p_size, p_umv_buf )

#define verbs_query_cq(h_cq, p_size) \
	h_cq->obj.p_ci_ca->verbs.query_cq( h_cq->h_ci_cq, p_size, p_umv_buf )

#define verbs_peek_cq(h_cq, p_n_cqes) \
	( ( h_cq->obj.p_ci_ca->verbs.peek_cq ) ? \
		h_cq->obj.p_ci_ca->verbs.peek_cq( h_cq->h_ci_cq, p_n_cqes) : \
		IB_UNSUPPORTED )

#define verbs_poll_cq(h_cq, pp_free_wclist, pp_done_wclist) \
	h_cq->obj.p_ci_ca->verbs.poll_cq( h_cq->h_ci_cq,\
		pp_free_wclist, pp_done_wclist )

#define verbs_rearm_cq(h_cq, solicited) \
	h_cq->obj.p_ci_ca->verbs.enable_cq_notify( h_cq->h_ci_cq,\
		solicited )

#define verbs_rearm_n_cq(h_cq, n_cqes) \
	( ( h_cq->obj.p_ci_ca->verbs.enable_ncomp_cq_notify ) ? \
		h_cq->obj.p_ci_ca->verbs.enable_ncomp_cq_notify(h_cq->h_ci_cq,n_cqes): \
		IB_UNSUPPORTED )

#define verbs_register_mr(h_pd, p_mr_create, p_lkey, p_rkey, h_mr) \
	h_mr->obj.p_ci_ca->verbs.register_mr_remap( h_pd->h_ci_pd,\
		p_mr_create, mapaddr, p_lkey, p_rkey, &h_mr->h_ci_mr, um_call )

#define verbs_register_pmr(h_pd, p_phys_create, p_vaddr,\
				p_lkey, p_rkey, h_mr) \
	h_mr->obj.p_ci_ca->verbs.register_pmr( h_pd->h_ci_pd,\
		p_phys_create, p_vaddr, p_lkey, p_rkey, &h_mr->h_ci_mr )

#define verbs_check_mr(h_mr)	((h_mr)->h_ci_mr)
#define verbs_check_mlnx_fmr(h_fmr)	((h_fmr)->h_ci_fmr)
#define verbs_deregister_mr(h_mr) \
	h_mr->obj.p_ci_ca->verbs.deregister_mr( h_mr->h_ci_mr )

/*
 * Remove this registration from the shmid's list to prevent any
 * new registrations from accessing it once it is deregistered.
 */
#define verbs_release_shmid(h_mr)	\
	if( h_mr->p_shmid )	\
	{	\
		cl_spinlock_acquire( &h_mr->p_shmid->obj.lock );	\
		cl_list_remove_object( &h_mr->p_shmid->mr_list, h_mr );	\
		cl_spinlock_release( &h_mr->p_shmid->obj.lock );	\
		release_shmid( h_mr->p_shmid );	\
		h_mr->p_shmid = NULL;	\
	}
#define verbs_query_mr(h_mr, p_mr_attr) \
	h_mr->obj.p_ci_ca->verbs.query_mr( h_mr->h_ci_mr, p_mr_attr )

#define verbs_modify_mr(h_mr, mr_modify_mask, p_mr_create, \
	p_lkey, p_rkey, h_pd ) \
		h_mr->obj.p_ci_ca->verbs.modify_mr( h_mr->h_ci_mr, mr_modify_mask, \
			p_mr_create, p_lkey, p_rkey, h_pd ? h_pd->h_ci_pd : NULL, \
			um_call )

#define verbs_modify_pmr(h_mr, mr_modify_mask, p_pmr_create, \
	p_vaddr, p_lkey, p_rkey, h_pd ) \
		h_mr->obj.p_ci_ca->verbs.modify_pmr( h_mr->h_ci_mr, mr_modify_mask, \
			p_pmr_create, p_vaddr, p_lkey, p_rkey, \
			h_pd ? h_pd->h_ci_pd : NULL )

#define verbs_register_smr(h_mr, h_pd, access_ctrl, p_vaddr, p_lkey, \
	p_rkey, ph_mr ) \
		h_mr->obj.p_ci_ca->verbs.register_smr( h_mr->h_ci_mr, h_pd->h_ci_pd,\
			access_ctrl, p_vaddr, p_lkey, p_rkey, &(ph_mr->h_ci_mr), \
			um_call )

#define verbs_create_mlnx_fmr(h_pd, p_fmr_create, h_fmr ) \
	h_fmr->obj.p_ci_ca->verbs.alloc_mlnx_fmr( h_pd->h_ci_pd,\
		p_fmr_create, &h_fmr->h_ci_fmr )

#define verbs_map_phys_mlnx_fmr( h_fmr, plist_addr, list_len, p_vaddr, p_lkey, p_rkey) \
	h_fmr->obj.p_ci_ca->verbs.map_phys_mlnx_fmr( h_fmr->h_ci_fmr,\
		plist_addr, list_len, p_vaddr, p_lkey, p_rkey )

#define verbs_unmap_mlnx_fmr( h_fmr, p_fmr_array ) \
	h_fmr->obj.p_ci_ca->verbs.unmap_mlnx_fmr( p_fmr_array)

#define verbs_destroy_mlnx_fmr( h_fmr ) \
	h_fmr->obj.p_ci_ca->verbs.dealloc_mlnx_fmr( h_fmr->h_ci_fmr )
	

#define verbs_create_mw(h_pd, p_rkey, h_mw) \
	h_mw->obj.p_ci_ca->verbs.create_mw( h_pd->h_ci_pd,\
		p_rkey, &h_mw->h_ci_mw, p_umv_buf )

#define verbs_check_mw(h_mw)	((h_mw)->h_ci_mw)
#define verbs_destroy_mw(h_mw) \
	h_mw->obj.p_ci_ca->verbs.destroy_mw( h_mw->h_ci_mw )

#define verbs_query_mw(h_mw, ph_pd, p_rkey) \
	h_mw->obj.p_ci_ca->verbs.query_mw(\
		h_mw->h_ci_mw, ph_pd, p_rkey, p_umv_buf )

#define convert_mr_handle(h_mr)	((h_mr)->h_ci_mr)

#define verbs_bind_mw(h_mw, h_qp, p_mw_bind, p_rkey) \
	h_qp->obj.p_ci_ca->verbs.bind_mw( h_mw->h_ci_mw,\
		h_qp->h_ci_qp, p_mw_bind, p_rkey )

static inline ib_api_status_t
verbs_allocate_pd(
	IN		const	ib_ca_handle_t				h_ca,
	IN				ib_pd_handle_t				h_pd,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	return h_ca->obj.p_ci_ca->verbs.allocate_pd(
		(p_umv_buf) ? h_ca->h_um_ca : h_ca->obj.p_ci_ca->h_ci_ca,
		h_pd->type, &h_pd->h_ci_pd, p_umv_buf );
}

/*
 * Reference the hardware PD.
 */
static inline ib_api_status_t
allocate_pd_alias(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_pd_handle_t				h_pd )
{
	UNUSED_PARAM( h_ca );
	h_pd->h_ci_pd = h_pd->obj.p_ci_ca->h_pd->h_ci_pd;
	ref_al_obj( &h_pd->obj.p_ci_ca->h_pd->obj );
	return IB_SUCCESS;
}

static inline void
deallocate_pd_alias(
	IN		const	ib_pd_handle_t				h_pd )
{
	deref_al_obj( &h_pd->obj.p_ci_ca->h_pd->obj );
}



#define verbs_check_pd(h_pd)	((h_pd)->h_ci_pd)
#define verbs_deallocate_pd(h_pd) \
	h_pd->obj.p_ci_ca->verbs.deallocate_pd( h_pd->h_ci_pd )

static inline ib_api_status_t
verbs_create_srq(
	IN		const	ib_pd_handle_t				h_pd,
	IN				ib_srq_handle_t				h_srq,
	IN		const	ib_srq_attr_t* const			p_srq_attr,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t		status;

	status = h_srq->obj.p_ci_ca->verbs.create_srq(
		h_pd->h_ci_pd, h_srq, ci_ca_async_event_cb, p_srq_attr,
		&h_srq->h_ci_srq, p_umv_buf );

	h_srq->h_recv_srq = h_srq->h_ci_srq;
	h_srq->pfn_post_srq_recv = h_srq->obj.p_ci_ca->verbs.post_srq_recv;
	return status;
}

#define verbs_check_srq(h_srq)	((h_srq)->h_ci_srq)

#define verbs_destroy_srq(h_srq) \
	h_srq->obj.p_ci_ca->verbs.destroy_srq( h_srq->h_ci_srq )

#define verbs_query_srq(h_srq, p_srq_attr) \
	h_srq->obj.p_ci_ca->verbs.query_srq( h_srq->h_ci_srq,\
		p_srq_attr, p_umv_buf )

#define verbs_modify_srq(h_srq, p_srq_attr, srq_attr_mask) \
	h_srq->obj.p_ci_ca->verbs.modify_srq( h_srq->h_ci_srq,\
		p_srq_attr, srq_attr_mask, p_umv_buf )

#define verbs_post_srq_recv(h_srq, p_recv_wr, pp_recv_failure) \
	h_srq->obj.p_ci_ca->verbs.post_srq_recv( h_srq->h_ci_srq,\
		p_recv_wr, pp_recv_failure )

#define convert_qp_handle( qp_create ) {\
	CL_ASSERT( qp_create.h_rq_cq );	\
	qp_create.h_rq_cq = qp_create.h_rq_cq->h_ci_cq;	\
	CL_ASSERT( qp_create.h_sq_cq );	\
	qp_create.h_sq_cq = qp_create.h_sq_cq->h_ci_cq;	\
	if (qp_create.h_srq) \
		qp_create.h_srq = qp_create.h_srq->h_ci_srq;	\
}

static inline ib_api_status_t
verbs_get_spl_qp(
	IN				ib_pd_handle_t				h_pd,
	IN				uint8_t						port_num,
	IN				ib_qp_handle_t				h_qp,
	IN				ib_qp_create_t				*p_qp_create,
	IN				ib_qp_attr_t				*p_qp_attr )
{
	ib_api_status_t		status;

	status = h_qp->obj.p_ci_ca->verbs.create_spl_qp(
		h_pd->h_ci_pd, port_num, h_qp, ci_ca_async_event_cb, p_qp_create,
		p_qp_attr, &h_qp->h_ci_qp );

	h_qp->h_recv_qp = h_qp->h_ci_qp;
	h_qp->h_send_qp = h_qp->h_ci_qp;

	h_qp->pfn_post_send = h_qp->obj.p_ci_ca->verbs.post_send;
	h_qp->pfn_post_recv = h_qp->obj.p_ci_ca->verbs.post_recv;
	return status;
}


static inline ib_api_status_t
verbs_create_qp(
	IN				ib_pd_handle_t				h_pd,
	IN				ib_qp_handle_t				h_qp,
	IN				ib_qp_create_t				*p_qp_create,
	IN				ib_qp_attr_t				*p_qp_attr,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	ib_api_status_t		status;

	status = h_qp->obj.p_ci_ca->verbs.create_qp(
		h_pd->h_ci_pd, h_qp, ci_ca_async_event_cb, p_qp_create, p_qp_attr,
		&h_qp->h_ci_qp, p_umv_buf );

	h_qp->h_recv_qp = h_qp->h_ci_qp;
	h_qp->h_send_qp = h_qp->h_ci_qp;

	h_qp->pfn_post_send = h_qp->obj.p_ci_ca->verbs.post_send;
	h_qp->pfn_post_recv = h_qp->obj.p_ci_ca->verbs.post_recv;
	return status;
}

#define verbs_check_qp(h_qp)	((h_qp)->h_ci_qp)
#define verbs_destroy_qp(h_qp) \
	h_qp->obj.p_ci_ca->verbs.destroy_qp( h_qp->h_ci_qp, h_qp->timewait )

#define verbs_query_qp(h_qp, p_qp_attr) \
	h_qp->obj.p_ci_ca->verbs.query_qp( h_qp->h_ci_qp,\
		p_qp_attr, p_umv_buf )

#define verbs_modify_qp(h_qp, p_qp_mod, p_qp_attr) \
	h_qp->obj.p_ci_ca->verbs.modify_qp( h_qp->h_ci_qp,\
		p_qp_mod, p_qp_attr, p_umv_buf )

#define verbs_ndi_modify_qp(h_qp, p_qp_mod, qp_attr, buf_size, p_buf) \
		h_qp->obj.p_ci_ca->verbs.ndi_modify_qp( h_qp->h_ci_qp,\
			p_qp_mod, &qp_attr, buf_size, p_buf )

#define verbs_post_send(h_qp, p_send_wr, pp_send_failure) \
	h_qp->obj.p_ci_ca->verbs.post_send( h_qp->h_ci_qp,\
		p_send_wr, pp_send_failure )

#define verbs_post_recv(h_qp, p_recv_wr, pp_recv_failure) \
	h_qp->obj.p_ci_ca->verbs.post_recv( h_qp->h_ci_qp,\
		p_recv_wr, pp_recv_failure )

#define verbs_local_mad(h_ca, port_num, p_src_av_attr, p_mad_in, p_mad_out) \
	h_ca->obj.p_ci_ca->verbs.local_mad( h_ca->obj.p_ci_ca->h_ci_ca,\
		port_num, p_src_av_attr, p_mad_in, p_mad_out)

#define check_local_mad(h_qp) \
	(h_qp->obj.p_ci_ca->verbs.local_mad)

#define init_alias_qp( h_qp, h_pd, port_guid, p_qp_create ) \
	init_qp_alias( h_qp, h_pd, port_guid, p_qp_create )

#define spl_qp_mad_send( h_mad_svc, p_mad_element_list, pp_mad_failure ) \
	IB_ERROR

#define spl_qp_cancel_mad( h_mad_svc, h_mad_send ) \
	IB_ERROR

#define create_reg_mad_pool( h_pool, h_pd, p_pool_key ) \
	IB_SUCCESS

#define dereg_destroy_mad_pool( pool_key )

#define verbs_attach_mcast(h_mcast)	\
	h_mcast->obj.p_ci_ca->verbs.attach_mcast(	\
			((ib_qp_handle_t)h_mcast->obj.p_parent_obj)->h_ci_qp, &h_mcast->member_rec.mgid,	\
			h_mcast->member_rec.mlid, &h_mcast->h_ci_mcast,	\
			NULL)

#define verbs_detach_mcast(h_mcast)	\
		h_mcast->obj.p_ci_ca->verbs.detach_mcast( \
			h_mcast->h_ci_mcast )

static inline ib_api_status_t
verbs_ci_call(
	IN				ib_ca_handle_t				h_ca,
	IN		const	void**				const	handle_array	OPTIONAL,
	IN				uint32_t					num_handles,
	IN				ib_ci_op_t*			const	p_ci_op,
	IN				ci_umv_buf_t*		const	p_umv_buf OPTIONAL )
{
	return h_ca->obj.p_ci_ca->verbs.vendor_call(
		p_umv_buf ? h_ca->h_um_ca : h_ca->obj.p_ci_ca->h_ci_ca,
		handle_array, num_handles, p_ci_op, p_umv_buf );
}


#else



	/* Macros for user-mode only */
#define verbs_create_av(h_pd, p_av_attr, h_av) \
	(h_pd->type == IB_PDT_ALIAS) ?\
	ual_pd_alias_create_av(h_pd, p_av_attr, h_av):\
	ual_create_av(h_pd, p_av_attr, h_av); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_check_av(h_av)	((h_av)->h_ci_av || (h_av)->obj.hdl)
#define convert_av_handle(h_qp, h_av) \
	((h_qp)->h_ci_qp?(h_av)->h_ci_av:(ib_av_handle_t)(ULONG_PTR)(h_av)->obj.hdl)
#define verbs_destroy_av(h_av) \
	ual_destroy_av(h_av)

#define verbs_query_av(h_av, p_av_attr, ph_pd) \
	ual_query_av(h_av, p_av_attr, ph_pd); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_modify_av(h_av, p_av_mod) \
	ual_modify_av(h_av, p_av_mod); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_query_ca(h_ca, p_ca_attr, p_size) \
	ual_query_ca(h_ca, p_ca_attr, p_size); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_modify_ca(h_ca, port_num, ca_mod, p_port_attr_mod) \
	ual_modify_ca(h_ca, port_num, ca_mod, p_port_attr_mod)

static inline ib_api_status_t
verbs_create_cq(
	IN		const	ib_ca_handle_t				h_ca,
	IN	OUT			ib_cq_create_t* const		p_cq_create,
	IN				ib_cq_handle_t				h_cq,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	UNUSED_PARAM( p_umv_buf );
	return ual_create_cq( h_ca->obj.p_ci_ca, p_cq_create, h_cq );
}


#define verbs_check_cq(h_cq)	((h_cq)->h_ci_cq || (h_cq)->obj.hdl)
#define verbs_destroy_cq(h_cq) \
	ual_destroy_cq(h_cq)

#define verbs_modify_cq(h_cq, p_size) \
	ual_modify_cq(h_cq, p_size); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_query_cq(h_cq, p_size) \
	ual_query_cq(h_cq, p_size); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_peek_cq(h_cq, p_n_cqes) \
	h_cq->pfn_peek( h_cq->h_peek_cq, p_n_cqes )

#define verbs_poll_cq(h_cq, pp_free_wclist, pp_done_wclist) \
	h_cq->pfn_poll( h_cq->h_poll_cq, pp_free_wclist, pp_done_wclist )

#define verbs_rearm_cq(h_cq, solicited) \
	h_cq->pfn_rearm( h_cq->h_rearm_cq, solicited )

#define verbs_rearm_n_cq(h_cq, n_cqes) \
	h_cq->pfn_rearm_n( h_cq->h_rearm_n_cq, n_cqes )

#define verbs_register_mr(h_pd, p_mr_create, p_lkey, p_rkey, h_mr) \
	ual_reg_mem(h_pd, p_mr_create, p_lkey, p_rkey, h_mr); \
	UNUSED_PARAM( um_call ); \
	UNUSED_PARAM( mapaddr )

#define verbs_register_pmr(h_pd, p_phys_create, p_vaddr, p_lkey, p_rkey, h_mr) \
	IB_UNSUPPORTED; \
	UNUSED_PARAM( h_pd ); \
	UNUSED_PARAM( p_phys_create ); \
	UNUSED_PARAM( p_vaddr ); \
	UNUSED_PARAM( p_lkey ); \
	UNUSED_PARAM( p_rkey ); \
	UNUSED_PARAM( h_mr )

#define verbs_check_mr(h_mr)	((h_mr)->h_ci_mr || (h_mr)->obj.hdl)
#define verbs_deregister_mr(h_mr) \
	ual_dereg_mr(h_mr)

/* For user-mode, this is nop */
#define verbs_release_shmid(h_mr)

#define verbs_query_mr(h_mr, p_mr_attr) \
	ual_query_mr(h_mr, p_mr_attr)

#define verbs_modify_mr(h_mr, mr_modify_mask, p_mr_create, \
	p_lkey, p_rkey, h_pd ) \
		ual_modify_mr( h_mr, mr_modify_mask, p_mr_create, \
			p_lkey, p_rkey, h_pd ); \
		UNUSED_PARAM( um_call )

#define verbs_modify_pmr( h_mr, mr_mod_mask, p_phys_create, \
	p_vaddr, p_lkey, p_rkey, h_pd ) \
		IB_UNSUPPORTED; \
		UNUSED_PARAM( h_mr ); \
		UNUSED_PARAM( mr_mod_mask ); \
		UNUSED_PARAM( p_phys_create ); \
		UNUSED_PARAM( p_vaddr ); \
		UNUSED_PARAM( p_lkey ); \
		UNUSED_PARAM( p_rkey ); \
		UNUSED_PARAM( h_pd )

#define verbs_register_smr(h_mr, h_pd, access_ctrl, p_vaddr, p_lkey, \
	p_rkey, ph_mr ) \
		ual_reg_shared( h_mr, h_pd, access_ctrl, p_vaddr, p_lkey, \
			p_rkey, ph_mr ); \
		UNUSED_PARAM( um_call )

#define verbs_create_mw(h_pd, p_rkey, h_mw) \
	ual_create_mw(h_pd, p_rkey, h_mw); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_check_mw(h_mw)	((h_mw)->h_ci_mw || (h_mw)->obj.hdl)
#define verbs_destroy_mw(h_mw) \
	ual_destroy_mw(h_mw)

#define verbs_query_mw(h_mw, ph_pd, p_rkey) \
	ual_query_mw(h_mw, ph_pd, p_rkey); \
	UNUSED_PARAM( p_umv_buf )

#define convert_mr_handle(h_mr)	(h_mr)

#define verbs_bind_mw(h_mw, h_qp, p_mw_bind, p_rkey) \
	ual_bind_mw(h_mw, h_qp, p_mw_bind, p_rkey)

static inline ib_api_status_t
verbs_allocate_pd(
	IN		const	ib_ca_handle_t				h_ca,
	IN				ib_pd_handle_t				h_pd,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf )
{
	UNUSED_PARAM( p_umv_buf );
	return ual_allocate_pd( h_ca, h_pd->type, h_pd );
}

/*
 * Get an alias to the kernel's hardware PD.
 */
static inline ib_api_status_t
allocate_pd_alias(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_pd_handle_t				h_pd )
{
	return ual_allocate_pd( h_ca, h_pd->type, h_pd );
}

#define deallocate_pd_alias( h_pd ) /* no action to take */

#define verbs_check_pd(h_pd)	((h_pd)->h_ci_pd || (h_pd)->obj.hdl)
#define verbs_deallocate_pd(h_pd) \
	ual_deallocate_pd(h_pd)

#define verbs_create_srq(h_pd, h_srq, p_srq_attr, p_umv_buf) \
	ual_create_srq (h_pd, h_srq, p_srq_attr); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_check_srq(h_srq)	((h_srq)->h_ci_srq || (h_srq)->obj.hdl)

#define verbs_destroy_srq(h_srq) \
	ual_destroy_srq(h_srq)

#define verbs_query_srq(h_srq, p_srq_attr) \
	ual_query_srq(h_srq, p_srq_attr); \
	UNUSED_PARAM( p_umv_buf );

#define verbs_modify_srq(h_srq, p_srq_attr, srq_attr_mask) \
	ual_modify_srq(h_srq, p_srq_attr, srq_attr_mask); \
	UNUSED_PARAM( p_umv_buf );

#define verbs_post_srq_recv(h_srq, p_recv_wr, pp_recv_failure) \
	ual_post_srq_recv(h_srq, p_recv_wr, pp_recv_failure)


/* For user-mode, handle conversion is done in ual files */

#define convert_qp_handle( qp_create )

/* TBD: Do we need to support this in user-mode? */
#define verbs_get_spl_qp(h_pd, port_num, h_qp, p_qp_create, p_qp_attr) \
		IB_UNSUPPORTED

#define verbs_create_qp(h_pd, h_qp, p_qp_create, p_qp_attr, p_umv_buf) \
	ual_create_qp (h_pd, h_qp, p_qp_create, p_qp_attr); \
	UNUSED_PARAM( p_umv_buf )

#define verbs_check_qp(h_qp)	((h_qp)->h_ci_qp || (h_qp)->obj.hdl)
#define verbs_destroy_qp(h_qp) \
	ual_destroy_qp(h_qp)

#define verbs_query_qp(h_qp, p_qp_attr) \
	ual_query_qp(h_qp, p_qp_attr); \
	UNUSED_PARAM( p_umv_buf );

#define verbs_modify_qp(h_qp, p_qp_mod, p_qp_attr) \
	ual_modify_qp(h_qp, p_qp_mod, p_qp_attr); \
	UNUSED_PARAM( p_umv_buf );

#define verbs_post_send(h_qp, p_send_wr, pp_send_failure) \
	ual_post_send(h_qp, p_send_wr, pp_send_failure)

#define verbs_post_recv(h_qp, p_recv_wr, pp_recv_failure) \
	ual_post_recv(h_qp, p_recv_wr, pp_recv_failure)

static inline ib_api_status_t
verbs_local_mad(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	uint8_t						port_num,
	IN		const	ib_av_attr_t*					p_src_av_attr,
	IN		const	void* const					p_mad_in,
	IN				void*						p_mad_out )
{
	return ual_local_mad( h_ca, port_num, p_mad_in, p_mad_out );
	UNUSED_PARAM( p_src_av_attr );
}

#define check_local_mad(h_qp) \
	(!h_qp)

#define init_alias_qp( h_qp, h_pd, port_guid, p_qp_create ) \
	ual_init_qp_alias( h_qp, h_pd, port_guid, p_qp_create )

#define spl_qp_mad_send( h_mad_svc, p_mad_element_list, pp_mad_failure ) \
	ual_spl_qp_mad_send( h_mad_svc, p_mad_element_list, pp_mad_failure )

#define spl_qp_cancel_mad( h_mad_svc, p_mad_element ) \
	ual_spl_qp_cancel_mad( h_mad_svc, p_mad_element )

#define create_reg_mad_pool( h_pool, h_pd, p_pool_key ) \
	ual_create_reg_mad_pool( h_pool, h_pd, p_pool_key )

#define dereg_destroy_mad_pool( pool_key ) \
	ual_dereg_destroy_mad_pool( pool_key )

#define verbs_attach_mcast(h_mcast)	\
	ual_attach_mcast( h_mcast )

#define verbs_detach_mcast(h_mcast)	\
	ual_detach_mcast( h_mcast )

#endif	/* CL_KERNEL */


#endif /* __AL_VERBS_H__ */
