/*++

Copyright (c) 2003	Microsoft Corporation All Rights Reserved

Module Name:

	BUSENUM.C

Abstract:

	This module contains routines to handle the function driver
	aspect of the bus driver. This sample is functionally
	equivalent to the WDM mxe bus driver.

Environment:

	kernel mode only

--*/

#include "precomp.h"
#include <initguid.h>
#include <wdmguid.h>

#include "workerThread.h"
#include <complib/cl_init.h>


#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "drv.tmh"
#endif 


#define DRV_VERSION	"1.0"
#define DRV_RELDATE	"02/01/2008"

GLOBALS g = {0};
uint32_t g_mlx4_dbg_flags = 0xffff;
uint32_t g_mlx4_dbg_level = TRACE_LEVEL_INFORMATION;
WCHAR g_wlog_buf[ MAX_LOG_BUF_LEN ];
UCHAR g_slog_buf[ MAX_LOG_BUF_LEN ];
u32 g_time_increment = 0;
int g_MaximumWorkingThreads = 4;

const WCHAR x_ParamtersKey[] = L"\\Parameters";

#ifndef USE_WDM_INTERRUPTS

typedef struct {
	int					int_num;
	PFDO_DEVICE_DATA	p_fdo;
	struct mlx4_eq *	eq;
} INTERRUPT_DATA, *PINTERRUPT_DATA;

WDF_DECLARE_CONTEXT_TYPE(INTERRUPT_DATA);

NTSTATUS	
EvtEnableInterrupt(	
	IN WDFINTERRUPT  Interrupt,
	IN WDFDEVICE  AssociatedDevice
	)
{
	UNUSED_PARAM(Interrupt);
	UNUSED_PARAM(AssociatedDevice);
	MLX4_ENTER(MLX4_DBG_DRV);
	MLX4_EXIT( MLX4_DBG_DRV );
	return STATUS_SUCCESS;
}

NTSTATUS
EvtDisableInterrupt (
	IN WDFINTERRUPT  Interrupt,
	IN WDFDEVICE  AssociatedDevice
	)
{
	UNUSED_PARAM(Interrupt);
	UNUSED_PARAM(AssociatedDevice);
	MLX4_ENTER(MLX4_DBG_DRV);
	MLX4_EXIT( MLX4_DBG_DRV );
	return STATUS_SUCCESS;
}

BOOLEAN
EvtInterruptIsr(
	IN WDFINTERRUPT  Interrupt,
	IN ULONG  MessageID
	)
{
	BOOLEAN isr_handled = FALSE;
	PINTERRUPT_DATA p_isr_ctx = WdfObjectGetTypedContext( Interrupt, INTERRUPT_DATA );

	UNUSED_PARAM(MessageID);

//	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, ("Fdo %p\n", p_isr_ctx->p_fdo));
	if (p_isr_ctx->eq && p_isr_ctx->eq->isr)
		isr_handled = p_isr_ctx->eq->isr( p_isr_ctx->eq->eq_ix, p_isr_ctx->eq->ctx );
	
	return isr_handled;
}

#endif

// In units of ms
uint64_t get_tickcount_in_ms(void)
{
	LARGE_INTEGER	Ticks;

	KeQueryTickCount(&Ticks);
	return Ticks.QuadPart * g_time_increment / 10000; // 10,000 moves from 100ns to ms
}

NTSTATUS
__create_child(
	__in PFDO_DEVICE_DATA	p_fdo,
	__in PWCHAR 	HardwareIds,
	__in PWCHAR 	CompatibleIds,
	__in PWCHAR 	DeviceDescription,
	__in ULONG		SerialNo
	)

/*++

Routine Description:

	The user application has told us that a new device on the bus has arrived.

	We therefore need to create a new PDO, initialize it, add it to the list
	of PDOs for this FDO bus, and then tell Plug and Play that all of this
	happened so that it will start sending prodding IRPs.

--*/

{
	NTSTATUS			status = STATUS_SUCCESS;
	BOOLEAN				unique = TRUE;
	WDFDEVICE			hChild;
	PPDO_DEVICE_DATA	p_pdo;
	WDFDEVICE			Device = p_fdo->FdoDevice;

	MLX4_ENTER(MLX4_DBG_DRV);

	//
	// First make sure that we don't already have another device with the
	// same serial number.
	// Framework creates a collection of all the child devices we have
	// created so far. So acquire the handle to the collection and lock
	// it before walking the item.
	//
	hChild = NULL;

	//
	// We need an additional lock to synchronize addition because
	// WdfFdoLockStaticChildListForIteration locks against anyone immediately
	// updating the static child list (the changes are put on a queue until the
	// list has been unlocked).  This type of lock does not enforce our concept
	// of unique IDs on the bus (ie SerialNo).
	//
	// Without our additional lock, 2 threads could execute this function, both
	// find that the requested SerialNo is not in the list and attempt to add
	// it.	If that were to occur, 2 PDOs would have the same unique SerialNo,
	// which is incorrect.
	//
	// We must use a passive level lock because you can only call WdfDeviceCreate
	// at PASSIVE_LEVEL.
	//
	WdfWaitLockAcquire(p_fdo->ChildLock, NULL);
	WdfFdoLockStaticChildListForIteration(Device);

	while ((hChild = WdfFdoRetrieveNextStaticChild(Device,
		hChild, WdfRetrieveAddedChildren)) != NULL) {
		//
		// WdfFdoRetrieveNextStaticChild returns reported and to be reported
		// children (ie children who have been added but not yet reported to PNP).
		//
		// A surprise removed child will not be returned in this list.
		//
		p_pdo = PdoGetData(hChild);
		p_pdo->PdoDevice = hChild;
		p_pdo->p_fdo = p_fdo;

		//
		// It's okay to plug in another device with the same serial number
		// as long as the previous one is in a surprise-removed state. The
		// previous one would be in that state after the device has been
		// physically removed, if somebody has an handle open to it.
		//
		if (SerialNo == p_pdo->SerialNo) {
		unique = FALSE;
		status = STATUS_INVALID_PARAMETER;
		break;
		}
	}

	if (unique) {
		//
		// Create a new child device.  It is OK to create and add a child while
		// the list locked for enumeration.  The enumeration lock applies only
		// to enumeration, not addition or removal.
		//
		status = create_pdo(Device, HardwareIds, CompatibleIds, DeviceDescription, SerialNo, p_fdo->pci_dev.location);
	}

	WdfFdoUnlockStaticChildListFromIteration(Device);
	WdfWaitLockRelease(p_fdo->ChildLock);

	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}


static 
NTSTATUS
__create_ib_devices(
	IN PFDO_DEVICE_DATA p_fdo
	)
/*++
Routine Description:

	The routine enables you to statically enumerate child devices
	during start instead of running the enum.exe/notify.exe to
	enumerate mxe devices.

	In order to statically enumerate, user must specify the number
	of mxes in the Mxe Bus driver's device registry. The
	default value is 2.

	You can also configure this value in the Mxe Bus Inf file.

--*/

{
	NTSTATUS status = STATUS_SUCCESS;
	int number_of_ib_ports;
	WDFDEVICE Device = p_fdo->FdoDevice;
	PPDO_DEVICE_DATA p_pdo	= &p_fdo->mlx4_hca_pdo;
	struct mlx4_dev *mdev	= p_fdo->pci_dev.dev;

	MLX4_ENTER(MLX4_DBG_DRV);
    
	if ( p_fdo->children_created )
		goto end;
	
	// eventually we'll have all information about children in Registry
	// DriverEntry will read it into a Global storage and
	// this routine will create all the children on base on this info
	number_of_ib_ports = mlx4_count_ib_ports(mdev);
	ASSERT(number_of_ib_ports >=0 && number_of_ib_ports <=2);


	//For now we it's either IB or ETH, and we always create LLE if it's ETH
	if(number_of_ib_ports > 0) {
		
		p_pdo->p_fdo = p_fdo;
		p_pdo->SerialNo = 0;
		p_pdo->PdoDevice = Device;

		status = RegisterVPIInterfaceIb( 
			p_pdo, p_fdo, Device, BUS_HARDWARE_DESCRIPTION, 0);

		if (!NT_SUCCESS(status)) {
			MLX4_PRINT_EV(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("RegisterVPIInterfaceIbfailed with 0x%x\n", status));
			 goto end;
		}
		else {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("Created MLX4_BUS_IB_INTERFACE_GUID interface for %d IB ports: port_type1 '%s', port_type2 '%s'\n", 
				number_of_ib_ports, mlx4_get_port_name(mdev, 1),
				(number_of_ib_ports > 1) ? mlx4_get_port_name(mdev, 2) : "n/a" ));
		}
        p_fdo->pci_dev.is_reset_prohibited = TRUE;
	}

end:
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}


static 
NTSTATUS
__create_eth_devices(
	IN PFDO_DEVICE_DATA p_fdo
	)
/*++
Routine Description:

	The routine enables you to statically enumerate child devices
	during start instead of running the enum.exe/notify.exe to
	enumerate mxe devices.

	In order to statically enumerate, user must specify the number
	of mxes in the Mxe Bus driver's device registry. The
	default value is 2.

	You can also configure this value in the Mxe Bus Inf file.

--*/

{
	NTSTATUS status = STATUS_SUCCESS;
	u8 i;
	struct mlx4_dev *mdev	= p_fdo->pci_dev.dev;
	BOOLEAN eth_created = FALSE;

#define MAX_HW_ID_STR 100    
    WCHAR eth_hw_ids[MAX_HW_ID_STR] = {0};

	MLX4_ENTER(MLX4_DBG_DRV);
    
#ifdef HP_PROD
    RtlStringCchPrintfW(eth_hw_ids,MAX_HW_ID_STR,L"%s&%x%x",ETH_HARDWARE_IDS,
                                                            mdev->pdev->sub_system_id,
                                                            mdev->pdev->sub_vendor_id
                                                            );
#else
    RtlStringCchCopyW(eth_hw_ids, MAX_HW_ID_STR, ETH_HARDWARE_IDS);
#endif

	if ( p_fdo->children_created )
		goto end;
	
	for (i = 1; i <= mdev->caps.num_ports; i++) {
		if (mlx4_is_enabled_port(mdev, i)) {
			if(mlx4_is_eth_port(mdev, i)) {
				status = __create_child(p_fdo, eth_hw_ids, ETH_COMPATIBLE_IDS, ETH_HARDWARE_DESCRIPTION, i);
				if (!NT_SUCCESS(status)) {
					MLX4_PRINT_EV(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("%s: __create_child (eth) failed with 0x%x\n", 
						mdev->pdev->name, status));
					break;
				}
				else {
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("Created mlx4ethx PDO for port %d: port_type '%s'\n", 
						i, mlx4_get_port_name(mdev, i) ));
				}
				eth_created = TRUE;

				if(mdev->caps.enable_fcox[i])
				{
					status = __create_child(p_fdo, FCOE_HARDWARE_IDS, FCOE_HARDWARE_IDS, FCOE_HARDWARE_DESCRIPTION, mdev->caps.num_ports + i);
					if (!NT_SUCCESS(status)) {
						 MLX4_PRINT_EV(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("__create_child (fcoe) failed with 0x%x\n", status));
						 break;
					}
					else {
						MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("Created mlx4fcox PDO for port %d: port_type '%s'\n", 
							i, mlx4_get_port_name(mdev, i) ));
					}
				}
			} else {
				if (eth_created){
					//
					// Illegal configuration the IB should be the first port
					//
					status = STATUS_INVALID_PARAMETER;
					MLX4_PRINT_EV(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("%s: __create_child (IB) failed. Invalid configuration, IB should be the first port.", mdev->pdev->name ));
					break;
				}
			}
		}
	}
	
end:
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}


