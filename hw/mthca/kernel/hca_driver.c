/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


/*
 * Provides the driver entry points for the Tavor VPD.
 */

#include <mt_utils.h>
#include "hca_driver.h"
#include "hca_debug.h"

#include "mthca_log.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_driver.tmh"
#endif
#include "mthca_dev.h"
#include <wdmguid.h>
#include <initguid.h>
#pragma warning( push, 3 )
//#include "MdCard.h"
#pragma warning( pop )
#include <iba/ib_ci_ifc.h>
#include "mthca/mthca_vc.h"
#include "mt_pa_cash.h"
/* from \inc\platform\evntrace.h
#define TRACE_LEVEL_NONE        0   // Tracing is not on
#define TRACE_LEVEL_FATAL       1   // Abnormal exit or termination
#define TRACE_LEVEL_ERROR       2   // Severe errors that need logging
#define TRACE_LEVEL_WARNING     3   // Warnings such as allocation failure
#define TRACE_LEVEL_INFORMATION 4   // Includes non-error cases(e.g.,Entry-Exit)
#define TRACE_LEVEL_VERBOSE     5   // Detailed traces from intermediate steps
*/
uint32_t g_mthca_dbg_level = TRACE_LEVEL_INFORMATION;
uint32_t g_mthca_dbg_flags= 0xffff;
WCHAR g_wlog_buf[ MAX_LOG_BUF_LEN ];
UCHAR g_slog_buf[ MAX_LOG_BUF_LEN ];
uint32_t g_skip_tavor_reset=0;		/* skip reset for Tavor cards */
uint32_t g_disable_tavor_reset=1;		/* disable Tavor reset for the next driver load */
uint32_t g_tune_pci=0;				/* 0 - skip tuning PCI configuration space of HCAs */
uint32_t g_processor_affinity = 0;
uint32_t g_max_DPC_time_us = 10000;
uint32_t g_profile_qp_num = 0;
uint32_t g_profile_rd_out = 0xffffffff;

UNICODE_STRING				g_param_path;


/*
 * UVP name does not include file extension.  For debug builds, UAL
 * will append "d.dll".  For release builds, UAL will append ".dll"
 */
char			mlnx_uvp_lib_name[MAX_LIB_NAME] = {"mthcau"};

void mlnx_poll_eq(struct ib_device *device, BOOLEAN bStart);


NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT				p_driver_obj,
	IN				PUNICODE_STRING				p_registry_path );

static NTSTATUS
__read_registry(
	IN				UNICODE_STRING* const		p_Param_Path );

static void
hca_drv_unload(
	IN				PDRIVER_OBJECT				p_driver_obj );

static NTSTATUS
hca_sysctl(
	IN				PDEVICE_OBJECT				p_dev_obj,
	IN				PIRP						p_irp );

static NTSTATUS
__pnp_notify_target(
	IN				TARGET_DEVICE_REMOVAL_NOTIFICATION	*p_notify,
	IN				void						*context );

static NTSTATUS
__pnp_notify_ifc(
	IN				DEVICE_INTERFACE_CHANGE_NOTIFICATION	*p_notify,
	IN				void						*context );

static NTSTATUS
fw_access_pciconf (
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		ULONG							op_flag,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length );

static NTSTATUS
fw_flash_write_data (
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length );

static NTSTATUS
fw_flash_read_data (
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length );

static NTSTATUS
fw_flash_read4( 
	IN			BUS_INTERFACE_STANDARD	*p_BusInterface,
	IN			uint32_t				addr, 
	IN	OUT		uint32_t				*p_data);

static NTSTATUS
fw_flash_readbuf(
	IN		BUS_INTERFACE_STANDARD	*p_BusInterface,
	IN		uint32_t				offset,
	IN OUT	void					*p_data,
	IN		uint32_t				len);
static NTSTATUS
fw_set_bank(
	IN		BUS_INTERFACE_STANDARD	*p_BusInterface,
	IN		uint32_t				bank );

static NTSTATUS
fw_flash_init(
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface  );

