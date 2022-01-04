/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#include "srp_data.h"
#include "srp_data_path.h"
#include "srp_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srp_driver.tmh"
#endif
#include "srp_descriptors.h"
#include "srp_hba.h"
#include "srp_session.h"

#include <complib/cl_math.h>
#include <complib/cl_mutex.h>
#include <complib/cl_init.h>


#define SCSI_MAXIMUM_TRANSFER_SIZE (1024 * 1024)

BOOLEAN             g_srp_system_shutdown = FALSE;


uint32_t			g_srp_dbg_level = TRACE_LEVEL_ERROR;
uint32_t			g_srp_dbg_flags = 0x0000ffff;
uint32_t			g_srp_mode_flags = 0;

char g_srb_function_name[][32] =
{
	"EXECUTE_SCSI",          // 0x00
	"CLAIM_DEVICE",          // 0x01
	"IO_CONTROL",            // 0x02
	"RECEIVE_EVENT",         // 0x03
	"RELEASE_QUEUE",         // 0x04
	"ATTACH_DEVICE",         // 0x05
	"RELEASE_DEVICE",        // 0x06
	"SHUTDOWN",              // 0x07
	"FLUSH",                 // 0x08
	"",                      // 0x09
	"",                      // 0x0A
	"",                      // 0x0B
	"",                      // 0x0C
	"",                      // 0x0D
	"",                      // 0x0E
	"",                      // 0x0F
	"ABORT_COMMAND",         // 0x10
	"RELEASE_RECOVERY",      // 0x11
	"RESET_BUS",             // 0x12
	"RESET_DEVICE",          // 0x13
	"TERMINATE_IO",          // 0x14
	"FLUSH_QUEUE",           // 0x15
	"REMOVE_DEVICE",         // 0x16
	"WMI",                   // 0x17
	"LOCK_QUEUE",            // 0x18
	"UNLOCK_QUEUE",          // 0x19
	"",                      // 0x1A
	"",                      // 0x1B
	"",                      // 0x1C
	"",                      // 0x1D
	"",                      // 0x1E
	"",                      // 0x1F
	"RESET_LOGICAL_UNIT",    // 0x20
	"SET_LINK_TIMEOUT",      // 0x21
	"LINK_TIMEOUT_OCCURRED", // 0x22
	"LINK_TIMEOUT_COMPLETE"  // 0x23
};

char g_srb_status_name[][32] =
{
	"PENDING",                // 0x00
	"SUCCESS",                // 0x01
	"ABORTED",                // 0x02
	"ABORT_FAILED",           // 0x03
	"ERROR",                  // 0x04
	"BUSY",                   // 0x05
	"INVALID_REQUEST",        // 0x06
	"INVALID_PATH_ID",        // 0x07
	"NO_DEVICE",              // 0x08
	"TIMEOUT",                // 0x09
	"SELECTION_TIMEOUT",      // 0x0A
	"COMMAND_TIMEOUT",        // 0x0B
	"",                       // 0x0C
	"MESSAGE_REJECTED",       // 0x0D
	"BUS_RESET",              // 0x0E
	"PARITY_ERROR",           // 0x0F
	"REQUEST_SENSE_FAILED",   // 0x10
	"NO_HBA",                 // 0x11
	"DATA_OVERRUN",           // 0x12
	"UNEXPECTED_BUS_FREE",    // 0x13
	"PHASE_SEQUENCE_FAILURE", // 0x14
	"BAD_SRB_BLOCK_LENGTH",   // 0x15
	"REQUEST_FLUSHED",        // 0x16
	"",                       // 0x17
	"",                       // 0x18
	"",                       // 0x19
	"",                       // 0x1A
	"",                       // 0x1B
	"",                       // 0x1C
	"",                       // 0x1D
	"",                       // 0x1E
	"",                       // 0x1F
	"INVALID_LUN",            // 0x20
	"INVALID_TARGET_ID",      // 0x21
	"BAD_FUNCTION",           // 0x22
	"ERROR_RECOVERY",         // 0x23
	"NOT_POWERED",            // 0x24
	"LINK_DOWN"               // 0x25
};

