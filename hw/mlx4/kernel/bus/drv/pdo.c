#include "precomp.h"
#include <initguid.h>
#include <wdmguid.h>
#include "iba\ibat_ifc.h"

#include "workerThread.h"

#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "pdo.tmh"
#endif 

#define MAX_ID_LEN 80

NTSTATUS Bus_GetRootLocationString(
  __inout  PVOID Context,
  __out    PWCHAR *LocationStrings
)

{
	const int size=30;
	NTSTATUS  status;

	PPDO_DEVICE_DATA			p_pdo = (PPDO_DEVICE_DATA)Context;

	
	WCHAR * temp = (WCHAR *)ExAllocatePoolWithTag(NonPagedPool, size, MT_TAG_KERNEL);
	if (temp == NULL) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("ExAllocatePoolWithTag returned NULL\n"));
		return STATUS_NO_MEMORY;
	}
	// Need to have two zero unicode charcters at the end of the first char
	memset(temp,0,size);
	status = RtlStringCbPrintfW(temp, size, L"MLX4(%d)", p_pdo->SerialNo);
	ASSERT(status == STATUS_SUCCESS);

	*LocationStrings = temp;

	return STATUS_SUCCESS;

}

static NTSTATUS QueryEthInterface(
	PFDO_DEVICE_DATA p_fdo,
	PINTERFACE	ExposedInterface)
{
	PMLX4_BUS_IB_INTERFACE p_ib_ifc = (PMLX4_BUS_IB_INTERFACE)ExposedInterface;

	if (p_fdo->pci_dev.dev) {
		p_ib_ifc->p_ibdev = p_fdo->bus_ib_ifc.p_ibdev;
		p_ib_ifc->pmlx4_dev = p_fdo->bus_ib_ifc.pmlx4_dev;
		p_ib_ifc->p_ibbus_fdo = p_fdo->p_ibbus_fdo;
		return STATUS_SUCCESS;
	}
	else {
		p_ib_ifc->p_ibdev = NULL;
		p_ib_ifc->pmlx4_dev = NULL;
		return STATUS_UNSUCCESSFUL;
	}
}

static NTSTATUS QueryFCoXInterface(
	PFDO_DEVICE_DATA p_fdo,
	PINTERFACE	ExposedInterface)
{
	MLX4_FC_BUS_IF *p_fc_if = (MLX4_FC_BUS_IF *) ExposedInterface;

	if (p_fdo->pci_dev.dev) {
		p_fc_if->pmlx4_dev = p_fdo->bus_ib_ifc.pmlx4_dev;
		return STATUS_SUCCESS;
	}
	else {
		p_fc_if->pmlx4_dev = NULL;
		return STATUS_UNSUCCESSFUL;
	}
}
#ifdef NTDDI_WIN8
EVT_WDF_DEVICE_PROCESS_QUERY_INTERFACE_REQUEST EvtDeviceProcessQueryInterfaceRequest;
#endif
NTSTATUS
EvtDeviceProcessQueryInterfaceRequest(
	IN WDFDEVICE  Device,
	IN LPGUID  InterfaceType,
	IN OUT PINTERFACE  ExposedInterface,
	IN OUT PVOID  ExposedInterfaceSpecificData
	)
{
	PPDO_DEVICE_DATA p_pdo = PdoGetData(Device);
	PFDO_DEVICE_DATA p_fdo	= p_pdo->p_fdo;
	NTSTATUS status;

	UNUSED_PARAM(ExposedInterfaceSpecificData);

	if(memcmp(InterfaceType, &MLX4_BUS_IB_INTERFACE_GUID, sizeof(GUID)) == 0)
	{
		status = QueryEthInterface(p_fdo , ExposedInterface);
	}
	else if(memcmp(InterfaceType, &MLX4_BUS_FC_INTERFACE_GUID, sizeof(GUID)) == 0)
	{
		status = QueryFCoXInterface(p_fdo , ExposedInterface);
	}
	else
	{
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}

#ifdef NTDDI_WIN8
EVT_WDF_DEVICE_PROCESS_QUERY_INTERFACE_REQUEST EvtDeviceProcessQueryInterfaceRequestIb;
#endif
NTSTATUS
EvtDeviceProcessQueryInterfaceRequestIb(
	IN WDFDEVICE  Device,
	IN LPGUID  InterfaceType,
	IN OUT PINTERFACE  ExposedInterface,
	IN OUT PVOID  ExposedInterfaceSpecificData
	)
{
	PFDO_DEVICE_DATA p_fdo = FdoGetData(Device);
	NTSTATUS status;

	UNUSED_PARAM(ExposedInterfaceSpecificData);

	if(memcmp(InterfaceType, &MLX4_BUS_IB_INTERFACE_GUID, sizeof(GUID)) == 0)
	{
		status = QueryEthInterface(p_fdo , ExposedInterface);
	}
	else
	{
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}

static void
__ref_ib_ifc(
	IN				PVOID				Context )
{
	PPDO_DEVICE_DATA p_pdo = (PPDO_DEVICE_DATA)Context;
	PFDO_DEVICE_DATA p_fdo = p_pdo->p_fdo;
	
	cl_atomic_inc( &p_fdo->n_ifc_ref );
	cl_atomic_inc( &p_fdo->bus_ib_ifc_ref );
	ObReferenceObject( p_fdo->p_dev_obj );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Acquired interface 'BUS_IB_IFC', ref %d\n",
		p_fdo->pci_dev.name, p_fdo->bus_ib_ifc_ref) );
}

