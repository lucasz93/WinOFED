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



#include <complib/cl_init.h>
#include "vnic_driver.h"


vnic_globals_t	g_vnic;

g_registry_params_t	reg_array[VNIC_REGISTRY_TBL_SIZE] = {
	{L"", 0},
	{L"MinHostPoolSz",			ENTRY_INIT_VALUE},
	{L"HostRecvPoolEntries",	ENTRY_INIT_VALUE},
	{L"MinEiocPoolSz",			ENTRY_INIT_VALUE},
	{L"MaxEiocPoolSz",			ENTRY_INIT_VALUE},
	{L"MinHostKickTimeout",		ENTRY_INIT_VALUE},
	{L"MaxHostKickTimeout",		ENTRY_INIT_VALUE},
	{L"MinHostKickEntries",		ENTRY_INIT_VALUE},
	{L"MaxHostKickEntries",		ENTRY_INIT_VALUE},
	{L"MinHostKickBytes",		ENTRY_INIT_VALUE},
	{L"MaxHostKickBytes",		ENTRY_INIT_VALUE},
	{L"MinHostUpdateSz",		ENTRY_INIT_VALUE},
	{L"MaxHostUpdateSz",		ENTRY_INIT_VALUE},
	{L"MinEiocUpdateSz",		ENTRY_INIT_VALUE},
	{L"MaxEiocUpdateSz",		ENTRY_INIT_VALUE},
	{L"HeartbeatTimeout",		ENTRY_INIT_VALUE},
};

#define DEFAULT_HOST_NAME "VNIC Host"

uint32_t g_vnic_dbg_lvl = VNIC_DEBUG_FLAGS;

static void
_vnic_complete_query(
	IN				vnic_adapter_t* const		p_adapter,
	IN				pending_oid_t* const		p_oid_info,
	IN		const	NDIS_STATUS					status,
	IN		const	void* const					p_buf,
	IN		const	ULONG						buf_len );

static NDIS_STATUS
__vnic_get_tcp_task_offload(
	IN				vnic_adapter_t*			p_adapter,
	IN				pending_oid_t* const		p_oid_info );

static NDIS_STATUS
__vnic_set_tcp_task_offload(
	IN				vnic_adapter_t*			p_adapter,
	IN				void* const				p_info_buf,
	IN				ULONG* const			p_info_len );

static NDIS_STATUS
_vnic_process_packet_filter(
		IN	vnic_adapter_t* const	p_adapter,
		IN	ULONG					pkt_filter );

static void
__vnic_read_machine_name( void );

static NTSTATUS
__vnic_read_service_registry(
		IN		UNICODE_STRING* const	p_registry_path );

static NDIS_STATUS
__vnic_set_machine_name(
		IN		VOID	*p_uni_array,
		IN		USHORT	buf_size );

/*
p_drv_obj
	Pointer to Driver Object for this device driver
p_registry_path
	Pointer to unicode string containing path to this driver's registry area
return
	STATUS_SUCCESS, NDIS_STATUS_BAD_CHARACTERISTICS, NDIS_STATUS_BAD_VERSION,
	NDIS_STATUS_RESOURCES, or NDIS_STATUS_FAILURE
IRQL = PASSIVE_LEVEL
*/
NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT				p_drv_obj,
	IN				PUNICODE_STRING				p_registry_path )
{
	NDIS_STATUS						status;
	NDIS_MINIPORT_CHARACTERISTICS	characteristics;

	VNIC_ENTER( VNIC_DBG_INIT );

#ifdef _DEBUG_
	PAGED_CODE();
#endif
/*
	status = CL_INIT;

	if( !NT_SUCCESS( status ) )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("cl_init failed.\n") );
		return status;
	}
*/
	status		= NDIS_STATUS_SUCCESS;

	g_vnic.ndis_handle = NULL;
	g_vnic.shutdown = 0;
	g_vnic.p_params = NULL;

	NdisMInitializeWrapper( &g_vnic.ndis_handle, p_drv_obj, p_registry_path, NULL );

	if ( g_vnic.ndis_handle == NULL )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("NdisMInitializeWrapper failed\n"));
		CL_DEINIT;
		return NDIS_STATUS_FAILURE;
	}

	cl_memclr( &characteristics, sizeof(characteristics) );

	characteristics.MajorNdisVersion		= MAJOR_NDIS_VERSION;
	characteristics.MinorNdisVersion		= MINOR_NDIS_VERSION;
	characteristics.CheckForHangHandler		= vnic_check_for_hang;
	characteristics.HaltHandler				= vnic_halt;
	characteristics.InitializeHandler		= vnic_initialize;
	characteristics.QueryInformationHandler	= vnic_oid_query_info;
	characteristics.ResetHandler			= vnic_reset;
	characteristics.SetInformationHandler	= vnic_oid_set_info;
	characteristics.ReturnPacketHandler		= vnic_return_packet;
	characteristics.SendPacketsHandler		= vnic_send_packets;

#ifdef NDIS51_MINIPORT
	characteristics.CancelSendPacketsHandler = vnic_cancel_xmit;
	characteristics.PnPEventNotifyHandler	= vnic_pnp_notify;
	characteristics.AdapterShutdownHandler	= vnic_shutdown;
#endif

	status = NdisMRegisterMiniport(
		g_vnic.ndis_handle, &characteristics, sizeof(characteristics) );

	if( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("NdisMRegisterMiniport failed with status of %d\n", status) );
		NdisTerminateWrapper( g_vnic.ndis_handle, NULL );
		CL_DEINIT;
	}
	else
	{
		NdisMRegisterUnloadHandler( g_vnic.ndis_handle, vnic_unload );
	
		__vnic_read_machine_name();
		__vnic_read_service_registry( p_registry_path );

#if ( LBFO_ENABLED )
		cl_qlist_init( &g_vnic.primary_list );
		cl_qlist_init( &g_vnic.secondary_list );
#endif
		cl_qlist_init( &g_vnic.adapter_list);
	}

	VNIC_EXIT( VNIC_DBG_INIT );
	return status;
}


VOID
vnic_unload(
	IN				PDRIVER_OBJECT				p_drv_obj )
{
	VNIC_ENTER( VNIC_DBG_INIT );

	UNREFERENCED_PARAMETER( p_drv_obj );

	VNIC_TRACE_EXIT( VNIC_DBG_INFO,
		(" Driver Unloaded....\n"));
	//CL_DEINIT;
}


//! Initialization function called for each IOC discovered
/*  The MiniportInitialize function is a required function that sets up a
NIC (or virtual NIC) for network I/O operations, claims all hardware
resources necessary to the NIC in the registry, and allocates resources
the driver needs to carry out network I/O operations.
IRQL = PASSIVE_LEVEL

@param p_open_status Pointer to a status field set if this function returns NDIS_STATUS_OPEN_ERROR
@param p_selected_medium_index Pointer to unsigned integer noting index into medium_array for this NIC
@param medium_array Array of mediums for this NIC
@param medium_array_size Number of elements in medium_array
@param h_handle Handle assigned by NDIS for this NIC
@param wrapper_config_context Handle used for Ndis initialization functions
@return NDIS_STATUS_SUCCESS, NDIS_STATUS_UNSUPPORTED_MEDIA, NDIS_STATUS_RESOURCES,
NDIS_STATUS_NOT_SUPPORTED
*/
NDIS_STATUS
vnic_initialize(
		OUT			PNDIS_STATUS				p_open_status,
		OUT			PUINT						p_selected_medium_index,
	IN				PNDIS_MEDIUM				medium_array,
	IN				UINT						medium_array_size,
	IN				NDIS_HANDLE					h_handle,
	IN				NDIS_HANDLE					wrapper_config_context )
{
	NDIS_STATUS			status = NDIS_STATUS_SUCCESS;
	ib_api_status_t		ib_status;
	ib_pnp_req_t		pnp_req;
	UINT				medium_index;
	vnic_adapter_t		*p_adapter;

	VNIC_ENTER( VNIC_DBG_INIT );

#ifdef _DEBUG_
	PAGED_CODE();
#endif

	UNUSED_PARAM( p_open_status );

	/* Search for our medium */
	for( medium_index = 0; medium_index < medium_array_size; ++medium_index )
	{
		/* Check to see if we found our medium */
		if( medium_array[medium_index] == NdisMedium802_3 )
			break;
	}

	if( medium_index == medium_array_size ) /* Never found it */
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR, ("No supported media.\n") );
		return NDIS_STATUS_UNSUPPORTED_MEDIA;
	}

	*p_selected_medium_index = medium_index;

		/* Create the adapter */
	ib_status = vnic_create_adapter( h_handle, wrapper_config_context, &p_adapter );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("returned status %x\n", ib_status ) );
		return NDIS_STATUS_FAILURE;
	}

	/* set NDIS features we support */
	NdisMSetAttributesEx( h_handle,
						(NDIS_HANDLE)p_adapter,
						0,						/*check for hung timeout, 2 sec */
						NDIS_ATTRIBUTE_BUS_MASTER |
						NDIS_ATTRIBUTE_DESERIALIZE |
						NDIS_ATTRIBUTE_SURPRISE_REMOVE_OK |
						NDIS_ATTRIBUTE_USES_SAFE_BUFFER_APIS,
						NdisInterfacePNPBus );

	/* Register for IOC events */
	pnp_req.pfn_pnp_cb = __vnic_pnp_cb;
	pnp_req.pnp_class = IB_PNP_IOC | IB_PNP_FLAG_REG_SYNC;
	pnp_req.pnp_context = (const void *)p_adapter;

	ib_status = p_adapter->ifc.reg_pnp( p_adapter->h_al, &pnp_req, &p_adapter->h_pnp );

	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("ib_reg_pnp returned %s\n", p_adapter->ifc.get_err_str( ib_status )) );
		NdisWriteErrorLogEntry( h_handle,
			NDIS_ERROR_CODE_OUT_OF_RESOURCES, 0);
		status = NDIS_STATUS_FAILURE;
	}
	else if( p_adapter->state != INIC_REGISTERED )
	{
		status = NDIS_STATUS_OPEN_FAILED;
		*p_open_status = NDIS_STATUS_DEVICE_FAILED;

		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC[%d] ADAPTER Initialization Failed\n",	p_adapter->ioc_num ) );
		NdisWriteErrorLogEntry( h_handle, 
			NDIS_ERROR_CODE_HARDWARE_FAILURE, 1, p_adapter->ioc_num );
	}

	if( status != NDIS_STATUS_SUCCESS )
	{
		vnic_destroy_adapter( p_adapter );
	}

	VNIC_EXIT( VNIC_DBG_INIT );
	return status;

}