char g_srb_scsi_status_name[][32] =
{
	"SCSISTAT_GOOD",				//0x00
	"",								//0x01
	" SCSISTAT_CHECK_CONDITION",	//0x02
	"",								//0x03
	" SCSISTAT_CONDITION_MET",		//0x04
	"",								//0x05
	"",								//0x06
	"",								//0x07
	" SCSISTAT_BUSY",				//0x08
	"",								//0x09
	"",								//0x0A
	"",								//0x0B
	"",								//0x0C
	"",								//0x0D
	"",								//0x0E
	"",								//0x0F
	" SCSISTAT_INTERMEDIATE",		//0x10
	"",								//0x11
	"",								//0x12
	"",								//0x13
	" SCSISTAT_INTERMEDIATE_COND_MET", //0x14
	"",								//0x15
	"",								//0x16
	"",								//0x17
	" SCSISTAT_RESERVATION_CONFLICT",  //0x18
	"",								//0x19
	"",								// 0x1A
	"",								// 0x1B
	"",								// 0x1C
	"",								// 0x1D
	"",								// 0x1E
	"",								// 0x1F
	"",								//0x20
	"",								//0x21
	" SCSISTAT_COMMAND_TERMINATED",    //0x22
	"",								//0x23
	"",								//0x24
	"",								//0x25
	"",								//0x26
	"",								//0x27
	" SCSISTAT_QUEUE_FULL",			//0x28
};

DRIVER_OBJECT       *gp_drv_obj;
cl_obj_t            g_drv_obj;

/* Mutex protecting the next lower device object pointer. */
KMUTEX              g_srp_pnp_mutex;

PDRIVER_DISPATCH    gpfn_pnp;
PDRIVER_ADD_DEVICE  gpfn_add_device;
PDRIVER_UNLOAD      gpfn_unload;


static NTSTATUS
__read_registry(
	IN				UNICODE_STRING* const		p_Param_Path );

NTSTATUS
srp_add_device(
	IN              DRIVER_OBJECT               *p_drv_obj,
	IN              DEVICE_OBJECT               *p_pdo );

NTSTATUS
srp_dispatch_pnp(
	IN              DEVICE_OBJECT               *p_dev_obj,
	IN              IRP                         *p_irp );

BOOLEAN
srp_init(
	IN              PVOID                       p_dev_ext );

BOOLEAN
srp_start_io(
	IN              PVOID                       p_dev_ext,
	IN              PSCSI_REQUEST_BLOCK         p_srb );

BOOLEAN
srp_isr(
	IN              PVOID                       p_dev_ext );

ULONG
srp_find_adapter(
	IN              PVOID                       p_dev_ext,
	IN              PVOID                       resv1,
	IN              PVOID                       resv2,
	IN              PCHAR                       arg_str,
	IN  OUT         PPORT_CONFIGURATION_INFORMATION     p_config,
		OUT         PBOOLEAN                    resv3 );

BOOLEAN
srp_reset(
	IN              PVOID                       p_dev_ext,
	IN              ULONG                       path_id );

SCSI_ADAPTER_CONTROL_STATUS
srp_adapter_ctrl(
	IN              PVOID                       p_dev_ext,
	IN              SCSI_ADAPTER_CONTROL_TYPE   ctrl_type,
	IN              PVOID                       params );

BOOLEAN
srp_build_io(
	IN              PVOID                       p_dev_ext,
	IN              PSCSI_REQUEST_BLOCK         p_srb );

static void
srp_unload(
	IN              DRIVER_OBJECT               *p_drv_obj );

static void
__srp_free(
	IN              cl_obj_t                    *p_obj );

#if DBG

void
srp_x_print(
	IN	void		*p_session );

void
srp_x_clean(
	IN	void		*p_session );

void* gp_session[SRP_MAX_SERVICE_ENTRIES];

#endif


