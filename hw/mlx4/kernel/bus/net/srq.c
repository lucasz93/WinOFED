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


#include "mlx4.h"
#include "icm.h"
#include "cmd.h"

#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srq.tmh"
#endif


struct mlx4_srq_context {
	__be32			state_logsize_srqn;
	u8			logstride;
	u8			reserved1;
	__be16			xrc_domain;
	__be32			pg_offset_cqn;
	u32			reserved2;
	u8			log_page_size;
	u8			reserved3[2];
	u8			mtt_base_addr_h;
	__be32			mtt_base_addr_l;
	__be32			pd;
	__be16			limit_watermark;
	__be16			wqe_cnt;
	u16			reserved4;
	__be16			wqe_counter;
	u32			reserved5;
	__be64			db_rec_addr;
};


void mlx4_srq_event(struct mlx4_dev *dev, u32 srqn, int event_type)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	struct mlx4_srq *srq;

	spin_lock_dpc(&srq_table->lock);

	srq = (mlx4_srq *)radix_tree_lookup(&srq_table->tree, srqn & (dev->caps.num_srqs - 1));
	if (srq)
		atomic_inc(&srq->refcount);

	spin_unlock_dpc(&srq_table->lock);

	if (!srq) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Async event for bogus SRQ %08x\n", srqn));
		return;
	}

	srq->event((mlx4_srq *)srq, (mlx4_event)event_type);

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
}

static int mlx4_SW2HW_SRQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int srq_num)
{
	return mlx4_cmd(dev, mailbox->dma.da | dev->caps.function, srq_num, 0, 
            MLX4_CMD_SW2HW_SRQ, MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_HW2SW_SRQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int srq_num)
{
	return mlx4_cmd_box(dev, 0, mailbox ? mailbox->dma.da : 0, srq_num,
			    mailbox ? 0 : 1, MLX4_CMD_HW2SW_SRQ,
			    MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_ARM_SRQ(struct mlx4_dev *dev, int srq_num, int limit_watermark)
{
	return mlx4_cmd(dev, limit_watermark, srq_num, 0, MLX4_CMD_ARM_SRQ,
			MLX4_CMD_TIME_CLASS_B);
}

static int mlx4_QUERY_SRQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int srq_num)
{
	return mlx4_cmd_box(dev, 0, mailbox->dma.da, srq_num, 0, MLX4_CMD_QUERY_SRQ,
			    MLX4_CMD_TIME_CLASS_A);
}

int mlx4_srq_alloc_icm(struct mlx4_dev *dev, int *srqn)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	u64 out_param;
	int err;

	if (mlx4_is_slave(dev)) {
		err = mlx4_cmd_imm(dev, 0, &out_param, RES_SRQ,
						       ICM_RESERVE_AND_ALLOC,
						       MLX4_CMD_ALLOC_RES,
						       MLX4_CMD_TIME_CLASS_A);
		if (err) {
			*srqn = -1;
			return err;
		} else {
			*srqn = (int)out_param;
			return 0;
		}
	}

	*srqn = mlx4_bitmap_alloc(&srq_table->bitmap);
	if (*srqn == -1)
		return -ENOMEM;

	err = mlx4_table_get(dev, &srq_table->table, *srqn);
	if (err)
		goto err_out;

	err = mlx4_table_get(dev, &srq_table->cmpt_table, *srqn);
	if (err)
		goto err_put;
	return 0;

err_put:
	mlx4_table_put(dev, &srq_table->table, *srqn);

err_out:
	mlx4_bitmap_free(&srq_table->bitmap, *srqn);
	return err;
}

void mlx4_srq_free_icm(struct mlx4_dev *dev, int srqn)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	u64 in_param;
	int err;

	if (mlx4_is_slave(dev)) {
		*((u32 *) &in_param) = srqn;
		*(((u32 *) &in_param) + 1) = 0;
		err = mlx4_cmd(dev, in_param, RES_SRQ, ICM_RESERVE_AND_ALLOC,
						       MLX4_CMD_FREE_RES,
						       MLX4_CMD_TIME_CLASS_A);
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Failed freeing cq:%d\n", srqn));
	} else {
		mlx4_table_put(dev, &srq_table->cmpt_table, srqn);
		mlx4_table_put(dev, &srq_table->table, srqn);
		mlx4_bitmap_free(&srq_table->bitmap, srqn);
	}
}

