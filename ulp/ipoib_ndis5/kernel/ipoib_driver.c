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

#include "limits.h"
#include "ipoib_driver.h"
#include "ipoib_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_driver.tmh"
#endif

#include "ipoib_port.h"
#include "ipoib_ibat.h"
#include <complib/cl_bus_ifc.h>
#include <complib/cl_init.h>
#include <initguid.h>
#include <iba/ipoib_ifc.h>
#include "ntstrsafe.h"
#include "strsafe.h"
#include <offload.h>





#if defined(NDIS50_MINIPORT)
#define MAJOR_NDIS_VERSION 5
#define MINOR_NDIS_VERSION 0
#elif defined (NDIS51_MINIPORT)
#define MAJOR_NDIS_VERSION 5
#define MINOR_NDIS_VERSION 1
#else
#error NDIS Version not defined, try defining NDIS50_MINIPORT or NDIS51_MINIPORT
#endif

PDRIVER_OBJECT				g_p_drv_obj;

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
	OID_GEN_PROTOCOL_OPTIONS,
	OID_GEN_MAC_OPTIONS,
	OID_GEN_MEDIA_CONNECT_STATUS,
	OID_GEN_MAXIMUM_SEND_PACKETS,
	OID_GEN_NETWORK_LAYER_ADDRESSES,
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

#define VENDOR_DESCRIPTION "Internet Protocol over InfiniBand"

#define IB_INFINITE_SERVICE_LEASE	0xFFFFFFFF

//The mask is 8 bit and can't contain more than 6 non-zero bits
#define MAX_GUID_MAX 0xFC


/* Global driver debug level */
uint32_t		g_ipoib_dbg_level = TRACE_LEVEL_ERROR;
uint32_t		g_ipoib_dbg_flags = 0x00000fff;
ipoib_globals_t	g_ipoib = {0};

typedef struct _IPOIB_REG_ENTRY
{
	NDIS_STRING RegName;                // variable name text
	BOOLEAN     bRequired;              // 1 -> required, 0 -> optional
	UINT        FieldOffset;            // offset in parent struct
	UINT        FieldSize;              // size (in bytes) of the field
	UINT        Default;                // default value to use
	UINT        Min;                    // minimum value allowed
	UINT        Max;                    // maximum value allowed
} IPOIB_REG_ENTRY, *PIPOIB_REG_ENTRY;

IPOIB_REG_ENTRY HCARegTable[] = {
	// reg value name             If Required  Offset in parentr struct             Field size                  Default         Min     Max
	{NDIS_STRING_CONST("GUIDMask"),         0, IPOIB_OFFSET(guid_mask),             IPOIB_SIZE(guid_mask),          0,          0,    MAX_GUID_MAX},
	/* GUIDMask should be the first element */
	{NDIS_STRING_CONST("RqDepth"),          1, IPOIB_OFFSET(rq_depth),              IPOIB_SIZE(rq_depth),           512,        128,    1024},
	{NDIS_STRING_CONST("RqLowWatermark"),   0, IPOIB_OFFSET(rq_low_watermark),      IPOIB_SIZE(rq_low_watermark),   4,          2,      8},
	{NDIS_STRING_CONST("SqDepth"),          1, IPOIB_OFFSET(sq_depth),              IPOIB_SIZE(sq_depth),           512,        128,    1024},
	{NDIS_STRING_CONST("SendChksum"),       1, IPOIB_OFFSET(send_chksum_offload),   IPOIB_SIZE(send_chksum_offload),CSUM_ENABLED,CSUM_DISABLED,CSUM_BYPASS},
	{NDIS_STRING_CONST("RecvChksum"),       1, IPOIB_OFFSET(recv_chksum_offload),   IPOIB_SIZE(recv_chksum_offload),CSUM_ENABLED,CSUM_DISABLED,CSUM_BYPASS},
	{NDIS_STRING_CONST("SaTimeout"),        1, IPOIB_OFFSET(sa_timeout),            IPOIB_SIZE(sa_timeout),         1000,       250,    UINT_MAX},
	{NDIS_STRING_CONST("SaRetries"),        1, IPOIB_OFFSET(sa_retry_cnt),          IPOIB_SIZE(sa_retry_cnt),       10,         1,      UINT_MAX},
	{NDIS_STRING_CONST("RecvRatio"),        1, IPOIB_OFFSET(recv_pool_ratio),       IPOIB_SIZE(recv_pool_ratio),    1,          1,      10},
	{NDIS_STRING_CONST("PayloadMtu"),       1, IPOIB_OFFSET(payload_mtu),           IPOIB_SIZE(payload_mtu),        2044,       512,    4092},
	{NDIS_STRING_CONST("lso"),              0, IPOIB_OFFSET(lso),                   IPOIB_SIZE(lso),                0,          0,      1},
	{NDIS_STRING_CONST("MCLeaveRescan"),    1, IPOIB_OFFSET(mc_leave_rescan),       IPOIB_SIZE(mc_leave_rescan),    260,        1,    3600},
	{NDIS_STRING_CONST("BCJoinRetry"),	    1, IPOIB_OFFSET(bc_join_retry),		    IPOIB_SIZE(bc_join_retry),      50,         0,    1000}
	
};  

#define IPOIB_NUM_REG_PARAMS (sizeof (HCARegTable) / sizeof(IPOIB_REG_ENTRY))


void
ipoib_create_log(
	NDIS_HANDLE h_adapter,
	UINT ind,
	ULONG eventLogMsgId)

{
#define cMaxStrLen  40
#define cArrLen  3

	PWCHAR logMsgArray[cArrLen]; 
	WCHAR strVal[cMaxStrLen];
	NDIS_STRING AdapterInstanceName;

	IPOIB_INIT_NDIS_STRING(&AdapterInstanceName);
	if (NdisMQueryAdapterInstanceName(&AdapterInstanceName, h_adapter)!= NDIS_STATUS_SUCCESS ){
		ASSERT(FALSE);
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR, ("[IPoIB] Init:Failed to retreive adapter name.\n"));
		return;
	}
	logMsgArray[0] = AdapterInstanceName.Buffer;
	
	if (RtlStringCbPrintfW(strVal, sizeof(strVal), L"0x%x", HCARegTable[ind].Default) != STATUS_SUCCESS) {
		ASSERT(FALSE);
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR,
		("[IPoIB] Init: Problem copying string value: exiting\n"));   
		return;
	}
	
	logMsgArray[0] = AdapterInstanceName.Buffer;
	logMsgArray[1] = HCARegTable[ind].RegName.Buffer;
	logMsgArray[2] = strVal;
	
	NdisWriteEventLogEntry(g_p_drv_obj, eventLogMsgId, 0, cArrLen, &logMsgArray, 0, NULL);

}



NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT				p_drv_obj,
	IN				PUNICODE_STRING				p_reg_path );

VOID
ipoib_unload(
	IN				PDRIVER_OBJECT				p_drv_obj );

NDIS_STATUS
ipoib_initialize(
		OUT			PNDIS_STATUS				p_open_err_status,
		OUT			PUINT						p_selected_medium_index,
	IN				PNDIS_MEDIUM				medium_array,
	IN				UINT						medium_array_size,
	IN				NDIS_HANDLE					h_adapter,
	IN				NDIS_HANDLE					wrapper_configuration_context );

BOOLEAN
ipoib_check_for_hang(
	IN				NDIS_HANDLE					adapter_context );

void
ipoib_halt(
	IN				NDIS_HANDLE					adapter_context );

NDIS_STATUS
ipoib_query_info(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_OID					oid,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len,
		OUT			PULONG						p_bytes_written,
		OUT			PULONG						p_bytes_needed );

NDIS_STATUS
ipoib_reset(
		OUT			PBOOLEAN					p_addressing_reset,
	IN				NDIS_HANDLE					adapter_context );

NDIS_STATUS
ipoib_set_info(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_OID					oid,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_length,
		OUT			PULONG						p_bytes_read,
		OUT			PULONG						p_bytes_needed );

void
ipoib_send_packets(
	IN				NDIS_HANDLE					adapter_context,
	IN				PPNDIS_PACKET				packet_array,
	IN				UINT						num_packets );

void
ipoib_pnp_notify(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_DEVICE_PNP_EVENT		pnp_event,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len );

void
ipoib_shutdown(
	IN				PVOID						adapter_context );

static void
ipoib_complete_query(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				pending_oid_t* const		p_oid_info,
	IN		const	NDIS_STATUS					status,
	IN		const	void* const					p_buf,
	IN		const	ULONG						buf_len );

static NDIS_STATUS
__ipoib_set_net_addr(
	IN		ipoib_adapter_t *	p_adapter,
	IN		PVOID				info_buf,
	IN		ULONG				info_buf_len,
		OUT	PULONG				p_bytes_read,
		OUT	PULONG				p_bytes_needed );

static NDIS_STATUS
__ipoib_get_tcp_task_offload(
	IN				ipoib_adapter_t*			p_adapter,
	IN				pending_oid_t* const		p_oid_info );

static void
__ipoib_ats_reg_cb(
	IN				ib_reg_svc_rec_t			*p_reg_svc_rec );

static void
__ipoib_ats_dereg_cb(
	IN				void						*context );

static NTSTATUS
__ipoib_read_registry(
	IN				UNICODE_STRING* const		p_registry_path );


