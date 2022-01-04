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
#include "mlnx_ual_mrw.tmh"
#endif



static ib_api_status_t
__pre_register_mr (
    IN		const ib_pd_handle_t		h_uvp_pd,
    IN		const ib_mr_create_t		*p_mr_create,
    IN OUT	ci_umv_buf_t			*p_umv_buf,
       OUT	ib_mr_handle_t				*ph_uvp_mr)
{
    UNREFERENCED_PARAMETER(ph_uvp_mr);
	
    UVP_ENTER(UVP_DBG_SHIM);
    CL_ASSERT(p_umv_buf);
    p_umv_buf->p_inout_buf = 0;
    p_umv_buf->input_size = 0;
    p_umv_buf->output_size = 0;

    UVP_EXIT(UVP_DBG_SHIM);
    return IB_SUCCESS;
}


static void
__post_register_mr (
    IN		const ib_pd_handle_t	h_uvp_pd,
    IN		ib_api_status_t			ioctl_status,
    IN		const uint32_t			*p_lkey,
    IN		const uint32_t			*p_rkey,
    IN OUT	const ib_mr_handle_t	*ph_uvp_mr,
    IN OUT	ci_umv_buf_t			*p_umv_buf)
{
    UVP_ENTER(UVP_DBG_SHIM);
    UVP_EXIT(UVP_DBG_SHIM);
    return;
}


static ib_api_status_t
__pre_query_mr (
    IN		const ib_mr_handle_t	h_uvp_mr,
    IN OUT	ci_umv_buf_t			*p_umv_buf)
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
__post_query_mr (
    IN		const ib_mr_handle_t	h_uvp_mr,
    IN		ib_api_status_t			ioctl_status,
    IN		const ib_mr_attr_t		*p_mr_query,
    IN OUT	ci_umv_buf_t			*p_umv_buf)
{
    UVP_ENTER(UVP_DBG_SHIM);
    UVP_EXIT(UVP_DBG_SHIM);
    return;
}


static ib_api_status_t
__pre_modify_mr (
    IN		const ib_mr_handle_t	h_uvp_mr,
    IN		const ib_pd_handle_t	h_uvp_pd	OPTIONAL,
    IN		const ib_mr_mod_t		mr_mod_mask,
    IN		const ib_mr_create_t		*p_mr_create	OPTIONAL,
    IN OUT	ci_umv_buf_t			*p_umv_buf)
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
__post_modify_mr (
    IN		const ib_mr_handle_t	h_uvp_mr,
    IN		const ib_pd_handle_t	h_uvp_pd	OPTIONAL,
    IN		ib_api_status_t			ioctl_status,
    IN		const uint32_t			*p_lkey,
    IN		const uint32_t			*p_rkey,
    IN OUT	ci_umv_buf_t			*p_umv_buf)
{
    UVP_ENTER(UVP_DBG_SHIM);
    UVP_EXIT(UVP_DBG_SHIM);
    return;
}


static ib_api_status_t
__pre_register_smr (
    IN		const ib_pd_handle_t	h_uvp_pd,
    IN		const ib_mr_handle_t	h_uvp_mr,
    IN		const ib_access_t		access_ctrl,
    IN		void				*p_vaddr,
    IN OUT	ci_umv_buf_t			*p_umv_buf)
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
__post_register_smr (
    IN		const ib_pd_handle_t	h_uvp_pd,
    IN		const ib_mr_handle_t	h_uvp_mr,
    IN		ib_api_status_t			ioctl_status,
    IN		const void			*p_vaddr,
    IN		const uint32_t			*p_lkey,
    IN		const uint32_t			*p_rkey,
    OUT		const ib_mr_handle_t	*ph_uvp_smr,
    IN OUT 	ci_umv_buf_t			*p_umv_buf)
{
    UVP_ENTER(UVP_DBG_SHIM);
    UVP_EXIT(UVP_DBG_SHIM);
    return;
}


static ib_api_status_t
__pre_deregister_mr (
    IN		const ib_mr_handle_t		h_uvp_mr,
    IN OUT 	ci_umv_buf_t			*p_umv_buf)
{
    UVP_ENTER(UVP_DBG_SHIM);
    UVP_EXIT(UVP_DBG_SHIM);
    return IB_SUCCESS;
}


static void
__post_deregister_mr (
    IN		const ib_mr_handle_t		h_uvp_mr,
    IN OUT 	ci_umv_buf_t			*p_umv_buf)
{
    UVP_ENTER(UVP_DBG_SHIM);
    UVP_EXIT(UVP_DBG_SHIM);
    return;
}

void
mlnx_get_mrw_interface (
    IN OUT	uvp_interface_t		*p_uvp )
{
    UVP_ENTER(UVP_DBG_SHIM);

    CL_ASSERT(p_uvp);

    /*
     * Memory Management Verbs
     */
//    p_uvp->pre_register_mr    = NULL;
//    p_uvp->post_register_mr   = NULL;
//    p_uvp->pre_query_mr       = NULL;
//    p_uvp->post_query_mr      = NULL;
//    p_uvp->pre_deregister_mr  = NULL;
//    p_uvp->post_deregister_mr = NULL;
//    p_uvp->pre_modify_mr      = NULL;
//    p_uvp->post_modify_mr     = NULL;
//    p_uvp->pre_register_smr   = NULL;
//    p_uvp->post_register_smr  = NULL;

    /*
     * Memory Window Verbs
     */
	p_uvp->pre_create_mw	= NULL;	// __pre_create_mw
	p_uvp->post_create_mw = NULL;	// __post_create_mw
	p_uvp->pre_query_mw 	= NULL;	// __pre_query_mw
	p_uvp->post_query_mw	= NULL;	// __post_query_mw
	p_uvp->pre_destroy_mw = NULL;	// __pre_destroy_mw
	p_uvp->post_destroy_mw = NULL;	// __post_destroy_mw

    /* register_pmr is not supported in user-mode */

    UVP_EXIT(UVP_DBG_SHIM);
}

