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


#if !defined(__AL_FMR_POOL_H__)
#define __AL_FMR_POOL_H__

#include <complib/cl_qlist.h>
#include <iba/ib_al.h>
#include "al_common.h"


/*
 * If an FMR is not in use, then the list member will point to either
 * its pool's free_list (if the FMR can be mapped again; that is,
 * remap_count < pool->max_remaps) or its pool's dirty_list (if the
 * FMR needs to be unmapped before being remapped).  In either of
 * these cases it is a bug if the ref_count is not 0.  In other words,
 * if ref_count is > 0, then the list member must not be linked into
 * either free_list or dirty_list.
 *
 * The cache_node member is used to link the FMR into a cache bucket
 * (if caching is enabled).  This is independent of the reference
 * count of the FMR.  When a valid FMR is released, its ref_count is
 * decremented, and if ref_count reaches 0, the FMR is placed in
 * either free_list or dirty_list as appropriate.  However, it is not
 * removed from the cache and may be "revived" if a call to
 * ib_fmr_register_physical() occurs before the FMR is remapped.  In
 * this case we just increment the ref_count and remove the FMR from
 * free_list/dirty_list.
 *
 * Before we remap an FMR from free_list, we remove it from the cache
 * (to prevent another user from obtaining a stale FMR).  When an FMR
 * is released, we add it to the tail of the free list, so that our
 * cache eviction policy is "least recently used."
 *
 * All manipulation of ref_count, list and cache_node is protected by
 * pool_lock to maintain consistency.
 */

#pragma warning( disable : 4200)
typedef struct _mlnx_fmr_pool_element {
	mlnx_fmr_handle_t			h_fmr;
	mlnx_fmr_pool_handle_t	h_pool;
	cl_list_item_t				list_item;
	cl_qlist_t					*p_cur_list;
	cl_list_item_t				cache_node;
	boolean_t				in_cash;
	int						ref_count;
	int						remap_count;
	uint64_t					io_virtual_address;
	net32_t					lkey;
	net32_t					rkey;
	int						page_list_len;
	uint64_t					page_list[0];
} mlnx_fmr_pool_element_t;
#pragma warning( default  : 4200)


typedef struct _mlnx_fmr_pool {

	al_obj_t						obj;			/* Child of ib_al_handle_t */
	cl_spinlock_t					pool_lock;

	int							pool_size;
	int							max_pages;
	int							max_remaps;
	int							dirty_watermark;
	int							dirty_len;
	cl_qlist_t						free_list;
	cl_qlist_t						dirty_list;
	cl_qlist_t						rest_list;	/* those, that not in free and not in dirty */
	cl_qlist_t						*cache_bucket;

	void							(*flush_function) (mlnx_fmr_pool_handle_t h_pool,void* arg);
	void							*flush_arg;

	cl_thread_t					thread;
	cl_event_t					do_flush_event;
	cl_event_t					flush_done_event;
	atomic32_t					flush_req;
	atomic32_t					should_stop;
} mlnx_fmr_pool_t;


#endif /* IB_FMR_POOL_H */

