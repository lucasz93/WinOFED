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
#include "al_common.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_mad.tmh"
#endif

#include "al_dev.h"
#include "al_qp.h"
#include "al_pd.h"
#include "ual_mad.h"
#include "ual_support.h"


static void
__destroying_ual_mad_svc(
	IN				struct _al_obj				*p_obj )
{
	ib_mad_svc_handle_t		h_mad_svc;

	AL_ENTER(AL_DBG_MAD_SVC);
	CL_ASSERT( p_obj );
	h_mad_svc = PARENT_STRUCT( p_obj, al_mad_svc_t, obj );

	/* Deregister the MAD service. */
	ual_dereg_mad_svc( h_mad_svc );

	AL_EXIT(AL_DBG_MAD_SVC);
}


ib_api_status_t
ual_reg_mad_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mad_svc_t* const			p_mad_svc,
		OUT			ib_mad_svc_handle_t* const	ph_mad_svc )
{
	ib_api_status_t				status;
	cl_status_t					cl_status;
	uintn_t						bytes_ret;
	ib_mad_svc_handle_t			h_mad_svc;
	al_qp_alias_t				*p_qp_alias;
	ual_reg_mad_svc_ioctl_t		ioctl_buf;

	AL_ENTER( AL_DBG_MAD );

	CL_ASSERT( h_qp && p_mad_svc && ph_mad_svc );

	h_mad_svc = cl_zalloc( sizeof( al_mad_svc_t) );
	if( !h_mad_svc )
	{
		AL_EXIT( AL_DBG_MAD );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the MAD service. */
	construct_al_obj( &h_mad_svc->obj, AL_OBJ_TYPE_H_MAD_SVC );

	p_qp_alias = PARENT_STRUCT( h_qp, al_qp_alias_t, qp );
	h_mad_svc->obj.context = p_mad_svc->mad_svc_context;
	h_mad_svc->pfn_user_recv_cb = p_mad_svc->pfn_mad_recv_cb;
	h_mad_svc->pfn_user_send_cb = p_mad_svc->pfn_mad_send_cb;

	/* Initialize the MAD service. */
	status = init_al_obj( &h_mad_svc->obj, p_mad_svc->mad_svc_context,
		TRUE, NULL, __destroying_ual_mad_svc, free_mad_svc );
	if( status != IB_SUCCESS )
	{
		cl_free( h_mad_svc );
		AL_EXIT( AL_DBG_MAD );
		return status;
	}
	attach_al_obj( &h_qp->obj, &h_mad_svc->obj );

	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	ioctl_buf.in.h_qp = h_qp->obj.hdl;
	ioctl_buf.in.mad_svc = *p_mad_svc;

	/* Replace the context in mad_svc */
	ioctl_buf.in.mad_svc.mad_svc_context = h_mad_svc;

	cl_status = do_al_dev_ioctl( UAL_REG_MAD_SVC,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_REG_MAD_SVC IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ioctl_buf.out.status;
		if(status == IB_SUCCESS)
			h_mad_svc->obj.hdl = ioctl_buf.out.h_mad_svc;
			
	}

	if( status != IB_SUCCESS )
	{
		h_mad_svc->obj.pfn_destroy( &h_mad_svc->obj, NULL );
		AL_EXIT( AL_DBG_MAD );
		return status;
	}

	*ph_mad_svc = h_mad_svc;

	AL_EXIT( AL_DBG_MAD );
	return status;
}



ib_api_status_t
ual_dereg_mad_svc(
	IN		const	ib_mad_svc_handle_t			h_mad_svc )
{
	ual_dereg_mad_svc_ioctl_t	ioctl_buf;
	uintn_t						bytes_ret;
	cl_status_t					cl_status;
	ib_api_status_t				status;

	AL_ENTER(AL_DBG_MAD);

	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	ioctl_buf.in.h_mad_svc = h_mad_svc->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_DEREG_MAD_SVC, &ioctl_buf,
		sizeof(ioctl_buf.in), &ioctl_buf, sizeof(ioctl_buf.out), &bytes_ret );
	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
		status = IB_ERROR;
	else
		status = ioctl_buf.out.status;

	if( status != IB_SUCCESS )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("Error deregistering MAD svc: %s\n", ib_get_err_str( status ) ) );
	}

	AL_EXIT(AL_DBG_MAD);
	return status;
}



