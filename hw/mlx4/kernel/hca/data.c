/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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
#include "data.tmh"
#endif

static cl_spinlock_t	hca_lock;



uint32_t		g_mlnx_dpc2thread = 0;


cl_qlist_t		mlnx_hca_list;

/////////////////////////////////////////////////////////
// ### HCA
/////////////////////////////////////////////////////////
void
mlnx_hca_insert(
	IN				mlnx_hca_t					*p_hca )
{
	cl_spinlock_acquire( &hca_lock );
	cl_qlist_insert_tail( &mlnx_hca_list, &p_hca->list_item );
	cl_spinlock_release( &hca_lock );
}

void
mlnx_hca_remove(
	IN				mlnx_hca_t					*p_hca )
{
	cl_spinlock_acquire( &hca_lock );
	cl_qlist_remove_item( &mlnx_hca_list, &p_hca->list_item );
	cl_spinlock_release( &hca_lock );
}

mlnx_hca_t*
mlnx_hca_from_guid(
	IN				ib_net64_t					guid )
{
	cl_list_item_t	*p_item;
	mlnx_hca_t		*p_hca = NULL;

	cl_spinlock_acquire( &hca_lock );
	p_item = cl_qlist_head( &mlnx_hca_list );
	while( p_item != cl_qlist_end( &mlnx_hca_list ) )
	{
		p_hca = PARENT_STRUCT( p_item, mlnx_hca_t, list_item );
		if( p_hca->guid == guid )
			break;
		p_item = cl_qlist_next( p_item );
		p_hca = NULL;
	}
	cl_spinlock_release( &hca_lock );
	return p_hca;
}

/////////////////////////////////////////////////////////
// ### HCA
/////////////////////////////////////////////////////////
cl_status_t
mlnx_hcas_init( void )
{
	cl_qlist_init( &mlnx_hca_list );
	return cl_spinlock_init( &hca_lock );
}


/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////

void ca_event_handler(struct ib_event *ev, void *context)
{
	mlnx_hca_t *p_hca = (mlnx_hca_t *)context;
	ib_event_rec_t event_rec;
	LIST_ENTRY *entry;
	ci_event_handler_t *event_handler;

	// prepare parameters
	event_rec.type = ev->event;
	event_rec.port_number = ev->element.port_num;
	if (event_rec.type > IB_AE_UNKNOWN) {
		// CL_ASSERT(0); // This shouldn't happen
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_SHIM,("Unmapped E_EV_CA event of type 0x%x. Replaced by 0x%x (IB_AE_LOCAL_FATAL)\n", 
			event_rec.type, IB_AE_LOCAL_FATAL));
		event_rec.type = IB_AE_LOCAL_FATAL;
	}

	// call the user callback
	KeAcquireSpinLockAtDpcLevel(&p_hca->event_list_lock);
	for (entry = p_hca->event_list.Flink; entry != &p_hca->event_list;
		 entry = entry->Flink) {

		event_handler = CONTAINING_RECORD(entry, ci_event_handler_t, entry);
		event_rec.context = (void *) event_handler;
		event_handler->pfn_async_event_cb(&event_rec);
	}
	KeReleaseSpinLockFromDpcLevel(&p_hca->event_list_lock);

	if (p_hca && p_hca->async_cb_p) {
		event_rec.context = (void *)p_hca->ca_context;
		(p_hca->async_cb_p)(&event_rec);
	} else {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("Incorrect context. Async callback was not invoked\n"));
	}
}


void event_handler( struct ib_event_handler *handler, struct ib_event *event )
{
	ca_event_handler( event, handler->ctx );
}

