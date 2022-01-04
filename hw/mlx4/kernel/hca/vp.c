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
#include "vp.tmh"
#endif

static ib_api_status_t
mlnx_um_open(
	IN		const	ib_ca_handle_t				h_ca,
	IN				KPROCESSOR_MODE				mode,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf,
		OUT			ib_ca_handle_t* const		ph_um_ca )
{
	ib_api_status_t		status;
	mlnx_hca_t			*p_hca = (mlnx_hca_t *)h_ca;
	PHCA_FDO_DEVICE_DATA p_fdo = hca2fdo(p_hca);
	struct ib_device *p_ibdev = hca2ibdev(p_hca);
	struct ib_ucontext *p_uctx;
	struct ibv_get_context_resp *p_uresp;

	HCA_ENTER(HCA_DBG_SHIM);

	// sanity check
	ASSERT( p_umv_buf );
	if( !p_umv_buf->command )
	{ // no User Verb Provider
		p_uctx = cl_zalloc( sizeof(struct ib_ucontext) );
		if( !p_uctx )
		{
			status = IB_INSUFFICIENT_MEMORY;
			goto err_alloc_ucontext;
		}
		/* Copy the dev info. */
		p_uctx->device = p_ibdev;
		p_uctx->x.mode = mode;
		p_umv_buf->output_size = 0;
		status = IB_SUCCESS;
	}
	else
	{
	// sanity check
		if ( p_umv_buf->input_size < sizeof(struct ibv_get_context_req) ||
		     p_umv_buf->output_size < sizeof(struct ibv_get_context_resp) ||
		!p_umv_buf->p_inout_buf) {
		status = IB_INVALID_PARAMETER;
		goto err_inval_params;
	}

		status = ibv_um_open( p_ibdev, mode, p_umv_buf, &p_uctx );
	if (status != IB_SUCCESS) {
		goto end;
	}
	
	// fill more parameters for user (sanity checks are in mthca_alloc_ucontext) 
	p_uresp = (struct ibv_get_context_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
	p_uresp->vend_id		 = (uint32_t)p_fdo->p_bus_ib_ifc->pdev->ven_id;
	p_uresp->dev_id			 = (uint16_t)p_fdo->p_bus_ib_ifc->pdev->dev_id;
	p_uresp->max_qp_wr		 = hca2mdev(p_hca)->caps.max_wqes;
	p_uresp->max_cqe		 = hca2mdev(p_hca)->caps.max_cqes;
	p_uresp->max_sge		 = min( hca2mdev(p_hca)->caps.max_sq_sg,
		hca2mdev(p_hca)->caps.max_rq_sg );
	}

	// fill the rest of ib_ucontext_ex fields 
	atomic_set(&p_uctx->x.usecnt, 0);
	p_uctx->x.va = p_uctx->x.p_mdl = NULL;
	p_uctx->x.fw_if_open = FALSE;
	mutex_init( &p_uctx->x.mutex );

	// chain user context to the device
	spin_lock( &p_fdo->uctx_lock );
	cl_qlist_insert_tail( &p_fdo->uctx_list, &p_uctx->x.list_item );
	cl_atomic_inc(&p_fdo->usecnt);
	spin_unlock( &p_fdo->uctx_lock );
	
	// return the result
	if (ph_um_ca) *ph_um_ca = (ib_ca_handle_t)p_uctx;

	status = IB_SUCCESS;
	goto end;

err_inval_params:
err_alloc_ucontext:
end:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("completes with ERROR status %x\n", status));
	}
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}


static void
mlnx_um_close(
	IN				ib_ca_handle_t				h_ca,
	IN				ib_ca_handle_t				h_um_ca )
{
	struct ib_ucontext *p_uctx = (struct ib_ucontext *)h_um_ca;
	PHCA_FDO_DEVICE_DATA p_fdo = p_uctx->device->x.p_fdo;

	UNUSED_PARAM(h_ca);
	
	unmap_crspace_for_all(p_uctx);
	spin_lock( &p_fdo->uctx_lock );
	cl_qlist_remove_item( &p_fdo->uctx_list, &p_uctx->x.list_item );
	cl_atomic_dec(&p_fdo->usecnt);
	spin_unlock( &p_fdo->uctx_lock );
	if( !p_uctx->x.uar.kva)
		cl_free( h_um_ca );		// no User Verb Provider
	else 
		ibv_um_close(p_uctx);
#if 0
	// TODO: replace where pa_cash.c is found
	pa_cash_print();
#endif
	return;
}


