
#include "hca_driver.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_pci.tmh"
#endif
#include <complib/cl_thread.h>
#include <initguid.h>
#include <wdmguid.h>

#define HCA_RESET_HCR_OFFSET				0x000F0010
#define HCA_RESET_TOKEN						CL_HTON32(0x00000001)

#define PCI_CAPABILITY_ID_VPD				0x03
#define PCI_CAPABILITY_ID_PCIX				0x07
#define PCI_CAPABILITY_ID_PCIEXP			0x10

boolean_t
FindBridgeIf(
	IN hca_dev_ext_t		*pi_ext,
	IN	PBUS_INTERFACE_STANDARD	pi_pInterface
	);


/*
 * Vital Product Data Capability
 */
typedef struct _PCI_VPD_CAPABILITY {

	PCI_CAPABILITIES_HEADER	Header;

	USHORT		Flags;
	ULONG			Data;

} PCI_VPD_CAPABILITY, *PPCI_VPD_CAPABILITY;


/*
 * PCI-X Capability
 */
typedef struct _PCI_PCIX_CAPABILITY {

	PCI_CAPABILITIES_HEADER	Header;

	USHORT		Command;
	ULONG			Status;

/* for Command: */
} PCI_PCIX_CAPABILITY, *PPCI_PCIX_CAPABILITY;

#define  PCI_X_CMD_MAX_READ     0x000c  /* Max Memory Read Byte Count */

/*
 * PCI-Express Capability
 */
typedef struct _PCI_PCIEXP_CAPABILITY {

	PCI_CAPABILITIES_HEADER	Header;

	USHORT		Flags;
	ULONG			DevCapabilities;
	USHORT		DevControl;
	USHORT		DevStatus;
	ULONG			LinkCapabilities;
	USHORT		LinkControl;
	USHORT		LinkStatus;
	ULONG			SlotCapabilities;
	USHORT		SlotControl;
	USHORT		SlotStatus;
	USHORT		RootControl;
	USHORT		RootCapabilities;
	USHORT		RootStatus;
} PCI_PCIEXP_CAPABILITY, *PPCI_PCIEXP_CAPABILITY;

/* for DevControl: */
#define  PCI_EXP_DEVCTL_READRQ  0x7000  /* Max_Read_Request_Size */

static NTSTATUS
__get_bus_ifc(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN		const	GUID* const					pGuid,
		OUT			BUS_INTERFACE_STANDARD		*pBusIfc );

static void
__fixup_pci_capabilities(
	IN				PCI_COMMON_CONFIG* const	pConfig );

static NTSTATUS
__save_pci_config(
	IN				BUS_INTERFACE_STANDARD		*pBusIfc,
		OUT			PCI_COMMON_CONFIG* const	pConfig );

static NTSTATUS
__restore_pci_config(
	IN				BUS_INTERFACE_STANDARD		*pBusIfc,
	IN				PCI_COMMON_CONFIG* const	pConfig,
	IN				const int 						is_bridge );


/*
 * Returns the offset in configuration space of the PCI-X capabilites.
 */
static ULONG
__FindCapability(
	IN				PCI_COMMON_CONFIG* const	pConfig,  
	IN				char cap_id
	)
{
	ULONG						offset = 0;
	PCI_CAPABILITIES_HEADER		*pHdr = NULL;
	UCHAR						*pBuf = (UCHAR*)pConfig;

	HCA_ENTER( HCA_DBG_PNP );

	if  ( pConfig->HeaderType == PCI_DEVICE_TYPE ) {
		if( pConfig->u.type0.CapabilitiesPtr )
		{
			pHdr = (PCI_CAPABILITIES_HEADER*)
				(pBuf + pConfig->u.type0.CapabilitiesPtr);
		}
	}

	if  ( pConfig->HeaderType == PCI_BRIDGE_TYPE ) {
		if( pConfig->u.type1.CapabilitiesPtr )
		{
			pHdr = (PCI_CAPABILITIES_HEADER*)
				(pBuf + pConfig->u.type1.CapabilitiesPtr);
		}
	}

	/*
	 * Fix up any fields that might cause changes to the
	 * device - like writing VPD data.
	 */
	while( pHdr )
	{
		if( pHdr->CapabilityID == cap_id )
		{
			offset = (UCHAR)(((ULONG_PTR)pHdr) - ((ULONG_PTR)pConfig));
			break;
		}

		if( pHdr->Next )
			pHdr = (PCI_CAPABILITIES_HEADER*)(pBuf + pHdr->Next);
		else
			pHdr = NULL;
	}

	HCA_EXIT( HCA_DBG_PNP );
	return offset;
}

