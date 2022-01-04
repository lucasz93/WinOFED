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
#include "mlnx_uvp.h"
#include "mx_abi.h"

#include "mlnx_ual_main.h"
#if defined(EVENT_TRACING)
#include "mlnx_ual_av.tmh"
#endif

static void
__post_create_av (
	IN		const ib_pd_handle_t	h_uvp_pd,
	IN		ib_api_status_t			ioctl_status,
	IN OUT	ib_av_handle_t			*ph_uvp_av,
	IN OUT	ci_umv_buf_t			*p_umv_buf);


ib_api_status_t
map_itom_av_attr (
	IN		const ib_av_attr_t	*p_av_attr,
	OUT		struct ibv_ah_attr		*p_attr)
{
	ib_api_status_t	status = IB_SUCCESS;

	p_attr->sl = p_av_attr->sl;
	p_attr->port_num = p_av_attr->port_num;
	p_attr->dlid = CL_NTOH16 (p_av_attr->dlid);
	p_attr->src_path_bits = p_av_attr->path_bits; // PATH:

	//TODO: how static_rate is coded ?
	p_attr->static_rate   =
		(p_av_attr->static_rate == IB_PATH_RECORD_RATE_10_GBS ? 0 : 3);
		
	/* For global destination or Multicast address:*/
	if (p_av_attr->grh_valid) {
		p_attr->is_global = TRUE;
		p_attr->grh.hop_limit		 = p_av_attr->grh.hop_limit;
		ib_grh_get_ver_class_flow( p_av_attr->grh.ver_class_flow, NULL,
			&p_attr->grh.traffic_class, &p_attr->grh.flow_label );
		p_attr->grh.sgid_index = p_av_attr->grh.resv2;
		cl_memcpy (p_attr->grh.dgid.raw, p_av_attr->grh.dest_gid.raw, 
			sizeof (IB_gid_t));
	}else{
		p_attr->is_global = FALSE;
	}

	return status;
} 

