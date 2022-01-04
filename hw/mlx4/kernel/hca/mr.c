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

#include "precomp.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mr.tmh"
#endif

/*
 *	Memory Management Verbs.
 */

ib_api_status_t
mlnx_register_mr_remap (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t				*p_mr_create,
	IN		const	uint64_t					mapaddr,
	OUT			net32_t* const					p_lkey,
	OUT			net32_t* const					p_rkey,
	OUT			ib_mr_handle_t					*ph_mr,
	IN				boolean_t					um_call )
{
	ib_api_status_t 	status;
	int err;
	struct ib_mr *p_ib_mr;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;

	HCA_ENTER(HCA_DBG_MEMORY);

	UNREFERENCED_PARAMETER(um_call);

	// sanity checks
	if( !cl_is_blockable() ) {
		status = IB_UNSUPPORTED;
		goto err_unsupported;
	} 
	if (!p_mr_create || 0 == p_mr_create->length) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_MEMORY,
			("invalid attributes\n"));
		status = IB_INVALID_PARAMETER;
		goto err_invalid_parm; 
	}
	if( BYTE_OFFSET( p_mr_create->vaddr ) != BYTE_OFFSET( mapaddr ) )
	{
		HCA_PRINT(TRACE_LEVEL_WARNING, HCA_DBG_MEMORY,
			("mapaddr offset != vaddr offset\n"));
		status = IB_INVALID_PARAMETER;
		goto err_invalid_parm;
	}

	/*
	 * Local write permission is required if remote write or
	 * remote atomic permission is also requested.
	 */
	if (p_mr_create->access_ctrl & (IB_AC_RDMA_WRITE | IB_AC_ATOMIC) &&
		!(p_mr_create->access_ctrl & IB_AC_LOCAL_WRITE)) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_MEMORY,
			("invalid access rights\n"));
		status = IB_INVALID_PERMISSION;
		goto err_invalid_access; 
	}

	// register mr 
	p_ib_mr = ibv_reg_mr(p_ib_pd, (ULONG_PTR)p_mr_create->vaddr, 
		p_mr_create->length, mapaddr, 
		to_qp_acl(p_mr_create->access_ctrl));
	if (IS_ERR(p_ib_mr)) {
		err = PTR_ERR(p_ib_mr);
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY,
			("ibv_reg_mr failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_reg_mr;
	}

	// results
	*p_lkey = p_ib_mr->lkey;
	*p_rkey = cl_hton32( p_ib_mr->rkey );
	*ph_mr = (ib_mr_handle_t)p_ib_mr;
	status = IB_SUCCESS;

err_reg_mr:
err_invalid_access:	
err_invalid_parm:
err_unsupported:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}

ib_api_status_t
mlnx_register_mr (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t				*p_mr_create,
	OUT				net32_t* const				p_lkey,
	OUT				net32_t* const				p_rkey,
	OUT				ib_mr_handle_t				*ph_mr,
	IN				boolean_t					um_call )
{
	ib_api_status_t 	status;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if (!p_mr_create) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_MEMORY,
			("invalid attributes\n"));
		status = IB_INVALID_PARAMETER;
		goto err_invalid_parm; 
	}

	status = mlnx_register_mr_remap(
		h_pd, p_mr_create, (ULONG_PTR)p_mr_create->vaddr, p_lkey, p_rkey, ph_mr, um_call );

err_invalid_parm:
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}

