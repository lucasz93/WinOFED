/*
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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


#include "mlx4.h"
#include "mx_abi.h"

#ifndef PCI_VENDOR_ID_MELLANOX
#define PCI_VENDOR_ID_MELLANOX			0x15b3
#endif

#define HCA(v, d) \
	{PCI_VENDOR_ID_##v,			\
	  d }

struct {
	unsigned		vendor;
	unsigned		device;
} hca_table[] = {
	HCA(MELLANOX, 0x6340),	/* MT25408 "Hermon" SDR */
	HCA(MELLANOX, 0x6341),	/* MT25409 "Hermon" SDR */
	HCA(MELLANOX, 0x634a),	/* MT25418 "Hermon" DDR */
	HCA(MELLANOX, 0x6354),	/* MT25428 "Hermon" QDR */
	HCA(MELLANOX, 0x634B),	/* MT25419 "Hermon" DDR */
	HCA(MELLANOX, 0x6732),	/* MT26418 "Hermon" DDR PCIe gen2 */
	HCA(MELLANOX, 0x6733),	/* MT26419 "Hermon" DDR PCIe gen2 */
	HCA(MELLANOX, 0x6778),	/* MT26488 "Hermon" DDR PCIe gen2 */
	HCA(MELLANOX, 0x673C),	/* MT26428 "Hermon" QDR PCIe gen2 */
	HCA(MELLANOX, 0x673D),	/* MT26429 "Hermon" QDR PCIe gen2 */
	HCA(MELLANOX, 0x6746),	/* MT26438 "Hermon" QDR PCIe gen2 */
	HCA(MELLANOX, 0x1000),	/* MT4096  */
	HCA(MELLANOX, 0x1001),	/* MT4097 "Adir" */
	HCA(MELLANOX, 0x1002),	/* MT4098 "Adir" VF */
	HCA(MELLANOX, 0x1003),	/* MT4099 "Kfir" */
	HCA(MELLANOX, 0x1004),	/* MT4100 "Kfir" VF */
	HCA(MELLANOX, 0x1007),	/* MT4103 */
	HCA(MELLANOX, 0x1008),	/* MT4104 */
	HCA(MELLANOX, 0x1009),	/* MT4105 */
	HCA(MELLANOX, 0x100a),	/* MT4106 */

	HCA(MELLANOX, 0x6368),	/* MT25448 "Hermon" Ethernet */
	HCA(MELLANOX, 0x6369),	/* MT25449 "Hermon" Ethernet */
	HCA(MELLANOX, 0x6372),	/* MT25458 "Hermon" Ethernet Yatir*/
	HCA(MELLANOX, 0x6750),	/* MT26448 "Hermon" Ethernet PCIe gen2 */
	HCA(MELLANOX, 0x6751),	/* MT26449 "Hermon" Ethernet PCIe gen2 */
	HCA(MELLANOX, 0x675A),	/* MT26458 "Hermon" Ethernet Yatir PCIe gen2*/
	HCA(MELLANOX, 0x6764),	/* MT26468 "Hermon" B0 Ethernet PCIe gen2*/
	HCA(MELLANOX, 0x6765),	/* MT26469 "Hermon" B0 Ethernet PCIe gen2*/
	HCA(MELLANOX, 0x676E),	/* MT26478 "Hermon" B0 40Gb Ethernet PCIe gen2*/
};


struct ibv_context * mlx4_alloc_context()
{
	struct mlx4_context *context;
	
	/* allocate context */
	context = cl_zalloc(sizeof *context);
	if (!context)
		goto end;

	context->qp_table_mutex = CreateMutex(NULL, FALSE, NULL);
	if (!context->qp_table_mutex)
		goto err_qp_mutex;

#ifdef XRC_SUPPORT
	context->xrc_srq_table_mutex = CreateMutex(NULL, FALSE, NULL);
	if (!context->xrc_srq_table_mutex)
		goto err_xrc_mutex;
#endif	

	context->db_list_mutex = CreateMutex(NULL, FALSE, NULL);
	if (!context->db_list_mutex)
		goto err_db_mutex;

	if (cl_spinlock_init(&context->uar_lock))
		goto err_uar_spinlock;

	if (cl_spinlock_init(&context->bf_lock))
		goto err_bf_spinlock;

	return &context->ibv_ctx;

err_bf_spinlock:
	cl_spinlock_destroy(&context->uar_lock);
err_uar_spinlock:
	CloseHandle(context->db_list_mutex);
err_db_mutex:
#ifdef XRC_SUPPORT
	CloseHandle(context->xrc_srq_table_mutex);
err_xrc_mutex:
#endif	
	CloseHandle(context->qp_table_mutex);
err_qp_mutex:
	cl_free(context);
end:
	return NULL;
	
}

