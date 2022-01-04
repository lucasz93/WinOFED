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


#include "hca_driver.h"
#include "hca_utils.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_data.tmh"
#endif

#include "mthca_dev.h"
#include <ib_cache.h>

static cl_spinlock_t	hob_lock;



uint32_t		g_mlnx_dpc2thread = 0;


cl_qlist_t		mlnx_hca_list;

mlnx_hob_t		mlnx_hob_array[MLNX_NUM_HOBKL];		// kernel HOB - one per HCA (cmdif access)
mlnx_hobul_t	*mlnx_hobul_array[MLNX_NUM_HOBUL];	// kernel HOBUL - one per HCA (kar access)

/////////////////////////////////////////////////////////
// ### HCA
/////////////////////////////////////////////////////////
void
mlnx_hca_insert(
	IN				mlnx_hca_t					*p_hca )
{
	cl_spinlock_acquire( &hob_lock );
	cl_qlist_insert_tail( &mlnx_hca_list, &p_hca->list_item );
	cl_spinlock_release( &hob_lock );
}

void
mlnx_hca_remove(
	IN				mlnx_hca_t					*p_hca )
{
	cl_spinlock_acquire( &hob_lock );
	cl_qlist_remove_item( &mlnx_hca_list, &p_hca->list_item );
	cl_spinlock_release( &hob_lock );
}

mlnx_hca_t*
mlnx_hca_from_guid(
	IN				ib_net64_t					guid )
{
	cl_list_item_t	*p_item;
	mlnx_hca_t		*p_hca = NULL;

	cl_spinlock_acquire( &hob_lock );
	p_item = cl_qlist_head( &mlnx_hca_list );
	while( p_item != cl_qlist_end( &mlnx_hca_list ) )
	{
		p_hca = PARENT_STRUCT( p_item, mlnx_hca_t, list_item );
		if( p_hca->guid == guid )
			break;
		p_item = cl_qlist_next( p_item );
		p_hca = NULL;
	}
	cl_spinlock_release( &hob_lock );
	return p_hca;
}

/*
void
mlnx_names_from_guid(
	IN				ib_net64_t					guid,
		OUT			char						**hca_name_p,
		OUT			char						**dev_name_p)
{
	unsigned int idx;

	if (!hca_name_p) return;
	if (!dev_name_p) return;

	for (idx = 0; idx < mlnx_num_hca; idx++)
	{
		if (mlnx_hca_array[idx].ifx.guid == guid)
		{
			*hca_name_p = mlnx_hca_array[idx].hca_name_p;
			*dev_name_p = mlnx_hca_array[idx].dev_name_p;
		}
	}
}
*/

/////////////////////////////////////////////////////////
// ### HCA
/////////////////////////////////////////////////////////
cl_status_t
mlnx_hcas_init( void )
{
	cl_qlist_init( &mlnx_hca_list );
	return cl_spinlock_init( &hob_lock );
}