static NTSTATUS
__read_registry(
	IN				UNICODE_STRING* const		p_registry_path )
{
	NTSTATUS					status;
	/* Remember the terminating entry in the table below. */
	RTL_QUERY_REGISTRY_TABLE	table[4];
	UNICODE_STRING				param_path;

	SRP_ENTER( SRP_DBG_PNP );

	RtlInitUnicodeString( &param_path, NULL );
	param_path.MaximumLength = p_registry_path->Length + 
		sizeof(L"\\Parameters");
	param_path.Buffer = cl_zalloc( param_path.MaximumLength );
	if( !param_path.Buffer )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR, 
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
	table[0].EntryContext = &g_srp_dbg_level;
	table[0].DefaultType = REG_DWORD;
	table[0].DefaultData = &g_srp_dbg_level;
	table[0].DefaultLength = sizeof(ULONG);

	table[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[1].Name = L"DebugFlags";
	table[1].EntryContext = &g_srp_dbg_flags;
	table[1].DefaultType = REG_DWORD;
	table[1].DefaultData = &g_srp_dbg_flags;
	table[1].DefaultLength = sizeof(ULONG);

	table[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[2].Name = L"ModeFlags";
	table[2].EntryContext = &g_srp_mode_flags;
	table[2].DefaultType = REG_DWORD;
	table[2].DefaultData = &g_srp_mode_flags;
	table[2].DefaultLength = sizeof(ULONG);


	/* Have at it! */
	status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
		param_path.Buffer, table, NULL, NULL );

#ifndef EVENT_TRACING
	if( g_srp_dbg_flags & SRP_DBG_ERR )
		g_srp_dbg_flags |= CL_DBG_ERROR;
#endif

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("debug level %d debug flags 0x%.8x\n",
		g_srp_dbg_level,
		g_srp_dbg_flags) );

	cl_free( param_path.Buffer );
	SRP_EXIT( SRP_DBG_PNP );
	return status;
}


ULONG
DriverEntry(
	IN              DRIVER_OBJECT               *p_drv_obj,
	IN              UNICODE_STRING              *p_registry_path )
{
	ULONG                       status;
	HW_INITIALIZATION_DATA      hw_data;
	cl_status_t                 cl_status;

	SRP_ENTER( SRP_DBG_PNP );

#if defined(EVENT_TRACING)
	WPP_INIT_TRACING( p_drv_obj, p_registry_path );
#endif

	status = CL_INIT;
	if( !NT_SUCCESS(status) )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("cl_init returned %08X.\n", status) );
		return status;
	}

	gp_drv_obj = p_drv_obj;

	/* Get the registry values. */
	status = __read_registry( p_registry_path );
	if( !NT_SUCCESS(status) )
	{
		CL_DEINIT;
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("__read_registry returned %08x.\n", status) );
		return status;
	}

	cl_obj_construct( &g_drv_obj, SRP_OBJ_TYPE_DRV );

	KeInitializeMutex( &g_srp_pnp_mutex, 0 );

	cl_memclr( &hw_data, sizeof(HW_INITIALIZATION_DATA) );

	hw_data.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

	hw_data.AdapterInterfaceType = Internal;

	/* Miniport driver routines */
	hw_data.HwInitialize     = srp_init;
	hw_data.HwStartIo        = srp_start_io;
//	hw_data.HwInterrupt      = srp_isr;
	hw_data.HwInterrupt      = NULL;
	hw_data.HwFindAdapter    = srp_find_adapter;
	hw_data.HwResetBus       = srp_reset;
	hw_data.HwAdapterControl = srp_adapter_ctrl;
	hw_data.HwBuildIo        = srp_build_io;
	hw_data.HwDmaStarted     = NULL;
	hw_data.HwAdapterState   = NULL;

	/* Extension sizes. */
	hw_data.DeviceExtensionSize = sizeof(srp_ext_t);
	/* TODO: Do we need per-LU data? */
	hw_data.SpecificLuExtensionSize = 0;
	hw_data.SrbExtensionSize = sizeof(srp_send_descriptor_t);

	/* Driver parameters. */
	hw_data.NumberOfAccessRanges = 1;
	/* TODO: Can this be STOR_MAP_NO_BUFFERS? */
	hw_data.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;

	hw_data.NeedPhysicalAddresses = TRUE;
	hw_data.TaggedQueuing = TRUE;
	hw_data.AutoRequestSense = TRUE;
	hw_data.MultipleRequestPerLu = TRUE;

	cl_status =
		cl_obj_init( &g_drv_obj, CL_DESTROY_SYNC, NULL, NULL, __srp_free );
	if( cl_status == CL_SUCCESS )
	{
		// Invoke the port initialization function.
		status = StorPortInitialize(p_drv_obj, p_registry_path, &hw_data, NULL);
		if( NT_SUCCESS( status ) )
		{
			/*
			 * Overwrite the PnP entrypoint, but save the original
			 * so we can call it.
			 */
			gpfn_pnp = p_drv_obj->MajorFunction[IRP_MJ_PNP];
			p_drv_obj->MajorFunction[IRP_MJ_PNP] = srp_dispatch_pnp;
			gpfn_add_device = p_drv_obj->DriverExtension->AddDevice;
			p_drv_obj->DriverExtension->AddDevice = srp_add_device;
			gpfn_unload = p_drv_obj->DriverUnload;
			p_drv_obj->DriverUnload = srp_unload;
		}
		else
		{
			CL_DEINIT;
			SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				("StorPortInitialize returned 0x%x.\n", status) );
		}
	}
	else
	{
		CL_DEINIT;
		status = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
	}

	SRP_PRINT_EXIT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("DriverEntry returning status of 0x%x.\n", status) );
	return status;
}