ib_api_status_t
mlnx_set_cb(
	IN				mlnx_hca_t				*	p_hca, 
	IN				ci_async_event_cb_t			async_cb_p,
	IN		const	void* const					ib_context)
{
	int err;
	cl_status_t		cl_status;
	struct ib_device *ibdev = hca2ibdev(p_hca);

	// Setup the callbacks
	if (!p_hca->async_proc_mgr_p)
	{
		p_hca->async_proc_mgr_p = cl_malloc( sizeof( cl_async_proc_t ) );
		if( !p_hca->async_proc_mgr_p )
		{
			return IB_INSUFFICIENT_MEMORY;
		}
		cl_async_proc_construct( p_hca->async_proc_mgr_p );
		cl_status = cl_async_proc_init( p_hca->async_proc_mgr_p, MLNX_NUM_CB_THR, "CBthread" );
		if( cl_status != CL_SUCCESS )
		{
			cl_async_proc_destroy( p_hca->async_proc_mgr_p );
			cl_free(p_hca->async_proc_mgr_p);
			p_hca->async_proc_mgr_p = NULL;
			return IB_INSUFFICIENT_RESOURCES;
		}
	}

	p_hca->async_cb_p = async_cb_p;
	p_hca->ca_context = ib_context; // This is the context our CB forwards to IBAL

	// register callback with bus driver
	INIT_IB_EVENT_HANDLER( &p_hca->ib_event_handler, ibdev, 
		event_handler, p_hca, NULL, 0, MLX4_PORT_TYPE_IB );
		
	err = ibdev->x.register_ev_cb(&p_hca->ib_event_handler);
	if (err) {
		RtlZeroMemory( &p_hca->ib_event_handler, sizeof(p_hca->ib_event_handler) );
		return IB_ERROR;
	}

	return IB_SUCCESS;
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void
mlnx_reset_cb(
	IN				mlnx_hca_t				*	p_hca)
{
	cl_async_proc_t	*p_async_proc;
	struct ib_device *ibdev = hca2ibdev(p_hca);

	cl_spinlock_acquire( &hca_lock );

	// unregister callback with bus driver
	if ( p_hca->ib_event_handler.handler )
		ibdev->x.unregister_ev_cb(&p_hca->ib_event_handler);

	p_async_proc = p_hca->async_proc_mgr_p;
	p_hca->async_proc_mgr_p = NULL;

	p_hca->async_cb_p = NULL;
	p_hca->ca_context = NULL;
	p_hca->cl_device_h = NULL;

	cl_spinlock_release( &hca_lock );

	if( p_async_proc )
	{
		cl_async_proc_destroy( p_async_proc );
		cl_free( p_async_proc );
	}

}

/////////////////////////////////////////////////////////
void
from_port_cap(
	IN				u32			mthca_port_cap,
		OUT			ib_port_cap_t				*ibal_port_cap_p)
{
#define SET_CAP(flag,cap) 	if (mthca_port_cap & flag) ibal_port_cap_p->cap = TRUE

	SET_CAP(IB_PORT_CM_SUP,cm);
	SET_CAP(IB_PORT_SNMP_TUNNEL_SUP,snmp);
	SET_CAP(IB_PORT_DEVICE_MGMT_SUP,dev_mgmt);
	SET_CAP(IB_PORT_VENDOR_CLASS_SUP,vend);
	SET_CAP(IB_PORT_SM_DISABLED,sm_disable);
	SET_CAP(IB_PORT_SM,sm);
	SET_CAP(IB_PORT_NOTICE_SUP,notice);
	SET_CAP(IB_PORT_TRAP_SUP,trap);
	SET_CAP(IB_PORT_AUTO_MIGR_SUP,apm);
	SET_CAP(IB_PORT_SL_MAP_SUP,slmap);
	SET_CAP(IB_PORT_LED_INFO_SUP,ledinfo);
	SET_CAP(IB_PORT_CAP_MASK_NOTICE_SUP,capm_notice);
	SET_CAP(IB_PORT_CLIENT_REG_SUP,client_reregister);
	SET_CAP(IB_PORT_SYS_IMAGE_GUID_SUP,sysguid);
	SET_CAP(IB_PORT_BOOT_MGMT_SUP,boot_mgmt);
	SET_CAP(IB_PORT_DR_NOTICE_SUP,dr_notice);
	SET_CAP(IB_PORT_PKEY_SW_EXT_PORT_TRAP_SUP,pkey_switch_ext_port);
	SET_CAP(IB_PORT_LINK_LATENCY_SUP,link_rtl);
	SET_CAP(IB_PORT_REINIT_SUP,reinit);
	SET_CAP(IB_PORT_OPT_IPD_SUP,ipd);
	SET_CAP(IB_PORT_MKEY_NVRAM,mkey_nvram);
	SET_CAP(IB_PORT_PKEY_NVRAM,pkey_nvram);
	// there no MTHCA flags for qkey_ctr, pkey_ctr, port_active, bm IBAL capabilities;
}


/////////////////////////////////////////////////////////
void
from_hca_cap(
	IN				struct ib_device *ib_dev,
	IN				struct ib_device_attr *hca_info_p,
	IN				struct ib_port_attr  *hca_ports,
	OUT			ib_ca_attr_t 				*ca_attr_p)
{
	uint8_t			port_num;
	ib_port_attr_t  *ibal_port_p;
	struct ib_port_attr  *mthca_port_p;

	ca_attr_p->vend_id  = hca_info_p->vendor_id;
	ca_attr_p->dev_id   = (uint16_t)hca_info_p->vendor_part_id;
	ca_attr_p->revision = (uint16_t)hca_info_p->hw_ver;
	ca_attr_p->fw_ver = hca_info_p->fw_ver;
	ca_attr_p->ca_guid   = *(UNALIGNED64 uint64_t *)&ib_dev->node_guid;
	ca_attr_p->num_ports = ib_dev->phys_port_cnt;
	ca_attr_p->max_qps   = hca_info_p->max_qp;
	ca_attr_p->max_wrs   = hca_info_p->max_qp_wr;
	ca_attr_p->max_sges   = hca_info_p->max_sge;
	ca_attr_p->max_rd_sges = hca_info_p->max_sge_rd;
	ca_attr_p->max_cqs    = hca_info_p->max_cq;
	ca_attr_p->max_cqes  = hca_info_p->max_cqe;
	ca_attr_p->max_pds    = hca_info_p->max_pd;
	ca_attr_p->init_regions = hca_info_p->max_mr;
	ca_attr_p->init_windows = hca_info_p->max_mw;
	ca_attr_p->init_region_size = hca_info_p->max_mr_size;
	ca_attr_p->max_addr_handles = hca_info_p->max_ah;
	ca_attr_p->atomicity     = hca_info_p->atomic_cap;
	ca_attr_p->max_partitions = hca_info_p->max_pkeys;
	ca_attr_p->max_qp_resp_res =(uint8_t) hca_info_p->max_qp_rd_atom;
	ca_attr_p->max_resp_res    = (uint8_t)hca_info_p->max_res_rd_atom;
	ca_attr_p->max_qp_init_depth = (uint8_t)hca_info_p->max_qp_init_rd_atom;
	ca_attr_p->max_ipv6_qps    = hca_info_p->max_raw_ipv6_qp;
	ca_attr_p->max_ether_qps   = hca_info_p->max_raw_ethy_qp;
	ca_attr_p->max_mcast_grps  = hca_info_p->max_mcast_grp;
	ca_attr_p->max_mcast_qps   = hca_info_p->max_total_mcast_qp_attach;
	ca_attr_p->max_qps_per_mcast_grp = hca_info_p->max_mcast_qp_attach;
	ca_attr_p->max_fmr   = hca_info_p->max_fmr;
	ca_attr_p->max_map_per_fmr   = hca_info_p->max_map_per_fmr;
	ca_attr_p->max_srq = hca_info_p->max_srq;
	ca_attr_p->max_srq_wrs = hca_info_p->max_srq_wr;
	ca_attr_p->max_srq_sges = hca_info_p->max_srq_sge;
	ca_attr_p->system_image_guid = hca_info_p->sys_image_guid;

	ca_attr_p->local_ack_delay = hca_info_p->local_ca_ack_delay;
	ca_attr_p->bad_pkey_ctr_support = hca_info_p->device_cap_flags & IB_DEVICE_BAD_PKEY_CNTR;
	ca_attr_p->bad_qkey_ctr_support = hca_info_p->device_cap_flags & IB_DEVICE_BAD_QKEY_CNTR;
	ca_attr_p->raw_mcast_support    = hca_info_p->device_cap_flags & IB_DEVICE_RAW_MULTI;
	ca_attr_p->apm_support          = hca_info_p->device_cap_flags & IB_DEVICE_AUTO_PATH_MIG;
	ca_attr_p->av_port_check        = hca_info_p->device_cap_flags & IB_DEVICE_UD_AV_PORT_ENFORCE;
	ca_attr_p->change_primary_port  = hca_info_p->device_cap_flags & IB_DEVICE_CHANGE_PHY_PORT;
	ca_attr_p->modify_wr_depth      = hca_info_p->device_cap_flags & IB_DEVICE_RESIZE_MAX_WR;
	ca_attr_p->modify_srq_depth      = hca_info_p->device_cap_flags & IB_DEVICE_SRQ_RESIZE;
	ca_attr_p->system_image_guid_support = hca_info_p->device_cap_flags & IB_DEVICE_SYS_IMAGE_GUID;
	ca_attr_p->hw_agents            = FALSE; // in the context of IBAL then agent is implemented on the host
	ca_attr_p->ipoib_csum           = hca_info_p->device_cap_flags & IB_DEVICE_IPOIB_CSUM;
	ca_attr_p->bf_reg_size			= (uint32_t)hca_info_p->bf_reg_size;
	ca_attr_p->bf_regs_per_page 	= (uint32_t)hca_info_p->bf_regs_per_page;
	ca_attr_p->max_sq_desc_sz		= (uint32_t)hca_info_p->max_sq_desc_sz;

	ca_attr_p->num_page_sizes = 1;
	ca_attr_p->p_page_size[0] = PAGE_SIZE; // TBD: extract an array of page sizes from HCA cap

	if ( hca_ports )
		for (port_num = 0; port_num <= (end_port(ib_dev) - start_port(ib_dev)); ++port_num)
		{
			// Setup port pointers
			ibal_port_p = &ca_attr_p->p_port_attr[port_num];
			mthca_port_p = &hca_ports[port_num];

			// Port Cabapilities
			cl_memclr(&ibal_port_p->cap, sizeof(ib_port_cap_t));
			from_port_cap(mthca_port_p->port_cap_flags, &ibal_port_p->cap);

			// Port Atributes
			ibal_port_p->port_num   = (u8)(port_num + start_port(ib_dev));
			ibal_port_p->port_guid  = ibal_port_p->p_gid_table[0].unicast.interface_id;
			ibal_port_p->lid        = cl_ntoh16(mthca_port_p->lid);
			ibal_port_p->lmc        = mthca_port_p->lmc;
			ibal_port_p->max_vls    = mthca_port_p->max_vl_num;
			ibal_port_p->sm_lid     = cl_ntoh16(mthca_port_p->sm_lid);
			ibal_port_p->sm_sl      = mthca_port_p->sm_sl;
			ibal_port_p->transport  = mthca_port_p->transport;
			ibal_port_p->link_state = (mthca_port_p->state != 0) ? (uint8_t)mthca_port_p->state : IB_LINK_DOWN;
			ibal_port_p->num_gids   = (uint16_t)mthca_port_p->gid_tbl_len;
			ibal_port_p->num_pkeys  = mthca_port_p->pkey_tbl_len;
			ibal_port_p->pkey_ctr   = (uint16_t)mthca_port_p->bad_pkey_cntr;
			ibal_port_p->qkey_ctr   = (uint16_t)mthca_port_p->qkey_viol_cntr;
			ibal_port_p->max_msg_size = mthca_port_p->max_msg_sz;
			ibal_port_p->mtu = (uint8_t)mthca_port_p->max_mtu;
			ibal_port_p->active_speed = mthca_port_p->active_speed;
			ibal_port_p->active_width = mthca_port_p->active_width;
			ibal_port_p->phys_state = mthca_port_p->phys_state;
			ibal_port_p->ext_active_speed = mthca_port_p->ext_active_speed;
			ibal_port_p->link_encoding = mthca_port_p->link_encoding;

			ibal_port_p->subnet_timeout = mthca_port_p->subnet_timeout;
			// ibal_port_p->local_ack_timeout = 3; // TBD: currently ~32 usec
			HCA_PRINT(TRACE_LEVEL_VERBOSE, HCA_DBG_SHIM ,("Port %d port_guid 0x%I64x\n",
				ibal_port_p->port_num, cl_ntoh64(ibal_port_p->port_guid)));
		}
}

enum ib_rate to_rate(uint8_t rate)
{
	if (rate == IB_PATH_RECORD_RATE_2_5_GBS) return IB_RATE_2_5_GBPS;
	if (rate == IB_PATH_RECORD_RATE_5_GBS) return IB_RATE_5_GBPS;
	if (rate == IB_PATH_RECORD_RATE_10_GBS) return IB_RATE_10_GBPS;
	if (rate == IB_PATH_RECORD_RATE_20_GBS) return IB_RATE_20_GBPS;
	if (rate == IB_PATH_RECORD_RATE_30_GBS) return IB_RATE_30_GBPS;
	if (rate == IB_PATH_RECORD_RATE_40_GBS) return IB_RATE_40_GBPS;
	if (rate == IB_PATH_RECORD_RATE_60_GBS) return IB_RATE_60_GBPS;
	if (rate == IB_PATH_RECORD_RATE_80_GBS) return IB_RATE_80_GBPS;
	if (rate == IB_PATH_RECORD_RATE_120_GBS) return IB_RATE_120_GBPS;
	if (rate == IB_PATH_RECORD_RATE_14_GBS) return IB_RATE_14_GBPS;
	if (rate == IB_PATH_RECORD_RATE_56_GBS) return IB_RATE_56_GBPS;
	if (rate == IB_PATH_RECORD_RATE_112_GBS) return IB_RATE_112_GBPS;
	if (rate == IB_PATH_RECORD_RATE_168_GBS) return IB_RATE_168_GBPS;
	if (rate == IB_PATH_RECORD_RATE_25_GBS) return IB_RATE_25_GBPS;
	if (rate == IB_PATH_RECORD_RATE_100_GBS) return IB_RATE_100_GBPS;
	if (rate == IB_PATH_RECORD_RATE_200_GBS) return IB_RATE_200_GBPS;
	if (rate == IB_PATH_RECORD_RATE_300_GBS) return IB_RATE_300_GBPS;
	return IB_RATE_PORT_CURRENT;
}

uint8_t from_rate(enum ib_rate ib_rate)
{
	if (ib_rate == IB_RATE_2_5_GBPS) return IB_PATH_RECORD_RATE_2_5_GBS;
	if (ib_rate == IB_RATE_5_GBPS) return IB_PATH_RECORD_RATE_5_GBS;
	if (ib_rate == IB_RATE_10_GBPS) return IB_PATH_RECORD_RATE_10_GBS;
	if (ib_rate == IB_RATE_20_GBPS) return IB_PATH_RECORD_RATE_20_GBS;
	if (ib_rate == IB_RATE_30_GBPS) return IB_PATH_RECORD_RATE_30_GBS;
	if (ib_rate == IB_RATE_40_GBPS) return IB_PATH_RECORD_RATE_40_GBS;
	if (ib_rate == IB_RATE_60_GBPS) return IB_PATH_RECORD_RATE_60_GBS;
	if (ib_rate == IB_RATE_80_GBPS) return IB_PATH_RECORD_RATE_80_GBS;
	if (ib_rate == IB_RATE_120_GBPS) return IB_PATH_RECORD_RATE_120_GBS;
	if (ib_rate == IB_RATE_14_GBPS) return IB_PATH_RECORD_RATE_14_GBS;
	if (ib_rate == IB_RATE_56_GBPS) return IB_PATH_RECORD_RATE_56_GBS;
	if (ib_rate == IB_RATE_112_GBPS) return IB_PATH_RECORD_RATE_112_GBS;
	if (ib_rate == IB_RATE_168_GBPS) return IB_PATH_RECORD_RATE_168_GBS;
	if (ib_rate == IB_RATE_25_GBPS) return IB_PATH_RECORD_RATE_25_GBS;
	if (ib_rate == IB_RATE_100_GBPS) return IB_PATH_RECORD_RATE_100_GBS;
	if (ib_rate == IB_RATE_200_GBPS) return IB_PATH_RECORD_RATE_200_GBS;
	if (ib_rate == IB_RATE_300_GBPS) return IB_PATH_RECORD_RATE_300_GBS;
	return 0;
}

int
to_av(
	IN		const	struct ib_device 	*p_ib_dev,
	IN		const	ib_av_attr_t		*p_ib_av_attr,
	OUT			struct ib_ah_attr		*p_ib_ah_attr)
{
	int err = 0;
	u8 port_num;
	u16 gid_index;
	
	p_ib_ah_attr->port_num = p_ib_av_attr->port_num;
	p_ib_ah_attr->sl   = p_ib_av_attr->sl;
	p_ib_ah_attr->dlid = cl_ntoh16(p_ib_av_attr->dlid);
	p_ib_ah_attr->static_rate = to_rate(p_ib_av_attr->static_rate);
	p_ib_ah_attr->src_path_bits = p_ib_av_attr->path_bits; // PATH:

	/* For global destination or Multicast address:*/
	if (p_ib_av_attr->grh_valid) {
		p_ib_ah_attr->ah_flags |= IB_AH_GRH;
		p_ib_ah_attr->grh.hop_limit     = p_ib_av_attr->grh.hop_limit;
		ib_grh_get_ver_class_flow( p_ib_av_attr->grh.ver_class_flow, NULL,
			&p_ib_ah_attr->grh.traffic_class, &p_ib_ah_attr->grh.flow_label );
		err = p_ib_dev->x.find_cached_gid((struct ib_device *)p_ib_dev, 
			(union ib_gid	*)p_ib_av_attr->grh.src_gid.raw, &port_num, &gid_index);
		if (err) {
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,
			("ib_find_cached_gid failed %d (%#x). Using default:  sgid_index = 0\n", err, err));
			gid_index = 0;
		}
		else if (port_num != p_ib_ah_attr->port_num) {
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,
				("ib_find_cached_gid returned wrong port_num %u (Expected - %u). Using the expected.\n", 
				(u32)port_num, (u32)p_ib_ah_attr->port_num));
		}
		p_ib_ah_attr->grh.sgid_index = (u8)gid_index;
		RtlCopyMemory(p_ib_ah_attr->grh.dgid.raw, 
			p_ib_av_attr->grh.dest_gid.raw, sizeof(p_ib_ah_attr->grh.dgid));
	}
	else
		p_ib_ah_attr->ah_flags = 0;

	return err;
}

