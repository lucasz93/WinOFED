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

#if !defined(__IB_AL_MAD_H__)
#define __IB_AL_MAD_H__


#include <iba/ib_al.h>

#include <complib/cl_async_proc.h>
#include <complib/cl_atomic.h>
#include <complib/cl_ptr_vector.h>
#include <complib/cl_qlist.h>
#include <complib/cl_qpool.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_timer.h>
#include <complib/cl_vector.h>

#include "al_common.h"
#include "al_mad_pool.h"



/*
 * The MAD dispatcher routes completed MAD work requests to the correct
 * MAD service.
 */
typedef struct _al_mad_disp
{
	al_obj_t					obj;
	ib_qp_handle_t				h_qp;

	/* Client information. */
	cl_vector_t					client_vector;

	/*
	 * Indicates the version of supported MADs.  1 based-index.  This is
	 * a vector of class vectors.
	 */
	cl_vector_t					version_vector;

}	al_mad_disp_t;


typedef al_mad_disp_t			*al_mad_disp_handle_t;


typedef void
(*pfn_mad_svc_send_done_t)(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				al_mad_wr_t					*p_mad_wr,
	IN				ib_wc_t						*p_wc );

typedef void
(*pfn_mad_svc_recv_done_t)(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element );


typedef struct _al_mad_disp_reg
{
	ib_mad_svc_handle_t			h_mad_svc;
	al_mad_disp_handle_t		h_mad_disp;
	uint32_t					client_id;
	atomic32_t					ref_cnt;

	/* Clients must specify completion routines for user-mode support. */
	pfn_mad_svc_send_done_t		pfn_send_done;
	pfn_mad_svc_recv_done_t		pfn_recv_done;

	/* Mgmt class supported by the client.  Class 0x81 is mapped to 0x00. */
	uint8_t						mgmt_class;
	uint8_t						mgmt_version;
	boolean_t					support_unsol;

}	al_mad_disp_reg_t;



/* The registration handle is an index into the client_vector. */
typedef	al_mad_disp_reg_t*		al_mad_reg_handle_t;



ib_api_status_t
create_mad_disp(
	IN				al_obj_t* const				p_parent_obj,
	IN		const	ib_qp_handle_t				h_qp,
	IN				al_mad_disp_handle_t* const	ph_mad_disp );

void
mad_disp_send_done(
	IN				al_mad_disp_handle_t		h_mad_disp,
	IN				al_mad_wr_t					*p_mad_wr,
	IN				ib_wc_t						*p_wc );

ib_api_status_t
mad_disp_recv_done(
	IN				al_mad_disp_handle_t		h_mad_disp,
	IN				ib_mad_element_t			*p_mad_element );




/*
 * MAD service used to send and receive MADs.  MAD services are responsible
 * for retransmissions and SAR.
 */
typedef struct _al_mad_svc
{
	al_obj_t					obj;

	ib_mad_svc_type_t			svc_type;

	atomic32_t					ref_cnt;

	al_mad_reg_handle_t			h_mad_reg;
	ib_pfn_mad_comp_cb_t		pfn_user_send_cb;
	ib_pfn_mad_comp_cb_t		pfn_user_recv_cb;

	cl_qlist_t					send_list;
	cl_timer_t					send_timer;
    ULONG                       send_jitter;

	cl_qlist_t					recv_list;
	cl_timer_t					recv_timer;

	/* The PD and port number are used to create address vectors on sends. */
	ib_pd_handle_t				h_pd;
	uint8_t						port_num;

}	al_mad_svc_t;


void
free_mad_svc(
	IN				struct _al_obj				*p_obj );


/*
 * Register a MAD service with a QP.
 */
ib_api_status_t
reg_mad_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mad_svc_t* const			p_mad_svc,
		OUT			ib_mad_svc_handle_t* const	ph_mad_svc );


ib_api_status_t
al_local_mad(	
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	uint8_t						port_num,
	IN		const	ib_av_attr_t*					p_av_attr,
	IN		const	void* const					p_mad_in,
	IN				void*						p_mad_out );

/*
 * TID management
 */
typedef union _al_tid
{
	ib_net64_t		tid64;
	struct _tid
	{
		ib_net32_t	al_tid;
		ib_net32_t	user_tid;
	}	tid32;

}	al_tid_t;


ib_net32_t
al_get_user_tid(
	IN		const	ib_net64_t					tid64 );


uint32_t
al_get_al_tid(
	IN		const	ib_net64_t					tid64 );


void
al_set_al_tid(
	IN				ib_net64_t*		const		p_tid64,
	IN		const	uint32_t					tid32 );



void
build_mad_recv(
	IN				ib_mad_element_t*			p_mad_element,
	IN				ib_wc_t*					p_wc );


ib_mad_t*
get_mad_hdr_from_wr(
	IN				al_mad_wr_t* const			p_mad_wr );


ib_api_status_t
ib_delay_mad(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element,
	IN		const	uint32_t					delay_ms );


#endif /* __IB_AL_MAD_H__ */
