
#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "pci.tmh"
#endif

#include <complib/cl_thread.h>
#include <initguid.h>
#include <wdmguid.h>

#define MLX4_RESET_BASE     0xf0000
#define MLX4_RESET_SIZE       0x400
#define MLX4_SEM_OFFSET       0x3fc
#define MLX4_RESET_OFFSET      0x10
#define MLX4_RESET_VALUE    swab32(1)

#define MLX4_OWNER_BASE     0x8069c
#define MLX4_OWNER_SIZE     4


#define MLX4_SEM_TIMEOUT_JIFFIES_MS 10000   /* 10 sec */

//#define PCI_CAPABILITY_ID_VPD             0x03
//#define PCI_CAPABILITY_ID_PCIX                0x07
//#define PCI_CAPABILITY_ID_PCI_EXPRESS         0x10
//#define PCI_CAPABILITY_ID_MSIX            0x11

/*
 * MSI-X Capability
 */
typedef struct _PCI_MSIX_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;

    USHORT      Flags;
    ULONG       Table_Offset;
    ULONG       PBA_Offset;
    
} PCI_MSIX_CAPABILITY, *PPCI_MSIX_CAPABILITY;

#define MSIX_FLAGS_MSIX_ENABLE(flags)           (((flags)>>15)&1)   /* MSI-X is enabled */
#define MSIX_FLAGS_MSIX_FUNCTION_MASK(flags)    (((flags)>>14)&1)   /* all interrupts masked */
#define MSIX_FLAGS_SUPPORTED(flags)             ((flags)&0x07ff)    /* vector table size */
#define MSIX_OFFSET_BIR(offset)                 ((offset)&7)        /* BAR index register */
#define MSIX_OFFSET_ADDR(offset)                ((offset)&0xfffffff8)       /* offset */

/* this structure describes one MSI-X vector from N */
typedef struct _PCI_MSIX_VECTOR {

    ULONGLONG   Addr;
    ULONG       Data;
    ULONG       Flags;
    
} PCI_MSIX_VECTOR, *PPCI_MSIX_VECTOR;

#define MSIX_VECTOR_MASKED(flags)           ((flags)&1) /* this vector is masked */

/* this structure pending state of 64 MSI-X vectors from N */
typedef struct _PCI_MSIX_PENDING {

    ULONGLONG   Mask;
    
} PCI_MSIX_PENDING, *PPCI_MSIX_PENDING;

/*
 * Vital Product Data Capability
 */
typedef struct _PCI_VPD_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;

    USHORT      Flags;
    ULONG           Data;

} PCI_VPD_CAPABILITY, *PPCI_VPD_CAPABILITY;


/*
 * PCI-X Capability
 */
typedef struct _PCI_PCIX_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;

    USHORT      Command;
    ULONG           Status;

/* for Command: */
} PCI_PCIX_CAPABILITY, *PPCI_PCIX_CAPABILITY;

#define  PCI_X_CMD_MAX_READ     0x000c  /* Max Memory Read Byte Count */

/*
 * PCI-Express Capability
 */
typedef struct _PCI_PCIEXP_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;

    USHORT      Flags;
    ULONG           DevCapabilities;
    USHORT      DevControl;
    USHORT      DevStatus;
    ULONG           LinkCapabilities;
    USHORT      LinkControl;
    USHORT      LinkStatus;
    ULONG           SlotCapabilities;
    USHORT      SlotControl;
    USHORT      SlotStatus;
    USHORT      RootControl;
    USHORT      RootCapabilities;
    USHORT      RootStatus;
} PCI_PCIEXP_CAPABILITY, *PPCI_PCIEXP_CAPABILITY;

/* for DevControl: */
#define  PCI_EXP_DEVCTL_READRQ  0x7000  /* Max_Read_Request_Size */

static NTSTATUS
__get_bus_ifc(
    IN              DEVICE_OBJECT* const        pDevObj,
    IN      const   GUID* const                 pGuid,
        OUT         BUS_INTERFACE_STANDARD      *pBusIfc );

static NTSTATUS
__restore_pci_config(
    IN              BUS_INTERFACE_STANDARD      *pBusIfc,
    IN              PCI_COMMON_CONFIG* const    pConfig );

extern
int mlx4_QUERY_FW(struct mlx4_dev *dev);

/*
 * Returns the offset in configuration space of the PCI-X capabilites.
 */
