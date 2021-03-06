/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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


#include "mlx4.h"
#include "cmd.h"
#include "icm.h"

#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mr.tmh"
#endif


/*
 * Must be packed because mtt_seg is 64 bits but only aligned to 32 bits.
 */
#pragma pack(push,1)
struct mlx4_mpt_entry {
	__be32 flags;
	__be32 qpn;
	__be32 key;
	__be32 pd_flags;
	__be64 start;
	__be64 length;
	__be32 lkey;
	__be32 win_cnt;
	u8	reserved1;	
	u8	flags2; 
	u8	reserved2;
	u8	mtt_rep;
	__be64 mtt_seg;
	__be32 mtt_sz;
	__be32 entity_size;
	__be32 first_byte_offset;
} __attribute__((packed));
#pragma pack(pop)

#define MLX4_MPT_FLAG_SW_OWNS		(0xfUL << 28)
#define MLX4_MPT_FLAG_FREE		(0x3UL << 28)
#define MLX4_MPT_FLAG_MIO		(1 << 17)
#define MLX4_MPT_FLAG_BIND_ENABLE	(1 << 15)
#define MLX4_MPT_FLAG_PHYSICAL		(1 <<  9)
#define MLX4_MPT_FLAG_REGION		(1 <<  8)

#define MLX4_MPT_PD_FLAG_FAST_REG	(1 << 27)
#define MLX4_MPT_PD_FLAG_RAE		(1 << 28)
#define MLX4_MPT_PD_FLAG_EN_INV		(3 << 24)

#define MLX4_MPT_FLAG2_FBO_EN		 (1 <<	7)

#define MLX4_MPT_STATUS_SW		0xF0
#define MLX4_MPT_STATUS_HW		0x00

static u32 mlx4_buddy_alloc(struct mlx4_buddy *buddy, int order)
{
	int o;
	int m;
	u32 seg;

	spin_lock(&buddy->lock);

	for (o = order; o <= buddy->max_order; ++o) {
		if (buddy->num_free[o]) {
			m = 1 << (buddy->max_order - o);
			seg = find_first_bit(buddy->bits[o], m);
			if (seg < (u32)m)
				goto found;
		}
	}

	spin_unlock(&buddy->lock);
	return (u32)-1;

 found:
	clear_bit(seg, buddy->bits[o]);
	--buddy->num_free[o];

	while (o > order) {
		--o;
		seg <<= 1;
		set_bit(seg ^ 1, buddy->bits[o]);
		++buddy->num_free[o];
	}

	spin_unlock(&buddy->lock);

	seg <<= order;

	return seg;
}

static void mlx4_buddy_free(struct mlx4_buddy *buddy, u32 seg, int order)
{
	seg >>= order;

	spin_lock(&buddy->lock);

	while (test_bit(seg ^ 1, buddy->bits[order])) {
		clear_bit(seg ^ 1, buddy->bits[order]);
		--buddy->num_free[order];
		seg >>= 1;
		++order;
	}

	set_bit(seg, buddy->bits[order]);
	++buddy->num_free[order];

	spin_unlock(&buddy->lock);
}

static int mlx4_buddy_init(struct mlx4_buddy *buddy, int max_order)
{
	int i, s;

	buddy->max_order = max_order;
	spin_lock_init(&buddy->lock);

	buddy->bits = (unsigned long **)kzalloc((buddy->max_order + 1) * sizeof (long *),
				  GFP_KERNEL);
	buddy->num_free = (unsigned int *)kzalloc((buddy->max_order + 1) * sizeof (int *),
				  GFP_KERNEL);
	if (!buddy->bits || !buddy->num_free)
		goto err_out;

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->bits[i] = (unsigned long *)kmalloc(s * sizeof (long), GFP_KERNEL);
		if (!buddy->bits[i])
			goto err_out_free;
		bitmap_zero(buddy->bits[i], 1 << (buddy->max_order - i));
	}

	set_bit(0, buddy->bits[buddy->max_order]);
	buddy->num_free[buddy->max_order] = 1;

	return 0;