int from_av(
	IN		const	struct ib_device 	*p_ib_dev,
	IN 			struct ib_qp_attr 		*p_ib_qp_attr,
	IN			struct ib_ah_attr		*p_ib_ah_attr,
	OUT			ib_av_attr_t			*p_ib_av_attr)
{
	int err = 0;
	
	p_ib_av_attr->port_num					= p_ib_ah_attr->port_num;
	p_ib_av_attr->sl						= p_ib_ah_attr->sl;
	p_ib_av_attr->dlid						= cl_hton16(p_ib_ah_attr->dlid);
	p_ib_av_attr->static_rate				= from_rate(p_ib_ah_attr->static_rate);
	p_ib_av_attr->path_bits					= p_ib_ah_attr->src_path_bits;

	if (p_ib_qp_attr) {
		p_ib_av_attr->conn.path_mtu				= p_ib_qp_attr->path_mtu; // MTU
		p_ib_av_attr->conn.local_ack_timeout	= p_ib_qp_attr->timeout; // MTU
		p_ib_av_attr->conn.seq_err_retry_cnt	= p_ib_qp_attr->retry_cnt; // MTU
		p_ib_av_attr->conn.rnr_retry_cnt		= p_ib_qp_attr->rnr_retry; // MTU
	}

	if (p_ib_ah_attr->ah_flags & IB_AH_GRH) {
		p_ib_av_attr->grh_valid = TRUE;
		p_ib_av_attr->grh.hop_limit = p_ib_ah_attr->grh.hop_limit;
		p_ib_av_attr->grh.ver_class_flow = ib_grh_set_ver_class_flow(
			0, p_ib_ah_attr->grh.traffic_class, p_ib_ah_attr->grh.flow_label );
		RtlCopyMemory(p_ib_av_attr->grh.dest_gid.raw, 
			p_ib_ah_attr->grh.dgid.raw, sizeof(p_ib_av_attr->grh.dest_gid));
		err = p_ib_dev->x.get_cached_gid((struct ib_device *)p_ib_dev, 
			p_ib_ah_attr->port_num, p_ib_ah_attr->grh.sgid_index,
			(union ib_gid*)p_ib_av_attr->grh.src_gid.raw );
		if (err) {
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,
			("ib_get_cached_gid failed %d (%#x). Using default:  sgid_index = 0\n", err, err));
		}
	}
	else
		p_ib_av_attr->grh_valid = FALSE;
		

	return err;
}