ib_api_status_t
mlnx_register_mdl (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t				*p_mr_create,
	IN				MDL                         *p_mdl,
	OUT				net32_t* const				p_lkey,
	OUT				net32_t* const				p_rkey,
	OUT				ib_mr_handle_t				*ph_mr )
{
	ib_api_status_t 	status;
	int err;
	struct ib_mr *p_ib_mr;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if( !cl_is_blockable() ) {
		status = IB_UNSUPPORTED;
		goto err_unsupported;
	} 
	if (!p_mr_create || 0 == p_mr_create->length) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_MEMORY,
			("invalid attributes\n"));
		status = IB_INVALID_PARAMETER;
		goto err_invalid_parm; 
	}

	/*
	 * Local write permission is required if remote write or
	 * remote atomic permission is also requested.
	 */
	if (p_mr_create->access_ctrl & (IB_AC_RDMA_WRITE | IB_AC_ATOMIC) &&
		!(p_mr_create->access_ctrl & IB_AC_LOCAL_WRITE)) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_MEMORY,
			("invalid access rights\n"));
		status = IB_INVALID_PERMISSION;
		goto err_invalid_access; 
	}

	// register mr 
	p_ib_mr = ibv_reg_mdl(p_ib_pd, p_mdl, 
		p_mr_create->length, to_qp_acl(p_mr_create->access_ctrl));
	if (IS_ERR(p_ib_mr)) {
		err = PTR_ERR(p_ib_mr);
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY,
			("ibv_reg_mdl failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_reg_mr;
	}

	// results
	*p_lkey = p_ib_mr->lkey;
	*p_rkey = cl_hton32( p_ib_mr->rkey );
	*ph_mr = (ib_mr_handle_t)p_ib_mr;
	status = IB_SUCCESS;

err_reg_mr:
err_invalid_access:
err_invalid_parm:
err_unsupported:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}

ib_api_status_t
mlnx_register_pmr (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_phys_create_t* const		p_pmr_create,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr )
{
	ib_api_status_t		status;
	int err;
	struct ib_mr *p_ib_mr;
	struct ib_phys_buf *buffer_list;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if( !cl_is_blockable() ) {
		status = IB_UNSUPPORTED;
		goto err_unsupported;
	}	
	if (!p_vaddr || !p_pmr_create ||
		0 == p_pmr_create->length ) {
		status = IB_INVALID_PARAMETER;
		goto err_invalid_parm; 
	}

	// prepare parameters
	buffer_list = (void*)p_pmr_create->range_array;
	//NB: p_pmr_create->buf_offset is not used, i.e. supposed that region is page-aligned
	//NB: p_pmr_create->hca_page_size is not used, i.e. supposed it is always the same
	
	// register pmr	
	if (p_pmr_create->length == (uint64_t)-1i64)
	{
		p_ib_mr = ib_get_dma_mr( p_ib_pd,
			to_qp_acl(p_pmr_create->access_ctrl) );
	}
	else
		p_ib_mr = ib_reg_phys_mr(p_ib_pd, buffer_list, p_pmr_create->num_ranges, 
			to_qp_acl(p_pmr_create->access_ctrl), p_vaddr );
	if (IS_ERR(p_ib_mr)) {
		err = PTR_ERR(p_ib_mr);
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY,
			("ib_reg_phys_mr failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_reg_phys_mr;
	}

	// results
	if (ph_mr)	*ph_mr = (ib_mr_handle_t)p_ib_mr;
	*p_lkey = p_ib_mr->lkey;
	*p_rkey = cl_hton32( p_ib_mr->rkey );
	//NB:  p_vaddr was not changed
	status = IB_SUCCESS;

err_reg_phys_mr:
err_invalid_parm:
err_unsupported:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %x\n", status));

	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
	
}

ib_api_status_t
mlnx_query_mr (
	IN		const	ib_mr_handle_t				h_mr,
		OUT			ib_mr_attr_t				*p_mr_query )
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_mr *p_ib_mr = (struct ib_mr *)h_mr;
	struct ib_mr_attr mr_attr;
	UNREFERENCED_PARAMETER(p_mr_query);

	HCA_ENTER(HCA_DBG_MEMORY);

	err = p_ib_mr->device->query_mr ?
		p_ib_mr->device->query_mr(p_ib_mr, &mr_attr) : -ENOSYS;
	status = errno_to_iberr(err);

	if (err) {
		// TODO: convert struct ib_mr_attr to ib_mr_attr_t
	}

	if (status != IB_SUCCESS)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("ib_query_mr failed with status %x\n", status));

	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}