err_out_free:
	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

err_out:
	kfree(buddy->bits);
	kfree(buddy->num_free);

	return -ENOMEM;
}

static void mlx4_buddy_cleanup(struct mlx4_buddy *buddy)
{
	int i;

	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

	kfree(buddy->bits);
	kfree(buddy->num_free);
}

u32 mlx4_alloc_mtt_range(struct mlx4_dev *dev, int order)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;
	u64 in_param;
	u64 out_param;
	u32 seg;
	int err;

	if (mlx4_is_slave(dev)) {
		*((u32 *) &in_param) = order;
		*(((u32 *) &in_param) + 1) = 0;
		err = mlx4_cmd_imm(dev, in_param, &out_param, RES_MTT,
							   ICM_RESERVE_AND_ALLOC,
							   MLX4_CMD_ALLOC_RES,
							   MLX4_CMD_TIME_CLASS_A);
		if (err)
			return (u32)-1;
		else
			return (u32)out_param;
	}

	seg = mlx4_buddy_alloc(&mr_table->mtt_buddy, order);
	if (seg == -1)
		return (u32)-1;

	if (mlx4_table_get_range(dev, &mr_table->mtt_table, seg,
				 seg + (1 << order) - 1)) {
		mlx4_buddy_free(&mr_table->mtt_buddy, seg, order);
		return (u32)-1;
	}

	return seg;
}

int mlx4_mtt_init(struct mlx4_dev *dev, int npages, int page_shift,
		  struct mlx4_mtt *mtt)
{
	int i;

	if (!npages) {
		mtt->order		= -1;
		mtt->page_shift = MLX4_ICM_PAGE_SHIFT;
		return 0;
	} else
		mtt->page_shift = page_shift;

	for (mtt->order = 0, i = dev->caps.mtts_per_seg; i < npages; i <<= 1)
		++mtt->order;

	mtt->first_seg = mlx4_alloc_mtt_range(dev, mtt->order);
	if (mtt->first_seg == -1)
		return -ENOMEM;

	return 0;
}

void mlx4_free_mtt_range(struct mlx4_dev *dev, u32 first_seg, int order)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;
	u64 in_param;
	int err;

	if (mlx4_is_slave(dev)) {
		*((u32 *) &in_param) = first_seg;
		*(((u32 *) &in_param) + 1) = order;
		err = mlx4_cmd(dev, in_param, RES_MTT, ICM_RESERVE_AND_ALLOC,
							   MLX4_CMD_FREE_RES,
							   MLX4_CMD_TIME_CLASS_A);
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to free mtt range at:%d order:%d\n", 
			dev->pdev->name, first_seg, order));
	} else {
		mlx4_buddy_free(&mr_table->mtt_buddy, first_seg, order);
		mlx4_table_put_range(dev, &mr_table->mtt_table, first_seg,
						 first_seg + (1 << order) - 1);
	}
}

void mlx4_mtt_cleanup(struct mlx4_dev *dev, struct mlx4_mtt *mtt)
{
	if (mtt->order < 0)
		return;

	mlx4_free_mtt_range(dev, mtt->first_seg, mtt->order);
}

u64 mlx4_mtt_addr(struct mlx4_dev *dev, struct mlx4_mtt *mtt)
{
	return (u64) mtt->first_seg * dev->caps.mtt_entry_sz;
}

static u32 hw_index_to_key(u32 ind)
{
	return (ind >> 24) | (ind << 8);
}

static u32 key_to_hw_index(u32 key)
{
	return (key << 24) | (key >> 8);
}

static int mlx4_SW2HW_MPT(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int mpt_index)
{
	return mlx4_cmd(dev, mailbox->dma.da | dev->caps.function, mpt_index, 0, 
			MLX4_CMD_SW2HW_MPT, MLX4_CMD_TIME_CLASS_B);
}

static int mlx4_HW2SW_MPT(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int mpt_index)
{
	return mlx4_cmd_box(dev, 0, mailbox ? mailbox->dma.da : 0, mpt_index,
				(u8)!mailbox, MLX4_CMD_HW2SW_MPT, MLX4_CMD_TIME_CLASS_B);
}

