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


#include "hca_driver.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_verbs.tmh"
#endif
#include "mthca_dev.h"
#include "ib_cache.h"
#include "mx_abi.h"
#include "mt_pa_cash.h"



// Local declarations
ib_api_status_t
mlnx_query_qp (
	IN		const	ib_qp_handle_t				h_qp,
		OUT			ib_qp_attr_t				*p_qp_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf );

/* 
* CA Access Verbs
*/
ib_api_status_t
mlnx_open_ca (
	IN		const	ib_net64_t					ca_guid, // IN  const char *                ca_name,
	IN		const	ci_async_event_cb_t			pfn_async_event_cb,
	IN		const	void*const					ca_context,
		OUT			ib_ca_handle_t				*ph_ca)
{
	mlnx_hca_t				*p_hca;
	ib_api_status_t status = IB_NOT_FOUND;
	struct ib_device *ib_dev;

	HCA_ENTER(HCA_DBG_SHIM);
	HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_SHIM,
		("context 0x%p\n", ca_context));

	// find CA object
	p_hca = mlnx_hca_from_guid( ca_guid );
	if( !p_hca ) {
		if (status != IB_SUCCESS) 
		{
			HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM,
			("completes with ERROR status IB_NOT_FOUND\n"));
		}
		HCA_EXIT(HCA_DBG_SHIM);
		return IB_NOT_FOUND;
	}

	ib_dev = &p_hca->mdev->ib_dev;

	HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_SHIM,
		("context 0x%p\n", ca_context));
	if (pfn_async_event_cb) {
	status = mlnx_hobs_set_cb(&p_hca->hob,
		pfn_async_event_cb,
		ca_context);
	if (IB_SUCCESS != status) {
		goto err_set_cb;
	}
	}
	
	//TODO: do we need something for kernel users ?

	// Return pointer to HOB object
	if (ph_ca) *ph_ca = &p_hca->hob;
	status =  IB_SUCCESS;

//err_mad_cache:
err_set_cb:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}

static void
mlnx_register_event_handler (
	IN		const	ib_ca_handle_t				h_ca,
	IN				ci_event_handler_t*			p_reg)
{
	mlnx_hob_t *hob_p = (mlnx_hob_t *) h_ca;
	KIRQL irql;

	KeAcquireSpinLock(&hob_p->event_list_lock, &irql);
	InsertTailList(&hob_p->event_list, &p_reg->entry);
	KeReleaseSpinLock(&hob_p->event_list_lock, irql);
}

static void
mlnx_unregister_event_handler (
	IN		const	ib_ca_handle_t				h_ca,
	IN				ci_event_handler_t*			p_reg)
{
	mlnx_hob_t *hob_p = (mlnx_hob_t *) h_ca;
	KIRQL irql;

	KeAcquireSpinLock(&hob_p->event_list_lock, &irql);
	RemoveEntryList(&p_reg->entry);
	KeReleaseSpinLock(&hob_p->event_list_lock, irql);
}

