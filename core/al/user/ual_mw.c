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
#include "ual_support.h"
#include "al.h"
#include "al_pd.h"
#include "al_qp.h"
#include "al_mw.h"
#include "al_mr.h"


#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_mw.tmh"
#endif

ib_api_status_t
ual_create_mw(
	IN		const	ib_pd_handle_t				h_pd,
		OUT			net32_t* const				p_rkey,
	IN	OUT			ib_mw_handle_t				h_mw )
{
	ual_create_mw_ioctl_t	mw_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_pd->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_MW );

	/* Clear the mw_ioctl */
	cl_memclr( &mw_ioctl, sizeof(mw_ioctl) );

	/* Pre call to the UVP library */
	if( h_pd->h_ci_pd && uvp_intf.pre_create_mw )
	{
		status = uvp_intf.pre_create_mw(
			h_pd->h_ci_pd, &mw_ioctl.in.umv_buf, &h_mw->h_ci_mw );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_MW );
			return status;
		}
	}

	mw_ioctl.in.h_pd = h_pd->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_CREATE_MW,
		&mw_ioctl.in, sizeof(mw_ioctl.in), &mw_ioctl.out, sizeof(mw_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mw_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_CREATE_MW IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = mw_ioctl.out.status;
		if( status == IB_SUCCESS )
		{
			h_mw->obj.hdl = mw_ioctl.out.h_mw;
			*p_rkey = mw_ioctl.out.rkey;
		}
	}

	/* Post uvp call */
	if( h_pd->h_ci_pd && uvp_intf.post_create_mw )
	{
		uvp_intf.post_create_mw( h_pd->h_ci_pd, status,
			mw_ioctl.out.rkey, &h_mw->h_ci_mw,
			&mw_ioctl.out.umv_buf );
	}

	

	AL_EXIT( AL_DBG_MW );
	return status;
}


ib_api_status_t
ual_destroy_mw(
	IN			ib_mw_handle_t				h_mw )
{
	ual_destroy_mw_ioctl_t	mw_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_mw->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_MW );

	/* Clear the mw_ioctl */
	cl_memclr( &mw_ioctl, sizeof(mw_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid handle */
	if( h_mw->h_ci_mw && uvp_intf.pre_destroy_mw )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_destroy_mw( h_mw->h_ci_mw );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_MW );
			return status;
		}
	}

	mw_ioctl.in.h_mw = h_mw->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_DESTROY_MW,
		&mw_ioctl.in, sizeof(mw_ioctl.in), &mw_ioctl.out, sizeof(mw_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mw_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DESTROY_MW IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = mw_ioctl.out.status;
	}

	/* Post uvp call */
	if( h_mw->h_ci_mw && uvp_intf.post_destroy_mw )
		uvp_intf.post_destroy_mw( h_mw->h_ci_mw, status );

	if( status == IB_SUCCESS )
	{
		h_mw->obj.hdl = AL_INVALID_HANDLE;
		h_mw->h_ci_mw = NULL;
	}

	AL_EXIT( AL_DBG_MW );
	return status;
}


ib_api_status_t
ual_query_mw(
	IN			ib_mw_handle_t				h_mw,
	OUT			ib_pd_handle_t*				ph_pd,
	OUT			net32_t* const				p_rkey )
{
	ual_query_mw_ioctl_t	mw_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_mw->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_MW );
	/* Clear the mw_ioctl */
	cl_memclr( &mw_ioctl, sizeof(mw_ioctl) );

	/* Pre call to the UVP library */
	if( h_mw->h_ci_mw && uvp_intf.pre_query_mw )
	{
		status = uvp_intf.pre_query_mw(
			h_mw->h_ci_mw, &mw_ioctl.in.umv_buf );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_MW );
			return status;
		}
	}

	mw_ioctl.in.h_mw = h_mw->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_QUERY_MW,
		&mw_ioctl.in, sizeof(mw_ioctl.in), &mw_ioctl.out, sizeof(mw_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mw_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_QUERY_MW IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = mw_ioctl.out.status;
	}

	if( IB_SUCCESS == status )
	{
		*p_rkey = mw_ioctl.out.rkey;
		*ph_pd = (ib_pd_handle_t)h_mw->obj.p_parent_obj;
	}

	/* Post uvp call */
	if( h_mw->h_ci_mw && uvp_intf.post_query_mw )
	{
		ib_pd_handle_t junk;
		uvp_intf.post_query_mw( h_mw->h_ci_mw, status,
			mw_ioctl.out.rkey, &junk, &mw_ioctl.out.umv_buf );
	}

	AL_EXIT( AL_DBG_MW );
	return status;
}


ib_api_status_t
ual_bind_mw(
	IN		const	ib_mw_handle_t				h_mw,
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_bind_wr_t*				p_mw_bind,
		OUT			net32_t* const				p_rkey )
{
	ual_bind_mw_ioctl_t		mw_ioctl;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uintn_t					bytes_ret;
	ib_mr_handle_t			h_user_mr;
	/*
	 * Check whether a vendor library is available and the
	 * bind_mw call is implemented.  If so, the call terminates
	 * at the UVP library.  If not, pass this to kernel.
	 */
	uvp_interface_t			uvp_intf = h_mw->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_MW );

	/* Clear the mw_ioctl */
	cl_memclr( &mw_ioctl, sizeof(mw_ioctl) );

	/* Call to the UVP library */
	if( h_mw->h_ci_mw && h_qp->h_ci_qp && uvp_intf.bind_mw )
	{
		h_user_mr = p_mw_bind->h_mr;
		p_mw_bind->h_mr = p_mw_bind->h_mr->h_ci_mr;
		status = uvp_intf.bind_mw( h_mw->h_ci_mw,
			h_qp->h_ci_qp, p_mw_bind, p_rkey);
		p_mw_bind->h_mr = h_user_mr;
		AL_EXIT( AL_DBG_MW );
		return status;
	}

	mw_ioctl.in.h_mw = h_mw->obj.hdl;
	mw_ioctl.in.h_qp = h_qp->obj.hdl;
	mw_ioctl.in.mw_bind = *p_mw_bind;
	mw_ioctl.in.mw_bind.h_mr_padding = p_mw_bind->h_mr->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_BIND_MW,
		&mw_ioctl.in, sizeof(mw_ioctl.in), &mw_ioctl.out, sizeof(mw_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mw_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_BIND_MW IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else if( mw_ioctl.out.status == IB_SUCCESS )
	{
		*p_rkey = mw_ioctl.out.r_key;
	}

	AL_EXIT( AL_DBG_MW );
	return mw_ioctl.out.status;
}
