/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#pragma once

#ifndef _AL_CM_CEP_H_
#define _AL_CM_CEP_H_

#include <iba/ib_al.h>
#include "al_common.h"

#ifdef CL_KERNEL
#include <iba/ib_al_ifc.h>
#include <iba/ib_cm_ifc.h>
#endif

#define CEP_EVENT_TIMEOUT	0x80000000
#define CEP_EVENT_RECV		0x40000000
#define CEP_EVENT_REQ		0x00000001
#define CEP_EVENT_REP		0x00000002
#define CEP_EVENT_RTU		0x00000004
#define CEP_EVENT_DREQ		0x00000008
#define CEP_EVENT_DREP		0x00000010
#define CEP_EVENT_MRA		0x00000020
#define CEP_EVENT_REJ		0x00000040
#define CEP_EVENT_LAP		0x00000080
#define CEP_EVENT_APR		0x00000100
#define CEP_EVENT_SIDR		0x00800000


#define AL_INVALID_CID		0xFFFFFFFF
#define AL_RESERVED_CID		0


typedef void
(*al_pfn_cep_cb_t)(
	IN		const	ib_al_handle_t				h_al,
	IN		const	net32_t						cid );
/* PARAMETERS
*	h_al
*		[in] Handle to the AL instance to pass into the al_cep_poll call.
*
*	cid
*		[in] CID of the CEP on which the event occurred.  The CID should
*		be passed into the al_cep_poll call.
*
* RETURN VALUES:
*	This function does not return a value.
*
* NOTES
*	The callback is invoked at DISPATCH_LEVEL.
*
*	Recipients of the callback are expected to call al_cep_poll to retrieve
*	event specific details until al_cep_poll returns IB_NOT_DONE.  This may
*	be done in a different thread context.
*********/


ib_api_status_t
create_cep_mgr(
	IN				al_obj_t* const				p_parent_obj );


void
al_cep_cleanup_al(
	IN		const	ib_al_handle_t				h_al );


ib_api_status_t
al_create_cep(
	IN				ib_al_handle_t				h_al,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL,
	IN	OUT			net32_t* const				p_cid );
/*
* NOTES
*	This function may be invoked at DISPATCH_LEVEL
*
* The pfn_cb parameter may be NULL in the kernel if using IRPs for
* event notification.
*********/

#ifdef CL_KERNEL
ib_api_status_t
kal_cep_alloc(
	IN				ib_al_handle_t				h_al,
	IN	OUT			net32_t* const				p_cid );

ib_api_status_t
kal_cep_config(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb );
#endif

/* Destruction is asynchronous. */
void
al_destroy_cep(
	IN				ib_al_handle_t				h_al,
	IN	OUT			net32_t* const				p_cid,
	IN				boolean_t					reusable );
/*
*********/

ib_api_status_t
al_cep_listen(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				ib_cep_listen_t* const		p_listen_info );


#ifdef CL_KERNEL
ib_api_status_t
kal_cep_cancel_listen(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid );

ib_api_status_t
kal_cep_pre_req(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	iba_cm_req* const			p_cm_req,
	IN				uint8_t						rnr_nak_timeout,
	IN	OUT			ib_qp_mod_t* const			p_init OPTIONAL );
#endif

ib_api_status_t
al_cep_pre_req(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_req_t* const			p_cm_req,
		OUT			ib_qp_mod_t* const			p_init );


ib_api_status_t
al_cep_send_req(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid );


ib_api_status_t
al_cep_pre_rep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL,
	IN		const	ib_cm_rep_t* const			p_cm_rep,
	IN	OUT			net32_t* const				p_cid,
		OUT			ib_qp_mod_t* const			p_init );

#ifdef CL_KERNEL
ib_api_status_t
kal_cep_pre_rep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	iba_cm_rep* const			p_cm_rep,
	IN				uint8_t						rnr_nak_timeout,
	IN	OUT			ib_qp_mod_t* const			p_init OPTIONAL );

void
kal_cep_destroy(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				NTSTATUS					status );
#endif

ib_api_status_t
al_cep_send_rep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid );

#ifdef CL_KERNEL
void
kal_cep_format_event(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				ib_mad_element_t			*p_mad,
	IN	OUT			iba_cm_event				*p_event);

ib_api_status_t
al_cep_get_init_attr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			ib_qp_mod_t* const			p_init );
#endif

ib_api_status_t
al_cep_get_rtr_attr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			ib_qp_mod_t* const			p_rtr );


ib_api_status_t
al_cep_get_rts_attr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			ib_qp_mod_t* const			p_rts );


ib_api_status_t
al_cep_rtu(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t*					p_pdata OPTIONAL,
	IN				uint8_t						pdata_len );


ib_api_status_t
al_cep_rej(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				ib_rej_status_t				rej_status,
	IN		const	uint8_t* const				p_ari,
	IN				uint8_t						ari_len,
	IN		const	uint8_t* const				p_pdata,
	IN				uint8_t						pdata_len );


ib_api_status_t
al_cep_mra(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_mra_t* const			p_cm_mra );


ib_api_status_t
al_cep_lap(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_lap_t* const			p_cm_lap );


ib_api_status_t
al_cep_pre_apr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_apr_t* const			p_cm_apr,
		OUT			ib_qp_mod_t* const			p_apr );


ib_api_status_t
al_cep_send_apr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid );


