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



#ifndef _SRP_SESSION_H_
#define _SRP_SESSION_H_


#include <iba/ib_al.h>
#include <complib/cl_obj.h>
#include <complib/cl_qlist.h>

#include "srp_hba.h"
#include "srp_hca.h"
#include "srp_connection.h"
#include "srp_descriptors.h"

/* Session information. */
typedef struct _srp_session
{
	cl_obj_t            obj;
	cl_obj_rel_t        rel;

	srp_hba_t           *p_hba;
	atomic32_t           repost_is_on;
	srp_hca_t           hca;
	srp_connection_t    connection;
	srp_descriptors_t   descriptors;

	SCSI_REQUEST_BLOCK  *p_shutdown_srb;

	/* keep session level SCSI address */
	UCHAR				target_id;
	cl_event_t			offload_event;
	cl_thread_t			recovery_thread;

#if DBG
	/* statistics */

	/* packets, built */
	uint64_t		x_pkt_fmr;		/* number of packets, mapped by fmr_pool */
	uint64_t		x_pkt_built;		/* number of packets, built  */

	/* request_limit_delta */
	int64_t		x_rld_total;		/* sum of req_limit_delta values  */
	int32_t		x_rld_num;		/* number of req_limit_delta values */
	int32_t		x_rld_max;		/* max req_limit_delta value */
	int32_t		x_rld_min;		/* min req_limit_delta value */
	int32_t		x_rld_zeroes;	/* number of zeroes */

	int32_t		x_rld_zeroes_cur;	/* number of zeroes */
	int32_t		x_rld_zeroes_cur_min;	/* number of zeroes */
	int32_t		x_rld_busy_success;	
	int32_t		x_rld_busy_fail;		

	/* pending queue */
	uint64_t		x_pend_total;		/* sum of pending_descriptors queue sizes  */
	uint32_t		x_pend_num;		/* number of pending_descriptors queue sizes */
	uint32_t		x_pend_max;		/* max pending_descriptors queue size */

	/* pending queue */
	uint64_t		x_sent_total;		/* sum of sent_descriptors queue sizes  */
	uint32_t		x_sent_num;		/* number of sent_descriptors queue sizes */
	uint32_t		x_sent_max;		/* max sent_descriptors queue size */

	uint32_t		x_req_limit;		/* max number in-flight packets */
#endif	
}   srp_session_t;

srp_session_t*
srp_new_session(
	IN      srp_hba_t       *p_hba,
	IN      ib_svc_entry_t  *p_svc_entry,
	IN      ib_path_rec_t   *p_path_rec,
	OUT     ib_api_status_t *p_status );

ib_api_status_t
srp_session_login(
	IN  srp_session_t   *p_srp_session );

void
__srp_cleanup_session(
	IN  cl_obj_t    *p_obj );

ib_api_status_t
srp_session_connect( 
  IN		srp_hba_t			*p_hba,
  IN OUT	p_srp_session_t		*pp_srp_session,
  IN		UCHAR				svc_idx );

void
srp_session_adjust_params( 
	IN		srp_session_t		*p_session );

void
srp_session_failed(
IN		srp_session_t*	p_srp_session );

void
srp_session_recovery_thread(
IN		void*	 const		context );

#endif  /* _SRP_SESSION_H_ */
