/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *	   Redistribution and use in source and binary forms, with or
 *	   without modification, are permitted provided that the following
 *	   conditions are met:
 *
 *		- Redistributions of source code must retain the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer.
 *
 *		- Redistributions in binary form must reproduce the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer in the documentation and/or other materials
 *		  provided with the distribution.
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

#include "Precompile.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_driver.tmh"
#endif

#include "ipoib_port.h"
#include <complib/cl_bus_ifc.h>
#include <complib/cl_init.h>
#include <initguid.h>
#include <iba/ipoib_ifc.h>
#include <offload.h>
#include "iba\ndk_ifc.h"

#define MAJOR_DRIVER_VERSION 2
#define MINOR_DRIVER_VERSION 1

#define MAJOR_NDIS_VERSION 6

#if defined(NDIS630_MINIPORT)
#define MINOR_NDIS_VERSION 30
#else
#define MINOR_NDIS_VERSION 1
#endif

#define LSO_MIN_SEG_COUNT 2

PDRIVER_OBJECT				g_p_drv_obj;


static const NDIS_OID SUPPORTED_OIDS[] =
{
	OID_GEN_SUPPORTED_LIST,
	OID_GEN_HARDWARE_STATUS,
	OID_GEN_MEDIA_SUPPORTED,
	OID_GEN_MEDIA_IN_USE,
	OID_GEN_MAXIMUM_LOOKAHEAD,
	OID_GEN_MAXIMUM_FRAME_SIZE,
	OID_GEN_TRANSMIT_BUFFER_SPACE,
	OID_GEN_RECEIVE_BUFFER_SPACE,
	OID_GEN_TRANSMIT_BLOCK_SIZE,
	OID_GEN_RECEIVE_BLOCK_SIZE,
	OID_GEN_VENDOR_ID,
	OID_GEN_VENDOR_DESCRIPTION,
	OID_GEN_VENDOR_DRIVER_VERSION,
	OID_GEN_CURRENT_PACKET_FILTER,
	OID_GEN_CURRENT_LOOKAHEAD,
	OID_GEN_DRIVER_VERSION,
	OID_GEN_MAXIMUM_TOTAL_SIZE,
	OID_GEN_MAC_OPTIONS,
	OID_GEN_MAXIMUM_SEND_PACKETS,
	OID_GEN_XMIT_OK,
	OID_GEN_RCV_OK,
	OID_GEN_XMIT_ERROR,
	OID_GEN_RCV_ERROR,
	OID_GEN_RCV_NO_BUFFER,
	//OID_GEN_RCV_CRC_ERROR,
	//OID_GEN_TRANSMIT_QUEUE_LENGTH,
	OID_802_3_PERMANENT_ADDRESS,
	OID_802_3_CURRENT_ADDRESS,
	OID_802_3_MULTICAST_LIST,
	OID_802_3_MAXIMUM_LIST_SIZE,
	OID_802_3_RCV_ERROR_ALIGNMENT,
	OID_802_3_XMIT_ONE_COLLISION,
	OID_802_3_XMIT_MORE_COLLISIONS,
	//OID_802_3_XMIT_DEFERRED,
	//OID_802_3_XMIT_MAX_COLLISIONS,
	//OID_802_3_RCV_OVERRUN,
	//OID_802_3_XMIT_UNDERRUN,
	//OID_802_3_XMIT_HEARTBEAT_FAILURE,
	//OID_802_3_XMIT_TIMES_CRS_LOST,
	//OID_802_3_XMIT_LATE_COLLISIONS,

#if !BUILD_W2K
	OID_GEN_PHYSICAL_MEDIUM,
#endif

	OID_TCP_TASK_OFFLOAD,
	
/* powermanagement */

	OID_PNP_CAPABILITIES,
	OID_PNP_SET_POWER,
	OID_PNP_QUERY_POWER,
	OID_PNP_ADD_WAKE_UP_PATTERN,
	OID_PNP_REMOVE_WAKE_UP_PATTERN,
	OID_PNP_ENABLE_WAKE_UP,

#if 0
/* custom oid WMI support */
	OID_CUSTOM_PERF_COUNTERS,
	OID_CUSTOM_STRING,
#endif

	OID_GEN_RECEIVE_SCALE_CAPABILITIES,
	OID_GEN_RECEIVE_SCALE_PARAMETERS,
   

//
// new and required for NDIS 6 miniports
//
	OID_GEN_LINK_PARAMETERS,
	OID_GEN_INTERRUPT_MODERATION,
	OID_GEN_STATISTICS,

/* Offload */
	OID_TCP_OFFLOAD_CURRENT_CONFIG,
	OID_TCP_OFFLOAD_PARAMETERS,
	OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES,
	OID_OFFLOAD_ENCAPSULATION,
	
#if 0

/* Header - Data separation */
	OID_GEN_HD_SPLIT_PARAMETERS,
	OID_GEN_HD_SPLIT_CURRENT_CONFIG,

/* VLAN */
	OID_ADD_VALN_ID,
	OID_DELETE_VLAN_ID,


/* Set MAC */
	OID_SET_MAC_ADDRESS
#endif

#if defined(NDIS630_MINIPORT)
	//
	// Ndis 6.30
	//
	OID_NDK_SET_STATE,
	OID_NDK_STATISTICS,
	OID_NDK_CONNECTIONS,
	OID_NDK_LOCAL_ENDPOINTS
	
#endif

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
NDIS_HANDLE		g_IpoibMiniportDriverHandle = NULL;
NDIS_HANDLE		g_IpoibDriverContext = NULL;
ULONG			g_ipoib_send	= 0;
ULONG			g_ipoib_send_ack	= 0;
ULONG			g_ipoib_send_SW	= 0;
ULONG			g_ipoib_send_SG	= 0;
ULONG			g_ipoib_send_SW_in_loop = 0;
ULONG			g_ipoib_send_SG_pending = 0;
ULONG			g_ipoib_send_SG_real = 0;
ULONG			g_ipoib_send_SG_failed = 0;
ULONG			g_ipoib_send_reset = 0;

ULONG			g_NBL = 0;
ULONG			g_NBL_complete = 0;
ULONG			g_reset = 0;
ULONG 			g_reset_complete = 0;


typedef struct _IPOIB_REG_ENTRY
{
	NDIS_STRING RegName;				// variable name text
	BOOLEAN 	bRequired;				// 1 -> required, 0 -> optional
	UINT		FieldOffset;			// offset in parent struct
	UINT		FieldSize;				// size (in bytes) of the field
	UINT		Default;				// default value to use
	UINT		Min;					// minimum value allowed
	UINT		Max;					// maximum value allowed
} IPOIB_REG_ENTRY, *PIPOIB_REG_ENTRY;

IPOIB_REG_ENTRY HCARegTable[] = {
	// reg value name			  If Required  Offset in parentr struct 			Field size					Default 		Min 	Max
	{NDIS_STRING_CONST("GUIDMask"), 		0, IPOIB_OFFSET(guid_mask), 			IPOIB_SIZE(guid_mask),			0,			0,	  MAX_GUID_MAX},
	/* GUIDMask should be the first element */
	{NDIS_STRING_CONST("RqDepth"),			1, IPOIB_OFFSET(rq_depth),				IPOIB_SIZE(rq_depth),			512,		128,	1024},
	{NDIS_STRING_CONST("RqLowWatermark"),	0, IPOIB_OFFSET(rq_low_watermark),		IPOIB_SIZE(rq_low_watermark),	4,			2,		8},
	{NDIS_STRING_CONST("SqDepth"),			1, IPOIB_OFFSET(sq_depth),				IPOIB_SIZE(sq_depth),			512,		128,	1024},
	{NDIS_STRING_CONST("SendChksum"),		1, IPOIB_OFFSET(send_chksum_offload),	IPOIB_SIZE(send_chksum_offload),CSUM_ENABLED,CSUM_DISABLED,CSUM_BYPASS},
	{NDIS_STRING_CONST("RecvChksum"),		1, IPOIB_OFFSET(recv_chksum_offload),	IPOIB_SIZE(recv_chksum_offload),CSUM_ENABLED,CSUM_DISABLED,CSUM_BYPASS},
	{NDIS_STRING_CONST("SaTimeout"),		1, IPOIB_OFFSET(sa_timeout),			IPOIB_SIZE(sa_timeout), 		1000,		250,	UINT_MAX},
	{NDIS_STRING_CONST("SaRetries"),		1, IPOIB_OFFSET(sa_retry_cnt),			IPOIB_SIZE(sa_retry_cnt),		10, 		1,		UINT_MAX},
	{NDIS_STRING_CONST("RecvRatio"),		1, IPOIB_OFFSET(recv_pool_ratio),		IPOIB_SIZE(recv_pool_ratio),	1,			1,		10},
	{NDIS_STRING_CONST("PayloadMtu"),		1, IPOIB_OFFSET(payload_mtu),			IPOIB_SIZE(payload_mtu),		2044,		600,   MAX_UD_PAYLOAD_MTU},
	{NDIS_STRING_CONST("lso"),				0, IPOIB_OFFSET(lso),					IPOIB_SIZE(lso),				0,			0,		1},
	{NDIS_STRING_CONST("MCLeaveRescan"),	1, IPOIB_OFFSET(mc_leave_rescan),		IPOIB_SIZE(mc_leave_rescan),	260,		1,	  3600},
	{NDIS_STRING_CONST("BCJoinRetry"),		1, IPOIB_OFFSET(bc_join_retry),			IPOIB_SIZE(bc_join_retry),		50, 		0,	  1000},
	{NDIS_STRING_CONST("CmEnabled"),		0, IPOIB_OFFSET(cm_enabled),			IPOIB_SIZE(cm_enabled), 		FALSE,	   FALSE, TRUE},
	{NDIS_STRING_CONST("CmPayloadMtu"), 	1, IPOIB_OFFSET(cm_payload_mtu),		IPOIB_SIZE(cm_payload_mtu), 	MAX_CM_PAYLOAD_MTU, 512, MAX_CM_PAYLOAD_MTU},
	{NDIS_STRING_CONST("*LsoV1IPv4"),		0, IPOIB_OFFSET(LsoV1IPv4), 			IPOIB_SIZE(LsoV1IPv4),			1,			0,	  1},
	{NDIS_STRING_CONST("*LsoV2IPv4"),		0, IPOIB_OFFSET(LsoV2IPv4), 			IPOIB_SIZE(LsoV2IPv4),			1,			0,	  1},
	{NDIS_STRING_CONST("*LsoV2IPv6"),		0, IPOIB_OFFSET(LsoV2IPv6), 			IPOIB_SIZE(LsoV2IPv6),			1,			0,	  1},
};

#define IPOIB_NUM_REG_PARAMS (sizeof (HCARegTable) / sizeof(IPOIB_REG_ENTRY))


void
ipoib_create_log(
	NDIS_HANDLE h_adapter,
	UINT ind,
	ULONG eventLogMsgId)

{
#define cMaxStrLen	40
#define cArrLen  3

	PWCHAR logMsgArray[cArrLen]; 
	WCHAR strVal[cMaxStrLen];
	NDIS_STRING AdapterInstanceName;

	IPOIB_INIT_NDIS_STRING(&AdapterInstanceName);
	if (NdisMQueryAdapterInstanceName(&AdapterInstanceName, h_adapter)!= NDIS_STATUS_SUCCESS ){
		ASSERT(FALSE);
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR,
			("[IPoIB] Init:Failed to retreive adapter name.\n"));
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

extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C"
NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT				p_drv_obj,
	IN				PUNICODE_STRING				p_reg_path );
#ifdef _DEBUG_
#pragma NDIS_PAGEABLE_FUNCTION(DriverEntry)
#endif


VOID
ipoib_unload(
	IN				PDRIVER_OBJECT				p_drv_obj );

#ifdef _DEBUG_
extern "C"
#endif
NDIS_STATUS
ipoib_initialize_ex(
	IN				NDIS_HANDLE			h_adapter,
	IN				NDIS_HANDLE 		config_context,
	IN PNDIS_MINIPORT_INIT_PARAMETERS	MiniportInitParameters);
#ifdef _DEBUG_
#pragma NDIS_PAGEABLE_FUNCTION(ipoib_initialize_ex)
#endif

BOOLEAN
ipoib_check_for_hang(
	IN				NDIS_HANDLE					adapter_context );

void
ipoib_halt_ex(
	IN NDIS_HANDLE	adapter_context,
	IN				NDIS_HALT_ACTION			HaltAction);

NDIS_STATUS
ipoib_query_info(
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest);

NDIS_STATUS
ipoib_direct_query_info(
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest);

NDIS_STATUS
ipoib_reset(
	IN	NDIS_HANDLE 	adapter_context,
	OUT PBOOLEAN		p_addr_reset);

NDIS_STATUS
ipoib_set_info(
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest);


//NDIS60
void
ipoib_send_net_buffer_list(
	IN	NDIS_HANDLE 		adapter_context,
	IN	PNET_BUFFER_LIST	net_buffer_list,
	IN	NDIS_PORT_NUMBER	port_num,
	IN	ULONG				send_flags);

void
ipoib_pnp_notify(
	IN				NDIS_HANDLE					adapter_context,
	IN PNET_DEVICE_PNP_EVENT  pnp_event);

VOID
ipoib_shutdown_ex(
	IN NDIS_HANDLE	adapter_context,
	IN NDIS_SHUTDOWN_ACTION  shutdown_action);


void
ipoib_cancel_xmit(
	IN				NDIS_HANDLE					adapter_context,
	IN				PVOID						cancel_id );


static NDIS_STATUS
ipoib_complete_query(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				pending_oid_t* const		p_oid_info,
	IN		const	NDIS_STATUS					status,
	IN		const	void* const					p_buf,
	IN		const	ULONG						buf_len );

static NDIS_STATUS
__ipoib_get_tcp_task_offload(
	IN				ipoib_adapter_t*			p_adapter,
	OUT				pending_oid_t				*pNdisRequest);

static void
__ipoib_ats_reg_cb(
	IN				ib_reg_svc_rec_t			*p_reg_svc_rec );

static void
__ipoib_ats_dereg_cb(
	IN				void						*context );

static NTSTATUS
__ipoib_read_registry(
	IN				UNICODE_STRING* const		p_registry_path );

static NDIS_STATUS
ipoib_set_options(
	IN NDIS_HANDLE	NdisMiniportDriverHandle,
	IN NDIS_HANDLE	MiniportDriverContext);

static NDIS_STATUS
ipoib_oid_handler(
	IN	NDIS_HANDLE 		adapter_context,
	IN	PNDIS_OID_REQUEST	pNdisRequest);

static NDIS_STATUS
ipoib_direct_oid_handler(
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest);

static void
ipoib_cancel_oid_request(
	IN	NDIS_HANDLE 		   adapter_context,
	IN	PVOID				   requestId);

static void
ipoib_cancel_direct_oid_request(
	IN	NDIS_HANDLE 		   adapter_context,
	IN	PVOID				   requestId);

static NDIS_STATUS 
ipoib_pause(
	IN	NDIS_HANDLE 						adapter_context,	
	IN	PNDIS_MINIPORT_PAUSE_PARAMETERS 	pause_parameters);

static NDIS_STATUS 
ipoib_restart(
	IN	NDIS_HANDLE 						adapter_context,	
	IN	PNDIS_MINIPORT_RESTART_PARAMETERS	restart_parameters);



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
extern "C" 
NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT				p_drv_obj,
	IN				PUNICODE_STRING				p_registry_path )
{
	NDIS_STATUS						status;
	NDIS_MINIPORT_DRIVER_CHARACTERISTICS characteristics;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	g_p_drv_obj = p_drv_obj;

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

	__ipoib_read_registry(p_registry_path);
	
	KeInitializeSpinLock( &g_ipoib.lock );
	cl_qlist_init( &g_ipoib.adapter_list );
	ipoib_st_init();
	g_stat.drv.obj = p_drv_obj;

	memset(&characteristics, 0, sizeof(characteristics));

	characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
	characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
	characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;

	characteristics.MajorNdisVersion		= MAJOR_NDIS_VERSION;
	characteristics.MinorNdisVersion		= MINOR_NDIS_VERSION;
	characteristics.MajorDriverVersion		= MAJOR_DRIVER_VERSION;
	characteristics.MinorDriverVersion		= MINOR_DRIVER_VERSION;

	characteristics.CheckForHangHandlerEx		= ipoib_check_for_hang;
	characteristics.HaltHandlerEx				= ipoib_halt_ex;
	characteristics.InitializeHandlerEx 		= ipoib_initialize_ex;
	characteristics.OidRequestHandler			= ipoib_oid_handler;
	characteristics.CancelOidRequestHandler 	= ipoib_cancel_oid_request;
#if (NDIS_SUPPORT_NDIS61)
	characteristics.DirectOidRequestHandler			= ipoib_direct_oid_handler;
	characteristics.CancelDirectOidRequestHandler	= ipoib_cancel_direct_oid_request;
#endif // (NDIS_SUPPORT_NDIS61)
	characteristics.ResetHandlerEx				= ipoib_reset;
	characteristics.DevicePnPEventNotifyHandler	= ipoib_pnp_notify;
	characteristics.ReturnNetBufferListsHandler	= ipoib_return_net_buffer_list;
	characteristics.SendNetBufferListsHandler	= ipoib_send_net_buffer_list;

	characteristics.SetOptionsHandler			= ipoib_set_options;
	characteristics.PauseHandler				= ipoib_pause;
	characteristics.RestartHandler				= ipoib_restart;
	characteristics.UnloadHandler				= ipoib_unload;
	characteristics.CancelSendHandler			= ipoib_cancel_xmit;
	characteristics.ShutdownHandlerEx			= ipoib_shutdown_ex;

	status = NdisMRegisterMiniportDriver( p_drv_obj,
										  p_registry_path,
										  (PNDIS_HANDLE)&g_IpoibDriverContext, 
										  &characteristics,
										  &g_IpoibMiniportDriverHandle );
	if( status != NDIS_STATUS_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
			("NdisMRegisterMiniportDriver failed with status of %d\n", status) );
		CL_DEINIT;
	}

