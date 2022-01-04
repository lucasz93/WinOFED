/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
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
 *
 */

#include "mlx4.h"
#include "cmd.h"
#include "public.h"
#include "en_port.h"
#include "mlx4_debug.h"
#include "ib_pack.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "port.tmh"
#endif


void mlx4_init_mac_table(struct mlx4_dev *dev, struct mlx4_mac_table *table)
{
	int i;

	sema_init(&table->mac_sem, 1);
	for (i = 0; i < MLX4_MAX_MAC_NUM; i++)
		table->entries[i] = 0;
	table->max	 = 1 << dev->caps.log_num_macs;
	table->total = 0;
}


void mlx4_init_vlan_table(struct mlx4_dev *dev, struct mlx4_vlan_table *table)
{
	int i;

	sema_init(&table->vlan_sem, 1);
	for (i = 0; i < MLX4_MAX_VLAN_NUM; i++) {
		table->entries[i] = 0;
		table->refs[i] = 0;
	}
	table->max = 1 << dev->caps.log_num_vlans;
	if(table->max > MLX4_MAX_VLAN_NUM)
	{
		table->max = MLX4_MAX_VLAN_NUM;
	}
	table->total = 0;
}

static int mlx4_set_port_mac_table(struct mlx4_dev *dev, u8 port,
				   __be64 *entries)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 in_mod;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memcpy(mailbox->buf, entries, MLX4_MAC_TABLE_SIZE);

	in_mod = MLX4_SET_PORT_MAC_TABLE << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma.da, in_mod, 1, MLX4_CMD_SET_PORT,
			   MLX4_CMD_TIME_CLASS_B);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

static void mlx4_addrconf_ifid_eui48_win(u8 *eui, u64 mac)
{
	u8 *p = (u8*)&mac+2; //mac 6 bytes
	memcpy(eui, p, 3);
	memcpy(eui + 5, p + 3, 3);
	eui[3] = 0xFF;
	eui[4] = 0xFE;
	eui[0] ^= 2;
}


int mlx4_update_ipv6_gids_win(struct mlx4_dev *dev, int port, int clear, u64 mac)
{
	struct mlx4_cmd_mailbox *mailbox;
	union ib_gid *gids, *tmpgids;
	int err;

	tmpgids = (ib_gid*)kzalloc(128 * sizeof *gids, GFP_ATOMIC);
	if (!tmpgids)
		return -ENOMEM;

	if (!clear) {
		mlx4_addrconf_ifid_eui48_win(&tmpgids[0].raw[8], cpu_to_be64(mac));
		tmpgids[0].global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	}

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto out;
	}

	gids = (ib_gid*)mailbox->buf;
	memcpy(gids, tmpgids, 128 * sizeof *gids);

	err = mlx4_cmd(dev, mailbox->dma.da, MLX4_SET_PORT_GID_TABLE << 8 | port,
			   1, MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B);

	mlx4_free_cmd_mailbox(dev, mailbox);

out:
	kfree(tmpgids);
	return err;
}

static int mlx4_uc_steer_add(struct mlx4_dev *dev, u8 port,
				 u64 mac, u8 check_vlan, u8 vlan_valid, u16 vlan_id, int *qpn, bool reserve)
{
	struct mlx4_qp qp;
	u8 vep_num;
	u8 gid[16] = {0};
	int err;

	if (reserve) {
		err = mlx4_qp_reserve_range(dev, 1, 1, qpn);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to reserve qp for mac registration\n", dev->pdev->name));
			return err;
		}
	}
	qp.qpn = *qpn;

	vep_num = ((u8) (mac >> 48));
	mac &= 0xffffffffffffULL;
	mac = cpu_to_be64(mac << 16);
	memcpy(&gid[10], &mac, ETH_ALEN);
	gid[4] = vep_num;
	gid[5] = port;
	gid[7] = MLX4_UC_STEER << 1;

	if(check_vlan)
	{
		gid[7] |= MLX4_MGID_CHECK_VLAN;
	}
	
	if(check_vlan && vlan_valid)
	{
		vlan_id &= 0xfff;
		vlan_id = cpu_to_be16(vlan_id);
		gid[7] |= MLX4_MGID_VLAN_PRESENT;
		memcpy(&gid[8], &vlan_id, sizeof(vlan_id));
	}
	
	err = mlx4_qp_attach_common(dev, &qp, gid, 0,
					MLX4_PROT_ETH, MLX4_UC_STEER, vlan_valid /* only if vlan id valid, attach at the front */, 
					FALSE /* block loopback */);
	if (err && reserve)
		mlx4_qp_release_range(dev, *qpn, 1);

	return err;
}

static void mlx4_uc_steer_release(struct mlx4_dev *dev, u8 port,
				  u64 mac, u8 check_vlan, u8 vlan_valid, u16 vlan_id, int qpn, bool free)
{
	struct mlx4_qp qp;
	u8 vep_num;
	u8 gid[16] = {0};

	qp.qpn = qpn;
	vep_num = ((u8) (mac >> 48));
	mac &= 0xffffffffffffULL;
	mac = cpu_to_be64(mac << 16);
	memcpy(&gid[10], &mac, ETH_ALEN);
	gid[4] = vep_num;
	gid[5] = port;
	gid[7] = MLX4_MGID_UC;

	if(check_vlan)
	{
		gid[7] |= MLX4_MGID_CHECK_VLAN;
	}

	if(check_vlan && vlan_valid)
	{
		vlan_id &= 0xfff;
		vlan_id = cpu_to_be16(vlan_id);
		gid[7] |= MLX4_MGID_VLAN_PRESENT;
		memcpy(&gid[8], &vlan_id, sizeof(vlan_id));
	}

	mlx4_qp_detach_common(dev, &qp, gid, MLX4_PROT_ETH, MLX4_UC_STEER);
	if (free)
		mlx4_qp_release_range(dev, qpn, 1);
}

static int validate_index(struct mlx4_dev *dev,
			  struct mlx4_mac_table *table, int index)
{
	int err = 0;

	if (index < 0 || index >= table->max || !table->entries[index] || !table->refs[index]) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: No valid Mac entry for the given index\n", dev->pdev->name));
		err = -EINVAL;
	}
	return err;
}

static int mlx4_mac_table_add(struct mlx4_dev *dev, u8 port, u64 mac, int* index)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;
	int i, err = 0;
	int free = -1;

	down(&table->mac_sem);
	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (free < 0 && !table->entries[i]) {
			free = i;
			continue;
		}

		if (mac == (MLX4_MAC_MASK & be64_to_cpu(table->entries[i]))) {
			/* MAC + PF already registered, increase refcount */
			/* This is needed for VMQ since multiple VMQs can be set with the same MAC, and we
						  want to register/unregister them independently */
			++table->refs[i];
			if(index)
				*index = i;
			goto out;
		}
	}

	if (table->total == table->max) {
		/* No free mac entries */
		err = -ENOSPC;
		goto out;
	}

	/* Register new MAC */
	table->refs[free] = 1;
	table->entries[free] = cpu_to_be64(mac | MLX4_MAC_VALID);

	err = mlx4_set_port_mac_table(dev, port, table->entries);
	if (unlikely(err)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Failed adding MAC: 0x%I64x\n", mac));
		table->refs[free] = 0;
		table->entries[free] = 0;
		goto out;
	}

	++table->total;
	if(index)
		*index = free;
	
out:
	up(&table->mac_sem);
	return err;
}

static void mlx4_mac_table_remove(struct mlx4_dev *dev, u8 port, int index)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;

	down(&table->mac_sem);

	if (validate_index(dev, table, index))
		goto out;

	--table->refs[index];

	if (table->refs[index]) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Have more references for index %d," 
			"no need to modify mac table\n", index));
		goto out;
	}

	table->entries[index] = 0;
	if ( !mlx4_is_barred(dev) )
		mlx4_set_port_mac_table(dev, port, table->entries);
	--table->total;

out:
	up(&table->mac_sem);
}

