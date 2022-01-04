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
#include "ca.tmh"
#endif

ib_api_status_t
mlnx_open_ca (
	IN		const	ib_net64_t					ca_guid, // IN  const char *                ca_name,
	IN		const	ci_async_event_cb_t			pfn_async_event_cb,
	IN		const	void*const					ca_context,
		OUT			ib_ca_handle_t				*ph_ca)
{
	mlnx_hca_t				*p_hca;
	ib_api_status_t status = IB_NOT_FOUND;
	struct ib_device *p_ibdev;

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

	p_ibdev = hca2ibdev(p_hca);

	HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_SHIM,
		("context 0x%p\n", ca_context));
	if (pfn_async_event_cb) {
	status = mlnx_set_cb(p_hca,
		pfn_async_event_cb,
		ca_context);
	if (IB_SUCCESS != status) {
		goto err_set_cb;
		}
	}

	//TODO: do we need something for kernel users ?

	// Return pointer to HCA object
	if (ph_ca) *ph_ca = (ib_ca_handle_t)p_hca;
	status =  IB_SUCCESS;

//err_mad_cache:
err_set_cb:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,
			("completes with ERROR status %x\n", status));
	}
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}

static void
mlnx_register_event_handler (
	IN		const	ib_ca_handle_t				h_ca,
	IN				ci_event_handler_t*			p_reg)
{
	mlnx_hca_t *p_hca = (mlnx_hca_t *) h_ca;
	KIRQL irql;

	KeAcquireSpinLock(&p_hca->event_list_lock, &irql);
	InsertTailList(&p_hca->event_list, &p_reg->entry);
	KeReleaseSpinLock(&p_hca->event_list_lock, irql);
}

static void
mlnx_unregister_event_handler (
	IN		const	ib_ca_handle_t				h_ca,
	IN				ci_event_handler_t*			p_reg)
{
	mlnx_hca_t *p_hca = (mlnx_hca_t *) h_ca;
	KIRQL irql;

	KeAcquireSpinLock(&p_hca->event_list_lock, &irql);
	RemoveEntryList(&p_reg->entry);
	KeReleaseSpinLock(&p_hca->event_list_lock, irql);
}

ib_api_status_t
mlnx_query_ca (
	IN		const	ib_ca_handle_t				h_ca,
		OUT			ib_ca_attr_t				*p_ca_attr,
	IN	OUT			uint32_t					*p_byte_count,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int i;
	int err;
	ib_api_status_t		status;
	uint32_t			size, required_size;
	int					port_num, num_ports;
	uint32_t			num_gids, num_pkeys;
	uint32_t			num_page_sizes = 1; // TBD: what is actually supported
	uint8_t				*last_p;
	struct ib_device_attr props;
	struct ib_port_attr  *hca_ports = NULL;
	mlnx_hca_t *p_hca = (mlnx_hca_t *)h_ca;
	struct ib_device *p_ibdev = hca2ibdev(p_hca);
	
	
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
	RtlZeroMemory(&props, sizeof(props));
	err = p_ibdev->query_device(p_ibdev, &props);
	if (err) {
		HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM, 
			("ib_query_device failed (%d)\n",err));
		status = errno_to_iberr(err);
		// this function is to return IB_INSUFFICIENT_MEMORY only when the buffer is small
		if ( status == IB_INSUFFICIENT_MEMORY )
			status = IB_INSUFFICIENT_RESOURCES;
		goto err_query_device;
	}
	
	// allocate array for port properties
	num_ports = p_ibdev->phys_port_cnt;   /* Number of physical ports of the HCA */
	if ( num_ports )
		if (NULL == (hca_ports = cl_zalloc( num_ports * sizeof *hca_ports))) {
			HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM, ("Failed to cl_zalloc ports array\n"));
			// this function is to return IB_INSUFFICIENT_MEMORY only when the buffer is small
			status = IB_INSUFFICIENT_RESOURCES;
			goto err_alloc_ports;
		}

	// start calculation of ib_ca_attr_t full size
	num_gids = 0;
	num_pkeys = 0;
	required_size = PTR_ALIGN(sizeof(ib_ca_attr_t)) +
		PTR_ALIGN(sizeof(uint32_t) * num_page_sizes) +
		PTR_ALIGN(sizeof(ib_port_attr_t) * num_ports)+
		PTR_ALIGN(MLX4_BOARD_ID_LEN)+
		PTR_ALIGN(sizeof(uplink_info_t));	/* uplink info */
	
	// get port properties
	for (port_num = 0; port_num < num_ports; ++port_num) {
		// request
		err = p_ibdev->query_port(p_ibdev, (u8)(port_num + start_port(p_ibdev)), &hca_ports[port_num]);
		if (err) {
			HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM, ("ib_query_port failed(%d) for port %d\n",err, port_num));
			status = errno_to_iberr(err);
			// this function is to return IB_INSUFFICIENT_MEMORY only when the buffer is small
			if ( status == IB_INSUFFICIENT_MEMORY )
				status = IB_INSUFFICIENT_RESOURCES;
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
		status = IB_INSUFFICIENT_MEMORY;
		if ( p_ca_attr != NULL) {
			HCA_PRINT (TRACE_LEVEL_ERROR,HCA_DBG_SHIM, 
				("Failed *p_byte_count (%d) < required_size (%d)\n", *p_byte_count, required_size ));
		}
		*p_byte_count = required_size;
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
	cl_memcpy(last_p,hca2mdev(p_hca)->board_id, MLX4_BOARD_ID_LEN);
	last_p += PTR_ALIGN(MLX4_BOARD_ID_LEN);
	*(uplink_info_t*)last_p = hca2pdev(p_hca)->uplink_info;
	last_p += PTR_ALIGN(sizeof(uplink_info_t));	/* uplink info */
	
	// Separate the loops to ensure that table pointers are always setup
	for (port_num = 0; port_num < num_ports; port_num++) {

		// get pkeys, using cache
		for (i=0; i < hca_ports[port_num].pkey_tbl_len; ++i) {
			err = p_ibdev->x.get_cached_pkey( p_ibdev, (u8)(port_num + start_port(p_ibdev)), i,
				&p_ca_attr->p_port_attr[port_num].p_pkey_table[i] );
			if (err) {
				status = errno_to_iberr(err);
				HCA_PRINT (TRACE_LEVEL_ERROR,HCA_DBG_SHIM, 
					("ib_get_cached_pkey failed (%d) for port_num %d, index %d\n",
					err, port_num + start_port(p_ibdev), i));
				goto err_get_pkey;
			}
		}
		
		// get gids, using cache
		for (i=0; i < hca_ports[port_num].gid_tbl_len; ++i) {
			union ib_gid * gid = (union ib_gid *)&p_ca_attr->p_port_attr[port_num].p_gid_table[i];
			err = p_ibdev->x.get_cached_gid( p_ibdev, (u8)(port_num + start_port(p_ibdev)), i, (union ib_gid *)gid );
			//TODO: do we need to convert gids to little endian
			if (err) {
				status = errno_to_iberr(err);
				HCA_PRINT (TRACE_LEVEL_ERROR, HCA_DBG_SHIM, 
					("ib_get_cached_gid failed (%d) for port_num %d, index %d\n",
					err, port_num + start_port(p_ibdev), i));
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
	from_hca_cap(p_ibdev, &props, hca_ports, p_ca_attr);

	status = IB_SUCCESS;

err_get_gid:
err_get_pkey:
err_insuff_mem:
err_query_port:
	if (hca_ports)
		cl_free(hca_ports);
err_alloc_ports:
err_query_device:
err_byte_count:	
err_unsupported:
err_user_unsupported:
	if( status != IB_INSUFFICIENT_MEMORY && status != IB_SUCCESS )
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_SHIM,
		("completes with ERROR status %x\n", status));

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
	mlnx_hca_t *p_hca = (mlnx_hca_t *)h_ca;
	struct ib_device *p_ibdev = hca2ibdev(p_hca);

	HCA_ENTER(HCA_DBG_SHIM);

	//sanity check
	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	}
	
	if (port_num < start_port(p_ibdev) || port_num > end_port(p_ibdev)) {
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
	err = p_ibdev->modify_port(p_ibdev, port_num, port_modify_mask, &props );
	if (err) {
		status = errno_to_iberr(err);
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM  ,("ib_modify_port failed (%d) \n",err));
		goto err_modify_port;
	}

	status =	IB_SUCCESS;

err_modify_port:
err_port:
err_unsupported:
	if (status != IB_SUCCESS)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_SHIM,
			("completes with ERROR status %x\n", status));
	}
	HCA_EXIT(HCA_DBG_SHIM);
	return status;
}