#if defined(NDIS630_MINIPORT)
	ndk_create( p_drv_obj, p_registry_path, g_IpoibMiniportDriverHandle );
#endif

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}

static NDIS_STATUS
ipoib_set_options(
	IN NDIS_HANDLE	NdisMiniportDriverHandle,
	IN NDIS_HANDLE	MiniportDriverContext
	)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	UNREFERENCED_PARAMETER(NdisMiniportDriverHandle);
	UNREFERENCED_PARAMETER(MiniportDriverContext);

#if defined(NDIS630_MINIPORT)

	// add NDK callbacks
	status = ndk_prov_set_handlers( NdisMiniportDriverHandle );
	
	if ( !NT_SUCCESS(status) )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ndk_prov_set_handlers failed with status 0x%.8x.\n", status ) );
	}

#endif	 

	IPOIB_EXIT( IPOIB_DBG_INIT );
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
	param_path.Buffer = (PWCH) cl_zalloc( param_path.MaximumLength );
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
	memset( table, 0, sizeof(table) );

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
	UNREFERENCED_PARAMETER(p_drv_obj);

	#if defined(EVENT_TRACING)
	WPP_CLEANUP(p_drv_obj);
	#endif

	//NDIS6.0
	NdisMDeregisterMiniportDriver(g_IpoibMiniportDriverHandle);
	UNREFERENCED_PARAMETER( p_drv_obj );

#if defined(NDIS630_MINIPORT)
	ndk_delete();
#endif

	CL_DEINIT;
	IPOIB_EXIT( IPOIB_DBG_INIT );
}



NDIS_STATUS
ipoib_get_adapter_params(
	IN	OUT			ipoib_adapter_t				*p_adapter,
	OUT				PUCHAR						*p_mac,
	OUT				UINT						*p_len)
{
	NDIS_STATUS						status;
	NDIS_HANDLE						h_config;
	NDIS_CONFIGURATION_OBJECT		config_obj;
	NDIS_CONFIGURATION_PARAMETER	*p_param;
	UINT							value;
	PIPOIB_REG_ENTRY				pRegEntry;
	UINT							i;
	PUCHAR							structPointer;
	
	int sq_depth_step = 128;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	config_obj.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
	config_obj.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
	config_obj.Header.Size = sizeof(NDIS_CONFIGURATION_OBJECT);
	config_obj.NdisHandle = p_adapter->h_adapter;
	config_obj.Flags = 0;

	status = NdisOpenConfigurationEx( &config_obj, &h_config);
	if( status != NDIS_STATUS_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisOpenConfigurationEx returned 0x%.8x\n", status) );
		return status;
	}

	// read all the registry values 
	for (i = 0, pRegEntry = HCARegTable; i < IPOIB_NUM_REG_PARAMS; ++i)
	{
		// initialize pointer to appropriate place inside 'params'
		structPointer = (PUCHAR) &p_adapter->params + pRegEntry[i].FieldOffset;

		// Get the configuration value for a specific parameter. Under NT the
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
				ipoib_create_log(p_adapter->h_adapter, i,
									EVENT_IPOIB_WRONG_PARAMETER_WRN);
				IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_INIT,
					("Read configuration.Registry %S value is out of range, "
					 "setting default value= 0x%x\n",
					pRegEntry[i].RegName.Buffer, value));
			}
			else
			{
				value = p_param->ParameterData.IntegerData;
				IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_INIT,
					("Read configuration. Registry %S, Value= 0x%x\n",
					pRegEntry[i].RegName.Buffer, value));
			}
		}
		else
		{
			value = pRegEntry[i].Default;
			status = NDIS_STATUS_SUCCESS;
			if (pRegEntry[i].bRequired)
			{
				ipoib_create_log(p_adapter->h_adapter, i,
									EVENT_IPOIB_WRONG_PARAMETER_ERR);
				IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_INIT,
					("Read configuration.Registry %S value not found, setting "
					 "default value= 0x%x\n",
					pRegEntry[i].RegName.Buffer, value));
			}
			else
			{
				ipoib_create_log(p_adapter->h_adapter, i, EVENT_IPOIB_WRONG_PARAMETER_INFO);
				IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_INIT,
					("Read configuration. Registry %S value not found, Value 0x%x\n",
						pRegEntry[i].RegName.Buffer, value));
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
				IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Bogus field size %d\n", pRegEntry[i].FieldSize));
				break;
		}
	}

	// Send queue depth needs to be a power of two
	//static const INT sq_depth_step = 128;

	if (p_adapter->params.sq_depth % sq_depth_step) {
		static const c_sq_ind = 2;
		p_adapter->params.sq_depth = sq_depth_step *
			(p_adapter->params.sq_depth / sq_depth_step +
			!!((p_adapter->params.sq_depth % sq_depth_step) > (sq_depth_step/2)));
		ipoib_create_log(p_adapter->h_adapter, c_sq_ind,
							EVENT_IPOIB_WRONG_PARAMETER_WRN);
		IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_INIT,
			("SQ DEPTH value was rounded to the closest acceptable value "
			 "of 0x%x\n", p_adapter->params.sq_depth ));
	}

#if FORCE_CM_MODE
	if( p_adapter->params.cm_enabled == 0 )
	{ /* XXX FIXME ASAP - FORCE CM mode */
		p_adapter->params.cm_enabled  = 1;
	}
#endif

	if (p_adapter->params.lso) {
		if (!p_adapter->params.LsoV1IPv4 && 
			!p_adapter->params.LsoV2IPv4 && 
			!p_adapter->params.LsoV2IPv6) {
			//
			// LSO option is set but all the well known keywords are disabled. 
			// Disable LSO
			//
			p_adapter->params.lso = false;
		}
	} else {
		//
		// The LSO option is disbled. Disabled all well known keywords
		//
		p_adapter->params.LsoV1IPv4 = false;
		p_adapter->params.LsoV2IPv4 = false;
		p_adapter->params.LsoV2IPv6 = false;
	}
	
	// Adjusting the low watermark parameter
	p_adapter->params.rq_low_watermark =
			p_adapter->params.rq_depth / p_adapter->params.rq_low_watermark;

	if( p_adapter->params.cm_enabled )
	{
		/* disable LSO if CM is active */
		if( p_adapter->params.lso )
		{
			NdisWriteErrorLogEntry( p_adapter->h_adapter,
					EVENT_IPOIB_CM_LSO_DISABLED, 1, 0xbadc0de0 );
			p_adapter->params.lso = 0;
			p_adapter->params.LsoV1IPv4 = false;
			p_adapter->params.LsoV2IPv4 = false;
			p_adapter->params.LsoV2IPv6 = false;
		}

		/* disable Send & Recv checksum offload if CM is active */
		if( p_adapter->params.send_chksum_offload != CSUM_DISABLED )
			p_adapter->params.send_chksum_offload = CSUM_DISABLED;

		if( p_adapter->params.recv_chksum_offload != CSUM_DISABLED )
			p_adapter->params.recv_chksum_offload = CSUM_DISABLED;

		p_adapter->params.rq_low_watermark *= 2;
		p_adapter->params.cm_xfer_block_size = 
			(sizeof(eth_hdr_t) + p_adapter->params.cm_payload_mtu);
	}
	
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
		("CM_enabled[%s] CM_mtu %u Depth: ud_sq %u ud_rq %u low_water %u\n",
			(p_adapter->params.cm_enabled ? "True" : "False"),
			p_adapter->params.cm_payload_mtu,
			p_adapter->params.sq_depth,
			p_adapter->params.rq_depth,
			p_adapter->params.rq_low_watermark) );

	p_adapter->params.xfer_block_size = 
			(sizeof(eth_hdr_t) + p_adapter->params.payload_mtu);

	NdisReadNetworkAddress( &status, (PVOID *) p_mac, p_len, h_config );
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
		p_adapter->p_ifc->wdm.InterfaceDereference( p_adapter->p_ifc->wdm.Context );

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return NDIS_STATUS_SUCCESS;
}


//! Initialization function called for each IOC discovered
/*	The MiniportInitialize function is a required function that sets up a
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

/*void foo1(int i)
{
		char temp[5200];
		if (i ==0) return;
		cl_msg_out("i = %d\n", i);
		foo1(i-1);
	 
}*/
	