/* Forwards the request to the HCA's PDO. */
static NTSTATUS
__get_bus_ifc(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN		const	GUID* const					pGuid,
		OUT			BUS_INTERFACE_STANDARD		*pBusIfc )
{
	NTSTATUS			status;
	IRP					*pIrp;
	IO_STATUS_BLOCK		ioStatus;
	IO_STACK_LOCATION	*pIoStack;
	DEVICE_OBJECT		*pDev;
	KEVENT				event;

	HCA_ENTER( HCA_DBG_PNP );

	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

	pDev = IoGetAttachedDeviceReference( pDevObj );

	KeInitializeEvent( &event, NotificationEvent, FALSE );

	/* Build the IRP for the HCA. */
	pIrp = IoBuildSynchronousFsdRequest( IRP_MJ_PNP, pDev,
		NULL, 0, NULL, &event, &ioStatus );
	if( !pIrp )
	{
		ObDereferenceObject( pDev );
		HCA_PRINT( TRACE_LEVEL_ERROR,HCA_DBG_PNP, 
			("IoBuildSynchronousFsdRequest failed.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	/* Copy the request query parameters. */
	pIoStack = IoGetNextIrpStackLocation( pIrp );
	pIoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
	pIoStack->Parameters.QueryInterface.Size = sizeof(BUS_INTERFACE_STANDARD);
	pIoStack->Parameters.QueryInterface.Version = 1;
	pIoStack->Parameters.QueryInterface.InterfaceType = pGuid;
	pIoStack->Parameters.QueryInterface.Interface = (INTERFACE*)pBusIfc;
	pIoStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

	pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;

	/* Send the IRP. */
	status = IoCallDriver( pDev, pIrp );
	if( status == STATUS_PENDING )
	{
		KeWaitForSingleObject( &event, Executive, KernelMode,
			FALSE, NULL );

		status = ioStatus.Status;
	}
	ObDereferenceObject( pDev );

	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


/*
 * Reads and saves the PCI configuration of the device accessible
 * through the provided bus interface.  Does not read registers 22 or 23
 * as directed in Tavor PRM 1.0.1, Appendix A. InfiniHost Software Reset.
 */
static NTSTATUS
__save_pci_config(
	IN				BUS_INTERFACE_STANDARD		*pBusIfc,
		OUT			PCI_COMMON_CONFIG* const	pConfig )
{
	ULONG					len;
	UINT32					*pBuf;

	HCA_ENTER( HCA_DBG_PNP );
	
	pBuf = (UINT32*)pConfig;

	/*
	 * Read the lower portion of the configuration, up to but excluding
	 * register 22.
	 */
	len = pBusIfc->GetBusData(
		pBusIfc->Context, PCI_WHICHSPACE_CONFIG, &pBuf[0], 0, 88 );
	if( len != 88 )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR  , HCA_DBG_PNP  ,("Failed to read HCA config.\n"));
		return STATUS_DEVICE_NOT_READY;
	}

	/* Read the upper portion of the configuration, from register 24. */
	len = pBusIfc->GetBusData(
		pBusIfc->Context, PCI_WHICHSPACE_CONFIG, &pBuf[24], 96, 160 );
	if( len != 160 )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR  ,HCA_DBG_PNP  ,("Failed to read HCA config.\n"));
		return STATUS_DEVICE_NOT_READY;
	}

	HCA_EXIT( HCA_DBG_PNP );
	return STATUS_SUCCESS;
}


