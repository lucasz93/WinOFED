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


#if !defined( _HCA_DRIVER_H_ )
#define _HCA_DRIVER_H_


#include <complib/cl_types.h>
#include <complib/cl_pnp_po.h>
#include <complib/cl_mutex.h>
#include <iba/ib_ci_ifc.h>
#include "mthca/mthca_vc.h"
#include "hca_data.h"
#include "mt_l2w.h"
#include "hca_debug.h"


#include "hca_pnp.h"
#include "hca_pci.h"

#if !defined(FILE_DEVICE_INFINIBAND) // Not defined in WXP DDK
#define FILE_DEVICE_INFINIBAND          0x0000003B
#endif

/****s* HCA/hca_reg_state_t
* NAME
*	hca_reg_state_t
*
* DESCRIPTION
*	State for tracking registration with AL.  This state is independent of the
*	device PnP state, and both are used to properly register with AL.
*
* SYNOPSIS
*/
typedef enum _hca_reg_state
{
	HCA_SHUTDOWN,
	HCA_ADDED,
	HCA_STARTED

}	hca_reg_state_t;
/*
* VALUES
*	HCA_SHUTDOWN
*		Cleaning up.
*
*	HCA_ADDED
*		AddDevice was called and successfully registered for interface
*		notifications.
*
*	HCA_STARTED
*		IRP_MN_START_DEVICE was called.  The HCA is fully functional.
*
*********/


typedef enum _hca_bar_type
{
	HCA_BAR_TYPE_HCR,
	HCA_BAR_TYPE_UAR,
	HCA_BAR_TYPE_DDR,
	HCA_BAR_TYPE_MAX

}	hca_bar_type_t;


typedef struct _hca_bar
{
	uint64_t			phys;
	void				*virt;
	SIZE_T				size;

}	hca_bar_t;


typedef struct _hca_dev_ext
{
	/* -------------------------------------------------
	*		PNP DATA 	 
	* ------------------------------------------------ */
	cl_pnp_po_ext_t	cl_ext;						/* COMPLIB PnP object */
	void					*	pnp_ifc_entry;			/* Notification entry for PnP interface events. */
	void					*	pnp_target_entry;	/* Notification entry for PnP target events. */
	PNP_DEVICE_STATE			pnpState; /* state for PnP Manager */

	/* -------------------------------------------------
	*		POWER MANAGER DATA 	 
	* ------------------------------------------------ */
	/* Cache of the system to device power states. */
	DEVICE_POWER_STATE		DevicePower[PowerSystemMaximum];
	DEVICE_POWER_STATE		DevicePowerState;
	SYSTEM_POWER_STATE		SystemPowerState;
	PIO_WORKITEM			pPoWorkItem;

	/* -------------------------------------------------
	*		IB_AL DATA 	 
	* ------------------------------------------------ */
	/* Number of references on the upper interface. */
	atomic32_t					n_hca_ifc_ref;
	hca_reg_state_t					state;				/* State for tracking registration with AL */
	DEVICE_OBJECT				*	p_al_dev;		/* IB_AL FDO */
	FILE_OBJECT					*	p_al_file_obj;	/* IB_AL file object */
	UNICODE_STRING					al_sym_name;	/* IB_AL symbolic name */

	/* -------------------------------------------------
	*		LOW LEVEL DRIVER' DATA 	 
	* ------------------------------------------------ */
	mlnx_hca_t							hca;
	atomic32_t								usecnt; /* the number of working applications*/
	cl_spinlock_t							uctx_lock;			// spinlock for the below chain
	cl_qlist_t								uctx_list;			// chain of user contexts

	/* -------------------------------------------------
	*		OS DATA 	 
	* ------------------------------------------------ */
	hca_bar_t							bar[HCA_BAR_TYPE_MAX];		/* HCA memory bars */
	CM_PARTIAL_RESOURCE_DESCRIPTOR	interruptInfo;	/* HCA interrupt resources */
	PKINTERRUPT						int_obj;										/* HCA interrupt object */
	spinlock_t							isr_lock; 									/* lock for the ISR */
	ULONG									bus_number;							/* HCA's bus number */
	BUS_INTERFACE_STANDARD			hcaBusIfc;	/* PCI bus interface */

	/* -------------------------------------------------
	*		VARIABLES 	 
	* ------------------------------------------------ */
	DMA_ADAPTER				*	p_dma_adapter;		/* HCA adapter object */
	ULONG									n_map_regs;			/* num of allocated adapter map registers */
	PCI_COMMON_CONFIG		hcaConfig;				/* saved HCA PCI configuration header */
	int										hca_hidden;			/* flag: when set - no attached DDR memory */
	
}	hca_dev_ext_t;

#define EXT_FROM_HOB(hob_p)			(container_of(hob_p,  hca_dev_ext_t, hca.hob))
#define HCA_FROM_HOB(hob_p)			(container_of(hob_p,  mlnx_hca_t, hob))
#define MDEV_FROM_HOB(hob_p)		(HCA_FROM_HOB(hob_p)->mdev)
#define IBDEV_FROM_HOB(hob_p)		(&EXT_FROM_HOB(hob_p)->hca.mdev->ib_dev)
#define HOBUL_FROM_HOB(hob_p)		(&EXT_FROM_HOB(hob_p)->hca.hobul)
#define HOB_FROM_IBDEV(dev_p)		(mlnx_hob_t *)&dev_p->mdev->ext->hca.hob


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

#if 1//WORKAROUND_POLL_EQ
#define FW_POLL_EQ_START		0x0D
#define FW_POLL_EQ_STOP 		0x0E
#endif

#define FW_SIGNATURE		(0x5a445a44)
#define FW_SECT_SIZE		(0x10000)

static inline errno_to_iberr(int err)
{
#define MAP_ERR(err,ibstatus)	case err: ib_status = ibstatus; break
	ib_api_status_t ib_status = IB_UNKNOWN_ERROR;
	if (err < 0)
		err = -err;
	switch (err) {
		MAP_ERR( ENOENT, IB_NOT_FOUND );
		MAP_ERR( EINTR, IB_INTERRUPTED );
		MAP_ERR( EAGAIN, IB_RESOURCE_BUSY );
		MAP_ERR( ENOMEM, IB_INSUFFICIENT_MEMORY );
		MAP_ERR( EACCES, IB_INVALID_PERMISSION );
		MAP_ERR( EFAULT, IB_ERROR );
		MAP_ERR( EBUSY, IB_RESOURCE_BUSY );
		MAP_ERR( ENODEV, IB_UNSUPPORTED );
		MAP_ERR( EINVAL, IB_INVALID_PARAMETER );
		MAP_ERR( ENOSYS, IB_UNSUPPORTED );
		MAP_ERR( ERANGE, IB_INVALID_SETTING );
		default:
			//HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_SHIM,
			//	"Unmapped errno (%d)\n", err);
			break;
	}
	return ib_status;
}

#endif	/* !defined( _HCA_DRIVER_H_ ) */