static void
srp_unload(
	IN              DRIVER_OBJECT               *p_drv_obj )
{
	SRP_ENTER( SRP_DBG_PNP );
#if defined(EVENT_TRACING)
	WPP_CLEANUP( p_drv_obj );
#endif

	/* Kill all SRP objects. */
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
		("Destroying all SRP objects.\n") );
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("Driver Object ref_cnt = %d\n", g_drv_obj.ref_cnt) );
	cl_obj_destroy( &g_drv_obj );

	CL_DEINIT;

	/* Invoke the port driver's unload routine. */
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
		("Invoking the port driver's unload routine.\n") );
	gpfn_unload( p_drv_obj );

	SRP_EXIT( SRP_DBG_PNP );
}


static void
__srp_free(
	IN              cl_obj_t                    *p_obj )
{
//  CL_ASSERT( p_obj == &g_drv_obj );
	UNUSED_PARAM ( p_obj );
	cl_obj_deinit( &g_drv_obj );
}


NTSTATUS
srp_add_device(
	IN              DRIVER_OBJECT               *p_drv_obj,
	IN              DEVICE_OBJECT               *p_pdo )
{
	NTSTATUS    status;

	SRP_ENTER( SRP_DBG_PNP );

	status = gpfn_add_device( p_drv_obj, p_pdo );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("srp_add_device status = 0x%x.\n", status) );

	SRP_EXIT( SRP_DBG_PNP );
	return status;
}


NTSTATUS
srp_dispatch_pnp(
	IN              DEVICE_OBJECT               *p_dev_obj,
	IN              IRP                         *p_irp )
{
	NTSTATUS            status;
	IO_STACK_LOCATION   *p_stack;
	UCHAR               minor;
	SRP_ENTER( SRP_DBG_PNP );

	p_stack = IoGetCurrentIrpStackLocation( p_irp );
	minor = p_stack->MinorFunction;
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("Minor PNP Function = %d.\n", minor) );

	if( minor == IRP_MN_START_DEVICE )
	{
		NTSTATUS    wait_status;

		wait_status = KeWaitForMutexObject(
			&g_srp_pnp_mutex, Executive, KernelMode, FALSE, NULL );

		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
			("KeWaitForMutexObject status = 0x%x.\n", wait_status) );
		gp_self_do = p_dev_obj;
	}
	status = gpfn_pnp( p_dev_obj, p_irp );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("gpfn_pnp status = 0x%x.\n", status) );

	if( minor == IRP_MN_START_DEVICE )
	{
		LONG    release_status;
		gp_self_do = NULL;
		release_status = KeReleaseMutex( &g_srp_pnp_mutex, FALSE );
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
			("KeReleaseMutex status = %d.\n", release_status) );
	}

	#if DBG_STATISTICS
	/* statistics */

	/* this function is called sometimes in the begging of the test with 
		IRP_MN_QUERY_DEVICE_RELATIONS (7) request. Use this fact to print the statistics */
	{
		/* sometimes it's called once in 50msec, so we'll print once in 20 times */
		static int interval = 40; /* 2 sec */
		static int cnt = 0;
		static int i;
		if (++cnt >= interval)
		{
			cnt = 0;
			if(i > 3 ) i = 0;
			srp_x_print( gp_session[i] );
			srp_x_clean( gp_session[i] );
			i++;
		}
	}
	
	#endif

	SRP_EXIT( SRP_DBG_PNP );
	return status;
}


