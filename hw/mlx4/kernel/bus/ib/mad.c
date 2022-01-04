/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include <mlx4_debug.h>
#include "mlx4_ib.h"
#include <ib_mad.h>
#include <ib_smi.h>
#include <ib_cache.h>
#include "cmd.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mad.tmh"
#endif

static int is_vendor_id(__be16 attr_id)
{
	return ((attr_id & IB_SMP_ATTR_VENDOR_MASK) == IB_SMP_ATTR_VENDOR_MASK);
}

static int supported_vendor_id(__be16 attr_id)
{
	//
	// This function is preperation for later use. In the future we can specify which 
	// mad we want to move to FW and which not. Meantime all messages are downloaded 
	// to FW. The FW failed it if it unsupported
	//
	UNUSED_PARAM(attr_id);
	return 1;
}

#ifdef FLEX10_FOR_IB_SUPPORT
/* 
The below function are not implemented in IB driver.
They are found in Linux Core
	ib_free_send_mad
	ib_create_send_mad
	ib_post_send_mad
These functions can be replaced by low-level ones:	
	ib_alloc_pd
	ib_dealloc_pd
	ib_destroy_cq
	ib_create_qp
	ib_modify_qp
	ib_destroy_qp
	ib_get_dma_mr
	ib_dereg_mr
*/

#pragma pack(push,1)
struct mlx4_tunnel_mad {
	struct ib_grh grh;
	struct mlx4_ib_tunnel_header hdr;
	struct ib_mad mad;
} __attribute__ ((packed));

void mlx4_ib_tunnel_comp_worker(
   IN PDEVICE_OBJECT  DeviceObject,
   IN struct ib_cq *cq );

/* XXX until SM multi-gid support is in place, all multicast records contain
 * the default port gid in the sgid; thus we need to map it back to the
 * guest-specific gid */
static int mlx4_ib_demux_sa_handler(struct ib_device *ibdev, int port, int slave,
	struct ib_sa_mad *sa_mad)
{
	if (sa_mad->mad_hdr.attr_id == IB_MAD_ATTR_MCMEMBER_RECORD) {
		if (sa_mad->mad_hdr.method == IB_MGMT_METHOD_GET_RESP) {
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: restoring ingress mcast_get_resp gid slave id from:%d to:%d\n",
				to_mdev(ibdev)->dev->pdev->name, sa_mad->data[16 + MLX4_SLAVE_ID_GID_OFFSET],
				to_mdev(ibdev)->sriov.demux[port - 1].gid_id_base + slave));
            
			sa_mad->data[16 + MLX4_SLAVE_ID_GID_OFFSET] =
				(u8)(to_mdev(ibdev)->sriov.demux[port - 1].gid_id_base + slave);
		}
	}
	return 0;
}

static int mlx4_ib_find_real_gid(struct ib_device *ibdev, u8 port, __be64 guid)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	int i;

	for (i = 0; i < dev->dev->caps.sqp_demux; i++) {
		if (dev->sriov.demux[port - 1].guid_cache[i] == guid)
			return i;
		}
	return -1;
}

static int mlx4_ib_demux_mad(struct ib_device *ibdev, u8 port,
	ib_wc_t *wc, struct ib_grh *grh,
	struct ib_mad *mad)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct ib_mad_agent *agent = NULL;
	struct ib_mad_send_buf *send_buf;
	struct ib_ah_attr attr;
	struct mlx4_ib_ah *mlx4_ah;
	int dqpn;	int err;
	int slave;
	u8 *slave_id;

	/* Initially assume that this mad is for us */
	slave = slave_gid_index(dev);

	/* See if the slave id is encoded in a response mad */
	if (mad->mad_hdr.method & 0x80) {
		slave_id = (u8*) &mad->mad_hdr.tid;
		slave = *slave_id;
		*slave_id = 0;
		/* remap tid */	
	}

	/* If a grh is present, we demux according to it */
	if (wc->recv.ud.recv_opt & IB_RECV_OPT_GRH_VALID) {
		slave = mlx4_ib_find_real_gid(ibdev, port, grh->dgid.global.interface_id);
		if (slave < 0) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: failed matching grh\n",
				to_mdev(ibdev)->dev->pdev->name ));
			return -ENOENT;
		}
	}

	/* Class-specific handling */
	switch (mad->mad_hdr.mgmt_class) {
	case IB_MGMT_CLASS_SUBN_ADM:
		if (mlx4_ib_demux_sa_handler(ibdev, port, slave, (struct ib_sa_mad *) mad))
			return 0;
		break;

	case IB_MGMT_CLASS_CM:
		break;

	default:
		/* Drop unsupported classes for slaves in tunnel mode */
		if (slave != slave_gid_index(dev)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: dropping unsupported ingress mad from class:%d "
				"for slave:%d\n", to_mdev(ibdev)->dev->pdev->name,
				mad->mad_hdr.mgmt_class, slave));
			return 0;
		}
	}

	/* The demux function owns qp1 and thus receives mads directly */
	if (slave == slave_gid_index(dev))
		return -ENOENT;
	dqpn = mlx4_get_slave_sqp(to_mdev(ibdev)->dev, slave);
	if (!dqpn) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: couldn't find sqpn of slave:%d\n", 
			to_mdev(ibdev)->dev->pdev->name,slave));
		return 0;
	}