enum ib_access_flags
to_qp_acl(
	IN				ib_access_t					ibal_acl)
{
#define IBAL_ACL(ifl,mfl) if (ibal_acl & ifl) acc |= mfl
	enum ib_access_flags acc = 0;

	IBAL_ACL(IB_AC_RDMA_READ,IB_ACCESS_REMOTE_READ);
	IBAL_ACL(IB_AC_RDMA_WRITE,IB_ACCESS_REMOTE_WRITE);
	IBAL_ACL(IB_AC_ATOMIC,IB_ACCESS_REMOTE_ATOMIC);
	IBAL_ACL(IB_AC_LOCAL_WRITE,IB_ACCESS_LOCAL_WRITE);
	IBAL_ACL(IB_AC_MW_BIND,IB_ACCESS_MW_BIND);
	IBAL_ACL(IB_AC_NOT_CACHABLE,IB_ACCESS_NO_SECURE);

	return acc;
}

ib_access_t
from_qp_acl(
	IN		enum ib_access_flags			acc)
{
#define IB_ACL(ifl,mfl) if (acc & ifl) ibal_acl |= mfl
	ib_access_t ibal_acl = 0;

	IB_ACL(IB_ACCESS_REMOTE_READ,IB_AC_RDMA_READ);
	IB_ACL(IB_ACCESS_REMOTE_WRITE,IB_AC_RDMA_WRITE);
	IB_ACL(IB_ACCESS_REMOTE_ATOMIC,IB_AC_ATOMIC);
	IB_ACL(IB_ACCESS_LOCAL_WRITE,IB_AC_LOCAL_WRITE);
	IB_ACL(IB_ACCESS_MW_BIND,IB_AC_MW_BIND);
	IB_ACL(IB_ACCESS_NO_SECURE,IB_AC_NOT_CACHABLE);

	return ibal_acl;
}