ULONG
srp_find_adapter(
	IN				PVOID						p_dev_ext,
	IN				PVOID						resv1,
	IN				PVOID						resv2,
	IN				PCHAR						arg_str,
	IN	OUT			PPORT_CONFIGURATION_INFORMATION		p_config,
		OUT			PBOOLEAN					resv3 )
{
	srp_ext_t			*p_ext;
	ib_api_status_t		ib_status;

	SRP_ENTER( SRP_DBG_PNP );

	UNUSED_PARAM( resv1 );
	UNUSED_PARAM( resv2 );
	UNUSED_PARAM( resv3 );
	UNUSED_PARAM( arg_str );
	UNUSED_PARAM( p_config );

	if( KeGetCurrentIrql() >= DISPATCH_LEVEL )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Improper IRQL!\n") );
		return SP_RETURN_ERROR;
	}

	p_ext = (srp_ext_t*)p_dev_ext;

	ib_status = srp_hba_create( &g_drv_obj, p_ext );
	if( ib_status != IB_SUCCESS )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("srp_hba_create returned %d\n", ib_status) );
		return SP_RETURN_ERROR;
	}

	p_config->SrbExtensionSize            = MAX( p_ext->p_hba->max_srb_ext_sz, sizeof( srp_send_descriptor_t ));
	//CL_ASSERT( p_config->SrbExtensionSize >= sizeof( srp_send_descriptor_t ) );

	p_config->MaximumTransferLength       = SCSI_MAXIMUM_TRANSFER_SIZE;
	p_config->AlignmentMask               = 0; /* byte alignment */
	p_config->NumberOfBuses               = 1;
	p_config->ScatterGather               = TRUE;
    p_config->Master                      = TRUE; // The HBA is a "bus" master.
//	p_config->CachesData                  = TRUE; // Assume the HBA does cache data.
	p_config->CachesData                  = FALSE; // Assume the HBA does not cache data.
	p_config->MaximumNumberOfTargets      = p_ext->p_hba->ioc_info.profile.num_svc_entries;
	p_config->MaximumNumberOfLogicalUnits = SCSI_MAXIMUM_LUNS_PER_TARGET;
	p_config->MultipleRequestPerLu        = TRUE;
	p_config->SynchronizationModel        = StorSynchronizeFullDuplex;
	p_config->MapBuffers                  = STOR_MAP_NON_READ_WRITE_BUFFERS;
	p_config->ResetTargetSupported        = FALSE;

//	p_config->InitiatorBusId[0]           = 127;
//	p_config->DeviceExtensionSize         = sizeof( srp_ext_t );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("NumberOfPhysicalBreaks passed in = %d.\n", p_config->NumberOfPhysicalBreaks) );

	if ( p_config->NumberOfPhysicalBreaks == SP_UNINITIALIZED_VALUE )
	{
		p_config->NumberOfPhysicalBreaks = p_ext->p_hba->max_sg - 1;
	}
	else
	{
		if (g_srp_mode_flags & SRP_MODE_SG_UNLIMITED)
			// It is prohibited by DDK, but seems like work
			p_config->NumberOfPhysicalBreaks = p_ext->p_hba->max_sg - 1;
		else
			p_config->NumberOfPhysicalBreaks = MIN( p_ext->p_hba->max_sg - 1, p_config->NumberOfPhysicalBreaks );
	}

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		( "max_sg %d, New NumberOfPhysicalBreaks %d\n", 
		 p_ext->p_hba->max_sg, p_config->NumberOfPhysicalBreaks));

	SRP_EXIT( SRP_DBG_PNP );
	return SP_RETURN_FOUND;
}

BOOLEAN
srp_init(
	IN              PVOID                       p_dev_ext )
{
	SRP_ENTER( SRP_DBG_PNP );

	UNUSED_PARAM( p_dev_ext );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("called at IRQL %d\n", KeGetCurrentIrql()) );

	SRP_EXIT( SRP_DBG_PNP );
	return TRUE;
}