#if 0	
	if (wc->qp->qp_num) {
		agent = to_mdev(ibdev)->send_agent[port - 1][1];
		dqpn += port + 1;
	} else 
#endif	
	{
		agent = to_mdev(ibdev)->send_agent[port - 1][0];
		dqpn += port - 1;
	}

	/* Our agent might not yet be registered when mads start to arrive */
	if (!agent)
		return 0;

	memset(&attr, 0, sizeof attr);
	attr.dlid = to_mdev(ibdev)->sriov.local_lid[port - 1]; /* What about dlid path-bits? do we want to integrate them? */
	attr.port_num = port;
	attr.sl = wc->recv.ud.remote_sl;
	attr.src_path_bits = 0; /* N.A. - we forward the original source lid */
	attr.static_rate = 0;
	if (wc->recv.ud.recv_opt & IB_RECV_OPT_GRH_VALID) {
		attr.ah_flags = IB_AH_GRH;
		attr.grh.dgid = grh->dgid;
		attr.grh.flow_label = be32_to_cpu(grh->version_tclass_flow) & 0xfffff;
		attr.grh.hop_limit = grh->hop_limit;
		attr.grh.sgid_index = 0; /* N.A. - we forward the original sgid */
		attr.grh.traffic_class = (u8)((be32_to_cpu(grh->version_tclass_flow) >> 20) & 0xff);
	}

	send_buf = ib_create_send_mad(agent, dqpn, wc->recv.ud.pkey_index, 0, IB_MGMT_MAD_HDR,
		IB_MGMT_MAD_DATA, GFP_KERNEL);
	if (IS_ERR(send_buf))
		return 0;
	memcpy(send_buf->mad, mad, sizeof *mad);
	send_buf->ah = ib_create_ah(agent->qp->pd, &attr);
	if (!IS_ERR(send_buf->ah)) {
		mlx4_ah = to_mah(send_buf->ah);
		mlx4_ah->av.port_pd |= cpu_to_be32(0x80000000); /* Mark force loopback */
		mlx4_ah->ex = kmalloc(sizeof (struct mlx4_ib_ah_ext), GFP_KERNEL);
		mlx4_ah->gsi_demux_lb = 0; /* this time GSI mads are forwarded to the slave qp1 */
		if (mlx4_ah->ex) {
			/* Insert source information as if this packet was received from
			* the wire */
			memcpy(mlx4_ah->ex->sgid, &grh->sgid, 16);
			mlx4_ah->ex->slid = wc->recv.ud.remote_lid;
			mlx4_ah->ex->sqpn = wc->recv.ud.remote_qp;
			send_buf->context[0] = send_buf->ah;
			err = ib_post_send_mad(send_buf, NULL);
			if (!err) {
				#if 0
					mlx4_warn(to_mdev(ibdev)->dev, "%s: Tunnelling ingress mad - port:%d slid:%d "
					"dlid:%d sqp:%d dqpn:%d\n", 
					to_mdev(ibdev)->dev->pdev->name, port, wc->slid,
					attr.dlid, wc->src_qp, dqpn);
					if (wc->wc_flags & IB_WC_GRH) {
						mlx4_warn(to_mdev(ibdev)->dev, "%s: sgid_hi:0x%016llx sgid_low:0x%016llx\n",
							to_mdev(ibdev)->dev->pdev->name,
							be64_to_cpu(grh->sgid.global.subnet_prefix),
							be64_to_cpu(grh->sgid.global.interface_id));
						mlx4_warn(to_mdev(ibdev)->dev, "%s: dgid_hi:0x%016llx dgid_low:0x%016llx\n",
							to_mdev(ibdev)->dev->pdev->name,
							be64_to_cpu(attr.grh.dgid.global.subnet_prefix),
							be64_to_cpu(attr.grh.dgid.global.interface_id));
						}
				#endif
				return 0;
			}
		}

		ib_destroy_ah(send_buf->ah);
	}

	ib_free_send_mad(send_buf);	
	return 0;
}

