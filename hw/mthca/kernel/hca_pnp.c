/* BEGIN_ICS_COPYRIGHT ****************************************
** END_ICS_COPYRIGHT   ****************************************/

/*
	$Revision: 1.1 $
*/


/*
 * Provides the driver entry points for the Tavor VPD.
 */

#include "hca_driver.h"
#include "mthca_dev.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_pnp.tmh"
#endif
#include "mthca.h"
#include <initguid.h>
#include <wdmguid.h>
#include <rdma\verbs.h>

extern const char *mthca_version;
hca_dev_ext_t *g_ext = NULL;

static NTSTATUS
hca_start(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_query_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_cancel_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_query_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static void
hca_release_resources(
	IN				DEVICE_OBJECT* const		p_dev_obj );

static NTSTATUS
hca_cancel_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_query_capabilities(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_query_pnp_state(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_query_bus_relations(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_query_removal_relations(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_query_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
hca_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static ci_interface_t*
__alloc_hca_ifc(
	IN				hca_dev_ext_t* const		p_ext );

static NTSTATUS
hca_query_interface(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__get_ci_interface(
	IN				DEVICE_OBJECT* const		p_dev_obj );



static cl_vfptr_pnp_po_t	vfptrHcaPnp;


void
hca_init_vfptr( void )
{
	vfptrHcaPnp.identity = "HCA driver";
	vfptrHcaPnp.pfn_start = hca_start;
	vfptrHcaPnp.pfn_query_stop = hca_query_stop;
	vfptrHcaPnp.pfn_stop = hca_stop;
	vfptrHcaPnp.pfn_cancel_stop = hca_cancel_stop;
	vfptrHcaPnp.pfn_query_remove = hca_query_remove;
	vfptrHcaPnp.pfn_release_resources = hca_release_resources;
	vfptrHcaPnp.pfn_remove = cl_do_remove;
	vfptrHcaPnp.pfn_cancel_remove = hca_cancel_remove;
	vfptrHcaPnp.pfn_surprise_remove = hca_surprise_remove;
	vfptrHcaPnp.pfn_query_capabilities = hca_query_capabilities;
	vfptrHcaPnp.pfn_query_pnp_state = hca_query_pnp_state;
	vfptrHcaPnp.pfn_filter_res_req = cl_irp_skip;
	vfptrHcaPnp.pfn_dev_usage_notification = cl_do_sync_pnp;
	vfptrHcaPnp.pfn_query_bus_relations = cl_irp_ignore;
	vfptrHcaPnp.pfn_query_ejection_relations = cl_irp_ignore;
	vfptrHcaPnp.pfn_query_removal_relations = cl_irp_ignore;
	vfptrHcaPnp.pfn_query_target_relations = cl_irp_ignore;
	vfptrHcaPnp.pfn_unknown = cl_irp_ignore;
	vfptrHcaPnp.pfn_query_resources = cl_irp_ignore;
	vfptrHcaPnp.pfn_query_res_req = cl_irp_ignore;
	vfptrHcaPnp.pfn_query_bus_info = cl_irp_ignore;
	vfptrHcaPnp.pfn_query_interface = hca_query_interface;
	vfptrHcaPnp.pfn_read_config = cl_irp_ignore;
	vfptrHcaPnp.pfn_write_config = cl_irp_ignore;
	vfptrHcaPnp.pfn_eject = cl_irp_ignore;
	vfptrHcaPnp.pfn_set_lock = cl_irp_ignore;
	vfptrHcaPnp.pfn_query_power = hca_query_power;
	vfptrHcaPnp.pfn_set_power = hca_set_power;
	vfptrHcaPnp.pfn_power_sequence = cl_irp_ignore;
	vfptrHcaPnp.pfn_wait_wake = cl_irp_ignore;
}


NTSTATUS
hca_add_device(
	IN				PDRIVER_OBJECT				pDriverObj,
	IN				PDEVICE_OBJECT				pPdo )
{
	NTSTATUS			status;
	DEVICE_OBJECT		*p_dev_obj, *pNextDevObj;
	hca_dev_ext_t		*p_ext;

	HCA_ENTER(HCA_DBG_PNP);

	/*
	 * Create the device so that we have a device extension to store stuff in.
	 */
	status = IoCreateDevice( pDriverObj, sizeof(hca_dev_ext_t),
		NULL, FILE_DEVICE_INFINIBAND, FILE_DEVICE_SECURE_OPEN,
		FALSE, &p_dev_obj );
	if( !NT_SUCCESS( status ) )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
			("IoCreateDevice returned 0x%08X.\n", status));
		return status;
	}

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	if (!g_ext)
		g_ext = p_ext;
	cl_memclr( p_ext, sizeof(hca_dev_ext_t) );
	cl_spinlock_init( &p_ext->uctx_lock );
	cl_qlist_init( &p_ext->uctx_list );
	atomic_set(&p_ext->usecnt, 0);

	/* Attach to the device stack. */
	pNextDevObj = IoAttachDeviceToDeviceStack( p_dev_obj, pPdo );
	if( !pNextDevObj )
	{
		//cl_event_destroy( &p_ext->mutex );
		IoDeleteDevice( p_dev_obj );
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
			("IoAttachDeviceToDeviceStack failed.\n"));
		return STATUS_NO_SUCH_DEVICE;
	}

	/* Inititalize the complib extension. */
	cl_init_pnp_po_ext( p_dev_obj, pNextDevObj, pPdo, CL_DBG_PNP | CL_DBG_ERROR,
		&vfptrHcaPnp, NULL );

	p_ext->state = HCA_ADDED;

	HCA_EXIT(HCA_DBG_PNP);
	return status;
}


static ci_interface_t*
__alloc_hca_ifc(
	IN				hca_dev_ext_t* const		p_ext )
{
	ci_interface_t	*pIfc;

	HCA_ENTER( HCA_DBG_PNP );

	pIfc = (ci_interface_t*)ExAllocatePoolWithTag( NonPagedPool,
												   sizeof(ci_interface_t),
												   'pnpa' );
	if( !pIfc )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR,HCA_DBG_PNP, 
			("Failed to allocate ci_interface_t (%d bytes).\n",
			sizeof(ci_interface_t)));
		return NULL;
	}

	setup_ci_interface( p_ext->hca.guid, pIfc );

	pIfc->p_hca_obj = &p_ext->hca.hob;
	pIfc->vend_id = (uint32_t)p_ext->hcaConfig.VendorID;
	pIfc->dev_id = (uint16_t)p_ext->hcaConfig.DeviceID;
	pIfc->dev_revision = (uint16_t)p_ext->hca.hw_ver;

	HCA_EXIT( HCA_DBG_PNP );
	return pIfc;
}


/*
 * Walk the resource lists and store the information.  The write-only
 * flag is not set for the UAR region, so it is indistinguishable from the
 * DDR region since both are prefetchable.  The code here assumes that the
 * resources get handed in order - HCR, UAR, DDR.
 *	- Configuration Space: not prefetchable, read/write
 *	- UAR space: prefetchable, write only.
 *	- DDR: prefetchable, read/write.
 */
static NTSTATUS
__SetupHcaResources(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				CM_RESOURCE_LIST* const		pHcaResList,
	IN				CM_RESOURCE_LIST* const		pHostResList )
{
	NTSTATUS						status = STATUS_SUCCESS;
	hca_dev_ext_t					*p_ext;
	USHORT							i;
	hca_bar_type_t					type = HCA_BAR_TYPE_HCR;

	CM_PARTIAL_RESOURCE_DESCRIPTOR	*pHcaRes, *pHostRes;

	HCA_ENTER( HCA_DBG_PNP );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	// store the bus number for reset of Tavor
	p_ext->bus_number = pHostResList->List[0].BusNumber;
	
	for( i = 0; i < pHostResList->List[0].PartialResourceList.Count; i++ )
	{
		pHcaRes =
			&pHcaResList->List[0].PartialResourceList.PartialDescriptors[i];
		pHostRes = 
			&pHostResList->List[0].PartialResourceList.PartialDescriptors[i];


		/*
		 * Save the interrupt information so that we can power the device
		 * up and down.  Since the device will lose state when powered down
		 * we have to fully disable it.  Note that we can leave memory mapped
		 * resources in place when powered down as the resource assignments
		 * won't change.  However, we must disconnect our interrupt, and
		 * reconnect it when powering up.
		 */
		if( pHcaRes->Type == CmResourceTypeInterrupt )
		{
			p_ext->interruptInfo = *pHostRes;
			if ( g_processor_affinity == 0xFFFFFFFF ) 
			{
				/* 
				 * Calculate the mask of the last processor
				 */
				KAFFINITY		n_active_processors_bitmask;
				uint32_t  		last_processor_mask = 0 , tmp_processor_mask = 1;
				
				n_active_processors_bitmask = KeQueryActiveProcessors();
				while ( tmp_processor_mask & n_active_processors_bitmask )
				{
						last_processor_mask = tmp_processor_mask;
						tmp_processor_mask = tmp_processor_mask << 1;
				}
				p_ext->interruptInfo.u.Interrupt.Affinity = last_processor_mask; 
			}
			else if (g_processor_affinity != 0) 
			{
				p_ext->interruptInfo.u.Interrupt.Affinity = g_processor_affinity;				
			}
			HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PNP,("Set Interrupt affinity to : 0x%08X\n",
					 (int)p_ext->interruptInfo.u.Interrupt.Affinity ));

			continue;
		}
		
		if( pHcaRes->Type != CmResourceTypeMemory )
			continue;

		/*
		 * Sanity check that our assumption on how resources
		 * are reported hold.
		 */
		if( type == HCA_BAR_TYPE_HCR &&
			(pHcaRes->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) )
		{
			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
				("First memory resource is prefetchable - expected HCR.\n"));
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		p_ext->bar[type].phys = pHcaRes->u.Memory.Start.QuadPart;
		p_ext->bar[type].size = pHcaRes->u.Memory.Length;
#ifdef MAP_ALL_HCA_MEMORY		
		/*leo: no need to map all the resources */
		p_ext->bar[type].virt = MmMapIoSpace( pHostRes->u.Memory.Start,
			pHostRes->u.Memory.Length, MmNonCached );
		if( !p_ext->bar[type].virt )
		{
			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
				("Failed to map memory resource type %d\n", type));
			status = STATUS_UNSUCCESSFUL;
			break;
		}
#else		
		p_ext->bar[type].virt = NULL;
#endif		

		type++;
	}

	if( type == HCA_BAR_TYPE_DDR)
	{
		p_ext->hca_hidden = 1;
	}
	else 
	if( type != HCA_BAR_TYPE_MAX )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR  ,HCA_DBG_SHIM  ,("Failed to map all memory resources.\n"));
		status = STATUS_UNSUCCESSFUL;
	}

	if( p_ext->interruptInfo.Type != CmResourceTypeInterrupt )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR  ,HCA_DBG_SHIM  ,("No interrupt resource.\n"));
		status = STATUS_UNSUCCESSFUL;
	}

	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