/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
ib_api_status_t
mlnx_hobs_set_cb(
	IN				mlnx_hob_t					*hob_p, 
	IN				ci_async_event_cb_t			async_cb_p,
	IN		const	void* const					ib_context)
{
	cl_status_t		cl_status;

	// Setup the callbacks
	if (!hob_p->async_proc_mgr_p)
	{
		hob_p->async_proc_mgr_p = cl_malloc( sizeof( cl_async_proc_t ) );
		if( !hob_p->async_proc_mgr_p )
		{
			return IB_INSUFFICIENT_MEMORY;
		}
		cl_async_proc_construct( hob_p->async_proc_mgr_p );
		cl_status = cl_async_proc_init( hob_p->async_proc_mgr_p, MLNX_NUM_CB_THR, "CBthread" );
		if( cl_status != CL_SUCCESS )
		{
			cl_async_proc_destroy( hob_p->async_proc_mgr_p );
			cl_free(hob_p->async_proc_mgr_p);
			hob_p->async_proc_mgr_p = NULL;
			return IB_INSUFFICIENT_RESOURCES;
		}
	}

	hob_p->async_cb_p = async_cb_p;
	hob_p->ca_context = ib_context; // This is the context our CB forwards to IBAL
	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_SHIM,("CL: hca_idx %d context 0x%p\n", (int)(hob_p - mlnx_hob_array), ib_context));
	return IB_SUCCESS;
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void
mlnx_hobs_remove(
	IN				mlnx_hob_t					*hob_p)
{
	cl_async_proc_t	*p_async_proc;

	cl_spinlock_acquire( &hob_lock );

	hob_p->mark = E_MARK_INVALID;

	p_async_proc = hob_p->async_proc_mgr_p;
	hob_p->async_proc_mgr_p = NULL;

	hob_p->async_cb_p = NULL;
	hob_p->ca_context = NULL;
	hob_p->cl_device_h = NULL;

	cl_spinlock_release( &hob_lock );

	if( p_async_proc )
	{
		cl_async_proc_destroy( p_async_proc );
		cl_free( p_async_proc );
	}



	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_SHIM,("CL: hobs_remove idx %d \n", (int)(hob_p - mlnx_hob_array)));
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void
mthca_port_cap_to_ibal(
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
mlnx_conv_hca_cap(
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
	
	ca_attr_p->num_page_sizes = 1;
	ca_attr_p->p_page_size[0] = PAGE_SIZE; // TBD: extract an array of page sizes from HCA cap

	for (port_num = 0; port_num <= end_port(ib_dev) - start_port(ib_dev); ++port_num)
	{
		// Setup port pointers
		ibal_port_p = &ca_attr_p->p_port_attr[port_num];
		mthca_port_p = &hca_ports[port_num];

		// Port Cabapilities
		cl_memclr(&ibal_port_p->cap, sizeof(ib_port_cap_t));
		mthca_port_cap_to_ibal(mthca_port_p->port_cap_flags, &ibal_port_p->cap);

		// Port Atributes
		ibal_port_p->port_num   = port_num + start_port(ib_dev);
		ibal_port_p->port_guid  = ibal_port_p->p_gid_table[0].unicast.interface_id;
		ibal_port_p->lid        = cl_ntoh16(mthca_port_p->lid);
		ibal_port_p->lmc        = mthca_port_p->lmc;
		ibal_port_p->max_vls    = mthca_port_p->max_vl_num;
		ibal_port_p->sm_lid     = cl_ntoh16(mthca_port_p->sm_lid);
		ibal_port_p->sm_sl      = mthca_port_p->sm_sl;
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

		ibal_port_p->subnet_timeout = mthca_port_p->subnet_timeout;
		// ibal_port_p->local_ack_timeout = 3; // TBD: currently ~32 usec
		HCA_PRINT(TRACE_LEVEL_VERBOSE, HCA_DBG_SHIM ,("Port %d port_guid 0x%I64x\n",
			ibal_port_p->port_num, cl_ntoh64(ibal_port_p->port_guid)));
	}
}

void ca_event_handler(struct ib_event *ev, void *context)
{
	mlnx_hob_t *hob_p = (mlnx_hob_t *)context;
	ib_event_rec_t event_rec;
	LIST_ENTRY *entry;
	ci_event_handler_t *event_handler;

	if ( hob_p->mark == E_MARK_INVALID )
		return;

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
	KeAcquireSpinLockAtDpcLevel(&hob_p->event_list_lock);
	for (entry = hob_p->event_list.Flink; entry != &hob_p->event_list;
		 entry = entry->Flink) {

		event_handler = CONTAINING_RECORD(entry, ci_event_handler_t, entry);
		event_rec.context = (void *) event_handler;
		event_handler->pfn_async_event_cb(&event_rec);
	}
	KeReleaseSpinLockFromDpcLevel(&hob_p->event_list_lock);

	if (hob_p && hob_p->async_cb_p) {
		event_rec.context = (void *)hob_p->ca_context;
		(hob_p->async_cb_p)(&event_rec);
	} else {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("Incorrect context. Async callback was not invoked\n"));
	}
}