static void mlx4_ib_tunnel_comp_handler(void *arg)
{
	struct mlx4_ib_demux_ctx *ctx = arg;

	UNUSED_PARAM(arg);
	
	if (ctx->port)
		IoQueueWorkItem( ctx->work_item, mlx4_ib_tunnel_comp_worker,
			DelayedWorkQueue, ctx->cq );
}

static int mlx4_ib_post_tunnel_buf(struct mlx4_ib_demux_ctx *ctx, int index)
{
	ib_local_ds_t sg_list;
	ib_recv_wr_t recv_wr, *bad_recv_wr;

	sg_list.vaddr = ctx->ring[index].map.da;
	sg_list.length = sizeof (struct mlx4_tunnel_mad);
	sg_list.lkey = ctx->mr->lkey;

	recv_wr.p_next = NULL;
	recv_wr.ds_array = &sg_list;
	recv_wr.num_ds = 1;
	recv_wr.wr_id = index;
	return ib_post_recv(ctx->qp, &recv_wr, &bad_recv_wr);
}

/* XXX Until SM multi-gid support is in place, we do some ugly hacks for multicast packets:
 * - Silently drop multicast leave packets
 * - An ugly hack to force the supplied gid to the default port gid
 */
static int mlx4_ib_multiplex_sa_handler(struct ib_device *ibdev, int port, struct ib_sa_mad *sa_mad)
{
	if (sa_mad->mad_hdr.attr_id == IB_MAD_ATTR_MCMEMBER_RECORD) {
		if (sa_mad->mad_hdr.method == IB_MAD_METHOD_DELETE) {
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: dropping mcast_leave mad\n", to_mdev(ibdev)->dev->pdev->name));
			return -EINVAL;
		}
		if (sa_mad->mad_hdr.method == IB_MGMT_METHOD_SET) {
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: setting egress mcast_set gid slave id from:%d to:%d\n",
				     to_mdev(ibdev)->dev->pdev->name, sa_mad->data[16 + MLX4_SLAVE_ID_GID_OFFSET],
				     to_mdev(ibdev)->sriov.demux[port - 1].gid_id_base));
			sa_mad->data[16 + MLX4_SLAVE_ID_GID_OFFSET] = 
				     to_mdev(ibdev)->sriov.demux[port - 1].gid_id_base;
		}
	}
	return 0;
}

static void mlx4_ib_multiplex_mad(struct mlx4_ib_demux_ctx *ctx, ib_wc_t *wc)
{
	struct mlx4_ib_dev *dev = to_mdev(ctx->ib_dev);
	struct ib_mad_agent *agent = NULL;
	struct mlx4_tunnel_mad *tunnel = ctx->ring[wc->wr_id].addr;
	struct ib_mad_send_buf *send_buf;
	struct mlx4_ib_ah ah;
	struct ib_ah_attr ah_attr;
	u8 sgid_index;
	u8 *slave_id;
	u32 qpn;
	int ret;
	int slave;

	/* Get slave that sent this packet */
	for (slave = 0; slave < dev->dev->caps.sqp_demux; slave++) {
		qpn = mlx4_get_slave_sqp(dev->dev, slave);
		if (qpn + ctx->port - 1 == wc->recv.ud.remote_qp) {
			agent = dev->send_agent[ctx->port - 1][0];
			break;
		}
		if (qpn + ctx->port + 1 == wc->recv.ud.remote_qp) {
			agent = dev->send_agent[ctx->port - 1][1];
			break;
		}
	}
	if (slave == dev->dev->caps.sqp_demux)
		return;

	/* Map transaction ID */
	switch (tunnel->mad.mad_hdr.method) {
	case IB_MGMT_METHOD_SET:
	case IB_MGMT_METHOD_GET:
	case IB_MGMT_METHOD_REPORT:
	case IB_MAD_METHOD_GETTABLE:
	case IB_MAD_METHOD_DELETE:
	case IB_MAD_METHOD_GETMULTI:
	case IB_MAD_METHOD_GETTRACETABLE:
		slave_id = (u8*) &tunnel->mad.mad_hdr.tid;
		if (*slave_id) {
			/* XXX TODO: hold a mapping instead of failing */
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: egress mad has non-null tid msb:%d "
				     "class:%d slave:%d\n", 
				     to_mdev(ctx->ib_dev)->dev->pdev->name, *slave_id,
				     tunnel->mad.mad_hdr.mgmt_class, slave));
			return;
		} else
			*slave_id = (u8)slave;
	default:
		/* nothing */;
	}

	/* Class-specific handling */
	switch (tunnel->mad.mad_hdr.mgmt_class) {
	case IB_MGMT_CLASS_SUBN_ADM:
		if (mlx4_ib_multiplex_sa_handler(ctx->ib_dev, ctx->port,
			      (struct ib_sa_mad *) &tunnel->mad))
			return;
		break;
	case IB_MGMT_CLASS_CM:
		break;
	default:
		/* Drop unsupported classes for slaves in tunnel mode */
		if (slave != slave_gid_index(dev)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: dropping unsupported egress mad from class:%d "
				     "for slave:%d\n", 
				     to_mdev(ctx->ib_dev)->dev->pdev->name, tunnel->mad.mad_hdr.mgmt_class, slave));
			return;
		}
	}

	send_buf = ib_create_send_mad(agent, tunnel->hdr.remote_qpn,
				      tunnel->hdr.pkey_index, 0, IB_MGMT_MAD_HDR,
				      IB_MGMT_MAD_DATA, GFP_KERNEL);
	if (IS_ERR(send_buf))
		return;
	memcpy(send_buf->mad, &tunnel->mad, sizeof tunnel->mad);

	/* We are using standard ib_core services to send the mad, so generate a
	 * stadard address handle by decoding the tunnelled mlx4_ah fields */
	memcpy(&ah.av, &tunnel->hdr.av, sizeof (struct mlx4_av));
	mlx4_ib_query_ah(&ah.ibah, &ah_attr);
	if ((ah_attr.ah_flags & IB_AH_GRH) &&
	    (ah_attr.grh.sgid_index != slave)) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s:%d accessed invalid sgid_index:%d\n",
			to_mdev(ctx->ib_dev)->dev->pdev->name, slave, ah_attr.grh.sgid_index));
		goto err_mad;
	}

	/* ib_create_ah() can only access our own virtual gids, so cache the real gid
	 * index, create an ah with gid0, and then modify the mlx4 representation */
	sgid_index = ah_attr.grh.sgid_index;
	ah_attr.grh.sgid_index = 0;
	send_buf->ah = ib_create_ah(agent->qp->pd, &ah_attr);
	if (!IS_ERR(send_buf->ah)) {
		to_mah(send_buf->ah)->av.gid_index = sgid_index; /* restore real gid */
		to_mah(send_buf->ah)->gsi_demux_lb = 0; /* this time GSI mads are sent to the wire */
		send_buf->context[0] = send_buf->ah;
		ret = ib_post_send_mad(send_buf, NULL);
		if (!ret)
			return;
		ib_destroy_ah(send_buf->ah);
	}
