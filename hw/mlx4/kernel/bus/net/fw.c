/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	   Redistribution and use in source and binary forms, with or
 *	   without modification, are permitted provided that the following
 *	   conditions are met:
 *
 *		- Redistributions of source code must retain the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer.
 *
 *		- Redistributions in binary form must reproduce the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer in the documentation and/or other materials
 *		  provided with the distribution.
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

#include "fw.h"
#include "cmd.h"
#include "icm.h"
#include "mlx4_debug.h"
#include <stdio.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "fw.tmh"
#endif


enum {
	MLX4_COMMAND_INTERFACE_MIN_REV		= 2,
	MLX4_COMMAND_INTERFACE_MAX_REV		= 3,
	MLX4_COMMAND_INTERFACE_NEW_PORT_CMDS	= 3,
};

#define MLX4_GET(dest, source, offset)						\
	{								  \
		void *__p = (char *) (source) + (offset);			\
		void *__d = &(dest);								\
		switch (sizeof (dest)) {							\
		case 1: *(u8 *) __d = *(u8 *) __p;		break;		\
		case 2: *(__be16 *) __d = be16_to_cpup(__p); break; \
		case 4: *(__be32 *) __d = be32_to_cpup(__p); break; \
		case 8: *(__be64 *) __d = be64_to_cpup(__p); break; \
		default: ASSERTMSG("Incorrect dest field\n", !__p); \
		}							  \
	}

#define MLX4_PUT(dest, source, offset)					  \
	{								  \
		void *__d = ((char *) (dest) + (offset));		  \
		switch (sizeof(source)) {				  \
		case 1: *(u8 *) __d = (u8)(source); 		   break; \
		case 2: *(__be16 *) __d = cpu_to_be16((u16)(source)); break; \
		case 4: *(__be32 *) __d = cpu_to_be32((u32)(source)); break; \
		case 8: *(__be64 *) __d = cpu_to_be64((u64)(source)); break; \
		default: ASSERTMSG("Incorrect dest field\n", !__d);  \
		}							  \
	}

static void dump_dev_cap_flags(struct mlx4_dev *dev, u64 flags)
{
	static char *fname[64];
	static int filled = 0;
	int i;

	if (!filled)
	{
		memset( fname, 0, sizeof(fname) );
		fname[0] = "RC transport";
		fname[1] = "UC transport";
		fname[2] = "UD transport";
		fname[3] = "XRC transport";
		fname[4] = "reliable multicast";
		fname[5] = "FCoIB support";
		fname[6] = "SRQ support";
		fname[7] = "IPoIB checksum offload";
		fname[8] = "P_Key violation counter";
		fname[9] = "Q_Key violation counter";
		fname[10] = "VMM";
		fname[12] = "DPDP (different port interfaces)";
		fname[16] = "MW support";
		fname[17] = "APM support";
		fname[18] = "Atomic ops support";
		fname[19] = "Raw multicast support";
		fname[20] = "Address vector port checking support";
		fname[21] = "UD multicast support";
		fname[24] = "Demand paging support";
		fname[25] = "Router support";
 		fname[30] = "IBoE support";
 		fname[48] = "Basic counters support";
 		fname[49] = "Extended counters support";
        fname[52] = "RSS IP frag support";
        fname[53] = "ETS support";
		fname[62] = "64 byte CQE support";
	}

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,( "%s: DEV_CAP flags 0x%I64x\n", dev->pdev->name, flags ));
	for (i = 0; i < ARRAY_SIZE(fname); ++i)
		if (fname[i] && (flags & (1ULL << i)))
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,( "    %s (bit %d)\n", fname[i], i));
}

int mlx4_MOD_STAT_CFG(struct mlx4_dev *dev, struct mlx4_mod_stat_cfg *cfg)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *inbox;
	int err = 0;

#define MOD_STAT_CFG_IN_SIZE		0x100

#define MOD_STAT_CFG_PG_SZ_M_OFFSET 0x002
#define MOD_STAT_CFG_PG_SZ_OFFSET	0x003

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	inbox = (u32*)mailbox->buf;

	memset(inbox, 0, MOD_STAT_CFG_IN_SIZE);

	MLX4_PUT(inbox, cfg->log_pg_sz, MOD_STAT_CFG_PG_SZ_OFFSET);
	MLX4_PUT(inbox, cfg->log_pg_sz_m, MOD_STAT_CFG_PG_SZ_M_OFFSET);

	err = mlx4_cmd(dev, mailbox->dma.da, 0, 0, MLX4_CMD_MOD_STAT_CFG,
			MLX4_CMD_TIME_CLASS_A);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_QUERY_VEP_CFG(struct mlx4_dev *dev, u8 vep_num, u8 port,
			struct mlx4_vep_cfg *cfg)
{
	int err;
	u32 in_mod;
	u64 output;

#define QUERY_VEP_CFG_OPMOD 	3

#define QUERY_VEP_CFG_INMOD 	   (2 << 28)
#define QUERY_VEP_CFG_INMOD_VEP_OFFSET 16
#define QUERY_VEP_CFG_INMOD_PORT_OFFSET 8

#define QUERY_VEP_CFG_MAC_OFFSET   0x90
#define QUERY_VEP_CFG_LINK_OFFSET	0xa0


	in_mod = QUERY_VEP_CFG_INMOD | (vep_num << QUERY_VEP_CFG_INMOD_VEP_OFFSET) |
		(port << QUERY_VEP_CFG_INMOD_PORT_OFFSET);
	err = mlx4_cmd_imm(dev, 0, &output, in_mod | QUERY_VEP_CFG_MAC_OFFSET,
				QUERY_VEP_CFG_OPMOD, MLX4_CMD_MOD_STAT_CFG,
				MLX4_CMD_TIME_CLASS_A);
	if (err) {        
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: Failed to retrieve mac for function %d\n",dev->pdev->name, vep_num));        
		return err;
	}
	cfg->mac = output & 0xffffffffffffUL;
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: retrieve mac for function %d, mac=0x%I64x\n", dev->pdev->name, vep_num, cfg->mac));
	
	err = mlx4_cmd_imm(dev, 0, &output, in_mod | QUERY_VEP_CFG_LINK_OFFSET,
				QUERY_VEP_CFG_OPMOD, MLX4_CMD_MOD_STAT_CFG,
				MLX4_CMD_TIME_CLASS_A);
	if (err) {        
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: Failed to retrieve link for function %d\n", dev->pdev->name, vep_num));
		return err;
	}

	cfg->link = (u8)((output >> 32) & 1);
	return 0;
}

struct vep_config {
	#define VEP_MODIFIED 1 << 7
	u8	vg_m;
	u8	vep_group;
	u8	reserve1;
	u8	bw_allocation;
	u8	reserve2;
	u8	max_bw_unit;
	u8	reserve3;
	u8	max_bw_value;
};

int mlx4_update_uplink_arbiter(struct mlx4_dev *dev, int port)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 in_mod;
	int err;
	u8 i;
	u8 *buf;
	u64 *buf64;
	u8 slave_id;
	struct vep_config* vep_cfg; 
	struct mlx4_priv* priv = mlx4_priv(dev);
	
	#define QUERY_UPLINK_ARB_OPMOD		 2
	#define QUERY_UPLINK_ARB_INMOD	   (3 << 28)
	#define QUERY_UPLINK_ARB_PORT_OFFSET   8
	#define SET_PORT_ARB_MOD	   2

	ASSERT(mlx4_is_master(dev));
	
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	buf = (u8*)mailbox->buf;
	in_mod = QUERY_UPLINK_ARB_INMOD | ((u8)port << QUERY_UPLINK_ARB_PORT_OFFSET);
	err = mlx4_cmd_box(dev, 0, mailbox->dma.da, in_mod, QUERY_UPLINK_ARB_OPMOD,
		MLX4_CMD_MOD_STAT_CFG, MLX4_CMD_TIME_CLASS_A); 
	if (err) {        
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: Failed to read uplink arbiter configuration for port %d\n", dev->pdev->name, port));
		goto out;
	}

	#define UPLINK_VEP_MODE 	0x2
	#define MODIFY_UPLINK_TYPE	0x1 << 7
	#define VEP_CONFIG_OFFSET	0x40
	#define VEP_CONFIG_SIZE 	0x8
	#define VEP_ENABLE_MASK 	(1ull << 63 | 1ull << 39 | 1ull << 31) 

	if (buf[3] != UPLINK_VEP_MODE) {
		/* not running in vep mode, nothing to do */
		/* TODO: config ets mode */
		priv->vep_mode[port] = false;
		goto out;
	}

	priv->vep_mode[port] = true;
	buf[0] = MODIFY_UPLINK_TYPE;				   // Set Modify uplink_type bit
	
	for (i = 0; i < priv->mfunc.master.vep_num[port]; i++) {
		vep_cfg = (struct vep_config*) (&buf[VEP_CONFIG_OFFSET + i * VEP_CONFIG_SIZE]);
		buf64 = (u64 *) (&buf[VEP_CONFIG_OFFSET + i * VEP_CONFIG_SIZE]);
		*buf64 |= cpu_to_be64(VEP_ENABLE_MASK);

		slave_id = get_slave(dev, (u8)port, i);
		
		ASSERT((vep_cfg->max_bw_unit == 3) || (vep_cfg->max_bw_unit == 4));
		switch (vep_cfg->max_bw_unit) {
			case 3:
				//
				//	100Mbps units
				//
				priv->mfunc.master.slave_state[slave_id].vep_cfg.bw_value = vep_cfg->max_bw_value;
				break;
			case 4: 
				//
				//	1Gbps units, move to 100Mbps 
				//
				priv->mfunc.master.slave_state[slave_id].vep_cfg.bw_value = 10*vep_cfg->max_bw_value;
				break;
			default:
				ASSERT(FALSE);
		}

		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "##%d## max_bw_unit=0x%x - max_bw_value=0x%x\n",slave_id,vep_cfg->max_bw_unit,vep_cfg->max_bw_value));
	}
	
	err = mlx4_cmd(dev, mailbox->dma.da, (u32) port, SET_PORT_ARB_MOD,
		MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_A);
	if (err)
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: Failed to set uplink arbiter configuration for port %d\n", dev->pdev->name, port));

 out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