ib_api_status_t
mlnx_close_ca (
	IN				ib_ca_handle_t				h_ca)
{
	mlnx_hca_t *p_hca = (mlnx_hca_t *)h_ca;
	HCA_ENTER(HCA_DBG_SHIM);
	mlnx_reset_cb(p_hca);
	HCA_EXIT(HCA_DBG_SHIM);
	return IB_SUCCESS;
}

enum rdma_transport_type
mlnx_port_get_transport (
	IN		const	ib_ca_handle_t	h_ca,
	IN	const uint8_t				port_num )
{
	mlnx_hca_t *p_hca = (mlnx_hca_t *)h_ca;
	struct ib_device *p_ibdev = hca2ibdev(p_hca);

	HCA_ENTER(HCA_DBG_SHIM);
	
	ASSERT (port_num >= start_port(p_ibdev) && port_num <= end_port(p_ibdev));

	HCA_EXIT(HCA_DBG_SHIM);

	return p_ibdev->get_port_transport(p_ibdev, port_num);


}

uint8_t
mlnx_get_sl_for_ip_port (
	IN		const	ib_ca_handle_t	h_ca,
	IN	const uint8_t				ca_port_num,
	IN	const uint16_t				ip_port_num)
{
	mlnx_hca_t *p_hca = (mlnx_hca_t *)h_ca;
	struct ib_device *p_ibdev = hca2ibdev(p_hca);

	HCA_ENTER(HCA_DBG_SHIM);
	
	ASSERT (ca_port_num >= start_port(p_ibdev) && ca_port_num <= end_port(p_ibdev));

	HCA_EXIT(HCA_DBG_SHIM);

	return p_ibdev->x.get_sl_for_ip_port(p_ibdev, ca_port_num, ip_port_num);
}

void
mlnx_ca_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->open_ca = mlnx_open_ca;
	p_interface->modify_ca = mlnx_modify_ca; 
	p_interface->query_ca = mlnx_query_ca;
	p_interface->close_ca = mlnx_close_ca;
	p_interface->register_event_handler = mlnx_register_event_handler;
	p_interface->unregister_event_handler = mlnx_unregister_event_handler;
	p_interface->rdma_port_get_transport = mlnx_port_get_transport;
	p_interface->get_sl_for_ip_port = mlnx_get_sl_for_ip_port;
}