ib_qp_state_t mlnx_qps_to_ibal(enum ib_qp_state qps)
{
#define MAP_QPS(val1,val2) case val1: ib_qps = val2; break
	ib_qp_state_t ib_qps;
	switch (qps) {
		MAP_QPS( IBQPS_RESET, IB_QPS_RESET );
		MAP_QPS( IBQPS_INIT, IB_QPS_INIT );
		MAP_QPS( IBQPS_RTR, IB_QPS_RTR );
		MAP_QPS( IBQPS_RTS, IB_QPS_RTS );
		MAP_QPS( IBQPS_SQD, IB_QPS_SQD );
		MAP_QPS( IBQPS_SQE, IB_QPS_SQERR );
		MAP_QPS( IBQPS_ERR, IB_QPS_ERROR );
		default:
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("Unmapped MTHCA qp_state %d\n", qps));
			ib_qps = 0xffffffff;
	}
	return ib_qps;
}

enum ib_qp_state mlnx_qps_from_ibal(ib_qp_state_t ib_qps)
{
#define MAP_IBQPS(val1,val2) case val1: qps = val2; break
	enum ib_qp_state qps;
	switch (ib_qps) {
		MAP_IBQPS( IB_QPS_RESET, IBQPS_RESET );
		MAP_IBQPS( IB_QPS_INIT, IBQPS_INIT );
		MAP_IBQPS( IB_QPS_RTR, IBQPS_RTR );
		MAP_IBQPS( IB_QPS_RTS, IBQPS_RTS );
		MAP_IBQPS( IB_QPS_SQD, IBQPS_SQD );
		MAP_IBQPS( IB_QPS_SQD_DRAINING, IBQPS_SQD );
		MAP_IBQPS( IB_QPS_SQD_DRAINED, IBQPS_SQD );
		MAP_IBQPS( IB_QPS_SQERR, IBQPS_SQE );
		MAP_IBQPS( IB_QPS_ERROR, IBQPS_ERR );
		default:
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("Unmapped IBAL qp_state %d\n", ib_qps));
			qps = 0xffffffff;
	}
	return qps;
}

enum ib_mig_state to_apm_state(ib_apm_state_t apm)
{
	if (apm == IB_APM_MIGRATED) return IB_MIG_MIGRATED;
	if (apm == IB_APM_REARM) return IB_MIG_REARM;
	if (apm == IB_APM_ARMED) return IB_MIG_ARMED;
	return 0xffffffff;
}

