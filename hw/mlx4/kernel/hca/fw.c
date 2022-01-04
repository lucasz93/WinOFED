/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "fw.tmh"
#endif


/***********************************
Firmware Update definitions
***********************************/
#define PCI_CONF_ADDR	(0x00000058)
#define PCI_CONF_DATA	(0x0000005c)
#define FLASH_OFFSET	(0x000f01a4)
#define READ_BIT		(1<<29)
#define WRITE_BIT		(2<<29)
#define ADDR_MSK		(0x0007ffff)
#define CMD_MASK		(0xe0000000)
#define BANK_SHIFT		(19)
#define BANK_MASK		(0xfff80000)
#define MAX_FLASH_SIZE	(0x80000) // 512K

#define SEMAP63				(0xf03fc)
#define GPIO_DIR_L			(0xf008c)
#define GPIO_POL_L			(0xf0094)
#define GPIO_MOD_L			(0xf009c)
#define GPIO_DAT_L			(0xf0084)
#define GPIO_DATACLEAR_L	(0xf00d4)
#define GPIO_DATASET_L		(0xf00dc)

#define CPUMODE				(0xf0150)
#define CPUMODE_MSK			(0xc0000000UL)
#define CPUMODE_SHIFT		(30)

/* Definitions intended to become shared with UM. Later... */
#define FW_READ			0x00
#define FW_WRITE		0x01
#define FW_READ_CMD		0x08
#define FW_WRITE_CMD	0x09
#define FW_OPEN_IF		0xe7
#define FW_CLOSE_IF		0x7e

#define FW_SIGNATURE		(0x5a445a44)
#define FW_SECT_SIZE		(0x10000)

typedef struct Primary_Sector{
	uint32_t fi_addr;
	uint32_t fi_size;
	uint32_t signature;
	uint32_t fw_reserved[5];
	uint32_t vsd[56];
	uint32_t branch_to;
	uint32_t crc016;
} primary_sector_t;

static uint32_t old_dir;
static uint32_t old_pol;
static uint32_t old_mod;
static uint32_t old_dat;


static NTSTATUS
fw_access_pciconf (
		IN		BUS_INTERFACE_STANDARD		*p_BusInterface,
		IN		ULONG						op_flag,
		IN		PVOID						p_buffer,
		IN		ULONG						offset,
		IN		ULONG POINTER_ALIGNMENT		length )
{

	ULONG				bytes;	
	NTSTATUS			status = STATUS_SUCCESS;

	if( !p_buffer )
		return STATUS_INVALID_PARAMETER;

	if (p_BusInterface)
	{

		bytes = p_BusInterface->SetBusData(
						p_BusInterface->Context,
						PCI_WHICHSPACE_CONFIG,
						(PVOID)&offset,
						PCI_CONF_ADDR,
						sizeof(ULONG) );

		if( op_flag == 0 )
		{
			if ( bytes )
				bytes = p_BusInterface->GetBusData(
							p_BusInterface->Context,
							PCI_WHICHSPACE_CONFIG,
							p_buffer,
							PCI_CONF_DATA,
							length );
			if ( !bytes )
				status = STATUS_NOT_SUPPORTED;
		}

		else
		{
			if ( bytes )
				bytes = p_BusInterface->SetBusData(
							p_BusInterface->Context,
							PCI_WHICHSPACE_CONFIG,
							p_buffer,
							PCI_CONF_DATA,
							length);

			if ( !bytes )
				status = STATUS_NOT_SUPPORTED;
		}
	}
	return status;
}


static NTSTATUS
__map_crspace(
	IN		struct ib_ucontext *		p_uctx,
	IN		PVOID						p_buf,
	IN		ULONG						buf_size
	)
{
	NTSTATUS status;
	PMDL p_mdl;
	PVOID ua, ka;
	ULONG sz;
	PHCA_FDO_DEVICE_DATA p_fdo = p_uctx->device->x.p_fdo;
	map_crspace *p_res = (map_crspace *)p_buf;
	struct pci_dev *p_pdev = p_fdo->p_bus_ib_ifc->pdev;

	HCA_ENTER( HCA_DBG_PNP );

	// sanity checks
	if ( buf_size < sizeof *p_res || !p_buf ) {
		status = STATUS_INVALID_PARAMETER;
		goto err_invalid_params;
	}

	// map memory
	sz =(ULONG)p_pdev->bar[HCA_BAR_TYPE_HCR].size;
	if (!p_pdev->bar[HCA_BAR_TYPE_HCR].virt) {
		PHYSICAL_ADDRESS pa;
		pa.QuadPart = p_pdev->bar[HCA_BAR_TYPE_HCR].phys;
		ka = MmMapIoSpace( pa, sz, MmNonCached ); 
		if ( ka == NULL) {
			HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM,
				("No kernel mapping of CR space.\n") );
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto err_map_to_kernel;
		}
		p_pdev->bar[HCA_BAR_TYPE_HCR].virt = ka;
	}
	ka = p_pdev->bar[HCA_BAR_TYPE_HCR].virt;

	// prepare for mapping to user space 
	p_mdl = IoAllocateMdl( ka, sz, FALSE,FALSE,NULL);
	if (p_mdl == NULL) {
		HCA_PRINT(TRACE_LEVEL_ERROR  , HCA_DBG_SHIM, 
			("IoAllocateMdl failed.\n") );
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto err_alloc_mdl;
	}

	// fill MDL
	MmBuildMdlForNonPagedPool(p_mdl);
	
	// map the buffer into user space 
	__try
	{
		ua = MmMapLockedPagesSpecifyCache( p_mdl, UserMode, MmNonCached,
			NULL, FALSE, NormalPagePriority );
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR , HCA_DBG_SHIM,
			("MmMapLockedPagesSpecifyCache failed.\n") );
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto err_map_to_user;
	}
	
	// fill the results
	p_res->va = (uint64_t)(ULONG_PTR)ua;
	p_res->size = sz;

	// resource tracking
	p_uctx->x.p_mdl = p_mdl;
	p_uctx->x.va = ua;