//! Standard Windows Device Driver Entry Point
/*! DriverEntry is the first routine called after a driver is loaded, and
is responsible for initializing the driver.  On W2k this occurs when the PnP
Manager matched a PnP ID to one in an INF file that references this driver.
Any not success return value will cause the driver to fail to load.
IRQL = PASSIVE_LEVEL

@param p_drv_obj Pointer to Driver Object for this device driver
@param p_registry_path Pointer to unicode string containing path to this driver's registry area
@return STATUS_SUCCESS, NDIS_STATUS_BAD_CHARACTERISTICS, NDIS_STATUS_BAD_VERSION,
NDIS_STATUS_RESOURCES, or NDIS_STATUS_FAILURE
*/
NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT				p_drv_obj,
	IN				PUNICODE_STRING				p_registry_path )
{
	NDIS_STATUS						status;
	NDIS_HANDLE						ndis_handle;
	NDIS_MINIPORT_CHARACTERISTICS	characteristics;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	g_p_drv_obj = p_drv_obj;

#ifdef _DEBUG_
	PAGED_CODE();
#endif
#if defined(EVENT_TRACING)
	WPP_INIT_TRACING(p_drv_obj, p_registry_path);
#endif
	status = CL_INIT;
	if( !NT_SUCCESS( status ) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_init failed.\n") );
		return status;
	}

	status		= NDIS_STATUS_SUCCESS;
	ndis_handle	= NULL;

	__ipoib_read_registry(p_registry_path);
	
	KeInitializeSpinLock( &g_ipoib.lock );
	cl_qlist_init( &g_ipoib.adapter_list );
	ipoib_st_init();
	g_stat.drv.obj = p_drv_obj;

	NdisMInitializeWrapper(
		&g_ipoib.h_ndis_wrapper, p_drv_obj, p_registry_path, NULL );

	memset(&characteristics, 0, sizeof(characteristics));
	characteristics.MajorNdisVersion		= MAJOR_NDIS_VERSION;
	characteristics.MinorNdisVersion		= MINOR_NDIS_VERSION;
	characteristics.CheckForHangHandler		= ipoib_check_for_hang;
	characteristics.HaltHandler				= ipoib_halt;
	characteristics.InitializeHandler		= ipoib_initialize;
	characteristics.QueryInformationHandler	= ipoib_query_info;
	characteristics.ResetHandler			= ipoib_reset;
	characteristics.SetInformationHandler	= ipoib_set_info;

	characteristics.ReturnPacketHandler		= ipoib_return_packet;
	characteristics.SendPacketsHandler		= ipoib_send_packets;

#ifdef NDIS51_MINIPORT
	characteristics.PnPEventNotifyHandler	= ipoib_pnp_notify;
	characteristics.AdapterShutdownHandler	= ipoib_shutdown;
#endif

	status = NdisMRegisterMiniport(
		g_ipoib.h_ndis_wrapper, &characteristics, sizeof(characteristics) );
	if( status != NDIS_STATUS_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
			("NdisMRegisterMiniport failed with status of %d\n", status) );
		NdisTerminateWrapper( g_ipoib.h_ndis_wrapper, NULL );
		CL_DEINIT;
		return status;
	}

	NdisMRegisterUnloadHandler( g_ipoib.h_ndis_wrapper, ipoib_unload );

	IPOIB_PRINT_EXIT(TRACE_LEVEL_WARNING, IPOIB_DBG_INIT, 
		("=====> DriverEntry exited\n"));
	return status;
}


static NTSTATUS
__ipoib_read_registry(
	IN				UNICODE_STRING* const		p_registry_path )
{
	NTSTATUS						status;
	/* Remember the terminating entry in the table below. */
	RTL_QUERY_REGISTRY_TABLE		table[4];
	UNICODE_STRING					param_path;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	RtlInitUnicodeString( &param_path, NULL );
	param_path.MaximumLength = p_registry_path->Length + 
		sizeof(L"\\Parameters");
	param_path.Buffer = cl_zalloc( param_path.MaximumLength );
	if( !param_path.Buffer )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
			("Failed to allocate parameters path buffer.\n") );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlAppendUnicodeStringToString( &param_path, p_registry_path );
	RtlAppendUnicodeToString( &param_path, L"\\Parameters" );

	/*
	 * Clear the table.  This clears all the query callback pointers,
	 * and sets up the terminating table entry.
	 */
	cl_memclr( table, sizeof(table) );

	/* Setup the table entries. */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[0].Name = L"DebugLevel";
	table[0].EntryContext = &g_ipoib_dbg_level;
	table[0].DefaultType = REG_DWORD;
	table[0].DefaultData = &g_ipoib_dbg_level;
	table[0].DefaultLength = sizeof(ULONG);

	table[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[1].Name = L"DebugFlags";
	table[1].EntryContext = &g_ipoib_dbg_flags;
	table[1].DefaultType = REG_DWORD;
	table[1].DefaultData = &g_ipoib_dbg_flags;
	table[1].DefaultLength = sizeof(ULONG);

	table[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[2].Name = L"bypass_check_bcast_rate";
	table[2].EntryContext = &g_ipoib.bypass_check_bcast_rate;
	table[2].DefaultType = REG_DWORD;
	table[2].DefaultData = &g_ipoib.bypass_check_bcast_rate;
	table[2].DefaultLength = sizeof(ULONG);

	/* Have at it! */
	status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
		param_path.Buffer, table, NULL, NULL );

	IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("debug level %d debug flags 0x%.8x\n",
			g_ipoib_dbg_level,
			g_ipoib_dbg_flags));

#if DBG
	if( g_ipoib_dbg_flags & IPOIB_DBG_ERR )
		g_ipoib_dbg_flags |= CL_DBG_ERROR;
#endif

	cl_free( param_path.Buffer );
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


VOID
ipoib_unload(
	IN				PDRIVER_OBJECT				p_drv_obj )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );
	#if defined(EVENT_TRACING)
	WPP_CLEANUP(p_drv_obj);
	#endif

	UNREFERENCED_PARAMETER( p_drv_obj );
	CL_DEINIT;
	IPOIB_EXIT( IPOIB_DBG_INIT );
}



NDIS_STATUS
ipoib_get_adapter_params(
	IN				NDIS_HANDLE* const			wrapper_config_context,
	IN	OUT			ipoib_adapter_t				*p_adapter,
	OUT				PUCHAR						*p_mac,
	OUT				UINT						*p_len)
{
	NDIS_STATUS						status;
	NDIS_HANDLE						h_config;
	NDIS_CONFIGURATION_PARAMETER	*p_param;
	UINT                            value;
	PIPOIB_REG_ENTRY                pRegEntry;
	UINT                            i;
	PUCHAR                          structPointer;
	int sq_depth_step = 128;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	NdisOpenConfiguration( &status, &h_config, wrapper_config_context );
	if( status != NDIS_STATUS_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisOpenConfiguration returned 0x%.8x\n", status) );
		return status;
	}

	// read all the registry values 
	for (i = 0, pRegEntry = HCARegTable; i < IPOIB_NUM_REG_PARAMS; ++i)
	{
		// initialize pointer to appropriate place inside 'params'
		structPointer = (PUCHAR) &p_adapter->params + pRegEntry[i].FieldOffset;

		// Get the configuration value for a specific parameter.  Under NT the
		// parameters are all read in as DWORDs.
		NdisReadConfiguration(
			&status,
			&p_param,
			h_config,
			&pRegEntry[i].RegName,
			NdisParameterInteger);

		// If the parameter was present, then check its value for validity.
		if (status == NDIS_STATUS_SUCCESS)
		{
			// Check that param value is not too small or too large
			if (p_param->ParameterData.IntegerData < pRegEntry[i].Min ||
				p_param->ParameterData.IntegerData > pRegEntry[i].Max)
			{
				value = pRegEntry[i].Default;
				ipoib_create_log(p_adapter->h_adapter, i, EVENT_IPOIB_WRONG_PARAMETER_WRN);
				IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_INIT, ("Read configuration.Registry %S value is out of range, setting default value= 0x%x\n", pRegEntry[i].RegName.Buffer, value));                                

			}
			else
			{
				value = p_param->ParameterData.IntegerData;
				IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_INIT, ("Read configuration. Registry %S, Value= 0x%x\n", pRegEntry[i].RegName.Buffer, value));
			}
		}

		else
		{
			value = pRegEntry[i].Default;
			status = NDIS_STATUS_SUCCESS;
			if (pRegEntry[i].bRequired)
			{
				ipoib_create_log(p_adapter->h_adapter, i, EVENT_IPOIB_WRONG_PARAMETER_ERR);
				IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_INIT, ("Read configuration.Registry %S value not found, setting default value= 0x%x\n", pRegEntry[i].RegName.Buffer, value));
			}
			else
			{
				ipoib_create_log(p_adapter->h_adapter, i, EVENT_IPOIB_WRONG_PARAMETER_INFO);
				IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_INIT, ("Read configuration. Registry %S value not found, Value= 0x%x\n", pRegEntry[i].RegName.Buffer, value));
			}

		}
		//
		// Store the value in the adapter structure.
		//
		switch(pRegEntry[i].FieldSize)
		{
			case 1:
				*((PUCHAR) structPointer) = (UCHAR) value;
				break;

			case 2:
				*((PUSHORT) structPointer) = (USHORT) value;
				break;

			case 4:
				*((PULONG) structPointer) = (ULONG) value;
				break;

			default:
				IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("Bogus field size %d\n", pRegEntry[i].FieldSize));
				break;
		}
	}

	// Send queue depth needs to be a power of two
	//static const INT sq_depth_step = 128;

	if (p_adapter->params.sq_depth % sq_depth_step) {
		static const c_sq_ind = 2;
		p_adapter->params.sq_depth = sq_depth_step *(
			p_adapter->params.sq_depth / sq_depth_step + !!( (p_adapter->params.sq_depth % sq_depth_step) > (sq_depth_step/2) ));
		ipoib_create_log(p_adapter->h_adapter, c_sq_ind, EVENT_IPOIB_WRONG_PARAMETER_WRN);
		IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_INIT, ("SQ DEPTH value was rounded to the closest acceptable value of  0x%x\n", p_adapter->params.sq_depth ));

	}


	// Adjusting the low watermark parameter
	p_adapter->params.rq_low_watermark =
			p_adapter->params.rq_depth / p_adapter->params.rq_low_watermark;

	p_adapter->params.xfer_block_size = (sizeof(eth_hdr_t) + p_adapter->params.payload_mtu);
	NdisReadNetworkAddress( &status, p_mac, p_len, h_config );
	if (status != NDIS_STATUS_SUCCESS) {
		// Don't rely on NDIS, zero the values
		*p_mac = NULL;
		*p_len = 0;
	}

	NdisCloseConfiguration( h_config );

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return NDIS_STATUS_SUCCESS;
}