static NTSTATUS
fw_flash_deinit(
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface  );


NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT			p_driver_obj,
	IN				PUNICODE_STRING			p_registry_path )
{
	NTSTATUS			status;
	cl_status_t			cl_status;
#if defined(EVENT_TRACING)
	WPP_INIT_TRACING(p_driver_obj ,p_registry_path);
#endif
	HCA_ENTER( HCA_DBG_DEV );

	/* init common mechanisms */
	fill_bit_tbls();

	status = __read_registry( p_registry_path );
	if( !NT_SUCCESS( status ) )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_INIT, 
			("__read_registry_path returned 0x%X.\n", status));
		return status;
	}

	/* Initialize Adapter DB */
	cl_status = mlnx_hcas_init();
	if( cl_status != CL_SUCCESS )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_INIT ,
			("mlnx_hcas_init returned %#x.\n", cl_status));
		return cl_to_ntstatus( cl_status );
	}
//	cl_memclr( mlnx_hca_array, MLNX_MAX_HCA * sizeof(ci_interface_t) );

	/* init pa cash */
	status = pa_cash_init();
	if (status) 
	{
		HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_INIT ,
			("pa_cash_init failed.\n"));
		return status;
	}

	/*leo:  init function table */
	hca_init_vfptr();
	
	p_driver_obj->MajorFunction[IRP_MJ_PNP] = cl_pnp;
	p_driver_obj->MajorFunction[IRP_MJ_POWER] = cl_power;
	p_driver_obj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = hca_sysctl;
	p_driver_obj->DriverUnload = hca_drv_unload;
	p_driver_obj->DriverExtension->AddDevice = hca_add_device;

	/* init core */
	if (ib_core_init()) {
		HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_INIT ,("Failed to init core, aborting.\n"));
		return STATUS_UNSUCCESSFUL;
	}

	/* init uverbs module */
	if (ib_uverbs_init()) {
		HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_INIT ,("Failed ib_uverbs_init, aborting.\n"));
		return STATUS_UNSUCCESSFUL;
	}
	HCA_EXIT( HCA_DBG_DEV );
	return STATUS_SUCCESS;
}


