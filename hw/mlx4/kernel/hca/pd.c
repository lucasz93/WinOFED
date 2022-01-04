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

#include "precomp.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "pd.tmh"
#endif


/* Protection domains */

ib_api_status_t
mlnx_allocate_pd (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_pd_type_t				type,
		OUT			ib_pd_handle_t				*ph_pd,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t		status;
	struct ib_device *p_ibdev;
	struct ib_ucontext *p_uctx;
	struct ib_pd *p_ib_pd;
	struct ib_udata udata;
	struct ibv_alloc_pd_resp *p_resp = NULL;
	int err;

	//TODO: how are we to use it ?
	UNREFERENCED_PARAMETER(type);
	
	HCA_ENTER(HCA_DBG_PD);

	if( p_umv_buf ) {
		p_uctx = (struct ib_ucontext *)h_ca;
		p_ibdev = p_uctx->device;

		if( p_umv_buf->command ) {
			// sanity checks 
			if (p_umv_buf->output_size < sizeof(struct ibv_alloc_pd_resp) ||
				!p_umv_buf->p_inout_buf) {
				status = IB_INVALID_PARAMETER;
				goto err_alloc_pd;
			}

			// prepare user parameters
			p_resp = (struct ibv_alloc_pd_resp*)(ULONG_PTR)p_umv_buf->p_inout_buf;
			INIT_UDATA(&udata, NULL, &p_resp->pdn, 
				0, sizeof(p_resp->pdn));
		}
		else {
            // Discard PDN output, so use status as temp storage.
            INIT_UDATA(&udata, NULL, &status, 0, sizeof(status));
		}
	}
	else {
		mlnx_hca_t *p_hca = (mlnx_hca_t *)h_ca;
		p_ibdev = hca2ibdev(p_hca);
		p_uctx = NULL;
	}
	
	// create PD
	p_ib_pd = p_ibdev->alloc_pd(p_ibdev, p_uctx, &udata);

	if (IS_ERR(p_ib_pd)){
		err = PTR_ERR(p_ib_pd);
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_PD,
			("ibv_alloc_pd failed (%#x)\n", status));
		goto err_alloc_pd;
	}
	else {
		p_ib_pd->device  = p_ibdev;
		p_ib_pd->p_uctx = p_uctx;
		atomic_set(&p_ib_pd->usecnt, 0);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_PD ,("pdn %d, usecnt %d, pd_handle %p, ctx %p \n", 
			((struct mlx4_ib_pd*)p_ib_pd)->pdn, p_ib_pd->usecnt, p_ib_pd, p_ib_pd->p_uctx));
	}

	// complete user response
	if (p_umv_buf && p_umv_buf->command) {
		p_resp->pd_handle = (u64)(ULONG_PTR)p_ib_pd;
	}
	
	// return the result
	if (ph_pd) *ph_pd = (ib_pd_handle_t)p_ib_pd;

	status = IB_SUCCESS;
	
err_alloc_pd:	
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	HCA_EXIT(HCA_DBG_PD);
	return status;
}

ib_api_status_t
mlnx_deallocate_pd (
	IN				ib_pd_handle_t				h_pd)
{
	ib_api_status_t		status;
	int err;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;

	HCA_ENTER( HCA_DBG_PD);

	HCA_PRINT(TRACE_LEVEL_INFORMATION,HCA_DBG_PD,
		("pcs %p\n", PsGetCurrentProcess()));
	
	if (atomic_read(&p_ib_pd->usecnt)) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_PD,
			("resources are not released (pdn %d, cnt %d)\n", 
			((struct mlx4_ib_pd*)p_ib_pd)->pdn, p_ib_pd->usecnt));
		status = IB_RESOURCE_BUSY;
		goto err_dealloc_pd;
	}		

	err = p_ib_pd->device->dealloc_pd(p_ib_pd);
	if (err) {
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_PD
			,("ibv_dealloc_pd failed (%#x)\n", status));
		goto err_dealloc_pd;
	}
	status = IB_SUCCESS;

err_dealloc_pd:
	HCA_EXIT(HCA_DBG_PD);
	return status;
}


void
mlnx_pd_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->allocate_pd = mlnx_allocate_pd;
	p_interface->deallocate_pd = mlnx_deallocate_pd;
}

