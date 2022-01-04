/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
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

#include "mlx4_ib.h"
#include "ib_smi.h"
#include "driver.h"
#include "cmd.h"
#include "user.h"
#include "ib_cache.h"
#include "net\mlx4.h"
#include "mlx4_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "main.tmh"
#endif


#if	1//WORKAROUND_POLL_EQ
void mlx4_poll_eq(struct ib_device *dev, BOOLEAN bStart);
#endif

#define MLX4_ATTR_EXTENDED_PORT_INFO            cpu_to_be16(0xff90)


static void init_query_mad(struct ib_smp *mad)
{
	mad->base_version  = 1;
	mad->mgmt_class    = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	mad->class_version = 1;
	mad->method	   = IB_MGMT_METHOD_GET;
}

static int mlx4_ib_query_device(struct ib_device *ibdev,
				struct ib_device_attr *props)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	if (mlx4_is_barred(ibdev->dma_device))
		return -EFAULT;
	
	in_mad  = (ib_smp *)kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = (ib_smp *)kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_INFO;

	err = mlx4_MAD_IFC(dev, 1, 1, 1, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memset(props, 0, sizeof *props);

	props->fw_ver = dev->dev->caps.fw_ver;
	props->device_cap_flags    = IB_DEVICE_CHANGE_PHY_PORT |
		IB_DEVICE_PORT_ACTIVE_EVENT		|
		IB_DEVICE_SYS_IMAGE_GUID		|
		IB_DEVICE_RC_RNR_NAK_GEN		|
		IB_DEVICE_BLOCK_MULTICAST_LOOPBACK;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_BAD_PKEY_CNTR)
		props->device_cap_flags |= IB_DEVICE_BAD_PKEY_CNTR;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_BAD_QKEY_CNTR)
		props->device_cap_flags |= IB_DEVICE_BAD_QKEY_CNTR;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_APM)
		props->device_cap_flags |= IB_DEVICE_AUTO_PATH_MIG;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_UD_AV_PORT)
		props->device_cap_flags |= IB_DEVICE_UD_AV_PORT_ENFORCE;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_IPOIB_CSUM)
		props->device_cap_flags |= IB_DEVICE_IPOIB_CSUM;
	if (dev->dev->caps.max_gso_sz && dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_BLH)
		props->device_cap_flags |= IB_DEVICE_UD_TSO;
	if (dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_RESERVED_LKEY)
		props->device_cap_flags |= IB_DEVICE_LOCAL_DMA_LKEY;
	if ((dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_LOCAL_INV) &&
	    (dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_REMOTE_INV) &&
	    (dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_FAST_REG_WR))
		props->device_cap_flags |= IB_DEVICE_MEM_MGT_EXTENSIONS;
#ifdef SUPPORT_XRC	
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC)
		props->device_cap_flags |= IB_DEVICE_XRC;
#endif	
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_RAW_ETY)
		props->max_raw_ethy_qp = dev->ib_dev.phys_port_cnt;

	props->vendor_id	   = be32_to_cpup((__be32 *) (out_mad->data + 36)) &
		0xffffff;
	props->vendor_part_id	   = be16_to_cpup((__be16 *) (out_mad->data + 30));
	props->hw_ver		   = be32_to_cpup((__be32 *) (out_mad->data + 32));
	memcpy(&props->sys_image_guid, out_mad->data +	4, 8);

	props->max_mr_size	   = ~0ull;
	props->page_size_cap	   = dev->dev->caps.page_size_cap;
	props->max_qp		   = dev->dev->caps.num_qps - dev->dev->caps.reserved_qps;
	props->max_qp_wr	   = dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE;

	props->max_sge		   = min(dev->dev->caps.max_sq_sg,
					 dev->dev->caps.max_rq_sg);
	props->max_cq		   = dev->dev->caps.num_cqs - dev->dev->caps.reserved_cqs;
	props->max_cqe		   = dev->dev->caps.max_cqes;
	props->max_mr		   = dev->dev->caps.num_mpts - dev->dev->caps.reserved_mrws;
	props->max_pd		   = dev->dev->caps.num_pds - dev->dev->caps.reserved_pds;
	props->max_qp_rd_atom	   = dev->dev->caps.max_qp_dest_rdma;
	props->max_qp_init_rd_atom = dev->dev->caps.max_qp_init_rdma;
	props->max_res_rd_atom	   = props->max_qp_rd_atom * props->max_qp;
	props->max_srq		   = dev->dev->caps.num_srqs - dev->dev->caps.reserved_srqs;
	props->max_srq_wr	   = dev->dev->caps.max_srq_wqes - 1;
	props->max_srq_sge	   = dev->dev->caps.max_srq_sge;
	props->max_fast_reg_page_list_len = PAGE_SIZE / sizeof (u64);
	props->local_ca_ack_delay  = (u8)dev->dev->caps.local_ca_ack_delay;
	props->atomic_cap	   = dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_ATOMIC ?
		IB_ATOMIC_HCA : IB_ATOMIC_NON;
	props->max_pkeys	   = (u16)dev->dev->caps.pkey_table_len[1];
	props->max_mcast_grp	   = dev->dev->caps.num_mgms + dev->dev->caps.num_amgms;
	props->max_mcast_qp_attach = dev->dev->caps.num_qp_per_mgm;
	props->max_total_mcast_qp_attach = props->max_mcast_qp_attach *
					   props->max_mcast_grp;
	props->max_map_per_fmr = (1 << (32 - ilog2(dev->dev->caps.num_mpts))) - 1;
	props->bf_reg_size		= dev->dev->caps.bf_reg_size;
	props->bf_regs_per_page	= dev->dev->caps.bf_regs_per_page;
	props->max_sq_desc_sz	= dev->dev->caps.max_sq_desc_sz;

