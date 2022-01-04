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
#include "al.h"
#include "ual_support.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_mr.tmh"
#endif

#include "al_mr.h"
#include "al_pd.h"
#include "al_res_mgr.h"


ib_api_status_t
ual_reg_mem(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t* const		p_mr_create,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN	OUT			ib_mr_handle_t				h_mr )
{
	ual_reg_mem_ioctl_t		mr_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status = IB_ERROR;

	AL_ENTER( AL_DBG_MR );

	mr_ioctl.in.h_pd = h_pd->obj.hdl;
	mr_ioctl.in.mem_create = *p_mr_create;
	mr_ioctl.in.mem_create.vaddr_padding = (ULONG_PTR)p_mr_create->vaddr;

	cl_status = do_al_dev_ioctl( UAL_REG_MR,
		&mr_ioctl.in, sizeof(mr_ioctl.in), &mr_ioctl.out, sizeof(mr_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mr_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_REG_MR IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = mr_ioctl.out.status;
		if( status == IB_SUCCESS )
		{
			h_mr->obj.hdl = mr_ioctl.out.h_mr;
			*p_lkey = mr_ioctl.out.lkey;
			*p_rkey = mr_ioctl.out.rkey;
		}
	}

	AL_EXIT( AL_DBG_MR );
	return status;
}


ib_api_status_t
ual_dereg_mr(
	IN			ib_mr_handle_t				h_mr )
{
	ual_dereg_mr_ioctl_t	mr_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_MR );

	/* Clear the mr_ioctl */
	cl_memclr( &mr_ioctl, sizeof(mr_ioctl) );

	mr_ioctl.in.h_mr = h_mr->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_DEREG_MR,
		&mr_ioctl.in, sizeof(mr_ioctl.in), &mr_ioctl.out, sizeof(mr_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mr_ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_DEREG_MR IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_MR );
	return mr_ioctl.out.status;
}


ib_api_status_t
ual_modify_mr(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mr_mod_mask,
	IN		const	ib_mr_create_t* const		p_mr_create OPTIONAL,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL )
{
	ual_rereg_mem_ioctl_t	mr_ioctl;
	uintn_t					bytes_ret;
	uint64_t				h_al_pd	= AL_INVALID_HANDLE;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_MR );

	/* Clear the mr_ioctl */
	cl_memclr( &mr_ioctl, sizeof(mr_ioctl) );

	if( h_pd )
		h_al_pd = h_pd->obj.hdl;

	mr_ioctl.in.h_mr = h_mr->obj.hdl;
	mr_ioctl.in.mem_mod_mask = mr_mod_mask;
	if( p_mr_create )
		mr_ioctl.in.mem_create = *p_mr_create;

	mr_ioctl.in.h_pd = h_al_pd;

	cl_status = do_al_dev_ioctl( UAL_MODIFY_MR,
		&mr_ioctl.in, sizeof(mr_ioctl.in), &mr_ioctl.out, sizeof(mr_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mr_ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_MODIFY_MR IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}
	else if( mr_ioctl.out.status == IB_SUCCESS )
	{
		*p_lkey = mr_ioctl.out.lkey;
		*p_rkey = mr_ioctl.out.rkey;
	}

	AL_EXIT( AL_DBG_MR );
	return mr_ioctl.out.status;
}


ib_api_status_t
ual_query_mr(
	IN			ib_mr_handle_t				h_mr,
		OUT		ib_mr_attr_t*				p_mr_attr )
{
	ual_query_mr_ioctl_t	mr_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_MR );

	/* Clear the mr_ioctl */
	cl_memclr( &mr_ioctl, sizeof(mr_ioctl) );

	mr_ioctl.in.h_mr = h_mr->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_QUERY_MR,
		&mr_ioctl.in, sizeof(mr_ioctl.in), &mr_ioctl.out, sizeof(mr_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mr_ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_QUERY_MR IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}
	else if( mr_ioctl.out.status == IB_SUCCESS )
	{
		*p_mr_attr = mr_ioctl.out.attr;
		p_mr_attr->h_pd_padding = 0;
		p_mr_attr->h_pd = CONTAINING_RECORD( h_mr->obj.p_parent_obj, ib_pd_t, obj );
	}

	AL_EXIT( AL_DBG_MR );
	return mr_ioctl.out.status;
}


