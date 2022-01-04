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

#if !defined( __AL_MAD_POOL_H__ )
#define __AL_MAD_POOL_H__

#include <complib/cl_qlist.h>
#include <iba/ib_al.h>
#include "al_common.h"


typedef struct _al_pool
{
	al_obj_t				obj;			/* Child of ib_al_handle_t */
#if defined( CL_KERNEL )
	NPAGED_LOOKASIDE_LIST	mad_stack;
#else
	cl_qlist_t				mad_stack;
#endif
	cl_qlist_t				key_list;
	size_t					max;
	size_t					actual;
	size_t					grow_size;
#if defined( CL_KERNEL )
	NPAGED_LOOKASIDE_LIST	mad_send_pool;
	NPAGED_LOOKASIDE_LIST	mad_rmpp_pool;
#else
	cl_qpool_t				mad_send_pool;
	cl_qpool_t				mad_rmpp_pool;
#endif

}	al_pool_t;



/*
 * Pool key type used to distinguish between pool_keys allocated by the user
 * and those that reference AL's internal MAD pool_key.
 */
typedef enum _al_key_type
{
	AL_KEY_NORMAL,
	AL_KEY_ALIAS

}	al_key_type_t;



typedef struct _al_pool_key
{
	al_obj_t				obj;			/* Not a child object. */
											/* Parent of mad_reg_t */
	al_key_type_t			type;

	/*
	 * Pool keys can be destroyed either by deregistering them, destroying
	 * the associated pool, or destroying the associated PD.  We track the
	 * pool key with the AL instance in order to synchronize destruction.
	 */
	boolean_t				in_al_list;
	ib_al_handle_t			h_al;
	cl_list_item_t			al_item;		/* Chain in ib_al_t for dereg */
	cl_list_item_t			pool_item;		/* Chain in al_pool_t for grow */

	ib_pool_handle_t		h_pool;
#ifndef CL_KERNEL
	ib_pd_handle_t			h_pd;
#else
	ib_mr_handle_t			h_mr;
	net32_t					lkey;
#endif

	/* Number of MADs currently removed from pool using this key. */
	atomic32_t				mad_cnt;

	/* For alias keys, maintain a reference to the actual pool key. */
	ib_pool_key_t			pool_key;

}	al_pool_key_t;


ib_api_status_t
reg_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
	IN		const	ib_pd_handle_t				h_pd,
		OUT			ib_pool_key_t* const		pp_pool_key );

/* Deregister a MAD pool key if it is of the expected type. */
ib_api_status_t
dereg_mad_pool(
	IN		const	ib_pool_key_t				pool_key,
	IN		const	al_key_type_t				expected_type );

typedef struct _al_mad_element
{
	/*
	 * List item used to track free MADs by the MAD pool.  Also used by
	 * the SMI and MAD QPs to track received MADs.
	 */
	cl_list_item_t			list_item;
	ib_al_handle_t			h_al;			/* Track out-of-pool MAD owner */
	cl_list_item_t			al_item;		/* Out-of-pool MAD owner chain */
	ib_mad_element_t		element;
	ib_pool_key_t			pool_key;		/* For getting mads for RMPP ACK */
	ib_local_ds_t			grh_ds;			/* GRH + 256 byte buffer. */
	ib_local_ds_t			mad_ds;			/* Registered 256-byte buffer. */
	ib_mad_t				*p_al_mad_buf;	/* Allocated send/recv buffer. */
#if defined( CL_KERNEL )
	uint8_t					mad_buf[MAD_BLOCK_GRH_SIZE];
#endif

	uint64_t				h_proxy_element; /* For user-mode support */

}	al_mad_element_t;



ib_api_status_t
al_resize_mad(
		OUT			ib_mad_element_t			*p_mad_element,
	IN		const	size_t						buf_size );



/* We don't have MAD array structures in the Windows kernel. */
#if !defined( CL_KERNEL )
typedef struct _mad_array
{
	al_obj_t				obj;			/* Child of al_pool_t */
	ib_pool_handle_t		h_pool;
	size_t					sizeof_array;
	void*					p_data;

}	mad_array_t;
#endif


typedef struct _mad_item
{
	al_mad_element_t		al_mad_element;
#if defined( CL_KERNEL )
	ib_pool_key_t			pool_key;
#else
	mad_array_t*			p_mad_array;
#endif

}	mad_item_t;



/*
 * Work request structure to use when posting sends.
 */
typedef struct _al_mad_wr
{
	cl_list_item_t		list_item;

	uint32_t			client_id;
	ib_net64_t			client_tid;

	/*
	 * Work request used when sending MADs.  This permits formatting the
	 * work request once and re-using it when issuing retries.
	 */
	ib_send_wr_t		send_wr;

}	al_mad_wr_t;



/*
 * Structure used to track an outstanding send request.
 */
typedef struct _al_mad_send
{
	cl_pool_item_t				pool_item;
	ib_mad_element_t			*p_send_mad;
	ib_av_handle_t				h_av;

	al_mad_wr_t					mad_wr;

	/*
	 * Received MAD in response to a send.  This is not set until
	 * the entire response has been received.
	 */
	ib_mad_element_t			*p_resp_mad;

	/* Absolute time that the request should be retried. */
	uint64_t					retry_time;

	/* Delay, in milliseconds, to add before the next retry. */
	uint32_t					delay;

	/* Number of times that the request can be retried. */
	uint32_t					retry_cnt;
	boolean_t					canceled;	/* indicates if send was canceled */

	/*
	 * SAR tracking information.
	 */
	boolean_t					uses_rmpp;
	uint32_t					ack_seg;	/* last segment number ack'ed*/
	uint32_t					cur_seg;	/* current segment to send */
	uint32_t					seg_limit;	/* max. segment to send */
	uint32_t					total_seg;	/* total segments to send */

}	al_mad_send_t;



/*
 * Structure used to track receiving an RMPP MAD.
 */
typedef struct _al_mad_rmpp
{
	cl_pool_item_t				pool_item;

	boolean_t					inactive;	/* used to time out reassembly */

	uint32_t					expected_seg;/* next segment to receive */
	uint32_t					seg_limit;	 /* upper bound of recv window */
	ib_mad_element_t			*p_mad_element;/* reassembled recv */

}	al_mad_rmpp_t;



ib_mad_send_handle_t
get_mad_send(
	IN		const	al_mad_element_t			*p_mad_element );


void
put_mad_send(
	IN				ib_mad_send_handle_t		h_mad_send );


al_mad_rmpp_t*
get_mad_rmpp(
	IN		const	al_mad_element_t			*p_mad_element );


void
put_mad_rmpp(
	IN				al_mad_rmpp_t				*p_rmpp );


#endif	// __AL_MAD_POOL_H__