static void
__UnmapHcaMemoryResources(
	IN				DEVICE_OBJECT* const		p_dev_obj )
{
	hca_dev_ext_t		*p_ext;
	USHORT				i;

	HCA_ENTER( HCA_DBG_PNP );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	for( i = 0; i < HCA_BAR_TYPE_MAX; i++ )
	{
		if( p_ext->bar[i].virt )
		{
			MmUnmapIoSpace( p_ext->bar[i].virt, p_ext->bar[i].size );
			cl_memclr( &p_ext->bar[i], sizeof(hca_bar_t) );
		}
	}

	HCA_EXIT( HCA_DBG_PNP );
}


static NTSTATUS
hca_start(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	hca_dev_ext_t		*p_ext;
	IO_STACK_LOCATION	*pIoStack;
	POWER_STATE			powerState;
	DEVICE_DESCRIPTION	devDesc;
	int					err;

	HCA_ENTER( HCA_DBG_PNP );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	/* Handled on the way up. */
	status = cl_do_sync_pnp( p_dev_obj, p_irp, p_action );
	if( !NT_SUCCESS( status ) )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
			("Lower drivers failed IRP_MN_START_DEVICE (%#x).\n", status));
		return status;
	}

	pIoStack = IoGetCurrentIrpStackLocation( p_irp );

	/*
	 * Walk the resource lists and store the information.  The write-only
	 * flag is not set for the UAR region, so it is indistinguishable from the
	 * DDR region since both are prefetchable.  The code here assumes that the
	 * resources get handed in order - HCR, UAR, DDR.
	 *	- Configuration Space: not prefetchable, read/write
	 *	- UAR space: prefetchable, write only.
	 *	- DDR: prefetchable, read/write.
	 */
	status = __SetupHcaResources( p_dev_obj,
		pIoStack->Parameters.StartDevice.AllocatedResources,
		pIoStack->Parameters.StartDevice.AllocatedResourcesTranslated );
	if( !NT_SUCCESS( status ) )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
			("__ProcessResources returned %08X.\n", status));
		return status;
	}
	
	/* save PCI bus i/f, PCI configuration info and enable device */
	hca_enable_pci( p_dev_obj, &p_ext->hcaBusIfc, &p_ext->hcaConfig );

	/*
	 * Get the DMA adapter representing the HCA so we can
	 * allocate common buffers.
	 */
	RtlZeroMemory( &devDesc, sizeof(devDesc) );
	devDesc.Version = DEVICE_DESCRIPTION_VERSION2;
	devDesc.Master = TRUE;
	devDesc.ScatterGather = TRUE;
	devDesc.Dma32BitAddresses = TRUE;
	devDesc.Dma64BitAddresses = TRUE;
	devDesc.InterfaceType = PCIBus;

	// get the adapter object
	// 0x80000000 is a threshold, that's why - 1
	devDesc.MaximumLength = 0x80000000 - 1;
	p_ext->p_dma_adapter = IoGetDmaAdapter(
		p_ext->cl_ext.p_pdo, &devDesc, &p_ext->n_map_regs );
	if( !p_ext->p_dma_adapter )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
			("Failed to get DMA_ADAPTER for HCA.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	/* Initialize the HCA now. */
	status = mthca_init_one( p_ext );
	if( !NT_SUCCESS( status ) )
	{
		//TODO: no cleanup on error
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
			("mthca_init_one returned %08X\n", status));
		return status;
	}

	err = mthca_get_dev_info( p_ext->hca.mdev, &p_ext->hca.guid, &p_ext->hca.hw_ver );
	if (err) {

		//TODO: no cleanup on error
		HCA_PRINT( TRACE_LEVEL_ERROR,HCA_DBG_PNP, 
			("can't get guid - mthca_query_port()"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	/* queue HCA  */
	mlnx_hca_insert( &p_ext->hca );

	/*
	 * Change the state since the PnP callback can happen
	 * before the callback returns.
	 */
	p_ext->state = HCA_STARTED;
	
	/* We get started fully powered. */
	p_ext->DevicePowerState = PowerDeviceD0;
	powerState.DeviceState = PowerDeviceD0;
	powerState = PoSetPowerState ( p_ext->cl_ext.p_self_do, DevicePowerState, powerState );
	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PNP, 
		("PoSetPowerState: old state %d, new state to %d\n", 
		powerState.DeviceState, p_ext->DevicePowerState ));


	{
		struct mthca_dev *mdev = p_ext->hca.mdev;
		HCA_PRINT_EV(TRACE_LEVEL_INFORMATION ,HCA_DBG_LOW ,
			("Ven %x Dev %d Hw %x Fw %d.%d.%d Drv %s (%s)", 
			(unsigned)p_ext->hcaConfig.VendorID, (unsigned)p_ext->hcaConfig.DeviceID,
			p_ext->hca.hw_ver, 	(int) (mdev->fw_ver >> 32),
			(int) (mdev->fw_ver >> 16) & 0xffff, (int) (mdev->fw_ver & 0xffff),
			DRV_VERSION, DRV_RELDATE
			));
		HCA_PRINT_EV(TRACE_LEVEL_INFORMATION ,HCA_DBG_LOW ,
			("Flags %s%s%s%s%s%s\n", 
			(mdev->mthca_flags & MTHCA_FLAG_MEMFREE) ? "MemFree:" : "",
			(mdev->mthca_flags & MTHCA_FLAG_NO_LAM) ? "NoLam:" : "",
			(mdev->mthca_flags & MTHCA_FLAG_FMR) ? "Fmr:" : "",
			(mdev->mthca_flags & MTHCA_FLAG_SRQ) ? "Srq:" : "",
			(mdev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN) ? "HideDdr:" : "",
			(mdev->mthca_flags & MTHCA_FLAG_PCIE) ? "PciEx:" : ""
			));
	}

	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


/* release the resources, allocated in hca_start */
static void
__hca_release_resources(
	IN				DEVICE_OBJECT* const		p_dev_obj )
{
	hca_dev_ext_t		*p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	HCA_ENTER( HCA_DBG_PNP );

	switch( p_ext->state )
	{
	case HCA_STARTED:
		/* dequeue HCA  */
		mlnx_hca_remove( &p_ext->hca );
	}

	if (p_ext->al_sym_name.Buffer) {
		ExFreePool( p_ext->al_sym_name.Buffer );
		p_ext->al_sym_name.Buffer = NULL;
	}
	
	if( p_ext->pnp_target_entry )
	{
		ASSERT( p_ext->pnp_ifc_entry );
		IoUnregisterPlugPlayNotification( p_ext->pnp_target_entry );
		p_ext->pnp_target_entry = NULL;
	}

	if( p_ext->pnp_ifc_entry ) {
		IoUnregisterPlugPlayNotification( p_ext->pnp_ifc_entry );
		p_ext->pnp_ifc_entry = NULL;
	}

	if( p_ext->p_al_file_obj ) {
		ObDereferenceObject( p_ext->p_al_file_obj );
		p_ext->p_al_file_obj = NULL;
	}

	mthca_remove_one( p_ext );

	if( p_ext->p_dma_adapter ) {
		p_ext->p_dma_adapter->DmaOperations->PutDmaAdapter( p_ext->p_dma_adapter );
		p_ext->p_dma_adapter = NULL;
	}

	hca_disable_pci( &p_ext->hcaBusIfc );

	//cl_event_destroy( &p_ext->mutex );
	__UnmapHcaMemoryResources( p_dev_obj );

	p_ext->state = HCA_SHUTDOWN;

	HCA_EXIT( HCA_DBG_PNP );
}


static void
hca_release_resources(
	IN				DEVICE_OBJECT* const		p_dev_obj )
{
	hca_dev_ext_t		*p_ext;
	POWER_STATE		powerState;

	HCA_ENTER( HCA_DBG_PNP );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	/* release all the resources, allocated in hca_start */
	__hca_release_resources(p_dev_obj);

	/* Notify the power manager that the device is powered down. */
	p_ext->DevicePowerState = PowerDeviceD3;
	powerState.DeviceState = PowerDeviceD3;
	powerState = PoSetPowerState ( p_ext->cl_ext.p_self_do, DevicePowerState, powerState );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PNP, 
		("PoSetPowerState: old state %d, new state to %d\n", 
		powerState.DeviceState, p_ext->DevicePowerState ));

	/* Clear the PnP state in case we get restarted. */
	p_ext->pnpState = PNP_DEVICE_REMOVED;

	HCA_EXIT( HCA_DBG_PNP );
}


static NTSTATUS
hca_query_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	/* All kernel clients will get notified through the device hierarchy. */

	/* TODO: set a flag to fail creation of any new IB resources. */
	return cl_irp_skip( p_dev_obj, p_irp, p_action );
}


