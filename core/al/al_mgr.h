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

#if !defined(__AL_MGR_H__)
#define __AL_MGR_H__


#include <complib/cl_event.h>
#include "al_ci_ca.h"
#include "al_common.h"
#include "al_proxy_ioctl.h"

#ifndef CL_KERNEL
#include "ual_mgr.h"
#endif


typedef struct _al_mgr
{
	al_obj_t					obj;

	/* List of all AL object's in the system. */
	cl_qlist_t					al_obj_list;
	cl_spinlock_t				lock;

	/* Table of Channel Adapters. */
	cl_qlist_t					ci_ca_list;

#ifndef CL_KERNEL
	ual_mgr_t					ual_mgr;
#endif

}	al_mgr_t;


/*
 * Globals used throughout AL
 *
 * Note that the exported symbols are only exported for the bus driver
 * (loader) and are not intended for use by any normal AL clients.
 */
extern cl_async_proc_t		*gp_async_proc_mgr;
extern cl_async_proc_t		*gp_async_pnp_mgr;
extern al_mgr_t			*gp_al_mgr;
extern ib_al_handle_t		gh_al;
extern ib_pool_handle_t	gh_mad_pool;
extern void				*gp_tmp;
extern cl_obj_mgr_t		*gp_obj_mgr;



ib_api_status_t
create_al_mgr( void );


void
print_al_objs(
	IN		const	ib_al_handle_t				h_al );

void
print_al_obj(
	IN				al_obj_t * const			p_obj );

void
print_tail_al_objs( void );


al_ci_ca_t*
acquire_ci_ca(
	IN		const	ib_net64_t					ci_ca_guid,
	IN		const	ib_ca_handle_t				h_ca );

void
release_ci_ca(
	IN		const	ib_ca_handle_t				h_ca );


ib_ca_handle_t
acquire_ca(
	IN		const	ib_net64_t					ci_ca_guid );


void
add_ci_ca(
	IN				al_ci_ca_t* const			p_ci_ca );

void
remove_ci_ca(
	IN				al_ci_ca_t* const			p_ci_ca );

al_ci_ca_t*
find_ci_ca(
	IN		const	ib_net64_t					ci_ca_guid );


#endif /* __AL_MGR_H__ */