ULONG
__find_capability(
    IN              PCI_COMMON_CONFIG* const    pConfig,  
    IN              char cap_id
    )
{
    ULONG                       offset = 0;
    PCI_CAPABILITIES_HEADER     *pHdr = NULL;
    UCHAR                       *pBuf = (UCHAR*)pConfig;
    UCHAR                       type = PCI_CONFIGURATION_TYPE(pConfig);

    MLX4_ENTER( MLX4_DBG_PNP );

    if  ( type == PCI_DEVICE_TYPE ) {
        if( pConfig->u.type0.CapabilitiesPtr )
        {
            pHdr = (PCI_CAPABILITIES_HEADER*)
                (pBuf + pConfig->u.type0.CapabilitiesPtr);
        }
    }

    if  ( type == PCI_BRIDGE_TYPE ) {
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

    MLX4_EXIT( MLX4_DBG_PNP );
    return offset;
}

static NTSTATUS
__restore_msix_capability(
    IN              struct pci_dev *pdev
    )
{
    int offset;
    NTSTATUS status = STATUS_SUCCESS;
    PCI_MSIX_CAPABILITY     *pPciMsix;
    PBUS_INTERFACE_STANDARD p_ifc = &pdev->bus_pci_ifc;
    PCI_COMMON_CONFIG* p_cfg = &pdev->pci_cfg_space;

    MLX4_ENTER( MLX4_DBG_PNP );

    /* restore MSI-X Capability */
    offset = __find_capability( p_cfg, PCI_CAPABILITY_ID_MSIX );
    pPciMsix = (PCI_MSIX_CAPABILITY*)(((UCHAR*)p_cfg) + offset);
    if (offset) { 
        /* restore MSI-X control register */
        if ( sizeof( pPciMsix->Flags) != p_ifc->SetBusData( 
            p_ifc->Context, PCI_WHICHSPACE_CONFIG,
            &pPciMsix->Flags,   offset + 
            offsetof( PCI_MSIX_CAPABILITY, Flags),
            sizeof( pPciMsix->Flags ) )) {
            MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
                ("%s: Couldn't restore MSI-X Control register, aborting.\n", pdev->name));
            status = STATUS_UNSUCCESSFUL;
            goto end;
        }
    }

end:    
    MLX4_EXIT( MLX4_DBG_PNP );
    return status;
}

static NTSTATUS
__pci_restore_msix_info(
    IN              struct pci_dev *pdev,
    struct msix_saved_info *        p_info
    )
{
    int i;
    NTSTATUS status = STATUS_SUCCESS;
    PPCI_MSIX_VECTOR p_cvector, p_svector;

    MLX4_ENTER( MLX4_DBG_PNP );

    if ( p_info->valid ) {
        /* restore PBA Table */
        p_info->valid = 0;
        memcpy( p_info->mca, p_info->msa, p_info->msz );
        kfree( p_info->msa );
        MmUnmapIoSpace( p_info->mca, p_info->msz );

        /* restore Vector Table */
        p_svector = (PPCI_MSIX_VECTOR)p_info->vsa;
        p_cvector = (PPCI_MSIX_VECTOR)p_info->vca;
        for (i=0; i<p_info->num; i++) 
            p_cvector[i].Flags = p_svector[i].Flags;
        kfree( p_info->vsa );
        MmUnmapIoSpace( p_info->vca, p_info->vsz );

        /* restore MSI-X Capability */
		__restore_msix_capability(pdev);

    }

    MLX4_EXIT( MLX4_DBG_PNP );
    return status;
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
    IN              BUS_INTERFACE_STANDARD      *pBusIfc,
    IN              PCI_COMMON_CONFIG* const    pConfig )
{
    NTSTATUS status = STATUS_SUCCESS;
    int     i, *pci_hdr = (int*)pConfig;

    MLX4_ENTER( MLX4_DBG_PNP );

    /* restore capabilities*/
    {
        int offset;
        PCI_PCIEXP_CAPABILITY   *pPciExpCap;

        /* PCI-X */
        offset = __find_capability( pConfig, PCI_CAPABILITY_ID_PCIX );
        if (offset) {
            if ( 4 != pBusIfc->SetBusData( pBusIfc->Context, PCI_WHICHSPACE_CONFIG,
                &pci_hdr[offset/4], offset, 4) ) {
                MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
                    ("Couldn't restore HCA PCI-X command register, aborting.\n"));
                status = STATUS_UNSUCCESSFUL;
                goto out;
            }
        }

        /* PCI-Express */
        offset = __find_capability( pConfig, PCI_CAPABILITY_ID_PCI_EXPRESS );
        pPciExpCap = (PCI_PCIEXP_CAPABILITY*)(((UCHAR*)pConfig) + offset);
        if (offset) {
            /* restore HCA PCI Express Device Control register */
            if ( sizeof( pPciExpCap->DevControl ) != pBusIfc->SetBusData( 
                pBusIfc->Context, PCI_WHICHSPACE_CONFIG,
                &pPciExpCap->DevControl,    offset + 
                offsetof( PCI_PCIEXP_CAPABILITY, DevControl),
                sizeof( pPciExpCap->DevControl ) )) {
                MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
                    ("Couldn't restore HCA PCI Express Device Control register, aborting.\n"));
                status = STATUS_UNSUCCESSFUL;
                goto out;
            }
            /* restore HCA PCI Express Link Control register */
            if ( sizeof( pPciExpCap->LinkControl ) != pBusIfc->SetBusData( 
                pBusIfc->Context, PCI_WHICHSPACE_CONFIG,
                &pPciExpCap->LinkControl,   offset + 
                offsetof( PCI_PCIEXP_CAPABILITY, LinkControl),
                sizeof( pPciExpCap->LinkControl ) )) {
                MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
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
            MLX4_PRINT( TRACE_LEVEL_ERROR ,MLX4_DBG_PNP ,
                ("Couldn't restore PCI cfg reg %x,   aborting.\n", i));
            status =  STATUS_DEVICE_NOT_READY;
            goto out;
        }
    }

    /* Write the command register. */
    if (4 != pBusIfc->SetBusData( pBusIfc->Context,
        PCI_WHICHSPACE_CONFIG, &pci_hdr[1], 4, 4 )) {
        MLX4_PRINT( TRACE_LEVEL_ERROR ,MLX4_DBG_PNP ,("Couldn't restore COMMAND.\n"));
        status =  STATUS_DEVICE_NOT_READY;
    }