static void
__fixup_pci_capabilities(
	IN				PCI_COMMON_CONFIG* const	pConfig )
{
	UCHAR						*pBuf;
	PCI_CAPABILITIES_HEADER		*pHdr, *pNextHdr;

	HCA_ENTER( HCA_DBG_PNP );

	pBuf = (UCHAR*)pConfig;

	if( pConfig->HeaderType == PCI_DEVICE_TYPE )
	{
		if( pConfig->u.type0.CapabilitiesPtr )
		{
			pNextHdr = (PCI_CAPABILITIES_HEADER*)
				(pBuf + pConfig->u.type0.CapabilitiesPtr);
		}
		else
		{
			pNextHdr = NULL;
		}
	}
	else
	{
		ASSERT( pConfig->HeaderType == PCI_BRIDGE_TYPE );
		if( pConfig->u.type1.CapabilitiesPtr )
		{
			pNextHdr = (PCI_CAPABILITIES_HEADER*)
				(pBuf + pConfig->u.type1.CapabilitiesPtr);
		}
		else
		{
			pNextHdr = NULL;
		}
	}

	/*
	 * Fix up any fields that might cause changes to the
	 * device - like writing VPD data.
	 */
	while( pNextHdr )
	{
		pHdr = pNextHdr;
		if( pNextHdr->Next )
			pNextHdr = (PCI_CAPABILITIES_HEADER*)(pBuf + pHdr->Next);
		else
			pNextHdr = NULL;

		switch( pHdr->CapabilityID )
		{
		case PCI_CAPABILITY_ID_VPD:
			/* Clear the flags field so we don't cause a write. */
			((PCI_VPD_CAPABILITY*)pHdr)->Flags = 0;
			break;

		default:
			break;
		}
	}

	HCA_EXIT( HCA_DBG_PNP );
}


/*
 * Restore saved PCI configuration, skipping registers 22 and 23, as well
 * as any registers where writing will have side effects such as the flags
 * field of the VPD and vendor specific capabilities.  The function also delays
 * writing the command register, bridge control register (if applicable), and
 * PCIX command register (if present).
 */
