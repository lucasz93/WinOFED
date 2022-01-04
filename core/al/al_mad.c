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

#include <iba/ib_al.h>
#include <complib/cl_byteswap.h>
#include <complib/cl_timer.h>
#include <limits.h>

#include "al.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_mad.tmh"
#endif

#include "al_cq.h"
#include "al_mad.h"
#include "al_qp.h"
#include "al_res_mgr.h"
#include "al_verbs.h"

#include "ib_common.h"


#define	MAX_TIME				CL_CONST64(0xFFFFFFFFFFFFFFFF)
#define MAD_VECTOR_SIZE			8
#define MAX_METHOD				127
#define DEFAULT_RMPP_VERSION	1

#define AL_RMPP_WINDOW			16				/* Max size of RMPP window */
#define AL_REASSEMBLY_TIMEOUT	5000			/* 5 seconds */
#define AL_RMPP_RETRIES			5

static void
__cleanup_mad_disp(
	IN				al_obj_t					*p_obj );

static void
__free_mad_disp(
	IN				al_obj_t					*p_obj );

static cl_status_t
__init_mad_reg(
	IN				void* const					p_element,
	IN				void*						context );

static cl_status_t
__init_version_entry(
	IN				void* const					p_element,
	IN				void*						context );

static void
__destroy_version_entry(
	IN				void* const					p_element,
	IN				void*						context );

static cl_status_t
__init_class_entry(
	IN				void* const					p_element,
	IN				void*						context );

static void
__destroy_class_entry(
	IN				void* const					p_element,
	IN				void*						context );

static __inline uint8_t
__mgmt_class_index(
	IN		const	uint8_t						mgmt_class );

static __inline uint8_t
__mgmt_version_index(
	IN		const	uint8_t						mgmt_version );

static boolean_t
__mad_disp_reg_unsol(
	IN		const	al_mad_disp_handle_t		h_mad_disp,
	IN		const	al_mad_reg_handle_t			h_mad_reg,
	IN		const	ib_mad_svc_t				*p_mad_svc );

static boolean_t
__use_tid_routing(
	IN		const	ib_mad_t* const				p_mad_hdr,
	IN		const	boolean_t					are_we_sender );

/*
 * Issue a send request to the MAD dispatcher.
 */
static void
__mad_disp_queue_send(
	IN		const	al_mad_reg_handle_t			h_mad_reg,
	IN				al_mad_wr_t* const			p_mad_wr );

static inline void
__mad_disp_resume_send(
	IN		const	al_mad_reg_handle_t			h_mad_reg );

static void
__destroying_mad_svc(
	IN				struct _al_obj				*p_obj );

static void
__cleanup_mad_svc(
	IN				struct _al_obj				*p_obj );

static void
__send_timer_cb(
	IN				void						*context );

static void
__check_send_queue(
	IN				ib_mad_svc_handle_t			h_mad_svc );

static void
__recv_timer_cb(
	IN				void						*context );

static ib_api_status_t
__init_send_mad(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN		const	ib_mad_send_handle_t		h_send,
	IN				ib_mad_element_t* const		p_mad_element );

static boolean_t
__does_send_req_rmpp(
	IN		const	ib_mad_svc_type_t			mad_svc_type,
	IN		const	ib_mad_element_t* const		p_mad_element,
		OUT			uint8_t						*p_rmpp_version );

static void
__queue_mad_wr(
	IN		const	al_mad_reg_handle_t			h_mad_reg,
	IN		const	ib_mad_send_handle_t		h_send );

static void
__queue_rmpp_seg(
	IN		const	al_mad_reg_handle_t			h_mad_reg,
	IN				ib_mad_send_handle_t		h_send );

static ib_api_status_t
__create_send_av(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_send_handle_t		h_send );

static void
__cleanup_mad_send(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_send_handle_t		h_send,
	IN				uint16_t					ctx);

static __inline int
__set_retry_time(
	IN				ib_mad_send_handle_t		h_send,
    IN              ULONG                       send_jitter );

static void
__mad_svc_send_done(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				al_mad_wr_t					*p_mad_wr,
	IN				ib_wc_t						*p_wc );

static boolean_t
__is_send_mad_done(
	IN				ib_mad_send_handle_t		h_send,
	IN				ib_wc_status_t				status );

static void
__notify_send_comp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_send_handle_t		h_send,
	IN				ib_wc_status_t				wc_status );

static void
__mad_svc_recv_done(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element );

static void
__process_recv_resp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element );

static cl_status_t
__do_rmpp_recv(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN	OUT			ib_mad_element_t			**pp_mad_element );

static __inline boolean_t
__recv_requires_rmpp(
	IN		const	ib_mad_svc_type_t			mad_svc_type,
	IN		const	ib_mad_element_t* const		p_mad_element );

static __inline boolean_t
__is_internal_send(
	IN		const	ib_mad_svc_type_t			mad_svc_type,
	IN		const	ib_mad_element_t* const		p_mad_element );

static cl_status_t
__process_rmpp_data(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN	OUT			ib_mad_element_t			**pp_mad_element );

static void
__process_rmpp_ack(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element );

static void
__process_rmpp_nack(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element );

static cl_status_t
__process_segment(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				al_mad_rmpp_t				*p_rmpp,
	IN	OUT			ib_mad_element_t			**pp_mad_element,
		OUT			ib_mad_element_t			**pp_rmpp_resp_mad );

static al_mad_rmpp_t*
__find_rmpp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN	OUT			ib_mad_element_t			*p_mad_element );

static al_mad_rmpp_t*
__get_mad_rmpp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element );

static void
__put_mad_rmpp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				al_mad_rmpp_t				*p_rmpp );

static void
__init_reply_element(
	IN				ib_mad_element_t			*p_dst_element,
	IN				ib_mad_element_t			*p_src_element );

static ib_mad_element_t*
__get_rmpp_ack(
	IN				al_mad_rmpp_t				*p_rmpp );

ib_net64_t
__get_send_tid(
	IN				ib_mad_send_handle_t		h_send )
{
	return ((ib_mad_t*)ib_get_mad_buf( h_send->p_send_mad ))->trans_id;
}


ib_mad_t*
get_mad_hdr_from_wr(
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_mad_send_handle_t	h_send;

	CL_ASSERT( p_mad_wr );

	h_send = PARENT_STRUCT( p_mad_wr, al_mad_send_t, mad_wr );
	return h_send->p_send_mad->p_mad_buf;
}



/*
 * Construct a MAD element from a receive work completion.
 */
void
build_mad_recv(
	IN				ib_mad_element_t*			p_mad_element,
	IN				ib_wc_t*					p_wc )
{
	AL_ENTER( AL_DBG_SMI );

	CL_ASSERT( p_mad_element );
	CL_ASSERT( p_wc );

	/* Build the MAD element from the work completion. */
	p_mad_element->status		= p_wc->status;
	p_mad_element->remote_qp	= p_wc->recv.ud.remote_qp;

	/*
	 * We assume all communicating managers using MAD services use
	 * the same QKEY.
	 */

	/*
	 * Mellanox workaround:
	 * The Q_KEY from the QP context must be used if the high bit is
	 * set in the Q_KEY part of the work request. See section 10.2.5
	 * on Q_KEYS Compliance Statement C10-15.
	 * This must be enabled to permit future non special QP's to have
	 * MAD level service capability. To use SAR in a generic way.
	 */

	/*
	 * p_mad_element->remote_qkey = IB_QP_PRIVILEGED_Q_KEY;
	 */

	p_mad_element->remote_qkey	= IB_QP1_WELL_KNOWN_Q_KEY;
	p_mad_element->remote_lid	= p_wc->recv.ud.remote_lid;
	p_mad_element->remote_sl	= p_wc->recv.ud.remote_sl;
	p_mad_element->pkey_index	= p_wc->recv.ud.pkey_index;
	p_mad_element->path_bits	= p_wc->recv.ud.path_bits;
	p_mad_element->recv_opt	= p_wc->recv.ud.recv_opt;

	p_mad_element->grh_valid	= p_wc->recv.ud.recv_opt & IB_RECV_OPT_GRH_VALID;
	
	if( p_wc->recv.ud.recv_opt & IB_RECV_OPT_IMMEDIATE )
		p_mad_element->immediate_data = p_wc->recv.ud.immediate_data;

	AL_EXIT( AL_DBG_SMI );
}



/*
 *
 * MAD Dispatcher.
 *
 */


ib_api_status_t
create_mad_disp(
	IN				al_obj_t* const				p_parent_obj,
	IN		const	ib_qp_handle_t				h_qp,
	IN				al_mad_disp_handle_t* const	ph_mad_disp )
{
	al_mad_disp_handle_t	h_mad_disp;
	ib_api_status_t			status;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_MAD_SVC );
	h_mad_disp = cl_zalloc( sizeof( al_mad_disp_t ) );
	if( !h_mad_disp )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("insufficient memory\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Initialize the MAD dispatcher. */
	cl_vector_construct( &h_mad_disp->client_vector );
	cl_vector_construct( &h_mad_disp->version_vector );
	construct_al_obj( &h_mad_disp->obj, AL_OBJ_TYPE_MAD_DISP );
	status = init_al_obj( &h_mad_disp->obj, h_mad_disp, TRUE,
		NULL, __cleanup_mad_disp, __free_mad_disp );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("init obj: %s\n",
			ib_get_err_str(status)) );
		__free_mad_disp( &h_mad_disp->obj );
		return status;
	}
	status = attach_al_obj( p_parent_obj, &h_mad_disp->obj );
	if( status != IB_SUCCESS )
	{
		h_mad_disp->obj.pfn_destroy( &h_mad_disp->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Obtain a reference to the QP to post sends to. */
	h_mad_disp->h_qp = h_qp;
	ref_al_obj( &h_qp->obj );

	/* Create the client vector. */
	cl_status = cl_vector_init( &h_mad_disp->client_vector, 1, MAD_VECTOR_SIZE,
		sizeof( al_mad_disp_reg_t ), __init_mad_reg, NULL, h_mad_disp );
	if( cl_status != CL_SUCCESS )
	{
		h_mad_disp->obj.pfn_destroy( &h_mad_disp->obj, NULL );
		return ib_convert_cl_status( cl_status );
	}

	/* Create the version vector. */
	cl_status = cl_vector_init( &h_mad_disp->version_vector,
		1, 1, sizeof( cl_vector_t ), __init_version_entry,
		__destroy_version_entry, &h_mad_disp->version_vector );
	if( cl_status != CL_SUCCESS )
	{
		h_mad_disp->obj.pfn_destroy( &h_mad_disp->obj, NULL );
		return ib_convert_cl_status( cl_status );
	}

	*ph_mad_disp = h_mad_disp;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_mad_disp->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_MAD_SVC );
	return IB_SUCCESS;
}



static void
__cleanup_mad_disp(
	IN				al_obj_t					*p_obj )
{
	al_mad_disp_handle_t	h_mad_disp;

	AL_ENTER( AL_DBG_MAD_SVC );
	CL_ASSERT( p_obj );
	h_mad_disp = PARENT_STRUCT( p_obj, al_mad_disp_t, obj );

	/* Detach from the QP that we were using. */
	if( h_mad_disp->h_qp )
		deref_al_obj( &h_mad_disp->h_qp->obj );

	AL_EXIT( AL_DBG_MAD_SVC );
}



static void
__free_mad_disp(
	IN				al_obj_t					*p_obj )
{
	al_mad_disp_handle_t	h_mad_disp;

	AL_ENTER( AL_DBG_MAD_SVC );
	CL_ASSERT( p_obj );
	h_mad_disp = PARENT_STRUCT( p_obj, al_mad_disp_t, obj );

	cl_vector_destroy( &h_mad_disp->client_vector );
	cl_vector_destroy( &h_mad_disp->version_vector );
	destroy_al_obj( p_obj );
	cl_free( h_mad_disp );
	AL_EXIT( AL_DBG_MAD_SVC );
}



static al_mad_reg_handle_t
__mad_disp_reg(
	IN		const	al_mad_disp_handle_t		h_mad_disp,
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN		const	ib_mad_svc_t				*p_mad_svc,
	IN		const	pfn_mad_svc_send_done_t		pfn_send_done,
	IN		const	pfn_mad_svc_recv_done_t		pfn_recv_done )
{
	al_mad_reg_handle_t		h_mad_reg;
	size_t					i;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_MAD_SVC );
	cl_spinlock_acquire( &h_mad_disp->obj.lock );

	/* Find an empty slot in the client vector for the registration. */
	for( i = 0; i < cl_vector_get_size( &h_mad_disp->client_vector ); i++ )
	{
		h_mad_reg = cl_vector_get_ptr( &h_mad_disp->client_vector, i );
		if( !h_mad_reg->ref_cnt )
			break;
	}
	/* Trap for ClientID overflow. */
	if( i >= 0xFFFFFFFF )
	{
		cl_spinlock_release( &h_mad_disp->obj.lock );
		return NULL;
	}
	cl_status = cl_vector_set_min_size( &h_mad_disp->client_vector, i+1 );
	if( cl_status != CL_SUCCESS )
	{
		cl_spinlock_release( &h_mad_disp->obj.lock );
		return NULL;
	}
	h_mad_reg = cl_vector_get_ptr( &h_mad_disp->client_vector, i );

	/* Record the registration. */
	h_mad_reg->client_id = (uint32_t)i;
	h_mad_reg->support_unsol = p_mad_svc->support_unsol;
	h_mad_reg->mgmt_class = p_mad_svc->mgmt_class;
	h_mad_reg->mgmt_version = p_mad_svc->mgmt_version;
	h_mad_reg->pfn_recv_done = pfn_recv_done;
	h_mad_reg->pfn_send_done = pfn_send_done;

	/* If the client requires support for unsolicited MADs, add tracking. */
	if( p_mad_svc->support_unsol )
	{
		if( !__mad_disp_reg_unsol( h_mad_disp, h_mad_reg, p_mad_svc ) )
		{
			cl_spinlock_release( &h_mad_disp->obj.lock );
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("reg unsol failed\n") );
			return NULL;
		}
	}

	/* Record that the registration was successful. */
	h_mad_reg->h_mad_svc = h_mad_svc;
	h_mad_reg->ref_cnt = 1;
	cl_spinlock_release( &h_mad_disp->obj.lock );

	/* The MAD service needs to take a reference on the dispatcher. */
	ref_al_obj( &h_mad_disp->obj );

	AL_EXIT( AL_DBG_MAD_SVC );
	return h_mad_reg;
}