int mlx4_WRITE_MTT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						 struct mlx4_cmd_mailbox *inbox,
						 struct mlx4_cmd_mailbox *outbox)
{
	struct mlx4_mtt mtt;
	u64 *page_list = (u64*)inbox->buf;
	int i;

	UNUSED_PARAM(slave);
	UNUSED_PARAM(outbox);
	
	/* Call the SW implementation of write_mtt:
	 * - Prepare a dummy mtt struct
	 * - Translate inbox contents to simple addresses in host endianess */
	mtt.first_seg = 0;
	mtt.order = 0;
	mtt.page_shift = 0;
	for (i = 0; i < (int)vhcr->in_modifier; ++i)
		page_list[i + 2] = be64_to_cpu(page_list[i + 2]) & ~1ULL;
	vhcr->err = mlx4_write_mtt(dev, &mtt, (int)be64_to_cpu(page_list[0]),
						vhcr->in_modifier,
						page_list + 2);
	return 0;
}

static int mlx4_WRITE_MTT(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int num_entries)
{
	return mlx4_cmd(dev, mailbox->dma.da, num_entries, 0, MLX4_CMD_WRITE_MTT,
			MLX4_CMD_TIME_CLASS_A);
}

int mlx4_mr_reserve(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 out_param;
	int err;
	u32 ret ;

	if (mlx4_is_slave(dev)) {
		err = mlx4_cmd_imm(dev, 0, &out_param, RES_MPT, ICM_RESERVE,
								MLX4_CMD_ALLOC_RES,
								MLX4_CMD_TIME_CLASS_A);
		if (err)
			return -1;
		return (int)out_param;
	}
	
	ret = mlx4_bitmap_alloc(&priv->mr_table.mpt_bitmap);
	return ret;
}

void mlx4_mr_release(struct mlx4_dev *dev, u32 index)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 in_param;
	int err;

	if (mlx4_is_slave(dev)) {
		*((u32 *) &in_param) = index;
		*(((u32 *) &in_param) + 1) = 0;
		err = mlx4_cmd(dev, in_param, RES_MPT, ICM_RESERVE,
							   MLX4_CMD_FREE_RES,
							   MLX4_CMD_TIME_CLASS_A);
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to release mr index:%d\n", 
				dev->pdev->name, index));
	} else
		mlx4_bitmap_free(&priv->mr_table.mpt_bitmap, index);
}

int mlx4_mr_alloc_icm(struct mlx4_dev *dev, u32 index)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;
	u64 param;

	if (mlx4_is_slave(dev)) {
		*((u32 *) &param) = index;
		*(((u32 *) &param) + 1) = 0;
		return mlx4_cmd_imm(dev, param, &param, RES_MPT, ICM_ALLOC,
							MLX4_CMD_ALLOC_RES,
							MLX4_CMD_TIME_CLASS_A);
	} else
		return mlx4_table_get(dev, &mr_table->dmpt_table, index);
}

void mlx4_mr_free_icm(struct mlx4_dev *dev, u32 index)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;
	u64 in_param;
	int err;

	if (mlx4_is_slave(dev)) {
		*((u32 *) &in_param) = index;
		*(((u32 *) &in_param) + 1) = 0;
		err = mlx4_cmd(dev, in_param, RES_MPT, ICM_ALLOC,
							   MLX4_CMD_FREE_RES,
							   MLX4_CMD_TIME_CLASS_A);
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to free icm of mr index:%d\n", 
				dev->pdev->name, index));
	} else
		mlx4_table_put(dev, &mr_table->dmpt_table, index);
}

