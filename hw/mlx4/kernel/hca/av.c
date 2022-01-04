/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "av.tmh"
#endif

/* 
* Address Vector Management Verbs
*/
ib_api_status_t
mlnx_create_av (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t				*p_ib_av_attr,
		OUT			ib_av_handle_t				*ph_av,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err = 0;
	ib_api_status_t		status = IB_SUCCESS;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;
	struct ib_ah *p_ib_ah;
	struct ib_ah_attr ah_attr;
	struct ib_ucontext *p_uctx = NULL;

	HCA_ENTER(HCA_DBG_AV);

	if (p_umv_buf && p_umv_buf->command) {
		status = IB_UNSUPPORTED;
		goto err_nosys;
	}

	// fill parameters 
	err = to_av( p_ib_pd->device, p_ib_av_attr, &ah_attr );
	if (err) {
		status = errno_to_iberr(err);
		goto err_to_av;
	}

	// create AV
	p_ib_ah = p_ib_pd->device->create_ah(p_ib_pd, &ah_attr);
	if (IS_ERR(p_ib_ah)) {
		err = PTR_ERR(p_ib_ah);
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV ,("create_ah failed (%d)\n", err));
		goto err_create_ah;
	}

	// fill results
	p_ib_ah->device  = p_ib_pd->device;
	p_ib_ah->pd      = p_ib_pd;
	p_ib_ah->p_uctx = p_uctx;
	atomic_inc(&p_ib_pd->usecnt);
	HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_AV	,("PD%d use cnt %d, pd_handle %p, ctx %p \n", 
		((struct mlx4_ib_pd*)p_ib_pd)->pdn, p_ib_pd->usecnt, p_ib_pd, p_ib_pd->p_uctx));

	// return the result
	if (ph_av) *ph_av = (ib_av_handle_t)p_ib_ah;

	status = IB_SUCCESS;

err_create_ah:	
err_to_av:
err_nosys:	
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_AV);
	return status;
}

ib_api_status_t
mlnx_query_av (
	IN		const	ib_av_handle_t				h_av,
		OUT			ib_av_attr_t				*p_ib_av_attr,
		OUT			ib_pd_handle_t				*ph_pd,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_ah *p_ib_ah = (struct ib_ah *)h_av;
	struct ib_ah_attr ah_attr;
	UNUSED_PARAM(p_umv_buf);

	HCA_ENTER(HCA_DBG_AV);

	if (p_umv_buf && p_umv_buf->command) {
		status = IB_UNSUPPORTED;
		goto err_nosys;
	}

	// query AV
	err = p_ib_ah->device->query_ah(p_ib_ah, &ah_attr);
	if (err) {
		status = errno_to_iberr(err);
		goto err_query_ah;
	}

	// results
	if (ph_pd)
		*ph_pd = (ib_pd_handle_t)p_ib_ah->pd;
	err = from_av( p_ib_ah->device, NULL, &ah_attr, p_ib_av_attr );
	status = errno_to_iberr(err);
	
err_query_ah:
err_nosys:	
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_AV);
	return status;
}

ib_api_status_t
mlnx_modify_av (
	IN		const	ib_av_handle_t				h_av,
	IN		const	ib_av_attr_t				*p_ib_av_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	struct ib_ah_attr ah_attr;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_ah *p_ib_ah = (struct ib_ah *)h_av;
	UNUSED_PARAM(p_umv_buf);
	
	HCA_ENTER(HCA_DBG_AV);

	if (p_umv_buf && p_umv_buf->command) {
		status = IB_UNSUPPORTED;
		goto err_nosys;
	}

	// fill parameters 
	err = to_av( p_ib_ah->device, p_ib_av_attr, &ah_attr );
	if (err) {
		status = errno_to_iberr(err);
		goto err_to_av;
	}

	// modify AV
	if (p_ib_ah->device->modify_ah)
		err = p_ib_ah->device->modify_ah(p_ib_ah, &ah_attr);
	else
		err = -ENOSYS;
	status = errno_to_iberr(err);

err_to_av:
err_nosys:	
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_AV);
	return status;
}

ib_api_status_t
mlnx_destroy_av (
	IN		const	ib_av_handle_t				h_av)
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_ah *p_ib_ah = (struct ib_ah *)h_av;
	struct ib_pd *pd = p_ib_ah->pd;

	HCA_ENTER(HCA_DBG_AV);

	// destroy AV
	err = p_ib_ah->device->destroy_ah(p_ib_ah);
	if (!err) {
		atomic_dec(&pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_MEMORY ,("pdn %d, usecnt %d, pd_handle %p, ctx %p \n", 
			((struct mlx4_ib_pd*)pd)->pdn, pd->usecnt, pd, pd->p_uctx));
	}
	status = errno_to_iberr(err);

	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_AV);
	return status;
}


	

void
mlnx_av_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->create_av = mlnx_create_av;
	p_interface->query_av = mlnx_query_av;
	p_interface->modify_av = mlnx_modify_av;
	p_interface->destroy_av = mlnx_destroy_av;
}