static 
VOID
__complete_enumeration(
	IN PFDO_DEVICE_DATA p_fdo
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	struct mlx4_dev *mdev	= p_fdo->pci_dev.dev;

	MLX4_ENTER(MLX4_DBG_DRV);

	// create ETH  devices
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("__complete_enumeration: calling __create_eth_devices\n"));
	status = __create_eth_devices( p_fdo );
	if (!NT_SUCCESS(status)) {
		MLX4_PRINT_EV(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("__create_eth_devices failed with 0x%x\n", status));
	}

	p_fdo->children_created = TRUE;
		
	MLX4_EXIT( MLX4_DBG_DRV );
}


VOID notify_cb (PVOID ifc_ctx, ULONG type, PVOID p_data, PCHAR str)
{
	PFDO_DEVICE_DATA p_fdo	= (PFDO_DEVICE_DATA)ifc_ctx;
	struct mlx4_dev *mdev	= p_fdo->pci_dev.dev;
	int n_roce_ports = mlx4_count_roce_ports( mdev );

	UNREFERENCED_PARAMETER(type);

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("%s\n", str));
	p_fdo->p_ibbus_fdo = (PDEVICE_OBJECT)p_data;
	if ( n_roce_ports ) 
	{
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("__notify_cb: calling __complete_enumeration\n"));
		WriteEventLogEntryData( p_fdo->pci_dev.p_self_do, (ULONG)EVENT_MLX4_INFO_ROCE_START, 0, 0, 1,
			L"%s", p_fdo->pci_dev.location );
		__complete_enumeration( p_fdo );
	}
}

static 
NTSTATUS
__do_static_enumeration(
	IN WDFDEVICE Device
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	PFDO_DEVICE_DATA p_fdo	= FdoGetData(Device);
	struct mlx4_dev *mdev	= p_fdo->pci_dev.dev;
	int n_roce_ports = mlx4_count_roce_ports( mdev );
	int n_eth_ports = mlx4_count_eth_ports( mdev );
	
	MLX4_ENTER(MLX4_DBG_DRV);
    
	if ( p_fdo->children_created )
		goto end;

	// create IB  devices
	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("__do_static_enumeration: calling __create_ib_devices\n"));
	status = __create_ib_devices( p_fdo );
	if (!NT_SUCCESS(status)) 
		goto end;

	// (for RoCE) - need to wait till IBAL starts
	if ( n_roce_ports ) {
		WriteEventLogEntryData( p_fdo->pci_dev.p_self_do, (ULONG)EVENT_MLX4_WARN_DEFERRED_ROCE_START, 0, 0, 2,
			L"%s", p_fdo->pci_dev.location, L"%d", n_roce_ports );
		goto end;
	}
	
	if ( n_eth_ports )
	{
		// create ETH  devices
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("__do_static_enumeration: %d Eth ports - calling __create_eth_devices\n", n_eth_ports));
		status = __create_eth_devices( p_fdo );
	}	

	p_fdo->children_created = TRUE;
	
end:
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}


// This function gets a string name in the format "1,0" and sets the corosponding feature
NTSTATUS 
__read_dev_string(
    struct pci_dev *pdev,
    WDFKEY hParamsKey,
    PCUNICODE_STRING StringName,
    u8*EnableFeature
    ) 
{
	
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING uvalue;	  
#define  MAX_UVALUE 100
	WCHAR uvalue_data[MAX_UVALUE]={0};
	uvalue.Buffer = uvalue_data;
	uvalue.MaximumLength = MAX_UVALUE;
	uvalue.Length = 0;

	status = WdfRegistryQueryUnicodeString(hParamsKey, StringName, NULL, &uvalue);
	if (NT_SUCCESS (status)) {
		if (!wcscmp(uvalue_data, L"0,0")) {
			EnableFeature[0] = 0;
			EnableFeature[1] = 0;
		} else
		if (!wcscmp(uvalue_data, L"0,1")) {
			EnableFeature[0] = 0;
			EnableFeature[1] = 1;
		} else
		if (!wcscmp(uvalue_data, L"1,0")) {
			EnableFeature[0] = 1;
			EnableFeature[1] = 0;
		} else
		if (!wcscmp(uvalue_data, L"1,1")) {
			EnableFeature[0] = 1;
			EnableFeature[1] = 1;
		} else {
			MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,(" Invalid value, data = %S\n", uvalue_data));
			WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_INVALID_PORT_TYPE_VALUE, 0, 0, 1,
				L"%s",uvalue_data); 		
		}
	}
	else
	{// default values
		EnableFeature[0] = 0;
		EnableFeature[1] = 0;
	}
	return status;
}

NTSTATUS 
__read_port_type_string(
    struct pci_dev *pdev,
    WDFKEY hParamsKey,
    PCUNICODE_STRING StringName,
    enum mlx4_port_type* stype
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    UNICODE_STRING uvalue;    
#define  MAX_UVALUE 100
    WCHAR uvalue_data[MAX_UVALUE]={0};
    uvalue.Buffer = uvalue_data;
    uvalue.MaximumLength = MAX_UVALUE;
    uvalue.Length = 0;

    status = WdfRegistryQueryUnicodeString(hParamsKey, StringName, NULL, &uvalue);
    if(status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        //
        // port type was not found set port types to none
        //        
        stype[0] = MLX4_PORT_TYPE_NONE;
        stype[1] = MLX4_PORT_TYPE_NONE;
        return STATUS_SUCCESS;
    }
    
    if(!NT_SUCCESS (status))
    {        
        MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_DEV ,("WdfRegistryQueryUnicodeString(PortType) Failed status = 0x%x\n", status));
        return status;
    }
   
    if (!wcscmp(uvalue_data, L"ib,ib")) 
    {
        stype[0] = MLX4_PORT_TYPE_IB;
        stype[1] = MLX4_PORT_TYPE_IB;
    } 
    else if (!wcscmp(uvalue_data, L"ib,eth")) 
    {
        stype[0] = MLX4_PORT_TYPE_IB;
        stype[1] = MLX4_PORT_TYPE_ETH;
    } 
    else if (!wcscmp(uvalue_data, L"eth,ib")) 
    {
        stype[0] = MLX4_PORT_TYPE_ETH;
        stype[1] = MLX4_PORT_TYPE_IB;
    } 
    else if (!wcscmp(uvalue_data, L"eth,eth")) 
    {
        stype[0] = MLX4_PORT_TYPE_ETH;
        stype[1] = MLX4_PORT_TYPE_ETH;
    } 
    else if (!wcscmp(uvalue_data, L"auto,auto")) 
    {
        stype[0] = MLX4_PORT_TYPE_AUTO;
        stype[1] = MLX4_PORT_TYPE_AUTO;
    } 
    else if (!wcscmp(uvalue_data, L"auto,ib")) 
    {
        stype[0] = MLX4_PORT_TYPE_AUTO;
        stype[1] = MLX4_PORT_TYPE_IB;
    } 
    else if (!wcscmp(uvalue_data, L"ib,auto")) 
    {
        stype[0] = MLX4_PORT_TYPE_IB;
        stype[1] = MLX4_PORT_TYPE_AUTO;
    } 
    else if (!wcscmp(uvalue_data, L"auto,eth")) 
    {
        stype[0] = MLX4_PORT_TYPE_AUTO;
        stype[1] = MLX4_PORT_TYPE_ETH;
    } 
    else if (!wcscmp(uvalue_data, L"eth,auto")) 
    {
        stype[0] = MLX4_PORT_TYPE_ETH;
        stype[1] = MLX4_PORT_TYPE_AUTO;
    } 
    //Case we have single port.
    else if(!wcscmp(uvalue_data, L"eth"))
    {
        stype[0] = MLX4_PORT_TYPE_ETH;
    }
    else if(!wcscmp(uvalue_data, L"ib"))
    {
        stype[0] = MLX4_PORT_TYPE_IB;
    }
    else if(!wcscmp(uvalue_data, L"auto"))
    {
        stype[0] = MLX4_PORT_TYPE_AUTO;        
    }
    else
    {
        MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,("%s: Invalid value, PortType = %S\n", pdev->name, uvalue_data));			
        WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_INVALID_PORT_TYPE_VALUE, 0, 0, 1,
        L"%s",uvalue_data);
        status = STATUS_INVALID_PARAMETER;
    }

    return status;        
}


NTSTATUS
__read_dev_params(IN WDFDEVICE	Device, struct mlx4_dev_params *dev_params)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY hKey = NULL;
	WDFKEY hParamsKey = NULL;
	PFDO_DEVICE_DATA p_fdo	= FdoGetData(Device);
	struct pci_dev *pdev = &p_fdo->pci_dev;
	DECLARE_CONST_UNICODE_STRING(Parameters, L"Parameters");
	DECLARE_CONST_UNICODE_STRING(PortType, L"PortType");
	DECLARE_CONST_UNICODE_STRING(EnableFCoX, L"EnableFCoX");
	DECLARE_CONST_UNICODE_STRING(EnableRoce, L"EnableRoce");


	// default values
	memset(dev_params, 0, sizeof(*dev_params));
	dev_params->mod_port_type[0] = MLX4_PORT_TYPE_AUTO;
	dev_params->mod_port_type[1] = MLX4_PORT_TYPE_AUTO;

	status = WdfDeviceOpenRegistryKey(Device, PLUGPLAY_REGKEY_DRIVER, 
		STANDARD_RIGHTS_ALL, WDF_NO_OBJECT_ATTRIBUTES, &hKey);
    if( !NT_SUCCESS( status ) ) 
    {
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,
			("WdfDeviceOpenRegistryKey(\\Registry\\Machine\\Control\\Class\\...) Failed status = 0x%x\n", status));
		if(status == STATUS_OBJECT_NAME_NOT_FOUND ) 
        {		
            //
			// This is the case of iSCSI install, 
			// we need to continue going up according to mlx4_bys\services registry
			// According to PortType1 and PortType2 parameters
			//
			dev_params->mod_port_type[0] = (mlx4_port_type)g.PortType1;
			dev_params->mod_port_type[1] = (mlx4_port_type)g.PortType2;
			status = STATUS_SUCCESS;
			goto err;
		}
		WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_REG_OPEN_DEV_KEY, 0, 0, 1,
			L"%#x", status );
		goto err;
	}

	status = WdfRegistryOpenKey(hKey, &Parameters, STANDARD_RIGHTS_ALL, WDF_NO_OBJECT_ATTRIBUTES, &hParamsKey);
	if( !NT_SUCCESS( status ) ) 
    {
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,("WdfRegistryOpenKey(Prameters) Failed status = 0x%x\n", status));
		WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_REG_ACTION, 0, 0, 3,
			L"%s", L"WdfRegistryOpenKey", L"%s", Parameters.Buffer, L"%#x", status );
		goto err;
	}
    
    status = __read_port_type_string(pdev, hParamsKey, &PortType, dev_params->mod_port_type);    
	if(!NT_SUCCESS(status)) 
	{
        goto err;
	}
    
	__read_dev_string(pdev, hParamsKey, &EnableFCoX, &dev_params->enable_fcox[0] );
	// status ignored by design
	
	__read_dev_string(pdev, hParamsKey, &EnableRoce, &dev_params->enable_roce[1] );
	// status ignored by design


err:
	if (hKey != NULL) 
		WdfRegistryClose(hKey);

	if (hParamsKey != NULL) 
		WdfRegistryClose(hParamsKey);

	return status;
}