struct ibv_context * mlx4_fill_context(struct ibv_context *ctx, struct ibv_get_context_resp *p_resp)
{
	struct mlx4_context *context = to_mctx(ctx);
	SYSTEM_INFO sys_info;
	int i;

	/* check device type */
	for (i = 0; i < sizeof hca_table / sizeof hca_table[0]; ++i) 
		if (p_resp->vend_id == hca_table[i].vendor &&
			p_resp->dev_id == hca_table[i].device) 
			goto found;
	goto err_dev_type;

found:
	context->num_qps			= p_resp->qp_tab_size;
	context->qp_table_shift	= ffsl(context->num_qps) - 1 - MLX4_QP_TABLE_BITS;
	context->qp_table_mask	= (1 << context->qp_table_shift) - 1;

	for (i = 0; i < MLX4_QP_TABLE_SIZE; ++i)
		context->qp_table[i].refcnt = 0;

#ifdef XRC_SUPPORT
	context->num_xrc_srqs	= p_resp->qp_tab_size;
	context->xrc_srq_table_shift = ffsl(context->num_xrc_srqs) - 1
				       - MLX4_XRC_SRQ_TABLE_BITS;
	context->xrc_srq_table_mask = (1 << context->xrc_srq_table_shift) - 1;

	for (i = 0; i < MLX4_XRC_SRQ_TABLE_SIZE; ++i)
		context->xrc_srq_table[i].refcnt = 0;
#endif

	for (i = 0; i < MLX4_NUM_DB_TYPE; ++i)
		context->db_list[i] = NULL;

	context->uar 			= (uint8_t *)(uintptr_t)p_resp->mapping_results[ibv_get_context_uar].Information;
	context->bf_page		= (uint8_t *)(uintptr_t)p_resp->mapping_results[ibv_get_context_bf].Information;
	context->bf_buf_size	= p_resp->bf_buf_size;
	context->bf_offset		= p_resp->bf_offset;
	context->cqe_size		= p_resp->cqe_size;

	context->max_qp_wr	= p_resp->max_qp_wr;
	context->max_sge		= p_resp->max_sge;
	context->max_cqe		= p_resp->max_cqe;

	GetSystemInfo(&sys_info);
	context->ibv_ctx.page_size = sys_info.dwPageSize;

	return &context->ibv_ctx;

err_dev_type:
	mlx4_free_context(&context->ibv_ctx);
	return NULL;
}

void mlx4_free_context(struct ibv_context *ctx)
{
	struct mlx4_context *context = to_mctx(ctx);

	cl_spinlock_destroy(&context->bf_lock);
	cl_spinlock_destroy(&context->uar_lock);
	CloseHandle(context->db_list_mutex);
#ifdef XRC_SUPPORT
	CloseHandle(context->xrc_srq_table_mutex);
#endif
	CloseHandle(context->qp_table_mutex);
	cl_free(context);
}

