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

#if !defined(__AL_SRQ_H__)
#define __AL_SRQ_H__

#include <iba/ib_al.h>
#include <iba/ib_ci.h>
#include <complib/cl_qlist.h>
#include <complib/cl_vector.h>

#include "al_ca.h"
#include "al_common.h"


typedef ib_api_status_t
(*ib_pfn_post_srq_recv_t)(
	IN		const	ib_srq_handle_t				h_srq,
	IN				ib_recv_wr_t* const			p_recv_wr,
	IN				ib_recv_wr_t				**p_recv_failure OPTIONAL );


/*
 * Shared queue pair information required by the access layer.  This structure
 * is referenced by a user's SRQ handle.
 */
typedef struct _ib_srq
{
	al_obj_t					obj;			/* Must be first. */

	ib_srq_handle_t			h_ci_srq;	/* kernel SRQ handle */
	ib_pfn_post_srq_recv_t		pfn_post_srq_recv;	/* post_srq_recv call */
	ib_srq_handle_t			h_recv_srq;	/* srq handle for the post_srq_recv call */
	ib_pfn_event_cb_t			pfn_event_cb;	/* user async event handler */
	cl_qlist_t					qp_list;	/* List of QPs bound to this CQ. */

}	ib_srq_t;

ib_api_status_t
create_srq(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_srq_attr_t* const			p_srq_attr,
	IN		const	void* const					srq_context,
	IN		const	ib_pfn_event_cb_t				pfn_srq_event_cb,
		OUT			ib_srq_handle_t* const			ph_srq,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
query_srq(
	IN		const	ib_srq_handle_t				h_srq,
		OUT			ib_srq_attr_t* const			p_srq_attr,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
modify_srq(
	IN		const	ib_srq_handle_t				h_srq,
	IN		const	ib_srq_attr_t* const			p_srq_attr,
	IN		const	ib_srq_attr_mask_t				srq_attr_mask,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


void
srq_async_event_cb(
	IN				ib_async_event_rec_t* const	p_event_rec );

void
srq_attach_qp(
	IN		const	ib_srq_handle_t				h_srq,
	IN				cl_obj_rel_t* const			p_qp_rel );

void
srq_detach_qp(
	IN		const	ib_srq_handle_t				h_srq,
	IN				cl_obj_rel_t* const			p_qp_rel );

#endif /* __AL_QP_H__ */