static 
void InitInterface(PFDO_DEVICE_DATA p_fdo)
{
	//
	// prepare MLX4 IB interface
	//

	// fill the header
	p_fdo->bus_ib_ifc.i.Size = sizeof(MLX4_BUS_IB_INTERFACE);
	p_fdo->bus_ib_ifc.i.Version = MLX4_BUS_IB_INTERFACE_VERSION;

	p_fdo->bus_ib_ifc.pdev = &p_fdo->pci_dev;
	p_fdo->bus_ib_ifc.p_ibdev = p_fdo->pci_dev.ib_dev;
	p_fdo->bus_ib_ifc.pmlx4_dev = to_mdev(p_fdo->pci_dev.ib_dev)->dev;
	if ( mlx4_is_msi(p_fdo->bus_ib_ifc.pmlx4_dev) )
		p_fdo->bus_ib_ifc.n_msi_vectors = 
			mlx4_priv( p_fdo->pci_dev.dev )->eq_table.num_eth_eqs;

	//
	// prepare MLX4 FC interface
	//
	p_fdo->bus_fc_if.i.Size = sizeof(MLX4_FC_BUS_IF);
	p_fdo->bus_fc_if.i.Version = MLX4_BUS_FC_INTERFACE_VERSION;

	p_fdo->bus_fc_if.pdev = &p_fdo->pci_dev;
	p_fdo->bus_fc_if.pmlx4_dev = to_mdev(p_fdo->pci_dev.ib_dev)->dev;
}

void __write_cap_flag_to_registry(WDFDEVICE  Device)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY hKey = NULL;
	WDFKEY hParamsKey = NULL;
	PFDO_DEVICE_DATA p_fdo	= FdoGetData(Device);
	struct pci_dev *pdev = &p_fdo->pci_dev;    
	DECLARE_CONST_UNICODE_STRING(Parameters, L"Parameters");
	DECLARE_CONST_UNICODE_STRING(CapabilityFlag, L"CapabilityFlag");
		
	status = WdfDeviceOpenRegistryKey(Device,
									  PLUGPLAY_REGKEY_DRIVER, 
									  STANDARD_RIGHTS_ALL,
									  WDF_NO_OBJECT_ATTRIBUTES,
									  &hKey
									  );
	if(!NT_SUCCESS(status)) 
	{
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,
			("WdfDeviceOpenRegistryKey(\\Registry\\Machine\\Control\\Class\\...) Failed status = 0x%x\n", status));
		WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_REG_OPEN_DEV_KEY, 0, 0, 1,
			L"%#x", status );
		goto err;
	}

	status = WdfRegistryOpenKey(hKey,
								&Parameters,
								STANDARD_RIGHTS_ALL,
								WDF_NO_OBJECT_ATTRIBUTES,
								&hParamsKey
								);
	if(!NT_SUCCESS(status)) 
	{
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,("WdfRegistryOpenKey(Prameters) Failed status = 0x%x\n", status));
		WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_REG_ACTION, 0, 0, 3,
			L"%s", L"WdfRegistryOpenKey", L"%s", Parameters.Buffer, L"%#x", status );
		goto err;
	}
	
	status = WdfRegistryAssignValue(hParamsKey,
									&CapabilityFlag,
									REG_DWORD,
									sizeof(pdev->dev->capability_flag),
									&pdev->dev->capability_flag.u.Value
									);
	if(!NT_SUCCESS(status)) 
	{
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,("WdfRegistryAssignValue(CapabilityFlag) Failed status = 0x%x\n", status));
		WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_REG_ACTION, 0, 0, 3,
			L"%s", L"WdfRegistryAssignValue", L"%s", CapabilityFlag.Buffer, L"%#x", status );
		goto err;
	}
	
err:	
	if (hKey != NULL) 
		WdfRegistryClose(hKey);

	if (hParamsKey != NULL) 
		WdfRegistryClose(hParamsKey);
}

static void
__ref_hca_ifc(
	IN				PVOID				Context )
{
	PFDO_DEVICE_DATA p_fdo = (PFDO_DEVICE_DATA)Context;

	cl_atomic_inc( &p_fdo->bus_hca_ifc_ref );
	cl_atomic_inc( &p_fdo->n_ifc_ref );
	ObReferenceObject( p_fdo->p_dev_obj );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Acquired interface 'GUID_RDMA_INTERFACE_VERBS', CA_guid %I64x, hca_ifc_ref %d\n",
		p_fdo->pci_dev.name, p_fdo->mlx4_hca_data.hca.guid, p_fdo->bus_hca_ifc_ref) );
}

static void
__deref_hca_ifc(
	IN				PVOID				Context )
{
	PFDO_DEVICE_DATA p_fdo = (PFDO_DEVICE_DATA)Context;
	int ref_cnt;

	cl_atomic_dec( &p_fdo->bus_hca_ifc_ref );
	MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
		("%s: Released interface 'GUID_RDMA_INTERFACE_VERBS', CA_guid %I64x, hca_ifc_ref %d\n",
		p_fdo->pci_dev.name, p_fdo->mlx4_hca_data.hca.guid, p_fdo->bus_hca_ifc_ref) );

	ref_cnt = cl_atomic_dec( &p_fdo->n_ifc_ref );
	ObDereferenceObject( p_fdo->p_dev_obj );
	if ( ref_cnt <= 0 )
		KeSetEvent(&p_fdo->exit_event, 0, FALSE);
}


static 
NTSTATUS
__start_card(
	IN WDFDEVICE  Device,
	IN PFDO_DEVICE_DATA p_fdo 
	)
{
	int err;
	NTSTATUS status = STATUS_SUCCESS;
	struct pci_dev *pdev = &p_fdo->pci_dev;
	struct mlx4_dev_params			dev_params;
	int number_of_ib_ports;

	MLX4_ENTER(MLX4_DBG_DRV);

	if ( p_fdo->card_started )
		goto err; 

	status = __read_dev_params(Device, &dev_params);
	if( !NT_SUCCESS( status ) ) {		
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,
			("__read_dev_params Failed status = 0x%x\n", status));
		goto err;
	}

	// enable the card
	status = pci_hca_enable( &pdev->bus_pci_ifc, &pdev->pci_cfg_space );
	if( !NT_SUCCESS( status ) ) {
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,
			("pci_hca_enable Failed status = 0x%x\n", status));
		goto err;
	}

	//
	// init the card
	//

#ifndef USE_WDM_INTERRUPTS
	// enable interrupts for start up
	for ( i = 0; i < MLX4_MAX_INTERRUPTS; ++i ) 
		WdfInterruptEnable(p_fdo->interrupt[i].WdfInterrupt);
#endif	

	// NET library
	err = mlx4_init_one( &p_fdo->pci_dev, &dev_params );
	if (err) {
		status = errno_to_ntstatus(err);		
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,
			("mlx4_init_one Failed status = 0x%x\n", status));
		goto err;
	}

    __write_cap_flag_to_registry(Device);

#ifndef USE_WDM_INTERRUPTS
	//
	// complete filling interrupt context (for more efficiency)
	//
	for ( i = 0; i < MLX4_MAX_INTERRUPTS; ++i ) {
		struct mlx4_priv *priv = mlx4_priv( p_fdo->pci_dev.dev );
		PINTERRUPT_DATA p_isr_ctx = WdfObjectGetTypedContext( 
			p_fdo->interrupt[i].WdfInterrupt, INTERRUPT_DATA );

		p_isr_ctx->eq = &priv->eq_table.eq[i];
	}
#endif

	InitInterface(p_fdo);

	number_of_ib_ports = mlx4_count_ib_ports(p_fdo->pci_dev.dev);
	status = mlx4_hca_start(
		&p_fdo->mlx4_hca_data,	// MLX4_HCA.LIB context
		&pdev->bus_pci_ifc, 	// PCI interface
		&p_fdo->bus_ib_ifc
		);

	if( !NT_SUCCESS( status ) ) {
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,
			("pci_hca_enable Failed status = 0x%x\n", status));
		goto err;
	}

	if ( number_of_ib_ports ) {
		RDMA_INTERFACE_VERBS rdma_ifc, *p_ifc = &rdma_ifc;
		WDF_QUERY_INTERFACE_CONFIG qiConfig;

		/* Allocate and populate our HCA interface structure. */

		/* fill interface fields */
		p_ifc->InterfaceHeader.Size = sizeof(RDMA_INTERFACE_VERBS);
		p_ifc->InterfaceHeader.Version = VerbsVersion(VERBS_MAJOR_VER, VERBS_MINOR_VER);
		p_ifc->InterfaceHeader.Context = p_fdo;
		p_ifc->InterfaceHeader.InterfaceReference = __ref_hca_ifc;
		p_ifc->InterfaceHeader.InterfaceDereference = __deref_hca_ifc;
        mlx4_setup_hca_ifc(&p_fdo->mlx4_hca_data, &p_ifc->Verbs);
		p_ifc->post_event = st_et_event_func;

		/* create an upper interface */
		WDF_QUERY_INTERFACE_CONFIG_INIT( &qiConfig, (PINTERFACE)p_ifc,
			&GUID_RDMA_INTERFACE_VERBS, NULL);

		status = WdfDeviceAddQueryInterface( Device, &qiConfig );
		if (!NT_SUCCESS(status)) {
			MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV  ,("WdfDeviceAddQueryInterface for GUID_RDMA_INTERFACE_VERBS failed %#x\n", status));
			goto err;
		}
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV	,("Created GUID_RDMA_INTERFACE_VERBS interface  for %d IB ports\n", number_of_ib_ports));
	}
	else {
		MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_DEV	,("Skipped creating GUID_RDMA_INTERFACE_VERBS interface - no IB ports\n"));
	}
	
	p_fdo->card_started = TRUE;

err:
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}

static
void
__stop_card(
	IN PFDO_DEVICE_DATA p_fdo
	)
{
	if ( p_fdo->card_started ) {
		p_fdo->card_started = FALSE;
		mlx4_hca_stop( &p_fdo->mlx4_hca_data );
		mlx4_remove_one( &p_fdo->pci_dev);
	}
}


static WCHAR* __port_type_enum_to_str(enum mlx4_port_type port_type)
{
    if(port_type == MLX4_PORT_TYPE_IB)
    {
        return L"IB";
    }
    
    if(port_type == MLX4_PORT_TYPE_ETH)
    {
        return L"ETH";
    }

    if(port_type == MLX4_PORT_TYPE_AUTO)
    {
        return L"AUTO";
    }

    return L"none";
}


