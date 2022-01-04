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



#if !defined( __IOU_IOU_MGR_H__ )
#define __IOU_IOU_MGR_H__

#include <iba/ib_al.h>
#include <complib/cl_mutex.h>
#include <complib/cl_obj.h>
#include <iba/iou_ifc.h>

/* Global load service */
typedef struct _ioc_mgr
{
	cl_obj_t					obj;

	ib_al_ifc_t					ifc;

	ib_al_handle_t				h_al;
	ib_pnp_handle_t				h_pnp;	/* Handle for iou PnP events */

	/* Attributes for this IOU. */
	iou_ifc_data_t				info;

	/* Mutex protects both pointer vectors. */
	cl_mutex_t					pdo_mutex;

	/* Pointer vector of child IOC PDOs. */
	cl_qlist_t					pdo_list;

}	ioc_mgr_t;


#pragma pack(push, 1)

/* Windows pnp device information */
#define MAX_DEVICE_ID_LEN     200
#define MAX_DEVICE_STRING_LEN 		MAX_DEVICE_ID_LEN + 2	//add extra 4 bytes in case we need double NULL ending

typedef struct _ca_ioc_info {
	net64_t					ca_guid;
	ib_ioc_info_t				info;
	ib_svc_entry_t				svc_entry_array[1];
} ca_ioc_info_t;

typedef struct _child_device_info {
	wchar_t		device_id[MAX_DEVICE_STRING_LEN];  
	uint32_t	device_id_size;
	wchar_t		compatible_id[MAX_DEVICE_STRING_LEN];
	uint32_t	compatible_id_size;
	wchar_t		hardware_id[MAX_DEVICE_STRING_LEN];
	uint32_t	hardware_id_size;
	wchar_t		description[MAX_DEVICE_STRING_LEN];
	uint32_t	description_size;
	ca_ioc_info_t	ca_ioc_path;
	uint32_t	uniqueinstanceid;
}  child_device_info_t;

typedef struct _child_device_info_list{
	struct _child_device_info_list *next_device_info;
	child_device_info_t io_device_info;
}child_device_info_list_t;

typedef struct _ca_ioc_map {
	cl_list_item_t				ioc_list;
	ioc_mgr_t				*p_ioc_mgr;
	net64_t					ca_guid;
	ib_ioc_info_t				info;
	ib_svc_entry_t				svc_entry_array[1];
} ca_ioc_map_t;
#pragma pack(pop)

ib_api_status_t
_create_ioc_pdo(
	IN				child_device_info_t*			p_child_dev,
	IN				ca_ioc_map_t*				p_ca_ioc_map);

ca_ioc_map_t	*find_ca_ioc_map(net64_t	ca_guid,
				 ib_net64_t	ioc_guid);


void
ioc_mgr_construct(
	IN	OUT			ioc_mgr_t* const			p_ioc_mgr );

ib_api_status_t
ioc_mgr_init(
	IN	OUT			ioc_mgr_t* const			p_ioc_mgr );

NTSTATUS
ioc_mgr_get_iou_relations(
	IN				ioc_mgr_t* const			p_ioc_mgr,
	IN				IRP* const					p_irp );

#endif