out:    
    MLX4_EXIT( MLX4_DBG_PNP );
    return status;
}

static int
__save_msix_info(
    IN              PCI_COMMON_CONFIG *     p_cfg,
    OUT             struct msix_saved_info *p_info )
{
    u64 bar;
    int n_supported;
    PHYSICAL_ADDRESS pa;
    ULONG cap_offset, bir;
    PPCI_MSIX_CAPABILITY pPciMsixCap;

    MLX4_ENTER( MLX4_DBG_PNP );

    /* find capability */
    cap_offset = __find_capability( p_cfg, PCI_CAPABILITY_ID_MSIX );
    if( !cap_offset ) {
        p_info->valid = 0;
        return 0;
    }   
    pPciMsixCap = (PPCI_MSIX_CAPABILITY)(((UCHAR*)p_cfg) + cap_offset);
    n_supported = MSIX_FLAGS_SUPPORTED(pPciMsixCap->Flags) + 1;
    p_info->num = n_supported;

    /* map memory for vectors */
    p_info->vsz =(ULONG)(n_supported * sizeof(PCI_MSIX_VECTOR));
    bir = MSIX_OFFSET_BIR(pPciMsixCap->Table_Offset);
    bar = *(u64*)&p_cfg->u.type1.BaseAddresses[bir] & ~0x0f;
    pa.QuadPart = bar + MSIX_OFFSET_ADDR(pPciMsixCap->Table_Offset);
    p_info->vca = MmMapIoSpace( pa, p_info->vsz, MmNonCached ); 
    if ( p_info->vca == NULL) {
        MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
            ("Failed kernel mapping of MSI-X vector table.\n") );
        goto end;
    }

	/* alloc memory for vector table */
    p_info->vsa = kmalloc(p_info->vsz, GFP_KERNEL);
	if ( !p_info->vsa ) {
		MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
			("Failed allocate memory for MSI-X vector table \n") );
		goto err_alloc_vectors;
	}
	memcpy( p_info->vsa, p_info->vca, p_info->vsz );

    /* map memory for mask table */
    bir = MSIX_OFFSET_BIR(pPciMsixCap->PBA_Offset);
    bar = *(u64*)&p_cfg->u.type1.BaseAddresses[bir] & ~0x0f;
    p_info->msz =(ULONG)(((n_supported +63) / 64) * sizeof(PCI_MSIX_PENDING) );
    pa.QuadPart = bar + MSIX_OFFSET_ADDR(pPciMsixCap->PBA_Offset);
    p_info->mca = MmMapIoSpace( pa, p_info->msz, MmNonCached ); 
    if ( p_info->mca == NULL) {
        MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
            ("Failed kernel mapping of MSI-X mask table.\n") );
        goto err_map_masks;
    }

	/* alloc memory for mask table */
    p_info->msa = kmalloc(p_info->msz, GFP_KERNEL);
	if ( !p_info->msa ) {
		MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
			("Failed allocate memory for MSI-X vector table \n") );
		goto err_alloc_masks;
	}
	memcpy( p_info->msa, p_info->mca, p_info->msz );

    p_info->valid = 1;
    return 0;

err_alloc_masks:
    MmUnmapIoSpace( p_info->mca, p_info->msz );
    p_info->mca = NULL;

err_map_masks:
    kfree(p_info->vsa);
    p_info->vsa = NULL;

err_alloc_vectors:
    MmUnmapIoSpace( p_info->vca, p_info->vsz );
    p_info->vca = NULL;

end:    
    MLX4_EXIT( MLX4_DBG_PNP );
    return -EFAULT;
}

void
pci_free_msix_info_resources(
    IN struct msix_saved_info *     pMsixInfo
    )
{
    if (pMsixInfo->vca && pMsixInfo->vsz)
        MmUnmapIoSpace( pMsixInfo->vca, pMsixInfo->vsz );

    if (pMsixInfo->mca && pMsixInfo->msz)
        MmUnmapIoSpace( pMsixInfo->mca, pMsixInfo->msz );

    if (pMsixInfo->msa)
        kfree(pMsixInfo->msa);

    if (pMsixInfo->vsa)
        kfree(pMsixInfo->vsa);

    memset(pMsixInfo,0,sizeof(struct msix_saved_info));
}

/*
 * Reads and saves the PCI configuration of the device accessible
 * through the provided bus interface.  Does not read registers 22 or 23
 * as directed in Tavor PRM 1.0.1, Appendix A. InfiniHost Software Reset.
 */