NTSTATUS
EvtDeviceD0Entry(
	IN WDFDEVICE  Device,
	IN WDF_POWER_DEVICE_STATE  PreviousState
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	PFDO_DEVICE_DATA p_fdo	= FdoGetData(Device);
	struct pci_dev *pdev	= &p_fdo->pci_dev;
	struct mlx4_dev *mdev;
	struct ib_device_attr props;
	struct ib_device *p_ibdev;

	MLX4_ENTER(MLX4_DBG_DRV);

	MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV, ("PreviousState 0x%x\n", PreviousState));

	st_et_event("Bus: EvtDeviceD0Entry started \n");

	// start card (needed after Hibernation or standby)
	if ( !pdev->start_event_taken )
		KeWaitForSingleObject(&g.start_event, Executive, KernelMode, FALSE, NULL);
	if (PreviousState > WdfPowerDeviceD0)
		status = __start_card( Device, p_fdo );
	pdev->start_event_taken = FALSE;
	KeSetEvent(&g.start_event, 0, FALSE);
	if ( !NT_SUCCESS( status ) ) 
		goto err;
	mdev = pdev->dev;

	// create child device
	if ( !(g.mode_flags & MLX4_MODE_SKIP_CHILDS) ) {
		status = __do_static_enumeration(Device);
			if (!NT_SUCCESS(status)) {
				MLX4_PRINT_EV(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("%s: __do_static_enumeration failed with 0x%x\n", 
					mdev->pdev->name, status));
				goto err;
			}
	}

	// Log Success Message
	MLX4_PRINT(TRACE_LEVEL_INFORMATION ,MLX4_DBG_DRV ,
		("Ven %x Dev %d Fw %d.%d.%d\n", 
		(unsigned)pdev->ven_id, (unsigned)pdev->dev_id,
		(int) (mdev->caps.fw_ver >> 32),
		(int) (mdev->caps.fw_ver >> 16) & 0xffff, 
		(int) (mdev->caps.fw_ver & 0xffff)
		));
	memset ( &props, 0, sizeof( props) );
	p_ibdev = pdev->ib_dev;
	(p_ibdev->query_device)( p_ibdev, &props );
    if (!mlx4_is_mfunc(mdev) || mlx4_is_master(mdev))
    {
    	WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_INFO_DEV_STARTED, 0, 0, 16,
    		L"%S", pdev->name, 
    		L"%04x", (ULONG)pdev->ven_id, 
    		L"%04x", (ULONG)pdev->dev_id,
    		L"%04x", (ULONG)pdev->sub_vendor_id,
    		L"%04x", (ULONG)pdev->sub_system_id,
    		L"%02x", (ULONG)pdev->revision_id,
    		L"%d", (int) (mdev->caps.fw_ver >> 32),
    		L"%d", (int) (mdev->caps.fw_ver >> 16) & 0xffff, 
    		L"%d", (int) (mdev->caps.fw_ver & 0xffff),
    		L"%08x", be32_to_cpu(*(PULONG)((PUCHAR)&p_ibdev->node_guid + 0)), 
    		L"%08x", be32_to_cpu(*(PULONG)((PUCHAR)&p_ibdev->node_guid + 4)), 
    		L"%d", mdev->caps.num_ports,
    		L"%s", __port_type_enum_to_str(mdev->dev_params.mod_port_type[0]),
    		L"%s", __port_type_enum_to_str(mdev->dev_params.mod_port_type[1]),
    		L"%s", __port_type_enum_to_str(mdev->caps.port_type_final[1]),
    		L"%s", __port_type_enum_to_str(mdev->caps.port_type_final[2])
    		); 
    }
err:
	st_et_event("Bus: EvtDeviceD0Entry ended with status %#x\n", status);
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}

static void 
__wait_for_clients_to_exit(PFDO_DEVICE_DATA p_fdo)
{
	LARGE_INTEGER  *p_timemout = NULL;
	NTSTATUS status;
	int i=0;

#if defined(_DEBUG_)
	LARGE_INTEGER  timeout;
	
	timeout.QuadPart = -10000000;	/* 1 sec */
	p_timemout = &timeout;
#endif

	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
		("mlx4_bus: Waiting for clients to exit: bus_ib_ifc_ref %d, bus_not_ifc_ref %d, bus_loc_ifc_ref %d, bus_fc_ifc_ref %d, n_hca_ifc_ref %d\n", 
		p_fdo->bus_ib_ifc_ref, p_fdo->bus_not_ifc_ref, p_fdo->bus_loc_ifc_ref, p_fdo->bus_fc_ifc_ref, p_fdo->mlx4_hca_data.n_hca_ifc_ref));

	while (p_fdo->n_ifc_ref > 0)
	{
		status = KeWaitForSingleObject(&p_fdo->exit_event, Executive, KernelMode, FALSE, p_timemout);
		if ( status == STATUS_SUCCESS ) {
			if ( p_fdo->n_ifc_ref <= 0 )
				break;
			else {
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_PNP, ("Unexpected end of wait: n_ifc_ref %d \n", p_fdo->n_ifc_ref));
				continue;
			}
		}
		if ( status != STATUS_TIMEOUT )
			continue;
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
			("Waiting for clients to exit (%d sec): bus_ib_ifc_ref %d, bus_not_ifc_ref %d, bus_loc_ifc_ref %d, bus_fc_ifc_ref %d, n_hca_ifc_ref %d\n", 
			++i, p_fdo->bus_ib_ifc_ref, p_fdo->bus_not_ifc_ref, p_fdo->bus_loc_ifc_ref, p_fdo->bus_fc_ifc_ref, p_fdo->mlx4_hca_data.n_hca_ifc_ref));
	}
	ASSERT(p_fdo->bus_ib_ifc_ref <= 0 && p_fdo->bus_not_ifc_ref <= 0 && p_fdo->bus_loc_ifc_ref <= 0 && p_fdo->bus_fc_ifc_ref <= 0);
}

NTSTATUS
EvtDeviceD0Exit(
	IN WDFDEVICE  Device,
	IN WDF_POWER_DEVICE_STATE  TargetState
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	PFDO_DEVICE_DATA p_fdo	= FdoGetData(Device);
	
	MLX4_ENTER(MLX4_DBG_DRV);
	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("TargetState 0x%x\n", TargetState));

	st_et_event("Bus: EvtDeviceD0Exit started with TargetState %d\n", TargetState);

	switch (TargetState) {
	case WdfPowerDeviceD1:	/* hopefully, it is STANDBY state */
	case WdfPowerDeviceD2:	/* hopefully, it is STANDBY state */
	case WdfPowerDeviceD3:	/* hopefully, it is STANDBY state */
	case WdfPowerDevicePrepareForHibernation:
		status = STATUS_SUCCESS;
		break;
	default:
		if ( p_fdo->n_ifc_ref ) {
			MLX4_PRINT( TRACE_LEVEL_WARNING, MLX4_DBG_PNP, 
				("%s: Power Down - some interfaces are not released: ib %d, not %d, loc %d, fc %d \n",
				p_fdo->pci_dev.name, p_fdo->bus_ib_ifc_ref, 
				p_fdo->bus_not_ifc_ref, p_fdo->bus_loc_ifc_ref, p_fdo->bus_fc_ifc_ref) );
//			ASSERT(0);
		}
		status = STATUS_SUCCESS;
		break;
	}

	// this if if - only because we skip close on shutdown
	if (TargetState != WdfPowerDeviceD3Final) 
		__wait_for_clients_to_exit(p_fdo);

	if (TargetState != WdfPowerDeviceD3Final)
		__stop_card( p_fdo );

	st_et_event("Bus: EvtDeviceD0Exit ended with status %#x\n", status);
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}


char *GetNotificationTypeString(WDF_SPECIAL_FILE_TYPE NotificationType)
{
	switch(NotificationType) {
		case WdfSpecialFilePaging : return "WdfSpecialFilePaging";
		case WdfSpecialFileHibernation : return "WdfSpecialFileHibernation";
		case WdfSpecialFileDump : return "WdfSpecialFileDump";
		default:
			return "Unknown type";
	}

}
#ifdef NTDDI_WIN8
EVT_WDF_DEVICE_USAGE_NOTIFICATION EvtDeviceUsageNotification;
#endif
VOID
EvtDeviceUsageNotification (
	IN WDFDEVICE  Device,
	IN WDF_SPECIAL_FILE_TYPE NotificationType,
	IN BOOLEAN	IsInNotificationPath
	)
{
	UNUSED_PARAM(Device);
	
	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
		("EvtDeviceUsageNotification called NotificationType = %s ,IsInNotificationPath=%s \n",
		GetNotificationTypeString(NotificationType), 
		IsInNotificationPath ? "true" : "false"));
	// BUGBUG: Tzachid 27/4/2010 Need to implment this functions as part of the iSCSI install.
	// For more details see IRP_MN_DEVICE_USAGE_NOTIFICATION in the wdk

}

/* Forwards the request to the HCA's PDO. */
static 
void
__put_bus_ifc(
		IN	BUS_INTERFACE_STANDARD		*pBusIfc )
{
	MLX4_ENTER(MLX4_DBG_DRV);
	MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV, ("pBusIfc=0x%p\n", pBusIfc));
	pBusIfc->InterfaceDereference( pBusIfc->Context );
	MLX4_EXIT( MLX4_DBG_DRV );
}

static 
NTSTATUS
__get_bus_ifc(
	IN				PFDO_DEVICE_DATA const	p_fdo,
	IN		const	GUID* const				pGuid,
	OUT			BUS_INTERFACE_STANDARD	*pBusIfc )
{
	NTSTATUS status;
	WDFDEVICE  FdoDevice = p_fdo->FdoDevice;
	MLX4_ENTER(MLX4_DBG_DRV);
	
	status = WdfFdoQueryForInterface( FdoDevice, pGuid, (PINTERFACE)pBusIfc,
		sizeof(BUS_INTERFACE_STANDARD), 1, NULL );
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}

static
void
__put_dma_adapter(
	IN PFDO_DEVICE_DATA p_fdo,
	IN PDMA_ADAPTER 	p_dma )
{
	UNUSED_PARAM(p_fdo);
	UNUSED_PARAM(p_dma);
	MLX4_ENTER(MLX4_DBG_DRV);
	MLX4_EXIT( MLX4_DBG_DRV );
}


// this routine releases the resources, taken in __get_resources
static
void 
__put_resources(
	IN PFDO_DEVICE_DATA p_fdo
	)
{
	struct pci_dev *pdev = &p_fdo->pci_dev;

	MLX4_ENTER(MLX4_DBG_DRV);

	if (pdev->msix_info.valid) 
		pci_free_msix_info_resources(&pdev->msix_info);

	if (p_fdo->dma_adapter_taken) {
		p_fdo->dma_adapter_taken = FALSE;
		__put_dma_adapter( p_fdo, pdev->p_dma_adapter );
	}

	if (p_fdo->pci_bus_ifc_taken) {
		p_fdo->pci_bus_ifc_taken = FALSE;
		__put_bus_ifc(&pdev->bus_pci_ifc);
	}

	if (pdev->p_msix_map)
		kfree(pdev->p_msix_map);

	
	MLX4_EXIT( MLX4_DBG_DRV );
}

static
NTSTATUS 
__get_dma_adapter(
	IN PFDO_DEVICE_DATA p_fdo,
	OUT PDMA_ADAPTER *	pp_dma )
{
	NTSTATUS status;
	WDF_DMA_ENABLER_CONFIG	dmaConfig;
	
	MLX4_ENTER(MLX4_DBG_DRV);

	WDF_DMA_ENABLER_CONFIG_INIT( &dmaConfig,
		WdfDmaProfileScatterGather64, 0x80000000 - 1 );

	status = WdfDmaEnablerCreate( p_fdo->FdoDevice,
		&dmaConfig, WDF_NO_OBJECT_ATTRIBUTES, &p_fdo->dma_enabler );
	if (!NT_SUCCESS (status)) {
		return status;
	}
	
	*pp_dma = WdfDmaEnablerWdmGetDmaAdapter( 
		p_fdo->dma_enabler, WdfDmaDirectionReadFromDevice );

	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}

// this routine fills pci_dev structure, containing all HW 
// and some other necessary common resources
static
NTSTATUS 
__get_resources(
	IN PFDO_DEVICE_DATA p_fdo,
	IN WDFCMRESLIST  ResourcesRaw,
	IN WDFCMRESLIST  ResourcesTranslated
	)
{
	NTSTATUS status;
	ULONG i, k=0;
	BUS_INTERFACE_STANDARD	bus_pci_ifc;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR  desc;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR  desc_raw;
	struct pci_dev *pdev = &p_fdo->pci_dev;

	MLX4_ENTER(MLX4_DBG_DRV);

	//
	// Get PCI BUS interface
	// 
	status = __get_bus_ifc( p_fdo, &GUID_BUS_INTERFACE_STANDARD, &bus_pci_ifc );
	if( !NT_SUCCESS( status ) ) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
			("failed: status=0x%x\n", status));
		goto err;
	}
	RtlCopyMemory( &pdev->bus_pci_ifc, &bus_pci_ifc, sizeof(BUS_INTERFACE_STANDARD) );
	p_fdo->pci_bus_ifc_taken = TRUE;

	// 
	// get HW resources
	//
	pdev->n_msi_vectors_alloc = 0;
	for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); i++) {

		desc = WdfCmResourceListGetDescriptor( ResourcesTranslated, i );
		desc_raw = WdfCmResourceListGetDescriptor( ResourcesRaw, i );

		switch (desc->Type) {

			case CmResourceTypeMemory:
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
					("EvtPrepareHardware(Raw): Desc %d: Memory: Start %#I64x, Length %#x\n", 
					i, desc_raw->u.Memory.Start.QuadPart, desc_raw->u.Memory.Length ));
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
					("EvtPrepareHardware: Desc %d: Memory: Start %#I64x, Length %#x\n", 
					i, desc->u.Memory.Start.QuadPart, desc->u.Memory.Length ));

				if (k < N_BARS) {
					pdev->bar[k].phys = desc->u.Memory.Start.QuadPart;
					pdev->bar[k].size = (SIZE_T)desc->u.Memory.Length;
				}
				k++;
				break;

