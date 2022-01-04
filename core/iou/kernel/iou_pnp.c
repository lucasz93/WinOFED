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



/*
 * Implemenation of all PnP functionality for FDO (power policy owners).
 */


#include "iou_driver.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "iou_pnp.tmh"
#endif
#include "iou_pnp.h"
#include "iou_ioc_mgr.h"
#include <complib/cl_memory.h>
#include <complib/cl_bus_ifc.h>
#include <initguid.h>
#include <iba/iou_ifc.h>


static NTSTATUS
fdo_start(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action );

static void
fdo_release_resources(
	IN					DEVICE_OBJECT* const	p_dev_obj );

static NTSTATUS
fdo_query_remove(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action );

static NTSTATUS
fdo_query_capabilities(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action );

static NTSTATUS
fdo_query_iou_relations(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action );

static NTSTATUS
fdo_query_interface(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__fdo_query_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__fdo_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );



/* Global virtual function pointer tables shared between all instances of FDO. */
static const cl_vfptr_pnp_po_t		vfptr_fdo_pnp = {
	"IB IOU",
	fdo_start,
	cl_irp_skip,
	cl_irp_skip,
	cl_do_sync_pnp,
	fdo_query_remove,
	fdo_release_resources,
	cl_do_remove,
	cl_do_sync_pnp,
	cl_irp_skip,
	fdo_query_capabilities,
	cl_irp_skip,
	cl_irp_skip,
	cl_do_sync_pnp,
	fdo_query_iou_relations,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,			/* QueryInterface */
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	__fdo_query_power,		/* QueryPower */
	__fdo_set_power,		/* SetPower */
	cl_irp_ignore,			/* PowerSequence */
	cl_irp_ignore			/* WaitWake */
};
/*
 * NOTE: The QueryInterface entry point is not used because we only enable/disable
 * our interface so that user-mode AL can find a device to perform IOCTLs to.
 */