#if 0	
	HCA_PRINT(TRACE_LEVEL_INFORMATION, HCA_DBG_SHIM,
		("MTHCA: __map_crspace succeeded with .ka %I64x, size %I64x va %I64x, size %x, pa %I64x \n",
		p_pdev->bar[HCA_BAR_TYPE_HCR].virt, p_pdev->bar[HCA_BAR_TYPE_HCR].size, 
		p_res->va, p_res->size, p_pdev->bar[HCA_BAR_TYPE_HCR].phys ));
#endif
	status = STATUS_SUCCESS;
	goto out;

err_map_to_user:
	IoFreeMdl( p_mdl );
err_alloc_mdl:
err_map_to_kernel:
err_invalid_params:	
out:	
	HCA_EXIT( HCA_DBG_PNP );
	return status;
}


static void
__unmap_crspace(
	IN		struct ib_ucontext *			p_uctx
	)
{
	HCA_ENTER( HCA_DBG_PNP );

	if (p_uctx->x.va && p_uctx->x.p_mdl) {
		MmUnmapLockedPages(p_uctx->x.va, p_uctx->x.p_mdl);
		IoFreeMdl( p_uctx->x.p_mdl );
		p_uctx->x.va = p_uctx->x.p_mdl = NULL;
		//NB: the unmap of IO space is being done in __UnmapHcaMemoryResources
	}

	HCA_EXIT( HCA_DBG_PNP );
}

static void
__open_fw_access(
	IN				struct ib_ucontext*			p_uctx,
	IN				PBUS_INTERFACE_STANDARD		p_bus_interface )
{
	if( !p_uctx->x.fw_if_open )
	{
		p_bus_interface->InterfaceReference( p_bus_interface->Context );
		p_uctx->x.fw_if_open = TRUE;
	}
}

static void 
__close_fw_access(
	IN		struct ib_ucontext *	p_uctx,
	IN		PBUS_INTERFACE_STANDARD	p_bus_interface
	)
{
	if (p_uctx->x.fw_if_open ) {
		p_bus_interface->InterfaceDereference((PVOID)p_bus_interface->Context);
		p_uctx->x.fw_if_open = FALSE;
	}
}


void
unmap_crspace_for_all( struct ib_ucontext *p_uctx )
{
	PHCA_FDO_DEVICE_DATA p_fdo = p_uctx->device->x.p_fdo;
	PBUS_INTERFACE_STANDARD    p_bus_interface = p_fdo->p_bus_pci_ifc;

	HCA_ENTER( HCA_DBG_PNP );

	mutex_lock( &p_uctx->x.mutex );
	__unmap_crspace( p_uctx);
	__close_fw_access(p_uctx, p_bus_interface);
	mutex_unlock( &p_uctx->x.mutex );

	HCA_EXIT( HCA_DBG_PNP );
}

static NTSTATUS
fw_flash_write_data (
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length )
{
	NTSTATUS		status;
	uint32_t		cnt = 0;
	uint32_t		lcl_data;

	if (!length)
		return IB_INVALID_PARAMETER;
	
	lcl_data = (*((uint32_t*)p_buffer) << 24);

	status = fw_access_pciconf(p_BusInterface, FW_WRITE , &lcl_data, FLASH_OFFSET+4, length );
	if ( status != STATUS_SUCCESS )
		return status;
	lcl_data = ( WRITE_BIT | (offset & ADDR_MSK));
		
	status = fw_access_pciconf(p_BusInterface, FW_WRITE , &lcl_data, FLASH_OFFSET, 4 );
	if ( status != STATUS_SUCCESS )
	return status;

	lcl_data = 0;
	
	do
	{
		if (++cnt > 5000)
		{
			return STATUS_DEVICE_NOT_READY;
		}

		status = fw_access_pciconf(p_BusInterface, FW_READ , &lcl_data, FLASH_OFFSET, 4 );
		if ( status != STATUS_SUCCESS )
		return status;

	} while(lcl_data & CMD_MASK);

	return status;
}