NDIS_STATUS
ipoib_get_adapter_guids(
	IN				NDIS_HANDLE* const			h_adapter,
	IN	OUT			ipoib_adapter_t				*p_adapter )
{
	NTSTATUS			status;
	ib_al_ifc_data_t	data;
	IO_STACK_LOCATION	io_stack, *p_fwd_io_stack;
	DEVICE_OBJECT		*p_pdo;
	IRP					*p_irp;
	KEVENT				event;
	IO_STATUS_BLOCK		io_status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	NdisMGetDeviceProperty( h_adapter, &p_pdo, NULL, NULL, NULL, NULL );

	/* Query for our interface */
	data.size = sizeof(ipoib_ifc_data_t);
	data.version = IPOIB_INTERFACE_DATA_VERSION;
	data.type = &GUID_IPOIB_INTERFACE_DATA;
	data.p_data = &p_adapter->guids;

	io_stack.MinorFunction = IRP_MN_QUERY_INTERFACE;
	io_stack.Parameters.QueryInterface.Version = AL_INTERFACE_VERSION;
	io_stack.Parameters.QueryInterface.Size = sizeof(ib_al_ifc_t);
	io_stack.Parameters.QueryInterface.Interface =
		(INTERFACE*)p_adapter->p_ifc;
	io_stack.Parameters.QueryInterface.InterfaceSpecificData = &data;
	io_stack.Parameters.QueryInterface.InterfaceType = 
		&GUID_IB_AL_INTERFACE;

	KeInitializeEvent( &event, NotificationEvent, FALSE );

	/* Build the IRP for the HCA. */
	p_irp = IoBuildSynchronousFsdRequest( IRP_MJ_PNP, p_pdo,
		NULL, 0, NULL, &event, &io_status );
	if( !p_irp )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate query interface IRP.\n") );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	/* Copy the request query parameters. */
	p_fwd_io_stack = IoGetNextIrpStackLocation( p_irp );
	p_fwd_io_stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
	p_fwd_io_stack->Parameters.QueryInterface =
		io_stack.Parameters.QueryInterface;
	p_irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

	/* Send the IRP. */
	status = IoCallDriver( p_pdo, p_irp );
	if( status == STATUS_PENDING )
	{
		KeWaitForSingleObject( &event, Executive, KernelMode,
			FALSE, NULL );
		status = io_status.Status;
	}

	if( !NT_SUCCESS( status ) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Query interface for IPOIB interface returned %08x.\n", status) );
		return status;
	}

	/*
	 * Dereference the interface now so that the bus driver doesn't fail a
	 * query remove IRP.  We will always get unloaded before the bus driver
	 * since we're a child device.
	 */
	if (p_adapter->p_ifc)
		p_adapter->p_ifc->wdm.InterfaceDereference(
			p_adapter->p_ifc->wdm.Context );
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return NDIS_STATUS_SUCCESS;
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
@param h_adapter Handle assigned by NDIS for this NIC
@param wrapper_config_context Handle used for Ndis initialization functions
@return NDIS_STATUS_SUCCESS, NDIS_STATUS_UNSUPPORTED_MEDIA, NDIS_STATUS_RESOURCES,
NDIS_STATUS_NOT_SUPPORTED 
*/
NDIS_STATUS
ipoib_initialize(
		OUT			PNDIS_STATUS				p_open_status,
		OUT			PUINT						p_selected_medium_index,
	IN				PNDIS_MEDIUM				medium_array,
	IN				UINT						medium_array_size,
	IN				NDIS_HANDLE					h_adapter,
	IN				NDIS_HANDLE					wrapper_config_context )
{
	NDIS_STATUS			status;
	ib_api_status_t		ib_status;
	UINT				medium_index;
	ipoib_adapter_t		*p_adapter;

	IPOIB_ENTER( IPOIB_DBG_INIT );

#ifdef _DEBUG_
	PAGED_CODE();
#endif

	UNUSED_PARAM( p_open_status );
	UNUSED_PARAM( wrapper_config_context );

	/* Search for our medium */
	for( medium_index = 0; medium_index < medium_array_size; ++medium_index )
	{
		/* Check to see if we found our medium */
		if( medium_array[medium_index] == NdisMedium802_3 )
			break;
	}

	if( medium_index == medium_array_size ) /* Never found it */
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("No supported media.\n") );
		return NDIS_STATUS_UNSUPPORTED_MEDIA;
	}

	*p_selected_medium_index = medium_index;

	/* Create the adapter adapter */
	ib_status = ipoib_create_adapter( wrapper_config_context, h_adapter, &p_adapter );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_create_adapter returned status %d.\n", ib_status ) );
		return NDIS_STATUS_FAILURE;
	}

	/* Allow ten seconds for all SA queries to finish up. */
	NdisMSetAttributesEx( h_adapter, p_adapter, 5,
		NDIS_ATTRIBUTE_BUS_MASTER | NDIS_ATTRIBUTE_DESERIALIZE |
		NDIS_ATTRIBUTE_USES_SAFE_BUFFER_APIS,
		NdisInterfacePNPBus );

#if IPOIB_USE_DMA
	status =
		NdisMInitializeScatterGatherDma( h_adapter, TRUE, p_adapter->params.xfer_block_size );
	if( status != NDIS_STATUS_SUCCESS )
	{
		ipoib_destroy_adapter( p_adapter );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisMInitializeScatterGatherDma returned 0x%.8x.\n", status) );
		return status;
	}
#endif

	/* Create the adapter adapter */
	ib_status = ipoib_start_adapter( p_adapter );
	if( ib_status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( h_adapter,
			NDIS_ERROR_CODE_HARDWARE_FAILURE, 0 );
		ipoib_destroy_adapter( p_adapter );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_start_adapter returned status %d.\n", ib_status ) );
		return NDIS_STATUS_FAILURE;
	}

	ipoib_ref_ibat();

	IPOIB_PRINT_EXIT(TRACE_LEVEL_WARNING, IPOIB_DBG_INIT, 
		("=====> ipoib_initialize exited\n"));

	return status;
}


//! Deallocates resources when the NIC is removed and halts the NIC..
/*  IRQL = DISPATCH_LEVEL

@param adapter_context The adapter context allocated at start
*/
void
ipoib_halt(
	IN				NDIS_HANDLE					adapter_context )
{
	ipoib_adapter_t	*p_adapter;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	ipoib_deref_ibat();

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Port %016I64x (CA %016I64x port %d) halting\n",
			p_adapter->guids.port_guid.guid, p_adapter->guids.ca_guid,
			p_adapter->guids.port_num) );

	ipoib_destroy_adapter( p_adapter );

	IPOIB_PRINT_EXIT(TRACE_LEVEL_WARNING, IPOIB_DBG_INIT, 
		("=====> ipoib_halt exited\n"));
}


//! Reports the state of the NIC, or monitors the responsiveness of an underlying device driver.
/*  IRQL = DISPATCH_LEVEL

@param adapter_context The adapter context allocated at start
@return TRUE if the driver determines that its NIC is not operating
*/
BOOLEAN
ipoib_check_for_hang(
	IN				NDIS_HANDLE					adapter_context )
{
	ipoib_adapter_t	*p_adapter;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	if( p_adapter->reset )
	{
		IPOIB_EXIT( IPOIB_DBG_INIT );
		return FALSE;
	}
	if (p_adapter->hung) {
		ipoib_resume_oids(p_adapter);
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return (p_adapter->hung? TRUE:FALSE);
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
ipoib_query_info(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_OID					oid,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len,
		OUT			PULONG						p_bytes_written,
		OUT			PULONG						p_bytes_needed )
{
	ipoib_adapter_t		*p_adapter;
	NDIS_STATUS			status;
	USHORT				version;
	ULONG				info;
	PVOID				src_buf;
	ULONG				buf_len;
	pending_oid_t		oid_info;
	uint8_t				port_num;

	IPOIB_ENTER( IPOIB_DBG_OID );

	oid_info.oid = oid;
	oid_info.p_buf = info_buf;
	oid_info.buf_len = info_buf_len;
	oid_info.p_bytes_used = p_bytes_written;
	oid_info.p_bytes_needed = p_bytes_needed;

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	CL_ASSERT( p_bytes_written );
	CL_ASSERT( p_bytes_needed );
	CL_ASSERT( !p_adapter->pending_query );

	status = NDIS_STATUS_SUCCESS;
	src_buf = &info;
	buf_len = sizeof(info);

	port_num = p_adapter->guids.port_num;

	switch( oid )
	{
	/* Required General */
	case OID_GEN_SUPPORTED_LIST:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_SUPPORTED_LIST\n", port_num) );
		src_buf = (PVOID)SUPPORTED_OIDS;
		buf_len = sizeof(SUPPORTED_OIDS);
		break;

	case OID_GEN_HARDWARE_STATUS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_HARDWARE_STATUS\n", port_num) );
		cl_obj_lock( &p_adapter->obj );
		switch( p_adapter->state )
		{
		case IB_PNP_PORT_ADD:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d returning NdisHardwareStatusInitializing\n", port_num) );
			info = NdisHardwareStatusInitializing;
			break;
			
		case IB_PNP_PORT_ACTIVE:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d returning NdisHardwareStatusReady\n", port_num) );
			info = NdisHardwareStatusReady;
			break;

		default:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d returning NdisHardwareStatusNotReady\n", port_num) );
			info = NdisHardwareStatusNotReady;
		}
		cl_obj_unlock( &p_adapter->obj );
		break;

	case OID_GEN_MEDIA_SUPPORTED:
	case OID_GEN_MEDIA_IN_USE:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MEDIA_SUPPORTED "
			"or OID_GEN_MEDIA_IN_USE\n", port_num) );
		info = NdisMedium802_3;
		break;

	case OID_GEN_MAXIMUM_FRAME_SIZE:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MAXIMUM_FRAME_SIZE\n", port_num) );
		info = p_adapter->params.payload_mtu;
		break;

	case OID_GEN_LINK_SPEED:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_LINK_SPEED\n", port_num) );
		cl_obj_lock( &p_adapter->obj );
		info = p_adapter->port_rate;
		cl_obj_unlock( &p_adapter->obj );
		break;

	case OID_GEN_TRANSMIT_BUFFER_SPACE:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_TRANSMIT_BUFFER_SPACE\n", port_num) );
		info = p_adapter->params.sq_depth * p_adapter->params.xfer_block_size;
		break;

	case OID_GEN_RECEIVE_BUFFER_SPACE:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_TRANSMIT_BUFFER_SPACE "
			"or OID_GEN_RECEIVE_BUFFER_SPACE\n", port_num) );
		info = p_adapter->params.rq_depth * p_adapter->params.xfer_block_size;
		break;

	case OID_GEN_MAXIMUM_LOOKAHEAD:
	case OID_GEN_CURRENT_LOOKAHEAD:
	case OID_GEN_TRANSMIT_BLOCK_SIZE:
	case OID_GEN_RECEIVE_BLOCK_SIZE:
	case OID_GEN_MAXIMUM_TOTAL_SIZE:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MAXIMUM_LOOKAHEAD "
			"or OID_GEN_CURRENT_LOOKAHEAD or "
			"OID_GEN_TRANSMIT_BLOCK_SIZE or "
			"OID_GEN_RECEIVE_BLOCK_SIZE or "
			"OID_GEN_MAXIMUM_TOTAL_SIZE\n", port_num) );
		info = p_adapter->params.xfer_block_size;
		break;

	case OID_GEN_VENDOR_ID:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_VENDOR_ID\n", port_num) );
		src_buf = (void*)VENDOR_ID;
		buf_len = sizeof(VENDOR_ID);
		break;

	case OID_GEN_VENDOR_DESCRIPTION:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID, 
			("Port %d received query for OID_GEN_VENDOR_DESCRIPTION\n", port_num) );
		src_buf = VENDOR_DESCRIPTION;
		buf_len = sizeof(VENDOR_DESCRIPTION);
		break;

	case OID_GEN_VENDOR_DRIVER_VERSION:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_VENDOR_DRIVER_VERSION\n", port_num) );
		src_buf = &version;
		buf_len = sizeof(version);
		//TODO: Figure out what the right version is.
		version = 1 << 8 | 1;
		break;

	case OID_GEN_PHYSICAL_MEDIUM:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_PHYSICAL_MEDIUM\n", port_num) );
		info = NdisPhysicalMediumUnspecified;
		break;

	case OID_GEN_CURRENT_PACKET_FILTER:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_CURRENT_PACKET_FILTER\n", port_num) );
		info = p_adapter->packet_filter;
		break;

	case OID_GEN_DRIVER_VERSION:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_DRIVER_VERSION\n", port_num) );
		src_buf = &version;
		buf_len = sizeof(version);
		version = MAJOR_NDIS_VERSION << 8 | MINOR_NDIS_VERSION;
		break;

	case OID_GEN_MAC_OPTIONS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MAC_OPTIONS\n", port_num) );
		info = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
			NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
			NDIS_MAC_OPTION_NO_LOOPBACK |
			NDIS_MAC_OPTION_FULL_DUPLEX;