static NTSTATUS
__restore_pci_config(
	IN				BUS_INTERFACE_STANDARD		*pBusIfc,
	IN				PCI_COMMON_CONFIG* const	pConfig,
	IN				const int 						is_bridge )
{
	NTSTATUS status = STATUS_SUCCESS;
	int		i, *pci_hdr = (int*)pConfig;
	int hca_pcix_cap = 0;

	HCA_ENTER( HCA_DBG_PNP );

	/* get capabilities */
	hca_pcix_cap = __FindCapability( pConfig, PCI_CAPABILITY_ID_PCIX );

	/* restore capabilities*/
	if (is_bridge) {
		if ( 4 != pBusIfc->SetBusData( pBusIfc->Context, PCI_WHICHSPACE_CONFIG,
			&pci_hdr[(hca_pcix_cap + 0x8) / 4], hca_pcix_cap + 0x8, 4) ) {
			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
				("Couldn't restore HCA bridge Upstream split transaction control, aborting.\n"));
			status = STATUS_UNSUCCESSFUL;
			goto out;
		}
		if ( 4 != pBusIfc->SetBusData( pBusIfc->Context, PCI_WHICHSPACE_CONFIG,
			&pci_hdr[(hca_pcix_cap + 0xc) / 4], hca_pcix_cap + 0xc, 4) ) {
			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
				("Couldn't restore HCA bridge Downstream split transaction control, aborting.\n"));
			status = STATUS_UNSUCCESSFUL;
			goto out;
		}
	}
	else {
		int hca_pcie_cap = __FindCapability( pConfig, PCI_CAPABILITY_ID_PCIEXP );
		PCI_PCIEXP_CAPABILITY	*pPciExpCap = (PCI_PCIEXP_CAPABILITY*)(((UCHAR*)pConfig) + hca_pcie_cap);

		if (hca_pcix_cap) {
			if ( 4 != pBusIfc->SetBusData( pBusIfc->Context, PCI_WHICHSPACE_CONFIG,
				&pci_hdr[hca_pcix_cap/4], hca_pcix_cap, 4) ) {
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
					("Couldn't restore HCA PCI-X command register, aborting.\n"));
				status = STATUS_UNSUCCESSFUL;
				goto out;
			}
		}

		if (hca_pcie_cap) {
			/* restore HCA PCI Express Device Control register */
			if ( sizeof( pPciExpCap->DevControl ) != pBusIfc->SetBusData( 
				pBusIfc->Context, PCI_WHICHSPACE_CONFIG,
				&pPciExpCap->DevControl, 	hca_pcie_cap + 
				offsetof( PCI_PCIEXP_CAPABILITY, DevControl),
				sizeof( pPciExpCap->DevControl ) )) {
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
					("Couldn't restore HCA PCI Express Device Control register, aborting.\n"));
				status = STATUS_UNSUCCESSFUL;
				goto out;
			}
			/* restore HCA PCI Express Link Control register */
			if ( sizeof( pPciExpCap->LinkControl ) != pBusIfc->SetBusData( 
				pBusIfc->Context, PCI_WHICHSPACE_CONFIG,
				&pPciExpCap->LinkControl,	hca_pcie_cap + 
				offsetof( PCI_PCIEXP_CAPABILITY, LinkControl),
				sizeof( pPciExpCap->LinkControl ) )) {
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
					("Couldn't restore HCA PCI Express Link Control register, aborting.\n"));
				status = STATUS_UNSUCCESSFUL;
				goto out;
			}
		}
	}

	/* write basic part */
	for (i = 0; i < 16; ++i) {
		if (i == 1)
			continue;
	
		if (4 != pBusIfc->SetBusData( pBusIfc->Context,
			PCI_WHICHSPACE_CONFIG, &pci_hdr[i], i * 4, 4 )) {
			HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_PNP ,
				("Couldn't restore PCI cfg reg %x,   aborting.\n", i));
			status =  STATUS_DEVICE_NOT_READY;
			goto out;
		}
	}

	/* Write the command register. */
	if (4 != pBusIfc->SetBusData( pBusIfc->Context,
		PCI_WHICHSPACE_CONFIG, &pci_hdr[1], 4, 4 )) {
		HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_PNP ,("Couldn't restore COMMAND.\n"));
		status =  STATUS_DEVICE_NOT_READY;
	}

out:	
	HCA_EXIT( HCA_DBG_PNP );
	return status;
}

