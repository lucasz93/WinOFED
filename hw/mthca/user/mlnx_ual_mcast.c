/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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

#include "mlnx_ual_main.h"

#if defined(EVENT_TRACING)
#include "mlnx_ual_mcast.tmh"
#endif

static ib_api_status_t
__pre_attach_mcast (
	IN	const	ib_qp_handle_t		h_uvp_qp,
	IN	const	ib_gid_t			*p_mcast_gid,
	IN	const	uint16_t			mcast_lid,
	IN OUT	  	ci_umv_buf_t		*p_umv_buf,
	   OUT		ib_mcast_handle_t	*ph_mcast)
{
	UNREFERENCED_PARAMETER(ph_mcast);
	
	UVP_ENTER(UVP_DBG_SHIM);
	CL_ASSERT(p_umv_buf);
	p_umv_buf->p_inout_buf = 0;;
	p_umv_buf->input_size = 0;
	p_umv_buf->output_size = 0;
	p_umv_buf->command = TRUE;

	UVP_EXIT(UVP_DBG_SHIM);
	return IB_SUCCESS;
}



static void
__post_attach_mcast (
	IN		const ib_qp_handle_t	h_uvp_qp,
	IN		ib_api_status_t			ioctl_status,
	IN OUT	ib_mcast_handle_t		*ph_mcast,
	IN OUT	ci_umv_buf_t			*p_umv_buf)
{
	UVP_ENTER(UVP_DBG_SHIM);
	UVP_EXIT(UVP_DBG_SHIM);
}



static ib_api_status_t
__pre_detach_mcast (
	IN		ib_mcast_handle_t	h_uvp_mcast,
	IN OUT	ci_umv_buf_t		*p_umv_buf)
{
	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(p_umv_buf);
	p_umv_buf->p_inout_buf = 0;
	p_umv_buf->input_size = 0;
	p_umv_buf->output_size = 0;

	UVP_EXIT(UVP_DBG_SHIM);
	return IB_SUCCESS;
}


static void
__post_detach_mcast (
	IN		ib_mcast_handle_t	h_uvp_mcast,
	IN		ib_api_status_t		ioctl_status,
	IN OUT	ci_umv_buf_t		*p_umv_buf)
{
	UVP_ENTER(UVP_DBG_SHIM);
	UVP_EXIT(UVP_DBG_SHIM);
}

void
mlnx_get_mcast_interface (
	IN OUT	uvp_interface_t		*p_uvp )
{
	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(p_uvp);

	/*
	 * Multicast Support Verbs
	 */
	p_uvp->pre_attach_mcast  = NULL;
	p_uvp->post_attach_mcast = NULL;
	p_uvp->pre_detach_mcast  = NULL;
	p_uvp->post_detach_mcast = NULL;

	UVP_EXIT(UVP_DBG_SHIM);
}