static cl_status_t
__init_mad_reg(
	IN				void* const					p_element,
	IN				void*						context )
{
	al_mad_reg_handle_t			h_mad_reg;

	/* Record the MAD dispatcher for the registration structure. */
	h_mad_reg = p_element;
	h_mad_reg->h_mad_disp = context;
	h_mad_reg->ref_cnt = 0;

	return CL_SUCCESS;
}


/*
 * Initialize an entry in the version vector.  Each entry is a vector of
 * classes.
 */
static cl_status_t
__init_version_entry(
	IN				void* const					p_element,
	IN				void*						context )
{
	cl_vector_t		*p_vector;

	p_vector = p_element;
	UNUSED_PARAM( context );

	cl_vector_construct( p_vector );
	return cl_vector_init( p_vector, MAD_VECTOR_SIZE, MAD_VECTOR_SIZE,
		sizeof( cl_ptr_vector_t ), __init_class_entry, __destroy_class_entry,
		p_vector );
}


static void
__destroy_version_entry(
	IN				void* const					p_element,
	IN				void*						context )
{
	cl_vector_t		*p_vector;

	p_vector = p_element;
	UNUSED_PARAM( context );

	cl_vector_destroy( p_vector );
}


/*
 * Initialize an entry in the class vector.  Each entry is a pointer vector
 * of methods.
 */
static cl_status_t
__init_class_entry(
	IN				void* const					p_element,
	IN				void*						context )
{
	cl_ptr_vector_t		*p_ptr_vector;

	p_ptr_vector = p_element;
	UNUSED_PARAM( context );

	cl_ptr_vector_construct( p_ptr_vector );
	return cl_ptr_vector_init( p_ptr_vector,
		MAD_VECTOR_SIZE, MAD_VECTOR_SIZE );
}


static void
__destroy_class_entry(
	IN				void* const					p_element,
	IN				void*						context )
{
	cl_ptr_vector_t		*p_ptr_vector;

	p_ptr_vector = p_element;
	UNUSED_PARAM( context );

	cl_ptr_vector_destroy( p_ptr_vector );
}


/*
 * Add support for unsolicited MADs for the given MAD service.
 */
static boolean_t
__mad_disp_reg_unsol(
	IN		const	al_mad_disp_handle_t		h_mad_disp,
	IN		const	al_mad_reg_handle_t			h_mad_reg,
	IN		const	ib_mad_svc_t				*p_mad_svc )
{
	cl_status_t			cl_status;
	cl_vector_t			*p_class_vector;
	cl_ptr_vector_t		*p_method_ptr_vector;
	uint8_t				i;

	/* Ensure that we are ready to handle this version number. */
	AL_ENTER( AL_DBG_MAD_SVC );
	cl_status = cl_vector_set_min_size( &h_mad_disp->version_vector,
		__mgmt_version_index( p_mad_svc->mgmt_version ) + 1 );
	if( cl_status != CL_SUCCESS )
		return FALSE;

	/* Get the list of classes in use for this version. */
	p_class_vector = cl_vector_get_ptr( &h_mad_disp->version_vector,
		__mgmt_version_index( p_mad_svc->mgmt_version ) );

	/* Ensure that we are ready to handle the specified class. */
	cl_status = cl_vector_set_min_size( p_class_vector,
		__mgmt_class_index( p_mad_svc->mgmt_class ) + 1 );
	if( cl_status != CL_SUCCESS )
		return FALSE;

	/* Get the list of methods in use for this class. */
	p_method_ptr_vector = cl_vector_get_ptr( p_class_vector,
		__mgmt_class_index( p_mad_svc->mgmt_class ) );

	/* Ensure that we can handle all requested methods. */
	for( i = MAX_METHOD - 1; i > 0; i-- )
	{
		if( p_mad_svc->method_array[i] )
		{
			cl_status = cl_ptr_vector_set_min_size( p_method_ptr_vector, i+1 );
			if( cl_status != CL_SUCCESS )
				return FALSE;

			/* No one else can be registered for this method. */
			if( cl_ptr_vector_get( p_method_ptr_vector, i ) )
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("Other client already registered for Un-Solicited Method "
					"%u for version %u of class %u.\n", i, p_mad_svc->mgmt_version,
					p_mad_svc->mgmt_class ) );
				return FALSE;
			}
		}
	}

	/* We can support the request.  Record the methods. */
	for( i = 0; i < MAX_METHOD; i++ )
	{
		if( p_mad_svc->method_array[i] )
		{
			cl_ptr_vector_set( p_method_ptr_vector, i, h_mad_reg );

			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
				("Register version:%u (%u) class:0x%02X(%u) method:0x%02X Hdl:%p\n",
				p_mad_svc->mgmt_version,
				__mgmt_version_index( p_mad_svc->mgmt_version ),
				p_mad_svc->mgmt_class,
				__mgmt_class_index( p_mad_svc->mgmt_class ),
				i,
				h_mad_reg) );
		}
	}

	AL_EXIT( AL_DBG_MAD_SVC );
	return TRUE;
}


static __inline uint8_t
__mgmt_version_index(
	IN		const	uint8_t						mgmt_version )
{
	return (uint8_t)(mgmt_version - 1);
}


static __inline uint8_t
__mgmt_class_index(
	IN		const	uint8_t						mgmt_class )
{
	/* Map class 0x81 to 0 to remove empty class values. */
	if( mgmt_class == IB_MCLASS_SUBN_DIR )
		return IB_MCLASS_SUBN_LID;
	else
		return mgmt_class;
}



/*
 * Deregister a MAD service from the dispatcher.
 */
static void
__mad_disp_dereg(
	IN		const	al_mad_reg_handle_t			h_mad_reg )
{
	al_mad_disp_handle_t	h_mad_disp;
	cl_vector_t				*p_class_vector;
	cl_ptr_vector_t			*p_method_ptr_vector;
	size_t					i;

	AL_ENTER( AL_DBG_MAD_SVC );
	h_mad_disp = h_mad_reg->h_mad_disp;

	cl_spinlock_acquire( &h_mad_disp->obj.lock );

	if( h_mad_reg->support_unsol )
	{
		/* Deregister the service from receiving unsolicited MADs. */
		p_class_vector = cl_vector_get_ptr( &h_mad_disp->version_vector,
			__mgmt_version_index( h_mad_reg->mgmt_version ) );

		p_method_ptr_vector = cl_vector_get_ptr( p_class_vector,
			__mgmt_class_index( h_mad_reg->mgmt_class ) );

		/* Deregister all methods registered to the client. */
		for( i = 0; i < cl_ptr_vector_get_size( p_method_ptr_vector ); i++ )
		{
			if( cl_ptr_vector_get( p_method_ptr_vector, i ) == h_mad_reg )
			{
				cl_ptr_vector_set( p_method_ptr_vector, i, NULL );
			}
		}
	}

	cl_spinlock_release( &h_mad_disp->obj.lock );

	/* Decrement the reference count in the registration table. */
	cl_atomic_dec( &h_mad_reg->ref_cnt );

	/* The MAD service no longer requires access to the MAD dispatcher. */
	deref_al_obj( &h_mad_disp->obj );
	AL_EXIT( AL_DBG_MAD_SVC );
}



static void
__mad_disp_queue_send(
	IN		const	al_mad_reg_handle_t			h_mad_reg,
	IN				al_mad_wr_t* const			p_mad_wr )
{
	ib_mad_t				*p_mad_hdr;

	/*
	 * Increment the reference count on the registration to ensure that
	 * the MAD service does not go away until the send completes.
	 */
	AL_ENTER( AL_DBG_MAD_SVC );
	cl_atomic_inc( &h_mad_reg->ref_cnt );
	ref_al_obj( &h_mad_reg->h_mad_svc->obj );

	/* Get the MAD header. */
	p_mad_hdr = get_mad_hdr_from_wr( p_mad_wr );
	CL_ASSERT( !p_mad_wr->send_wr.wr_id );
	p_mad_wr->send_wr.wr_id = (uintn_t)p_mad_wr;

	/*
	 * If we are the originator of the transaction, we need to modify the
	 * TID to ensure that duplicate TIDs are not used by multiple clients.
	 */
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("dispatching TID: 0x%I64x\n",
		p_mad_hdr->trans_id) );
	p_mad_wr->client_tid = p_mad_hdr->trans_id;
	if( __use_tid_routing( p_mad_hdr, TRUE ) )
	{
		/* Clear the AL portion of the TID before setting. */
		((al_tid_t*)&p_mad_hdr->trans_id)->tid32.al_tid = 0;

#pragma warning( push, 3 )
		al_set_al_tid( &p_mad_hdr->trans_id, h_mad_reg->client_id );
#pragma warning( pop )

		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("modified TID to: 0x%0I64x\n", p_mad_hdr->trans_id) );
	}

	/* Post the work request to the QP. */
	p_mad_wr->client_id = h_mad_reg->client_id;
	h_mad_reg->h_mad_disp->h_qp->pfn_queue_mad(
		h_mad_reg->h_mad_disp->h_qp, p_mad_wr );

	AL_EXIT( AL_DBG_MAD_SVC );
}


static inline void
__mad_disp_resume_send(
	IN		const	al_mad_reg_handle_t			h_mad_reg )
{
	AL_ENTER( AL_DBG_MAD_SVC );

	h_mad_reg->h_mad_disp->h_qp->pfn_resume_mad(
		h_mad_reg->h_mad_disp->h_qp );

	AL_EXIT( AL_DBG_MAD_SVC );
}


/*
 * Complete a sent MAD.  Route the completion to the correct MAD service.
 */
void
mad_disp_send_done(
	IN				al_mad_disp_handle_t		h_mad_disp,
	IN				al_mad_wr_t					*p_mad_wr,
	IN				ib_wc_t						*p_wc )
{
	al_mad_reg_handle_t		h_mad_reg;
	ib_mad_t				*p_mad_hdr;

	AL_ENTER( AL_DBG_MAD_SVC );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
		("p_mad_wr 0x%p\n", p_mad_wr ) );

	/* Get the MAD header. */
	p_mad_hdr = get_mad_hdr_from_wr( p_mad_wr );

	/* Get the MAD service that issued the send. */
	cl_spinlock_acquire( &h_mad_disp->obj.lock );
	h_mad_reg = cl_vector_get_ptr( &h_mad_disp->client_vector,
		p_mad_wr->client_id );
	cl_spinlock_release( &h_mad_disp->obj.lock );
	CL_ASSERT( h_mad_reg && (h_mad_reg->client_id == p_mad_wr->client_id) );

	/* Reset the TID and WR ID. */
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("send done TID: 0x%I64x\n",
		p_mad_hdr->trans_id) );
	p_mad_hdr->trans_id = p_mad_wr->client_tid;
	p_mad_wr->send_wr.wr_id = 0;

	/* Return the completed request to the MAD service. */
	CL_ASSERT( h_mad_reg->h_mad_svc );
	h_mad_reg->pfn_send_done( h_mad_reg->h_mad_svc, p_mad_wr, p_wc );

	/* The MAD service is no longer referenced once the send completes. */
	deref_al_obj( &h_mad_reg->h_mad_svc->obj );
	cl_atomic_dec( &h_mad_reg->ref_cnt );

	AL_EXIT( AL_DBG_MAD_SVC );
}



/*
 * Process a received MAD.  Route the completion to the correct MAD service.
 */
ib_api_status_t
mad_disp_recv_done(
	IN				al_mad_disp_handle_t		h_mad_disp,
	IN				ib_mad_element_t			*p_mad_element )
{
	ib_mad_t				*p_mad_hdr;
	al_mad_reg_handle_t		h_mad_reg;
	ib_al_handle_t			h_al;
	ib_mad_svc_handle_t		h_mad_svc;

	cl_vector_t				*p_class_vector;
	cl_ptr_vector_t			*p_method_ptr_vector;
	uint8_t					method;

	AL_ENTER( AL_DBG_MAD_SVC );
	p_mad_hdr = ib_get_mad_buf( p_mad_element );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
		("TID = 0x%I64x\n"
		 "class = 0x%x.\n"
		 "version = 0x%x.\n"
		 "method = 0x%x.\n",
		p_mad_hdr->trans_id,
		p_mad_hdr->mgmt_class,
		p_mad_hdr->class_ver,
		p_mad_hdr->method) );

	/* Get the client to route the receive to. */
	cl_spinlock_acquire( &h_mad_disp->obj.lock );
	if( __use_tid_routing( p_mad_hdr, FALSE ) )
	{
		/* The MAD was received in response to a send. */
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("routing based on TID\n"));

		/* Verify that we have a registration entry. */
		if( al_get_al_tid( p_mad_hdr->trans_id ) >=
			cl_vector_get_size( &h_mad_disp->client_vector ) )
		{
			/* No clients for this version-class-method. */
			cl_spinlock_release( &h_mad_disp->obj.lock );
			AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
				("invalid client ID\n") );
			return IB_NOT_FOUND;
		}

		h_mad_reg = cl_vector_get_ptr( &h_mad_disp->client_vector,
			al_get_al_tid( p_mad_hdr->trans_id ) );