out:
	kfree(in_mad);
	kfree(out_mad);

	return err;
}

static enum rdma_transport_type
mlx4_ib_port_get_transport(struct ib_device *device, u8 port_num)
{
	struct mlx4_dev *dev = to_mdev(device)->dev;

	return dev->caps.transport_type[port_num];
}

static u8
mlx4_get_sl_for_ip_port(struct ib_device *device, u8 ca_port_num, u16 ip_port_num)
{
	struct mlx4_dev *dev = to_mdev(device)->dev; 
	struct mlx4_priv* priv = mlx4_priv(dev);
	struct mlx4_qos_settings qos_settings = priv->qos_settings[ca_port_num-1];
	int res= -1;

	spin_lock(&priv->ctx_lock);
	
	//Check for default settings.
	res = qos_settings.defaultSettingExist ? (int)qos_settings.priority : -1;
	
	if(qos_settings.count == 0)
	{
		goto out;
	}
	
	for(DWORD i=0;i<qos_settings.count;i++)
	{
		int port_num = qos_settings.p_qos_settings[i].port_number;
		if(be16_to_cpu(ip_port_num) ==  port_num)
		{
			res =(int) qos_settings.p_qos_settings[i].port_priority;
			goto out;
		}

	}


out:
	spin_unlock(&priv->ctx_lock);
	return (u8) res;
}

static int ib_link_query_port(struct ib_device *ibdev, u8 port,
			       struct ib_port_attr *props)
{
	int ext_active_speed;
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	static int print_it = 1;
	int ext_speed_active, ext_speed_suppported, ext_speed_enabled, fdr10_supported = 0;

	in_mad  = (ib_smp *)kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = (ib_smp *)kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cpu_to_be32(port);

	err = mlx4_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;
	ext_speed_active = out_mad->data[62] >> 4;
	ext_speed_suppported = out_mad->data[62] & 0x0f;
	ext_speed_enabled = out_mad->data[63] & 0x1f;

	props->lid		= be16_to_cpup((__be16 *) (out_mad->data + 16));
	props->lmc		= out_mad->data[34] & 0x7;
	props->sm_lid		= be16_to_cpup((__be16 *) (out_mad->data + 18));
	props->sm_sl		= out_mad->data[36] & 0xf;
	props->state		= (ib_port_state)(out_mad->data[32] & 0xf);
	props->phys_state	= out_mad->data[33] >> 4;
	props->port_cap_flags	= be32_to_cpup((__be32 *) (out_mad->data + 20));
	props->gid_tbl_len	= to_mdev(ibdev)->dev->caps.gid_table_len[port];
	props->max_msg_sz	= to_mdev(ibdev)->dev->caps.max_msg_sz;
	props->pkey_tbl_len	= (u16)to_mdev(ibdev)->dev->caps.pkey_table_len[port];
	props->bad_pkey_cntr	= be16_to_cpup((__be16 *) (out_mad->data + 46));
	props->qkey_viol_cntr	= be16_to_cpup((__be16 *) (out_mad->data + 48));
	props->active_width	= out_mad->data[31] & 0xf;
	props->active_speed	= out_mad->data[35] >> 4;
	props->max_mtu		= (ib_mtu)(out_mad->data[41] & 0xf);
	props->active_mtu	= (ib_mtu)(out_mad->data[36] >> 4);
	props->subnet_timeout	= out_mad->data[51] & 0x1f;
	props->max_vl_num	= out_mad->data[37] >> 4;
	props->init_type_reply	= out_mad->data[41] >> 4;
	props->transport= RDMA_TRANSPORT_IB;
	props->ext_active_speed	= out_mad->data[62];
	/* Cache local lid for qp1 tunneling in sriov */
	to_mdev(ibdev)->sriov.local_lid[port - 1] = props->lid;

	/* Check if extended speeds (EDR/FDR/...) are supported */
	if (props->port_cap_flags & IB_PORT_EXTENDED_SPEEDS_SUP) {
		ext_active_speed = out_mad->data[62] >> 4;

		switch (ext_active_speed) {
		case 1:
			props->active_speed = 16; /* FDR */
			break;
		case 2:
			props->active_speed = 32; /* EDR */
			break;
		}
	}