NDIS_STATUS
SetDeviceRegistrationAttributes(
	ipoib_adapter_t *p_adapter,
	NDIS_HANDLE	h_adapter )
{
	NDIS_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES atr;
	NTSTATUS Status;

	memset(&atr, 0, sizeof(NDIS_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES));

	//
	// setting registration attributes
	//
	atr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES;
	atr.Header.Revision = NDIS_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES_REVISION_1;
	atr.Header.Size = NDIS_SIZEOF_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES_REVISION_1;


	atr.MiniportAddDeviceContext = (NDIS_HANDLE)p_adapter;
	atr.Flags = 0; 

	Status = NdisMSetMiniportAttributes(h_adapter,
					(PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&atr);

	return Status;
}

//NDIS 6.1
#if 0
NDIS_STATUS
SetHardwareAssistAttributes(
	ipoib_adapter_t *p_adapter,
	NDIS_HANDLE	h_adapter
	)
{
	NDIS_MINIPORT_ADAPTER_HARDWARE_ASSIST_ATTRIBUTES atr;
	NTSTATUS Status;

	memset(&atr, 0, sizeof(NDIS_MINIPORT_ADAPTER_HARDWARE_ASSIST_ATTRIBUTES));

	//
	// setting registration attributes
	//
	atr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_HARDWARE_ASSIST_ATTRIBUTES;
	atr.Header.Revision = NDIS_MINIPORT_ADAPTER_HARDWARE_ASSIST_ATTRIBUTES_REVISION_1;
	atr.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_HARDWARE_ASSIST_ATTRIBUTES_REVISION_1;

	NDIS_HD_SPLIT_ATTRIBUTES nhsa;
	memset(&nhsa, 0, sizeof(nhsa));

	nhsa.Header.Type = NDIS_OBJECT_TYPE_HD_SPLIT_ATTRIBUTES;
	nhsa.Header.Revision = NDIS_OFFLOAD_REVISION_1;
	nhsa.Header.Size = NDIS_SIZEOF_HD_SPLIT_ATTRIBUTES_REVISION_1;

	// BUGBUG: We are just cheating here ...
	nhsa.HardwareCapabilities = NDIS_HD_SPLIT_CAPS_SUPPORTS_HEADER_DATA_SPLIT;
#if 0
	... Only supported on B0

								 NDIS_HD_SPLIT_CAPS_SUPPORTS_IPV4_OPTIONS |
								 NDIS_HD_SPLIT_CAPS_SUPPORTS_IPV6_EXTENSION_HEADERS |
								 NDIS_HD_SPLIT_CAPS_SUPPORTS_TCP_OPTIONS;
#endif

	// The bellow should be left zero
	if (pPort->Config.HeaderDataSplit) {
		nhsa.CurrentCapabilities = NDIS_HD_SPLIT_CAPS_SUPPORTS_HEADER_DATA_SPLIT;
	} else {
		nhsa.CurrentCapabilities = 0;
	}

	nhsa.HDSplitFlags = 0;
	nhsa.BackfillSize = 0;
	nhsa.MaxHeaderSize = 0;    

	atr.HDSplitAttributes = &nhsa;

	Status = NdisMSetMiniportAttributes(h_adapter,
					(PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&atr);

	if (nhsa.HDSplitFlags & NDIS_HD_SPLIT_ENABLE_HEADER_DATA_SPLIT) {
		ASSERT(pPort->Config.HeaderDataSplit == TRUE);
		pPort->Config.HeaderDataSplit = TRUE;
	} 
	else {
		ASSERT(pPort->Config.HeaderDataSplit == FALSE);
		pPort->Config.HeaderDataSplit = FALSE;
	}

	return Status;
}
#endif

/*++
Routine Description:
	the routine sets attributes that are associated with a miniport adapter.

Arguments:
	pPort - Pointer to port object

Return Value:
	NDIS_STATUS

Note:
	Should be called in PASSIVE_LEVEL
	
--*/
NDIS_STATUS
SetAdapterRegistrationAttributes(
	ipoib_adapter_t *p_adapter,
	NDIS_HANDLE	h_adapter )
{
	NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES atr;
	NTSTATUS Status;

	memset(&atr, 0, sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES));

	/* setting registration attributes */

	atr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
	atr.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
	atr.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;

	atr.MiniportAdapterContext = (NDIS_HANDLE)p_adapter; 
	atr.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
	atr.CheckForHangTimeInSeconds = 10;
	atr.InterfaceType = NdisInterfacePci ;	 // ???? UH
	//TODO NDIS60 PNP or PCI ?
	//RegistrationAttributes.InterfaceType = NdisInterfacePNPBus;

	Status = NdisMSetMiniportAttributes(h_adapter,
			(PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&atr);

	return Status;
}


/*++
Routine Description:
	the routine sets generic attributes that are associated with a miniport 
	adapter.

Arguments:
	pPort - Pointer to port object

Return Value:
	NDIS_STATUS

Note:
	Should be called in PASSIVE_LEVEL
	
--*/
NDIS_STATUS
SetGenericAttributes(
	ipoib_adapter_t *p_adapter,
	NDIS_HANDLE	h_adapter )
{
	NDIS_STATUS Status;

	NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES gat;
	memset(&gat, 0, sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES));

	/* set up generic attributes */

	gat.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
	gat.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
	gat.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);

	gat.MediaType = 		NdisMedium802_3;
	gat.MaxXmitLinkSpeed =	IPOIB_MEDIA_MAX_SPEED;
	gat.MaxRcvLinkSpeed =	IPOIB_MEDIA_MAX_SPEED;
	gat.XmitLinkSpeed =		NDIS_LINK_SPEED_UNKNOWN;
	gat.RcvLinkSpeed =		NDIS_LINK_SPEED_UNKNOWN;

	gat.MediaConnectState = MediaConnectStateConnected; //TODO NDIS60 Check the current state
	gat.MediaDuplexState = MediaDuplexStateFull;

	if( p_adapter->params.cm_enabled )
	{
		gat.MtuSize = p_adapter->params.cm_payload_mtu;
		gat.LookaheadSize = p_adapter->params.cm_xfer_block_size;
	}
	else
	{
		gat.MtuSize = p_adapter->params.payload_mtu;
		gat.LookaheadSize = MAX_XFER_BLOCK_SIZE;
	}

	gat.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | 
					 NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
					 NDIS_MAC_OPTION_NO_LOOPBACK |
					 NDIS_MAC_OPTION_FULL_DUPLEX;
					//NDIS_MAC_OPTION_8021P_PRIORITY; //TODO NDIS60
					// DT: Enable for Header Data Split WHQL
					// |  NDIS_MAC_OPTION_8021Q_VLAN;

	gat.SupportedPacketFilters =	NDIS_PACKET_TYPE_DIRECTED |
									NDIS_PACKET_TYPE_MULTICAST |
									//NDIS_PACKET_TYPE_ALL_MULTICAST |
									NDIS_PACKET_TYPE_BROADCAST;
					 
	gat.MaxMulticastListSize = MAX_MCAST;

	gat.MacAddressLength = HW_ADDR_LEN;
	
	memcpy(gat.PermanentMacAddress, p_adapter->mac.addr, HW_ADDR_LEN);
	memcpy(gat.CurrentMacAddress, p_adapter->params.conf_mac.addr, HW_ADDR_LEN);

	gat.PhysicalMediumType = NdisPhysicalMedium802_3;
	gat.AccessType = NET_IF_ACCESS_BROADCAST; 

	gat.SupportedOidList = (PNDIS_OID)SUPPORTED_OIDS;
	gat.SupportedOidListLength = sizeof(SUPPORTED_OIDS);

	gat.DirectionType = NET_IF_DIRECTION_SENDRECEIVE; 
	gat.ConnectionType = NET_IF_CONNECTION_DEDICATED; 
	gat.IfType = IF_TYPE_ETHERNET_CSMACD; 
	gat.IfConnectorPresent = TRUE; 
	gat.AccessType = NET_IF_ACCESS_BROADCAST; // NET_IF_ACCESS_BROADCAST for a typical ethernet adapter

	//TODO NDIS60 is it possible to reduce unsupported statistics
	gat.SupportedStatistics = 
							NDIS_STATISTICS_XMIT_OK_SUPPORTED |
							NDIS_STATISTICS_RCV_OK_SUPPORTED |
							NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
							NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
							NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED |
							NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
							NDIS_STATISTICS_TRANSMIT_QUEUE_LENGTH_SUPPORTED;

	//SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
							// NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED;


	//
	// Set power management capabilities
	//
	gat.PowerManagementCapabilities = NULL;
#if 0
	NDIS_PNP_CAPABILITIES PowerManagementCapabilities;
	memset(&PowerManagementCapabilities, 0, sizeof(NDIS_PNP_CAPABILITIES));
	if (MPIsPoMgmtSupported(pPort))
	{
		MPFillPoMgmtCaps(pPort, &PowerManagementCapabilities, &Status, &unUsed);
		ASSERT(NT_SUCCESS(Status)); 
		gat.PowerManagementCapabilities = &PowerManagementCapabilities;
	} 
	else
	{
		
	}
#endif

	//
	// Set RSS attributes
	//
	gat.RecvScaleCapabilities = NULL;
#if 0
	NDIS_RECEIVE_SCALE_CAPABILITIES RssCapabilities;
	memset(&RssCapabilities, 0, sizeof(PNDIS_RECEIVE_SCALE_CAPABILITIES));
	Status = MPFillRssCapabilities(pPort, &RssCapabilities, &unUsed);
	if (NT_SUCCESS(Status)) 
	{
		gat.RecvScaleCapabilities = &RssCapabilities;
	} 
	else
	{
		//
		// do not fail the call because of failure to get PM caps
		//
		Status = NDIS_STATUS_SUCCESS;
		gat.RecvScaleCapabilities = NULL;
	}
#endif

	Status = NdisMSetMiniportAttributes(h_adapter,
			(PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&gat);

	return Status;
}


/*++
Routine Description:
	The routine sets an NDIS_OFFLOAD structure indicates the current offload 
	capabilities that are provided by the miniport adapter 
	
Arguments:
	pPort - a pointer to port object
	offload - reference to NDIS_OFFLOAD object that should be filled

Return Value:
	None.
	
--*/
static
void
OffloadConfig(
	ipoib_adapter_t *p_adapter,
	NDIS_OFFLOAD *p_offload
	)
{ 
	ULONG ulEncapsulation = NDIS_ENCAPSULATION_IEEE_802_3 | NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q;

	memset(p_offload, 0, NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1);

	p_offload->Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
	p_offload->Header.Revision = NDIS_OFFLOAD_REVISION_1; // Should be Revision 1, otherwise NDIS will not work at Win2008 R1
	p_offload->Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1;

	p_offload->Checksum.IPv4Transmit.Encapsulation = ulEncapsulation;
	p_offload->Checksum.IPv4Transmit.IpOptionsSupported = 
	p_offload->Checksum.IPv4Transmit.TcpOptionsSupported = 
	p_offload->Checksum.IPv4Transmit.TcpChecksum = 
	p_offload->Checksum.IPv4Transmit.UdpChecksum = 
	p_offload->Checksum.IPv4Transmit.IpChecksum = !!(p_adapter->params.send_chksum_offload);

	p_offload->Checksum.IPv4Receive.Encapsulation = ulEncapsulation;
	p_offload->Checksum.IPv4Receive.IpOptionsSupported = 
	p_offload->Checksum.IPv4Receive.TcpOptionsSupported = 
	p_offload->Checksum.IPv4Receive.TcpChecksum = 
	p_offload->Checksum.IPv4Receive.UdpChecksum = 
	p_offload->Checksum.IPv4Receive.IpChecksum = !!(p_adapter->params.recv_chksum_offload); 

	p_offload->Checksum.IPv6Transmit.Encapsulation = ulEncapsulation;
	p_offload->Checksum.IPv6Transmit.IpExtensionHeadersSupported = false;
	p_offload->Checksum.IPv6Transmit.TcpOptionsSupported =
	p_offload->Checksum.IPv6Transmit.TcpChecksum = 
	p_offload->Checksum.IPv6Transmit.UdpChecksum = !!(p_adapter->params.send_chksum_offload);


	p_offload->Checksum.IPv6Receive.Encapsulation = ulEncapsulation;
	p_offload->Checksum.IPv6Receive.IpExtensionHeadersSupported = false;
	p_offload->Checksum.IPv6Receive.TcpOptionsSupported = 
	p_offload->Checksum.IPv6Receive.TcpChecksum = 
	p_offload->Checksum.IPv6Receive.UdpChecksum = !!(p_adapter->params.recv_chksum_offload); 

	if (p_adapter->params.lso )
	{
		  ASSERT(p_adapter->params.LsoV1IPv4 || 
				 p_adapter->params.LsoV2IPv4 || 
				 p_adapter->params.LsoV2IPv6);
		  
		  if (p_adapter->params.LsoV1IPv4)
		  {
			  p_offload->LsoV1.IPv4.Encapsulation = ulEncapsulation;
			  p_offload->LsoV1.IPv4.MaxOffLoadSize = MAX_LSO_SIZE;
			  p_offload->LsoV1.IPv4.MinSegmentCount = LSO_MIN_SEG_COUNT;
			  p_offload->LsoV1.IPv4.TcpOptions = NDIS_OFFLOAD_SUPPORTED;
			  p_offload->LsoV1.IPv4.IpOptions = NDIS_OFFLOAD_SUPPORTED;
		  }
	
		  if (p_adapter->params.LsoV2IPv4)
		  {
			  p_offload->LsoV2.IPv4.Encapsulation = ulEncapsulation;
			  p_offload->LsoV2.IPv4.MaxOffLoadSize = MAX_LSO_SIZE;
			  p_offload->LsoV2.IPv4.MinSegmentCount = LSO_MIN_SEG_COUNT;
		  }
	
		  if (p_adapter->params.LsoV2IPv6)
		  {
			  p_offload->LsoV2.IPv6.Encapsulation = ulEncapsulation;
			  p_offload->LsoV2.IPv6.MaxOffLoadSize = MAX_LSO_SIZE;
			  p_offload->LsoV2.IPv6.MinSegmentCount = LSO_MIN_SEG_COUNT;
		  
			  p_offload->LsoV2.IPv6.IpExtensionHeadersSupported = NDIS_OFFLOAD_NOT_SUPPORTED;
			  p_offload->LsoV2.IPv6.TcpOptionsSupported = NDIS_OFFLOAD_NOT_SUPPORTED;
		  }
	  }    
}


/*++
Routine Description:
	The routine sets an NDIS_OFFLOAD structure that indicates all the task 
	offload capabilites that are supported by the NIC. These capabilities include
	capabilities that are currently disabled by standardized keywords in the registry. 
	
Arguments:
	offload - reference to NDIS_OFFLOAD object that should be filled

Return Value:
	None.
	
--*/
static
void
OffloadCapabilities(
	ipoib_adapter_t *p_adapter,
	NDIS_OFFLOAD	*p_offload
	)
{ 
	ULONG ulEncapsulation = NDIS_ENCAPSULATION_IEEE_802_3 | NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q ;
	memset(p_offload, 0, NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1);    

	p_offload->Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
	p_offload->Header.Revision = NDIS_OFFLOAD_REVISION_1; // BUGBUG: do we need to support revision 2? UH 17-May-2008
	p_offload->Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1;

	p_offload->Checksum.IPv4Transmit.Encapsulation = ulEncapsulation;
	p_offload->Checksum.IPv4Transmit.IpOptionsSupported = 
	p_offload->Checksum.IPv4Transmit.TcpOptionsSupported = 
	p_offload->Checksum.IPv4Transmit.TcpChecksum = 
	p_offload->Checksum.IPv4Transmit.UdpChecksum = 
	p_offload->Checksum.IPv4Transmit.IpChecksum = p_adapter->offload_cap.send_chksum_offload;

	p_offload->Checksum.IPv4Receive.Encapsulation = ulEncapsulation;
	p_offload->Checksum.IPv4Receive.IpOptionsSupported = 
	p_offload->Checksum.IPv4Receive.TcpOptionsSupported = 
	p_offload->Checksum.IPv4Receive.TcpChecksum = 
	p_offload->Checksum.IPv4Receive.UdpChecksum = 
	p_offload->Checksum.IPv4Receive.IpChecksum = p_adapter->offload_cap.recv_chksum_offload;


	//
	//	BUGBUG::
	//	During a HW bug that didn't handle correctly packets with 
	//	IPv6 Extension Headers -> we set IpExtensionHeadersSupported to TRUE
	//
	p_offload->Checksum.IPv6Transmit.Encapsulation = ulEncapsulation;
	p_offload->Checksum.IPv6Transmit.IpExtensionHeadersSupported = 
	p_offload->Checksum.IPv6Transmit.TcpOptionsSupported = 
	p_offload->Checksum.IPv6Transmit.TcpChecksum = 
	p_offload->Checksum.IPv6Transmit.UdpChecksum = FALSE;


	p_offload->Checksum.IPv6Receive.Encapsulation = ulEncapsulation;
	p_offload->Checksum.IPv6Receive.IpExtensionHeadersSupported = 
	p_offload->Checksum.IPv6Receive.TcpOptionsSupported = 
	p_offload->Checksum.IPv6Receive.TcpChecksum = 
	p_offload->Checksum.IPv6Receive.UdpChecksum = FALSE;

	if (p_adapter->offload_cap.lso) {

		p_offload->LsoV1.IPv4.Encapsulation = ulEncapsulation;
		p_offload->LsoV1.IPv4.MaxOffLoadSize = MAX_LSO_SIZE;
		p_offload->LsoV1.IPv4.MinSegmentCount = 2;
		p_offload->LsoV1.IPv4.TcpOptions = NDIS_OFFLOAD_SUPPORTED;
		p_offload->LsoV1.IPv4.IpOptions = NDIS_OFFLOAD_SUPPORTED;

		p_offload->LsoV2.IPv4.Encapsulation = ulEncapsulation;
		p_offload->LsoV2.IPv4.MaxOffLoadSize = MAX_LSO_SIZE;
		p_offload->LsoV2.IPv4.MinSegmentCount = 2;

	} else {
		p_offload->LsoV1.IPv4.TcpOptions = NDIS_OFFLOAD_NOT_SUPPORTED;
		p_offload->LsoV1.IPv4.IpOptions = NDIS_OFFLOAD_NOT_SUPPORTED;
		
	}

	/*p_offload->LsoV2.IPv6.Encapsulation = ulEncapsulation;
	p_offload->LsoV2.IPv6.MaxOffLoadSize = LARGE_SEND_OFFLOAD_SIZE;
	p_offload->LsoV2.IPv6.MinSegmentCount = 2;*/

	p_offload->LsoV2.IPv6.IpExtensionHeadersSupported = NDIS_OFFLOAD_NOT_SUPPORTED;
	p_offload->LsoV2.IPv6.TcpOptionsSupported = NDIS_OFFLOAD_NOT_SUPPORTED;

}


/*++
Routine Description:
	The routine sets offload attributes that are associated with a miniport 
	adapter.

Arguments:
	pPort - Pointer to port object

Return Value:
	NDIS_STATUS

Note:
	Should be called in PASSIVE_LEVEL
	
--*/
NDIS_STATUS
SetOffloadAttributes(
	ipoib_adapter_t *p_adapter,
	NDIS_HANDLE	h_adapter
	)
{
	NDIS_STATUS Status;
	NDIS_OFFLOAD offload,hwOffload;
	//ULONG ulEncapsulation = NDIS_ENCAPSULATION_IEEE_802_3 | NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q;

	NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES oat;	 
	memset(&oat, 0, sizeof(NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES));

	oat.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES;
	oat.Header.Revision = NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;
	oat.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;


	OffloadConfig(p_adapter, &offload);


	OffloadCapabilities(p_adapter, &hwOffload);

	oat.DefaultOffloadConfiguration = &offload;
	oat.HardwareOffloadCapabilities = &hwOffload;

	Status = NdisMSetMiniportAttributes(h_adapter,
				(PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&oat);

	return Status;
}


/*++

Routine Description:
	An NDIS 6.0 miniport driver must call NdisMSetMiniportAttributes
	at least twice. The first call is to register itself with NDIS.
	The second call is to register the miniport driver's general
	attributes with NDIS.

	NdisMSetMiniportAttributes takes a parameter of type
	NDIS_MINIPORT_ADAPTER_ATTRIBUTES, which is a union of several miniport
	adapter attributes. Miniport drivers must first call
	NdisMSetMiniportAttributes and pass in an
	NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES structure
	that contains the pointer to its own context area, attribute flags,
	check-for-hang time, and interface type.

	All NDIS 6.0 miniport drivers are deserialized by default.

Arguments:
	pPort - Pointer to port object

Return Value:
	NDIS_STATUS

Note:
	Should be called in PASSIVE_LEVEL
	
--*/
NDIS_STATUS
SetAttributes(
	ipoib_adapter_t *p_adapter,
	NDIS_HANDLE	h_adapter
	)
{
	NTSTATUS Status;

	Status = SetDeviceRegistrationAttributes(p_adapter, h_adapter);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		//ETH_PRINT(TRACE_LEVEL_ERROR, ETH_INIT, "Set device registration failed Error=0x%x\n", Status);
		NdisWriteErrorLogEntry( h_adapter,
			EVENT_IPOIB_START_UP_DEV_REG_ATTR, 1, Status );
		return Status;
	}

	Status = SetAdapterRegistrationAttributes(p_adapter, h_adapter);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		//ETH_PRINT(TRACE_LEVEL_ERROR, ETH_INIT, "Set adapter attributes failed Error=0x%x\n", Status);
		NdisWriteErrorLogEntry( h_adapter,
			EVENT_IPOIB_START_UP_ADAPTER_REG_ATTR, 1, Status );
		return Status;
	}

#if 0
	if(!pPort->Config.fWHQL)
	{
		Status = SetHardwareAssistAttributes(pPort);
		if (Status != NDIS_STATUS_SUCCESS)
		{
			//ETH_PRINT(TRACE_LEVEL_ERROR, ETH_INIT, "Set Hardware Assist Attributes failed Error=0x%x\n", Status);
			return Status;
		}
	}
#endif

	Status = SetGenericAttributes(p_adapter, h_adapter);
	if (Status != NDIS_STATUS_SUCCESS)
	{
		//ETH_PRINT(TRACE_LEVEL_ERROR, ETH_INIT, "Set generic attributes failed Error=0x%x\n", Status);
		return Status;
	}

	return Status;
}