ib_api_status_t
al_cep_dreq(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t* const				p_pdata OPTIONAL,
	IN		const	uint8_t						pdata_len );


ib_api_status_t
al_cep_drep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t* const				p_pdata OPTIONAL,
	IN		const	uint8_t						pdata_len );


ib_api_status_t
al_cep_get_timewait(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			uint64_t* const				p_timewait_us );


ib_api_status_t
al_cep_migrate(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid );


ib_api_status_t
al_cep_established(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid );


ib_api_status_t
al_cep_poll(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			void**						p_context,
		OUT			net32_t* const				p_new_cid,
		OUT			ib_mad_element_t** const	pp_mad );


#ifdef CL_KERNEL
void cm_get_interface(iba_cm_interface *p_ifc);

NTSTATUS
al_cep_queue_irp(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				IRP* const					p_irp );

NTSTATUS
al_cep_get_pdata(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
        OUT         uint8_t                     *p_init_depth,
        OUT         uint8_t                     *p_resp_res,
	IN	OUT			uint8_t						*p_psize,
		OUT			uint8_t*					pdata );

void*
kal_cep_get_context(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				ib_pfn_destroy_cb_t			pfn_addref );
#endif /* CL_KERNEL */


/****s* Access Layer/al_cep_sreq_t
* NAME
*	al_cep_sreq_t
*
* DESCRIPTION
*	Connection request information used to establish a new connection.
*
* SYNOPSIS
*/
typedef struct _al_cep_sreq
{
	ib_net64_t					svc_id;

	ib_path_rec_t*				p_path;

	const uint8_t*				p_pdata;
	uint8_t						pdata_len;

	uint8_t						max_cm_retries;
	ib_net16_t					pkey;
	uint32_t					timeout_ms;

}	al_cep_sreq_t;
/*
* FIELDS
*	svc_id
*		The ID of the remote service to which the SIDR request is
*		being made.
*
*	p_path
*		Path information over which to send the request.
*
*	p_pdata
*		Optional user-defined private data sent as part of the SIDR request.
*
*	pdata_len
*		Defines the size of the user-defined private data.
*
*	max_cm_retries
*		The maximum number of times that either CM should
*		resend a SIDR message.
*
*	timeout_ms
*		Timeout value in milli-seconds for the SIDR REQ to expire.  The CM will
*		add twice packet lifetime to this value to determine the actual timeout
*		value used.
*
*	pkey
*		pkey to be used as part of the request.
*
* SEE ALSO
*	al_cep_sreq
*****/

ib_api_status_t
al_cep_sreq(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	al_cep_sreq_t* const		p_sreq );


/****s* Access Layer/al_cep_srep_t
* NAME
*	al_cep_srep_t
*
* DESCRIPTION
*	SIDR reply information.
*
* SYNOPSIS
*/
typedef struct _al_cep_srep
{
	net32_t						qp_num;
	net32_t						qkey;

	const uint8_t*				p_pdata;
	const void*					p_info;

	uint8_t						pdata_len;
	uint8_t						info_len;

	ib_sidr_status_t			status;

}	al_cep_srep_t;
/*
* FIELDS
*	qp_num
*		The number of the queue pair on which the requested service
*		is supported.
*
*	qp_key
*		The QKEY of the returned queue pair.
*
*	p_pdata
*		Optional user-defined private data sent as part of the SIDR reply.
*
*	p_info
*		Optional "additonal information" sent as part of the SIDR reply.
*
*	pdata_len
*		Size of the user-defined private data.
*
*	info_len
*		Size of the "additional information".
*
*	status
*		sidr status value returned back to a previously received REQ.
*
* SEE ALSO
*	al_cep_srep
*****/

ib_api_status_t
al_cep_srep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	al_cep_srep_t* const		p_sreq );




/*
 * Return the local ACK timeout value based on the given packet lifetime
 * and target ACK delay.  Both input values are assumed to be in the form
 * 4.096 x 2 ^ input.
 */
#define MAX_LOCAL_ACK_TIMEOUT		0x1F		/* limited to 5 bits */

inline uint8_t
calc_lcl_ack_timeout(
	IN		const	uint8_t						round_trip_time,
	IN		const	uint8_t						target_ack_delay )
{
	uint64_t	timeout;
	uint8_t		local_ack_timeout;

	if( !target_ack_delay )
	{
		if( round_trip_time > MAX_LOCAL_ACK_TIMEOUT )
			return MAX_LOCAL_ACK_TIMEOUT;
		else
			return round_trip_time;
	}

	/*
	 * Since both input and the output values are in the same form, we
	 * can ignore the 4.096 portion by dividing it out.
	 */

	/* The input parameter is the round trip time. */
	timeout = (uint64_t)1 << round_trip_time;

	/* Add in the target ack delay. */
	if( target_ack_delay )
		timeout += (uint64_t)1 << target_ack_delay;

	/* Calculate the local ACK timeout. */
	local_ack_timeout = 1;
	while( (1ui64 << local_ack_timeout) <= timeout )
	{
		local_ack_timeout++;

		/* Only 5-bits are valid. */
		if( local_ack_timeout > MAX_LOCAL_ACK_TIMEOUT )
			return MAX_LOCAL_ACK_TIMEOUT;
	}

	return local_ack_timeout;
}

#endif	/* _AL_CM_CEP_H_ */
