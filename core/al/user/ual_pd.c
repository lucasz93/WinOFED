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
#include "al_pd.h"


#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_pd.tmh"
#endif

ib_api_status_t
ual_allocate_pd(
	IN				ib_ca_handle_t				h_ca,
	IN		const	ib_pd_type_t				pd_type,
	IN	OUT			ib_pd_handle_t				h_pd )
{
	/* The first two arguments is probably not needed */
	ual_alloc_pd_ioctl_t	pd_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	ib_ca_handle_t			h_uvp_ca;
	uvp_interface_t			uvp_intf = h_ca->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_PD );

	/* Clear the pd_ioctl */
	cl_memclr( &pd_ioctl, sizeof(pd_ioctl) );

	h_uvp_ca = h_ca->obj.p_ci_ca->h_ci_ca;

	/* Pre call to the UVP library */
	if( pd_type != IB_PDT_ALIAS && h_uvp_ca && uvp_intf.pre_allocate_pd )
	{
		status = uvp_intf.pre_allocate_pd( h_uvp_ca, &pd_ioctl.in.umv_buf, &h_pd->h_ci_pd );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_PD );
			return status;
		}
	}

	pd_ioctl.in.h_ca = h_ca->obj.p_ci_ca->obj.hdl;
	pd_ioctl.in.type = pd_type;
	pd_ioctl.in.context = (ULONG_PTR)h_pd;

	cl_status = do_al_dev_ioctl( UAL_ALLOC_PD,
		&pd_ioctl.in, sizeof(pd_ioctl.in), &pd_ioctl.out, sizeof(pd_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(pd_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_ALLOC_PD IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = pd_ioctl.out.status;
		if( status == IB_SUCCESS )
			h_pd->obj.hdl = pd_ioctl.out.h_pd;
			
	}



	/* Post uvp call */
	if( pd_type != IB_PDT_ALIAS && h_uvp_ca && uvp_intf.post_allocate_pd )
	{
		uvp_intf.post_allocate_pd( h_uvp_ca, status,
			&h_pd->h_ci_pd, &pd_ioctl.out.umv_buf );
	}

	AL_EXIT( AL_DBG_PD );
	return status;
}


ib_api_status_t
ual_deallocate_pd(
	IN			ib_pd_handle_t				h_pd )
{
	ual_dealloc_pd_ioctl_t	pd_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf = h_pd->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_PD );

	/* Clear the pd_ioctl */
	cl_memclr( &pd_ioctl, sizeof(pd_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( h_pd->h_ci_pd && uvp_intf.pre_deallocate_pd )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_deallocate_pd( h_pd->h_ci_pd );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_PD );
			return status;
		}
	}

	pd_ioctl.in.h_pd = h_pd->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_DEALLOC_PD,
		&pd_ioctl.in, sizeof(pd_ioctl.in), &pd_ioctl.out, sizeof(pd_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(pd_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DEALLOC_PD IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = pd_ioctl.out.status;
	}

	/* Call vendor's post_close ca */
	if( h_pd->h_ci_pd && uvp_intf.post_deallocate_pd )
		uvp_intf.post_deallocate_pd( h_pd->h_ci_pd, status );


	AL_EXIT( AL_DBG_PD );
	return status;
}