BOOLEAN
IsValidOffloadConfig(ipoib_adapter_t *p_adapter, PNDIS_OFFLOAD_PARAMETERS pOffloadParam)
{
	BOOLEAN bRet = TRUE;

	UCHAR CheckSumConfig[5]={0};
	CheckSumConfig[0] = pOffloadParam->IPv4Checksum;
	CheckSumConfig[1] = pOffloadParam->TCPIPv4Checksum;
	CheckSumConfig[2] = pOffloadParam->UDPIPv4Checksum;
	CheckSumConfig[3] = pOffloadParam->TCPIPv6Checksum;
	CheckSumConfig[4] = pOffloadParam->UDPIPv6Checksum;

	for(int i=0 ; i<5 ; i++)
	{
		if(CheckSumConfig[i] != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
		{
			switch (CheckSumConfig[i]) {
				case NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED:
					bRet = TRUE;
					break;
				//return FALSE in any case when NDIS tries to set unsupported value
				case NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED:
					bRet = (BOOLEAN) p_adapter->offload_cap.send_chksum_offload;
					break;
				case NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED:
					bRet = (BOOLEAN) p_adapter->offload_cap.recv_chksum_offload;
					break;
				case NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED:
					bRet = (BOOLEAN) (p_adapter->offload_cap.send_chksum_offload && 
							 p_adapter->offload_cap.recv_chksum_offload);
					break;
				default:
					ASSERT (FALSE);
			}
		
			if (!bRet) 
				return FALSE;
					
					
			for(int j=0 ; j<5 ; j++)
			{
				if( (CheckSumConfig[j] != 0) && (CheckSumConfig[j] != CheckSumConfig[i])  )
				{
				   bRet = FALSE;
				   goto Exit;
				}
			}			 
		}
	}


	UCHAR OffloadConfig[3]={0};
	OffloadConfig[0] = pOffloadParam->LsoV1;
	OffloadConfig[1] = pOffloadParam->LsoV2IPv4;
	OffloadConfig[2] = pOffloadParam->LsoV2IPv6;

	if (!p_adapter->offload_cap.lso) {
		if ((pOffloadParam->LsoV1 == NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED) ||
			(pOffloadParam->LsoV2IPv4 == NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED))
		{
			return FALSE;
		}
	}
	
	pOffloadParam->LsoV1;
	OffloadConfig[1] = pOffloadParam->LsoV2IPv4;

	for(int i=0 ; i<3 ; i++)
	{
		if(OffloadConfig[i] != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
		{
			for(int j=0 ; j<3 ; j++)
			{
				if( (OffloadConfig[j] != 0) && (OffloadConfig[j] != OffloadConfig[i])  )
				{
				   bRet = FALSE;
				   goto Exit;
				}
			}			 
		}		 
	}
   
Exit:
	return bRet;		
}

static
NDIS_STATUS 
SetOffloadParameters(
	ipoib_adapter_t * p_adapter,
	void* const pBuf,
	ULONG len 
	)
{
	IPOIB_ENTER(IPOIB_DBG_OID);

	ASSERT(pBuf != NULL); 
	
	PNDIS_OFFLOAD_PARAMETERS pOffloadParam = NULL;
	PNDIS_OBJECT_HEADER pOffloadHeader = NULL;
	NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
	bool StatusIndicationFlag = FALSE;
 
	if (len != NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_2)
	{
		IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_OID,
			("Buffer is too small. offloading task requirs %d but the buffer "
			 "size is: %d \n", NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_2, len));
		Status =  NDIS_STATUS_INVALID_LENGTH;
		goto Exit;
	}

	IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_OID,
		("received set for OID_TCP_TASK_OFFLOAD\n"));

	pOffloadParam = (PNDIS_OFFLOAD_PARAMETERS) pBuf;
	pOffloadHeader = &(pOffloadParam->Header);
	
	if((pOffloadHeader->Type != NDIS_OBJECT_TYPE_DEFAULT) ||
	   (pOffloadHeader->Revision != NDIS_OFFLOAD_PARAMETERS_REVISION_2) ||
	   (pOffloadHeader->Size != NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_2))
	{		
		ASSERT(FALSE);
		IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_OID,
			("Set offloading task Illegal header\n"));
		Status = NDIS_STATUS_INVALID_DATA;
		goto Exit;
	}

	if ((pOffloadParam->IPsecV1 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE) ||
		(pOffloadParam->TcpConnectionIPv4 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE) ||
		(pOffloadParam->TcpConnectionIPv6 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE) ||
		(pOffloadParam->Flags != 0))
	{
		Status = NDIS_STATUS_NOT_SUPPORTED;
		goto Exit;
	}

	//Eliminate currently unsupported statistic
	if ((pOffloadParam->TCPIPv6Checksum != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE) ||
		(pOffloadParam->UDPIPv6Checksum != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE) ||
		(pOffloadParam->LsoV2IPv6		!= NDIS_OFFLOAD_PARAMETERS_NO_CHANGE))
	{
		Status = NDIS_STATUS_NOT_SUPPORTED;
		goto Exit;
	}
		

	BOOLEAN bRet = IsValidOffloadConfig(p_adapter, pOffloadParam);
	if(bRet == FALSE)
	{
		//ASSERT(FALSE);
		Status = NDIS_STATUS_NOT_SUPPORTED;
		goto Exit;		  
	}

	// Set current offload configuration capabilites
	if ((pOffloadParam->IPv4Checksum == 	NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED) ||
		(pOffloadParam->TCPIPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED) ||
		(pOffloadParam->UDPIPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED))
	{
		p_adapter->params.send_chksum_offload = CSUM_ENABLED;
		p_adapter->params.recv_chksum_offload = CSUM_DISABLED;
		StatusIndicationFlag = TRUE;
	}

	else if ((pOffloadParam->IPv4Checksum == NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED) ||
		(pOffloadParam->TCPIPv4Checksum == NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED) ||
		(pOffloadParam->UDPIPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED))
	{
		p_adapter->params.recv_chksum_offload = CSUM_ENABLED;
		p_adapter->params.send_chksum_offload = CSUM_DISABLED;
		StatusIndicationFlag = TRUE;
	}

	else if ((pOffloadParam->IPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED ) ||
		(pOffloadParam->TCPIPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED ) ||
		(pOffloadParam->UDPIPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED ))
	{
		p_adapter->params.send_chksum_offload = CSUM_ENABLED;
		p_adapter->params.recv_chksum_offload = CSUM_ENABLED;
		StatusIndicationFlag = TRUE;
	}
	else if ((pOffloadParam->IPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED ) ||
			(pOffloadParam->TCPIPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED ) ||
			(pOffloadParam->UDPIPv4Checksum ==	NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED ))
	{
		p_adapter->params.send_chksum_offload = CSUM_DISABLED;
		p_adapter->params.recv_chksum_offload = CSUM_DISABLED;
		StatusIndicationFlag = TRUE;
	}
	
		

 #if 0	
	if(pOffloadParam->TCPIPv6Checksum != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
	{
		UpdateOffloadSeeting(pOffloadParam->TCPIPv6Checksum, pPort->Config.TCPUDPIPv4Chksum);		 
		StatusIndicationFlag = TRUE;
	}
	
	if(pOffloadParam->UDPIPv6Checksum != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
	{
		UpdateOffloadSeeting(pOffloadParam->UDPIPv6Checksum, pPort->Config.TCPUDPIPv4Chksum);		 
		StatusIndicationFlag = TRUE;
	}
#endif

   // SetCheksumOffloadingModes(pPort->Config);
	
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
	
	//////////////////////////// OFFLOAD OFFLOAD ////////////////////////////
	if(pOffloadParam->LsoV1 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
	{
		if(pOffloadParam->LsoV1 == NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED)
		{
			p_adapter->params.LsoV1IPv4 = true; 		   
		}
		else
		{
			p_adapter->params.LsoV1IPv4 = false;			
		}
		StatusIndicationFlag = TRUE;
	}
	
	if(pOffloadParam->LsoV2IPv4 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
	{
		if(pOffloadParam->LsoV2IPv4 == NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED)
		{
			p_adapter->params.LsoV2IPv4 = true;   
		}
		else
		{
			p_adapter->params.LsoV2IPv4 = false;
		}
		StatusIndicationFlag = TRUE;
	}
	
	if(pOffloadParam->LsoV2IPv6 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
	{
		if(pOffloadParam->LsoV2IPv6 == NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED)
		{
			p_adapter->params.LsoV2IPv6 = true;
		}
		else
		{
			p_adapter->params.LsoV2IPv6 = false;
		}
		StatusIndicationFlag = TRUE;
	}

	p_adapter->params.lso = (p_adapter->params.LsoV1IPv4 || 
							 p_adapter->params.LsoV2IPv4 || 
							 p_adapter->params.LsoV2IPv6);

	if( p_adapter->params.lso && p_adapter->params.cm_enabled )
	{
		ASSERT( p_adapter->params.lso == 0 );
		p_adapter->params.lso =
		p_adapter->params.LsoV1IPv4 =
		p_adapter->params.LsoV2IPv4 =
		p_adapter->params.LsoV2IPv6 = FALSE;
	}

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//	

	if(StatusIndicationFlag)
	{		 
		NDIS_OFFLOAD CurrentOffloadCapapilities;
		NDIS_STATUS_INDICATION StatusIndication;				

		OffloadConfig(p_adapter, &CurrentOffloadCapapilities);
				 
		IPOIB_INIT_NDIS_STATUS_INDICATION(&StatusIndication,
									   p_adapter->h_adapter,
									   NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG ,
									   (PVOID)&CurrentOffloadCapapilities,
									   sizeof(CurrentOffloadCapapilities));
									   
		NdisMIndicateStatusEx(p_adapter->h_adapter, &StatusIndication); 	   
	}
				
Exit:	
	IPOIB_EXIT(IPOIB_DBG_OID);
	return Status;	 
}

/*++

Routine Description:
	The routine handles setting of OID_GEN_INTERRUPT_MODERATION.

Arguments:
	InformationBuffer - Pointer to the buffer that contains the data
	InformationBufferLength - data length
	
Return Value:
	NDIS_STAUS
	
--*/
static 
NDIS_STATUS
SetInterruptModeration(
	PVOID InformationBuffer,
	ULONG InformationBufferLength )
{
	IPOIB_ENTER(IPOIB_DBG_OID);
	
	NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
	
	if (InformationBufferLength != sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS))
	{
		Status = NDIS_STATUS_INVALID_LENGTH;
		goto Exit;
	}
	
	PNDIS_INTERRUPT_MODERATION_PARAMETERS pInteruptModerationParam =
					(PNDIS_INTERRUPT_MODERATION_PARAMETERS)InformationBuffer;
	
	if ((pInteruptModerationParam->Header.Type != NDIS_OBJECT_TYPE_DEFAULT) ||
		(pInteruptModerationParam->Header.Revision != NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1) ||
		(pInteruptModerationParam->Header.Size != NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1))
	{
		Status = NDIS_STATUS_INVALID_DATA;
		goto Exit;
	}
	//
	// BUGBUG: Need to handle disabling of interrupt moderation 
	//							UH, 4-Jun-2008
	//
//	  ASSERT(pInteruptModerationParam->Flags == NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_RESET);
//	  ASSERT(pInteruptModerationParam->InterruptModeration == NdisInterruptModerationEnabled);

Exit:
	IPOIB_EXIT(IPOIB_DBG_OID);
	return Status;
}


NDIS_STATUS
InitNdisScatterGatherDma(
	ipoib_adapter_t	*p_adapter,
	NDIS_HANDLE		h_adapter )
{
	NDIS_STATUS						status;
	NDIS_SG_DMA_DESCRIPTION			DmaDescription;
	
	memset(&DmaDescription, 0, sizeof(DmaDescription));

	DmaDescription.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
	DmaDescription.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
	DmaDescription.Header.Size = sizeof(NDIS_SG_DMA_DESCRIPTION);
	DmaDescription.Flags = NDIS_SG_DMA_64_BIT_ADDRESS; 
	//
	// Even if offload is enabled, the packet size for mapping shouldn't change
	//
	//TODO bug ?
	DmaDescription.MaximumPhysicalMapping = MAX_LSO_SIZE + LSO_MAX_HEADER;

	DmaDescription.ProcessSGListHandler = ipoib_process_sg_list;
	DmaDescription.SharedMemAllocateCompleteHandler = NULL;

	DmaDescription.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
	DmaDescription.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
	DmaDescription.Header.Size = sizeof(NDIS_SG_DMA_DESCRIPTION);//NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;

	DmaDescription.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
	//DmaDescription.MaximumPhysicalMapping = pPort->p_adapter->params.xfer_block_size;

	
	status = NdisMRegisterScatterGatherDma(
					h_adapter,
					&DmaDescription,
					&p_adapter->NdisMiniportDmaHandle );

	if( status != NDIS_STATUS_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				 ("NdisMRegisterScatterGatherDma returned 0x%.8x.\n", status) );
		return status;
		
	}
	//NDIS sets this value before it returns from NdisMRegisterScatterGatherDma.
	//Miniport drivers should use this size to preallocate memory for each
	// scatter/gather list. 
	p_adapter->sg_list_size = DmaDescription.ScatterGatherListSize ;
	
	return status;
}



#if defined(NDIS630_MINIPORT)
NDK_HANDLE
ndk_get_ndk_handle(
	IN				NDIS_HANDLE 			adapter_context
	)
{
	ipoib_adapter_t*	p_adapter;
	
	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return p_adapter->h_ndk;
}
#endif



/*++
Routine Description:

	MiniportInitialize handler

Arguments:

	MiniportAdapterHandle	The handle NDIS uses to refer to us
	MiniportDriverContext	Handle passed to NDIS when we registered the driver
	MiniportInitParameters	Initialization parameters
	
Return Value:

	NDIS_STATUS_SUCCESS unless something goes wrong

--*/

NDIS_STATUS
ipoib_initialize_ex(
		IN NDIS_HANDLE	h_adapter,
		IN NDIS_HANDLE	config_context,
		IN PNDIS_MINIPORT_INIT_PARAMETERS  MiniportInitParameters)
{
		NDIS_STATUS 		status;
		ib_api_status_t 	ib_status;
		ipoib_adapter_t 	*p_adapter;

		IPOIB_ENTER( IPOIB_DBG_INIT );

#if 0
if(cl_get_time_stamp_sec() < 30) {
	cl_dbg_out("Disable/Enable IPoIB adapter to continue running\n");
	return NDIS_STATUS_HARD_ERRORS;
}
#endif
	
#ifdef _DEBUG_
		PAGED_CODE();
#endif
		
		UNUSED_PARAM( config_context );
		UNUSED_PARAM( MiniportInitParameters );
		
		/* Create the adapter adapter */
		ib_status = ipoib_create_adapter( h_adapter, &p_adapter );
		if( ib_status != IB_SUCCESS )
		{
			//ASSERT(FALSE);
			NdisWriteErrorLogEntry( h_adapter,
				EVENT_IPOIB_START_UP_CREATE_ADAPTER, 1, ib_status );
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ipoib_create_adapter returned status %d.\n", ib_status ) );
			return NDIS_STATUS_FAILURE;
		}

		status	= SetAttributes(p_adapter, h_adapter);
		if (status != NDIS_STATUS_SUCCESS) {
			//ASSERT(FALSE);
			ipoib_destroy_adapter( p_adapter );
			return NDIS_STATUS_FAILURE;
		}

#if IPOIB_USE_DMA
		status = InitNdisScatterGatherDma(p_adapter, h_adapter);
		if( status != NDIS_STATUS_SUCCESS )
		{	
			ipoib_destroy_adapter( p_adapter );
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("InitNdisScatterGatherDma returned status 0x%.8x.\n", status ) );
			return NDIS_STATUS_FAILURE;
		}
#endif
		/* Create the adapter adapter */
		ib_status = ipoib_start_adapter( p_adapter );
		if( ib_status != IB_SUCCESS )
		{
			//ASSERT(FALSE);
			NdisWriteErrorLogEntry( h_adapter,
				EVENT_IPOIB_START_UP_START_ADAPTER, 1, ib_status );
#if  IPOIB_USE_DMA
			NdisMDeregisterScatterGatherDma(p_adapter->NdisMiniportDmaHandle);
#endif
			ipoib_destroy_adapter( p_adapter );
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ipoib_start_adapter returned status %d.\n", ib_status ) );
			return NDIS_STATUS_FAILURE;
		}
		
		status = SetOffloadAttributes(p_adapter, h_adapter);
		if (status != NDIS_STATUS_SUCCESS)
		{
			NdisWriteErrorLogEntry( h_adapter,
				EVENT_IPOIB_START_UP_SET_OFFLOAD_ATTR, 1, status );
#if  IPOIB_USE_DMA
			NdisMDeregisterScatterGatherDma(p_adapter->NdisMiniportDmaHandle);
#endif
			ipoib_destroy_adapter( p_adapter );
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("SetOffloadAttributes returned status 0x%.8x.\n", status ) );
			return NDIS_STATUS_FAILURE;
		}

        UINT64 mac = 0;
        RtlCopyMemory( &mac, &p_adapter->mac, sizeof(p_adapter->mac) );

        IBAT_PORT_RECORD rec;
        rec.CaGuid = p_adapter->guids.ca_guid;
        rec.PortGuid = p_adapter->guids.port_guid.guid;
        rec.PortNum = p_adapter->guids.port_num;
        rec.PKey = p_adapter->guids.port_guid.pkey;

        status = p_adapter->p_ifc->ibat_register(
            mac,
            MiniportInitParameters->NetLuid.Value,
            &p_adapter->guids.driver_id,
            &rec,
            FALSE,
            &p_adapter->ibatRouter
            );
        if( !NT_SUCCESS(status) )
        {
#if  IPOIB_USE_DMA
            NdisMDeregisterScatterGatherDma(p_adapter->NdisMiniportDmaHandle);
#endif
            ipoib_destroy_adapter( p_adapter );
            IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
                ("Failed to register with IBAT: 0x%.8x.\n", status) );
            return status;
        }

