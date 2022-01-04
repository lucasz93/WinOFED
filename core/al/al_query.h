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

#if !defined(__AL_QUERY_H__)
#define __AL_QUERY_H__


#include "al_common.h"
#include <iba/ib_al_ioctl.h>
#include <complib/cl_event.h>


/* Per port sa_req service */
typedef struct _sa_req_svc
{
	al_obj_t					obj;

	ib_net64_t					port_guid;
	uint8_t						port_num;
	ib_net16_t					sm_lid;
	uint8_t						sm_sl;

	ib_qp_handle_t				h_qp;
	ib_mad_svc_handle_t			h_mad_svc;
	ib_av_handle_t				h_av;

	ib_pool_key_t				pool_key;
	atomic32_t					trans_id;

}	sa_req_svc_t;



struct _al_sa_req;


typedef void
(*pfn_sa_req_cb_t)(
	IN				struct _al_sa_req			*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response );



typedef enum _al_sa_reg_state
{
	SA_REG_STARTING = 0,	/* Request sent to SA - awaiting response. */
	SA_REG_ACTIVE,			/* Request successfully ack'ed by SA. */
	SA_REG_CANCELING,		/* Canceling STARTING request to SA. */
	SA_REG_HALTING,			/* Deregistering from SA. */
	SA_REG_ERROR			/* There was an error registering. */

}	al_sa_reg_state_t;


/* Notes: Only the pfn_sa_req_cb field is required to send a request. */
typedef struct _al_sa_req
{
	cl_list_item_t				list_item;

	ib_api_status_t				status;

#ifdef CL_KERNEL
	sa_req_svc_t				*p_sa_req_svc;	/* For cancellation */
	ib_mad_element_t			*p_mad_response;
	ib_mad_element_t			*p_mad_request;	/* For cancellation */
	KEVENT						*p_sync_event;
#else	/* defined( CL_KERNEL ) */
	uint64_t					hdl;
	ual_send_sa_req_ioctl_t		ioctl;
	OVERLAPPED					ov;
#endif	/* defined( CL_KERNEL ) */
	const void					*user_context;
	pfn_sa_req_cb_t				pfn_sa_req_cb;

}	al_sa_req_t;



typedef struct _al_query
{
	al_sa_req_t					sa_req;		/* Must be first. */

	ib_al_handle_t				h_al;
	ib_pfn_query_cb_t			pfn_query_cb;
	ib_query_type_t				query_type;

}	al_query_t;



ib_api_status_t
create_sa_req_mgr(
	IN				al_obj_t* const				p_parent_obj );

ib_api_status_t
al_send_sa_req(
	IN				al_sa_req_t					*p_sa_req,
	IN		const	net64_t						port_guid,
	IN		const	uint32_t					timeout_ms,
	IN		const	uint32_t					retry_cnt,
	IN		const	ib_user_query_t* const		p_sa_req_data,
	IN		const	ib_al_flags_t				flags );

#if defined( CL_KERNEL )
static __inline void
al_cancel_sa_req(
	IN		const	al_sa_req_t					*p_sa_req )
{
	ib_cancel_mad( p_sa_req->p_sa_req_svc->h_mad_svc,
		p_sa_req->p_mad_request );
}
#else	/* defined( CL_KERNEL ) */
void
al_cancel_sa_req(
	IN		const	al_sa_req_t					*p_sa_req );
#endif	/* defined( CL_KERNEL ) */

ib_api_status_t
convert_wc_status(
	IN		const	ib_wc_status_t				wc_status );


#endif /* __AL_QUERY_H__ */