static NTSTATUS
__read_registry(
	IN				UNICODE_STRING* const	p_registry_path )
{
	NTSTATUS					status;
	/* Remember the terminating entry in the table below. */
	RTL_QUERY_REGISTRY_TABLE	table[10];

	HCA_ENTER( HCA_DBG_DEV );

	RtlInitUnicodeString( &g_param_path, NULL );
	g_param_path.MaximumLength = p_registry_path->Length + 
		sizeof(L"\\Parameters");
	g_param_path.Buffer = cl_zalloc( g_param_path.MaximumLength );
	if( !g_param_path.Buffer )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_INIT, 
			("Failed to allocate parameters path buffer.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlAppendUnicodeStringToString( &g_param_path, p_registry_path );
	RtlAppendUnicodeToString( &g_param_path, L"\\Parameters" );

	/*
	 * Clear the table.  This clears all the query callback pointers,
	 * and sets up the terminating table entry.
	 */
	cl_memclr( table, sizeof(table) );

	/* Setup the table entries. */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[0].Name = L"DebugLevel";
	table[0].EntryContext = &g_mthca_dbg_level;
	table[0].DefaultType = REG_DWORD;
	table[0].DefaultData = &g_mthca_dbg_level;
	table[0].DefaultLength = sizeof(ULONG);

	
	table[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[1].Name = L"DebugFlags";
	table[1].EntryContext = &g_mthca_dbg_flags;
	table[1].DefaultType = REG_DWORD;
	table[1].DefaultData = &g_mthca_dbg_flags;
	table[1].DefaultLength = sizeof(ULONG);

	table[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[2].Name = L"SkipTavorReset";
	table[2].EntryContext = &g_skip_tavor_reset;
	table[2].DefaultType = REG_DWORD;
	table[2].DefaultData = &g_skip_tavor_reset;
	table[2].DefaultLength = sizeof(ULONG);

	table[3].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[3].Name = L"DisableTavorResetOnFailure";
	table[3].EntryContext = &g_disable_tavor_reset;
	table[3].DefaultType = REG_DWORD;
	table[3].DefaultData = &g_disable_tavor_reset;
	table[3].DefaultLength = sizeof(ULONG);

	table[4].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[4].Name = L"TunePci";
	table[4].EntryContext = &g_tune_pci;
	table[4].DefaultType = REG_DWORD;
	table[4].DefaultData = &g_tune_pci;
	table[4].DefaultLength = sizeof(ULONG);

	table[5].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[5].Name = L"ProcessorAffinity";
	table[5].EntryContext = &g_processor_affinity;
	table[5].DefaultType = REG_DWORD;
	table[5].DefaultData = &g_processor_affinity;
	table[5].DefaultLength = sizeof(ULONG);

	table[6].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[6].Name = L"MaxDpcTimeUs";
	table[6].EntryContext = &g_max_DPC_time_us;
	table[6].DefaultType = REG_DWORD;
	table[6].DefaultData = &g_max_DPC_time_us;
	table[6].DefaultLength = sizeof(ULONG);

	table[7].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[7].Name = L"ProfileQpNum";
	table[7].EntryContext = &g_profile_qp_num;
	table[7].DefaultType = REG_DWORD;
	table[7].DefaultData = &g_profile_qp_num;
	table[7].DefaultLength = sizeof(ULONG);

	table[8].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[8].Name = L"ProfileRdOut";
	table[8].EntryContext = &g_profile_rd_out;
	table[8].DefaultType = REG_DWORD;
	table[8].DefaultData = &g_profile_rd_out;
	table[8].DefaultLength = sizeof(ULONG);

	/* Have at it! */
	status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
		g_param_path.Buffer, table, NULL, NULL );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_INIT, 
		("debug level  %d debug flags 0x%.8x SkipTavorReset %d DisableTavorReset %d TunePci %d"
		"g_processor_affinity %d g_max_DPC_time_us %d g_profile_qp_num %d g_profile_rd_out %d\n",
		g_mthca_dbg_level, g_mthca_dbg_flags,
		g_skip_tavor_reset, g_disable_tavor_reset,
		g_tune_pci, g_processor_affinity, g_max_DPC_time_us,
		g_profile_qp_num, g_profile_rd_out ));

	HCA_EXIT( HCA_DBG_DEV );
	return status;
}

void set_skip_tavor_reset()
{
	NTSTATUS status;
	HANDLE key_handle;
	UNICODE_STRING key_name;
	ULONG val = 1;
	OBJECT_ATTRIBUTES oa;

	HCA_ENTER( HCA_DBG_DEV );

	InitializeObjectAttributes( &oa, &g_param_path, 
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL );


	status = ZwOpenKey( &key_handle, GENERIC_WRITE, &oa );
	if( !NT_SUCCESS( status ) ) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_LOW, 
			("ZwOpenKey failed (%#x)\n", status));
		goto err_open_key;
	}

	RtlInitUnicodeString( &key_name, L"SkipTavorReset" );
	status = ZwSetValueKey( key_handle, &key_name, 0, 
		REG_DWORD, &val, sizeof(ULONG) );
	if( !NT_SUCCESS( status ) ) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_LOW, 
			("ZwSetValueKey failed (%#x)\n", status));
	}

	ZwClose( key_handle );

err_open_key:
	HCA_EXIT( HCA_DBG_DEV );
}

static void
hca_drv_unload(
	IN				PDRIVER_OBJECT			p_driver_obj )
{
	HCA_ENTER( HCA_DBG_DEV );

	UNUSED_PARAM( p_driver_obj );

	pa_cash_release();
	ib_uverbs_cleanup();
	ib_core_cleanup();
	cl_free( g_param_path.Buffer );
	
	HCA_EXIT( HCA_DBG_DEV );
#if defined(EVENT_TRACING)
	WPP_CLEANUP(p_driver_obj);
#endif

}


