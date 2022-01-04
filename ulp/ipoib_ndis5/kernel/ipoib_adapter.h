/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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


#ifndef _IPOIB_ADAPTER_H_
#define _IPOIB_ADAPTER_H_

#include <iba/ipoib_ifc.h>
#include <iba/ib_al.h>
#include <complib/cl_obj.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_mutex.h>
#include <complib/cl_qpool.h>
#include <complib/cl_atomic.h>
#include <complib/cl_perf.h>
#include <complib/cl_vector.h>
#include <ip_packet.h>
#include "ip_stats.h"
#include "ipoib_stat.h"


/*
 * Definitions
 */
#define MAX_MCAST				32

#define IPV4_ADDR_SIZE			4

#define PORT_NUM_INDEX_IN_GUID	3 /* 0 based index into big endian GUID to get port number */

/*
 * Macros
 */
typedef enum {CSUM_DISABLED = 0, CSUM_ENABLED, CSUM_BYPASS} tCsumTypeEn;

typedef struct _ipoib_params
{
	int32_t		rq_depth;
	int32_t		rq_low_watermark;
	int32_t		sq_depth;
	int32_t		send_chksum_offload;  //is actually of type tCsumTypeEn
	int32_t		recv_chksum_offload; //is actually of type tCsumTypeEn
	uint32_t	sa_timeout;
	uint32_t	sa_retry_cnt;
	uint32_t	recv_pool_ratio;
	uint32_t	payload_mtu;
	boolean_t	lso;
	uint32_t	xfer_block_size;
	mac_addr_t	conf_mac;
	uint32_t	mc_leave_rescan;
	uint32_t	guid_mask;
	uint32_t	bc_join_retry;
}	ipoib_params_t;
/*
* FIELDS
*	rq_depth
*		Number of receive WQEs to allocate.
*
*	rq_low_watermark
*		Receives are indicated with NDIS_STATUS_RESOURCES when the number of
*		receives posted to the RQ falls bellow this value.
*
*	sq_depth
*		Number of send WQEs to allocate.
*
*	send_chksum_offload
*	recv_chksum_offload
*		Flags to indicate whether to offload send/recv checksums.
*		0 - No hardware cheksum
*		1 - Try to offload if the device support it
*		2 - Always report success (checksum bypass)
*
*	wsdp_enabled
*		Flag to indicate whether WSDP is enabled for an adapter adapter.
*
*	static_lid
*		LID to assign to the port if that port is down (not init) and has none.
*		This feature allows a LID to be assigned, alowing locally targetted
*		traffic to occur even on ports that are not plugged in.
*
*	sa_timeout
*		Time, in milliseconds, to wait for a response before retransmitting an
*		SA query request.
*
*	sa_retry_cnt
*		Number of times to retry an SA query request.
*
*	recv_pool_ratio
*		Initial ratio of receive pool size to receive queue depth.
*
*	grow_thresh
*		Threshold at which to grow the receive pool.  Valid values start are
*		powers of 2, excluding 1.  When zero, grows only when the pool is
*		exhausted.  Other values indicate fractional values
*		(i.e. 2 indicates 1/2, 4 indicates 1/4, etc.)
*
*	payload_mtu
*		The maximum available size of IPoIB transfer unit.
*		It should be lower by size of IPoIB header (==4B)
*		For example, if the HCA support 4K MTU, 
*		upper threshold for payload mtu is 4092B and not 4096B
*	lso
*	It indicates if there's a support for hardware large/giant send offload
*		
*********/


typedef struct _pending_oid
{
	NDIS_OID			oid;
	PVOID				p_buf;
	ULONG				buf_len;
	PULONG				p_bytes_used;
	PULONG				p_bytes_needed;

}	pending_oid_t;


