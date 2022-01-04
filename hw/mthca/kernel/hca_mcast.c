/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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


#include <iba/ib_ci.h>
#include <complib/comp_lib.h>

#include "hca_driver.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_mcast.tmh"
#endif
#include "mthca_dev.h"

/*
*	Multicast Support Verbs.
*/
ib_api_status_t
mlnx_attach_mcast (
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_gid_t					*p_mcast_gid,
	IN		const	uint16_t					mcast_lid,
		OUT			ib_mcast_handle_t			*ph_mcast,
	IN	OUT			ci_umv_buf_t				*p_umv_buf )
{
	int err;
	ib_api_status_t 	status;
	struct ib_qp *ib_qp_p = (struct ib_qp *)h_qp;
	mlnx_mcast_t *mcast_p;

	HCA_ENTER(HCA_DBG_MCAST);

	// sanity checks
	if( p_umv_buf && p_umv_buf->command ) {
		HCA_PRINT(TRACE_LEVEL_ERROR , HCA_DBG_MCAST,
			("User mode is not supported yet\n"));
		status = IB_UNSUPPORTED;
		goto err_user_unsupported;
	}

	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	}

	if (!p_mcast_gid || !ph_mcast) {
		status = IB_INVALID_PARAMETER;
		goto err_invalid_param;
	}

	// allocate structure
	mcast_p = (mlnx_mcast_t*)kmalloc(sizeof *mcast_p, GFP_ATOMIC );
	if (mcast_p == NULL) {
		status = IB_INSUFFICIENT_MEMORY;
		goto err_no_mem;
	}
	
	// attach to mcast group
	if( p_umv_buf && p_umv_buf->command ) {
		//TODO: call uverbs
	}
	else {
		err = ibv_attach_mcast(ib_qp_p, (union ib_gid *)p_mcast_gid, (u16)mcast_lid);
		if (err) {
			HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_MCAST,
				("ibv_attach_mcast failed (%d)\n", err));
			status = errno_to_iberr(err);
			goto err_attach;
		}
	}

	// fill the structure
	mcast_p->ib_qp_p = ib_qp_p;
	mcast_p->mcast_lid = mcast_lid;
	RtlCopyMemory(mcast_p->mcast_gid.raw, p_mcast_gid->raw, sizeof *p_mcast_gid);
	HCA_PRINT(TRACE_LEVEL_INFORMATION,HCA_DBG_MCAST,
		("mcasth %p, qp_p %p, mlid %hx, mgid %I64x`%I64x\n", 
		mcast_p, mcast_p->ib_qp_p, mcast_p->mcast_lid,
		cl_ntoh64(*(uint64_t*)&mcast_p->mcast_gid.raw[0]),
		cl_ntoh64(*(uint64_t*)&mcast_p->mcast_gid.raw[8] )));
	
	// return the result
	if (ph_mcast) *ph_mcast = (ib_mcast_handle_t)mcast_p;

	status = IB_SUCCESS;
	goto end;
		
err_attach: 
	kfree(mcast_p);
err_no_mem:	
err_invalid_param:
err_unsupported:	
err_user_unsupported:
end:		
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MCAST,
			("completes with ERROR status %#x\n", status));
	}
	HCA_EXIT(HCA_DBG_MCAST);
	return status;
}

ib_api_status_t
mlnx_detach_mcast (
	IN		const	ib_mcast_handle_t			h_mcast)
{
	ib_api_status_t 	status = IB_INVALID_PARAMETER;
	int err;
	mlnx_mcast_t *mcast_p = (mlnx_mcast_t*)h_mcast;
	struct ib_device *ib_dev;

	
	HCA_ENTER(HCA_DBG_MCAST);
	// sanity check
	if (!mcast_p || !mcast_p->ib_qp_p)
	{
		HCA_PRINT(TRACE_LEVEL_ERROR , HCA_DBG_MCAST,
			("completes with ERROR status IB_INVALID_PARAMETER\n"));
		status =  IB_INVALID_PARAMETER;
		goto err_invalid_param;
	}
	ib_dev = mcast_p->ib_qp_p->device;

	if( !cl_is_blockable() ) {
			status = IB_UNSUPPORTED;
			goto err_unsupported;
	}


	HCA_PRINT(TRACE_LEVEL_INFORMATION,HCA_DBG_MCAST,
		("mcasth %p, qp_p %p, mlid %hx, mgid %I64x`%I64x\n", 
		mcast_p, mcast_p->ib_qp_p, mcast_p->mcast_lid,
		*(uint64_t*)&mcast_p->mcast_gid.raw[0],
		*(uint64_t*)&mcast_p->mcast_gid.raw[8] ));
	
	// detach
	err = ibv_detach_mcast( mcast_p->ib_qp_p, 
		(union ib_gid *)&mcast_p->mcast_gid, mcast_p->mcast_lid );
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_MCAST,
			("ibv_detach_mcast failed (%d)\n", err));
		status = errno_to_iberr(err);
		goto err_detach_mcast;
	}

	status = IB_SUCCESS;

err_detach_mcast:
	kfree(mcast_p);
err_unsupported:	
err_invalid_param:
	if (status != IB_SUCCESS) 
	{
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MCAST,
			("completes with ERROR status %d\n", status));
	}
	HCA_EXIT(HCA_DBG_MCAST);
	return status;
}


void
mlnx_mcast_if(
	IN	OUT			ci_interface_t				*p_interface )
{
	p_interface->attach_mcast = mlnx_attach_mcast;
	p_interface->detach_mcast = mlnx_detach_mcast;
}