int mlx4_mr_alloc(struct mlx4_dev *dev, u32 pd, u64 iova, u64 size, u32 access,
		  int npages, int page_shift, struct mlx4_mr *mr)
{
	u32 index;
	int err;

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	index = mlx4_mr_reserve(dev);
	if (index == -1)
		return -ENOMEM;


	err = mlx4_mtt_init(dev, npages, page_shift, &mr->mtt);
	if (err) {
		mlx4_mr_release(dev, index);
	} else {
		mr->iova	   = iova;
		mr->size	   = size;
		mr->pd		   = pd;
		mr->access	   = access;
		mr->enabled    = 0;
		mr->key 	   = hw_index_to_key(index);
	}

	MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV,
		( "%s: mlx4_mr_alloc: allocated lkey %#x, hw_ix %#x\n", 
		dev->pdev->name, mr->key, index ));

	return err;
}

int mlx4_mr_alloc_reserved(struct mlx4_dev *dev, u32 mridx, u32 pd,
			   u64 iova, u64 size, u32 access, int npages,
			   int page_shift, struct mlx4_mr *mr)
{
	mr->iova	   = iova;
	mr->size	   = size;
	mr->pd		   = pd;
	mr->access	   = access;
	mr->enabled    = 0;
	mr->key		   = hw_index_to_key(mridx);

	return mlx4_mtt_init(dev, npages, page_shift, &mr->mtt);
}

void mlx4_mr_free(struct mlx4_dev *dev, struct mlx4_mr *mr)
{
	int err;

	if (!mlx4_is_barred(dev) && mr->enabled) {
		err = mlx4_HW2SW_MPT(dev, NULL,
					 key_to_hw_index(mr->key) &
					 (dev->caps.num_mpts - 1));
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: HW2SW_MPT failed (%d)\n", dev->pdev->name, err));
	}

	MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV,
		( "%s: mlx4_mr_free: released lkey %#x, hw_ix %#x\n", 
		dev->pdev->name, mr->key, key_to_hw_index(mr->key) ));

	mlx4_mtt_cleanup(dev, &mr->mtt);
	mlx4_mr_release(dev, key_to_hw_index(mr->key));
	mlx4_mr_free_icm(dev, key_to_hw_index(mr->key));
}

void mlx4_mr_free_reserved(struct mlx4_dev *dev, struct mlx4_mr *mr)
{
	int err;

	if (mr->enabled) {
		err = mlx4_HW2SW_MPT(dev, NULL,
					 key_to_hw_index(mr->key) &
					 (dev->caps.num_mpts - 1));
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "HW2SW_MPT failed (%d)\n", err));
	}

	mlx4_mtt_cleanup(dev, &mr->mtt);
}

int mlx4_mr_reserve_range(struct mlx4_dev *dev, int cnt, int align, int *base_mridx)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u32 mridx;

	mridx = mlx4_bitmap_alloc_range(&priv->mr_table.mpt_bitmap, cnt, align);
	if (mridx == -1)
		return -ENOMEM;

	*base_mridx = mridx;
	return 0;
}

void mlx4_mr_release_range(struct mlx4_dev *dev, int base_mridx, int cnt)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	mlx4_bitmap_free_range(&priv->mr_table.mpt_bitmap, base_mridx, cnt);
}

static void __print_mpt(struct mlx4_dev *dev, struct mlx4_mpt_entry *mpt_entry, u32 key)
{
	int i, j;
	u32 *ptr = (u32*)mpt_entry;
	
	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
		( "%s: MPT entry (in be): lkey %#x, hw_ix %#x\n", 
		dev->pdev->name, key, key_to_hw_index(key) ));
	
	for ( i = 0, j = 0; i < (sizeof(struct mlx4_mpt_entry) >> 2); i+=4 )
	{
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			( "%s: %d. %08x %08x %08x %08x \n", 
			dev->pdev->name, j++, ptr[i], ptr[i+1], ptr[i+2], ptr[i+3] ));
	}
}

//int g_print_mpt = 0;