/*
 * Disable warning about passing unaligned 64-bit value.
 * The value is always aligned given how buffers are allocated
 * and given the layout of a MAD.
 */
#pragma warning( push, 3 )
		al_set_al_tid( &p_mad_hdr->trans_id, 0 );
#pragma warning( pop )
	}
	else
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("routing based on version, class, method\n"));

		/* The receive is unsolicited.  Find the client. */
		if( __mgmt_version_index( p_mad_hdr->class_ver ) >=
			cl_vector_get_size( &h_mad_disp->version_vector ) )
		{
			/* No clients for this version of MADs. */
			cl_spinlock_release( &h_mad_disp->obj.lock );
			AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
				("no clients registered for this class version\n") );
			return IB_NOT_FOUND;
		}

		/* See if we have a client for this class of MADs. */
		p_class_vector = cl_vector_get_ptr( &h_mad_disp->version_vector,
			__mgmt_version_index( p_mad_hdr->class_ver ) );

		if( __mgmt_class_index( p_mad_hdr->mgmt_class ) >=
			cl_vector_get_size( p_class_vector ) )
		{
			/* No clients for this version-class. */
			cl_spinlock_release( &h_mad_disp->obj.lock );
			AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
				("no clients registered for this class\n") );
			return IB_NOT_FOUND;
		}

		/* See if we have a client for this method. */
		p_method_ptr_vector = cl_vector_get_ptr( p_class_vector,
			__mgmt_class_index( p_mad_hdr->mgmt_class ) );
		method = (uint8_t)(p_mad_hdr->method & (~IB_MAD_METHOD_RESP_MASK));

		if( method >= cl_ptr_vector_get_size( p_method_ptr_vector ) )
		{
			/* No clients for this version-class-method. */
			cl_spinlock_release( &h_mad_disp->obj.lock );
			AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
				("no clients registered for this method-out of range\n") );
			return IB_NOT_FOUND;
		}

		h_mad_reg = cl_ptr_vector_get( p_method_ptr_vector, method );
		if( !h_mad_reg )
		{
			/* No clients for this version-class-method. */
			cl_spinlock_release( &h_mad_disp->obj.lock );
			AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
				("no clients registered for method %u of class %u(%u) version %u(%u)\n",
				 method,
				 p_mad_hdr->mgmt_class,
				 __mgmt_class_index( p_mad_hdr->mgmt_class ),
				 p_mad_hdr->class_ver,
				 __mgmt_version_index( p_mad_hdr->class_ver )
				 ) );
			return IB_NOT_FOUND;
		}
	}

	/* Verify that the registration is still valid. */
	if( !h_mad_reg->ref_cnt )
	{
		cl_spinlock_release( &h_mad_disp->obj.lock );
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
			("no client registered\n") );
		return IB_NOT_FOUND;
	}

	/* Take a reference on the MAD service in case it deregisters. */
	h_mad_svc = h_mad_reg->h_mad_svc;
	ref_al_obj( &h_mad_svc->obj );
	cl_spinlock_release( &h_mad_disp->obj.lock );

	/* Handoff the MAD to the correct AL instance. */
	h_al = qp_get_al( (ib_qp_handle_t)(h_mad_svc->obj.p_parent_obj) );
	al_handoff_mad( h_al, p_mad_element );

	h_mad_reg->pfn_recv_done( h_mad_svc, p_mad_element );
	deref_al_obj( &h_mad_svc->obj );
	AL_EXIT( AL_DBG_MAD_SVC );
	return IB_SUCCESS;
}



/*
 * Return TRUE if we should route the MAD to the recipient based on the TID.
 */
static boolean_t
__use_tid_routing(
	IN		const	ib_mad_t* const				p_mad_hdr,
	IN		const	boolean_t					are_we_sender )
{
	boolean_t			is_orig;

	AL_ENTER( AL_DBG_MAD_SVC );

	/* CM MADs are never TID routed. */
	if( p_mad_hdr->mgmt_class == IB_MCLASS_COMM_MGMT )
	{
		AL_EXIT( AL_DBG_MAD_SVC );
		return FALSE;
	}

	if (are_we_sender)
		is_orig = !ib_mad_is_response(p_mad_hdr);
	else
		is_orig = ib_mad_is_response(p_mad_hdr);

	AL_EXIT( AL_DBG_MAD_SVC );
	return is_orig;
}



/*
 *
 * MAD Service.
 *
 */



/*
 * Create and initialize a MAD service for use.
 */
ib_api_status_t
reg_mad_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mad_svc_t* const			p_mad_svc,
		OUT			ib_mad_svc_handle_t* const	ph_mad_svc )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;
	ib_mad_svc_handle_t	h_mad_svc;
	al_qp_alias_t		*p_qp_alias;
	ib_qp_attr_t		qp_attr;
    static ULONG        seed = 0;

	AL_ENTER( AL_DBG_MAD_SVC );
	CL_ASSERT( h_qp );

	switch( h_qp->type )
	{
	case IB_QPT_QP0:
	case IB_QPT_QP1:
	case IB_QPT_QP0_ALIAS:
	case IB_QPT_QP1_ALIAS:
	case IB_QPT_MAD:
		break;

	default:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	if( !p_mad_svc || !ph_mad_svc )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	h_mad_svc = cl_zalloc( sizeof( al_mad_svc_t) );
	if( !h_mad_svc )
	{
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the MAD service. */
	construct_al_obj( &h_mad_svc->obj, AL_OBJ_TYPE_H_MAD_SVC );
	cl_timer_construct( &h_mad_svc->send_timer );
	cl_timer_construct( &h_mad_svc->recv_timer );
	cl_qlist_init( &h_mad_svc->send_list );
	cl_qlist_init( &h_mad_svc->recv_list );

    if( seed == 0 )
    {
        seed = (ULONG)(ULONG_PTR)p_mad_svc;
    }
#ifdef CL_KERNEL
    h_mad_svc->send_jitter = RtlRandomEx( &seed );
#endif

	p_qp_alias = PARENT_STRUCT( h_qp, al_qp_alias_t, qp );
	h_mad_svc->svc_type = p_mad_svc->svc_type;
	h_mad_svc->obj.context = p_mad_svc->mad_svc_context;
	h_mad_svc->pfn_user_recv_cb = p_mad_svc->pfn_mad_recv_cb;
	h_mad_svc->pfn_user_send_cb = p_mad_svc->pfn_mad_send_cb;

	/* Initialize the MAD service. */
	status = init_al_obj( &h_mad_svc->obj, p_mad_svc->mad_svc_context,
		TRUE, __destroying_mad_svc, __cleanup_mad_svc, free_mad_svc );
	if( status != IB_SUCCESS )
	{
		free_mad_svc( &h_mad_svc->obj );
		return status;
	}
	status = attach_al_obj( &h_qp->obj, &h_mad_svc->obj );
	if( status != IB_SUCCESS )
	{
		h_mad_svc->obj.pfn_destroy( &h_mad_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Record which port this MAD service uses, to use when creating AVs. */
	status = ib_query_qp( h_qp, &qp_attr );
	if( status != IB_SUCCESS )
	{
		h_mad_svc->obj.pfn_destroy( &h_mad_svc->obj, NULL );
		return status;
	}
	h_mad_svc->h_pd = qp_attr.h_pd;
	h_mad_svc->port_num = qp_attr.primary_port;

	cl_status = cl_timer_init( &h_mad_svc->send_timer,
		__send_timer_cb, h_mad_svc );
	if( cl_status != CL_SUCCESS )
	{
		h_mad_svc->obj.pfn_destroy( &h_mad_svc->obj, NULL );
		return ib_convert_cl_status( cl_status );
	}

	cl_status = cl_timer_init( &h_mad_svc->recv_timer,
		__recv_timer_cb, h_mad_svc );
	if( cl_status != CL_SUCCESS )
	{
		h_mad_svc->obj.pfn_destroy( &h_mad_svc->obj, NULL );
		return ib_convert_cl_status( cl_status );
	}

	*ph_mad_svc = h_mad_svc;
	
	h_mad_svc->h_mad_reg = __mad_disp_reg( p_qp_alias->h_mad_disp,
		h_mad_svc, p_mad_svc, __mad_svc_send_done, __mad_svc_recv_done );
	if( !h_mad_svc->h_mad_reg )
	{
		h_mad_svc->obj.pfn_destroy( &h_mad_svc->obj, NULL );
		return IB_INSUFFICIENT_MEMORY;
	}

	AL_EXIT( AL_DBG_MAD_SVC );
	return IB_SUCCESS;
}



static void
__destroying_mad_svc(
	IN				struct _al_obj				*p_obj )
{
	ib_qp_handle_t			h_qp;
	ib_mad_svc_handle_t		h_mad_svc;
	ib_mad_send_handle_t	h_send;
	cl_list_item_t			*p_list_item;
	int32_t					timeout_ms;
#ifdef CL_KERNEL
	KIRQL					old_irql;
#endif

	AL_ENTER( AL_DBG_MAD_SVC );
	CL_ASSERT( p_obj );
	h_mad_svc = PARENT_STRUCT( p_obj, al_mad_svc_t, obj );

	/* Deregister the MAD service. */
	h_qp = (ib_qp_handle_t)p_obj->p_parent_obj;
	if( h_qp->pfn_dereg_mad_svc )
		h_qp->pfn_dereg_mad_svc( h_mad_svc );

	/* Wait here until the MAD service is no longer in use. */
	timeout_ms = (int32_t)h_mad_svc->obj.timeout_ms;
	while( h_mad_svc->ref_cnt && timeout_ms > 0 )
	{
		/* Use a timeout to avoid waiting forever - just in case. */
		cl_thread_suspend( 10 );
		timeout_ms -= 10;
	}

	/*
	 * Deregister from the MAD dispatcher.  The MAD dispatcher holds
	 * a reference on the MAD service when invoking callbacks.  Since we
	 * issue sends, we know how many callbacks are expected for send
	 * completions.  With receive completions, we need to wait until all
	 * receive callbacks have completed before cleaning up receives.
	 */
	if( h_mad_svc->h_mad_reg )
		__mad_disp_dereg( h_mad_svc->h_mad_reg );

	/* Cancel all outstanding send requests. */
	cl_spinlock_acquire( &h_mad_svc->obj.lock );
	for( p_list_item = cl_qlist_head( &h_mad_svc->send_list );
		 p_list_item != cl_qlist_end( &h_mad_svc->send_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("canceling MAD\n") );
		h_send = PARENT_STRUCT( p_list_item, al_mad_send_t, pool_item );
		h_send->canceled = TRUE;
	}
	cl_spinlock_release( &h_mad_svc->obj.lock );

	/*
	 * Invoke the timer callback to return the canceled MADs to the user.
	 * Since the MAD service is being destroyed, the user cannot be issuing
	 * sends.
	 */
	if( h_mad_svc->h_mad_reg )
	{
#ifdef CL_KERNEL
		old_irql = KeRaiseIrqlToDpcLevel();
#endif
		__check_send_queue( h_mad_svc );
#ifdef CL_KERNEL
		KeLowerIrql( old_irql );
#endif
	}

	cl_timer_destroy( &h_mad_svc->send_timer );

#ifdef CL_KERNEL
	/*
	 * Reclaim any pending receives sent to the proxy for UAL.
	 */
	if( h_mad_svc->obj.h_al->p_context )
	{
		cl_qlist_t					*p_cblist;
		al_proxy_cb_info_t			*p_cb_info;

		cl_spinlock_acquire( &h_mad_svc->obj.h_al->p_context->cb_lock );
		p_cblist = &h_mad_svc->obj.h_al->p_context->misc_cb_list;
		p_list_item = cl_qlist_head( p_cblist );
		while( p_list_item != cl_qlist_end( p_cblist ) )
		{
			p_cb_info = (al_proxy_cb_info_t*)p_list_item;
			p_list_item = cl_qlist_next( p_list_item );

			if( p_cb_info->p_al_obj && p_cb_info->p_al_obj == &h_mad_svc->obj )
			{
				cl_qlist_remove_item( p_cblist, &p_cb_info->pool_item.list_item );
				deref_al_obj( p_cb_info->p_al_obj );
				proxy_cb_put( p_cb_info );
			}
		}
		cl_spinlock_release( &h_mad_svc->obj.h_al->p_context->cb_lock );
	}
#endif

	AL_EXIT( AL_DBG_MAD_SVC );
}



static void
__cleanup_mad_svc(
	IN				struct _al_obj				*p_obj )
{
	ib_mad_svc_handle_t		h_mad_svc;
	al_mad_rmpp_t			*p_rmpp;
	cl_list_item_t			*p_list_item;

	CL_ASSERT( p_obj );
	h_mad_svc = PARENT_STRUCT( p_obj, al_mad_svc_t, obj );

	/*
	 * There are no more callbacks from the MAD dispatcher that are active.
	 * Cleanup any receives that may still be lying around.  Stop the receive
	 * timer to avoid synchronizing with it.
	 */
	cl_timer_destroy( &h_mad_svc->recv_timer );
	for( p_list_item = cl_qlist_head( &h_mad_svc->recv_list );
		 p_list_item != cl_qlist_end( &h_mad_svc->recv_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_rmpp = PARENT_STRUCT( p_list_item, al_mad_rmpp_t, pool_item );
		p_rmpp->inactive = TRUE;
	}
	__recv_timer_cb( h_mad_svc );

	CL_ASSERT( cl_is_qlist_empty( &h_mad_svc->send_list ) );
	CL_ASSERT( cl_is_qlist_empty( &h_mad_svc->recv_list ) );
}



void
free_mad_svc(
	IN				al_obj_t					*p_obj )
{
	ib_mad_svc_handle_t	h_mad_svc;

	CL_ASSERT( p_obj );
	h_mad_svc = PARENT_STRUCT( p_obj, al_mad_svc_t, obj );

	destroy_al_obj( p_obj );
	cl_free( h_mad_svc );
}



ib_api_status_t
ib_send_mad(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element_list,
		OUT			ib_mad_element_t			**pp_mad_failure OPTIONAL )
{
	ib_api_status_t				status = IB_SUCCESS;
#ifdef CL_KERNEL
	ib_mad_send_handle_t		h_send;
	ib_mad_element_t			*p_cur_mad, *p_next_mad;
#endif

	AL_ENTER( AL_DBG_MAD_SVC );

	if( AL_OBJ_INVALID_HANDLE( h_mad_svc, AL_OBJ_TYPE_H_MAD_SVC ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}
	if( !p_mad_element_list ||
		( p_mad_element_list->p_next && !pp_mad_failure ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

#ifndef CL_KERNEL
	/* This is a send from user mode using special QP alias */
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
		("ib_send_mad: ual_context non-zero, TID = 0x%I64x.\n",
		((ib_mad_t*)(ib_get_mad_buf( p_mad_element_list )))->trans_id ));
	status = spl_qp_mad_send( h_mad_svc, p_mad_element_list,
		pp_mad_failure );
	AL_EXIT( AL_DBG_MAD_SVC );
	return status;
#else
	/* Post each send on the list. */
	p_cur_mad = p_mad_element_list;
	while( p_cur_mad )
	{
		p_next_mad = p_cur_mad->p_next;

		/* Get an element to track the send. */
		h_send = get_mad_send( PARENT_STRUCT( p_cur_mad,
			al_mad_element_t, element ) );
		if( !h_send )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("unable to get mad_send\n") );
			if( pp_mad_failure )
				*pp_mad_failure = p_cur_mad;
			return IB_INSUFFICIENT_RESOURCES;
		}

		/* Initialize the MAD for sending. */
		status = __init_send_mad( h_mad_svc, h_send, p_cur_mad );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("init_send_mad failed: %s\n",
				ib_get_err_str(status)) );
			put_mad_send( h_send );
			if( pp_mad_failure )
				*pp_mad_failure = p_cur_mad;
			return status;
		}

		/* Add the MADs to our list. */
		cl_spinlock_acquire( &h_mad_svc->obj.lock );
		cl_qlist_insert_tail( &h_mad_svc->send_list,
			(cl_list_item_t*)&h_send->pool_item );

		/* Post the MAD to the dispatcher, and check for failures. */
		ref_al_obj( &h_mad_svc->obj );
		p_cur_mad->p_next = NULL;
		if( h_send->uses_rmpp )
			__queue_rmpp_seg( h_mad_svc->h_mad_reg, h_send );
		else
			__queue_mad_wr( h_mad_svc->h_mad_reg, h_send );
		cl_spinlock_release( &h_mad_svc->obj.lock );

		p_cur_mad = p_next_mad;
	}

	/*
	 * Resume any sends that can now be sent without holding
	 * the mad service lock.
	 */
	__mad_disp_resume_send( h_mad_svc->h_mad_reg );

	AL_EXIT( AL_DBG_MAD_SVC );
	return status;
#endif
}



static ib_api_status_t
__init_send_mad(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN		const	ib_mad_send_handle_t		h_send,
	IN				ib_mad_element_t* const		p_mad_element )
{
	ib_rmpp_mad_t		*p_rmpp_hdr;
	uint8_t				rmpp_version;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MAD_SVC );

	/* Initialize tracking the send. */
	h_send->p_send_mad = p_mad_element;
	h_send->retry_time = MAX_TIME;
	h_send->retry_cnt = p_mad_element->retry_cnt;

	/* See if the send uses RMPP. */
	h_send->uses_rmpp = __does_send_req_rmpp( h_mad_svc->svc_type,
		p_mad_element, &rmpp_version );
	if( h_send->uses_rmpp )
	{
		/* The RMPP header is present. */
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("RMPP is activated\n") );
		p_rmpp_hdr = (ib_rmpp_mad_t*)p_mad_element->p_mad_buf;

		/* We only support version 1. */
		if( rmpp_version != DEFAULT_RMPP_VERSION )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("unsupported version\n") );
			return IB_INVALID_SETTING;
		}

		if( !p_mad_element->timeout_ms )
			p_mad_element->timeout_ms = AL_REASSEMBLY_TIMEOUT;

		if( !h_send->retry_cnt )
			h_send->retry_cnt = AL_RMPP_RETRIES;

		p_rmpp_hdr->rmpp_version = rmpp_version;
		p_rmpp_hdr->rmpp_type = IB_RMPP_TYPE_DATA;
		ib_rmpp_set_resp_time( p_rmpp_hdr, IB_RMPP_NO_RESP_TIME );
		p_rmpp_hdr->rmpp_status = IB_RMPP_STATUS_SUCCESS;
		/*
		 * The segment number, flags, and payload size are set when
		 * sending, so that they are set correctly when issuing retries.
		 */

		h_send->ack_seg = 0;
		h_send->seg_limit = 1;
		h_send->cur_seg = 1;
		/* For SA RMPP MADS we need different data size and header size */
		if( p_mad_element->p_mad_buf->mgmt_class == IB_MCLASS_SUBN_ADM )
		{
			h_send->total_seg = ( (p_mad_element->size - IB_SA_MAD_HDR_SIZE) +
				(IB_SA_DATA_SIZE - 1) ) / IB_SA_DATA_SIZE;
		} 
		else 
		{
			h_send->total_seg = ( (p_mad_element->size - MAD_RMPP_HDR_SIZE) +
				(MAD_RMPP_DATA_SIZE - 1) ) / MAD_RMPP_DATA_SIZE;
		}
		/*for cases that there is no data we still need 1 seg */
		h_send->total_seg = h_send->total_seg?h_send->total_seg:1;
	}

	/* See if we need to create the address vector for the user. 
		We also create AV for local send to pass the slid and grh in case of trap generation*/
	if( !p_mad_element->h_av){

		status = __create_send_av( h_mad_svc, h_send );
		if( status != IB_SUCCESS )
		{
			return status;
		}
	}

	AL_EXIT( AL_DBG_MAD_SVC );
	return IB_SUCCESS;
}