//! Deallocates resources when the NIC is removed and halts the NIC..
/*  IRQL = DISPATCH_LEVEL

@param adapter_context The adapter context allocated at start
*/
void
vnic_halt(
	IN			NDIS_HANDLE			adapter_context )
{
	VNIC_ENTER( VNIC_DBG_INIT );
	CL_ASSERT( adapter_context );

	vnic_destroy_adapter( (vnic_adapter_t*)adapter_context );

	VNIC_EXIT( VNIC_DBG_INIT );
}


//! Reports the state of the NIC, or monitors the responsiveness of an underlying device driver.
/*  IRQL = DISPATCH_LEVEL

@param adapter_context The adapter context allocated at start
@return TRUE if the driver determines that its NIC is not operating
*/

BOOLEAN
vnic_check_for_hang(
	IN				NDIS_HANDLE					adapter_context )
{
	vnic_adapter_t	*p_adapter;

	CL_ASSERT( adapter_context );
	p_adapter = (vnic_adapter_t*)adapter_context;

	/* current path is Ok */
	if( netpath_is_connected( p_adapter->p_currentPath ) )
	{
		return FALSE;
	}

	if( p_adapter->hung != 0 && 
		p_adapter->hung >= p_adapter->num_paths )
	{
		VNIC_TRACE( VNIC_DBG_ERROR, 
			("IOC[%d] Adapter Hung: %d NumPath: %d\n",
			p_adapter->ioc_num,	p_adapter->hung, p_adapter->num_paths ) );
			return TRUE;
	}

	return FALSE;
}