	/* If reported active speed is QDR, check if is  FDR-10 */
	if (props->active_speed == 4) {
		if (to_mdev(ibdev)->dev->caps.ext_port_cap[port] &
			MLX_EXT_PORT_CAP_FLAG_EXTENDED_PORT_INFO) {

			init_query_mad(in_mad);
			in_mad->attr_id  = MLX4_ATTR_EXTENDED_PORT_INFO;
			in_mad->attr_mod = cpu_to_be32(port);

			err = mlx4_MAD_IFC(to_mdev(ibdev), 1, 1, port,
				NULL, NULL, in_mad, out_mad);
			if (err)
				goto out;

			fdr10_supported = out_mad->data[15] & 0x1;
			if (out_mad->data[15] & 0x1)
				props->link_encoding = 1;
		}
	}
	
	// debugging
	if ( g.mode_flags & MLX4_MODE_SIM_FDR ) 
		props->ext_active_speed = 0x11;
	if ( g.mode_flags & MLX4_MODE_SIM_FDR10 ) 
		props->link_encoding = 1;
	
	if (print_it-- > 0) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: ext_speed_active %d, ext_speed_suppported %d, ext_speed_enabled %d, fdr10_supported %d \n", 
			to_mdev(ibdev)->dev->pdev->name, ext_speed_active, ext_speed_suppported, ext_speed_enabled, fdr10_supported ));
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: After checking simulation flags: ext_active_speed %d, ext_speed_suppported %d, FDR10 %d\n", 
			to_mdev(ibdev)->dev->pdev->name, props->ext_active_speed >> 4, props->ext_active_speed & 0x0f, props->link_encoding ));
	}

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static enum ib_port_state mlx4_ib_port_get_state(struct ib_device *ibdev, u8 port)
{
	struct mlx4_dev *dev = ibdev->dma_device;
	struct mlx4_priv *priv = mlx4_priv(dev);		
	return priv->link_up[port] ? IB_PORT_ACTIVE : IB_PORT_DOWN;
}

static u8 state_to_phys_state(enum ib_port_state state)
{
	return state == IB_PORT_ACTIVE ? 5 : 3;
}

#if DBG
static void print_props(char *title, struct ib_port_attr *props)
{
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s", title));
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   state              %d\n", (u32)props->state ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   max_mtu            %d\n", (u32)props->max_mtu ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   active_mtu         %d\n", (u32)props->active_mtu ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   gid_tbl_len        %d\n", (u32)props->gid_tbl_len ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   port_cap_flags     %d\n", (u32)props->port_cap_flags ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   max_msg_sz         %d\n", (u32)props->max_msg_sz ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   bad_pkey_cntr      %d\n", (u32)props->bad_pkey_cntr ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   qkey_viol_cntr     %d\n", (u32)props->qkey_viol_cntr ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   pkey_tbl_len       %d\n", (u32)props->pkey_tbl_len ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   lid                %d\n", (u32)props->lid ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   sm_lid             %d\n", (u32)props->sm_lid ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   lmc                %d\n", (u32)props->lmc ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   max_vl_num         %d\n", (u32)props->max_vl_num ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   sm_sl              %d\n", (u32)props->sm_sl ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   subnet_timeout     %d\n", (u32)props->subnet_timeout ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   init_type_reply    %d\n", (u32)props->init_type_reply ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   active_width       %d\n", (u32)props->active_width ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   active_speed       %d\n", (u32)props->active_speed ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   phys_state         %d\n", (u32)props->phys_state ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   transport          %d\n", (u32)props->transport ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   ext_active_speed   %d\n", (u32)props->ext_active_speed ));
    MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("   link_encoding      %d\n", (u32)props->link_encoding ));
}
#endif

/*
From f18080e6ee62b25030886c37b7120eb93746e5c9 Mon Sep 17 00:00:00 2001
From: Marcel Apfelbaum <marcela@dev.mellanox.co.il>
Date: Sun, 29 Jan 2012 18:16:53 +0200
Subject: [PATCH 2/2] IB/mlx4: fix wrong info returned when querying IBoE ports

To issue port query, use the QUERY_(Ethernet)_PORT command instead of
MAD_IFC command as the latter attempts to query the firmware IB SMA agent,
which is irrelevant for IBoE ports. This allows to support both 10Gb/s and
40Gb/s rates (e.g in sysfs), using QDR speed (10Gb/s) and width of 1X or 4X.
*/
static int eth_link_query_port(struct ib_device *ibdev, u8 port,
			       struct ib_port_attr *props)
{
	struct mlx4_dev *dev = to_mdev(ibdev)->dev;
	struct mlx4_cmd_mailbox *mailbox;
	int err = 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err = mlx4_cmd_box(dev, 0, mailbox->dma.da, port, 0,
			   MLX4_CMD_QUERY_PORT, MLX4_CMD_TIME_CLASS_B);

	if (err)
		goto out;