int mlx4_srq_alloc(struct mlx4_dev *dev, u32 pdn, u32 cqn, u16 xrcd,
		   struct mlx4_mtt *mtt, u64 db_rec, struct mlx4_srq *srq)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_srq_context *srq_context;
	u64 mtt_addr;
	int err;

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	err = mlx4_srq_alloc_icm(dev, &srq->srqn);
	if (err)
		return err;

	spin_lock_irq(&srq_table->lock);
	err = radix_tree_insert(&srq_table->tree, srq->srqn, srq);
	spin_unlock_irq(&srq_table->lock);
	if (err)
		goto err_icm;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_radix;
	}

	srq_context = (mlx4_srq_context *)mailbox->buf;
	memset(srq_context, 0, sizeof *srq_context);

	srq_context->state_logsize_srqn = cpu_to_be32((ilog2(srq->max) << 24) |
						      srq->srqn);
	srq_context->logstride          = (u8)(srq->wqe_shift - 4);
	srq_context->xrc_domain		= cpu_to_be16(xrcd);
	srq_context->pg_offset_cqn	= cpu_to_be32(cqn & 0xffffff);
	srq_context->log_page_size      = (u8)(mtt->page_shift - MLX4_ICM_PAGE_SHIFT);

	mtt_addr = mlx4_mtt_addr(dev, mtt);
	srq_context->mtt_base_addr_h    = (u8)(mtt_addr >> 32);
	srq_context->mtt_base_addr_l    = cpu_to_be32(mtt_addr & 0xffffffff);
	srq_context->pd			= cpu_to_be32(pdn);
	srq_context->db_rec_addr        = cpu_to_be64(db_rec);

	err = mlx4_SW2HW_SRQ(dev, mailbox, srq->srqn);
	mlx4_free_cmd_mailbox(dev, mailbox);
	if (err)
		goto err_radix;

	atomic_set(&srq->refcount, 1);
	init_completion(&srq->free);

	return 0;

err_radix:
	spin_lock_irq(&srq_table->lock);
	radix_tree_delete(&srq_table->tree, srq->srqn);
	spin_unlock_irq(&srq_table->lock);

err_icm:
	mlx4_srq_free_icm(dev, srq->srqn);

	return err;
}

void mlx4_srq_invalidate(struct mlx4_dev *dev, struct mlx4_srq *srq)
{
	int err;

	if ( mlx4_is_barred(dev) )
		return;

	err = mlx4_HW2SW_SRQ(dev, NULL, srq->srqn);
	if (err)
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "HW2SW_SRQ failed (%d) for SRQN %06x\n", err, srq->srqn));
}

void mlx4_srq_remove(struct mlx4_dev *dev, struct mlx4_srq *srq)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;

	spin_lock_irq(&srq_table->lock);
	radix_tree_delete(&srq_table->tree, srq->srqn);
	spin_unlock_irq(&srq_table->lock);
}

void mlx4_srq_free(struct mlx4_dev *dev, struct mlx4_srq *srq)
{
	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
	wait_for_completion(&srq->free);

	mlx4_srq_free_icm(dev, srq->srqn);
}

int mlx4_srq_arm(struct mlx4_dev *dev, struct mlx4_srq *srq, int limit_watermark)
{
	return mlx4_ARM_SRQ(dev, srq->srqn, limit_watermark);
}

int mlx4_srq_query(struct mlx4_dev *dev, struct mlx4_srq *srq, int *limit_watermark)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_srq_context *srq_context;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	srq_context = (mlx4_srq_context *)mailbox->buf;

	err = mlx4_QUERY_SRQ(dev, mailbox, srq->srqn);
	if (err)
		goto err_out;
	*limit_watermark = be16_to_cpu(srq_context->limit_watermark);

err_out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_init_srq_table(struct mlx4_dev *dev)
{
	struct mlx4_srq_table *srq_table = &mlx4_priv(dev)->srq_table;
	int err;

	spin_lock_init(&srq_table->lock);
	INIT_RADIX_TREE(&srq_table->tree, GFP_ATOMIC);
	if (mlx4_is_slave(dev))
		return 0;

	err = mlx4_bitmap_init(&srq_table->bitmap, dev->caps.num_srqs,
			       dev->caps.num_srqs - 1, dev->caps.reserved_srqs);
	if (err)
		return err;

	return 0;
}

void mlx4_cleanup_srq_table(struct mlx4_dev *dev)
{
	RMV_RADIX_TREE(&mlx4_priv(dev)->srq_table.tree);
	if (mlx4_is_slave(dev))
		return;
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->srq_table.bitmap);
}