#ifdef USE_WDM_INTERRUPTS
			case CmResourceTypeInterrupt:
				if (!pdev->n_msi_vectors_alloc) 
					pdev->int_info = *desc;
				if (desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
					pdev->n_msi_vectors_alloc += desc_raw->u.MessageInterrupt.Raw.MessageCount;
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("EvtPrepareHardware: Desc %d: MsiInterrupt: Share %d, Flags %#x, Level %d, Vector %#x, Affinity %#x\n", 
						i, desc->ShareDisposition, desc->Flags,
						desc->u.MessageInterrupt.Translated.Level, 
						desc->u.MessageInterrupt.Translated.Vector, 
						(u32)desc->u.MessageInterrupt.Translated.Affinity ));
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("EvtPrepareHardware: Desc %d: RawMsiInterrupt: Share %d, Flags %#x, MessageCount %#hx, Vector %#x, Affinity %#x\n", 
						i, desc_raw->ShareDisposition, desc_raw->Flags,
						desc_raw->u.MessageInterrupt.Raw.MessageCount, 
						desc_raw->u.MessageInterrupt.Raw.Vector,
						(u32)desc_raw->u.MessageInterrupt.Raw.Affinity ));
				}
				else { // line-based interrupt
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("EvtPrepareHardware: Desc %d: LineInterrupt: Share %d, Flags %#x, Level %d, Vector %#x, Affinity %#x\n", 
						i, desc->ShareDisposition, desc->Flags,
						desc->u.Interrupt.Level, desc->u.Interrupt.Vector, 
						(u32)desc->u.Interrupt.Affinity ));
				}
				break;
#endif

			default:
				//
				// Ignore all other descriptors.
				//
				break;
		}
	}
	if (i ==0) {
		// This means that no resources are found
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("WdfCmResourceListGetCount: returned 0, quiting\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	//
	// get uplink info. 
	//
	status = pci_save_config( &bus_pci_ifc, &pdev->pci_cfg_space );
	if( !NT_SUCCESS( status ) )
	{
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
			("Failed to save HCA config: status=0x%x\n", status));
		goto err;
	}
	pci_get_uplink_info( pdev, &pdev->pci_cfg_space, &pdev->uplink_info );

	//
	// allocate DMA adapter
	//
	status = __get_dma_adapter( p_fdo, &pdev->p_dma_adapter );
	if( !NT_SUCCESS( status ) )
	{
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
			("Failed to get DMA adapter: status=0x%x\n", status));
		goto err;
	}
	p_fdo->dma_adapter_taken = TRUE;

	//
	// allocate MSI-X vector map table
	//
	if ( pdev->n_msi_vectors_alloc )
	{
		pdev->p_msix_map = (msix_map *)kzalloc(sizeof(struct msix_map) * pdev->n_msi_vectors_alloc, GFP_KERNEL);
		if ( !pdev->p_msix_map )
		{
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
				("Failed to allocate MSI-X vector map table\n"));
			goto err;
		}
	}
	
	//
	// fill more fields in pci_dev
	//
	pdev->ven_id = pdev->pci_cfg_space.VendorID;
	pdev->dev_id = pdev->pci_cfg_space.DeviceID;
	pdev->sub_vendor_id = pdev->pci_cfg_space.u.type0.SubVendorID;
	pdev->sub_system_id = pdev->pci_cfg_space.u.type0.SubSystemID;
	pdev->revision_id = pdev->pci_cfg_space.RevisionID;
	pdev->p_self_do = WdfDeviceWdmGetDeviceObject(p_fdo->FdoDevice);
	pdev->pdo = WdfDeviceWdmGetPhysicalDevice(p_fdo->FdoDevice);
	pdev->p_stat->pdo = pdev->pdo;
	
	MLX4_EXIT( MLX4_DBG_DRV );
	return STATUS_SUCCESS;
err:
	__put_resources(p_fdo);
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}

EVT_WDF_DEVICE_FILTER_RESOURCE_REQUIREMENTS  EvtDeviceFilterAddResourceRequirements;
EVT_WDF_DEVICE_REMOVE_ADDED_RESOURCES   EvtDeviceRemoveAddedResources;

NTSTATUS
EvtDeviceRemoveAddedResources (
 IN WDFDEVICE  Device,
 IN WDFCMRESLIST  ResourcesRaw,
 IN WDFCMRESLIST  ResourcesTranslated
 )
{
	ULONG i, n_res_desc;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR desc;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR desc_raw;

	UNREFERENCED_PARAMETER(Device);

	n_res_desc = WdfCmResourceListGetCount(ResourcesTranslated);
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
		("WdfCmResourceListGetCount: there are %d descriptors\n", n_res_desc ));

	for (i = 0; i < n_res_desc; i++) {
#ifdef ADD_VECTORS
		int j;
#endif

		desc = WdfCmResourceListGetDescriptor( ResourcesTranslated, i );
		desc_raw = WdfCmResourceListGetDescriptor( ResourcesRaw, i );

		switch (desc->Type) {

			case CmResourceTypeInterrupt:
				if (desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("Desc %d: MsiInterrupt: Share %d, Flags %#x, Level %d, Vector %#x, Affinity %#I64x\n", 
							i, desc->ShareDisposition, desc->Flags,
							desc->u.MessageInterrupt.Translated.Level, 
							desc->u.MessageInterrupt.Translated.Vector, 
							(u64)desc->u.MessageInterrupt.Translated.Affinity ));
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("Desc %d: RawMsiInterrupt: Share %d, Flags %#x, MessageCount %#hx, Vector %#x, Affinity %#I64x\n", 
						i, desc_raw->ShareDisposition, desc_raw->Flags,
						desc_raw->u.MessageInterrupt.Raw.MessageCount, 
						desc_raw->u.MessageInterrupt.Raw.Vector,
						(u64)desc_raw->u.MessageInterrupt.Raw.Affinity ));
				}
				else { // line-based interrupt
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("Desc %d: LineInterrupt: Share %d, Flags %#x, Level %d, Vector %#x, Affinity %#I64x\n", 
						i, desc->ShareDisposition, desc->Flags,
						desc->u.Interrupt.Level, desc->u.Interrupt.Vector, 
						(u64)desc->u.Interrupt.Affinity ));
				}

#ifdef ADD_VECTORS
				// remove added vectors
				if ( desc->u.MessageInterrupt.Translated.Affinity == g.start_affinity ) {
					for ( j = 0; j < g.msix_num; j++ ) {
						MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
							("WdfCmResourceListRemove - Removing MSI-X vector with index %u\n", i ));
						WdfCmResourceListRemove( ResourcesRaw, i );
						WdfCmResourceListRemove( ResourcesTranslated, i );
				    }
			    	goto end;
				}
#endif				
				break;

			default:
				// Ignore all other descriptors.
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
					("Desc %d: DescType %d\n", i, desc->Type ) );
				break;
		}
	}

#ifdef ADD_VECTORS
end:
#endif				
 return STATUS_SUCCESS;
}


NTSTATUS
EvtDeviceFilterAddResourceRequirements (
 	IN WDFDEVICE  Device,
 	IN WDFIORESREQLIST IoResourceRequirementsList
 )
{
	ULONG i, j, k, n_log_conf, n_res_desc, n_limit;
#ifdef ADD_VECTORS
	IO_RESOURCE_DESCRIPTOR descriptor;
	int n_cfgs = 1; /* number of configurations to patch */
#endif
	int first_msix = -1, last_msix = -1;
	PFDO_DEVICE_DATA p_fdo = FdoGetData(Device);
	struct pci_dev *pdev = &p_fdo->pci_dev;

	char *irq_policy[] = {
		"IrqPolicyMachineDefault",
		"IrqPolicyAllCloseProcessors",
		"IrqPolicyOneCloseProcessor",
		"IrqPolicyAllProcessorsInMachine",
		"IrqPolicySpecifiedProcessors",
		"IrqPolicySpreadMessagesAcrossAllProcessors"
	};
	#define IRQ_POLICY_MAX (sizeof(irq_policy) / sizeof(char*) )
	 char *pri_policy[] = {
		"IrqPriorityUndefined",
		"IrqPriorityLow",
		"IrqPriorityNormal",
		"IrqPriorityHigh" 
	};
	#define PRI_POLICY_MAX (sizeof(pri_policy) / sizeof(char*) )
	char *str;

#ifdef ADD_VECTORS
	// prepare MSI-X resource descriptor
	RtlZeroMemory( &descriptor, sizeof(descriptor) );
	descriptor.Option = 0;
	descriptor.Type = CmResourceTypeInterrupt;
	descriptor.ShareDisposition = CmResourceShareDeviceExclusive;
	descriptor.Flags = CM_RESOURCE_INTERRUPT_LATCHED | CM_RESOURCE_INTERRUPT_MESSAGE | CM_RESOURCE_INTERRUPT_POLICY_INCLUDED;
	descriptor.u.Interrupt.MinimumVector = CM_RESOURCE_INTERRUPT_MESSAGE_TOKEN;
	descriptor.u.Interrupt.MaximumVector = CM_RESOURCE_INTERRUPT_MESSAGE_TOKEN;
	descriptor.u.Interrupt.AffinityPolicy = IrqPolicySpecifiedProcessors;
	descriptor.u.Interrupt.PriorityPolicy = IrqPriorityHigh;
#endif

	//
	// Obtain the number of logical configurations.
	//
	n_log_conf = WdfIoResourceRequirementsListGetCount(IoResourceRequirementsList);
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
		("IoResourceRequirementsList: there are %d logical configurations\n", n_log_conf ));

	//
	// Search each logical configuration.
	//
	for (i = 0; i < n_log_conf; i++) {
		WDFIORESLIST reslist;

		reslist = WdfIoResourceRequirementsListGetIoResList( IoResourceRequirementsList, i );
		if (!reslist) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("WdfIoResourceRequirementsListGetIoResList failed\n" ));
			return STATUS_UNSUCCESSFUL;
		}

		//
		// Get the number of resource descriptors that is in this logical configuration.
		//
		n_res_desc = WdfIoResourceListGetCount(reslist);
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
		 ("WdfIoResourceListGetCount: Log conf %d, descriptors %d\n", i, n_res_desc ));

		for (j = 0; j < n_res_desc; j++) {
			PIO_RESOURCE_DESCRIPTOR desc;

			//
			// Get the next resource descriptor.
			//
			desc = WdfIoResourceListGetDescriptor( reslist, j );
			if (!desc) {
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("WdfIoResourceListGetDescriptor failed\n" ));
				return STATUS_UNSUCCESSFUL;
			}

			// 
			// print HW resources
			//
			switch (desc->Type) {
				case CmResourceTypeMemory:
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("Desc %d: Memory: MinAddr %#I64x, MaxAddr %#I64x, Length %#x, Align %#x\n", 
						j, desc->u.Memory.MinimumAddress.QuadPart, 
						desc->u.Memory.MaximumAddress.QuadPart,
						desc->u.Memory.Length, desc->u.Memory.Alignment ));
					break;

				case CmResourceTypeInterrupt:
					if (desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
						str = "MSI-X";
						if ( first_msix == -1 )
							first_msix = j;
						if ( !i )
							pdev->n_msi_vectors_req++;
					}
					else {  // line-based interrupt
						str = "Legacy";
						if ( first_msix == -1 )
							first_msix = last_msix = j;
						else
							last_msix = j;
						if ( !i )
							pdev->n_msi_vectors_req++;
					}
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("Desc %d: Type %s, Share %d, Flags %#x, AffinityPolicy '%s' (%d), PriorityPolicy '%s' (%d), MinVector %#x, MaxVector %#x, Affinity %#I64x\n", 
						j, str, desc->ShareDisposition, desc->Flags,
						(desc->u.Interrupt.AffinityPolicy >= IRQ_POLICY_MAX) ? "Unknown" : irq_policy[desc->u.Interrupt.AffinityPolicy], 
						desc->u.Interrupt.AffinityPolicy, 
						(desc->u.Interrupt.PriorityPolicy >= PRI_POLICY_MAX) ? "Unknown" : pri_policy[desc->u.Interrupt.PriorityPolicy], 
						desc->u.Interrupt.PriorityPolicy, 
						desc->u.Interrupt.MinimumVector, desc->u.Interrupt.MaximumVector, 
						(u64)desc->u.Interrupt.TargetedProcessors ));
					break;

				default:
					// Ignore all other descriptors.
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,("Desc %d: DescType %d\n", j, desc->Type ) );
					break;
			}
		}

