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


#include "hca_utils.h"
#include "mthca_dev.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_memory.tmh"
#endif

/*
 *	Memory Management Verbs.
 */

ib_api_status_t
mlnx_register_mr (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t				*p_mr_create,
	OUT			net32_t* const				p_lkey,
	OUT			net32_t* const				p_rkey,
	OUT			ib_mr_handle_t				*ph_mr,
	IN				boolean_t					um_call )
{
	ib_api_status_t 	status;
	int err;
	struct ib_mr *mr_p;
	struct ib_pd *ib_pd_p = (struct ib_pd *)h_pd;

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
	mr_p = ibv_reg_mr(ib_pd_p, map_qp_ibal_acl(p_mr_create->access_ctrl), 
		p_mr_create->vaddr, p_mr_create->length, 
		(ULONG_PTR)p_mr_create->vaddr, um_call, TRUE );
	if (IS_ERR(mr_p)) {
		err = PTR_ERR(mr_p);
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY,
			("ibv_reg_mr failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_reg_mr;
	}

	// results
	*p_lkey = mr_p->lkey;
	*p_rkey = cl_hton32( mr_p->rkey );
	if (ph_mr)	*ph_mr = (ib_mr_handle_t)mr_p;
	status = IB_SUCCESS;

err_reg_mr:
err_invalid_access:	
err_invalid_parm:
err_unsupported:
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %#x\n", status));
	}
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
	struct ib_mr *mr_p;
	struct ib_phys_buf *buffer_list;
	struct ib_pd *ib_pd_p = (struct ib_pd *)h_pd;

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
		mr_p = ibv_get_dma_mr( ib_pd_p,
			map_qp_ibal_acl(p_pmr_create->access_ctrl) );
	}
	else
		mr_p = ibv_reg_phys_mr(ib_pd_p, buffer_list, p_pmr_create->num_ranges, 
			map_qp_ibal_acl(p_pmr_create->access_ctrl), p_vaddr );
	if (IS_ERR(mr_p)) {
		err = PTR_ERR(mr_p);
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY,
			("mthca_reg_phys_mr failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_reg_phys_mr;
	}

	// results
	if (ph_mr)	*ph_mr = (ib_mr_handle_t)mr_p;
	*p_lkey = mr_p->lkey;
	*p_rkey = cl_hton32( mr_p->rkey );
	//NB:  p_vaddr was not changed
	status = IB_SUCCESS;

err_reg_phys_mr:
err_invalid_parm:
err_unsupported:
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
	
}

ib_api_status_t
mlnx_query_mr (
	IN		const	ib_mr_handle_t				h_mr,
		OUT			ib_mr_attr_t				*p_mr_query )
{
	UNREFERENCED_PARAMETER(h_mr);
	UNREFERENCED_PARAMETER(p_mr_query);
	HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,("mlnx_query_mr not implemented\n"));
	return IB_UNSUPPORTED;
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
	ib_api_status_t 	status;
	int err;

	HCA_ENTER(HCA_DBG_SHIM);

	// sanity checks
	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	} 

	// deregister	
	err = ibv_dereg_mr((struct ib_mr *)h_mr);
	if (err) {
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_MEMORY, 
			("mthca_dereg_mr failed (%d)", status));
		goto err_dereg_mr;
	}

	status = IB_SUCCESS;
	
err_dereg_mr:
err_unsupported:
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %#x\n", status));
	}
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
	ib_api_status_t 	status;
	int err;
	struct ib_fmr * fmr_p;
	struct ib_pd *ib_pd_p = (struct ib_pd *)h_pd;
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
	fmr_p = ibv_alloc_fmr(ib_pd_p, 
		map_qp_ibal_acl(p_fmr_create->access_ctrl), &fmr_attr);
	if (IS_ERR(fmr_p)) {
		err = PTR_ERR(fmr_p);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,
			("mthca_alloc_fmr failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc_fmr;
	}

	// results
	if (ph_fmr)	*ph_fmr = (mlnx_fmr_handle_t)fmr_p;
	status = IB_SUCCESS;

err_alloc_fmr:
err_invalid_parm:
err_unsupported:
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %#x\n", status));
	}
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
	ib_api_status_t 	status;
	struct ib_fmr *ib_fmr = (struct ib_fmr *)h_fmr;
	uint64_t vaddr = (*p_vaddr) & ~(PAGE_SIZE - 1);

	HCA_ENTER(HCA_DBG_MEMORY);

	// mapping	
	err = ibv_map_phys_fmr(ib_fmr, (u64*)page_list, list_len, (uint64_t)(ULONG_PTR)vaddr);
	if (err) {
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,
			("ibv_map_phys_fmr failed (%d) for mr %p\n",	err, h_fmr));
		goto err_dealloc_fmr;
	}

	// return the results
	*p_vaddr = vaddr;
	*p_lkey = ib_fmr->lkey;
	*p_rkey = cl_hton32( ib_fmr->rkey );
	
	status = IB_SUCCESS;
	
err_dealloc_fmr:
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
}



ib_api_status_t
mlnx_unmap_fmr (
	IN		const	mlnx_fmr_handle_t			*ph_fmr)
{
	ib_api_status_t 	status;
	int err;
	struct ib_fmr *ib_fmr = (struct ib_fmr *)*ph_fmr;
	struct list_head fmr_list;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	} 

	INIT_LIST_HEAD(&fmr_list);
	while(*ph_fmr)
	{
		ib_fmr = (struct ib_fmr*)*ph_fmr;
		list_add_tail(&ib_fmr->list, &fmr_list);
		ph_fmr ++;
	}

	err = ibv_unmap_fmr(&fmr_list);
	if (err) {
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_MEMORY  ,
			("ibv_unmap_fmr failed (%d) \n",	err));
		goto err_unmap_fmr;
	}

	status = IB_SUCCESS;
	
err_unmap_fmr:
err_unsupported:
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_MEMORY);
	return status;
	
	
}
	

	
ib_api_status_t
mlnx_dealloc_fmr (
	IN		const	mlnx_fmr_handle_t			h_fmr
	)
{
	ib_api_status_t 	status;
	int err;

	HCA_ENTER(HCA_DBG_MEMORY);

	// sanity checks
	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	} 

	
	// deregister	
	err = ibv_dealloc_fmr((struct ib_fmr *)h_fmr);
	if (err) {
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY  ,
			("ibv_dealloc_fmr failed (%d) for mr %p\n",err, h_fmr));
		goto err_dealloc_fmr;
	}

	status = IB_SUCCESS;
	
err_dealloc_fmr:
err_unsupported:
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,
			("completes with ERROR status %#x\n", status));
	}
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


void
mlnx_memory_if(
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
}