	ASSERT(mailbox->buf);
	#if 0
	{
		struct mlx4_en_query_port_context* pContext = (struct mlx4_en_query_port_context*)mailbox->buf;
    	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("link_up %s, mtu %u, link_speed %d, transceiver %d, actual_speed %d\n", 
			pContext->link_up& MLX4_EN_LINK_UP_MASK ? "UP" : "DN", 
			be16_to_cpu(pContext->mtu), pContext->link_speed & MLX4_EN_SPEED_MASK, 
			pContext->transceiver, pContext->actual_speed ));
	}
	#endif
	
	props->port_cap_flags	= IB_PORT_CM_SUP;
	props->gid_tbl_len	= dev->caps.gid_table_len[port];
	props->max_msg_sz	= dev->caps.max_msg_sz;
	props->pkey_tbl_len	= 1;
	props->active_width	= (u8)((((u8 *)mailbox->buf)[5] == 0x40) ? IB_WIDTH_4X : IB_WIDTH_1X);
	props->active_speed	= 1;
	props->max_mtu		= IB_MTU_2048;
	props->active_mtu	= IB_MTU_256;
	props->max_vl_num   = 2;
	props->state		= IB_PORT_DOWN;
	props->phys_state	= state_to_phys_state(props->state);
	props->transport	= RDMA_TRANSPORT_RDMAOE;
	props->state		= mlx4_ib_port_get_state(ibdev, port);
	if (props->state == IB_PORT_ACTIVE) {
		// port mtu is set in mlx4_SET_PORT_general(), but is not saved
		// ?? we take RoCE mtu
		enum ib_mtu mtu = dev->dev_params.roce_mtu[port];
		props->active_mtu = mtu ? min(props->max_mtu, mtu) : IB_MTU_256;
		props->phys_state	= state_to_phys_state(props->state);
	}
out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

static int mlx4_ib_query_port(struct ib_device *ibdev, u8 port,
    			      struct ib_port_attr *props)
{
	int err;
	int is_ib = (mlx4_ib_port_get_transport(ibdev, port) == RDMA_TRANSPORT_IB);

	memset(props, 0, sizeof *props);

	if ( is_ib ) 
		err = ib_link_query_port(ibdev, port, props);
	else 
		err = eth_link_query_port(ibdev, port, props);

	return err;
}


static int mlx4_ib_query_gid_chunk(struct ib_device *ibdev, u8 port, int index,
			     union ib_gid gid[8], int size)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	__be64	subnet_prefix;
	int err = -ENOMEM;

	if (mlx4_is_barred(ibdev->dma_device))
		return -EFAULT;
	
	in_mad  = (ib_smp *)kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = (ib_smp *)kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cpu_to_be32(port);

	err = mlx4_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memcpy(&subnet_prefix, out_mad->data + 8, 8);

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_GUID_INFO;
	in_mad->attr_mod = cpu_to_be32(index / 8);

	err = mlx4_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	{ // copy the results
		int i;
		__be64 *guid = (__be64 *)out_mad->data;
		for (i=0; i<size; ++i) {
			gid[i].global.subnet_prefix = subnet_prefix;
			gid[i].global.interface_id = guid[i];
		}
	}

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static int mlx4_ib_query_gid(struct ib_device *ibdev, u8 port, int index,
			     union ib_gid *gid)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	int err = -ENOMEM;
	int clear = 0;

	in_mad  = (ib_smp *)kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = (ib_smp *)kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cpu_to_be32(port);

	err = mlx4_MAD_IFC(dev, 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memcpy(gid->raw, out_mad->data + 8, 8);
	if (dev->dev->caps.sqp_demux) {
		/* Cache subnet prefix */
		dev->sriov.demux[port - 1].subnet_prefix = gid->global.subnet_prefix;
	}

	if (mlx4_is_mfunc(dev->dev)) {
		if (index) {
			/* For any index > 0, return the null guid */
			err = 0;
			clear = 1;
			if (!dev->dev->caps.sqp_demux)
				goto out;

			/* If this function is demuxing qp1, we need to cache
			 * the real guids so fall through... */
		} else {
			/* Map index 0 to the gid index of this function */
			index = slave_gid_index(dev);
		}
	}
	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_GUID_INFO;
	in_mad->attr_mod = cpu_to_be32(index / 8);

	err = mlx4_MAD_IFC(dev, 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;
	memcpy(gid->raw + 8, out_mad->data + (index % 8) * 8, 8);

	if (dev->dev->caps.sqp_demux) {
		dev->sriov.demux[port - 1].guid_cache[index] =
						gid->global.interface_id;
	}

out:
	if (clear)
		memset(gid->raw + 8, 0, 8);
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static int mlx4_ib_set_guid(struct ib_device *ibdev, u8 port, int index, u8 *guid)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	in_mad  = (ib_smp *)kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = (ib_smp *)kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	/* First get relevant block */
	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_GUID_INFO;
	in_mad->attr_mod = cpu_to_be32(index / 8);

	err = mlx4_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	/* Copy block to set mad and update proper GUID */
	in_mad->method = IB_MGMT_METHOD_SET;
	memcpy(in_mad->data, out_mad->data, IB_SMP_DATA_SIZE);
	memcpy(in_mad->data + (index % 8) * 8, guid, 8);

	err = mlx4_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err)
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: Failed setting guid block\n", 
		to_mdev(ibdev)->dev->pdev->name));

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mlx4_ib_set_slave_guids(struct ib_device *ibdev)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	union ib_gid gid;
	int i, j;
	u8 base;

	for (i = 1; i <= dev->num_ports; ++i) {
		if (mlx4_ib_query_gid(ibdev, (u8)i, 0, &gid))
			return -EFAULT;

		dev->sriov.demux[i - 1].gid_id_base = base = gid.raw[MLX4_SLAVE_ID_GID_OFFSET];
		for (j = 1; j < dev->dev->caps.sqp_demux; j++) { /* slave0 gets the hw guid */
			gid.raw[MLX4_SLAVE_ID_GID_OFFSET] = (u8)(base + j); /* allow overflows */
			if (mlx4_ib_set_guid(ibdev, (u8)i, j, (u8*) &gid.global.interface_id))
				return -EFAULT;
		}
	}
	return 0;
}