static enum ib_qp_state to_qp_state(ib_qp_state_t ib_qps)
{
#define MAP_XIB_QPS(val1,val2) case val1: qps = val2; break
	enum ib_qp_state qps;
	switch (ib_qps) {
		MAP_XIB_QPS( IB_QPS_RESET, XIB_QPS_RESET );
		MAP_XIB_QPS( IB_QPS_INIT, XIB_QPS_INIT );
		MAP_XIB_QPS( IB_QPS_RTR, XIB_QPS_RTR );
		MAP_XIB_QPS( IB_QPS_RTS, XIB_QPS_RTS );
		MAP_XIB_QPS( IB_QPS_SQD, XIB_QPS_SQD );
		MAP_XIB_QPS( IB_QPS_SQD_DRAINING, XIB_QPS_SQD );
		MAP_XIB_QPS( IB_QPS_SQD_DRAINED, XIB_QPS_SQD );
		MAP_XIB_QPS( IB_QPS_SQERR, XIB_QPS_SQE );
		MAP_XIB_QPS( IB_QPS_ERROR, XIB_QPS_ERR );
		default:
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("Unmapped IBAL qp_state %d\n", ib_qps));
			qps = 0xffffffff;
	}
	return qps;
}

static ib_qp_state_t  from_qp_state(enum ib_qp_state qps, int draining)
{
#define MAP_IB_QPS(val1,val2) case val1: ib_qps = val2; break
	ib_qp_state_t ib_qps;

	if (qps == XIB_QPS_SQD) {
		ib_qps = draining ? IB_QPS_SQD_DRAINING : IB_QPS_SQD;
		return ib_qps;
	}

	switch (qps) {
		MAP_IB_QPS( XIB_QPS_RESET, IB_QPS_RESET );
		MAP_IB_QPS( XIB_QPS_INIT, IB_QPS_INIT );
		MAP_IB_QPS( XIB_QPS_RTR, IB_QPS_RTR );
		MAP_IB_QPS( XIB_QPS_RTS, IB_QPS_RTS );
		MAP_IB_QPS( XIB_QPS_SQE, IB_QPS_SQERR );
		MAP_IB_QPS( XIB_QPS_ERR, IB_QPS_ERROR );
		default:
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("Unmapped IBAL qp_state %d\n", qps));
			ib_qps = 0xffffffff;
	}
	return ib_qps;
}

enum ib_mig_state to_apm_state(ib_apm_state_t apm)
{
	if (apm == IB_APM_MIGRATED) return IB_MIG_MIGRATED;
	if (apm == IB_APM_REARM) return IB_MIG_REARM;
	if (apm == IB_APM_ARMED) return IB_MIG_ARMED;
	return 0xffffffff;
}