//TODO: Figure out if we will support priority and VLANs.
//				NDIS_MAC_OPTION_8021P_PRIORITY;
//#ifdef NDIS51_MINIPORT
//			info |= NDIS_MAC_OPTION_8021Q_VLAN;
//#endif
		break;

	case OID_GEN_MEDIA_CONNECT_STATUS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MEDIA_CONNECT_STATUS\n", port_num) );
		cl_obj_lock( &p_adapter->obj );
		switch( p_adapter->state )
		{
		case IB_PNP_PORT_ADD:
		case IB_PNP_PORT_INIT:
			/*
			 * Delay reporting media state until we know whether the port is
			 * either up or down.
			 */
			p_adapter->pending_query = TRUE;
			p_adapter->query_oid = oid_info;

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d returning NDIS_STATUS_PENDING\n", port_num) );
			status = NDIS_STATUS_PENDING;
			break;

		case IB_PNP_PORT_ACTIVE:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d returning NdisMediaStateConnected\n", port_num) );
			info = NdisMediaStateConnected;
			break;

		case IB_PNP_PORT_REMOVE:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d returning NDIS_STATUS_NOT_ACCEPTED\n", port_num) );
			status = NDIS_STATUS_NOT_ACCEPTED;
			break;

		default:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d returning NdisMediaStateDisconnected\n", port_num) );
			info = NdisMediaStateDisconnected;
		}
		cl_obj_unlock( &p_adapter->obj );
		break;

	case OID_GEN_MAXIMUM_SEND_PACKETS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MAXIMUM_SEND_PACKETS\n", port_num) );
		info = MINIPORT_MAX_SEND_PACKETS;
		break;

	/* Required General Statistics */
	case OID_GEN_XMIT_OK:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_XMIT_OK\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_send_stat( p_adapter, IP_STAT_SUCCESS, &oid_info );
		break;

	case OID_GEN_RCV_OK:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_RCV_OK\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_SUCCESS, &oid_info );
		break;

	case OID_GEN_XMIT_ERROR:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_XMIT_ERROR\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_send_stat( p_adapter, IP_STAT_ERROR, &oid_info );
		break;

	case OID_GEN_RCV_ERROR:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_RCV_ERROR\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_ERROR, &oid_info );
		break;

	case OID_GEN_RCV_NO_BUFFER:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_RCV_NO_BUFFER\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_DROPPED, &oid_info );
		break;

	case OID_GEN_DIRECTED_BYTES_XMIT:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_DIRECTED_BYTES_XMIT\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_send_stat( p_adapter, IP_STAT_UCAST_BYTES, &oid_info );
		break;

	case OID_GEN_DIRECTED_FRAMES_XMIT:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_DIRECTED_FRAMES_XMIT\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_send_stat( p_adapter, IP_STAT_UCAST_FRAMES, &oid_info );
		break;

	case OID_GEN_MULTICAST_BYTES_XMIT:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MULTICAST_BYTES_XMIT\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_send_stat( p_adapter, IP_STAT_MCAST_BYTES, &oid_info );
		break;

	case OID_GEN_MULTICAST_FRAMES_XMIT:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MULTICAST_FRAMES_XMIT\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_send_stat( p_adapter, IP_STAT_MCAST_FRAMES, &oid_info );
		break;

	case OID_GEN_BROADCAST_BYTES_XMIT:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_BROADCAST_BYTES_XMIT\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_send_stat( p_adapter, IP_STAT_BCAST_BYTES, &oid_info );
		break;

	case OID_GEN_BROADCAST_FRAMES_XMIT:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_BROADCAST_FRAMES_XMIT\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_send_stat( p_adapter, IP_STAT_BCAST_FRAMES, &oid_info );
		break;

	case OID_GEN_DIRECTED_BYTES_RCV:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_DIRECTED_BYTES_RCV\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_UCAST_BYTES, &oid_info );
		break;

	case OID_GEN_DIRECTED_FRAMES_RCV:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_DIRECTED_FRAMES_RCV\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_UCAST_FRAMES, &oid_info );
		break;

	case OID_GEN_MULTICAST_BYTES_RCV:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MULTICAST_BYTES_RCV\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_MCAST_BYTES, &oid_info );
		break;

	case OID_GEN_MULTICAST_FRAMES_RCV:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_MULTICAST_FRAMES_RCV\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_MCAST_FRAMES, &oid_info );
		break;

	case OID_GEN_BROADCAST_BYTES_RCV:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_BROADCAST_BYTES_RCV\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_BCAST_BYTES, &oid_info );
		break;

	case OID_GEN_BROADCAST_FRAMES_RCV:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_GEN_BROADCAST_FRAMES_RCV\n", port_num) );
		src_buf = NULL;
		status = ipoib_get_recv_stat( p_adapter, IP_STAT_BCAST_FRAMES, &oid_info );
		break;

	/* Required Ethernet operational characteristics */
	case OID_802_3_PERMANENT_ADDRESS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_802_3_PERMANENT_ADDRESS\n", port_num) );
		src_buf = &p_adapter->mac;
		buf_len = sizeof(p_adapter->mac);
		break;

	case OID_802_3_CURRENT_ADDRESS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_802_3_CURRENT_ADDRESS\n", port_num) );
		src_buf = &p_adapter->params.conf_mac;
		buf_len = sizeof(p_adapter->params.conf_mac);
		break;

	case OID_802_3_MULTICAST_LIST:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_802_3_MULTICAST_LIST\n", port_num) );
		src_buf = p_adapter->mcast_array;
		buf_len = p_adapter->mcast_array_size * sizeof(mac_addr_t);
		break;

	case OID_802_3_MAXIMUM_LIST_SIZE:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_802_3_MAXIMUM_LIST_SIZE\n", port_num) );
		info = MAX_MCAST;
		break;

	case OID_802_3_MAC_OPTIONS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_802_3_MAC_OPTIONS\n", port_num) );
		info = 0;
		break;

	/* Required Ethernet stats */
	case OID_802_3_RCV_ERROR_ALIGNMENT:
	case OID_802_3_XMIT_ONE_COLLISION:
	case OID_802_3_XMIT_MORE_COLLISIONS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received query for OID_802_3_RCV_ERROR_ALIGNMENT or "
			"OID_802_3_XMIT_ONE_COLLISION or "
			"OID_802_3_XMIT_MORE_COLLISIONS\n", port_num) );
		info = 0;
		break;

	case OID_TCP_TASK_OFFLOAD:
		src_buf = NULL;
		status = __ipoib_get_tcp_task_offload( p_adapter, &oid_info );
		break;

	/* Optional General */
	case OID_GEN_SUPPORTED_GUIDS:
#ifdef NDIS51_MINIPORT
	case OID_GEN_VLAN_ID:
#endif

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
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received an unsupported oid of 0x%.8X!\n", port_num, oid) );
		break;

	case OID_GEN_PROTOCOL_OPTIONS:
	case OID_GEN_NETWORK_LAYER_ADDRESSES:
	case OID_GEN_TRANSPORT_HEADER_OFFSET:
#ifdef NDIS51_MINIPORT
	case OID_GEN_MACHINE_NAME:
	case OID_GEN_RNDIS_CONFIG_PARAMETER:
#endif
	default:
		status = NDIS_STATUS_INVALID_OID;
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received an invalid oid of 0x%.8X!\n", port_num, oid) );
		break;
	}

	/*
	 * Complete the request as if it was handled asynchronously to maximize
	 * code reuse for when we really handle the requests asynchronously.
	 * Note that this requires the QueryInformation entry point to always
	 * return NDIS_STATUS_PENDING
	 */
	if( status != NDIS_STATUS_PENDING )
	{
		ipoib_complete_query(
			p_adapter, &oid_info, status, src_buf, buf_len );
	}

	IPOIB_EXIT( IPOIB_DBG_OID );
	return NDIS_STATUS_PENDING;
}