ib_api_status_t
mlnx_query_ca (
	IN		const	ib_ca_handle_t				h_ca,
		OUT			ib_ca_attr_t				*p_ca_attr,
	IN	OUT			uint32_t					*p_byte_count,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t		status;
	uint32_t			size, required_size;
	uint8_t			port_num, num_ports;
	uint32_t			num_gids, num_pkeys;
	uint32_t			num_page_sizes = 1; // TBD: what is actually supported
	uint8_t				*last_p;
	struct ib_device_attr props;
	struct ib_port_attr  *hca_ports = NULL;
	int i;
	
	mlnx_hob_t			*hob_p = (mlnx_hob_t *)h_ca;
	struct ib_device *ib_dev = IBDEV_FROM_HOB( hob_p );
	int err;
	
	HCA_ENTER(HCA_DBG_SHIM);

	// sanity checks
	if( p_umv_buf && p_umv_buf->command ) {
			HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM ,("User mode is not supported yet\n"));
			p_umv_buf->status = status = IB_UNSUPPORTED;
			goto err_user_unsupported;
	}

	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	}

	if (NULL == p_byte_count) {
		status = IB_INVALID_PARAMETER;
		goto err_byte_count;
	}

	// query the device
	err = mthca_query_device(ib_dev, &props );
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM, 
			("ib_query_device failed (%d)\n",err));
		status = errno_to_iberr(err);
		goto err_query_device;
	}
	
	// alocate arrary for port properties
	num_ports = ib_dev->phys_port_cnt;   /* Number of physical ports of the HCA */             
	if (NULL == (hca_ports = cl_zalloc( num_ports * sizeof *hca_ports))) {
		HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM, ("Failed to cl_zalloc ports array\n"));
		status = IB_INSUFFICIENT_MEMORY;
		goto err_alloc_ports;
	}

	// start calculation of ib_ca_attr_t full size
	num_gids = 0;
	num_pkeys = 0;
	required_size = PTR_ALIGN(sizeof(ib_ca_attr_t)) +
		PTR_ALIGN(sizeof(uint32_t) * num_page_sizes) +
		PTR_ALIGN(sizeof(ib_port_attr_t) * num_ports)+
		PTR_ALIGN(MTHCA_BOARD_ID_LEN)+
		PTR_ALIGN(sizeof(uplink_info_t));	/* uplink info */
	
	// get port properties
	for (port_num = 0; port_num <= end_port(ib_dev) - start_port(ib_dev); ++port_num) {
		// request
		err = mthca_query_port(ib_dev, port_num + start_port(ib_dev), &hca_ports[port_num]);
		if (err) {
			HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM, ("ib_query_port failed(%d) for port %d\n",err, port_num));
			status = errno_to_iberr(err);
			goto err_query_port;
		}

		// calculate GID table size
		num_gids  = hca_ports[port_num].gid_tbl_len;
		size = PTR_ALIGN(sizeof(ib_gid_t)  * num_gids);
		required_size += size;

		// calculate pkeys table size
		num_pkeys = hca_ports[port_num].pkey_tbl_len;
		size = PTR_ALIGN(sizeof(uint16_t) * num_pkeys);
		required_size += size;
	}

	// resource sufficience check
	if (NULL == p_ca_attr || *p_byte_count < required_size) {
		*p_byte_count = required_size;
		status = IB_INSUFFICIENT_MEMORY;
		if ( p_ca_attr != NULL) {
			HCA_PRINT (TRACE_LEVEL_ERROR,HCA_DBG_SHIM, 
				("Failed *p_byte_count (%d) < required_size (%d)\n", *p_byte_count, required_size ));
		}
		goto err_insuff_mem;
	}
	RtlZeroMemory(p_ca_attr, required_size);

	// Space is sufficient - setup table pointers
	last_p = (uint8_t*)p_ca_attr;
	last_p += PTR_ALIGN(sizeof(*p_ca_attr));

	p_ca_attr->p_page_size = (uint32_t*)last_p;
	last_p += PTR_ALIGN(num_page_sizes * sizeof(uint32_t));

	p_ca_attr->p_port_attr = (ib_port_attr_t *)last_p;
	last_p += PTR_ALIGN(num_ports * sizeof(ib_port_attr_t));

	for (port_num = 0; port_num < num_ports; port_num++) {
		p_ca_attr->p_port_attr[port_num].p_gid_table = (ib_gid_t *)last_p;
		size = PTR_ALIGN(sizeof(ib_gid_t) * hca_ports[port_num].gid_tbl_len);
		last_p += size;

		p_ca_attr->p_port_attr[port_num].p_pkey_table = (uint16_t *)last_p;
		size = PTR_ALIGN(sizeof(uint16_t) * hca_ports[port_num].pkey_tbl_len);
		last_p += size;
	}
	
	//copy vendor specific data
	cl_memcpy(last_p,to_mdev(ib_dev)->board_id, MTHCA_BOARD_ID_LEN);
	last_p += PTR_ALIGN(MTHCA_BOARD_ID_LEN);
	*(uplink_info_t*)last_p = to_mdev(ib_dev)->uplink_info;
	last_p += PTR_ALIGN(sizeof(uplink_info_t));	/* uplink info */
	
	// Separate the loops to ensure that table pointers are always setup
	for (port_num = 0; port_num < num_ports; port_num++) {

		// get pkeys, using cache
		for (i=0; i < hca_ports[port_num].pkey_tbl_len; ++i) {
			err = ib_get_cached_pkey( ib_dev, port_num + start_port(ib_dev), i,
				&p_ca_attr->p_port_attr[port_num].p_pkey_table[i] );
			if (err) {
				status = errno_to_iberr(err);
				HCA_PRINT (TRACE_LEVEL_ERROR,HCA_DBG_SHIM, 
					("ib_get_cached_pkey failed (%d) for port_num %d, index %d\n",
					err, port_num + start_port(ib_dev), i));
				goto err_get_pkey;
			}
		}
		
		// get gids, using cache
		for (i=0; i < hca_ports[port_num].gid_tbl_len; ++i) {
			union ib_gid * gid = (union ib_gid *)&p_ca_attr->p_port_attr[port_num].p_gid_table[i];
			err = ib_get_cached_gid( ib_dev, port_num + start_port(ib_dev), i, (union ib_gid *)gid );
			//TODO: do we need to convert gids to little endian
			if (err) {
				status = errno_to_iberr(err);
				HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM, 
					("ib_get_cached_gid failed (%d) for port_num %d, index %d\n",
					err, port_num + start_port(ib_dev), i));
				goto err_get_gid;
			}
		}

		HCA_PRINT(TRACE_LEVEL_VERBOSE, HCA_DBG_SHIM,("port %d gid0:\n", port_num));
		HCA_PRINT(TRACE_LEVEL_VERBOSE, HCA_DBG_SHIM,
			(" 0x%x%x%x%x%x%x%x%x-0x%x%x%x%x%x%x%x%x\n", 
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[0],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[1],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[2],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[3],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[4],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[5],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[6],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[7],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[8],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[9],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[10],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[11],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[12],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[13],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[14],
			p_ca_attr->p_port_attr[port_num].p_gid_table[0].raw[15]));
	}

	// set result size
	p_ca_attr->size = required_size;
	CL_ASSERT( required_size == (((uintn_t)last_p) - ((uintn_t)p_ca_attr)) );
	HCA_PRINT(TRACE_LEVEL_VERBOSE, HCA_DBG_SHIM , ("Space required %d used %d\n",
		required_size, (int)((uintn_t)last_p - (uintn_t)p_ca_attr) ));
	
	// !!! GID/PKEY tables must be queried before this call !!!
	mlnx_conv_hca_cap(ib_dev, &props, hca_ports, p_ca_attr);

	status = IB_SUCCESS;

err_get_gid:
err_get_pkey:
err_insuff_mem:
err_query_port:
	cl_free(hca_ports);
err_alloc_ports:
err_query_device:
err_byte_count:	
err_unsupported:
err_user_unsupported:
	if( status != IB_INSUFFICIENT_MEMORY && status != IB_SUCCESS )
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_SHIM,
			("completes with ERROR status %#x\n", status));
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}

ib_api_status_t
mlnx_modify_ca (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	uint8_t						port_num,
	IN		const	ib_ca_mod_t					modca_cmd,
	IN		const	ib_port_attr_mod_t			*p_port_attr)
{
#define SET_CAP_MOD(al_mask, al_fld, ib)		\
		if (modca_cmd & al_mask) {	\
			if (p_port_attr->cap.##al_fld)		\
				props.set_port_cap_mask |= ib;	\
			else		\
				props.clr_port_cap_mask |= ib;	\
		}

	ib_api_status_t status;
	int err;
	struct ib_port_modify props;
	int port_modify_mask = 0;
	mlnx_hob_t			*hob_p = (mlnx_hob_t *)h_ca;
	struct ib_device *ib_dev = IBDEV_FROM_HOB( hob_p );

	HCA_ENTER(HCA_DBG_SHIM);

	//sanity check
	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	}
	
	if (port_num < start_port(ib_dev) || port_num > end_port(ib_dev)) {
		status = IB_INVALID_PORT;
		goto err_port;
	}

	// prepare parameters
	RtlZeroMemory(&props, sizeof(props));
	SET_CAP_MOD(IB_CA_MOD_IS_SM, sm, IB_PORT_SM);
	SET_CAP_MOD(IB_CA_MOD_IS_SNMP_SUPPORTED, snmp, IB_PORT_SNMP_TUNNEL_SUP);
	SET_CAP_MOD(IB_CA_MOD_IS_DEV_MGMT_SUPPORTED, dev_mgmt, IB_PORT_DEVICE_MGMT_SUP);
	SET_CAP_MOD(IB_CA_MOD_IS_VEND_SUPPORTED, vend, IB_PORT_VENDOR_CLASS_SUP);
	if ((modca_cmd & IB_CA_MOD_QKEY_CTR) && (p_port_attr->qkey_ctr == 0)) 
		port_modify_mask |= IB_PORT_RESET_QKEY_CNTR;
	
	// modify port
	err = mthca_modify_port(ib_dev, port_num, port_modify_mask, &props );
	if (err) {
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM  ,("mthca_modify_port failed (%d) \n",err));
		goto err_modify_port;
	}

	status =	IB_SUCCESS;

