/* 
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Cisco Systems. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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

#include <ib_smi.h>

#include "mx_abi.h"
#include "mthca_dev.h"
#include "mt_pa_cash.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mthca_provider.tmh"
#endif
#include "mthca_cmd.h"
#include "mthca_memfree.h"

static void init_query_mad(struct ib_smp *mad)
{
	 mad->base_version	= 1;
	 mad->mgmt_class		= IB_MGMT_CLASS_SUBN_LID_ROUTED;
	 mad->class_version = 1;
	 mad->method				= IB_MGMT_METHOD_GET;
}

int mthca_query_device(struct ib_device *ibdev,
			      struct ib_device_attr *props)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	struct mthca_dev* mdev = to_mdev(ibdev);

	u8 status;

	RtlZeroMemory(props, sizeof *props);

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_INFO;

	err = mthca_MAD_IFC(mdev, 1, 1,
	    1, NULL, NULL, in_mad, out_mad, &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	props->fw_ver              = mdev->fw_ver;
	props->device_cap_flags    = mdev->device_cap_flags;
	props->vendor_id           = cl_ntoh32(*(__be32 *) (out_mad->data + 36)) &
		0xffffff;
	props->vendor_part_id      = cl_ntoh16(*(__be16 *) (out_mad->data + 30));
	props->hw_ver              = cl_ntoh32(*(__be32 *) (out_mad->data + 32));
	memcpy(&props->sys_image_guid, out_mad->data +  4, 8);
	props->max_mr_size         = ~0Ui64;
	props->page_size_cap       = mdev->limits.page_size_cap;
	props->max_qp              = mdev->limits.num_qps - mdev->limits.reserved_qps;
	props->max_qp_wr           = mdev->limits.max_wqes;
	props->max_sge             = mdev->limits.max_sg;
	props->max_cq              = mdev->limits.num_cqs - mdev->limits.reserved_cqs;
	props->max_cqe             = mdev->limits.max_cqes;
	props->max_mr              = mdev->limits.num_mpts - mdev->limits.reserved_mrws;
	props->max_pd              = mdev->limits.num_pds - mdev->limits.reserved_pds;
	props->max_qp_rd_atom      = 1 << mdev->qp_table.rdb_shift;
	props->max_qp_init_rd_atom = mdev->limits.max_qp_init_rdma;
	props->max_res_rd_atom     = props->max_qp_rd_atom * props->max_qp;
	props->max_srq             = mdev->limits.num_srqs - mdev->limits.reserved_srqs;
	props->max_srq_wr          = mdev->limits.max_srq_wqes;
	if (mthca_is_memfree(mdev))
		--props->max_srq_wr;
	props->max_srq_sge         = mdev->limits.max_srq_sge;
	props->local_ca_ack_delay  = (u8)mdev->limits.local_ca_ack_delay;
	props->atomic_cap          = mdev->limits.flags & DEV_LIM_FLAG_ATOMIC ? 
					IB_ATOMIC_LOCAL : IB_ATOMIC_NONE;
	props->max_pkeys           = (u16)mdev->limits.pkey_table_len;
	props->max_mcast_grp       = mdev->limits.num_mgms + mdev->limits.num_amgms;
	props->max_mcast_qp_attach = MTHCA_QP_PER_MGM;
	props->max_total_mcast_qp_attach = props->max_mcast_qp_attach * 
					   props->max_mcast_grp;
	props->max_ah              = mdev->limits.num_avs;

	/*
	 * If Sinai memory key optimization is being used, then only
	 * the 8-bit key portion will change.  For other HCAs, the
	 * unused index bits will also be used for FMR remapping.
	 */
	if (mdev->mthca_flags & MTHCA_FLAG_SINAI_OPT)
		props->max_map_per_fmr = 255;
	else
		props->max_map_per_fmr =
			(1 << (32 - long_log2(mdev->limits.num_mpts))) - 1;

	err = 0;
 out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mthca_query_port(struct ib_device *ibdev,
			    u8 port, struct ib_port_attr *props)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	u8 status;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cl_hton32(port);

	err = mthca_MAD_IFC(to_mdev(ibdev), 1, 1,
			    port, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	RtlZeroMemory(props, sizeof *props);
	props->lid               = cl_ntoh16(*(__be16 *) (out_mad->data + 16));
	props->lmc               = out_mad->data[34] & 0x7;
	props->sm_lid            = cl_ntoh16(*(__be16 *) (out_mad->data + 18));
	props->sm_sl             = out_mad->data[36] & 0xf;
	props->state             = out_mad->data[32] & 0xf;
	props->phys_state        = out_mad->data[33] >> 4;
	props->port_cap_flags    = cl_ntoh32(*(__be32 *) (out_mad->data + 20));
	props->gid_tbl_len       = to_mdev(ibdev)->limits.gid_table_len;
	props->max_msg_sz        = 0x80000000;
	props->pkey_tbl_len      = (u16)to_mdev(ibdev)->limits.pkey_table_len;
	props->bad_pkey_cntr     = cl_ntoh16(*(__be16 *) (out_mad->data + 46));
	props->qkey_viol_cntr    = cl_ntoh16(*(__be16 *) (out_mad->data + 48));
	props->active_width      = out_mad->data[31] & 0xf;
	props->active_speed      = out_mad->data[35] >> 4;
	props->max_mtu           = out_mad->data[41] & 0xf;
	props->active_mtu        = out_mad->data[36] >> 4;
	props->subnet_timeout    = out_mad->data[51] & 0x1f;

 out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mthca_modify_port(struct ib_device *ibdev,
			     u8 port, int port_modify_mask,
			     struct ib_port_modify *props)
{
	struct mthca_set_ib_param set_ib;
	struct ib_port_attr attr;
	int err;
	u8 status;

	if (down_interruptible(&to_mdev(ibdev)->cap_mask_mutex))
		return -EFAULT;

	err = mthca_query_port(ibdev, port, &attr);
	if (err)
		goto out;

	set_ib.set_si_guid     = 0;
	set_ib.reset_qkey_viol = !!(port_modify_mask & IB_PORT_RESET_QKEY_CNTR);

	set_ib.cap_mask = (attr.port_cap_flags | props->set_port_cap_mask) &
		~props->clr_port_cap_mask;

	err = mthca_SET_IB(to_mdev(ibdev), &set_ib, port, &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

out:
	up(&to_mdev(ibdev)->cap_mask_mutex);
	return err;
}

static int mthca_query_pkey_chunk(struct ib_device *ibdev,
			    u8 port, u16 index, __be16 pkey[32])
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	u8 status;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PKEY_TABLE;
	in_mad->attr_mod = cl_hton32(index / 32);

	err = mthca_MAD_IFC(to_mdev(ibdev), 1, 1,
			    port, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	{ // copy the results
		int i;
		__be16 *pkey_chunk = (__be16 *)out_mad->data;
		for (i=0; i<32; ++i) 
			pkey[i] = pkey_chunk[i];
	}

 out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static int mthca_query_gid_chunk(struct ib_device *ibdev, u8 port,
			   int index, union ib_gid gid[8])
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	u8 status;
	__be64	subnet_prefix;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cl_hton32(port);

	err = mthca_MAD_IFC(to_mdev(ibdev), 1, 1,
			    port, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	memcpy(&subnet_prefix, out_mad->data + 8, 8);

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_GUID_INFO;
	in_mad->attr_mod = cl_hton32(index / 8);

	err = mthca_MAD_IFC(to_mdev(ibdev), 1, 1,
			    port, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	{ // copy the results
		int i;
		__be64 *guid = (__be64 *)out_mad->data;
		for (i=0; i<8; ++i) {
			gid[i].global.subnet_prefix = subnet_prefix;
			gid[i].global.interface_id = guid[i];
		}
	}

 out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

struct ib_ucontext *mthca_alloc_ucontext(struct ib_device *ibdev,
						ci_umv_buf_t* const	p_umv_buf)
{
	struct ibv_get_context_resp uresp;
	struct mthca_ucontext           *context;
	int                              err;

	RtlZeroMemory(&uresp, sizeof uresp);

	uresp.qp_tab_size = to_mdev(ibdev)->limits.num_qps;
	if (mthca_is_memfree(to_mdev(ibdev)))
		uresp.uarc_size = to_mdev(ibdev)->uar_table.uarc_size;
	else
		uresp.uarc_size = 0;

	context = kzalloc(sizeof *context, GFP_KERNEL);
	if (!context) {
		err = -ENOMEM;
		goto err_nomem;
	}

	err = mthca_uar_alloc(to_mdev(ibdev), &context->uar);
	if (err) 
		goto err_uar_alloc;

	/*
	* map uar to user space
	*/

	/* map UAR to kernel */
	context->kva = ioremap((io_addr_t)context->uar.pfn << PAGE_SHIFT, PAGE_SIZE,&context->uar_size);
	if (!context->kva) {
		HCA_PRINT( TRACE_LEVEL_ERROR, HCA_DBG_LOW ,("Couldn't map kernel access region, aborting.\n") );
		err = -ENOMEM;
		goto err_ioremap;
	}

	/* build MDL */
	context->mdl = IoAllocateMdl( context->kva, (ULONG)context->uar_size,
		FALSE, TRUE, NULL );
	if( !context->mdl ) {
		err = -ENOMEM;
		goto err_alloc_mdl;
	}
	MmBuildMdlForNonPagedPool( context->mdl );

	/* Map the memory into the calling process's address space. */
	__try 	{
		context->ibucontext.user_uar = MmMapLockedPagesSpecifyCache( context->mdl,
			UserMode, MmNonCached, NULL, FALSE, NormalPagePriority );
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		err = -EACCES;
		goto err_map;
	}

	/* user_db_tab */
	context->db_tab = mthca_init_user_db_tab(to_mdev(ibdev));
	if (IS_ERR(context->db_tab)) {
		err = PTR_ERR(context->db_tab);
		goto err_init_user;
	}

	err = ib_copy_to_umv_buf(p_umv_buf, &uresp, sizeof uresp);
	if (err) 
		goto err_copy_to_umv_buf;

	context->ibucontext.device = ibdev;
	
	atomic_set(&context->ibucontext.usecnt, 0);
	return &context->ibucontext;

err_copy_to_umv_buf:
	mthca_cleanup_user_db_tab(to_mdev(ibdev), &context->uar,
		context->db_tab);
err_init_user:	
	MmUnmapLockedPages( context->ibucontext.user_uar, context->mdl );
err_map:
	IoFreeMdl(context->mdl);
err_alloc_mdl:	
	iounmap(context->kva, PAGE_SIZE);
err_ioremap:	
	mthca_uar_free(to_mdev(ibdev), &context->uar);
err_uar_alloc:
	kfree(context);
err_nomem:	
	return ERR_PTR(err);
}

 int mthca_dealloc_ucontext(struct ib_ucontext *context)
{
	struct mthca_ucontext		*mucontext = to_mucontext(context);

	mthca_cleanup_user_db_tab(to_mdev(context->device), &mucontext->uar,
		mucontext->db_tab);
	MmUnmapLockedPages( mucontext->ibucontext.user_uar, mucontext->mdl );
	IoFreeMdl(mucontext->mdl);
	iounmap(mucontext->kva, PAGE_SIZE);
	mthca_uar_free(to_mdev(context->device), &mucontext->uar);
	kfree(mucontext);
	
	return 0;
}

struct ib_pd *mthca_alloc_pd(struct ib_device *ibdev,
				    struct ib_ucontext *context,
				    ci_umv_buf_t* const			p_umv_buf)
{
	int err;
	struct mthca_pd *pd;
	struct ibv_alloc_pd_resp resp;

	/* sanity check */
	if (p_umv_buf && p_umv_buf->command) {
		if (p_umv_buf->output_size < sizeof(struct ibv_alloc_pd_resp)) {
			err = -EINVAL;
			goto err_param;
		}
	}
	
	pd = kmalloc(sizeof *pd, GFP_KERNEL);
	if (!pd) {
		err = -ENOMEM;
		goto err_mem;
	}

	err = mthca_pd_alloc(to_mdev(ibdev), !context, pd);
	if (err) {
		goto err_pd_alloc;
	}

	if (p_umv_buf && p_umv_buf->command) {
		resp.pd_handle = (u64)(UINT_PTR)pd;
		resp.pdn = pd->pd_num;
		if (ib_copy_to_umv_buf(p_umv_buf, &resp, sizeof(struct ibv_alloc_pd_resp))) {
			err = -EFAULT;
			goto err_copy;
		}
	}

	return &pd->ibpd;

err_copy:	
	mthca_pd_free(to_mdev(ibdev), pd);
err_pd_alloc:
	kfree(pd);
err_mem:
err_param:
	return ERR_PTR(err);
}

int mthca_dealloc_pd(struct ib_pd *pd)
{
	mthca_pd_free(to_mdev(pd->device), to_mpd(pd));
	kfree(pd);
	return 0;
}

static struct ib_ah *mthca_ah_create(struct ib_pd *pd,
				     struct ib_ah_attr *ah_attr)
{
	int err;
	struct mthca_ah *ah;

	ah = kzalloc(sizeof *ah, GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	err = mthca_create_ah(to_mdev(pd->device), to_mpd(pd), ah_attr, ah);
	if (err) {
		kfree(ah);
		return ERR_PTR(err);
	}

	return &ah->ibah;
}

static int mthca_ah_destroy(struct ib_ah *ah)
{
	mthca_destroy_ah(to_mdev(ah->device), to_mah(ah));
	kfree(ah);

	return 0;
}

static struct ib_srq *mthca_create_srq(struct ib_pd *pd,
				struct ib_srq_init_attr *init_attr,
				ci_umv_buf_t* const p_umv_buf)
{
	struct ibv_create_srq ucmd = { 0 };
	struct mthca_ucontext *context = NULL;
	struct mthca_srq *srq;
	int err;

	srq = kzalloc(sizeof *srq, GFP_KERNEL);
	if (!srq)
		return ERR_PTR(-ENOMEM);

	if (pd->ucontext) {
		context = to_mucontext(pd->ucontext);

		if (ib_copy_from_umv_buf(&ucmd, p_umv_buf, sizeof ucmd)) {
			err = -EFAULT;
			goto err_free;
		}
		err = mthca_map_user_db(to_mdev(pd->device), &context->uar,
					context->db_tab, ucmd.db_index,
					ucmd.db_page, NULL);

		if (err)
			goto err_free;

		srq->mr.ibmr.lkey = ucmd.lkey;
		srq->db_index     = ucmd.db_index;
	}

	err = mthca_alloc_srq(to_mdev(pd->device), to_mpd(pd),
		&init_attr->attr, srq);

	if (err && pd->ucontext)
		mthca_unmap_user_db(to_mdev(pd->device), &context->uar,
			context->db_tab, ucmd.db_index);

	if (err)
		goto err_free;

	if (context && ib_copy_to_umv_buf(p_umv_buf, &srq->srqn, sizeof (u32))) {
		mthca_free_srq(to_mdev(pd->device), srq);
		err = -EFAULT;
		goto err_free;
	}

	return &srq->ibsrq;

err_free:
	kfree(srq);

	return ERR_PTR(err);
}

static int mthca_destroy_srq(struct ib_srq *srq)
{
	struct mthca_ucontext *context;

	if (srq->ucontext) {
		context = to_mucontext(srq->ucontext);

		mthca_unmap_user_db(to_mdev(srq->device), &context->uar,
			context->db_tab, to_msrq(srq)->db_index);
	}

	mthca_free_srq(to_mdev(srq->device), to_msrq(srq));
	kfree(srq);

	return 0;
}

static struct ib_qp *mthca_create_qp(struct ib_pd *pd,
				     struct ib_qp_init_attr *init_attr,
				      ci_umv_buf_t* const			p_umv_buf)
{
	struct ibv_create_qp ucmd = {0};
	struct mthca_qp *qp = NULL;
	struct mthca_ucontext *context = NULL;
	int err;

	switch (init_attr->qp_type) {
	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
	case IB_QPT_UNRELIABLE_DGRM:
	{

		qp = kmalloc(sizeof *qp, GFP_KERNEL);
		if (!qp) {
			err = -ENOMEM;
			goto err_mem;
		}

		if (pd->ucontext) {
			context = to_mucontext(pd->ucontext);

			if (ib_copy_from_umv_buf(&ucmd, p_umv_buf, sizeof ucmd)) {
				err = -EFAULT;
				goto err_copy;
			}

			err = mthca_map_user_db(to_mdev(pd->device), &context->uar,
						context->db_tab,
						ucmd.sq_db_index, ucmd.sq_db_page, NULL);
			if (err) 
				goto err_map1;

			err = mthca_map_user_db(to_mdev(pd->device), &context->uar,
						context->db_tab,
						ucmd.rq_db_index, ucmd.rq_db_page, NULL);
			if (err) 
				goto err_map2;

			qp->mr.ibmr.lkey = ucmd.lkey;
			qp->sq.db_index  = ucmd.sq_db_index;
			qp->rq.db_index  = ucmd.rq_db_index;
		}

		err = mthca_alloc_qp(to_mdev(pd->device), to_mpd(pd),
				     to_mcq(init_attr->send_cq),
				     to_mcq(init_attr->recv_cq),
				     init_attr->qp_type, init_attr->sq_sig_type,
				     &init_attr->cap, qp);

		if (err)
			if (pd->ucontext) 
				goto err_alloc_qp_user;
			else 
				goto err_copy;

		qp->ibqp.qp_num = qp->qpn;
		break;
	}
	case IB_QPT_QP0:
	case IB_QPT_QP1:
	{
		/* Don't allow userspace to create special QPs */
		if (pd->ucontext) {
			err = -EINVAL;
			goto err_inval;
		}

		qp = kmalloc(sizeof (struct mthca_sqp), GFP_KERNEL);
		if (!qp) {
			err = -ENOMEM;
			goto err_mem;
		}

		qp->ibqp.qp_num = init_attr->qp_type == IB_QPT_QP0 ? 0 : 1;

		err = mthca_alloc_sqp(to_mdev(pd->device), to_mpd(pd),
				      to_mcq(init_attr->send_cq),
				      to_mcq(init_attr->recv_cq),
				      init_attr->sq_sig_type, &init_attr->cap,
				      qp->ibqp.qp_num, init_attr->port_num,
				      to_msqp(qp));
		if (err)
			goto err_alloc_sqp;
		
		break;
	}
	default:
		/* Don't support raw QPs */
		err = -ENOSYS;
		goto err_unsupported;
	}

	init_attr->cap.max_send_wr     = qp->sq.max;
	init_attr->cap.max_recv_wr     = qp->rq.max;
	init_attr->cap.max_send_sge    = qp->sq.max_gs;
	init_attr->cap.max_recv_sge    = qp->rq.max_gs;
	init_attr->cap.max_inline_data    = qp->max_inline_data;

	return &qp->ibqp;

		
err_alloc_qp_user:
	if (pd->ucontext) 
		mthca_unmap_user_db(to_mdev(pd->device),
			&context->uar, context->db_tab, ucmd.rq_db_index);
err_map2:
	if (pd->ucontext) 
		mthca_unmap_user_db(to_mdev(pd->device),
			&context->uar, context->db_tab, ucmd.sq_db_index);
err_map1: err_copy: err_alloc_sqp:
	if (qp)
		kfree(qp);
err_mem: err_inval:	err_unsupported:
	return ERR_PTR(err);
}

static int mthca_destroy_qp(struct ib_qp *qp)
{
	if (qp->ucontext) {
		mthca_unmap_user_db(to_mdev(qp->device),
				    &to_mucontext(qp->ucontext)->uar,
				    to_mucontext(qp->ucontext)->db_tab,
				    to_mqp(qp)->sq.db_index);
		mthca_unmap_user_db(to_mdev(qp->device),
				    &to_mucontext(qp->ucontext)->uar,
				    to_mucontext(qp->ucontext)->db_tab,
				    to_mqp(qp)->rq.db_index);
	}
	mthca_free_qp(to_mdev(qp->device), to_mqp(qp));
	kfree(qp);
	return 0;
}

static struct ib_cq *mthca_create_cq(struct ib_device *ibdev, int entries,
				     struct ib_ucontext *context,
				     ci_umv_buf_t* const			p_umv_buf)
{
	struct ibv_create_cq ucmd = {0};
	struct mthca_cq *cq;
	int nent;
	int err;
	void *u_arm_db_page = 0;

	if (entries < 1 || entries > to_mdev(ibdev)->limits.max_cqes)	
		return ERR_PTR(-EINVAL);

	if (context) {
		if (ib_copy_from_umv_buf(&ucmd, p_umv_buf, sizeof ucmd))
			return ERR_PTR(-EFAULT);

		err = mthca_map_user_db(to_mdev(ibdev), &to_mucontext(context)->uar,
					to_mucontext(context)->db_tab,
					ucmd.set_db_index, ucmd.set_db_page, NULL);
		if (err)
			return ERR_PTR(err);

		err = mthca_map_user_db(to_mdev(ibdev), &to_mucontext(context)->uar,
					to_mucontext(context)->db_tab,
					ucmd.arm_db_index, ucmd.arm_db_page, NULL);
		if (err)
			goto err_unmap_set;

		err = mthca_map_user_db(to_mdev(ibdev), &to_mucontext(context)->uar,
					to_mucontext(context)->db_tab,
					ucmd.u_arm_db_index, 
					(u64)(ULONG_PTR)PAGE_ALIGN(ucmd.u_arm_db_page), 
					&u_arm_db_page);
		if (err)
			goto err_unmap_arm;
	}

	cq = kmalloc(sizeof *cq, GFP_KERNEL);
	if (!cq) {
		err = -ENOMEM;
		goto err_unmap_ev;
	}

	if (context) {
		cq->mr.ibmr.lkey = ucmd.lkey;
		cq->set_ci_db_index = ucmd.set_db_index;
		cq->arm_db_index    = ucmd.arm_db_index;
		cq->u_arm_db_index    = ucmd.u_arm_db_index;
		cq->p_u_arm_sn = (volatile u32 *)((char*)u_arm_db_page + BYTE_OFFSET(ucmd.u_arm_db_page));
	}

	for (nent = 1; nent <= entries; nent <<= 1)
		; /* nothing */

	err = mthca_init_cq(to_mdev(ibdev), nent, 
			    context ? to_mucontext(context) : NULL,
			    context ? ucmd.mr.pdn : to_mdev(ibdev)->driver_pd.pd_num,
			    cq);
	if (err)
		goto err_free;

	if (context) {
		struct ibv_create_cq_resp *create_cq_resp = (struct ibv_create_cq_resp *)(ULONG_PTR)p_umv_buf->p_inout_buf;
		create_cq_resp->cqn = cq->cqn;
	}

	HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_LOW ,
		("uctx %p, cq_hndl %p, cq_num %#x, cqe  %#x\n",
		context, &cq->ibcq, cq->cqn, cq->ibcq.cqe ) );
	
	return &cq->ibcq;

err_free:
	kfree(cq);

err_unmap_ev:
	if (context)
		mthca_unmap_user_db(to_mdev(ibdev), &to_mucontext(context)->uar,
				    to_mucontext(context)->db_tab, ucmd.u_arm_db_index);

err_unmap_arm:
	if (context)
		mthca_unmap_user_db(to_mdev(ibdev), &to_mucontext(context)->uar,
				    to_mucontext(context)->db_tab, ucmd.arm_db_index);

err_unmap_set:
	if (context)
		mthca_unmap_user_db(to_mdev(ibdev), &to_mucontext(context)->uar,
				    to_mucontext(context)->db_tab, ucmd.set_db_index);

	return ERR_PTR(err);
}

static int mthca_destroy_cq(struct ib_cq *cq)
{
	if (cq->ucontext) {
		mthca_unmap_user_db(to_mdev(cq->device),
				    &to_mucontext(cq->ucontext)->uar,
				    to_mucontext(cq->ucontext)->db_tab,
				    to_mcq(cq)->u_arm_db_index);
		mthca_unmap_user_db(to_mdev(cq->device),
				    &to_mucontext(cq->ucontext)->uar,
				    to_mucontext(cq->ucontext)->db_tab,
				    to_mcq(cq)->arm_db_index);
		mthca_unmap_user_db(to_mdev(cq->device),
				    &to_mucontext(cq->ucontext)->uar,
				    to_mucontext(cq->ucontext)->db_tab,
				    to_mcq(cq)->set_ci_db_index);
	}
	mthca_free_cq(to_mdev(cq->device), to_mcq(cq));
	kfree(cq);

	return 0;
}

static
mthca_mpt_access_t
map_qp_mpt(
	IN				mthca_qp_access_t				qp_acl)
{
#define ACL_MTHCA(mfl,ifl) if (qp_acl & mfl)   mpt_acl |= ifl
	mthca_mpt_access_t mpt_acl = 0;

	ACL_MTHCA(MTHCA_ACCESS_REMOTE_READ,MTHCA_MPT_FLAG_REMOTE_READ);
	ACL_MTHCA(MTHCA_ACCESS_REMOTE_WRITE,MTHCA_MPT_FLAG_REMOTE_WRITE);
	ACL_MTHCA(MTHCA_ACCESS_REMOTE_ATOMIC,MTHCA_MPT_FLAG_ATOMIC);
	ACL_MTHCA(MTHCA_ACCESS_LOCAL_WRITE,MTHCA_MPT_FLAG_LOCAL_WRITE);

	return (mpt_acl | MTHCA_MPT_FLAG_LOCAL_READ);
}

struct ib_mr *mthca_get_dma_mr(struct ib_pd *pd, mthca_qp_access_t acc)
{
	struct mthca_mr *mr;
	int err;

	mr = kzalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	err = mthca_mr_alloc_notrans(to_mdev(pd->device),
				     to_mpd(pd)->pd_num,
				     map_qp_mpt(acc), mr);

	if (err) {
		kfree(mr);
		return ERR_PTR(err);
	}

	return &mr->ibmr;
}

static struct ib_mr *mthca_reg_phys_mr(struct ib_pd       *pd,
				       struct ib_phys_buf *buffer_list,
				       int                 num_phys_buf,
				       mthca_qp_access_t                 acc,
				       u64                *iova_start)
{
	struct mthca_mr *mr;
	u64 *page_list;
	u64 total_size;
	u64 mask;
	int shift;
	int npages;
	int err;
	int i, j, n;

	/* First check that we have enough alignment */
	if ((*iova_start & ~PAGE_MASK) != (buffer_list[0].addr & ~PAGE_MASK))
		return ERR_PTR(-EINVAL);

	if (num_phys_buf > 1 &&
	    ((buffer_list[0].addr + buffer_list[0].size) & ~PAGE_MASK))
		return ERR_PTR(-EINVAL);

	mask = 0;
	total_size = 0;
	for (i = 0; i < num_phys_buf; ++i) {
		if (i != 0)
			mask |= buffer_list[i].addr;
		if (i != num_phys_buf - 1)
			mask |= buffer_list[i].addr + buffer_list[i].size;

		total_size += buffer_list[i].size;
	}

	if (mask & ~PAGE_MASK)
		return ERR_PTR(-EINVAL);

	/* Find largest page shift we can use to cover buffers */
	for (shift = PAGE_SHIFT; shift < 31; ++shift)
		if (num_phys_buf > 1) {
			if ((1Ui64 << shift) & mask)
				break;
		} else {
			if (1Ui64 << shift >=
			    buffer_list[0].size +
			    (buffer_list[0].addr & ((1Ui64 << shift) - 1)))
				break;
		}

	buffer_list[0].size += buffer_list[0].addr & ((1Ui64 << shift) - 1);
	buffer_list[0].addr &= ~0Ui64 << shift;

	mr = kzalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	npages = 0;
	for (i = 0; i < num_phys_buf; ++i)
		npages += (int)((buffer_list[i].size + (1Ui64 << shift) - 1) >> shift);

	if (!npages)
		return &mr->ibmr;

	page_list = kmalloc(npages * sizeof *page_list, GFP_KERNEL);
	if (!page_list) {
		kfree(mr);
		return ERR_PTR(-ENOMEM);
	}

	n = 0;
	for (i = 0; i < num_phys_buf; ++i)
		for (j = 0;
		     j < (buffer_list[i].size + (1Ui64 << shift) - 1) >> shift;
		     ++j)
			page_list[n++] = buffer_list[i].addr + ((u64) j << shift);

	HCA_PRINT( TRACE_LEVEL_VERBOSE ,HCA_DBG_LOW ,("Registering memory at %I64x (iova %I64x) "
		  "in PD %x; shift %d, npages %d.\n",
		  (u64) buffer_list[0].addr,
		  (u64) *iova_start,
		  to_mpd(pd)->pd_num,
		  shift, npages));

	err = mthca_mr_alloc_phys(to_mdev(pd->device),
				  to_mpd(pd)->pd_num,
				  page_list, shift, npages,
				  *iova_start, total_size,
				  map_qp_mpt(acc), mr);

	if (err) {
		kfree(page_list);
		kfree(mr);
		return ERR_PTR(err);
	}

	kfree(page_list);
	return &mr->ibmr;
}

static struct ib_mr *mthca_reg_virt_mr(struct ib_pd *pd, 
	void* vaddr, uint64_t length, uint64_t hca_va,
	mthca_qp_access_t acc, boolean_t um_call, boolean_t secure)
{
	struct mthca_dev *dev = to_mdev(pd->device);
	struct mthca_mr *mr;
	u64 *pages;
	int err = 0;
	uint32_t i, n;
	mt_iobuf_t *iobuf_p;
	mt_iobuf_iter_t iobuf_iter;
	ib_access_t ib_acc;

	/*
	 * Be friendly to WRITE_MTT command and leave two 
	 * empty slots for the  index and reserved fields of the mailbox.
	 */
	int max_buf_list_size = PAGE_SIZE / sizeof (u64) - 2;

	HCA_ENTER(HCA_DBG_MEMORY);

	mr = kzalloc(sizeof *mr, GFP_KERNEL);
	if (!mr) {
		err = -ENOMEM;
		goto err_nomem;
	}

	/*
	 * We ask for writable memory if any access flags other than
	 * "remote read" are set.  "Local write" and "remote write"
	 * obviously require write access.  "Remote atomic" can do
	 * things like fetch and add, which will modify memory, and
	 * "MW bind" can change permissions by binding a window.
	 */

	// try register the buffer
	iobuf_p = &mr->iobuf;
	iobuf_init( (ULONG_PTR)vaddr, length, um_call, iobuf_p);
	ib_acc = (acc & ~MTHCA_ACCESS_REMOTE_READ) ? IB_AC_LOCAL_WRITE : 0;
	err =  iobuf_register_with_cash( (ULONG_PTR)vaddr, length, um_call, 
		&ib_acc, iobuf_p );
	if (err)
		goto err_reg_mem;
	mr->iobuf_used = TRUE;

	// allocate MTT's
	mr->mtt = mthca_alloc_mtt(dev, iobuf_p->nr_pages);
	if (IS_ERR(mr->mtt)) {
		err = PTR_ERR(mr->mtt);
		goto err_alloc_mtt;
	}

	// allocate buffer_list for writing MTT's
	pages = (u64 *) kmalloc(PAGE_SIZE,GFP_KERNEL);
	if (!pages) {
		err = -ENOMEM;
		goto err_pages;
	}

	// write MTT's
	iobuf_iter_init( iobuf_p, &iobuf_iter );
	n = 0;
	for (;;) {
		// get up to  max_buf_list_size page physical addresses
		i = iobuf_get_tpt_seg( iobuf_p, &iobuf_iter, max_buf_list_size, pages );
		if (!i)
			break;

		//TODO: convert physical adresses to dma one's

		// write 'i' dma addresses
		err = mthca_write_mtt(dev, mr->mtt, n, pages, i);
		if (err)
			goto err_write_mtt;
		n += i;
		if (n >= iobuf_p->nr_pages)
			break;
	}

	CL_ASSERT(n == iobuf_p->nr_pages);
	
	// write MPT
	err = mthca_mr_alloc(dev, to_mpd(pd)->pd_num, PAGE_SHIFT, hca_va,
		length, map_qp_mpt(acc), mr);
	if (err)
		goto err_mt_alloc;

	// secure memory
	if (!pd->ucontext || !secure)
		goto done;
	__try {
		mr->secure_handle = MmSecureVirtualMemory ( vaddr, (SIZE_T)length,
			(ib_acc & IB_AC_LOCAL_WRITE) ? PAGE_READWRITE : PAGE_READONLY );
		if (mr->secure_handle == NULL) {
			err = -EFAULT;
			goto err_secure;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		NTSTATUS Status = GetExceptionCode();
		UNUSED_PARAM_WOWPP(Status);
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY ,
			("Exception 0x%x on MmSecureVirtualMemory(), addr %p, size %I64d, access %#x\n", 
			Status, vaddr, length, acc ));
		err = -EFAULT;
		goto err_secure;
	}

done:	
	free_page((void*) pages);

	HCA_EXIT(HCA_DBG_MEMORY);
	return &mr->ibmr;

err_secure:
err_mt_alloc:
err_write_mtt:
	free_page((void*) pages);
err_pages:
	mthca_free_mtt(dev, mr->mtt);
err_alloc_mtt:
	iobuf_deregister(iobuf_p);
err_reg_mem:	
	kfree(mr);
err_nomem:	

	HCA_EXIT(HCA_DBG_MEMORY);
	return ERR_PTR(err);
}

int mthca_dereg_mr(struct ib_mr *mr)
{
	struct mthca_mr *mmr = to_mmr(mr);
	struct mthca_dev* dev = to_mdev(mr->device);

	if (mmr->secure_handle) {
		__try {
			MmUnsecureVirtualMemory( mmr->secure_handle );
			mmr->secure_handle = NULL;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			NTSTATUS Status = GetExceptionCode();
			UNUSED_PARAM_WOWPP(Status);
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY ,
				("Exception 0x%x on MmUnsecureVirtualMemory(), addr %I64x, size %I64x, seg_num %d, nr_pages %d\n", 
				Status, mmr->iobuf.va, (u64)mmr->iobuf.size, mmr->iobuf.seg_num, mmr->iobuf.nr_pages ));
		}
	}
	mthca_free_mr(dev, mmr);
	if (mmr->iobuf_used)
		iobuf_deregister_with_cash(&mmr->iobuf);
	kfree(mmr);
	return 0;
}

static struct ib_fmr *mthca_alloc_fmr(struct ib_pd *pd, mthca_qp_access_t acc,
				      struct ib_fmr_attr *fmr_attr)
{
	struct mthca_fmr *fmr;
	int err;

	fmr = kzalloc(sizeof *fmr, GFP_KERNEL);
	if (!fmr)
		return ERR_PTR(-ENOMEM);

	RtlCopyMemory(&fmr->attr, fmr_attr, sizeof *fmr_attr);
	err = mthca_fmr_alloc(to_mdev(pd->device), to_mpd(pd)->pd_num,
			     map_qp_mpt(acc), fmr);

	if (err) {
		kfree(fmr);
		return ERR_PTR(err);
	}

	return &fmr->ibfmr;
}

static int mthca_dealloc_fmr(struct ib_fmr *fmr)
{
	struct mthca_fmr *mfmr = to_mfmr(fmr);
	int err;

	err = mthca_free_fmr(to_mdev(fmr->device), mfmr);
	if (err)
		return err;

	kfree(mfmr);
	return 0;
}

static int mthca_unmap_fmr(struct list_head *fmr_list)
{
	struct ib_fmr *fmr;
	int err;
	u8 status;
	struct mthca_dev *mdev = NULL;

	list_for_each_entry(fmr, fmr_list, list,struct ib_fmr) {
		if (mdev && to_mdev(fmr->device) != mdev)
			return -EINVAL;
		mdev = to_mdev(fmr->device);
	}

	if (!mdev)
		return 0;

	if (mthca_is_memfree(mdev)) {
		list_for_each_entry(fmr, fmr_list, list,struct ib_fmr)
			mthca_arbel_fmr_unmap(mdev, to_mfmr(fmr));

		wmb();
	} else
		list_for_each_entry(fmr, fmr_list, list,struct ib_fmr)
			mthca_tavor_fmr_unmap(mdev, to_mfmr(fmr));

	err = mthca_SYNC_TPT(mdev, &status);
	if (err)
		return err;
	if (status)
		return -EINVAL;
	return 0;
}

static int mthca_init_node_data(struct mthca_dev *dev)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	u8 status;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_INFO;

	err = mthca_MAD_IFC(dev, 1, 1,
			    1, NULL, NULL, in_mad, out_mad,
			    &status);
	if (err)
		goto out;
	if (status) {
		err = -EINVAL;
		goto out;
	}

	memcpy(&dev->ib_dev.node_guid, out_mad->data + 12, 8);

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mthca_register_device(struct mthca_dev *dev)
{
	int ret;

	ret = mthca_init_node_data(dev);	
	if (ret)
		return ret;

	strlcpy(dev->ib_dev.name, "mthca%d", IB_DEVICE_NAME_MAX);
	dev->ib_dev.node_type            = IB_NODE_CA;
	dev->ib_dev.phys_port_cnt        = (u8)dev->limits.num_ports;
	dev->ib_dev.mdev           			= dev;
	dev->ib_dev.query_device         = mthca_query_device;
	dev->ib_dev.query_port           = mthca_query_port;
	dev->ib_dev.modify_port          = mthca_modify_port;
	dev->ib_dev.query_pkey_chunk           = mthca_query_pkey_chunk;
	dev->ib_dev.query_gid_chunk            = mthca_query_gid_chunk;
	dev->ib_dev.alloc_ucontext       = mthca_alloc_ucontext;
	dev->ib_dev.dealloc_ucontext     = mthca_dealloc_ucontext;
	dev->ib_dev.alloc_pd             = mthca_alloc_pd;
	dev->ib_dev.dealloc_pd           = mthca_dealloc_pd;
	dev->ib_dev.create_ah            = mthca_ah_create;
	dev->ib_dev.destroy_ah           = mthca_ah_destroy;

	if (dev->mthca_flags & MTHCA_FLAG_SRQ) {
		dev->ib_dev.create_srq           = mthca_create_srq;
		dev->ib_dev.modify_srq           = mthca_modify_srq;
		dev->ib_dev.query_srq            = mthca_query_srq;
		dev->ib_dev.destroy_srq          = mthca_destroy_srq;

		if (mthca_is_memfree(dev))
			dev->ib_dev.post_srq_recv = mthca_arbel_post_srq_recv;
		else
			dev->ib_dev.post_srq_recv = mthca_tavor_post_srq_recv;
	}

	dev->ib_dev.create_qp            = mthca_create_qp;
	dev->ib_dev.modify_qp            = mthca_modify_qp;
	dev->ib_dev.query_qp             = mthca_query_qp;
	dev->ib_dev.destroy_qp           = mthca_destroy_qp;
	dev->ib_dev.create_cq            = mthca_create_cq;
	dev->ib_dev.destroy_cq           = mthca_destroy_cq;
	dev->ib_dev.poll_cq              = mthca_poll_cq;
	dev->ib_dev.get_dma_mr           = mthca_get_dma_mr;
	dev->ib_dev.reg_phys_mr          = mthca_reg_phys_mr;
	dev->ib_dev.reg_virt_mr 		 = mthca_reg_virt_mr;
	dev->ib_dev.dereg_mr             = mthca_dereg_mr;

	if (dev->mthca_flags & MTHCA_FLAG_FMR) {
		dev->ib_dev.alloc_fmr            = mthca_alloc_fmr;
		dev->ib_dev.unmap_fmr            = mthca_unmap_fmr;
		dev->ib_dev.dealloc_fmr          = mthca_dealloc_fmr;
		if (mthca_is_memfree(dev))
			dev->ib_dev.map_phys_fmr = mthca_arbel_map_phys_fmr;
		else
			dev->ib_dev.map_phys_fmr = mthca_tavor_map_phys_fmr;
	}

	dev->ib_dev.attach_mcast         = mthca_multicast_attach;
	dev->ib_dev.detach_mcast         = mthca_multicast_detach;
	dev->ib_dev.process_mad          = mthca_process_mad;

	if (mthca_is_memfree(dev)) {
		dev->ib_dev.req_notify_cq = mthca_arbel_arm_cq;
		dev->ib_dev.post_send = mthca_arbel_post_send;
		dev->ib_dev.post_recv = mthca_arbel_post_recv;
	} else {
		dev->ib_dev.req_notify_cq = mthca_tavor_arm_cq;
		dev->ib_dev.post_send = mthca_tavor_post_send;
		dev->ib_dev.post_recv = mthca_tavor_post_recv;
	}

	KeInitializeMutex(&dev->cap_mask_mutex, 0);

	ret = ib_register_device(&dev->ib_dev);
	if (ret)
		return ret;

	mthca_start_catas_poll(dev);

	return 0;
}

void mthca_unregister_device(struct mthca_dev *dev)
{
	mthca_stop_catas_poll(dev);
	ib_unregister_device(&dev->ib_dev);
}