//! Returns information about the capabilities and status of the driver and/or its NIC.
/*  IRQL = DISPATCH_LEVEL

@param adapter_context The adapter context allocated at start
@param oid Object ID representing the query operation to be carried out
@param info_buf Buffer containing any input for this query and location for output
@param info_buf_len Number of bytes available in info_buf
@param p_bytes_written Pointer to number of bytes written into info_buf
@param p_bytes_needed Pointer to number of bytes needed to satisfy this oid
@return NDIS_STATUS_SUCCESS, NDIS_STATUS_PENDING, NDIS_STATUS_INVALID_OID,
NDIS_STATUS_INVALID_LENGTH, NDIS_STATUS_NOT_ACCEPTED, NDIS_STATUS_NOT_SUPPORTED,
NDIS_STATUS_RESOURCES
*/
NDIS_STATUS
vnic_oid_query_info(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_OID					oid,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len,
		OUT			PULONG						p_bytes_written,
		OUT			PULONG						p_bytes_needed )
{
	vnic_adapter_t		*p_adapter;
	Netpath_t			*p_netpath;

	NDIS_STATUS			status;
	USHORT				version;
	uint32_t			info32 = 0;
	uint64_t			info64 = 0;
	PVOID				src_buf;
	ULONG				buf_len;
	pending_oid_t		oid_info;

	VNIC_ENTER( VNIC_DBG_OID );

	oid_info.oid = oid;
	oid_info.p_buf = info_buf;
	oid_info.buf_len = info_buf_len;
	oid_info.p_bytes_used = p_bytes_written;
	oid_info.p_bytes_needed = p_bytes_needed;

	CL_ASSERT( adapter_context );
	p_adapter = (vnic_adapter_t *)adapter_context;

	CL_ASSERT( p_bytes_written );
	CL_ASSERT( p_bytes_needed );

	status = NDIS_STATUS_SUCCESS;
	src_buf = &info32;
	buf_len = sizeof(info32);

	if( g_vnic.shutdown != 0 )
	{
		*p_bytes_written = 0;
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	p_netpath = p_adapter->p_currentPath;
	if( !p_netpath || !p_netpath->carrier )
	{
		status = NDIS_STATUS_NOT_ACCEPTED;
		goto complete;
	}

	switch( oid )
	{
	/* Required General */
	case OID_GEN_SUPPORTED_LIST:
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_SUPPORTED_LIST\n") );
		src_buf = (PVOID)SUPPORTED_OIDS;
		buf_len = sizeof(SUPPORTED_OIDS);
		break;

	case OID_GEN_HARDWARE_STATUS:
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_HARDWARE_STATUS\n") );

		if( p_netpath->carrier )
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("returning NdisHardwareStatusReady\n") );
			info32 = NdisHardwareStatusReady;
		}
		else
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("returning NdisHardwareStatusInitializing\n") );
			info32 = NdisHardwareStatusNotReady;
		}
		break;

	case OID_GEN_MEDIA_SUPPORTED:
	case OID_GEN_MEDIA_IN_USE:
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_MEDIA_SUPPORTED "
			"or OID_GEN_MEDIA_IN_USE\n") );
		info32 = NdisMedium802_3;
		break;
			
	case OID_GEN_MAXIMUM_TOTAL_SIZE:	/* frame size inlcuding header */
			info32  = sizeof( eth_hdr_t );
	case OID_GEN_MAXIMUM_FRAME_SIZE:	/* wihout header */
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_MAXIMUM_FRAME_SIZE\n") );
		if( !p_netpath->carrier )
		{
			info32 += MIN_MTU;
		}
		else
		{
			info32 += p_adapter->p_currentPath->pViport->mtu;
		}
		break;

	case OID_GEN_LINK_SPEED:
		VNIC_TRACE( VNIC_DBG_OID,
				("received query for OID_GEN_LINK_SPEED\n") );

		if( p_netpath->carrier )
		{
			/* if we get link speed value - it is in Mbps units - have to convert to 100bps*/
			info32 = ( p_adapter->link_speed )?	
					( p_adapter->link_speed * LINK_SPEED_1MBIT_x100BPS ):
					DEFAULT_LINK_SPEED_x100BPS;
		}
		else
		{
			status = NDIS_STATUS_NOT_ACCEPTED;
		}
		break;

	case OID_GEN_TRANSMIT_BUFFER_SPACE:
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_TRANSMIT_BUFFER_SPACE\n") );
		if ( !p_netpath->carrier )
		{
			status= NDIS_STATUS_NOT_ACCEPTED;
		}
		else
		{
			info32 = p_netpath->pViport->data.xmitPool.bufferSz *
				p_netpath->pViport->data.xmitPool.poolSz;
		}
		break;

	case OID_GEN_RECEIVE_BUFFER_SPACE:
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_RECEIVE_BUFFER_SPACE "
			"or OID_GEN_RECEIVE_BUFFER_SPACE\n") );
		if ( !p_netpath->carrier )
		{
			status = NDIS_STATUS_NOT_ACCEPTED;
		}
		else
		{
			info32 = p_netpath->pViport->data.recvPool.bufferSz *
				p_netpath->pViport->data.recvPool.poolSz;
		}
		break;
	case OID_GEN_MAXIMUM_LOOKAHEAD:
	case OID_GEN_CURRENT_LOOKAHEAD:
	case OID_GEN_TRANSMIT_BLOCK_SIZE:
	case OID_GEN_RECEIVE_BLOCK_SIZE:
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_MAXIMUM_LOOKAHEAD "
			"or OID_GEN_CURRENT_LOOKAHEAD or "
			"OID_GEN_TRANSMIT_BLOCK_SIZE or "
			"OID_GEN_RECEIVE_BLOCK_SIZE\n") );
		if( !p_netpath->carrier )
		{
			info32 = MIN_MTU;
		}
		else
		{
			info32 = p_adapter->p_currentPath->pViport->mtu;
		}
		/*TODO: add VLAN tag size if support requested */
		info32 += sizeof(eth_hdr_t);
		break;

	case OID_GEN_VENDOR_ID:
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_VENDOR_ID\n") );

		src_buf = (void*)VENDOR_ID;
		buf_len = sizeof(VENDOR_ID);
		break;

	case OID_GEN_VENDOR_DESCRIPTION:
		VNIC_TRACE( VNIC_DBG_OID,
			("received query for OID_GEN_VENDOR_DESCRIPTION\n") );
		src_buf = VENDOR_DESCRIPTION;
		buf_len = sizeof(VENDOR_DESCRIPTION);
		break;

	case OID_GEN_VENDOR_DRIVER_VERSION:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_VENDOR_DRIVER_VERSION\n" ) );
		src_buf = &version;
		buf_len = sizeof(version);
		//TODO: Figure out what the right version is.
		version = INIC_MAJORVERSION << 8 | INIC_MINORVERSION;
		break;

	case OID_GEN_PHYSICAL_MEDIUM:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_PHYSICAL_MEDIUM\n" ) );
		info32 = NdisPhysicalMediumUnspecified;
		break;

	case OID_GEN_CURRENT_PACKET_FILTER:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_CURRENT_PACKET_FILTER\n" ) );
		info32 = p_adapter->packet_filter;
		break;

	case OID_GEN_DRIVER_VERSION:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_DRIVER_VERSION\n" ) );
		src_buf = &version;
		buf_len = sizeof(version);
		version = MAJOR_NDIS_VERSION << 8 | MINOR_NDIS_VERSION;
		break;

	case OID_GEN_MAC_OPTIONS:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_MAC_OPTIONS\n" ) );
		info32 = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
				NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
				NDIS_MAC_OPTION_NO_LOOPBACK |
				NDIS_MAC_OPTION_FULL_DUPLEX |
				NDIS_MAC_OPTION_8021P_PRIORITY |
				NDIS_MAC_OPTION_8021Q_VLAN;

		break;

	case OID_GEN_MEDIA_CONNECT_STATUS:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_MEDIA_CONNECT_STATUS\n" ) );

		info32 =  ( p_adapter->carrier )?
					NdisMediaStateConnected :
					NdisMediaStateDisconnected;
		break;

	case OID_GEN_MAXIMUM_SEND_PACKETS:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_MAXIMUM_SEND_PACKETS\n" ) );
		info32 = MAXLONG; // NDIS ignored it anyway
		break;

	/* Required General Statistics */
	case OID_GEN_XMIT_OK:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_XMIT_OK\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
			info64	= p_netpath->pViport->stats.ifOutOk;
			src_buf = &info64;
			buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_RCV_OK:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_RCV_OK\n" ) );
		if ( !p_netpath->carrier)
		{
			info32 = 0;
			break;
		}
		if ( info_buf_len == sizeof(info32) )
		{
			info32 = (uint32_t)p_netpath->pViport->stats.ifInOk;
		}
		else
		{
			info64	= p_netpath->pViport->stats.ifInOk;
			src_buf = &info64;
			buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_XMIT_ERROR:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_XMIT_ERROR\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64	= p_netpath->pViport->stats.ifOutErrors;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_RCV_ERROR:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_RCV_ERROR\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifInErrors;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_RCV_NO_BUFFER:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_RCV_NO_BUFFER\n" ) );
		info32 = 0;
		break;

	case OID_GEN_DIRECTED_BYTES_XMIT:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_DIRECTED_BYTES_XMIT\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifOutUcastBytes;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_DIRECTED_FRAMES_XMIT:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_DIRECTED_FRAMES_XMIT\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifOutNUcastPkts;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_MULTICAST_BYTES_XMIT:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_MULTICAST_BYTES_XMIT\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifOutMulticastBytes;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_MULTICAST_FRAMES_XMIT:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_MULTICAST_FRAMES_XMIT\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifOutMulticastPkts;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_BROADCAST_BYTES_XMIT:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_BROADCAST_BYTES_XMIT\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
			info64 = p_netpath->pViport->stats.ifOutBroadcastBytes;
			src_buf = &info64;
			buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_BROADCAST_FRAMES_XMIT:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_BROADCAST_FRAMES_XMIT\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifOutBroadcastPkts;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;
	case OID_GEN_DIRECTED_BYTES_RCV:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_DIRECTED_BYTES_RCV\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifInUcastBytes;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_DIRECTED_FRAMES_RCV:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_DIRECTED_FRAMES_RCV\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifInNUcastPkts;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_MULTICAST_BYTES_RCV:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_MULTICAST_BYTES_RCV\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifInMulticastBytes;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_MULTICAST_FRAMES_RCV:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_MULTICAST_FRAMES_RCV\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifInMulticastPkts;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_BROADCAST_BYTES_RCV:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_BROADCAST_BYTES_RCV\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifInBroadcastBytes;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	case OID_GEN_BROADCAST_FRAMES_RCV:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_GEN_BROADCAST_FRAMES_RCV\n" ) );
		if ( !p_netpath->carrier )
		{
			info32 = 0;
		}
		else
		{
		info64 = p_netpath->pViport->stats.ifInBroadcastPkts;
		src_buf = &info64;
		buf_len = sizeof(info64);
		}
		break;

	/* Required Ethernet operational characteristics */
	case OID_802_3_PERMANENT_ADDRESS:
	case OID_802_3_CURRENT_ADDRESS:
#if defined( _DEBUG_ )
		if( oid == OID_802_3_PERMANENT_ADDRESS )
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("  received query for OID_802_3_PERMANENT_ADDRESS\n" ) );
		}
		else
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("  received query for OID_802_3_CURRENT_ADDRESS\n" ) );
		}
#endif	/* defined( _DEBUG_ )*/
		if( !p_netpath->pViport ||
			p_netpath->pViport->errored ||
			p_netpath->pViport->disconnect )
		{
			status = NDIS_STATUS_NOT_ACCEPTED;
			break;
		}
		if ( !p_adapter->macSet )
		{
			p_adapter->pending_query = TRUE;
			p_adapter->query_oid = oid_info;

			VNIC_TRACE( VNIC_DBG_OID,
				("returning NDIS_STATUS_PENDING\n") );
			status = NDIS_STATUS_PENDING;
		}
		else
		{
			src_buf = &p_netpath->pViport->hwMacAddress;
			buf_len = HW_ADDR_LEN;
		}
		break;

	case OID_802_3_MULTICAST_LIST:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_802_3_MULTICAST_LIST\n" ) );
	
		if( !p_netpath->carrier ||
			!(p_netpath->pViport->flags & INIC_FLAG_ENABLE_NIC) )
		{
/*			p_adapter->pending_query = TRUE;
			p_adapter->query_oid = oid_info;

			VNIC_TRACE( VNIC_DBG_OID,
				("returning NDIS_STATUS_PENDING\n") );
			status = NDIS_STATUS_PENDING; 
*/
			status = NDIS_STATUS_NOT_ACCEPTED;
			break;
		}
		if( p_adapter->mc_count > 0 )
		{
			buf_len = p_adapter->mc_count * sizeof( mac_addr_t );
			src_buf = &p_adapter->mcast_array;
		}
		else
		{
			info32 = 0;
		}
		break;

	case OID_802_3_MAXIMUM_LIST_SIZE:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_802_3_MAXIMUM_LIST_SIZE\n" ) );
		if ( !p_adapter->macSet )
		{
			info32 = MAX_MCAST;
		}
		else
		{
			info32 = p_netpath->pViport->numMacAddresses - MCAST_ADDR_START;
		}
		break;
	case OID_802_3_MAC_OPTIONS:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_802_3_MAC_OPTIONS\n" ) );
		info32 = 0;
		break;

	/* Required Ethernet stats */
	case OID_802_3_RCV_ERROR_ALIGNMENT:
	case OID_802_3_XMIT_ONE_COLLISION:
	case OID_802_3_XMIT_MORE_COLLISIONS:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_802_3_RCV_ERROR_ALIGNMENT or "
			"OID_802_3_XMIT_ONE_COLLISION or "
			"OID_802_3_XMIT_MORE_COLLISIONS\n" ) );
		info32 = 0;
		break;

	case OID_TCP_TASK_OFFLOAD:
			VNIC_TRACE( VNIC_DBG_OID,
			("  received query for OID_TCP_TASK_OFFLOAD\n" ) );

		src_buf = NULL;
		status = __vnic_get_tcp_task_offload( p_adapter, &oid_info );
		break;

	case OID_GEN_VLAN_ID:
		/* return current vlan info */
		info32 =( p_adapter->vlan_info & 0x00000fff );
		break;
	/* Optional General */
	case OID_GEN_SUPPORTED_GUIDS:
	/* Optional General Stats */
	case OID_GEN_RCV_CRC_ERROR:
	case OID_GEN_TRANSMIT_QUEUE_LENGTH:

	/* Optional Ethernet Stats */
	case OID_802_3_XMIT_DEFERRED:
	case OID_802_3_XMIT_MAX_COLLISIONS:
	case OID_802_3_RCV_OVERRUN:
	case OID_802_3_XMIT_UNDERRUN:
	case OID_802_3_XMIT_HEARTBEAT_FAILURE:
	case OID_802_3_XMIT_TIMES_CRS_LOST:
	case OID_802_3_XMIT_LATE_COLLISIONS:
	case OID_PNP_CAPABILITIES:
		status = NDIS_STATUS_NOT_SUPPORTED;
		VNIC_TRACE( VNIC_DBG_OID,
			("  received an unsupported oid of 0x%.8X!\n" , oid) );
		break;

	case OID_GEN_PROTOCOL_OPTIONS:
	case OID_GEN_TRANSPORT_HEADER_OFFSET:
#ifdef NDIS51_MINIPORT
	case OID_GEN_MACHINE_NAME:
	case OID_GEN_RNDIS_CONFIG_PARAMETER:
#endif
	default:
		status = NDIS_STATUS_INVALID_OID;
		VNIC_TRACE( VNIC_DBG_OID,
			("  received an invalid oid of 0x%.8X!\n" , oid) );
		break;
	}

	/*
	 * Complete the request as if it was handled asynchronously to maximize
	 * code reuse for when we really handle the requests asynchronously.
	 * Note that this requires the QueryInformation entry point to always
	 * return NDIS_STATUS_PENDING
	 */
complete:
	if( status != NDIS_STATUS_PENDING )
	{
		_vnic_complete_query(
			p_adapter, &oid_info, status, src_buf, buf_len );
	}

	VNIC_EXIT( VNIC_DBG_OID );
	return NDIS_STATUS_PENDING;
}


static void
_vnic_complete_query(
	IN				vnic_adapter_t* const		p_adapter,
	IN				pending_oid_t* const		p_oid_info,
	IN		const	NDIS_STATUS					status,
	IN		const	void* const					p_buf,
	IN		const	ULONG						buf_len )
{
	NDIS_STATUS		oid_status = status;

	VNIC_ENTER( VNIC_DBG_OID );

	CL_ASSERT( status != NDIS_STATUS_PENDING );

	if( status == NDIS_STATUS_SUCCESS )
	{
		if( p_oid_info->buf_len < buf_len )
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("Insufficient buffer space.  "
				"Returning NDIS_STATUS_INVALID_LENGTH.\n") );
			oid_status = NDIS_STATUS_INVALID_LENGTH;
			*p_oid_info->p_bytes_needed = buf_len;
			*p_oid_info->p_bytes_used = 0;
		}
		else if( p_oid_info->p_buf )
		{
			/* Only copy if we have a distinct source buffer. */
			if( p_buf )
			{
				NdisMoveMemory( p_oid_info->p_buf, p_buf, buf_len );
				*p_oid_info->p_bytes_used = buf_len;
			}
		}
		else
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("Returning NDIS_NOT_ACCEPTED") );
			oid_status = NDIS_STATUS_NOT_ACCEPTED;
		}
	}
	else
	{
		*p_oid_info->p_bytes_used = 0;
	}

	p_adapter->pending_query = FALSE;

	NdisMQueryInformationComplete( p_adapter->h_handle, oid_status );

	VNIC_EXIT( VNIC_DBG_OID );
}