static void
ipoib_complete_query(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				pending_oid_t* const		p_oid_info,
	IN		const	NDIS_STATUS					status,
	IN		const	void* const					p_buf,
	IN		const	ULONG						buf_len )
{
	NDIS_STATUS		oid_status = status;

	IPOIB_ENTER( IPOIB_DBG_OID );

	CL_ASSERT( status != NDIS_STATUS_PENDING );

	if( status == NDIS_STATUS_SUCCESS )
	{
		if( p_oid_info->buf_len < buf_len )
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
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
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Returning NDIS_NOT_ACCEPTED") );
			oid_status = NDIS_STATUS_NOT_ACCEPTED;
		}
	}
	else
	{
		*p_oid_info->p_bytes_used = 0;
	}

	p_adapter->pending_query = FALSE;

	NdisMQueryInformationComplete( p_adapter->h_adapter, oid_status );

	IPOIB_EXIT( IPOIB_DBG_OID );
}


static NDIS_STATUS
__ipoib_get_tcp_task_offload(
	IN				ipoib_adapter_t*			p_adapter,
	IN				pending_oid_t* const		p_oid_info )
{
	NDIS_TASK_OFFLOAD_HEADER	*p_offload_hdr;
	NDIS_TASK_OFFLOAD			*p_offload_task;
	NDIS_TASK_TCP_IP_CHECKSUM	*p_offload_chksum;

	NDIS_TASK_TCP_LARGE_SEND	*p_offload_lso;
	ULONG						buf_len;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
		("Port %d received query for OID_TCP_TASK_OFFLOAD\n",
		p_adapter->guids.port_num) );

	buf_len = sizeof(NDIS_TASK_OFFLOAD_HEADER) +
		offsetof( NDIS_TASK_OFFLOAD, TaskBuffer ) +
		sizeof(NDIS_TASK_TCP_IP_CHECKSUM) +
		(p_adapter->params.lso  ? 
			sizeof(NDIS_TASK_OFFLOAD) + sizeof(NDIS_TASK_TCP_LARGE_SEND)
			: 0);

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

	p_offload_hdr->OffsetFirstTask = sizeof(NDIS_TASK_OFFLOAD_HEADER);
	p_offload_task = (NDIS_TASK_OFFLOAD*)(p_offload_hdr + 1);
	p_offload_task->Version = NDIS_TASK_OFFLOAD_VERSION;
	p_offload_task->Size = sizeof(NDIS_TASK_OFFLOAD);
	p_offload_task->Task = TcpIpChecksumNdisTask;
	p_offload_task->OffsetNextTask = 0;
	p_offload_task->TaskBufferLength = sizeof(NDIS_TASK_TCP_IP_CHECKSUM);
	p_offload_chksum =
		(NDIS_TASK_TCP_IP_CHECKSUM*)p_offload_task->TaskBuffer;
	
	p_offload_chksum->V4Transmit.IpOptionsSupported =
	p_offload_chksum->V4Transmit.TcpOptionsSupported =
	p_offload_chksum->V4Transmit.TcpChecksum =
	p_offload_chksum->V4Transmit.UdpChecksum =
	p_offload_chksum->V4Transmit.IpChecksum =
		!!(p_adapter->params.send_chksum_offload);

	p_offload_chksum->V4Receive.IpOptionsSupported =
	p_offload_chksum->V4Receive.TcpOptionsSupported =
	p_offload_chksum->V4Receive.TcpChecksum =
	p_offload_chksum->V4Receive.UdpChecksum =
	p_offload_chksum->V4Receive.IpChecksum =
		!!(p_adapter->params.recv_chksum_offload);

	p_offload_chksum->V6Transmit.IpOptionsSupported = FALSE;
	p_offload_chksum->V6Transmit.TcpOptionsSupported = FALSE;
	p_offload_chksum->V6Transmit.TcpChecksum = FALSE;
	p_offload_chksum->V6Transmit.UdpChecksum = FALSE;

	p_offload_chksum->V6Receive.IpOptionsSupported = FALSE;
	p_offload_chksum->V6Receive.TcpOptionsSupported = FALSE;
	p_offload_chksum->V6Receive.TcpChecksum = FALSE;
	p_offload_chksum->V6Receive.UdpChecksum = FALSE;


	if (p_adapter->params.lso) {
		// set the previous pointer to the correct place
		p_offload_task->OffsetNextTask = FIELD_OFFSET(NDIS_TASK_OFFLOAD, TaskBuffer) +
						p_offload_task->TaskBufferLength;
		// set the LSO packet
		p_offload_task = (PNDIS_TASK_OFFLOAD)
						((PUCHAR)p_offload_task + p_offload_task->OffsetNextTask);

		p_offload_task->Version = NDIS_TASK_OFFLOAD_VERSION;
		p_offload_task->Size = sizeof(NDIS_TASK_OFFLOAD);
		p_offload_task->Task = TcpLargeSendNdisTask;
		p_offload_task->OffsetNextTask = 0;
		p_offload_task->TaskBufferLength = sizeof(NDIS_TASK_TCP_LARGE_SEND);

		p_offload_lso = (PNDIS_TASK_TCP_LARGE_SEND) p_offload_task->TaskBuffer;

		p_offload_lso->Version = 0;
		//TODO optimal size: 60000, 64000 or 65536
		//TODO LSO_MIN_SEG_COUNT to be 1
		p_offload_lso->MaxOffLoadSize = LARGE_SEND_OFFLOAD_SIZE; 
#define LSO_MIN_SEG_COUNT 2
		p_offload_lso->MinSegmentCount = LSO_MIN_SEG_COUNT;
		p_offload_lso->TcpOptions = TRUE;
		p_offload_lso->IpOptions = TRUE;
	}

	*(p_oid_info->p_bytes_used) = buf_len;

	return NDIS_STATUS_SUCCESS;
}


static NDIS_STATUS
__ipoib_set_tcp_task_offload(
	IN				ipoib_adapter_t*			p_adapter,
	IN				void* const					p_info_buf,
	IN				ULONG* const				p_info_len )
{
	NDIS_TASK_OFFLOAD_HEADER	*p_offload_hdr;
	NDIS_TASK_OFFLOAD			*p_offload_task;
	NDIS_TASK_TCP_IP_CHECKSUM	*p_offload_chksum;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
		("Port %d received set for OID_TCP_TASK_OFFLOAD\n",
		p_adapter->guids.port_num) );

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

	if( !p_adapter->params.send_chksum_offload &&
		(p_offload_chksum->V4Transmit.IpOptionsSupported ||
		p_offload_chksum->V4Transmit.TcpOptionsSupported ||
		p_offload_chksum->V4Transmit.TcpChecksum ||
		p_offload_chksum->V4Transmit.UdpChecksum ||
		p_offload_chksum->V4Transmit.IpChecksum) )
	{
		return NDIS_STATUS_NOT_SUPPORTED;
	}

	if( !p_adapter->params.recv_chksum_offload &&
		(p_offload_chksum->V4Receive.IpOptionsSupported ||
		p_offload_chksum->V4Receive.TcpOptionsSupported ||
		p_offload_chksum->V4Receive.TcpChecksum ||
		p_offload_chksum->V4Receive.UdpChecksum ||
		p_offload_chksum->V4Receive.IpChecksum) )
	{
		return NDIS_STATUS_NOT_SUPPORTED;
	}

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
ipoib_reset(
		OUT			PBOOLEAN					p_addr_reset,
	IN				NDIS_HANDLE					adapter_context)
{
	ipoib_adapter_t* p_adapter;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_addr_reset );
	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	switch( ipoib_reset_adapter( p_adapter ) )
	{
	case IB_NOT_DONE:
		IPOIB_EXIT( IPOIB_DBG_INIT );
		return NDIS_STATUS_PENDING;

	case IB_SUCCESS:
		IPOIB_EXIT( IPOIB_DBG_INIT );
		*p_addr_reset = TRUE;
		return NDIS_STATUS_SUCCESS;

	case IB_INVALID_STATE:
		IPOIB_EXIT( IPOIB_DBG_INIT );
		return NDIS_STATUS_RESET_IN_PROGRESS;

	default:
		IPOIB_EXIT( IPOIB_DBG_INIT );
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
ipoib_set_info(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_OID					oid,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len,
		OUT			PULONG						p_bytes_read,
		OUT			PULONG						p_bytes_needed )
{
	ipoib_adapter_t*	p_adapter;
	NDIS_STATUS			status;

	ULONG				buf_len;
	uint8_t				port_num;

	KLOCK_QUEUE_HANDLE	hdl;
	
	IPOIB_ENTER( IPOIB_DBG_OID );

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	CL_ASSERT( p_bytes_read );
	CL_ASSERT( p_bytes_needed );
	CL_ASSERT( !p_adapter->pending_set );

	status = NDIS_STATUS_SUCCESS;
	*p_bytes_needed = 0;
	buf_len = sizeof(ULONG);

	port_num = p_adapter->guids.port_num;

	switch( oid )
	{
	/* Required General */
	case OID_GEN_CURRENT_PACKET_FILTER:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received set for OID_GEN_CURRENT_PACKET_FILTER\n", port_num));
		if( info_buf_len < sizeof(p_adapter->packet_filter) )
		{
			status = NDIS_STATUS_INVALID_LENGTH;
		}
		else if( !info_buf )
		{
			status = NDIS_STATUS_INVALID_DATA;
		}
		else
		{
			KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
			cl_obj_lock( &p_adapter->obj );
			switch( p_adapter->state )
			{
			case IB_PNP_PORT_ADD:
				p_adapter->set_oid.oid = oid;
				p_adapter->set_oid.p_buf = info_buf;
				p_adapter->set_oid.buf_len = info_buf_len;
				p_adapter->set_oid.p_bytes_used = p_bytes_read;
				p_adapter->set_oid.p_bytes_needed = p_bytes_needed;
				p_adapter->pending_set = TRUE;
				status = NDIS_STATUS_PENDING;
				break;

			case IB_PNP_PORT_REMOVE:
				status = NDIS_STATUS_NOT_ACCEPTED;
				break;

			default:
				if( !p_adapter->packet_filter && (*(uint32_t*)info_buf) )
				{
					cl_qlist_insert_tail(
						&g_ipoib.adapter_list, &p_adapter->entry );

					/*
					 * Filter was zero, now non-zero.  Register IP addresses
					 * with SA.
					 */
					ipoib_reg_addrs( p_adapter );
				}
				else if( p_adapter->packet_filter && !(*(uint32_t*)info_buf) )
				{
					/*
					 * Filter was non-zero, now zero.  Deregister IP addresses.
					 */
					ipoib_dereg_addrs( p_adapter );

					ASSERT( cl_qlist_count( &g_ipoib.adapter_list ) );
					cl_qlist_remove_item(
						&g_ipoib.adapter_list, &p_adapter->entry );
				}

				p_adapter->packet_filter = *(uint32_t*)info_buf;
			}
			cl_obj_unlock( &p_adapter->obj );
			KeReleaseInStackQueuedSpinLock( &hdl );
		}
		break;

	case OID_GEN_CURRENT_LOOKAHEAD:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received set for OID_GEN_CURRENT_LOOKAHEAD\n", port_num));
		if( info_buf_len < buf_len )
			status = NDIS_STATUS_INVALID_LENGTH;
		break;

	case OID_GEN_PROTOCOL_OPTIONS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received set for OID_GEN_PROTOCOL_OPTIONS\n", port_num));
		if( info_buf_len < buf_len )
			status = NDIS_STATUS_INVALID_LENGTH;
		break;

	case OID_GEN_NETWORK_LAYER_ADDRESSES:
		status = __ipoib_set_net_addr( p_adapter, info_buf, info_buf_len, p_bytes_read, p_bytes_needed);
		break;

#ifdef NDIS51_MINIPORT
	case OID_GEN_MACHINE_NAME:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OID,
			("Port %d received set for OID_GEN_MACHINE_NAME\n", port_num) );
		break;
#endif

	/* Required Ethernet operational characteristics */
	case OID_802_3_MULTICAST_LIST:
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_OID,
			("Port %d received set for OID_802_3_MULTICAST_LIST\n", port_num) );
		if( info_buf_len > MAX_MCAST * sizeof(mac_addr_t) )
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d OID_802_3_MULTICAST_LIST - Multicast list full.\n", port_num) );
			status = NDIS_STATUS_MULTICAST_FULL;
			*p_bytes_needed = MAX_MCAST * sizeof(mac_addr_t);
		}
		else if( info_buf_len % sizeof(mac_addr_t) )
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d OID_802_3_MULTICAST_LIST - Invalid input buffer.\n", port_num) );
			status = NDIS_STATUS_INVALID_DATA;
		}
		else if( !info_buf && info_buf_len )
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d OID_802_3_MULTICAST_LIST - Invalid input buffer.\n", port_num) );
			status = NDIS_STATUS_INVALID_DATA;
		}
		else
		{
			ipoib_refresh_mcast( p_adapter, (mac_addr_t*)info_buf,
				(uint8_t)(info_buf_len / sizeof(mac_addr_t)) );

			buf_len = info_buf_len;
			/*
			 * Note that we don't return pending.  It will likely take longer
			 * for our SA transactions to complete than NDIS will give us
			 * before reseting the adapter.  If an SA failure is encountered,
			 * the adapter will be marked as hung and we will get reset.
			 */
			status = NDIS_STATUS_SUCCESS;
		}
		break;

	case OID_TCP_TASK_OFFLOAD:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received set for OID_TCP_TASK_OFFLOAD\n", port_num) );

		buf_len = info_buf_len;
		status =
			__ipoib_set_tcp_task_offload( p_adapter, info_buf, &buf_len );
		break;

	/* Optional General */
	case OID_GEN_TRANSPORT_HEADER_OFFSET:
#ifdef NDIS51_MINIPORT
	case OID_GEN_RNDIS_CONFIG_PARAMETER:
	case OID_GEN_VLAN_ID:
#endif
		status = NDIS_STATUS_NOT_SUPPORTED;
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received an unsupported oid of 0x%.8X!\n", port_num, oid));
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
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received an invalid oid of 0x%.8X!\n", port_num, oid));
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

	IPOIB_EXIT( IPOIB_DBG_OID );
	return status;
}


//! Transfers some number of packets, specified as an array of packet pointers, over the network. 
/*  For a deserialized driver, these packets are completed asynchronously
using NdisMSendComplete.
IRQL <= DISPATCH_LEVEL

@param adapter_context Pointer to ipoib_adapter_t structure with per NIC state
@param packet_array Array of packets to send
@param numPackets Number of packets in the array
*/
void
ipoib_send_packets(
	IN				NDIS_HANDLE					adapter_context,
	IN				PPNDIS_PACKET				packet_array,
	IN				UINT						num_packets )
{
	ipoib_adapter_t		*p_adapter;
	ipoib_port_t		*p_port;
	UINT				packet_num;
	PERF_DECLARE( SendPackets );
	PERF_DECLARE( PortSend );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	cl_perf_start( SendPackets );

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	cl_obj_lock( &p_adapter->obj );
	if( p_adapter->state != IB_PNP_PORT_ACTIVE || !p_adapter->p_port )
	{
		cl_obj_unlock( &p_adapter->obj );
		for( packet_num = 0; packet_num < num_packets; ++packet_num )
		{
			ipoib_inc_send_stat( p_adapter, IP_STAT_DROPPED, 0 );
			NdisMSendCompleteX( p_adapter->h_adapter,
				packet_array[packet_num], NDIS_STATUS_ADAPTER_NOT_READY );
		}
		IPOIB_EXIT( IPOIB_DBG_SEND );
		return;
	}

	p_port = p_adapter->p_port;
	ipoib_port_ref( p_port, ref_send_packets );
	cl_obj_unlock( &p_adapter->obj );

	cl_perf_start( PortSend );
	ipoib_port_send( p_port, packet_array, num_packets );
	cl_perf_stop( &p_port->p_adapter->perf, PortSend );
	ipoib_port_deref( p_port, ref_send_packets );

	cl_perf_stop( &p_adapter->perf, SendPackets );

	cl_perf_log( &p_adapter->perf, SendBundle, num_packets );

	IPOIB_EXIT( IPOIB_DBG_SEND );
}


void
ipoib_pnp_notify(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_DEVICE_PNP_EVENT		pnp_event,
	IN				PVOID						info_buf,
	IN				ULONG						info_buf_len )
{
	ipoib_adapter_t	*p_adapter;

	IPOIB_ENTER( IPOIB_DBG_PNP );

	UNUSED_PARAM( info_buf );
	UNUSED_PARAM( info_buf_len );

	p_adapter = (ipoib_adapter_t*)adapter_context;

	IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_PNP, ("Event %d\n", pnp_event) );
	if( pnp_event != NdisDevicePnPEventPowerProfileChanged )
	{
		cl_obj_lock( &p_adapter->obj );
		p_adapter->state = IB_PNP_PORT_REMOVE;
		cl_obj_unlock( &p_adapter->obj );

		ipoib_resume_oids( p_adapter );
		if ( p_adapter->p_stat ) 
			p_adapter->p_stat->n_pnp_irps++;
	}
	else 
		if ( p_adapter->p_stat ) 
			p_adapter->p_stat->n_power_irps++;

	IPOIB_PRINT_EXIT(TRACE_LEVEL_WARNING, IPOIB_DBG_PNP, 
		("=====> ipoib_pnp_notify exited, PnP event %d\n", pnp_event));
}


void
ipoib_shutdown(
	IN				PVOID						adapter_context )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );
	UNUSED_PARAM( adapter_context );
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