ib_api_status_t
mlnx_local_mad (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	uint8_t						port_num,
	IN		const	ib_av_attr_t*					p_av_attr,
	IN		const	ib_mad_t					*p_mad_in,
	OUT		ib_mad_t					*p_mad_out )
{
	int err;
	ib_api_status_t		status = IB_SUCCESS;
	mlnx_hca_t			*p_hca = (mlnx_hca_t *)h_ca;
	PHCA_FDO_DEVICE_DATA p_fdo = hca2fdo(p_hca);
	struct ib_device *p_ibdev = p_fdo->p_bus_ib_ifc->p_ibdev;
	//TODO: do we need use flags (IB_MAD_IGNORE_MKEY, IB_MAD_IGNORE_BKEY) ?
	int mad_flags = 0;  
	//TODO: do we need use grh ?
	struct ib_grh *p_grh = NULL;
	ib_wc_t *p_wc = NULL;

	HCA_ENTER(HCA_DBG_MAD);

	// sanity checks
	if (port_num > 2) {
		status = IB_INVALID_PARAMETER;
		goto err_port_num;
	}

	if (p_av_attr){
		p_wc = cl_zalloc(sizeof(ib_wc_t));
		if(!p_wc){
			status =  IB_INSUFFICIENT_MEMORY ;
			goto err_wc_alloc;
		}
		//Copy part of the attributes need to fill the mad extended fields in mellanox devices
		p_wc->recv.ud.remote_lid = p_av_attr->dlid;
		p_wc->recv.ud.remote_sl  = p_av_attr->sl;
		p_wc->recv.ud.path_bits  = p_av_attr->path_bits;
		p_wc->recv.ud.recv_opt = p_av_attr->grh_valid ? IB_RECV_OPT_GRH_VALID : 0;

		if(p_wc->recv.ud.recv_opt & IB_RECV_OPT_GRH_VALID){
			p_grh = cl_zalloc(sizeof(struct _ib_grh));
			if(!p_grh){
				status =  IB_INSUFFICIENT_MEMORY ;
				goto err_grh_alloc;
			}
			p_grh->version_tclass_flow	= p_av_attr->grh.ver_class_flow;
			p_grh->hop_limit			= p_av_attr->grh.hop_limit;
			cl_memcpy( &p_grh->sgid, &p_av_attr->grh.src_gid, sizeof(p_grh->sgid) );
			cl_memcpy( &p_grh->dgid, &p_av_attr->grh.dest_gid, sizeof(p_grh->dgid) );
			// TODO: no direct analogue in IBAL (seems like it is from rmpp)
			p_grh->paylen				= 0;
			p_grh->next_hdr				= 0;
		}
			

	}

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_MAD, 
		("MAD: Class %02x, Method %02x, Attr %02x, HopPtr %d, HopCnt %d, \n",
		(uint32_t)((ib_smp_t *)p_mad_in)->mgmt_class, 
		(uint32_t)((ib_smp_t *)p_mad_in)->method, 
		(uint32_t)((ib_smp_t *)p_mad_in)->attr_id, 
		(uint32_t)((ib_smp_t *)p_mad_in)->hop_ptr,
		(uint32_t)((ib_smp_t *)p_mad_in)->hop_count));

	// process mad
	err = p_ibdev->process_mad( p_ibdev, mad_flags, (uint8_t)port_num, 
		p_wc, p_grh, (struct ib_mad*)p_mad_in, (struct ib_mad*)p_mad_out);
	if (!err) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_MAD, 
			("MAD failed:\n\tClass 0x%x\n\tMethod 0x%x\n\tAttr 0x%x",
			p_mad_in->mgmt_class, p_mad_in->method, p_mad_in->attr_id ));
		status = IB_ERROR;
		goto err_process_mad;
	}
	
	if( (p_mad_in->mgmt_class == IB_MCLASS_SUBN_DIR ||
		p_mad_in->mgmt_class == IB_MCLASS_SUBN_LID) &&
		p_mad_in->attr_id == IB_MAD_ATTR_PORT_INFO )
	{
		ib_port_info_t	*p_pi_in, *p_pi_out;

		if( p_mad_in->mgmt_class == IB_MCLASS_SUBN_DIR )
		{
			p_pi_in = (ib_port_info_t*)
				ib_smp_get_payload_ptr( (ib_smp_t*)p_mad_in );
			p_pi_out = (ib_port_info_t*)
				ib_smp_get_payload_ptr( (ib_smp_t*)p_mad_out );
		}
		else
		{
			p_pi_in = (ib_port_info_t*)(p_mad_in + 1);
			p_pi_out = (ib_port_info_t*)(p_mad_out + 1);
		}

		/* Work around FW bug 33958 */
		p_pi_out->subnet_timeout &= 0x7F;
		if( p_mad_in->method == IB_MAD_METHOD_SET )
			p_pi_out->subnet_timeout |= (p_pi_in->subnet_timeout & 0x80);
	}

	/* Modify direction for Direct MAD */
	if ( p_mad_in->mgmt_class == IB_MCLASS_SUBN_DIR )
		p_mad_out->status |= IB_SMP_DIRECTION;


err_process_mad:
	if(p_grh)
		cl_free(p_grh);
err_grh_alloc:
	if(p_wc)
		cl_free(p_wc);
err_wc_alloc:
err_port_num:	
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_MAD,
			("completes with ERROR status %x\n", status));
	}
	HCA_EXIT(HCA_DBG_MAD);
	return status;
}
	

void
setup_ci_interface(
	IN		const	ib_net64_t					ca_guid,
	IN	OUT			ci_interface_t				*p_interface )
{
	cl_memclr(p_interface, sizeof(*p_interface));

	/* Guid of the CA. */
	p_interface->guid = ca_guid;

	/* Version of this interface. */
	p_interface->version = VERBS_VERSION;

	/* UVP name */
	cl_memcpy( p_interface->libname, mlnx_uvp_lib_name, MAX_LIB_NAME);

	HCA_PRINT(TRACE_LEVEL_VERBOSE  , HCA_DBG_SHIM  ,("UVP filename %s\n", p_interface->libname));

	/* The real interface. */
	mlnx_pd_if(p_interface);
	p_interface->um_open_ca = mlnx_um_open;
	p_interface->um_close_ca = mlnx_um_close;
	p_interface->vendor_call = fw_access_ctrl;
	mlnx_ca_if(p_interface);
	mlnx_av_if(p_interface);
	mlnx_srq_if(p_interface);
	mlnx_qp_if(p_interface);
	mlnx_cq_if(p_interface);
	mlnx_mr_if(p_interface);
	mlnx_direct_if(p_interface);
	mlnx_mcast_if(p_interface);
	p_interface->local_mad = mlnx_local_mad;

	return;
}