NTSTATUS
hca_reset( DEVICE_OBJECT* const		pDevObj, int is_tavor )
{
	NTSTATUS				status = STATUS_SUCCESS;
	PCI_COMMON_CONFIG		hcaConfig, brConfig;
	BUS_INTERFACE_STANDARD	hcaBusIfc;
	BUS_INTERFACE_STANDARD	brBusIfc = {0};	// to bypass C4701
	hca_dev_ext_t			*pExt = (hca_dev_ext_t*)pDevObj->DeviceExtension;

	HCA_ENTER( HCA_DBG_PNP );

	/* sanity check */
	if (is_tavor && g_skip_tavor_reset) {
		HCA_PRINT(TRACE_LEVEL_WARNING  ,HCA_DBG_PNP  ,("Card reset is skipped, trying to proceed.\n"));
		goto resetExit;
	}

	/* get the resources */
	{
		/* Get the HCA's bus interface. */
		status = __get_bus_ifc( pDevObj, &GUID_BUS_INTERFACE_STANDARD, &hcaBusIfc );
		if( !NT_SUCCESS( status ) ) {
			HCA_PRINT( TRACE_LEVEL_ERROR  ,HCA_DBG_PNP  ,("Failed to get HCA bus interface.\n"));
			goto resetErr1;
		}

		/* Get the HCA Bridge's bus interface, if any */
		if (is_tavor) {
			if (!FindBridgeIf( pExt, &brBusIfc ))
				goto resetErr2;
		}
	}

	/* Save the HCA's PCI configuration headers */
	{
		status = __save_pci_config( &hcaBusIfc, &hcaConfig );
		if( !NT_SUCCESS( status ) ) {
			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP,
				("Failed to save HCA config.\n"));
			goto resetErr3;
		}

		/* Save the HCA bridge's configuration, if any */
		if (is_tavor) {
			int hca_pcix_cap;
			status = __save_pci_config( &brBusIfc, &brConfig );
			if( !NT_SUCCESS( status ) ) {
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP,
					("Failed to save bridge config.\n"));
				goto resetErr3;
			}
			hca_pcix_cap = __FindCapability( &brConfig, PCI_CAPABILITY_ID_PCIX );
			if (!hca_pcix_cap) {
				status = STATUS_UNSUCCESSFUL;
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP,
					("Couldn't locate HCA bridge PCI-X capability, aborting.\n"));
				goto resetErr3;
			}
		}
	}
	
	/* reset the card */
	{
		PULONG	reset_p;
		PHYSICAL_ADDRESS  pa;
		/* map reset register */
		pa.QuadPart = pExt->bar[HCA_BAR_TYPE_HCR].phys + (uint64_t)HCA_RESET_HCR_OFFSET;
		HCA_PRINT( TRACE_LEVEL_INFORMATION ,HCA_DBG_PNP  ,("Mapping reset register with address 0x%I64x\n", pa.QuadPart));
		reset_p = MmMapIoSpace( pa,	4, MmNonCached );
		if( !reset_p ) {
			HCA_PRINT( TRACE_LEVEL_ERROR  ,HCA_DBG_PNP  ,("Failed to map reset register with address 0x%I64x\n", pa.QuadPart));
			status = STATUS_UNSUCCESSFUL;
			goto resetErr3;
		}
		
		/* Issue the reset. */
		HCA_PRINT( TRACE_LEVEL_INFORMATION ,HCA_DBG_PNP  ,("Resetting  the chip ...\n"));
		WRITE_REGISTER_ULONG( reset_p, HCA_RESET_TOKEN );

		/* unmap the reset register */
		HCA_PRINT( TRACE_LEVEL_INFORMATION ,HCA_DBG_PNP  ,("Unmapping reset register \n"));
		MmUnmapIoSpace( reset_p, 4 );

		/* Wait a second. */
		cl_thread_suspend( 1000 );
	}

	/* Read the configuration register until it doesn't return 0xFFFFFFFF */
	{
		ULONG					data, i, reset_failed = 1;
		BUS_INTERFACE_STANDARD *p_ifc = (is_tavor) ? &brBusIfc : &hcaBusIfc;
		HCA_PRINT( TRACE_LEVEL_INFORMATION	,HCA_DBG_PNP  ,("Read the configuration register \n"));
		for( i = 0; i < 100; i++ ) {
			if (4 != p_ifc->GetBusData( p_ifc->Context,
				PCI_WHICHSPACE_CONFIG, &data, 0, 4)) {
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
					("Failed to read device configuration data. Card reset failed !\n"));
				status = STATUS_UNSUCCESSFUL;
				break;
			}
			/* See if we got valid data. */
			if( data != 0xFFFFFFFF ) {
				reset_failed = 0;
				break;
			}
		
			cl_thread_suspend( 100 );
		}	

		if (reset_failed) {
			/* on Tavor reset failure, if configured so, we disable the reset for next time */
			if (is_tavor && g_disable_tavor_reset)
				set_skip_tavor_reset();

			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
				("Doh! PCI device did not come back after reset!\n"));
			status = STATUS_UNSUCCESSFUL;
			goto resetErr3;
		}
	}

	/* restore the HCA's PCI configuration headers */
	{
		if (is_tavor) {
			/* Restore the HCA's bridge configuration. */
			HCA_PRINT( TRACE_LEVEL_INFORMATION  ,HCA_DBG_PNP  ,("Restoring bridge PCI configuration \n"));
			status = __restore_pci_config( &brBusIfc, &brConfig, TRUE );
			if( !NT_SUCCESS( status ) ) {
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
					("Failed to restore bridge config. Card reset failed !\n"));
				goto resetErr3;
			}
		}
		
		/* Restore the HCA's configuration. */
		HCA_PRINT( TRACE_LEVEL_INFORMATION  ,HCA_DBG_PNP  ,("Restoring HCA PCI configuration \n"));
		status = __restore_pci_config( &hcaBusIfc, &hcaConfig, FALSE );
		if( !NT_SUCCESS( status ) ) {
			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
				("Failed to restore HCA config. Card reset failed !\n"));
		}
	}