err_mad:
	ib_free_send_mad(send_buf);
}

static int mlx4_ib_alloc_tunnel_bufs(struct mlx4_ib_demux_ctx *ctx)
{
	int i;

	for (i = 0; i < MLX4_NUM_TUNNEL_BUFS; i++) {
#if 1
		ctx->ring[i].addr = dma_alloc_coherent(&to_mdev(ctx->ib_dev)->dev->pdev->dev,
			PAGE_SIZE, &ctx->ring[i].map, GFP_KERNEL);
		if (!ctx->ring[i].addr)
			goto err;
#else
		ctx->ring[i].addr = kmalloc(sizeof (struct mlx4_tunnel_mad), GFP_KERNEL);
		if (!ctx->ring[i].addr)
			goto err;
		ctx->ring[i].map = ib_dma_map_single(ctx->ib_dev,
						     ctx->ring[i].addr,
						     sizeof (struct mlx4_tunnel_mad),
						     DMA_FROM_DEVICE);
#endif
	}
	return 0;

err:
	while (i > 0) {
		--i;
		#if 1
			dma_free_coherent( &to_mdev(ctx->ib_dev)->dev->pdev->dev, 
				PAGE_SIZE, 	ctx->ring[i].addr, ctx->ring[i].map );
		#else
		ib_dma_unmap_single(ctx->ib_dev, ctx->ring[i].map,
						 sizeof (struct mlx4_tunnel_mad),
						 DMA_FROM_DEVICE);
		#endif
		kfree(ctx->ring[i].addr);
	}
	return -ENOMEM;
}

static void mlx4_ib_free_tunnel_bufs(struct mlx4_ib_demux_ctx *ctx)
{
	int i;

	for (i = 0; i < MLX4_NUM_TUNNEL_BUFS; i++) {
#if 1
			dma_free_coherent( &to_mdev(ctx->ib_dev)->dev->pdev->dev, 
				PAGE_SIZE, 	ctx->ring[i].addr, ctx->ring[i].map );
#else
		ib_dma_unmap_single(ctx->ib_dev, ctx->ring[i].map,
				    sizeof (struct mlx4_tunnel_mad),
				    DMA_FROM_DEVICE);
#endif
		kfree(ctx->ring[i].addr);
	}
}