#if defined(NDIS630_MINIPORT)
		// NDK support
		ipoib_ifc_data_t *p = &p_adapter->guids;

		status = ndk_adapter_init( h_adapter, p_adapter->mac.addr, NDK_PROVIDER_TYPE_IPOIB, 
			p_adapter->p_ifc->wdm.Context, p->port_num, 
			NDK_VALID_ALL, p->ca_guid, p->port_guid.guid, p->port_guid.pkey, &p_adapter->h_ndk ); 
        if(status != NDIS_STATUS_SUCCESS)
        {
			IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_ALL,
				("NDK init failed with status 0x%x. Continue without NDK.\n", status));
            status = NDIS_STATUS_SUCCESS;
			p_adapter->h_ndk = NULL;
        }
#endif

		IPOIB_EXIT( IPOIB_DBG_INIT );
		return status;
}


//! Deallocates resources when the NIC is removed and halts the NIC..
//TODO: Dispatch or Passive ?
/*	IRQL = DISPATCH_LEVEL

@param adapter_context The adapter context allocated at start
*/
void
ipoib_halt_ex(
	IN				NDIS_HANDLE					adapter_context,
	IN				NDIS_HALT_ACTION			HaltAction )
{
	ipoib_adapter_t	*p_adapter;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	UNUSED_PARAM(HaltAction);
		
	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

    CL_ASSERT( p_adapter->p_ifc->ibat_deregister != NULL );
    CL_ASSERT( p_adapter->ibatRouter != NULL );
    p_adapter->p_ifc->ibat_deregister( p_adapter->ibatRouter );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Port %016I64x (CA %016I64x port %d) halting\n",
			p_adapter->guids.port_guid.guid, p_adapter->guids.ca_guid,
			p_adapter->guids.port_num) );

#if IPOIB_USE_DMA
	if (p_adapter->NdisMiniportDmaHandle != NULL)
	{
		NdisMDeregisterScatterGatherDma(p_adapter->NdisMiniportDmaHandle);
		p_adapter->NdisMiniportDmaHandle = NULL;
	}
#endif


#if defined(NDIS630_MINIPORT)
	ndk_adapter_deinit( p_adapter->h_ndk );
	p_adapter->h_ndk = NULL;
#endif

	ipoib_destroy_adapter( p_adapter );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


//! Reports the state of the NIC, or monitors the responsiveness of an underlying device driver.
/*	IRQL = DISPATCH_LEVEL

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
	
	if (p_adapter->p_port)
	{
		if (p_adapter->p_port->send_mgr.pending_list.count > 0)
		{
			++(p_adapter->p_port->n_no_progress);
			if (p_adapter->p_port->n_no_progress >= 4)
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR,
					("port got stuck, reseting it !!!\n"));
				//CL_ASSERT(FALSE); //?????
				p_adapter->hung = TRUE;
			}
		}
		else 
		{
			p_adapter->p_port->n_no_progress = 0;
		}
	}
	
	if (p_adapter->hung) {
		ipoib_resume_oids(p_adapter);
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return (p_adapter->hung? TRUE:FALSE);
}


/*++
Routine Description:
	The routine sets an NDIS_OFFLOAD structure indicates the current offload 
	capabilities that are provided by the miniport adapter 

Arguments:
	pPort - a pointer to port object
	offload - reference to NDIS_OFFLOAD object that should be filled

Return Value:
	None.

--*/
//TODO
#if 0
static
void
__ipoib_get_offload_config(
	ipoib_port_t *pPort,
	NDIS_OFFLOAD *p_offload
	)
{
	NDIS_STATUS Status;
	ULONG TxChksumOffload = ((MP_GET_PORT_CONFIG(pPort, TxChksumOffload) == TRUE) ? NDIS_OFFLOAD_SET_ON : NDIS_OFFLOAD_SET_OFF);
	ULONG RxChksumOffload = ((MP_GET_PORT_CONFIG(pPort, RxChksumOffload) == TRUE) ? NDIS_OFFLOAD_SET_ON : NDIS_OFFLOAD_SET_OFF);
	BOOLEAN fLargeSendOffload = MP_GET_PORT_CONFIG(pPort, LargeSendOffload);
	ULONG ulEncapsulation = NDIS_ENCAPSULATION_IEEE_802_3 | NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q;
		
	memset(&*p_offload, 0, sizeof(NDIS_OFFLOAD));
	*p_offload.Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
	*p_offload.Header.Revision = NDIS_OFFLOAD_REVISION_1; // BUGBUG: do we need to support revision 2? UH 17-May-2008
	*p_offload.Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1;

	*p_offload.Checksum.IPv4Transmit.Encapsulation = ulEncapsulation;
	*p_offload.Checksum.IPv4Transmit.IpOptionsSupported = TxChksumOffload;
	*p_offload.Checksum.IPv4Transmit.TcpOptionsSupported = TxChksumOffload;
	*p_offload.Checksum.IPv4Transmit.TcpChecksum = TxChksumOffload;
	*p_offload.Checksum.IPv4Transmit.UdpChecksum = NDIS_OFFLOAD_NOT_SUPPORTED;
	*p_offload.Checksum.IPv4Transmit.IpChecksum = TxChksumOffload;

	*p_offload.Checksum.IPv4Receive.Encapsulation = ulEncapsulation;
	*p_offload.Checksum.IPv4Receive.IpOptionsSupported = RxChksumOffload;
	*p_offload.Checksum.IPv4Receive.TcpOptionsSupported = RxChksumOffload;
	*p_offload.Checksum.IPv4Receive.TcpChecksum = RxChksumOffload;
	*p_offload.Checksum.IPv4Receive.UdpChecksum = NDIS_OFFLOAD_NOT_SUPPORTED; 
	*p_offload.Checksum.IPv4Receive.IpChecksum = RxChksumOffload;

	*p_offload.Checksum.IPv6Transmit.Encapsulation = ulEncapsulation;
	*p_offload.Checksum.IPv6Transmit.IpExtensionHeadersSupported = TxChksumOffload;
	*p_offload.Checksum.IPv6Transmit.TcpOptionsSupported = TxChksumOffload;
	*p_offload.Checksum.IPv6Transmit.TcpChecksum = TxChksumOffload;
	*p_offload.Checksum.IPv6Transmit.UdpChecksum = NDIS_OFFLOAD_NOT_SUPPORTED;


	*p_offload.Checksum.IPv6Receive.Encapsulation = ulEncapsulation;
	*p_offload.Checksum.IPv6Receive.IpExtensionHeadersSupported = RxChksumOffload;
	*p_offload.Checksum.IPv6Receive.TcpOptionsSupported = RxChksumOffload;
	*p_offload.Checksum.IPv6Receive.TcpChecksum = RxChksumOffload;
	*p_offload.Checksum.IPv6Receive.UdpChecksum = NDIS_OFFLOAD_NOT_SUPPORTED;

	if (fLargeSendOffload)
	{
		*p_offload.LsoV1.IPv4.Encapsulation = ulEncapsulation;
		*p_offload.LsoV1.IPv4.MaxOffLoadSize = LARGE_SEND_OFFLOAD_SIZE;
		*p_offload.LsoV1.IPv4.MinSegmentCount = 1;
		*p_offload.LsoV1.IPv4.TcpOptions = NDIS_OFFLOAD_SUPPORTED;
		*p_offload.LsoV1.IPv4.IpOptions = NDIS_OFFLOAD_SUPPORTED;
	}
}
#endif