ib_api_status_t
mlnx_conv_qp_modify_attr(
	IN	 const	struct ib_qp *ib_qp_p,
	IN				ib_qp_type_t	qp_type,
	IN	 const	ib_qp_mod_t *modify_attr_p,		
	OUT 	struct ib_qp_attr *qp_attr_p,
	OUT 	int *qp_attr_mask_p
	)
{
	int err;
	ib_api_status_t 	status = IB_SUCCESS;
	struct mthca_qp *qp_p = (struct mthca_qp *)ib_qp_p;

	RtlZeroMemory( qp_attr_p, sizeof *qp_attr_p );
	*qp_attr_mask_p = IB_QP_STATE;
	qp_attr_p->qp_state = mlnx_qps_from_ibal( modify_attr_p->req_state ); 

	// skipped cases
	if (qp_p->state == IBQPS_RESET && modify_attr_p->req_state != IB_QPS_INIT)
		return IB_NOT_DONE;
		
	switch (modify_attr_p->req_state) {
	case IB_QPS_RESET:
	case IB_QPS_ERROR:
	case IB_QPS_SQERR:
	case IB_QPS_TIME_WAIT:
		break;

	case IB_QPS_INIT:
		
		switch (qp_type) {
			case IB_QPT_RELIABLE_CONN:
			case IB_QPT_UNRELIABLE_CONN:
				*qp_attr_mask_p |= IB_QP_PORT | IB_QP_PKEY_INDEX |IB_QP_ACCESS_FLAGS;
				qp_attr_p->qp_access_flags = map_qp_ibal_acl(modify_attr_p->state.init.access_ctrl);
				break;
			case IB_QPT_UNRELIABLE_DGRM:
			case IB_QPT_QP0:
			case IB_QPT_QP1:
			default:	
				*qp_attr_mask_p |= IB_QP_PORT | IB_QP_QKEY | IB_QP_PKEY_INDEX ;
				qp_attr_p->qkey 	 = cl_ntoh32 (modify_attr_p->state.init.qkey);
				break;
		}				
		
		// IB_QP_PORT
		qp_attr_p->port_num    = modify_attr_p->state.init.primary_port;

		// IB_QP_PKEY_INDEX
		qp_attr_p->pkey_index = modify_attr_p->state.init.pkey_index;

		break;
		
	case IB_QPS_RTR:
		/* modifying the WQE depth is not supported */
		if( modify_attr_p->state.rtr.opts & IB_MOD_QP_SQ_DEPTH ||
			modify_attr_p->state.rtr.opts & IB_MOD_QP_RQ_DEPTH )	{
			status = IB_UNSUPPORTED;
			break;
		}

		switch (qp_type) {
			case IB_QPT_RELIABLE_CONN:
				*qp_attr_mask_p |= /* required flags */
					IB_QP_DEST_QPN |IB_QP_RQ_PSN | IB_QP_MAX_DEST_RD_ATOMIC |
					IB_QP_AV |IB_QP_PATH_MTU | IB_QP_MIN_RNR_TIMER;

				// IB_QP_DEST_QPN
				qp_attr_p->dest_qp_num		= cl_ntoh32 (modify_attr_p->state.rtr.dest_qp);

				// IB_QP_RQ_PSN
				qp_attr_p->rq_psn 				= cl_ntoh32 (modify_attr_p->state.rtr.rq_psn);
				
				// IB_QP_MAX_DEST_RD_ATOMIC
				qp_attr_p->max_dest_rd_atomic	= modify_attr_p->state.rtr.resp_res;

				// IB_QP_AV, IB_QP_PATH_MTU: Convert primary RC AV (mandatory)
				err = mlnx_conv_ibal_av(ib_qp_p->device,
					&modify_attr_p->state.rtr.primary_av, &qp_attr_p->ah_attr);
				if (err) {
					status = IB_ERROR;
					break;
				}
				qp_attr_p->path_mtu 		= modify_attr_p->state.rtr.primary_av.conn.path_mtu; // MTU
				qp_attr_p->timeout 		= modify_attr_p->state.rtr.primary_av.conn.local_ack_timeout; // MTU
				qp_attr_p->retry_cnt 		= modify_attr_p->state.rtr.primary_av.conn.seq_err_retry_cnt; // MTU
				qp_attr_p->rnr_retry 		= modify_attr_p->state.rtr.primary_av.conn.rnr_retry_cnt; // MTU

				// IB_QP_MIN_RNR_TIMER, required in RTR, optional in RTS.
				qp_attr_p->min_rnr_timer	 = modify_attr_p->state.rtr.rnr_nak_timeout;

				// IB_QP_ACCESS_FLAGS: Convert Remote Atomic Flags
				if (modify_attr_p->state.rtr.opts & IB_MOD_QP_ACCESS_CTRL) {
					*qp_attr_mask_p |= IB_QP_ACCESS_FLAGS;		/* optional flag */
					qp_attr_p->qp_access_flags = map_qp_ibal_acl(modify_attr_p->state.rtr.access_ctrl);
				}

				// IB_QP_ALT_PATH: Convert alternate RC AV
				if (modify_attr_p->state.rtr.opts & IB_MOD_QP_ALTERNATE_AV) {
					*qp_attr_mask_p |= IB_QP_ALT_PATH;	/* required flag */
					err = mlnx_conv_ibal_av(ib_qp_p->device,
						&modify_attr_p->state.rtr.alternate_av, &qp_attr_p->alt_ah_attr);
					if (err) {
						status = IB_ERROR;
						break;
					}
					qp_attr_p->alt_timeout		 = modify_attr_p->state.rtr.alternate_av.conn.local_ack_timeout; // XXX: conv
				}

				// IB_QP_PKEY_INDEX 
				if (modify_attr_p->state.rtr.opts & IB_MOD_QP_PKEY) {
					*qp_attr_mask_p |= IB_QP_PKEY_INDEX;	
					qp_attr_p->pkey_index = modify_attr_p->state.rtr.pkey_index;
				}
				break;
				
			case IB_QPT_UNRELIABLE_CONN:
				*qp_attr_mask_p |= /* required flags */
					IB_QP_DEST_QPN |IB_QP_RQ_PSN | IB_QP_AV | IB_QP_PATH_MTU;

				// IB_QP_DEST_QPN
				qp_attr_p->dest_qp_num		= cl_ntoh32 (modify_attr_p->state.rtr.dest_qp);

				// IB_QP_RQ_PSN
				qp_attr_p->rq_psn 				= cl_ntoh32 (modify_attr_p->state.rtr.rq_psn);

				// IB_QP_PATH_MTU
				qp_attr_p->path_mtu 		= modify_attr_p->state.rtr.primary_av.conn.path_mtu;

				// IB_QP_AV: Convert primary AV (mandatory)
				err = mlnx_conv_ibal_av(ib_qp_p->device,
					&modify_attr_p->state.rtr.primary_av, &qp_attr_p->ah_attr);
				if (err) {
					status = IB_ERROR;
					break;
				}

				// IB_QP_ACCESS_FLAGS: Convert Remote Atomic Flags
				if (modify_attr_p->state.rtr.opts & IB_MOD_QP_ACCESS_CTRL) {
					*qp_attr_mask_p |= IB_QP_ACCESS_FLAGS;		/* optional flag */
					qp_attr_p->qp_access_flags = map_qp_ibal_acl(modify_attr_p->state.rtr.access_ctrl);
				}

				// IB_QP_ALT_PATH: Convert alternate RC AV
				if (modify_attr_p->state.rtr.opts & IB_MOD_QP_ALTERNATE_AV) {
					*qp_attr_mask_p |= IB_QP_ALT_PATH;	/* required flag */
					err = mlnx_conv_ibal_av(ib_qp_p->device,
						&modify_attr_p->state.rtr.alternate_av, &qp_attr_p->alt_ah_attr);
					if (err) {
						status = IB_ERROR;
						break;
					}
				}

				// IB_QP_PKEY_INDEX 
				if (modify_attr_p->state.rtr.opts & IB_MOD_QP_PKEY) {
					*qp_attr_mask_p |= IB_QP_PKEY_INDEX;	
					qp_attr_p->pkey_index = modify_attr_p->state.rtr.pkey_index;
				}
				break;
					
			case IB_QPT_UNRELIABLE_DGRM:
			case IB_QPT_QP0:
			case IB_QPT_QP1:
			default:	
				// IB_QP_PKEY_INDEX 
				if (modify_attr_p->state.rtr.opts & IB_MOD_QP_PKEY) {
					*qp_attr_mask_p |= IB_QP_PKEY_INDEX;	
					qp_attr_p->pkey_index = modify_attr_p->state.rtr.pkey_index;
				}

				// IB_QP_QKEY
				if (modify_attr_p->state.rtr.opts & IB_MOD_QP_QKEY) {
					*qp_attr_mask_p |= IB_QP_QKEY;	
					qp_attr_p->qkey 	 = cl_ntoh32 (modify_attr_p->state.rtr.qkey);
				}
				break;
				
		}
		break;
		
	case IB_QPS_RTS:
		/* modifying the WQE depth is not supported */
		if( modify_attr_p->state.rts.opts & IB_MOD_QP_SQ_DEPTH ||
			modify_attr_p->state.rts.opts & IB_MOD_QP_RQ_DEPTH )
		{
			status = IB_UNSUPPORTED;
			break;
		}

		switch (qp_type) {
			case IB_QPT_RELIABLE_CONN:
				if (qp_p->state != IBQPS_RTS)
					*qp_attr_mask_p |= /* required flags */
						IB_QP_SQ_PSN |IB_QP_MAX_QP_RD_ATOMIC | IB_QP_TIMEOUT |
						IB_QP_RETRY_CNT |IB_QP_RNR_RETRY;

				// IB_QP_MAX_QP_RD_ATOMIC
				qp_attr_p->max_rd_atomic	= modify_attr_p->state.rts.init_depth;

				// IB_QP_TIMEOUT
				qp_attr_p->timeout		 = modify_attr_p->state.rts.local_ack_timeout; // XXX: conv
				
				// IB_QP_RETRY_CNT
				qp_attr_p->retry_cnt = modify_attr_p->state.rts.retry_cnt;
				
				// IB_QP_RNR_RETRY
				qp_attr_p->rnr_retry	 = modify_attr_p->state.rts.rnr_retry_cnt;

				// IB_QP_MAX_DEST_RD_ATOMIC: Update the responder resources for RDMA/ATOMIC (optional for SQD->RTS)
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_RESP_RES) {
					*qp_attr_mask_p |= IB_QP_MAX_DEST_RD_ATOMIC;	
					qp_attr_p->max_dest_rd_atomic = modify_attr_p->state.rts.resp_res;
				}

#ifdef WIN_TO_BE_REMOVED
		//TODO: do we need that ?
		// Linux patch 4793: PKEY_INDEX is not a legal parameter in the RTR->RTS transition.

				// IB_QP_PKEY_INDEX 
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_PKEY) {
					*qp_attr_mask_p |= IB_QP_PKEY_INDEX;	
					qp_attr_p->pkey_index = modify_attr_p->state.rts.pkey_index;
				}
#endif				

				// IB_QP_MIN_RNR_TIMER
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_RNR_NAK_TIMEOUT) {
					*qp_attr_mask_p |= IB_QP_MIN_RNR_TIMER;	
					qp_attr_p->min_rnr_timer	 = modify_attr_p->state.rts.rnr_nak_timeout;
				}

				// IB_QP_PATH_MIG_STATE
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_APM_STATE) {
					*qp_attr_mask_p |= IB_QP_PATH_MIG_STATE;	
					qp_attr_p->path_mig_state =  to_apm_state(modify_attr_p->state.rts.apm_state);
				}

				// IB_QP_ACCESS_FLAGS
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_ACCESS_CTRL) {
					*qp_attr_mask_p |= IB_QP_ACCESS_FLAGS;		/* optional flags */
					qp_attr_p->qp_access_flags = map_qp_ibal_acl(modify_attr_p->state.rts.access_ctrl);
				}

				// IB_QP_ALT_PATH: Convert alternate RC AV
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_ALTERNATE_AV) {
					*qp_attr_mask_p |= IB_QP_ALT_PATH;	/* optional flag */
					err = mlnx_conv_ibal_av(ib_qp_p->device,
						&modify_attr_p->state.rts.alternate_av, &qp_attr_p->alt_ah_attr);
					if (err) {
						status = IB_ERROR;
						break;
					}
					qp_attr_p->alt_timeout		 = modify_attr_p->state.rts.alternate_av.conn.local_ack_timeout; // XXX: conv
				}
				break;
				
			case IB_QPT_UNRELIABLE_CONN:
				if (qp_p->state != IBQPS_RTS)
					*qp_attr_mask_p |= /* required flags */
						IB_QP_SQ_PSN;

				// IB_QP_MAX_DEST_RD_ATOMIC: Update the responder resources for RDMA/ATOMIC (optional for SQD->RTS)
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_RESP_RES) {
					*qp_attr_mask_p |= IB_QP_MAX_DEST_RD_ATOMIC;	
					qp_attr_p->max_dest_rd_atomic = modify_attr_p->state.rts.resp_res;
				}