err_modify_port:
err_port:
err_unsupported:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_SHIM,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}

ib_api_status_t
mlnx_close_ca (
	IN				ib_ca_handle_t				h_ca)
{
	HCA_ENTER(HCA_DBG_SHIM);
	mlnx_hobs_remove(h_ca);
	HCA_EXIT(HCA_DBG_SHIM);
	return IB_SUCCESS;
}


static ib_api_status_t
mlnx_um_open(
	IN		const	ib_ca_handle_t				h_ca,
	IN				KPROCESSOR_MODE				mode,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf,
		OUT			ib_ca_handle_t* const		ph_um_ca )
{
	int err;
	ib_api_status_t		status;
	mlnx_hob_t			*hob_p = (mlnx_hob_t *)h_ca;
	hca_dev_ext_t *ext_p = EXT_FROM_HOB( hob_p );
	struct ib_device *ib_dev = IBDEV_FROM_HOB( hob_p );
	struct ib_ucontext *p_context;
	struct ibv_get_context_resp *uresp_p;
	struct ibv_alloc_pd_resp resp;
	ci_umv_buf_t umv_buf;

	HCA_ENTER(HCA_DBG_SHIM);

	UNREFERENCED_PARAMETER( mode );

	// sanity check
	ASSERT( p_umv_buf );
	if( !p_umv_buf->command )
	{
		p_context = cl_zalloc( sizeof(struct ib_ucontext) );
		if( !p_context )
		{
			status = IB_INSUFFICIENT_MEMORY;
			goto err_alloc_ucontext;
		}
		/* Copy the dev info. */
		p_context->device = ib_dev;
		p_umv_buf->output_size = 0;
		goto done;
	}

	// create user context in kernel
	p_context = mthca_alloc_ucontext(ib_dev, p_umv_buf);
	if (IS_ERR(p_context)) {
		err = PTR_ERR(p_context);
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_SHIM,
			("mthca_alloc_ucontext failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc_ucontext;
	}

	/* allocate pd */
	umv_buf.command = 1;
	umv_buf.input_size = umv_buf.status = 0;
	umv_buf.output_size = sizeof(struct ibv_alloc_pd_resp);
	umv_buf.p_inout_buf = (ULONG_PTR)&resp;
	//NB: Pay attention ! Ucontext parameter is important here:
	// when it is present (i.e. - for user space) - mthca_alloc_pd won't create MR
	p_context->pd = ibv_alloc_pd(ib_dev, p_context, &umv_buf);
	if (IS_ERR(p_context->pd)) {
		err = PTR_ERR(p_context->pd);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM,
			("ibv_alloc_pd failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc_pd;
	}
	
	// fill more parameters for user (sanity checks are in mthca_alloc_ucontext)
	uresp_p = (struct ibv_get_context_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
	uresp_p->uar_addr = (uint64_t)(UINT_PTR)p_context->user_uar;
	uresp_p->pd_handle = resp.pd_handle;
	uresp_p->pdn = resp.pdn;
	uresp_p->vend_id = (uint32_t)ext_p->hcaConfig.VendorID;
	uresp_p->dev_id = (uint16_t)ext_p->hcaConfig.DeviceID;

done:
	// some more inits
	p_context->va = p_context->p_mdl = NULL;
	p_context->fw_if_open = FALSE;
	KeInitializeMutex( &p_context->mutex, 0 );
	// chain user context to the device
	cl_spinlock_acquire( &ext_p->uctx_lock );
	cl_qlist_insert_tail( &ext_p->uctx_list, &p_context->list_item );
	cl_atomic_inc(&ext_p->usecnt);
	cl_spinlock_release( &ext_p->uctx_lock );
	
	// return the result
	if (ph_um_ca) *ph_um_ca = (ib_ca_handle_t)p_context;

	status = IB_SUCCESS;
	goto end;
	
err_alloc_pd:
	mthca_dealloc_ucontext(p_context);
err_alloc_ucontext: 
end:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}

static void
mlnx_um_close(
	IN				ib_ca_handle_t				h_ca,
	IN				ib_ca_handle_t				h_um_ca )
{
	struct ib_ucontext *p_ucontext = (struct ib_ucontext *)h_um_ca;
	mlnx_hob_t			*hob_p = (mlnx_hob_t *)h_ca;
	hca_dev_ext_t *ext_p = EXT_FROM_HOB( hob_p );

	unmap_crspace_for_all(p_ucontext);
	cl_spinlock_acquire( &ext_p->uctx_lock );
	cl_qlist_remove_item( &ext_p->uctx_list, &p_ucontext->list_item );
	cl_atomic_dec(&ext_p->usecnt);
	cl_spinlock_release( &ext_p->uctx_lock );
	if( !p_ucontext->pd )
		cl_free( h_um_ca );
	else
		ibv_um_close(p_ucontext);
	pa_cash_print();
	return;
}


/*
*    Protection Domain and Reliable Datagram Domain Verbs
*/

ib_api_status_t
mlnx_allocate_pd (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_pd_type_t				type,
		OUT			ib_pd_handle_t				*ph_pd,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t		status;
	struct ib_device *ib_dev;
	struct ib_ucontext *p_context;
	struct ib_pd *ib_pd_p;
	int err;

	//TODO: how are we use it ?
	UNREFERENCED_PARAMETER(type);
	
	HCA_ENTER(HCA_DBG_PD);

	if( p_umv_buf ) {
		p_context = (struct ib_ucontext *)h_ca;
		ib_dev = p_context->device;
	}
	else {
		mlnx_hob_t			*hob_p = (mlnx_hob_t *)h_ca;
		p_context = NULL;
		ib_dev = IBDEV_FROM_HOB( hob_p );
	}
	
	// create PD
	ib_pd_p = ibv_alloc_pd(ib_dev, p_context, p_umv_buf);
	if (IS_ERR(ib_pd_p)) {
		err = PTR_ERR(ib_pd_p);
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_PD,
			("ibv_alloc_pd failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc_pd;
	}

 	// return the result
	if (ph_pd) *ph_pd = (ib_pd_handle_t)ib_pd_p;

 	status = IB_SUCCESS;
	
err_alloc_pd:	
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_PD,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_PD);
	return status;
}

ib_api_status_t
mlnx_deallocate_pd (
	IN				ib_pd_handle_t				h_pd)
{
	ib_api_status_t		status;
	int err;
	struct ib_pd *ib_pd_p = (struct ib_pd *)h_pd;

	HCA_ENTER( HCA_DBG_PD);

	HCA_PRINT(TRACE_LEVEL_INFORMATION,HCA_DBG_PD,
		("pcs %p\n", PsGetCurrentProcess()));
	
	// dealloc pd
	err = ibv_dealloc_pd( ib_pd_p );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_PD
			,("ibv_dealloc_pd failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_dealloc_pd;
	}
 	status = IB_SUCCESS;

err_dealloc_pd:
	if (status != IB_SUCCESS) 
	{
			HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_PD
		,("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_PD);
	return status;
}

/* 
* Address Vector Management Verbs
*/
ib_api_status_t
mlnx_create_av (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t				*p_addr_vector,
		OUT			ib_av_handle_t				*ph_av,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err = 0;
	ib_api_status_t		status = IB_SUCCESS;
	struct ib_pd *ib_pd_p = (struct ib_pd *)h_pd;
	struct ib_device *ib_dev = ib_pd_p->device;
	struct ib_ah *ib_av_p;
	struct ib_ah_attr ah_attr;
	struct ib_ucontext *p_context = NULL;

	HCA_ENTER(HCA_DBG_AV);

	if( p_umv_buf && p_umv_buf->command ) {
		// sanity checks 
		if (p_umv_buf->input_size < sizeof(struct ibv_create_ah) ||
			p_umv_buf->output_size < sizeof(struct ibv_create_ah_resp) ||
			!p_umv_buf->p_inout_buf) {
			status = IB_INVALID_PARAMETER;
			goto err_inval_params;
		}
		p_context = ib_pd_p->ucontext;
	}
	else 
		p_context = NULL;

	// fill parameters 
	RtlZeroMemory(&ah_attr, sizeof(ah_attr));
	mlnx_conv_ibal_av( ib_dev, p_addr_vector,  &ah_attr );

	ib_av_p = ibv_create_ah(ib_pd_p, &ah_attr, p_context, p_umv_buf);
	if (IS_ERR(ib_av_p)) {
		err = PTR_ERR(ib_av_p);
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_AV,
			("ibv_create_ah failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_alloc_av;
	}

	// return the result
	if (ph_av) *ph_av = (ib_av_handle_t)ib_av_p;

	status = IB_SUCCESS;

err_alloc_av:	
err_inval_params:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_AV);
	return status;
}

ib_api_status_t
mlnx_query_av (
	IN		const	ib_av_handle_t				h_av,
		OUT			ib_av_attr_t				*p_addr_vector,
		OUT			ib_pd_handle_t				*ph_pd,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_ah *ib_ah_p = (struct ib_ah *)h_av;

	HCA_ENTER(HCA_DBG_AV);

	// sanity checks
	if( p_umv_buf && p_umv_buf->command ) {
			HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_AV,
				("User mode is not supported yet\n"));
			status = IB_UNSUPPORTED;
			goto err_user_unsupported;
	}

	// query AV
#ifdef WIN_TO_BE_CHANGED
	//TODO: not implemented in low-level driver
	err = ibv_query_ah(ib_ah_p, &ah_attr)
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_AV,
			("ibv_query_ah failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_query_ah;
	}
	// convert to IBAL structure: something like that
	mlnx_conv_mthca_av( p_addr_vector,  &ah_attr );
#else

	err = mlnx_conv_mthca_av( ib_ah_p, p_addr_vector );
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_AV,
			("mlnx_conv_mthca_av failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_conv_mthca_av;
	}
#endif

	// results
	*ph_pd = (ib_pd_handle_t)ib_ah_p->pd;
	
err_conv_mthca_av:
err_user_unsupported:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_AV);
	return status;
}

ib_api_status_t
mlnx_modify_av (
	IN		const	ib_av_handle_t				h_av,
	IN		const	ib_av_attr_t				*p_addr_vector,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	struct ib_ah_attr ah_attr;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_ah *ib_ah_p = (struct ib_ah *)h_av;
	struct ib_device *ib_dev = ib_ah_p->pd->device;

	HCA_ENTER(HCA_DBG_AV);

	// sanity checks
	if( p_umv_buf && p_umv_buf->command ) {
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
				("User mode is not supported yet\n"));
			status = IB_UNSUPPORTED;
			goto err_user_unsupported;
	}

	// fill parameters 
	RtlZeroMemory(&ah_attr, sizeof(ah_attr));
	mlnx_conv_ibal_av( ib_dev, p_addr_vector,  &ah_attr );

	// modify AH
#ifdef WIN_TO_BE_CHANGED
	//TODO: not implemented in low-level driver
	err = ibv_modify_ah(ib_ah_p, &ah_attr)
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("ibv_query_ah failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_query_ah;
	}
#else

	mlnx_modify_ah( ib_ah_p, &ah_attr );
#endif

err_user_unsupported:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_AV);
	return status;
}

ib_api_status_t
mlnx_destroy_av (
	IN		const	ib_av_handle_t				h_av)
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_ah *ib_ah_p = (struct ib_ah *)h_av;

	HCA_ENTER(HCA_DBG_AV);

	// destroy AV
	err = ibv_destroy_ah( ib_ah_p );
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR ,HCA_DBG_AV,
			("ibv_destroy_ah failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_destroy_ah;
	}

err_destroy_ah:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_AV,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_AV);
	return status;
}