static PCHAR
GetOidName(ULONG oid)
{
    PCHAR oidName;
    static CHAR unknown[28];

    switch (oid)
	{
        #undef MAKECASE
        #define MAKECASE(oidx) case oidx: oidName = #oidx; break;

        MAKECASE(OID_GEN_MACHINE_NAME)
        MAKECASE(OID_GEN_SUPPORTED_LIST)
        MAKECASE(OID_GEN_HARDWARE_STATUS)
        MAKECASE(OID_GEN_MEDIA_SUPPORTED)
        MAKECASE(OID_GEN_MEDIA_IN_USE)
        MAKECASE(OID_GEN_MAXIMUM_LOOKAHEAD)
        MAKECASE(OID_GEN_CURRENT_LOOKAHEAD)
        MAKECASE(OID_GEN_MAXIMUM_FRAME_SIZE)
        MAKECASE(OID_GEN_LINK_SPEED)
        MAKECASE(OID_GEN_TRANSMIT_BUFFER_SPACE)
        MAKECASE(OID_GEN_RECEIVE_BUFFER_SPACE)
        MAKECASE(OID_GEN_TRANSMIT_BLOCK_SIZE)
        MAKECASE(OID_GEN_RECEIVE_BLOCK_SIZE)
        MAKECASE(OID_GEN_VENDOR_ID)
        MAKECASE(OID_GEN_VENDOR_DESCRIPTION)
        MAKECASE(OID_GEN_CURRENT_PACKET_FILTER)
        MAKECASE(OID_GEN_DRIVER_VERSION)
        MAKECASE(OID_GEN_MAXIMUM_TOTAL_SIZE)
        MAKECASE(OID_GEN_PROTOCOL_OPTIONS)
        MAKECASE(OID_GEN_MAC_OPTIONS)
        MAKECASE(OID_GEN_MEDIA_CONNECT_STATUS)
        MAKECASE(OID_GEN_MAXIMUM_SEND_PACKETS)
        MAKECASE(OID_GEN_VENDOR_DRIVER_VERSION)
        MAKECASE(OID_GEN_SUPPORTED_GUIDS)
        MAKECASE(OID_GEN_NETWORK_LAYER_ADDRESSES)
        MAKECASE(OID_GEN_TRANSPORT_HEADER_OFFSET)
        MAKECASE(OID_GEN_MEDIA_CAPABILITIES)
        MAKECASE(OID_GEN_PHYSICAL_MEDIUM)
        MAKECASE(OID_GEN_XMIT_OK)
        MAKECASE(OID_GEN_RCV_OK)
        MAKECASE(OID_GEN_XMIT_ERROR)
        MAKECASE(OID_GEN_RCV_ERROR)
        MAKECASE(OID_GEN_RCV_NO_BUFFER)
        MAKECASE(OID_GEN_DIRECTED_BYTES_XMIT)
        MAKECASE(OID_GEN_DIRECTED_FRAMES_XMIT)
        MAKECASE(OID_GEN_MULTICAST_BYTES_XMIT)
        MAKECASE(OID_GEN_MULTICAST_FRAMES_XMIT)
        MAKECASE(OID_GEN_BROADCAST_BYTES_XMIT)
        MAKECASE(OID_GEN_BROADCAST_FRAMES_XMIT)
        MAKECASE(OID_GEN_DIRECTED_BYTES_RCV)
        MAKECASE(OID_GEN_DIRECTED_FRAMES_RCV)
        MAKECASE(OID_GEN_MULTICAST_BYTES_RCV)
        MAKECASE(OID_GEN_MULTICAST_FRAMES_RCV)
        MAKECASE(OID_GEN_BROADCAST_BYTES_RCV)
        MAKECASE(OID_GEN_BROADCAST_FRAMES_RCV)
        MAKECASE(OID_GEN_RCV_CRC_ERROR)
        MAKECASE(OID_GEN_TRANSMIT_QUEUE_LENGTH)
        MAKECASE(OID_GEN_GET_TIME_CAPS)
        MAKECASE(OID_GEN_GET_NETCARD_TIME)
        MAKECASE(OID_GEN_NETCARD_LOAD)
        MAKECASE(OID_GEN_DEVICE_PROFILE)
        MAKECASE(OID_GEN_INIT_TIME_MS)
        MAKECASE(OID_GEN_RESET_COUNTS)
        MAKECASE(OID_GEN_MEDIA_SENSE_COUNTS)
        MAKECASE(OID_PNP_CAPABILITIES)
        MAKECASE(OID_PNP_SET_POWER)
        MAKECASE(OID_PNP_QUERY_POWER)
        MAKECASE(OID_PNP_ADD_WAKE_UP_PATTERN)
        MAKECASE(OID_PNP_REMOVE_WAKE_UP_PATTERN)
        MAKECASE(OID_PNP_ENABLE_WAKE_UP)
        MAKECASE(OID_802_3_PERMANENT_ADDRESS)
        MAKECASE(OID_802_3_CURRENT_ADDRESS)
        MAKECASE(OID_802_3_MULTICAST_LIST)
        MAKECASE(OID_802_3_MAXIMUM_LIST_SIZE)
        MAKECASE(OID_802_3_MAC_OPTIONS)
        MAKECASE(OID_802_3_RCV_ERROR_ALIGNMENT)
        MAKECASE(OID_802_3_XMIT_ONE_COLLISION)
        MAKECASE(OID_802_3_XMIT_MORE_COLLISIONS)
        MAKECASE(OID_802_3_XMIT_DEFERRED)
        MAKECASE(OID_802_3_XMIT_MAX_COLLISIONS)
        MAKECASE(OID_802_3_RCV_OVERRUN)
        MAKECASE(OID_802_3_XMIT_UNDERRUN)
        MAKECASE(OID_802_3_XMIT_HEARTBEAT_FAILURE)
        MAKECASE(OID_802_3_XMIT_TIMES_CRS_LOST)
        MAKECASE(OID_802_3_XMIT_LATE_COLLISIONS)

        default:
			StringCchPrintf( unknown, sizeof(unknown),
								"<* Unknown OID? %#X *>", oid );
            oidName = unknown;
            break;
    }

    return oidName;
#undef MAKECASE
}

//! Returns information about the capabilities and status of the driver and/or its NIC.
/*	IRQL = DISPATCH_LEVEL

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
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest )

{
		ipoib_adapter_t		*p_adapter;
		NDIS_STATUS 		status;
		ULONG				version;
		ULONG				info;
		PVOID				src_buf;
		ULONG				buf_len;
		pending_oid_t		oid_info;
		uint8_t				port_num;
		NDIS_OFFLOAD		offload;

		NDIS_INTERRUPT_MODERATION_PARAMETERS InterruptModerationParam;

		
		IPOIB_ENTER( IPOIB_DBG_OID );
		
		oid_info.oid = pNdisRequest->DATA.QUERY_INFORMATION.Oid;
		oid_info.p_buf = pNdisRequest->DATA.QUERY_INFORMATION.InformationBuffer;
		oid_info.buf_len = pNdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
		oid_info.p_bytes_used = (PULONG)&pNdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
		oid_info.p_bytes_needed = (PULONG)&pNdisRequest->DATA.QUERY_INFORMATION.BytesNeeded;
		oid_info.p_pending_oid = NULL;
		
		CL_ASSERT( adapter_context );
		p_adapter = (ipoib_adapter_t*)adapter_context;

		CL_ASSERT( oid_info.p_bytes_used );
		CL_ASSERT( oid_info.p_bytes_needed );
		CL_ASSERT( !p_adapter->pending_query );
		
		status = NDIS_STATUS_SUCCESS;
		src_buf = &info;
		buf_len = sizeof(info);
	
		port_num = p_adapter->guids.port_num;
	
		switch( oid_info.oid )
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
			if( p_adapter->params.cm_enabled )
			{
				info = p_adapter->params.cm_xfer_block_size;
			}
			else
			{
				info = p_adapter->params.payload_mtu;
			}
			break;
	
		case OID_GEN_TRANSMIT_BUFFER_SPACE:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_GEN_TRANSMIT_BUFFER_SPACE\n",
				port_num) );
			if( p_adapter->params.cm_enabled )
				info = p_adapter->params.sq_depth
							* p_adapter->params.cm_xfer_block_size;
			else
				info = p_adapter->params.sq_depth
							* p_adapter->params.xfer_block_size;
			break;
	
		case OID_GEN_RECEIVE_BUFFER_SPACE:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_GEN_RECEIVE_BUFFER_SPACE\n", port_num) );
			if( p_adapter->params.cm_enabled )
				info = p_adapter->params.rq_depth
							* p_adapter->params.cm_xfer_block_size;
			else
				info = p_adapter->params.rq_depth
							* p_adapter->params.xfer_block_size;
			break;
	
		case OID_GEN_MAXIMUM_LOOKAHEAD:
		case OID_GEN_CURRENT_LOOKAHEAD:
		case OID_GEN_TRANSMIT_BLOCK_SIZE:
		case OID_GEN_RECEIVE_BLOCK_SIZE:
		case OID_GEN_MAXIMUM_TOTAL_SIZE:
			if( p_adapter->params.cm_enabled )
				info = p_adapter->params.cm_xfer_block_size;
			else
				info = p_adapter->params.xfer_block_size;

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for %s = %d\n",
					port_num, GetOidName(oid_info.oid), info) );
			break;
	
		case OID_GEN_VENDOR_ID:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_GEN_VENDOR_ID\n", port_num) );
			src_buf = (void*)VENDOR_ID;
			buf_len = sizeof(VENDOR_ID);
			break;
	
		case OID_GEN_VENDOR_DESCRIPTION:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID, 
				("Port %d received query for OID_GEN_VENDOR_DESCRIPTION\n",
				port_num) );
			src_buf = VENDOR_DESCRIPTION;
			buf_len = sizeof(VENDOR_DESCRIPTION);
			break;
	
		case OID_GEN_VENDOR_DRIVER_VERSION:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_GEN_VENDOR_DRIVER_VERSION\n",
				port_num) );
			src_buf = &version;
			buf_len = sizeof(version);
			//TODO: Figure out what the right version is.
			version = 1 << 8 | 1;
			break;
	
		case OID_GEN_PHYSICAL_MEDIUM:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_GEN_PHYSICAL_MEDIUM\n",
				port_num) );
			info = NdisPhysicalMediumUnspecified;
			break;
	
		case OID_GEN_CURRENT_PACKET_FILTER:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_GEN_CURRENT_PACKET_FILTER\n",
				port_num) );
			info = p_adapter->packet_filter;
			break;
	
		case OID_GEN_DRIVER_VERSION:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_GEN_DRIVER_VERSION\n",
				port_num) );
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
				("Port %d received query for OID_GEN_MEDIA_CONNECT_STATUS\n",
				port_num) );
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
				p_adapter->query_oid.p_pending_oid = pNdisRequest;
	
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
				("Port %d received query for OID_GEN_MAXIMUM_SEND_PACKETS\n",
				port_num) );
			info = MINIPORT_MAX_SEND_PACKETS;
			break;
	
		/* Required General Statistics */
		case OID_GEN_STATISTICS:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_GEN_STATISTICS\n", port_num) );
			src_buf = NULL;   
			 buf_len =	sizeof(NDIS_STATISTICS_INFO);
			if (oid_info.buf_len < buf_len)
			{
			   break;
			} 
			status = ipoib_get_gen_stat(p_adapter, &oid_info );
			break;
	
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
		case OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES:
			buf_len = sizeof(NDIS_OFFLOAD);
			if (buf_len < oid_info.buf_len)
			{
				*oid_info.p_bytes_needed = buf_len;
				return NDIS_STATUS_BUFFER_TOO_SHORT;
			}

			OffloadCapabilities(p_adapter, &offload);
			src_buf = &offload;
			break;			
		case OID_GEN_INTERRUPT_MODERATION:
			InterruptModerationParam.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
			InterruptModerationParam.Header.Revision =
							NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
			InterruptModerationParam.Header.Size =
						NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
			InterruptModerationParam.Flags =
								NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_RESET;
			InterruptModerationParam.InterruptModeration =
										NdisInterruptModerationNotSupported;
			buf_len = sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS);
			src_buf = (PVOID) &InterruptModerationParam;  
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
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received an unsupported Optional %s\n",
				port_num, GetOidName(oid_info.oid)) );
			break;
	
		case OID_GEN_PROTOCOL_OPTIONS:
		case OID_GEN_NETWORK_LAYER_ADDRESSES:
		case OID_GEN_TRANSPORT_HEADER_OFFSET:
		case OID_PNP_ENABLE_WAKE_UP:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for %s\n",
					port_num, GetOidName(oid_info.oid)) );
			status = NDIS_STATUS_SUCCESS; 
			break;
			
		case OID_PNP_QUERY_POWER:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_PNP_QUERY_POWER\n", port_num) );
			// Status is pre-set in this routine to Success
			status = NDIS_STATUS_SUCCESS; 
			break;
	
		case OID_TCP_OFFLOAD_CURRENT_CONFIG:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for OID_TCP_OFFLOAD_CURRENT_CONFIG\n",
					port_num) );
				//ulBytesAvailable = ulInfoLen = sizeof(NDIS_OFFLOAD);
				if (oid_info.buf_len <	sizeof(NDIS_OFFLOAD))
				{
					
					*oid_info.p_bytes_needed = sizeof(NDIS_OFFLOAD) ;
					return NDIS_STATUS_BUFFER_TOO_SHORT;
				}
	
				OffloadConfig( p_adapter, &offload);
				src_buf = &offload;
				break;

		case OID_IP6_OFFLOAD_STATS:
		case OID_IP4_OFFLOAD_STATS:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query for UNSUPPORTED "
				 "OID_IP[4,6]_OFFLOAD_STATS\n", port_num) );
			status = NDIS_STATUS_NOT_SUPPORTED;
			break;

		case OID_GEN_MACHINE_NAME:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query OID_GEN_MACHINE_NAME\n", port_num) );
			status = NDIS_STATUS_NOT_SUPPORTED;
			break;

#if defined(NDIS630_MINIPORT)

		case OID_NDK_LOCAL_ENDPOINTS:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query OID_NDK_LOCAL_ENDPOINTS\n", port_num) );

			status = ndk_ca_enumerate_local_endpoints(
				p_adapter->h_ndk,
				oid_info.p_buf,
				oid_info.buf_len, 
				oid_info.p_bytes_used,
				oid_info.p_bytes_needed
				);

			src_buf = NULL;
			buf_len = *oid_info.p_bytes_used;			 
			break;

		case OID_NDK_CONNECTIONS:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
				("Port %d received query OID_NDK_CONNECTIONS\n", port_num) );

			status = ndk_ca_enumerate_connections(
				p_adapter->h_ndk,
				oid_info.p_buf,
				oid_info.buf_len, 
				oid_info.p_bytes_used,
				oid_info.p_bytes_needed
				);

			src_buf = NULL;
			buf_len = *oid_info.p_bytes_used;
			break;			  