NTSTATUS
pci_save_config(
    IN              BUS_INTERFACE_STANDARD      *pBusIfc,
        OUT         PCI_COMMON_CONFIG* const    pConfig )
{
    ULONG                   len;
    UINT32                  *pBuf;

    MLX4_ENTER( MLX4_DBG_PNP );
    
    pBuf = (UINT32*)pConfig;

    /*
     * Read the lower portion of the configuration, up to but excluding
     * register 22.
     */
    len = pBusIfc->GetBusData(
        pBusIfc->Context, PCI_WHICHSPACE_CONFIG, &pBuf[0], 0, 88 );
    if( len != 88 )
    {
        MLX4_PRINT( TRACE_LEVEL_ERROR  , MLX4_DBG_PNP  ,("Failed to read HCA config.\n"));
        return STATUS_DEVICE_NOT_READY;
    }

    /* Read the upper portion of the configuration, from register 24. */
    len = pBusIfc->GetBusData(
        pBusIfc->Context, PCI_WHICHSPACE_CONFIG, &pBuf[24], 96, 160 );
    if( len != 160 )
    {
        MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_PNP  ,("Failed to read HCA config.\n"));
        return STATUS_DEVICE_NOT_READY;
    }

    MLX4_EXIT( MLX4_DBG_PNP );
    return STATUS_SUCCESS;
}


/*
 * Tunes PCI configuration as described in 13.3.2 in the Tavor PRM.
 */
void
pci_get_uplink_info(
    IN              struct pci_dev *        pdev,
    IN              PCI_COMMON_CONFIG *     p_cfg,
    OUT             uplink_info_t *         p_uplink_info )
{
    ULONG                   cap_offset;
    PCI_PCIX_CAPABILITY     *pPciXCap;
    PCI_PCIEXP_CAPABILITY   *pPciExpCap;

    MLX4_ENTER( MLX4_DBG_PNP );

    // PCIX Capability
    cap_offset = __find_capability( p_cfg, PCI_CAPABILITY_ID_PCIX );
    if( cap_offset ) {
        pPciXCap = (PCI_PCIX_CAPABILITY*)(((UCHAR*)p_cfg) + cap_offset);

        p_uplink_info->bus_type = UPLINK_BUS_PCIX;
        if (pPciXCap->Status & (1 << 17))
            p_uplink_info->u.pci_x.capabilities = UPLINK_BUS_PCIX_133;
        
    }

    // PCI Express Capability
    cap_offset = __find_capability( p_cfg, PCI_CAPABILITY_ID_PCI_EXPRESS );
    if( cap_offset ) {
        pPciExpCap = (PCI_PCIEXP_CAPABILITY*)(((UCHAR*)p_cfg) + cap_offset);

        p_uplink_info->bus_type = UPLINK_BUS_PCIE;
        if ((pPciExpCap->LinkStatus & 15) == 1)
            p_uplink_info->u.pci_e.link_speed = UPLINK_BUS_PCIE_SDR;
        if ((pPciExpCap->LinkStatus & 15) == 2)
            p_uplink_info->u.pci_e.link_speed = UPLINK_BUS_PCIE_DDR;
        p_uplink_info->u.pci_e.link_width = (uint8_t)((pPciExpCap->LinkStatus >> 4) & 0x03f);
        p_uplink_info->u.pci_e.capabilities = (uint8_t)((pPciExpCap->LinkCapabilities >> 2) & 0xfc);
        p_uplink_info->u.pci_e.capabilities |= pPciExpCap->LinkCapabilities & 3;
    }

    // PCI MSI-X Capability
    memset( &p_uplink_info->x, 0, sizeof(p_uplink_info->x) );
    cap_offset = __find_capability( p_cfg, PCI_CAPABILITY_ID_MSIX );
    if( cap_offset ) {
        PVOID ka;
        ULONG sz;
        PHYSICAL_ADDRESS pa;
        PPCI_MSIX_VECTOR p_vector;
        PPCI_MSIX_PENDING p_pend;
        ULONG granted_mask = 0;
        PPCI_MSIX_CAPABILITY pPciMsixCap = (PPCI_MSIX_CAPABILITY)(((UCHAR*)p_cfg) + cap_offset);
        USHORT flags = pPciMsixCap->Flags;
        ULONG table_bir = MSIX_OFFSET_BIR(pPciMsixCap->Table_Offset);
        ULONG pend_bir = MSIX_OFFSET_BIR(pPciMsixCap->PBA_Offset);
        u64 table_bar = *(u64*)&p_cfg->u.type1.BaseAddresses[table_bir] & ~0x0f;
        u64 pend_bar = *(u64*)&p_cfg->u.type1.BaseAddresses[pend_bir] & ~0x0f;
        int i, n_supported = MSIX_FLAGS_SUPPORTED(flags) + 1;

        /* print capabilities structure */
        MLX4_PRINT( TRACE_LEVEL_WARNING  ,MLX4_DBG_MSIX  ,
            ("MSI-X Capability: Enabled - %d, Function Masked %d, Vectors Supported %d, Addr_Offset(BIR) %#x(%d), Pend_Offset(BIR) %#x(%d)\n",
            MSIX_FLAGS_MSIX_ENABLE(flags),
            MSIX_FLAGS_MSIX_FUNCTION_MASK(flags),
            n_supported, 
            MSIX_OFFSET_ADDR(pPciMsixCap->Table_Offset), MSIX_OFFSET_BIR(pPciMsixCap->Table_Offset), 
            MSIX_OFFSET_ADDR(pPciMsixCap->PBA_Offset), MSIX_OFFSET_BIR(pPciMsixCap->PBA_Offset) ));

        /* fill info */
        p_uplink_info->x.valid = 1;
        p_uplink_info->x.enabled	= MSIX_FLAGS_MSIX_ENABLE(flags);
        p_uplink_info->x.masked = MSIX_FLAGS_MSIX_FUNCTION_MASK(flags);
        p_uplink_info->x.requested = n_supported;

		pdev->n_msi_vectors_sup = p_uplink_info->x.enabled ? n_supported : 0;
        if (pdev->n_msi_vectors_alloc) {

            /* map memory */
            sz =(ULONG)(n_supported * sizeof(PCI_MSIX_VECTOR));
            pa.QuadPart = table_bar + MSIX_OFFSET_ADDR(pPciMsixCap->Table_Offset);
            ka = MmMapIoSpace( pa, sz, MmNonCached ); 
            if ( ka == NULL) {
                MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
                    ("Failed kernel mapping of MSI-X vector table.\n") );
                goto end;
            }
            
            p_vector = (PPCI_MSIX_VECTOR)ka;
            /* print (allocated+2) vectors */
            for (i=0; i<pdev->n_msi_vectors_alloc+2; i++) {
                MLX4_PRINT( TRACE_LEVEL_WARNING  ,MLX4_DBG_MSIX  ,
                    ("MSI-X Vectors: Id %d, Masked %d, Addr %#I64x, Data %#x\n",
                    i, MSIX_VECTOR_MASKED(p_vector[i].Flags),
                    p_vector[i].Addr, p_vector[i].Data ));
            }

            p_uplink_info->x.granted = pdev->n_msi_vectors_alloc;
            p_uplink_info->x.granted_mask = granted_mask;
            MmUnmapIoSpace( ka, sz );

            /* map memory */
            sz =(ULONG)(((n_supported +63) / 64) * sizeof(PCI_MSIX_PENDING) );
            pa.QuadPart = pend_bar + MSIX_OFFSET_ADDR(pPciMsixCap->PBA_Offset);
            ka = MmMapIoSpace( pa, sz, MmNonCached ); 
            if ( ka == NULL) {
                MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
                    ("Failed kernel mapping of MSI-X mask table.\n") );
                goto end;
            }

            /* print first pending register (64 vectors) */
            p_pend = (PPCI_MSIX_PENDING)ka;
            for (i=0; i<1; i++) {
                MLX4_PRINT( TRACE_LEVEL_WARNING  ,MLX4_DBG_MSIX  ,
                    ("MSI-X Pending: Id %d, Pend %#I64x\n", i, p_pend[i].Mask ));
            }
            p_uplink_info->x.pending_mask = *(u32*)&p_pend[0].Mask;
            MmUnmapIoSpace( ka, sz );
        }
        else {
            MLX4_PRINT( TRACE_LEVEL_WARNING  ,MLX4_DBG_MSIX  ,
                ("MSI-X Vectors: Allocated 0 vectors\n") );
        }

    }
    else { 
        MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_PNP    ,
            ("Failed to find MSI-X capability (cap id %d)\n", PCI_CAPABILITY_ID_MSIX) );
    }