static bool is_mfunc(struct mlx4_dev *dev)
{
    u8 output[8];
    int err;

    err = mlx4_cmd_imm(dev, 0, (u64 *) output, 0xa0, 0x3,
        MLX4_CMD_MOD_STAT_CFG, MLX4_CMD_TIME_CLASS_A);
    if (err) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Failed to qurey multi/single function mode: %d\n", err));
        return false;
    }

    if (output[4] & 0x10)
        return true;

    return false;
}

int mlx4_set_vep_maps(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u8 num_veps;
	u8 pf_num;
	u8 port, i;
	u64 output;
	u32 in_mod;
	int err;

	for (port = 1; port <= dev->caps.num_ports; port++) {
		in_mod = (1ul << 28) | (u32) port << 8 | 0x98;
		err = mlx4_cmd_imm(dev, 0, &output, in_mod, 0x3,
				   MLX4_CMD_MOD_STAT_CFG, MLX4_CMD_TIME_CLASS_A);
		if (err) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("Failed to retrieve number of veps\n"));
			return err;
		}
		num_veps = (u8) ((output >> 48) & 0x7f);
		priv->mfunc.master.vep_num[port] = num_veps;

		for (i = 0; i < num_veps; i++) {
			in_mod = (2ull << 28) | (u32) i << 16 | (u32) port << 8 | 0xa0;
			err = mlx4_cmd_imm(dev, 0, &output, in_mod, 0x3,
					   MLX4_CMD_MOD_STAT_CFG, MLX4_CMD_TIME_CLASS_A);
			if (err) {
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("failed to retieve func number for vep\n"));
				return err;
			}
			pf_num = (u8) ((output >> 48) & 0xff);
			priv->mfunc.master.slave_state[pf_num].function = pf_num;
			priv->mfunc.master.slave_state[pf_num].pf_num = pf_num;
			priv->mfunc.master.slave_state[pf_num].vep_num = i;

            ASSERT((port == 1) || (port == 2));
			priv->mfunc.master.slave_state[pf_num].port_num = port;
		}
	}
	mlx4_set_port_mask(dev, &dev->caps, dev->caps.function);
	return 0;
}

#define STAT_CLP_OFFSET		0x88
#define CLP_VER_MASK		0xffff

static u16 mlx4_QUERY_CLP(struct mlx4_dev* dev)
{
	u64 output;
	int err;

	err = mlx4_cmd_imm(dev, 0, &output, STAT_CLP_OFFSET, 0x3,
		MLX4_CMD_MOD_STAT_CFG, MLX4_CMD_TIME_CLASS_A);

	if (err) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("failed to retrieve clp version : %d", err));
		return 0;
	}

	return (u16) ((output >> 32) & CLP_VER_MASK);
}

static int query_function(struct mlx4_dev *dev, u8 slave, struct mlx4_vep_cfg* pConfig)
{
	struct mlx4_priv* priv = mlx4_priv(dev);
	
	pConfig->link = priv->mfunc.master.slave_state[slave].vep_cfg.link;
	pConfig->bw_value = priv->mfunc.master.slave_state[slave].vep_cfg.bw_value;
	
	return 0;
}

int mlx4_QUERY_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							  struct mlx4_cmd_mailbox *inbox,
							  struct mlx4_cmd_mailbox *outbox)
{
	UNUSED_PARAM(inbox);
	UNUSED_PARAM(vhcr);
	
	return query_function(dev, (u8)slave, (struct mlx4_vep_cfg*)outbox->buf);
}

static int mlx4_query_port(struct mlx4_dev *dev, u8 port, struct mlx4_vep_cfg* pConfig)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_en_query_port_context* pContext;
	int err = 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	
	pContext = (struct mlx4_en_query_port_context*)mailbox->buf;
	memset(pContext, 0, sizeof(struct mlx4_en_query_port_context));

	err = mlx4_cmd_box(dev, 0, mailbox->dma.da, port, 0, MLX4_CMD_QUERY_PORT, MLX4_CMD_TIME_CLASS_B);
	if (!err) {
		pContext = (struct mlx4_en_query_port_context*) mailbox->buf;
		pConfig->link = pContext->link_up & MLX4_EN_LINK_UP_MASK;

	   if (pContext->link_speed != 0xff) {
		   switch (pContext->link_speed & MLX4_EN_SPEED_MASK) {
		   case MLX4_EN_1G_SPEED:
			   pConfig->bw_value = LINE_SPEED_1GB;
			   break;
		   case MLX4_EN_10G_SPEED_XAUI:
		   case MLX4_EN_10G_SPEED_XFI:
			   pConfig->bw_value = LINE_SPEED_10GB;
			   break;
		   case MLX4_EN_40G_SPEED:
			   pConfig->bw_value = LINE_SPEED_40GB;
			   break;
		   default:
				pConfig->bw_value = LINE_SPEED_10GB;
			   break;
			}
	   } else {

			 pConfig->bw_value = pContext->actual_speed;
		}

	}
	
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}


int mlx4_QUERY_PORT(struct mlx4_dev *dev, u8 port, struct mlx4_vep_cfg* pConfig)
{
	int err = 0;
	struct mlx4_cmd_mailbox *mailbox;

	memset(pConfig, 0, sizeof(struct mlx4_vep_cfg));

	if (!mlx4_is_mfunc(dev)) {	
		return mlx4_query_port(dev, port, pConfig);
	}
	
	if (mlx4_is_master(dev))
		return query_function(dev, (u8)dev->caps.function, pConfig); 

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memset(mailbox->buf, 0, sizeof(struct mlx4_vep_cfg));
	
	err = mlx4_cmd_box(dev, 0, mailbox->dma.da, (u32)port, 0,
				MLX4_CMD_QUERY_PORT, MLX4_CMD_TIME_CLASS_B);
	if (!err) {
		memcpy(pConfig, mailbox->buf, sizeof(struct mlx4_vep_cfg));
	}
	mlx4_free_cmd_mailbox(dev, mailbox);

	return err;
}

int mlx4_QUERY_SLAVE_CAP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							   struct mlx4_cmd_mailbox *inbox,
							   struct mlx4_cmd_mailbox *outbox)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_mfunc_master_ctx *master = &priv->mfunc.master;
	struct mlx4_slave_state *slave_st = &master->slave_state[slave];
	struct mlx4_caps *caps = (mlx4_caps*)outbox->buf;
	u8 pf_num = slave_st->pf_num;
	int i;
	int err = 0;

	UNUSED_PARAM(vhcr);
	UNUSED_PARAM(inbox);

	memcpy(caps, &dev->caps, sizeof *caps);
	caps->clr_int_bar = mlx4_priv(dev)->fw.clr_int_bar;
	caps->clr_int_base = mlx4_priv(dev)->fw.clr_int_base;
	
	/* The Master function is in charge for qp1 of al slaves */
	caps->sqp_demux = 0;
	caps->vep_num = slave_st->vep_num;
	if (pf_num == slave) {
		err = mlx4_QUERY_VEP_CFG(dev, slave_st->vep_num,
					slave_st->port_num, &slave_st->vep_cfg);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Failed to retreive mac address for vep %d\n", pf_num));
		}
		else
			caps->def_mac[slave_st->port_num] = slave_st->vep_cfg.mac;
	}

	if (pf_num != slave || err) {	 
		for (i = 1; i <= dev->caps.num_ports; ++i)
			caps->def_mac[i] = dev->caps.def_mac[i] + (slave << 8);
	}
	
	/* Ports are activated according to physical function number */
	mlx4_set_port_mask(dev, caps, slave_st->pf_num);

	caps->function = slave;

	/* All other resources are allocated by the master, but we still report
	 * 'num' and 'reserved' capabilities as follows:
	 * - num remains the maximum resource index
	 * - 'num - reserved' is the total available objects of a resource, but
	 *	 resource indices may be less than 'reserved'
	 * TODO: set per-resource quotas */
	return 0;
}

