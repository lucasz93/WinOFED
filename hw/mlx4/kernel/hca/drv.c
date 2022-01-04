/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#include "precomp.h"
#include <initguid.h>
#include <wdmguid.h>
#include <rdma\verbs.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "drv.tmh"
#endif 

GLOBALS g;

/*
 * UVP name does not include file extension.  For debug builds, UAL
 * will append "d.dll".  For release builds, UAL will append ".dll"
 */
char			mlnx_uvp_lib_name[MAX_LIB_NAME] = {"mlx4u"};

static void
__unmap_fdo_memory(
	IN				PHCA_FDO_DEVICE_DATA const p_fdo )
{
	struct pci_dev *pdev;
	int				i;

	HCA_ENTER( HCA_DBG_PNP );

    if (p_fdo->p_bus_ib_ifc != NULL) {
		pdev = p_fdo->p_bus_ib_ifc->pdev;

		for( i = 0; i < HCA_BAR_TYPE_MAX; i++ ) {
			if (pdev->bar[i].virt) {
				MmUnmapIoSpace( pdev->bar[i].virt, pdev->bar[i].size );
				cl_memclr( &pdev->bar[i], sizeof(hca_bar_t) );
			}
		}
	}

	HCA_EXIT( HCA_DBG_PNP );
}

static int __get_dev_info(PHCA_FDO_DEVICE_DATA p_fdo, __be64 *node_guid, u32 *hw_id)
{
	struct ib_device_attr device_attr;
	struct ib_device *p_ibdev = p_fdo->p_bus_ib_ifc->p_ibdev;
	int err;

	HCA_ENTER( HCA_DBG_PNP );

	err = (p_ibdev->query_device)( p_ibdev, &device_attr );
	if (err)
		return err;

	*node_guid = p_ibdev->node_guid;
	*hw_id = device_attr.hw_ver;
	HCA_EXIT( HCA_DBG_PNP );
	return 0;
}


/* release the resources, allocated in hca_start */
static void
__hca_release_resources(
	IN		PHCA_FDO_DEVICE_DATA p_fdo )
{
	HCA_ENTER( HCA_DBG_PNP );

	switch( p_fdo->state )
	{
	case HCA_STARTED:
		/* dequeue HCA  */
		mlnx_hca_remove( &p_fdo->hca );
	}
	
	__unmap_fdo_memory( p_fdo );

	p_fdo->state = HCA_ADDED;

	HCA_EXIT( HCA_DBG_PNP );
}

void
mlx4_setup_hca_ifc(
    IN              PHCA_FDO_DEVICE_DATA const  p_fdo,
    OUT             ci_interface_t*             pIfc )
{
	HCA_ENTER( HCA_DBG_PNP );

	setup_ci_interface( p_fdo->hca.guid, pIfc );

    pIfc->driver_id =       GUID_MLX4_DRIVER;
	pIfc->p_hca_obj = &p_fdo->hca;
	pIfc->vend_id = (uint32_t)p_fdo->p_bus_ib_ifc->pdev->ven_id;
	pIfc->dev_id = (uint16_t)p_fdo->p_bus_ib_ifc->pdev->dev_id;
	pIfc->dev_revision = (uint16_t)p_fdo->hca.hw_ver;

	HCA_EXIT( HCA_DBG_PNP );
}

NTSTATUS mlx4_hca_driver_entry()
{
	NTSTATUS status;

	HCA_ENTER( HCA_DBG_PNP );

	status = mlnx_hcas_init();
	if( status	!= STATUS_SUCCESS ) {
		HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_PNP ,
			("mlnx_hcas_init returned %#x.\n", status));
	}

	HCA_EXIT( HCA_DBG_PNP );
	return status;
}

void mlx4_hca_add_device(
	IN		WDFDEVICE FdoDevice,
	IN		PHCA_FDO_DEVICE_DATA 			p_fdo
	)
{
	HCA_ENTER( HCA_DBG_PNP );

	// Init device context.
	RtlZeroMemory(p_fdo, sizeof(HCA_FDO_DEVICE_DATA));
	p_fdo->FdoDevice = FdoDevice;
	p_fdo->p_dev_obj = WdfDeviceWdmGetDeviceObject( FdoDevice );
	spin_lock_init( &p_fdo->uctx_lock );
	cl_qlist_init( &p_fdo->uctx_list );
	atomic_set(&p_fdo->usecnt, 0);
	p_fdo->state = HCA_ADDED;

	HCA_EXIT( HCA_DBG_PNP );
}

NTSTATUS mlx4_hca_start(
	IN		PHCA_FDO_DEVICE_DATA 		p_fdo,
	IN		BUS_INTERFACE_STANDARD		*p_bus_pci_ifc,
	IN		MLX4_BUS_IB_INTERFACE		*p_bus_ib_ifc
	)
{
	int err;

	//
	// from PrepareHardware
	//
	
	// Add interfaces
	p_fdo->p_bus_pci_ifc = p_bus_pci_ifc;
	p_fdo->p_bus_ib_ifc = p_bus_ib_ifc;
	p_fdo->p_bus_ib_ifc->p_ibdev->x.p_fdo = p_fdo;

	// some more inits
	InitializeListHead(&p_fdo->hca.event_list);
	KeInitializeSpinLock(&p_fdo->hca.event_list_lock);

	// get node GUID 
	err = __get_dev_info( p_fdo, &p_fdo->hca.guid, &p_fdo->hca.hw_ver );
	if (err) {

		HCA_PRINT(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,
			("can't get guid - ib_query_device() failed (%08X)\n", err ));
		//TODO: no cleanup on error
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	/* queue HCA  */
	mlnx_hca_insert( &p_fdo->hca );

	// Change the state since the PnP callback can happen before the callback returns.
	p_fdo->state = HCA_STARTED;

	return STATUS_SUCCESS;
}

void mlx4_hca_stop(
	IN		PHCA_FDO_DEVICE_DATA 		p_fdo
	)
{
	// release IBBUS resources
	__hca_release_resources(p_fdo);
}

int mlx4_hca_get_usecnt(PHCA_FDO_DEVICE_DATA p_fdo)
{
	return atomic_read(&p_fdo->usecnt);
}