static void
__deref_ib_ifc(
	IN				PVOID				Context )
{
	PPDO_DEVICE_DATA p_pdo = (PPDO_DEVICE_DATA)Context;
	PFDO_DEVICE_DATA p_fdo = p_pdo->p_fdo;
	int ref_cnt;

	cl_atomic_dec( &p_fdo->bus_ib_ifc_ref );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Released interface 'BUS_IB_IFC', ref %d\n",
		p_fdo->pci_dev.name, p_fdo->bus_ib_ifc_ref) );

	ref_cnt = cl_atomic_dec( &p_fdo->n_ifc_ref );
	ObDereferenceObject( p_fdo->p_dev_obj );
	if ( ref_cnt <= 0 )
		KeSetEvent(&p_fdo->exit_event, 0, FALSE);
}

static void
__ref_fc_ifc(
	IN				PVOID				Context )
{
	PPDO_DEVICE_DATA p_pdo = (PPDO_DEVICE_DATA)Context;
	PFDO_DEVICE_DATA p_fdo = p_pdo->p_fdo;

	cl_atomic_inc( &p_fdo->n_ifc_ref );
	cl_atomic_inc( &p_fdo->bus_fc_ifc_ref );
	ObReferenceObject( p_fdo->p_dev_obj );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Acquired interface 'BUS_FC_IFC', ref %d\n",
		p_fdo->pci_dev.name, p_fdo->bus_fc_ifc_ref) );
}

static void
__deref_fc_ifc(
	IN				PVOID				Context )
{
	PPDO_DEVICE_DATA p_pdo = (PPDO_DEVICE_DATA)Context;
	PFDO_DEVICE_DATA p_fdo = p_pdo->p_fdo;
	int ref_cnt;

	cl_atomic_dec( &p_fdo->bus_fc_ifc_ref );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Released interface 'BUS_FC_IFC', ref %d\n",
		p_fdo->pci_dev.name, p_fdo->bus_fc_ifc_ref) );

	ref_cnt = cl_atomic_dec( &p_fdo->n_ifc_ref );
	ObDereferenceObject( p_fdo->p_dev_obj );
	if ( ref_cnt <= 0 )
		KeSetEvent(&p_fdo->exit_event, 0, FALSE);
}

static void
__ref_loc_ifc(
	IN				PVOID				Context )
{
	PPDO_DEVICE_DATA p_pdo = (PPDO_DEVICE_DATA)Context;
	PFDO_DEVICE_DATA p_fdo = p_pdo->p_fdo;

	cl_atomic_inc( &p_fdo->n_ifc_ref );
	cl_atomic_inc( &p_fdo->bus_loc_ifc_ref );
	ObReferenceObject( p_fdo->p_dev_obj );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Acquired interface 'BUS_LOCATION_IFC', ref %d\n",
		p_fdo->pci_dev.name, p_fdo->bus_loc_ifc_ref) );
}