#if 0	
int mlx4_register_mac(struct mlx4_dev *dev, u8 port, u64 mac, int *qpn, bool wrap, bool reserve)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;
	struct mlx4_mac_entry *entry = NULL;
	int i, err = 0;
	int free = -1;

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	if (mlx4_is_slave(dev)) {
		struct slave_register_mac slave_in;
		slave_in.qpn = qpn;
		slave_in.mac = mac;
		slave_in.reserve = reserve;
		err = mlx4_cmd(dev, (ULONG_PTR)&slave_in, RES_MAC,	port,
				   MLX4_CMD_ALLOC_RES, MLX4_CMD_TIME_CLASS_A);
		return err;
	} 

	if (!wrap)
		mac |= (u64) (dev->caps.vep_num) << 48;

	if (dev->caps.vep_uc_steering) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x use UC steering\n", dev->pdev->name, mac));
		err = mlx4_uc_steer_add(dev, port, mac, FALSE, 0, qpn, reserve);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x failed. err=0x%x\n", dev->pdev->name, mac, err));
			return err;
		}
		entry = (mlx4_mac_entry *)kmalloc(sizeof *entry, GFP_KERNEL);
		if (!entry) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x failed. No memomery\n", dev->pdev->name, mac));
			mlx4_uc_steer_release(dev, port, mac, FALSE, 0, *qpn, reserve);
			return -ENOMEM;
		}
		entry->mac = mac;
		err = radix_tree_insert(&info->mac_tree, *qpn, entry);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x failed. err=0x%x\n", dev->pdev->name, mac, err));
			mlx4_uc_steer_release(dev, port, mac, FALSE, 0, *qpn, reserve);
			kfree(entry);
			return err;
		}
	}

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x Completed. qpn=0x%x. err=0x%x\n", 
		dev->pdev->name, mac, *qpn, err));
	
	down(&table->mac_sem);
	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (free < 0 && !table->entries[i]) {
			free = i;
			continue;
		}

		if (mac == (MLX4_MAC_MASK & be64_to_cpu(table->entries[i]))) {
			/* MAC + PF already registered, increase refcount */
			/* This is needed for VMQ since multiple VMQs can be set with the same MAC, and we
						  want to register/unregister them independently */
			++table->refs[i];
			if (!dev->caps.vep_uc_steering)
				*qpn = info->base_qpn + i;
			goto out;
		}
	}

	if (table->total == table->max) {
		/* No free mac entries */
		err = -ENOSPC;
		goto out;
	}

	/* Register new MAC */
	table->refs[free] = 1;
	table->entries[free] = cpu_to_be64(mac | MLX4_MAC_VALID);

	err = mlx4_set_port_mac_table(dev, port, table->entries);
	if (unlikely(err)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Failed adding MAC: 0x%I64x\n", mac));
		table->refs[free] = 0;
		table->entries[free] = 0;
		goto out;
	}

	if (!dev->caps.vep_uc_steering)
		*qpn = info->base_qpn + free;
	++table->total;
	
	//update port guid with mac address
out:
	if (err && dev->caps.vep_uc_steering) {
		mlx4_uc_steer_release(dev, port, mac, FALSE, 0, *qpn, reserve);
		radix_tree_delete(&info->mac_tree, *qpn);
		kfree(entry);
	}
	up(&table->mac_sem);
	return err;
}
#endif

static int find_index(struct mlx4_mac_table *table, u64 mac)
{
	int i;	

	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (mac == (MLX4_MAC_MASK & be64_to_cpu(table->entries[i])))\
			return i;
	}

	/* Mac not found */
	return -EINVAL;
}

#if 0
void mlx4_unregister_mac(struct mlx4_dev *dev, u8 port, int qpn, bool free)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;
	int index = qpn - info->base_qpn;
	struct mlx4_mac_entry *entry;	 

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( ">>>> %s: Unregistering MAC. qpn=0x%x\n", dev->pdev->name, qpn));

	if (mlx4_is_slave(dev)) {
		struct slave_register_mac slave_in;
		slave_in.qpn = &qpn;
		slave_in.reserve = !free;
		mlx4_cmd(dev, (ULONG_PTR)&slave_in, RES_MAC,  port,
			MLX4_CMD_FREE_RES, MLX4_CMD_TIME_CLASS_A);
		return; 
	}

	if (dev->caps.vep_uc_steering) {
		entry = (mlx4_mac_entry *)radix_tree_lookup(&info->mac_tree, qpn);
		if (entry) {
			index = find_index(table, entry->mac);
			mlx4_uc_steer_release(dev, port, entry->mac, FALSE, 0, qpn, free);
			radix_tree_delete(&info->mac_tree, qpn);
			kfree(entry);
		}
	}

	down(&table->mac_sem);

	if (validate_index(dev, table, index))
		goto out;

	--table->refs[index];

	if (table->refs[index]) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Have more references for index %d," 
			"no need to modify mac table\n", index));
		goto out;
	}

	table->entries[index] = 0;
	if ( !mlx4_is_barred(dev) )
		mlx4_set_port_mac_table(dev, port, table->entries);
	--table->total;

out:
	up(&table->mac_sem);
}
#endif

int mlx4_replace_mac(struct mlx4_dev *dev, u8 port, int qpn, u64 new_mac)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;
	int index = qpn - info->base_qpn;
	struct mlx4_mac_entry *entry;
	int err;

	if (mlx4_is_slave(dev)) {
		err = mlx4_cmd_imm(dev, new_mac, (u64 *) &qpn, RES_MAC_AND_VLAN, port,
				   MLX4_CMD_REPLACE_RES, MLX4_CMD_TIME_CLASS_A);
		return err;
	}

	if (dev->caps.vep_uc_steering) {
		entry = (mlx4_mac_entry *)radix_tree_lookup(&info->mac_tree, qpn);
		if (!entry)
			return -EINVAL;
		mlx4_uc_steer_release(dev, port, entry->mac, entry->check_vlan, entry->vlan_valid, entry->vlan, qpn, false);
		index = find_index(table, entry->mac);
		entry->mac = new_mac;
		err = mlx4_uc_steer_add(dev, port, entry->mac, entry->check_vlan, entry->vlan_valid, entry->vlan, &qpn, false);
		if (err)
			return err;
	}

	down(&table->mac_sem);

	err = validate_index(dev, table, index);
	if (err)
		goto out;

	table->entries[index] = cpu_to_be64(new_mac | MLX4_MAC_VALID);

	if ( !mlx4_is_barred(dev) ) {
		err = mlx4_set_port_mac_table(dev, port, table->entries);
		if (unlikely(err)) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed adding MAC: 0x%I64x\n", 
				dev->pdev->name, new_mac));
			table->entries[index] = 0;
		}
	}
out:
	up(&table->mac_sem);
	return err;
}


static int mlx4_set_port_vlan_table(struct mlx4_dev *dev, u8 port,
					__be32 *entries)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 in_mod;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memcpy(mailbox->buf, entries, MLX4_VLAN_TABLE_SIZE);
	in_mod = MLX4_SET_PORT_VLAN_TABLE << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma.da, in_mod, 1, MLX4_CMD_SET_PORT,
			   MLX4_CMD_TIME_CLASS_B);

	mlx4_free_cmd_mailbox(dev, mailbox);

	return err;
}

static int mlx4_vlan_table_add(struct mlx4_dev *dev, u8 port, u16 vlan, int *index)
{
	struct mlx4_vlan_table *table = &mlx4_priv(dev)->port[port].vlan_table;
	int i, err = 0;
	int free = -1;

	down(&table->vlan_sem);
	for (i = MLX4_VLAN_REGULAR; i < MLX4_MAX_VLAN_NUM; i++) {
		if (free < 0 && (table->refs[i] == 0)) {
			free = i;
			continue;
		}

		if (table->refs[i] &&
			(vlan == (MLX4_VLAN_MASK &
				  be32_to_cpu(table->entries[i])))) {
			/* Vlan already registered, increase refernce count */
			*index = i;
			++table->refs[i];
			goto out;
		}
	}

	if (table->total == table->max || free < 0) {
		/* No free vlan entries */
		err = -ENOSPC;
		goto out;
	}

	/* Register new MAC */
	table->refs[free] = 1;
	table->entries[free] = cpu_to_be32(vlan | MLX4_VLAN_VALID);

	err = mlx4_set_port_vlan_table(dev, port, table->entries);
	if (unlikely(err)) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed adding vlan: %u\n", dev->pdev->name, vlan));
		table->refs[free] = 0;
		table->entries[free] = 0;
		goto out;
	}

	*index = free;
	++table->total;
out:
	up(&table->vlan_sem);
	return err;
}