end:
    MLX4_EXIT( MLX4_DBG_PNP );
}


/*
 * Store Card's PCI Config space and print current MSI-X capabilities
 */
void
pci_get_msi_info(
    IN              struct pci_dev          *pdev,
        OUT         PCI_COMMON_CONFIG *     p_cfg,
        OUT         uplink_info_t *         p_uplink_info )
{
    ULONG                   cap_offset;

    MLX4_ENTER( MLX4_DBG_PNP );

    // PCI MSI-X Capability
    memset( &p_uplink_info->x, 0, sizeof(p_uplink_info->x) );
    cap_offset = __find_capability( p_cfg, PCI_CAPABILITY_ID_MSIX );
    if( cap_offset ) {
        PVOID ka;
        ULONG sz;
        PHYSICAL_ADDRESS pa;
        PPCI_MSIX_VECTOR p_vector;
        PPCI_MSIX_PENDING p_pend;
        ULONG granted_mask = 0;
        PPCI_MSIX_CAPABILITY pPciMsixCap = (PPCI_MSIX_CAPABILITY)(((UCHAR*)p_cfg) + cap_offset);
        USHORT flags = pPciMsixCap->Flags;
        ULONG table_bir = MSIX_OFFSET_BIR(pPciMsixCap->Table_Offset);
        ULONG pend_bir = MSIX_OFFSET_BIR(pPciMsixCap->PBA_Offset);
        u64 table_bar = *(u64*)&p_cfg->u.type1.BaseAddresses[table_bir] & ~0x0f;
        u64 pend_bar = *(u64*)&p_cfg->u.type1.BaseAddresses[pend_bir] & ~0x0f;
        int i, n_supported = MSIX_FLAGS_SUPPORTED(flags) + 1;

        /* print capabilities structure */
        MLX4_PRINT( TRACE_LEVEL_WARNING  ,MLX4_DBG_MSIX  ,
            ("MSI-X Capability: Enabled - %d, Function Masked %d, Vectors Supported %d, Addr_Offset(BIR) %#x(%d), Pend_Offset(BIR) %#x(%d)\n",
            MSIX_FLAGS_MSIX_ENABLE(flags),
            MSIX_FLAGS_MSIX_FUNCTION_MASK(flags),
            n_supported, 
            MSIX_OFFSET_ADDR(pPciMsixCap->Table_Offset), MSIX_OFFSET_BIR(pPciMsixCap->Table_Offset), 
            MSIX_OFFSET_ADDR(pPciMsixCap->PBA_Offset), MSIX_OFFSET_BIR(pPciMsixCap->PBA_Offset) ));

        /* fill info */
        p_uplink_info->x.valid = 1;
        p_uplink_info->x.enabled= MSIX_FLAGS_MSIX_ENABLE(flags);
        p_uplink_info->x.masked = MSIX_FLAGS_MSIX_FUNCTION_MASK(flags);
        p_uplink_info->x.requested = n_supported;

        if (pdev->n_msi_vectors_alloc) {

            /* map memory */
            sz =(ULONG)(n_supported * sizeof(PCI_MSIX_VECTOR));
            pa.QuadPart = table_bar + MSIX_OFFSET_ADDR(pPciMsixCap->Table_Offset);
            ka = MmMapIoSpace( pa, sz, MmNonCached ); 
            if ( ka == NULL) {
                MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
                    ("Failed kernel mapping of MSI-X vector table.\n") );
                goto end;
            }
            
            p_vector = (PPCI_MSIX_VECTOR)ka;
            /* print (allocated+2) vectors */
            for (i=0; i<pdev->n_msi_vectors_alloc+2; i++) {
                MLX4_PRINT( TRACE_LEVEL_WARNING  ,MLX4_DBG_MSIX  ,
                    ("MSI-X Vectors: Id %d, Masked %d, Addr %#I64x, Data %#x\n",
                    i, MSIX_VECTOR_MASKED(p_vector[i].Flags),
                    p_vector[i].Addr, p_vector[i].Data ));
            }

            p_uplink_info->x.granted = pdev->n_msi_vectors_alloc;
            p_uplink_info->x.granted_mask = granted_mask;
            MmUnmapIoSpace( ka, sz );

            /* map memory */
            sz =(ULONG)(((n_supported +63) / 64) * sizeof(PCI_MSIX_PENDING) );
            pa.QuadPart = pend_bar + MSIX_OFFSET_ADDR(pPciMsixCap->PBA_Offset);
            ka = MmMapIoSpace( pa, sz, MmNonCached ); 
            if ( ka == NULL) {
                MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
                    ("Failed kernel mapping of MSI-X mask table.\n") );
                goto end;
            }

            /* print first pending register (64 vectors) */
            p_pend = (PPCI_MSIX_PENDING)ka;
            for (i=0; i<1; i++) {
                MLX4_PRINT( TRACE_LEVEL_WARNING  ,MLX4_DBG_MSIX  ,
                    ("MSI-X Pending: Id %d, Pend %#I64x\n", i, p_pend[i].Mask ));
            }
            p_uplink_info->x.pending_mask = *(u32*)&p_pend[0].Mask;
            MmUnmapIoSpace( ka, sz );
        }
        else {
            MLX4_PRINT( TRACE_LEVEL_WARNING  ,MLX4_DBG_MSIX  ,
                ("MSI-X Vectors: Allocated 0 vectors\n") );
        }

    }
    else { 
        MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_PNP    ,
            ("Failed to find MSI-X capability (cap id %d)\n", PCI_CAPABILITY_ID_MSIX) );
    }