#endif	

		default:
			status = NDIS_STATUS_INVALID_OID;
			IPOIB_PRINT( TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR,
				("Port %d received an invalid oid of 0x%.8X!\n",
				port_num, oid_info.oid) );
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
			return ipoib_complete_query( p_adapter,
										 &oid_info,
										 status,
										 src_buf,
										 buf_len );
		}
	
		IPOIB_EXIT( IPOIB_DBG_OID );
		return NDIS_STATUS_PENDING;
}


static NDIS_STATUS
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
			//oid_status = NDIS_STATUS_INVALID_LENGTH;
			oid_status = NDIS_STATUS_BUFFER_TOO_SHORT;
			*p_oid_info->p_bytes_needed = buf_len;
			*p_oid_info->p_bytes_used = 0;
		}
		else if( p_oid_info->p_buf )
		{
			/* Only copy if we have a distinct source buffer. */
			if( p_buf )
			{
				memcpy( p_oid_info->p_buf, p_buf, buf_len );
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

	if (p_adapter->query_oid.p_pending_oid)
	{
		NdisMOidRequestComplete(p_adapter->h_adapter,p_adapter->query_oid.p_pending_oid,oid_status); 
		p_adapter->query_oid.p_pending_oid = NULL;
	}
	p_adapter->pending_query = FALSE;
	IPOIB_EXIT( IPOIB_DBG_OID );
	return oid_status;
}

NDIS_STATUS
ipoib_direct_query_info(
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;

#if defined(NDIS630_MINIPORT)
    
	NDIS_OID oid = pNdisRequest->DATA.QUERY_INFORMATION.Oid;
	PVOID information_buffer = pNdisRequest->DATA.QUERY_INFORMATION.InformationBuffer;
	ULONG information_buffer_length = pNdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
	PUINT p_bytes_written = &pNdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
	PUINT p_bytes_needed = &pNdisRequest->DATA.QUERY_INFORMATION.BytesNeeded;
	ipoib_adapter_t	*p_adapter;
	uint8_t port_num;

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t *) adapter_context;

	CL_ASSERT( p_bytes_written );
	*p_bytes_written = 0;

	CL_ASSERT( p_bytes_needed );
	*p_bytes_needed = 0;

	status = NDIS_STATUS_SUCCESS;
	
	port_num = p_adapter->guids.port_num;

	switch( oid )
	{
		case OID_NDK_STATISTICS:
			{
				IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
					("Port %d received query for OID_NDK_STATISTICS\n", port_num) );

				NDIS_NDK_STATISTICS_INFO *ndkStat = (NDIS_NDK_STATISTICS_INFO *) information_buffer;
			
				if(ndkStat->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
				   ndkStat->Header.Revision != NDIS_NDK_STATISTICS_INFO_REVISION_1) 
				{
					status = NDIS_STATUS_INVALID_DEVICE_REQUEST;
					break;
				}

				if(information_buffer_length < NDIS_SIZEOF_NDK_STATISTICS_INFO_REVISION_1 || 
				   ndkStat->Header.Size < NDIS_SIZEOF_NDK_STATISTICS_INFO_REVISION_1) 
				{
					status = NDIS_STATUS_BUFFER_TOO_SHORT;
					*p_bytes_needed = NDIS_SIZEOF_NDK_STATISTICS_INFO_REVISION_1;
					break;
				}
				
				status = ndk_query_ndk_stat(p_adapter->h_ndk, &ndkStat->CounterSet);

				*p_bytes_written = NDIS_SIZEOF_NDK_STATISTICS_INFO_REVISION_1;
			}
			break;
            
        default:
            ASSERT(FALSE);
            status = NDIS_STATUS_NOT_SUPPORTED;
            break;
	}
#else
    UNUSED_PARAM(adapter_context);
    UNUSED_PARAM(pNdisRequest);
#endif

	return status;
}

static
NDIS_STATUS 
SetOffloadEncapsulation(
	void* const pBuf,
	ULONG len 
	)
{
	ASSERT(pBuf != NULL);
	
	PNDIS_OFFLOAD_ENCAPSULATION pOffload;
	PNDIS_OBJECT_HEADER pOffloadHeader;

	if (len != sizeof(NDIS_OFFLOAD_ENCAPSULATION))
	{
		ASSERT(FALSE);
		IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_OID, ("Buffer is too small. offloading task requirs %d but the buffer size is: %d \n", sizeof(NDIS_OFFLOAD_ENCAPSULATION), len));
		return	NDIS_STATUS_INVALID_DATA;
	} 

	pOffload= (PNDIS_OFFLOAD_ENCAPSULATION) pBuf;
	pOffloadHeader = &(pOffload->Header);

	if((pOffloadHeader->Type != NDIS_OBJECT_TYPE_OFFLOAD_ENCAPSULATION) ||
	   (pOffloadHeader->Revision != NDIS_OFFLOAD_ENCAPSULATION_REVISION_1) ||
	   (pOffloadHeader->Size != NDIS_SIZEOF_OFFLOAD_ENCAPSULATION_REVISION_1))
	{		
		ASSERT(FALSE);
		IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_OID, ("Set offloading task Illegal header\n"));
		return NDIS_STATUS_INVALID_DATA;
	}

	//
	// BUGBUG: Need to handle the offload parameter setting
	//
	return NDIS_STATUS_SUCCESS;
}


//! Issues a hardware reset to the NIC and/or resets the driver's software state.
/*	Tear down the connection and start over again.	This is only called when there is a problem.
For example, if a send, query info, or set info had a time out.  MiniportCheckForHang will
be called first.
IRQL <= DISPATCH_LEVEL

@param p_addr_resetPointer to BOOLLEAN that is set to TRUE if the NDIS
library should call MiniportSetInformation to restore addressing information to the current values.
@param adapter_context The adapter context allocated at start
@return NDIS_STATUS_SUCCESS, NDIS_STATUS_PENDING, NDIS_STATUS_NOT_RESETTABLE,
NDIS_STATUS_RESET_IN_PROGRESS, NDIS_STATUS_SOFT_ERRORS, NDIS_STATUS_HARD_ERRORS
*/
NDIS_STATUS
ipoib_reset(
	IN	NDIS_HANDLE 	adapter_context,
	OUT PBOOLEAN		p_addr_reset)
{
	ipoib_adapter_t* p_adapter;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	CL_ASSERT( p_addr_reset );
	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;
	++g_reset;
	
	ib_api_status_t status = ipoib_reset_adapter( p_adapter );
	switch( status )
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
/*	For example, this is used to set multicast addresses and the packet filter.
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
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest)

{
	ipoib_adapter_t*	p_adapter;
	NDIS_STATUS			status;

	ULONG				buf_len;
	uint8_t				port_num;

	KLOCK_QUEUE_HANDLE	hdl;
	
	IPOIB_ENTER( IPOIB_DBG_OID );

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	NDIS_OID	oid = pNdisRequest->DATA.SET_INFORMATION.Oid;
	PVOID		info_buf = pNdisRequest->DATA.SET_INFORMATION.InformationBuffer;
	UINT		info_buf_len = pNdisRequest->DATA.SET_INFORMATION.InformationBufferLength;
	PULONG		p_bytes_read = (PULONG)&pNdisRequest->DATA.SET_INFORMATION.BytesRead;
	PULONG		p_bytes_needed = (PULONG)&pNdisRequest->DATA.SET_INFORMATION.BytesNeeded;
	
	CL_ASSERT( p_bytes_read );
	CL_ASSERT( p_bytes_needed );
	CL_ASSERT( !p_adapter->pending_set );

	status = NDIS_STATUS_SUCCESS;
	*p_bytes_needed = 0;
	buf_len = sizeof(ULONG);

	port_num = p_adapter->guids.port_num;
	
	cl_obj_lock( &p_adapter->obj );

	if( p_adapter->state == IB_PNP_PORT_REMOVE )
	{
		*p_bytes_read = 0;
		cl_obj_unlock( &p_adapter->obj );
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	cl_obj_unlock( &p_adapter->obj );

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
				p_adapter->set_oid.p_pending_oid = pNdisRequest;
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
				}
				else if( p_adapter->packet_filter && !(*(uint32_t*)info_buf) )
				{
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

	case OID_GEN_MAXIMUM_TOTAL_SIZE:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received set for OID_GEN_MAXIMUM_TOTAL_SIZE %ld\n",
				port_num, *((ULONG*)info_buf)) );
		if( info_buf_len < buf_len )
			status = NDIS_STATUS_INVALID_LENGTH;
		break;

	case OID_GEN_CURRENT_LOOKAHEAD:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received set for OID_GEN_CURRENT_LOOKAHEAD %ld\n",
				port_num, *((ULONG*)info_buf)) );
		if( info_buf_len < buf_len )
			status = NDIS_STATUS_INVALID_LENGTH;
		break;

	case OID_GEN_PROTOCOL_OPTIONS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received set for OID_GEN_PROTOCOL_OPTIONS\n", port_num));
		if( info_buf_len < buf_len )
			status = NDIS_STATUS_INVALID_LENGTH;
		break;

	case OID_GEN_MACHINE_NAME:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OID,
			("Port %d received set for OID_GEN_MACHINE_NAME\n", port_num) );
		break;

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
			ipoib_refresh_mcast( p_adapter,
								 (mac_addr_t*)info_buf,
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
		ASSERT (FALSE);
		IPOIB_PRINT( TRACE_LEVEL_ERROR,IPOIB_DBG_ERROR,
			("Port %d received set for OID_TCP_TASK_OFFLOAD\n", port_num) );

		buf_len = info_buf_len;
		//status =
		//	__ipoib_set_tcp_task_offload( p_adapter, info_buf, &buf_len );
		break;


	case OID_TCP_OFFLOAD_PARAMETERS:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received set for OID_TCP_OFFLOAD_PARAMETERS\n", port_num) );
		buf_len = info_buf_len;
		status = SetOffloadParameters(p_adapter, info_buf, info_buf_len);
		break;

	/* Optional General */
	case OID_GEN_TRANSPORT_HEADER_OFFSET:
		status = NDIS_STATUS_NOT_SUPPORTED;
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,IPOIB_DBG_OID,
			("Port %d received an unsupported %s\n",
				port_num, GetOidName(oid)));
		break;

	case OID_GEN_INTERRUPT_MODERATION:
		status = SetInterruptModeration(info_buf, info_buf_len);
		break;

	case OID_OFFLOAD_ENCAPSULATION:
		 status = SetOffloadEncapsulation(info_buf, info_buf_len);
		 break;

#if defined(NDIS630_MINIPORT)
		case OID_NDK_SET_STATE:
		{
		   if (info_buf_len != sizeof(BOOLEAN))
		   {
			  status = NDIS_STATUS_INVALID_DATA;
		   }
		   else
		   {
			  //
			  // Never issue NDK PnP events in the context of the MiniportRequestHandler.
			  // Otherwise, we'll lead to a deadlock. So, the handleNDKEnableDisable
			  // serializes this work to a worker thread.
			  //
			  // Miniport also must return success in reponse to the OID_NDK_SET_STATE
			  // request (i.e., do not return STATUS_PENDING).
			  //
			  BOOLEAN requestedState = *(UNALIGNED BOOLEAN *)info_buf;
		
			  ndk_enable_disable(p_adapter->h_ndk, requestedState);
			  
			  buf_len = info_buf_len;
		   }
		   break;
		}
#endif	
	
	case OID_GEN_SUPPORTED_LIST:
	//case OID_GEN_HARDWARE_STATUS:
	case OID_GEN_MEDIA_SUPPORTED:
	case OID_GEN_MEDIA_IN_USE:
	case OID_GEN_MAXIMUM_FRAME_SIZE:
	case OID_GEN_LINK_SPEED:
	case OID_GEN_TRANSMIT_BUFFER_SPACE:
	case OID_GEN_RECEIVE_BUFFER_SPACE:
	case OID_GEN_MAXIMUM_LOOKAHEAD:
	case OID_GEN_TRANSMIT_BLOCK_SIZE:
	case OID_GEN_RECEIVE_BLOCK_SIZE:
	case OID_GEN_VENDOR_ID:
	case OID_GEN_VENDOR_DESCRIPTION:
	case OID_GEN_VENDOR_DRIVER_VERSION:
	case OID_GEN_DRIVER_VERSION:
	case OID_GEN_MAC_OPTIONS:
	case OID_GEN_MEDIA_CONNECT_STATUS:
	case OID_GEN_MAXIMUM_SEND_PACKETS:
	case OID_GEN_SUPPORTED_GUIDS:
	case OID_GEN_PHYSICAL_MEDIUM:
	case OID_GEN_NETWORK_LAYER_ADDRESSES:
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
		
	default:
		status = NDIS_STATUS_INVALID_OID;
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port %d received unsupported %s\n", port_num, GetOidName(oid)));
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


static NDIS_STATUS
ipoib_oid_handler(
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest)
{
	NDIS_REQUEST_TYPE		RequestType;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_OID );

	RequestType = pNdisRequest->RequestType;

	switch(RequestType)
	{
		case NdisRequestSetInformation: 		   
			status = ipoib_set_info(adapter_context, pNdisRequest);
			break;
				
		case NdisRequestQueryInformation:
		case NdisRequestQueryStatistics:
			status = ipoib_query_info(adapter_context, pNdisRequest);
						
			break;

		default:
			status = NDIS_STATUS_NOT_SUPPORTED;
			break;
	}
	IPOIB_EXIT( IPOIB_DBG_OID );
	return status;
}

NDIS_STATUS
ipoib_direct_oid_handler(
	IN		NDIS_HANDLE 		adapter_context,
	IN	OUT PNDIS_OID_REQUEST	pNdisRequest)
{
	NDIS_REQUEST_TYPE		RequestType;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_OID );

	RequestType = pNdisRequest->RequestType;

	switch(RequestType)
	{
		case NdisRequestQueryInformation:
		case NdisRequestQueryStatistics:
			status = ipoib_direct_query_info(adapter_context, pNdisRequest);						
			break;

		case NdisRequestSetInformation: 		   
		default:
			ASSERT(FALSE);
			status = NDIS_STATUS_NOT_SUPPORTED;
			break;
	}
	IPOIB_EXIT( IPOIB_DBG_OID );
	return status;
}