static void mlx4_vlan_table_remove(struct mlx4_dev *dev, u8 port, int index)
{
	struct mlx4_vlan_table *table = &mlx4_priv(dev)->port[port].vlan_table;

	if (index < MLX4_VLAN_REGULAR) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Trying to free special vlan index %d\n", dev->pdev->name, index));
		return;
	}

	down(&table->vlan_sem);
	if (!table->refs[index]) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: No vlan entry for index %d\n", dev->pdev->name, index));
		goto out;
	}
	if (--table->refs[index]) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Have more references for index %d,"
			 "no need to modify vlan table\n", dev->pdev->name, index));
		goto out;
	}
	table->entries[index] = 0;
	mlx4_set_port_vlan_table(dev, port, table->entries);
	--table->total;
out:
	up(&table->vlan_sem);
}


int mlx4_register_mac_and_vlan(struct mlx4_dev *dev, 
									 u8 port, 
									 u64 mac, 
									 u8 check_vlan, 									 
									 u8 vlan_valid,
									 u16 vlan, 
									 int *qpn, 
									 bool wrap, 
									 bool reserve)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	int err = 0, mac_table_index = -1, vlan_table_index = -1;
	struct mlx4_mac_entry *entry = NULL;

	if ( mlx4_is_barred(dev) ) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC 0x%I64x Failed. mlx4 is barred\n", dev->pdev->name, mac));
		return -EFAULT;
	}

	// vlan filtering is supported only with new steering model
	if (vlan_valid && (! dev->caps.vep_uc_steering || ! dev->caps.steering_by_vlan)){
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC 0x%I64x with vlan id 0x%x Failed. FW doesn't support new steering model\n",
			dev->pdev->name, mac, vlan));
		return -EFAULT;
	}
	
	if (mlx4_is_slave(dev)) {
		struct slave_register_mac_and_vlan slave_in;
		slave_in.qpn = qpn;
		slave_in.mac = mac;
		slave_in.check_vlan = check_vlan;
		slave_in.vlan_valid = vlan_valid;
		slave_in.vlan = vlan;
		slave_in.reserve = reserve;
		err = mlx4_cmd(dev, (ULONG_PTR)&slave_in, RES_MAC_AND_VLAN,	port,
				   MLX4_CMD_ALLOC_RES, MLX4_CMD_TIME_CLASS_A);
		return err;
	} 

	if (!wrap)
		mac |= (u64) (dev->caps.vep_num) << 48;

	if (dev->caps.vep_uc_steering) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x use UC steering\n", dev->pdev->name, mac));
		err = mlx4_uc_steer_add(dev, port, mac, check_vlan, vlan_valid, vlan, qpn, reserve);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x failed. err=0x%x\n", dev->pdev->name, mac, err));
			return err;
		}
		entry = (struct mlx4_mac_entry *) kmalloc(sizeof *entry, GFP_KERNEL);
		if (!entry) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x failed. No memomery\n", dev->pdev->name, mac));
			mlx4_uc_steer_release(dev, port, mac, check_vlan, vlan_valid, vlan, *qpn, reserve);
			return -ENOMEM;
		}
		entry->mac = mac;
		entry->check_vlan = check_vlan;
		entry->vlan_valid = vlan_valid;
		entry->vlan = vlan;
		err = radix_tree_insert(&info->mac_tree, *qpn, entry);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x failed. err=0x%x\n", dev->pdev->name, mac, err));
			mlx4_uc_steer_release(dev, port, mac, check_vlan, vlan_valid, vlan, *qpn, reserve);
			kfree(entry);
			return err;
		}
	}
	
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Registering MAC: 0x%I64x Completed. qpn=0x%x. err=0x%x\n", 
		dev->pdev->name, mac, *qpn, err));
	
	err = mlx4_mac_table_add(dev, port, mac, &mac_table_index);
	if(err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC 0x%I64x Failed. Add mac to table failed. Error=0x%x\n",
			dev->pdev->name, mac, err));
		goto out;
	}
	
	if (!dev->caps.vep_uc_steering)
		*qpn = info->base_qpn + mac_table_index;

	if(vlan_valid)
	{
		err = mlx4_vlan_table_add(dev, port, vlan, &vlan_table_index);
		if(err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Registering MAC 0x%I64x with vlan id 0x%x Failed. Add VLAN to table failed. Error=0x%xl\n",
				dev->pdev->name, mac, vlan, err));
			mlx4_mac_table_remove(dev, port, mac_table_index);
			goto out;
		}
	}	
	
out:
	if (err && dev->caps.vep_uc_steering) {
		mlx4_uc_steer_release(dev, port, mac, check_vlan, vlan_valid, vlan, *qpn, reserve);
		radix_tree_delete(&info->mac_tree, *qpn);
		kfree(entry);
	}
	return err;
}

static int find_vlan_index(struct mlx4_vlan_table *table, u16 vlan)
{
	int i;	

	for (i = MLX4_VLAN_REGULAR; i < MLX4_MAX_VLAN_NUM; i++) {
		if (table->refs[i] && 
			vlan == (MLX4_VLAN_MASK & be32_to_cpu(table->entries[i])))
			return i;
	}

	/* vlan not found */
	return -EINVAL;
}

void mlx4_unregister_mac_and_vlan(struct mlx4_dev *dev, u8 port, int qpn, bool free)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;
	u8 vlan_valid = 0;
	int mac_table_index = qpn - info->base_qpn, vlan_table_index = 0;
	struct mlx4_mac_entry *entry;	 

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( ">>>> %s: Unregistering MAC. qpn=0x%x\n", dev->pdev->name, qpn));

	if (mlx4_is_slave(dev)) {
		struct slave_register_mac_and_vlan slave_in;
		slave_in.qpn = &qpn;
		slave_in.reserve = !free;
		mlx4_cmd(dev, (ULONG_PTR)&slave_in, RES_MAC_AND_VLAN,  port,
			MLX4_CMD_FREE_RES, MLX4_CMD_TIME_CLASS_A);
		return; 
	}

	if (dev->caps.vep_uc_steering) {
		entry = (struct mlx4_mac_entry *) radix_tree_lookup(&info->mac_tree, qpn);
		if (entry) {
			mac_table_index = find_index(table, entry->mac);
			vlan_valid = entry->vlan_valid;
			if(vlan_valid)
			{
				struct mlx4_vlan_table *vlan_table = &info->vlan_table;
				vlan_table_index = find_vlan_index(vlan_table, entry->vlan);
			}
			mlx4_uc_steer_release(dev, port, entry->mac, entry->check_vlan, entry->vlan_valid, entry->vlan, qpn, free);
			radix_tree_delete(&info->mac_tree, qpn);
			kfree(entry);
		}
	}

	if(mac_table_index >= 0)
		mlx4_mac_table_remove(dev, port, mac_table_index);

	if(vlan_valid && vlan_table_index >= 0)
		mlx4_vlan_table_remove(dev, port, vlan_table_index);	
}

int mlx4_find_vlan_index(struct mlx4_dev *dev, u8 port, u16 vlan, int* vidx)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_vlan_table *vlan_table = &info->vlan_table;
	int vlan_table_index = 0;

	ASSERT(vidx);
	
	vlan_table_index = find_vlan_index(vlan_table, vlan);

	if(vlan_table_index < 0)
	{// returned error
		return vlan_table_index;
	}

	*vidx = vlan_table_index;
	
	return 0;
}

int mlx4_get_port_ib_caps(struct mlx4_dev *dev, u8 port, __be32 *caps)
{
	struct mlx4_cmd_mailbox *inmailbox, *outmailbox;
	u8 *inbuf, *outbuf;
	int err;

	inmailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(inmailbox))
		return PTR_ERR(inmailbox);

	outmailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(outmailbox)) {
		mlx4_free_cmd_mailbox(dev, inmailbox);
		return PTR_ERR(outmailbox);
	}

	inbuf = (u8*)inmailbox->buf;
	outbuf = (u8*)outmailbox->buf;
	memset(inbuf, 0, 256);
	memset(outbuf, 0, 256);
	inbuf[0] = 1;
	inbuf[1] = 1;
	inbuf[2] = 1;
	inbuf[3] = 1;
	*(__be16 *) (&inbuf[16]) = cpu_to_be16(0x0015);
	*(__be32 *) (&inbuf[20]) = cpu_to_be32(port);

	err = mlx4_cmd_box(dev, inmailbox->dma.da, outmailbox->dma.da, port, 3,
			   MLX4_CMD_MAD_IFC, MLX4_CMD_TIME_CLASS_C);
	if (!err)
		*caps = *(__be32 *) (outbuf + 84);
	mlx4_free_cmd_mailbox(dev, inmailbox);
	mlx4_free_cmd_mailbox(dev, outmailbox);
	return err;
}