int mlx4_QUERY_SLAVE_CAP(struct mlx4_dev *dev, struct mlx4_caps *caps)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;

	ASSERT(mlx4_is_slave(dev));
	
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err = mlx4_cmd_box(dev, 0, mailbox->dma.da, 0, 0, MLX4_CMD_QUERY_SLAVE_CAP,
			   MLX4_CMD_TIME_CLASS_A);
	if (!err) {
		memcpy(caps, mailbox->buf, sizeof *caps);
		mlx4_priv(dev)->fw.clr_int_bar = caps->clr_int_bar;
		mlx4_priv(dev)->fw.clr_int_base = caps->clr_int_base;
	}

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_QUERY_DEV_CAP(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *outbox;
	u8 field;
	u16 size;
	u16 stat_rate;
	int err;
	int i;
	u32 tmp1, tmp2;

#define QUERY_DEV_CAP_OUT_SIZE			   0x100
#define QUERY_DEV_CAP_MAX_SRQ_SZ_OFFSET 	0x10
#define QUERY_DEV_CAP_MAX_QP_SZ_OFFSET		0x11
#define QUERY_DEV_CAP_RSVD_QP_OFFSET		0x12
#define QUERY_DEV_CAP_MAX_QP_OFFSET 	0x13
#define QUERY_DEV_CAP_RSVD_SRQ_OFFSET		0x14
#define QUERY_DEV_CAP_MAX_SRQ_OFFSET		0x15
#define QUERY_DEV_CAP_RSVD_EEC_OFFSET		0x16
#define QUERY_DEV_CAP_MAX_EEC_OFFSET		0x17
#define QUERY_DEV_CAP_RSVD_EQ_OFFSET		0x18
#define QUERY_DEV_CAP_MAX_CQ_SZ_OFFSET		0x19
#define QUERY_DEV_CAP_RSVD_CQ_OFFSET		0x1a
#define QUERY_DEV_CAP_MAX_CQ_OFFSET 	0x1b
#define QUERY_DEV_CAP_MAX_MPT_OFFSET		0x1d
#define QUERY_DEV_CAP_LOG_RSVD_EQ_OFFSET	0x1e
#define QUERY_DEV_CAP_MAX_EQ_OFFSET 	0x1f
#define QUERY_DEV_CAP_RSVD_MTT_OFFSET		0x20
#define QUERY_DEV_CAP_MAX_MRW_SZ_OFFSET 	0x21
#define QUERY_DEV_CAP_RSVD_MRW_OFFSET		0x22
#define QUERY_DEV_CAP_MAX_MTT_SEG_OFFSET	0x23
#define QUERY_DEV_CAP_MAX_AV_OFFSET 	0x27
#define QUERY_DEV_CAP_MAX_REQ_QP_OFFSET 	0x29
#define QUERY_DEV_CAP_MAX_RES_QP_OFFSET 	0x2b
#define QUERY_DEV_CAP_MAX_GSO_OFFSET		0x2d
#define QUERY_DEV_CAP_MAX_RDMA_OFFSET		0x2f
#define QUERY_DEV_CAP_RSZ_SRQ_OFFSET		0x33
#define QUERY_DEV_CAP_ACK_DELAY_OFFSET		0x35
#define QUERY_DEV_CAP_MTU_WIDTH_OFFSET		0x36
#define QUERY_DEV_CAP_VL_PORT_OFFSET		0x37
#define QUERY_DEV_CAP_MAX_MSG_SZ_OFFSET 	0x38
#define QUERY_DEV_CAP_MAX_GID_OFFSET		0x3b
#define QUERY_DEV_CAP_RATE_SUPPORT_OFFSET	0x3c
#define QUERY_DEV_CAP_MAX_PKEY_OFFSET		0x3f
#define QUERY_DEV_CAP_EXT_FLAGS_OFFSET		0x40
#define QUERY_DEV_IF_CNT_SUPPORT_OFFSET		0x41
#define QUERY_DEV_CAP_UDP_RSS_OFFSET		0x42
#define QUERY_DEV_CAP_ETH_UC_LOOPBACK_OFFSET	0x43
#define QUERY_DEV_CAP_FLAGS_OFFSET		0x44
#define QUERY_DEV_CAP_RSVD_UAR_OFFSET		0x48
#define QUERY_DEV_CAP_UAR_SZ_OFFSET 	0x49
#define QUERY_DEV_CAP_PAGE_SZ_OFFSET		0x4b
#define QUERY_DEV_CAP_BF_OFFSET 		0x4c
#define QUERY_DEV_CAP_LOG_BF_REG_SZ_OFFSET	0x4d
#define QUERY_DEV_CAP_LOG_MAX_BF_REGS_PER_PAGE_OFFSET	0x4e
#define QUERY_DEV_CAP_LOG_MAX_BF_PAGES_OFFSET	0x4f
#define QUERY_DEV_CAP_MAX_SG_SQ_OFFSET		0x51
#define QUERY_DEV_CAP_MAX_DESC_SZ_SQ_OFFSET 0x52
#define QUERY_DEV_CAP_MAX_SG_RQ_OFFSET		0x55
#define QUERY_DEV_CAP_MAX_DESC_SZ_RQ_OFFSET 0x56
#define QUERY_DEV_CAP_MAX_QP_MCG_OFFSET 	0x61
#define QUERY_DEV_CAP_RSVD_MCG_OFFSET		0x62
#define QUERY_DEV_CAP_MAX_MCG_OFFSET		0x63
#define QUERY_DEV_CAP_RSVD_PD_OFFSET		0x64
#define QUERY_DEV_CAP_MAX_PD_OFFSET 	0x65
#define QUERY_DEV_CAP_RSVD_XRC_OFFSET		0x66
#define QUERY_DEV_CAP_MAX_XRC_OFFSET		0x67
#define QUERY_DEV_CAP_MAX_IF_CNT_BASIC_OFFSET 0x68
#define QUERY_DEV_CAP_MAX_IF_CNT_EXTENDED_OFFSET 0x6C
#define QUERY_DEV_CAP_RDMARC_ENTRY_SZ_OFFSET	0x80
#define QUERY_DEV_CAP_QPC_ENTRY_SZ_OFFSET	0x82
#define QUERY_DEV_CAP_AUX_ENTRY_SZ_OFFSET	0x84
#define QUERY_DEV_CAP_ALTC_ENTRY_SZ_OFFSET	0x86
#define QUERY_DEV_CAP_EQC_ENTRY_SZ_OFFSET	0x88
#define QUERY_DEV_CAP_CQC_ENTRY_SZ_OFFSET	0x8a
#define QUERY_DEV_CAP_SRQ_ENTRY_SZ_OFFSET	0x8c
#define QUERY_DEV_CAP_C_MPT_ENTRY_SZ_OFFSET 0x8e
#define QUERY_DEV_CAP_MTT_ENTRY_SZ_OFFSET	0x90
#define QUERY_DEV_CAP_D_MPT_ENTRY_SZ_OFFSET 0x92
#define QUERY_DEV_CAP_BMME_FLAGS_OFFSET 	0x97
#define QUERY_DEV_CAP_RSVD_LKEY_OFFSET		0x98
#define QUERY_DEV_CAP_MAX_ICM_SZ_OFFSET 	0xa0

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = (u32*)mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma.da, 0, 0, MLX4_CMD_QUERY_DEV_CAP,
			   MLX4_CMD_TIME_CLASS_A);
	if (err)
		goto out;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_QP_OFFSET);
	dev_cap->reserved_qps = 1 << (field & 0xf);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_QP_OFFSET);
	dev_cap->max_qps = 1 << (field & 0x1f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_SRQ_OFFSET);
	dev_cap->reserved_srqs = 1 << (field >> 4);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_SRQ_OFFSET);
	dev_cap->max_srqs = 1 << (field & 0x1f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_CQ_SZ_OFFSET);
	dev_cap->max_cq_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_CQ_OFFSET);
	dev_cap->reserved_cqs = 1 << (field & 0xf);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_CQ_OFFSET);
	dev_cap->max_cqs = 1 << (field & 0x1f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MPT_OFFSET);
	dev_cap->max_mpts = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_LOG_RSVD_EQ_OFFSET);
	dev_cap->reserved_eqs = 1 << (field & 0x0f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_EQ_OFFSET);
	if ( (field & 0xff) > 0 )
		dev_cap->reserved_eqs = field & 0xff;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_EQ_OFFSET);
	dev_cap->max_eqs = 1 << (field & 0xf);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_MTT_OFFSET);
	dev_cap->reserved_mtts = 1 << (field >> 4);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MRW_SZ_OFFSET);
	dev_cap->max_mrw_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_MRW_OFFSET);
	dev_cap->reserved_mrws = 1 << (field & 0xf);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MTT_SEG_OFFSET);
	dev_cap->max_mtt_seg = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_REQ_QP_OFFSET);
	dev_cap->max_requester_per_qp = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_RES_QP_OFFSET);
	dev_cap->max_responder_per_qp = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_GSO_OFFSET);
	field &= 0x1f;
	if (!field)
		dev_cap->max_gso_sz = 0;
	else
		dev_cap->max_gso_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_RDMA_OFFSET);
	dev_cap->max_rdma_global = 1 << (field & 0x3f);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_ACK_DELAY_OFFSET);
	dev_cap->local_ca_ack_delay = field & 0x1f;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MTU_WIDTH_OFFSET);
	dev_cap->pf_num = field;
	if (is_mfunc(dev))
		dev->flags |= MLX4_FLAG_MASTER;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_VL_PORT_OFFSET);
	dev_cap->num_ports = field & 0xf;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MSG_SZ_OFFSET);
	dev_cap->max_msg_sz = 1 << (field & 0x1f);
	MLX4_GET(stat_rate, outbox, QUERY_DEV_CAP_RATE_SUPPORT_OFFSET);
	dev_cap->stat_rate_support = stat_rate;
	MLX4_GET(field, outbox, QUERY_DEV_IF_CNT_SUPPORT_OFFSET);
	dev_cap->if_cnt_basic_support    = field & 0x1;
    dev_cap->if_cnt_extended_support = field & 0x2;    
 	MLX4_GET(field, outbox, QUERY_DEV_CAP_UDP_RSS_OFFSET);
	dev_cap->vep_uc_steering = field & 0x2;
    dev_cap->vep_mc_steering = field & 0x4;  
	dev_cap->steering_by_vlan = field & 0x8; 
	MLX4_GET(field, outbox, QUERY_DEV_CAP_ETH_UC_LOOPBACK_OFFSET);
    dev_cap->loopback_support = field & 0x1;
	dev_cap->hds = field & 0x10;
    dev_cap->header_lookahead = field & 0x20;
	dev_cap->wol =	field & 0x40;
	MLX4_GET(tmp1, outbox, QUERY_DEV_CAP_EXT_FLAGS_OFFSET);
	MLX4_GET(tmp2, outbox, QUERY_DEV_CAP_FLAGS_OFFSET);
	dev_cap->flags = tmp2 | (u64)tmp1 << 32;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_UAR_OFFSET);
	dev_cap->reserved_uars = field >> 4;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_UAR_SZ_OFFSET);
	dev_cap->uar_size = 1 << ((field & 0x3f) + 20);
	MLX4_GET(field, outbox, QUERY_DEV_CAP_PAGE_SZ_OFFSET);
	dev_cap->min_page_sz = 1 << field;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_BF_OFFSET);
	if (field & 0x80) {
		MLX4_GET(field, outbox, QUERY_DEV_CAP_LOG_BF_REG_SZ_OFFSET);
		dev_cap->bf_reg_size = 1 << (field & 0x1f);
		MLX4_GET(field, outbox, QUERY_DEV_CAP_LOG_MAX_BF_REGS_PER_PAGE_OFFSET);
		if ((1 << (field & 0x3f)) > (PAGE_SIZE / dev_cap->bf_reg_size)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "log blue flame is invalid (%d), forcing 3\n", field & 0x1f));
			field = 3;
	 	}
		dev_cap->bf_regs_per_page = 1 << (field & 0x3f);
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: BlueFlame available (reg size %d, regs/page %d)\n",
			dev->pdev->name, dev_cap->bf_reg_size, dev_cap->bf_regs_per_page));
	} else {
		dev_cap->bf_reg_size = 0;
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: BlueFlame not available\n", dev->pdev->name ));
	}

	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_SG_SQ_OFFSET);
	dev_cap->max_sq_sg = field;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_MAX_DESC_SZ_SQ_OFFSET);
	dev_cap->max_sq_desc_sz = size;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_QP_MCG_OFFSET);
	dev_cap->max_qp_per_mcg = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_MCG_OFFSET);
	dev_cap->reserved_mgms = field & 0xf;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_MCG_OFFSET);
	dev_cap->max_mcgs = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_PD_OFFSET);
	dev_cap->reserved_pds = field >> 4;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_PD_OFFSET);
	dev_cap->max_pds = 1 << (field & 0x3f);

	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSVD_XRC_OFFSET);
	dev_cap->reserved_xrcds = field >> 4;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_XRC_OFFSET);
	dev_cap->max_xrcds = 1 << (field & 0x1f);
    
	MLX4_GET(dev_cap->max_if_cnt_basic, outbox, QUERY_DEV_CAP_MAX_IF_CNT_BASIC_OFFSET);
	MLX4_GET(dev_cap->max_if_cnt_extended, outbox, QUERY_DEV_CAP_MAX_IF_CNT_EXTENDED_OFFSET);

	MLX4_GET(size, outbox, QUERY_DEV_CAP_RDMARC_ENTRY_SZ_OFFSET);
	dev_cap->rdmarc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_QPC_ENTRY_SZ_OFFSET);
	dev_cap->qpc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_AUX_ENTRY_SZ_OFFSET);
	dev_cap->aux_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_ALTC_ENTRY_SZ_OFFSET);
	dev_cap->altc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_EQC_ENTRY_SZ_OFFSET);
	dev_cap->eqc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_CQC_ENTRY_SZ_OFFSET);
	dev_cap->cqc_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_SRQ_ENTRY_SZ_OFFSET);
	dev_cap->srq_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_C_MPT_ENTRY_SZ_OFFSET);
	dev_cap->cmpt_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_MTT_ENTRY_SZ_OFFSET);
	dev_cap->mtt_entry_sz = size;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_D_MPT_ENTRY_SZ_OFFSET);
	dev_cap->dmpt_entry_sz = size;

	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_SRQ_SZ_OFFSET);
	dev_cap->max_srq_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_QP_SZ_OFFSET);
	dev_cap->max_qp_sz = 1 << field;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_RSZ_SRQ_OFFSET);
	dev_cap->resize_srq = field & 1;
	MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_SG_RQ_OFFSET);
	dev_cap->max_rq_sg = field;
	MLX4_GET(size, outbox, QUERY_DEV_CAP_MAX_DESC_SZ_RQ_OFFSET);
	dev_cap->max_rq_desc_sz = size;

	MLX4_GET(dev_cap->bmme_flags, outbox,
		 QUERY_DEV_CAP_BMME_FLAGS_OFFSET);
	MLX4_GET(dev_cap->reserved_lkey, outbox,
		 QUERY_DEV_CAP_RSVD_LKEY_OFFSET);
	MLX4_GET(dev_cap->max_icm_sz, outbox,
		 QUERY_DEV_CAP_MAX_ICM_SZ_OFFSET);

	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
		for (i = 1; i <= dev_cap->num_ports; ++i) {
			MLX4_GET(field, outbox, QUERY_DEV_CAP_VL_PORT_OFFSET);
			dev_cap->max_vl[i]	   = field >> 4;
			MLX4_GET(field, outbox, QUERY_DEV_CAP_MTU_WIDTH_OFFSET);
			dev_cap->ib_mtu[i]	   = field >> 4;			
			dev_cap->max_port_width[i] = field & 0xf;
			MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_GID_OFFSET);
			dev_cap->max_gids[i]	   = 1 << (field & 0xf);
			MLX4_GET(field, outbox, QUERY_DEV_CAP_MAX_PKEY_OFFSET);
			dev_cap->max_pkeys[i]	   = 1 << (field & 0xf);
		}
	} else {
#define QUERY_PORT_SUPPORTED_TYPE_OFFSET	0x00
#define QUERY_PORT_MTU_OFFSET			0x01
#define QUERY_PORT_ETH_MTU_OFFSET		0x02
#define QUERY_PORT_WIDTH_OFFSET 		0x06
#define QUERY_PORT_MAX_GID_PKEY_OFFSET		0x07
#define QUERY_PORT_MAX_MACVLAN_OFFSET		0x0a
#define QUERY_PORT_MAX_VL_OFFSET		0x0b
#define QUERY_PORT_MAC_OFFSET			0x10

		for (i = 1; i <= dev_cap->num_ports; ++i) {
			err = mlx4_cmd_box(dev, 0, mailbox->dma.da, i, 0, MLX4_CMD_QUERY_PORT,
					   MLX4_CMD_TIME_CLASS_B);
			if (err)
				goto out;
			
			MLX4_GET(field, outbox, QUERY_PORT_SUPPORTED_TYPE_OFFSET);
			dev_cap->port_types_cap[i] = field & 3;
            dev_cap->port_types_default[i] = (field >> 3) & 1;
            dev_cap->port_types_do_sense[i] = (field >> 4) & 1;
			MLX4_GET(field, outbox, QUERY_PORT_MTU_OFFSET);
			dev_cap->ib_mtu[i]	   = field & 0xf;
			MLX4_GET(field, outbox, QUERY_PORT_WIDTH_OFFSET);
			dev_cap->max_port_width[i] = field & 0xf;
			MLX4_GET(field, outbox, QUERY_PORT_MAX_GID_PKEY_OFFSET);
			dev_cap->max_gids[i]	   = 1 << (field >> 4);
			dev_cap->max_pkeys[i]	   = 1 << (field & 0xf);
			MLX4_GET(field, outbox, QUERY_PORT_MAX_VL_OFFSET);
			dev_cap->max_vl[i]	   = field & 0xf;
			MLX4_GET(field, outbox, QUERY_PORT_MAX_MACVLAN_OFFSET);
			dev_cap->log_max_macs[i]  = field & 0xf;
			dev_cap->log_max_vlans[i] = field >> 4;
			MLX4_GET(dev_cap->eth_mtu[i], outbox, QUERY_PORT_ETH_MTU_OFFSET);
			MLX4_GET(dev_cap->def_mac[i], outbox, QUERY_PORT_MAC_OFFSET);

		}
	}

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Base MM extensions: flags %08x, rsvd L_Key %08x\n",
			dev->pdev->name, dev_cap->bmme_flags, dev_cap->reserved_lkey));

	/*
	 * Each UAR has 4 EQ doorbells; so if a UAR is reserved, then
	 * we can't use any EQs whose doorbell falls on that page,
	 * even if the EQ itself isn't reserved.
	 */
	dev_cap->reserved_eqs = max(dev_cap->reserved_uars * 4, dev_cap->reserved_eqs);

	dev_cap->clp_ver = mlx4_QUERY_CLP(dev);

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max ICM size %I64d MB\n",
		dev->pdev->name, dev_cap->max_icm_sz >> 20));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max QPs: %d, reserved QPs: %d, entry size: %d\n",
		 dev->pdev->name, dev_cap->max_qps, dev_cap->reserved_qps, dev_cap->qpc_entry_sz));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max SRQs: %d, reserved SRQs: %d, entry size: %d\n",
		 dev->pdev->name, dev_cap->max_srqs, dev_cap->reserved_srqs, dev_cap->srq_entry_sz));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max CQs: %d, reserved CQs: %d, entry size: %d\n",
		 dev->pdev->name, dev_cap->max_cqs, dev_cap->reserved_cqs, dev_cap->cqc_entry_sz));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max EQs: %d, reserved EQs: %d, entry size: %d\n",
		 dev->pdev->name, dev_cap->max_eqs, dev_cap->reserved_eqs, dev_cap->eqc_entry_sz));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: reserved MPTs: %d, reserved MTTs: %d\n",
		 dev->pdev->name, dev_cap->reserved_mrws, dev_cap->reserved_mtts));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max PDs: %d, reserved PDs: %d, reserved UARs: %d\n",
		 dev->pdev->name, dev_cap->max_pds, dev_cap->reserved_pds, dev_cap->reserved_uars));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max QP/MCG: %d, reserved MGMs: %d\n",
		 dev->pdev->name, dev_cap->max_pds, dev_cap->reserved_mgms));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max CQEs: %d, max WQEs: %d, max SRQ WQEs: %d\n",
		 dev->pdev->name, dev_cap->max_cq_sz, dev_cap->max_qp_sz, dev_cap->max_srq_sz));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Local CA ACK delay: %d, max MTU: %d, port width cap: %d\n",
		 dev->pdev->name, dev_cap->local_ca_ack_delay, 128 << dev_cap->ib_mtu[1],
		 dev_cap->max_port_width[1]));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max SQ desc size: %d, max SQ S/G: %d\n",
		 dev->pdev->name, dev_cap->max_sq_desc_sz, dev_cap->max_sq_sg));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max RQ desc size: %d, max RQ S/G: %d\n",
		 dev->pdev->name, dev_cap->max_rq_desc_sz, dev_cap->max_rq_sg));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Max GSO size: %d\n", dev->pdev->name, dev_cap->max_gso_sz));

	dump_dev_cap_flags(dev, dev_cap->flags);

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_map_cmd(struct mlx4_dev *dev, u16 op, struct mlx4_icm *icm, u64 virt)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_icm_iter iter;
	__be64 *pages;
	int lg;
	int nent = 0;
	unsigned int i;
	int err = 0;
	int ts = 0, tc = 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	memset(mailbox->buf, 0, MLX4_MAILBOX_SIZE);
	pages = (__be64*)mailbox->buf;

	for (mlx4_icm_first(icm, &iter);
		 !mlx4_icm_last(&iter);
		 mlx4_icm_next(&iter)) {
		/*
		 * We have to pass pages that are aligned to their
		 * size, so find the least significant 1 in the
		 * address or size and use that as our log2 size.
		 */
		unsigned long end = (unsigned long)(mlx4_icm_addr(&iter).da | mlx4_icm_size(&iter));
		lg = ffs(end) - 1;
		if (lg < MLX4_ICM_PAGE_SHIFT) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Got FW area not aligned to %d (%I64x/%lx).\n",
				dev->pdev->name, MLX4_ICM_PAGE_SIZE,
				   mlx4_icm_addr(&iter).da,
				   mlx4_icm_size(&iter)));
			err = -EINVAL;
			goto out;
		}

		for (i = 0; i < mlx4_icm_size(&iter) >> lg; ++i) {
			if (virt != -1) {
				pages[nent * 2] = cpu_to_be64(virt);
				virt += 1I64 << lg;
			}

			pages[nent * 2 + 1] =
				cpu_to_be64((mlx4_icm_addr(&iter).da + (i << lg)) |
						(lg - MLX4_ICM_PAGE_SHIFT));
			ts += 1 << (lg - 10);
			++tc;

			if (++nent == MLX4_MAILBOX_SIZE / 16) {
				err = mlx4_cmd(dev, mailbox->dma.da, nent, 0, op,
						MLX4_CMD_TIME_CLASS_B);
				if (err)
					goto out;
				nent = 0;
			}
		}
	}

	if (nent)
		err = mlx4_cmd(dev, mailbox->dma.da, nent, 0, op, MLX4_CMD_TIME_CLASS_B);
	if (err)
		goto out;

