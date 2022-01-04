/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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

#ifndef __UAL_MAIN_H__
#define __UAL_MAIN_H__

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

//#include <iba/ib_ci.h>
#include "mlnx_ual_data.h"
#include "mlnx_uvp_debug.h"
#include <complib/cl_byteswap.h>
#include <complib/cl_memory.h>
//#include <complib/cl_device.h>


#define		MAX_WRS_PER_CHAIN		16
#define		MAX_NUM_SGE				32

#define		MLNX_SGE_SIZE				16
#define		MLNX_UAL_ALLOC_HCA_UL_RES	1
#define		MLNX_UAL_FREE_HCA_UL_RES	2

typedef         unsigned __int3264            cl_dev_handle_t;

extern uint32_t mlnx_dbg_lvl;
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
		default:
			CL_TRACE (CL_DBG_ERROR, mlnx_dbg_lvl, ("Unmapped errno (%d)\n", err));
			break;
	}
	return ib_status;
}




/*
 * PROTOTYPES
 */

/************* CA operations *************************/
void  
mlnx_get_ca_interface (
    IN OUT	uvp_interface_t				*p_uvp );

/************* PD Management *************************/
void  
mlnx_get_pd_interface (
    IN OUT	uvp_interface_t				*p_uvp );

/************* AV Management *************************/
void
mlnx_get_av_interface (
    IN OUT	uvp_interface_t				*p_uvp );

/************* CQ Management *************************/
void  
mlnx_get_cq_interface (
    IN OUT	uvp_interface_t				*p_uvp );

/************* SRQ Management *************************/
void  
mlnx_get_srq_interface (
    IN OUT	uvp_interface_t				*p_uvp );

/************* QP Management *************************/
void  
mlnx_get_qp_interface (
    IN OUT	uvp_interface_t				*p_uvp );

/************* MR/MW Management *************************/
void  
mlnx_get_mrw_interface (
    IN OUT	uvp_interface_t				*p_uvp );

/************* MCAST Management *************************/
void  
mlnx_get_mcast_interface (
    IN OUT	uvp_interface_t				*p_uvp );

/************* OS BYPASS Management *************************/
void  
mlnx_get_osbypass_interface (
    IN OUT	uvp_interface_t				*p_uvp );

#endif
