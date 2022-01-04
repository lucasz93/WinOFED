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

////////////////////////////////////////////////////////
//
// INCLUDES
//
////////////////////////////////////////////////////////

#include <iba/ib_ci_ifc.h>
#include "data.h"
#include "debug.h"
#include "bus_intf.h"

#include "hca_fdo_data.h"

////////////////////////////////////////////////////////
//
// MACROS
//
////////////////////////////////////////////////////////

#if !defined(FILE_DEVICE_INFINIBAND) // Not defined in WXP DDK
#define FILE_DEVICE_INFINIBAND          0x0000003B
#endif

#define BUSENUM_POOL_TAG (ULONG) 'suBT'

#define HCARESOURCENAME L"MofResourceName"

#ifndef min
#define min(_a, _b)     (((_a) < (_b)) ? (_a) : (_b))
#endif

#ifndef max
#define max(_a, _b)     (((_a) > (_b)) ? (_a) : (_b))
#endif


////////////////////////////////////////////////////////
//
// TYPES
//
////////////////////////////////////////////////////////

//
// Structure for reporting data to WMI
//

typedef struct _HCA_WMI_STD_DATA {
	UINT32 DebugPrintLevel;
	UINT32 DebugPrintFlags;

} HCA_WMI_STD_DATA, * PHCA_WMI_STD_DATA;


#pragma warning(disable:4201) // nameless struct/union
typedef struct _GLOBALS {
	HCA_WMI_STD_DATA;
} GLOBALS;
#pragma warning(default:4201) // nameless struct/union

extern GLOBALS g;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(HCA_FDO_DEVICE_DATA, FdoGetData)

typedef struct _QUEUE_DATA
{
	PHCA_FDO_DEVICE_DATA FdoData;

} QUEUE_DATA, *PQUEUE_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_DATA, QueueGetData)

////////////////////////////////////////////////////////
//
// FUNCTIONS
//
////////////////////////////////////////////////////////

static inline PHCA_FDO_DEVICE_DATA hca2fdo(mlnx_hca_t *p_hca)
{
	return CONTAINING_RECORD(p_hca, HCA_FDO_DEVICE_DATA, hca);
}

static inline struct ib_device *hca2ibdev(mlnx_hca_t *p_hca)
{
	return (hca2fdo(p_hca))->p_bus_ib_ifc->p_ibdev;
}

static inline mlnx_hca_t *ibdev2hca(struct ib_device *p_ibdev)
{
	return &p_ibdev->x.p_fdo->hca;
}

static inline struct pci_dev *hca2pdev(mlnx_hca_t *p_hca)
{
	return (hca2fdo(p_hca))->p_bus_ib_ifc->pdev;
}

static inline struct mlx4_dev *hca2mdev(mlnx_hca_t *p_hca)
{
	return (hca2fdo(p_hca))->p_bus_ib_ifc->pdev->dev;
}

static inline ib_api_status_t errno_to_iberr(int err)
{
#define MAP_NT_ERR(err,ibstatus)	case err: ib_status = ibstatus; break
	ib_api_status_t ib_status;

	if (!err) 
		return IB_SUCCESS;

	if (err < 0)
		err = -err;
	switch (err) {
		MAP_NT_ERR( ENOENT, IB_NOT_FOUND );
		MAP_NT_ERR( EINTR, IB_INTERRUPTED );
		MAP_NT_ERR( EAGAIN, IB_RESOURCE_BUSY );
		MAP_NT_ERR( ENOMEM, IB_INSUFFICIENT_MEMORY );
		MAP_NT_ERR( EACCES, IB_INVALID_PERMISSION );
		MAP_NT_ERR( EFAULT, IB_ERROR );
		MAP_NT_ERR( EBUSY, IB_RESOURCE_BUSY );
		MAP_NT_ERR( ENODEV, IB_UNSUPPORTED );
		MAP_NT_ERR( EINVAL, IB_INVALID_PARAMETER );
		MAP_NT_ERR( ENOSYS, IB_UNSUPPORTED );
		MAP_NT_ERR( ERANGE, IB_INVALID_SETTING );
		default:
			//HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_SHIM,
			//	"Unmapped errno (%d)\n", err);
			ib_status = IB_UNKNOWN_ERROR;
			break;
	}
	return ib_status;
}

static inline int start_port(struct ib_device *device)
{
	return device->node_type == RDMA_NODE_IB_SWITCH ? 0 : 1;
}

static inline int end_port(struct ib_device *device)
{
	return device->node_type == RDMA_NODE_IB_SWITCH ? 0 : device->phys_port_cnt;
}

