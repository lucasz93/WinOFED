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
#include "mt_l2w.h"
#include "mlnx_uvp.h"
#include "mlnx_uvp_verbs.h"
#include "mx_abi.h"

#if defined(EVENT_TRACING)
#include "mlnx_ual_ca.tmh"
#endif

extern uint32_t	mlnx_dbg_lvl;

static ib_api_status_t
__pre_open_ca (
	IN		const	ib_net64_t					ca_guid,
	IN	OUT			ci_umv_buf_t				*p_umv_buf,
		OUT			ib_ca_handle_t				*ph_uvp_ca)
{
	ib_api_status_t  status = IB_SUCCESS;

	UNREFERENCED_PARAMETER(ph_uvp_ca);
		
	UVP_ENTER(UVP_DBG_SHIM);
	if( p_umv_buf )
	{
		p_umv_buf->p_inout_buf = (ULONG_PTR)cl_zalloc( sizeof(struct ibv_get_context_resp) );
		if( !p_umv_buf->p_inout_buf )
		{
			status = IB_INSUFFICIENT_MEMORY;
			goto err_memory;
		}
		p_umv_buf->input_size = p_umv_buf->output_size = sizeof(struct ibv_get_context_resp);
		p_umv_buf->command = TRUE;
	}
err_memory:	
	UVP_EXIT(UVP_DBG_SHIM);
	return IB_SUCCESS;
}


static ib_api_status_t
__post_open_ca (
	IN				const ib_net64_t			ca_guid,
	IN				ib_api_status_t				ioctl_status,
	IN	OUT			ib_ca_handle_t				*ph_uvp_ca,
	IN				ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t  status = ioctl_status;
	mlnx_ual_hobul_t *new_ca;
	struct ibv_get_context_resp *p_resp;
	struct ibv_context * ibvcontext;
	int err;

	UVP_ENTER(UVP_DBG_SHIM);

	p_resp = (struct ibv_get_context_resp *)p_umv_buf->p_inout_buf;

	if (IB_SUCCESS == status) {
		/* allocate ibv context */
		ibvcontext = mthca_alloc_context(p_resp);
		if (IS_ERR(ibvcontext)) {
			err = PTR_ERR(ibvcontext);
			UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_SHIM ,("mthca_alloc_context failed (%d)\n", err));
			status = errno_to_iberr(err);
			goto err_alloc_context;
		}

		/* allocate mthca context */
		new_ca = (mlnx_ual_hobul_t *)cl_zalloc( sizeof(mlnx_ual_hobul_t) );
		if( !new_ca ) {
			status = IB_INSUFFICIENT_MEMORY;
			goto err_memory;
		}

		/* return results */
		new_ca->ibv_ctx = ibvcontext;
		*ph_uvp_ca = (ib_ca_handle_t)new_ca;
	}

err_memory:	
err_alloc_context:
	if (p_resp)
		cl_free( p_resp );
	UVP_EXIT(UVP_DBG_SHIM);
	return status;
}


static ib_api_status_t
__pre_modify_ca (
    IN		ib_ca_handle_t				h_uvp_ca,
    IN		uint8_t						port_num,
    IN		ib_ca_mod_t					ca_mod,
    IN		const ib_port_attr_mod_t*	p_port_attr_mod)
{
    UVP_ENTER(UVP_DBG_SHIM);
    UVP_EXIT(UVP_DBG_SHIM);
    return IB_SUCCESS;
}


static void
__post_modify_ca (
	IN		ib_ca_handle_t			h_uvp_ca,
	IN		ib_api_status_t			ioctl_status)
{
	UVP_ENTER(UVP_DBG_SHIM);
	UVP_EXIT(UVP_DBG_SHIM);
}


static ib_api_status_t
__pre_close_ca (
	IN		ib_ca_handle_t		h_uvp_ca)
{
	UVP_ENTER(UVP_DBG_SHIM);
	UVP_EXIT(UVP_DBG_SHIM);
	return IB_SUCCESS;
}


static ib_api_status_t
__post_close_ca (
	IN		ib_ca_handle_t		h_uvp_ca,
	IN		ib_api_status_t		ioctl_status )
{
	mlnx_ual_hobul_t *p_hobul = (mlnx_ual_hobul_t *)((void*)h_uvp_ca);

	UVP_ENTER(UVP_DBG_SHIM);

	CL_ASSERT(p_hobul);

	if (IB_SUCCESS == ioctl_status) {
		if (p_hobul->ibv_ctx) {
			mthca_free_context(p_hobul->ibv_ctx);
			p_hobul->ibv_ctx = NULL;
		}

		cl_free(p_hobul);
	}
	
	UVP_EXIT(UVP_DBG_SHIM);
	return IB_SUCCESS;
}

void
mlnx_get_ca_interface (
	IN OUT	uvp_interface_t		*p_uvp )
{
	CL_ASSERT(p_uvp);

	/*
	 * HCA Access Verbs
	 */
	p_uvp->pre_open_ca  = __pre_open_ca;
	p_uvp->post_open_ca = __post_open_ca;


	p_uvp->pre_query_ca  = NULL;
	p_uvp->post_query_ca = NULL;

	p_uvp->pre_modify_ca  = NULL;
	p_uvp->post_modify_ca = NULL;

	p_uvp->pre_close_ca  = __pre_close_ca;
	p_uvp->post_close_ca = __post_close_ca;

}