#ifdef WIN_TO_BE_REMOVED
		//TODO: do we need that ?
		// Linux patch 4793: PKEY_INDEX is not a legal parameter in the RTR->RTS transition.

				// IB_QP_PKEY_INDEX 
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_PKEY) {
					*qp_attr_mask_p |= IB_QP_PKEY_INDEX;	
					qp_attr_p->pkey_index = modify_attr_p->state.rts.pkey_index;
				}
#endif				

				// IB_QP_PATH_MIG_STATE
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_APM_STATE) {
					*qp_attr_mask_p |= IB_QP_PATH_MIG_STATE;	
					qp_attr_p->path_mig_state =  to_apm_state(modify_attr_p->state.rts.apm_state);
				}

				// IB_QP_ACCESS_FLAGS
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_ACCESS_CTRL) {
					*qp_attr_mask_p |= IB_QP_ACCESS_FLAGS;		/* optional flags */
					qp_attr_p->qp_access_flags = map_qp_ibal_acl(modify_attr_p->state.rts.access_ctrl);
				}

				// IB_QP_ALT_PATH: Convert alternate RC AV
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_ALTERNATE_AV) {
					*qp_attr_mask_p |= IB_QP_ALT_PATH;	/* optional flag */
					err = mlnx_conv_ibal_av(ib_qp_p->device,
						&modify_attr_p->state.rts.alternate_av, &qp_attr_p->alt_ah_attr);
					if (err) {
						status = IB_ERROR;
						break;
					}
				}
				break;
					
			case IB_QPT_UNRELIABLE_DGRM:
			case IB_QPT_QP0:
			case IB_QPT_QP1:
			default:	
				if (qp_p->state != IBQPS_RTS)
					*qp_attr_mask_p |= /* required flags */
						IB_QP_SQ_PSN;

				// IB_QP_QKEY
				if (modify_attr_p->state.rts.opts & IB_MOD_QP_QKEY) {
					*qp_attr_mask_p |= IB_QP_QKEY;	
					qp_attr_p->qkey 	 = cl_ntoh32 (modify_attr_p->state.rts.qkey);
				}
				break;
				
				break;
				
		}

		// IB_QP_SQ_PSN: common for all
		qp_attr_p->sq_psn = cl_ntoh32 (modify_attr_p->state.rts.sq_psn);
		//NB: IB_QP_CUR_STATE flag is not provisioned by IBAL
		break;
		
	case IB_QPS_SQD:
	case IB_QPS_SQD_DRAINING:
	case IB_QPS_SQD_DRAINED:
		*qp_attr_mask_p |= IB_QP_EN_SQD_ASYNC_NOTIFY;
		qp_attr_p->en_sqd_async_notify = (u8)modify_attr_p->state.sqd.sqd_event;
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_SHIM ,("IB_QP_EN_SQD_ASYNC_NOTIFY seems like unsupported\n"));
		break;
		
	default:	
		//NB: is this an error case and we need this message  ? What about returning an error ?
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("Unmapped qp_state %d\n", modify_attr_p->req_state));
		break;
		
	}

	return status;
}	

