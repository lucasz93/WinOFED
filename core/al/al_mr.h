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

#if !defined(__AL_MR_H__)
#define __AL_MR_H__


#include <complib/cl_qpool.h>

#include "al_ca.h"


typedef struct _al_shmid
{
	al_obj_t					obj;
	cl_list_item_t				list_item;

	/* List of sharing registered memory regions. */
	cl_list_t					mr_list;
	int							id;

}	al_shmid_t;



/*
 * The MR and MR alias structures must be the same in order to seemlessly
 * handle posting of work requests.
 */
typedef struct _ib_mr
{
	al_obj_t					obj;
	ib_mr_handle_t				h_ci_mr;	/* Actual HW handle. */

	/* Reference to any memory registrations shared between processes. */
	al_shmid_t					*p_shmid;

}	ib_mr_t;


cl_status_t
mr_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item );


void
mr_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void*						context );


ib_api_status_t
reg_mem(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_mr_create_t* const		p_mr_create,
	IN		const	uint64_t					mapaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr,
	IN				boolean_t					um_call );


ib_api_status_t
reg_phys(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_phys_create_t* const		p_phys_create,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr,
	IN				boolean_t					um_call );


ib_api_status_t
reg_shared(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_access_t					access_ctrl,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr,
	IN				boolean_t					um_call );


ib_api_status_t
rereg_mem(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mr_mod_mask,
	IN		const	ib_mr_create_t* const		p_mr_create OPTIONAL,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL,
	IN				boolean_t					um_call );


ib_api_status_t
rereg_phys(
	IN		const	ib_mr_handle_t				h_mr,
	IN		const	ib_mr_mod_t					mr_mod_mask,
	IN		const	ib_phys_create_t* const		p_phys_create OPTIONAL,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
	IN		const	ib_pd_handle_t				h_pd OPTIONAL,
	IN				boolean_t					um_call );


ib_api_status_t
dereg_mr(
	IN		const	ib_mr_handle_t				h_mr );

ib_api_status_t
reg_shmid(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_shmid_t					shmid,
	IN		const	ib_mr_create_t* const		p_mr_create,
	IN	OUT			uint64_t* const				p_vaddr,
		OUT			net32_t* const				p_lkey,
		OUT			net32_t* const				p_rkey,
		OUT			ib_mr_handle_t* const		ph_mr );


#ifdef CL_KERNEL
typedef struct _mlnx_fmr
{
	al_obj_t					obj;
	mlnx_fmr_handle_t			h_ci_fmr;	/* Actual HW handle. */
	struct _mlnx_fmr*			p_next;
}	mlnx_fmr_t;



cl_status_t
mlnx_fmr_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item );


void
mlnx_fmr_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void*						context );



#endif


#endif /* __AL_MR_H__ */