static NTSTATUS
hca_sysctl(
	IN				PDEVICE_OBJECT				p_dev_obj,
	IN				PIRP						p_irp )
{
	NTSTATUS		status;
	hca_dev_ext_t	*p_ext;

	HCA_ENTER( HCA_DBG_DEV );

	p_ext = p_dev_obj->DeviceExtension;

	IoSkipCurrentIrpStackLocation( p_irp );
	status = IoCallDriver( p_ext->cl_ext.p_next_do, p_irp );

	HCA_EXIT( HCA_DBG_DEV );
	return status;
}

typedef struct Primary_Sector{
	uint32_t fi_addr;
	uint32_t fi_size;
	uint32_t signature;
	uint32_t fw_reserved[5];
	uint32_t vsd[56];
	uint32_t branch_to;
	uint32_t crc016;
} primary_sector_t;

static uint32_t old_dir;
static uint32_t old_pol;
static uint32_t old_mod;
static uint32_t old_dat;

static NTSTATUS
fw_access_pciconf (
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		ULONG							op_flag,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length )
{

	ULONG				bytes;	
	NTSTATUS			status = STATUS_SUCCESS;

	if( !p_buffer )
		return STATUS_INVALID_PARAMETER;

	if (p_BusInterface)
	{

		bytes = p_BusInterface->SetBusData(
						p_BusInterface->Context,
						PCI_WHICHSPACE_CONFIG,
						(PVOID)&offset,
						PCI_CONF_ADDR,
						sizeof(ULONG) );

		if( op_flag == 0 )
		{
			if ( bytes )
				bytes = p_BusInterface->GetBusData(
							p_BusInterface->Context,
							PCI_WHICHSPACE_CONFIG,
							p_buffer,
							PCI_CONF_DATA,
							length );
			if ( !bytes )
				status = STATUS_NOT_SUPPORTED;
		}

		else
		{
			if ( bytes )
				bytes = p_BusInterface->SetBusData(
							p_BusInterface->Context,
							PCI_WHICHSPACE_CONFIG,
							p_buffer,
							PCI_CONF_DATA,
							length);

			if ( !bytes )
				status = STATUS_NOT_SUPPORTED;
		}
	}
	return status;
}


static NTSTATUS
__map_crspace(
	IN		struct ib_ucontext *			p_context,
	IN		mlnx_hob_t			*	p_hob,
	IN		PVOID						p_buf,
	IN		ULONG						buf_size
	)
{
	NTSTATUS status;
	PMDL p_mdl;
	PVOID ua, ka;
	ULONG sz;
	hca_dev_ext_t *p_ext = EXT_FROM_HOB(p_hob);
	map_crspace *p_res = (map_crspace *)p_buf;

	HCA_ENTER( HCA_DBG_PNP );

	// sanity checks
	if ( buf_size < sizeof *p_res || !p_buf ) {
		status = STATUS_INVALID_PARAMETER;
		goto err_invalid_params;
	}

	// map memory
	sz =(ULONG)p_ext->bar[HCA_BAR_TYPE_HCR].size;
	if (!p_ext->bar[HCA_BAR_TYPE_HCR].virt) {
		PHYSICAL_ADDRESS pa;
		pa.QuadPart = p_ext->bar[HCA_BAR_TYPE_HCR].phys;
		ka = MmMapIoSpace( pa, sz, MmNonCached ); 
		if ( ka == NULL) {
			HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM,
				("No kernel mapping of CR space.\n") );
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto err_map_to_kernel;
		}
		p_ext->bar[HCA_BAR_TYPE_HCR].virt = ka;
	}
	ka = p_ext->bar[HCA_BAR_TYPE_HCR].virt;

	// prepare for mapping to user space 
	p_mdl = IoAllocateMdl( ka, sz, FALSE,FALSE,NULL);
	if (p_mdl == NULL) {
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM, 
			("IoAllocateMdl failed.\n") );
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto err_alloc_mdl;
	}

	// fill MDL
	MmBuildMdlForNonPagedPool(p_mdl);
	
	// map the buffer into user space 
	__try
	{
		ua = MmMapLockedPagesSpecifyCache( p_mdl, UserMode, MmNonCached,
			NULL, FALSE, NormalPagePriority );
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR , HCA_DBG_SHIM,
			("MmMapLockedPagesSpecifyCache failed.\n") );
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto err_map_to_user;
	}
	
	// fill the results
	p_res->va = (uint64_t)(ULONG_PTR)ua;
	p_res->size = sz;

	// resource tracking
	p_context->p_mdl = p_mdl;
	p_context->va = ua;