#ifdef ADD_VECTORS
		// try to add vectors to this configuration
		if ( n_cfgs && first_msix != -1 && last_msix != -1 ){
			NTSTATUS  status = STATUS_SUCCESS;
			ULONG index = last_msix;
			  
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
				("WdfIoResourceListInsertDescriptor - Trying to insert %d new MSI-X vectors after %d starting with affinity %#I64x\n", 
				g.msix_num, index, (u64)g.start_affinity ));
			  
			for ( i = 0; (int)i < g.msix_num; i++, index++ ) {
				KAFFINITY aff = g.start_affinity << i;
				descriptor.u.Interrupt.TargetedProcessors = aff;
				status = WdfIoResourceListInsertDescriptor( reslist, &descriptor, index );
				if (!NT_SUCCESS(status)) {
					MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
						("WdfIoResourceListInsertDescriptor failed to add MSI-X vector with affinity %#I64x at ix %u (status %#x)\n", 
						(u64)aff, index, status ));
					return status;
				}
				else {
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
						("WdfIoResourceListInsertDescriptor - Added new MSI-X vector with affinity %#I64x at %u\n", 
						(u64)aff, index ));
				}
			}

			first_msix = last_msix = -1;
			n_cfgs--;
		}
#endif		
	}

	//
	// remove unnecessary resources
	//

	if ( PCI_MULTIFUNCTION_DEVICE(&pdev->pci_cfg_space) ) 
		n_limit = g.max_multi_msix_num;
	else
		n_limit = g.max_single_msix_num;

	if ( !n_limit )
		n_limit = num_possible_cpus() + MLX4_NUM_IB_EQ;
	
	if ( n_limit ) {
	
		WDFIORESLIST reslist;
		
		reslist = WdfIoResourceRequirementsListGetIoResList( IoResourceRequirementsList, 0 );
		if (!reslist) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("WdfIoResourceRequirementsListGetIoResList failed\n" ));
			return STATUS_UNSUCCESSFUL;
		}
		
		//
		// Get the number of resource descriptors that is in this logical configuration.
		//
		n_res_desc = WdfIoResourceListGetCount(reslist);
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
		 ("WdfIoResourceListGetCount: Log conf %d, descriptors %d\n", 0, n_res_desc ));
		
	
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX, 
			("Removing vectors from log conf 0: limit %d, requested %d\n", n_limit, pdev->n_msi_vectors_req ));
	
		for ( i = j = k = 0; i < n_res_desc; i++, j++) {
			PIO_RESOURCE_DESCRIPTOR desc;
		
			//
			// Get the next resource descriptor.
			//
			desc = WdfIoResourceListGetDescriptor( reslist, j );
			if (!desc) {
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("WdfIoResourceListGetDescriptor failed\n" ));
				return STATUS_UNSUCCESSFUL;
			}
		
			// 
			// remove on need
			//
			if ( desc->Type == CmResourceTypeInterrupt ) {
				if (desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
					if ( (int)k++ >= n_limit ) {
						MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX, ("Removed desc %d\n",i));
						WdfIoResourceListRemove( reslist, j );
						j--;	// because the indeces shrink
					}
				}
			}
		}
	}

	return STATUS_SUCCESS;
}



NTSTATUS
EvtPrepareHardware(
	IN WDFDEVICE  Device,
	IN WDFCMRESLIST  ResourcesRaw,
	IN WDFCMRESLIST  ResourcesTranslated
	)
{
	NTSTATUS status;
	PFDO_DEVICE_DATA p_fdo	= FdoGetData(Device);
	struct pci_dev *pdev = &p_fdo->pci_dev;
	struct mlx4_dev *mdev;
	WDFMEMORY  memory;
	WDF_OBJECT_ATTRIBUTES  attributes;
    
	MLX4_ENTER(MLX4_DBG_DRV);
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("EvtPrepareHardware\n"));

	st_et_event("Bus: EvtPrepareHardware started\n");

	KeWaitForSingleObject(&g.start_event, Executive, KernelMode, FALSE, NULL);
	pdev->start_event_taken = TRUE;

	// get resources
	status = __get_resources( p_fdo, ResourcesRaw, ResourcesTranslated );
	if( !NT_SUCCESS( status ) ) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("__get_resources failed: status=0x%x\n", status));
		goto err;
	}

	// get card location
	p_fdo->pci_dev.location[0] = 0;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);    
	attributes.ParentObject = Device;

	
	status = WdfDeviceAllocAndQueryProperty(Device,
											DevicePropertyBusNumber,
											NonPagedPool,
											&attributes,
											&memory
											);	  
	if(NT_SUCCESS(status))
	{
		UCHAR *ptr;
		// get bus number
		ptr = (UCHAR *)WdfMemoryGetBuffer(memory, NULL);
		p_fdo->pci_dev.pci_bus = *(PULONG)ptr;
		WdfObjectDelete(memory);
	}
	else
	{
		WriteEventLogEntryData( p_fdo->pci_dev.p_self_do,
								(ULONG)EVENT_MLX4_ERROR_GET_LOCATION, 0, 0, 1,
								L"%x", status);
	}

	status = WdfDeviceAllocAndQueryProperty(Device,
											DevicePropertyAddress,
											NonPagedPool,
											&attributes,
											&memory
											);
	if(NT_SUCCESS(status)) 
	{
		UCHAR *ptr;
		// get function number
		ptr = (UCHAR *)WdfMemoryGetBuffer(memory, NULL);
		p_fdo->pci_dev.pci_func = *(PULONG)ptr;

		WdfObjectDelete(memory);
	}
	else
	{
		WriteEventLogEntryData( p_fdo->pci_dev.p_self_do,
								(ULONG)EVENT_MLX4_ERROR_GET_LOCATION, 0, 0, 1,
								L"%x", status);
	}
	
	p_fdo->pci_dev.pci_device = 0;

	status = RtlStringCbPrintfW( (LPWSTR)p_fdo->pci_dev.location, 
			sizeof(p_fdo->pci_dev.location), L"PCI bus %d, device %d, function %d",
			p_fdo->pci_dev.pci_bus,p_fdo->pci_dev.pci_device,p_fdo->pci_dev.pci_func );
	ASSERT(NT_SUCCESS( status ));
	
	p_fdo->pci_dev.devfn = (u16)PCI_DEVFN(p_fdo->pci_dev.pci_device, p_fdo->pci_dev.pci_func);
	RtlStringCbPrintfA( p_fdo->pci_dev.name, sizeof(p_fdo->pci_dev.name),
		"MLX4_SF_%d_%d_%d", p_fdo->pci_dev.pci_bus, 
		p_fdo->pci_dev.pci_device, p_fdo->pci_dev.pci_func );

	pdev->p_wdf_device = Device;
	pdev->ib_hca_created = 0;

	// start the card
	status = __start_card(Device, p_fdo );
	if( !NT_SUCCESS( status ) ) 
		goto err;
	mdev = pdev->dev;

	// get VPD - as far as it just info, we proceed the work in case of error
	status = pci_get_vpd( &pdev->bus_pci_ifc, &pdev->pci_cfg_space, &pdev->vpd, &pdev->vpd_size );
	if( !NT_SUCCESS( status ) )
	{
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, 
			("%s: Failed to read VPD from the card (status=0x%x). Continue the work.\n", 
			mdev->pdev->name, status));
		status = 0;
	}

#if WINVER >= 0x0601	
	PrintNumaCpuConfiguration(false, pdev->pdo);
#endif

err:
	st_et_event("Bus: EvtPrepareHardware ended with status %#x\n", status);
	if( !NT_SUCCESS( status ) )
		st_dev_rmv( p_fdo->pci_dev.p_stat );
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}

void fix_bus_ifc(struct pci_dev *pdev)
{
	PFDO_DEVICE_DATA p_fdo;

	p_fdo =  CONTAINING_RECORD(pdev, FDO_DEVICE_DATA, pci_dev);
	p_fdo->bus_ib_ifc.p_ibdev = p_fdo->pci_dev.ib_dev;
	p_fdo->bus_ib_ifc.pmlx4_dev = to_mdev(p_fdo->pci_dev.ib_dev)->dev;

	p_fdo->bus_fc_if.pmlx4_dev = to_mdev(p_fdo->pci_dev.ib_dev)->dev;
}

NTSTATUS
EvtReleaseHardware(
	IN WDFDEVICE  Device,
	IN WDFCMRESLIST  ResourcesTranslated
	)
{
	PFDO_DEVICE_DATA p_fdo	= FdoGetData(Device);
	struct pci_dev *pdev = &p_fdo->pci_dev;

	UNUSED_PARAM(ResourcesTranslated);

	MLX4_ENTER(MLX4_DBG_DRV);
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("EvtReleaseHardware\n"));

	st_et_event("Bus: EvtReleaseHardware started\n");

	if ( pdev->start_event_taken ) {
		pdev->start_event_taken = FALSE;
		KeSetEvent(&g.start_event, 0, FALSE);
	}

	if ( pdev->vpd ) {
		kfree( pdev->vpd );
		pdev->vpd = NULL;
		pdev->vpd_size = 0;
	}
	__stop_card( p_fdo );
	__put_resources( p_fdo );
	st_dev_rmv( p_fdo->pci_dev.p_stat );

	st_et_event("Bus: EvtReleaseHardware ended with status 0\n");
	MLX4_EXIT( MLX4_DBG_DRV );
	return STATUS_SUCCESS;
}

#ifndef USE_WDM_INTERRUPTS