static ib_api_status_t
__create_send_av(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_send_handle_t		h_send )
{
	ib_av_attr_t		av_attr;
	ib_mad_element_t	*p_mad_element;

	p_mad_element = h_send->p_send_mad;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
		("class %d attr %d AV attr: port num %d, sl %d, dlid %d, path bits %d",
		p_mad_element->p_mad_buf->mgmt_class,
		p_mad_element->p_mad_buf->attr_id,
		h_mad_svc->port_num,
		p_mad_element->remote_sl,
		p_mad_element->remote_lid,
		p_mad_element->path_bits)
		);

	av_attr.port_num = h_mad_svc->port_num;

	av_attr.sl = p_mad_element->remote_sl;
	av_attr.dlid = p_mad_element->remote_lid;

	av_attr.grh_valid = p_mad_element->grh_valid;
	if( av_attr.grh_valid )
	{
		av_attr.grh = *p_mad_element->p_grh;
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("ver_class_flow %08x, hop lmt %d, src gid %16I64x%16I64x, dest gid %16I64x%16I64x",
			av_attr.grh.ver_class_flow,
			av_attr.grh.hop_limit,
			cl_ntoh64( av_attr.grh.src_gid.unicast.prefix ),
			cl_ntoh64( av_attr.grh.src_gid.unicast.interface_id ),
			cl_ntoh64( av_attr.grh.dest_gid.unicast.prefix ),
			cl_ntoh64( av_attr.grh.dest_gid.unicast.interface_id ))
			);
	}

	av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
	av_attr.path_bits = p_mad_element->path_bits;

	return ib_create_av_ctx( h_mad_svc->h_pd, &av_attr, 2, &h_send->h_av );
}



static boolean_t
__does_send_req_rmpp(
	IN		const	ib_mad_svc_type_t			mad_svc_type,
	IN		const	ib_mad_element_t* const		p_mad_element,
		OUT			uint8_t						*p_rmpp_version )
{
	switch( mad_svc_type )
	{
	case IB_MAD_SVC_DEFAULT:
	case IB_MAD_SVC_RMPP:
		/* Internally generated MADs do not use RMPP. */
		if( __is_internal_send( mad_svc_type, p_mad_element ) )
			return FALSE;

		/* If the MAD has the version number set, just return it. */
		if( p_mad_element->rmpp_version )
		{
			*p_rmpp_version = p_mad_element->rmpp_version;
			return TRUE;
		}

		/* If the class is well known and uses RMPP, use the default version. */
		if( p_mad_element->p_mad_buf->mgmt_class == IB_MCLASS_SUBN_ADM )
		{
			switch( p_mad_element->p_mad_buf->method )
			{
			case IB_MAD_METHOD_GETTABLE_RESP:
			case IB_MAD_METHOD_GETMULTI:
			case IB_MAD_METHOD_GETMULTI_RESP:
				*p_rmpp_version = DEFAULT_RMPP_VERSION;
				return TRUE;

			default:
				return FALSE;
			}
		}
		if (ib_class_is_vendor_specific_high(p_mad_element->p_mad_buf->mgmt_class) &&
			ib_rmpp_is_flag_set((ib_rmpp_mad_t *) p_mad_element->p_mad_buf,
								IB_RMPP_FLAG_ACTIVE))
		{
			*p_rmpp_version = DEFAULT_RMPP_VERSION;
			return TRUE;
		}

		/* The RMPP is not active. */
		return FALSE;

	default:
		return FALSE;
	}
}



/*
 * Sends the next RMPP segment of an RMPP transfer.
 */
static void
__queue_rmpp_seg(
	IN		const	al_mad_reg_handle_t			h_mad_reg,
	IN				ib_mad_send_handle_t		h_send )
{
	ib_rmpp_mad_t		*p_rmpp_hdr;

	AL_ENTER( AL_DBG_MAD_SVC );

	CL_ASSERT( h_mad_reg && h_send );
	CL_ASSERT( h_send->cur_seg <= h_send->seg_limit );

	/* Reset information to track the send. */
	h_send->retry_time = MAX_TIME;

	/* Set the RMPP header information. */
	p_rmpp_hdr = (ib_rmpp_mad_t*)h_send->p_send_mad->p_mad_buf;
	p_rmpp_hdr->seg_num = cl_hton32( h_send->cur_seg );
	p_rmpp_hdr->rmpp_flags = IB_RMPP_FLAG_ACTIVE;
	p_rmpp_hdr->paylen_newwin = 0;

	/* See if this is the first segment that needs to be sent. */
	if( h_send->cur_seg == 1 )
	{
		p_rmpp_hdr->rmpp_flags |= IB_RMPP_FLAG_FIRST;

		/*
		 * Since the RMPP layer is the one to support SA MADs by duplicating
		 * the SA header. The actual Payload Length should include the
		 * original mad size + NumSegs * SA-extra-header.
		 */
		if( h_send->p_send_mad->p_mad_buf->mgmt_class == IB_MCLASS_SUBN_ADM )
		{
			/* Add sa_ext_hdr to each segment over the first one. */
			p_rmpp_hdr->paylen_newwin = cl_hton32(
				h_send->p_send_mad->size - MAD_RMPP_HDR_SIZE +
				(h_send->total_seg - 1) * 
				(IB_SA_MAD_HDR_SIZE - MAD_RMPP_HDR_SIZE) );
		}
		else 
		{
			/* For other RMPP packets we simply use the given MAD */
			p_rmpp_hdr->paylen_newwin = cl_hton32( h_send->p_send_mad->size -
				MAD_RMPP_HDR_SIZE );
		}
	}

	/* See if this is the last segment that needs to be sent. */
	if( h_send->cur_seg == h_send->total_seg )
	{
		p_rmpp_hdr->rmpp_flags |= IB_RMPP_FLAG_LAST;

		/* But for SA MADs we need extra header size */
		if( h_send->p_send_mad->p_mad_buf->mgmt_class == IB_MCLASS_SUBN_ADM )
		{
			p_rmpp_hdr->paylen_newwin = cl_hton32( h_send->p_send_mad->size -
				(h_send->cur_seg -1)*IB_SA_DATA_SIZE - MAD_RMPP_HDR_SIZE );
		}
		else
		{
			p_rmpp_hdr->paylen_newwin = cl_hton32( h_send->p_send_mad->size -
				(h_send->cur_seg -1)*MAD_RMPP_DATA_SIZE );
		}
	}

	/* Set the current segment to the next one. */
	h_send->cur_seg++;

	/* Send the MAD. */
	__queue_mad_wr( h_mad_reg, h_send );

	AL_EXIT( AL_DBG_MAD_SVC );
}



