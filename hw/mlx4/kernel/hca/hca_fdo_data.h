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


#pragma once

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


//
// The device extension of the bus itself.  From whence the PDO's are born.
//

typedef struct _HCA_FDO_DEVICE_DATA
{
	/* -------------------------------------------------
	*		WDF DATA	 
	* ------------------------------------------------ */
	WDFDEVICE					FdoDevice;
	DEVICE_OBJECT			*	p_dev_obj;		/* WDM dev object */

	/* -------------------------------------------------
	*		IBAL DATA 	 
	* ------------------------------------------------ */
	/* Number of references on the upper interface. */
	atomic32_t					n_hca_ifc_ref;
	hca_reg_state_t				state;				/* State for tracking registration with AL */

	/* -------------------------------------------------
	*		SHIM DATA	 
	* ------------------------------------------------ */
	mlnx_hca_t					hca;
	atomic32_t					usecnt; /* the number of working applications*/
	spinlock_t					uctx_lock;			// spinlock for the below chain
	cl_qlist_t					uctx_list;			// chain of user contexts
	int							bus_pci_ifc_taken;
	BUS_INTERFACE_STANDARD		bus_pci_ifc;		/* PCI bus interface */
	BUS_INTERFACE_STANDARD		*p_bus_pci_ifc;		/* pointer to PCI bus interface */

	/* -------------------------------------------------
	*		LOW LEVEL DRIVER' DATA	 
	* ------------------------------------------------ */
	MLX4_BUS_IB_INTERFACE		*p_bus_ib_ifc;

} HCA_FDO_DEVICE_DATA, *PHCA_FDO_DEVICE_DATA;


