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

#include "shutter.h"

typedef struct _HCA_FDO_DEVICE_DATA *PHCA_FDO_DEVICE_DATA;
struct ib_cq;
struct ib_event_handler;

struct ib_pd;
struct ib_udata;


/* extension for ib_device */
/* This structure is a part of MLX4_BUS_IB_INTERFACE */
/* Upon its changing you have to change MLX4_BUS_IB_INTERFACE_VERSION */
struct ib_device_ex 
{
	PHCA_FDO_DEVICE_DATA p_fdo;
	int (*get_cached_gid)(struct ib_device *device,
		u8 port_num, int index, union ib_gid *gid);
	int (*find_cached_gid)(struct ib_device *device,
		union ib_gid	*gid, u8 *port_num, u16 *index);
	int (*get_cached_pkey)(struct ib_device *device,
		u8 port_num, int index, __be16 *pkey);
	int (*find_cached_pkey)(struct ib_device *device,
		u8 port_num, __be16 pkey, u16 *index);
	int (*register_ev_cb) (struct ib_event_handler *event_handler);
	int (*unregister_ev_cb) (struct ib_event_handler *event_handler);
#if 1//WORKAROUND_POLL_EQ
	void (*poll_eq)(struct ib_device *device, BOOLEAN bStart);
#endif
	int (*poll_cq_array)(struct ib_cq *ibcq, const int num_entries, ib_wc_t* const wc);
	struct ib_mr *	(*reg_krnl_mr)(struct ib_pd *pd, PMDL p_mdl, u64 length, int access_flags);
	struct ib_mr * (*alloc_fast_reg_mr)(struct ib_pd *pd,
						int max_page_list_len);
	ib_fast_reg_page_list_t * (*alloc_fast_reg_page_list)(struct ib_device *ibdev,
									   int page_list_len);
	void (*free_fast_reg_page_list)(ib_fast_reg_page_list_t *page_list);

	u8 (*get_sl_for_ip_port)(struct ib_device *device,
							 u8 ca_port_num, u16 ip_port_num);

	/* version 11 */
	struct ib_cq * (*create_cq_ex)(struct ib_device *device, int cqe,
						ib_group_affinity_t *affinity,
						struct ib_ucontext *context,
						struct ib_udata *udata);
};


/* extension for ib_ucontext */
typedef struct {
	PVOID	uva;
	PMDL	mdl;
	PVOID	kva;
} umap_t;

struct ib_ucontext_ex 
{
	cl_list_item_t 		list_item;			// chain of user contexts
	umap_t				uar;
	umap_t				bf;
	atomic_t			usecnt; /* count all resources */
	// for tools support
	struct mutex 		mutex;
	PMDL				p_mdl;
	PVOID				va;
	int 				fw_if_open;
	KPROCESSOR_MODE		mode;
};

/* extension for ib_event */
struct ib_event_ex 
{
	uint64_t		vendor_specific;
};

/* extension for ib_cache */
struct ib_cache_ex 
{
	shutter_t		work_thread;
};