int mlx4_get_ext_port_caps(struct mlx4_dev *dev, u8 port, u64 *get_ext_caps)
{
	struct mlx4_cmd_mailbox *inmailbox, *outmailbox;
	u8 *inbuf, *outbuf;
	int err;

	inmailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(inmailbox))
		return PTR_ERR(inmailbox);

	outmailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(outmailbox)) {
		mlx4_free_cmd_mailbox(dev, inmailbox);
		return PTR_ERR(outmailbox);
	}

	inbuf = (u8*)inmailbox->buf;
	outbuf = (u8*)outmailbox->buf;
	memset(inbuf, 0, 256);
	memset(outbuf, 0, 256);
	inbuf[0] = 1;
	inbuf[1] = 1;
	inbuf[2] = 1;
	inbuf[3] = 1;
	*(__be16 *) (&inbuf[16]) = cpu_to_be16(0xff90);
	*(__be32 *) (&inbuf[20]) = cpu_to_be32(port);

	err = mlx4_cmd_box(dev, inmailbox->dma.da, outmailbox->dma.da, port, 3,
			   MLX4_CMD_MAD_IFC, MLX4_CMD_TIME_CLASS_C);

	if ((!err) && (!be16_to_cpu(*(__be16 *) (outbuf + 4))))
		*get_ext_caps |= MLX_EXT_PORT_CAP_FLAG_EXTENDED_PORT_INFO;

	mlx4_free_cmd_mailbox(dev, inmailbox);
	mlx4_free_cmd_mailbox(dev, outmailbox);
	return err;
}


static int mlx4_common_set_port(struct mlx4_dev *dev, int slave, u32 in_mod,
	u8 op_mod, struct mlx4_cmd_mailbox *inbox)	
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_port_info *port_info;
	struct mlx4_mfunc_master_ctx *master = &priv->mfunc.master;
	struct mlx4_slave_state *slave_st = &master->slave_state[slave];
	struct mlx4_set_port_rqp_calc_context *qpn_context;	
	struct mlx4_set_port_general_context *gen_context;
	int reset_qkey_viols;
	int port;
	int is_eth;
	u32 in_modifier;
	u32 promisc;
	u16 mtu, prev_mtu;
	int err;
	unsigned long i;
	__be32 agg_cap_mask;
	__be32 slave_cap_mask;
	__be32 new_cap_mask;

	port = in_mod & 0xff;
	in_modifier = in_mod >> 8;
	is_eth = op_mod;

	port_info = &priv->port[port];
	/* All slaves can perform SET_PORT operations, just need to verify
	 * we keep the mutual resources unchanged */
	 if (is_eth) {
		switch (in_modifier) {
			case MLX4_SET_PORT_RQP_CALC:
				qpn_context = (mlx4_set_port_rqp_calc_context *)inbox->buf;
				qpn_context->base_qpn = cpu_to_be32(port_info->base_qpn);
				qpn_context->n_mac = 0x7;
				promisc = be32_to_cpu(qpn_context->promisc) >>
					SET_PORT_PROMISC_SHIFT;
				qpn_context->promisc = cpu_to_be32(
					promisc << SET_PORT_PROMISC_SHIFT |
					port_info->base_qpn);
				promisc = be32_to_cpu(qpn_context->mcast) >>
					SET_PORT_MC_PROMISC_SHIFT;
				qpn_context->mcast = cpu_to_be32(
					promisc << SET_PORT_MC_PROMISC_SHIFT |
					port_info->base_qpn);
				break;

			case MLX4_SET_PORT_GENERAL:
				gen_context = (mlx4_set_port_general_context *)inbox->buf; 
				/* Mtu is configured as the max MTU among all the
				 * the functions on the port. */
				 mtu = be16_to_cpu(gen_context->mtu);
				 mtu = (u16)min_t(int, mtu, dev->caps.eth_mtu_cap[port]);
				 prev_mtu = slave_st->mtu[port];
				 slave_st->mtu[port] = mtu;
				 if (mtu > master->max_mtu[port])
					master->max_mtu[port] = mtu; 
				 if (mtu < prev_mtu && prev_mtu == master->max_mtu[port]) {
					slave_st->mtu[port] = mtu;
					master->max_mtu[port] = mtu;
					for (i = 0; i < dev->num_slaves; i++) {
						master->max_mtu[port] =
							max(master->max_mtu[port],
							master->slave_state[i].mtu[port]);
					}
				 }

				 gen_context->mtu = cpu_to_be16(master->max_mtu[port]);
				 break;

			 case MLX4_SET_PORT_MAC_TABLE:
			 case MLX4_SET_PORT_VLAN_TABLE:
			 case MLX4_SET_PORT_PRIO_MAP:
			 case MLX4_SET_PORT_GID_TABLE:
				break;
			 default:
				ASSERT(FALSE);
		}
		return mlx4_cmd(dev, inbox->dma.da, in_mod, op_mod,
			MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B);
	}

	/* For IB, we only consider:
	 * - The capability mask, which is set to the aggregate of all slave frunction
	 *	 capabilities
	 * - The QKey violatin counter - reset according to each request.
	 */

	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
		reset_qkey_viols = (*(u8 *) inbox->buf) & 0x40;
		new_cap_mask = ((__be32 *) inbox->buf)[2];
	} else {
		reset_qkey_viols = ((u8 *) inbox->buf)[3] & 0x1;
		new_cap_mask = ((__be32 *) inbox->buf)[1];
	}

	/* TODO: allow only a single function (must be a PF) to enable SM capability
	 * (IB_PORT_SM) on a given port at a time; demux SMP traffic accordingly.
	 * TODO: spoof MAD_IFC calls to return per-slave caps. */
	agg_cap_mask = 0;
	slave_cap_mask = priv->mfunc.master.slave_state[slave].ib_cap_mask[port];
	priv->mfunc.master.slave_state[slave].ib_cap_mask[port] = new_cap_mask;
	for (i = 0; i < (int)dev->num_slaves; i++)
		agg_cap_mask |= priv->mfunc.master.slave_state[slave].ib_cap_mask[port];

#if 0
	mlx4_warn(dev, "%s: old_slave_cap:0x%x slave_cap:0x%x cap:0x%x qkey_reset:%d\n",
			dev->pdev->name, 
			slave_cap_mask, priv->mfunc.master.slave_state[slave].ib_cap_mask[port],
			agg_cap_mask, reset_qkey_viols);
#endif

	memset(inbox->buf, 0, 256);
	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
		*(u8 *) inbox->buf	   = (u8)(!!reset_qkey_viols << 6);
		((__be32 *) inbox->buf)[2] = agg_cap_mask;
	} else {
		((u8 *) inbox->buf)[3]	   = (u8)(!!reset_qkey_viols);
		((__be32 *) inbox->buf)[1] = agg_cap_mask;
	}

	err = mlx4_cmd(dev, inbox->dma.da, port, (u8)is_eth, MLX4_CMD_SET_PORT,
			   MLX4_CMD_TIME_CLASS_B);
	if (err)
		priv->mfunc.master.slave_state[slave].ib_cap_mask[port] = slave_cap_mask;
	return err;
}

int mlx4_SET_PORT_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox)
{
	UNUSED_PARAM(outbox);
	return mlx4_common_set_port(dev, slave, vhcr->in_modifier,
					vhcr->op_modifier, inbox);
}

