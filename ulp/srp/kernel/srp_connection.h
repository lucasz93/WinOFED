/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#ifndef _SRP_CONNECTION_H_
#define _SRP_CONNECTION_H_


#include <iba/ib_al.h>
#include <complib/cl_atomic.h>
#include <complib/cl_event.h>

#include "srp.h"
#include "srp_connection.h"
#include "srp_hca.h"

/* Default Max Inflight IO Depth Commands */
#define SRP_DEFAULT_SEND_Q_DEPTH 1000
/* Default Max Inflight IO Depth Responses */
#define SRP_DEFAULT_RECV_Q_DEPTH 1000

typedef enum
{
	SRP_NOT_CONNECTED,
	SRP_CONNECT_REQUESTED,
	SRP_CONNECTED,
	SRP_CONNECT_FAILURE,
	SRP_CONNECTION_CLOSING,
	SRP_TARGET_DISCONNECTING
} srp_connection_state_t;

typedef struct _srp_session *p_srp_session_t;

/* Connection information. */
typedef struct _srp_connection
{
	BOOLEAN                 initialized;

	srp_connection_state_t  state;

	ib_cq_handle_t          h_send_cq;
	ib_cq_handle_t          h_recv_cq;
	ib_qp_handle_t          h_qp;

	ib_path_rec_t           *p_path_rec;
	srp_ib_port_id_t		init_port_id;
	srp_ib_port_id_t		targ_port_id;
	ib_net64_t              service_id;

	uint32_t                send_queue_depth;
	uint32_t                recv_queue_depth;

	atomic32_t              tag;
	atomic32_t              request_limit;
	atomic32_t				max_limit;
	int32_t                 request_threashold;
	uint32_t                init_to_targ_iu_sz;
	uint32_t                targ_to_init_iu_sz;

	ib_wc_t                 *p_wc_array;
	ib_wc_t                 *p_wc_free_list;

	uint32_t                signaled_send_completion_count;
	uint32_t                max_scatter_gather_entries;
	uint32_t				ioc_max_send_msg_size;
	uint32_t				req_max_iu_msg_size;
	cl_event_t              conn_req_event;
	DATA_BUFFER_DESCRIPTOR_FORMAT  descriptor_format;
	LOGIN_REJECT_CODE		reject_reason;
	uint8_t					ioc_max_send_msg_depth;
}   srp_connection_t;

ib_api_status_t
srp_init_connection(
	IN OUT  srp_connection_t        *p_connection,
	IN		ib_ioc_profile_t* const	p_profile,
	IN		net64_t					ca_guid,
	IN		net64_t					ext_id,
	IN      ib_path_rec_t           *p_path_rec,
	IN      ib_net64_t              service_id );

ib_api_status_t
srp_connect(
	IN OUT  srp_connection_t    *p_connection,
	IN      srp_hca_t           *p_hca,
	IN      uint8_t             send_msg_depth,
	IN      p_srp_session_t     p_session );

void
srp_free_connection(
	IN  srp_connection_t    *p_srp_connection );
static
ib_api_status_t
__srp_issue_session_login(
	IN OUT  srp_connection_t    *p_connection,
	IN      srp_hca_t           *p_hca,
	IN      uint8_t             send_msg_depth );

#endif  /* _SRP_CONNECTION_H_ */