int mlx4_mr_enable(struct mlx4_dev *dev, struct mlx4_mr *mr)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mpt_entry *mpt_entry;
	int err;

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	err = mlx4_mr_alloc_icm(dev, key_to_hw_index(mr->key));
	if (err)
		return err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_table;
	}
	mpt_entry = (mlx4_mpt_entry *)mailbox->buf;

	memset(mpt_entry, 0, sizeof *mpt_entry);

	mpt_entry->flags = cpu_to_be32(MLX4_MPT_FLAG_MIO	 |
					   MLX4_MPT_FLAG_REGION	 |
					   mr->access);

	mpt_entry->key		   = cpu_to_be32(key_to_hw_index(mr->key));
	mpt_entry->pd_flags    = cpu_to_be32(mr->pd | MLX4_MPT_PD_FLAG_EN_INV);
	mpt_entry->start	   = cpu_to_be64(mr->iova);
	mpt_entry->length	   = cpu_to_be64(mr->size);
	mpt_entry->entity_size = cpu_to_be32(mr->mtt.page_shift);
	if (mr->mtt.order < 0) {
		mpt_entry->flags |= cpu_to_be32(MLX4_MPT_FLAG_PHYSICAL);
		mpt_entry->mtt_seg = 0;
	} else {
		mpt_entry->mtt_seg = cpu_to_be64(mlx4_mtt_addr(dev, &mr->mtt));
	}

	if (mr->mtt.order >= 0 && mr->mtt.page_shift == 0) {
		/* fast register MR in free state */
		mpt_entry->flags	|= cpu_to_be32(MLX4_MPT_FLAG_FREE);
		mpt_entry->pd_flags |= cpu_to_be32(MLX4_MPT_PD_FLAG_FAST_REG |
						   MLX4_MPT_PD_FLAG_RAE);
		mpt_entry->mtt_sz	 = cpu_to_be32((1 << mr->mtt.order) *
						   dev->caps.mtts_per_seg);
	} else {
		mpt_entry->flags	|= cpu_to_be32(MLX4_MPT_FLAG_SW_OWNS);
	}

//	if ( g_print_mpt )
//		__print_mpt( dev, mpt_entry, mr->key );

	err = mlx4_SW2HW_MPT(dev, mailbox,
				 key_to_hw_index(mr->key) & (dev->caps.num_mpts - 1));
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: SW2HW_MPT failed (%d)\n", dev->pdev->name, err));
		goto err_cmd;
	}

	mr->enabled = 1;

	mlx4_free_cmd_mailbox(dev, mailbox);

	return 0;

err_cmd:
	mlx4_free_cmd_mailbox(dev, mailbox);

err_table:
	//	will be released in mlx4_mr_free
	//	mlx4_mr_free_icm(dev, key_to_hw_index(mr->key));
	return err;
}

static int mlx4_write_mtt_chunk(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
				int start_index, int npages, u64 *page_list)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	__be64 *mtts;
	dma_addr_t dma_handle;
	int i;
	int s = start_index * sizeof (u64);

	/* All MTTs must fit in the same page */
	if (start_index / (PAGE_SIZE / sizeof (u64)) !=
		(start_index + npages - 1) / (PAGE_SIZE / sizeof (u64)))
		return -EINVAL;

	if (start_index & (dev->caps.mtts_per_seg - 1))
		return -EINVAL;

	mtts = (__be64*)mlx4_table_find(&priv->mr_table.mtt_table, mtt->first_seg +
				s / dev->caps.mtt_entry_sz, &dma_handle);
	if (!mtts) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "mlx4_write_mtt_chunk: mlx4_table_find failed with -ENOMEM \n"));
		return -ENOMEM;
	}

	for (i = 0; i < npages; ++i)
		mtts[i] = cpu_to_be64(page_list[i] | MLX4_MTT_FLAG_PRESENT);

	dma_sync_single(&dev->pdev->dev, dma_handle, npages * sizeof (u64), DMA_TO_DEVICE);

	return 0;
}

