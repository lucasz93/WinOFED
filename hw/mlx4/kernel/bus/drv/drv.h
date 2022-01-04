/*++

Copyright (c) 2003 Microsoft Corporation All Rights Reserved

Module Name:

    mxe_drv.h

Abstract:

    This module contains the common private declarations
    for the Mxe Bus enumerator.

Environment:

    kernel mode only

--*/

#pragma once

#define BUSENUM_POOL_TAG (ULONG) 'suBT'
#define N_BARS		3

#include "net\mlx4.h"
#include <mlx4_debug.h>
#include "bus_intf.h"
#include "fc_bus_if.h"
#include "rdma\verbs.h"
#include "..\..\hca\data.h"
#include "..\..\hca\hca_fdo_data.h"

#if DBG
#define BUS_DEFAULT_DEBUG_OUTPUT_LEVEL 0x000FFFFF

#else

#define BUS_DEFAULT_DEBUG_OUTPUT_LEVEL 0x0

#endif

#define BUSRESOURCENAME L"MofResourceName"

#ifndef min
#define min(_a, _b)     (((_a) < (_b)) ? (_a) : (_b))
#endif

#ifndef max
#define max(_a, _b)     (((_a) > (_b)) ? (_a) : (_b))
#endif


#define MLX4_MAX_INTERRUPTS		33

typedef struct {
	WDFINTERRUPT				WdfInterrupt;
	BOOLEAN						valid;
} res_interrupt_t;

//
// The device extension for the PDOs.
// That's of the mxe device which this bus driver enumerates.
//
typedef struct _FDO_DEVICE_DATA *PFDO_DEVICE_DATA;

typedef struct _PDO_DEVICE_DATA 

{
	// Unique serial number of the device on the bus
	ULONG							SerialNo;
	// WDF PDO object
	WDFDEVICE						PdoDevice;
	// FDO context
	PFDO_DEVICE_DATA				p_fdo;
	// MLX4 BUS IB interface
	WDF_QUERY_INTERFACE_CONFIG		qiMlx4Bus;
	WDF_QUERY_INTERFACE_CONFIG		qiPciBus;
	WDF_QUERY_INTERFACE_CONFIG		qiMlx4FcoxIf;
 
} PDO_DEVICE_DATA, *PPDO_DEVICE_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PDO_DEVICE_DATA, PdoGetData)


//
// The device extension of the bus itself.  From whence the PDO's are born.
//
typedef struct _HCA_FDO_DEVICE_DATA HCA_FDO_DEVICE_DATA;

typedef struct _FDO_DEVICE_DATA
{
	BUS_WMI_STD_DATA			WmiData;
	WDFWAITLOCK					ChildLock;
	WDFDEVICE					FdoDevice;
	struct pci_dev 				pci_dev;
	int							card_started;
	int							pci_bus_ifc_taken;
	WDFDMAENABLER				dma_enabler;
	int							dma_adapter_taken;
	MLX4_BUS_IB_INTERFACE		bus_ib_ifc;
	MLX4_FC_BUS_IF		        bus_fc_if;
	MLX4_BUS_NOTIFY_INTERFACE	bus_not_ifc;
	int							children_created;
	// Data for the Ethernet device
	struct VipBusIfc			mtnic_Ifc;

#ifndef USE_WDM_INTERRUPTS
	res_interrupt_t 			interrupt[MLX4_MAX_INTERRUPTS];
#endif
	PDEVICE_OBJECT				p_ibbus_fdo;
	DEVICE_OBJECT			*	p_dev_obj;		/* WDM dev object */
	atomic32_t					bus_ib_ifc_ref;
	atomic32_t					bus_fc_ifc_ref;
	atomic32_t					bus_loc_ifc_ref;
	atomic32_t					bus_not_ifc_ref;
	atomic32_t					bus_hca_ifc_ref;
	atomic32_t					n_ifc_ref;
	KEVENT						exit_event;
	// hca data	
	HCA_FDO_DEVICE_DATA 		mlx4_hca_data;	// MLX4_HCA.LIB context
	PDO_DEVICE_DATA 			mlx4_hca_pdo;	// fake pdo_data

} FDO_DEVICE_DATA, *PFDO_DEVICE_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FDO_DEVICE_DATA, FdoGetData)

typedef struct _QUEUE_DATA
{
	PFDO_DEVICE_DATA FdoData;

} QUEUE_DATA, *PQUEUE_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_DATA, QueueGetData)

 //
// wmi.c
//

NTSTATUS
WmiRegistration(
	WDFDEVICE      Device
);

#ifdef NTDDI_WIN8
EVT_WDF_WMI_INSTANCE_SET_ITEM EvtStdDataSetItem;
#else
NTSTATUS
EvtStdDataSetItem(
	IN  WDFWMIINSTANCE WmiInstance,
	IN  ULONG DataItemId,
	IN  ULONG InBufferSize,
	IN  PVOID InBuffer
	);
#endif


#ifdef NTDDI_WIN8
EVT_WDF_WMI_INSTANCE_SET_INSTANCE EvtStdDataSetInstance;
#else
NTSTATUS
EvtStdDataSetInstance(
	IN  WDFWMIINSTANCE WmiInstance,
	IN  ULONG InBufferSize,
	IN  PVOID InBuffer
	);
#endif