ib_api_status_t
ual_send_one_mad(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element )
{
	ual_send_mad_ioctl_t	ioctl_buf;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	al_mad_element_t*		p_al_element;

	AL_ENTER( AL_DBG_MAD );

	CL_ASSERT( p_mad_element );

	p_al_element = PARENT_STRUCT(
		p_mad_element, al_mad_element_t, element );

	p_mad_element->status = IB_WCS_UNKNOWN;

	ioctl_buf.in.h_mad_svc = h_mad_svc->obj.hdl;

	/* Update the pool key to the proxy's handle. */
	ioctl_buf.in.pool_key = p_al_element->pool_key->obj.hdl;

	/*
	 * Convert user-mode AV handles to kernel AV handles.  Note that
	 * the completion handler will convert the handles back before
	 * returning the MAD to the user.
	 */
	if( p_mad_element->h_av )
		ioctl_buf.in.h_av = p_mad_element->h_av->obj.hdl;
	else
		ioctl_buf.in.h_av = AL_INVALID_HANDLE;

	ioctl_buf.in.p_mad_element = (ULONG_PTR)p_mad_element;
	ioctl_buf.in.size = p_mad_element->size;
	ioctl_buf.in.ph_proxy = (ULONG_PTR)&p_al_element->h_proxy_element;

	cl_status = do_al_dev_ioctl( UAL_MAD_SEND,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_MAD_SEND IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ioctl_buf.out.status;
	}

	AL_EXIT( AL_DBG_MAD );
	return status;
}



ib_api_status_t
ual_spl_qp_mad_send(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element_list,
		OUT			ib_mad_element_t			**pp_mad_failure OPTIONAL )
{
	ib_api_status_t			status;
	ib_mad_element_t*		p_next_element;

	AL_ENTER( AL_DBG_MAD );

	/* Count up the mads in the list. */
	p_next_element = p_mad_element_list;
	do
	{
		status = ual_send_one_mad( h_mad_svc, p_next_element );
		if( status != IB_SUCCESS )
			break;

		p_next_element = p_next_element->p_next;

	} while( p_next_element );

	if( status != IB_SUCCESS && pp_mad_failure )
		*pp_mad_failure = p_next_element;

	AL_EXIT( AL_DBG_MAD );
	return status;
}



ib_api_status_t
ual_spl_qp_cancel_mad(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element )
{
	ual_cancel_mad_ioctl_t	ioctl_buf;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	al_mad_element_t*		p_al_mad;

	AL_ENTER( AL_DBG_MAD );

	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	ioctl_buf.in.h_mad_svc = h_mad_svc->obj.hdl;
	/*
	 * Pass the corresponding kernel mode mad_element as KAL knows
	 * only about kernel mads.  This gets set when we send the mad.
	 */
	p_al_mad = PARENT_STRUCT(p_mad_element, al_mad_element_t, element);
	ioctl_buf.in.h_proxy_element = p_al_mad->h_proxy_element;

	cl_status = do_al_dev_ioctl( UAL_CANCEL_MAD,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CANCEL_MAD IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_MAD );
	return ioctl_buf.out.status;
}


ib_api_status_t
ual_create_reg_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
	IN		const	ib_pd_handle_t				h_pd,
	IN	OUT			ib_pool_key_t				p_pool_key )
{
	ual_reg_mad_pool_ioctl_t		ioctl_buf;
	uintn_t						bytes_ret;
	cl_status_t					cl_status;
	ib_api_status_t				status = IB_ERROR;
	
	AL_ENTER( AL_DBG_MAD );

	/*TODO: Can h_pool be removed as a param? */
	UNUSED_PARAM( h_pool );

	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	ioctl_buf.in.h_pd = h_pd->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_REG_MAD_POOL,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_REG_MAD_POOL IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status =  IB_ERROR;
	}
	else
	{
		status =  ioctl_buf.out.status;
		if(status == IB_SUCCESS )
			p_pool_key->obj.hdl = ioctl_buf.out.pool_key;
	}

	if( ioctl_buf.out.status == IB_SUCCESS )
		p_pool_key->obj.hdl = ioctl_buf.out.pool_key;

	AL_EXIT( AL_DBG_MAD );
	return status;
}


