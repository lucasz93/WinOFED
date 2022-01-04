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

#if !defined(__AL_QP_H__)
#define __AL_QP_H__

#include <iba/ib_al.h>
#include <iba/ib_ci.h>
#include <complib/cl_qlist.h>
#include <complib/cl_vector.h>

#include "al_ca.h"
#include "al_common.h"
#include "al_mad.h"
#include "al_mcast.h"
#ifdef CL_KERNEL
#include "al_smi.h"
#include "al_ndi_cm.h"
#endif	/* CL_KERNEL */


/*
 * Queue pair information required by the access layer.  This structure
 * is referenced by a user's QP handle.
 *
 * Other QP types are derived from this base object.
 */
typedef struct _ib_qp
{
	al_obj_t					obj;			/* Must be first. */

	ib_qp_handle_t				h_ci_qp;
	ib_qp_type_t				type;
	ib_net32_t					num;
	ib_qp_state_t				state;
	net64_t						port_guid;

#ifdef CL_KERNEL
	/* Timewait timeout time. */
	uint64_t					timewait;
#endif	/* CL_KERNEL */

	/* Handles to pass to post_send and post_recv. */
	ib_qp_handle_t				h_recv_qp;
	ib_qp_handle_t				h_send_qp;

	/*
	 * For UD QPs, we have to mess with AV handles.  This is
	 * where we store the actual post send function and appropriate
	 * handle.
	 */
	ib_qp_handle_t				h_ud_send_qp;
	ib_api_status_t
	(*pfn_ud_post_send)(
		IN		const	ib_qp_handle_t				h_qp,
		IN				ib_send_wr_t* const			p_send_wr,
		IN				ib_send_wr_t				**pp_send_failure OPTIONAL );

	ib_cq_handle_t				h_recv_cq;
	ib_cq_handle_t				h_send_cq;
	cl_obj_rel_t				recv_cq_rel;
	cl_obj_rel_t				send_cq_rel;

	ib_srq_handle_t				h_srq;
	cl_obj_rel_t				srq_rel;

	ib_pfn_event_cb_t			pfn_event_cb;

	ib_api_status_t
	(*pfn_modify_qp)(
		IN		const	ib_qp_handle_t				h_qp,
		IN		const	ib_qp_mod_t* const			p_qp_mod,
		IN	OUT			ci_umv_buf_t* const			p_umv_buf );
	ib_api_status_t
	(*pfn_post_recv)(
		IN		const	ib_qp_handle_t				h_qp,
		IN				ib_recv_wr_t* const			p_recv_wr,
		IN				ib_recv_wr_t				**p_recv_failure OPTIONAL );
	ib_api_status_t
	(*pfn_post_send)(
		IN		const	ib_qp_handle_t				h_qp,
		IN				ib_send_wr_t* const			p_send_wr,
		IN				ib_send_wr_t				**pp_send_failure OPTIONAL );
	ib_api_status_t
	(*pfn_reg_mad_svc)(
		IN		const	ib_qp_handle_t				h_qp,
		IN		const	ib_mad_svc_t* const			p_mad_svc,
			OUT			ib_mad_svc_handle_t* const	ph_mad_svc );
	ib_api_status_t
	(*pfn_dereg_mad_svc)(
		IN		const	ib_mad_svc_handle_t			h_mad_svc );
	void
	(*pfn_queue_mad)(
		IN		const	ib_qp_handle_t				h_qp,
		IN				al_mad_wr_t* const			p_mad_wr );
	void
	(*pfn_resume_mad)(
		IN		const	ib_qp_handle_t				h_qp );
	ib_api_status_t
	(*pfn_init_dgrm_svc)(
		IN		const	ib_qp_handle_t				h_qp,
		IN		const	ib_dgrm_info_t* const		p_dgrm_info );
	ib_api_status_t
	(*pfn_join_mcast)(
		IN		const	ib_qp_handle_t				h_qp,
		IN		const	ib_mcast_req_t* const		p_mcast_req,
		OUT		ib_mcast_handle_t*					p_h_mcast	);

}	ib_qp_t;


/*
 * Connected QP type.
 */
