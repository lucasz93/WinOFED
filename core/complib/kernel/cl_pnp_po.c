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


#include "complib/cl_pnp_po.h"
#include "complib/cl_debug.h"
#include "complib/cl_atomic.h"


static NTSTATUS
__start(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__query_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__cancel_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__query_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__cancel_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__query_pnp_state(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__device_usage_notification(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__query_device_relations(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
__query_id(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action );

static NTSTATUS
__query_device_text(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );





void
cl_init_pnp_po_ext(
	IN	OUT			DEVICE_OBJECT* const		p_dev_obj,
	IN				DEVICE_OBJECT* const		p_next_do,
	IN				DEVICE_OBJECT* const		p_pdo,
	IN		const	uint32_t					pnp_po_dbg_lvl,
	IN		const	cl_vfptr_pnp_po_t* const	vfptr_pnp_po,
	IN		const	cl_vfptr_query_txt_t* const	vfptr_query_txt OPTIONAL )
{
	cl_pnp_po_ext_t		*p_ext;

	CL_ENTER( CL_DBG_PNP, pnp_po_dbg_lvl );

	p_ext = p_dev_obj->DeviceExtension;

	p_ext->dbg_lvl = pnp_po_dbg_lvl;

	/* Store the pointer to our own device. */
	p_ext->p_self_do = p_dev_obj;

	IoInitializeRemoveLock( &p_ext->remove_lock, 'bilc', 15, 1000 );
	IoInitializeRemoveLock( &p_ext->stop_lock, 'dtci', 0, 1000 );
	
	/* Initialize the PnP states. */
	p_ext->pnp_state = NotStarted;
	p_ext->last_pnp_state = NotStarted;

	/* Store the pointer to the next device in the stack. */
	p_ext->p_next_do = p_next_do;

	/* Store the pointer to the underlying PDO. */
	p_ext->p_pdo = p_pdo;

	/* Store the PnP virtual function pointer table. */
	p_ext->vfptr_pnp_po = vfptr_pnp_po;

	/* Store the Power Management virtual function pointer table. */
	p_ext->vfptr_query_txt = vfptr_query_txt;

	/*
	 * Mark power routines as pageable.  This changes when the device is
	 * notified of being in the paging path.
	 */
	p_dev_obj->Flags |= DO_POWER_PAGABLE;

	/* Clear the initializing flag before returning. */
	p_dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
}

#ifdef PRINT_QUERY_INTERFACE
static PCHAR __action2str(cl_irp_action_t act)
{
	switch (act)
	{
		case IrpPassDown: return "IrpPassDown";
		case IrpSkip: return "IrpSkip";
		case IrpIgnore: return "IrpIgnore";
		case IrpComplete: return "IrpComplete";
		case IrpDoNothing: return "IrpDoNothing";
		default: return "Invalid";
	}
}
#endif
	
NTSTATUS
cl_pnp(
	IN				PDEVICE_OBJECT				p_dev_obj,
	IN				PIRP						p_irp )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	*p_io_stack;
	cl_pnp_po_ext_t		*p_ext;
	cl_irp_action_t		action = 0;
#ifdef PRINT_QUERY_INTERFACE
	static int irp_num = 0;
#endif

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
		("PDO %p, ext %p\n",  p_dev_obj, p_ext) );

	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );
	
	status = IoAcquireRemoveLock( &p_ext->remove_lock, p_irp );
	if( !NT_SUCCESS( status ) )
	{
		CL_TRACE_EXIT( CL_DBG_ERROR, p_ext->dbg_lvl, 
			("IoAcquireRemoveLock returned %08x.\n", status) );
		p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("Minor function %x for %s\n", p_io_stack->MinorFunction, p_ext->vfptr_pnp_po->identity) );
		p_irp->IoStatus.Status = status;
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
		return status;
	}

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	ASSERT( p_io_stack->MajorFunction == IRP_MJ_PNP );

	switch( p_io_stack->MinorFunction )
	{
	case IRP_MN_START_DEVICE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_START_DEVICE for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = __start( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_STOP_DEVICE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IRP_MN_QUERY_STOP_DEVICE for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = __query_stop( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_STOP_DEVICE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_STOP_DEVICE for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = __stop( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_CANCEL_STOP_DEVICE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_CANCEL_STOP_DEVICE for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = __cancel_stop( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_REMOVE_DEVICE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_QUERY_REMOVE_DEVICE for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = __query_remove( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_REMOVE_DEVICE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_REMOVE_DEVICE for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = __remove( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_CANCEL_REMOVE_DEVICE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_CANCEL_REMOVE_DEVICE for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = __cancel_remove( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_SURPRISE_REMOVAL:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IRP_MN_SURPRISE_REMOVAL for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = __surprise_remove( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_CAPABILITIES:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_QUERY_CAPABILITIES for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_query_capabilities( 
			p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_PNP_DEVICE_STATE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_QUERY_PNP_DEVICE_STATE for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = __query_pnp_state( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl,
			("IRP_MN_FILTER_RESOURCE_REQUIREMENTS for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_filter_res_req( 
			p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_DEVICE_USAGE_NOTIFICATION:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_DEVICE_USAGE_NOTIFICATION for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = __device_usage_notification( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_DEVICE_RELATIONS:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_QUERY_DEVICE_RELATIONS for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = __query_device_relations( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_RESOURCES:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IRP_MN_QUERY_RESOURCES for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_query_resources( 
			p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl,
			("IRP_MN_QUERY_RESOURCE_REQUIREMENTS for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_query_res_req( 
			p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_ID:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_QUERY_ID for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = __query_id( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_DEVICE_TEXT:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl,
			("IRP_MN_QUERY_DEVICE_TEXT for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = __query_device_text( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_BUS_INFORMATION:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_QUERY_BUS_INFORMATION for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_query_bus_info( 
			p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_QUERY_INTERFACE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IRP_MN_QUERY_INTERFACE for %s\n",
			p_ext->vfptr_pnp_po->identity) );
#ifdef PRINT_QUERY_INTERFACE
		DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
			"IRP_MN_QUERY_INTERFACE for %s: irp_num %d, p_irp %p, iostatus %#x, p_dev_obj %p,  pfn_query_interface %p, guid %I64x\n", 
			p_ext->vfptr_pnp_po->identity, ++irp_num, p_irp, 
			p_irp->IoStatus.Status,
			p_dev_obj, p_ext->vfptr_pnp_po->pfn_query_interface,
			*(UINT64*)p_io_stack->Parameters.QueryInterface.InterfaceType);
#endif
		status = p_ext->vfptr_pnp_po->pfn_query_interface( 
			p_dev_obj, p_irp, &action );
#ifdef PRINT_QUERY_INTERFACE
		DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
			"IRP_MN_QUERY_INTERFACE for %s: irp_num %d, p_irp %p, iostatus %#x, status %#x, action %s\n", 
			p_ext->vfptr_pnp_po->identity, irp_num, p_irp, 
			p_irp->IoStatus.Status,
			status, __action2str(action));
#endif
		break;

	case IRP_MN_READ_CONFIG:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl,
			("IRP_MN_READ_CONFIG for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_read_config(
			p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_WRITE_CONFIG:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl,
			("IRP_MN_WRITE_CONFIG for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_write_config( 
			p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_EJECT:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl,
			("IRP_MN_EJECT for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_eject( 
			p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_SET_LOCK:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl,
			("IRP_MN_SET_LOCK for %s\n", p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_set_lock( 
			p_dev_obj, p_irp, &action );
		break;

	default:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, 
			("Unknown IRP minor function 0x%x for %s\n", 
			p_io_stack->MinorFunction, p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_unknown(
			p_dev_obj, p_irp, &action );
		break;
	}
	
	switch( action )
	{
	case IrpPassDown:
		p_irp->IoStatus.Status = status;
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IrpPassDown: passing down to PDO %p, ext %p, status %#x\n",
			p_ext->p_next_do, p_ext, p_irp->IoStatus.Status) );
		IoCopyCurrentIrpStackLocationToNext( p_irp );
		status = IoCallDriver( p_ext->p_next_do, p_irp );
		break;

	case IrpSkip:
		p_irp->IoStatus.Status = status;

	case IrpIgnore:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IrpSkip/IrpIgnore: skipping down to PDO %p, ext %p, status %#x\n",
			p_ext->p_next_do, p_ext, p_irp->IoStatus.Status) );
		IoSkipCurrentIrpStackLocation( p_irp );
		status = IoCallDriver( p_ext->p_next_do, p_irp );
		break;

	case IrpComplete:
		p_irp->IoStatus.Status = status;
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IrpComplete: complete IRP with status %#x\n",
			p_irp->IoStatus.Status) );
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
		break;

	case IrpDoNothing:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IrpDoNothing: do nothing\n") );
		break;
	}

	CL_TRACE_EXIT( CL_DBG_PNP, p_ext->dbg_lvl, ("returned with status %#x\n", status ) );

	if( action != IrpDoNothing )
		IoReleaseRemoveLock( &p_ext->remove_lock, p_irp );

	return status;
}


static NTSTATUS
__start(
	IN				DEVICE_OBJECT* const	p_dev_obj,
	IN				IRP* const				p_irp, 
		OUT			cl_irp_action_t* const	p_action )
{
	cl_pnp_po_ext_t	*p_ext;
	NTSTATUS			status;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	status = p_ext->vfptr_pnp_po->pfn_start( p_dev_obj, p_irp, p_action );
	if( NT_SUCCESS( status ) ) 
	{
		cl_set_pnp_state( p_ext, Started );
	} 
	else
	{
		CL_TRACE( CL_DBG_ERROR, p_ext->dbg_lvl, 
			("p_ext->vfptr_pnp_po->pfn_start returned %08x. \n", status) );
	}

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__query_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	cl_pnp_po_ext_t	*p_ext;
	NTSTATUS			status;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	/* 
	 * We must fail the query if there are any paging, dump, or hibernation 
	 * files on the device.
	 */
	if( p_ext->n_crash_files || 
		p_ext->n_hibernate_files || 
		p_ext->n_paging_files )
	{
		*p_action = IrpComplete;
		/* Fail the request. */
		CL_TRACE_EXIT( CL_DBG_PNP, p_ext->dbg_lvl, 
			("Failing IRP_MN_QUERY_STOP_DEVICE - device %s has:\n"
			"\t\t%d paging files\n\t\t%d crash files\n"
			"\t\t%d hibernate files\n", p_ext->vfptr_pnp_po->identity,
			p_ext->n_paging_files, p_ext->n_crash_files, 
			p_ext->n_hibernate_files) );
		return STATUS_UNSUCCESSFUL;
	}

	/* 
	 * Mark the device as stop pending so that all new non-_PnP and non-_Power
	 * IRPs get queued or failed.
	 */
	cl_set_pnp_state( p_ext, StopPending );

	if( p_ext->last_pnp_state == Started )
	{
		/* Acquire the lock so we can release and wait. */
		status = IoAcquireRemoveLock( &p_ext->stop_lock, p_irp );
		if( !NT_SUCCESS( status ) )
		{
			CL_TRACE( CL_DBG_ERROR, p_ext->dbg_lvl, 
				("IoAcquireRemoveLock returned %08x. Continue anyway ...\n", status) );
			goto exit;
		}
		/* Wait for all IO operations to complete. */
		IoReleaseRemoveLockAndWait( &p_ext->stop_lock, p_irp );
	}

exit:
	status = p_ext->vfptr_pnp_po->pfn_query_stop( p_dev_obj, p_irp, p_action );
	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	cl_pnp_po_ext_t	*p_ext;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	ASSERT( p_ext->pnp_state == StopPending );

	cl_set_pnp_state( p_ext, Stopped );

	status = p_ext->vfptr_pnp_po->pfn_stop( p_dev_obj, p_irp, p_action );

	/* Release resources. */
	if( p_ext->vfptr_pnp_po->pfn_release_resources )
		p_ext->vfptr_pnp_po->pfn_release_resources( p_dev_obj );

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__cancel_stop(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	cl_pnp_po_ext_t*	p_ext;
	NTSTATUS			status;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	/* Call the device specific handler. */
	status = p_ext->vfptr_pnp_po->pfn_cancel_stop( p_dev_obj, p_irp, p_action );
	ASSERT( NT_SUCCESS(status) );

	/* 
	 * If we were never stopped (a higher level driver failed the 
	 * IRP_MN_QUERY_STOP but passed down the cancel), just return.
	 */
	if( p_ext->pnp_state != StopPending )
	{
		CL_TRACE_EXIT( CL_DBG_PNP, p_ext->dbg_lvl,
			("IRP_MN_CANCEL_STOP_DEVICE received in invalid state.\n") );
		return status;
	}

	/* Return to the previous PnP state. */
	cl_rollback_pnp_state( p_ext );

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__query_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	cl_pnp_po_ext_t	*p_ext;
	NTSTATUS			status;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	/*
	 * We must fail the query if there are any paging, dump, or hibernation
	 * files on the device.
	 */
	if( p_ext->n_crash_files || 
		p_ext->n_hibernate_files || 
		p_ext->n_paging_files )
	{
		*p_action = IrpComplete;
		/* Fail the request. */
		CL_TRACE_EXIT( CL_DBG_PNP, p_ext->dbg_lvl, 
			("Failing IRP_MN_QUERY_REMOVE_DEVICE - device has:\n"
			"\t\t%d paging files\n\t\t%d crash files\n"
			"\t\t%d hibernate files\n", p_ext->n_paging_files,
			p_ext->n_crash_files, p_ext->n_hibernate_files) );
		return STATUS_UNSUCCESSFUL;
	}

	/* We fail the query if we have any interface outstanding. */
	if( p_ext->n_ifc_ref )
	{
		*p_action = IrpComplete;
		CL_TRACE_EXIT( CL_DBG_PNP, p_ext->dbg_lvl,
			("Failing IRP_MN_QUERY_REMOVE_DEVICE - interface ref count: %d\n",
			p_ext->n_ifc_ref) );
		return STATUS_UNSUCCESSFUL;
	}

	/*
	 * Mark the device as remove pending so that all new non-PnP and 
	 * non-Power IRPs get queued or failed.
	 */
	cl_set_pnp_state( p_ext, RemovePending );

	/* Call type specific handler. */
	status = 
		p_ext->vfptr_pnp_po->pfn_query_remove( p_dev_obj, p_irp, p_action );

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	cl_pnp_po_ext_t	*p_ext;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	ASSERT( p_ext->pnp_state == NotStarted ||
		p_ext->pnp_state == Started ||
		p_ext->pnp_state == RemovePending ||
		p_ext->pnp_state == SurpriseRemoved ||
		// it can be on this state if IRP_MN_START_DEVICE failed
		// pnpdtest /rebalance FailRestart creates this situation
		p_ext->pnp_state == Stopped);

	/* Set the device state. */
	cl_set_pnp_state( p_ext, Deleted );

	status = p_ext->vfptr_pnp_po->pfn_remove( p_dev_obj, p_irp, p_action );

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


NTSTATUS
cl_do_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	cl_pnp_po_ext_t	*p_ext;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	/* Wait for all I/O operations to complete. */
	IoReleaseRemoveLockAndWait( &p_ext->remove_lock, p_irp );

	/* Release resources if it was not done yet. */
	if( p_ext->last_pnp_state != SurpriseRemoved &&
		p_ext->last_pnp_state != Stopped && 
		p_ext->vfptr_pnp_po->pfn_release_resources )
	{
		p_ext->vfptr_pnp_po->pfn_release_resources( p_dev_obj );
	}

	/* Set the IRP status. */
	p_irp->IoStatus.Status = STATUS_SUCCESS;

	/* Pass the IRP down. */
	IoSkipCurrentIrpStackLocation( p_irp );
	status = IoCallDriver( p_ext->p_next_do, p_irp );
	*p_action = IrpDoNothing;

	/* Detach and destroy the device. */
	IoDetachDevice( p_ext->p_next_do );
	IoDeleteDevice( p_dev_obj );

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__cancel_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	cl_pnp_po_ext_t	*p_ext;
	NTSTATUS			status;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	status = 
		p_ext->vfptr_pnp_po->pfn_cancel_remove( p_dev_obj, p_irp, p_action );
	ASSERT( NT_SUCCESS(status) );

	if( p_ext->pnp_state != RemovePending )
	{
		CL_TRACE_EXIT( CL_DBG_PNP, p_ext->dbg_lvl, 
			("IRP_MN_CANCEL_REMOVE_DEVICE received in invalid state.\n") );
		return status;
	}

	cl_rollback_pnp_state( p_ext );

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	cl_pnp_po_ext_t	*p_ext;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	cl_set_pnp_state( p_ext, SurpriseRemoved );

	/* Call handler before releasing resources. */
	status = 
		p_ext->vfptr_pnp_po->pfn_surprise_remove( p_dev_obj, p_irp, p_action );

	/* Release resources. */
	if( p_ext->last_pnp_state != Stopped &&
		p_ext->vfptr_pnp_po->pfn_release_resources )
	{
		p_ext->vfptr_pnp_po->pfn_release_resources( p_dev_obj );
	}

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__query_pnp_state(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	cl_pnp_po_ext_t	*p_ext;
	NTSTATUS			status;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	/* 
	 * Flag the device as not removable if there are any special files on it.
	 */
	if( p_ext->n_paging_files || 
		p_ext->n_crash_files || 
		p_ext->n_hibernate_files )
	{
		p_irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
	}

	status = 
		p_ext->vfptr_pnp_po->pfn_query_pnp_state( p_dev_obj, p_irp, p_action );
	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static inline void
__lock_po_code(
	IN	OUT			cl_pnp_po_ext_t* const		p_ext )
{
	if( !p_ext->h_cl_locked_section )
	{
		/*
		 * No handle exists.  This is the first lock.  Once locked, the
		 * handle is valid as long as the driver is loaded.  Lock any 
		 * function in the PAGE_PNP section.
		 */
#pragma warning( push, 3 )
		p_ext->h_cl_locked_section = MmLockPagableCodeSection( cl_power );
		/* TODO: Pick first non-CL function */
		p_ext->h_user_locked_section = MmLockPagableCodeSection( 
			p_ext->vfptr_pnp_po->pfn_set_power );
#pragma warning( pop )
	}
	else
	{
		/* Handle already exists.  Locking by handle is faster. */
		MmLockPagableSectionByHandle( p_ext->h_cl_locked_section );
		if( p_ext->h_user_locked_section )
			MmLockPagableSectionByHandle( p_ext->h_user_locked_section );
	}
}


static inline void
__unlock_po_code(
	IN	OUT			cl_pnp_po_ext_t* const		p_ext )
{
	ASSERT( p_ext->h_cl_locked_section );
	MmUnlockPagableImageSection( p_ext->h_cl_locked_section );
	if( p_ext->h_user_locked_section )
		MmUnlockPagableImageSection( p_ext->h_user_locked_section );
}


static NTSTATUS
__device_usage_notification(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	cl_pnp_po_ext_t		*p_ext;
	IO_STACK_LOCATION	*p_io_stack;
	atomic32_t			*p_val;
	NTSTATUS			status;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	switch( p_io_stack->Parameters.UsageNotification.Type )
	{
	case DeviceUsageTypePaging:
		p_val = &p_ext->n_paging_files;
		break;

	case DeviceUsageTypeDumpFile:
		p_val = &p_ext->n_crash_files;
		break;

	case DeviceUsageTypeHibernation:
		p_val = &p_ext->n_hibernate_files;
		break;

	default:
		CL_TRACE_EXIT( CL_DBG_ERROR, p_ext->dbg_lvl, 
			("Invalid notification type.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	if( p_io_stack->Parameters.UsageNotification.InPath )
	{
		cl_atomic_inc( p_val );
		__lock_po_code( p_ext );
	}
	else
	{
		__unlock_po_code( p_ext );
		cl_atomic_dec( p_val );
	}

	/* 
	 * Set the flag in the device extension to indicate that power management
	 * can happen at elevated IRQL.
	 */
	if( p_ext->n_paging_files || 
		p_ext->n_crash_files || 
		p_ext->n_hibernate_files )
	{
		p_dev_obj->Flags &= ~DO_POWER_PAGABLE;
	}
	else
	{
		p_dev_obj->Flags |= DO_POWER_PAGABLE;
	}

	/* Call type specific (FDO, PDO) function for propagating the IRP. */
	status = p_ext->vfptr_pnp_po->pfn_dev_usage_notification( 
		p_dev_obj, p_irp, p_action );

	if( NT_SUCCESS( status ) )
	{
		/* Notify the PnP manager that the device state may have changed. */
		IoInvalidateDeviceState( p_ext->p_pdo );
	}
	else
	{
		/* Propagation failed.  Undo. */
		if( p_io_stack->Parameters.UsageNotification.InPath )
		{
			/* Someone does not support the type of special file requested. */
			__unlock_po_code( p_ext );
			cl_atomic_dec( p_val );
		}
		else
		{
			/* 
			 * Someone failed the notification for the removal of a special
			 * file.  This is unlikely to happen, but handle it anyway.
			 */
			cl_atomic_inc( p_val );
			__lock_po_code( p_ext );
		}

		/* 
		 * Set the flag in the device extension to indicate that power 
		 * management can happen at elevated IRQL.
		 */
		if( p_ext->n_paging_files || 
			p_ext->n_crash_files || 
			p_ext->n_hibernate_files )
		{
			p_dev_obj->Flags &= ~DO_POWER_PAGABLE;
		}
		else
		{
			p_dev_obj->Flags |= DO_POWER_PAGABLE;
		}
	}

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__query_device_relations(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	*p_io_stack;
	cl_pnp_po_ext_t		*p_ext;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	switch( p_io_stack->Parameters.QueryDeviceRelations.Type )
	{
	case BusRelations:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("BusRelations for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_query_bus_relations( 
			p_dev_obj, p_irp, p_action );
		break;

	case EjectionRelations:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("EjectionRelations for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_query_ejection_relations(
			p_dev_obj, p_irp, p_action );
		break;

	case RemovalRelations:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("RemovalRelations for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_query_removal_relations(
			p_dev_obj, p_irp, p_action );
		break;

	case TargetDeviceRelation:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("TargetDeviceRelation for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_query_target_relations(
			p_dev_obj, p_irp, p_action );
		break;

	default:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("Unknown Relation for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_unknown(
			p_dev_obj, p_irp, p_action );
		break;
	}

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__query_id(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	*p_io_stack;
	cl_pnp_po_ext_t		*p_ext;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	/* Only PDOs handle query ID and query text IRPs */
	if( p_ext->p_next_do )
	{
		status = cl_irp_ignore( p_dev_obj, p_irp, p_action );
		CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
		return status;
	}

	*p_action = IrpComplete;

	switch( p_io_stack->Parameters.QueryId.IdType )
	{
	case BusQueryDeviceID:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("BusQueryDeviceID for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = 
			p_ext->vfptr_query_txt->pfn_query_device_id( p_dev_obj, p_irp );
		break;

	case BusQueryHardwareIDs:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("BusQueryHardwareIDs for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = 
			p_ext->vfptr_query_txt->pfn_query_hardware_id( p_dev_obj, p_irp );
		break;

	case BusQueryCompatibleIDs:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("BusQueryCompatibleIDs for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_query_txt->pfn_query_compatible_id( 
			p_dev_obj, p_irp );
		break;

	case BusQueryInstanceID:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("BusQueryInstanceID for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = 
			p_ext->vfptr_query_txt->pfn_query_unique_id( p_dev_obj, p_irp );
		break;

	default:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("Unsupported ID type for %s\n",
			p_ext->vfptr_pnp_po->identity) );
		status = p_irp->IoStatus.Status;
		break;
	}

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


static NTSTATUS
__query_device_text(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status;
	cl_pnp_po_ext_t		*p_ext;
	IO_STACK_LOCATION	*p_io_stack;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	/* Only PDOs handle query ID and query text IRPs */
	if( p_ext->p_next_do )
	{
		status = cl_irp_ignore( p_dev_obj, p_irp, p_action );
		CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
		return status;
	}

	*p_action = IrpComplete;

	switch( p_io_stack->Parameters.QueryDeviceText.DeviceTextType )
	{
	case DeviceTextDescription:
		status = 
			p_ext->vfptr_query_txt->pfn_query_description( p_dev_obj, p_irp );
		break;

	case DeviceTextLocationInformation:
		status = 
			p_ext->vfptr_query_txt->pfn_query_location( p_dev_obj, p_irp );
		break;

	default:
		status = p_irp->IoStatus.Status;
		break;
	}

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}

#ifdef NTDDI_WIN8
static IO_COMPLETION_ROUTINE __sync_completion;
#endif
static NTSTATUS
__sync_completion(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp,
	IN				void						*context )
{
	UNUSED_PARAM( p_dev_obj );

	ASSERT( p_irp );
	ASSERT( context );

	/* 
	 * We only wait if IoCallDriver returned STATUS_PENDING.  Only set
	 * the event if the IRP returned pending, so that we don't needlessly
	 * signal it.
	 */
	if( p_irp->PendingReturned )
		KeSetEvent( (KEVENT*)context, IO_NO_INCREMENT, FALSE );

	/* We need to process the IRP further. */
	return STATUS_MORE_PROCESSING_REQUIRED;
}


NTSTATUS
cl_do_sync_pnp(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	KEVENT				event;
	NTSTATUS			status;
	cl_pnp_po_ext_t	*p_ext;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	KeInitializeEvent( &event, NotificationEvent, FALSE );

	/* Setup the IRP. */
	IoCopyCurrentIrpStackLocationToNext( p_irp );
#pragma warning( push, 3 )
	IoSetCompletionRoutine( p_irp, __sync_completion, &event, 
		TRUE, TRUE, TRUE );
#pragma warning( pop )

	status = IoCallDriver( p_ext->p_next_do, p_irp );
	if( status == STATUS_PENDING )
	{
		/* Wait for the completion. */
		KeWaitForSingleObject( &event, Executive, KernelMode, 
			FALSE, NULL );

		status = p_irp->IoStatus.Status;
	}
	*p_action = IrpComplete;
	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


NTSTATUS
cl_alloc_relations(
	IN				IRP* const					p_irp,
	IN		const	size_t						n_devs )
{
	DEVICE_RELATIONS	*p_rel, *p_old_rel;
	size_t				alloc_size;
#ifdef _DEBUG_
	/* Debug variable to prevent warnings when using CL_TRACE macros. */
	uint32_t			dbg_error = CL_DBG_ERROR;
#endif

	ASSERT( n_devs );

	alloc_size = sizeof(DEVICE_RELATIONS) + 
		(sizeof(PDEVICE_OBJECT) * (n_devs - 1));
	
	/* If there are already relations, copy them. */
	p_old_rel = (DEVICE_RELATIONS*)p_irp->IoStatus.Information;
	if( p_old_rel )
		alloc_size += (sizeof(PDEVICE_OBJECT) * p_old_rel->Count);

	/* Allocate the new relations structure. */
	p_rel = ExAllocatePoolWithTag( NonPagedPool, alloc_size, 'ralc' );
	p_irp->IoStatus.Information = (ULONG_PTR)p_rel;
	if( !p_rel )
	{
		/* 
		 * Allocation failed.  Release the existing relations and fail the IRP.
		 */
		if( p_old_rel )
			ExFreePool( p_old_rel );
		CL_TRACE( CL_DBG_ERROR, dbg_error, 
			("Failed to allocate DEVICE_RELATIONS (%d bytes).\n", alloc_size) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	/* 
	 * Since the structure doesn't contain the callers target devices,
	 * the count is only set to what existing relations specify.
	 */
	if( p_old_rel )
	{
		/* Copy the existing relations. */
		RtlCopyMemory( p_rel->Objects, p_old_rel->Objects, 
			sizeof(PDEVICE_OBJECT) * p_old_rel->Count );
		p_rel->Count = p_old_rel->Count;
		/* Done with the copy, free the old relations structure. */
		ExFreePool( p_old_rel );
	}
	else
	{
		p_rel->Count = 0;
	}

	return STATUS_SUCCESS;
}


NTSTATUS
cl_power(
	IN				PDEVICE_OBJECT				p_dev_obj,
	IN				PIRP						p_irp )
{
	NTSTATUS			status;
	IO_STACK_LOCATION	*p_io_stack;
	cl_pnp_po_ext_t		*p_ext;
	cl_irp_action_t		action;

	p_ext = p_dev_obj->DeviceExtension;

	CL_ENTER( CL_DBG_PNP, p_ext->dbg_lvl );

	status = IoAcquireRemoveLock( &p_ext->remove_lock, p_irp );
	if( !NT_SUCCESS( status ) )
	{
		CL_TRACE_EXIT( CL_DBG_ERROR, p_ext->dbg_lvl, 
			("IoAcquireRemoveLock returned %08x for %s.\n",
			status, p_ext->vfptr_pnp_po->identity) );
		PoStartNextPowerIrp( p_irp );
		p_irp->IoStatus.Status = status;
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
		return status;
	}

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	ASSERT( p_io_stack->MajorFunction == IRP_MJ_POWER );

	switch( p_io_stack->MinorFunction )
	{
	case IRP_MN_QUERY_POWER:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IRP_MN_QUERY_POWER for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = 
			p_ext->vfptr_pnp_po->pfn_query_power( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_SET_POWER:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IRP_MN_SET_POWER for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = 
			p_ext->vfptr_pnp_po->pfn_set_power( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_WAIT_WAKE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IRP_MN_WAIT_WAKE for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = 
			p_ext->vfptr_pnp_po->pfn_wait_wake( p_dev_obj, p_irp, &action );
		break;

	case IRP_MN_POWER_SEQUENCE:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("IRP_MN_POWER_SEQUENCE for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = 
			p_ext->vfptr_pnp_po->pfn_power_sequence( p_dev_obj, p_irp, &action );
		break;
		
	default:
		CL_TRACE( CL_DBG_PNP, p_ext->dbg_lvl, ("Unknown IRP minor function for %s\n", 
			p_ext->vfptr_pnp_po->identity) );
		status = p_ext->vfptr_pnp_po->pfn_unknown(
			p_dev_obj, p_irp, &action );
	}

	switch( action )
	{
	case IrpPassDown:
		/* 
		 * A completion routine has already been set.
		 * PoStartNextPowerIrp should be called in the completion routine.
		 */
		status = PoCallDriver( p_ext->p_next_do, p_irp );
		break;

	case IrpSkip:
		p_irp->IoStatus.Status = status;

	case IrpIgnore:
		PoStartNextPowerIrp( p_irp );
		IoSkipCurrentIrpStackLocation( p_irp );
		/* TODO: Documentation says to return STATUS_PENDING.  Seems odd. */
		status = PoCallDriver( p_ext->p_next_do, p_irp );
		break;

	case IrpComplete:
		p_irp->IoStatus.Status = status;
		PoStartNextPowerIrp( p_irp );
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
		break;

	case IrpDoNothing:
		/* 
		 * Returned when sending a device IRP_MN_SET_POWER IRP so that
		 * processing can continue in the completion routine without releasing
		 * the remove lock.
		 */
		break;
	}

	if( action != IrpDoNothing )
		IoReleaseRemoveLock( &p_ext->remove_lock, p_irp );

	CL_EXIT( CL_DBG_PNP, p_ext->dbg_lvl );
	return status;
}


NTSTATUS
cl_irp_skip(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	UNUSED_PARAM( p_dev_obj );
	UNUSED_PARAM( p_irp );
	*p_action = IrpSkip;
	return STATUS_SUCCESS;
}


NTSTATUS
cl_irp_ignore(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	UNUSED_PARAM( p_dev_obj );
	UNUSED_PARAM( p_irp );
	*p_action = IrpIgnore;
	return STATUS_SUCCESS;
}



NTSTATUS
cl_irp_complete(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	UNUSED_PARAM( p_dev_obj );
	*p_action = IrpComplete;
	return p_irp->IoStatus.Status;
}


NTSTATUS
cl_irp_succeed(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	UNUSED_PARAM( p_dev_obj );
	UNUSED_PARAM( p_irp );
	*p_action = IrpComplete;
	return STATUS_SUCCESS;
}


NTSTATUS
cl_irp_unsupported(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	UNUSED_PARAM( p_dev_obj );
	UNUSED_PARAM( p_irp );
	*p_action = IrpComplete;
	return STATUS_NOT_SUPPORTED;
}