static void mlx4_ib_tunnel_comp_worker(
	IN PDEVICE_OBJECT  DeviceObject,
	IN struct ib_cq *cq )
{
	int ret;
	ib_wc_t					wc;
	ib_wc_t*				p_free_wc = &wc;
	ib_wc_t*				p_done_wc;
	struct mlx4_ib_demux_ctx *ctx = cq->cq_context;

	UNUSED_PARAM(DeviceObject);

	ASSERT( cq == ctx->cq );
	
	ib_req_notify_cq(ctx->cq, IB_CQ_NEXT_COMP);

	for (;;) {
		ret = cq->device->poll_cq(cq, &p_free_wc, &p_done_wc);
		if (ret < 0 || !p_done_wc)
			break;

		if (wc.status == IB_WC_SUCCESS) {
			switch (wc.wc_type) {
			case IB_WC_RECV:
				mlx4_ib_multiplex_mad(ctx, &wc);
				ret = mlx4_ib_post_tunnel_buf(ctx, (int)wc.wr_id);
				if (ret)
					MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Failed reposting tunnel "
							"buf:%I64d\n", wc.wr_id));

				break;
			default:
				BUG_ON(1);
				break;
			}
		} else
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "mlx4_ib: completion error in tunnnel:%d\n", ctx->port));
	}
}

static int mlx4_ib_alloc_demux_ctx(struct mlx4_ib_dev *dev,
				    struct mlx4_ib_demux_ctx *ctx,
				    int port)
{
	struct ib_device *ib_dev = &dev->ib_dev;
	struct ib_qp_init_attr qp_init_attr;
	struct ib_qp_attr attr;
	char name[10];
	int ret;
	int i;

	memset(ctx, 0, sizeof *ctx);
	ctx->ib_dev = &dev->ib_dev;
	ret = mlx4_ib_alloc_tunnel_bufs(ctx);
	if (ret) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Failed allocating tunnel bufs\n"));
		return -ENOMEM;
	}
	ctx->cq = ib_dev->create_cq(ib_dev, MLX4_NUM_TUNNEL_BUFS, 0, NULL, NULL);
	if (IS_ERR(ctx->cq)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't create tunnel CQ\n"));
		goto err_buf;
	}
	ctx->cq->device        = ib_dev;
	ctx->cq->p_uctx        = NULL;
	ctx->cq->comp_handler  = mlx4_ib_tunnel_comp_handler;
	ctx->cq->event_handler = NULL;
	ctx->cq->cq_context    = ctx;

	ctx->pd = ib_alloc_pd(ib_dev);
	if (IS_ERR(ctx->pd)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't create tunnel PD\n"));
		ret = PTR_ERR(ctx->pd);
		goto err_cq;
	}

	ctx->mr = ib_get_dma_mr(ctx->pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(ctx->mr)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't get tunnel DMA MR\n"));
		ret = PTR_ERR(ctx->mr);
		goto err_pd;
	}

	memset(&qp_init_attr, 0, sizeof qp_init_attr);
	qp_init_attr.send_cq = ctx->cq;
	qp_init_attr.recv_cq = ctx->cq;
	qp_init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	qp_init_attr.cap.max_send_wr = 0;
	qp_init_attr.cap.max_recv_wr = MLX4_NUM_TUNNEL_BUFS;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;
	qp_init_attr.qp_type = IB_QPT_UD;
	qp_init_attr.port_num = (u8)port;
	qp_init_attr.qp_context = ctx;
	qp_init_attr.create_flags = MLX4_IB_QP_TUNNEL;
	qp_init_attr.event_handler = NULL;
	ctx->qp = ib_create_qp(ctx->pd, &qp_init_attr);
	if (IS_ERR(ctx->qp)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't create tunnel QP\n"));
		ret = PTR_ERR(ctx->qp);
		goto err_mr;
	}

	memset(&attr, 0, sizeof attr);
	attr.qp_state = IB_QPS_INIT;
	attr.pkey_index = 0;
	attr.qkey = IB_QP1_QKEY;
	attr.port_num = (u8)port;
	ret = ib_modify_qp(ctx->qp, &attr, IB_QP_STATE | IB_QP_PKEY_INDEX |
					   IB_QP_QKEY | IB_QP_PORT);
	if (ret) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't change tunnel qp state to INIT\n"));
		goto err_qp;
	}
	attr.qp_state = IB_QPS_RTR;
	ret = ib_modify_qp(ctx->qp, &attr, IB_QP_STATE);
	if (ret) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't change tunnel qp state to RTR\n"));
		goto err_qp;
	}
	attr.qp_state = IB_QPS_RTS;
	attr.sq_psn = 0;
	ret = ib_modify_qp(ctx->qp, &attr, IB_QP_STATE | IB_QP_SQ_PSN);
	if (ret) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't change tunnel qp state to RTS\n"));
		goto err_qp;
	}

	for (i = 0; i < MLX4_NUM_TUNNEL_BUFS; i++) {
		ret = mlx4_ib_post_tunnel_buf(ctx, i);
		if (ret)
			goto err_qp;
	}

	snprintf(name, sizeof name, "mlx4_ib%d", port);
	ctx->work_item = IoAllocateWorkItem( dev->dev->pdev->p_self_do );
	if (!ctx->work_item) {
		ret = -ENOMEM;
		goto err_qp;
	}

	ctx->port = port;
	ret = ib_req_notify_cq(ctx->cq, IB_CQ_NEXT_COMP);
	if (ret) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't arm tunnel cq\n"));
		goto err_wq;
	}
	return 0;

