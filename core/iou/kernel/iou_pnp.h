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



#if !defined _IOU_DRV_PNP_H_
#define _IOU_DRV_PNP_H_


#include "iou_driver.h"


/****f* InfiniBand Bus Driver: Plug and Play/iou_add_device
* NAME
*	iou_add_device
*
* DESCRIPTION
*	Main AddDevice entrypoint for the IB Bus driver.
*	Adds the bus root functional device object to the device node.  The
*	bus root FDO performs all PnP operations for fabric attached devices.
*
* SYNOPSIS
*/
NTSTATUS
iou_add_device(
	IN				PDRIVER_OBJECT				p_driver_obj,
	IN				PDEVICE_OBJECT				p_pdo );
/*
* PARAMETERS
*	p_driver_obj
*		Driver object for the IOU driver.
*
*	p_pdo
*		Pointer to the device object representing the PDO for the device on
*		which we are loading.
*
* RETURN VIOUUES
*	STATUS_SUCCESS if the device was successfully added.
*
*	Other NTSTATUS error values if errors are encountered.
*
* SEE ALSO
*********/


void
update_relations(
	IN				cl_qlist_t*	const			p_pdo_list,
	IN	OUT			DEVICE_RELATIONS* const		p_rel );


#endif	// !defined _IOU_DRV_PNP_H_