int
mlnx_conv_ibal_av(
	IN		const	struct ib_device *ib_dev_p,
	IN		const	ib_av_attr_t				*ibal_av_p,
	OUT			struct ib_ah_attr	*ah_attr_p)
{
	int err = 0;
	u8 port_num;
	u16 gid_index;
	
	ah_attr_p->port_num = ibal_av_p->port_num;
	ah_attr_p->sl   = ibal_av_p->sl;
	ah_attr_p->dlid = cl_ntoh16(ibal_av_p->dlid);
	//TODO: how static_rate is coded ?
	ah_attr_p->static_rate   =
		(ibal_av_p->static_rate == IB_PATH_RECORD_RATE_10_GBS ? 0 : 3);
	ah_attr_p->src_path_bits = ibal_av_p->path_bits; // PATH:

	/* For global destination or Multicast address:*/
	if (ibal_av_p->grh_valid)
	{
		ah_attr_p->ah_flags |= IB_AH_GRH;
		ah_attr_p->grh.hop_limit     = ibal_av_p->grh.hop_limit;
		ib_grh_get_ver_class_flow( ibal_av_p->grh.ver_class_flow, NULL,
			&ah_attr_p->grh.traffic_class, &ah_attr_p->grh.flow_label );
		err = ib_find_cached_gid((struct ib_device *)ib_dev_p, 
			(union ib_gid	*)ibal_av_p->grh.src_gid.raw, &port_num, &gid_index);
		if (err) {

			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("ib_find_cached_gid failed %d (%#x). Using default:  sgid_index = 0\n", err, err));
			gid_index = 0;
		}
		else if (port_num != ah_attr_p->port_num) {
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("ib_find_cached_gid returned wrong port_num %u (Expected - %u). Using the expected.\n", 
				(u32)port_num, (u32)ah_attr_p->port_num));
		}
		ah_attr_p->grh.sgid_index = (u8)gid_index;
		RtlCopyMemory(ah_attr_p->grh.dgid.raw, ibal_av_p->grh.dest_gid.raw, sizeof(ah_attr_p->grh.dgid));
	}

	return err;
}