#if 0
	switch (op) {
	case MLX4_CMD_MAP_FA:
		mlx4_dbg(dev, "%s: Mapped %d chunks/%d KB for FW.\n", dev->pdev->name, tc, ts);
		break;
	case MLX4_CMD_MAP_ICM_AUX:
		mlx4_dbg(dev, "%s: Mapped %d chunks/%d KB for ICM aux.\n", dev->pdev->name, tc, ts);
		break;
	case MLX4_CMD_MAP_ICM:
		mlx4_dbg(dev, "%s: Mapped %d chunks/%d KB at %llx for ICM.\n",
			  dev->pdev->name, tc, ts, (unsigned long long) virt - (ts << 10));
		break;
	}
#endif	

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_MAP_FA(struct mlx4_dev *dev, struct mlx4_icm *icm)
{
	return mlx4_map_cmd(dev, MLX4_CMD_MAP_FA, icm, (u64)-1);
}

int mlx4_UNMAP_FA(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_UNMAP_FA, MLX4_CMD_TIME_CLASS_B);
}


int mlx4_RUN_FW(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_RUN_FW, MLX4_CMD_TIME_CLASS_A);
}

int mlx4_QUERY_FW(struct mlx4_dev *dev)
{
	struct mlx4_fw	*fw  = &mlx4_priv(dev)->fw;
	struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
	struct mlx4_cmd_mailbox *mailbox;
	u32 *outbox;
	int err = 0;
	u64 fw_ver;
	u16 cmd_if_rev;
	u8 lg;

#define QUERY_FW_OUT_SIZE			  0x100
#define QUERY_FW_VER_OFFSET 		   0x00
#define QUERY_FW_PPF_ID 			   0x09
#define QUERY_FW_CMD_IF_REV_OFFSET	   0x0a
#define QUERY_FW_MAX_CMD_OFFSET 	   0x0f
#define QUERY_FW_ERR_START_OFFSET	   0x30
#define QUERY_FW_ERR_SIZE_OFFSET	   0x38
#define QUERY_FW_ERR_BAR_OFFSET 	   0x3c

#define QUERY_FW_SIZE_OFFSET		   0x00
#define QUERY_FW_CLR_INT_BASE_OFFSET   0x20
#define QUERY_FW_CLR_INT_BAR_OFFSET    0x28

#define QUERY_FW_COMM_BASE_OFFSET	   0x40
#define QUERY_FW_COMM_BAR_OFFSET	   0x48

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = (u32*)mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma.da, 0, 0, MLX4_CMD_QUERY_FW,
				MLX4_CMD_TIME_CLASS_A);
	if (err)
		goto out;

	MLX4_GET(fw_ver, outbox, QUERY_FW_VER_OFFSET);
	/*
	 * FW subminor version is at more significant bits than minor
	 * version, so swap here.
	 */
	dev->caps.fw_ver = (fw_ver & 0xffff00000000ull) |
		((fw_ver & 0xffff0000ull) >> 16) |
		((fw_ver & 0x0000ffffull) << 16);

	MLX4_GET(lg, outbox, QUERY_FW_PPF_ID);
	dev->caps.function = lg;

	MLX4_GET(cmd_if_rev, outbox, QUERY_FW_CMD_IF_REV_OFFSET);
	if (cmd_if_rev < MLX4_COMMAND_INTERFACE_MIN_REV ||
		cmd_if_rev > MLX4_COMMAND_INTERFACE_MAX_REV) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: Installed FW has unsupported command interface revision %d.\n",dev->pdev->name, cmd_if_rev));
        
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: (Installed FW version is %d.%d.%03d)\n",
			        dev->pdev->name, 
			 (int) (dev->caps.fw_ver >> 32),
			 (int) (dev->caps.fw_ver >> 16) & 0xffff,
			 (int) dev->caps.fw_ver & 0xffff));

        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: This driver version supports only revisions %d to %d.\n",
			    dev->pdev->name,
			    MLX4_COMMAND_INTERFACE_MIN_REV, MLX4_COMMAND_INTERFACE_MAX_REV));
        
		err = -ENODEV;
		goto out;
	}

	if (dev->caps.fw_ver < FW_FIRST_SUPPORTED) {        
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			("%s: HCA FW version %d.%d.%d is not supported. Use %d.%d.%d or higher.\n",
        			dev->pdev->name, 
        			(int) (dev->caps.fw_ver >> 32), (int) (dev->caps.fw_ver >> 16) & 0xffff,
        			(int) (dev->caps.fw_ver & 0xffff), (int) (FW_FIRST_SUPPORTED >> 32),
        			(int) (FW_FIRST_SUPPORTED>> 16) & 0xffff, (int) (FW_FIRST_SUPPORTED & 0xffff)));
		err = -ENODEV;
		goto out;
	}
	else 
	if (dev->caps.fw_ver < FW_MIN_GOOD_SUPPORTED) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: The HCA FW version is not the latest one. \n"
			"If you meet any issues with the HCA please first try to upgrade the FW to version %d.%d.%d or higher.\n",
			dev->pdev->name, 
			(int) (FW_MIN_GOOD_SUPPORTED >> 32), (int) (FW_MIN_GOOD_SUPPORTED >> 16) & 0xffff, (int) (FW_MIN_GOOD_SUPPORTED & 0xffff)));
        
	}

	if (cmd_if_rev < MLX4_COMMAND_INTERFACE_NEW_PORT_CMDS)
		dev->flags |= MLX4_FLAG_OLD_PORT_CMDS;

	MLX4_GET(lg, outbox, QUERY_FW_MAX_CMD_OFFSET);
	cmd->max_cmds = 1 << lg;

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Current FW version %d.%d.%03d (cmd intf rev %d), max commands %d\n",
		dev->pdev->name, (int) (dev->caps.fw_ver >> 32), (int) (dev->caps.fw_ver >> 16) & 0xffff,
		(int) dev->caps.fw_ver & 0xffff, cmd_if_rev, cmd->max_cmds));

	MLX4_GET(fw->catas_offset, outbox, QUERY_FW_ERR_START_OFFSET);
	MLX4_GET(fw->catas_size,   outbox, QUERY_FW_ERR_SIZE_OFFSET);
	MLX4_GET(fw->catas_bar,    outbox, QUERY_FW_ERR_BAR_OFFSET);
	fw->catas_bar = (fw->catas_bar >> 6) * 2;

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Catastrophic error buffer at 0x%I64x, size 0x%x, BAR %d\n",
		dev->pdev->name, fw->catas_offset, fw->catas_size, fw->catas_bar));

	MLX4_GET(fw->fw_pages,	   outbox, QUERY_FW_SIZE_OFFSET);
	MLX4_GET(fw->clr_int_base, outbox, QUERY_FW_CLR_INT_BASE_OFFSET);
	MLX4_GET(fw->clr_int_bar,  outbox, QUERY_FW_CLR_INT_BAR_OFFSET);
	fw->clr_int_bar = (fw->clr_int_bar >> 6) * 2;

	MLX4_GET(fw->comm_base, outbox, QUERY_FW_COMM_BASE_OFFSET);
	MLX4_GET(fw->comm_bar,	outbox, QUERY_FW_COMM_BAR_OFFSET);
	fw->comm_bar = (fw->comm_bar >> 6) * 2;
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Communication vector bar:%d offset:0x%I64x\n", 
		dev->pdev->name, fw->comm_bar, fw->comm_base));

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: FW size %d KB\n", dev->pdev->name, fw->fw_pages >> 2));

	/*
	 * Round up number of system pages needed in case
	 * MLX4_ICM_PAGE_SIZE < PAGE_SIZE.
	 */
	fw->fw_pages =
		ALIGN(fw->fw_pages, PAGE_SIZE / MLX4_ICM_PAGE_SIZE) >>
		(PAGE_SHIFT - MLX4_ICM_PAGE_SHIFT);

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Clear int @ %I64x, BAR %d\n",
		 dev->pdev->name, fw->clr_int_base, fw->clr_int_bar));

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}