void
ipoib_resume_oids(
	IN				ipoib_adapter_t* const		p_adapter )
{
	ULONG				info;
	NDIS_STATUS			status;
	boolean_t			pending_query, pending_set;
	pending_oid_t		query_oid = {0};
	pending_oid_t		set_oid = {0};
	KLOCK_QUEUE_HANDLE	hdl;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	cl_obj_lock( &p_adapter->obj );
	/*
	 * Set the status depending on our state.  Fail OID requests that
	 * are pending while we reset the adapter.
	 */
	switch( p_adapter->state )
	{
	case IB_PNP_PORT_ADD:
		status = NDIS_STATUS_FAILURE;
		break;

	case IB_PNP_PORT_REMOVE:
		status = NDIS_STATUS_NOT_ACCEPTED;
		break;
		
	default:
		status = NDIS_STATUS_SUCCESS;
	}

	pending_query = p_adapter->pending_query;
	if( pending_query )
	{
		query_oid = p_adapter->query_oid;
		p_adapter->pending_query = FALSE;
	}
	pending_set = p_adapter->pending_set;
	if( pending_set )
	{
		set_oid = p_adapter->set_oid;
		p_adapter->pending_set = FALSE;
	}
	cl_obj_unlock( &p_adapter->obj );

	/*
	 * If we had a pending OID request for OID_GEN_LINK_SPEED,
	 * complete it now.  Note that we hold the object lock since
	 * NdisMQueryInformationComplete is called at DISPATCH_LEVEL.
	 */
	if( pending_query )
	{
		switch( query_oid.oid )
		{
		case OID_GEN_MEDIA_CONNECT_STATUS:
			info = NdisMediaStateConnected;
			ipoib_complete_query( p_adapter, &query_oid,
				status, &info, sizeof(info) );
			break;

		default:
			CL_ASSERT( query_oid.oid == OID_GEN_MEDIA_CONNECT_STATUS );
			break;
		}
	}

	if( pending_set )
	{
		switch( set_oid.oid )
		{
		case OID_GEN_CURRENT_PACKET_FILTER:
			/* Validation already performed in the SetInformation path. */

			KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
			cl_obj_lock( &p_adapter->obj );
			if( !p_adapter->packet_filter && (*(PULONG)set_oid.p_buf) )
			{
				cl_qlist_insert_tail(
					&g_ipoib.adapter_list, &p_adapter->entry );
				/*
				 * Filter was zero, now non-zero.  Register IP addresses
				 * with SA.
				 */
				ipoib_reg_addrs( p_adapter );
			}
			else if( p_adapter->packet_filter && !(*(PULONG)set_oid.p_buf) )
			{
				/* Filter was non-zero, now zero.  Deregister IP addresses. */
				ipoib_dereg_addrs( p_adapter );

				ASSERT( cl_qlist_count( &g_ipoib.adapter_list ) );
				cl_qlist_remove_item(
					&g_ipoib.adapter_list, &p_adapter->entry );
			}
			p_adapter->packet_filter = *(PULONG)set_oid.p_buf;

			cl_obj_unlock( &p_adapter->obj );
			KeReleaseInStackQueuedSpinLock( &hdl );

			NdisMSetInformationComplete( p_adapter->h_adapter, status );
			break;

		case OID_GEN_NETWORK_LAYER_ADDRESSES:
			status = __ipoib_set_net_addr( p_adapter,
										   p_adapter->set_oid.p_buf,
										   p_adapter->set_oid.buf_len,
										   p_adapter->set_oid.p_bytes_used,
										   p_adapter->set_oid.p_bytes_needed );
			if( status != NDIS_STATUS_PENDING )
			{
				NdisMSetInformationComplete( p_adapter->h_adapter, status );
			}
			break;

		default:
			CL_ASSERT( set_oid.oid && 0 );
			break;
		}
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static NDIS_STATUS
__ipoib_set_net_addr(
	IN		ipoib_adapter_t *	p_adapter,
	IN		PVOID				info_buf,
	IN		ULONG				info_buf_len,
		OUT	PULONG				p_bytes_read,
		OUT	PULONG				p_bytes_needed )
{
	NDIS_STATUS				status;
	PNETWORK_ADDRESS_LIST	p_net_addrs;
	PNETWORK_ADDRESS		p_net_addr_oid;
	PNETWORK_ADDRESS_IP		p_ip_addr;

	net_address_item_t		*p_addr_item;

	cl_status_t				cl_status;

	size_t					idx;
	LONG					i;
	ULONG					addr_size;
	ULONG					total_size;

	uint8_t					port_num;

	IPOIB_ENTER( IPOIB_DBG_OID );

	status = NDIS_STATUS_SUCCESS;
	port_num = p_adapter->guids.port_num;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
		("Port %d received set for OID_GEN_NETWORK_LAYER_ADDRESSES\n",
		port_num) );

	if( !info_buf )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port %d - OID_GEN_NETWORK_LAYER_ADDRESSES - "
			"NULL buffer\n", port_num) );
		IPOIB_EXIT( IPOIB_DBG_OID );
		return NDIS_STATUS_INVALID_DATA;
	}

	/*
	 * Must use field offset because the structures define array's of size one
	 * of a the incorrect type for what is really stored.
	 */
	if( info_buf_len < FIELD_OFFSET(NETWORK_ADDRESS_LIST, Address) )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
			("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - "
			"bad length of %d, not enough "
			"for NETWORK_ADDRESS_LIST (%d)\n", port_num, info_buf_len,
			FIELD_OFFSET(NETWORK_ADDRESS_LIST, Address)) );
		*p_bytes_needed = FIELD_OFFSET(NETWORK_ADDRESS_LIST, Address);
		IPOIB_EXIT( IPOIB_DBG_OID );
		return NDIS_STATUS_INVALID_LENGTH;
	}

	p_net_addrs = (PNETWORK_ADDRESS_LIST)info_buf;
	if( p_net_addrs->AddressCount == 0)
	{
		if( p_net_addrs->AddressType == NDIS_PROTOCOL_ID_TCP_IP )
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OID,
				("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - "
				"clear TCP/IP addresses\n", port_num) );
		}
		else
		{
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OID,
				("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - "
				"Non TCP/IP address type of 0x%.4X on clear\n",
				port_num, p_net_addrs->AddressType) );
			IPOIB_EXIT( IPOIB_DBG_OID );
			return NDIS_STATUS_SUCCESS;
		}
	}

	addr_size = FIELD_OFFSET(NETWORK_ADDRESS, Address) +
		NETWORK_ADDRESS_LENGTH_IP;
	total_size = FIELD_OFFSET(NETWORK_ADDRESS_LIST, Address) +
		addr_size * p_net_addrs->AddressCount;

	if( info_buf_len < total_size )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - "
			"bad length of %d, %d required for %d addresses\n",
			port_num, info_buf_len, total_size, p_net_addrs->AddressCount) );
		*p_bytes_needed = total_size;
		IPOIB_EXIT( IPOIB_DBG_OID );
		return NDIS_STATUS_INVALID_LENGTH;
	}

	/* Lock lists for duration since SA callbacks can occur on other CPUs */
	cl_obj_lock( &p_adapter->obj );

	/* Set the capacity of the vector to accomodate all assinged addresses. */
	cl_status = cl_vector_set_capacity(
		&p_adapter->ip_vector, p_net_addrs->AddressCount );
	if( cl_status != CL_SUCCESS )
	{
		cl_obj_unlock( &p_adapter->obj );
		IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port %d - OID_GEN_NETWORK_LAYER_ADDRESSES - "
			"Failed to set IP vector capacity: %#x\n", port_num,
			cl_status) );
		IPOIB_EXIT( IPOIB_DBG_OID );
		return NDIS_STATUS_RESOURCES;
	}

	*p_bytes_read = total_size;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
		("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - List contains %d addresses\n",
			port_num, p_net_addrs->AddressCount));

	/* First look for addresses we had that should be removed */
	for( idx = 0; idx != cl_vector_get_size( &p_adapter->ip_vector ); idx++ )
	{
		p_addr_item = (net_address_item_t*)
			cl_vector_get_ptr( &p_adapter->ip_vector, idx );
		p_net_addr_oid = (PNETWORK_ADDRESS)p_net_addrs->Address;

		for( i = 0; i < p_net_addrs->AddressCount; ++i, p_net_addr_oid =
			(PNETWORK_ADDRESS)((uint8_t *)p_net_addr_oid +
			FIELD_OFFSET(NETWORK_ADDRESS, Address) +
			p_net_addr_oid->AddressLength) )
		{

			if( p_net_addr_oid->AddressType != NDIS_PROTOCOL_ID_TCP_IP )
			{
				IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_OID,
					("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Address %d is wrong type of 0x%.4X, "
						"should be 0x%.4X\n", port_num, i, p_net_addr_oid->AddressType,
						NDIS_PROTOCOL_ID_TCP_IP));
				continue;
			}

			if( p_net_addr_oid->AddressLength != NETWORK_ADDRESS_LENGTH_IP)
			{
				IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_OID,
					("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Address %d is wrong size of %d, "
						"should be %d\n", port_num, i, p_net_addr_oid->AddressLength,
						NETWORK_ADDRESS_LENGTH_IP));
				continue;
			}
			p_ip_addr = (PNETWORK_ADDRESS_IP)p_net_addr_oid->Address;
			if( !cl_memcmp( &p_ip_addr->in_addr,
				&p_addr_item->address.as_ulong, sizeof(ULONG) ) )
			{
				break;
			}
		}

		if( i == p_net_addrs->AddressCount )
		{
			/* Didn't find a match, delete from SA */
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Deleting Address %d.%d.%d.%d\n",
					port_num,
					p_addr_item->address.as_bytes[0],
					p_addr_item->address.as_bytes[1],
					p_addr_item->address.as_bytes[2],
					p_addr_item->address.as_bytes[3]));

			if( p_addr_item->p_reg )
			{
				if( p_addr_item->p_reg->h_reg_svc )
				{
					p_adapter->p_ifc->dereg_svc(
						p_addr_item->p_reg->h_reg_svc, __ipoib_ats_dereg_cb );
				}
				else
				{
					cl_free( p_addr_item->p_reg );
				}
				p_addr_item->p_reg = NULL;
			}
			p_addr_item->address.as_ulong = 0;
		}
	}

	/* Now look for new addresses */
	p_net_addr_oid = (NETWORK_ADDRESS *)p_net_addrs->Address;
	idx = 0;
	for( i = 0; i < p_net_addrs->AddressCount; i++, p_net_addr_oid =
		(PNETWORK_ADDRESS)((uint8_t *)p_net_addr_oid +
		FIELD_OFFSET(NETWORK_ADDRESS, Address) + p_net_addr_oid->AddressLength) )
	{

		if( p_net_addr_oid->AddressType != NDIS_PROTOCOL_ID_TCP_IP )
		{
			IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_OID,
				("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Address %d is wrong type of 0x%.4X, "
					"should be 0x%.4X\n", port_num, i, p_net_addr_oid->AddressType,
					NDIS_PROTOCOL_ID_TCP_IP));
			continue;
		}

		if( p_net_addr_oid->AddressLength != NETWORK_ADDRESS_LENGTH_IP)
		{
			IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_OID,
				("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Address %d is wrong size of %d, "
					"should be %d\n", port_num, i, p_net_addr_oid->AddressLength,
					NETWORK_ADDRESS_LENGTH_IP));
			continue;
		}

		p_ip_addr = (PNETWORK_ADDRESS_IP)p_net_addr_oid->Address;

		/* Size the vector as needed. */
		if( cl_vector_get_size( &p_adapter->ip_vector ) <= idx )
			cl_vector_set_size( &p_adapter->ip_vector, idx + 1 );

		p_addr_item = cl_vector_get_ptr( &p_adapter->ip_vector, idx );
		if( !cl_memcmp( &p_ip_addr->in_addr, &p_addr_item->address.as_ulong,
			sizeof(ULONG) ) )
		{
			idx++;
			/* Already have this address - no change needed */
			continue;
		}

		/*
		 * Copy the address information, but don't register yet - the port
		 * could be down.
		 */
		if( p_addr_item->p_reg )
		{
			/* If in use by some other address, deregister. */
			if( p_addr_item->p_reg->h_reg_svc )
			{
				p_adapter->p_ifc->dereg_svc(
					p_addr_item->p_reg->h_reg_svc, __ipoib_ats_dereg_cb );
			}
			else
			{
				cl_free( p_addr_item->p_reg );
			}
			p_addr_item->p_reg = NULL;
		}
		memcpy ((void *)&p_addr_item->address.as_ulong, (const void *)&p_ip_addr->in_addr, sizeof(ULONG) );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Adding Address %d.%d.%d.%d\n",
			port_num,
			p_addr_item->address.as_bytes[0],
			p_addr_item->address.as_bytes[1],
			p_addr_item->address.as_bytes[2],
			p_addr_item->address.as_bytes[3]) );
		idx++;
	}

	/* Now clear any extra entries that shouldn't be there. */
	while( idx < cl_vector_get_size( &p_adapter->ip_vector ) )
	{
		p_addr_item = (net_address_item_t*)
			cl_vector_get_ptr( &p_adapter->ip_vector,
			cl_vector_get_size( &p_adapter->ip_vector ) - 1 );

		if( p_addr_item->p_reg )
		{
			if( p_addr_item->p_reg->h_reg_svc )
			{
				p_adapter->p_ifc->dereg_svc(
					p_addr_item->p_reg->h_reg_svc, __ipoib_ats_dereg_cb );
			}
			else
			{
				cl_free( p_addr_item->p_reg );
			}
			p_addr_item->p_reg = NULL;
			p_addr_item->address.as_ulong = 0;
		}

		/* No need to check return value - shrinking always succeeds. */
		cl_vector_set_size( &p_adapter->ip_vector,
			cl_vector_get_size( &p_adapter->ip_vector ) - 1 );
	}

	if( p_adapter->state == IB_PNP_PORT_ACTIVE && p_adapter->packet_filter )
		ipoib_reg_addrs( p_adapter );

	cl_obj_unlock( &p_adapter->obj );

	IPOIB_EXIT( IPOIB_DBG_OID );
	return NDIS_STATUS_SUCCESS;
}