int
mlnx_conv_mthca_av(
	IN		const	struct ib_ah *ib_ah_p,
	OUT			ib_av_attr_t				*ibal_av_p)
{
	int err = 0;
	struct ib_ud_header header;
	struct mthca_ah *ah_p = (struct mthca_ah *)ib_ah_p;
	struct ib_device *ib_dev_p = ib_ah_p->pd->device;
	struct mthca_dev *dev_p = (struct mthca_dev *)ib_dev_p;

	err = mthca_read_ah( dev_p, ah_p, &header);
	if (err)
		goto err_read_ah;

	// common part
	ibal_av_p->sl       		= header.lrh.service_level;
	mthca_get_av_params(ah_p, &ibal_av_p->port_num,
		&ibal_av_p->dlid, &ibal_av_p->static_rate, &ibal_av_p->path_bits );

	// GRH
	ibal_av_p->grh_valid = header.grh_present;
	if (ibal_av_p->grh_valid) {
		ibal_av_p->grh.ver_class_flow = ib_grh_set_ver_class_flow(
			header.grh.ip_version, header.grh.traffic_class, header.grh.flow_label );
		ibal_av_p->grh.hop_limit = header.grh.hop_limit;
		RtlCopyMemory(ibal_av_p->grh.src_gid.raw, 
			header.grh.source_gid.raw, sizeof(ibal_av_p->grh.src_gid));
		RtlCopyMemory(ibal_av_p->grh.src_gid.raw, 
			header.grh.destination_gid.raw, sizeof(ibal_av_p->grh.dest_gid));
	}

	//TODO: don't know, how to fill conn. Note, that previous version didn't fill it also.

err_read_ah:
		return err;
}