err_wq:
	IoFreeWorkItem(ctx->work_item);

err_qp:
	ib_destroy_qp(ctx->qp);

err_mr:
	ib_dereg_mr(ctx->mr);

err_pd:
	ib_dealloc_pd(ctx->pd);

err_cq:
	ib_destroy_cq(ctx->cq);

err_buf:
	mlx4_ib_alloc_tunnel_bufs(ctx);
	return ret;
}

static void mlx4_ib_free_demux_ctx(struct mlx4_ib_demux_ctx *ctx)
{
	if (ctx) {
		ctx->port = 0;
		IoFreeWorkItem(ctx->work_item);
		ib_destroy_qp(ctx->qp);
		mlx4_ib_free_tunnel_bufs(ctx);
		ib_dereg_mr(ctx->mr);
		ib_dealloc_pd(ctx->pd);
		ib_destroy_cq(ctx->cq);
	}
}

int mlx4_ib_init_sriov(struct mlx4_ib_dev *dev)
{
	int i;
	int err;
	u8 *ptr;

	if (!mlx4_is_mfunc(dev->dev))
		return 0;

	/* XXX user-space RDMACM relies on unqie node guids to distinguish among
	 * cma devices. */
	ptr = &((u8*) &dev->ib_dev.node_guid)[MLX4_SLAVE_ID_NODE_GUID_OFFSET];
	*ptr = *ptr + (u8)slave_gid_index(dev);
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: mutli-function enabled\n", dev->dev->pdev->name));
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: using node_guid:0x%016I64x\n",
		dev->dev->pdev->name, be64_to_cpu(dev->ib_dev.node_guid)));

	if (!dev->dev->caps.sqp_demux)
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: operating in qp1 tunnel mode\n", dev->dev->pdev->name));

	if (dev->dev->caps.sqp_demux) {
		/* XXX until we have proper SM support, we mannually assign
		 * additional port guids for guests */
		if (mlx4_ib_set_slave_guids(&dev->ib_dev))
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: Failed setting slave guids\n", dev->dev->pdev->name));
	
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: starting demux service for %d qp1 clients\n",
			dev->dev->pdev->name, dev->dev->caps.sqp_demux));
		for (i = 0; i < dev->num_ports; i++) {
			err = mlx4_ib_alloc_demux_ctx(dev, &dev->sriov.demux[i], i + 1);
			if (err)
				goto err;
		}
	}
	return 0;

err:
	while (i > 0) {
		mlx4_ib_free_demux_ctx(&dev->sriov.demux[i]);
		--i;
	}
	return err;
}

void mlx4_ib_close_sriov(struct mlx4_ib_dev *dev)
{
	int i;

    if (!mlx4_is_mfunc(dev->dev))
        return 0;

	if (dev->dev->caps.sqp_demux)
		for (i = 0; i < dev->num_ports; i++)
			mlx4_ib_free_demux_ctx(&dev->sriov.demux[i]);
}

#else

int mlx4_ib_init_sriov(struct mlx4_ib_dev *dev)
{
	UNUSED_PARAM(dev);
	return 0;
}

void mlx4_ib_close_sriov(struct mlx4_ib_dev *dev)
{
	UNUSED_PARAM(dev);
}

#endif

struct ext_info_t{
	__be32		my_qpn;
	u32		reserved1;
	__be32		rqpn;
	u8		sl;
	u8		g_path;
	u16		reserved2[2];
	__be16		pkey;
	u32		reserved3[11];
	u8		grh[40];
};