static int
query_port_state(struct mlx4_dev *dev, u8 port)
{
	int err = 0;
	u32 input_modifier = 0;    
	u64 out_param;

#define MOD_STAT_OPMOD_QUERY_INLINE 0x3
#define MOD_STAT_OFFSET_PORT_EN 0x8

	input_modifier = (1 << 28) | (port << 8) | MOD_STAT_OFFSET_PORT_EN;

	err = mlx4_cmd_imm(dev, 0, &out_param, input_modifier, 
				MOD_STAT_OPMOD_QUERY_INLINE, 
				MLX4_CMD_QUERY_STAT_CFG,
				MLX4_CMD_TIME_CLASS_A);
	if (err) {
		return err;
	}
	
	dev->caps.port_state[port] = (((out_param >> 20) & 1) ? MLX4_PORT_ENABLED : MLX4_PORT_DISABLED);
	return 0;
}

int mlx4_port_state(struct mlx4_dev *dev)
{
	u8 i = 0;
	int err = 0;

	
	for (i = 1; i <= dev->caps.num_ports; ++i)
	{
		dev->caps.port_state[i] = MLX4_PORT_ENABLED;
	}
	
	for (i = 1; i <= dev->caps.num_ports; ++i)
	{
		err = query_port_state(dev, i);
		if (err)
		{
			return err;
		}
	}
	
	return 0;
}