static NTSTATUS
hca_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	hca_dev_ext_t		*p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	if (p_ext->hca.mdev) { // quiet the HCA
		struct mthca_dev *mdev = p_ext->hca.mdev;
		(void) hca_reset(mdev->ext->cl_ext.p_self_do,(mdev->hca_type == 0/*TAVOR*/));
	}

	/*
	 * Must disable everything.  Complib framework will
	 * call ReleaseResources handler.
	 */
	return cl_irp_skip( p_dev_obj, p_irp, p_action );
}


static NTSTATUS
hca_cancel_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	/* Handled on the way up. */
	return cl_do_sync_pnp( p_dev_obj, p_irp, p_action );
}


static NTSTATUS
hca_query_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	hca_dev_ext_t*p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	if (atomic_read(&p_ext->usecnt)) {
		cl_dbg_out( "MTHCA: Can't get unloaded. %d applications are still in work\n", p_ext->usecnt);
		p_irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
		return cl_irp_complete( p_dev_obj, p_irp, p_action );
	}
	/* TODO: set a flag to fail creation of any new IB resources. */
	return cl_irp_skip( p_dev_obj, p_irp, p_action );
}


static NTSTATUS
hca_cancel_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	/* Handled on the way up. */
	return cl_do_sync_pnp( p_dev_obj, p_irp, p_action );
}