static ib_api_status_t
__pre_create_av (
	IN		const ib_pd_handle_t	h_uvp_pd,
	IN		const ib_av_attr_t		*p_av_attr,
	IN OUT	ci_umv_buf_t			*p_umv_buf,
	   OUT	ib_av_handle_t			*ph_uvp_av)
{
	int err;
	struct mthca_ah *ah;
	struct ibv_ah_attr attr;
	struct ibv_create_ah *p_create_av;
	ib_api_status_t status = IB_SUCCESS;
	size_t size = max( sizeof(struct ibv_create_ah), sizeof(struct ibv_create_ah_resp) );
	mlnx_ual_pd_info_t *p_pd = (mlnx_ual_pd_info_t *)h_uvp_pd;
	mlnx_ual_hobul_t *p_hobul = p_pd->p_hobul;

	UNREFERENCED_PARAMETER(ph_uvp_av);
	
	UVP_ENTER(UVP_DBG_AV);

	CL_ASSERT(p_umv_buf);

	// convert parameters 
	cl_memset( &attr, 0, sizeof(attr));
	status = map_itom_av_attr (p_av_attr, &attr);
	if(status != IB_SUCCESS ) 
		goto end;

	// allocate Ah object
	ah = cl_zalloc( sizeof *ah );
	if( !ah ) {
		status = IB_INSUFFICIENT_MEMORY;
		goto end;
	}

	// fill AH partly
	ah->h_uvp_pd = h_uvp_pd;
	cl_memcpy( &ah->av_attr, p_av_attr, sizeof(ah->av_attr) );

	// try to create AV
	err = mthca_alloc_av(to_mpd(p_pd->ibv_pd), &attr, ah, NULL);
	if (err) {
		UVP_PRINT(TRACE_LEVEL_ERROR ,UVP_DBG_AV , ("mthca_alloc_av failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc_av;
	}

	// allocate parameters
	p_umv_buf->p_inout_buf = (ULONG_PTR)cl_zalloc( size );
	if( !p_umv_buf->p_inout_buf )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto err_mem;
	}

	// fill the parameters
	p_umv_buf->input_size = sizeof(struct ibv_create_ah);
	p_umv_buf->output_size = sizeof(struct ibv_create_ah_resp);
	p_umv_buf->command = TRUE;
	p_create_av = (struct ibv_create_ah *)p_umv_buf->p_inout_buf;
	p_create_av->user_handle = (uint64_t)(ULONG_PTR)ah;
	if (ah->in_kernel) {
		struct mthca_ah_page *page = ah->page;
		p_create_av->mr.start = (uint64_t)(ULONG_PTR)page->buf;
		p_create_av->mr.length = g_page_size;
		p_create_av->mr.hca_va = (uint64_t)(ULONG_PTR)page->buf;
		p_create_av->mr.pd_handle	 = p_pd->ibv_pd->handle;
		p_create_av->mr.pdn = to_mpd(p_pd->ibv_pd)->pdn;
		p_create_av->mr.access_flags = 0;	//local read
		status = IB_SUCCESS;
	}
	else {
		__post_create_av(h_uvp_pd, IB_SUCCESS, ph_uvp_av, p_umv_buf);
		status = IB_VERBS_PROCESSING_DONE;
	}
	goto end;

err_mem:	
	mthca_free_av(ah);
err_alloc_av:	
	cl_free(ah);
end:
	UVP_EXIT(UVP_DBG_AV);
	return status;
}


static void
__post_create_av (
	IN		const ib_pd_handle_t	h_uvp_pd,
	IN		ib_api_status_t			ioctl_status,
	IN OUT		ib_av_handle_t		*ph_uvp_av,
	IN OUT	ci_umv_buf_t			*p_umv_buf)
{
	int err;
	struct mthca_ah *ah;
	struct mthca_ah_page *page;
	struct ibv_create_ah_resp *p_resp;
	ib_api_status_t status = IB_SUCCESS;
	mlnx_ual_pd_info_t *p_pd = (mlnx_ual_pd_info_t *)h_uvp_pd;

	UVP_ENTER(UVP_DBG_AV);

	CL_ASSERT(p_umv_buf);

	p_resp = (struct ibv_create_ah_resp *)p_umv_buf->p_inout_buf;
	ah = (struct mthca_ah *)(ULONG_PTR)p_resp->user_handle;

	if (IB_SUCCESS == ioctl_status) {

		if (!mthca_is_memfree(p_pd->ibv_pd->context)) {
			page = ah->page;
			if (ah->in_kernel) {
				// fill mr parameters
				page->mr.handle = p_resp->mr.mr_handle;
				page->mr.lkey = p_resp->mr.lkey;
				page->mr.rkey = p_resp->mr.rkey;
				page->mr.pd = p_pd->ibv_pd;
				page->mr.context = p_pd->ibv_pd->context;
			}
			ah->key  = page->mr.lkey;
		}
		*ph_uvp_av = (ib_av_handle_t)ah;
	}
	else {
		mthca_free_av(ah);
		cl_free(ah);
	}
	goto end;
	
end:	
	if (p_resp)
		cl_free( p_resp );
	UVP_EXIT(UVP_DBG_AV);
}

static ib_api_status_t
__pre_query_av (
	IN	const	ib_av_handle_t		h_uvp_av,
	IN OUT		ci_umv_buf_t		*p_umv_buf )
{
	UNREFERENCED_PARAMETER(h_uvp_av);
	UNREFERENCED_PARAMETER(p_umv_buf);
	UVP_ENTER(UVP_DBG_AV);
	UVP_EXIT(UVP_DBG_AV);
	return IB_VERBS_PROCESSING_DONE;
}


static void
__post_query_av (
	IN		const	ib_av_handle_t				h_uvp_av,
	IN				ib_api_status_t				ioctl_status,
	IN	OUT			ib_av_attr_t				*p_addr_vector,
	IN	OUT			ib_pd_handle_t				*ph_pd,
	IN	OUT			ci_umv_buf_t				*p_umv_buf)
{
	struct mthca_ah *ah = (struct mthca_ah *)h_uvp_av;
	UNREFERENCED_PARAMETER(p_umv_buf);

	UVP_ENTER(UVP_DBG_AV);
	CL_ASSERT(p_umv_buf);
	CL_ASSERT(p_addr_vector);

	if (ioctl_status == IB_SUCCESS)
	{
		cl_memcpy (p_addr_vector, &ah->av_attr, sizeof (ib_av_attr_t));
		if (ph_pd)
			*ph_pd = (ib_pd_handle_t)ah->h_uvp_pd;
	}
	
	UVP_EXIT(UVP_DBG_AV);
}

void mthca_set_av_params( struct mthca_ah *ah_p, struct ibv_ah_attr *ah_attr );

static ib_api_status_t
__pre_modify_av (
	IN		const ib_av_handle_t	h_uvp_av,
	IN		const ib_av_attr_t		*p_addr_vector,
	IN OUT	ci_umv_buf_t			*p_umv_buf)
{
	ib_api_status_t status ;
	struct mthca_ah *mthca_ah = (struct mthca_ah *)h_uvp_av;
	mlnx_ual_pd_info_t *p_pd_info;
	mlnx_ual_hobul_t *p_hobul;
	struct ibv_ah_attr attr;

	UNREFERENCED_PARAMETER(p_umv_buf);
	
	UVP_ENTER(UVP_DBG_AV);
	
	CL_ASSERT(p_umv_buf);
		
	p_pd_info = mthca_ah->h_uvp_pd;
	CL_ASSERT (p_pd_info);

	p_hobul = p_pd_info->p_hobul;
	CL_ASSERT (p_hobul);

	status = map_itom_av_attr (p_addr_vector, &attr);
	if(status != IB_SUCCESS)	return status;
	
	mthca_set_av_params( mthca_ah, &attr);
	cl_memcpy (&mthca_ah->av_attr, p_addr_vector, sizeof(ib_av_attr_t));
	
	UVP_EXIT(UVP_DBG_AV);

	return IB_VERBS_PROCESSING_DONE;
}

static void
__post_modify_av (
	IN		const ib_av_handle_t	h_uvp_av,
	IN		ib_api_status_t		ioctl_status,
	IN OUT	ci_umv_buf_t		*p_umv_buf)
{
	UVP_ENTER(UVP_DBG_AV);
	UVP_EXIT(UVP_DBG_AV);
}


static ib_api_status_t
__pre_destroy_av (
	IN		const ib_av_handle_t		h_uvp_av)
{
	ib_api_status_t status ;
	struct mthca_ah *mthca_ah = (struct mthca_ah *)h_uvp_av;
	UVP_ENTER(UVP_DBG_AV);
	if (mthca_ah->in_kernel)
		status = IB_SUCCESS;
	else {
		mthca_free_av(mthca_ah);
		cl_free(mthca_ah);
		status = IB_VERBS_PROCESSING_DONE;
	}
	UVP_EXIT(UVP_DBG_AV);
	return status;
}

static void
__post_destroy_av (
	IN		const ib_av_handle_t		h_uvp_av,
	IN		ib_api_status_t			ioctl_status)
{
	struct mthca_ah *mthca_ah = (struct mthca_ah *)h_uvp_av;

	UVP_ENTER(UVP_DBG_AV);
	CL_ASSERT (h_uvp_av);

	if (IB_SUCCESS == ioctl_status) {
		mthca_free_av(mthca_ah);
		cl_free(mthca_ah);
	}

	UVP_EXIT(UVP_DBG_AV);
	return;
}

void
mlnx_get_av_interface (
	IN OUT	uvp_interface_t		*p_uvp )
{

	CL_ASSERT(p_uvp);

	/*
	 * Address Vector Management Verbs
	 */
	p_uvp->pre_create_av  = __pre_create_av;
	p_uvp->post_create_av = __post_create_av;
	p_uvp->pre_query_av   = __pre_query_av;
	p_uvp->post_query_av  = __post_query_av;
	p_uvp->pre_modify_av   = __pre_modify_av;
	p_uvp->post_modify_av  = __post_modify_av;
	p_uvp->pre_destroy_av  = __pre_destroy_av;
	p_uvp->post_destroy_av = __post_destroy_av;

}