static void get_board_id(u8 *vsd, char *board_id)
{
	int i;

#define VSD_OFFSET_SIG1 	0x00
#define VSD_OFFSET_SIG2 	0xde
#define VSD_OFFSET_MLX_BOARD_ID 0xd0
#define VSD_OFFSET_TS_BOARD_ID	0x20

#define VSD_SIGNATURE_TOPSPIN	0x5ad

	memset(board_id, 0, MLX4_BOARD_ID_LEN);

	if (be16_to_cpup(vsd + VSD_OFFSET_SIG1) == VSD_SIGNATURE_TOPSPIN &&
		be16_to_cpup(vsd + VSD_OFFSET_SIG2) == VSD_SIGNATURE_TOPSPIN) {
		strlcpy(board_id, vsd + VSD_OFFSET_TS_BOARD_ID, MLX4_BOARD_ID_LEN);
	} else {
		/*
		 * The board ID is a string but the firmware byte
		 * swaps each 4-byte word before passing it back to
		 * us.	Therefore we need to swab it before printing.
		 */
		for (i = 0; i < 4; ++i)
			((u32 *) board_id)[i] =
				swab32(*(u32 *) (vsd + VSD_OFFSET_MLX_BOARD_ID + i * 4));
	}
}

int mlx4_QUERY_ADAPTER(struct mlx4_dev *dev, struct mlx4_adapter *adapter)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *outbox;
	int err;

#define QUERY_ADAPTER_OUT_SIZE			   0x100
#define QUERY_ADAPTER_INTA_PIN_OFFSET	   0x10
#define QUERY_ADAPTER_VSD_OFFSET		   0x20

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = (u32*)mailbox->buf;

	err = mlx4_cmd_box(dev, 0, mailbox->dma.da, 0, 0, MLX4_CMD_QUERY_ADAPTER,
			   MLX4_CMD_TIME_CLASS_A);
	if (err)
		goto out;

	MLX4_GET(adapter->inta_pin, outbox,    QUERY_ADAPTER_INTA_PIN_OFFSET);

	get_board_id((u8*)(outbox + QUERY_ADAPTER_VSD_OFFSET / 4),
			 adapter->board_id);

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_INIT_HCA(struct mlx4_dev *dev, struct mlx4_init_hca_param *param)
{
	struct mlx4_cmd_mailbox *mailbox;
	__be32 *inbox;
	int err;

#define INIT_HCA_IN_SIZE					0x200
#define INIT_HCA_VERSION_OFFSET 	 		0x000
#define INIT_HCA_VERSION		 			2
#define INIT_HCA_CACHELINE_SZ_OFFSET	 	0x0e
#define INIT_HCA_X86_64_BYTE_CACHELINE_SZ	0x40
#define INIT_HCA_FLAGS_OFFSET		 		0x014
#define INIT_HCA_QPC_OFFSET 	 			0x020
#define INIT_HCA_EQE_CQE_OFFSETS	 		(INIT_HCA_QPC_OFFSET + 0x38)
#define INIT_HCA_QPC_BASE_OFFSET	 		(INIT_HCA_QPC_OFFSET + 0x10)
#define INIT_HCA_LOG_QP_OFFSET 	 			(INIT_HCA_QPC_OFFSET + 0x17)
#define INIT_HCA_SRQC_BASE_OFFSET			(INIT_HCA_QPC_OFFSET + 0x28)
#define INIT_HCA_LOG_SRQ_OFFSET				(INIT_HCA_QPC_OFFSET + 0x2f)
#define INIT_HCA_CQC_BASE_OFFSET			(INIT_HCA_QPC_OFFSET + 0x30)
#define INIT_HCA_LOG_CQ_OFFSET 	 			(INIT_HCA_QPC_OFFSET + 0x37)
#define INIT_HCA_ALTC_BASE_OFFSET	 		(INIT_HCA_QPC_OFFSET + 0x40)
#define INIT_HCA_AUXC_BASE_OFFSET	 		(INIT_HCA_QPC_OFFSET + 0x50)
#define INIT_HCA_EQC_BASE_OFFSET	 		(INIT_HCA_QPC_OFFSET + 0x60)
#define INIT_HCA_LOG_EQ_OFFSET 	 			(INIT_HCA_QPC_OFFSET + 0x67)
#define INIT_HCA_RDMARC_BASE_OFFSET	 		(INIT_HCA_QPC_OFFSET + 0x70)
#define INIT_HCA_LOG_RD_OFFSET 	 			(INIT_HCA_QPC_OFFSET + 0x77)
#define INIT_HCA_MCAST_OFFSET		 		0x0c0
#define INIT_HCA_MC_BASE_OFFSET	 			(INIT_HCA_MCAST_OFFSET + 0x00)
#define INIT_HCA_LOG_MC_ENTRY_SZ_OFFSET 	(INIT_HCA_MCAST_OFFSET + 0x12)
#define INIT_HCA_LOG_MC_HASH_SZ_OFFSET  	(INIT_HCA_MCAST_OFFSET + 0x16)
#define INIT_HCA_UC_STEERING_OFFSET	 		(INIT_HCA_MCAST_OFFSET + 0x18)
#define INIT_HCA_LOG_MC_TABLE_SZ_OFFSET 	(INIT_HCA_MCAST_OFFSET + 0x1b)
#define INIT_HCA_TPT_OFFSET 	 			0x0f0
#define INIT_HCA_DMPT_BASE_OFFSET	 		(INIT_HCA_TPT_OFFSET + 0x00)
#define INIT_HCA_LOG_MPT_SZ_OFFSET  		(INIT_HCA_TPT_OFFSET + 0x0b)
#define INIT_HCA_MTT_BASE_OFFSET	 		(INIT_HCA_TPT_OFFSET + 0x10)
#define INIT_HCA_CMPT_BASE_OFFSET	 		(INIT_HCA_TPT_OFFSET + 0x18)
#define INIT_HCA_UAR_OFFSET 	 			0x120
#define INIT_HCA_LOG_UAR_SZ_OFFSET  		(INIT_HCA_UAR_OFFSET + 0x0a)
#define INIT_HCA_UAR_PAGE_SZ_OFFSET	 		(INIT_HCA_UAR_OFFSET + 0x0b)

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	inbox = (__be32*)mailbox->buf;

	memset(inbox, 0, INIT_HCA_IN_SIZE);

	*((u8 *) mailbox->buf + INIT_HCA_VERSION_OFFSET) = INIT_HCA_VERSION;
#ifndef _M_IX86
	*((u8 *) mailbox->buf + INIT_HCA_CACHELINE_SZ_OFFSET) = INIT_HCA_X86_64_BYTE_CACHELINE_SZ;
#endif

#if defined(__LITTLE_ENDIAN)
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) &= ~cpu_to_be32(1 << 1);
#elif defined(__BIG_ENDIAN)
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 1);
#else
#error Host endianness not defined
#endif

	if (g.interrupt_from_first) {
		// Bit 30,31 tell the moderation method, 0 default, 1 from first packet
		*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 30);
	}

	/* Check port for UD address vector: */
	*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1);

	/* Enable IPoIB checksumming if we can: */
	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_IPOIB_CSUM)
		*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 3);

    /* Enable rss ip fragment support if module parameter set */
	if (dev->caps.flags & (1ULL << 52))
        *(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 13);

	/* Enable QoS support if module parameter set */
	if (g.enable_qos)
		*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 2);

	/* QPC/EEC/CQC/EQC/RDMARC attributes */

	mlx4_warn(dev, "using %d bytes CQE\n", dev->caps.cqe_size);
	if (dev->caps.cqe_size == 64)
		*(inbox + INIT_HCA_EQE_CQE_OFFSETS / 4) |= cpu_to_be32(1 << 30);

	MLX4_PUT(inbox, param->qpc_base,	  INIT_HCA_QPC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_num_qps,   INIT_HCA_LOG_QP_OFFSET);
	MLX4_PUT(inbox, param->srqc_base,	  INIT_HCA_SRQC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_num_srqs,  INIT_HCA_LOG_SRQ_OFFSET);
	MLX4_PUT(inbox, param->cqc_base,	  INIT_HCA_CQC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_num_cqs,   INIT_HCA_LOG_CQ_OFFSET);
	MLX4_PUT(inbox, param->altc_base,	  INIT_HCA_ALTC_BASE_OFFSET);
	MLX4_PUT(inbox, param->auxc_base,	  INIT_HCA_AUXC_BASE_OFFSET);
	MLX4_PUT(inbox, param->eqc_base,	  INIT_HCA_EQC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_num_eqs,   INIT_HCA_LOG_EQ_OFFSET);
	MLX4_PUT(inbox, param->rdmarc_base,   INIT_HCA_RDMARC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_rd_per_qp, INIT_HCA_LOG_RD_OFFSET);

	/* multicast attributes */

	MLX4_PUT(inbox, param->mc_base, 	INIT_HCA_MC_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_mc_entry_sz, INIT_HCA_LOG_MC_ENTRY_SZ_OFFSET);
	MLX4_PUT(inbox, param->log_mc_hash_sz,	INIT_HCA_LOG_MC_HASH_SZ_OFFSET);
	if (dev->caps.vep_mc_steering)
		MLX4_PUT(inbox, (u8) (1 << 3),	INIT_HCA_UC_STEERING_OFFSET);
	MLX4_PUT(inbox, param->log_mc_table_sz, INIT_HCA_LOG_MC_TABLE_SZ_OFFSET);

	/* TPT attributes */

	MLX4_PUT(inbox, param->dmpt_base,  INIT_HCA_DMPT_BASE_OFFSET);
	MLX4_PUT(inbox, param->log_mpt_sz, INIT_HCA_LOG_MPT_SZ_OFFSET);
	MLX4_PUT(inbox, param->mtt_base,   INIT_HCA_MTT_BASE_OFFSET);
	MLX4_PUT(inbox, param->cmpt_base,  INIT_HCA_CMPT_BASE_OFFSET);

	/* UAR attributes */

	MLX4_PUT(inbox, (u8) (PAGE_SHIFT - 12), INIT_HCA_UAR_PAGE_SZ_OFFSET);
	MLX4_PUT(inbox, param->log_uar_sz,		INIT_HCA_LOG_UAR_SZ_OFFSET);

	if (!g.mlx4_pre_t11_mode && dev->caps.flags & (u32) MLX4_DEV_CAP_FLAG_FC_T11)		 
		*(inbox + INIT_HCA_FLAGS_OFFSET / 4) |= cpu_to_be32(1 << 10);

	err = mlx4_cmd(dev, mailbox->dma.da, 0, 0, MLX4_CMD_INIT_HCA, 10000);

	if (err)
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: INIT_HCA returns %d\n", dev->pdev->name, err));

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_SET_VEP(struct mlx4_dev *dev, int slave, u8 vep_link)
{
	struct mlx4_cmd_mailbox *mailbox;
	u8 *buffer;
	int ret;
	u8 vep_num, port;	
	u32 in_param;	

	ASSERT(mlx4_is_mfunc(dev));

	vep_num = mlx4_priv(dev)->mfunc.master.slave_state[slave].vep_num;
	port = mlx4_priv(dev)->mfunc.master.slave_state[slave].port_num;
	in_param = ((u32) vep_num << 8) | port;
	
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	buffer = (u8*)mailbox->buf;

	buffer[0] = 1 << 6;
	buffer[3] = vep_link << 1;

	ret = mlx4_cmd(dev, mailbox->dma.da, in_param, 0, MLX4_CMD_SET_VEP,
			   MLX4_CMD_TIME_CLASS_A);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return ret;
}