#ifdef NTDDI_WIN8
EVT_WDF_WMI_INSTANCE_QUERY_INSTANCE EvtStdDataQueryInstance;
#else
NTSTATUS
EvtStdDataQueryInstance(
	IN  WDFWMIINSTANCE WmiInstance,
	IN  ULONG OutBufferSize,
	IN  PVOID OutBuffer,
	OUT PULONG BufferUsed
	);
#endif



//
// drv.c
//
extern "C"
DRIVER_INITIALIZE DriverEntry;
extern "C"
NTSTATUS
DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath
	);

#ifdef NTDDI_WIN8
EVT_WDF_DRIVER_UNLOAD EvtDriverUnload;
#endif
void
EvtDriverUnload(
	IN		WDFDRIVER  Driver
	);

#ifdef NTDDI_WIN8
EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
#endif
NTSTATUS
EvtDriverDeviceAdd(
	IN WDFDRIVER        Driver,
	IN PWDFDEVICE_INIT  DeviceInit
	);

#ifdef NTDDI_WIN8
EVT_WDF_DEVICE_PREPARE_HARDWARE EvtPrepareHardware;
#endif
NTSTATUS
EvtPrepareHardware(
	IN WDFDEVICE  Device,
	IN WDFCMRESLIST  ResourcesRaw,
	IN WDFCMRESLIST  ResourcesTranslated
	);

#ifdef NTDDI_WIN8
EVT_WDF_DEVICE_RELEASE_HARDWARE EvtReleaseHardware;
#endif
NTSTATUS
EvtReleaseHardware(
	IN WDFDEVICE  Device,
	IN WDFCMRESLIST  ResourcesTranslated
	);

#ifdef NTDDI_WIN8
EVT_WDF_DEVICE_D0_EXIT EvtDeviceD0Exit;
#endif
NTSTATUS
EvtDeviceD0Exit(
	IN WDFDEVICE  Device,
	IN WDF_POWER_DEVICE_STATE  TargetState
	);

#ifdef NTDDI_WIN8
EVT_WDF_DEVICE_D0_ENTRY EvtDeviceD0Entry;
#endif
NTSTATUS
EvtDeviceD0Entry(
	IN WDFDEVICE  Device,
	IN WDF_POWER_DEVICE_STATE  PreviousState
	);


//
// pci.c
//

ULONG
__find_capability(
    PCI_COMMON_CONFIG* const    pConfig,  
    char cap_id
    );

void
pci_free_msix_info_resources(
	IN struct msix_saved_info * 	pMsixInfo
	);

NTSTATUS
pci_save_config(
	IN				BUS_INTERFACE_STANDARD		*pBusIfc,
		OUT			PCI_COMMON_CONFIG* const	pConfig
	);

NTSTATUS
pci_hca_reset( 
	IN		struct pci_dev *pdev
	);

void
pci_get_uplink_info(
	IN				struct pci_dev *		pdev,
	IN				PCI_COMMON_CONFIG * 	p_cfg,
	OUT 			uplink_info_t * 		p_uplink_info
	);

NTSTATUS
pci_hca_enable(
	IN		PBUS_INTERFACE_STANDARD 	p_ifc,
	IN		PCI_COMMON_CONFIG*			p_cfg
	);

//
// pdo.c
//

NTSTATUS
create_pdo(
	__in WDFDEVICE  Device,
	__in PWCHAR     HardwareIds,
	__in PWCHAR 	CompatibleIds,
	__in PWCHAR     DeviceDescription,
	__in ULONG      SerialNo,
	__in PWCHAR     Location
);

NTSTATUS RegisterVPIInterfaceIb(PPDO_DEVICE_DATA p_pdo, 
						   PFDO_DEVICE_DATA p_fdo, 
						   WDFDEVICE hChild,
						   PWCHAR DeviceDescription,
						   ULONG SerialNo);

//
// vpd.c
//
NTSTATUS pci_get_vpd(
	BUS_INTERFACE_STANDARD		*pBusIfc,
	PCI_COMMON_CONFIG* const	pConfig,
	UCHAR*						*p_vpd,
	int*						p_vpd_size
	);

VOID notify_cb (PVOID ifc_ctx, ULONG type, PVOID p_data, PCHAR str);

#ifdef __cplusplus
extern "C"
{
#endif
		
	NTSTATUS mlx4_hca_driver_entry();
	
	void mlx4_hca_add_device(
		IN		WDFDEVICE FdoDevice,
		IN		PHCA_FDO_DEVICE_DATA			p_fdo
		);
	
	NTSTATUS mlx4_hca_start(
		IN		PHCA_FDO_DEVICE_DATA		p_fdo,
		IN		BUS_INTERFACE_STANDARD		*p_bus_pci_ifc,
		IN		MLX4_BUS_IB_INTERFACE		*p_bus_ib_ifc
		);
	
	void mlx4_hca_stop(
		IN		PHCA_FDO_DEVICE_DATA			p_fdo
		);
	
	int mlx4_hca_get_usecnt(
		IN		PHCA_FDO_DEVICE_DATA			p_fdo
		);

    void
    mlx4_setup_hca_ifc(
        IN      PHCA_FDO_DEVICE_DATA const      p_fdo,
        OUT     ci_interface_t*                 pIfc
		);

#ifdef __cplusplus
}
#endif

