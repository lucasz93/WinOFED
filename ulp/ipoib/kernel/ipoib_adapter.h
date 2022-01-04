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
#include "shutter.h"
#include "iba/ndk_ifc.h"



/*
 * Definitions
 */
#define MAX_MCAST				32

#define IPV4_ADDR_SIZE			4

#define PORT_NUM_INDEX_IN_GUID	3 /* 0 based index into big endian GUID to get port number */

/*
 * Macros
 */
typedef enum 
{
	CSUM_DISABLED = 0, 
	CSUM_ENABLED, 
	CSUM_BYPASS
} csum_flag_t;

typedef 	uint32_t ipoib_state_t;
#define		IPOIB_UNINIT		0
#define		IPOIB_INIT			1
#define 	IPOIB_PAUSED 		2
#define		IPOIB_PAUSING		4
#define	    IPOIB_RUNNING		8
#define	    IPOIB_RESET_OR_DOWN 16


typedef struct _ipoib_offloads_cap_ {
	boolean_t	lso;
	boolean_t	send_chksum_offload;
	boolean_t	recv_chksum_offload;
}
ipoib_offloads_cap_t;


typedef struct _ipoib_params
{
	int32_t		rq_depth;
	int32_t		rq_low_watermark;
	int32_t		sq_depth;
	csum_flag_t	send_chksum_offload;
	csum_flag_t	recv_chksum_offload;
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
	boolean_t	cm_enabled;
	uint32_t	cm_payload_mtu;
	uint32_t	cm_xfer_block_size;
	boolean_t LsoV1IPv4;              // Registry value for large send offload Version 1
	boolean_t LsoV2IPv4;              // Registry value for large send offload Version 2 IPV4
	boolean_t LsoV2IPv6;              // Registry value for large send offload Version2 IPV6

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
*		0 - No hardware checksum
*		1 - Try to offload if the device support it
*		2 - Always report success (checksum bypass)
*
*	wsdp_enabled
*		Flag to indicate whether WSDP is enabled for an adapter.
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
*		
*		If using UD mode:
*		It should be decremented by size of IPoIB header (==4B)
*		For example, if the HCA support 4K MTU, 
*		upper threshold for payload mtu is 4092B and not 4096B
*
*		If using CM mode:
*		MTU will be not limited by 4K threshold.
*		UD QP still may be used for different protocols (like ARP).
*		For these situations the threshold for the UD QP will take the default
*		value
*
*	lso
*		(TRUE) Indicates support for hardware large/giant send offload
*		
*********/


typedef struct _pending_oid
{
	NDIS_OID			oid;
	PVOID				p_buf;
	ULONG				buf_len;
	PULONG				p_bytes_used;
	PULONG				p_bytes_needed;
	PNDIS_OID_REQUEST	p_pending_oid;
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
	ipoib_offloads_cap_t	offload_cap;
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

	cl_perf_t				perf;
    NDIS_HANDLE  			NdisMiniportDmaHandle;
	ipoib_state_t			ipoib_state;
	ib_al_ifc_t				*p_ifc;

	ULONG					sg_list_size;

	// statistics 
	PIPOIB_ST_DEVICE		p_stat;
	ULONG					n_send_NBL;			// number of send NBLs, gotten from NDIS
	ULONG					n_send_NBL_done;	// number of send NBLs, completed

	//
	shutter_t               recv_shutter;

#if defined(NDIS630_MINIPORT)
	NDK_HANDLE 				h_ndk;
#endif

    IBAT_ROUTING_CONTEXT    ibatRouter;

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

static inline void ipoib_cnt_inc( PULONG p_cnt)
{
	++*p_cnt;
}

ib_api_status_t
ipoib_create_adapter(
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
ipoib_get_gen_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	OUT             pending_oid_t* const		p_oid_info );

NDIS_STATUS
ipoib_get_recv_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN				pending_oid_t* const		p_oid_info );


void
ipoib_inc_recv_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN		const	size_t						bytes OPTIONAL,
	IN		const	size_t						packets OPTIONAL );


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

#define IPOIB_INIT_NDIS_STATUS_INDICATION(_pStatusIndication, _M, _St, _Buf, _BufSize)        \
    {                                                                                      \
        memset(_pStatusIndication, 0, sizeof(NDIS_STATUS_INDICATION));                     \
        (_pStatusIndication)->Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;            \
        (_pStatusIndication)->Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;         \
        (_pStatusIndication)->Header.Size = sizeof(NDIS_STATUS_INDICATION);                \
        (_pStatusIndication)->SourceHandle = _M;                                           \
        (_pStatusIndication)->StatusCode = _St;                                            \
        (_pStatusIndication)->StatusBuffer = _Buf;                                         \
        (_pStatusIndication)->StatusBufferSize = _BufSize;                                 \
    }

#define IPOIB_MEDIA_MAX_SPEED	40000000000

#define SET_PORT_RATE_BPS(x) (uint64_t(100) * (x))


#endif	/* _IPOIB_ADAPTER_H_ */