static NDIS_STATUS
__vnic_get_tcp_task_offload(
	IN				vnic_adapter_t*			p_adapter,
	IN				pending_oid_t* const		p_oid_info )
{
	NDIS_TASK_OFFLOAD_HEADER	*p_offload_hdr;
	NDIS_TASK_OFFLOAD			*p_offload_task;
	NDIS_TASK_TCP_IP_CHECKSUM	*p_offload_chksum;

	ULONG						buf_len;
	uint32_t					enabled_TxCsum;
	uint32_t					enabled_RxCsum;

	buf_len = sizeof(NDIS_TASK_OFFLOAD_HEADER) +
		sizeof(NDIS_TASK_OFFLOAD) +
		sizeof(NDIS_TASK_TCP_IP_CHECKSUM) - 1;

	*(p_oid_info->p_bytes_needed) = buf_len;

	if( p_oid_info->buf_len < buf_len )
		return NDIS_STATUS_INVALID_LENGTH;

	p_offload_hdr = (NDIS_TASK_OFFLOAD_HEADER*)p_oid_info->p_buf;
	if( p_offload_hdr->Version != NDIS_TASK_OFFLOAD_VERSION )
		return NDIS_STATUS_INVALID_DATA;

	if( p_offload_hdr->EncapsulationFormat.Encapsulation !=
		IEEE_802_3_Encapsulation )
	{
		return NDIS_STATUS_INVALID_DATA;
	}
	/* too early, we didn't get response on CMD_INIT_INIC yet */
	if( !netpath_is_connected( p_adapter->p_currentPath ) )
	{
		return NDIS_STATUS_NOT_ACCEPTED;
	}
	enabled_RxCsum = 
		(uint32_t)( p_adapter->params.UseRxCsum && 
		netpath_canRxCsum( p_adapter->p_currentPath ) );

	enabled_TxCsum = 
		(uint32_t)( p_adapter->params.UseTxCsum &&
		netpath_canTxCsum( p_adapter->p_currentPath ) );

	p_offload_hdr->OffsetFirstTask = sizeof(NDIS_TASK_OFFLOAD_HEADER);
	p_offload_task = (NDIS_TASK_OFFLOAD*)(p_offload_hdr + 1);
	p_offload_task->Version = NDIS_TASK_OFFLOAD_VERSION;
	p_offload_task->Size = sizeof(NDIS_TASK_OFFLOAD);
	p_offload_task->Task = TcpIpChecksumNdisTask;
	p_offload_task->OffsetNextTask = 0;
	p_offload_task->TaskBufferLength = sizeof(NDIS_TASK_TCP_IP_CHECKSUM);
	p_offload_chksum =
		(NDIS_TASK_TCP_IP_CHECKSUM*)p_offload_task->TaskBuffer;

	p_offload_chksum->V4Transmit.IpOptionsSupported = enabled_TxCsum;
	p_offload_chksum->V4Transmit.TcpOptionsSupported = enabled_TxCsum;
	p_offload_chksum->V4Transmit.TcpChecksum = enabled_TxCsum;
	p_offload_chksum->V4Transmit.UdpChecksum = enabled_TxCsum;
	p_offload_chksum->V4Transmit.IpChecksum = enabled_TxCsum;

	p_offload_chksum->V4Receive.IpOptionsSupported = enabled_RxCsum;
	p_offload_chksum->V4Receive.TcpOptionsSupported = enabled_RxCsum;
	p_offload_chksum->V4Receive.TcpChecksum = enabled_RxCsum;
	p_offload_chksum->V4Receive.UdpChecksum = enabled_RxCsum;
	p_offload_chksum->V4Receive.IpChecksum = enabled_RxCsum;

	p_offload_chksum->V6Transmit.IpOptionsSupported = enabled_TxCsum;
	p_offload_chksum->V6Transmit.TcpOptionsSupported = enabled_TxCsum;
	p_offload_chksum->V6Transmit.TcpChecksum = enabled_TxCsum;
	p_offload_chksum->V6Transmit.UdpChecksum = enabled_TxCsum;

	p_offload_chksum->V6Receive.IpOptionsSupported = enabled_RxCsum;
	p_offload_chksum->V6Receive.TcpOptionsSupported = enabled_RxCsum;
	p_offload_chksum->V6Receive.TcpChecksum = enabled_RxCsum;
	p_offload_chksum->V6Receive.UdpChecksum = enabled_RxCsum;

	*(p_oid_info->p_bytes_used) = buf_len;

	return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
__vnic_set_tcp_task_offload(
	IN				vnic_adapter_t*			p_adapter,
	IN				void* const				p_info_buf,
	IN				ULONG* const			p_info_len )
{
	NDIS_TASK_OFFLOAD_HEADER	*p_offload_hdr;
	NDIS_TASK_OFFLOAD			*p_offload_task;
	NDIS_TASK_TCP_IP_CHECKSUM	*p_offload_chksum;

	ULONG						enabled_TxCsum;
	ULONG						enabled_RxCsum;

	VNIC_ENTER( VNIC_DBG_OID );

	if( !netpath_is_connected( p_adapter->p_currentPath ) )
	{
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	p_offload_hdr = (NDIS_TASK_OFFLOAD_HEADER*)p_info_buf;

	if( *p_info_len < sizeof(NDIS_TASK_OFFLOAD_HEADER) )
		return NDIS_STATUS_INVALID_LENGTH;

	if( p_offload_hdr->Version != NDIS_TASK_OFFLOAD_VERSION )
		return NDIS_STATUS_INVALID_DATA;

	if( p_offload_hdr->Size != sizeof(NDIS_TASK_OFFLOAD_HEADER) )
		return NDIS_STATUS_INVALID_LENGTH;

	if( !p_offload_hdr->OffsetFirstTask )
		return NDIS_STATUS_SUCCESS;

	if( p_offload_hdr->EncapsulationFormat.Encapsulation !=
		IEEE_802_3_Encapsulation )
	{
		return NDIS_STATUS_INVALID_DATA;
	}

	p_offload_task = (NDIS_TASK_OFFLOAD*)
		(((UCHAR*)p_offload_hdr) + p_offload_hdr->OffsetFirstTask);

	if( *p_info_len < sizeof(NDIS_TASK_OFFLOAD_HEADER) +
		offsetof( NDIS_TASK_OFFLOAD, TaskBuffer ) +
		sizeof(NDIS_TASK_TCP_IP_CHECKSUM) )
	{
		return NDIS_STATUS_INVALID_LENGTH;
	}

	if( p_offload_task->Version != NDIS_TASK_OFFLOAD_VERSION )
		return NDIS_STATUS_INVALID_DATA;
	p_offload_chksum =
		(NDIS_TASK_TCP_IP_CHECKSUM*)p_offload_task->TaskBuffer;

	enabled_TxCsum = 
		( p_adapter->params.UseTxCsum && 
			netpath_canTxCsum( p_adapter->p_currentPath ) );
	enabled_RxCsum = 
		( p_adapter->params.UseRxCsum && 
			netpath_canRxCsum( p_adapter->p_currentPath ) );
	
	if( !enabled_TxCsum &&
		(p_offload_chksum->V4Transmit.IpOptionsSupported ||
		p_offload_chksum->V4Transmit.TcpOptionsSupported ||
		p_offload_chksum->V4Transmit.TcpChecksum ||
		p_offload_chksum->V4Transmit.UdpChecksum ||
		p_offload_chksum->V4Transmit.IpChecksum ||
		p_offload_chksum->V6Transmit.IpOptionsSupported ||
		p_offload_chksum->V6Transmit.TcpOptionsSupported ||
		p_offload_chksum->V6Transmit.TcpChecksum ||
		p_offload_chksum->V6Transmit.UdpChecksum ) )
	{
		return NDIS_STATUS_NOT_SUPPORTED;
	}

	if( !enabled_RxCsum &&
		(p_offload_chksum->V4Receive.IpOptionsSupported ||
		p_offload_chksum->V4Receive.TcpOptionsSupported ||
		p_offload_chksum->V4Receive.TcpChecksum ||
		p_offload_chksum->V4Receive.UdpChecksum ||
		p_offload_chksum->V4Receive.IpChecksum ||
		p_offload_chksum->V6Receive.IpOptionsSupported ||
		p_offload_chksum->V6Receive.TcpOptionsSupported ||
		p_offload_chksum->V6Receive.TcpChecksum ||
		p_offload_chksum->V6Receive.UdpChecksum ) )
	{
		return NDIS_STATUS_NOT_SUPPORTED;
	}

	VNIC_EXIT( VNIC_DBG_OID );

	return NDIS_STATUS_SUCCESS;
}


//! Issues a hardware reset to the NIC and/or resets the driver's software state.
/*  Tear down the connection and start over again.  This is only called when there is a problem.
For example, if a send, query info, or set info had a time out.  MiniportCheckForHang will
be called first.
IRQL = DISPATCH_LEVEL

@param p_addr_resetPointer to BOOLLEAN that is set to TRUE if the NDIS
library should call MiniportSetInformation to restore addressing information to the current values.
@param adapter_context The adapter context allocated at start
@return NDIS_STATUS_SUCCESS, NDIS_STATUS_PENDING, NDIS_STATUS_NOT_RESETTABLE,
NDIS_STATUS_RESET_IN_PROGRESS, NDIS_STATUS_SOFT_ERRORS, NDIS_STATUS_HARD_ERRORS
*/
NDIS_STATUS
vnic_reset(
	OUT			PBOOLEAN			p_addr_reset,
	IN			NDIS_HANDLE			adapter_context)
{
	vnic_adapter_t* p_adapter;
	ib_api_status_t		status;

	VNIC_ENTER( VNIC_DBG_INIT );

	CL_ASSERT( p_addr_reset );
	CL_ASSERT( adapter_context );

	p_adapter = (vnic_adapter_t*)adapter_context;

	status = adapter_reset( p_adapter );
	VNIC_EXIT( VNIC_DBG_INIT );
	switch( status )
	{
		case IB_SUCCESS:
			*p_addr_reset = TRUE;
			return NDIS_STATUS_SUCCESS;
		
		case IB_NOT_DONE:
			return NDIS_STATUS_PENDING;

		case IB_INVALID_STATE:
			return NDIS_STATUS_RESET_IN_PROGRESS;

		default:
			return NDIS_STATUS_HARD_ERRORS;
	}
}

//! Request changes in the state information that the miniport driver maintains
/*  For example, this is used to set multicast addresses and the packet filter.
IRQL = DISPATCH_LEVEL

@param adapter_context The adapter context allocated at start
@param oid Object ID representing the set operation to be carried out
@param info_buf Buffer containing input for this set and location for any output
@param info_buf_len Number of bytes available in info_buf
@param p_bytes_read Pointer to number of bytes read from info_buf
@param p_bytes_needed Pointer to number of bytes needed to satisfy this oid
@return NDIS_STATUS_SUCCESS, NDIS_STATUS_PENDING, NDIS_STATUS_INVALID_OID,
NDIS_STATUS_INVALID_LENGTH, NDIS_STATUS_INVALID_DATA, NDIS_STATUS_NOT_ACCEPTED,
NDIS_STATUS_NOT_SUPPORTED, NDIS_STATUS_RESOURCES
*/
NDIS_STATUS
vnic_oid_set_info(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_OID					oid,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len,
		OUT			PULONG						p_bytes_read,
		OUT			PULONG						p_bytes_needed )
{
	vnic_adapter_t*		p_adapter;
	Netpath_t*			p_netpath;			
	NDIS_STATUS			status;
	ULONG				buf_len;
	pending_oid_t		oid_info;

	VNIC_ENTER( VNIC_DBG_OID );

	CL_ASSERT( adapter_context );
	CL_ASSERT( p_bytes_read );
	CL_ASSERT( p_bytes_needed );

	p_adapter = (vnic_adapter_t*)adapter_context;
	CL_ASSERT( !p_adapter->pending_set );

	p_netpath = p_adapter->p_currentPath;
	/* do not set anything until IB path initialized and NIC is enabled */
	if( !netpath_is_valid( p_netpath ) || !p_netpath->carrier )
	{
		*p_bytes_read = 0;
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	status = NDIS_STATUS_SUCCESS;
	*p_bytes_needed = 0;
	buf_len = sizeof(ULONG);

	oid_info.oid = oid;
	oid_info.p_buf = info_buf;
	oid_info.buf_len = info_buf_len;
	oid_info.p_bytes_used = p_bytes_read;
	oid_info.p_bytes_needed = p_bytes_needed;

	switch( oid )
	{
	/* Required General */
	case OID_GEN_CURRENT_PACKET_FILTER:
		VNIC_TRACE( VNIC_DBG_OID,
			("  IOC %d received set for OID_GEN_CURRENT_PACKET_FILTER, %#x\n",
				p_netpath->pViport->ioc_num, *(uint32_t*)info_buf ) );

		if( info_buf_len < sizeof( p_adapter->packet_filter ) )
		{
			status = NDIS_STATUS_INVALID_LENGTH;
		}
		else if( !info_buf )
		{
			status = NDIS_STATUS_INVALID_DATA;
		}
		else
		{
			p_adapter->set_oid = oid_info;
			status = _vnic_process_packet_filter( p_adapter, *((ULONG*)info_buf) );
		}
		break;

	case OID_GEN_CURRENT_LOOKAHEAD:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received set for OID_GEN_CURRENT_LOOKAHEAD\n" ));
		if( info_buf_len < buf_len )
			status = NDIS_STATUS_INVALID_LENGTH;
		break;

	case OID_GEN_PROTOCOL_OPTIONS:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received set for OID_GEN_PROTOCOL_OPTIONS\n" ));
		if( info_buf_len < buf_len )
			status = NDIS_STATUS_INVALID_LENGTH;
		break;

#ifdef NDIS51_MINIPORT
	case OID_GEN_MACHINE_NAME:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received set for OID_GEN_MACHINE_NAME\n" ) );
		if( info_buf_len < buf_len )
			status = NDIS_STATUS_INVALID_LENGTH;
	//	else
	//		status = __vnic_set_machine_name( info_buf,
	//										 (USHORT)info_buf_len );
		break;
#endif

	/* Required Ethernet operational characteristics */
	case OID_802_3_MULTICAST_LIST:
		if( !p_adapter->p_currentPath->carrier )
		{
			status = NDIS_STATUS_NOT_ACCEPTED;
			break;
		}
		VNIC_TRACE( VNIC_DBG_OID,
			("  IOC %d received set for OID_802_3_MULTICAST_LIST\n",
				p_adapter->p_currentPath->pViport->ioc_num ) );
		if( info_buf_len > MAX_MCAST * sizeof(mac_addr_t) )
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("  OID_802_3_MULTICAST_LIST - Multicast list full.\n" ) );
			status = NDIS_STATUS_MULTICAST_FULL;
			*p_bytes_needed = MAX_MCAST * sizeof(mac_addr_t);
		}
		else if( info_buf_len % sizeof(mac_addr_t) )
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("  OID_802_3_MULTICAST_LIST - Invalid input buffer length.\n" ) );
			status = NDIS_STATUS_INVALID_DATA;
		}
		else if( info_buf == NULL && info_buf_len != 0 )
		{
			VNIC_TRACE( VNIC_DBG_OID,
				("  OID_802_3_MULTICAST_LIST - Invalid input buffer.\n" ) );
			status = NDIS_STATUS_INVALID_DATA;
		}
		else
		{
			p_adapter->set_oid = oid_info;
			status = adapter_set_mcast( p_adapter, (mac_addr_t*)info_buf,
						(uint8_t)(info_buf_len / sizeof(mac_addr_t)) );
		}
		break;

	case OID_TCP_TASK_OFFLOAD:
		VNIC_TRACE( VNIC_DBG_OID,
			("  received set for OID_TCP_TASK_OFFLOAD\n" ) );
		buf_len = info_buf_len;
		status = __vnic_set_tcp_task_offload( p_adapter, info_buf, &buf_len );
		break;

	/* Optional General */
	case OID_GEN_TRANSPORT_HEADER_OFFSET:
			VNIC_TRACE( VNIC_DBG_OID,
				("Set for OID_GEN_TRANSPORT_HEADER_OFFSET\n") );
		break;
	case OID_GEN_VLAN_ID:
		if( info_buf_len != 4 )
		{
			status = NDIS_STATUS_INVALID_LENGTH;
			break;
		}
		if( *(( uint32_t *)info_buf) < 4095 )
		{
			p_adapter->vlan_info = *((uint32_t*)info_buf );
		}
		else
		{
			status = NDIS_STATUS_INVALID_DATA;
		}
		break;

	case OID_GEN_SUPPORTED_LIST:
	case OID_GEN_HARDWARE_STATUS:
	case OID_GEN_MEDIA_SUPPORTED:
	case OID_GEN_MEDIA_IN_USE:
	case OID_GEN_MAXIMUM_FRAME_SIZE:
	case OID_GEN_LINK_SPEED:
	case OID_GEN_TRANSMIT_BUFFER_SPACE:
	case OID_GEN_RECEIVE_BUFFER_SPACE:
	case OID_GEN_MAXIMUM_LOOKAHEAD:
	case OID_GEN_TRANSMIT_BLOCK_SIZE:
	case OID_GEN_RECEIVE_BLOCK_SIZE:
	case OID_GEN_MAXIMUM_TOTAL_SIZE:
	case OID_GEN_VENDOR_ID:
	case OID_GEN_VENDOR_DESCRIPTION:
	case OID_GEN_VENDOR_DRIVER_VERSION:
	case OID_GEN_DRIVER_VERSION:
	case OID_GEN_MAC_OPTIONS:
	case OID_GEN_MEDIA_CONNECT_STATUS:
	case OID_GEN_MAXIMUM_SEND_PACKETS:
	case OID_GEN_SUPPORTED_GUIDS:
	case OID_GEN_PHYSICAL_MEDIUM:
	default:
		status = NDIS_STATUS_INVALID_OID;
		VNIC_TRACE( VNIC_DBG_OID,
			("  received an invalid oid of 0x%.8X!\n" , oid));
		break;
	}

	if( status == NDIS_STATUS_SUCCESS )
	{
		*p_bytes_read = buf_len;
	}
	else
	{
		if( status == NDIS_STATUS_INVALID_LENGTH )
		{
			if ( !*p_bytes_needed )
			{
				*p_bytes_needed = buf_len;
			}
		}

		*p_bytes_read = 0;
	}

	VNIC_EXIT( VNIC_DBG_OID );
	return status;
}