static int mlx4_ib_query_pkey_chunk(struct ib_device *ibdev, u8 port, u16 index,
			     __be16 pkey[32], int size)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	if (mlx4_is_barred(ibdev->dma_device))
		return -EFAULT;

	in_mad  = (ib_smp *)kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = (ib_smp *)kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PKEY_TABLE;
	in_mad->attr_mod = cpu_to_be32(index / 32);

	err = mlx4_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	{ // copy the results
		int i;
		__be16 *pkey_chunk = (__be16 *)out_mad->data;
		for (i=0; i<size; ++i) 
			pkey[i] = pkey_chunk[i];
	}

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
static int mlx4_ib_modify_device(struct ib_device *ibdev, int mask,
				 struct ib_device_modify *props)
{
	if (mlx4_is_barred(ibdev->dma_device))
		return -EFAULT;

	if (mask & ~IB_DEVICE_MODIFY_NODE_DESC)
		return -EOPNOTSUPP;

	if (mask & IB_DEVICE_MODIFY_NODE_DESC) {
		spin_lock(&to_mdev(ibdev)->sm_lock);
		memcpy(ibdev->node_desc, props->node_desc, 64);
		spin_unlock(&to_mdev(ibdev)->sm_lock);
	}

	return 0;
}

static int mlx4_ib_SET_PORT(struct mlx4_dev *dev, u8 port, 
	int reset_qkey_viols, u32 cap_mask)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;

	if (dev->caps.port_type_final[port] == MLX4_PORT_TYPE_ETH)
		return 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memset(mailbox->buf, 0, 256);
	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
		*(u8 *) mailbox->buf	     = (u8)(!!reset_qkey_viols << 6);
		((__be32 *) mailbox->buf)[2] = cpu_to_be32(cap_mask);
	} else {
		((u8 *) mailbox->buf)[3]     = (u8)!!reset_qkey_viols;
		((__be32 *) mailbox->buf)[1] = cpu_to_be32(cap_mask);
	}
	
	err = mlx4_cmd(dev, mailbox->dma.da, port, 0, MLX4_CMD_SET_PORT,
				MLX4_CMD_TIME_CLASS_B); 

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

static int mlx4_ib_modify_port(struct ib_device *ibdev, u8 port, int mask,
			       struct ib_port_modify *props)
{
	struct ib_port_attr attr;
	u32 cap_mask;
	int err;

	if (mlx4_is_barred(ibdev->dma_device))
		return -EFAULT;

	mutex_lock(&to_mdev(ibdev)->cap_mask_mutex);

	err = mlx4_ib_query_port(ibdev, port, &attr);
	if (err)
		goto out;

	cap_mask = (attr.port_cap_flags | props->set_port_cap_mask) &
		~props->clr_port_cap_mask;

	err = mlx4_ib_SET_PORT(to_mdev(ibdev)->dev, port,
			    !!(mask & IB_PORT_RESET_QKEY_CNTR),
			    cap_mask);

out:
	mutex_unlock(&to_mdev(ibdev)->cap_mask_mutex);
	return err;
}

static struct ib_ucontext *mlx4_ib_alloc_ucontext(struct ib_device *ibdev,
						  struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_ucontext *context;
	struct mlx4_ib_alloc_ucontext_resp resp;
	int err;

	if (mlx4_is_barred(ibdev->dma_device))
		return (ib_ucontext *)ERR_PTR(-EFAULT);

	if (!dev->ib_active)
		return (ib_ucontext *)ERR_PTR(-EAGAIN);

	resp.qp_tab_size      = dev->dev->caps.num_qps;
	resp.bf_reg_size      = (__u16)dev->dev->caps.bf_reg_size;
	resp.bf_regs_per_page = (__u16)dev->dev->caps.bf_regs_per_page;
	resp.cqe_size		  = dev->dev->caps.cqe_size;

	context = (mlx4_ib_ucontext *)kzalloc(sizeof *context, GFP_KERNEL);
	if (!context)
		return (ib_ucontext *)ERR_PTR(-ENOMEM);

	err = mlx4_uar_alloc(to_mdev(ibdev)->dev, &context->uar);
	if (err) {
		kfree(context);
		return (ib_ucontext *)ERR_PTR(err);
	}

	INIT_LIST_HEAD(&context->db_page_list);
	mutex_init(&context->db_page_mutex);

	err = ib_copy_to_udata(udata, &resp, sizeof resp);
	if (err) {
		mlx4_uar_free(to_mdev(ibdev)->dev, &context->uar);
		kfree(context);
		return (ib_ucontext *)ERR_PTR(-EFAULT);
	}

	return &context->ibucontext;
}