int mlx4_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   int start_index, int npages, u64 *page_list)
{
	struct mlx4_cmd_mailbox *mailbox = NULL;
	int chunk;
	int err = 0;
	__be64 *inbox = NULL;
	int i;

	if (mtt->order < 0)
		return -EINVAL;

	if (mlx4_is_slave(dev)) {
		mailbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(mailbox))
			return PTR_ERR(mailbox);
		inbox = (__be64*)mailbox->buf;
	}

	while (npages > 0) {
		if (mlx4_is_slave(dev)) {
			int s = mtt->first_seg * dev->caps.mtts_per_seg + start_index;
			chunk = min_t(int, MLX4_MAILBOX_SIZE / sizeof(u64) - dev->caps.mtts_per_seg, npages);
			if (s / (PAGE_SIZE / sizeof (u64)) != (s + chunk - 1) / (PAGE_SIZE / sizeof (u64)))
				chunk = PAGE_SIZE / sizeof (u64) - (s % (PAGE_SIZE / sizeof (u64)));

			inbox[0] = cpu_to_be64(mtt->first_seg * dev->caps.mtts_per_seg + start_index);
			inbox[1] = 0;
			for (i = 0; i < chunk; ++i)
				inbox[i + 2] = cpu_to_be64(page_list[i] | MLX4_MTT_FLAG_PRESENT);
			err = mlx4_WRITE_MTT(dev, mailbox, chunk);
		} else {
			chunk = min_t(int, PAGE_SIZE / sizeof(u64), npages);
			err = mlx4_write_mtt_chunk(dev, mtt, start_index, chunk, page_list);
		}
		if (err)
			goto out;

		npages		-= chunk;
		start_index += chunk;
		page_list	+= chunk;
	}

out:
	if (mlx4_is_slave(dev))
		mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_buf_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
			   struct mlx4_buf *buf)
{
	u64 *page_list;
	int err;
	int i;

	page_list = (u64*)kmalloc(buf->npages * sizeof *page_list, GFP_KERNEL);
	if (!page_list) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "mlx4_buf_write_mtt: kmalloc failed with -ENOMEM \n"));
		return -ENOMEM;
	}

	for (i = 0; i < buf->npages; ++i)
		if (buf->nbufs == 1)
			page_list[i] = buf->direct.map.da + (i << buf->page_shift);
		else
			page_list[i] = buf->page_list[i].map.da;

	err = mlx4_write_mtt(dev, mtt, 0, buf->npages, page_list);

	kfree(page_list);
	return err;
}

int mlx4_init_mr_table(struct mlx4_dev *dev)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;
	int err;

	/* Nothing to do for slaves - all MR handling is forwarded to the master */
	if (mlx4_is_slave(dev))
		return 0;

// Linux has here:	
//	err = mlx4_bitmap_init(&mr_table->mpt_bitmap, dev->caps.num_mpts,
//				   ~0, dev->caps.reserved_mrws, 0);

	if (!is_power_of_2(dev->caps.num_mpts))
		return -EINVAL;

	dev->caps.reserved_fexch_mpts_base = dev->caps.num_mpts -
		(2 * dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_EXCH]);
	err = mlx4_bitmap_init_with_effective_max(&mr_table->mpt_bitmap,
					dev->caps.num_mpts,
					(u32)~0, dev->caps.reserved_mrws,
					dev->caps.reserved_fexch_mpts_base);



	if (err)
		return err;

	err = mlx4_buddy_init(&mr_table->mtt_buddy,
				  ilog2(dev->caps.num_mtt_segs));
	if (err)
		goto err_buddy;

	if (dev->caps.reserved_mtts) {
		if (mlx4_alloc_mtt_range(dev, fls(dev->caps.reserved_mtts - 1)) == -1) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: MTT table of order %d is too small.\n",
				dev->pdev->name, mr_table->mtt_buddy.max_order));
			err = -ENOMEM;
			goto err_reserve_mtts;
		}
	}

	return 0;

err_reserve_mtts:
	mlx4_buddy_cleanup(&mr_table->mtt_buddy);

err_buddy:
	mlx4_bitmap_cleanup(&mr_table->mpt_bitmap);

	return err;
}

void mlx4_cleanup_mr_table(struct mlx4_dev *dev)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;

	if (mlx4_is_slave(dev))
		return;
	mlx4_buddy_cleanup(&mr_table->mtt_buddy);
	mlx4_bitmap_cleanup(&mr_table->mpt_bitmap);
}