int mlx4_SET_PORT(struct mlx4_dev *dev, u8 port)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;
    PWSTR keyName = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\MiniNT";
    NTSTATUS status = STATUS_SUCCESS;

	if (dev->caps.port_type_final[port] == MLX4_PORT_TYPE_ETH)
		return 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memset(mailbox->buf, 0, 256);

	/* Set mmc bit no. 22 to allow MAX_MTU_SIZE change */
	/* Set mvc bit no. 21 to allow vl_cap change /*
	/* Always set MAX_MTU_SIZE == 4K for MLX4 - bits 12:15 */
	/* Change vl_cap to be maximum 4 - bits 5:7 */
	if (g.set_4k_mtu)
		((__be32 *) mailbox->buf)[0] |= cpu_to_be32((1 << 22) | (1 << 21) | (5 << 12) | (2 << 4));

	((__be32 *) mailbox->buf)[1] = dev->caps.ib_port_def_cap[port];

     //Query the registry to check if we are in WINPE mode
    status = RtlCheckRegistryKey(RTL_REGISTRY_ABSOLUTE,keyName);
    if(NT_SUCCESS(status))
    {
        ((__be32 *) mailbox->buf)[0] |= cpu_to_be32((1 << 28));
        ((__be32 *) mailbox->buf)[28] |= cpu_to_be32((1 << 4));
    }  

	if (mlx4_is_master(dev))
		err = mlx4_common_set_port(dev, dev->caps.function, port, 0, mailbox);
	else
		err = mlx4_cmd(dev, mailbox->dma.da, port, 0, MLX4_CMD_SET_PORT,
			   MLX4_CMD_TIME_CLASS_B);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}


int mlx4_SET_PORT_general(struct mlx4_dev *dev, u8 port, int mtu, int roce_mtu,
			  u8 pptx, u8 pfctx, u8 pprx, u8 pfcrx)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_port_general_context *context;
	int err;
	u32 in_mod;

	if(mlx4_is_roce_port(dev, port))
	{
		// save RoCE port mtu
		dev->dev_params.roce_mtu[port] = roce_get_mtu(roce_mtu);
	}
	
	//
	// pfctx and pptx must be mutual exclusive
	//
	ASSERT((pptx == 0) || (pfctx == 0));
	ASSERT((pprx == 0) || (pfcrx == 0));

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	context = (mlx4_set_port_general_context *)mailbox->buf;
	memset(context, 0, sizeof *context);

	//
	// Set that pprx, pptx and MTU values are valid
	//
	context->flags = SET_PORT_GEN_ALL_VALID;
	
	//
	// Set MTU of the ports in bytes
	//
	context->mtu = cpu_to_be16(mtu);

	//
	// Set TX pause policy. If pptx is 1, generate pause frame otherwise generates 
	// pause frame per priority
	//
	context->pptx = (pptx == 0) ? 0 : (1 << 7);
	context->pfctx = pfctx;

	//
	// Set RX pause policy. If pprx is 1, generate pause frame otherwise generates 
	// pause frame per priority
	//
	context->pprx = (pprx == 0) ? 0 : (1 << 7);
	context->pfcrx = pfcrx;

	in_mod = MLX4_SET_PORT_GENERAL << 8 | port;
	if (mlx4_is_master(dev))
		err = mlx4_common_set_port(dev, dev->caps.function, in_mod, 1, mailbox);
	else
		err = mlx4_cmd(dev, mailbox->dma.da, in_mod, 1, MLX4_CMD_SET_PORT,
				   MLX4_CMD_TIME_CLASS_B);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_general);


int mlx4_SET_PORT_qpn_calc(struct mlx4_dev *dev, u8 port, u32 base_qpn,
			   u8 promisc, u8 fVmq)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_port_rqp_calc_context *context;
	int err;
	u32 in_mod;
	u32 m_promisc = (dev->caps.vep_mc_steering) ? MCAST_DIRECT : MCAST_DEFAULT;

	if (dev->caps.vep_mc_steering && dev->caps.vep_uc_steering) {
		return 0;
	}

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	context = (mlx4_set_port_rqp_calc_context *)mailbox->buf;
	memset(context, 0, sizeof *context);

	context->base_qpn = cpu_to_be32(base_qpn);
	context->n_mac = 0x7;
	context->promisc = cpu_to_be32(promisc << SET_PORT_PROMISC_SHIFT |
					   base_qpn);

	context->mcast = cpu_to_be32(m_promisc << SET_PORT_MC_PROMISC_SHIFT | base_qpn);

	context->intra_no_vlan = 0;
	context->no_vlan = MLX4_NO_VLAN_IDX;
	context->intra_vlan_miss = 0;
	context->vlan_miss = MLX4_VLAN_MISS_IDX;

	if(fVmq)
	{
		context->n_mac = 7; 	 // use 7 bits of MAC index in QPn calculation
		context->n_vlan = 7;	 // use 7 bits of VLAN index in QPn calculation
		context->mac_miss = 128; // 128 is promiscuous QP

		// in entry 0 we will have VLAN tag = 0, which is handled same as untagged by VMQ filter definition.
		context->no_vlan = 126; // 126 is promiscuous 
		context->vlan_miss = 127; // 127 is promiscuous
	}

	in_mod = MLX4_SET_PORT_RQP_CALC << 8 | port;
	if (mlx4_is_master(dev))
		err = mlx4_common_set_port(dev, dev->caps.function, in_mod, 1, mailbox);
	else
		err = mlx4_cmd(dev, mailbox->dma.da, in_mod, 1, MLX4_CMD_SET_PORT,
				   MLX4_CMD_TIME_CLASS_B);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_qpn_calc);

int mlx4_SET_PORT_PRIO2TC(struct mlx4_dev *dev, u8 port, prio2tc_context* inbox)
{
    struct mlx4_cmd_mailbox *mailbox = NULL;
    int err = 0;
    u32 in_mod = 0;
 
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
 
 	memcpy(mailbox->buf, inbox, sizeof(struct prio2tc_context));
    in_mod = 0x8 << 8 | port;
    
    err = mlx4_cmd(dev, mailbox->dma.da, in_mod, 1, MLX4_CMD_PORT_SCHEDULER,MLX4_CMD_TIME_CLASS_B);
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;       
}

int mlx4_SET_PORT_SCHEDULER(struct mlx4_dev *dev, u8 port, set_port_scheduler_context* inbox)
{
    struct mlx4_cmd_mailbox *mailbox = NULL;
    int err = 0;
    u32 in_mod = 0;
 
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
 
 	memcpy(mailbox->buf, inbox, sizeof(struct set_port_scheduler_context));
    in_mod = 0x9 << 8 | port;
    
    err = mlx4_cmd(dev, mailbox->dma.da, in_mod, 1, MLX4_CMD_PORT_SCHEDULER,MLX4_CMD_TIME_CLASS_B);
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;           
}