static NTSTATUS
hca_surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	/*
	 * TODO: Set state so that all further requests
	 * automatically succeed/fail as needed.
	 */
	return cl_irp_skip( p_dev_obj, p_irp, p_action );
}


static NTSTATUS
hca_query_capabilities(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	hca_dev_ext_t		*p_ext;
	IO_STACK_LOCATION	*pIoStack;
	DEVICE_CAPABILITIES	*pCaps;

	HCA_ENTER( HCA_DBG_PNP );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	/* Process on the way up. */
	status = cl_do_sync_pnp( p_dev_obj, p_irp, p_action );
	if( !NT_SUCCESS( status ) )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
			("cl_do_sync_pnp returned %08X.\n", status));
		return status;
	}

	pIoStack = IoGetCurrentIrpStackLocation( p_irp );
	pCaps = pIoStack->Parameters.DeviceCapabilities.Capabilities;

	/*
	 * Store the device power mapping into our extension since we're
	 * the power policy owner.  The mapping is used when handling
	 * IRP_MN_SET_POWER IRPs.
	 */
	cl_memcpy(
		p_ext->DevicePower, pCaps->DeviceState, sizeof(p_ext->DevicePower) );

	if( pCaps->DeviceD1 )
	{
		HCA_PRINT( TRACE_LEVEL_WARNING ,HCA_DBG_PNP,
			("WARNING: Device reports support for DeviceD1 power state.\n"));
		pCaps->DeviceD1 = FALSE;
	}

	if( pCaps->DeviceD2 )
	{
		HCA_PRINT( TRACE_LEVEL_WARNING,HCA_DBG_PNP,
			("WARNING: Device reports support for DeviceD2 power state.\n"));
		pCaps->DeviceD2 = FALSE;
	}

	if( pCaps->SystemWake != PowerSystemUnspecified )
	{
		HCA_PRINT( TRACE_LEVEL_WARNING ,HCA_DBG_PNP,
			("WARNING: Device reports support for system wake.\n"));
		pCaps->SystemWake = PowerSystemUnspecified;
	}

	if( pCaps->DeviceWake != PowerDeviceUnspecified )
	{
		HCA_PRINT( TRACE_LEVEL_WARNING, HCA_DBG_PNP,
			("WARNING: Device reports support for device wake.\n"));
		pCaps->DeviceWake = PowerDeviceUnspecified;
	}

	HCA_EXIT( HCA_DBG_PNP );
	return status;
}