ib_api_status_t
to_qp_attr(
	IN	 const	struct ib_qp *p_ib_qp,
	IN				ib_qp_type_t	qp_type,
	IN	 const	ib_qp_mod_t *p_ib_qp_mod,		
	OUT 	struct ib_qp_attr *p_ib_qp_attr,
	OUT 	int *p_qp_attr_mask
	)
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct mlx4_ib_qp *p_mib_qp = (struct mlx4_ib_qp *)p_ib_qp;

	RtlZeroMemory( p_ib_qp_attr, sizeof *p_ib_qp_attr );
	*p_qp_attr_mask = IB_QP_STATE;
	p_ib_qp_attr->qp_state = to_qp_state( p_ib_qp_mod->req_state ); 

	// skipped cases
	if (p_mib_qp->state == XIB_QPS_RESET && p_ib_qp_mod->req_state != IB_QPS_INIT)
		return IB_NOT_DONE;
		
	switch (p_ib_qp_mod->req_state) {
	case IB_QPS_RESET:
	case IB_QPS_ERROR:
	case IB_QPS_SQERR:
	case IB_QPS_TIME_WAIT:
		break;

	case IB_QPS_INIT:
		
		switch (qp_type) {
			case IB_QPT_RELIABLE_CONN:
			case IB_QPT_UNRELIABLE_CONN:
				*p_qp_attr_mask |= IB_QP_PORT | IB_QP_PKEY_INDEX |IB_QP_ACCESS_FLAGS;
				p_ib_qp_attr->qp_access_flags = to_qp_acl(p_ib_qp_mod->state.init.access_ctrl);
				break;
			case IB_QPT_QP0:
			case IB_QPT_QP1:
				// TODO: these cases had IB_QP_PORT in mthca
				// TODO: they do not pass ib_modify_qp_is_ok control here
				*p_qp_attr_mask |= IB_QP_QKEY | IB_QP_PKEY_INDEX ;
				p_ib_qp_attr->qkey 	 = cl_ntoh32 (p_ib_qp_mod->state.init.qkey);
				break;
			case IB_QPT_UNRELIABLE_DGRM:
			default:	
				*p_qp_attr_mask |= IB_QP_PORT | IB_QP_QKEY | IB_QP_PKEY_INDEX ;
				p_ib_qp_attr->qkey 	 = cl_ntoh32 (p_ib_qp_mod->state.init.qkey);
				break;
		}				
		
		// IB_QP_PORT
		p_ib_qp_attr->port_num    = p_ib_qp_mod->state.init.primary_port;

		// IB_QP_PKEY_INDEX
		p_ib_qp_attr->pkey_index = p_ib_qp_mod->state.init.pkey_index;

		break;
		
	case IB_QPS_RTR:
		/* modifying the WQE depth is not supported */
		if( p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_SQ_DEPTH ||
			p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_RQ_DEPTH )	{
			status = IB_UNSUPPORTED;
			break;
		}

		switch (qp_type) {
			case IB_QPT_RELIABLE_CONN:
				*p_qp_attr_mask |= /* required flags */
					IB_QP_DEST_QPN |IB_QP_RQ_PSN | IB_QP_MAX_DEST_RD_ATOMIC |
					IB_QP_AV |IB_QP_PATH_MTU | IB_QP_MIN_RNR_TIMER;

				// IB_QP_DEST_QPN
				p_ib_qp_attr->dest_qp_num		= cl_ntoh32 (p_ib_qp_mod->state.rtr.dest_qp);

				// IB_QP_RQ_PSN
				p_ib_qp_attr->rq_psn 				= cl_ntoh32 (p_ib_qp_mod->state.rtr.rq_psn);
				
				// IB_QP_MAX_DEST_RD_ATOMIC
				p_ib_qp_attr->max_dest_rd_atomic	= p_ib_qp_mod->state.rtr.resp_res;

				// IB_QP_AV, IB_QP_PATH_MTU: Convert primary RC AV (mandatory)
				err = to_av(p_ib_qp->device,
					&p_ib_qp_mod->state.rtr.primary_av, &p_ib_qp_attr->ah_attr);
				if (err) {
					status = IB_ERROR;
					break;
				}
				p_ib_qp_attr->path_mtu 		= p_ib_qp_mod->state.rtr.primary_av.conn.path_mtu; // MTU
				p_ib_qp_attr->timeout 		= p_ib_qp_mod->state.rtr.primary_av.conn.local_ack_timeout; // MTU
				p_ib_qp_attr->retry_cnt 		= p_ib_qp_mod->state.rtr.primary_av.conn.seq_err_retry_cnt; // MTU
				p_ib_qp_attr->rnr_retry 		= p_ib_qp_mod->state.rtr.primary_av.conn.rnr_retry_cnt; // MTU

				// IB_QP_MIN_RNR_TIMER, required in RTR, optional in RTS.
				p_ib_qp_attr->min_rnr_timer	 = p_ib_qp_mod->state.rtr.rnr_nak_timeout;

				// IB_QP_ACCESS_FLAGS: Convert Remote Atomic Flags
				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_ACCESS_CTRL) {
					*p_qp_attr_mask |= IB_QP_ACCESS_FLAGS;		/* optional flag */
					p_ib_qp_attr->qp_access_flags = to_qp_acl(p_ib_qp_mod->state.rtr.access_ctrl);
				}

				// IB_QP_ALT_PATH: Convert alternate RC AV
				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_ALTERNATE_AV) {
					*p_qp_attr_mask |= IB_QP_ALT_PATH;	/* required flag */
					err = to_av(p_ib_qp->device,
						&p_ib_qp_mod->state.rtr.alternate_av, &p_ib_qp_attr->alt_ah_attr);
					if (err) {
						status = IB_ERROR;
						break;
					}
					p_ib_qp_attr->alt_timeout		 = p_ib_qp_mod->state.rtr.alternate_av.conn.local_ack_timeout; // XXX: conv
					p_ib_qp_attr->alt_port_num		= p_ib_qp_mod->state.rtr.alternate_av.port_num;
				}

				// IB_QP_PKEY_INDEX 
				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_PKEY) {
					*p_qp_attr_mask |= IB_QP_PKEY_INDEX;	
					p_ib_qp_attr->pkey_index = p_ib_qp_mod->state.rtr.pkey_index;
				}

				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_GENERATE_PRIO_TAG) {
					*p_qp_attr_mask |= IB_QP_GENERATE_PRIO_TAG;	
				}
				break;
				
			case IB_QPT_UNRELIABLE_CONN:
				*p_qp_attr_mask |= /* required flags */
					IB_QP_DEST_QPN |IB_QP_RQ_PSN | IB_QP_AV | IB_QP_PATH_MTU;

				// IB_QP_DEST_QPN
				p_ib_qp_attr->dest_qp_num		= cl_ntoh32 (p_ib_qp_mod->state.rtr.dest_qp);

				// IB_QP_RQ_PSN
				p_ib_qp_attr->rq_psn 				= cl_ntoh32 (p_ib_qp_mod->state.rtr.rq_psn);

				// IB_QP_PATH_MTU
				p_ib_qp_attr->path_mtu 		= p_ib_qp_mod->state.rtr.primary_av.conn.path_mtu;

				// IB_QP_AV: Convert primary AV (mandatory)
				err = to_av(p_ib_qp->device,
					&p_ib_qp_mod->state.rtr.primary_av, &p_ib_qp_attr->ah_attr);
				if (err) {
					status = IB_ERROR;
					break;
				}

				// IB_QP_ACCESS_FLAGS: Convert Remote Atomic Flags
				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_ACCESS_CTRL) {
					*p_qp_attr_mask |= IB_QP_ACCESS_FLAGS;		/* optional flag */
					p_ib_qp_attr->qp_access_flags = to_qp_acl(p_ib_qp_mod->state.rtr.access_ctrl);
				}

				// IB_QP_ALT_PATH: Convert alternate RC AV
				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_ALTERNATE_AV) {
					*p_qp_attr_mask |= IB_QP_ALT_PATH;	/* required flag */
					err = to_av(p_ib_qp->device,
						&p_ib_qp_mod->state.rtr.alternate_av, &p_ib_qp_attr->alt_ah_attr);
					if (err) {
						status = IB_ERROR;
						break;
					}
					p_ib_qp_attr->alt_timeout		= p_ib_qp_mod->state.rtr.alternate_av.conn.local_ack_timeout; // XXX: conv
					p_ib_qp_attr->alt_port_num		= p_ib_qp_mod->state.rtr.alternate_av.port_num;
				}

				// IB_QP_PKEY_INDEX 
				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_PKEY) {
					*p_qp_attr_mask |= IB_QP_PKEY_INDEX;	
					p_ib_qp_attr->pkey_index = p_ib_qp_mod->state.rtr.pkey_index;
				}
				break;
					
			case IB_QPT_UNRELIABLE_DGRM:
			case IB_QPT_QP0:
			case IB_QPT_QP1:
			default:	
				// IB_QP_PKEY_INDEX 
				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_PKEY) {
					*p_qp_attr_mask |= IB_QP_PKEY_INDEX;	
					p_ib_qp_attr->pkey_index = p_ib_qp_mod->state.rtr.pkey_index;
				}

				// IB_QP_QKEY
				if (p_ib_qp_mod->state.rtr.opts & IB_MOD_QP_QKEY) {
					*p_qp_attr_mask |= IB_QP_QKEY;	
					p_ib_qp_attr->qkey 	 = cl_ntoh32 (p_ib_qp_mod->state.rtr.qkey);
				}
				break;
				
		}
		break;
		
	case IB_QPS_RTS:
		/* modifying the WQE depth is not supported */
		if( p_ib_qp_mod->state.rts.opts & IB_MOD_QP_SQ_DEPTH ||
			p_ib_qp_mod->state.rts.opts & IB_MOD_QP_RQ_DEPTH )
		{
			status = IB_UNSUPPORTED;
			break;
		}

		switch (qp_type) {
			case IB_QPT_RELIABLE_CONN:
				if (p_mib_qp->state != XIB_QPS_RTS)
					*p_qp_attr_mask |= /* required flags */
						IB_QP_SQ_PSN |IB_QP_MAX_QP_RD_ATOMIC | IB_QP_TIMEOUT |
						IB_QP_RETRY_CNT |IB_QP_RNR_RETRY;

				// IB_QP_MAX_QP_RD_ATOMIC
				p_ib_qp_attr->max_rd_atomic	= p_ib_qp_mod->state.rts.init_depth;

				// IB_QP_TIMEOUT
				p_ib_qp_attr->timeout		 = p_ib_qp_mod->state.rts.local_ack_timeout; // XXX: conv
				
				// IB_QP_RETRY_CNT
				p_ib_qp_attr->retry_cnt = p_ib_qp_mod->state.rts.retry_cnt;
				
				// IB_QP_RNR_RETRY
				p_ib_qp_attr->rnr_retry	 = p_ib_qp_mod->state.rts.rnr_retry_cnt;

				// IB_QP_MAX_DEST_RD_ATOMIC: Update the responder resources for RDMA/ATOMIC (optional for SQD->RTS)
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_RESP_RES) {
					*p_qp_attr_mask |= IB_QP_MAX_DEST_RD_ATOMIC;	
					p_ib_qp_attr->max_dest_rd_atomic = p_ib_qp_mod->state.rts.resp_res;
				}

