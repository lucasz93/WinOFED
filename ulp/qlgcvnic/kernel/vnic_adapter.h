/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
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

#if !defined _VNIC_ADAPTER_H_
#define _VNIC_ADAPTER_H_

#include <complib/comp_lib.h>
#include <iba/ib_al.h>
#include <initguid.h>
#include <ip_packet.h>
#include "vnic_ib.h"
#include "vnic_controlpkt.h"
#include "vnic_config.h"
#include "vnic_control.h"
#include "vnic_data.h"
#include "vnic_viport.h"
#include <iba/ioc_ifc.h>

typedef struct _pending_oid
{
	NDIS_OID			oid;
	PVOID				p_buf;
	ULONG				buf_len;
	PULONG				p_bytes_used;
	PULONG				p_bytes_needed;

}	pending_oid_t;

typedef struct _ipv4_address_item
{
	union _net_address_item_address
	{
		ULONG			as_ulong;
		UCHAR			as_bytes[4];
	}	address;

}	ipv4_address_item_t;

typedef struct _lbfo_failover {
	struct _vnic_adapter *p_adapter;
	NDIS_HANDLE			primary_handle;
	uint32_t			bundle_id;
	lbfo_state_t		fo_state;
} lbfo_failover_t;

typedef struct _vnic_params {
	uint32_t	MaxAddressEntries;
	uint32_t	MinAddressEntries;
	uint32_t	MinMtu;
	uint32_t	MaxMtu;
	uint32_t	HostRecvPoolEntries;
	uint32_t	MinHostPoolSz;
	uint32_t	MinEiocPoolSz;
	uint32_t	MaxEiocPoolSz;
	uint32_t	MinHostKickTimeout;
	uint32_t	MaxHostKickTimeout;
	uint32_t	MinHostKickEntries;
	uint32_t	MaxHostKickEntries;
	uint32_t	MinHostKickBytes;
	uint32_t	MaxHostKickBytes;
	uint32_t	MinHostUpdateSz;
	uint32_t	MaxHostUpdateSz;
	uint32_t	MinEiocUpdateSz;
	uint32_t	MaxEiocUpdateSz;
	uint32_t	NotifyBundleSz;
	uint32_t	ViportStatsInterval;
	uint32_t	ViportHbInterval;
	uint32_t	ViportHbTimeout;
	uint32_t	ControlRspTimeout;
	uint32_t	ControlReqRetryCount;
	uint32_t	RetryCount;
	uint32_t	MinRnrTimer;
	uint32_t	MaxViportsPerNetpath;
	uint32_t	DefaultViportsPerNetpath;
	uint32_t	DefaultPkey;
	uint32_t	DefaultNoPathTimeout;
	uint32_t	DefaultPrimaryConnectTimeout;
	uint32_t	DefaultPrimaryReconnectTimeout;
	uint32_t	DefaultPrimarySwitchTimeout;
	uint32_t	DefaultPreferPrimary;
	uint32_t	UseRxCsum;
	uint32_t	UseTxCsum;
	uint32_t	VlanInfo;
	uint32_t	SecondaryPath;
	uint32_t	bundle_id;
	mac_addr_t	conf_mac;
}	vnic_params_t;


typedef struct _vnic_adapter {
	cl_list_item_t			list_item;
	NDIS_HANDLE				h_handle;
	DEVICE_OBJECT			*p_pdo;
	NDIS_SPIN_LOCK			lock;
	ib_al_ifc_t				ifc;
	ioc_ifc_data_t			ifc_data;
	ib_ioc_info_t			ioc_info;
	LIST_ENTRY				send_pending_list;
	LIST_ENTRY				cancel_send_list;
	NDIS_SPIN_LOCK			pending_list_lock;
	NDIS_SPIN_LOCK			cancel_list_lock;
	NDIS_SPIN_LOCK			path_records_lock;
	cl_qlist_t				path_records_list;
	ib_al_handle_t			h_al;
	ib_ca_handle_t			h_ca;
	ib_pnp_handle_t			h_pnp;
	ib_pnp_event_t			pnp_state;
	IbCa_t					ca;
	InicState_t				state;
	struct Netpath			primaryPath;
	struct Netpath			secondaryPath;
	struct Netpath			*p_currentPath;
	vnic_params_t			params;
	uint32_t				num_paths;
	int						macSet;
	int						mc_count;
	mac_addr_t				mcast_array[MAX_MCAST];
	LONG					xmitStarted;
	LONG					carrier;
	uint32_t				ioc_num;
	uint32_t				link_speed;
	uint32_t				packet_filter;
	uint32_t				vlan_info;
	uint32_t				hung;
	BOOLEAN					reset;
	BOOLEAN					pending_set;
	BOOLEAN					pending_query;
	pending_oid_t			query_oid;
	pending_oid_t			set_oid;
	ib_svc_entry_t			svc_entries[2];

#if ( LBFO_ENABLED )
	lbfo_failover_t			failover;
#endif
#ifdef VNIC_STATISTIC
	struct {
		uint64_t			startTime;
		uint64_t			connTime;
		uint64_t			disconnRef;     /* Intermediate time */
		uint64_t			disconnTime;
		uint32_t			disconnNum;
		uint64_t			xmitTime;
		uint32_t			xmitNum;
		uint32_t			xmitFail;
		uint64_t			recvTime;
		uint32_t			recvNum;
		uint64_t			xmitRef;        /* Intermediate time */
		uint64_t			xmitOffTime;
		uint32_t			xmitOffNum;
		uint64_t			carrierRef;     /* Intermediate time */
		uint64_t			carrierOffTime;
		uint32_t			carrierOffNum;
	}   statistics;
#endif /* VNIC_STATISTIC */
	cl_list_item_t			list_adapter;
} vnic_adapter_t;

ib_api_status_t
vnic_create_adapter(
	IN		NDIS_HANDLE			h_handle,
	IN		NDIS_HANDLE			wrapper_config_context,
	OUT		vnic_adapter_t**	const pp_adapter);

void
vnic_destroy_adapter(
	IN		vnic_adapter_t*		p_adapter);

ib_api_status_t
vnic_construct_adapter(
	IN		vnic_adapter_t		*p_adapter);

NDIS_STATUS
adapter_set_mcast(
	IN				vnic_adapter_t* const		p_adapter,
	IN				mac_addr_t* const			p_mac_array,
	IN		const	uint8_t						mc_count );

ib_api_status_t
adapter_viport_allocate(
	IN			vnic_adapter_t*	const 		p_adapter,
	IN OUT		viport_t**		const		pp_viport );

void
vnic_resume_set_oids(
	IN				vnic_adapter_t* const		p_adapter );

void
vnic_resume_oids(
	IN				vnic_adapter_t* const		p_adapter );

ib_api_status_t
__vnic_pnp_cb(
	IN			ib_pnp_rec_t			*p_pnp_rec );

ib_api_status_t
ibregion_physInit(
	IN			struct _vnic_adapter*		p_adapter,
		OUT		IbRegion_t					*pRegion,
	IN			ib_pd_handle_t				hPd,
	IN			uint64_t					*p_vaddr,
	IN			uint64_t					len );

void
__vnic_pnp_dereg_cb(
	IN		void*			context );

ib_api_status_t
adapter_reset(
	IN		vnic_adapter_t* const		p_adapter );
void
__pending_queue_cleanup( 
	IN		vnic_adapter_t	*p_adapter );

#endif	/* !defined _VNIC_ADAPTER_H_ */