NTSTATUS
iou_add_device(
	IN				DRIVER_OBJECT				*p_driver_obj,
	IN				DEVICE_OBJECT				*p_pdo )
{
	NTSTATUS		status;
	DEVICE_OBJECT	*p_dev_obj, *p_next_do;
	iou_fdo_ext_t	*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	/* Create the FDO device object to attach to the stack. */
	status = IoCreateDevice( p_driver_obj, sizeof(iou_fdo_ext_t),
		NULL, FILE_DEVICE_BUS_EXTENDER,
		FILE_DEVICE_SECURE_OPEN, FALSE, &p_dev_obj );
	if( !NT_SUCCESS(status) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to create bus root FDO device.\n") );
		return status;
	}

	p_ext = p_dev_obj->DeviceExtension;

	ioc_mgr_construct( &p_ext->ioc_mgr );

	p_next_do = IoAttachDeviceToDeviceStack( p_dev_obj, p_pdo );
	if( !p_next_do )
	{
		IoDeleteDevice( p_dev_obj );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("IoAttachToDeviceStack failed.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	cl_init_pnp_po_ext( p_dev_obj, p_next_do, p_pdo, g_iou_dbg_flags,
		&vfptr_fdo_pnp, NULL );

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
__get_iou_ifc(
	IN					iou_fdo_ext_t* const	p_ext )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	io_stack;
	ib_al_ifc_data_t	data;

	IOU_ENTER( IOU_DBG_PNP );

	data.type = &GUID_IOU_INTERFACE_DATA;
	data.version = IOU_INTERFACE_DATA_VERSION;
	data.size = sizeof(iou_ifc_data_t);
	data.p_data = &p_ext->ioc_mgr.info;

	io_stack.MinorFunction = IRP_MN_QUERY_INTERFACE;
	io_stack.Parameters.QueryInterface.Version = AL_INTERFACE_VERSION;
	io_stack.Parameters.QueryInterface.Size = sizeof(ib_al_ifc_t);
	io_stack.Parameters.QueryInterface.Interface =
		(INTERFACE*)&p_ext->ioc_mgr.ifc;
	io_stack.Parameters.QueryInterface.InterfaceSpecificData =
		&data;
	io_stack.Parameters.QueryInterface.InterfaceType = &GUID_IB_AL_INTERFACE;

	status = cl_fwd_query_ifc( p_ext->cl_ext.p_next_do, &io_stack );

	/*
	 * Dereference the interface now so that the bus driver doesn't fail a
	 * query remove IRP.  We will always get unloaded before the bus driver
	 * since we're a child device.
	 */
	if( NT_SUCCESS( status ) )
	{
		p_ext->ioc_mgr.ifc.wdm.InterfaceDereference(
			p_ext->ioc_mgr.ifc.wdm.Context );
	}

	IOU_EXIT( IOU_DBG_PNP );
	return status;
}


static NTSTATUS
fdo_start(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	NTSTATUS		status;
	iou_fdo_ext_t	*p_ext;
	ib_api_status_t	ib_status;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;

	/* Handled on the way up. */
	status = cl_do_sync_pnp( p_dev_obj, p_irp, p_action );
	if( !NT_SUCCESS( status ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Lower drivers failed IRP_MN_START_DEVICE.\n") );
		return status;
	}

	status = __get_iou_ifc( p_ext );
	if( !NT_SUCCESS( status ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to get IOU interface.\n") );
		return status;
	}

	/* Initialize the IOU manager. */
	ib_status = ioc_mgr_init( &p_ext->ioc_mgr );
	if( ib_status != IB_SUCCESS )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("ioc_mgr_init returned %s.\n",
			p_ext->ioc_mgr.ifc.get_err_str(ib_status)) );
		return STATUS_UNSUCCESSFUL;
	}

	IOU_EXIT( IOU_DBG_PNP );
	return status;
}


static NTSTATUS
fdo_query_remove(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	iou_fdo_ext_t	*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;

	*p_action = IrpSkip;
	/* The FDO driver must set the status even when passing down. */
	p_irp->IoStatus.Status = STATUS_SUCCESS;
	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}

static void _destroy_ca_ioc_maps(
	IN					ioc_mgr_t	*p_ioc_mgr)
{
	ca_ioc_map_t	*p_map = NULL;
	cl_list_item_t	*list_item;

	cl_mutex_acquire( &iou_globals.list_mutex );

	list_item = cl_qlist_head( &iou_globals.ca_ioc_map_list );

	while( list_item != cl_qlist_end( &iou_globals.ca_ioc_map_list ) ) {
		p_map = ( ca_ioc_map_t	* ) list_item;
		list_item = cl_qlist_next( list_item ); // Get the next element before freeing it

		if ( p_map->p_ioc_mgr == p_ioc_mgr ) {
			cl_qlist_remove_item( &iou_globals.ca_ioc_map_list, ( cl_list_item_t * )p_map );
			cl_free( p_map );
		}

	}

	cl_mutex_release( &iou_globals.list_mutex );
}

/*
 * This function gets called after releasing the remove lock and waiting
 * for all other threads to release the lock.  No more modifications will
 * occur to the PDO pointer vectors.
 */
static void
fdo_release_resources(
	IN					DEVICE_OBJECT* const	p_dev_obj )
{
	iou_fdo_ext_t	*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;

	_destroy_ca_ioc_maps(&p_ext->ioc_mgr);

	//TODO: Fail outstanding I/O operations.
	cl_obj_destroy( &p_ext->ioc_mgr.obj );

	IOU_EXIT( IOU_DBG_PNP );
}


static NTSTATUS
fdo_query_capabilities(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	NTSTATUS			status;
	iou_fdo_ext_t		*p_ext;
	IO_STACK_LOCATION	*p_io_stack;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;

	/* Process on the way up. */
	status = cl_do_sync_pnp( p_dev_obj, p_irp, p_action );

	if( !NT_SUCCESS( status ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("cl_do_sync_pnp returned %08x.\n", status) );
		return status;
	}

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	/*
	 * Store the device power maping into our extension since we're
	 * the power policy owner.  The mapping is used when handling
	 * IRP_MN_SET_POWER IRPs.
	 */
	cl_memcpy( p_ext->po_state, 
		p_io_stack->Parameters.DeviceCapabilities.Capabilities->DeviceState,
		sizeof( p_ext->po_state ) );

	IOU_EXIT( IOU_DBG_PNP );
	return status;
}


static NTSTATUS
fdo_query_iou_relations(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	iou_fdo_ext_t	*p_ext;
	NTSTATUS		status;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;

	status = ioc_mgr_get_iou_relations( &p_ext->ioc_mgr, p_irp );
	switch( status )
	{
	case STATUS_NO_SUCH_DEVICE:
		*p_action = IrpSkip;
		status = STATUS_SUCCESS;
		break;

	case STATUS_SUCCESS:
		*p_action = IrpPassDown;
		break;

	default:
		*p_action = IrpComplete;
		break;
	}

	IOU_EXIT( IOU_DBG_PNP );
	return status;
}


static NTSTATUS
__fdo_query_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status = STATUS_SUCCESS;
	IO_STACK_LOCATION	*p_io_stack;

	IOU_ENTER( IOU_DBG_POWER );

	UNUSED_PARAM( p_dev_obj );

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	switch( p_io_stack->Parameters.Power.Type )
	{
	case SystemPowerState:
		/* Fail any requests to hibernate or sleep the system. */
		switch( p_io_stack->Parameters.Power.State.SystemState )
		{
			case PowerSystemWorking:
			case PowerSystemShutdown:
				/* We only support fully working and shutdown system states. */
				break;

			default:
				status = STATUS_NOT_SUPPORTED;
		}
		break;

	case DevicePowerState:
		/* Fail any query for low power states. */
		switch( p_io_stack->Parameters.Power.State.DeviceState )
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

	if( status == STATUS_NOT_SUPPORTED )
		*p_action = IrpComplete;
	else
		*p_action = IrpSkip;

	IOU_EXIT( IOU_DBG_POWER );
	return status;
}


static void
__request_power_completion(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				UCHAR						minor_function,
	IN				POWER_STATE					power_state,
	IN				void						*context,
	IN				IO_STATUS_BLOCK				*p_io_status )
{
	IRP					*p_irp;
	cl_pnp_po_ext_t		*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	UNUSED_PARAM( minor_function );
	UNUSED_PARAM( power_state );

	p_irp = (IRP*)context;
	p_ext = p_dev_obj->DeviceExtension;

	/* Propagate the device IRP status to the system IRP status. */
	p_irp->IoStatus.Status = p_io_status->Status;

	/* Continue Power IRP processing. */
	PoStartNextPowerIrp( p_irp );
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	IoReleaseRemoveLock( &p_ext->remove_lock, p_irp );
	IOU_EXIT( IOU_DBG_PNP );
}


/*NOTE: Completion routines must NEVER be pageable. */
static NTSTATUS
__set_power_completion(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp,
	IN				void						*context )
{
	NTSTATUS			status;
	POWER_STATE			state;
	iou_fdo_ext_t		*p_ext;
	IO_STACK_LOCATION	*p_io_stack;

	IOU_ENTER( IOU_DBG_PNP );

	UNUSED_PARAM( context );

	p_ext = p_dev_obj->DeviceExtension;
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	if( !NT_SUCCESS( p_irp->IoStatus.Status ) )
	{
		PoStartNextPowerIrp( p_irp );
		IoReleaseRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("IRP_MN_SET_POWER for system failed by lower driver with %08x.\n",
			p_irp->IoStatus.Status) );
		return STATUS_SUCCESS;
	}

	state.DeviceState = 
		p_ext->po_state[p_io_stack->Parameters.Power.State.SystemState];

	/*
	 * Send a device power IRP to our devnode.  Using our device object will
	 * only work on win2k and other NT based systems.
	 */
	status = PoRequestPowerIrp( p_dev_obj, IRP_MN_SET_POWER, state,
		__request_power_completion, p_irp, NULL );

	if( !NT_SUCCESS( p_irp->IoStatus.Status ) )
	{
		PoStartNextPowerIrp( p_irp );
		/* Propagate the failure. */
		p_irp->IoStatus.Status = status;
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
		IoReleaseRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );
		IOU_PRINT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("PoRequestPowerIrp returned %08x.\n", status) );
	}

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_MORE_PROCESSING_REQUIRED;
}


