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
 * Provides the driver entry points for the ALTS kernel driver.
 */


#include <complib/cl_types.h>
#include <complib/cl_pnp_po.h>
#include <complib/cl_thread.h>
#include "alts_common.h"
#include "alts_debug.h"
#include <complib/cl_init.h>


#if !defined(FILE_DEVICE_INFINIBAND) // Not defined in WXP DDK
#define FILE_DEVICE_INFINIBAND          0x0000003B
#endif

uint32_t	alts_dbg_lvl = ALTS_DBG_ERROR | ALTS_DBG_STATUS;


NTSTATUS
DriverEntry(
	IN				DRIVER_OBJECT				*p_driver_obj,
	IN				UNICODE_STRING				*p_registry_path );

static void
alts_drv_unload(
	IN				DRIVER_OBJECT				*p_driver_obj );

//static NTSTATUS
//alts_ioctl(
//	IN				DEVICE_OBJECT				*p_dev_obj,
//	IN				IRP							*p_irp );

static NTSTATUS
alts_sysctl(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp );

static NTSTATUS
alts_add_device(
	IN				DRIVER_OBJECT				*p_driver_obj,
	IN				DEVICE_OBJECT				*p_pdo );

static NTSTATUS
alts_start_tests(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static void
alts_release_resources(
	IN				DEVICE_OBJECT* const		p_dev_obj );



static const cl_vfptr_pnp_po_t	alts_vfptr_pnp = {
	"ALTS",
	alts_start_tests,		// StartDevice
	cl_irp_skip,
	cl_irp_skip,
	cl_irp_skip,
	cl_irp_skip,			// QueryRemove
	alts_release_resources,
	cl_do_remove,			// Remove
	cl_irp_skip,			// CancelRemove
	cl_irp_skip,			// SurpriseRemove
	cl_irp_skip,		
	cl_irp_skip,
	cl_irp_skip,
	cl_irp_skip,
	cl_irp_skip,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,
	cl_irp_ignore,			// QueryPower
	cl_irp_ignore,			// SetPower
	cl_irp_ignore,			// PowerSequence
	cl_irp_ignore			// WaitWake
};


NTSTATUS
DriverEntry(
	IN				PDRIVER_OBJECT			p_driver_obj,
	IN				PUNICODE_STRING			p_registry_path )
{
	NTSTATUS			status;
#ifdef _DEBUG_
	static boolean_t	exit = FALSE;
#endif

	ALTS_ENTER( ALTS_DBG_DEV );

	UNUSED_PARAM( p_registry_path );

#ifdef _DEBUG_
	DbgBreakPoint();
	if( exit )
	{
		ALTS_TRACE_EXIT( ALTS_DBG_DEV, ("Load aborted.\n") );
		return STATUS_DRIVER_INTERNAL_ERROR;
	}
#endif

	status = CL_INIT;
	if( !NT_SUCCESS(status) )
	{
		ALTS_TRACE_EXIT( ALTS_DBG_ERROR,
			("cl_init returned %08X.\n", status) );
		return status;
	}

	p_driver_obj->MajorFunction[IRP_MJ_PNP] = cl_pnp;
	p_driver_obj->MajorFunction[IRP_MJ_POWER] = cl_power;
//	p_driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = alts_ioctl;
	p_driver_obj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = alts_sysctl;
	p_driver_obj->DriverUnload = alts_drv_unload;
	p_driver_obj->DriverExtension->AddDevice = alts_add_device;

	ALTS_EXIT( ALTS_DBG_DEV );
	return STATUS_SUCCESS;
}


static void
alts_drv_unload(
	IN				PDRIVER_OBJECT			p_driver_obj )
{
	ALTS_ENTER( ALTS_DBG_DEV );

	UNUSED_PARAM( p_driver_obj );

	CL_DEINIT;

	ALTS_EXIT( ALTS_DBG_DEV );
}


//static NTSTATUS
//alts_ioctl(
//	IN				DEVICE_OBJECT				*p_dev_obj,
//	IN				IRP							*p_irp )
//{
//
//}


static NTSTATUS
alts_sysctl(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	NTSTATUS		status;
	cl_pnp_po_ext_t	*p_ext;

	ALTS_ENTER( ALTS_DBG_DEV );

	p_ext = p_dev_obj->DeviceExtension;

	IoSkipCurrentIrpStackLocation( p_irp );
	status = IoCallDriver( p_ext->p_next_do, p_irp );

	ALTS_EXIT( ALTS_DBG_DEV );
	return status;
}


static NTSTATUS
alts_add_device(
	IN				DRIVER_OBJECT				*p_driver_obj,
	IN				DEVICE_OBJECT				*p_pdo )
{
	NTSTATUS			status;
	DEVICE_OBJECT		*p_dev_obj, *p_next_do;

	ALTS_ENTER( ALTS_DBG_PNP );

	/*
	 * Create the device so that we have a device extension to store stuff in.
	 */
	status = IoCreateDevice( p_driver_obj, sizeof(cl_pnp_po_ext_t),
		NULL, FILE_DEVICE_INFINIBAND, FILE_DEVICE_SECURE_OPEN,
		FALSE, &p_dev_obj );
	if( !NT_SUCCESS( status ) )
	{
		ALTS_TRACE_EXIT( ALTS_DBG_ERROR,
			("IoCreateDevice returned 0x%08X.\n", status) );
		return status;
	}

	/* Attach to the device stack. */
	p_next_do = IoAttachDeviceToDeviceStack( p_dev_obj, p_pdo );
	if( !p_next_do )
	{
		IoDeleteDevice( p_dev_obj );
		ALTS_TRACE_EXIT( ALTS_DBG_ERROR,
			("IoAttachDeviceToDeviceStack failed.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	/* Inititalize the complib extension. */
	cl_init_pnp_po_ext( p_dev_obj, p_next_do, p_pdo, alts_dbg_lvl,
		&alts_vfptr_pnp, NULL );

	ALTS_EXIT( ALTS_DBG_PNP );
	return status;
}


static NTSTATUS
alts_start_tests(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS		status;
	ib_api_status_t	ib_status;

	status = cl_do_sync_pnp( p_dev_obj, p_irp, p_action );
	if( !NT_SUCCESS( status ) )
		return status;

	/* Wait 10 seconds for LIDs to get assigned. */
	cl_thread_suspend( 60000 );

	/* We're started.  Launch the tests. */
	ib_status = al_test_openclose();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nOpenClose returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_querycaattr();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nQueryCAAttribute returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_modifycaattr();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nModifyCAAttribute returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_alloc_dealloc_pd();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nAllocDeallocPD returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_create_destroy_av();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nCreateDestroyAV returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_query_modify_av();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nQueryAndModifyAV returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_create_destroy_cq();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nCreateAndDestroyCQ returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_query_modify_cq();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nQueryAndModifyCQ returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_register_mem();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nRegisterMemRegion returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_register_phys_mem();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nRegisterPhyMemRegion returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_create_mem_window();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nCreateMemWindow returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_register_shared_mem();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nRegisterSharedMemRegion returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_multi_send_recv();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nMultiSend returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_register_pnp();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nRegisterPnP returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_mad();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nMadTests returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_query();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nMadQuery returned %s\n\n", ib_get_err_str( ib_status )) );

	ib_status = al_test_cm();
	ALTS_TRACE( ALTS_DBG_STATUS,
		("\nCmTests returned %s\n\n", ib_get_err_str( ib_status )) );

	return status;
}


static void
alts_release_resources(
	IN				DEVICE_OBJECT* const		p_dev_obj )
{
	UNUSED_PARAM( p_dev_obj );
}