static void
__deref_loc_ifc(
	IN				PVOID				Context )
{
	PPDO_DEVICE_DATA p_pdo = (PPDO_DEVICE_DATA)Context;
	PFDO_DEVICE_DATA p_fdo = p_pdo->p_fdo;
	int ref_cnt;

	cl_atomic_dec( &p_fdo->bus_loc_ifc_ref );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Released interface 'BUS_LOCATION_IFC', ref %d\n",
		p_fdo->pci_dev.name, p_fdo->bus_loc_ifc_ref) );

	ref_cnt = cl_atomic_dec( &p_fdo->n_ifc_ref );
	ObDereferenceObject( p_fdo->p_dev_obj );
	if ( ref_cnt <= 0 )
		KeSetEvent(&p_fdo->exit_event, 0, FALSE);
}

static void
__ref_not_ifc(
	IN				PVOID				Context )
{
	PFDO_DEVICE_DATA p_fdo = (PFDO_DEVICE_DATA)Context;

	cl_atomic_inc( &p_fdo->n_ifc_ref );
	cl_atomic_inc( &p_fdo->bus_not_ifc_ref );
	ObReferenceObject( p_fdo->p_dev_obj );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Acquired interface 'BUS_NOTIFY_IFC', ref %d\n",
		p_fdo->pci_dev.name, p_fdo->bus_not_ifc_ref) );
}

static void
__deref_not_ifc(
	IN				PVOID				Context )
{
	PFDO_DEVICE_DATA p_fdo = (PFDO_DEVICE_DATA)Context;
	int ref_cnt;
	
	cl_atomic_dec( &p_fdo->bus_not_ifc_ref );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Released interface 'BUS_NOTIFY_IFC', ref %d\n",
		p_fdo->pci_dev.name, p_fdo->bus_not_ifc_ref) );

	ref_cnt = cl_atomic_dec( &p_fdo->n_ifc_ref );
	ObDereferenceObject( p_fdo->p_dev_obj );
	if ( ref_cnt <= 0 )
		KeSetEvent(&p_fdo->exit_event, 0, FALSE);
}

static 
NTSTATUS
__create_notify_ifc(
	IN PFDO_DEVICE_DATA p_fdo,
	IN WDFDEVICE Device
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	struct mlx4_dev *mdev	= p_fdo->pci_dev.dev;
	PMLX4_BUS_NOTIFY_INTERFACE p_ifc = &p_fdo->bus_not_ifc;
	WDF_QUERY_INTERFACE_CONFIG	qiConfig;
	
	RtlZeroMemory( p_ifc, sizeof(MLX4_BUS_NOTIFY_INTERFACE));
	
	p_ifc->i.Size = sizeof(MLX4_BUS_NOTIFY_INTERFACE);
	p_ifc->i.Version = 1;
	p_ifc->i.Context = (PVOID)p_fdo;
	
	//
	// Let the framework handle reference counting.
	//
	p_ifc->i.InterfaceReference = __ref_not_ifc;
	p_ifc->i.InterfaceDereference = __deref_not_ifc;

	p_ifc->notify = notify_cb;
	
	WDF_QUERY_INTERFACE_CONFIG_INIT(&qiConfig, (PINTERFACE) p_ifc,
		&MLX4_BUS_NOTIFY_GUID, NULL);
	
	status = WdfDeviceAddQueryInterface(Device, &qiConfig);
	if (!NT_SUCCESS(status)) {
		MLX4_PRINT_EV(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("WdfDeviceAddQueryInterface failed with 0x%x\n", status));
	}

	return status;
}