/*
 * Posts a send work request to the dispatcher for a MAD send.
 */
static void
__queue_mad_wr(
	IN		const	al_mad_reg_handle_t			h_mad_reg,
	IN		const	ib_mad_send_handle_t		h_send )
{
	ib_send_wr_t		*p_send_wr;
	al_mad_element_t	*p_al_element;
	ib_rmpp_mad_t		*p_rmpp_hdr;
	uint8_t				*p_rmpp_src, *p_rmpp_dst;
	uintn_t				hdr_len, offset, max_len;

	AL_ENTER( AL_DBG_MAD_SVC );
	p_send_wr = &h_send->mad_wr.send_wr;

	cl_memclr( p_send_wr, sizeof( ib_send_wr_t ) );

	p_send_wr->wr_type = WR_SEND;
	p_send_wr->send_opt = h_send->p_send_mad->send_opt;

	p_al_element = PARENT_STRUCT( h_send->p_send_mad,
		al_mad_element_t, element );

	/* See if the MAD requires RMPP support. */
	if( h_send->uses_rmpp && p_al_element->p_al_mad_buf )
	{
#if defined( CL_KERNEL )
		p_rmpp_dst = p_al_element->mad_buf + sizeof(ib_grh_t);
#else
		p_rmpp_dst = (uint8_t*)(uintn_t)p_al_element->mad_ds.vaddr;
#endif
		p_rmpp_src = (uint8_t*)h_send->p_send_mad->p_mad_buf;
		p_rmpp_hdr = (ib_rmpp_mad_t*)p_rmpp_src;

		if( h_send->p_send_mad->p_mad_buf->mgmt_class == IB_MCLASS_SUBN_ADM )
			hdr_len = IB_SA_MAD_HDR_SIZE;
		else
			hdr_len = MAD_RMPP_HDR_SIZE;

		max_len = MAD_BLOCK_SIZE - hdr_len;

		offset = hdr_len + (max_len * (cl_ntoh32( p_rmpp_hdr->seg_num ) - 1));

		/* Copy the header into the registered send buffer. */
		cl_memcpy( p_rmpp_dst, p_rmpp_src, hdr_len );

		/* Copy this segment's payload into the registered send buffer. */
		CL_ASSERT( h_send->p_send_mad->size != offset );
		if( (h_send->p_send_mad->size - offset) < max_len )
		{
			max_len = h_send->p_send_mad->size - offset;
			/* Clear unused payload. */
			cl_memclr( p_rmpp_dst + hdr_len + max_len,
				MAD_BLOCK_SIZE - hdr_len - max_len );
		}

		cl_memcpy(
			p_rmpp_dst + hdr_len, p_rmpp_src + offset, max_len );
	}

	p_send_wr->num_ds = 1;
	p_send_wr->ds_array = &p_al_element->mad_ds;

	p_send_wr->dgrm.ud.remote_qp = h_send->p_send_mad->remote_qp;
	p_send_wr->dgrm.ud.remote_qkey = h_send->p_send_mad->remote_qkey;
	p_send_wr->dgrm.ud.pkey_index = h_send->p_send_mad->pkey_index;

	/* See if we created the address vector on behalf of the user. */
	if( h_send->p_send_mad->h_av )
		p_send_wr->dgrm.ud.h_av = h_send->p_send_mad->h_av;
	else
		p_send_wr->dgrm.ud.h_av = h_send->h_av;

	__mad_disp_queue_send( h_mad_reg, &h_send->mad_wr );

	AL_EXIT( AL_DBG_MAD_SVC );
}



static cl_status_t
__mad_svc_find_send(
	IN		const	cl_list_item_t* const		p_list_item,
	IN				void*						context )
{
	ib_mad_send_handle_t	h_send;

	h_send = PARENT_STRUCT( p_list_item, al_mad_send_t, pool_item );

	if( h_send->p_send_mad == context )
		return CL_SUCCESS;
	else
		return CL_NOT_FOUND;
}



ib_api_status_t
ib_cancel_mad(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element )
{
#ifdef CL_KERNEL
	cl_list_item_t			*p_list_item;
	ib_mad_send_handle_t	h_send;
#else
	ib_api_status_t			status;
#endif

	AL_ENTER( AL_DBG_MAD_SVC );

	if( AL_OBJ_INVALID_HANDLE( h_mad_svc, AL_OBJ_TYPE_H_MAD_SVC ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}
	if( !p_mad_element )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

#ifndef CL_KERNEL
	/* This is a send from user mode using special QP alias */
	status = spl_qp_cancel_mad( h_mad_svc, p_mad_element );
	AL_EXIT( AL_DBG_MAD_SVC );
	return status;
#else
	/* Search for the MAD in our MAD list.  It may have already completed. */
	cl_spinlock_acquire( &h_mad_svc->obj.lock );
	p_list_item = cl_qlist_find_from_head( &h_mad_svc->send_list,
		__mad_svc_find_send, p_mad_element );

	if( p_list_item == cl_qlist_end( &h_mad_svc->send_list ) )
	{
		cl_spinlock_release( &h_mad_svc->obj.lock );
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("mad not found\n") );
		return IB_NOT_FOUND;
	}

	/* Mark the MAD as having been canceled. */
	h_send = PARENT_STRUCT( p_list_item, al_mad_send_t, pool_item );
	h_send->canceled = TRUE;

	/* If the MAD is active, process it in the send callback. */
	if( h_send->retry_time != MAX_TIME )
	{
		/* Process the canceled MAD using the timer thread. */
		cl_timer_trim( &h_mad_svc->send_timer, 0 );
	}

	cl_spinlock_release( &h_mad_svc->obj.lock );
	AL_EXIT( AL_DBG_MAD_SVC );
	return IB_SUCCESS;
#endif
}


ib_api_status_t
ib_delay_mad(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element,
	IN		const	uint32_t					delay_ms )
{
#ifdef CL_KERNEL
	cl_list_item_t			*p_list_item;
	ib_mad_send_handle_t	h_send;
#endif

	AL_ENTER( AL_DBG_MAD_SVC );

	if( AL_OBJ_INVALID_HANDLE( h_mad_svc, AL_OBJ_TYPE_H_MAD_SVC ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}
	if( !p_mad_element )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

#ifndef CL_KERNEL
	UNUSED_PARAM( p_mad_element );
	UNUSED_PARAM( delay_ms );
	/* TODO: support for user-mode MAD QP's. */
	AL_EXIT( AL_DBG_MAD_SVC );
	return IB_UNSUPPORTED;
#else
	/* Search for the MAD in our MAD list.  It may have already completed. */
	cl_spinlock_acquire( &h_mad_svc->obj.lock );
	p_list_item = cl_qlist_find_from_head( &h_mad_svc->send_list,
		__mad_svc_find_send, p_mad_element );

	if( p_list_item == cl_qlist_end( &h_mad_svc->send_list ) )
	{
		cl_spinlock_release( &h_mad_svc->obj.lock );
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("MAD not found\n") );
		return IB_NOT_FOUND;
	}

	/* Mark the MAD as having been canceled. */
	h_send = PARENT_STRUCT( p_list_item, al_mad_send_t, pool_item );

	if( h_send->retry_time == MAX_TIME )
		h_send->delay = delay_ms;
	else
		h_send->retry_time += ((uint64_t)delay_ms * 1000Ui64);

	cl_spinlock_release( &h_mad_svc->obj.lock );
	AL_EXIT( AL_DBG_MAD_SVC );
	return IB_SUCCESS;
#endif
}


/*
 * Process a send completion.
 */
static void
__mad_svc_send_done(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				al_mad_wr_t					*p_mad_wr,
	IN				ib_wc_t						*p_wc )
{
	ib_mad_send_handle_t	h_send;

	AL_ENTER( AL_DBG_MAD_SVC );
	CL_ASSERT( h_mad_svc && p_mad_wr && !p_wc->p_next );

	h_send = PARENT_STRUCT( p_mad_wr, al_mad_send_t, mad_wr );
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("send callback TID:0x%I64x\n",
		__get_send_tid( h_send )) );

	/* We need to synchronize access to the list as well as the MAD request. */
	cl_spinlock_acquire( &h_mad_svc->obj.lock );

	/* Complete internally sent MADs. */
	if( __is_internal_send( h_mad_svc->svc_type, h_send->p_send_mad ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC, ("internal send\n") );
		cl_qlist_remove_item( &h_mad_svc->send_list,
			(cl_list_item_t*)&h_send->pool_item );
		cl_spinlock_release( &h_mad_svc->obj.lock );
		ib_put_mad( h_send->p_send_mad );
		__cleanup_mad_send( h_mad_svc, h_send, 1 );
		return;
	}

	/* See if the send request has completed. */
	if( __is_send_mad_done( h_send, p_wc->status ) )
	{
		/* The send has completed. */
		cl_qlist_remove_item( &h_mad_svc->send_list,
			(cl_list_item_t*)&h_send->pool_item );
		cl_spinlock_release( &h_mad_svc->obj.lock );

		/* Report the send as canceled only if we don't have the response. */
		if( h_send->canceled && !h_send->p_resp_mad )
			__notify_send_comp( h_mad_svc, h_send, IB_WCS_CANCELED );
		else
			__notify_send_comp( h_mad_svc, h_send, p_wc->status );
	}
	else
	{
		/* See if this is an RMPP MAD, and we should send more segments. */
		if( h_send->uses_rmpp && (h_send->cur_seg <= h_send->seg_limit) )
		{
			/* Send the next segment. */
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
				("sending next RMPP segment for TID:0x%I64x\n",
				__get_send_tid( h_send )) );

			__queue_rmpp_seg( h_mad_svc->h_mad_reg, h_send );
		}
		else
		{
			/* Continue waiting for a response or ACK. */
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
				("waiting for response for TID:0x%I64x\n",
				__get_send_tid( h_send )) );

			cl_timer_trim( &h_mad_svc->send_timer,
				__set_retry_time( h_send, h_mad_svc->send_jitter ) );
		}
		cl_spinlock_release( &h_mad_svc->obj.lock );
	}

	/*
	 * Resume any sends that can now be sent without holding
	 * the mad service lock.
	 */
	__mad_disp_resume_send( h_mad_svc->h_mad_reg );

	AL_EXIT( AL_DBG_MAD_SVC );
}



/*
 * Notify the user of a completed send operation.
 */
static void
__notify_send_comp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_send_handle_t		h_send,
	IN				ib_wc_status_t				wc_status )
{
	AL_ENTER( AL_DBG_MAD_SVC );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("completing TID:0x%I64x\n",
		__get_send_tid( h_send )) );

	h_send->p_send_mad->status = wc_status;

	/* Notify the user of a received response, if one exists. */
	if( h_send->p_resp_mad )
	{
		h_mad_svc->pfn_user_recv_cb( h_mad_svc, (void*)h_mad_svc->obj.context,
			h_send->p_resp_mad );
	}

	/* The transaction has completed, return the send MADs. */
	h_mad_svc->pfn_user_send_cb( h_mad_svc, (void*)h_mad_svc->obj.context,
		h_send->p_send_mad );

	__cleanup_mad_send( h_mad_svc, h_send, 2 );

	AL_EXIT( AL_DBG_MAD_SVC );
}



/*
 * Return a send MAD tracking structure to its pool and cleanup any resources
 * it may have allocated.
 */
#pragma warning(disable:4100) //unreferenced formal parameter
static void
__cleanup_mad_send(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_send_handle_t		h_send,
	IN				uint16_t					ctx)
{
	/* Release any address vectors that we may have created. */
	if( h_send->h_av )
	{
		ib_destroy_av_ctx( h_send->h_av, 3 );
		h_send->h_av = NULL;
	}

	/* Return the send MAD tracking structure to its pool. */
	put_mad_send( h_send );

#if DBG
	insert_pool_trace(&g_mad_trace, h_send, POOL_PUT, ctx);
#endif


	/* We no longer need to reference the MAD service. */
	deref_al_obj( &h_mad_svc->obj );
}



static boolean_t
__is_send_mad_done(
	IN				ib_mad_send_handle_t		h_send,
	IN				ib_wc_status_t				status )
{
	AL_ENTER( AL_DBG_MAD_SVC );

	/* Complete the send if the request failed. */
	if( status != IB_WCS_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("y-send failed\n" ) );
		return TRUE;
	}

	/* Complete the send if it has been canceled. */
	if( h_send->canceled )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("y-send was canceled\n") );
		return TRUE;
	}

	/* Complete the send if we have its response. */
	if( h_send->p_resp_mad )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("y-response received\n") );
		return TRUE;
	}

	/* RMPP sends cannot complete until all segments have been acked. */
	if( h_send->uses_rmpp && (h_send->ack_seg < h_send->total_seg) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("more RMPP segments to send\n") );
		return FALSE;
	}

	/*
	 * All segments of this send have been sent.
	 * The send has completed if we are not waiting for a response.
	 */
	if( h_send->p_send_mad->resp_expected )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("no-waiting on response\n") );
		return FALSE;
	}
	else
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("send completed\n") );
		return TRUE;
	}
}



