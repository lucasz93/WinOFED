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

/*
 * Abstract:
 *	This header file defines data structures for the kernel-mode NDI support 
 *
 * Environment:
 *	Kernel .
 */


#ifndef _AL_NDI_CM_H_
#define _AL_NDI_CM_H_

#include "complib/cl_ioctl_osd.h"

/* QP creation parameters */
#define QP_ATTRIB_RESPONDER_RESOURCES	4
#define QP_ATTRIB_INITIATOR_DEPTH		4
#define QP_ATTRIB_RETRY_COUNT			6
#define QP_ATTRIB_RNR_RETRY				7
#define QP_ATTRIB_RNR_NAK_TIMEOUT		8 /* 16 ms */

#define QP_ATTRIB_SQ_DEPTH				16

/* CM timeouts */
#define CM_MIN_LOCAL_TIMEOUT	(18)
#define CM_LOCAL_TIMEOUT		(1)
#define CM_MIN_REMOTE_TIMEOUT	(18)
#define CM_REMOTE_TIMEOUT		(2)
#define CM_RETRIES 4

typedef enum _ndi_cm_state 
{
	NDI_CM_IDLE,
	NDI_CM_LISTEN,
	NDI_CM_CONNECTING_QPR_SENT, // QPR = Query path record
	NDI_CM_CONNECTING_REQ_SENT,
	NDI_CM_CONNECTING_REQ_RCVD,
	NDI_CM_CONNECTING_REP_SENT,
	NDI_CM_CONNECTING_REP_RCVD,
	NDI_CM_CONNECTED,
    NDI_CM_DISCONNECTING,
	NDI_CM_CONNECTED_DREQ_RCVD,
    NDI_CM_INVALID

} ndi_cm_state_t;

typedef struct _ib_qp	ib_qp_t;

typedef struct _nd_csq
{
	IO_CSQ						csq;
	LIST_ENTRY					queue;
	ib_al_handle_t				h_al;
	union {
		uint64_t				h_qp;
		ib_mad_element_t		*p_mad_head;
	};
	union {
		ib_query_handle_t		h_query;
		ib_mad_element_t		*p_mad_tail;
	};
	net32_t						cid;
	ndi_cm_state_t				state;
	PIO_WORKITEM				p_workitem;
	volatile LONG				ref_cnt;
	KSPIN_LOCK					lock;

} nd_csq_t;


ib_api_status_t
ndi_modify_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t* const			p_qp_mod,
	IN		const	uint32_t					buf_size,
	IN				uint8_t* const				p_outbuf);

void
nd_csq_ref( nd_csq_t* p_csq );

void
nd_csq_release( nd_csq_t* p_csq );

void
nd_cm_handler(
	IN		const	ib_al_handle_t				h_al,
	IN		const	net32_t						cid );

NTSTATUS
ndi_req_cm(
	IN		ib_al_handle_t					h_al,
	IN		PIRP							p_irp
	);

NTSTATUS
ndi_rep_cm(
	IN		ib_al_handle_t					h_al,
	IN		PIRP							p_irp
	);

cl_status_t
ndi_rtu_cm(
	IN		nd_csq_t						*p_csq,
	IN		PIRP							p_irp
	);

NTSTATUS
ndi_dreq_cm(
	IN		nd_csq_t						*p_csq,
	IN		PIRP							p_irp
	);

void
ndi_cancel_cm_irps(
	IN		nd_csq_t						*p_csq
	);

NTSTATUS
ndi_listen_cm(
	IN		ib_al_handle_t					h_al,
	IN		ib_cep_listen_t					*p_listen,
		OUT	net32_t							*p_cid,
		OUT	size_t							*p_ret_bytes
	);

NTSTATUS
ndi_get_req_cm(
	IN		nd_csq_t						*p_csq,
	IN		PIRP							p_irp
	);

#endif