end:    
    MLX4_EXIT( MLX4_DBG_PNP );
}

NTSTATUS
pci_hca_reset( 
    IN      struct pci_dev *pdev
)
{
    u32                         sem;
    NTSTATUS                    status = STATUS_SUCCESS, status1;
    PBUS_INTERFACE_STANDARD     p_ifc = &pdev->bus_pci_ifc;
    PCI_COMMON_CONFIG*          p_cfg = &pdev->pci_cfg_space;
    struct msix_saved_info      msix_info;
    ULONG                       len;
    uint64_t t0 = cl_get_time_stamp(), t1, t2, t3, t4, t5, t6, t7, t8;

    MLX4_ENTER( MLX4_DBG_PNP );

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    status = pci_save_config( &pdev->bus_pci_ifc, p_cfg );
    if (!NT_SUCCESS(status)) {
        MLX4_PRINT(TRACE_LEVEL_ERROR  , MLX4_DBG_PNP,
            ("Failed to read PCI configuration of the card.\n") );
           return STATUS_DEVICE_NOT_READY;
    }
	
    /* save Card Config Space including MSI-X capabilities */
    if ( g.mode_flags & MLX4_MODE_PRINT_MSIX_INFO ) 
        pci_get_msi_info( pdev, p_cfg, &pdev->uplink_info );

    t5 = t4 = t3 = t2 = t1 = cl_get_time_stamp();
    /* save MSI-X info, if any */
    if ( pdev->dev->rev_id < 0xb0 || (g.mode_flags & MLX4_MODE_RESTORE_MSIX)) {
        len = __save_msix_info( p_cfg, &msix_info );
        if (len) {
            MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_PNP  ,("Failed to save MSI-X config.\n"));
            return STATUS_DEVICE_NOT_READY;
        }
    }
    t2 = cl_get_time_stamp();

    /* reset the card */    
    MLX4_PRINT( TRACE_LEVEL_WARNING ,MLX4_DBG_PNP , ("\nResetting HCA ... \n\n"));
    {
        u64                         end, start;
        PUCHAR  p_reset;
        PHYSICAL_ADDRESS  pa;
        int cnt = 0;

        /* map reset register */
        pa.QuadPart = pdev->bar[HCA_BAR_TYPE_HCR].phys + (uint64_t)MLX4_RESET_BASE;
        p_reset = (PUCHAR)MmMapIoSpace( pa, MLX4_RESET_SIZE, MmNonCached );
        MLX4_PRINT( TRACE_LEVEL_INFORMATION ,MLX4_DBG_PNP  ,
            ("Reset area ia mapped from pa 0x%I64x to va %p, size %#x\n", 
            pa.QuadPart, p_reset, MLX4_RESET_SIZE));
        if( !p_reset ) {
            MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_PNP  ,("Failed to map reset register with address 0x%I64x\n", pa.QuadPart));
            status = STATUS_UNSUCCESSFUL;
            goto err;
        }

        t3 = cl_get_time_stamp();
        /* grab HW semaphore to lock out flash updates  f0014 - dev_id 00a00190 */
        start = jiffies;
        end = start + MLX4_SEM_TIMEOUT_JIFFIES_MS;
        MLX4_PRINT( TRACE_LEVEL_INFORMATION ,MLX4_DBG_PNP  ,
            ("Obtaining HW semaphore at %p till %I64d\n", p_reset + MLX4_SEM_OFFSET, end));
        do {
            sem = 1;
            sem = READ_REGISTER_ULONG(( ULONG *)(void*)(p_reset + MLX4_SEM_OFFSET));
            if (!sem)
                break;
        
            cl_thread_suspend(1);
            ++cnt;
        } while (time_before(jiffies, end));
        
        if (sem) {
            MLX4_PRINT( TRACE_LEVEL_ERROR ,MLX4_DBG_PNP  ,
                ("Failed to obtain HW semaphore in %d attemps during %I64d msecs, aborting\n",
                cnt, end - start));
            status = STATUS_UNSUCCESSFUL;
            MmUnmapIoSpace( p_reset, MLX4_RESET_SIZE );
            goto err;
        }
        t4 = cl_get_time_stamp();
        
        
        /* Issue the reset. */
        MLX4_PRINT( TRACE_LEVEL_INFORMATION ,MLX4_DBG_PNP  ,
            ("Resetting  the chip at %p with %#x...\n", p_reset + MLX4_RESET_OFFSET, MLX4_RESET_VALUE));
        WRITE_REGISTER_ULONG( (ULONG *)(void*)(p_reset + MLX4_RESET_OFFSET), MLX4_RESET_VALUE );

        /* unmap the reset register */
        MLX4_PRINT( TRACE_LEVEL_INFORMATION ,MLX4_DBG_PNP  ,("Unmapping reset register \n"));
        MmUnmapIoSpace( p_reset, MLX4_RESET_SIZE );

        /* Wait a 200 ms. */
        cl_thread_suspend( 200 );
    }

    /* Read the configuration register until it doesn't return 0xFFFFFFFF */
    {
        ULONG                   i, reset_failed = 1;
        USHORT                  vendor;
        MLX4_PRINT( TRACE_LEVEL_INFORMATION ,MLX4_DBG_PNP  ,("Read the configuration register \n"));
        for( i = 0; i < 500; i++ ) {
            vendor = 0xFFFF;
            if (2 != p_ifc->GetBusData( p_ifc->Context,
                PCI_WHICHSPACE_CONFIG, &vendor, 0, 2)) {
                MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
                    ("Failed to read device configuration data. Card reset failed !\n"));
                status = STATUS_UNSUCCESSFUL;
                break;
            }
            /* See if we got valid data. */
            if( vendor != 0xFFFF ) {
                reset_failed = 0;
                break;
            }
        
            cl_thread_suspend( 4 );
        }   

        if (reset_failed) {
            MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
                ("Doh! PCI device did not come back after reset!\n"));
            status = STATUS_UNSUCCESSFUL;
            goto err;
        }
    }

    t5 = cl_get_time_stamp();
    if ( pdev->dev->rev_id < 0xb0 || (g.mode_flags & MLX4_MODE_RESTORE_PCI_CFG)) {
        /* Restore the HCA's configuration. */
        MLX4_PRINT( TRACE_LEVEL_INFORMATION  ,MLX4_DBG_PNP  ,("Restoring HCA PCI configuration \n"));
        status = __restore_pci_config( p_ifc, p_cfg );
        if( !NT_SUCCESS( status ) ) {
            MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, 
                ("Failed to restore HCA config. Card reset failed !\n"));
            goto err;
        }
    }
    status = STATUS_SUCCESS;

