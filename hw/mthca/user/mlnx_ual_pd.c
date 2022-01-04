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


#include "mt_l2w.h"
#include "mlnx_ual_main.h"
#include "mlnx_uvp.h"
#include "mx_abi.h"

#if defined(EVENT_TRACING)
#include "mlnx_ual_pd.tmh"
#endif

static ib_api_status_t
__pre_allocate_pd (
	IN		const ib_ca_handle_t	h_uvp_ca,
	IN OUT	ci_umv_buf_t			*p_umv_buf,
	    OUT	ib_pd_handle_t			*ph_uvp_pd)
{
	ib_api_status_t status = IB_SUCCESS;

	UNREFERENCED_PARAMETER(ph_uvp_pd);
	
	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(p_umv_buf);

	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_zalloc( sizeof(struct ibv_alloc_pd_resp) );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_memory;
	}
	p_umv_buf->input_size = p_umv_buf->output_size = sizeof(struct ibv_alloc_pd_resp);
	p_umv_buf->command = TRUE;
	
err_memory:
		UVP_EXIT(UVP_DBG_SHIM);
		return status;
}


static void
__post_allocate_pd (
	IN				ib_ca_handle_t				h_uvp_ca,
	IN				ib_api_status_t				ioctl_status,
	IN	OUT			ib_pd_handle_t				*ph_uvp_pd,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ibv_alloc_pd_resp *p_resp;
	struct ibv_pd *ibv_pd;
	mlnx_ual_hobul_t *p_hobul = (mlnx_ual_hobul_t *)((void *)h_uvp_ca);
	mlnx_ual_pd_info_t *p_new_pd;

	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(p_hobul);
	CL_ASSERT(p_umv_buf);
	p_resp = (struct ibv_alloc_pd_resp *)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == ioctl_status) {

		/* allocate ibv_pd */
		ibv_pd = p_hobul->ibv_ctx->ops.alloc_pd(p_hobul->ibv_ctx, p_resp);
		if (IS_ERR(ibv_pd)) {
			err = PTR_ERR(ibv_pd);
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_SHIM , ("mthca_alloc_pd failed (%d)\n", err));
			status = errno_to_iberr(err);
			goto err_alloc_pd;
		}

		/* allocate pd */
		p_new_pd = (mlnx_ual_pd_info_t *)cl_zalloc( sizeof(mlnx_ual_pd_info_t) );
		if( !p_new_pd ) {
			status = IB_INSUFFICIENT_MEMORY;
			goto err_memory;
		}

		/* return results */
		p_new_pd->ibv_pd = ibv_pd;
		p_new_pd->p_hobul = p_hobul;
		*ph_uvp_pd = (ib_pd_handle_t)p_new_pd;
	}
	goto end;
	
err_memory: 
	p_hobul->ibv_ctx->ops.dealloc_pd(ibv_pd);
err_alloc_pd:
end:	
	if (p_resp)
		cl_free( p_resp );
	UVP_EXIT(UVP_DBG_SHIM);
	return;
}


static ib_api_status_t
__pre_deallocate_pd (
	IN		const ib_pd_handle_t		h_uvp_pd)
{
	mlnx_ual_pd_info_t *p_pd_info = (mlnx_ual_pd_info_t *)((void *)h_uvp_pd);
	UVP_ENTER(UVP_DBG_SHIM);
	CL_ASSERT(p_pd_info);
	UVP_EXIT(UVP_DBG_SHIM);
	return IB_SUCCESS;
}


static void
__post_deallocate_pd (
	IN		const ib_pd_handle_t	h_uvp_pd,
	IN		ib_api_status_t			ioctl_status )
{
	int err;
	mlnx_ual_pd_info_t *p_pd_info = (mlnx_ual_pd_info_t *)((void *)h_uvp_pd);

	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(p_pd_info || p_pd_info->ibv_pd);

	if (IB_SUCCESS == ioctl_status) {
		err = p_pd_info->p_hobul->ibv_ctx->ops.dealloc_pd( p_pd_info->ibv_pd );
		if (err) 
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_SHIM , ("mthca_alloc_pd failed (%d)\n", err));

		cl_free (p_pd_info);
	}
	UVP_EXIT(UVP_DBG_SHIM);
}

void
mlnx_get_pd_interface (
	IN OUT	uvp_interface_t		*p_uvp )
{
	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(p_uvp);

	/*
	 * Protection Domain
	 */
	p_uvp->pre_allocate_pd    = __pre_allocate_pd;
	p_uvp->post_allocate_pd   = __post_allocate_pd;
	p_uvp->pre_deallocate_pd  = __pre_deallocate_pd;
	p_uvp->post_deallocate_pd = __post_deallocate_pd;

	UVP_EXIT(UVP_DBG_SHIM);
}