#ifdef WIN_TO_BE_REMOVED
		//TODO: do we need that ?
		// Linux patch 4793: PKEY_INDEX is not a legal parameter in the RTR->RTS transition.

				// IB_QP_PKEY_INDEX 
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_PKEY) {
					*p_qp_attr_mask |= IB_QP_PKEY_INDEX;	
					p_ib_qp_attr->pkey_index = p_ib_qp_mod->state.rts.pkey_index;
				}
#endif				

				// IB_QP_MIN_RNR_TIMER
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_RNR_NAK_TIMEOUT) {
					*p_qp_attr_mask |= IB_QP_MIN_RNR_TIMER;	
					p_ib_qp_attr->min_rnr_timer	 = p_ib_qp_mod->state.rts.rnr_nak_timeout;
				}

				// IB_QP_PATH_MIG_STATE
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_APM_STATE) {
					*p_qp_attr_mask |= IB_QP_PATH_MIG_STATE;	
					p_ib_qp_attr->path_mig_state =  to_apm_state(p_ib_qp_mod->state.rts.apm_state);
				}

				// IB_QP_ACCESS_FLAGS
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_ACCESS_CTRL) {
					*p_qp_attr_mask |= IB_QP_ACCESS_FLAGS;		/* optional flags */
					p_ib_qp_attr->qp_access_flags = to_qp_acl(p_ib_qp_mod->state.rts.access_ctrl);
				}

				// IB_QP_ALT_PATH: Convert alternate RC AV
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_ALTERNATE_AV) {
					*p_qp_attr_mask |= IB_QP_ALT_PATH;	/* optional flag */
					err = to_av(p_ib_qp->device,
						&p_ib_qp_mod->state.rts.alternate_av, &p_ib_qp_attr->alt_ah_attr);
					if (err) {
						status = IB_ERROR;
						break;
					}
					p_ib_qp_attr->alt_timeout		= p_ib_qp_mod->state.rts.alternate_av.conn.local_ack_timeout; // XXX: conv
					p_ib_qp_attr->alt_port_num		= p_ib_qp_mod->state.rts.alternate_av.port_num;
				}
				break;
				
			case IB_QPT_UNRELIABLE_CONN:
				if (p_mib_qp->state != XIB_QPS_RTS)
					*p_qp_attr_mask |= /* required flags */
						IB_QP_SQ_PSN;

				// IB_QP_MAX_DEST_RD_ATOMIC: Update the responder resources for RDMA/ATOMIC (optional for SQD->RTS)
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_RESP_RES) {
					*p_qp_attr_mask |= IB_QP_MAX_DEST_RD_ATOMIC;	
					p_ib_qp_attr->max_dest_rd_atomic = p_ib_qp_mod->state.rts.resp_res;
				}

#ifdef WIN_TO_BE_REMOVED
		//TODO: do we need that ?
		// Linux patch 4793: PKEY_INDEX is not a legal parameter in the RTR->RTS transition.

				// IB_QP_PKEY_INDEX 
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_PKEY) {
					*p_qp_attr_mask |= IB_QP_PKEY_INDEX;	
					p_ib_qp_attr->pkey_index = p_ib_qp_mod->state.rts.pkey_index;
				}
#endif				

				// IB_QP_PATH_MIG_STATE
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_APM_STATE) {
					*p_qp_attr_mask |= IB_QP_PATH_MIG_STATE;	
					p_ib_qp_attr->path_mig_state =  to_apm_state(p_ib_qp_mod->state.rts.apm_state);
				}

				// IB_QP_ACCESS_FLAGS
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_ACCESS_CTRL) {
					*p_qp_attr_mask |= IB_QP_ACCESS_FLAGS;		/* optional flags */
					p_ib_qp_attr->qp_access_flags = to_qp_acl(p_ib_qp_mod->state.rts.access_ctrl);
				}

				// IB_QP_ALT_PATH: Convert alternate RC AV
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_ALTERNATE_AV) {
					*p_qp_attr_mask |= IB_QP_ALT_PATH;	/* optional flag */
					err = to_av(p_ib_qp->device,
						&p_ib_qp_mod->state.rts.alternate_av, &p_ib_qp_attr->alt_ah_attr);
					if (err) {
						status = IB_ERROR;
						break;
					}
					p_ib_qp_attr->alt_timeout		= p_ib_qp_mod->state.rts.alternate_av.conn.local_ack_timeout; // XXX: conv
					p_ib_qp_attr->alt_port_num		= p_ib_qp_mod->state.rts.alternate_av.port_num;
				}
				break;
					
			case IB_QPT_UNRELIABLE_DGRM:
			case IB_QPT_QP0:
			case IB_QPT_QP1:
			default:	
				if (p_mib_qp->state != XIB_QPS_RTS)
					*p_qp_attr_mask |= /* required flags */
						IB_QP_SQ_PSN;

				// IB_QP_QKEY
				if (p_ib_qp_mod->state.rts.opts & IB_MOD_QP_QKEY) {
					*p_qp_attr_mask |= IB_QP_QKEY;	
					p_ib_qp_attr->qkey 	 = cl_ntoh32 (p_ib_qp_mod->state.rts.qkey);
				}
				break;
				
				break;
				
		}

		// IB_QP_SQ_PSN: common for all
		p_ib_qp_attr->sq_psn = cl_ntoh32 (p_ib_qp_mod->state.rts.sq_psn);
		//NB: IB_QP_CUR_STATE flag is not provisioned by IBAL
		break;
		
	case IB_QPS_SQD:
	case IB_QPS_SQD_DRAINING:
	case IB_QPS_SQD_DRAINED:
		*p_qp_attr_mask |= IB_QP_EN_SQD_ASYNC_NOTIFY;
		p_ib_qp_attr->en_sqd_async_notify = (u8)p_ib_qp_mod->state.sqd.sqd_event;
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_SHIM ,("IB_QP_EN_SQD_ASYNC_NOTIFY seems like unsupported\n"));
		break;
		
	default:	
		//NB: is this an error case and we need this message  ? What about returning an error ?
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("Unmapped qp_state %d\n", p_ib_qp_mod->req_state));
		break;
		
	}

	return status;
}	

