/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#if !defined( __BUS_IOU_MGR_H__ )
#define __BUS_IOU_MGR_H__

#include <iba/ib_al.h>
#include <complib/cl_mutex.h>
#include <complib/cl_obj.h>


/* Global load service */
typedef struct _iou_mgr
{
	cl_obj_t					obj;

	/* Mutex protects both pointer vectors. */
	cl_mutex_t					pdo_mutex;

	/* Pointer vector of child IOU PDOs. */
	cl_qlist_t					iou_list;

}	iou_mgr_t;


struct _bus_filter_instance;

ib_api_status_t
create_iou_mgr(
		IN			struct _bus_filter_instance*	p_bfi );

NTSTATUS
iou_mgr_get_bus_relations(
	IN			struct _bus_filter_instance*	p_bfi,
	IN				IRP* const					p_irp );

#endif