/*
*	Shared Queue Pair Management Verbs
*/


ib_api_status_t
mlnx_create_srq (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	void						*srq_context,
	IN				ci_async_event_cb_t		event_handler,
	IN		const	ib_srq_attr_t * const		p_srq_attr,
		OUT			ib_srq_handle_t				*ph_srq,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status;
	struct ib_srq *ib_srq_p;
	struct ib_srq_init_attr srq_init_attr;
	struct ib_ucontext *p_context = NULL;
	struct ib_pd *ib_pd_p = (struct ib_pd *)h_pd;

	HCA_ENTER(HCA_DBG_SRQ);

	if( p_umv_buf  && p_umv_buf->command) {

		// sanity checks 
		if (p_umv_buf->input_size < sizeof(struct ibv_create_srq) ||
			p_umv_buf->output_size < sizeof(struct ibv_create_srq_resp) ||
			!p_umv_buf->p_inout_buf) {
			status = IB_INVALID_PARAMETER;
			goto err_inval_params;
		}
		p_context = ib_pd_p->ucontext;
	}

	// prepare the parameters
	RtlZeroMemory(&srq_init_attr, sizeof(srq_init_attr));
	srq_init_attr.event_handler = event_handler;
	srq_init_attr.srq_context = (void*)srq_context;
	srq_init_attr.attr = *p_srq_attr;

	// allocate srq	
	ib_srq_p = ibv_create_srq(ib_pd_p, &srq_init_attr, p_context, p_umv_buf );
	if (IS_ERR(ib_srq_p)) {
		err = PTR_ERR(ib_srq_p);
		HCA_PRINT (TRACE_LEVEL_ERROR ,HCA_DBG_SRQ, ("ibv_create_srq failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_create_srq;
	}

	// return the result
	if (ph_srq) *ph_srq = (ib_srq_handle_t)ib_srq_p;

	status = IB_SUCCESS;
	
err_create_srq:
err_inval_params:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}


ib_api_status_t
mlnx_modify_srq (
		IN		const	ib_srq_handle_t				h_srq,
		IN		const	ib_srq_attr_t* const			p_srq_attr,
		IN		const	ib_srq_attr_mask_t			srq_attr_mask,
		IN	OUT 		ci_umv_buf_t				*p_umv_buf OPTIONAL )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_srq *ib_srq = (struct ib_srq *)h_srq;
	UNUSED_PARAM(p_umv_buf);

	HCA_ENTER(HCA_DBG_SRQ);

	err = ibv_modify_srq(ib_srq, (void*)p_srq_attr, srq_attr_mask);
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_AV,
			("ibv_modify_srq failed (%d)\n", err));
		status = errno_to_iberr(err);
	}

	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}

