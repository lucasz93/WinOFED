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

#if !defined(__AL_RES_MGR_H__)
#define __AL_RES_MGR_H__


#include <iba/ib_al.h>

#include "al_av.h"
#include "al_mr.h"
#include "al_qp.h"
#include "al_mad.h"

#include <complib/cl_qpool.h>
#include <complib/cl_qlist.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_timer.h>



typedef struct _al_res_mgr
{
	al_obj_t					obj;

	cl_qpool_t					mr_pool;
	cl_qpool_t					av_pool;
#ifdef CL_KERNEL
	cl_qpool_t					fmr_pool;
#endif

}	al_res_mgr_t;



ib_api_status_t
create_res_mgr(
	IN				al_obj_t					*p_parent_obj );


ib_mr_handle_t
alloc_mr(void);


void
put_mr(
	IN				ib_mr_handle_t				h_mr );

#ifdef CL_KERNEL
mlnx_fmr_handle_t
alloc_mlnx_fmr(void);


void
put_mlnx_fmr(
	IN				mlnx_fmr_handle_t			h_fmr );
#endif

ib_av_handle_t
alloc_av(void);


void
put_av(
	IN				ib_av_handle_t				h_av );


#endif /* __AL_RES_MGR_H__ */