BOOLEAN
srp_start_io(
	IN              PVOID                       p_dev_ext,
	IN              PSCSI_REQUEST_BLOCK         p_srb )
{
	SRP_ENTER( SRP_DBG_DATA );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			   ("Starting I/O for Function = %s(0x%x), Path = 0x%x, "
			   "Target = 0x%x, Lun = 0x%x\n",
			   g_srb_function_name[p_srb->Function],
			   p_srb->Function,
			   p_srb->PathId,
			   p_srb->TargetId,
			   p_srb->Lun) );

	CL_ASSERT( p_srb->SrbExtension != NULL );

	// Check the operation here
	switch ( p_srb->Function )
	{
		case SRB_FUNCTION_EXECUTE_SCSI:
			srp_post_io_request( p_dev_ext, p_srb );
			break;

		case SRB_FUNCTION_ABORT_COMMAND:
			srp_abort_command( p_dev_ext, p_srb  );
			break;
#if !defined(WinXP)
		case SRB_FUNCTION_RESET_LOGICAL_UNIT:
#endif
		case SRB_FUNCTION_RESET_DEVICE:
			srp_lun_reset( p_dev_ext, p_srb  );
			break;

		case SRB_FUNCTION_SHUTDOWN: /* Only receive this if CachesData is TRUE in PORT_CONFIGURATION_INFORMATION */
		{
			srp_hba_t       *p_hba         = ((srp_ext_t *)p_dev_ext)->p_hba;
			srp_session_t   *p_srp_session = p_hba->session_list[p_srb->TargetId];

			g_srp_system_shutdown = TRUE;

			if ( (p_srb->Lun == 0) && (p_srp_session != NULL) )
			{
				p_srp_session->p_shutdown_srb = p_srb;

				if( !p_hba->adapter_stopped )
				{
					p_hba->adapter_stopped = TRUE;
					srp_disconnect_sessions( p_hba );
				}
			}
			else
			{
				p_srb->SrbStatus = SRB_STATUS_SUCCESS;
				SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
						   ("Returning SrbStatus %s(0x%x) for "
						   "Function = %s(0x%x), Path = 0x%x, "
						   "Target = 0x%x, Lun = 0x%x\n",
						   g_srb_status_name[p_srb->SrbStatus],
						   p_srb->SrbStatus,
						   g_srb_function_name[p_srb->Function],
						   p_srb->Function,
						   p_srb->PathId,
						   p_srb->TargetId,
						   p_srb->Lun) );
				StorPortNotification( RequestComplete, p_dev_ext, p_srb );
			}
			break;
		}

		case SRB_FUNCTION_FLUSH: /* Only receive this if CachesData is TRUE in PORT_CONFIGURATION_INFORMATION */
			p_srb->SrbStatus = SRB_STATUS_SUCCESS;
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
					   ("Returning SrbStatus %s(0x%x) for "
					   "Function = %s(0x%x), Path = 0x%x, "
					   "Target = 0x%x, Lun = 0x%x\n",
					   g_srb_status_name[p_srb->SrbStatus],
					   p_srb->SrbStatus,
					   g_srb_function_name[p_srb->Function],
					   p_srb->Function,
					   p_srb->PathId,
					   p_srb->TargetId,
					   p_srb->Lun) );
			StorPortNotification( RequestComplete, p_dev_ext, p_srb );
			break;

		case SRB_FUNCTION_IO_CONTROL: /***** May Need To Support *****/

		case SRB_FUNCTION_RESET_BUS:
		case SRB_FUNCTION_TERMINATE_IO:
		case SRB_FUNCTION_RELEASE_RECOVERY:
		case SRB_FUNCTION_RECEIVE_EVENT:
		case SRB_FUNCTION_LOCK_QUEUE:
		case SRB_FUNCTION_UNLOCK_QUEUE:
		case SRB_FUNCTION_CLAIM_DEVICE:
		case SRB_FUNCTION_RELEASE_QUEUE:
		case SRB_FUNCTION_ATTACH_DEVICE:
		case SRB_FUNCTION_RELEASE_DEVICE:
		case SRB_FUNCTION_FLUSH_QUEUE:
		case SRB_FUNCTION_REMOVE_DEVICE:
		case SRB_FUNCTION_WMI:
#if !defined(WinXP)
		case SRB_FUNCTION_SET_LINK_TIMEOUT:
		case SRB_FUNCTION_LINK_TIMEOUT_OCCURRED:
		case SRB_FUNCTION_LINK_TIMEOUT_COMPLETE:
#endif
		default:
			p_srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
					   ("Returning SrbStatus %s(0x%x) for "
					   "Function = %s(0x%x), Path = 0x%x, "
					   "Target = 0x%x, Lun = 0x%x\n",
					   g_srb_status_name[p_srb->SrbStatus],
					   p_srb->SrbStatus,
					   g_srb_function_name[p_srb->Function],
					   p_srb->Function,
					   p_srb->PathId,
					   p_srb->TargetId,
					   p_srb->Lun) );
			StorPortNotification( RequestComplete, p_dev_ext, p_srb );

	}

	SRP_EXIT( SRP_DBG_DATA );

	return ( TRUE );
}