static inline int mlx4_check_fmr(struct mlx4_fmr *fmr, 
				  int npages, u64 iova)
{
	int page_mask;

    ASSERT(fmr != NULL);

	if (npages > fmr->max_pages)
		return -EINVAL;

	page_mask = (1 << fmr->page_shift) - 1;

	/* We are getting page lists, so va must be page aligned. */
	if (iova & page_mask)
		return -EINVAL;

	if (fmr->maps >= fmr->max_maps)
		return -EINVAL;

	return 0;
}

int mlx4_map_phys_fmr_fbo(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
			  u64 *page_list, int npages, u64 iova, u32 fbo,
			  u32 len, u32 *lkey, u32 *rkey, int same_key)
{
	u32 key;
	int i, err;

	err = mlx4_check_fmr(fmr, npages, iova);
	if (err)
		return err;

	++fmr->maps;

	key = key_to_hw_index(fmr->mr.key);
	if (!same_key)
		key += dev->caps.num_mpts;
	*lkey = *rkey = fmr->mr.key = hw_index_to_key(key);

	*(u8 *) fmr->mpt = MLX4_MPT_STATUS_SW;

	/* Make sure MPT status is visible before writing MTT entries */
	wmb();

	for (i = 0; i < npages; ++i)
		fmr->mtts[i] = cpu_to_be64(page_list[i] | MLX4_MTT_FLAG_PRESENT);

	dma_sync_single(&dev->pdev->dev, fmr->dma_handle,
			npages * sizeof(u64), DMA_TO_DEVICE);

	fmr->mpt->key	 = cpu_to_be32(key);
	fmr->mpt->lkey	 = cpu_to_be32(key);
	fmr->mpt->length = cpu_to_be64(len);
	fmr->mpt->start  = cpu_to_be64(iova);
	fmr->mpt->first_byte_offset = cpu_to_be32(fbo & 0x001fffff);
	fmr->mpt->flags2 = (fbo ? MLX4_MPT_FLAG2_FBO_EN : 0);

	/* Make MTT entries are visible before setting MPT status */
	wmb();

	*(u8 *) fmr->mpt = MLX4_MPT_STATUS_HW;

	/* Make sure MPT status is visible before consumer can use FMR */
	wmb();

	return 0;
}

int mlx4_map_phys_fmr(struct mlx4_dev *dev, struct mlx4_fmr *fmr, u64 *page_list,
			  int npages, u64 iova, u32 *lkey, u32 *rkey)
{
	u32 key;
	int i, err;

	err = mlx4_check_fmr(fmr, npages, iova);
	if (err)
		return err;

	++fmr->maps;

	key = key_to_hw_index(fmr->mr.key);
	key += dev->caps.num_mpts;
	*lkey = *rkey = fmr->mr.key = hw_index_to_key(key);

	*(u8 *) fmr->mpt = MLX4_MPT_STATUS_SW;

	/* Make sure MPT status is visible before writing MTT entries */
	wmb();

	for (i = 0; i < npages; ++i)
		fmr->mtts[i] = cpu_to_be64(page_list[i] | MLX4_MTT_FLAG_PRESENT);

	dma_sync_single(&dev->pdev->dev, fmr->dma_handle,
			npages * sizeof(u64), DMA_TO_DEVICE);

	fmr->mpt->key	 = cpu_to_be32(key);
	fmr->mpt->lkey	 = cpu_to_be32(key);
	fmr->mpt->length = cpu_to_be64(npages * (1ull << fmr->page_shift));
	fmr->mpt->start  = cpu_to_be64(iova);

	/* Make MTT entries are visible before setting MPT status */
	wmb();

	*(u8 *) fmr->mpt = MLX4_MPT_STATUS_HW;

	/* Make sure MPT status is visible before consumer can use FMR */
	wmb();

	return 0;
}