static void
__ref_ifc(
	IN				DEVICE_OBJECT*				p_dev_obj )
{
	hca_dev_ext_t	*p_ext  = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	HCA_ENTER( HCA_DBG_PNP );

	cl_atomic_inc( &p_ext->n_hca_ifc_ref );
	ObReferenceObject( p_dev_obj );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PNP, 
		("MTHCA: CA_guid %I64x, hca_ifc_ref %d\n",
		p_ext->hca.guid, p_ext->n_hca_ifc_ref) );

	HCA_EXIT( HCA_DBG_PNP );
}

static void
__deref_ifc(
	IN				DEVICE_OBJECT*				p_dev_obj )
{
	hca_dev_ext_t	*p_ext  = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	HCA_ENTER( HCA_DBG_PNP );

	cl_atomic_dec( &p_ext->n_hca_ifc_ref );
	ObDereferenceObject( p_dev_obj );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PNP, 
		("MTHCA: CA_guid %I64x, hca_ifc_ref %d\n",
		p_ext->hca.guid, p_ext->n_hca_ifc_ref) );

	HCA_EXIT( HCA_DBG_PNP );
}

static NTSTATUS
__query_ci_ifc(
	IN					DEVICE_OBJECT* const		p_dev_obj,
	IN					IO_STACK_LOCATION* const	p_io_stack )
{
	RDMA_INTERFACE_VERBS	*p_ifc;
	hca_dev_ext_t			*p_ext;
	ci_interface_t			*p_hca_ifc;
	NTSTATUS				status;
	UINT8					version;

	HCA_ENTER( HCA_DBG_PNP );

	version = VerbsVersionMajor(p_io_stack->Parameters.QueryInterface.Version);
	if(  version > VERBS_MAJOR_VER )
	{
		status = STATUS_NOT_SUPPORTED;
		goto exit;
	}

	if( p_io_stack->Parameters.QueryInterface.Size < sizeof(RDMA_INTERFACE_VERBS) )
	{
		status = STATUS_BUFFER_TOO_SMALL;
		goto exit;
	}

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	p_hca_ifc = __alloc_hca_ifc( p_ext );
	if( !p_hca_ifc )
	{
		status = STATUS_NO_MEMORY;
		goto exit;
	}

	p_ifc = (RDMA_INTERFACE_VERBS *) p_io_stack->Parameters.QueryInterface.Interface;

	p_ifc->InterfaceHeader.Size = sizeof(RDMA_INTERFACE_VERBS);
	p_ifc->InterfaceHeader.Version = VerbsVersion(VERBS_MAJOR_VER, VERBS_MINOR_VER);
	p_ifc->InterfaceHeader.Context = p_dev_obj;
	p_ifc->InterfaceHeader.InterfaceReference = __ref_ifc;
	p_ifc->InterfaceHeader.InterfaceDereference = __deref_ifc;
	p_ifc->Verbs = *p_hca_ifc;

	/* take the reference before returning. */
	__ref_ifc( p_dev_obj );

	ExFreePool( p_hca_ifc );
	status = STATUS_SUCCESS;

exit:
	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


static NTSTATUS
hca_query_interface(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	*p_io_stack;

	HCA_ENTER( HCA_DBG_PNP );

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	
	/* Compare requested GUID with our supported interface GUIDs. */
	if( IsEqualGUID( p_io_stack->Parameters.QueryInterface.InterfaceType,
		&GUID_RDMA_INTERFACE_VERBS ) )
	{
		status = __query_ci_ifc( p_dev_obj, p_io_stack );
		if( !NT_SUCCESS( status ) )
		{
			*p_action = IrpComplete;
		}
		else
		{
			*p_action = IrpSkip;
		}
	}
	else
	{
		status = p_irp->IoStatus.Status;
		*p_action = IrpSkip;
	}

	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


static NTSTATUS
hca_query_pnp_state(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	hca_dev_ext_t		*p_ext;

	HCA_ENTER( HCA_DBG_PNP );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;

	p_irp->IoStatus.Information |= p_ext->pnpState;

	*p_action = IrpSkip;

	HCA_EXIT( HCA_DBG_PNP );
	return STATUS_SUCCESS;;
}

static NTSTATUS
hca_query_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status = STATUS_SUCCESS;
	IO_STACK_LOCATION	*pIoStack;

	HCA_ENTER(HCA_DBG_PO);

	UNUSED_PARAM( p_dev_obj );

	pIoStack = IoGetCurrentIrpStackLocation( p_irp );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PO, 
		("QUERY_POWER for FDO %p: type %s, state %d, action %d, IRQL %d, IRP %p\n",
		p_dev_obj, 
		(pIoStack->Parameters.Power.Type) ? "DevicePowerState" : "SystemPowerState",
		pIoStack->Parameters.Power.State.DeviceState, 
		pIoStack->Parameters.Power.ShutdownType, KeGetCurrentIrql(), p_irp ));

	switch( pIoStack->Parameters.Power.Type )
	{
	case SystemPowerState:
		/* Fail any requests to hibernate or sleep the system. */
		switch( pIoStack->Parameters.Power.State.SystemState )
		{
			case PowerSystemSleeping1:	// STANDBY support
			case PowerSystemSleeping2:	// STANDBY support
			case PowerSystemSleeping3:	// STANDBY support
			case PowerSystemHibernate:
			{
				hca_dev_ext_t*p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
				if (atomic_read(&p_ext->usecnt)) 
					status = STATUS_UNSUCCESSFUL;
				break;
			}

			case PowerSystemWorking:
			case PowerSystemShutdown:
				break;

			default:
				status = STATUS_NOT_SUPPORTED;
		}
		break;

	case DevicePowerState:
		/* Fail any query for low power states. */
		switch( pIoStack->Parameters.Power.State.DeviceState )
		{
		case PowerDeviceD0:
		case PowerDeviceD3:
			/* We only support fully powered or off power states. */
			break;

		default:
			status = STATUS_NOT_SUPPORTED;
		}
		break;
	}

	if( status == STATUS_SUCCESS )
		*p_action = IrpSkip;
	else
		*p_action = IrpComplete;

	HCA_EXIT( HCA_DBG_PO );
	return status;
}


