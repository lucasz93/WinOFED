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

#ifndef _VNIC_DRIVER_H_
#define _VNIC_DRIVER_H_

#include "vnic_adapter.h"
#include "vnic_debug.h"


#if defined(NDIS50_MINIPORT)
#define MAJOR_NDIS_VERSION 5
#define MINOR_NDIS_VERSION 0
#elif defined (NDIS51_MINIPORT)
#define MAJOR_NDIS_VERSION 5
#define MINOR_NDIS_VERSION 1
#else
#error NDIS Version not defined, try defining NDIS50_MINIPORT or NDIS51_MINIPORT
#endif

#include <ndis.h>

static const NDIS_OID SUPPORTED_OIDS[] =
{
	OID_GEN_SUPPORTED_LIST,
	OID_GEN_HARDWARE_STATUS,
	OID_GEN_MEDIA_SUPPORTED,
	OID_GEN_MEDIA_IN_USE,
	OID_GEN_MAXIMUM_LOOKAHEAD,
	OID_GEN_MAXIMUM_FRAME_SIZE,
	OID_GEN_LINK_SPEED,
	OID_GEN_TRANSMIT_BUFFER_SPACE,
	OID_GEN_RECEIVE_BUFFER_SPACE,
	OID_GEN_TRANSMIT_BLOCK_SIZE,
	OID_GEN_RECEIVE_BLOCK_SIZE,
	OID_GEN_VENDOR_ID,
	OID_GEN_VENDOR_DESCRIPTION,
	OID_GEN_CURRENT_PACKET_FILTER,
	OID_GEN_CURRENT_LOOKAHEAD,
	OID_GEN_DRIVER_VERSION,
	OID_GEN_MAXIMUM_TOTAL_SIZE,
	OID_GEN_PROTOCOL_OPTIONS, // ?
	OID_GEN_MAC_OPTIONS,
	OID_GEN_MEDIA_CONNECT_STATUS,
	OID_GEN_MAXIMUM_SEND_PACKETS,
	OID_GEN_VENDOR_DRIVER_VERSION,
	OID_GEN_PHYSICAL_MEDIUM,
	OID_GEN_XMIT_OK,
	OID_GEN_RCV_OK,
	OID_GEN_XMIT_ERROR,
	OID_GEN_RCV_ERROR,
	OID_GEN_RCV_NO_BUFFER,
	OID_GEN_DIRECTED_BYTES_XMIT,
	OID_GEN_DIRECTED_FRAMES_XMIT,
	OID_GEN_MULTICAST_BYTES_XMIT,
	OID_GEN_MULTICAST_FRAMES_XMIT,
	OID_GEN_BROADCAST_BYTES_XMIT,
	OID_GEN_BROADCAST_FRAMES_XMIT,
	OID_GEN_DIRECTED_BYTES_RCV,
	OID_GEN_DIRECTED_FRAMES_RCV,
	OID_GEN_MULTICAST_BYTES_RCV,
	OID_GEN_MULTICAST_FRAMES_RCV,
	OID_GEN_BROADCAST_BYTES_RCV,
	OID_GEN_BROADCAST_FRAMES_RCV,
	OID_GEN_VLAN_ID,
	OID_802_3_PERMANENT_ADDRESS,
	OID_802_3_CURRENT_ADDRESS,
	OID_802_3_MULTICAST_LIST,
	OID_802_3_MAXIMUM_LIST_SIZE,
	OID_802_3_MAC_OPTIONS,
	OID_802_3_RCV_ERROR_ALIGNMENT,
	OID_802_3_XMIT_ONE_COLLISION,
	OID_802_3_XMIT_MORE_COLLISIONS,
	OID_TCP_TASK_OFFLOAD
};

static const unsigned char VENDOR_ID[] = {0x00, 0x06, 0x6A, 0x00};
#define VENDOR_DESCRIPTION "Virtual Ethernet over InfiniBand"
#define	DEFAULT_VNIC_NAME	"VNIC"

NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT				p_drv_obj,
	IN				PUNICODE_STRING				p_reg_path );

VOID
vnic_unload(
	IN				PDRIVER_OBJECT				p_drv_obj );

NDIS_STATUS
vnic_initialize(
		OUT			PNDIS_STATUS				p_open_err_status,
		OUT			PUINT						p_selected_medium_index,
	IN				PNDIS_MEDIUM				medium_array,
	IN				UINT						medium_array_size,
	IN				NDIS_HANDLE					h_handle,
	IN				NDIS_HANDLE					wrapper_configuration_context );

BOOLEAN
vnic_check_for_hang(
	IN				NDIS_HANDLE					adapter_context );

void
vnic_halt(
	IN				NDIS_HANDLE					adapter_context );

NDIS_STATUS
vnic_oid_query_info(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_OID					oid,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len,
		OUT			PULONG						p_bytes_written,
		OUT			PULONG						p_bytes_needed );

NDIS_STATUS
vnic_reset(
		OUT			PBOOLEAN					p_addressing_reset,
	IN				NDIS_HANDLE					adapter_context );

NDIS_STATUS
vnic_oid_set_info(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_OID					oid,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_length,
		OUT			PULONG						p_bytes_read,
		OUT			PULONG						p_bytes_needed );

void
vnic_send_packets(
	IN				NDIS_HANDLE					adapter_context,
	IN				PPNDIS_PACKET				packet_array,
	IN				UINT						num_packets );

void
vnic_cancel_xmit(
	IN				NDIS_HANDLE					adapter_context,
    IN				PVOID						cancel_id );
void
vnic_pnp_notify(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_DEVICE_PNP_EVENT		pnp_event,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len );

void
vnic_shutdown(
	IN				PVOID						adapter_context );

NDIS_STATUS
vnic_get_agapter_interface(
	IN	NDIS_HANDLE			h_handle,
	IN	vnic_adapter_t		*p_adapter);


/* same as cl_timer_start() except it takes timeout param in usec */
/* need to run kicktimer */
cl_status_t 
usec_timer_start(
	IN		cl_timer_t* const	p_timer,
	IN		const uint32_t		time_usec );

#endif /* _VNIC_DRIVER_H_ */
