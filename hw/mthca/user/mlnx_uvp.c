/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include "mt_l2w.h"
#include "mlnx_uvp.h"

#if defined(EVENT_TRACING)
#include "mlnx_uvp.tmh"
#endif

#include "mx_abi.h"

size_t g_page_size = 0;

#ifndef PCI_VENDOR_ID_MELLANOX
#define PCI_VENDOR_ID_MELLANOX			0x15b3
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_TAVOR
#define PCI_DEVICE_ID_MELLANOX_TAVOR		0x5a44
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT
#define PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT	0x6278
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_ARBEL
#define PCI_DEVICE_ID_MELLANOX_ARBEL		0x6282
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_SINAI_OLD
#define PCI_DEVICE_ID_MELLANOX_SINAI_OLD	0x5e8c
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_SINAI
#define PCI_DEVICE_ID_MELLANOX_SINAI		0x6274
#endif

#ifndef PCI_VENDOR_ID_TOPSPIN
#define PCI_VENDOR_ID_TOPSPIN			0x1867
#endif


#define HCA(v, d, t) \
	{ PCI_VENDOR_ID_##v,	PCI_DEVICE_ID_MELLANOX_##d, MTHCA_##t }

static struct pci_device_id {
	unsigned		vendor;
	unsigned		device;
	enum mthca_hca_type	type;
} mthca_pci_table[] = {
	HCA( MELLANOX, 	TAVOR,	    			TAVOR),
	HCA( MELLANOX, 	ARBEL_COMPAT, 	TAVOR),
	HCA( MELLANOX, 	ARBEL,	    				ARBEL),
	HCA( MELLANOX, 	SINAI_OLD,    		ARBEL),
	HCA( MELLANOX, 	SINAI,	    				ARBEL),
	HCA( TOPSPIN, 		TAVOR,	    			TAVOR),
	HCA( TOPSPIN, 		ARBEL_COMPAT, 	TAVOR),
	HCA( TOPSPIN, 		ARBEL,	    				ARBEL),
	HCA( TOPSPIN, 		SINAI_OLD, 			ARBEL),
	HCA( TOPSPIN, 		SINAI,	    				ARBEL),
};

static struct ibv_context_ops mthca_ctx_ops = {
	NULL,	// mthca_query_device,
	NULL,	// mthca_query_port,
	mthca_alloc_pd,
	mthca_free_pd,
	NULL, 	// mthca_reg_mr,
	NULL, 	// mthca_dereg_mr,
	mthca_create_cq_pre,
	mthca_create_cq_post,
	mthca_poll_cq,
	mthca_poll_cq_list,
	NULL,	/* req_notify_cq */
	mthca_destroy_cq,
	NULL,	// mthca_create_srq,
	NULL,	// mthca_modify_srq,
	NULL,	// mthca_destroy_srq,
	NULL, 	/* post_srq_recv */
	mthca_create_qp_pre,
	mthca_create_qp_post,
	mthca_modify_qp,
	NULL,
	NULL, 	/* post_send */
	NULL, 	/* post_recv */
	mthca_attach_mcast,
	mthca_detach_mcast
};

struct ibv_context *mthca_alloc_context(struct ibv_get_context_resp *resp_p)
{
	struct mthca_context	*	context;
	struct ibv_alloc_pd_resp	pd_resp;
	int                              		i;

	/* allocate context */
	context = cl_zalloc(sizeof *context);
	if (!context)
		return NULL;

	/* find page size  */
	if (!g_page_size) {
		SYSTEM_INFO sys_info;
		GetSystemInfo(&sys_info);
		g_page_size 	= sys_info.dwPageSize;
	}

	/* calculate device type */
	for (i = 0; i < sizeof mthca_pci_table / sizeof mthca_pci_table[0]; ++i) 
		if (resp_p->vend_id == mthca_pci_table[i].vendor &&
			resp_p->dev_id == mthca_pci_table[i].device) 
			goto found;
	goto err_dev_type;

found:
	context->hca_type = mthca_pci_table[i].type;
	context->uar = (void*)(UINT_PTR)resp_p->uar_addr;
	context->num_qps        = resp_p->qp_tab_size;
	context->qp_table_shift = ffs(context->num_qps) - 1 - MTHCA_QP_TABLE_BITS;
	context->qp_table_mask  = (1 << context->qp_table_shift) - 1;

	if (mthca_is_memfree(&context->ibv_ctx)) {
		context->db_tab = mthca_alloc_db_tab(resp_p->uarc_size);
		if (!context->db_tab)
			goto err_alloc_db_tab;
	} else
		context->db_tab = NULL;

	context->qp_table_mutex = CreateMutex( NULL, FALSE, NULL );
	if (!context->qp_table_mutex)
		goto err_mutex;
	for (i = 0; i < MTHCA_QP_TABLE_SIZE; ++i)
		context->qp_table[i].refcnt = 0;

	cl_spinlock_construct(&context->uar_lock);
	if (cl_spinlock_init(&context->uar_lock))
		goto err_spinlock;

	pd_resp.pd_handle = resp_p->pd_handle;
	pd_resp.pdn = resp_p->pdn;
	context->pd = mthca_alloc_pd(&context->ibv_ctx, &pd_resp);
	if (!context->pd)
		goto err_unmap;

	context->ibv_ctx.ops = mthca_ctx_ops;

	if (mthca_is_memfree(&context->ibv_ctx)) {
		context->ibv_ctx.ops.req_notify_cq = mthca_arbel_arm_cq;
		context->ibv_ctx.ops.post_send     = mthca_arbel_post_send;
		context->ibv_ctx.ops.post_recv     = mthca_arbel_post_recv;
		context->ibv_ctx.ops.post_srq_recv = mthca_arbel_post_srq_recv;
	} else {
		context->ibv_ctx.ops.req_notify_cq = mthca_tavor_arm_cq;
		context->ibv_ctx.ops.post_send     = mthca_tavor_post_send;
		context->ibv_ctx.ops.post_recv     = mthca_tavor_post_recv;
		context->ibv_ctx.ops.post_srq_recv = mthca_tavor_post_srq_recv;
	}

	return &context->ibv_ctx;

err_unmap:
err_spinlock:
err_mutex:
	mthca_free_db_tab(context->db_tab);

err_alloc_db_tab:
err_dev_type:
	cl_free(context);
	return NULL;
}

void mthca_free_context(struct ibv_context *ibctx)
{
	struct mthca_context *context = to_mctx(ibctx);

	cl_spinlock_destroy(&context->uar_lock);
	mthca_free_pd(context->pd);
	mthca_free_db_tab(context->db_tab);
	cl_free(context);
}
