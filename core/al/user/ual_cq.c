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


#include "ual_support.h"
#include "al.h"
#include "al_ca.h"
#include "al_ci_ca.h"
#include "al_cq.h"

#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_cq.tmh"
#endif


ib_api_status_t
ual_create_cq(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN				ib_cq_create_t* const		p_cq_create,
	IN	OUT			ib_cq_handle_t				h_cq )
{
	ual_create_cq_ioctl_t	cq_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_CQ );

	/* Clear the IOCTL buffer */
	cl_memclr( &cq_ioctl, sizeof(cq_ioctl) );

	/* Pre call to the UVP library */
	if( p_ci_ca->h_ci_ca && uvp_intf.pre_create_cq )
	{
		status = uvp_intf.pre_create_cq( p_ci_ca->h_ci_ca,
			&p_cq_create->size, &cq_ioctl.in.umv_buf, &h_cq->h_ci_cq );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_CQ );
			return status;
		}
	}

	cq_ioctl.in.h_ca = p_ci_ca->obj.hdl;
	cq_ioctl.in.size = p_cq_create->size;
	cq_ioctl.in.h_wait_obj = HandleToHandle64( p_cq_create->h_wait_obj );
	cq_ioctl.in.context = (ULONG_PTR)h_cq;
	cq_ioctl.in.ev_notify = (h_cq->pfn_event_cb != NULL) ? TRUE : FALSE;

	cl_status = do_al_dev_ioctl( UAL_CREATE_CQ,
		&cq_ioctl.in, sizeof(cq_ioctl.in), &cq_ioctl.out, sizeof(cq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(cq_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_CREATE_CQ IOCTL returned %s.\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = cq_ioctl.out.status;
	}

	h_cq->obj.hdl = cq_ioctl.out.h_cq;

	/* Post uvp call */
	if( p_ci_ca->h_ci_ca && uvp_intf.post_create_cq )
	{
		uvp_intf.post_create_cq( p_ci_ca->h_ci_ca,
			status, cq_ioctl.out.size, &h_cq->h_ci_cq,
			&cq_ioctl.out.umv_buf );

		if( uvp_intf.peek_cq )
		{
			h_cq->pfn_peek = uvp_intf.peek_cq;
			h_cq->h_peek_cq = h_cq->h_ci_cq;
		}
		else
		{
			h_cq->pfn_peek = ual_peek_cq;
			h_cq->h_peek_cq = h_cq;
		}

		if( uvp_intf.poll_cq )
		{
			h_cq->pfn_poll = uvp_intf.poll_cq;
			h_cq->h_poll_cq = h_cq->h_ci_cq;
		}
		else
		{
			h_cq->pfn_poll = ual_poll_cq;
			h_cq->h_poll_cq = h_cq;
		}

		if( uvp_intf.rearm_cq )
		{
			h_cq->pfn_rearm = uvp_intf.rearm_cq;
			h_cq->h_rearm_cq = h_cq->h_ci_cq;
		}
		else
		{
			h_cq->pfn_rearm = ual_rearm_cq;
			h_cq->h_rearm_cq = h_cq;
		}

		if( uvp_intf.rearm_n_cq )
		{
			h_cq->pfn_rearm_n = uvp_intf.rearm_n_cq;
			h_cq->h_rearm_n_cq = h_cq->h_ci_cq;
		}
		else
		{
			h_cq->pfn_rearm_n = ual_rearm_n_cq;
			h_cq->h_rearm_n_cq = h_cq;
		}
	}
	else
	{
		h_cq->pfn_peek = ual_peek_cq;
		h_cq->pfn_poll = ual_poll_cq;
		h_cq->pfn_rearm = ual_rearm_cq;
		h_cq->pfn_rearm_n = ual_rearm_n_cq;
		h_cq->h_peek_cq = h_cq;
		h_cq->h_poll_cq = h_cq;
		h_cq->h_rearm_cq = h_cq;
		h_cq->h_rearm_n_cq = h_cq;
	}

	AL_EXIT( AL_DBG_CQ );
	return status;
}


ib_api_status_t
ual_destroy_cq(
	IN			ib_cq_handle_t				h_cq )
{
	ual_destroy_cq_ioctl_t	cq_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_cq->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_CQ );

	/* Clear the IOCTL buffer */
	cl_memclr( &cq_ioctl, sizeof(cq_ioctl) );

	if( h_cq->h_ci_cq && uvp_intf.pre_destroy_cq )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_destroy_cq( h_cq->h_ci_cq );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_CQ );
			return status;
		}
	}

	cq_ioctl.in.h_cq = h_cq->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_DESTROY_CQ,
		&cq_ioctl.in, sizeof(cq_ioctl.in), &cq_ioctl.out, sizeof(cq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(cq_ioctl.out ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DESTROY_CQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = cq_ioctl.out.status;
	}

	if( h_cq->h_ci_cq && uvp_intf.post_destroy_cq )
		uvp_intf.post_destroy_cq( h_cq->h_ci_cq, status );

	AL_EXIT( AL_DBG_CQ );
	return status;
}