int mlx4_MAD_IFC(struct mlx4_ib_dev *dev, int ignore_mkey, int ignore_bkey,
		 int port, ib_wc_t *in_wc, struct ib_grh *in_grh,
		 void *in_mad, void *response_mad)
{
	struct mlx4_cmd_mailbox *inmailbox, *outmailbox;
	u8 *inbox;
	int err;
	u32 in_modifier = port;
	u8 op_modifier = 0;

	inmailbox = mlx4_alloc_cmd_mailbox(dev->dev);
	if (IS_ERR(inmailbox))
		return PTR_ERR(inmailbox);
	inbox = (u8*)inmailbox->buf;

	outmailbox = mlx4_alloc_cmd_mailbox(dev->dev);
	if (IS_ERR(outmailbox)) {
		mlx4_free_cmd_mailbox(dev->dev, inmailbox);
		return PTR_ERR(outmailbox);
	}

	memcpy(inbox, in_mad, 256);

	/*
	 * Key check traps can't be generated unless we have in_wc to
	 * tell us where to send the trap.
	 */
	if (ignore_mkey || !in_wc)
		op_modifier |= 0x1;
	if (ignore_bkey || !in_wc)
		op_modifier |= 0x2;

	if (in_wc) {
		ext_info_t *ext_info;

		memset(inbox + 256, 0, 256);
		ext_info = (ext_info_t*)(inbox + 256);

		ext_info->rqpn   = in_wc->recv.ud.remote_qp;
		ext_info->sl     = in_wc->recv.ud.remote_sl << 4;
		ext_info->g_path = in_wc->recv.ud.path_bits |
			(in_wc->recv.ud.recv_opt & IB_RECV_OPT_GRH_VALID ? 0x80 : 0);
		ext_info->pkey   = cpu_to_be16(in_wc->recv.ud.pkey_index);

		if (in_grh)
			memcpy(ext_info->grh, in_grh, 40);

		op_modifier |= 0x4;

		in_modifier |= be16_to_cpu(in_wc->recv.ud.remote_lid) << 16;
	}

	err = mlx4_cmd_box(dev->dev, inmailbox->dma.da, outmailbox->dma.da,
			   in_modifier, op_modifier,
			   MLX4_CMD_MAD_IFC, MLX4_CMD_TIME_CLASS_C);

	if (!err)
		memcpy(response_mad, outmailbox->buf, 256);

//	mlx4_dbg( dev->dev, "%s: mlx4_MAD_IFC : port %d, err %d \n", dev->dev->pdev->name, port, err );

	mlx4_free_cmd_mailbox(dev->dev, inmailbox);
	mlx4_free_cmd_mailbox(dev->dev, outmailbox);

	return err;
}

static void update_sm_ah(struct mlx4_ib_dev *dev, u8 port_num, u16 lid, u8 sl)
{
	struct ib_ah *new_ah;
	struct ib_ah_attr ah_attr;

	if (!dev->send_agent[port_num - 1][0])
		return;

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.dlid     = lid;
	ah_attr.sl       = sl;
	ah_attr.port_num = port_num;

	new_ah = ib_create_ah(dev->send_agent[port_num - 1][0]->qp->pd,
			      &ah_attr);
	if (IS_ERR(new_ah))
		return;

	spin_lock(&dev->sm_lock);
	if (dev->sm_ah[port_num - 1])
		ib_destroy_ah(dev->sm_ah[port_num - 1]);
	dev->sm_ah[port_num - 1] = new_ah;
	spin_unlock(&dev->sm_lock);
}


/*
 * Snoop SM MADs for port info and P_Key table sets, so we can
 * synthesize LID change and P_Key change events.
 */