typedef struct _al_conn_qp
{
	ib_qp_t						qp;				/* Must be first. */

	ib_cm_handle_t				p_conn;

	net32_t						cid;

	/* Callback table. */
	ib_pfn_cm_req_cb_t			pfn_cm_req_cb;
	ib_pfn_cm_rep_cb_t			pfn_cm_rep_cb;
	ib_pfn_cm_mra_cb_t			pfn_cm_mra_cb;
	ib_pfn_cm_rtu_cb_t			pfn_cm_rtu_cb;
	ib_pfn_cm_lap_cb_t			pfn_cm_lap_cb;
	ib_pfn_cm_apr_cb_t			pfn_cm_apr_cb;
	ib_pfn_cm_dreq_cb_t			pfn_cm_dreq_cb;
	ib_pfn_cm_drep_cb_t			pfn_cm_drep_cb;
	ib_pfn_cm_rej_cb_t			pfn_cm_rej_cb; /* If RTU times out */


}	al_conn_qp_t;


/*
 * Datagram QP type.
 */
typedef ib_qp_t					al_dgrm_qp_t;


/*
 * Special QPs - SMI, GSI.
 */
typedef struct _al_special_qp
{
	ib_qp_t						qp;				/* Must be first. */
	cl_qlist_t					to_send_queue;

}	al_special_qp_t;


/*
 * QP alias to SMI, GSI QPs.
 */
typedef struct _al_qp_alias
{
	ib_qp_t						qp;				/* Must be first. */
	al_mad_disp_handle_t		h_mad_disp;
	ib_pool_key_t				pool_key;		/* Global MAD pool. */

}	al_qp_alias_t;


/*
 * QPs that support MAD operations.
 */
typedef struct _al_mad_qp
{
	ib_qp_t						qp;				/* Must be first. */
	ib_qp_handle_t				h_dgrm_qp;
	al_mad_disp_handle_t		h_mad_disp;

	ib_cq_handle_t				h_recv_cq;
	ib_cq_handle_t				h_send_cq;

	cl_qlist_t					to_send_queue;	/* Waiting to be posted		*/
	cl_qlist_t					send_queue;		/* Posted sends				*/
	cl_qlist_t					recv_queue;		/* Posted receives			*/
	uint32_t					max_rq_depth;	/* Maximum recv queue depth	*/
	atomic32_t					cur_rq_depth;	/* Current recv queue depth */

	ib_pool_handle_t			h_pool;
	ib_pool_key_t				pool_key;

}	al_mad_qp_t;



ib_api_status_t
create_qp(
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN		const	void* const					qp_context,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb,
		OUT			ib_qp_handle_t* const		ph_qp,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
query_qp(
	IN		const	ib_qp_handle_t				h_qp,
		OUT			ib_qp_attr_t* const			p_qp_attr,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
modify_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
get_spl_qp(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_net64_t					port_guid,
	IN	OUT			ib_qp_create_t* const		p_qp_create,
	IN		const	void* const					qp_context,
	IN		const	ib_pfn_event_cb_t			pfn_qp_event_cb,
		OUT			ib_pool_key_t* const		p_pool_key OPTIONAL,
		OUT			ib_qp_handle_t* const		ph_qp,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );



ib_mad_send_handle_t
get_send_mad_wp(
	IN		const	al_qp_alias_t				*p_qp_alias );



void
put_send_mad_wp(
	IN		const	al_qp_alias_t				*p_qp_alias,
	IN		const	ib_mad_send_handle_t		h_send_mad );


void
special_qp_resume_sends(
	IN		const	ib_qp_handle_t				h_qp );


void
special_qp_queue_mad(
	IN		const	ib_qp_handle_t				h_qp,
	IN				al_mad_wr_t* const			p_mad_wr );


void
qp_async_event_cb(
	IN				ib_async_event_rec_t* const	p_event_rec );


/* Return the AL instance associated with this QP. */
static inline ib_al_handle_t
qp_get_al(
	IN		const	ib_qp_handle_t				h_qp )
{
	return h_qp->obj.h_al;
}


#endif /* __AL_QP_H__ */