static int mlx4_common_set_mcast_fltr(struct mlx4_dev *dev, int function,
					  int port, u64 addr, u64 clear, u8 mode)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err = 0;
	struct mlx4_mcast_entry *entry, *tmp;
	struct mlx4_slave_state *s_state = &priv->mfunc.master.slave_state[function];
	unsigned long i;

	switch (mode) {
	case MLX4_MCAST_DISABLE:
		/* The multicast filter is disabled only once,
		 * If some other function already done it, operation
		 * is ignored */
		if (!(priv->mfunc.master.disable_mcast_ref[port]++))
			err = mlx4_cmd(dev, 0, port, MLX4_MCAST_DISABLE,
					MLX4_CMD_SET_MCAST_FLTR,
					MLX4_CMD_TIME_CLASS_B);
		break;
	case MLX4_MCAST_ENABLE:
		/* We enable the muticast filter only if all functions
		 * have the filter enabled */
		if (!(--priv->mfunc.master.disable_mcast_ref[port]))
			err = mlx4_cmd(dev, 0, port, MLX4_MCAST_ENABLE,
					MLX4_CMD_SET_MCAST_FLTR,
					MLX4_CMD_TIME_CLASS_B);
		break;
	case MLX4_MCAST_CONFIG:
		if (clear) {
			/* Disable the muticast filter while updating it */
			if (!priv->mfunc.master.disable_mcast_ref[port]) {
				err = mlx4_cmd(dev, 0, port, MLX4_MCAST_DISABLE,
						MLX4_CMD_SET_MCAST_FLTR,
						MLX4_CMD_TIME_CLASS_B);
				if (err) {
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to disable multicast "
							   "filter\n", dev->pdev->name));
					goto out;
				}
			}
			/* Clear the multicast filter */
			err = mlx4_cmd(dev, clear << 63, port,
					   MLX4_MCAST_CONFIG,
					   MLX4_CMD_SET_MCAST_FLTR,
					   MLX4_CMD_TIME_CLASS_B);
			if (err) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed clearing the multicast filter\n", dev->pdev->name));
				goto out;
			}

			/* Clear the multicast addresses for the given slave */
			list_for_each_entry_safe(entry, tmp,
				&s_state->mcast_filters[port], list,
				struct mlx4_mcast_entry, struct mlx4_mcast_entry) {

				list_del(&entry->list);
				kfree(entry);
			}

			/* Assign all the multicast addresses that still exist */
			for (i = 0; i < dev->num_slaves; i++) {
				list_for_each_entry(entry,
					&priv->mfunc.master.slave_state[function].mcast_filters[port],
					list, struct mlx4_mcast_entry) {
					if (mlx4_cmd(dev, entry->addr, port,
							 MLX4_MCAST_CONFIG,
							 MLX4_CMD_SET_MCAST_FLTR,
							 MLX4_CMD_TIME_CLASS_B))
						MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to reconfigure "
							  "multicast address: 0x%I64x\n",
							  dev->pdev->name, entry->addr));
				}
			}
			/* Enable the filter */
			if (!priv->mfunc.master.disable_mcast_ref[port]) {
				err = mlx4_cmd(dev, 0, port, MLX4_MCAST_ENABLE,
						MLX4_CMD_SET_MCAST_FLTR,
						MLX4_CMD_TIME_CLASS_B);
				if (err) {
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to enable multicast "
							   "filter\n", dev->pdev->name));
					goto out;
				}
			}
		}
		/* Add the new address if exists */
		if (addr) {
			entry = (mlx4_mcast_entry *)kzalloc(sizeof (struct mlx4_mcast_entry),
					GFP_KERNEL);
			if (!entry) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to allocate entry for "
						   "muticast address\n", dev->pdev->name));
				err = -ENOMEM;
				goto out;
			}
			INIT_LIST_HEAD(&entry->list);
			entry->addr = addr;
			list_add_tail(&entry->list, &s_state->mcast_filters[port]);
			err = mlx4_cmd(dev, addr, port, MLX4_MCAST_CONFIG,
					   MLX4_CMD_SET_MCAST_FLTR,
					   MLX4_CMD_TIME_CLASS_B);
			if (err)
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to add the new address:"
						   "0x%I64x\n", dev->pdev->name, addr));
		}
		break;
	default:
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: SET_MCAST_FILTER called with illegal modifier\n", dev->pdev->name));
		err = -EINVAL;
	}
out:
	return err;
}

int mlx4_SET_MCAST_FLTR_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox)
{
	int port = vhcr->in_modifier;
	u64 addr = vhcr->in_param & 0xffffffffffffULL;
	u64 clear = vhcr->in_param >> 63;
	u8 mode = vhcr->op_modifier;

	UNREFERENCED_PARAMETER(outbox);
	UNREFERENCED_PARAMETER(inbox);
	return mlx4_common_set_mcast_fltr(dev, slave, port, addr, clear, mode);
}

int mlx4_SET_MCAST_FLTR(struct mlx4_dev *dev, u8 port,
			u64 mac, u64 clear, u8 mode)
{
	/*
	 * With Ethernet mc steering exact match, there is no point to configure
	 * this filter 
	 */
	if (dev->caps.vep_mc_steering)
		return 0;

	if (mlx4_is_master(dev))
		return mlx4_common_set_mcast_fltr(dev, dev->caps.function,
						  port, mac, clear, mode);
	else
		return mlx4_cmd(dev, (mac | (clear << 63)), port, mode,
				MLX4_CMD_SET_MCAST_FLTR, MLX4_CMD_TIME_CLASS_B);
}
EXPORT_SYMBOL(mlx4_SET_MCAST_FLTR);


int mlx4_common_set_vlan_fltr(struct mlx4_dev *dev, int function,
					 int port, void *buf)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_vlan_fltr *filter;
	struct mlx4_slave_state *s_state = &priv->mfunc.master.slave_state[function];
	int i, err;
	unsigned long j;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	/* Update slave's Vlan filter */
	memcpy(s_state->vlan_filter[port]->entry, buf,
		sizeof(struct mlx4_vlan_fltr));

	/* We configure the Vlan filter to allow the vlans of
	 * all slaves */
	filter = (mlx4_vlan_fltr *)mailbox->buf;
	memset(filter, 0, sizeof(*filter));
	for (i = VLAN_FLTR_SIZE - 1; i >= 0; i--) {
		for (j = 0; j < dev->num_slaves; j++) {
			s_state = &priv->mfunc.master.slave_state[j];
			filter->entry[i] |= s_state->vlan_filter[port]->entry[i];
		}
	}

	err = mlx4_cmd(dev, mailbox->dma.da, port, 0, MLX4_CMD_SET_VLAN_FLTR,
			   MLX4_CMD_TIME_CLASS_B);
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_SET_VLAN_FLTR_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox)
{
	int err, port;

	UNREFERENCED_PARAMETER(outbox);

	port = vhcr->in_modifier;
	err =  mlx4_common_set_vlan_fltr(dev, slave, vhcr->in_modifier, inbox->buf);

	if (!err)
		err = mlx4_add_vlan_fltr_to_tracked_slave(dev, slave,  port);
	return err;
}

int mlx4_SET_VLAN_FLTR(struct mlx4_dev *dev, u8 port, u32 *Vlans)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_vlan_fltr *filter;
	int err = 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	filter = (mlx4_vlan_fltr *)mailbox->buf;	  
	if (Vlans) {
		memcpy(filter, Vlans, VLAN_FLTR_SIZE * sizeof(u32));
	} else {
		/* When no vlans are configured we block all vlans */
		memset(filter, 0, sizeof(*filter));
	}
	
	if (mlx4_is_master(dev))
		err = mlx4_common_set_vlan_fltr(dev, dev->caps.function,
						port, mailbox->buf);
	else
		err = mlx4_cmd(dev, mailbox->dma.da, port, 0, MLX4_CMD_SET_VLAN_FLTR,
				   MLX4_CMD_TIME_CLASS_B);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_VLAN_FLTR);


int mlx4_SET_IF_STAT(struct mlx4_dev *dev, u8 stat_mode)
{
    int err;
    err = mlx4_cmd(dev, stat_mode,0, 0, MLX4_CMD_SET_IF_STAT,MLX4_CMD_TIME_CLASS_B);
    return err;
}


static void fill_IF_statistics(void *statistics,
				 struct net_device_stats *stats)
{
    struct mlx4_query_if_stat_ext_out_mbox* p_if_stat_ext = NULL;
    struct mlx4_query_if_stat_basic_out_mbox* p_if_stat_basic = NULL;

    p_if_stat_basic = (struct mlx4_query_if_stat_basic_out_mbox*)statistics;
    
    if(p_if_stat_basic->if_cnt_mode == 0)
    {        
        stats->rx_packets = be64_to_cpu(*(u64*)&p_if_stat_basic->ifRxFrames_high);
        stats->rx_bytes   = be64_to_cpu(*(u64*)&p_if_stat_basic->ifRxOctets_high);
        
        stats->tx_packets = be64_to_cpu(*(u64*)&p_if_stat_basic->ifTxFrames_high);
        stats->tx_bytes   = be64_to_cpu(*(u64*)&p_if_stat_basic->ifTxOctets_high);
        return;
    }

    p_if_stat_ext = (mlx4_query_if_stat_ext_out_mbox*)statistics;
    ASSERT(p_if_stat_ext->if_cnt_mode == 1);
    ASSERT(p_if_stat_ext->num_of_if == 1);

    stats->rx_unicast_pkts   = be64_to_cpu(*(u64*)&p_if_stat_ext->ifRxUnicastFrames_high);
    stats->rx_unicast_octets = be64_to_cpu(*(u64*)&p_if_stat_ext->ifRxUnicastOctets_high);    
    
    stats->rx_multicast_pkts   = be64_to_cpu(*(u64*)&p_if_stat_ext->ifRxMulticastFrames_high);
    stats->rx_multicast_octets = be64_to_cpu(*(u64*)&p_if_stat_ext->ifRxMulticastOctets_high);
    
    stats->rx_broadcast_pkts   = be64_to_cpu(*(u64*)&p_if_stat_ext->ifRxBroadcastFrames_high);
    stats->rx_broadcast_octets = be64_to_cpu(*(u64*)&p_if_stat_ext->ifRxBroadcastOctets_high);   
    