static
NTSTATUS 
__create_interrupt(
	IN WDFDEVICE				device,
	IN int						int_num,
	IN PFN_WDF_INTERRUPT_ISR	isr,
	IN PFN_WDF_INTERRUPT_DPC	dpc,
	IN PFDO_DEVICE_DATA			p_fdo,
	OUT WDFINTERRUPT		*	p_int_obj
	)
{
	NTSTATUS Status;

	WDF_INTERRUPT_CONFIG  interruptConfig;
	WDF_OBJECT_ATTRIBUTES  interruptAttributes;
	PINTERRUPT_DATA p_isr_ctx;

	MLX4_ENTER(MLX4_DBG_DRV);

	WDF_INTERRUPT_CONFIG_INIT( &interruptConfig, isr, dpc );
	
	interruptConfig.EvtInterruptEnable = EvtEnableInterrupt;
	interruptConfig.EvtInterruptDisable = EvtDisableInterrupt;
	
	
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE( 
		&interruptAttributes, INTERRUPT_DATA );

	Status = WdfInterruptCreate( device,
		&interruptConfig, &interruptAttributes, p_int_obj );

	p_isr_ctx = WdfObjectGetTypedContext( *p_int_obj, INTERRUPT_DATA );
	p_isr_ctx->int_num = int_num;
	p_isr_ctx->p_fdo = p_fdo;
	p_isr_ctx->eq = NULL;

	// one can call WdfInterruptSetPolicy() to set the policy, affinity etc

	MLX4_EXIT( MLX4_DBG_DRV );
	return Status;
}

#endif

inline void InitBusIsr(
	 struct VipBusIfc* pVipBusIfc
	)
{
	memset(pVipBusIfc, 0, sizeof(struct VipBusIfc));
	KeInitializeEvent(&pVipBusIfc->NicData.ConfigChangeEvent, SynchronizationEvent, TRUE);
}


NTSTATUS InitWorkerThreads()
{
    NTSTATUS Status = STATUS_SUCCESS;

	g_pWorkerThreads = new WorkerThreads;
    if (g_pWorkerThreads == NULL) 
    {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("new WorkerThreads failed\n"));
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = g_pWorkerThreads->Init();
    if (!NT_SUCCESS(Status)) 
    {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
			("g_pWorkerThreads.Init failed status = 0x%x \n", Status));
        delete g_pWorkerThreads;
    	g_pWorkerThreads = NULL;
    }

Exit:
    return Status;
}

VOID ShutdownWorkerThreads()
{
    if(g_pWorkerThreads != NULL) {
        g_pWorkerThreads->ShutDown();            
        delete g_pWorkerThreads;
        g_pWorkerThreads = NULL;
    }
}

NTSTATUS
EvtDeviceQueryRemove(
	IN WDFDEVICE  Device
	)
{
	PFDO_DEVICE_DATA p_fdo = FdoGetData(Device);
	int usecnt = mlx4_hca_get_usecnt(&p_fdo->mlx4_hca_data);
	
	MLX4_ENTER( MLX4_DBG_PNP );

	if (usecnt) {
		cl_dbg_out( "MLX4_BUS: Can't get unloaded. %d applications are still in work\n", usecnt);
		return STATUS_UNSUCCESSFUL;
	}
	MLX4_EXIT( MLX4_DBG_PNP );
	return STATUS_SUCCESS;
}
 

//#define DONT_START_ON_BOOT

NTSTATUS
EvtDriverDeviceAdd(
	IN WDFDRIVER		Driver,
	IN PWDFDEVICE_INIT	DeviceInit
	)
/*++
Routine Description:

	EvtDriverDeviceAdd is called by the framework in response to AddDevice
	call from the PnP manager. We create and initialize a device object to
	represent a new instance of mxe bus.

Arguments:

	Driver - Handle to a framework driver object created in DriverEntry

	DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

	NTSTATUS

--*/
{
#ifndef USE_WDM_INTERRUPTS
	int i;
#endif
	WDF_OBJECT_ATTRIBUTES			attributes;
	NTSTATUS						status;
	WDFDEVICE						device;
	PFDO_DEVICE_DATA				p_fdo;
	PNP_BUS_INFORMATION				busInfo;
	WDF_PNPPOWER_EVENT_CALLBACKS	Callbacks;
	WDF_DEVICE_PNP_CAPABILITIES		pnpCaps;
	WDF_FDO_EVENT_CALLBACKS			fdoCallbacks;
	WDFMEMORY  memory;
	int bus = 0, dev = 0, function= 0;
	UCHAR *ptr;
	

	UNREFERENCED_PARAMETER(Driver);

	MLX4_ENTER(MLX4_DBG_DRV);
	st_et_event("Bus: EvtDriverDeviceAdd started \n");

#ifdef DONT_START_ON_BOOT
	{
		int    QueryTimeIncrement = KeQueryTimeIncrement();
		LARGE_INTEGER	Ticks;

		KeQueryTickCount(&Ticks);
		if (Ticks.QuadPart * QueryTimeIncrement / 10000 < 30000) // 10,000 moves from 100ns to ms
		{
			return STATUS_NO_MEMORY;
		}
	}
#endif
	//
	// register PnP & Power stuff
	//
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Callbacks);
	Callbacks.EvtDevicePrepareHardware = EvtPrepareHardware;
	Callbacks.EvtDeviceReleaseHardware = EvtReleaseHardware;
	Callbacks.EvtDeviceD0Entry = EvtDeviceD0Entry;
	Callbacks.EvtDeviceD0Exit = EvtDeviceD0Exit;
	Callbacks.EvtDeviceUsageNotification = EvtDeviceUsageNotification;
	Callbacks.EvtDeviceQueryRemove	= EvtDeviceQueryRemove;

	WdfDeviceInitSetPnpPowerEventCallbacks( DeviceInit, &Callbacks );
	
	 
	//
	// add callbacks for filtering device resources
	//
	WDF_FDO_EVENT_CALLBACKS_INIT(&fdoCallbacks);
	fdoCallbacks.EvtDeviceFilterAddResourceRequirements = EvtDeviceFilterAddResourceRequirements;
	fdoCallbacks.EvtDeviceRemoveAddedResources = EvtDeviceRemoveAddedResources;

	WdfFdoInitSetEventCallbacks( DeviceInit, &fdoCallbacks );
	
	
	
	//
	// Initialize all the properties specific to the device.
	// Framework has default values for the one that are not
	// set explicitly here. So please read the doc and make sure
	// you are okay with the defaults.
	//
	WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);
	WdfDeviceInitSetExclusive(DeviceInit, TRUE);

	// 
	// Initialize attributes structure to specify size and accessor function
	// for storing device context.
	//
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FDO_DEVICE_DATA);

	//
	// Create a framework device object. In response to this call, framework
	// creates a WDM deviceobject.
	//
	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
	if (!NT_SUCCESS(status)) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, 
			("WdfDeviceCreate failed with 0x%x\n", status));
		goto end;
	}

	//
	// set PnP capabilities
	//
	WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
	pnpCaps.Removable = WdfTrue;
	pnpCaps.SurpriseRemovalOK = WdfTrue;
	 
	
	WdfDeviceSetPnpCapabilities( device, &pnpCaps );

	//
	// Get the device context.
	//
	p_fdo = FdoGetData(device);
	RtlZeroMemory(p_fdo, sizeof(FDO_DEVICE_DATA));
	p_fdo->FdoDevice = device; 
	p_fdo->p_dev_obj = WdfDeviceWdmGetDeviceObject( device );
	p_fdo->pci_dev.post_event = st_et_event_func;
	KeInitializeEvent(&p_fdo->exit_event, SynchronizationEvent, FALSE);

	//
	// Init the BusIsr data
	//
	InitBusIsr(&p_fdo->mtnic_Ifc);

	//
	// Purpose of this lock is documented in PlugInDevice routine below.
	//
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = device;
	status = WdfWaitLockCreate(&attributes, &p_fdo->ChildLock);
	if (!NT_SUCCESS(status)) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, 
			("WdfWaitLockCreate failed with 0x%x\n", status));
		goto end;
	}

	//
	// This value is used in responding to the IRP_MN_QUERY_BUS_INFORMATION
	// for the child devices. This is an optional information provided to
	// uniquely identify the bus the device is connected.
	//

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = device;
    
	status = WdfDeviceAllocAndQueryProperty(device,
											DevicePropertyBusNumber,
											NonPagedPool,
											&attributes,
											&memory
											);	  
	if(!NT_SUCCESS(status)) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, 
			("WdfDeviceAllocAndQueryProperty failed with 0x%x\n", status));
		goto end;
	}


	// get bus number
	ptr = (UCHAR *)WdfMemoryGetBuffer(memory, NULL);
	bus = *(PULONG)ptr;

	WdfObjectDelete(memory);
	

	status = WdfDeviceAllocAndQueryProperty(device,
											DevicePropertyAddress,
											NonPagedPool,
											&attributes,
											&memory
											);

	if(!NT_SUCCESS(status)) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, 
			("WdfDeviceAllocAndQueryProperty failed with 0x%x\n", status));	
		goto end;
	}


	// get function number
	ptr = (UCHAR *)WdfMemoryGetBuffer(memory, NULL);
	function = *(PULONG)ptr;

	WdfObjectDelete(memory);
	

	//
	//	We don't use dev number in code
	//	Since in all machines the value is zero 
	//	we will set it to zero
	//
	dev = 0;

	
	busInfo.BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
	busInfo.LegacyBusType = Internal;
	busInfo.BusNumber = bus;

	WdfDeviceSetBusInformationForChildren(device, &busInfo);
	WdfDeviceSetSpecialFileSupport(device, WdfSpecialFilePaging, TRUE);
	WdfDeviceSetSpecialFileSupport(device, WdfSpecialFileHibernation, TRUE);
	WdfDeviceSetSpecialFileSupport(device, WdfSpecialFileDump, TRUE);

	//
	// WMI
	//
	status = WmiRegistration(device);
	if (!NT_SUCCESS(status)) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, 
			("WmiRegistration failed with 0x%x\n", status));
		goto end;
	}

#ifndef USE_WDM_INTERRUPTS

	//
	// create interrupt objects
	//
	for ( i = 0; i < MLX4_MAX_INTERRUPTS; ++i ) {
		status = __create_interrupt( p_fdo->FdoDevice, i, EvtInterruptIsr,
			NULL, p_fdo, &p_fdo->interrupt[i].WdfInterrupt );
		if (NT_SUCCESS(status)) 
			p_fdo->interrupt[i].valid = TRUE;
		else {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				("WdfInterruptCreate failed %#x\n", status ));
			goto end;
		}
	}

#endif

	// init pdev lock
	KeInitializeEvent( &p_fdo->pci_dev.remove_dev_lock, SynchronizationEvent , TRUE);
	
	// statistics
	p_fdo->pci_dev.p_stat = st_dev_add(bus, function);
	if ( p_fdo->pci_dev.p_stat ) {
		p_fdo->pci_dev.p_stat->p_fdo = p_fdo;
		p_fdo->pci_dev.p_stat->h_wdf_device = device;
		p_fdo->pci_dev.p_stat->flags = g.stat_flags;
		status = STATUS_SUCCESS;
	} else {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("st_dev_add failed (bus %d, func %d) \n" ,
			bus, function ));
		status = STATUS_NO_MEMORY;
		goto end;
	}

	mlx4_hca_add_device( device, &p_fdo->mlx4_hca_data );
	
end:	
	st_et_event("Bus: EvtDriverDeviceAdd ended with status %#x \n", status);
	MLX4_EXIT( MLX4_DBG_DRV );
	return status;
}

void
EvtDriverUnload(
	IN		WDFDRIVER  Driver
	)
{
	MLX4_ENTER( MLX4_DBG_DRV );

	UNUSED_PARAM( Driver );

	ShutdownWorkerThreads();

	mlx4_ib_cleanup();
	core_cleanup();

	CL_DEINIT;

	st_et_event("Bus: EvtDriverUnload called \n");
	st_mm_deinit();
	st_et_deinit();

	MLX4_EXIT( MLX4_DBG_DRV );
#if defined(EVENT_TRACING)
	WPP_CLEANUP(WdfDriverWdmGetDriverObject(Driver));
#endif

}