ib_api_status_t
mlnx_modify_mr (
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mem_modify_req,
	IN		const	ib_mr_create_t				*p_mr_create,
		OUT			uint32_t					*p_lkey,
		OUT			uint32_t					*p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL,
	IN				boolean_t					um_call )
{
	UNREFERENCED_PARAMETER(h_mr);
	UNREFERENCED_PARAMETER(mem_modify_req);
	UNREFERENCED_PARAMETER(p_mr_create);
	UNREFERENCED_PARAMETER(p_lkey);
	UNREFERENCED_PARAMETER(p_rkey);
	UNREFERENCED_PARAMETER(h_pd);
	UNREFERENCED_PARAMETER(um_call);
	HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,("mlnx_modify_mr not implemented\n"));
	return IB_UNSUPPORTED;
}


ib_api_status_t
mlnx_modify_pmr (
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mem_modify_req,
	IN		const	ib_phys_create_t* const		p_pmr_create,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			uint32_t* const				p_lkey,
		OUT			uint32_t* const				p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL )
{
	UNREFERENCED_PARAMETER(h_mr);
	UNREFERENCED_PARAMETER(mem_modify_req);
	UNREFERENCED_PARAMETER(p_pmr_create);
	UNREFERENCED_PARAMETER(p_vaddr);
	UNREFERENCED_PARAMETER(p_lkey);
	UNREFERENCED_PARAMETER(p_rkey);
	UNREFERENCED_PARAMETER(h_pd);
	HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,("mlnx_modify_pmr not implemented\n"));
	return IB_UNSUPPORTED;
}

ib_api_status_t
mlnx_register_smr (
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_access_t					access_ctrl,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr,
	IN				boolean_t					um_call )
{
	UNREFERENCED_PARAMETER(h_mr);
	UNREFERENCED_PARAMETER(h_pd);
	UNREFERENCED_PARAMETER(access_ctrl);
	UNREFERENCED_PARAMETER(p_vaddr);
	UNREFERENCED_PARAMETER(p_lkey);
	UNREFERENCED_PARAMETER(p_rkey);
	UNREFERENCED_PARAMETER(ph_mr);
	UNREFERENCED_PARAMETER(um_call);
	HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,("mlnx_register_smr not implemented\n"));
	return IB_UNSUPPORTED;
}

ib_api_status_t
mlnx_deregister_mr (
	IN		const	ib_mr_handle_t				h_mr)
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_mr *p_ib_mr = (struct ib_mr *)h_mr;

	HCA_ENTER(HCA_DBG_SHIM);

	// sanity checks
	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	}

	// deregister	
	err = ib_dereg_mr(p_ib_mr);
	status = errno_to_iberr(err);

err_unsupported:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("ib_dereg_mr failed with status %x\n", status));

	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}

ib_api_status_t
mlnx_alloc_fmr(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	mlnx_fmr_create_t* const	p_fmr_create,
		OUT			mlnx_fmr_handle_t*	const	ph_fmr
	)
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_fmr * p_ib_fmr;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;
	struct ib_fmr_attr fmr_attr;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if( !cl_is_blockable() ) {
		status = IB_UNSUPPORTED;
		goto err_unsupported;
	}
	if (!p_fmr_create ) {
		status = IB_INVALID_PARAMETER;
		goto err_invalid_parm; 
	}
	// TODO: check Max remap in AL

	// prepare parameters
	RtlZeroMemory(&fmr_attr, sizeof(struct ib_fmr_attr));
	fmr_attr.max_maps = p_fmr_create->max_maps;
	fmr_attr.max_pages = p_fmr_create->max_pages;
	fmr_attr.page_shift = p_fmr_create->page_size;

	// register mr 
	p_ib_fmr = p_ib_pd->device->alloc_fmr(p_ib_pd, 
		to_qp_acl(p_fmr_create->access_ctrl), &fmr_attr);
	if (IS_ERR(p_ib_fmr)) {
		err = PTR_ERR(p_ib_fmr);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,
			("ib_alloc_fmr failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc_fmr;
	}
	else {
		p_ib_fmr->device = p_ib_pd->device;
		p_ib_fmr->pd     = p_ib_pd;
		atomic_inc(&p_ib_pd->usecnt);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_MEMORY ,("PD%d use cnt %d \n", 
			((struct mlx4_ib_pd*)p_ib_pd)->pdn, p_ib_pd->usecnt));
	}

	// results
	if (ph_fmr)
		*ph_fmr = (mlnx_fmr_handle_t)p_ib_fmr;