err:
    t6 = cl_get_time_stamp();
    if ( pdev->dev->rev_id < 0xb0 || (g.mode_flags & MLX4_MODE_RESTORE_MSIX)) {
        /* restore MSI-X info after reset */
        status1 = __pci_restore_msix_info( pdev, &msix_info );
        status = (!status) ? status1 : status;  /* return the only or the first error */
    }
	t7 = cl_get_time_stamp();

    /* check, whether MSI-X capabilities have been restored */
    if ( g.mode_flags & MLX4_MODE_PRINT_MSIX_INFO ) 
        pci_get_msi_info( pdev, p_cfg, &pdev->uplink_info );

    if (pdev->msix_info.valid) 
        pci_free_msix_info_resources(&pdev->msix_info);

	int err = mlx4_cmd_init(pdev->dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to init command interface, aborting.\n", pdev->dev->pdev->name));
		goto err;
	}

	err = mlx4_QUERY_FW(pdev->dev);
	if(! err)
	{
		if ( pdev->dev->caps.fw_ver < FW_MIN_GOOD_SUPPORTED) 
		{
			/* restore MSI-X capability after reset */
	        status1 = __restore_msix_capability( pdev );
	        status = (!status) ? status1 : status;  /* return the only or the first error */
		}
	}
	else
	{
		if (err == -EACCES) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("Non-primary physical function at '%S, "
					   "running in slave mode.\n", pdev->location ));