void FillVPIInterface( 
	PFDO_DEVICE_DATA p_fdo, 
	ULONG SerialNo,
	PPDO_DEVICE_DATA p_pdo
	)
{
	//
	// Set bus IB interface
	//
	p_fdo->bus_ib_ifc.port_id = (u8) SerialNo;
	p_fdo->bus_ib_ifc.pVipBusIfc = &p_fdo->mtnic_Ifc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_pd_alloc = mlx4_pd_alloc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_pd_free = mlx4_pd_free;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_uar_alloc = mlx4_uar_alloc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_uar_free = mlx4_uar_free;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_mr_alloc = mlx4_mr_alloc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_mr_free = mlx4_mr_free;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_mr_enable = mlx4_mr_enable;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_qp_get_region = mlx4_qp_get_region;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_alloc_hwq_res = mlx4_alloc_hwq_res;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_free_hwq_res = mlx4_free_hwq_res;
	
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_cq_alloc = mlx4_cq_alloc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_cq_free = mlx4_cq_free;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_cq_modify = mlx4_cq_modify;
	
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_replace_mac = mlx4_replace_mac;
	
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_srq_alloc = mlx4_srq_alloc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_srq_free = mlx4_srq_free;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_srq_invalidate = mlx4_srq_invalidate;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_srq_remove = mlx4_srq_remove;
	
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_qp_alloc = mlx4_qp_alloc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_qp_free = mlx4_qp_free;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_qp_remove = mlx4_qp_remove;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_qp_modify = mlx4_qp_modify;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_qp_to_ready = mlx4_qp_to_ready;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_qp_reserve_range = mlx4_qp_reserve_range;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_qp_release_range = mlx4_qp_release_range;
	
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_alloc_cmd_mailbox = mlx4_alloc_cmd_mailbox;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_free_cmd_mailbox = mlx4_free_cmd_mailbox;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_cmd = imlx4_cmd;
	
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_INIT_PORT = mlx4_INIT_PORT;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_CLOSE_PORT = mlx4_CLOSE_PORT;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_add_eq = mlx4_add_eq;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_remove_eq = mlx4_remove_eq;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_process_eqes = mlx4_process_eqes;

	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_register_ev_cb = mlx4_reset_cb_register;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_unregister_ev_cb = mlx4_reset_cb_unregister;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_reset_request = mlx4_reset_request;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_reset_execute = mlx4_reset_execute;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_reset_ready = mlx4_reset_ready;

	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_register_mac_and_vlan = mlx4_register_mac_and_vlan;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_unregister_mac_and_vlan = mlx4_unregister_mac_and_vlan;
	
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_multicast_attach = mlx4_multicast_attach;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_multicast_detach = mlx4_multicast_detach;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_multicast_promisc_add = mlx4_multicast_promisc_add;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_multicast_promisc_remove = mlx4_multicast_promisc_remove;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_enable_unicast_promisc = mlx4_enable_unicast_promisc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_disable_unicast_promisc = mlx4_disable_unicast_promisc;
	
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_set_vlan_fltr = mlx4_SET_VLAN_FLTR;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_set_mcast_fltr = mlx4_SET_MCAST_FLTR;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_set_port_qpn_calc = mlx4_SET_PORT_qpn_calc;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_set_port_general = mlx4_SET_PORT_general;
    p_fdo->bus_ib_ifc.mlx4_interface.mlx4_query_port = mlx4_QUERY_PORT;

    p_fdo->bus_ib_ifc.mlx4_interface.mlx4_set_port_prio2tc = mlx4_SET_PORT_PRIO2TC;
    p_fdo->bus_ib_ifc.mlx4_interface.mlx4_set_port_scheduler= mlx4_SET_PORT_SCHEDULER;

    p_fdo->bus_ib_ifc.mlx4_interface.mlx4_query_if_stat = mlx4_QUERY_IF_STAT;

	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_set_wol = mlx4_set_wol;

	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_set_protocol_qos = mlx4_set_protocol_qos;
	
	/* Diagnostics */
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_test_interrupts = mlx4_test_interrupts;

	/* WorkerThread */
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_add_ring = mlx4_add_ring;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_remove_ring = mlx4_remove_ring;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_check_if_starving = mlx4_check_if_starving;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_start_polling = mlx4_start_polling;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_stop_polling = mlx4_stop_polling;
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_continue_polling = mlx4_continue_polling;

	/* Legacy statistics */
	p_fdo->bus_ib_ifc.mlx4_interface.mlx4_dump_stats = mlx4_DUMP_ETH_STATS;
		
	p_fdo->bus_ib_ifc.i.InterfaceReference = __ref_ib_ifc;
	p_fdo->bus_ib_ifc.i.InterfaceDereference = __deref_ib_ifc;
	p_fdo->bus_ib_ifc.i.Context = p_pdo;

}

