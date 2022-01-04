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

#if !defined(__AL_CQ_H__)
#define __AL_CQ_H__

#include "al_ca.h"

typedef void
(*pfn_proc_comp_t)(
	IN		const	ib_cq_handle_t				h_cq );

typedef ib_api_status_t
(*pfn_peek_cq_t)(
	IN		const	ib_cq_handle_t				h_cq,
	OUT				uint32_t* const				p_n_cqes );

typedef ib_api_status_t
(*pfn_poll_cq_t)(
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			ib_wc_t**	const			pp_free_wclist,
		OUT			ib_wc_t**	const			pp_done_wclist );

typedef ib_api_status_t
(*pfn_rearm_cq_t)(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	boolean_t					solicited );

typedef ib_api_status_t
(*pfn_rearm_n_cq_t)(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	uint32_t					n_cqes );

#ifdef CL_KERNEL

typedef struct _ib_cq	ib_cq_t;

typedef struct _ndi_cq_csq
{
	IO_CSQ						csq;
	ib_cq_t*					h_cq;
	LIST_ENTRY					queue;
} ndi_cq_csq_t;

#endif

/*
 * Completion queue information required by the access layer.  This structure
 * is referenced by a user's CQ handle.
 */
typedef struct _ib_cq
{
	al_obj_t					obj;		/* Must be first. */

	cl_qlist_t					qp_list;	/* List of QPs bound to this CQ. */

	ib_pfn_comp_cb_t			pfn_user_comp_cb;
	cl_waitobj_handle_t			h_wait_obj;

	ib_cq_handle_t				h_ci_cq;

	/* Function pointers for the various speed path operations. */
#ifndef CL_KERNEL
	pfn_peek_cq_t				pfn_peek;
	ib_cq_handle_t				h_peek_cq;

	pfn_poll_cq_t				pfn_poll;
	ib_cq_handle_t				h_poll_cq;

	pfn_rearm_cq_t				pfn_rearm;
	ib_cq_handle_t				h_rearm_cq;

	pfn_rearm_n_cq_t			pfn_rearm_n;
	ib_cq_handle_t				h_rearm_n_cq;
#endif

	ib_pfn_event_cb_t			pfn_event_cb;

	/* NDI CQ fields */
#ifdef CL_KERNEL
	ndi_cq_csq_t				compl;
	ndi_cq_csq_t				error;
#endif

}	ib_cq_t;



ib_api_status_t
create_cq(
	IN		const	ib_ca_handle_t				h_ca,
	IN OUT			ib_cq_create_t* const		p_cq_create,
	IN		const	void* const					cq_context,
	IN		const	ib_pfn_event_cb_t			pfn_cq_event_cb,
		OUT			ib_cq_handle_t* const		ph_cq,
	IN OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
modify_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			uint32_t* const				p_size,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
query_cq(
	IN		const	ib_cq_handle_t				h_cq,
		OUT			uint32_t* const				p_size,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


void
cq_async_event_cb(
	IN				ib_async_event_rec_t* const	p_event_rec );


void
cq_attach_qp(
	IN		const	ib_cq_handle_t				h_cq,
	IN				cl_obj_rel_t* const			p_qp_rel );


void
cq_detach_qp(
	IN		const	ib_cq_handle_t				h_cq,
	IN				cl_obj_rel_t* const			p_qp_rel );

#endif /* __AL_CQ_H__ */