typedef struct _ipoib_adapter
{
	cl_obj_t				obj;
	NDIS_HANDLE				h_adapter;
	PDEVICE_OBJECT			pdo;
	ipoib_ifc_data_t		guids;

	cl_list_item_t			entry;

	ib_al_handle_t			h_al;
	ib_pnp_handle_t			h_pnp;

	ib_pnp_event_t			state;
	boolean_t				hung;
	boolean_t				reset;
	boolean_t				registering;

	boolean_t				pending_query;
	pending_oid_t			query_oid;
	boolean_t				pending_set;
	pending_oid_t			set_oid;

	struct _ipoib_port		*p_port;

	uint32_t				port_rate;

	ipoib_params_t			params;
	cl_spinlock_t			recv_stat_lock;
	ip_stats_t				recv_stats;
	cl_spinlock_t			send_stat_lock;
	ip_stats_t				send_stats;

	boolean_t				is_primary;
	struct _ipoib_adapter	*p_primary;

	uint32_t				packet_filter;

	mac_addr_t				mac;
	mac_addr_t				mcast_array[MAX_MCAST];
	uint8_t					mcast_array_size;

	cl_qpool_t				item_pool;

	KMUTEX					mutex;

	cl_thread_t				destroy_thread;
	cl_vector_t				ip_vector;

	cl_perf_t				perf;
	ib_al_ifc_t				*p_ifc;
	PIPOIB_ST_DEVICE		p_stat;

}	ipoib_adapter_t;
/*
* FIELDS
*	obj
*		Complib object for reference counting and destruction synchronization.
*
*	h_adapter
*		NDIS adapter handle.
*
*	guids
*		CA and port GUIDs returned by the bus driver.
*
*	entry
*		List item for storing all adapters in a list for address translation.
*		We add adapters when their packet filter is set to a non-zero value,
*		and remove them when their packet filter is cleared.  This is needed
*		since user-mode removal events are generated after the packet filter
*		is cleared, but before the adapter is destroyed.
*
*	h_al
*		AL handle for all IB resources.
*
*	h_pnp
*		PNP registration handle for port events.
*
*	state
*		State of the adapter.  IB_PNP_PORT_ADD indicates that the adapter
*		is ready to transfer data.
*
*	hung
*		Boolean flag used to return whether we are hung or not.
*
*	p_port
*		Pointer to an ipoib_port_t representing all resources for moving data
*		on the IB fabric.
*
*	rate
*		Rate, in 100bps increments, of the link.
*
*	params
*		Configuration parameters.
*
*	pending_query
*		Indicates that an query OID request is being processed asynchronously.
*
*	query_oid
*		Information about the pended query OID request.
*		Valid only if pending_query is TRUE.
*
*	pending_set
*		Indicates that an set OID request is being processed asynchronously.
*
*	set_oid
*		Information about the pended set OID request.
*		Valid only if pending_set is TRUE.
*
*	recv_lock
*		Spinlock protecting receive processing.
*
*	recv_stats
*		Receive statistics.
*
*	send_lock
*		Spinlock protecting send processing.
*
*	send_stats
*		Send statistics.
*
*	is_primary
*		Boolean flag to indicate if an adapter is the primary adapter
*		of a bundle.
*
*	p_primary
*		Pointer to the primary adapter for a bundle.
*
*	packet_filter
*		Packet filter set by NDIS.
*
*	mac_addr
*		Ethernet MAC address reported to NDIS.
*
*	mcast_array
*		List of multicast MAC addresses programmed by NDIS.
*
*	mcast_array_size
*		Number of entries in the multicat MAC address array;
*
*	item_pool
*		Pool of cl_pool_obj_t structures to use for queueing pending
*		packets for transmission.
*
*	mutex
*		Mutex to synchronized PnP callbacks with destruction.
*
*	ip_vector
*		Vector of assigned IP addresses.
*
*	p_ifc
*		Pointer to transport interface.
*
*********/


typedef struct _ats_reg
{
	ipoib_adapter_t		*p_adapter;
	ib_reg_svc_handle_t	h_reg_svc;

}	ats_reg_t;
/*
* FIELDS
*	p_adapter
*		Pointer to the adapter to which this address is assigned.
*
*	h_reg_svc
*		Service registration handle.
*********/


typedef struct _net_address_item
{
	ats_reg_t			*p_reg;
	union _net_address_item_address
	{
		ULONG			as_ulong;
		UCHAR			as_bytes[IPV4_ADDR_SIZE];
	}	address;

}	net_address_item_t;
/*
* FIELDS
*	p_reg
*		Pointer to the ATS registration assigned to this address.
*
*	address
*		Union representing the IP address as an unsigned long or as
*		an array of bytes.
*
*	as_ulong
*		The IP address represented as an unsigned long.  Windows stores
*		IPs this way.
*
*	as_bytes
*		The IP address represented as an array of bytes.
*********/


ib_api_status_t
ipoib_create_adapter(
	IN		NDIS_HANDLE			wrapper_config_context,
	IN		void* const			h_adapter,
	OUT		ipoib_adapter_t**  const	pp_adapter );


ib_api_status_t
ipoib_start_adapter(
	IN				ipoib_adapter_t* const		p_adapter );


void
ipoib_destroy_adapter(
	IN				ipoib_adapter_t* const		p_adapter );


/* Joins/leaves mcast groups based on currently programmed mcast MACs. */
void
ipoib_refresh_mcast(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				mac_addr_t* const			p_mac_array,
	IN		const	uint8_t						num_macs );
/*
* PARAMETERS
*	p_adapter
*		Instance whose multicast MAC address list to modify.
*
*	p_mac_array
*		Array of multicast MAC addresses assigned to the adapter.
*
*	num_macs
*		Number of MAC addresses in the array.
*********/


NDIS_STATUS
ipoib_get_recv_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN				pending_oid_t* const		p_oid_info );


void
ipoib_inc_recv_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN		const	size_t						bytes OPTIONAL );


NDIS_STATUS
ipoib_get_send_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN				pending_oid_t* const		p_oid_info );


void
ipoib_inc_send_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN		const	size_t						bytes OPTIONAL );


void
ipoib_set_rate(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	uint8_t						link_width,
	IN		const	uint8_t						link_speed );


ib_api_status_t
ipoib_set_active(
	IN				ipoib_adapter_t* const		p_adapter );

void
ipoib_set_inactive(
	IN				ipoib_adapter_t* const		p_adapter );

ib_api_status_t
ipoib_reset_adapter(
	IN				ipoib_adapter_t* const		p_adapter );

void
ipoib_reg_addrs(
	IN				ipoib_adapter_t* const		p_adapter );

void
ipoib_dereg_addrs(
	IN				ipoib_adapter_t* const		p_adapter );

#endif	/* _IPOIB_ADAPTER_H_ */