ib_api_status_t
ual_modify_cq(
	IN			ib_cq_handle_t				h_cq,
	IN	OUT		uint32_t*					p_size )
{
	ual_modify_cq_ioctl_t	cq_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_cq->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_CQ );

	/* Clear the IOCTL buffer */
	cl_memclr( &cq_ioctl, sizeof(cq_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( h_cq->h_ci_cq && uvp_intf.pre_resize_cq )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_resize_cq( h_cq->h_ci_cq,
			p_size, &cq_ioctl.in.umv_buf );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_CQ );
			return status;
		}
	}

	cq_ioctl.in.h_cq = h_cq->obj.hdl;
	cq_ioctl.in.size = *p_size;

	cl_status = do_al_dev_ioctl( UAL_MODIFY_CQ,
		&cq_ioctl.in, sizeof(cq_ioctl.in), &cq_ioctl.out, sizeof(cq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(cq_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_MODIFY_CQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = cq_ioctl.out.status;
		if( status == IB_SUCCESS )
			*p_size = cq_ioctl.out.size;
	}

	/* Post uvp call */
	if( h_cq->h_ci_cq && uvp_intf.post_resize_cq )
	{
		uvp_intf.post_resize_cq( h_cq->h_ci_cq,
			status, cq_ioctl.out.size, &cq_ioctl.out.umv_buf );
	}

	AL_EXIT( AL_DBG_CQ );
	return status;
}


ib_api_status_t
ual_query_cq(
	IN			ib_cq_handle_t				h_cq,
		OUT		uint32_t*					p_size )
{
	ual_query_cq_ioctl_t	cq_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_cq->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_CQ );

	/* Clear the IOCTL buffer */
	cl_memclr( &cq_ioctl, sizeof(cq_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid cq handle */
	if( h_cq->h_ci_cq && uvp_intf.pre_query_cq )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_query_cq(
			h_cq->h_ci_cq, p_size, &cq_ioctl.in.umv_buf );
		if( status == IB_VERBS_PROCESSING_DONE )
		{
			AL_EXIT( AL_DBG_CQ );
			return IB_SUCCESS;
		}
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_CQ );
			return status;
		}
	}

	cq_ioctl.in.h_cq = h_cq->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_QUERY_CQ,
		&cq_ioctl.in, sizeof(cq_ioctl.in), &cq_ioctl.out, sizeof(cq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(cq_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_QUERY_CQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = cq_ioctl.out.status;
		if( status == IB_SUCCESS )
			*p_size = cq_ioctl.out.size;
	}

	/* Post uvp call */
	if( h_cq->h_ci_cq && uvp_intf.post_query_cq )
	{
		uvp_intf.post_query_cq( h_cq->h_ci_cq,
			status, cq_ioctl.out.size, &cq_ioctl.out.umv_buf );
	}

	AL_EXIT( AL_DBG_CQ );
	return status;
}