static int mlx4_common_init_port(struct mlx4_dev *dev, int function, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;

	if (function == dev->caps.function ||
		function == priv->mfunc.master.slave_state[function].pf_num)
		mlx4_SET_VEP(dev, function, 1);

	if (priv->mfunc.master.slave_state[function].init_port_mask & (1 << port))
		return 0;

	/* Enable port only if it was previously disabled */
	if (!priv->mfunc.master.init_port_ref[port]) {
		mlx4_update_uplink_arbiter(dev, port);
		err = mlx4_cmd(dev, 0, port, 0, MLX4_CMD_INIT_PORT,
				   MLX4_CMD_TIME_CLASS_A);
		if (err)
			return err;
	}
	++priv->mfunc.master.init_port_ref[port];
	priv->mfunc.master.slave_state[function].init_port_mask |= (1 << port);
	return 0;
}

int mlx4_INIT_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							 struct mlx4_cmd_mailbox *inbox,
							 struct mlx4_cmd_mailbox *outbox)
{
	int port;
	
	UNUSED_PARAM(inbox);
	UNUSED_PARAM(outbox);
	
	port = vhcr->in_modifier;
	return mlx4_common_init_port(dev, slave, port);
}

int mlx4_INIT_PORT(struct mlx4_dev *dev, int port)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *inbox;
	int err;
	u32 flags;
	u16 field;

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: mlx4_INIT_PORT, port %d\n", dev->pdev->name, port));
	
	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
#define INIT_PORT_IN_SIZE		   256
#define INIT_PORT_FLAGS_OFFSET	   0x00
#define INIT_PORT_FLAG_SIG		   (1 << 18)
#define INIT_PORT_FLAG_NG		   (1 << 17)
#define INIT_PORT_FLAG_G0		   (1 << 16)
#define INIT_PORT_VL_SHIFT		   4
#define INIT_PORT_PORT_WIDTH_SHIFT 8
#define INIT_PORT_MTU_OFFSET	   0x04
#define INIT_PORT_MAX_GID_OFFSET   0x06
#define INIT_PORT_MAX_PKEY_OFFSET  0x0a
#define INIT_PORT_GUID0_OFFSET	   0x10
#define INIT_PORT_NODE_GUID_OFFSET 0x18
#define INIT_PORT_SI_GUID_OFFSET   0x20

		mailbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(mailbox))
			return PTR_ERR(mailbox);
		inbox = (u32*)mailbox->buf;

		memset(inbox, 0, INIT_PORT_IN_SIZE);

		flags = 0;
		flags |= (dev->caps.vl_cap[port] & 0xf) << INIT_PORT_VL_SHIFT;
		flags |= (dev->caps.port_width_cap[port] & 0xf) << INIT_PORT_PORT_WIDTH_SHIFT;
		MLX4_PUT(inbox, flags,		  INIT_PORT_FLAGS_OFFSET);

		field = (u16)(128 << dev->caps.ib_mtu_cap[port]);
		MLX4_PUT(inbox, field, INIT_PORT_MTU_OFFSET);
		field = (u16)dev->caps.gid_table_len[port];
		MLX4_PUT(inbox, field, INIT_PORT_MAX_GID_OFFSET);
		field = (u16)dev->caps.pkey_table_len[port];
		MLX4_PUT(inbox, field, INIT_PORT_MAX_PKEY_OFFSET);

		err = mlx4_cmd(dev, mailbox->dma.da, port, 0, MLX4_CMD_INIT_PORT,
				   MLX4_CMD_TIME_CLASS_A);

		mlx4_free_cmd_mailbox(dev, mailbox);
	} else {
		if (mlx4_is_master(dev))
			err = mlx4_common_init_port(dev, dev->caps.function,
							port);
		else
			err = mlx4_cmd(dev, 0, port, 0, MLX4_CMD_INIT_PORT,
					   MLX4_CMD_TIME_CLASS_A);
	}

	return err;
}

static int mlx4_common_close_port(struct mlx4_dev *dev, int function, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;

	if (function == dev->caps.function ||
		function == priv->mfunc.master.slave_state[function].pf_num)
		mlx4_SET_VEP(dev, function, 0);

	if (!(priv->mfunc.master.slave_state[function].init_port_mask & (1 << port)))
		return 0;

	if (priv->mfunc.master.init_port_ref[port] == 1) {
		err = mlx4_cmd(dev, 0, port, 0, MLX4_CMD_CLOSE_PORT, 1000);
		if (err)
			return err;
	}
	--priv->mfunc.master.init_port_ref[port];
	priv->mfunc.master.slave_state[function].init_port_mask &= ~(1 << port);
	return 0;
}

int mlx4_CLOSE_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
							  struct mlx4_cmd_mailbox *inbox,
							  struct mlx4_cmd_mailbox *outbox)
{
	int port;
	