//! Transfers some number of packets, specified as an array of packet pointers, over the network.
/*  For a deserialized driver, these packets are completed asynchronously
using NdisMSendComplete.
IRQL <= DISPATCH_LEVEL

@param adapter_context Pointer to vnic_adapter_t structure with per NIC state
@param packet_array Array of packets to send
@param numPackets Number of packets in the array
*/
void
vnic_send_packets(
	IN				NDIS_HANDLE					adapter_context,
	IN				PPNDIS_PACKET				packet_array,
	IN				UINT						num_packets )
{
	vnic_adapter_t*	 const	p_adapter =(vnic_adapter_t* const )adapter_context;
	UINT				packet_num;

	VNIC_ENTER( VNIC_DBG_SEND );

	CL_ASSERT( adapter_context );

	for( packet_num = 0; packet_num < num_packets; ++packet_num )
	{
		netpath_xmitPacket( p_adapter->p_currentPath,
											packet_array[packet_num] );
	}
	VNIC_EXIT( VNIC_DBG_SEND );
}

void vnic_cancel_xmit(
	IN				NDIS_HANDLE					adapter_context,
    IN				PVOID						cancel_id )
{
	vnic_adapter_t*	 const	p_adapter =(vnic_adapter_t* const )adapter_context;

	if( p_adapter && 
		p_adapter->p_currentPath )
	{
		netpath_cancel_xmit( p_adapter->p_currentPath, cancel_id );
	}
}

void
vnic_pnp_notify(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_DEVICE_PNP_EVENT		pnp_event,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len )
{
	vnic_adapter_t	*p_adapter;	

	VNIC_ENTER( VNIC_DBG_PNP );

	UNUSED_PARAM( info_buf );
	UNUSED_PARAM( info_buf_len );

	p_adapter = (vnic_adapter_t*)adapter_context;

	CL_ASSERT( p_adapter );

	VNIC_TRACE( VNIC_DBG_PNP, ("Event %d for IOC[%d] in state %#x\n", 
		pnp_event, p_adapter->ioc_num, p_adapter->pnp_state ) );

	switch ( pnp_event )
	{
	case NdisDevicePnPEventPowerProfileChanged: 
		break;
	case NdisDevicePnPEventQueryRemoved:
	case NdisDevicePnPEventRemoved:
	case NdisDevicePnPEventSurpriseRemoved:
	case NdisDevicePnPEventQueryStopped:
	case NdisDevicePnPEventStopped:
	case NdisDevicePnPEventMaximum:

		if( InterlockedExchange( (volatile LONG*)&p_adapter->pnp_state, IB_PNP_IOC_REMOVE ) == IB_PNP_IOC_ADD )
		{
			if( netpath_is_valid( p_adapter->p_currentPath ) )
			{
				netpath_stopXmit( p_adapter->p_currentPath );
				InterlockedExchange( &p_adapter->p_currentPath->carrier, (LONG)FALSE );
				netpath_linkDown( p_adapter->p_currentPath );
				__pending_queue_cleanup( p_adapter );
			}
		}
		vnic_resume_oids( p_adapter );
		break;
	}

	VNIC_EXIT( VNIC_DBG_PNP );
}