    stats->tx_unicast_pkts   = be64_to_cpu(*(u64*)&p_if_stat_ext->ifTxUnicastFrames_high);
    stats->tx_unicast_octets = be64_to_cpu(*(u64*)&p_if_stat_ext->ifTxUnicastOctets_high);    
    
    stats->tx_multicast_pkts   = be64_to_cpu(*(u64*)&p_if_stat_ext->ifTxMulticastFrames_high);
    stats->tx_multicast_octets = be64_to_cpu(*(u64*)&p_if_stat_ext->ifTxMulticastOctets_high);
    
    stats->tx_broadcast_pkts   = be64_to_cpu(*(u64*)&p_if_stat_ext->ifTxBroadcastFrames_high);
    stats->tx_broadcast_octets = be64_to_cpu(*(u64*)&p_if_stat_ext->ifTxBroadcastOctets_high);
    
    stats->rx_bad_crc    = be64_to_cpu(*(u64*)&p_if_stat_ext->ifRxErrorFrames_high);
    stats->rx_drop_ovflw = be64_to_cpu(*(u64*)&p_if_stat_ext->ifRxNoBufferFrames_high);
    stats->tx_errors     = be64_to_cpu(*(u64*)&p_if_stat_ext->ifTxDroppedFrames_high);

    stats->rx_packets = stats->rx_unicast_pkts   + 
                        stats->rx_multicast_pkts +
                        stats->rx_broadcast_pkts;
    
    stats->rx_bytes = stats->rx_unicast_octets   +
                      stats->rx_multicast_octets +
                      stats->rx_broadcast_octets;
    
    stats->tx_packets = stats->tx_unicast_pkts   +
                        stats->tx_multicast_pkts +
                        stats->tx_broadcast_pkts;

    stats->tx_bytes = stats->tx_unicast_octets   +
                      stats->tx_multicast_octets +
                      stats->tx_broadcast_octets;
    
    stats->rx_errors = stats->rx_bad_crc + stats->rx_drop_ovflw;        
}

//
//  This wrapper will will overwrite the original counter_index
//  part of the in_mode and write function number instead of port number
//
int mlx4_QUERY_IF_STAT_wrapper(struct mlx4_dev *dev, int slave,
	struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox
	)
{    
    u8 function = mlx4_priv(dev)->mfunc.master.slave_state[slave].function;    
	vhcr->in_modifier &= 0xFFFF0000;
    vhcr->in_modifier |= function;

    UNUSED_PARAM(inbox);
    
	return mlx4_cmd_box(dev, 0, outbox->dma.da, vhcr->in_modifier, 0,
			   MLX4_CMD_QUERY_IF_STAT, MLX4_CMD_TIME_CLASS_B);    
}


int mlx4_QUERY_IF_STAT(struct mlx4_dev *dev,u8 reset,u8 batch, u16 counter_index,
			   struct net_device_stats *stats)
{
    struct mlx4_cmd_mailbox *mailbox;
    u32 in_mod = 0;
    int err;
    
    in_mod = (reset << 31);
    in_mod |= (batch << 30);
    in_mod |= (counter_index);

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);
 
    err = mlx4_cmd_box(dev, 0, mailbox->dma.da, in_mod, 0,
               MLX4_CMD_QUERY_IF_STAT, MLX4_CMD_TIME_CLASS_B);
    if (err)
        goto out;

    if (!reset && (stats != NULL))
        fill_IF_statistics(mailbox->buf, stats);

    if(reset && stats != NULL)
    {
        memset(stats, 0, sizeof(*stats));
    }

out:
    mlx4_free_cmd_mailbox(dev, mailbox);
    return err;
}

int 
mlx4_set_protocol_qos(
	struct mlx4_dev *dev,
	struct mlx4_qos_settings* settings,
	u16 count,
	u8 port
	)
{


	struct mlx4_priv* priv = mlx4_priv(dev);
	mlx4_qos_settings& qos_setting = priv->qos_settings[port-1];
	int alloc_count = qos_setting.alloc_count;
	int res = 0;
	
	//acquire the spin_lock.
	spin_lock(&priv->ctx_lock);
	if(count > alloc_count)
	{	
		//Free memory.
		kfree(qos_setting.p_qos_settings);
		qos_setting.p_qos_settings = NULL;
		qos_setting.count = 0;
		qos_setting.alloc_count = 0; 
	}

	if((qos_setting.p_qos_settings == NULL) && count != 0)
	{
		
		//Allocate count*2 elements.
		qos_setting.p_qos_settings = (mlx4_qos_element*) kzalloc(sizeof(mlx4_qos_element)*count*2,GFP_KERNEL);
		if(qos_setting.p_qos_settings == NULL)
		{ 
			res = -ENOMEM;
			goto out;
		}
		
		qos_setting.alloc_count = 2*count;
	}
	
	memcpy(qos_setting.p_qos_settings, settings->p_qos_settings,sizeof(mlx4_qos_element)*count);

	qos_setting.defaultSettingExist = settings->defaultSettingExist;
	qos_setting.priority = settings->priority;
	qos_setting.count = count;
	
	//release the spin lock
out:
	spin_unlock(&priv->ctx_lock);
	return res;
}

int mlx4_common_dump_eth_stats(struct mlx4_dev *dev, u32 in_mod, 
    struct mlx4_cmd_mailbox *outbox)
{
	return mlx4_cmd_box(dev, 0, outbox->dma.da, in_mod, 0,
			   MLX4_CMD_DUMP_ETH_STATS, MLX4_CMD_TIME_CLASS_B);
}

int mlx4_DUMP_ETH_STATS_wrapper(struct mlx4_dev *dev, int slave,
				   struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox)
{    
    u8 port_num = mlx4_priv(dev)->mfunc.master.slave_state[slave].port_num;
    u8 vep_num = mlx4_priv(dev)->mfunc.master.slave_state[slave].vep_num;
    
	UNUSED_PARAM(inbox);
	UNUSED_PARAM(outbox);
     ASSERT((port_num == 1) || (port_num == 2));
    
    //
    // bits[7:0] - port number, 1 - port 1, 2 - port 2
    // bit[8] - clear statistics (already set on slave side)
    // bits[11:19] - reserved
    // bits [15:12] - 0-port counters, 8-vep counters
    // bits[23:16] - vep number
    // bits 31:24] -reserved
    //
    vhcr->in_modifier |= ((vep_num << 16) | (MLX4_DUMP_STATS_FUNC_COUNTERS << 12) | port_num);
 	return mlx4_common_dump_eth_stats(dev, vhcr->in_modifier, outbox);
}

static void fill_port_statistics(void *statistics,
				 struct net_device_stats *stats)
{
	struct mlx4_stat_out_mbox *mlx4_port_stats = (struct mlx4_stat_out_mbox *) statistics;

    ASSERT(stats != NULL);
    ASSERT(mlx4_port_stats != NULL);

    stats->tx_packets = be64_to_cpu(mlx4_port_stats->TTOT_prio_0) +
                be64_to_cpu(mlx4_port_stats->TTOT_prio_1) +
                be64_to_cpu(mlx4_port_stats->TTOT_prio_2) +
                be64_to_cpu(mlx4_port_stats->TTOT_prio_3) +
                be64_to_cpu(mlx4_port_stats->TTOT_prio_4) +
                be64_to_cpu(mlx4_port_stats->TTOT_prio_5) +
                be64_to_cpu(mlx4_port_stats->TTOT_prio_6) +
                be64_to_cpu(mlx4_port_stats->TTOT_prio_7) +
                be64_to_cpu(mlx4_port_stats->TTOT_novlan) +
                be64_to_cpu(mlx4_port_stats->TTOT_loopbk);
    
    stats->rx_bytes = be64_to_cpu(mlx4_port_stats->ROCT_prio_0) +
              be64_to_cpu(mlx4_port_stats->ROCT_prio_1) +
              be64_to_cpu(mlx4_port_stats->ROCT_prio_2) +
              be64_to_cpu(mlx4_port_stats->ROCT_prio_3) +
              be64_to_cpu(mlx4_port_stats->ROCT_prio_4) +
              be64_to_cpu(mlx4_port_stats->ROCT_prio_5) +
              be64_to_cpu(mlx4_port_stats->ROCT_prio_6) +
              be64_to_cpu(mlx4_port_stats->ROCT_prio_7) +
              be64_to_cpu(mlx4_port_stats->ROCT_novlan);