static void smp_snoop(struct ib_device *ibdev, u8 port_num, struct ib_mad *mad)
{
	struct ib_event event;

	if ((mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    mad->mad_hdr.method == IB_MGMT_METHOD_SET) {
		if (mad->mad_hdr.attr_id == IB_SMP_ATTR_PORT_INFO) {
			struct ib_port_info *pinfo =
				(struct ib_port_info *) ((struct ib_smp *) mad)->data;

			update_sm_ah(to_mdev(ibdev), port_num,
				     be16_to_cpu(pinfo->sm_lid),
				     pinfo->neighbormtu_mastersmsl & 0xf);

			event.device	       = ibdev;
			event.element.port_num = port_num;

			if(pinfo->clientrereg_resv_subnetto & 0x80)
				event.event    = IB_EVENT_CLIENT_REREGISTER;
			else
				event.event    = IB_EVENT_LID_CHANGE;

			ib_dispatch_event(&event);
		}

		if (mad->mad_hdr.attr_id == IB_SMP_ATTR_PKEY_TABLE) {
			event.device	       = ibdev;
			event.event	       = IB_EVENT_PKEY_CHANGE;
			event.element.port_num = port_num;
			ib_dispatch_event(&event);
		}
	}
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
static void node_desc_override(struct ib_device *dev,
			       struct ib_mad *mad)
{
	if ((mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    mad->mad_hdr.method == IB_MGMT_METHOD_GET_RESP &&
	    mad->mad_hdr.attr_id == IB_SMP_ATTR_NODE_DESC) {
		spin_lock(&to_mdev(dev)->sm_lock);
        C_ASSERT(sizeof(((struct ib_smp*)mad)->data) == sizeof(dev->node_desc));
		memcpy(((struct ib_smp *) mad)->data, dev->node_desc, sizeof(dev->node_desc));
		spin_unlock(&to_mdev(dev)->sm_lock);
	}
}

int mlx4_ib_process_mad(struct ib_device *ibdev, int mad_flags,	u8 port_num,
			ib_wc_t *in_wc, struct ib_grh *in_grh,
			struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	u16 slid;
	int err;

	if (mlx4_is_barred(ibdev->dma_device))
		return -EFAULT;

#if 0
	/* XXX debug - print source of qp1 messages */
	if (in_wc->qp->qp_num) {
		mlx4_dbg(to_mdev(ibdev)->dev, "%s: received MAD: slid:%d sqpn:%d "
			"dlid_bits:%d dqpn:%d wc_flags:0x%x\n",
			to_mdev(ibdev)->dev->pdev->name, 
			in_wc->slid, in_wc->src_qp,
			in_wc->dlid_path_bits,
			in_wc->qp->qp_num,
			in_wc->wc_flags);
		if (in_wc->wc_flags & IB_WC_GRH) {
			mlx4_dbg(to_mdev(ibdev)->dev, "%s: sgid_hi:0x%016llx sgid_lo:0x%016llx\n",
				to_mdev(ibdev)->dev->pdev->name, 
				be64_to_cpu(in_grh->sgid.global.subnet_prefix),
				be64_to_cpu(in_grh->sgid.global.interface_id));
			mlx4_dbg(to_mdev(ibdev)->dev, "%s: dgid_hi:0x%016llx dgid_lo:0x%016llx\n",
				to_mdev(ibdev)->dev->pdev->name, 
				be64_to_cpu(in_grh->dgid.global.subnet_prefix),
				be64_to_cpu(in_grh->dgid.global.interface_id));
		}
	}
#endif	

	slid = in_wc ? be16_to_cpu(in_wc->recv.ud.remote_lid) : be16_to_cpu(XIB_LID_PERMISSIVE);

	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP && slid == 0) {
		// we never comes here !
		ASSERT(0);
		MLX4_PRINT( TRACE_LEVEL_ERROR ,MLX4_DBG_MAD ,
			(" Received a trap from HCA, which is unexpected here !\n" ));
		// forward_trap(to_mdev(ibdev), port_num, in_mad);
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
	}

#ifdef FLEX10_FOR_IB_SUPPORT
	if (to_mdev(ibdev)->dev->caps.sqp_demux) {
		err = mlx4_ib_demux_mad(ibdev, port_num, in_wc, in_grh, in_mad);
		if (!err)
			return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
	}
#endif	

	if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	    in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
		if (in_mad->mad_hdr.method   != IB_MGMT_METHOD_GET &&
		    in_mad->mad_hdr.method   != IB_MGMT_METHOD_SET &&
		    in_mad->mad_hdr.method   != IB_MGMT_METHOD_TRAP_REPRESS)
			return IB_MAD_RESULT_SUCCESS;

		/*
		 * Don't process SMInfo queries -- the SMA can't handle them.
		 */
		if (in_mad->mad_hdr.attr_id == IB_SMP_ATTR_SM_INFO)
			return IB_MAD_RESULT_SUCCESS;
	} else if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_PERF_MGMT ||
		   in_mad->mad_hdr.mgmt_class == IB_MLX_VENDOR_CLASS1   ||
		   in_mad->mad_hdr.mgmt_class == IB_MLX_VENDOR_CLASS2   ||
		   in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_CONG_MGMT ) {
		if (in_mad->mad_hdr.method  != IB_MGMT_METHOD_GET &&
		    in_mad->mad_hdr.method  != IB_MGMT_METHOD_SET)
			return IB_MAD_RESULT_SUCCESS;
	} else
		return IB_MAD_RESULT_SUCCESS;

	err = mlx4_MAD_IFC(to_mdev(ibdev),
			   mad_flags & IB_MAD_IGNORE_MKEY,
			   mad_flags & IB_MAD_IGNORE_BKEY,
			   port_num, in_wc, in_grh, in_mad, out_mad);
	if (err)
		return IB_MAD_RESULT_FAILURE;

	if (!out_mad->mad_hdr.status) {
		smp_snoop(ibdev, port_num, in_mad);
		node_desc_override(ibdev, out_mad);
	}

	/* set return bit in status of directed route responses */
	if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		out_mad->mad_hdr.status |= cpu_to_be16(1 << 15);

	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP_REPRESS)
		/* no response for trap repress */
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