/* Object lock is held when this function is called. */
void
ipoib_reg_addrs(
	IN				ipoib_adapter_t* const		p_adapter )
{
	net_address_item_t		*p_addr_item;

	size_t					idx;

	uint8_t					port_num;

	ib_api_status_t			ib_status;
	ib_reg_svc_req_t		ib_service;
	ib_gid_t				port_gid;

	IPOIB_ENTER( IPOIB_DBG_OID );

	if(p_adapter->guids.port_guid.pkey != IB_DEFAULT_PKEY)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR,
		("ATS Service available for default pkey only\n"));      
		return;
	}
	port_num = p_adapter->guids.port_num;

	/* Setup our service call with things common to all calls */
	cl_memset( &ib_service, 0, sizeof(ib_service) );

	/* BUGBUG Only register local subnet GID prefix for now */
	ib_gid_set_default( &port_gid, p_adapter->guids.port_guid.guid );
	ib_service.svc_rec.service_gid		= port_gid;

	ib_service.svc_rec.service_pkey		= IB_DEFAULT_PKEY;
	ib_service.svc_rec.service_lease	= IB_INFINITE_SERVICE_LEASE;

	/* Must cast here because the service name is an array of unsigned chars but
	 * strcpy want a pointer to a signed char */
	if ( StringCchCopy( (char *)ib_service.svc_rec.service_name, 
		sizeof(ib_service.svc_rec.service_name) / sizeof(char), ATS_NAME ) != S_OK) {
		ASSERT(FALSE);
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR,
		("Problem copying ATS name: exiting\n"));
		return;
	}
    
	/* IP Address in question will be put in below */
	ib_service.port_guid		= p_adapter->guids.port_guid.guid;
	ib_service.timeout_ms		= p_adapter->params.sa_timeout;
	ib_service.retry_cnt		= p_adapter->params.sa_retry_cnt;

	/* Can't set IB_FLAGS_SYNC here because I can't wait at dispatch */
	ib_service.flags			= 0;

	/* Service context will be put in below */

	ib_service.svc_data_mask	= IB_SR_COMPMASK_SID		|
								  IB_SR_COMPMASK_SGID		|
								  IB_SR_COMPMASK_SPKEY		|
								  IB_SR_COMPMASK_SLEASE		|
								  IB_SR_COMPMASK_SNAME		|
								  IB_SR_COMPMASK_SDATA8_12	|
								  IB_SR_COMPMASK_SDATA8_13	|
								  IB_SR_COMPMASK_SDATA8_14	|
								  IB_SR_COMPMASK_SDATA8_15;
	ib_service.pfn_reg_svc_cb = __ipoib_ats_reg_cb;

	for( idx = 0; idx < cl_vector_get_size( &p_adapter->ip_vector); idx++ )
	{
		p_addr_item = (net_address_item_t*)
			cl_vector_get_ptr(  &p_adapter->ip_vector, idx );

		if( p_addr_item->p_reg )
			continue;

		p_addr_item->p_reg = cl_zalloc( sizeof(ats_reg_t) );
		if( !p_addr_item->p_reg )
			break;

		p_addr_item->p_reg->p_adapter = p_adapter;

		ib_service.svc_context		= p_addr_item->p_reg;

		ib_service.svc_rec.service_id =
			ATS_SERVICE_ID & CL_HTON64(0xFFFFFFFFFFFFFF00);
		/* ATS service IDs start at 0x10000CE100415453 */
		ib_service.svc_rec.service_id |= ((uint64_t)(idx + 0x53)) << 56;

		cl_memcpy( &ib_service.svc_rec.service_data8[ATS_IPV4_OFFSET],
			p_addr_item->address.as_bytes, IPV4_ADDR_SIZE );

		/* Take a reference for each service request. */
		cl_obj_ref(&p_adapter->obj);
		ib_status = p_adapter->p_ifc->reg_svc(
			p_adapter->h_al, &ib_service, &p_addr_item->p_reg->h_reg_svc );
		if( ib_status != IB_SUCCESS )
		{
			if( ib_status == IB_INVALID_GUID )
			{
				/* If this occurs, we log the error but do not fail the OID yet */
				IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_OID,
					("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - "
					"Failed to register IP Address "
					"of %d.%d.%d.%d with error IB_INVALID_GUID\n",
					port_num,
					p_addr_item->address.as_bytes[0],
					p_addr_item->address.as_bytes[1],
					p_addr_item->address.as_bytes[2],
					p_addr_item->address.as_bytes[3]) );
			}
			else
			{
				/* Fatal error. */
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Failed to register IP Address "
					"of %d.%d.%d.%d with error %s\n",
					port_num,
					p_addr_item->address.as_bytes[0],
					p_addr_item->address.as_bytes[1],
					p_addr_item->address.as_bytes[2],
					p_addr_item->address.as_bytes[3],
					p_adapter->p_ifc->get_err_str( ib_status )) );
				p_adapter->hung = TRUE;
			}
			cl_obj_deref(&p_adapter->obj);
			cl_free( p_addr_item->p_reg );
			p_addr_item->p_reg = NULL;
		}
	}

	IPOIB_EXIT( IPOIB_DBG_OID );
}


/* Object lock is held when this function is called. */
void
ipoib_dereg_addrs(
	IN				ipoib_adapter_t* const		p_adapter )
{
	net_address_item_t		*p_addr_item;

	size_t					idx;

	IPOIB_ENTER( IPOIB_DBG_OID );

	for( idx = 0; idx < cl_vector_get_size( &p_adapter->ip_vector); idx++ )
	{
		p_addr_item = (net_address_item_t*)
			cl_vector_get_ptr( &p_adapter->ip_vector, idx );

		if( !p_addr_item->p_reg )
			continue;

		if( p_addr_item->p_reg->h_reg_svc )
		{
			p_adapter->p_ifc->dereg_svc(
				p_addr_item->p_reg->h_reg_svc, __ipoib_ats_dereg_cb );
		}
		else
		{
			cl_free( p_addr_item->p_reg );
		}
		p_addr_item->p_reg = NULL;
	}

	IPOIB_EXIT( IPOIB_DBG_OID );
}


static void
__ipoib_ats_reg_cb(
	IN				ib_reg_svc_rec_t			*p_reg_svc_rec )
{
	ats_reg_t				*p_reg;
	uint8_t					port_num;

	IPOIB_ENTER( IPOIB_DBG_OID );

	CL_ASSERT( p_reg_svc_rec );
	CL_ASSERT( p_reg_svc_rec->svc_context );

	p_reg = (ats_reg_t*)p_reg_svc_rec->svc_context;
	port_num = p_reg->p_adapter->guids.port_num;

	cl_obj_lock( &p_reg->p_adapter->obj );

	if( p_reg_svc_rec->req_status == IB_SUCCESS &&
		!p_reg_svc_rec->resp_status )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
					 ("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Registered IP Address "
					  "of %d.%d.%d.%d\n",
					  port_num,
					  p_reg_svc_rec->svc_rec.service_data8[ATS_IPV4_OFFSET],
					  p_reg_svc_rec->svc_rec.service_data8[ATS_IPV4_OFFSET+1],
					  p_reg_svc_rec->svc_rec.service_data8[ATS_IPV4_OFFSET+2],
					  p_reg_svc_rec->svc_rec.service_data8[ATS_IPV4_OFFSET+3]) );
	}
	else if( p_reg_svc_rec->req_status != IB_CANCELED )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_OID,
					 ("Port %d OID_GEN_NETWORK_LAYER_ADDRESSES - Failed to register IP Address "
					  "of %d.%d.%d.%d with error %s\n",
					  port_num,
					  p_reg_svc_rec->svc_rec.service_data8[ATS_IPV4_OFFSET],
					  p_reg_svc_rec->svc_rec.service_data8[ATS_IPV4_OFFSET+1],
					  p_reg_svc_rec->svc_rec.service_data8[ATS_IPV4_OFFSET+2],
					  p_reg_svc_rec->svc_rec.service_data8[ATS_IPV4_OFFSET+3],
					  p_reg->p_adapter->p_ifc->get_err_str( p_reg_svc_rec->resp_status )) );
		p_reg->p_adapter->hung = TRUE;
		p_reg->h_reg_svc = NULL;
	}

	cl_obj_unlock( &p_reg->p_adapter->obj );
	cl_obj_deref(&p_reg->p_adapter->obj);

	IPOIB_EXIT( IPOIB_DBG_OID );
}


static void
__ipoib_ats_dereg_cb(
	IN				void						*context )
{
	cl_free( context );
}