void
mlnx_modify_ah(
	IN		const	struct ib_ah *ib_ah_p,
	IN 	const 	struct ib_ah_attr *ah_attr_p)
{
	struct ib_device *ib_dev_p = ib_ah_p->pd->device;
	struct mthca_dev *dev_p = (struct mthca_dev *)ib_dev_p;
	
	mthca_set_av_params(dev_p, (struct mthca_ah *)ib_ah_p, (struct ib_ah_attr *)ah_attr_p );
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
	return 0;
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
		err = ib_get_cached_gid((struct ib_device *)p_ib_dev, 
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

ib_apm_state_t from_apm_state(enum ib_mig_state apm)
{
	if (apm == IB_MIG_MIGRATED) return IB_APM_MIGRATED;
	if (apm == IB_MIG_REARM) return IB_APM_REARM;
	if (apm == IB_MIG_ARMED) return IB_APM_ARMED;
	return 0xffffffff;
}

ib_api_status_t
mlnx_conv_qp_attr(
	IN	 const	struct ib_qp 	*p_ib_qp,
	IN 	struct ib_qp_attr 		*p_ib_qp_attr,
	OUT 	ib_qp_attr_t		*p_qp_attr
	)
{
	int err;
	RtlZeroMemory( p_qp_attr, sizeof *p_qp_attr );
	p_qp_attr->h_pd = (ib_pd_handle_t)p_ib_qp->pd;
	p_qp_attr->qp_type = p_ib_qp->qp_type;
	p_qp_attr->access_ctrl = map_qp_mthca_acl(p_ib_qp_attr->qp_access_flags);
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

	p_qp_attr->sq_signaled = (((struct mthca_qp *)p_ib_qp)->sq_policy == IB_SIGNAL_ALL_WR) ? TRUE : FALSE;

	p_qp_attr->state = mlnx_qps_to_ibal( p_ib_qp_attr->qp_state);
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