#if 0	
	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_SHIM,
		("MTHCA: __map_crspace succeeded with .ka %I64x, size %I64x va %I64x, size %x, pa %I64x \n",
		p_ext->bar[HCA_BAR_TYPE_HCR].virt, p_ext->bar[HCA_BAR_TYPE_HCR].size, 
		p_res->va, p_res->size, p_ext->bar[HCA_BAR_TYPE_HCR].phys ));
#endif
	status = STATUS_SUCCESS;
	goto out;

err_map_to_user:
	IoFreeMdl( p_mdl );
err_alloc_mdl:
err_map_to_kernel:
err_invalid_params:	
out:	
	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


static void
__unmap_crspace(
	IN		struct ib_ucontext *			p_context
	)
{
	HCA_ENTER( HCA_DBG_PNP );

	if (p_context->va && p_context->p_mdl) {
		MmUnmapLockedPages(p_context->va, p_context->p_mdl);
		IoFreeMdl( p_context->p_mdl );
		p_context->va = p_context->p_mdl = NULL;
		//NB: the unmap of IO space is being done in __UnmapHcaMemoryResources
	}

	HCA_EXIT( HCA_DBG_PNP );
}


static void
__open_fw_access(
	IN				struct ib_ucontext*			p_context,
	IN				PBUS_INTERFACE_STANDARD		p_bus_interface )
{
	if( !p_context->fw_if_open )
	{
		p_bus_interface->InterfaceReference( p_bus_interface->Context );
		p_context->fw_if_open = TRUE;
	}
}


static void 
__close_fw_access(
	IN		struct ib_ucontext *	p_context,
	IN		PBUS_INTERFACE_STANDARD	p_bus_interface
	)
{
	if (p_context->fw_if_open ) {
		p_bus_interface->InterfaceDereference((PVOID)p_bus_interface->Context);
		p_context->fw_if_open = FALSE;
	}
}


void
unmap_crspace_for_all( struct ib_ucontext *p_context )
{
	mlnx_hob_t	*p_hob = HOB_FROM_IBDEV( p_context->device );
	hca_dev_ext_t *p_ext = EXT_FROM_HOB(p_hob);
	PBUS_INTERFACE_STANDARD    p_bus_interface = &p_ext->hcaBusIfc;

	HCA_ENTER( HCA_DBG_PNP );

	down( &p_context->mutex );
	__unmap_crspace( p_context);
	__close_fw_access(p_context, p_bus_interface);
	up( &p_context->mutex );

	HCA_EXIT( HCA_DBG_PNP );
}