static void
__RequestPowerCompletion(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				UCHAR						minorFunction,
	IN				POWER_STATE					powerState,
	IN				void						*context,
	IN				IO_STATUS_BLOCK				*pIoStatus )
{
	IRP					*p_irp;
	cl_pnp_po_ext_t		*p_ext;

	HCA_ENTER( HCA_DBG_PO );

	UNUSED_PARAM( minorFunction );
	UNUSED_PARAM( powerState );

	p_irp = (IRP*)context;
	p_ext = (cl_pnp_po_ext_t*)p_dev_obj->DeviceExtension;

	/* Propagate the device IRP status to the system IRP status. */
	p_irp->IoStatus.Status = pIoStatus->Status;

	/* Continue Power IRP processing. */
	PoStartNextPowerIrp( p_irp );
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	IoReleaseRemoveLock( &p_ext->remove_lock, p_irp );
	HCA_EXIT( HCA_DBG_PO );
}


/*NOTE: Completion routines must NEVER be pageable. */
static NTSTATUS
__SystemPowerCompletion(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp,
	IN				void						*context )
{
	NTSTATUS			status;
	POWER_STATE			state;
	hca_dev_ext_t		*p_ext;
	IO_STACK_LOCATION	*pIoStack;

	HCA_ENTER( HCA_DBG_PO );

	UNUSED_PARAM( context );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	pIoStack = IoGetCurrentIrpStackLocation( p_irp );

	if( !NT_SUCCESS( p_irp->IoStatus.Status ) )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PO, 
			("IRP_MN_SET_POWER for system failed by lower driver with %08x.\n",
			p_irp->IoStatus.Status));
		status = STATUS_SUCCESS;
		PoStartNextPowerIrp( p_irp );
		goto release;
	}

	state.DeviceState = 
		p_ext->DevicePower[pIoStack->Parameters.Power.State.SystemState];

	/*
	 * Send a device power IRP to our devnode.  Using our device object will
	 * only work on win2k and other NT based systems.
	 */
	status = PoRequestPowerIrp( p_dev_obj, IRP_MN_SET_POWER, state,
		__RequestPowerCompletion, p_irp, NULL );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PO, 
		("PoRequestPowerIrp: SET_POWER 'PowerDeviceD%d', status %#x\n", 
		state.DeviceState - 1, status ));

	if( status != STATUS_PENDING ) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PO,
			("PoRequestPowerIrp returned %08x.\n", status));
		p_irp->IoStatus.Status = status; 	/* Propagate the failure. */
		PoStartNextPowerIrp( p_irp );
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
		goto release;
	}

	status = STATUS_MORE_PROCESSING_REQUIRED;
	goto exit;