/*
 * Try to find a send that matches the received response.  This call must
 * be synchronized with access to the MAD service send_list.
 */
static ib_mad_send_handle_t
__mad_svc_match_recv(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_recv_mad )
{
	ib_mad_t				*p_recv_hdr;
	cl_list_item_t			*p_list_item;
	ib_mad_send_handle_t	h_send;

	AL_ENTER( AL_DBG_MAD_SVC );

	p_recv_hdr = p_recv_mad->p_mad_buf;

	/* Search the send list for a matching request. */
	for( p_list_item = cl_qlist_head( &h_mad_svc->send_list );
		 p_list_item != cl_qlist_end( &h_mad_svc->send_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		h_send = PARENT_STRUCT( p_list_item, al_mad_send_t, pool_item );

		/* Match on the transaction ID, ignoring internally generated sends. */
		AL_EXIT( AL_DBG_MAD_SVC );
		if( al_get_user_tid(p_recv_hdr->trans_id) ==
			al_get_user_tid(h_send->mad_wr.client_tid) &&
			 !__is_internal_send( h_mad_svc->svc_type, h_send->p_send_mad ) )
		{
			return h_send;
		}
	}

	return NULL;
}



static void
__mad_svc_recv_done(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element )
{
	ib_mad_t				*p_mad_hdr;
	ib_api_status_t			cl_status;

	AL_ENTER( AL_DBG_MAD_SVC );

	p_mad_hdr = ib_get_mad_buf( p_mad_element );
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("recv done TID:0x%I64x\n",
		p_mad_hdr->trans_id) );

	/* Raw MAD services get all receives. */
	if( h_mad_svc->svc_type == IB_MAD_SVC_RAW )
	{
		/* Report the receive. */
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
			("recv TID:0x%I64x\n", p_mad_hdr->trans_id) );
		h_mad_svc->pfn_user_recv_cb( h_mad_svc, (void*)h_mad_svc->obj.context,
			p_mad_element );
		return;
	}

	/* Fully reassemble received MADs before completing them. */
	if( __recv_requires_rmpp( h_mad_svc->svc_type, p_mad_element ) )
	{
		/* Reassembling the receive. */
		cl_status = __do_rmpp_recv( h_mad_svc, &p_mad_element );
		if( cl_status != CL_SUCCESS )
		{
			/* The reassembly is not done. */
			AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
				("no RMPP receive to report\n") );
			return;
		}

		/*
		 * Get the header to the MAD element to report to the user.  This
		 * will be a MAD element received earlier.
		 */
		p_mad_hdr = ib_get_mad_buf( p_mad_element );
	}

	/*
	 * If the response indicates that the responder was busy, continue
	 * retrying the request.
	 */
	if( p_mad_hdr->status & IB_MAD_STATUS_BUSY )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("responder busy TID:0x%I64x\n", p_mad_hdr->trans_id) );
		ib_put_mad( p_mad_element );
		return;
	}

	/*
	 * See if the MAD was sent in response to a previously sent MAD.
	 */
	if( ib_mad_is_response( p_mad_hdr ) )
	{
		/* Process the received response. */
		__process_recv_resp( h_mad_svc, p_mad_element );
	}
	else
	{
		/* Report the receive. */
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("unsol recv TID:0x%I64x\n",
			p_mad_hdr->trans_id) );
		h_mad_svc->pfn_user_recv_cb( h_mad_svc, (void*)h_mad_svc->obj.context,
			p_mad_element );
	}
	AL_EXIT( AL_DBG_MAD_SVC );
}



/*
 * A MAD was received in response to a send.  Find the corresponding send
 * and process the receive completion.
 */
static void
__process_recv_resp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element )
{
	ib_mad_t				*p_mad_hdr;
	ib_mad_send_handle_t	h_send;

	/*
	 * Try to find the send.  The send may have already timed out or
	 * have been canceled, so we need to search for it.
	 */
	AL_ENTER( AL_DBG_MAD_SVC );
	p_mad_hdr = ib_get_mad_buf( p_mad_element );
	cl_spinlock_acquire( &h_mad_svc->obj.lock );

	h_send = __mad_svc_match_recv( h_mad_svc, p_mad_element );
	if( !h_send )
	{
		/* A matching send was not found. */
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
			("unmatched resp TID:0x%I64x\n", p_mad_hdr->trans_id) );
		cl_spinlock_release( &h_mad_svc->obj.lock );
		ib_put_mad( p_mad_element );
		return;
	}

	/* We've found the matching send. */
	h_send->p_send_mad->status = IB_WCS_SUCCESS;

	/* Record the send contexts with the receive. */
	p_mad_element->send_context1 = (void*)h_send->p_send_mad->context1;
	p_mad_element->send_context2 = (void*)h_send->p_send_mad->context2;

	if( h_send->retry_time == MAX_TIME )
	{
		/* The send is currently active.  Do not report it. */
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("resp send active TID:0x%I64x\n", p_mad_hdr->trans_id) );
		/* Handle a duplicate receive happening before the send completion is processed. */
		if( h_send->p_resp_mad )
			ib_put_mad( h_send->p_resp_mad );
		h_send->p_resp_mad = p_mad_element;
		cl_spinlock_release( &h_mad_svc->obj.lock );
	}
	else
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC,
			("resp received TID:0x%I64x\n", p_mad_hdr->trans_id) );

		/* Report the send completion below. */
		cl_qlist_remove_item( &h_mad_svc->send_list,
			(cl_list_item_t*)&h_send->pool_item );
		cl_spinlock_release( &h_mad_svc->obj.lock );

		/* Report the receive. */
		h_mad_svc->pfn_user_recv_cb( h_mad_svc, (void*)h_mad_svc->obj.context,
			p_mad_element );

		/* Report the send completion. */
		h_mad_svc->pfn_user_send_cb( h_mad_svc, (void*)h_mad_svc->obj.context,
			h_send->p_send_mad );
		__cleanup_mad_send( h_mad_svc, h_send, 3 );
	}
	AL_EXIT( AL_DBG_MAD_SVC );
}



/*
 * Return TRUE if a received MAD requires RMPP processing.
 */
static __inline boolean_t
__recv_requires_rmpp(
	IN		const	ib_mad_svc_type_t			mad_svc_type,
	IN		const	ib_mad_element_t* const		p_mad_element )
{
	ib_rmpp_mad_t				*p_rmpp_mad;

	AL_ENTER( AL_DBG_MAD_SVC );

	p_rmpp_mad = (ib_rmpp_mad_t*)ib_get_mad_buf( p_mad_element );

	AL_EXIT( AL_DBG_MAD_SVC );

	switch( mad_svc_type )
	{
	case IB_MAD_SVC_DEFAULT:
		return (p_rmpp_mad->common_hdr.mgmt_class == IB_MCLASS_SUBN_ADM ||
				ib_class_is_vendor_specific_high(p_rmpp_mad->common_hdr.mgmt_class)) &&
			   ib_rmpp_is_flag_set(p_rmpp_mad, IB_RMPP_FLAG_ACTIVE);

	case IB_MAD_SVC_RMPP:
		return( ib_rmpp_is_flag_set( p_rmpp_mad, IB_RMPP_FLAG_ACTIVE ) );

	default:
		return FALSE;
	}
}



/*
 * Return TRUE if the MAD was issued by AL itself.
 */
static __inline boolean_t
__is_internal_send(
	IN		const	ib_mad_svc_type_t			mad_svc_type,
	IN		const	ib_mad_element_t* const		p_mad_element )
{
	ib_rmpp_mad_t		*p_rmpp_mad;

	p_rmpp_mad = (ib_rmpp_mad_t*)ib_get_mad_buf( p_mad_element );

	/* See if the MAD service issues internal MADs. */
	switch( mad_svc_type )
	{
	case IB_MAD_SVC_DEFAULT:
		/* Internal sends are non-RMPP data MADs. */
		return ((p_rmpp_mad->common_hdr.mgmt_class == IB_MCLASS_SUBN_ADM ||
				 ib_class_is_vendor_specific_high(p_rmpp_mad->common_hdr.mgmt_class)) &&
				 (p_rmpp_mad->rmpp_type && p_rmpp_mad->rmpp_type != IB_RMPP_TYPE_DATA));

	case IB_MAD_SVC_RMPP:
		/* The RMPP header is present.  Check its type. */
		return( (p_rmpp_mad->rmpp_type) &&
				(p_rmpp_mad->rmpp_type != IB_RMPP_TYPE_DATA) );

	default:
		return FALSE;
	}
}



/*
 * Fully reassemble a received MAD.  Return TRUE once all segments of the
 * MAD have been received.  Return the fully reassembled MAD.
 */
static cl_status_t
__do_rmpp_recv(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN	OUT			ib_mad_element_t			**pp_mad_element )
{
	ib_rmpp_mad_t		*p_rmpp_mad;
	cl_status_t			cl_status;

	AL_ENTER( AL_DBG_MAD_SVC );

	p_rmpp_mad = ib_get_mad_buf( *pp_mad_element );
	CL_ASSERT( ib_rmpp_is_flag_set( p_rmpp_mad, IB_RMPP_FLAG_ACTIVE ) );

	/* Perform the correct operation base on the RMPP MAD type. */
	switch( p_rmpp_mad->rmpp_type )
	{
	case IB_RMPP_TYPE_DATA:
		cl_status = __process_rmpp_data( h_mad_svc, pp_mad_element );
		/* Return the received element back to its MAD pool if not needed. */
		if( (cl_status != CL_SUCCESS) && (cl_status != CL_NOT_DONE) )
		{
			ib_put_mad( *pp_mad_element );
		}
		break;

	case IB_RMPP_TYPE_ACK:
		/* Process the ACK. */
		__process_rmpp_ack( h_mad_svc, *pp_mad_element );
		ib_put_mad( *pp_mad_element );
		cl_status = CL_COMPLETED;
		break;

	case IB_RMPP_TYPE_STOP:
	case IB_RMPP_TYPE_ABORT:
	default:
		/* Process the ABORT or STOP. */
		__process_rmpp_nack( h_mad_svc, *pp_mad_element );
		ib_put_mad( *pp_mad_element );
		cl_status = CL_REJECT;
		break;
	}

	AL_EXIT( AL_DBG_MAD_SVC );
	return cl_status;
}



/*
 * Process an RMPP DATA message.  Reassemble the received data.  If the
 * received MAD is fully reassembled, this call returns CL_SUCCESS.
 */
static cl_status_t
__process_rmpp_data(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN	OUT			ib_mad_element_t			**pp_mad_element )
{
	ib_mad_element_t	*p_rmpp_resp_mad = NULL;
	al_mad_rmpp_t		*p_rmpp;
	ib_rmpp_mad_t		*p_rmpp_hdr;
	uint32_t			cur_seg;
	cl_status_t			cl_status;
	ib_api_status_t		status;

	p_rmpp_hdr = ib_get_mad_buf( *pp_mad_element );
	CL_ASSERT( p_rmpp_hdr->rmpp_type == IB_RMPP_TYPE_DATA );

	/* Try to find a receive already being reassembled. */
	cl_spinlock_acquire( &h_mad_svc->obj.lock );
	p_rmpp = __find_rmpp( h_mad_svc, *pp_mad_element );
	if( !p_rmpp )
	{
		/* This receive is not being reassembled. It should be the first seg. */
		if( cl_ntoh32( p_rmpp_hdr->seg_num ) != 1 )
		{
			cl_spinlock_release( &h_mad_svc->obj.lock );
			return CL_NOT_FOUND;
		}

		/* Start tracking the new reassembly. */
		p_rmpp = __get_mad_rmpp( h_mad_svc, *pp_mad_element );
		if( !p_rmpp )
		{
			cl_spinlock_release( &h_mad_svc->obj.lock );
			return CL_INSUFFICIENT_MEMORY;
		}
	}

	/* Verify that we just received the expected segment. */
	cur_seg = cl_ntoh32( p_rmpp_hdr->seg_num );
	if( cur_seg == p_rmpp->expected_seg )
	{
		/* Copy the new segment's data into our reassembly buffer. */
		cl_status = __process_segment( h_mad_svc, p_rmpp,
			pp_mad_element, &p_rmpp_resp_mad );

		/* See if the RMPP is done. */
		if( cl_status == CL_SUCCESS )
		{
			/* Stop tracking the reassembly. */
			__put_mad_rmpp( h_mad_svc, p_rmpp );
		}
		else if( cl_status == CL_NOT_DONE )
		{
			/* Start the reassembly timer. */
			cl_timer_trim( &h_mad_svc->recv_timer, AL_REASSEMBLY_TIMEOUT );
		}
	}
	else if( cur_seg < p_rmpp->expected_seg )
	{
		/* We received an old segment.  Resend the last ACK. */
		p_rmpp_resp_mad = __get_rmpp_ack( p_rmpp );
		cl_status = CL_DUPLICATE;
	}
	else
	{
		/* The sender is confused, ignore this MAD.  We could ABORT here. */
		cl_status = CL_OVERRUN;
	}

	cl_spinlock_release( &h_mad_svc->obj.lock );

	/*
	 * Send any response MAD (ACK, ABORT, etc.) to the sender.  Note that
	 * we are currently in the callback from the MAD dispatcher.  The
	 * dispatcher holds a reference on the MAD service while in the callback,
	 * preventing the MAD service from being destroyed.  This allows the
	 * call to ib_send_mad() to proceed even if the user tries to destroy
	 * the MAD service.
	 */
	if( p_rmpp_resp_mad )
	{
		status = ib_send_mad( h_mad_svc, p_rmpp_resp_mad, NULL );
		if( status != IB_SUCCESS )
		{
			/* Return the MAD.  The MAD is considered dropped. */
			ib_put_mad( p_rmpp_resp_mad );
		}
	}

	return cl_status;
}