static void __get_uvp_interface(uvp_interface_t *p_uvp)
{
	p_uvp->pre_open_ca		= mlx4_pre_open_ca;
	p_uvp->post_open_ca		= mlx4_post_open_ca;
	p_uvp->pre_query_ca		= NULL;
	p_uvp->post_query_ca	= NULL;
	p_uvp->pre_modify_ca	= NULL;
	p_uvp->post_modify_ca	= NULL;
	p_uvp->pre_close_ca		= NULL;
	p_uvp->post_close_ca	= mlx4_post_close_ca;


	/*
	 * Protection Domain
	 */
	p_uvp->pre_allocate_pd		= mlx4_pre_alloc_pd;
	p_uvp->post_allocate_pd		= mlx4_post_alloc_pd;
	p_uvp->pre_deallocate_pd	= NULL;
	p_uvp->post_deallocate_pd	= mlx4_post_free_pd;


	/*
	 * SRQ Management Verbs
	 */
	p_uvp->pre_create_srq	= mlx4_pre_create_srq;
	p_uvp->post_create_srq	= mlx4_post_create_srq;
	p_uvp->pre_query_srq	= NULL;
	p_uvp->post_query_srq	= NULL;
	p_uvp->pre_modify_srq	= NULL;
	p_uvp->post_modify_srq	= NULL;
	p_uvp->pre_destroy_srq	= NULL;
	p_uvp->post_destroy_srq	= mlx4_post_destroy_srq;

	p_uvp->pre_create_qp	= mlx4_pre_create_qp;
	p_uvp->wv_pre_create_qp = mlx4_wv_pre_create_qp;
	p_uvp->post_create_qp	= mlx4_post_create_qp;
	p_uvp->pre_modify_qp	= mlx4_pre_modify_qp;
	p_uvp->post_modify_qp	= mlx4_post_modify_qp;
	p_uvp->pre_query_qp		= NULL;
	p_uvp->post_query_qp	= mlx4_post_query_qp;
	p_uvp->pre_destroy_qp	= mlx4_pre_destroy_qp;
	p_uvp->post_destroy_qp	= mlx4_post_destroy_qp;
	p_uvp->nd_modify_qp		= mlx4_nd_modify_qp;
	p_uvp->nd_get_qp_state	= mlx4_nd_get_qp_state;


	/*
	 * Completion Queue Management Verbs
	 */
	p_uvp->pre_create_cq	= mlx4_pre_create_cq;
	p_uvp->post_create_cq	= mlx4_post_create_cq;
	p_uvp->pre_query_cq		= mlx4_pre_query_cq;
	p_uvp->post_query_cq	= NULL;
	p_uvp->pre_resize_cq	= NULL;
	p_uvp->post_resize_cq	= NULL;
	p_uvp->pre_destroy_cq	= NULL;
	p_uvp->post_destroy_cq	= mlx4_post_destroy_cq;


	/*
	 * AV Management
	 */
	p_uvp->pre_create_av	= mlx4_pre_create_ah;
	p_uvp->post_create_av	= NULL;
	p_uvp->pre_query_av		= mlx4_pre_query_ah;
	p_uvp->post_query_av	= mlx4_post_query_ah;
	p_uvp->pre_modify_av	= mlx4_pre_modify_ah;
	p_uvp->post_modify_av	= NULL;
	p_uvp->pre_destroy_av	= mlx4_pre_destroy_ah;
	p_uvp->post_destroy_av	= NULL;


	/*
	 * Memory Region / Window Management Verbs
	 */
	p_uvp->pre_create_mw	= NULL;
	p_uvp->post_create_mw	= NULL;
	p_uvp->pre_query_mw 	= NULL;
	p_uvp->post_query_mw	= NULL;
	p_uvp->pre_destroy_mw	= NULL;
	p_uvp->post_destroy_mw	= NULL;


	/*
	 * Multicast Support Verbs
	 */
	p_uvp->pre_attach_mcast		= NULL;
	p_uvp->post_attach_mcast	= NULL;
	p_uvp->pre_detach_mcast		= NULL;
	p_uvp->post_detach_mcast	= NULL;


	/*
	 * OS bypass (send, receive, poll/notify cq)
	 */
	p_uvp->post_send		= mlx4_post_send;
	p_uvp->post_recv		= mlx4_post_recv;
	p_uvp->post_srq_recv	= mlx4_post_srq_recv;
	p_uvp->poll_cq			= mlx4_poll_cq_list;
	p_uvp->poll_cq_array	= mlx4_poll_cq_array;
	p_uvp->rearm_cq			= mlx4_arm_cq;
	p_uvp->rearm_n_cq		= NULL;
	p_uvp->peek_cq			= NULL;
	p_uvp->bind_mw			= NULL;
}

/* TODO: define and expose XRC through new interface GUID */
#ifdef XRC_SUPPORT
static void __get_xrc_interface(uvp_xrc_interface_t *p_xrc)
{
	/*
	 * XRC Management Verbs
	 */
	p_uvp->pre_create_xrc_srq		= mlx4_pre_create_xrc_srq;
	p_uvp->post_create_xrc_srq		= mlx4_post_create_xrc_srq;
	p_uvp->pre_open_xrc_domain		= mlx4_pre_open_xrc_domain;
	p_uvp->post_open_xrc_domain		= mlx4_post_open_xrc_domain;
	p_uvp->pre_close_xrc_domain		= NULL;
	p_uvp->post_close_xrc_domain	= mlx4_post_close_xrc_domain;
	p_uvp->pre_create_xrc_rcv_qp	= NULL;
	p_uvp->post_create_xrc_rcv_qp	= NULL;
	p_uvp->pre_modify_xrc_rcv_qp	= NULL;
	p_uvp->post_modify_xrc_rcv_qp	= NULL;
	p_uvp->pre_query_xrc_rcv_qp		= NULL;
	p_uvp->post_query_xrc_rcv_qp	= NULL;
	p_uvp->pre_reg_xrc_rcv_qp		= NULL;
	p_uvp->post_reg_xrc_rcv_qp		= NULL;
	p_uvp->pre_unreg_xrc_rcv_qp		= NULL;
	p_uvp->post_unreg_xrc_rcv_qp	= NULL;
}
#endif

__declspec(dllexport) ib_api_status_t
uvp_get_interface (GUID iid, void* pifc)
{
	ib_api_status_t	status = IB_SUCCESS;

	if (IsEqualGUID(&iid, &IID_UVP))
	{
		__get_uvp_interface((uvp_interface_t *) pifc);
	}
	else
	{
		status = IB_UNSUPPORTED;
	}

	return status;
}
