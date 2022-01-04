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

#if !defined(__AL_MCAST_H__)
#define __AL_MCAST_H__

#include <complib/cl_async_proc.h>

#include "al_common.h"
#include "al_query.h"
#include <iba/ib_ci.h>


/*
 * Tracks attaching to a multicast group in the kernel for QPs allocated to
 * user-mode clients.
 */
typedef struct _al_attach
{
	al_obj_t					obj;
	ib_mcast_handle_t			h_ci_mcast;		/* CI CA handle from attach */

}	al_attach_t, *al_attach_handle_t;


typedef struct _ib_mcast
{
	al_obj_t					obj;

	al_sa_req_t					sa_reg_req;
	al_sa_req_t					sa_dereg_req;

	ib_mcast_handle_t			h_ci_mcast;

	cl_async_proc_item_t		async;

	/* Used to perform synchronous requests. */
	ib_al_flags_t				flags;
	cl_event_t					event;

	/* Status of the join/leave request. */
	ib_api_status_t				req_status;

	/* Additional status information returned in the join/leave response. */
	ib_net16_t					resp_status;

	al_sa_reg_state_t			state;
	ib_pfn_mcast_cb_t			pfn_mcast_cb;

	/* Store member record to report to SA later. */
	ib_member_rec_t				member_rec;
	ib_net64_t					port_guid;

}	ib_mcast_t;




void
al_cancel_mcast(
	IN		const	ib_mcast_handle_t			h_mcast );


ib_api_status_t
al_join_mcast(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mcast_req_t* const		p_mcast_req,
	OUT		ib_mcast_handle_t*					p_h_mcast	);

ib_api_status_t
al_join_mcast_no_qp(
	IN		const	ib_mcast_req_t* const		p_mcast_req,
	OUT		ib_mcast_handle_t*					p_h_mcast	);


#if defined( CL_KERNEL )
/*
 * Called by proxy to attach a QP to a multicast group.
 */
ib_api_status_t
al_attach_mcast(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_gid_t					*p_mcast_gid,
	IN		const	ib_net16_t					mcast_lid,
		OUT			al_attach_handle_t			*ph_attach,
	IN	OUT			ci_umv_buf_t				*p_umv_buf OPTIONAL );


#endif	/* CL_KERNEL */


#endif /* __AL_MCAST_H__ */