ib_api_status_t
fw_access_ctrl(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	void** const				handle_array	OPTIONAL,
	IN				uint32_t					num_handles,
	IN				ib_ci_op_t* const			p_ci_op,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	DEVICE_OBJECT				*p_dev_obj;
	PBUS_INTERFACE_STANDARD		p_bus_interface;
	NTSTATUS					status = STATUS_SUCCESS;
	PVOID						p_data;
	ULONG						offset;
	ULONG POINTER_ALIGNMENT		length;
	struct ib_ucontext *			p_context;
	mlnx_hob_t					*p_hob;
	hca_dev_ext_t *p_ext;

	UNREFERENCED_PARAMETER(handle_array);
	UNREFERENCED_PARAMETER(num_handles);

	if( !p_umv_buf )
	{
#if 1//WORKAROUND_POLL_EQ
		if ((p_ci_op->command == FW_POLL_EQ_START) || (p_ci_op->command == FW_POLL_EQ_STOP))
		{ // poll EQ (in case of missed interrupt) 
			struct ib_device *ib_dev;
			p_hob = (mlnx_hob_t *)h_ca;
			ib_dev = IBDEV_FROM_HOB( p_hob );

			if(p_ci_op->command == FW_POLL_EQ_START)
			{
				mlnx_poll_eq(ib_dev,1);
			}else
			{
				mlnx_poll_eq(ib_dev,0);
			}
			return IB_SUCCESS;
		}
		else
#endif
			return IB_UNSUPPORTED;
	}

	p_context = (struct ib_ucontext *)h_ca;
	p_hob = HOB_FROM_IBDEV( p_context->device );
	p_ext = EXT_FROM_HOB(p_hob);
	p_dev_obj = (DEVICE_OBJECT *)p_ext->cl_ext.p_self_do;
	p_bus_interface = &p_ext->hcaBusIfc;

	if ( !p_ci_op )
		return IB_INVALID_PARAMETER;

	length = p_ci_op->buf_size;
	offset = p_ci_op->buf_info;
	p_data = p_ci_op->p_buf;

	down( &p_context->mutex );

	switch ( p_ci_op->command )
	{
	case FW_MAP_CRSPACE:
		status = __map_crspace(p_context, p_hob, p_data, length);
		break;
		
	case FW_UNMAP_CRSPACE:
		__unmap_crspace(p_context);
		break;
				
	case FW_OPEN_IF: // open BusInterface
		__open_fw_access( p_context, p_bus_interface );
		break;

	case FW_READ: // read data from flash
		if ( p_context->fw_if_open )
			status = fw_flash_read_data(p_bus_interface, p_data, offset, length);
		break;

	case FW_WRITE: // write data to flash
		if ( p_context->fw_if_open )
			status = fw_flash_write_data(p_bus_interface, p_data, offset, length);
		break;

	case FW_READ_CMD:
		if ( p_context->fw_if_open )
			status = fw_access_pciconf(p_bus_interface, 0 , p_data, offset, 4);
		break;

	case FW_WRITE_CMD:
		if ( p_context->fw_if_open )
			status = fw_access_pciconf(p_bus_interface, 1 , p_data, offset, 4);
		break;

	case FW_CLOSE_IF: // close BusInterface
		__close_fw_access(p_context, p_bus_interface);
		break;

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
	}

	if ( status != STATUS_SUCCESS ) {
		__close_fw_access(p_context, p_bus_interface);
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_INIT, 
			("fw_access_ctrl failed, ntstatus: %08x.\n", status));
	}

	up( &p_context->mutex );

	switch( status ) {
		case STATUS_SUCCESS:					return IB_SUCCESS;
		case STATUS_INVALID_DEVICE_REQUEST:	return IB_UNSUPPORTED;
		case STATUS_INSUFFICIENT_RESOURCES:	return IB_INSUFFICIENT_RESOURCES;
		default:									return IB_ERROR;
	}
}

static NTSTATUS
fw_flash_write_data (
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length )
{
	NTSTATUS		status;
	uint32_t		cnt = 0;
	uint32_t		lcl_data;

	if (!length)
		return IB_INVALID_PARAMETER;
	
	lcl_data = (*((uint32_t*)p_buffer) << 24);

	status = fw_access_pciconf(p_BusInterface, FW_WRITE , &lcl_data, FLASH_OFFSET+4, length );
	if ( status != STATUS_SUCCESS )
		return status;
	lcl_data = ( WRITE_BIT | (offset & ADDR_MSK));
		
	status = fw_access_pciconf(p_BusInterface, FW_WRITE , &lcl_data, FLASH_OFFSET, 4 );
	if ( status != STATUS_SUCCESS )
	return status;

	lcl_data = 0;
	
	do
	{
		if (++cnt > 5000)
		{
			return STATUS_DEVICE_NOT_READY;
		}

		status = fw_access_pciconf(p_BusInterface, FW_READ , &lcl_data, FLASH_OFFSET, 4 );
		if ( status != STATUS_SUCCESS )
		return status;

	} while(lcl_data & CMD_MASK);

	return status;
}