/*
 * Locate an existing RMPP MAD being reassembled.  Return NULL if one is not
 * found.  This call assumes access to the recv_list is synchronized.
 */
static al_mad_rmpp_t*
__find_rmpp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN	OUT			ib_mad_element_t			*p_mad_element )
{
	al_mad_rmpp_t			*p_rmpp;
	cl_list_item_t			*p_list_item;
	ib_mad_t				*p_mad_hdr, *p_mad_hdr2;
	ib_mad_element_t		*p_mad_element2;


	p_mad_hdr = ib_get_mad_buf( p_mad_element );

	/* Search all MADs being reassembled. */
	for( p_list_item = cl_qlist_head( &h_mad_svc->recv_list );
		 p_list_item != cl_qlist_end( &h_mad_svc->recv_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_rmpp = PARENT_STRUCT( p_list_item, al_mad_rmpp_t, pool_item );

		p_mad_element2 = p_rmpp->p_mad_element;
		p_mad_hdr2 = ib_get_mad_buf( p_mad_element2 );

		/* See if the incoming MAD matches - what a check. */
		if( (p_mad_hdr->trans_id		== p_mad_hdr2->trans_id)		&&
			(p_mad_hdr->class_ver		== p_mad_hdr2->class_ver)		&&
			(p_mad_hdr->mgmt_class		== p_mad_hdr2->mgmt_class)		&&
			(p_mad_hdr->method			== p_mad_hdr2->method)			&&
			(p_mad_element->remote_lid	== p_mad_element2->remote_lid)	&&
			(p_mad_element->remote_qp	== p_mad_element2->remote_qp) )
		{
			return p_rmpp;
		}
	}

	return NULL;
}



/*
 * Acquire a new RMPP tracking structure.  This call assumes access to
 * the recv_list is synchronized.
 */
static al_mad_rmpp_t*
__get_mad_rmpp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element )
{
	al_mad_rmpp_t		*p_rmpp;
	al_mad_element_t	*p_al_element;

	p_al_element = PARENT_STRUCT( p_mad_element, al_mad_element_t, element );

	/* Get an RMPP tracking structure. */
	p_rmpp = get_mad_rmpp( p_al_element );
	if( !p_rmpp )
		return NULL;

	/* Initialize the tracking information. */
	p_rmpp->expected_seg = 1;
	p_rmpp->seg_limit = 1;
	p_rmpp->inactive = FALSE;
	p_rmpp->p_mad_element = p_mad_element;

	/* Insert the tracking structure into the reassembly list. */
	cl_qlist_insert_tail( &h_mad_svc->recv_list,
		(cl_list_item_t*)&p_rmpp->pool_item );

	return p_rmpp;
}



/*
 * Return the RMPP tracking structure.  This call assumes access to
 * the recv_list is synchronized.
 */
static void
__put_mad_rmpp(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				al_mad_rmpp_t				*p_rmpp )
{
	/* Remove the tracking structure from the reassembly list. */
	cl_qlist_remove_item( &h_mad_svc->recv_list,
		(cl_list_item_t*)&p_rmpp->pool_item );

	/* Return the RMPP tracking structure. */
	put_mad_rmpp( p_rmpp );
}



/*
 * Process a received RMPP segment.  Copy the data into our receive buffer,
 * update the expected segment, and send an ACK if needed.
 */
static cl_status_t
__process_segment(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				al_mad_rmpp_t				*p_rmpp,
	IN	OUT			ib_mad_element_t			**pp_mad_element,
		OUT			ib_mad_element_t			**pp_rmpp_resp_mad )
{
	ib_rmpp_mad_t			*p_rmpp_hdr;
	uint32_t				cur_seg;
	ib_api_status_t			status;
	cl_status_t				cl_status;
	uint8_t					*p_dst_seg, *p_src_seg;
	uint32_t				paylen;

	CL_ASSERT( h_mad_svc && p_rmpp && pp_mad_element && *pp_mad_element );

	p_rmpp_hdr = (ib_rmpp_mad_t*)(*pp_mad_element)->p_mad_buf;
	cur_seg = cl_ntoh32( p_rmpp_hdr->seg_num );
	CL_ASSERT( cur_seg == p_rmpp->expected_seg );
	CL_ASSERT( cur_seg <= p_rmpp->seg_limit );

	/* See if the receive has been fully reassembled. */
	if( ib_rmpp_is_flag_set( p_rmpp_hdr, IB_RMPP_FLAG_LAST ) )
		cl_status = CL_SUCCESS;
	else
		cl_status = CL_NOT_DONE;
	
	/* Save the payload length for later use. */
	paylen = cl_ntoh32(p_rmpp_hdr->paylen_newwin);

	/* The element of the first segment starts the reasembly. */
	if( *pp_mad_element != p_rmpp->p_mad_element )
	{
		/* SA MADs require extra header size ... */
		if( (*pp_mad_element)->p_mad_buf->mgmt_class == IB_MCLASS_SUBN_ADM )
		{
			/* Copy the received data into our reassembly buffer. */
			p_src_seg = ((uint8_t*)(*pp_mad_element)->p_mad_buf) +
				IB_SA_MAD_HDR_SIZE;
			p_dst_seg = ((uint8_t*)p_rmpp->p_mad_element->p_mad_buf) +
				IB_SA_MAD_HDR_SIZE + IB_SA_DATA_SIZE * (cur_seg - 1);
			cl_memcpy( p_dst_seg, p_src_seg, IB_SA_DATA_SIZE );
		}
		else 
		{
			/* Copy the received data into our reassembly buffer. */
			p_src_seg = ((uint8_t*)(*pp_mad_element)->p_mad_buf) +
				MAD_RMPP_HDR_SIZE;
			p_dst_seg = ((uint8_t*)p_rmpp->p_mad_element->p_mad_buf) +
				MAD_RMPP_HDR_SIZE + MAD_RMPP_DATA_SIZE * (cur_seg - 1);
			cl_memcpy( p_dst_seg, p_src_seg, MAD_RMPP_DATA_SIZE );
		}
		/* This MAD is no longer needed. */
		ib_put_mad( *pp_mad_element );
	}

	/* Update the size of the mad if the last segment */
	if ( cl_status == CL_SUCCESS )
	{
		if (p_rmpp->p_mad_element->p_mad_buf->mgmt_class == IB_MCLASS_SUBN_ADM )
		{
			/*
			 * Note we will get one extra SA Hdr size in the paylen, 
			 * so we only take the rmpp header size of the first segment.
			 */
			p_rmpp->p_mad_element->size = 
				MAD_RMPP_HDR_SIZE + IB_SA_DATA_SIZE *(cur_seg - 1) + paylen;
		}
		else
		{
			 p_rmpp->p_mad_element->size = 
				MAD_RMPP_HDR_SIZE + MAD_RMPP_DATA_SIZE * (cur_seg - 1) + paylen;
		}
	}

	/*
	 * We are ready to accept the next segment.  We increment expected segment
	 * even if we're done, so that ACKs correctly report the last segment.
	 */
	p_rmpp->expected_seg++;

	/* Mark the RMPP as active if we're not destroying the MAD service. */
	p_rmpp->inactive = (h_mad_svc->obj.state == CL_DESTROYING);

	/* See if the receive has been fully reassembled. */
	if( cl_status == CL_NOT_DONE && cur_seg == p_rmpp->seg_limit )
	{
		/* Allocate more segments for the incoming receive. */
		status = al_resize_mad( p_rmpp->p_mad_element,
			p_rmpp->p_mad_element->size + AL_RMPP_WINDOW * MAD_RMPP_DATA_SIZE );

		/* If we couldn't allocate a new buffer, just drop the MAD. */
		if( status == IB_SUCCESS )
		{
			/* Send an ACK indicating that more space is available. */
			p_rmpp->seg_limit += AL_RMPP_WINDOW;
			*pp_rmpp_resp_mad = __get_rmpp_ack( p_rmpp );
		}
	}
	else if( cl_status == CL_SUCCESS )
	{
		/* Return the element referencing the reassembled MAD. */
		*pp_mad_element = p_rmpp->p_mad_element;
		*pp_rmpp_resp_mad = __get_rmpp_ack( p_rmpp );
	}

	return cl_status;
}



/*
 * Get an ACK message to return to the sender of an RMPP MAD.
 */
static ib_mad_element_t*
__get_rmpp_ack(
	IN				al_mad_rmpp_t				*p_rmpp )
{
	ib_mad_element_t		*p_mad_element;
	al_mad_element_t		*p_al_element;
	ib_api_status_t			status;
	ib_rmpp_mad_t			*p_ack_rmpp_hdr, *p_data_rmpp_hdr;

	/* Get a MAD to carry the ACK. */
	p_al_element = PARENT_STRUCT( p_rmpp->p_mad_element,
		al_mad_element_t, element );
	status = ib_get_mad( p_al_element->pool_key, MAD_BLOCK_SIZE,
		&p_mad_element );
	if( status != IB_SUCCESS )
	{
		/* Just return.  The ACK will be treated as being dropped. */
		return NULL;
	}

	/* Format the ACK. */
	p_ack_rmpp_hdr = ib_get_mad_buf( p_mad_element );
	p_data_rmpp_hdr = ib_get_mad_buf( p_rmpp->p_mad_element );

	__init_reply_element( p_mad_element, p_rmpp->p_mad_element );

	/* Copy the MAD common header. */
	cl_memcpy( &p_ack_rmpp_hdr->common_hdr, &p_data_rmpp_hdr->common_hdr,
		sizeof( ib_mad_t ) );

	/* Reset the status (in case the BUSY bit is set). */
	p_ack_rmpp_hdr->common_hdr.status = 0;

	/* Flip the response bit in the method */
	p_ack_rmpp_hdr->common_hdr.method ^= IB_MAD_METHOD_RESP_MASK;

	p_ack_rmpp_hdr->rmpp_version = p_data_rmpp_hdr->rmpp_version;
	p_ack_rmpp_hdr->rmpp_type = IB_RMPP_TYPE_ACK;
	ib_rmpp_set_resp_time( p_ack_rmpp_hdr, IB_RMPP_NO_RESP_TIME );
	p_ack_rmpp_hdr->rmpp_flags |= IB_RMPP_FLAG_ACTIVE;
	p_ack_rmpp_hdr->rmpp_status = IB_RMPP_STATUS_SUCCESS;

	p_ack_rmpp_hdr->seg_num = cl_hton32( p_rmpp->expected_seg - 1 );

	if (p_rmpp->seg_limit == p_rmpp->expected_seg - 1 &&
		!ib_rmpp_is_flag_set( p_data_rmpp_hdr, IB_RMPP_FLAG_LAST ) )
	{
		p_ack_rmpp_hdr->paylen_newwin = cl_hton32( 1 + p_rmpp->seg_limit);
	}
	else
	{
		p_ack_rmpp_hdr->paylen_newwin = cl_hton32( p_rmpp->seg_limit );
	}

	return p_mad_element;
}



/*
 * Copy necessary data between MAD elements to allow the destination
 * element to be sent to the sender of the source element.
 */
static void
__init_reply_element(
	IN				ib_mad_element_t			*p_dst_element,
	IN				ib_mad_element_t			*p_src_element )
{
	p_dst_element->remote_qp = p_src_element->remote_qp;
	p_dst_element->remote_qkey = p_src_element->remote_qkey;

	if( p_src_element->grh_valid )
	{
		p_dst_element->grh_valid = p_src_element->grh_valid;
		cl_memcpy( p_dst_element->p_grh, p_src_element->p_grh,
			sizeof( ib_grh_t ) );
	}

	p_dst_element->remote_lid = p_src_element->remote_lid;
	p_dst_element->remote_sl = p_src_element->remote_sl;
	p_dst_element->pkey_index = p_src_element->pkey_index;
	p_dst_element->path_bits = p_src_element->path_bits;
}



/*
 * Process an RMPP ACK message.  Continue sending addition segments.
 */