static int mlx4_ib_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct mlx4_ib_ucontext *context = to_mucontext(ibcontext);

	mlx4_uar_free(to_mdev(ibcontext->device)->dev, &context->uar);
	kfree(context);

	return 0;
}

#if 0
	// TODO: not clear, what is the usage 
static int mlx4_ib_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct mlx4_ib_dev *dev = to_mdev(context->device);

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	if (vma->vm_pgoff == 0) {
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

		if (io_remap_pfn_range(vma, vma->vm_start,
				       to_mucontext(context)->uar.pfn,
				       PAGE_SIZE, vma->vm_page_prot))
			return -EAGAIN;
	} else if (vma->vm_pgoff == 1 && dev->dev->caps.bf_reg_size != 0) {
		/* FIXME want pgprot_writecombine() for BlueFlame pages */
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

		if (io_remap_pfn_range(vma, vma->vm_start,
				       to_mucontext(context)->uar.pfn +
				       dev->dev->caps.num_uars,
				       PAGE_SIZE, vma->vm_page_prot))
			return -EAGAIN;
	} else
		return -EINVAL;

	return 0;
}
#endif	

static struct ib_pd *mlx4_ib_alloc_pd(struct ib_device *ibdev,
				      struct ib_ucontext *context,
				      struct ib_udata *udata)
{
	struct mlx4_ib_pd *pd;
	int err;

	if (mlx4_is_barred(ibdev->dma_device))
		return (ib_pd *)ERR_PTR(-EFAULT);

	pd = (mlx4_ib_pd*)kmalloc(sizeof *pd, GFP_KERNEL);
	if (!pd)
		return (ib_pd *)ERR_PTR(-ENOMEM);

	err = mlx4_pd_alloc(to_mdev(ibdev)->dev, &pd->pdn);
	if (err) {
		kfree(pd);
		return (ib_pd *)ERR_PTR(err);
	}

	if (context)
		if (ib_copy_to_udata(udata, &pd->pdn, sizeof (__u32))) {
			mlx4_pd_free(to_mdev(ibdev)->dev, pd->pdn);
			kfree(pd);
			return (ib_pd *)ERR_PTR(-EFAULT);
		}

	return &pd->ibpd;
}

static int mlx4_ib_dealloc_pd(struct ib_pd *pd)
{
	mlx4_pd_free(to_mdev(pd->device)->dev, to_mpd(pd)->pdn);
	kfree(pd);

	return 0;
}

static int mlx4_ib_mcg_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	UNUSED_PARAM(lid);
	if (mlx4_is_barred(ibqp->device->dma_device))
		return -EFAULT;
	return mlx4_multicast_attach(to_mdev(ibqp->device)->dev,
				     &to_mqp(ibqp)->mqp, gid->raw,
				     !!(to_mqp(ibqp)->flags & MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK),
					 MLX4_PROT_IB_IPV6, FALSE /* block loopback */);
}

static int mlx4_ib_mcg_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	UNUSED_PARAM(lid);
	if (mlx4_is_barred(ibqp->device->dma_device))
		return -EFAULT;
	return mlx4_multicast_detach(to_mdev(ibqp->device)->dev,
				     &to_mqp(ibqp)->mqp, gid->raw,
				     MLX4_PROT_IB_IPV6);
}