    stats->tx_bytes = be64_to_cpu(mlx4_port_stats->TTTLOCT_prio_0) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_prio_1) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_prio_2) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_prio_3) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_prio_4) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_prio_5) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_prio_6) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_prio_7) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_novlan) +
              be64_to_cpu(mlx4_port_stats->TTTLOCT_loopbk);

	stats->rx_errors = be64_to_cpu(mlx4_port_stats->PCS) +
			   be32_to_cpu(mlx4_port_stats->RdropLength) +
			   be32_to_cpu(mlx4_port_stats->RJBBR) +
			   be32_to_cpu(mlx4_port_stats->RCRC) +
			   be32_to_cpu(mlx4_port_stats->RRUNT);
    
	stats->tx_errors = be32_to_cpu(mlx4_port_stats->TDROP);

    //
    // number of frames that the NIC cannot receive due to lack of NIC receive buffer space
    //
    stats->rx_drop_ovflw = be32_to_cpu(mlx4_port_stats->RdropOvflw);

    //
    // number of frames that are received with checksum errors
    //
    stats->rx_bad_crc = be32_to_cpu(mlx4_port_stats->RCRC);

    //
    //  Advanced Statistics:
    //  Number of Tx Unicast/Multicast/Broadcast frames        
    //    
    stats->tx_unicast_pkts =   be64_to_cpu(mlx4_port_stats->TTOTG_prio_0) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_prio_1) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_prio_2) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_prio_3) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_prio_4) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_prio_5) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_prio_6) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_prio_7) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_novlan) +
                                be64_to_cpu(mlx4_port_stats->TTOTG_loopbk);
    
    stats->tx_multicast_pkts = be64_to_cpu(mlx4_port_stats->TMCAST_prio_0) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_prio_1) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_prio_2) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_prio_3) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_prio_4) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_prio_5) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_prio_6) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_prio_7) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_novlan) +
                                be64_to_cpu(mlx4_port_stats->TMCAST_loopbk);

    stats->tx_broadcast_pkts = be64_to_cpu(mlx4_port_stats->TBCAST_prio_0) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_prio_1) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_prio_2) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_prio_3) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_prio_4) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_prio_5) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_prio_6) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_prio_7) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_novlan) +
                                be64_to_cpu(mlx4_port_stats->TBCAST_loopbk);
    
    //
    //  Advanced Statistics:
    //  Number of Rx Unicast/Multicast/Broadcast frames        
    //      
    stats->rx_unicast_pkts =   be64_to_cpu(mlx4_port_stats->RTOTG_prio_0) +
                                be64_to_cpu(mlx4_port_stats->RTOTG_prio_1) +
                                be64_to_cpu(mlx4_port_stats->RTOTG_prio_2) +
                                be64_to_cpu(mlx4_port_stats->RTOTG_prio_3) +
                                be64_to_cpu(mlx4_port_stats->RTOTG_prio_4) +
                                be64_to_cpu(mlx4_port_stats->RTOTG_prio_5) +
                                be64_to_cpu(mlx4_port_stats->RTOTG_prio_6) +
                                be64_to_cpu(mlx4_port_stats->RTOTG_prio_7) +
                                be64_to_cpu(mlx4_port_stats->RTOTG_novlan) ;
        
    stats->rx_multicast_pkts = be64_to_cpu(mlx4_port_stats->MCAST_prio_0) +
                                be64_to_cpu(mlx4_port_stats->MCAST_prio_1) +
                                be64_to_cpu(mlx4_port_stats->MCAST_prio_2) +
                                be64_to_cpu(mlx4_port_stats->MCAST_prio_3) +
                                be64_to_cpu(mlx4_port_stats->MCAST_prio_4) +
                                be64_to_cpu(mlx4_port_stats->MCAST_prio_5) +
                                be64_to_cpu(mlx4_port_stats->MCAST_prio_6) +
                                be64_to_cpu(mlx4_port_stats->MCAST_prio_7) +
                                be64_to_cpu(mlx4_port_stats->MCAST_novlan) ;
    
     stats->rx_broadcast_pkts = be64_to_cpu(mlx4_port_stats->RBCAST_prio_0) +
                                 be64_to_cpu(mlx4_port_stats->RBCAST_prio_1) +
                                 be64_to_cpu(mlx4_port_stats->RBCAST_prio_2) +
                                 be64_to_cpu(mlx4_port_stats->RBCAST_prio_3) +
                                 be64_to_cpu(mlx4_port_stats->RBCAST_prio_4) +
                                 be64_to_cpu(mlx4_port_stats->RBCAST_prio_5) +
                                 be64_to_cpu(mlx4_port_stats->RBCAST_prio_6) +
                                 be64_to_cpu(mlx4_port_stats->RBCAST_prio_7) +
                                 be64_to_cpu(mlx4_port_stats->RBCAST_novlan) ;  
     
     stats->rx_packets = stats->rx_broadcast_pkts +
                         stats->rx_unicast_pkts +
                         stats->rx_multicast_pkts;
}

static void fill_function_statistics(void *statistics,
				     struct net_device_stats *stats)
{
	struct mlx4_func_stat_out_mbox *mlx4_function_stats = (struct mlx4_func_stat_out_mbox *) statistics;

    ASSERT(stats != NULL);
    ASSERT(mlx4_function_stats != NULL);
    
    stats->rx_packets =
        be64_to_cpu(mlx4_function_stats->etherStatsPkts); 
    stats->rx_multicast_pkts = 
        be64_to_cpu(mlx4_function_stats->etherStatsMulticastPkts);
    stats->rx_broadcast_pkts =
        be64_to_cpu(mlx4_function_stats->etherStatsBroadcastPkts);
    stats->rx_unicast_pkts =
        stats->rx_packets - 
        stats->rx_multicast_pkts -
        stats->rx_broadcast_pkts;

    stats->rx_bytes = 
        be64_to_cpu(mlx4_function_stats->etherStatsOctets);
    
	stats->rx_errors =
		be64_to_cpu(mlx4_function_stats->etherStatsCRCAlignErrors) +
		be64_to_cpu(mlx4_function_stats->etherStatsFragments) +
		be64_to_cpu(mlx4_function_stats->etherStatsJabbers);
    
	stats->rx_drop_ovflw =
		be64_to_cpu(mlx4_function_stats->etherStatsDropEvents);

	stats->rx_bad_crc =
		be64_to_cpu(mlx4_function_stats->etherStatsCRCAlignErrors);
    
    stats->tx_packets = 0; 
    stats->tx_bytes = 0;       
    stats->tx_errors = 0;      

    
    stats->tx_unicast_pkts = 0;    
    stats->tx_multicast_pkts = 0;  
    stats->tx_broadcast_pkts = 0;  
}

int mlx4_DUMP_ETH_STATS(struct mlx4_dev *dev, u8 port, u8 reset,
			   struct net_device_stats *stats)
{
	struct mlx4_cmd_mailbox *mailbox;
	void (*do_fill_statistics)(void *, struct net_device_stats *) = NULL;
	u32 in_mod;
	int err;

	in_mod = (reset << 8);
	if (mlx4_is_mfunc(dev)) {
		 if (mlx4_is_master(dev)) {
			in_mod |= ((MLX4_DUMP_STATS_FUNC_COUNTERS << 12) | (dev->caps.vep_num << 16) | port);
		 }
	} else {
		in_mod |= ((MLX4_DUMP_STATS_PORT_COUNTERS << 12) | port);
	}

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	if (mlx4_is_master(dev))
		err = mlx4_common_dump_eth_stats(dev, in_mod, mailbox);
	else
		err = mlx4_cmd_box(dev, 0, mailbox->dma.da, in_mod, 0,
			   MLX4_CMD_DUMP_ETH_STATS, MLX4_CMD_TIME_CLASS_B);
	if (err)
		goto out;

	do_fill_statistics = mlx4_is_mfunc(dev) ? fill_function_statistics
						: fill_port_statistics;

	if (!reset && (stats != NULL))
		do_fill_statistics(mailbox->buf, stats);

out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}