int mlx4_fmr_alloc(struct mlx4_dev *dev, u32 pd, u32 access, int max_pages,
		   int max_maps, u8 page_shift, struct mlx4_fmr *fmr)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 mtt_seg;
	int err = -ENOMEM;

	if (page_shift < (ffs(dev->caps.page_size_cap) - 1) || page_shift >= 32)
		return -EINVAL;

	/* All MTTs must fit in the same page */
	if (max_pages * sizeof *fmr->mtts > PAGE_SIZE)
		return -EINVAL;

	fmr->page_shift = page_shift;
	fmr->max_pages	= max_pages;
	fmr->max_maps	= max_maps;
	fmr->maps = 0;

	err = mlx4_mr_alloc(dev, pd, 0, 0, access, max_pages,
				page_shift, &fmr->mr);
	if (err)
		return err;

	mtt_seg = fmr->mr.mtt.first_seg * dev->caps.mtt_entry_sz;

	fmr->mtts = (__be64*)mlx4_table_find(&priv->mr_table.mtt_table,
					fmr->mr.mtt.first_seg,
					&fmr->dma_handle);
	if (!fmr->mtts) {
		err = -ENOMEM;
		goto err_free;
	}
	return 0;

err_free:
	mlx4_mr_free(dev, &fmr->mr);
	return err;
}

int mlx4_fmr_alloc_reserved(struct mlx4_dev *dev, u32 mridx,
				u32 pd, u32 access, int max_pages,
				int max_maps, u8 page_shift, struct mlx4_fmr *fmr)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 mtt_seg;
	int err = -ENOMEM;

	if (page_shift < (ffs(dev->caps.page_size_cap) - 1) || page_shift >= 32)
		return -EINVAL;

	/* All MTTs must fit in the same page */
	if (max_pages * sizeof *fmr->mtts > PAGE_SIZE)
		return -EINVAL;

	fmr->page_shift = page_shift;
	fmr->max_pages	= max_pages;
	fmr->max_maps	= max_maps;
	fmr->maps = 0;

	err = mlx4_mr_alloc_reserved(dev, mridx, pd, 0, 0, access, max_pages,
					 page_shift, &fmr->mr);
	if (err)
		return err;

	mtt_seg = fmr->mr.mtt.first_seg * dev->caps.mtt_entry_sz;

	fmr->mtts = (__be64*)mlx4_table_find(&priv->mr_table.mtt_table,
					fmr->mr.mtt.first_seg,
					&fmr->dma_handle);
	if (!fmr->mtts) {
		err = -ENOMEM;
		goto err_free;
	}

	return 0;

err_free:
	mlx4_mr_free_reserved(dev, &fmr->mr);
	return err;
}

int mlx4_fmr_enable(struct mlx4_dev *dev, struct mlx4_fmr *fmr)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;

	err = mlx4_mr_enable(dev, &fmr->mr);
	if (err)
		return err;

	fmr->mpt = (mlx4_mpt_entry *)mlx4_table_find(&priv->mr_table.dmpt_table,
					key_to_hw_index(fmr->mr.key), NULL);
	if (!fmr->mpt)
		return -ENOMEM;

	return 0;
}

void mlx4_fmr_unmap(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
			u32 *lkey, u32 *rkey)
{
	u32 key;

	if (!fmr->maps)
		return;

	key = key_to_hw_index(fmr->mr.key);
	key &= dev->caps.num_mpts - 1;
	*lkey = *rkey = fmr->mr.key = hw_index_to_key(key);

	fmr->maps = 0;

	*(u8 *) fmr->mpt = MLX4_MPT_STATUS_SW;
}

int mlx4_fmr_free(struct mlx4_dev *dev, struct mlx4_fmr *fmr)
{
	if (fmr->maps)
		return -EBUSY;

	fmr->mr.enabled = 0;
	mlx4_mr_free(dev, &fmr->mr);

	return 0;
}

int mlx4_fmr_free_reserved(struct mlx4_dev *dev, struct mlx4_fmr *fmr)
{
	if (fmr->maps)
		return -EBUSY;

	fmr->mr.enabled = 0;
	mlx4_mr_free_reserved(dev, &fmr->mr);

	return 0;
}

int mlx4_SYNC_TPT(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_SYNC_TPT, 1000);
}