static NTSTATUS
fw_flash_read_data (
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length )
{
	NTSTATUS	status = STATUS_SUCCESS;
	uint32_t	cnt = 0;
	uint32_t	lcl_data = ( READ_BIT | (offset & ADDR_MSK));

	if (!length)
		return IB_INVALID_PARAMETER;
	
	status = fw_access_pciconf(p_BusInterface, FW_WRITE, &lcl_data, FLASH_OFFSET, 4 );
	if ( status != STATUS_SUCCESS )
		return status;

	lcl_data = 0;
	do
	{
		// Timeout checks
		if (++cnt > 5000 )
		{
			return STATUS_DEVICE_NOT_READY;
	}

		status = fw_access_pciconf(p_BusInterface, FW_READ, &lcl_data, FLASH_OFFSET, 4 );
	
		if ( status != STATUS_SUCCESS )
			return status;

	} while(lcl_data & CMD_MASK);

	status = fw_access_pciconf(p_BusInterface, FW_READ, p_buffer, FLASH_OFFSET+4, length );
	return status;
}

static NTSTATUS
fw_flash_read4( 
	IN			BUS_INTERFACE_STANDARD	*p_BusInterface,
	IN			uint32_t				addr, 
	IN	OUT		uint32_t				*p_data)
{
	NTSTATUS	status = STATUS_SUCCESS;
	uint32_t lcl_data = 0;
	uint32_t bank;
	static uint32_t curr_bank =	0xffffffff;

	if (addr & 0x3)
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_INIT,
			("Invalid address %08x\n", addr) );
		return STATUS_INVALID_PARAMETER;
	}

	bank = addr & BANK_MASK;
	if (bank !=  curr_bank)
	{
		curr_bank = bank;
		if ((status = fw_set_bank(p_BusInterface, bank)) != STATUS_SUCCESS )
		{
			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_INIT,
				("fw_set_bank returned %08x\n", status) );
			return STATUS_INVALID_PARAMETER;
		}
	}
	status = fw_flash_read_data(p_BusInterface, &lcl_data, addr, 4);
	*p_data = cl_ntoh32(lcl_data);
	return STATUS_SUCCESS;
}

static NTSTATUS
fw_flash_readbuf(
		IN		BUS_INTERFACE_STANDARD	*p_BusInterface,
		IN		uint32_t				offset,
		IN OUT	void					*p_data,
		IN		uint32_t				len)
{
	NTSTATUS	status = STATUS_SUCCESS;
	uint32_t *p_lcl_data;
	uint32_t	i;

    if (offset & 0x3)
    {
        //Address should be 4-bytes aligned
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_INIT,
			("Invalid address %08x\n", offset) );
        return STATUS_INVALID_PARAMETER;
    }
    if (len & 0x3)
    {
        //Length should be 4-bytes aligned
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_INIT,
			("Invalid length %d\n", len) );
        return STATUS_INVALID_PARAMETER;
    }
    p_lcl_data = (uint32_t *)p_data;
    
	for ( i=0; i < (len >> 2); i++)
    {					
        if ( (status = fw_flash_read_data( p_BusInterface, p_lcl_data, offset, sizeof(uint32_t) )) != STATUS_SUCCESS )
            return status;
        offset += 4;
		p_lcl_data++;
    }
    return STATUS_SUCCESS;
} // Flash::flash_read