release:	
	IoReleaseRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );
exit:
	HCA_EXIT( HCA_DBG_PO );
	return status;
}


/* Work item callback to handle DevicePowerD0 IRPs at passive level. */
static void
__DevicePowerUpCompletionWorkItem(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				void*						context )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	*pIoStack;
	hca_dev_ext_t		*p_ext;
	IRP					*p_irp;
	POWER_STATE powerState;

	HCA_ENTER( HCA_DBG_PO );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	p_irp = (IRP*)context;
	pIoStack = IoGetCurrentIrpStackLocation( p_irp );

	IoFreeWorkItem( p_ext->pPoWorkItem );
	p_ext->pPoWorkItem = NULL;

	/* restart the HCA */
	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PO, 
		("***** Restart the HCA, IRQL %d\n", KeGetCurrentIrql()));

	status = mthca_init_one( p_ext );
	if( !NT_SUCCESS( status ) ) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PO, 
			("!!! mthca_init_one failed (%#x) \n", status));
		goto err_mthca_init;
	}

	p_ext->DevicePowerState = pIoStack->Parameters.Power.State.DeviceState;
	powerState = PoSetPowerState( p_dev_obj, DevicePowerState,
		pIoStack->Parameters.Power.State );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PO, 
		("PoSetPowerState: old state %d, new state to %d\n", 
		powerState.DeviceState, p_ext->DevicePowerState ));

	goto exit;

err_mthca_init:
	/* Flag device as having failed. */
	p_ext->pnpState |= PNP_DEVICE_FAILED;
	IoInvalidateDeviceState( p_ext->cl_ext.p_pdo );
exit:
	PoStartNextPowerIrp( p_irp );
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	IoReleaseRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );
	HCA_EXIT( HCA_DBG_PO );
}

/*NOTE: Completion routines must NEVER be pageable. */
static NTSTATUS
__DevicePowerUpCompletion(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp,
	IN				void						*context )
{
	NTSTATUS			status = STATUS_SUCCESS;
	hca_dev_ext_t		*p_ext;
	IO_STACK_LOCATION	*pIoStack;

	HCA_ENTER( HCA_DBG_PO );

	UNUSED_PARAM( context );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	pIoStack = IoGetCurrentIrpStackLocation( p_irp );

	if( !NT_SUCCESS( p_irp->IoStatus.Status ) ) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PO, 
			("IRP_MN_SET_POWER for device failed by lower driver with %08x.\n",
			p_irp->IoStatus.Status));
		status =  STATUS_SUCCESS;
		PoStartNextPowerIrp( p_irp );
		goto release;
	}

	/* Process in a work item - mthca_start blocks. */
	ASSERT( !p_ext->pPoWorkItem );
	p_ext->pPoWorkItem = IoAllocateWorkItem( p_dev_obj );
	if( !p_ext->pPoWorkItem ) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PO, 
			("Failed to allocate work item.\n" ));
		status = STATUS_SUCCESS;
		p_ext->pnpState |= PNP_DEVICE_FAILED;
		IoInvalidateDeviceState( p_ext->cl_ext.p_pdo );
		PoStartNextPowerIrp( p_irp );
		goto release;
	}

	/* Process in work item callback. */
	IoMarkIrpPending( p_irp );
	IoQueueWorkItem( p_ext->pPoWorkItem, 
		__DevicePowerUpCompletionWorkItem, DelayedWorkQueue, p_irp );
	status = STATUS_MORE_PROCESSING_REQUIRED;
	goto exit;

release:	
	IoReleaseRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );
exit:	
	HCA_EXIT( HCA_DBG_PO );
	return status;
}