static int init_node_data(struct mlx4_ib_dev *dev)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	in_mad  = (ib_smp *)kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = (ib_smp *)kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_DESC;

	err = mlx4_MAD_IFC(dev, 1, 1, 1, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	dev->dev->rev_id = be32_to_cpup((__be32 *) (out_mad->data + 32));
	memcpy(dev->ib_dev.node_desc, out_mad->data, 64);

	in_mad->attr_id = IB_SMP_ATTR_NODE_INFO;

	err = mlx4_MAD_IFC(dev, 1, 1, 1, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memcpy(&dev->ib_dev.node_guid, out_mad->data + 12, 8);

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static void *mlx4_ib_add(struct mlx4_dev *dev)
{
	struct mlx4_ib_dev *ibdev;
	int num_ports = mlx4_count_ib_ports(dev);

#if 0
	/* No point in registering a device with no ports... */
	if (num_ports == 0)
		return NULL;
#endif

	ibdev = (struct mlx4_ib_dev *) ib_alloc_device(sizeof *ibdev);
	if (!ibdev) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("Device struct alloc failed\n"));
		return NULL;
	}

	MLX4_INIT_DOORBELL_LOCK(&ibdev->uar_lock);

	INIT_LIST_HEAD(&ibdev->pgdir_list);
	mutex_init(&ibdev->pgdir_mutex);

	ibdev->dev = dev;

	strlcpy(ibdev->ib_dev.name, "mlx4_%d", IB_DEVICE_NAME_MAX);
	ibdev->ib_dev.node_type		= RDMA_NODE_IB_CA;
	ibdev->num_ports		= num_ports;
	ibdev->ib_dev.phys_port_cnt	= (u8)num_ports;
	ibdev->ib_dev.dma_device	= dev->pdev->dev;

	ibdev->ib_dev.uverbs_abi_ver	= MLX4_IB_UVERBS_ABI_VERSION;
	ibdev->ib_dev.query_device	= mlx4_ib_query_device;
	ibdev->ib_dev.query_port	= mlx4_ib_query_port;
	ibdev->ib_dev.get_port_transport = mlx4_ib_port_get_transport;
	ibdev->ib_dev.query_gid_chunk	= mlx4_ib_query_gid_chunk;
	ibdev->ib_dev.query_pkey_chunk	= mlx4_ib_query_pkey_chunk;
	ibdev->ib_dev.modify_device	= mlx4_ib_modify_device;
	ibdev->ib_dev.modify_port	= mlx4_ib_modify_port;
	ibdev->ib_dev.alloc_ucontext	= mlx4_ib_alloc_ucontext;
	ibdev->ib_dev.dealloc_ucontext	= mlx4_ib_dealloc_ucontext;
	ibdev->ib_dev.mmap		= NULL;		/* mlx4_ib_mmap; */
	ibdev->ib_dev.alloc_pd		= mlx4_ib_alloc_pd;
	ibdev->ib_dev.dealloc_pd	= mlx4_ib_dealloc_pd;
	ibdev->ib_dev.create_ah		= mlx4_ib_create_ah;
	ibdev->ib_dev.query_ah		= mlx4_ib_query_ah;
	ibdev->ib_dev.modify_ah		= mlx4_ib_modify_ah;
	ibdev->ib_dev.destroy_ah	= mlx4_ib_destroy_ah;
	ibdev->ib_dev.create_srq	= mlx4_ib_create_srq;
	ibdev->ib_dev.modify_srq	= mlx4_ib_modify_srq;
	ibdev->ib_dev.query_srq		= mlx4_ib_query_srq;
	ibdev->ib_dev.destroy_srq	= mlx4_ib_destroy_srq;
	ibdev->ib_dev.post_srq_recv	= mlx4_ib_post_srq_recv;
	ibdev->ib_dev.create_qp		= mlx4_ib_create_qp;
	ibdev->ib_dev.modify_qp		= mlx4_ib_modify_qp;
	ibdev->ib_dev.query_qp		= mlx4_ib_query_qp;
	ibdev->ib_dev.destroy_qp	= mlx4_ib_destroy_qp;
	ibdev->ib_dev.post_send		= mlx4_ib_post_send;
	ibdev->ib_dev.post_recv		= mlx4_ib_post_recv;
	ibdev->ib_dev.create_cq		= mlx4_ib_create_cq;
	ibdev->ib_dev.modify_cq		= mlx4_ib_modify_cq;
	ibdev->ib_dev.destroy_cq	= mlx4_ib_destroy_cq;
	ibdev->ib_dev.poll_cq		= mlx4_ib_poll_cq;
	ibdev->ib_dev.req_notify_cq	= mlx4_ib_arm_cq;
	ibdev->ib_dev.get_dma_mr	= mlx4_ib_get_dma_mr;
	ibdev->ib_dev.reg_user_mr	= mlx4_ib_reg_user_mr;
	ibdev->ib_dev.dereg_mr		= mlx4_ib_dereg_mr;
#ifdef SUPPORT_FAST_REG	
	ibdev->ib_dev.alloc_fast_reg_mr = mlx4_ib_alloc_fast_reg_mr;
	ibdev->ib_dev.alloc_fast_reg_page_list = mlx4_ib_alloc_fast_reg_page_list;
	ibdev->ib_dev.free_fast_reg_page_list  = mlx4_ib_free_fast_reg_page_list;
#endif	
	ibdev->ib_dev.attach_mcast	= mlx4_ib_mcg_attach;
	ibdev->ib_dev.detach_mcast	= mlx4_ib_mcg_detach;
	ibdev->ib_dev.process_mad	= mlx4_ib_process_mad;

	ibdev->ib_dev.alloc_fmr		= mlx4_ib_fmr_alloc;
	ibdev->ib_dev.map_phys_fmr	= mlx4_ib_map_phys_fmr;
	ibdev->ib_dev.unmap_fmr		= mlx4_ib_unmap_fmr;
	ibdev->ib_dev.dealloc_fmr	= mlx4_ib_fmr_dealloc;
	ibdev->ib_dev.x.find_cached_gid		= ib_find_cached_gid;
	ibdev->ib_dev.x.find_cached_pkey	= ib_find_cached_pkey;
	ibdev->ib_dev.x.get_cached_gid		= ib_get_cached_gid;
	ibdev->ib_dev.x.get_cached_pkey		= ib_get_cached_pkey;
	ibdev->ib_dev.x.register_ev_cb		= mlx4_reset_cb_register;
	ibdev->ib_dev.x.unregister_ev_cb	= mlx4_reset_cb_unregister;
#if 1//WORKAROUND_POLL_EQ
	ibdev->ib_dev.x.poll_eq				= mlx4_poll_eq;
#endif
	ibdev->ib_dev.x.poll_cq_array		= mlx4_ib_poll_cq_array;
	ibdev->ib_dev.x.reg_krnl_mr			= mlx4_ib_reg_krnl_mr;
	ibdev->ib_dev.x.alloc_fast_reg_mr 			= mlx4_ib_alloc_fast_reg_mr;
	ibdev->ib_dev.x.alloc_fast_reg_page_list	= mlx4_ib_alloc_fast_reg_page_list;
	ibdev->ib_dev.x.free_fast_reg_page_list 	= mlx4_ib_free_fast_reg_page_list;
	ibdev->ib_dev.x.get_sl_for_ip_port	= mlx4_get_sl_for_ip_port;
	ibdev->ib_dev.x.create_cq_ex	= mlx4_ib_create_cq_ex;

	if (mlx4_pd_alloc(dev, &ibdev->priv_pdn))
		goto err_dealloc;

	if (mlx4_uar_alloc(dev, &ibdev->priv_uar))
		goto err_pd;

	ibdev->priv_uar.map = (u8*)ioremap(ibdev->priv_uar.pfn << PAGE_SHIFT, PAGE_SIZE, MmNonCached);
	if (!ibdev->priv_uar.map)
		goto err_uar;

	if (init_node_data(ibdev))
		goto err_map;

	spin_lock_init(&ibdev->sm_lock);
	mutex_init(&ibdev->cap_mask_mutex);
	mutex_init(&ibdev->xrc_reg_mutex);

	if (ib_register_device(&ibdev->ib_dev))
		goto err_map;

	if (mlx4_ib_init_sriov(ibdev))
		goto err_reg;

	ibdev->ib_active = 1;

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: IB interface is ADDED ! \n", ibdev->dev->pdev->name ));

	return ibdev;

err_reg:
	ib_unregister_device(&ibdev->ib_dev);

err_map:
	iounmap(ibdev->priv_uar.map, PAGE_SIZE);

err_uar:
	mlx4_uar_free(dev, &ibdev->priv_uar);

err_pd:
	mlx4_pd_free(dev, ibdev->priv_pdn);

err_dealloc:
	ibdev->ib_dev.reg_state = ib_device::IB_DEV_UNINITIALIZED;
	ib_dealloc_device(&ibdev->ib_dev);

	return NULL;
}