NTSTATUS RegisterVPIInterfaceIb(PPDO_DEVICE_DATA p_pdo, 
						   PFDO_DEVICE_DATA p_fdo, 
						   WDFDEVICE hChild,
						   PWCHAR DeviceDescription,
						   ULONG SerialNo)
{
	NTSTATUS status;
	UNREFERENCED_PARAMETER(DeviceDescription);

	FillVPIInterface( p_fdo, SerialNo, p_pdo );

	status = __create_notify_ifc(p_fdo, hChild );
	if (!NT_SUCCESS(status)) 
		return status;		

	WDF_QUERY_INTERFACE_CONFIG_INIT( &p_pdo->qiMlx4Bus,
		(PINTERFACE) &p_fdo->bus_ib_ifc,
		&MLX4_BUS_IB_INTERFACE_GUID, EvtDeviceProcessQueryInterfaceRequestIb);

	status = WdfDeviceAddQueryInterface( hChild, &p_pdo->qiMlx4Bus );

	return status;
}

static NTSTATUS RegisterVPIInterfaceEth(PPDO_DEVICE_DATA p_pdo, 
						   PFDO_DEVICE_DATA p_fdo, 
						   WDFDEVICE hChild,
						   PWCHAR DeviceDescription,
						   ULONG SerialNo)
{
	NTSTATUS status;
	UNREFERENCED_PARAMETER(DeviceDescription);

	FillVPIInterface( p_fdo, SerialNo, p_pdo );

	p_fdo->bus_ib_ifc.p_ibbus_fdo = p_fdo->p_ibbus_fdo;

	WDF_QUERY_INTERFACE_CONFIG_INIT( &p_pdo->qiMlx4Bus,
		(PINTERFACE) &p_fdo->bus_ib_ifc,
		&MLX4_BUS_IB_INTERFACE_GUID, EvtDeviceProcessQueryInterfaceRequest);
	
	status = WdfDeviceAddQueryInterface( hChild, &p_pdo->qiMlx4Bus );

	return status;
}

static NTSTATUS RegisterFCoXInterface(PPDO_DEVICE_DATA p_pdo, 
						   PFDO_DEVICE_DATA p_fdo, 
						   WDFDEVICE hChild,
						   PWCHAR DeviceDescription,
						   ULONG SerialNo)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	UNREFERENCED_PARAMETER(DeviceDescription);

	//
	// Setup bus FC interface
	//	  
	p_fdo->bus_fc_if.port_id = (u8) (SerialNo - p_fdo->bus_fc_if.pmlx4_dev->caps.num_ports);
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_alloc_hwq_res = mlx4_alloc_hwq_res;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_free_hwq_res = mlx4_free_hwq_res;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_qp_reserve_range = mlx4_qp_reserve_range;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_qp_release_range = mlx4_qp_release_range;	
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_alloc_cmd_mailbox = mlx4_alloc_cmd_mailbox;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_free_cmd_mailbox = mlx4_free_cmd_mailbox;	
	//p_fdo->bus_fc_if.mlx4_bus_api.mlx4_register_mac = mlx4_register_mac;
	//p_fdo->bus_fc_if.mlx4_bus_api.mlx4_unregister_mac = mlx4_unregister_mac;	
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_mr_reserve_range = mlx4_mr_reserve_range;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_mr_release_range = mlx4_mr_release_range;	
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_mr_alloc = mlx4_mr_alloc;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_mr_free = mlx4_mr_free;	  
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_mr_enable = mlx4_mr_enable;	  
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_pd_alloc = mlx4_pd_alloc;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_pd_free = mlx4_pd_free;	  
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_uar_alloc = mlx4_uar_alloc;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_uar_free = mlx4_uar_free;	
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_qp_alloc = mlx4_qp_alloc;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_qp_free = mlx4_qp_free;	  
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_qp_query = mlx4_qp_query;	
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_qp_remove = mlx4_qp_remove;	  
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_qp_to_ready = mlx4_qp_to_ready;	  
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_cq_alloc = mlx4_cq_alloc;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_cq_free = mlx4_cq_free;	  
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_register_interface = mlx4_register_interface;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_unregister_interface = mlx4_unregister_interface;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_cmd = imlx4_cmd;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_map_phys_fmr_fbo = mlx4_map_phys_fmr_fbo;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_fmr_unmap = mlx4_fmr_unmap;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_fmr_alloc_reserved = mlx4_fmr_alloc_reserved;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_fmr_enable = mlx4_fmr_enable;
	p_fdo->bus_fc_if.mlx4_bus_api.mlx4_fmr_free_reserved = mlx4_fmr_free_reserved;

	//
	// Create a custom interface so that other drivers can
	// query (IRP_MN_QUERY_INTERFACE) and use our callbacks directly.
	//
	p_fdo->bus_fc_if.i.InterfaceReference = __ref_fc_ifc;
	p_fdo->bus_fc_if.i.InterfaceDereference = __deref_fc_ifc;
	p_fdo->bus_fc_if.i.Context = p_pdo;

	WDF_QUERY_INTERFACE_CONFIG_INIT( &p_pdo->qiMlx4FcoxIf,
		(PINTERFACE) &p_fdo->bus_fc_if,
		&MLX4_BUS_FC_INTERFACE_GUID, EvtDeviceProcessQueryInterfaceRequest);

	status = WdfDeviceAddQueryInterface( hChild, &p_pdo->qiMlx4FcoxIf );

	return status;
}