void
vnic_shutdown(
	IN		PVOID		adapter_context )
{
	vnic_adapter_t	*p_adapter;
	Netpath_t	*p_netpath;

	VNIC_ENTER( VNIC_DBG_INIT );
	p_adapter = (vnic_adapter_t *)adapter_context;

	if( p_adapter )
	{
		g_vnic.shutdown = p_adapter->ioc_num;
		VNIC_TRACE( VNIC_DBG_INIT, 
			("IOC[%d] going to Shutdown\n", p_adapter->ioc_num ));
		p_netpath = p_adapter->p_currentPath;

		if( p_netpath && p_netpath->pViport )
		{
			netpath_stopXmit( p_netpath );
			InterlockedExchange( &p_netpath->carrier, (LONG)FALSE );
			netpath_linkDown( p_netpath );
		}

		vnic_destroy_adapter( p_adapter );
	}

	VNIC_EXIT( VNIC_DBG_INIT );
}


void
vnic_resume_set_oids(
	IN				vnic_adapter_t* const		p_adapter )
{
	NDIS_STATUS		status = NDIS_STATUS_SUCCESS;
	BOOLEAN					pending_set;
	pending_oid_t	set_oid = {0};

	Netpath_t*	p_netpath;
	VNIC_ENTER( VNIC_DBG_OID );

	if( !p_adapter->pending_set )
	{
		VNIC_EXIT( VNIC_DBG_OID );
		return;
	}
	NdisAcquireSpinLock( &p_adapter->lock );
	p_netpath = p_adapter->p_currentPath;
		VNIC_TRACE( VNIC_DBG_OID,
					("IOC[%d]Resume Set OID %#x\n",
						p_adapter->ioc_num, p_adapter->set_oid.oid ));
	/*
	 * Set the status depending on our state.  Fail OID requests that
	 * are pending while we reset the adapter.
	 */
	if( p_adapter->pnp_state != IB_PNP_IOC_REMOVE && 
		netpath_is_connected( p_netpath ) )
	{
		status = NDIS_STATUS_SUCCESS;
	}
	else
	{
		status = NDIS_STATUS_NOT_ACCEPTED;
		if( p_adapter->pending_set )
		{
			VNIC_TRACE( VNIC_DBG_PNP,
				("IOC[%d]Pending set OID %#x while removing \n",
				p_adapter->ioc_num, p_adapter->set_oid.oid ));
		}
	}

	pending_set = p_adapter->pending_set;
	if( pending_set )
	{
		set_oid = p_adapter->set_oid;
		p_adapter->pending_set = FALSE;
	}
	NdisReleaseSpinLock( &p_adapter->lock );

	ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	if( pending_set && status == NDIS_STATUS_SUCCESS )
	{
		switch( set_oid.oid )
		{
		case OID_GEN_CURRENT_PACKET_FILTER:
				VNIC_TRACE( VNIC_DBG_OID,
					("  IOC %d resume PACKET_FILTER set \n",
						p_netpath->pViport->ioc_num ) );
			/* Validation already performed in the SetInformation path. */
			p_adapter->packet_filter = *(PULONG)set_oid.p_buf;
			break;

		case OID_GEN_MACHINE_NAME:
			status = __vnic_set_machine_name ( p_adapter->set_oid.p_buf, (USHORT)p_adapter->set_oid.buf_len );
			break;

		case OID_802_3_MULTICAST_LIST:
					VNIC_TRACE( VNIC_DBG_OID,
						("  IOC %d resume MULTICAST_LIST\n",
						p_netpath->pViport->ioc_num ) );
			break;

		default:
			CL_ASSERT( set_oid.oid && 0 );
			break;
		}
	}

	if( !netpath_is_connected( p_netpath ) )
	{
		NdisMSetInformationComplete( p_adapter->h_handle, 
			NDIS_STATUS_NOT_ACCEPTED );
	}
	else
		NdisMSetInformationComplete( p_adapter->h_handle, status );

	VNIC_EXIT( VNIC_DBG_OID );
}


void
vnic_resume_oids(
	IN				vnic_adapter_t* const		p_adapter )
{
	ULONG			info;
	NDIS_STATUS		status = NDIS_STATUS_SUCCESS;
	BOOLEAN			pending_query;
	pending_oid_t	query_oid = {0};
	KIRQL			irql;

	Netpath_t*		p_netpath = p_adapter->p_currentPath;

	uint8_t		mac[HW_ADDR_LEN];

	VNIC_ENTER( VNIC_DBG_OID );

	NdisAcquireSpinLock( &p_adapter->lock );
	/*
	 * Set the status depending on our state.  Fail OID requests that
	 * are pending while we reset the adapter.
	 */
	if( !netpath_is_connected( p_netpath ) )
	{
		status = NDIS_STATUS_NOT_ACCEPTED;
	}

	switch( p_adapter->pnp_state )
	{
	case IB_PNP_IOC_ADD:
		break;

	case IB_PNP_IOC_REMOVE:
	default:
		status = NDIS_STATUS_NOT_ACCEPTED;
		break;
	}

	pending_query = p_adapter->pending_query;

	if( pending_query )
	{
		query_oid = p_adapter->query_oid;
		p_adapter->pending_query = FALSE;
	}
	NdisReleaseSpinLock( &p_adapter->lock );

	KeRaiseIrql( DISPATCH_LEVEL, &irql );

	/*
	 * If we had a pending OID request for OID_GEN_LINK_SPEED,
	 * complete it now.  Note that we hold the object lock since
	 * NdisMQueryInformationComplete is called at DISPATCH_LEVEL.
	 */
	if( pending_query )
	{
		switch( query_oid.oid )
		{
		case OID_802_3_CURRENT_ADDRESS:
		case OID_802_3_PERMANENT_ADDRESS:
			if ( status == NDIS_STATUS_SUCCESS )
			{
				cl_memcpy( mac, p_netpath->pViport->hwMacAddress, HW_ADDR_LEN );
			}
			_vnic_complete_query( p_adapter,
							&query_oid,
							status,
							mac,
							HW_ADDR_LEN );
			break;
		case OID_GEN_LINK_SPEED:
			info	= DEFAULT_LINK_SPEED_x100BPS;
			_vnic_complete_query( p_adapter,
							&query_oid,
							status,
							&info,
							sizeof( info ) );
			break;

		case OID_GEN_MEDIA_CONNECT_STATUS:
			info = ( p_adapter->carrier )? NdisMediaStateConnected :
										 NdisMediaStateDisconnected;
			_vnic_complete_query( p_adapter,
							&query_oid,
							status,
							&info,
							sizeof( info ) );
			break;
		case OID_802_3_MULTICAST_LIST:
				_vnic_complete_query( p_adapter,
							&query_oid,
							status,
							p_netpath->p_adapter->mcast_array,
							p_adapter->mc_count * sizeof( mac_addr_t ) );
			 break;
		case OID_GEN_TRANSMIT_BUFFER_SPACE:
			if( status == NDIS_STATUS_SUCCESS )
			{
				info = p_netpath->pViport->data.xmitPool.bufferSz *
						p_netpath->pViport->data.xmitPool.numXmitBufs;
			}
			else
			{
				info = 0;
			}
				_vnic_complete_query( p_adapter,
							&query_oid,
							status,
							&info,
							sizeof( info ) );
			break;
		case OID_GEN_RECEIVE_BUFFER_SPACE:
			if( status == NDIS_STATUS_SUCCESS )
			{
				info = p_netpath->pViport->data.recvPool.bufferSz *
						p_netpath->pViport->data.recvPool.poolSz;
			}
			else
			{
				info = 0;
			}
				_vnic_complete_query( p_adapter,
							&query_oid,
							status,
							&info,
							sizeof( info ) );
			break;
		default:
				 CL_ASSERT( query_oid.oid == OID_GEN_LINK_SPEED   ||
					query_oid.oid == OID_GEN_MEDIA_CONNECT_STATUS ||
					query_oid.oid == OID_802_3_MULTICAST_LIST     ||
					query_oid.oid == OID_802_3_CURRENT_ADDRESS    ||
					query_oid.oid == OID_802_3_PERMANENT_ADDRESS  ||
					query_oid.oid == OID_GEN_RECEIVE_BUFFER_SPACE ||
					query_oid.oid == OID_GEN_TRANSMIT_BUFFER_SPACE );
					break;
		}
	}

	vnic_resume_set_oids( p_adapter );

	KeLowerIrql( irql );

	VNIC_EXIT( VNIC_DBG_OID );
}

static NDIS_STATUS
__vnic_set_machine_name(
		IN		VOID	*p_uni_array,
		IN		USHORT	buf_size )
{
	NDIS_STATUS		status = NDIS_STATUS_SUCCESS;
	uint8_t	*p_src_buf = (uint8_t *)p_uni_array;
	uint32_t i;

	VNIC_ENTER( VNIC_DBG_OID );

	if( buf_size > 0 )
	{
		cl_memclr(g_vnic.host_name, sizeof( g_vnic.host_name ) );
		for( i = 0; i < min( ( buf_size > 1 ), sizeof( g_vnic.host_name ) ); i++ )
		{
			g_vnic.host_name[i] = *(p_src_buf + i*2);
		}
	}

	return status;
}