static void mlx4_ib_remove(struct mlx4_dev *dev, void *ibdev_ptr)
{
	struct mlx4_ib_dev *ibdev = (mlx4_ib_dev *)ibdev_ptr;
	int p;

	mlx4_ib_close_sriov(ibdev);
	ib_unregister_device(&ibdev->ib_dev);
	
	for (p = 1; p <= dev->caps.num_ports; ++p)
		mlx4_CLOSE_PORT(dev, p);

	iounmap(ibdev->priv_uar.map, PAGE_SIZE);
	mlx4_uar_free(dev, &ibdev->priv_uar);
	mlx4_pd_free(dev, ibdev->priv_pdn);
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: IB interface is REMOVED ! \n", ibdev->dev->pdev->name));
	ibdev->ib_dev.reg_state = ib_device::IB_DEV_UNINITIALIZED;
	ib_dealloc_device(&ibdev->ib_dev);
}

static void mlx4_ib_event(struct mlx4_dev *dev, void *ibdev_ptr,
			  enum mlx4_dev_event event, int port)
{
	struct ib_event ibev;
	struct mlx4_ib_dev *ibdev = to_mdev((struct ib_device *) ibdev_ptr);

	UNUSED_PARAM(dev);

	switch (event) {
	case MLX4_DEV_EVENT_PORT_UP:
		ibev.event = IB_EVENT_PORT_ACTIVE;
		break;

	case MLX4_DEV_EVENT_PORT_DOWN:
		ibev.event = IB_EVENT_PORT_ERR;
		break;

	case MLX4_DEV_EVENT_CATASTROPHIC_ERROR:
		ibdev->ib_active = 0;
		ibev.event = IB_EVENT_DEVICE_FATAL;
		break;

	default:
		return;
	}

	ibev.device	      = (ib_device *)ibdev_ptr;
	ibev.element.port_num = (u8)port;

	ib_dispatch_event(&ibev);
}

static struct mlx4_interface mlx4_ib_interface = {
	mlx4_ib_add,		/* add */
	mlx4_ib_remove,		/* remove */
	mlx4_ib_event,		/* event */
	NULL, NULL			/* list */
};

int __init mlx4_ib_init(void)
{
	mlx4_ib_qp_init();
	return mlx4_register_interface(&mlx4_ib_interface);
}

void __exit mlx4_ib_cleanup(void)
{
	mlx4_unregister_interface(&mlx4_ib_interface);
}