enum ib_qp_type to_qp_type(ib_qp_type_t qp_type)
{
#define MAP_TYPE(val1,val2) case val1: ib_qp_type = val2; break
	enum ib_qp_type ib_qp_type;

	switch (qp_type) {
		MAP_TYPE( IB_QPT_RELIABLE_CONN, IB_QPT_RC );
		MAP_TYPE( IB_QPT_UNRELIABLE_CONN, IB_QPT_UC );
		MAP_TYPE( IB_QPT_UNRELIABLE_DGRM, IB_QPT_UD );
		MAP_TYPE( IB_QPT_QP0, IB_QPT_SMI );
		MAP_TYPE( IB_QPT_QP1, IB_QPT_GSI );
		MAP_TYPE( IB_QPT_RAW_IPV6, IB_QPT_RAW_IP_V6 );
		MAP_TYPE( IB_QPT_RAW_ETHER, IB_QPT_RAW_ETY );
		default:
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,
				("Unmapped MLX4 ib_wc_type %d\n", qp_type));
			ib_qp_type = 0xffffffff;
	}
	return ib_qp_type;
}

ib_qp_type_t  from_qp_type(enum ib_qp_type ib_qp_type)
{
#define MAP_IB_TYPE(val1,val2) case val1: qp_type = val2; break
	ib_qp_type_t qp_type;

	switch (ib_qp_type) {
		MAP_IB_TYPE( IB_QPT_RC, IB_QPT_RELIABLE_CONN );
		MAP_IB_TYPE( IB_QPT_UC, IB_QPT_UNRELIABLE_CONN );
		MAP_IB_TYPE( IB_QPT_UD, IB_QPT_UNRELIABLE_DGRM );
		MAP_IB_TYPE( IB_QPT_SMI, IB_QPT_QP0 );
		MAP_IB_TYPE( IB_QPT_GSI, IB_QPT_QP1 );
		MAP_IB_TYPE( IB_QPT_RAW_IP_V6, IB_QPT_RAW_IPV6 );
		MAP_IB_TYPE( IB_QPT_RAW_ETY, IB_QPT_RAW_ETHER );
		default:
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,
				("Unmapped MLX4 ib_wc_type %d\n", ib_qp_type));
			qp_type = 0xffffffff;
	}
	return qp_type;
}

ib_apm_state_t from_apm_state(enum ib_mig_state apm)
{
	if (apm == IB_MIG_MIGRATED) return IB_APM_MIGRATED;
	if (apm == IB_MIG_REARM) return IB_APM_REARM;
	if (apm == IB_MIG_ARMED) return IB_APM_ARMED;
	return 0xffffffff;
}

ib_api_status_t
from_qp_attr(
	IN	 const	struct ib_qp 	*p_ib_qp,
	IN 	struct ib_qp_attr 		*p_ib_qp_attr,
	OUT 	ib_qp_attr_t		*p_qp_attr
	)
{
	int err;
	RtlZeroMemory( p_qp_attr, sizeof *p_qp_attr );
	p_qp_attr->h_pd = (ib_pd_handle_t)p_ib_qp->pd;
	p_qp_attr->qp_type = from_qp_type(p_ib_qp->qp_type);
	p_qp_attr->access_ctrl = from_qp_acl(p_ib_qp_attr->qp_access_flags);
	p_qp_attr->pkey_index = p_ib_qp_attr->pkey_index;

	p_qp_attr->sq_max_inline = p_ib_qp_attr->cap.max_inline_data;
	p_qp_attr->sq_depth = p_ib_qp_attr->cap.max_send_wr;
	p_qp_attr->rq_depth = p_ib_qp_attr->cap.max_recv_wr;
	p_qp_attr->sq_sge = p_ib_qp_attr->cap.max_send_sge;
	p_qp_attr->rq_sge = p_ib_qp_attr->cap.max_recv_sge;
	p_qp_attr->init_depth = p_ib_qp_attr->max_rd_atomic;
	p_qp_attr->resp_res = p_ib_qp_attr->max_dest_rd_atomic;

	p_qp_attr->h_sq_cq = (ib_cq_handle_t)p_ib_qp->send_cq;
	p_qp_attr->h_rq_cq = (ib_cq_handle_t)p_ib_qp->recv_cq;
	p_qp_attr->h_srq = (ib_srq_handle_t)p_ib_qp->srq;

	p_qp_attr->sq_signaled = !!((struct mlx4_ib_qp *)p_ib_qp)->sq_signal_bits;

	p_qp_attr->state = from_qp_state( p_ib_qp_attr->qp_state, 
		p_ib_qp_attr->sq_draining);
	p_qp_attr->num = cl_hton32(p_ib_qp->qp_num);
	p_qp_attr->dest_num = cl_hton32(p_ib_qp_attr->dest_qp_num);
	p_qp_attr->qkey = cl_hton32(p_ib_qp_attr->qkey);

	p_qp_attr->sq_psn = cl_hton32(p_ib_qp_attr->sq_psn);
	p_qp_attr->rq_psn = cl_hton32(p_ib_qp_attr->rq_psn);

	p_qp_attr->primary_port = p_ib_qp_attr->port_num;
	p_qp_attr->alternate_port = p_ib_qp_attr->alt_port_num;
	err = from_av( p_ib_qp->device, p_ib_qp_attr, &p_ib_qp_attr->ah_attr, &p_qp_attr->primary_av);
	if (err)
		goto err_av;
	err = from_av( p_ib_qp->device, p_ib_qp_attr, &p_ib_qp_attr->alt_ah_attr, &p_qp_attr->alternate_av);
	if (err)
		goto err_av;
	p_qp_attr->apm_state = from_apm_state(p_ib_qp_attr->path_mig_state);

	return IB_SUCCESS;

err_av:
	return errno_to_iberr(err);
}