resetErr3:
	if (is_tavor) 
		brBusIfc.InterfaceDereference( brBusIfc.Context );

resetErr2:
	hcaBusIfc.InterfaceDereference( hcaBusIfc.Context );

resetErr1:
resetExit:
	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


/*
 * Tunes PCI configuration as described in 13.3.2 in the Tavor PRM.
 */
NTSTATUS
hca_tune_pci(
	IN				DEVICE_OBJECT* const		pDevObj,
	OUT				uplink_info_t *p_uplink_info )
{
	NTSTATUS				status;
	PCI_COMMON_CONFIG		hcaConfig;
	BUS_INTERFACE_STANDARD	hcaBusIfc;
	ULONG					len;
	ULONG					capOffset;
	PCI_PCIX_CAPABILITY		*pPciXCap;
	PCI_PCIEXP_CAPABILITY	*pPciExpCap;

	HCA_ENTER( HCA_DBG_PNP );

	/* Get the HCA's bus interface. */
	status = __get_bus_ifc( pDevObj, &GUID_BUS_INTERFACE_STANDARD, &hcaBusIfc );
	if( !NT_SUCCESS( status ) )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_PNP ,("Failed to get HCA bus interface.\n"));
		return status;
	}

	/* Save the HCA's configuration. */
	status = __save_pci_config( &hcaBusIfc, &hcaConfig );
	if( !NT_SUCCESS( status ) )
	{
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
			("Failed to save HCA config.\n"));
		status = STATUS_UNSUCCESSFUL;
		goto tweakErr;
	}
	status = 0;

	/*
	*		PCIX Capability
	*/
	capOffset = __FindCapability( &hcaConfig, PCI_CAPABILITY_ID_PCIX );
	if( capOffset )
	{
		pPciXCap = (PCI_PCIX_CAPABILITY*)(((UCHAR*)&hcaConfig) + capOffset);

		/* fill uplink features */
		p_uplink_info->bus_type = UPLINK_BUS_PCIX;
		if (pPciXCap->Status & (1 << 17))
			p_uplink_info->u.pci_x.capabilities = UPLINK_BUS_PCIX_133;
		
		/* Update the command field to max the read byte count if needed. */
		if ( g_tune_pci && (pPciXCap->Command & 0x000C) != 0x000C )
		{
			HCA_PRINT( TRACE_LEVEL_WARNING, HCA_DBG_PNP,
				("Updating max recv byte count of PCI-X capability.\n"));
			pPciXCap->Command = (pPciXCap->Command & ~PCI_X_CMD_MAX_READ) | (3 << 2);
			len = hcaBusIfc.SetBusData( hcaBusIfc.Context, PCI_WHICHSPACE_CONFIG,
				&pPciXCap->Command,
				capOffset + offsetof( PCI_PCIX_CAPABILITY, Command),
				sizeof( pPciXCap->Command ) );
			if( len != sizeof( pPciXCap->Command ) )
			{
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
					("Failed to update PCI-X maximum read byte count.\n"));
				status = STATUS_UNSUCCESSFUL;
				goto tweakErr;
			}
		}
	}


	/*
	* 	PCI Express Capability
	*/
	capOffset = __FindCapability( &hcaConfig, PCI_CAPABILITY_ID_PCIEXP );
	if( capOffset )
	{
		pPciExpCap = (PCI_PCIEXP_CAPABILITY*)(((UCHAR*)&hcaConfig) + capOffset);

		/* fill uplink features */
		p_uplink_info->bus_type = UPLINK_BUS_PCIE;
		if ((pPciExpCap->LinkStatus & 15) == 1)
			p_uplink_info->u.pci_e.link_speed = UPLINK_BUS_PCIE_SDR;
		if ((pPciExpCap->LinkStatus & 15) == 2)
			p_uplink_info->u.pci_e.link_speed = UPLINK_BUS_PCIE_DDR;
		p_uplink_info->u.pci_e.link_width = (uint8_t)((pPciExpCap->LinkStatus >> 4) & 0x03f);
		p_uplink_info->u.pci_e.capabilities = (uint8_t)((pPciExpCap->LinkCapabilities >> 2) & 0xfc);
		p_uplink_info->u.pci_e.capabilities |= pPciExpCap->LinkCapabilities & 3;

		if (g_tune_pci) {
			/* Update Max_Read_Request_Size. */
			HCA_PRINT( TRACE_LEVEL_WARNING ,HCA_DBG_PNP,
				("Updating max recv byte count of PCI-Express capability.\n"));
			pPciExpCap->DevControl = (pPciExpCap->DevControl & ~PCI_EXP_DEVCTL_READRQ) | (5 << 12);
			len = hcaBusIfc.SetBusData( hcaBusIfc.Context, PCI_WHICHSPACE_CONFIG,
				&pPciExpCap->DevControl,
				capOffset + offsetof( PCI_PCIEXP_CAPABILITY, DevControl),
				sizeof( pPciExpCap->DevControl ) );
			if( len != sizeof( pPciExpCap->DevControl ) )
			{
				HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP, 
					("Failed to update PCI-Exp maximum read byte count.\n"));
				goto tweakErr;
			}
		}
	}


