#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "vpd.tmh"
#endif

#define SMALL_RESOURCE_TAG(x) (*((u8 *)x)>>3 & 0xf)
#define LARGE_RESOURCE_TAG(x) (*((u8 *)x) & 0x7f)

#define IS_LARGE_RESOURCE(x) (*((u8 *)x) & 0x80)

#define VPD_TAG(x) (IS_LARGE_RESOURCE(x) ? LARGE_RESOURCE_TAG(x) : SMALL_RESOURCE_TAG(x))

#define LARGE_RESOURCE_SIZE(item) (3 + \
			((u16)(item->data.large_data.length_lsb) & 0xff) + \
			(((u16)(item->data.large_data.length_msb) & 0xff)<<8))

#define SMALL_RESOURCE_SIZE(item) (((u16)(item->type_tag) & 0x7) + 1);

enum {
	VPD_TAG_ID  = 0x02, /* String TAG  */
	VPD_TAG_R   = 0x10, /* VPD-R TAG   */
	VPD_TAG_W   = 0x11, /* VPD-W TAG   */
	VPD_TAG_END = 0x0f  /* END TAG     */
};

struct small_resource_data {
	u8 byte[1];
};

struct large_resource_data {
	u8 length_lsb;
	u8 length_msb;
	u8 byte[1];
};

union resource_data {
	struct small_resource_data small_data;
	struct large_resource_data large_data;
};

struct vpd_item {
	u8 type_tag;
	union resource_data data;
};


static NTSTATUS __read_vpd_dword(
    IN              BUS_INTERFACE_STANDARD      *pBusIfc,
    IN              ULONG                       cap_offset,
    IN              ULONG                       offset,
        OUT         UCHAR                       *p_data
    )
{
    #define MAX_TRIES   500 
    ULONG len, tries=0;
    USHORT addr = (USHORT)offset;

    /* write offset inside VPD data */
    len = pBusIfc->SetBusData( pBusIfc->Context, PCI_WHICHSPACE_CONFIG, &addr, cap_offset+2,2 );
    if ( len != 2 ) 
        goto err_write;

    /* wait for data to be put in the data register for (MAX_TRIES*2) usecs */
    while ( !(addr & 0x8000) && tries++ < MAX_TRIES ) {
        len = pBusIfc->GetBusData( pBusIfc->Context, PCI_WHICHSPACE_CONFIG, &addr, cap_offset+2, 2 );
        if ( len != 2 )
            goto err_read;
        cond_resched();
    } 

    if ( tries >= MAX_TRIES )
        goto err_to;
        
    /* get the tag value */
    len = pBusIfc->GetBusData( pBusIfc->Context, PCI_WHICHSPACE_CONFIG, p_data, cap_offset+4, 4 );
    if ( len != 4 )
        goto err_read;

    return STATUS_SUCCESS;
    
err_write:
    MLX4_PRINT( TRACE_LEVEL_ERROR  , MLX4_DBG_PNP  , 
        ("Failed to write HCA config. \n" )); 
    return STATUS_DEVICE_NOT_READY;

err_read:   
    MLX4_PRINT( TRACE_LEVEL_ERROR  , MLX4_DBG_PNP  , 
        ("Failed to read HCA config. \n" )); 
    return STATUS_DEVICE_NOT_READY;

err_to: 
    MLX4_PRINT( TRACE_LEVEL_ERROR  , MLX4_DBG_PNP  , 
        ("FW hasn't provided VPD data in %d usecs \n", 
        (g_cmd_interval.u.LowPart / (-10)) * MAX_TRIES )); 
    return STATUS_DEVICE_NOT_READY;
}


NTSTATUS pci_get_vpd(
    BUS_INTERFACE_STANDARD      *pBusIfc,
    PCI_COMMON_CONFIG* const    pConfig,
    UCHAR*                      *p_vpd,
    int*                        p_vpd_size
    )
{
	struct vpd_item *item;
	int prev_offset = -1;
	ULONG cap_offset;
	u16 offset = 0;
	u32 data = 0;
	int i;
    unsigned char *vpd = NULL;
    NTSTATUS status;

    *p_vpd = NULL;
    *p_vpd_size = 0;

    cap_offset = __find_capability( pConfig, PCI_CAPABILITY_ID_VPD );
    if( cap_offset == 0) {
        MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_PNP, ("VPD doesn't exist. Ignore it.\n"));
       return STATUS_SUCCESS;
    }

    // 
    // Decide the VPD size
    //
	while (VPD_TAG(&data) != VPD_TAG_END) {                     
        status = __read_vpd_dword( pBusIfc, cap_offset, offset, (UCHAR*)&data);
        if( !NT_SUCCESS( status ) ) {
            MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, ("Failed to read VPD dword at offset %d.\n", offset));
			return status;
        }

		if ((offset == 0) && (VPD_TAG(&data) != VPD_TAG_ID)) {
            MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, ("Invalid VPD in offset %d.\n", offset));
			return STATUS_UNSUCCESSFUL;
		}
        
		prev_offset = offset;
		item = (struct vpd_item *) &data;
		offset += IS_LARGE_RESOURCE(&data) ?
				LARGE_RESOURCE_SIZE(item) :
				SMALL_RESOURCE_SIZE(item);

		if (offset <= prev_offset || offset > (1<<12)) {
            MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, ("Invalid VPD in offset %d.\n", offset));
			return STATUS_UNSUCCESSFUL;
		}
	}

    ASSERT(offset != 0);

    vpd = (unsigned char *)kmalloc(offset, GFP_KERNEL);
    if (vpd == NULL ) {
        MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, ("Failed to allocate VPD buffer of size %d.\n", offset));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    
    for (i = 0; i < offset; i += 4) {
        status = __read_vpd_dword( pBusIfc, cap_offset, i, (vpd+i));
        if( !NT_SUCCESS( status ) ) {
            MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_PNP, ("Failed to read VPD dword at offset %d.\n", offset));
            kfree( vpd );
            return status;
        }
    }

    *p_vpd = vpd;
    *p_vpd_size = offset;
    
    return STATUS_SUCCESS;
}