BOOLEAN
srp_isr(
	IN              PVOID                       p_dev_ext )
{
	SRP_ENTER( SRP_DBG_PNP );

	UNUSED_PARAM( p_dev_ext );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("called at IRQL %d\n", KeGetCurrentIrql()) );

	SRP_EXIT( SRP_DBG_PNP );
	return TRUE;
}

BOOLEAN
srp_reset(
	IN              PVOID                       p_dev_ext,
	IN              ULONG                       path_id )
{
	SRP_ENTER( SRP_DBG_PNP );

	UNUSED_PARAM( p_dev_ext );

	StorPortCompleteRequest( p_dev_ext, (UCHAR)path_id, SP_UNTAGGED, SP_UNTAGGED, SRB_STATUS_NO_HBA );
	
	SRP_EXIT( SRP_DBG_PNP );
	return FALSE;
}

SCSI_ADAPTER_CONTROL_STATUS
srp_adapter_ctrl(
	IN              PVOID                       p_dev_ext,
	IN              SCSI_ADAPTER_CONTROL_TYPE   ctrl_type,
	IN              PVOID                       params )
{
	srp_ext_t                           *p_ext;
	SCSI_SUPPORTED_CONTROL_TYPE_LIST    *p_ctrl_list;

	SRP_ENTER( SRP_DBG_PNP );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("called at IRQL %d\n", KeGetCurrentIrql()) );

	p_ext = (srp_ext_t*)p_dev_ext;

	switch( ctrl_type )
	{
	case ScsiQuerySupportedControlTypes:
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("ScsiQuerySupportedControlTypes\n") );
		p_ctrl_list = (SCSI_SUPPORTED_CONTROL_TYPE_LIST*)params;
		p_ctrl_list->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
		p_ctrl_list->SupportedTypeList[ScsiStopAdapter] = TRUE;
		p_ctrl_list->SupportedTypeList[ScsiRestartAdapter] = FALSE;
		p_ctrl_list->SupportedTypeList[ScsiSetBootConfig] = FALSE;
		p_ctrl_list->SupportedTypeList[ScsiSetRunningConfig] = FALSE;
		break;

	case ScsiStopAdapter:
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("ScsiStopAdapter\n") );
		if( p_ext->p_hba )
		{
			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
				("HBA Object ref_cnt = %d\n", p_ext->p_hba->obj.ref_cnt) );
			if( !p_ext->p_hba->adapter_stopped )
				p_ext->p_hba->adapter_stopped = TRUE;
			srp_disconnect_sessions( p_ext->p_hba );
			cl_obj_destroy( &p_ext->p_hba->obj );
			p_ext->p_hba = NULL;
		}
		break;

	case ScsiRestartAdapter:
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("ScsiRestartAdapter\n") );
		break;

	case ScsiSetBootConfig:
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("ScsiSetBootConfig\n") );
		break;

	case ScsiSetRunningConfig:
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("ScsiSetRunningConfig\n") );
		break;
	}

	SRP_EXIT( SRP_DBG_PNP );
	return ScsiAdapterControlSuccess;
}

BOOLEAN
srp_build_io(
	IN              PVOID                       p_dev_ext,
	IN              PSCSI_REQUEST_BLOCK         p_srb )
{
	SRP_ENTER( SRP_DBG_DATA );

	if ( p_srb->Function == SRB_FUNCTION_EXECUTE_SCSI )
	{

		CL_ASSERT( p_srb->SrbExtension != NULL );

		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
				   ("Building I/O for Function = %s(0x%x), "
				   "Path = 0x%x, Target = 0x%x, Lun = 0x%x\n",
				   g_srb_function_name[p_srb->Function],
				   p_srb->Function,
				   p_srb->PathId,
				   p_srb->TargetId,
				   p_srb->Lun) );

		if ( srp_format_io_request( p_dev_ext, p_srb ) == FALSE )
		{
			SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_DATA,
							("Returning SrbStatus %s(0x%x) for "
							"Function = %s(0x%x), Path = 0x%x, "
							"Target = 0x%x, Lun = 0x%x\n",
						    g_srb_status_name[p_srb->SrbStatus],
							p_srb->SrbStatus,
							g_srb_function_name[p_srb->Function],
							p_srb->Function,
							p_srb->PathId,
							p_srb->TargetId,
							p_srb->Lun) );

			StorPortNotification( RequestComplete, p_dev_ext, p_srb );

			return ( FALSE );
		}
	}

	SRP_EXIT( SRP_DBG_DATA );

	return ( TRUE );
}