REG_ENTRY BusRegTable[] = 
{
//	reg value name										Offset in g								Field size					Default Value	Min  Max
	{REG_STRING_CONST("DebugLevel"),				REG_OFFSET_BWSD(g, DebugPrintLevel),	REG_SIZE_BWSD(g, DebugPrintLevel),	3,	0,	0xFFFF},
	{REG_STRING_CONST("DebugFlags"),				REG_OFFSET_BWSD(g, DebugPrintFlags),	REG_SIZE_BWSD(g, DebugPrintFlags),	0xFFFF,	0,	0xFFFF},
	// "log maximum number of QPs per HCA"
	{REG_STRING_CONST("LogNumQp"),					REG_OFFSET(g, log_num_qp),				REG_SIZE(g, log_num_qp),	0x11,	0x11,	0x18},
	// "log number of RDMARC buffers per QP"
	{REG_STRING_CONST("LogNumRdmaRc"),				REG_OFFSET(g, log_rdmarc_per_qp),		REG_SIZE(g, log_rdmarc_per_qp),	4,	0,	7},
	// "log maximum number of SRQs per HCA"
	{REG_STRING_CONST("LogNumSrq"),					REG_OFFSET(g, log_num_srq),				REG_SIZE(g, log_num_srq),	0x10,	8,	0x17},
	// "log maximum number of CQs per HCA"
	{REG_STRING_CONST("LogNumCq"),					REG_OFFSET(g, log_num_cq),				REG_SIZE(g, log_num_cq),	0x10,	8,	0x18},
	// "log maximum number of multicast groups per HCA"
	{REG_STRING_CONST("LogNumMcg"),					REG_OFFSET(g, log_num_mcg),				REG_SIZE(g, log_num_mcg),	0xD,	8,	0x10},
	// "log maximum number of memory protection table entries per HCA"
	{REG_STRING_CONST("LogNumMpt"),					REG_OFFSET(g, log_num_mpt),				REG_SIZE(g, log_num_mpt),	0x11,	0x11,	0x1F},
	// "log maximum number of memory translation table segments per HCA"
	{REG_STRING_CONST("LogNumMtt"),					REG_OFFSET(g, log_num_mtt),				REG_SIZE(g, log_num_mtt),	0x14,	0x14,	0x1F},
	// "Maximum number of MACs per ETH port (1-127, default 127)"
	{REG_STRING_CONST("LogNumMac"),					REG_OFFSET(g, log_num_mac),				REG_SIZE(g, log_num_mac),	7,	0,	7},
	// "Maximum number of VLANs per ETH port (0-127, default 127)"
	{REG_STRING_CONST("LogNumVlan"),				REG_OFFSET(g, log_num_vlan),			REG_SIZE(g, log_num_vlan),	7,	0,	7},
	// "Enable steering by VLAN priority on ETH ports (0/1, default 0)"
	{REG_STRING_CONST("UsePrio"),					REG_OFFSET(g, use_prio),				REG_SIZE(g, use_prio),	0,	0,	1},
	// "max number of FC_EXCH (0-N, default 0)"
	{REG_STRING_CONST("NumFcExch"),					REG_OFFSET(g, num_fc_exch),				REG_SIZE(g, num_fc_exch), 0,	0,	0x8000},
	// "Enable Quality of Service support in the HCA if > 0, (default 1)"
	{REG_STRING_CONST("EnableQoS"),					REG_OFFSET(g, enable_qos),				REG_SIZE(g, enable_qos), 0,	0,	1},
	// "Block multicast loopback packets if > 0 (default 1)"
	{REG_STRING_CONST("BlockMcastLoopBack"),		REG_OFFSET(g, mlx4_blck_lb),			REG_SIZE(g, mlx4_blck_lb),	0,	0,	1},
	// "Measure the interrupt from the first packet (default 1)"
	{REG_STRING_CONST("InterruptFromFirstPacket"),	REG_OFFSET(g, interrupt_from_first),	REG_SIZE(g, interrupt_from_first),	1,	0,	1},
	// "Work Mode Flags"
	{REG_STRING_CONST("ModeFlags"),					REG_OFFSET(g, mode_flags),				REG_SIZE(g, mode_flags), 0,	0,	0xFFFFFFFF},
	{REG_STRING_CONST("StatFlags"),					REG_OFFSET(g, stat_flags),				REG_SIZE(g, stat_flags),	0,	0,	0xFFFFFFFF},
	{REG_STRING_CONST("LogMttsPerSeg"),				REG_OFFSET(g, log_mtts_per_seg),		REG_SIZE(g, log_mtts_per_seg),	3,	1,	5},
	{REG_STRING_CONST("SrIov"),						REG_OFFSET(g, sr_iov),					REG_SIZE(g, sr_iov), 0,	0,	1},
	{REG_STRING_CONST("ProbeVf"),					REG_OFFSET(g, probe_vf),				REG_SIZE(g, probe_vf), 0,	0,	1},
	{REG_STRING_CONST("Set4kMtu"),					REG_OFFSET(g, set_4k_mtu),				REG_SIZE(g, set_4k_mtu), 1,	0,	1},
	{REG_STRING_CONST("ProcessorAffinity"),			REG_OFFSET(g, affinity),				REG_SIZE(g, affinity), 0,	0,	ULONG_MAX},
	{REG_STRING_CONST("SlaveNum"),					REG_OFFSET(g, slave_num),				REG_SIZE(g, slave_num), 7,	0,	ULONG_MAX},
	{REG_STRING_CONST("SingleMsixNum"),				REG_OFFSET(g, max_single_msix_num),		REG_SIZE(g, max_single_msix_num), 0,	0,	ULONG_MAX},
	{REG_STRING_CONST("MultiMsixNum"),				REG_OFFSET(g, max_multi_msix_num),		REG_SIZE(g, max_multi_msix_num), 0,	0,	ULONG_MAX},
	{REG_STRING_CONST("EnableFcPreT11"),			REG_OFFSET(g, mlx4_pre_t11_mode),		REG_SIZE(g, mlx4_pre_t11_mode), 0,	0,	1},
	{REG_STRING_CONST("MaximumWorkingThreads"),		REG_OFFSET(g, max_working_threads),		REG_SIZE(g, max_working_threads), 4,	1,	256},
	{REG_STRING_CONST("PortType1"),					REG_OFFSET(g, PortType1),				REG_SIZE(g, PortType1), MLX4_PORT_TYPE_AUTO,	MLX4_PORT_TYPE_NONE,	MLX4_PORT_TYPE_AUTO},
	{REG_STRING_CONST("PortType2"),					REG_OFFSET(g, PortType2),				REG_SIZE(g, PortType2), MLX4_PORT_TYPE_AUTO,	MLX4_PORT_TYPE_NONE,	MLX4_PORT_TYPE_AUTO},
	{REG_STRING_CONST("busy_wait_behavior"),		REG_OFFSET(g, busy_wait_behavior),		REG_SIZE(g, busy_wait_behavior), IMMIDIATE_BREAK,	RESUME_RUNNING,	IMMIDIATE_BREAK},
	{REG_STRING_CONST("CmdNumToFail"),				REG_OFFSET(g, cmd_num_to_fail),			REG_SIZE(g, cmd_num_to_fail), 0,	0,	6000},

};

#define BUS_NUM_REG_PARAMS (sizeof (BusRegTable) / sizeof(REG_ENTRY))


static
NTSTATUS
__read_bus_registry(
	IN PUNICODE_STRING RegistryPath
	)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = ReadRegistry(RegistryPath->Buffer, L"\\Parameters", BusRegTable, BUS_NUM_REG_PARAMS);
	if (!NT_SUCCESS (status)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			("__read_registry failed %#x\n", status ));
		return status;
	}

	// Save debug parameters for global use
	g_mlx4_dbg_level = g.bwsd.DebugPrintLevel;
	g_mlx4_dbg_flags = g.bwsd.DebugPrintFlags;

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
		("Globals: slave_num %d, mode_flags %x\n", g.slave_num, g.mode_flags));

return STATUS_SUCCESS;
}


extern "C"
NTSTATUS
DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath
	)
/*++
Routine Description:

	Initialize the call backs structure of Driver Framework.

Arguments:

	DriverObject - pointer to the driver object

	RegistryPath - pointer to a unicode string representing the path,
				to driver-specific key in the registry.

Return Value:

  NT Status Code

--*/
{
	int err;
	WDF_DRIVER_CONFIG	config;
	NTSTATUS			status;
	WDFDRIVER hDriver;
#if defined(EVENT_TRACING)
	WPP_INIT_TRACING(DriverObject, RegistryPath);
#endif

	// statistics
	RtlZeroMemory( &g_stat, sizeof(g_stat) );
	st_et_init();
	st_et_event("Bus: DriverEntry started \n");
	st_mm_init();

	RtlZeroMemory( &g, sizeof(g) );
	g_mlx4_dbg_level = g.bwsd.DebugPrintLevel = TRACE_LEVEL_VERBOSE;
	g_mlx4_dbg_flags = g.bwsd.DebugPrintFlags = 0xffff;
	g_time_increment = KeQueryTimeIncrement();
	KeInitializeEvent( &g.start_event, SynchronizationEvent , TRUE);
	//BUGBUG: SRIOV is not supported
	KeInitializeEvent( &g.surprise_removal_event, SynchronizationEvent , TRUE);
    
#if 0    
	MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV, 
		("Built %s %s, Version %s, RelDate %s\n", 
		__DATE__, __TIME__, DRV_VERSION, DRV_RELDATE));
#endif

	status = CL_INIT;
	if( !NT_SUCCESS(status) )
	{
		MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("cl_init returned %08X.\n", status) );
		goto exit_wpp;
	}


	//
	// Initiialize driver config to control the attributes that
	// are global to the driver. Note that framework by default
	// provides a driver unload routine. If you create any resources
	// in the DriverEntry and want to be cleaned in driver unload,
	// you can override that by specifing one in the Config structure.
	//

	WDF_DRIVER_CONFIG_INIT(
		&config, EvtDriverDeviceAdd );
	config.EvtDriverUnload = EvtDriverUnload;

	//
	// Create a framework driver object to represent our driver.
	//
	status = WdfDriverCreate(DriverObject,
		RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
		&config, &hDriver);

	if (!NT_SUCCESS(status)) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, ("WdfDriverCreate failed with status 0x%x\n", status));
		goto error;
	}

	// get Registry parameters
	status = __read_bus_registry(RegistryPath);
	if ( !NT_SUCCESS( status ) )
		goto error;

	// global initializations
	err = mlx4_net_init();
	if (err) {
		status = errno_to_ntstatus(err);
		goto error;
	}
	err = core_init();
	if (err) {
		status = errno_to_ntstatus(err);
		goto error;
	}
	err = mlx4_ib_init();
	if (err) {
		status = errno_to_ntstatus(err);
		goto error;
	}

	g_MaximumWorkingThreads = g.max_working_threads;

	// statistics
	g_stat.drv.p_globals = &g;
	g_stat.drv.h_wdf_driver = hDriver;

	status = InitWorkerThreads();
	if (!NT_SUCCESS(status)) 
	{
	    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV, 
			("InitWorkerThreads failed. Error= 0x%x\n", status));
	   goto error;
	}

	status =  mlx4_hca_driver_entry();
	if (!NT_SUCCESS(status)) 
	   goto error;
	
	// we don't matter the failure in the work with Registry
	st_et_event("Bus: DriverEntry ended with status 0\n");
	return STATUS_SUCCESS;
	
error:
    CL_DEINIT;
	st_et_event("Bus: DriverEntry ended with %#x \n", status);
	st_et_deinit();

exit_wpp:
#if defined(EVENT_TRACING)
		WPP_CLEANUP(DriverObject);
#endif
	return status;

}