static NTSTATUS
fw_flash_writebuf(
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length )
{
	NTSTATUS status = STATUS_SUCCESS;
	uint32_t	i;
	uint8_t	*p_data = (uint8_t *)p_buffer;

	for ( i = 0; i < length;  i++ )
	{
		status = fw_flash_write_data (p_BusInterface, p_data, offset, 1 );
		if (status != STATUS_SUCCESS )
			return status;
		p_data++;
		offset++;
	}
	return status;
}
static NTSTATUS
fw_flash_init(
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface  )
{
	uint32_t dir;
    uint32_t pol;
    uint32_t mod;

    uint32_t cnt=0;
    uint32_t data;
	NTSTATUS status = STATUS_SUCCESS;
	uint32_t	semaphore = 0;
    
	while ( !semaphore )
	{
		status = fw_access_pciconf(p_BusInterface, FW_READ , &data, SEMAP63, 4);
		if ( status != STATUS_SUCCESS )
			break;
		if( !data )
		{
			semaphore = 1;
			break;
		}
        if (++cnt > 5000 )
        {
            break;
        }
    } 

	if ( !semaphore )
	{
		return STATUS_NOT_SUPPORTED;
	}

    // Save old values
    
	status = fw_access_pciconf(p_BusInterface, FW_READ , &old_dir,GPIO_DIR_L , 4);
	if ( status == STATUS_SUCCESS )
		status = fw_access_pciconf(p_BusInterface, FW_READ , &old_pol,GPIO_POL_L , 4);
	if ( status == STATUS_SUCCESS )
		status = fw_access_pciconf(p_BusInterface, FW_READ , &old_mod,GPIO_MOD_L , 4);
	if ( status == STATUS_SUCCESS )
		status = fw_access_pciconf(p_BusInterface, FW_READ , &old_dat,GPIO_DAT_L , 4);

   // Set Direction=1, Polarity=0, Mode=0 for 3 GPIO lower bits
    dir = old_dir | 0x70;
    pol = old_pol & ~0x70;
    mod = old_mod & ~0x70;

	status = fw_access_pciconf(p_BusInterface, FW_WRITE , &dir,GPIO_DIR_L , 4);
	if ( status == STATUS_SUCCESS )
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &pol,GPIO_POL_L , 4);
	if ( status == STATUS_SUCCESS )
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &mod,GPIO_MOD_L , 4);
	if ( status == STATUS_SUCCESS )
		// Set CPUMODE
		status = fw_access_pciconf(p_BusInterface, FW_READ , &data, CPUMODE, 4);
    if ( status == STATUS_SUCCESS )
	{
		data &= ~CPUMODE_MSK;
		data |= 1 << CPUMODE_SHIFT;
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &data, CPUMODE, 4);
	}
	if ( status == STATUS_SUCCESS )
	{
		// Reset flash
		data = 0xf0;
		status = fw_flash_write_data(p_BusInterface, &data, 0x0, 4);
	}
	return status;
}

static NTSTATUS
fw_flash_deinit(
	IN		BUS_INTERFACE_STANDARD	*p_BusInterface )
{
	uint32_t data = 0;
	NTSTATUS status = STATUS_SUCCESS;
    
	status = fw_set_bank(p_BusInterface, 0);
	if ( status == STATUS_SUCCESS )
		// Restore origin values
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &old_dir,GPIO_DIR_L , 4);
	if ( status == STATUS_SUCCESS )
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &old_pol,GPIO_POL_L , 4);
	if ( status == STATUS_SUCCESS )
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &old_mod,GPIO_MOD_L , 4);
	if ( status == STATUS_SUCCESS )
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &old_dat,GPIO_DAT_L , 4);
	if ( status == STATUS_SUCCESS )
		// Free GPIO Semaphore
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &data, SEMAP63, 4);
	return status;
}

static NTSTATUS
fw_set_bank(
	IN		BUS_INTERFACE_STANDARD	*p_BusInterface,
	IN		 uint32_t bank )
{
	NTSTATUS  status = STATUS_SUCCESS;
	uint32_t	data = ( (uint32_t)0x70 << 24 );
	uint32_t	mask = ((bank >> (BANK_SHIFT-4)) << 24 );

	status = fw_access_pciconf(p_BusInterface, FW_WRITE , &data, GPIO_DATACLEAR_L, 4);
	if (status == STATUS_SUCCESS)
	{
	// A1
		data &= mask;
		//data |= mask; // for A0
		status = fw_access_pciconf(p_BusInterface, FW_WRITE , &data, GPIO_DATASET_L, 4);
	}
	return status;
}