static NTSTATUS
__fdo_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	*p_io_stack;
	iou_fdo_ext_t		*p_ext;

	IOU_ENTER( IOU_DBG_POWER );

	p_ext = p_dev_obj->DeviceExtension;
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	switch( p_io_stack->Parameters.Power.Type )
	{
	case SystemPowerState:
		/*
		 * Process on the way up the stack.  We cannot block since the 
		 * power dispatch function can be called at elevated IRQL if the
		 * device is in a paging/hibernation/crash dump path.
		 */
		IoMarkIrpPending( p_irp );
		IoCopyCurrentIrpStackLocationToNext( p_irp );
#pragma warning( push, 3 )
		IoSetCompletionRoutine( p_irp, __set_power_completion, NULL, 
			TRUE, TRUE, TRUE );
#pragma warning( pop )
		PoCallDriver( p_ext->cl_ext.p_next_do, p_irp );

		*p_action = IrpDoNothing;
		status = STATUS_PENDING;
		break;

	case DevicePowerState:
	default:
		/* Pass down and let the PDO driver handle it. */
		*p_action = IrpIgnore;
		status = STATUS_SUCCESS;
		break;
	}

	IOU_EXIT( IOU_DBG_POWER );
	return status;
}


void
update_relations(
	IN				cl_qlist_t*	const			p_pdo_list,
	IN	OUT			DEVICE_RELATIONS* const		p_rel )
{
	cl_list_item_t	*p_list_item;
	iou_pdo_ext_t	*p_pdo_ext;

	IOU_ENTER( IOU_DBG_PNP );

	p_list_item = cl_qlist_head( p_pdo_list );
	while( p_list_item != cl_qlist_end( p_pdo_list ) )
	{
		p_pdo_ext = PARENT_STRUCT( p_list_item, iou_pdo_ext_t, list_item );

		/* Move the list item to the next object. */
		p_list_item = cl_qlist_next( p_list_item );

		if( !p_pdo_ext->b_present )
		{
			/*
			 * We don't report a PDO that is no longer present.  This is how
			 * the PDO will get cleaned up.
			 */
			p_pdo_ext->b_reported_missing = TRUE;
			continue;
		}
		p_rel->Objects[p_rel->Count] = p_pdo_ext->cl_ext.p_pdo;
		ObReferenceObject( p_rel->Objects[p_rel->Count++] );
	}

	IOU_EXIT( IOU_DBG_PNP );
}