static void
__vnic_read_machine_name( void )
{
	/* this code is borrowed from the bus_driver */

	NTSTATUS					nt_status;
	/* Remember the terminating entry in the table below. */
	RTL_QUERY_REGISTRY_TABLE	table[2];
	UNICODE_STRING				hostNamePath;
	UNICODE_STRING				hostNameW;
	ANSI_STRING					hostName;

	VNIC_ENTER( VNIC_DBG_INIT );

	/* Get the host name. */
	RtlInitUnicodeString( &hostNamePath, L"ComputerName\\ComputerName" );
	RtlInitUnicodeString( &hostNameW, NULL );

	/*
	 * Clear the table.  This clears all the query callback pointers,
	 * and sets up the terminating table entry.
	 */
	cl_memclr( table, sizeof(table) );

	/* Setup the table entries. */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED;
	table[0].Name = L"ComputerName";
	table[0].EntryContext = &hostNameW;
	table[0].DefaultType = REG_SZ;
	table[0].DefaultData = &hostNameW;
	table[0].DefaultLength = 0;

	/* Have at it! */
	nt_status = RtlQueryRegistryValues( RTL_REGISTRY_CONTROL,
		hostNamePath.Buffer, table, NULL, NULL );
	if( NT_SUCCESS( nt_status ) )
	{
		/* Convert the UNICODE host name to UTF-8 (ASCII). */
		hostName.Length = 0;
		hostName.MaximumLength = sizeof(g_vnic.host_name) - 1;
		hostName.Buffer = (PCHAR)g_vnic.host_name;
		nt_status = RtlUnicodeStringToAnsiString( &hostName, &hostNameW, FALSE );
		RtlFreeUnicodeString( &hostNameW );
	}
	else
	{
		VNIC_TRACE(VNIC_DBG_ERROR , ("Failed to get host name from registry\n") );
		/* Use the default name... */
		cl_memcpy( g_vnic.host_name,
					DEFAULT_HOST_NAME,
					min (sizeof( g_vnic.host_name), sizeof(DEFAULT_HOST_NAME) ) );
	}

	VNIC_EXIT( VNIC_DBG_INIT );
}

/* function: usec_timer_start
		uses cl_timer* functionality (init/destroy/stop/start )
		except it takes expiration time parameter in microseconds.
*/
cl_status_t
usec_timer_start(
	IN	cl_timer_t* const	p_timer,
	IN	const uint32_t		time_usec )
{
	LARGE_INTEGER	due_time;

	CL_ASSERT( p_timer );
	CL_ASSERT( p_timer->pfn_callback );
	CL_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

	/* Due time is in 100 ns increments.  Negative for relative time. */
	due_time.QuadPart = -(int64_t)(((uint64_t)time_usec) * 10);

	/* Store the timeout time in the timer object. in microseconds */
	p_timer->timeout_time = cl_get_time_stamp() + (((uint64_t)time_usec));

	KeSetTimer( &p_timer->timer, due_time, &p_timer->dpc );
	return( CL_SUCCESS );
}


static NDIS_STATUS
_vnic_process_packet_filter(
		IN	vnic_adapter_t* const	p_adapter,
		IN	ULONG					pkt_filter )
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	viport_t	*p_viport;
	ULONG		need_updates = 0;

	VNIC_ENTER(VNIC_DBG_FUNC );

	ASSERT( p_adapter );

	p_viport = p_adapter->p_currentPath->pViport;

	ASSERT( p_viport );
	ASSERT( !( p_viport->updates & SYNC_QUERY ) );

	if( ( InterlockedExchange( 
		(volatile LONG*)&p_adapter->packet_filter, pkt_filter )) == (LONG)pkt_filter )
	{
		InterlockedExchange( &p_viport->updates, 0 );
		return status;
	}

	ASSERT( (p_viport->updates & ~MCAST_OVERFLOW) == 0 );

	if( pkt_filter )
	{
		if( !( p_viport->flags & INIC_FLAG_ENABLE_NIC ) )
		{
			p_viport->newFlags &= ~INIC_FLAG_DISABLE_NIC;
			p_viport->newFlags |= INIC_FLAG_ENABLE_NIC | INIC_FLAG_SET_MTU;
			p_viport->newMtu = (uint16_t)p_adapter->params.MinMtu;
			InterlockedOr( (volatile LONG*)&need_updates, NEED_LINK_CONFIG );
		}

		if( pkt_filter & NDIS_PACKET_TYPE_ALL_MULTICAST )
		{
			if( !( p_viport->flags & INIC_FLAG_ENABLE_MCAST_ALL ) )
			{
				p_viport->newFlags &= ~INIC_FLAG_DISABLE_MCAST_ALL;
				p_viport->newFlags |= INIC_FLAG_ENABLE_MCAST_ALL;
				InterlockedOr( (volatile LONG*)&need_updates,
								NEED_LINK_CONFIG | MCAST_OVERFLOW );
			}
		}
		else
		{
			if( p_viport->flags & NDIS_PACKET_TYPE_ALL_MULTICAST )
			{
				p_viport->newFlags &= ~INIC_FLAG_ENABLE_MCAST_ALL;
				p_viport->newFlags |= INIC_FLAG_DISABLE_MCAST_ALL;
				InterlockedOr( (volatile LONG*)&need_updates, NEED_LINK_CONFIG );
			}
		}

		if ( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS )
		{
			if( !( p_viport->flags & INIC_FLAG_ENABLE_PROMISC ) )
			{
				p_viport->newFlags &= ~INIC_FLAG_DISABLE_PROMISC;
				p_viport->newFlags |= INIC_FLAG_ENABLE_PROMISC;
				InterlockedOr( (volatile LONG*)&need_updates, NEED_LINK_CONFIG );
			}
		}
		else
		{
			if( p_viport->flags & INIC_FLAG_ENABLE_PROMISC )
			{
				p_viport->newFlags &= ~INIC_FLAG_ENABLE_PROMISC;
				p_viport->newFlags |= INIC_FLAG_DISABLE_PROMISC;
				InterlockedOr( (volatile LONG*)&need_updates, NEED_LINK_CONFIG );
			}
		}
	}

	/* ENABLE NIC, BROADCAST and MULTICAST flags set on start */
	if( need_updates )
	{
		p_adapter->pending_set = TRUE;
		InterlockedOr( &p_viport->updates, need_updates );

		status = _viport_process_query( p_viport, FALSE );
		VNIC_TRACE( VNIC_DBG_OID,
						("LINK CONFIG status %x\n", status ));
		if( status != NDIS_STATUS_PENDING )
		{
			p_adapter->pending_set = FALSE;
		}
	}

	VNIC_EXIT( VNIC_DBG_FUNC );
	return status;
}

static NTSTATUS
__vnic_read_service_registry(
	IN			UNICODE_STRING* const		p_registry_path )
{
	NTSTATUS	status;
	RTL_QUERY_REGISTRY_TABLE		table[VNIC_REGISTRY_TBL_SIZE + 1];
	UNICODE_STRING					param_path;
	ULONG						_reg_dbg_lvl = 0;
	int							i;

	RtlInitUnicodeString( &param_path, NULL );
	param_path.MaximumLength = 
		p_registry_path->Length + sizeof(L"\\Parameters");
	
	param_path.Buffer = cl_zalloc( param_path.MaximumLength );
	if( !param_path.Buffer )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("Failed to allocate parameters path buffer.\n") );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlAppendUnicodeStringToString( &param_path, p_registry_path );
	RtlAppendUnicodeToString( &param_path, L"\\Parameters" );

	cl_memclr( table, sizeof(table) );
	
	/* init debug flags entry */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[0].Name = L"DebugFlags";
	table[0].EntryContext = &_reg_dbg_lvl;
	table[0].DefaultType = REG_DWORD;
	table[0].DefaultData = &_reg_dbg_lvl;
	table[0].DefaultLength = sizeof(ULONG);

	/* Setup the rest of table entries. */
	for ( i = 1; i < VNIC_REGISTRY_TBL_SIZE; i++ )
	{
		table[i].Flags = RTL_QUERY_REGISTRY_DIRECT;
		table[i].Name = reg_array[i].name;
		table[i].EntryContext = &reg_array[i].value;
		table[i].DefaultType = REG_DWORD;
		table[i].DefaultData = &reg_array[i].value;
		table[i].DefaultLength = sizeof(ULONG);
	}

	status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
		param_path.Buffer, table, NULL, NULL );

	if( status == STATUS_SUCCESS )
	{
		g_vnic_dbg_lvl |= _reg_dbg_lvl;
		VNIC_TRACE(VNIC_DBG_INFO, ("DebugFlags Set %#x\n",g_vnic_dbg_lvl ));
		g_vnic.p_params = reg_array;
	}

	cl_free( param_path.Buffer );

	return status;
}