//				WriteEventLogEntryData( dev->pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_QUERY_FW, 0, 0, 1,
//					L"%s", dev->pdev->location );
		}
		else {
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: QUERY_FW command failed, aborting.\n", pdev->name ));
			WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_QUERY_FW, 0, 0, 2,
				L"%S", pdev->name, L"%d", err );
		}
		status = STATUS_UNSUCCESSFUL;
	}
	
    MLX4_EXIT( MLX4_DBG_PNP );
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    t8 = cl_get_time_stamp();
    if( NT_SUCCESS( status ) ) {
        MLX4_PRINT( TRACE_LEVEL_WARNING ,MLX4_DBG_PNP , 
            ("HCA has been reset ! Time: start %d, MSIX_save %d, map %d, sem %d, reset %d, PCI_rest %d, MSIX_rest %d, end %d, total %d\n",
            (int)(t1-t0), (int)(t2-t1), (int)(t3-t2), (int)(t4-t3),
            (int)(t5-t4), (int)(t6-t5), (int)(t7-t6), (int)(t8-t7), (int)(t8-t0)));
    }
    return status;
}


NTSTATUS
pci_hca_enable(
    IN      PBUS_INTERFACE_STANDARD     p_ifc,
    IN      PCI_COMMON_CONFIG*          p_cfg
    )
{
        NTSTATUS            status = STATUS_SUCCESS;
        ULONG               len;
    
        MLX4_ENTER( MLX4_DBG_PNP );
    
        /* fix command register (set PCI Master bit) */
        // NOTE: we change here the saved value of the command register
        if ( (p_cfg->Command & 7) != 7 ) {
            p_cfg->Command |= 7;
            len = p_ifc->SetBusData( p_ifc->Context, PCI_WHICHSPACE_CONFIG,
                (PVOID)&p_cfg->Command , 4, sizeof(ULONG) );
            if( len != sizeof(ULONG) ) {
                MLX4_PRINT( TRACE_LEVEL_ERROR  ,MLX4_DBG_PNP  ,("Failed to write command register.\n"));
                status = STATUS_DEVICE_NOT_READY;
            }
        }

        MLX4_EXIT( MLX4_DBG_PNP );
        return status;
}


int mlx4_get_ownership(struct mlx4_dev *dev)
{
    void __iomem *owner;
    u32 ret;

    owner = ioremap(pci_resource_start(dev->pdev, 0) + MLX4_OWNER_BASE, MLX4_OWNER_SIZE, MmNonCached);
    if (!owner) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_PNP,("Failed to obtain ownership bit\n"));
        return -ENOMEM;
    }

    ret = readl(owner);
    return (int) !!ret;
}

void mlx4_free_ownership(struct mlx4_dev *dev)
{
    void __iomem *owner;
    LARGE_INTEGER wait_time;

    owner = ioremap(pci_resource_start(dev->pdev, 0) + MLX4_OWNER_BASE, MLX4_OWNER_SIZE, MmNonCached);
    if (!owner) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_PNP,( "Failed to obtain ownership bit\n"));
        return;
    }
    writel(0, owner);

    //
    //  Sleep for 1 sec
    //
    wait_time.QuadPart  = (-10)* (__int64)(HZ);
    KeDelayExecutionThread( KernelMode, FALSE, &wait_time );
}