tweakErr:
	hcaBusIfc.InterfaceDereference( hcaBusIfc.Context );

	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


/* leo */

NTSTATUS
hca_enable_pci(
	IN		DEVICE_OBJECT* const		pDevObj,
	OUT		PBUS_INTERFACE_STANDARD	phcaBusIfc,
	OUT		PCI_COMMON_CONFIG*	pHcaConfig
	)
{
		NTSTATUS				status;
		ULONG 				len;
	
		HCA_ENTER( HCA_DBG_PNP );
	
		/* Get the HCA's bus interface. */
		status = __get_bus_ifc( pDevObj, &GUID_BUS_INTERFACE_STANDARD, phcaBusIfc );
		if( !NT_SUCCESS( status ) )
		{
			HCA_PRINT( TRACE_LEVEL_ERROR  , HCA_DBG_PNP  ,("Failed to get HCA bus interface.\n"));
			return STATUS_DEVICE_NOT_READY;
		}
	
		/* Save the HCA's configuration. */
		status = __save_pci_config( phcaBusIfc, pHcaConfig );
		if( !NT_SUCCESS( status ) )
		{
			HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_PNP,
				("Failed to save HCA config.\n"));
			goto pciErr;
		}

		/* fix command register (set PCI Master bit) */
		// NOTE: we change here the saved value of the command register
		pHcaConfig->Command |= 7;
		len = phcaBusIfc->SetBusData( phcaBusIfc->Context, PCI_WHICHSPACE_CONFIG,
			(PVOID)&pHcaConfig->Command , 4, sizeof(ULONG) );
		if( len != sizeof(ULONG) )
		{
			HCA_PRINT( TRACE_LEVEL_ERROR  ,HCA_DBG_PNP  ,("Failed to write command register.\n"));
			status = STATUS_DEVICE_NOT_READY;
			goto pciErr;
		}
		status = STATUS_SUCCESS;
		goto out;

	pciErr:
		phcaBusIfc->InterfaceDereference( phcaBusIfc->Context );
		phcaBusIfc->InterfaceDereference = NULL;
	out:
		HCA_EXIT( HCA_DBG_PNP );
		return status;
}

void hca_disable_pci(PBUS_INTERFACE_STANDARD	phcaBusIfc)
{
	// no need to disable the card, so just release the PCI bus i/f
	if (phcaBusIfc->InterfaceDereference) {
		phcaBusIfc->InterfaceDereference( phcaBusIfc->Context );
		phcaBusIfc->InterfaceDereference = NULL;
	}
}