NTSTATUS RegisterInterface(PPDO_DEVICE_DATA p_pdo, 
						   PFDO_DEVICE_DATA p_fdo, 
						   WDFDEVICE hChild,
						   PWCHAR DeviceDescription,
						   ULONG SerialNo)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	if( (wcscmp(DeviceDescription, ETH_HARDWARE_DESCRIPTION) == 0) )
	{
		status = RegisterVPIInterfaceEth(p_pdo, p_fdo, hChild, DeviceDescription, SerialNo);
	}
	else if(wcscmp(DeviceDescription, FCOE_HARDWARE_DESCRIPTION) == 0)
	{
		status = RegisterFCoXInterface(p_pdo, p_fdo, hChild, DeviceDescription, SerialNo);
	}
	else
	{
		ASSERT(FALSE);
	}
	return status;
}

#if (NTDDI_VERSION < NTDDI_VISTA)
// It seems that on the 6001.18001 versions on the ddk this does not appear although it appears on 
// previous versions and is documented to be in windows 2003.
DEFINE_GUID( GUID_PNP_LOCATION_INTERFACE,		   0x70211b0e,	0x0afb, 0x47db, 0xaf, 0xc1, 0x41, 0x0b, 0xf8, 0x42, 0x49, 0x7a );
#endif