void
ual_dereg_destroy_mad_pool(
	IN		const	ib_pool_key_t				pool_key )
{
	ual_dereg_mad_pool_ioctl_t	ioctl_buf;
	uintn_t						bytes_ret;
	cl_status_t					cl_status;

	AL_ENTER( AL_DBG_MAD );

	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	ioctl_buf.in.pool_key = pool_key->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_DEREG_MAD_POOL,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DEREG_MAD_POOL IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)));
	}
	else if( ioctl_buf.out.status != IB_SUCCESS )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("Error deregistering MAD pool: %s\n",
			ib_get_err_str( ioctl_buf.out.status )) );
	}
	AL_EXIT( AL_DBG_MAD );
}


/*
 * We've receive a MAD.  We need to get a user-mode MAD of the
 * correct size, then send it down to retrieve the received MAD
 * from the kernel.
 */
ib_api_status_t
ual_get_recv_mad(
	IN				ib_pool_key_t				p_pool_key,
	IN		const	uint64_t					h_mad,
	IN		const	size_t						buf_size,
		OUT			ib_mad_element_t** const	pp_mad_element )
{
	ual_mad_recv_ioctl_t	ioctl_buf;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	ib_mad_element_t		*p_mad = NULL;
	ib_mad_t				*p_mad_buf = NULL;
	ib_grh_t				*p_grh = NULL;

	AL_ENTER( AL_DBG_MAD );

	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	/*
	 * Get a MAD large enough to receive the MAD.  If we can't get a
	 * MAD, we still perform the IOCTL so that the kernel will return
	 * the MAD to its pool, resulting in a dropped MAD.
	 */
	status = ib_get_mad( p_pool_key, buf_size, &p_mad );

	/*
	 * Note that we issue the IOCTL regardless of failure of ib_get_mad.
	 * This is done in order to release the kernel-mode MAD.
	 */
	ioctl_buf.in.p_user_mad = (ULONG_PTR)p_mad;

	if( p_mad )
	{
		/* Save off the pointers since the proxy overwrites the element. */
		p_mad_buf = p_mad->p_mad_buf;
		p_grh = p_mad->p_grh;

		ioctl_buf.in.p_mad_buf = (ULONG_PTR)p_mad_buf;
		ioctl_buf.in.p_grh = (ULONG_PTR)p_grh;
	}
	ioctl_buf.in.h_mad = h_mad;

	cl_status = do_al_dev_ioctl( UAL_MAD_RECV_COMP,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );
	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_MAD_RECV_COMP IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ioctl_buf.out.status;
	}

	if( p_mad )
	{
		/* We need to reset MAD data pointers. */
		p_mad->p_mad_buf = p_mad_buf;
		p_mad->p_grh = p_grh;
		if( status != IB_SUCCESS )
		{
			ib_put_mad( p_mad );
			p_mad = NULL;
		}
	}

	*pp_mad_element = p_mad;

	AL_EXIT( AL_DBG_MAD );
	return status;
}


ib_api_status_t
ual_local_mad(
	IN				const ib_ca_handle_t		h_ca,
	IN				const uint8_t				port_num,
	IN				ib_mad_t* const				p_mad_in,
	IN				ib_mad_t*					p_mad_out )
{
	ual_local_mad_ioctl_t		local_mad_ioctl;
	uintn_t						bytes_ret;
	cl_status_t					cl_status = CL_SUCCESS;
	ib_api_status_t				status = IB_SUCCESS;

	AL_ENTER( AL_DBG_CA );

	local_mad_ioctl.in.h_ca = h_ca->obj.p_ci_ca->obj.hdl;
	local_mad_ioctl.in.port_num = port_num;
	cl_memcpy( local_mad_ioctl.in.mad_in, p_mad_in,
		sizeof(local_mad_ioctl.in.mad_in) );

	cl_status = do_al_dev_ioctl( UAL_LOCAL_MAD,
		&local_mad_ioctl.in, sizeof(local_mad_ioctl.in),
		&local_mad_ioctl.out, sizeof(local_mad_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(local_mad_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_LOCAL_MAD IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = local_mad_ioctl.out.status;
		cl_memcpy( p_mad_out, local_mad_ioctl.out.mad_out,
			sizeof(local_mad_ioctl.out.mad_out) );
	}

	AL_EXIT( AL_DBG_CA );
	return status;
}