static NTSTATUS
fw_flash_read_data (
		IN		BUS_INTERFACE_STANDARD			*p_BusInterface,
		IN		PVOID							p_buffer,
		IN		ULONG							offset,
		IN		ULONG POINTER_ALIGNMENT			length )
{
	NTSTATUS	status = STATUS_SUCCESS;
	uint32_t	cnt = 0;
	uint32_t	lcl_data = ( READ_BIT | (offset & ADDR_MSK));

	if (!length)
		return IB_INVALID_PARAMETER;
	
	status = fw_access_pciconf(p_BusInterface, FW_WRITE, &lcl_data, FLASH_OFFSET, 4 );
	if ( status != STATUS_SUCCESS )
		return status;

	lcl_data = 0;
	do
	{
		// Timeout checks
		if (++cnt > 5000 )
		{
			return STATUS_DEVICE_NOT_READY;
	}

		status = fw_access_pciconf(p_BusInterface, FW_READ, &lcl_data, FLASH_OFFSET, 4 );
	
		if ( status != STATUS_SUCCESS )
			return status;

	} while(lcl_data & CMD_MASK);

	status = fw_access_pciconf(p_BusInterface, FW_READ, p_buffer, FLASH_OFFSET+4, length );
	return status;
}

ib_api_status_t
fw_access_ctrl(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	void** const				handle_array	OPTIONAL,
	IN				uint32_t					num_handles,
	IN				ib_ci_op_t* const			p_ci_op,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	NTSTATUS					status = STATUS_SUCCESS;
	PVOID						p_data;
	ULONG						offset;
	ULONG POINTER_ALIGNMENT		length;
	struct ib_ucontext *p_uctx = (struct ib_ucontext *)h_ca;
	PHCA_FDO_DEVICE_DATA p_fdo = p_uctx->device->x.p_fdo;
	PBUS_INTERFACE_STANDARD		p_bus_interface = p_fdo->p_bus_pci_ifc;

	UNREFERENCED_PARAMETER(handle_array);
	UNREFERENCED_PARAMETER(num_handles);

	if(!p_umv_buf ) {
#if 1//WORKAROUND_POLL_EQ
		if ((p_ci_op->command == FW_POLL_EQ_START) || (p_ci_op->command == FW_POLL_EQ_STOP)){ // poll EQ (in case of missed interrupt) 
			mlnx_hca_t *p_hca = (mlnx_hca_t *)h_ca;
			struct ib_device *p_ibdev = hca2ibdev(p_hca);
			if(p_ci_op->command == FW_POLL_EQ_START)
			{
				p_ibdev->x.poll_eq(p_ibdev,1);
			}else
			{
				p_ibdev->x.poll_eq(p_ibdev,0);
			}
			return IB_SUCCESS;
		}
		else
#endif
		return IB_UNSUPPORTED;
	}

	if ( !p_ci_op )
		return IB_INVALID_PARAMETER;

	length = p_ci_op->buf_size;
	offset = p_ci_op->buf_info;
	p_data = p_ci_op->p_buf;

	mutex_lock( &p_uctx->x.mutex );

	switch ( p_ci_op->command )
	{
	case FW_MAP_CRSPACE:
		status = __map_crspace(p_uctx, p_data, length);
		break;
		
	case FW_UNMAP_CRSPACE:
		__unmap_crspace(p_uctx);
		break;
				
	case FW_OPEN_IF: // open BusInterface
		if (p_bus_interface)
			__open_fw_access( p_uctx, p_bus_interface );
		break;

	case FW_READ: // read data from flash
		if ( p_uctx->x.fw_if_open )
			status = fw_flash_read_data(p_bus_interface, p_data, offset, length);
		break;

	case FW_WRITE: // write data to flash
		if ( p_uctx->x.fw_if_open )
			status = fw_flash_write_data(p_bus_interface, p_data, offset, length);
		break;

	case FW_READ_CMD:
		if ( p_uctx->x.fw_if_open )
			status = fw_access_pciconf(p_bus_interface, 0 , p_data, offset, 4);
		break;

	case FW_WRITE_CMD:
		if ( p_uctx->x.fw_if_open )
			status = fw_access_pciconf(p_bus_interface, 1 , p_data, offset, 4);
		break;

	case FW_CLOSE_IF: // close BusInterface
		__close_fw_access(p_uctx, p_bus_interface);
		break;

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
	}

	if ( status != STATUS_SUCCESS ) {
		__close_fw_access(p_uctx, p_bus_interface);
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_INIT, 
			("fw_access_ctrl failed, ntstatus: %08x.\n", status));
	}

	mutex_unlock( &p_uctx->x.mutex );

	switch( status ) {
		case STATUS_SUCCESS:					return IB_SUCCESS;
		case STATUS_INVALID_DEVICE_REQUEST:	return IB_UNSUPPORTED;
		case STATUS_INSUFFICIENT_RESOURCES:	return IB_INSUFFICIENT_RESOURCES;
		default:									return IB_ERROR;
	}
}