/* Transfers some number of packets, specified as an array of packet pointers,
 *	over the network. 
 *	For a deserialized driver, these packets are completed asynchronously
 *	using NdisMSendComplete.
 * IRQL <= DISPATCH_LEVEL

 @param adapter_context - Pointer to ipoib_adapter_t structure with per NIC state
 @param net_buffer_list - Array of packets to send
 @param port_num - miniport number.
 @param send_flags - NDIS send flags.
*/
void
ipoib_send_net_buffer_list(
	IN	NDIS_HANDLE 		adapter_context,
	IN	PNET_BUFFER_LIST	net_buffer_list,
	IN	NDIS_PORT_NUMBER	port_num,
	IN	ULONG				send_flags )
{
	ipoib_adapter_t		*p_adapter;
	ipoib_port_t		*p_port;
	ULONG				send_complete_flags;
	PNET_BUFFER_LIST	curr_net_buffer_list;
	PNET_BUFFER_LIST	next_net_buffer_list;
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;
	
	UNREFERENCED_PARAMETER(port_num);
	PERF_DECLARE( SendPackets );
	PERF_DECLARE( PortSend );

	IPOIB_ENTER( IPOIB_DBG_SEND );
	cl_perf_start( SendPackets );

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	cl_obj_lock( &p_adapter->obj );
	if( p_adapter->ipoib_state & IPOIB_PAUSING ||
		p_adapter->ipoib_state & IPOIB_PAUSED)
	{
		status = NDIS_STATUS_PAUSED; 
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Got send during PAUSE, complete with error \n") );
		cl_obj_unlock( &p_adapter->obj );
		goto compl_status;
	}

	if( p_adapter->state != IB_PNP_PORT_ACTIVE || !p_adapter->p_port )
	{
		cl_obj_unlock( &p_adapter->obj );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Got send during port non-active, complete with error \n") );
		status = NDIS_STATUS_FAILURE; 
		goto compl_status;
	}

	p_port = p_adapter->p_port;
	ipoib_port_ref( p_port, ref_send_packets );
	cl_obj_unlock( &p_adapter->obj );

	for (curr_net_buffer_list = net_buffer_list;
		curr_net_buffer_list != NULL;
		curr_net_buffer_list = next_net_buffer_list)
	{
		++g_NBL;
		ipoib_cnt_inc( &p_adapter->n_send_NBL );
		
		next_net_buffer_list = NET_BUFFER_LIST_NEXT_NBL(curr_net_buffer_list);
		KeMemoryBarrierWithoutFence();
		// Important issue, break the connection between the different nbls
		NET_BUFFER_LIST_NEXT_NBL(curr_net_buffer_list) = NULL;
		
		cl_perf_start( PortSend );
		IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_BUF,
				("Sending NBL=%p, g_NBL=%d, g_NBL_completed=%d \n",
				curr_net_buffer_list, g_NBL, g_NBL_complete) );

		ipoib_port_send( p_port, curr_net_buffer_list, send_flags);

		cl_perf_stop( &p_adapter->perf, PortSend );
	}
	ipoib_port_deref( p_port, ref_send_packets );

	cl_perf_stop( &p_adapter->perf, SendPackets );

	cl_perf_log( &p_adapter->perf, SendBundle, 1 );

compl_status:
	if (status != NDIS_STATUS_SUCCESS)
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Got bad status, g_NBL=%d, g_NBL_completed=%d \n",
				g_NBL, g_NBL_complete) );
		send_complete_flags = 0;
		
		for (curr_net_buffer_list = net_buffer_list;
				 curr_net_buffer_list != NULL;
				 curr_net_buffer_list = next_net_buffer_list)
		{
			++g_NBL;
			ipoib_cnt_inc( &p_adapter->n_send_NBL );
			
			next_net_buffer_list = NET_BUFFER_LIST_NEXT_NBL(curr_net_buffer_list);
			NET_BUFFER_LIST_STATUS(curr_net_buffer_list) = status;
			ipoib_inc_send_stat( p_adapter, IP_STAT_DROPPED, 0 );
		}
		// Pay attention, we complete here the LIST OF NBL in one shot
		if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(send_flags))
		{
			ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
			NDIS_SET_SEND_COMPLETE_FLAG(send_complete_flags,
									NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
		}
		NdisMSendNetBufferListsCompleteX( p_adapter,
										  net_buffer_list,
										  send_complete_flags ); 
	}
	IPOIB_EXIT( IPOIB_DBG_SEND );
}


void
ipoib_pnp_notify(
	IN				NDIS_HANDLE					adapter_context,
	IN PNET_DEVICE_PNP_EVENT  pnp_event)
{
	ipoib_adapter_t	*p_adapter;

	IPOIB_ENTER( IPOIB_DBG_PNP );

	p_adapter = (ipoib_adapter_t*)adapter_context;

	IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_PNP,
		("Event %d\n", pnp_event->DevicePnPEvent) );
	if( pnp_event->DevicePnPEvent != NdisDevicePnPEventPowerProfileChanged )
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

	IPOIB_EXIT( IPOIB_DBG_PNP );
}


VOID
ipoib_shutdown_ex(
	IN NDIS_HANDLE	adapter_context,
	IN NDIS_SHUTDOWN_ACTION  shutdown_action)
{
	KLOCK_QUEUE_HANDLE	hdl;
	ipoib_adapter_t *p_adapter = (ipoib_adapter_t *) adapter_context;
	
	IPOIB_ENTER( IPOIB_DBG_INIT ) ;
	UNUSED_PARAM( shutdown_action );
	
	if( shutdown_action == NdisShutdownPowerOff )
	{
		ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
		// We need to wait only if this is not a blue screen any way
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Shutter shut, state = %d\n", p_adapter->ipoib_state));
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Shutter shut, state = %d\n", p_adapter->ipoib_state));
		// Wait until NDIS will return all indicated NBLs that were received
		// Avoid shutting the shutter twice
		KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
		
		if ( p_adapter->ipoib_state == IPOIB_RUNNING ) 
		{ //ensure that there was no active reset
			p_adapter->ipoib_state |= IPOIB_RESET_OR_DOWN;
			KeReleaseInStackQueuedSpinLock( &hdl );
			shutter_shut( &p_adapter->recv_shutter );
			// Notify that shutter was already shut
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION,  IPOIB_DBG_SHUTTER,
				("ipoib_state was IPOIB_RUNNING and IPOIB_RESET_OR_DOWN flag was set\n") );
		}
		else
		{
			if ( p_adapter->ipoib_state & IPOIB_RESET_OR_DOWN ) 
			{
				IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_ALL,
				("Shutdown occured while reset process wasn't completed yet\n") );
			}
			KeReleaseInStackQueuedSpinLock( &hdl );
		}
	}

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
		case OID_GEN_LINK_SPEED:
			ipoib_complete_query( p_adapter, &query_oid,
				status, &p_adapter->port_rate, sizeof(p_adapter->port_rate) );
			break;

		case OID_GEN_MEDIA_CONNECT_STATUS:
			info = NdisMediaStateConnected;
			ipoib_complete_query( p_adapter, &query_oid,
				status, &info, sizeof(info) );
			break;

		default:
			CL_ASSERT( query_oid.oid == OID_GEN_LINK_SPEED ||
				query_oid.oid == OID_GEN_MEDIA_CONNECT_STATUS );
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
			}
			else if( p_adapter->packet_filter && !(*(PULONG)set_oid.p_buf) )
			{
				ASSERT( cl_qlist_count( &g_ipoib.adapter_list ) );
				cl_qlist_remove_item(
					&g_ipoib.adapter_list, &p_adapter->entry );
			}
			p_adapter->packet_filter = *(PULONG)set_oid.p_buf;

			cl_obj_unlock( &p_adapter->obj );
			KeReleaseInStackQueuedSpinLock( &hdl );
			p_adapter->set_oid.p_pending_oid = NULL;
			NdisMOidRequestComplete( p_adapter->h_adapter, set_oid.p_pending_oid, status );
			break;

		default:
			CL_ASSERT( set_oid.oid && 0 );
			break;
		}
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


void
ipoib_cancel_xmit(
	IN				NDIS_HANDLE		adapter_context,
	IN				PVOID			cancel_id )
{

	ipoib_adapter_t* const p_adapter =
		(ipoib_adapter_t* const )adapter_context;
	
	if( p_adapter && p_adapter->p_port )
	{
		ipoib_port_cancel_xmit( p_adapter->p_port, cancel_id );
	}

	return;

}


static NDIS_STATUS 
ipoib_pause(
	IN	NDIS_HANDLE 						adapter_context,	
	IN	PNDIS_MINIPORT_PAUSE_PARAMETERS 	pause_parameters)
{
	ipoib_adapter_t		*p_adapter;
	KLOCK_QUEUE_HANDLE	hdl;
	ipoib_state_t 		prev_state;
	cl_qlist_t	complete_list;

	UNREFERENCED_PARAMETER( pause_parameters );
	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( adapter_context );
	p_adapter = (ipoib_adapter_t*)adapter_context;
	cl_qlist_init(&complete_list);
	
	KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
	prev_state = p_adapter->ipoib_state;
	CL_ASSERT ( prev_state & IPOIB_RUNNING );
	p_adapter->ipoib_state = IPOIB_PAUSING;
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION,  IPOIB_DBG_SHUTTER,
		("[%I64u]ipoib_state changed to IPOIB_PAUSING\n", cl_get_time_stamp()) );
	KeReleaseInStackQueuedSpinLock( &hdl );
	
	if( p_adapter->p_port )
	{
		cl_spinlock_acquire( &p_adapter->p_port->send_lock );
		ipoib_port_resume( p_adapter->p_port,FALSE, &complete_list );
		cl_spinlock_release( &p_adapter->p_port->send_lock );
		send_complete_list_complete(&complete_list, IRQL_TO_COMPLETE_FLAG());
	}
	if ( prev_state == IPOIB_RUNNING ) { //i.e there was no active reset
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Shutter shut, state = %d\n", p_adapter->ipoib_state) );
		shutter_shut ( &p_adapter->recv_shutter );
	}
	else {
		ASSERT ( prev_state & IPOIB_RESET_OR_DOWN);
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
		("[%I64u]Got pause during reset or got reset when IPoIB port wasn't active\n", cl_get_time_stamp()) );
	}
	
	KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
	p_adapter->ipoib_state = IPOIB_PAUSED;
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,  IPOIB_DBG_SHUTTER,
		("[%I64u]ipoib_state changed to IPOIB_PAUSED\n", cl_get_time_stamp()) );
	KeReleaseInStackQueuedSpinLock( &hdl );

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS 
ipoib_restart(
	IN	NDIS_HANDLE 						adapter_context,	
	IN	PNDIS_MINIPORT_RESTART_PARAMETERS	restart_parameters)
{
	ipoib_adapter_t		*p_adapter;
	KLOCK_QUEUE_HANDLE	hdl;
	PNDIS_RESTART_ATTRIBUTES	 NdisRestartAttributes;
	PNDIS_RESTART_GENERAL_ATTRIBUTES  NdisGeneralAttributes;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	NdisRestartAttributes = restart_parameters->RestartAttributes;

	if (NdisRestartAttributes != NULL)
	{
		CL_ASSERT(NdisRestartAttributes->Oid == OID_GEN_MINIPORT_RESTART_ATTRIBUTES);
		NdisGeneralAttributes = (PNDIS_RESTART_GENERAL_ATTRIBUTES)NdisRestartAttributes->Data;	 
		//
		// Check to see if we need to change any attributes
	}
	
	if ( p_adapter->ipoib_state & (IPOIB_PAUSED | IPOIB_INIT) )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("[%I64u]Shutter Alive, ipoib_state = %d\n",
				cl_get_time_stamp(), p_adapter->ipoib_state));
		shutter_alive( &p_adapter->recv_shutter );
	}
	else
	{
		IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_SHUTTER,
		("*****Shutter Was not \"Alived\", state = %d*****\n", p_adapter->ipoib_state));
	}

	KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
	p_adapter->ipoib_state = IPOIB_RUNNING;
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION,  IPOIB_DBG_SHUTTER,
		("[%I64u]ipoib_state changed to IPOIB_RUNNING\n",cl_get_time_stamp()) );
	KeReleaseInStackQueuedSpinLock( &hdl );


	IPOIB_EXIT( IPOIB_DBG_INIT );
	return NDIS_STATUS_SUCCESS;
}

/*++
Routine Description:

	This function aborts the request pending in the miniport.

Arguments:

	MiniportAdapterContext	Pointer to the adapter structure
	RequestId				Specify the request to be cancelled.
	
Return Value:
	
--*/
static void
ipoib_cancel_oid_request(
	IN	NDIS_HANDLE 		   adapter_context,
	IN	PVOID				   requestId
	)
{
	PNDIS_OID_REQUEST	 pending_request;
	ipoib_adapter_t		 *p_adapter;

	IPOIB_ENTER( IPOIB_DBG_OID );
	p_adapter = (ipoib_adapter_t*)adapter_context;

	cl_obj_lock( &p_adapter->obj );
	
	if ( p_adapter->query_oid.p_pending_oid &&
		p_adapter->query_oid.p_pending_oid->RequestId == requestId)
	{
		pending_request = p_adapter->query_oid.p_pending_oid;
		p_adapter->query_oid.p_pending_oid = NULL;
		p_adapter->pending_query = FALSE;
	}
	else if(p_adapter->set_oid.p_pending_oid && 
			p_adapter->set_oid.p_pending_oid->RequestId == requestId)
	{
		 pending_request = p_adapter->set_oid.p_pending_oid;
		 p_adapter->set_oid.p_pending_oid = NULL;
		 p_adapter->pending_set = FALSE;
	}
	else
	{
		cl_obj_unlock( &p_adapter->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
		("No Pending OID found\n") );
		return;
	}
	cl_obj_unlock( &p_adapter->obj );

	NdisMOidRequestComplete(p_adapter->h_adapter, 
							pending_request, 
							NDIS_STATUS_REQUEST_ABORTED);

	IPOIB_EXIT( IPOIB_DBG_OID );
}

void
ipoib_cancel_direct_oid_request(
	IN	NDIS_HANDLE 		   adapter_context,
	IN	PVOID				   requestId
	)
{
	UNUSED_PARAM(adapter_context);
	UNUSED_PARAM(requestId);
	ASSERT(FALSE);
}

#if defined(NDIS630_MINIPORT)

#define GLOBAL_ALLOCATION_TAG ' kdN'

class ZeroMemoryClass {
} zmClass;

void* __cdecl operator new(size_t n ) throw() {

	//From WinDDK: "Avoid calling ExAllocatePoolWithTag with memory size == 0. Doing so will result in pool header wastage"
	// Verifier with low mem simulation will crash with  memory size == 0
	//TODO throw exception
	if (n ==0) {
		return &zmClass;
	}
    
    void * p = ExAllocatePoolWithTag(NonPagedPool , n, GLOBAL_ALLOCATION_TAG);
    if (p) {
        RtlZeroMemory(p , n);
    }
    return p;
}

void __cdecl operator delete(void* p) {
	if (p != &zmClass)
    {   
        ExFreePoolWithTag(p, GLOBAL_ALLOCATION_TAG);
    }
    
}

#endif