	UNUSED_PARAM(inbox);
	UNUSED_PARAM(outbox);

	port= vhcr->in_modifier;
	return mlx4_common_close_port(dev, slave, port);
}

int mlx4_CLOSE_PORT(struct mlx4_dev *dev, int port)
{
	if ( mlx4_is_barred(dev) )
		return -EFAULT;
	
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: mlx4_CLOSE_PORT, port %d\n", dev->pdev->name, port));

	if (mlx4_is_master(dev))
		return mlx4_common_close_port(dev, dev->caps.function, port);
	else
		return mlx4_cmd(dev, 0, port, 0, MLX4_CMD_CLOSE_PORT, 1000);
}

int mlx4_CLOSE_HCA(struct mlx4_dev *dev, int panic)
{
	return mlx4_cmd(dev, 0, 0, (u8)panic, MLX4_CMD_CLOSE_HCA, 1000);
}

int mlx4_SET_ICM_SIZE(struct mlx4_dev *dev, u64 icm_size, u64 *aux_pages)
{
	int ret = mlx4_cmd_imm(dev, icm_size, aux_pages, 0, 0,
				   MLX4_CMD_SET_ICM_SIZE,
				   MLX4_CMD_TIME_CLASS_A);
	if (ret)
		return ret;

	/*
	 * Round up number of system pages needed in case
	 * MLX4_ICM_PAGE_SIZE < PAGE_SIZE.
	 */
	*aux_pages = ALIGN(*aux_pages, PAGE_SIZE / MLX4_ICM_PAGE_SIZE) >>
		(PAGE_SHIFT - MLX4_ICM_PAGE_SHIFT);

	return 0;
}

int mlx4_NOP(struct mlx4_dev *dev)
{
	/* Input modifier of 0x1f means "finish as soon as possible." */
	return mlx4_cmd(dev, 0, 0x1f, 0, MLX4_CMD_NOP, 100);
}

int mlx4_QUERY_FUNC(struct mlx4_dev *dev, int func, u8 *pf_num)
{
	struct mlx4_cmd_mailbox *mailbox;
	u8 *outbox;
	int ret;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = (u8*)mailbox->buf;

	ret = mlx4_cmd_box(dev, 0, mailbox->dma.da, func & 0xff, 0,
			   MLX4_CMD_QUERY_FUNC, MLX4_CMD_TIME_CLASS_A);
	if (ret)
		goto out;

	*pf_num = outbox[3];

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return ret;
}

int mlx4_query_diag_counters(struct mlx4_dev *dev, int array_length,
				 u8 op_modifier, u32 in_offset[], u32 counter_out[])
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 *outbox;
	int ret;
	int i;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	outbox = (u32*)mailbox->buf;

	ret = mlx4_cmd_box(dev, 0, mailbox->dma.da, 0, op_modifier,
			   MLX4_CMD_DIAG_RPRT, MLX4_CMD_TIME_CLASS_A);
	if (ret)
		goto out;

	for (i=0; i < array_length; i++) {
		if (in_offset[i] > MLX4_MAILBOX_SIZE) {
			ret = -EINVAL;
			goto out;
		}

		MLX4_GET(counter_out[i], outbox, in_offset[i]);
	}

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return ret;
}

void mlx4_get_fc_t11_settings(struct mlx4_dev *dev, int *enable_pre_t11, int *t11_supported)
{	 
	*enable_pre_t11 = !!g.mlx4_pre_t11_mode;	
	*t11_supported = !!(dev->caps.flags & MLX4_DEV_CAP_FLAG_FC_T11);
}

#define MLX4_WOL_SETUP_MODE (5 << 28)

#define	MLX4_EN_WOL_MAGIC  (1ULL << 61)
#define MLX4_EN_WOL_ENABLED (1ULL << 62)
#define	MLX4_EN_WOL_DO_MODIFY (1ULL << 63)

static int mlx4_wol_read(struct mlx4_dev *dev, u64 *config, int port)
{
	u32 in_mod = MLX4_WOL_SETUP_MODE | port << 8;

	ASSERT(!mlx4_is_mfunc(dev));

	return mlx4_cmd_imm(dev, 0, config, in_mod, 0x3,
		MLX4_CMD_MOD_STAT_CFG, MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_wol_write(struct mlx4_dev *dev, u64 config, int port)
{
	u32 in_mod = MLX4_WOL_SETUP_MODE | port << 8;

	ASSERT(!mlx4_is_mfunc(dev));
	
	return mlx4_cmd(dev, config, in_mod, 0x1, MLX4_CMD_MOD_STAT_CFG,
		MLX4_CMD_TIME_CLASS_A);
}


int mlx4_set_wol(struct mlx4_dev *dev, ULONG wol_type, int port)
{
	int err = 0;
    u64 config =0;

	if (!dev->caps.wol) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: The device unsupported WOL\n", dev->pdev->name));
		return -EOPNOTSUPP;
	}

	if ((wol_type != 0) && (wol_type & ~WAKE_MAGIC)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: The device unsupported WOL type\n", dev->pdev->name));
		return -EINVAL;
	}
	
	err = mlx4_wol_read(dev, &config, port);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("Failed to get WoL information\n"));
		return err;
	}
	
	if (wol_type & WAKE_MAGIC) {
		config |= MLX4_EN_WOL_DO_MODIFY | MLX4_EN_WOL_ENABLED | MLX4_EN_WOL_MAGIC;
	} else {
		config &= 0x1fffffffffffffffULL;
        config |= MLX4_EN_WOL_DO_MODIFY & ~MLX4_EN_WOL_MAGIC; 
	}

	err = mlx4_wol_write(dev, config, port);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("Failed to set WoL information\n"));
		return err;
	}

	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Set WOL completes successfully\n", dev->pdev->name));
	return 0;	 
}


bool is_wol_supported(struct mlx4_dev *dev, int port)
{
	struct mlx4_caps * p_caps = &(dev->caps);
    u64 config = 0;
    int err;
    
    if (!p_caps->wol) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s:WoL is not supported\n", dev->pdev->name));
        return false;
    }
    
    ASSERT((port == 1) || (port == 2));
    err = mlx4_wol_read(dev, &config, port);
    if (err) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed to get WoL information\n", dev->pdev->name));
        return false;
    }

    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s:WoL is supported\n", dev->pdev->name));
    return  (config & MLX4_EN_WOL_ENABLED) ? true : false;
}


enum {
	ADD_TO_MCG = 0x26,
};


void mlx4_opreq_action(
	IN DEVICE_OBJECT*		p_dev_obj,
	IN PVOID				context
	)
{
	struct mlx4_dev *dev = (struct mlx4_dev *)context;
	struct mlx4_priv *priv = mlx4_priv(dev); 
	int num_tasks = atomic_read(&priv->opreq_count);
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mgm *mgm;
	u32 *outbox;
	u32 modifier;
	u16 token;
	u16 type_m;
	u16 type;
	int err;
	u32 num_qps;
	struct mlx4_qp qp;
	u32 i;
	u8 rem_mcg;
	u8 prot;

#define GET_OP_REQ_MODIFIER_OFFSET	0x08
#define GET_OP_REQ_TOKEN_OFFSET		0x14
#define GET_OP_REQ_TYPE_OFFSET		0x1a
#define GET_OP_REQ_DATA_OFFSET		0x20

	UNUSED_PARAM(p_dev_obj);

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("Failed to allocate mailbox for GET_OP_REQ\n"));
		return;
	}
	outbox = (u32*)mailbox->buf;
	
	while (num_tasks) {
		err = mlx4_cmd_box(dev, 0, mailbox->dma.da, 0, 0,
				   MLX4_CMD_GET_OP_REQ, MLX4_CMD_TIME_CLASS_A);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("Failed to retreive required operation: %d\n", err));
			return;
		}
		MLX4_GET(modifier, outbox, GET_OP_REQ_MODIFIER_OFFSET);
		MLX4_GET(token, outbox, GET_OP_REQ_TOKEN_OFFSET);
		MLX4_GET(type, outbox, GET_OP_REQ_TYPE_OFFSET);
		type_m = type >> 12;
		type &= 0xfff;
	
		switch (type) {
		case ADD_TO_MCG:
			mgm = (struct mlx4_mgm *) ((u8 *) (outbox) + GET_OP_REQ_DATA_OFFSET);
			num_qps = be32_to_cpu(mgm->members_count) & MGM_QPN_MASK;
			rem_mcg = ((u8 *) (&mgm->members_count))[0] & 1;
			prot = ((u8 *) (&mgm->members_count))[0] >> 6;

			for (i = 0; i < num_qps; i++) {
				qp.qpn = be32_to_cpu(mgm->qp[i]);
				if (rem_mcg)
					err = mlx4_multicast_detach(dev, &qp, mgm->gid, (mlx4_protocol)prot);
				else
					err = mlx4_multicast_attach(dev, &qp, mgm->gid, 0, (mlx4_protocol)prot, FALSE /* block loopback */);
				if (err)
					break;
			}
			break;
		default:
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV, ("Bad type for required operation\n"));
			err = EINVAL;
			break;
		}
		err = mlx4_cmd(dev, 0, ((u32) err | cpu_to_be32(token) << 16), 1,
				   MLX4_CMD_GET_OP_REQ, MLX4_CMD_TIME_CLASS_A);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("Failed to acknowledge required request: %d\n", err));
			goto out;
		}
		memset(outbox, 0, 0xffc);
		num_tasks = atomic_dec_return(&priv->opreq_count);
	}
	
out:
	mlx4_free_cmd_mailbox(dev, mailbox);
}
	
	