static NTSTATUS __DevicePowerDownWorkItemCompletion(
	IN				DEVICE_OBJECT	*p_dev_obj,
	IN				IRP				*p_irp,
	IN				void				*context )
{
	hca_dev_ext_t  *p_ext  = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	UNUSED_PARAM( context );

	HCA_ENTER( HCA_DBG_PO );

	PoStartNextPowerIrp( p_irp );
	IoReleaseRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );

	HCA_EXIT( HCA_DBG_PO );
	return STATUS_SUCCESS;
}

/* Work item callback to handle DevicePowerD3 IRPs at passive level. */
static void
__DevicePowerDownWorkItem(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				void*						context )
{
	IO_STACK_LOCATION	*pIoStack;
	hca_dev_ext_t		*p_ext;
	IRP					*p_irp;
	POWER_STATE powerState;

	HCA_ENTER( HCA_DBG_PO );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	p_irp = (IRP*)context;
	pIoStack = IoGetCurrentIrpStackLocation( p_irp );

	IoFreeWorkItem( p_ext->pPoWorkItem );
	p_ext->pPoWorkItem = NULL;

	p_ext->DevicePowerState = pIoStack->Parameters.Power.State.DeviceState;
	powerState = PoSetPowerState( p_dev_obj, DevicePowerState,
		pIoStack->Parameters.Power.State );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PO, 
		("PoSetPowerState: old state %d, new state to %d, IRQL %d\n", 
		powerState.DeviceState, p_ext->DevicePowerState, KeGetCurrentIrql() ));

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PO, 
		("***** Remove the HCA \n"));

	mthca_remove_one( p_ext );

	IoCopyCurrentIrpStackLocationToNext( p_irp );
#pragma warning( push, 3 )
	IoSetCompletionRoutine( p_irp, __DevicePowerDownWorkItemCompletion,
		NULL, TRUE, TRUE, TRUE );
#pragma warning( pop )
	PoCallDriver( p_ext->cl_ext.p_next_do, p_irp );

	HCA_EXIT( HCA_DBG_PO );
}


static NTSTATUS
hca_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	*pIoStack;
	hca_dev_ext_t		*p_ext;

	HCA_ENTER( HCA_DBG_PO );

	p_ext = (hca_dev_ext_t*)p_dev_obj->DeviceExtension;
	pIoStack = IoGetCurrentIrpStackLocation( p_irp );

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PO, 
		("SET_POWER for FDO %p (ext %p): type %s, state %d, action %d, IRQL %d \n",
		p_dev_obj, p_ext,
		(pIoStack->Parameters.Power.Type) ? "DevicePowerState" : "SystemPowerState",
		pIoStack->Parameters.Power.State.DeviceState, 
		pIoStack->Parameters.Power.ShutdownType, KeGetCurrentIrql() ));

	switch( pIoStack->Parameters.Power.Type )
	{
	case SystemPowerState:
		p_ext->SystemPowerState = pIoStack->Parameters.Power.State.SystemState;
		
		/*
		 * Process on the way up the stack.  We cannot block since the 
		 * power dispatch function can be called at elevated IRQL if the
		 * device is in a paging/hibernation/crash dump path.
		 */
		IoMarkIrpPending( p_irp );
		IoCopyCurrentIrpStackLocationToNext( p_irp );
#pragma warning( push, 3 )
		IoSetCompletionRoutine( p_irp, __SystemPowerCompletion, NULL, 
			TRUE, TRUE, TRUE );
#pragma warning( pop )
		PoCallDriver( p_ext->cl_ext.p_next_do, p_irp );

		*p_action = IrpDoNothing;
		status = STATUS_PENDING;
		break;

	case DevicePowerState:
		IoMarkIrpPending( p_irp );
		if( pIoStack->Parameters.Power.State.DeviceState == PowerDeviceD0 && 
			p_ext->SystemPowerState == PowerSystemWorking)
		{ /* power up */
			/* If we're already powered up, just pass down. */
			if( p_ext->DevicePowerState == PowerDeviceD0 )
			{
				status = STATUS_SUCCESS;
				*p_action = IrpIgnore;
				break;
			}

			/* Process in I/O completion callback. */
			IoCopyCurrentIrpStackLocationToNext( p_irp );
#pragma warning( push, 3 )
			IoSetCompletionRoutine( p_irp, __DevicePowerUpCompletion, NULL, 
				TRUE, TRUE, TRUE );
#pragma warning( pop )
			PoCallDriver( p_ext->cl_ext.p_next_do, p_irp );
		}
		else
		{ /* power down */

			/* Process in a work item - deregister_ca and HcaDeinit block. */
			ASSERT( !p_ext->pPoWorkItem );
			p_ext->pPoWorkItem = IoAllocateWorkItem( p_dev_obj );
			if( !p_ext->pPoWorkItem )
			{
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}

			/* Process in work item callback. */
			IoQueueWorkItem(
				p_ext->pPoWorkItem, __DevicePowerDownWorkItem, DelayedWorkQueue, p_irp );
		}
		*p_action = IrpDoNothing;
		status = STATUS_PENDING;
		break;

	default:
		/* Pass down and let the PDO driver handle it. */
		*p_action = IrpIgnore;
		status = STATUS_SUCCESS;
		break;
	}

	if( !NT_SUCCESS( status ) )
		*p_action = IrpComplete;

	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


