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


#include "al.h"
#include "al_av.h"
#include "al_ci_ca.h"
#include "al_cq.h"
#include "al_pd.h"
#include "al_srq.h"
#include "ual_mad.h"
#include "ual_support.h"


#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_srq.tmh"
#endif


ib_api_status_t
ual_post_srq_recv(
	IN		const	ib_srq_handle_t				h_srq,
	IN				ib_recv_wr_t*	const		p_recv_wr,
		OUT			ib_recv_wr_t				**pp_recv_failure OPTIONAL )
{
	uintn_t					failed_index;
	uintn_t					bytes_ret;
	uint32_t				num_wr		= 0;
	uint32_t				num_ds		= 0;
	ib_recv_wr_t*			p_wr;
	ib_local_ds_t*			p_ds;
	ual_post_srq_recv_ioctl_t	*p_srq_ioctl;
	size_t					ioctl_buf_sz;
	cl_status_t				cl_status;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SRQ );

	/*
	 * Since the work request is a link list and we need to pass this
	 * to the kernel as a array of work requests.  So first walk through
	 * the list and find out how much memory we need to allocate.
	 */
	for( p_wr = p_recv_wr; p_wr; p_wr = p_wr->p_next )
	{
		num_wr++;

		/* Check for overflow */
		if( !num_wr )
			break;
		if( num_ds > num_ds + p_wr->num_ds )
		{
			num_wr = 0;
			break;
		}

		num_ds += p_wr->num_ds;
	}
	if( !num_wr )
	{
		AL_EXIT( AL_DBG_SRQ );
		return IB_INVALID_PARAMETER;
	}

	ioctl_buf_sz = sizeof(ual_post_recv_ioctl_t);
	ioctl_buf_sz += sizeof(ib_recv_wr_t) * (num_wr - 1);
	ioctl_buf_sz += sizeof(ib_local_ds_t) * num_ds;

	p_srq_ioctl = (ual_post_srq_recv_ioctl_t*)cl_zalloc( ioctl_buf_sz );
	if( !p_srq_ioctl )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("Failed to allocate IOCTL buffer.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}
	p_ds = (ib_local_ds_t*)&p_srq_ioctl->in.recv_wr[num_wr];

	/* Now populate the ioctl buffer and send down the ioctl */
	p_srq_ioctl->in.h_srq = h_srq->obj.hdl;
	p_srq_ioctl->in.num_wr = num_wr;
	p_srq_ioctl->in.num_ds = num_ds;
	num_wr = 0;
	for( p_wr = p_recv_wr; p_wr; p_wr = p_wr->p_next )
	{
		p_srq_ioctl->in.recv_wr[num_wr++] = *p_wr;
		cl_memcpy(
			p_ds, p_wr->ds_array, sizeof(ib_local_ds_t) * p_wr->num_ds );
		p_ds += p_wr->num_ds;
	}

	cl_status = do_al_dev_ioctl( UAL_POST_SRQ_RECV,
		&p_srq_ioctl->in, ioctl_buf_sz,
		&p_srq_ioctl->out, sizeof(p_srq_ioctl->out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(p_srq_ioctl->out) )
	{
		if( pp_recv_failure )
			*pp_recv_failure = p_recv_wr;

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_POST_SRQ_RECV IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = p_srq_ioctl->out.status;

		if( status != IB_SUCCESS && pp_recv_failure )
		{
			/* Get the failed index */
			failed_index = num_wr - p_srq_ioctl->out.failed_cnt;
			p_wr = p_recv_wr;
			while( failed_index-- )
				p_wr = p_wr->p_next;

			*pp_recv_failure = p_wr;
		}
	}

	cl_free( p_srq_ioctl );
	AL_EXIT( AL_DBG_SRQ );
	return status;
}



ib_api_status_t
ual_create_srq(
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_srq_handle_t				h_srq,
	IN		const	ib_srq_attr_t* const			p_srq_attr)
{
	/* The first argument is probably not needed */
	ual_create_srq_ioctl_t	srq_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_srq->obj.p_ci_ca->verbs.user_verbs;
	ib_srq_attr_t				srq_attr;

	AL_ENTER( AL_DBG_SRQ );

	/* Clear the srq_ioctl */
	cl_memclr( &srq_ioctl, sizeof(srq_ioctl) );

	/* Pre call to the UVP library */
	if( h_pd->h_ci_pd && uvp_intf.pre_create_srq )
	{
		/* The post call MUST exist as it sets the UVP srq handle. */
		CL_ASSERT( uvp_intf.post_create_srq );
		/* Convert the handles to UVP handles */
		srq_attr = *p_srq_attr;
		status = uvp_intf.pre_create_srq( h_pd->h_ci_pd,
			&srq_attr, &srq_ioctl.in.umv_buf, &h_srq->h_ci_srq );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_SRQ );
			return status;
		}
	}
	/*
	 * Convert the handles to KAL handles once again starting
	 * from the input srq attribute
	 */
	srq_ioctl.in.h_pd = h_pd->obj.hdl;
	srq_ioctl.in.srq_attr = *p_srq_attr;
	srq_ioctl.in.context = (ULONG_PTR)h_srq;
	srq_ioctl.in.ev_notify = (h_srq->pfn_event_cb != NULL) ? TRUE : FALSE;

	cl_status = do_al_dev_ioctl( UAL_CREATE_SRQ,
		&srq_ioctl.in, sizeof(srq_ioctl.in), &srq_ioctl.out, sizeof(srq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(srq_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_CREATE_SRQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = srq_ioctl.out.status;
	}

	/* Post uvp call */
	if( h_pd->h_ci_pd && uvp_intf.post_create_srq )
	{
		uvp_intf.post_create_srq( h_pd->h_ci_pd,
			status, &h_srq->h_ci_srq, &srq_ioctl.out.umv_buf );

		if( uvp_intf.post_recv )
		{
			h_srq->h_recv_srq = h_srq->h_ci_srq;
			h_srq->pfn_post_srq_recv = uvp_intf.post_srq_recv;
		}
		else
		{
			h_srq->h_recv_srq = h_srq;
			h_srq->pfn_post_srq_recv = ual_post_srq_recv;
		}
	}
	else
	{
		h_srq->h_recv_srq = h_srq;
		h_srq->pfn_post_srq_recv = ual_post_srq_recv;
	}

	if( status == IB_SUCCESS )
	{
		h_srq->obj.hdl = srq_ioctl.out.h_srq;
	}

	AL_EXIT( AL_DBG_SRQ );
	return status;
}


ib_api_status_t
ual_modify_srq(
	IN					ib_srq_handle_t			h_srq,
	IN		const		ib_srq_attr_t*		const	p_srq_attr,
	IN		const		ib_srq_attr_mask_t			srq_attr_mask)
{
	ual_modify_srq_ioctl_t		srq_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_srq->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_SRQ );

	/* Clear the srq_ioctl */
	cl_memclr( &srq_ioctl, sizeof(srq_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid srq handle */
	if( h_srq->h_ci_srq && uvp_intf.pre_modify_srq )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_modify_srq( h_srq->h_ci_srq,
			p_srq_attr, srq_attr_mask, &srq_ioctl.in.umv_buf );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_SRQ );
			return status;
		}
	}

	srq_ioctl.in.h_srq = h_srq->obj.hdl;
	srq_ioctl.in.srq_attr = *p_srq_attr;
	srq_ioctl.in.srq_attr_mask = srq_attr_mask;

	cl_status = do_al_dev_ioctl( UAL_MODIFY_SRQ,
		&srq_ioctl.in, sizeof(srq_ioctl.in), &srq_ioctl.out, sizeof(srq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(srq_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_MODIFY_SRQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = srq_ioctl.out.status;
	}

	/* Post uvp call */
	if( h_srq->h_ci_srq && uvp_intf.post_modify_srq )
	{
		uvp_intf.post_modify_srq( h_srq->h_ci_srq, status,
			&srq_ioctl.out.umv_buf );
	}

	//if( status == IB_SUCCESS )
	//{
	//	*p_srq_attr = srq_ioctl.out.srq_attr;
	//}

	AL_EXIT( AL_DBG_SRQ );
	return status;
}


ib_api_status_t
ual_query_srq(
	IN			ib_srq_handle_t				h_srq,
		OUT		ib_srq_attr_t*				p_srq_attr )
{
	ual_query_srq_ioctl_t		srq_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_srq->obj.p_ci_ca->verbs.user_verbs;
	ib_srq_attr_t*				p_attr;

	AL_ENTER( AL_DBG_SRQ );

	/* Clear the srq_ioctl */
	cl_memclr( &srq_ioctl, sizeof(srq_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( h_srq->h_ci_srq && uvp_intf.pre_query_srq )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_query_srq( h_srq->h_ci_srq, &srq_ioctl.in.umv_buf );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_SRQ );
			return status;
		}
	}

	srq_ioctl.in.h_srq = h_srq->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_QUERY_SRQ,
		&srq_ioctl.in, sizeof(srq_ioctl.in), &srq_ioctl.out, sizeof(srq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(srq_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_QUERY_SRQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = srq_ioctl.out.status;
	}

	p_attr = &srq_ioctl.out.srq_attr;

	/* Post uvp call */
	if( h_srq->h_ci_srq && uvp_intf.post_query_srq )
	{
		uvp_intf.post_query_srq( h_srq->h_ci_srq, status,
			p_attr, &srq_ioctl.out.umv_buf );
	}

	if( IB_SUCCESS == status )
	{
		/* UVP handles in srq_attr will be converted to UAL's handles
		 * by the common code
		 */
		*p_srq_attr = *p_attr;
	}

	AL_EXIT( AL_DBG_SRQ );
	return status;
}


ib_api_status_t
ual_destroy_srq(
	IN			ib_srq_handle_t				h_srq )
{
	ual_destroy_srq_ioctl_t		srq_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_srq->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_SRQ );

	/* Call the uvp pre call if the vendor library provided a valid srq handle */
	if( h_srq->h_ci_srq && uvp_intf.pre_destroy_srq )
	{
		status = uvp_intf.pre_destroy_srq( h_srq->h_ci_srq );
		if (status != IB_SUCCESS)
		{
			AL_EXIT( AL_DBG_SRQ );
			return status;
		}
	}

	cl_memclr( &srq_ioctl, sizeof(srq_ioctl) );
	srq_ioctl.in.h_srq = h_srq->obj.hdl;
	cl_status = do_al_dev_ioctl( UAL_DESTROY_SRQ,
		&srq_ioctl.in, sizeof(srq_ioctl.in), &srq_ioctl.out, sizeof(srq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(srq_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DESTROY_SRQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = srq_ioctl.out.status;
	}

	/* Call vendor's post_destroy_srq */
	if( h_srq->h_ci_srq && uvp_intf.post_destroy_srq )
		uvp_intf.post_destroy_srq( h_srq->h_ci_srq, status );

	AL_EXIT( AL_DBG_SRQ );
	return status;
}