err_alloc_fmr:
err_invalid_parm:
err_unsupported:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %x\n", status));

	HCA_EXIT(HCA_DBG_MEMORY);
	return status;

}

ib_api_status_t
mlnx_map_phys_fmr (
	IN		const	mlnx_fmr_handle_t			h_fmr,
	IN		const	uint64_t* const				page_list,
	IN		const	int							list_len,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey
	)
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_fmr *p_ib_fmr = (struct ib_fmr *)h_fmr;
	uint64_t vaddr = (*p_vaddr) & ~(PAGE_SIZE - 1);

	HCA_ENTER(HCA_DBG_MEMORY);

	// mapping	
	err = ib_map_phys_fmr(p_ib_fmr, (u64*)page_list, list_len, (uint64_t)(ULONG_PTR)vaddr);
	status = errno_to_iberr(err);

	// return the results
	*p_vaddr = vaddr;
	*p_lkey = p_ib_fmr->lkey;
	*p_rkey = cl_hton32( p_ib_fmr->rkey );
	
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("ibv_map_phys_fmr failed with status %x\n", status));

	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}



ib_api_status_t
mlnx_unmap_fmr (
	IN		const	mlnx_fmr_handle_t			*ph_fmr)
{
	int err;
	struct list_head fmr_list;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_fmr *p_ib_fmr = (struct ib_fmr *)*ph_fmr;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	}

	INIT_LIST_HEAD(&fmr_list);
	while(*ph_fmr) {
		list_add_tail(&p_ib_fmr->list, &fmr_list);
		p_ib_fmr = (struct ib_fmr *)*++ph_fmr;
	}

	if (list_empty(&fmr_list))
		goto done;

	p_ib_fmr = list_entry(fmr_list.Flink, struct ib_fmr, list);
	err = p_ib_fmr->device->unmap_fmr(&fmr_list);
	status = errno_to_iberr(err);

err_unsupported:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("ibv_unmap_fmr failed with status %x\n", status));

done:
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
	
	
}


ib_api_status_t
mlnx_dealloc_fmr (
	IN		const	mlnx_fmr_handle_t			h_fmr
	)
{
	int err;
	ib_api_status_t status = IB_SUCCESS;
	struct ib_fmr *p_ib_fmr = (struct ib_fmr *)h_fmr;
	struct ib_pd *pd = p_ib_fmr->pd;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	}

	// deregister	
	err = p_ib_fmr->device->dealloc_fmr(p_ib_fmr);
	if (!err)
		atomic_dec(&pd->usecnt);
	status = errno_to_iberr(err);

err_unsupported:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("ibv_dealloc_fmr failed with status %x\n", status));

	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
		
}



/*
*	Memory Window Verbs.
*/

ib_api_status_t
mlnx_create_mw (
	IN		const	ib_pd_handle_t				h_pd,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mw_handle_t				*ph_mw,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	UNREFERENCED_PARAMETER(h_pd);
	UNREFERENCED_PARAMETER(p_rkey);
	UNREFERENCED_PARAMETER(ph_mw);
	UNREFERENCED_PARAMETER(p_umv_buf);
	HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,("mlnx_create_mw not implemented\n"));
	return IB_UNSUPPORTED;
}

ib_api_status_t
mlnx_query_mw (
	IN		const	ib_mw_handle_t				h_mw,
		OUT			ib_pd_handle_t				*ph_pd,
		OUT			net32_t* const				p_rkey,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	UNREFERENCED_PARAMETER(h_mw);
	UNREFERENCED_PARAMETER(ph_pd);
	UNREFERENCED_PARAMETER(p_rkey);
	UNREFERENCED_PARAMETER(p_umv_buf);
	HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,("mlnx_query_mw not implemented\n"));
	return IB_UNSUPPORTED;
}

ib_api_status_t
mlnx_destroy_mw (
	IN		const	ib_mw_handle_t				h_mw)
{
	UNREFERENCED_PARAMETER(h_mw);
	HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,("mlnx_destroy_mw not implemented\n"));
	return IB_UNSUPPORTED;
}