ib_api_status_t
mlnx_query_srq (
	IN		const	ib_srq_handle_t				h_srq,
		OUT			ib_srq_attr_t* const			p_srq_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf OPTIONAL )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_srq *ib_srq = (struct ib_srq *)h_srq;
	UNUSED_PARAM(p_umv_buf);

	HCA_ENTER(HCA_DBG_SRQ);

	err = ibv_query_srq(ib_srq, p_srq_attr);
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_AV,
			("ibv_query_srq failed (%d)\n", err));
		status = errno_to_iberr(err);
	}

	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}

ib_api_status_t
mlnx_destroy_srq (
	IN	const	ib_srq_handle_t		h_srq )
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_srq *ib_srq = (struct ib_srq *)h_srq;

	HCA_ENTER(HCA_DBG_SRQ);

	err = ibv_destroy_srq(ib_srq);
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_AV,
			("ibv_destroy_srq failed (%d)\n", err));
		status = errno_to_iberr(err);
	}

	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SRQ,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_SRQ);
	return status;
}

/*
*	Queue Pair Management Verbs
*/


static ib_api_status_t
_create_qp (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	uint8_t						port_num,
	IN		const	void						*qp_context,
	IN				ci_async_event_cb_t			event_handler,
	IN		const	ib_qp_create_t				*p_create_attr,
		OUT			ib_qp_attr_t				*p_qp_attr,
		OUT			ib_qp_handle_t				*ph_qp,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status;
	struct ib_qp * ib_qp_p;
	struct mthca_qp *qp_p;
	struct ib_qp_init_attr qp_init_attr;
	struct ib_ucontext *p_context = NULL;
	struct ib_pd *ib_pd_p = (struct ib_pd *)h_pd;
	
	HCA_ENTER(HCA_DBG_QP);

	if( p_umv_buf && p_umv_buf->command ) {
		// sanity checks 
		if (p_umv_buf->input_size < sizeof(struct ibv_create_qp) ||
			p_umv_buf->output_size < sizeof(struct ibv_create_qp_resp) ||
			!p_umv_buf->p_inout_buf) {
			status = IB_INVALID_PARAMETER;
			goto err_inval_params;
		}
		p_context = ib_pd_p->ucontext;
	}

	// prepare the parameters
	RtlZeroMemory(&qp_init_attr, sizeof(qp_init_attr));
	qp_init_attr.qp_type = p_create_attr->qp_type;
	qp_init_attr.event_handler = event_handler;
	qp_init_attr.qp_context = (void*)qp_context;
	qp_init_attr.recv_cq = (struct ib_cq *)p_create_attr->h_rq_cq;
	qp_init_attr.send_cq = (struct ib_cq *)p_create_attr->h_sq_cq;
	qp_init_attr.srq = (struct ib_srq *)p_create_attr->h_srq;
	qp_init_attr.cap.max_inline_data = p_create_attr->sq_max_inline;
	qp_init_attr.cap.max_recv_sge = p_create_attr->rq_sge;
	qp_init_attr.cap.max_send_sge = p_create_attr->sq_sge;
	qp_init_attr.cap.max_recv_wr = p_create_attr->rq_depth;
	qp_init_attr.cap.max_send_wr = p_create_attr->sq_depth;
	qp_init_attr.sq_sig_type = (p_create_attr->sq_signaled) ? IB_SIGNAL_ALL_WR : IB_SIGNAL_REQ_WR;
	qp_init_attr.port_num = port_num;


	// create qp		
	ib_qp_p = ibv_create_qp( ib_pd_p, &qp_init_attr, p_context, p_umv_buf );
	if (IS_ERR(ib_qp_p)) {
		err = PTR_ERR(ib_qp_p);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_QP,
			("ibv_create_qp failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_create_qp;
	}

	// fill the object
	qp_p = (struct mthca_qp *)ib_qp_p;
	qp_p->qp_init_attr = qp_init_attr;

	// Query QP to obtain requested attributes
	if (p_qp_attr) {
		status = mlnx_query_qp ((ib_qp_handle_t)ib_qp_p, p_qp_attr, p_umv_buf);
		if (status != IB_SUCCESS)
				goto err_query_qp;
	}
	
	// return the results
	if (ph_qp) *ph_qp = (ib_qp_handle_t)ib_qp_p;

	status = IB_SUCCESS;
	goto end;

err_query_qp:
	ibv_destroy_qp( ib_qp_p );
err_create_qp:
err_inval_params:
end:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_create_spl_qp (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	uint8_t						port_num,
	IN		const	void						*qp_context,
	IN				ci_async_event_cb_t			event_handler,
	IN	OUT			ib_qp_create_t				*p_create_attr,
		OUT			ib_qp_attr_t				*p_qp_attr,
		OUT			ib_qp_handle_t				*ph_qp )
{
	ib_api_status_t 	status;

	HCA_ENTER(HCA_DBG_SHIM);

	status =	_create_qp( h_pd, port_num,
		qp_context, event_handler, p_create_attr, p_qp_attr, ph_qp, NULL );
		
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_create_qp (
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	void						*qp_context,
	IN				ci_async_event_cb_t			event_handler,
	IN	OUT			ib_qp_create_t				*p_create_attr,
		OUT			ib_qp_attr_t				*p_qp_attr,
		OUT			ib_qp_handle_t				*ph_qp,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	ib_api_status_t 	status;

	//NB: algorithm of mthca_alloc_sqp() requires port_num
	// PRM states, that special pares are created in couples, so
	// looks like we can put here port_num = 1 always
	uint8_t port_num = 1;

	HCA_ENTER(HCA_DBG_QP);

	status = _create_qp( h_pd, port_num,
		qp_context, event_handler, p_create_attr, p_qp_attr, ph_qp, p_umv_buf );
		
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_modify_qp (
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t					*p_modify_attr,
		OUT			ib_qp_attr_t				*p_qp_attr OPTIONAL,
	IN	OUT			ci_umv_buf_t				*p_umv_buf OPTIONAL )
{
	ib_api_status_t 	status;
	int err;
	struct ib_qp_attr qp_attr;
	int qp_attr_mask;
	struct ib_qp *ib_qp_p = (struct ib_qp *)h_qp;

	HCA_ENTER(HCA_DBG_QP);

	// sanity checks
	if( p_umv_buf && p_umv_buf->command ) {
		// sanity checks 
		if (p_umv_buf->output_size < sizeof(struct ibv_modify_qp_resp) ||
			!p_umv_buf->p_inout_buf) {
			status = IB_INVALID_PARAMETER;
			goto err_inval_params;
		}
	}
	
	// fill parameters 
	status = mlnx_conv_qp_modify_attr( ib_qp_p, ib_qp_p->qp_type, 
		p_modify_attr,  &qp_attr, &qp_attr_mask );
	if (status == IB_NOT_DONE)
		goto query_qp;
	if (status != IB_SUCCESS ) 
		goto err_mode_unsupported;

	// modify QP
	err = ibv_modify_qp(ib_qp_p, &qp_attr, qp_attr_mask);
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_QP,
			("ibv_modify_qp failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_modify_qp;
	}

	// Query QP to obtain requested attributes
query_qp:	
	if (p_qp_attr) {
		status = mlnx_query_qp ((ib_qp_handle_t)ib_qp_p, p_qp_attr, p_umv_buf);
		if (status != IB_SUCCESS)
				goto err_query_qp;
	}
	
	if( p_umv_buf && p_umv_buf->command ) {
			struct ibv_modify_qp_resp resp;
			resp.attr_mask = qp_attr_mask;
			resp.qp_state = qp_attr.qp_state;
			err = ib_copy_to_umv_buf(p_umv_buf, &resp, sizeof(struct ibv_modify_qp_resp));
			if (err) {
				HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM  ,("ib_copy_to_umv_buf failed (%d)\n", err));
				status = errno_to_iberr(err);
				goto err_copy;
			}
	}

	status = IB_SUCCESS;

err_copy:	
err_query_qp:
err_modify_qp:	
err_mode_unsupported:
err_inval_params:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_ndi_modify_qp (
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_qp_mod_t					*p_modify_attr,
		OUT			ib_qp_attr_t				*p_qp_attr OPTIONAL,
	IN		const	uint32_t					buf_size,
	IN				uint8_t* const				p_outbuf)
{
	ci_umv_buf_t umv_buf;
	ib_api_status_t status;
	struct ibv_modify_qp_resp resp;
	void *buf = &resp;

	HCA_ENTER(HCA_DBG_QP);

	if (buf_size < sizeof(resp.qp_state)) {
		status = IB_INVALID_PARAMETER;
		goto out;
	}

	/* imitate umv_buf */
	umv_buf.command = TRUE;	/* special case for NDI. Usually it's TRUE */
	umv_buf.input_size = 0;
	umv_buf.output_size = sizeof(struct ibv_modify_qp_resp);
	umv_buf.p_inout_buf = (ULONG_PTR)buf;

	status = mlnx_modify_qp ( h_qp, p_modify_attr, p_qp_attr, &umv_buf );

	if (status == IB_SUCCESS) {
		cl_memclr( p_outbuf, buf_size );
		*p_outbuf = resp.qp_state;
	}

out:
	HCA_EXIT(HCA_DBG_QP);
	return status;
}



ib_api_status_t
mlnx_query_qp (
	IN		const	ib_qp_handle_t				h_qp,
		OUT			ib_qp_attr_t				*p_qp_attr,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	int qp_attr_mask = 0;
	ib_api_status_t 	status = IB_SUCCESS;
	struct ib_qp *ib_qp_p = (struct ib_qp *)h_qp;
	struct ib_qp_attr qp_attr;
	struct ib_qp_init_attr qp_init_attr;
	struct mthca_qp *qp_p = (struct mthca_qp *)ib_qp_p;

	UNREFERENCED_PARAMETER(p_umv_buf);
	
	HCA_ENTER( HCA_DBG_QP);

	// sanity checks
	if (!p_qp_attr) {
		status =  IB_INVALID_PARAMETER;
		goto err_parm;
	}

	memset( &qp_attr, 0, sizeof(struct ib_qp_attr) );

	if (qp_p->state == IBQPS_RESET) {
		// the QP doesn't yet exist in HW - fill what we can fill now
		p_qp_attr->h_pd					= (ib_pd_handle_t)qp_p->ibqp.pd;
		p_qp_attr->qp_type				= qp_p->ibqp.qp_type;
		p_qp_attr->sq_max_inline		= qp_p->qp_init_attr.cap.max_inline_data;
		p_qp_attr->sq_depth				= qp_p->qp_init_attr.cap.max_send_wr;
		p_qp_attr->rq_depth				= qp_p->qp_init_attr.cap.max_recv_wr;
		p_qp_attr->sq_sge				= qp_p->qp_init_attr.cap.max_send_sge;
		p_qp_attr->rq_sge				= qp_p->qp_init_attr.cap.max_recv_sge;
		p_qp_attr->resp_res				= qp_p->resp_depth;
		p_qp_attr->h_sq_cq				= (ib_cq_handle_t)qp_p->ibqp.send_cq;
		p_qp_attr->h_rq_cq				= (ib_cq_handle_t)qp_p->ibqp.recv_cq;
		p_qp_attr->sq_signaled			= qp_p->sq_policy == IB_SIGNAL_ALL_WR;
		p_qp_attr->state				= mlnx_qps_to_ibal( qp_p->state );
		p_qp_attr->num					= cl_hton32(qp_p->ibqp.qp_num);
		p_qp_attr->primary_port			= qp_p->qp_init_attr.port_num;
	}
	else {
		//request the info from the card
		err = ib_qp_p->device->query_qp( ib_qp_p, &qp_attr, 
			qp_attr_mask, &qp_init_attr);
		if (err){
			status = errno_to_iberr(err);
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_PD,
				("ib_query_qp failed (%#x)\n", status));
			goto err_query_qp;
		}
		
		// convert the results back to IBAL
		status = mlnx_conv_qp_attr( ib_qp_p, &qp_attr, p_qp_attr );
	}

err_query_qp:
err_parm:
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

ib_api_status_t
mlnx_destroy_qp (
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	uint64_t					timewait )
{
	ib_api_status_t 	status;
	int err;
	struct ib_qp *ib_qp_p = (struct ib_qp *)h_qp;

	UNUSED_PARAM( timewait );

	HCA_ENTER( HCA_DBG_QP);

	HCA_PRINT(TRACE_LEVEL_INFORMATION 	,HCA_DBG_SHIM  ,
		("qpnum %#x, pcs %p\n", ib_qp_p->qp_num, PsGetCurrentProcess()) );

	err = ibv_destroy_qp( ib_qp_p );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_QP,
			("ibv_destroy_qp failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_destroy_qp;
	}

	status = IB_SUCCESS;

err_destroy_qp:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_QP,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_QP);
	return status;
}

/*
* Completion Queue Managment Verbs.
*/

ib_api_status_t
mlnx_create_cq (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	void						*cq_context,
	IN				ci_async_event_cb_t			event_handler,
	IN				ci_completion_cb_t			cq_comp_handler,
	IN	OUT			uint32_t					*p_size,
		OUT			ib_cq_handle_t				*ph_cq,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status;
	struct ib_cq *ib_cq_p;
	mlnx_hob_t			*hob_p;
	struct ib_device *ib_dev;
	struct ib_ucontext *p_context;

	HCA_ENTER(HCA_DBG_CQ);

	if( p_umv_buf ) {

		p_context = (struct ib_ucontext *)h_ca;
		hob_p = HOB_FROM_IBDEV(p_context->device);
		ib_dev = p_context->device;

		// sanity checks 
		if (p_umv_buf->input_size < sizeof(struct ibv_create_cq) ||
			p_umv_buf->output_size < sizeof(struct ibv_create_cq_resp) ||
			!p_umv_buf->p_inout_buf) {
			status = IB_INVALID_PARAMETER;
			goto err_inval_params;
		}
	}
	else {
		hob_p = (mlnx_hob_t *)h_ca;
		p_context = NULL;
		ib_dev = IBDEV_FROM_HOB( hob_p );
	}

	/* sanity check */
	if (!*p_size || *p_size > (uint32_t)ib_dev->mdev->limits.max_cqes) {
		status = IB_INVALID_CQ_SIZE;
		goto err_cqe;
	}

	// allocate cq	
	ib_cq_p = ibv_create_cq(ib_dev, 
		cq_comp_handler, event_handler,
		(void*)cq_context, *p_size, p_context, p_umv_buf );
	if (IS_ERR(ib_cq_p)) {
		err = PTR_ERR(ib_cq_p);
		HCA_PRINT (TRACE_LEVEL_ERROR ,HCA_DBG_CQ, ("ibv_create_cq failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_create_cq;
	}

	// return the result
//	*p_size = *p_size;	// return the same value
	*p_size = ib_cq_p->cqe;

	if (ph_cq) *ph_cq = (ib_cq_handle_t)ib_cq_p;

	status = IB_SUCCESS;
	
err_create_cq:
err_inval_params:
err_cqe:
	if (p_umv_buf && p_umv_buf->command) 
		p_umv_buf->status = status;
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_CQ);
	return status;
}

ib_api_status_t
mlnx_resize_cq (
	IN		const	ib_cq_handle_t				h_cq,
	IN	OUT			uint32_t					*p_size,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	UNREFERENCED_PARAMETER(h_cq);
	UNREFERENCED_PARAMETER(p_size);
	if (p_umv_buf && p_umv_buf->command) {
		p_umv_buf->status = IB_UNSUPPORTED;
	}
	HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_CQ,("mlnx_resize_cq not implemented\n"));
	return IB_UNSUPPORTED;
}

ib_api_status_t
mlnx_query_cq (
	IN		const	ib_cq_handle_t				h_cq,
		OUT			uint32_t					*p_size,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	UNREFERENCED_PARAMETER(h_cq);
	UNREFERENCED_PARAMETER(p_size);
	if (p_umv_buf && p_umv_buf->command) {
		p_umv_buf->status = IB_UNSUPPORTED;
	}
	HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,("mlnx_query_cq not implemented\n"));
	return IB_UNSUPPORTED;
}

ib_api_status_t
mlnx_destroy_cq (
	IN		const	ib_cq_handle_t				h_cq)
{
																				
	ib_api_status_t 	status;
	int err;
	struct ib_cq *ib_cq_p = (struct ib_cq *)h_cq;

	HCA_ENTER( HCA_DBG_QP);

	HCA_PRINT(TRACE_LEVEL_INFORMATION,HCA_DBG_CQ,
		("cqn %#x, pcs %p\n", ((struct mthca_cq*)ib_cq_p)->cqn, PsGetCurrentProcess()) );

	// destroy CQ
	err = ibv_destroy_cq( ib_cq_p );
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR ,HCA_DBG_SHIM,
			("ibv_destroy_cq failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_destroy_cq;
	}

	status = IB_SUCCESS;

err_destroy_cq:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_CQ,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_CQ);
	return status;
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
	mlnx_hob_t			*hob_p = (mlnx_hob_t *)h_ca;
	struct ib_device *ib_dev = IBDEV_FROM_HOB( hob_p );
	//TODO: do we need use flags (IB_MAD_IGNORE_MKEY, IB_MAD_IGNORE_BKEY) ?
	int mad_flags = 0;  
	struct _ib_wc *wc_p = NULL;
	//TODO: do we need use grh ?
	struct _ib_grh *grh_p = NULL;

	HCA_ENTER(HCA_DBG_MAD);

	// sanity checks
	if (port_num > 2) {
		status = IB_INVALID_PARAMETER;
		goto err_port_num;
	}

	if (p_av_attr){
		wc_p = cl_zalloc(sizeof(struct _ib_wc));
		if(!wc_p){
			status =  IB_INSUFFICIENT_MEMORY ;
			goto err_wc_alloc;
		}
		//Copy part of the attributes need to fill the mad extended fields in mellanox devices
		wc_p->recv.ud.remote_lid = p_av_attr->dlid;
		wc_p->recv.ud.remote_sl  = p_av_attr->sl;
		wc_p->recv.ud.path_bits  = p_av_attr->path_bits;
		wc_p->recv.ud.recv_opt = p_av_attr->grh_valid?IB_RECV_OPT_GRH_VALID:0;

		if(wc_p->recv.ud.recv_opt &IB_RECV_OPT_GRH_VALID){
			grh_p = cl_zalloc(sizeof(struct _ib_grh));
			if(!grh_p){
				status =  IB_INSUFFICIENT_MEMORY ;
				goto err_grh_alloc;
			}
			cl_memcpy(grh_p, &p_av_attr->grh, sizeof(ib_grh_t));
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
	
	err = mthca_process_mad(ib_dev, mad_flags, (uint8_t)port_num, 
		wc_p, grh_p, (struct ib_mad*)p_mad_in, (struct ib_mad*)p_mad_out);
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
	if(grh_p)
		cl_free(grh_p);
err_grh_alloc:
	if(wc_p)
		cl_free(wc_p);
err_wc_alloc:
err_port_num:	
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_MAD,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_MAD);
	return status;
}
	


enum rdma_transport_type
mlnx_port_get_transport (
	IN		const	ib_ca_handle_t	h_ca,
	IN	const uint8_t				port_num )
{
	UNREFERENCED_PARAMETER(h_ca);
	UNREFERENCED_PARAMETER(port_num);
	return RDMA_TRANSPORT_IB;
}

uint8_t
mlnx_get_sl_for_ip_port (
	IN		const	ib_ca_handle_t	h_ca,
	IN	const uint8_t				ca_port_num,
	IN	const uint16_t				ip_port_num)
{
	UNREFERENCED_PARAMETER(h_ca);
	UNREFERENCED_PARAMETER(ca_port_num);
	UNREFERENCED_PARAMETER(ip_port_num);
	return 0xff;
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
	p_interface->open_ca = mlnx_open_ca;
	p_interface->modify_ca = mlnx_modify_ca; 
	p_interface->query_ca = mlnx_query_ca;
	p_interface->close_ca = mlnx_close_ca;
	p_interface->um_open_ca = mlnx_um_open;
	p_interface->um_close_ca = mlnx_um_close;
	p_interface->register_event_handler = mlnx_register_event_handler;
	p_interface->unregister_event_handler = mlnx_unregister_event_handler;

	p_interface->allocate_pd = mlnx_allocate_pd;
	p_interface->deallocate_pd = mlnx_deallocate_pd;
	p_interface->vendor_call = fw_access_ctrl;

	p_interface->create_av = mlnx_create_av;
	p_interface->query_av = mlnx_query_av;
	p_interface->modify_av = mlnx_modify_av;
	p_interface->destroy_av = mlnx_destroy_av;

	p_interface->create_srq = mlnx_create_srq;
	p_interface->modify_srq = mlnx_modify_srq;
	p_interface->query_srq = mlnx_query_srq;
	p_interface->destroy_srq = mlnx_destroy_srq;

	p_interface->create_qp = mlnx_create_qp;
	p_interface->create_spl_qp = mlnx_create_spl_qp;
	p_interface->modify_qp = mlnx_modify_qp;
	p_interface->ndi_modify_qp = mlnx_ndi_modify_qp;
	p_interface->query_qp = mlnx_query_qp;
	p_interface->destroy_qp = mlnx_destroy_qp;

	p_interface->create_cq = mlnx_create_cq;
	p_interface->resize_cq = mlnx_resize_cq;
	p_interface->query_cq = mlnx_query_cq;
	p_interface->destroy_cq = mlnx_destroy_cq;

	p_interface->local_mad = mlnx_local_mad;
	p_interface->rdma_port_get_transport = mlnx_port_get_transport;
	p_interface->get_sl_for_ip_port = mlnx_get_sl_for_ip_port;

	mlnx_memory_if(p_interface);
	mlnx_direct_if(p_interface);
	mlnx_mcast_if(p_interface);

	return;
}


