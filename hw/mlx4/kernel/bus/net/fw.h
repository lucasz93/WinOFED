/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems.	All rights reserved.
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

#ifndef MLX4_FW_H
#define MLX4_FW_H

#include "mlx4.h"
#include "icm.h"

#pragma warning( disable : 4200)
struct mlx4_mod_stat_cfg {
	u8 log_pg_sz;
	u8 log_pg_sz_m;
};
#pragma warning( default  : 4200)

struct mlx4_adapter {
	char board_id[MLX4_BOARD_ID_LEN];
	u8	 inta_pin;
};

struct mlx4_init_hca_param {
	u64 qpc_base;
	u64 rdmarc_base;
	u64 auxc_base;
	u64 altc_base;
	u64 srqc_base;
	u64 cqc_base;
	u64 eqc_base;
	u64 mc_base;
	u64 dmpt_base;
	u64 cmpt_base;
	u64 mtt_base;
	u16 log_mc_entry_sz;
	u16 log_mc_hash_sz;
	u8	log_num_qps;
	u8	log_num_srqs;
	u8	log_num_cqs;
	u8	log_num_eqs;
	u8	log_rd_per_qp;
	u8	log_mc_table_sz;
	u8	log_mpt_sz;
	u8	log_uar_sz;
};

struct mlx4_init_ib_param {
	int port_width;
	int vl_cap;
	int mtu_cap;
	u16 gid_cap;
	u16 pkey_cap;
	int set_guid0;
	u64 guid0;
	int set_node_guid;
	u64 node_guid;
	int set_si_guid;
	u64 si_guid;
};

struct mlx4_set_ib_param {
	int set_si_guid;
	int reset_qkey_viol;
	u64 si_guid;
	u32 cap_mask;
};

int mlx4_QUERY_DEV_CAP(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap);
int mlx4_QUERY_SLAVE_CAP(struct mlx4_dev *dev, struct mlx4_caps *caps);
int mlx4_QUERY_SLAVE_CAP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_MAP_FA(struct mlx4_dev *dev, struct mlx4_icm *icm);
int mlx4_UNMAP_FA(struct mlx4_dev *dev);
int mlx4_RUN_FW(struct mlx4_dev *dev);
int mlx4_QUERY_FW(struct mlx4_dev *dev);
int mlx4_port_state(struct mlx4_dev *dev);
int mlx4_QUERY_ADAPTER(struct mlx4_dev *dev, struct mlx4_adapter *adapter);
int mlx4_INIT_HCA(struct mlx4_dev *dev, struct mlx4_init_hca_param *param);
int mlx4_CLOSE_HCA(struct mlx4_dev *dev, int panic);
int mlx4_map_cmd(struct mlx4_dev *dev, u16 op, struct mlx4_icm *icm, u64 virt);
int mlx4_SET_ICM_SIZE(struct mlx4_dev *dev, u64 icm_size, u64 *aux_pages);
int mlx4_MAP_ICM_AUX(struct mlx4_dev *dev, struct mlx4_icm *icm);
int mlx4_UNMAP_ICM_AUX(struct mlx4_dev *dev);
int mlx4_NOP(struct mlx4_dev *dev);
int mlx4_MOD_STAT_CFG(struct mlx4_dev *dev, struct mlx4_mod_stat_cfg *cfg);
int mlx4_QUERY_VEP_CFG(struct mlx4_dev *dev, u8 vep_num, u8 port, struct mlx4_vep_cfg *cfg);
int mlx4_QUERY_FUNC(struct mlx4_dev *dev, int func, u8 *pf_num);
int mlx4_update_uplink_arbiter(struct mlx4_dev *dev, int port);
int mlx4_set_vep_maps(struct mlx4_dev *dev);
IO_WORKITEM_ROUTINE mlx4_opreq_action; 

#endif /* MLX4_FW_H */