ib_api_status_t
mlnx_alloc_fast_reg_mr (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	int							max_page_list_len,
	OUT			net32_t* const					p_lkey,
	OUT			net32_t* const					p_rkey,
	OUT 		ib_mr_handle_t					*ph_mr )
{
	ib_api_status_t 	status;
	int err;
	struct ib_mr *p_ib_mr;
	struct ib_pd *p_ib_pd = (struct ib_pd *)h_pd;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if( !cl_is_blockable() ) {
		status = IB_UNSUPPORTED;
		goto err_unsupported;
	} 

	// alloc mr 
	p_ib_mr = ib_alloc_fast_reg_mr(p_ib_pd, max_page_list_len);
	if (IS_ERR(p_ib_mr)) {
		err = PTR_ERR(p_ib_mr);
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY,
			("ib_alloc_fast_reg_mr failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_reg_mr;
	}

	// results
	if (p_lkey)
		*p_lkey = p_ib_mr->lkey;
	if (p_rkey)
		*p_rkey = cl_hton32( p_ib_mr->rkey );
	if (ph_mr)	*ph_mr = (ib_mr_handle_t)p_ib_mr;
	status = IB_SUCCESS;

err_reg_mr:
err_unsupported:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}


ib_api_status_t
mlnx_alloc_fast_reg_page_list (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	int							max_page_list_len,
	OUT 		ib_fast_reg_page_list_t			**ph_fr )
{
	ib_api_status_t 	status;
	int err;
	ib_fast_reg_page_list_t *p_ib_fr;
	mlnx_hca_t *p_hca;
	struct ib_device *p_ibdev;
	p_hca = (mlnx_hca_t *)h_ca;
	p_ibdev = hca2ibdev(p_hca);


	HCA_ENTER(HCA_DBG_MEMORY);

	// alloc page list 
	p_ib_fr = ib_alloc_fast_reg_page_list(p_ibdev, max_page_list_len);
	if (IS_ERR(p_ib_fr)) {
		err = PTR_ERR(p_ib_fr);
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY,
			("ib_alloc_fast_reg_page_list failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc;
	}

	// results
	if (ph_fr)	
		*ph_fr = (ib_fast_reg_page_list_t*)p_ib_fr;
	status = IB_SUCCESS;

err_alloc:
	if (status != IB_SUCCESS) 
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %x\n", status));
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}

void
mlnx_free_fast_reg_page_list (
	IN				ib_fast_reg_page_list_t		*h_fr )
{
	ib_fast_reg_page_list_t *page_list = (ib_fast_reg_page_list_t *)h_fr;
	struct ib_device *device = page_list->device;

	HCA_ENTER(HCA_DBG_MEMORY);

	device->x.free_fast_reg_page_list(page_list);

	HCA_EXIT(HCA_DBG_MEMORY);
}



void
mlnx_mr_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->register_mr = mlnx_register_mr;
	p_interface->register_pmr = mlnx_register_pmr;
	p_interface->query_mr = mlnx_query_mr;
	p_interface->modify_mr = mlnx_modify_mr;
	p_interface->modify_pmr = mlnx_modify_pmr;
	p_interface->register_smr = mlnx_register_smr;
	p_interface->deregister_mr = mlnx_deregister_mr;

	p_interface->alloc_mlnx_fmr = mlnx_alloc_fmr;
	p_interface->map_phys_mlnx_fmr = mlnx_map_phys_fmr;
	p_interface->unmap_mlnx_fmr = mlnx_unmap_fmr;
	p_interface->dealloc_mlnx_fmr = mlnx_dealloc_fmr;

	p_interface->create_mw = mlnx_create_mw;
	p_interface->query_mw = mlnx_query_mw;
	p_interface->destroy_mw = mlnx_destroy_mw;

	p_interface->alloc_fast_reg_mr = mlnx_alloc_fast_reg_mr;
	p_interface->alloc_fast_reg_page_list = mlnx_alloc_fast_reg_page_list;
	p_interface->free_fast_reg_page_list = mlnx_free_fast_reg_page_list;

	p_interface->register_mr_remap = mlnx_register_mr_remap;
    p_interface->register_mdl = mlnx_register_mdl;
}

void
mlnx_mr_if_livefish(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->register_pmr = mlnx_register_pmr;
	p_interface->deregister_mr = mlnx_deregister_mr;
}


