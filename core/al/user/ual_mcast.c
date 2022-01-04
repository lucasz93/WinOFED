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
#include "al_qp.h"
#include "ual_support.h"
#include "ual_mcast.h"

#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_mcast.tmh"
#endif

ib_api_status_t
ual_attach_mcast(
	IN			ib_mcast_handle_t			h_mcast )
{
	ual_attach_mcast_ioctl_t	ioctl_buf;
	uintn_t						bytes_ret;
	cl_status_t					cl_status;
	ib_api_status_t				status = IB_ERROR;
	ib_qp_handle_t				h_qp;
	uvp_interface_t				uvp_intf;

	AL_ENTER( AL_DBG_MCAST );

	h_qp = PARENT_STRUCT( h_mcast->obj.p_parent_obj,
		al_dgrm_qp_t, obj );
	uvp_intf = h_qp->obj.p_ci_ca->verbs.user_verbs;

	/* Clear the ioctl_buf */
	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	/* Pre call to the UVP library */
	if( h_qp->h_ci_qp && uvp_intf.pre_attach_mcast )
	{
		status = uvp_intf.pre_attach_mcast( h_qp->h_ci_qp,
			&h_mcast->member_rec.mgid, h_mcast->member_rec.mlid,
			&ioctl_buf.in.umv_buf, &h_mcast->h_ci_mcast );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_PD );
			return status;
		}
	}

	ioctl_buf.in.h_qp = h_qp->obj.hdl;
	ioctl_buf.in.mgid = h_mcast->member_rec.mgid;
	ioctl_buf.in.mlid = h_mcast->member_rec.mlid;

	cl_status = do_al_dev_ioctl( UAL_ATTACH_MCAST,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_ATTACH_MCAST IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ioctl_buf.out.status;
		if( status == IB_SUCCESS ){
			h_mcast->obj.hdl = ioctl_buf.out.h_attach;
			h_mcast->h_ci_mcast = (ib_mcast_handle_t)(ULONG_PTR)ioctl_buf.out.h_attach;
		}
	}

	/* Post uvp call */
	if( h_qp->h_ci_qp && uvp_intf.post_attach_mcast )
	{
		uvp_intf.post_attach_mcast( h_qp->h_ci_qp,
			status, &h_mcast->h_ci_mcast, &ioctl_buf.out.umv_buf);
	}

	AL_EXIT( AL_DBG_MCAST );
	return status;
}


ib_api_status_t
ual_detach_mcast(
	IN			ib_mcast_handle_t			h_mcast )
{
	ual_detach_mcast_ioctl_t	ioctl_buf;
	uintn_t						bytes_ret;
	cl_status_t					cl_status;
	ib_api_status_t				status;
	ib_qp_handle_t				h_qp;
	uvp_interface_t				uvp_intf;

	AL_ENTER( AL_DBG_MCAST );

	h_qp = PARENT_STRUCT( h_mcast->obj.p_parent_obj,
		al_dgrm_qp_t, obj );
	uvp_intf = h_qp->obj.p_ci_ca->verbs.user_verbs;

	/* Clear the ioctl_buf */
	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	/* Pre call to the UVP library */
	if( h_qp->h_ci_qp && uvp_intf.pre_detach_mcast )
	{
		status = uvp_intf.pre_detach_mcast( h_mcast->h_ci_mcast );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_MCAST );
			return status;
		}
	}

	ioctl_buf.in.h_attach = h_mcast->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_DETACH_MCAST,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DETACH_MCAST IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ioctl_buf.out.status;
	}

	/* Post uvp call */
	if( h_mcast->h_ci_mcast && uvp_intf.post_detach_mcast )
		uvp_intf.post_detach_mcast( h_mcast->h_ci_mcast, status );

	AL_EXIT( AL_DBG_MCAST );
	return status;
}