ib_api_status_t
ual_reg_shared(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_access_t					access_ctrl,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN	OUT			ib_mr_handle_t				h_new_mr )
{
	ual_reg_shared_ioctl_t	mr_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_MR );

	/* Clear the mr_ioctl */
	cl_memclr( &mr_ioctl, sizeof(mr_ioctl) );

	mr_ioctl.in.h_mr = h_mr->obj.hdl;
	mr_ioctl.in.h_pd = h_pd->obj.hdl;
	mr_ioctl.in.access_ctrl = access_ctrl;
	mr_ioctl.in.vaddr = *p_vaddr;

	cl_status = do_al_dev_ioctl( UAL_REG_SHARED,
		&mr_ioctl.in, sizeof(mr_ioctl.in), &mr_ioctl.out, sizeof(mr_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mr_ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_REG_SHARED IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}
	else if( mr_ioctl.out.status == IB_SUCCESS )
	{
		h_new_mr->obj.hdl = mr_ioctl.out.h_new_mr;
		*p_lkey = mr_ioctl.out.lkey;
		*p_rkey = mr_ioctl.out.rkey;
		*p_vaddr = mr_ioctl.out.vaddr;
	}

	AL_EXIT( AL_DBG_MR );
	return mr_ioctl.out.status;
}


ib_api_status_t
ib_reg_shmid(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_shmid_t					shmid,
	IN		const	ib_mr_create_t* const		p_mr_create,
		OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr )
{
	ual_reg_shmid_ioctl_t			mr_ioctl;
	uintn_t							bytes_ret;
	cl_status_t						cl_status;
	ib_api_status_t					status;
	ib_mr_handle_t					h_mr;

	AL_ENTER( AL_DBG_MR );

	if( AL_OBJ_INVALID_HANDLE( h_pd, AL_OBJ_TYPE_H_PD ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PD_HANDLE\n") );
		return IB_INVALID_PD_HANDLE;
	}
	if( !p_mr_create || !p_vaddr || !p_lkey || !p_rkey || !ph_mr )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Allocate a user mode memory handle */
	h_mr = alloc_mr();
	if( !h_mr )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("unable to allocate memory handle\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Attach this under the pd */
	attach_al_obj( &h_pd->obj, &h_mr->obj );

	/* Clear the mr_ioctl */
	cl_memclr( &mr_ioctl, sizeof(mr_ioctl) );

	cl_memcpy( mr_ioctl.in.shmid, shmid, sizeof(ib_shmid_t) );
	mr_ioctl.in.mr_create = *p_mr_create;
	mr_ioctl.in.h_pd = h_pd->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_REG_SHMID,
		&mr_ioctl.in, sizeof(mr_ioctl.in), &mr_ioctl.out, sizeof(mr_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(mr_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_REG_SHMID IOCTL returned %s.\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = mr_ioctl.out.status;
	}

	if( IB_SUCCESS == status )
	{
		*p_vaddr = mr_ioctl.out.vaddr;
		*p_lkey = mr_ioctl.out.lkey;
		*p_rkey = mr_ioctl.out.rkey;

		/* Store the kernel handle and return the user handle */
		h_mr->obj.hdl = mr_ioctl.out.h_mr;
		*ph_mr = h_mr;

		/* Release the reference taken in alloc_mr. */
		deref_al_obj( &h_mr->obj );
	}
	else
	{
		h_mr->obj.pfn_destroy( &h_mr->obj, NULL );
	}

	AL_EXIT( AL_DBG_MR );
	return status;
}