static void
__process_rmpp_ack(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element )
{
	ib_mad_send_handle_t	h_send;
	ib_rmpp_mad_t			*p_rmpp_mad;
	boolean_t				send_done = FALSE;
	ib_wc_status_t			wc_status = IB_WCS_SUCCESS;

	AL_ENTER( AL_DBG_MAD_SVC );
	p_rmpp_mad = (ib_rmpp_mad_t*)ib_get_mad_buf( p_mad_element );

	/*
	 * Search for the send.  The send may have timed out, been canceled,
	 * or received a response.
	 */
	cl_spinlock_acquire( &h_mad_svc->obj.lock );
	h_send = __mad_svc_match_recv( h_mad_svc, p_mad_element );
	if( !h_send )
	{
		cl_spinlock_release( &h_mad_svc->obj.lock );
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
			("ACK cannot find a matching send\n") );
		return;
	}

	/* Drop old ACKs. */
	if( cl_ntoh32( p_rmpp_mad->seg_num ) < h_send->ack_seg )
	{
		cl_spinlock_release( &h_mad_svc->obj.lock );
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
			("old ACK - being dropped\n") );
		return;
	}

	/* Update the acknowledged segment and segment limit. */
	h_send->ack_seg = cl_ntoh32( p_rmpp_mad->seg_num );

	/* Keep seg_limit <= total_seg to simplify checks. */
	if( cl_ntoh32( p_rmpp_mad->paylen_newwin ) > h_send->total_seg )
		h_send->seg_limit = h_send->total_seg;
	else
		h_send->seg_limit = cl_ntoh32( p_rmpp_mad->paylen_newwin );

	/* Reset the current segment to start resending from the ACK. */
	h_send->cur_seg = h_send->ack_seg + 1;

	/* If the send is active, we will finish processing it once it completes. */
	if( h_send->retry_time == MAX_TIME )
	{
		cl_spinlock_release( &h_mad_svc->obj.lock );
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
			("ACK processed, waiting for send to complete\n") );
		return;
	}

	/*
	 * Complete the send if all segments have been ack'ed and no
	 * response is expected.  (If the response for a send had already been
	 * received, we would have reported the completion regardless of the
	 * send having been ack'ed.)
	 */
	CL_ASSERT( !h_send->p_send_mad->resp_expected || !h_send->p_resp_mad );
	if( (h_send->ack_seg == h_send->total_seg) &&
		!h_send->p_send_mad->resp_expected )
	{
		/* The send is done.  All segments have been ack'ed. */
		send_done = TRUE;
	}
	else if( h_send->ack_seg < h_send->seg_limit )
	{
		/* Send the next segment. */
		__queue_rmpp_seg( h_mad_svc->h_mad_reg, h_send );
	}

	if( send_done )
	{
		/* Notify the user of a send completion or error. */
		cl_qlist_remove_item( &h_mad_svc->send_list,
			(cl_list_item_t*)&h_send->pool_item );
		cl_spinlock_release( &h_mad_svc->obj.lock );
		__notify_send_comp( h_mad_svc, h_send, wc_status );
	}
	else
	{
		/* Continue waiting for a response or a larger send window. */
		cl_spinlock_release( &h_mad_svc->obj.lock );
	}

	/*
	 * Resume any sends that can now be sent without holding
	 * the mad service lock.
	 */
	__mad_disp_resume_send( h_mad_svc->h_mad_reg );

	AL_EXIT( AL_DBG_MAD_SVC );
}



/*
 * Process an RMPP STOP or ABORT message.
 */
static void
__process_rmpp_nack(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t			*p_mad_element )
{
	ib_mad_send_handle_t	h_send;
	ib_rmpp_mad_t			*p_rmpp_mad;

	AL_ENTER( AL_DBG_MAD_SVC );
	p_rmpp_mad = (ib_rmpp_mad_t*)ib_get_mad_buf( p_mad_element );

	/* Search for the send.  The send may have timed out or been canceled. */
	cl_spinlock_acquire( &h_mad_svc->obj.lock );
	h_send = __mad_svc_match_recv( h_mad_svc, p_mad_element );
	if( !h_send )
	{
		cl_spinlock_release( &h_mad_svc->obj.lock );
		return;
	}

	/* If the send is active, we will finish processing it once it completes. */
	if( h_send->retry_time == MAX_TIME )
	{
		h_send->canceled = TRUE;
		cl_spinlock_release( &h_mad_svc->obj.lock );
		AL_EXIT( AL_DBG_MAD_SVC );
		return;
	}

	/* Fail the send operation. */
	cl_qlist_remove_item( &h_mad_svc->send_list,
		(cl_list_item_t*)&h_send->pool_item );
	cl_spinlock_release( &h_mad_svc->obj.lock );
	__notify_send_comp( h_mad_svc, h_send, IB_WCS_CANCELED );

	AL_EXIT( AL_DBG_MAD_SVC );
}



static __inline int
__set_retry_time(
	IN				ib_mad_send_handle_t		h_send,
    IN              ULONG                       send_jitter )
{
    int timeout = (int)h_send->p_send_mad->timeout_ms;

    //
    // Negative values indicate recursive doubling.
    //
    if( timeout < 0 )
    {
        int max;
        timeout = -timeout;
        max = timeout >> 16;
        timeout &= 0xFFFFUL;

        if( max == 0 )
        {
            max = SHRT_MAX;
        }

        if( (timeout * 2) <= max )
        {
            //
            // Double the timeout for the next iteration.
            //
            h_send->p_send_mad->timeout_ms = (ULONG)-((max << 16) | (timeout * 2));
        }
        else
        {
            h_send->p_send_mad->timeout_ms = (ULONG)-((max << 16) | max);
        }
    }

    //
    // Add some jitter, random number between 0 and 1/2 timeout.
    // Note that this is in microseconds and not milliseconds.
    //
    timeout += (send_jitter % timeout) / 2;
    timeout += h_send->delay;

	h_send->retry_time = (uint64_t)(timeout) * 1000Ui64 + cl_get_time_stamp();
	h_send->delay = 0;
    return timeout;
}



static void
__send_timer_cb(
	IN				void						*context )
{
	AL_ENTER( AL_DBG_MAD_SVC );

	__check_send_queue( (ib_mad_svc_handle_t)context );

	AL_EXIT( AL_DBG_MAD_SVC );
}



/*
 * Check the send queue for any sends that have timed out or were canceled
 * by the user.
 */
static void
__check_send_queue(
	IN				ib_mad_svc_handle_t			h_mad_svc )
{
	ib_mad_send_handle_t	h_send;
	cl_list_item_t			*p_list_item, *p_next_item;
	uint64_t				cur_time;
	cl_qlist_t				timeout_list;

	AL_ENTER( AL_DBG_MAD_SVC );

	/*
	 * The timeout out list is used to call the user back without
	 * holding the lock on the MAD service.
	 */
	cl_qlist_init( &timeout_list );
	cur_time = cl_get_time_stamp();

	cl_spinlock_acquire( &h_mad_svc->obj.lock );

	/* Check all outstanding sends. */
	for( p_list_item = cl_qlist_head( &h_mad_svc->send_list );
		 p_list_item != cl_qlist_end( &h_mad_svc->send_list );
		 p_list_item = p_next_item )
	{
		p_next_item = cl_qlist_next( p_list_item );
		h_send = PARENT_STRUCT( p_list_item, al_mad_send_t, pool_item );

		/* See if the request is active. */
		if( h_send->retry_time == MAX_TIME )
		{
			/* The request is still active. */
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("active TID:0x%I64x\n",
				__get_send_tid( h_send )) );
			continue;
		}

		/* The request is not active. */
		/* See if the request has been canceled. */
		if( h_send->canceled )
		{
			/* The request has been canceled. */
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("canceling TID:0x%I64x\n",
				__get_send_tid( h_send )) );

			h_send->p_send_mad->status = IB_WCS_CANCELED;
			cl_qlist_remove_item( &h_mad_svc->send_list, p_list_item );
			cl_qlist_insert_tail( &timeout_list, p_list_item );
			continue;
		}

		/* Skip requests that have not timed out. */
		if( cur_time < h_send->retry_time )
		{
			/* The request has not timed out. */
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("waiting on TID:0x%I64x\n",
				__get_send_tid( h_send )) );

			/* Set the retry timer to the minimum needed time, in ms. */
			cl_timer_trim( &h_mad_svc->send_timer,
				((uint32_t)(h_send->retry_time - cur_time) / 1000) );
			continue;
		}

		/* See if we need to retry the send operation. */
		if( h_send->retry_cnt )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MAD_SVC, ("retrying TID:0x%I64x\n",
				__get_send_tid( h_send )) );

			/* Retry the send. */
			h_send->retry_time = MAX_TIME;
			h_send->retry_cnt--;

			if( h_send->uses_rmpp )
			{
				if( h_send->ack_seg < h_send->seg_limit )
				{
					/* Resend all unacknowledged segments. */
					h_send->cur_seg = h_send->ack_seg + 1;
					__queue_rmpp_seg( h_mad_svc->h_mad_reg, h_send );
				}
				else
				{
					/* The send was delivered.  Continue waiting. */
					cl_timer_trim( &h_mad_svc->send_timer,
						__set_retry_time( h_send, h_mad_svc->send_jitter ) );
				}
			}
			else
			{
				/* The work request should already be formatted properly. */
				__mad_disp_queue_send( h_mad_svc->h_mad_reg,
					&h_send->mad_wr );
			}
			continue;
		}
		/* The request has timed out or failed to be retried. */
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_MAD_SVC,
			("timing out TID:0x%I64x\n", __get_send_tid( h_send )) );

		h_send->p_send_mad->status = IB_WCS_TIMEOUT_RETRY_ERR;
		cl_qlist_remove_item( &h_mad_svc->send_list, p_list_item );
		cl_qlist_insert_tail( &timeout_list, p_list_item );
	}

	cl_spinlock_release( &h_mad_svc->obj.lock );

	/*
	 * Resume any sends that can now be sent without holding
	 * the mad service lock.
	 */
	__mad_disp_resume_send( h_mad_svc->h_mad_reg );

	/* Report all timed out sends to the user. */
	p_list_item = cl_qlist_remove_head( &timeout_list );
	while( p_list_item != cl_qlist_end( &timeout_list ) )
	{
		h_send = PARENT_STRUCT( p_list_item, al_mad_send_t, pool_item );

		h_mad_svc->pfn_user_send_cb( h_mad_svc, (void*)h_mad_svc->obj.context,
			h_send->p_send_mad );
		__cleanup_mad_send( h_mad_svc, h_send, 4 );
		p_list_item = cl_qlist_remove_head( &timeout_list );
	}
	AL_EXIT( AL_DBG_MAD_SVC );
}



static void
__recv_timer_cb(
	IN				void						*context )
{
	ib_mad_svc_handle_t		h_mad_svc;
	al_mad_rmpp_t			*p_rmpp;
	cl_list_item_t			*p_list_item, *p_next_item;
	boolean_t				restart_timer;

	AL_ENTER( AL_DBG_MAD_SVC );

	h_mad_svc = (ib_mad_svc_handle_t)context;

	cl_spinlock_acquire( &h_mad_svc->obj.lock );

	/* Check all outstanding receives. */
	for( p_list_item = cl_qlist_head( &h_mad_svc->recv_list );
		 p_list_item != cl_qlist_end( &h_mad_svc->recv_list );
		 p_list_item = p_next_item )
	{
		p_next_item = cl_qlist_next( p_list_item );
		p_rmpp = PARENT_STRUCT( p_list_item, al_mad_rmpp_t, pool_item );

		/* Fail all RMPP MADs that have remained inactive. */
		if( p_rmpp->inactive )
		{
			ib_put_mad( p_rmpp->p_mad_element );
			__put_mad_rmpp( h_mad_svc, p_rmpp );
		}
		else
		{
			/* Mark the RMPP as inactive. */
			 p_rmpp->inactive = TRUE;
		}
	}

	restart_timer = !cl_is_qlist_empty( &h_mad_svc->recv_list );
	cl_spinlock_release( &h_mad_svc->obj.lock );

	if( restart_timer )
		cl_timer_start( &h_mad_svc->recv_timer, AL_REASSEMBLY_TIMEOUT );
	AL_EXIT( AL_DBG_MAD_SVC );
}



ib_api_status_t
ib_local_mad(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	uint8_t						port_num,
	IN		const	void* const					p_mad_in,
	IN				void*						p_mad_out )
{
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MAD_SVC );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}
	if( !p_mad_in || !p_mad_out )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = al_local_mad(h_ca, port_num, NULL,p_mad_in, p_mad_out);

	AL_EXIT( AL_DBG_MAD_SVC );
	return status;
}

ib_api_status_t
al_local_mad(	
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	uint8_t						port_num,
	IN		const	ib_av_attr_t*					p_src_av_attr,
	IN		const	void* const					p_mad_in,
	IN				void*						p_mad_out )
{
	ib_api_status_t			status;
	void*					p_mad_out_local = NULL;
	AL_ENTER( AL_DBG_MAD_SVC );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}
	if( !p_mad_in )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}
	if( !p_mad_out )
	{
		p_mad_out_local = cl_zalloc(256);
		if(!p_mad_out_local)
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INSUFFICIENT_MEMORY\n") );
			return IB_INSUFFICIENT_MEMORY;
		}
	}else
	{
		p_mad_out_local = p_mad_out;
	}
		
	status = verbs_local_mad( h_ca, port_num, p_src_av_attr, p_mad_in, p_mad_out_local );
	
	if( !p_mad_out )
	{
		cl_free(p_mad_out_local);
	}
	
	AL_EXIT( AL_DBG_MAD_SVC );
	return status;
	
}

ib_net32_t
al_get_user_tid(
	IN		const	ib_net64_t						tid64 )
{
	al_tid_t		al_tid;

	al_tid.tid64 = tid64;
	return( al_tid.tid32.user_tid );
}

uint32_t
al_get_al_tid(
	IN		const	ib_net64_t						tid64 )
{
	al_tid_t		al_tid;

	al_tid.tid64 = tid64;
	return( cl_ntoh32( al_tid.tid32.al_tid ) );
}

void
al_set_al_tid(
	IN				ib_net64_t*		const			p_tid64,
	IN		const	uint32_t						tid32 )
{
	al_tid_t		*p_al_tid;

	p_al_tid = (al_tid_t*)p_tid64;

	if( tid32 )
	{
		CL_ASSERT( !p_al_tid->tid32.al_tid );
	}

	p_al_tid->tid32.al_tid = cl_hton32( tid32 );
}