NTSTATUS
create_pdo(
	__in WDFDEVICE	Device,
	__in PWCHAR 	HardwareIds,
	__in PWCHAR 	CompatibleIds,
	__in PWCHAR 	DeviceDescription,
	__in ULONG		SerialNo,
	__in PWCHAR 	Location
)
/*++

Routine Description:

	This routine creates and initialize a PDO.

Arguments:

Return Value:

	NT Status code.

--*/
{
	NTSTATUS					status;
	PWDFDEVICE_INIT 			pDeviceInit = NULL;
	PPDO_DEVICE_DATA			p_pdo = NULL;
	PFDO_DEVICE_DATA			p_fdo;
	WDFDEVICE					hChild = NULL;
	WDF_OBJECT_ATTRIBUTES		pdoAttributes;
	WDF_DEVICE_PNP_CAPABILITIES pnpCaps;
	WDF_DEVICE_POWER_CAPABILITIES powerCaps;
	UNICODE_STRING compatId;
	UNICODE_STRING compatIdGeneral;
	UNICODE_STRING deviceId;
	DECLARE_UNICODE_STRING_SIZE(buffer, MAX_ID_LEN);
	DECLARE_UNICODE_STRING_SIZE(deviceLocation, MAX_ID_LEN);
	PNP_LOCATION_INTERFACE		locationInterface;
	WDF_QUERY_INTERFACE_CONFIG	qiConfig;
	struct mlx4_dev *mdev;
    bool f_wol = false;
    
	MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV, ("Entered CreatePdo\n"));

	RtlInitUnicodeString(&compatIdGeneral, CompatibleIds);
	RtlInitUnicodeString(&compatId, HardwareIds);

	//
	// Allocate a WDFDEVICE_INIT structure and set the properties
	// so that we can create a device object for the child.
	//
	pDeviceInit = WdfPdoInitAllocate(Device);

	if (pDeviceInit == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Cleanup;
	}

	//
	// Set DeviceType
	//
	WdfDeviceInitSetDeviceType(pDeviceInit, FILE_DEVICE_BUS_EXTENDER);

	//
	// Provide DeviceID, HardwareIDs, CompatibleIDs and InstanceId
	//
	RtlInitUnicodeString(&deviceId,HardwareIds);

	status = WdfPdoInitAssignDeviceID(pDeviceInit, &deviceId);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	//
	// Note same string  is used to initialize hardware id too
	//
	status = WdfPdoInitAddHardwareID(pDeviceInit, &deviceId);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	status = WdfPdoInitAddCompatibleID(pDeviceInit, &compatId);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	if (RtlCompareUnicodeString(&compatId, &compatIdGeneral, FALSE)) {		  
		status = WdfPdoInitAddCompatibleID(pDeviceInit, &compatIdGeneral);
		if (!NT_SUCCESS(status)) {
			goto Cleanup;
		}		 
	}
	
	status =  RtlUnicodeStringPrintf(&buffer, L"%02d", SerialNo);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	status = WdfPdoInitAssignInstanceID(pDeviceInit, &buffer);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	//
	// Provide a description about the device. This text is usually read from
	// the device. In the case of USB device, this text comes from the string
	// descriptor. This text is displayed momentarily by the PnP manager while
	// it's looking for a matching INF. If it finds one, it uses the Device
	// Description from the INF file or the friendly name created by
	// coinstallers to display in the device manager. FriendlyName takes
	// precedence over the DeviceDesc from the INF file.
	//
	status = RtlUnicodeStringPrintf(&buffer,DeviceDescription  , SerialNo );
	
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	//
	// You can call WdfPdoInitAddDeviceText multiple times, adding device
	// text for multiple locales. When the system displays the text, it
	// chooses the text that matches the current locale, if available.
	// Otherwise it will use the string for the default locale.
	// The driver can specify the driver's default locale by calling
	// WdfPdoInitSetDefaultLocale.
	//
	status =  RtlUnicodeStringPrintf(&deviceLocation, L"MLX4 %s", Location+4);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	status = WdfPdoInitAddDeviceText(pDeviceInit,
		&buffer, &deviceLocation, 0x409);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	WdfPdoInitSetDefaultLocale(pDeviceInit, 0x409);

	//
	// Initialize the attributes to specify the size of PDO device extension.
	// All the state information private to the PDO will be tracked here.
	//
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttributes, PDO_DEVICE_DATA);

	status = WdfDeviceCreate(&pDeviceInit, &pdoAttributes, &hChild);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	//
	// Once the device is created successfully, framework frees the
	// DeviceInit memory and sets the pDeviceInit to NULL. So don't
	// call any WdfDeviceInit functions after that.
	//
	// Get the device context.
	//
	p_pdo = PdoGetData(hChild);
	p_fdo = FdoGetData(Device);
    mdev = p_fdo->pci_dev.dev;
	p_pdo->p_fdo = p_fdo;
	p_pdo->SerialNo = SerialNo;
	p_pdo->PdoDevice = hChild;

	//
	// Set PnP properties for the child device.
	//
	WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
	pnpCaps.Address  = SerialNo;
	pnpCaps.UINumber = SerialNo;
	pnpCaps.SurpriseRemovalOK = WdfTrue;
	pnpCaps.Removable = WdfTrue;

	WdfDeviceSetPnpCapabilities(hChild, &pnpCaps);

	//
	// Set Power properties for the child device
	//
	WDF_DEVICE_POWER_CAPABILITIES_INIT(&powerCaps);

    f_wol = is_wol_supported(mdev, SerialNo);
        
	powerCaps.DeviceD1	 = WdfTrue;
	powerCaps.WakeFromD0 = WdfTrue;
	powerCaps.WakeFromD1 = WdfTrue;
	powerCaps.WakeFromD2 = WdfTrue;
	powerCaps.WakeFromD3 = WdfTrue;
	powerCaps.DeviceWake = f_wol ? PowerDeviceD3 : PowerDeviceD1;
	powerCaps.SystemWake  = f_wol ? PowerSystemHibernate : PowerSystemWorking;

	powerCaps.DeviceState[PowerSystemWorking]	= PowerDeviceD0;
	powerCaps.DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
	powerCaps.DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
	powerCaps.DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
	powerCaps.DeviceState[PowerSystemHibernate] = PowerDeviceD3;
	powerCaps.DeviceState[PowerSystemShutdown] = PowerDeviceD3;

	WdfDeviceSetPowerCapabilities(hChild, &powerCaps);

	status = RegisterInterface(p_pdo, p_fdo, hChild, DeviceDescription, SerialNo);
	if (!NT_SUCCESS(status))
		goto Cleanup;

	//
	// Expose also PCI.SYS interface for MLX4_HCA
	//
	WDF_QUERY_INTERFACE_CONFIG_INIT( &qiConfig, NULL, &GUID_BUS_INTERFACE_STANDARD, NULL);
    qiConfig.SendQueryToParentStack = TRUE;

	// TODO: Soft Reset - how tobar getting interface during RESET_IN_PROGRESS
	// maybe - using EvtDeviceProcessQueryInterfaceRequest
	status = WdfDeviceAddQueryInterface( hChild, &qiConfig );
	if (!NT_SUCCESS(status))
		goto Cleanup;

    WDF_QUERY_INTERFACE_CONFIG_INIT( &qiConfig, NULL, &GUID_IBAT_INTERFACE, NULL );
    qiConfig.SendQueryToParentStack = TRUE;

    status = WdfDeviceAddQueryInterface( hChild, &qiConfig );
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

	//
	// Create a custom interface so that other drivers can
	// query (IRP_MN_QUERY_INTERFACE) and use our callbacks directly.
	//
	
	RtlZeroMemory(&locationInterface, sizeof(locationInterface));
	
	locationInterface.Size = sizeof(locationInterface);
	locationInterface.Version = 1;
	locationInterface.Context = (PVOID)p_pdo;// device;

	//
	// Let the framework handle reference counting.
	//
	locationInterface.InterfaceReference = __ref_loc_ifc;
	locationInterface.InterfaceDereference = __deref_loc_ifc;

	locationInterface.GetLocationString  = Bus_GetRootLocationString;
	
	WDF_QUERY_INTERFACE_CONFIG_INIT(&qiConfig,
									(PINTERFACE) &locationInterface,
									&GUID_PNP_LOCATION_INTERFACE,
									NULL);
	
	status = WdfDeviceAddQueryInterface(hChild, &qiConfig);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}
	
	WdfDeviceSetSpecialFileSupport(hChild,WdfSpecialFilePaging,TRUE);
	WdfDeviceSetSpecialFileSupport(hChild,WdfSpecialFileHibernation,TRUE);
	WdfDeviceSetSpecialFileSupport(hChild,WdfSpecialFileDump,TRUE);

	//
	// Add this device to the FDO's collection of children.
	// After the child device is added to the static collection successfully,
	// driver must call WdfPdoMarkMissing to get the device deleted. It
	// shouldn't delete the child device directly by calling WdfObjectDelete.
	//
	status = WdfFdoAddStaticChild(Device, hChild);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	return status;

Cleanup:
	KdPrint(("BusEnum: Bus_CreatePdo failed %x\n", status));

	//
	// Call WdfDeviceInitFree if you encounter an error before the
	// device is created. Once the device is created, framework
	// NULLs the pDeviceInit value.
	//
	if (pDeviceInit != NULL) {
		WdfDeviceInitFree(pDeviceInit);
	}

	if(hChild) {
		WdfObjectDelete(hChild);
	}

	return status;
}