ib_api_status_t
ual_peek_cq(
	IN		const	ib_cq_handle_t				h_cq,
	OUT				uint32_t* const				p_n_cqes )
{
	ual_peek_cq_ioctl_t		cq_ioctl;
	cl_status_t				cl_status;
	uintn_t					bytes_ret;

	AL_ENTER( AL_DBG_CQ );

	/* Clear the IOCTL buffer */
	cl_memclr(&cq_ioctl, sizeof(cq_ioctl));

	cq_ioctl.in.h_cq = h_cq->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_PEEK_CQ,
		&cq_ioctl.in, sizeof(cq_ioctl.in), &cq_ioctl.out, sizeof(cq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(cq_ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_PEEK_CQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}

	if( cq_ioctl.out.status == IB_SUCCESS )
		*p_n_cqes = cq_ioctl.out.n_cqes;

	AL_EXIT( AL_DBG_CQ );
	return cq_ioctl.out.status;
}


ib_api_status_t
ual_poll_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			ib_wc_t**	const			pp_free_wclist,
		OUT			ib_wc_t**	const			pp_done_wclist )
{
	uintn_t					bytes_ret;
	ual_poll_cq_ioctl_t		*p_cq_ioctl;
	size_t					ioctl_buf_sz;
	uint32_t				num_wc;
	ib_wc_t*				p_wc_start;
	ib_wc_t*				p_next_wc;
	cl_status_t				cl_status;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	/*
	 * Since the work request is a link list and we need to pass this
	 * to the kernel as a array of work requests.  So first walk through
	 * the list and find out how much memory we need to allocate.
	 */
	p_next_wc = *pp_free_wclist;
	num_wc = 0;
	while( p_next_wc )
	{
		num_wc++;

		/* Check for overflow */
		if( !num_wc )
			break;

		p_next_wc = p_next_wc->p_next;
	}
	if( !num_wc )
	{
		AL_EXIT( AL_DBG_CQ );
		return IB_INVALID_PARAMETER;
	}

	ioctl_buf_sz = sizeof(p_cq_ioctl->out);
	ioctl_buf_sz += sizeof(ib_wc_t) * (num_wc - 1);

	p_cq_ioctl = (ual_poll_cq_ioctl_t*)cl_zalloc( ioctl_buf_sz );

	/* Now populate the ioctl buffer and send down the ioctl */
	p_cq_ioctl->in.h_cq = h_cq->obj.hdl;
	p_cq_ioctl->in.num_wc = num_wc;

	cl_status = do_al_dev_ioctl( UAL_POLL_CQ,
		p_cq_ioctl, sizeof(p_cq_ioctl->in), p_cq_ioctl, ioctl_buf_sz,
		&bytes_ret );

	/* Make sure we got the right amount of data returned. */
	if( cl_status != CL_SUCCESS ||
		bytes_ret < (sizeof(p_cq_ioctl->out) - sizeof(ib_wc_t)) ||
		(cl_status == CL_SUCCESS && bytes_ret < (sizeof(p_cq_ioctl->out) -
		sizeof(ib_wc_t) + (sizeof(ib_wc_t) * p_cq_ioctl->out.num_wc))) )
	{
		cl_free( p_cq_ioctl );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_POLL_CQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}

	status = p_cq_ioctl->out.status;
	if( status == IB_SUCCESS )
	{
		CL_ASSERT( p_cq_ioctl->out.num_wc );
		/* Fix up the free and done lists. */
		p_next_wc = *pp_free_wclist;
		num_wc = 0;
		p_wc_start = p_next_wc;
		do
		{
			p_wc_start = p_next_wc;
			CL_ASSERT( p_wc_start );
			/* Save next pointer. */
			p_next_wc = p_wc_start->p_next;
			/* Copy WC contents back to user. */
			cl_memcpy(
				p_wc_start, &p_cq_ioctl->out.wc[num_wc], sizeof(ib_wc_t) );
			/* Restore next pointer. */
			p_wc_start->p_next = p_next_wc;
		} while( ++num_wc < p_cq_ioctl->out.num_wc );

		p_wc_start->p_next = NULL;
		*pp_done_wclist = *pp_free_wclist;
		*pp_free_wclist = p_next_wc;
	}

	cl_free( p_cq_ioctl );

	AL_EXIT( AL_DBG_CQ );
	return status;
}


ib_api_status_t
ual_rearm_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	boolean_t					solicited )
{
	ual_rearm_cq_ioctl_t	cq_ioctl;
	cl_status_t				cl_status;
	uintn_t					bytes_ret;

	AL_ENTER( AL_DBG_CQ );

	/* Clear the IOCTL buffer */
	cl_memclr(&cq_ioctl, sizeof(cq_ioctl));

	cq_ioctl.in.h_cq = h_cq->obj.hdl;
	cq_ioctl.in.solicited = solicited;

	cl_status = do_al_dev_ioctl( UAL_REARM_CQ,
		&cq_ioctl.in, sizeof(cq_ioctl.in), &cq_ioctl.out, sizeof(cq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(cq_ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_REARM_CQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CQ );
	return cq_ioctl.out.status;
}


ib_api_status_t
ual_rearm_n_cq(
	IN		const	ib_cq_handle_t				h_cq,
	IN		const	uint32_t					n_cqes )
{
	ual_rearm_n_cq_ioctl_t	cq_ioctl;
	cl_status_t				cl_status;
	uintn_t					bytes_ret;

	AL_ENTER( AL_DBG_CQ );

	/* Clear the IOCTL buffer */
	cl_memclr(&cq_ioctl, sizeof(cq_ioctl));

	cq_ioctl.in.h_cq = h_cq->obj.hdl;
	cq_ioctl.in.n_cqes = n_cqes;

	cl_status = do_al_dev_ioctl( UAL_REARM_N_CQ,
		&cq_ioctl.in, sizeof(cq_ioctl.in), &cq_ioctl.out, sizeof(cq_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(cq_ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_REARM_N_CQ IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}
	
	AL_EXIT( AL_DBG_CQ );
	return cq_ioctl.out.status;
}
