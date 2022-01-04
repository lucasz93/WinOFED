/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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
#include "cmd.h"
#include "icm.h"
#include "cq.h"
#include <mlx4_debug.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "cq.tmh"
#endif


#define MLX4_CQ_STATUS_OK		( 0 << 28)
#define MLX4_CQ_STATUS_OVERFLOW		( 9 << 28)
#define MLX4_CQ_STATUS_WRITE_FAIL	(10 << 28)
#define MLX4_CQ_FLAG_CC			( 1 << 18)
#define MLX4_CQ_FLAG_OI			( 1 << 17)
#define MLX4_CQ_STATE_ARMED		( 9 <<  8)
#define MLX4_CQ_STATE_ARMED_SOL		( 6 <<  8)
#define MLX4_EQ_STATE_FIRED		(10 <<  8)

void mlx4_cq_completion(struct mlx4_dev *dev, struct mlx4_eq* eq, u32 cqn)
{
	struct mlx4_cq *cq;

	spin_lock_dpc(&eq->cq_lock);
	cq = (mlx4_cq *)radix_tree_lookup(&eq->cq_tree, cqn & (dev->caps.num_cqs - 1));
	spin_unlock_dpc(&eq->cq_lock);

	if (!cq) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Completion event for bogus CQ %08x\n", dev->pdev->name, cqn));
		return;
	}

	if (cq->p_u_arm_sn)
		++*cq->p_u_arm_sn;
	else
		++cq->arm_sn;

	cq->comp(cq);
}

void mlx4_cq_event(struct mlx4_dev *dev, struct mlx4_eq* eq, u32 cqn, int event_type)
{
	struct mlx4_cq *cq;

	spin_lock_dpc(&eq->cq_lock);

	cq = (mlx4_cq *)radix_tree_lookup(&eq->cq_tree, cqn & (dev->caps.num_cqs - 1));
	if (cq)
		atomic_inc(&cq->refcount);

	spin_unlock_dpc(&eq->cq_lock);

	if (!cq) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Async event for bogus CQ %08x\n", dev->pdev->name, cqn));
		return;
	}

	cq->event((mlx4_cq *)cq, (mlx4_event)event_type);

	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
}

static int mlx4_SW2HW_CQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int cq_num)
{
	return mlx4_cmd(dev, mailbox->dma.da | dev->caps.function, cq_num, 0, MLX4_CMD_SW2HW_CQ,
			MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_MODIFY_CQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int cq_num, u32 opmod)
{
	return mlx4_cmd(dev, mailbox->dma.da | dev->caps.function, cq_num, (u8)opmod, MLX4_CMD_MODIFY_CQ,
			MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_HW2SW_CQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int cq_num)
{
	return mlx4_cmd_box(dev, 0, mailbox ? mailbox->dma.da : 0, cq_num,
			    mailbox ? 0 : 1, MLX4_CMD_HW2SW_CQ,
			    MLX4_CMD_TIME_CLASS_A);
}

int mlx4_cq_modify(struct mlx4_dev *dev, struct mlx4_cq *cq,
		   struct mlx4_cq_context *context, int modify)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;

	if ( mlx4_is_barred(dev) ){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
		( "%s: MLX4_FLAG_RESET_DRIVER (mlx4_is_barred) flag is set.\n",
			dev->pdev->ib_dev->name));
	
		return -EFAULT;
	}
	
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)){
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			( "%s: mlx4_alloc_cmd_mailbox() failed with error:%d.\n",
			dev->pdev->ib_dev->name, PTR_ERR(mailbox)));
		
		return PTR_ERR(mailbox);
	}
	
	memcpy(mailbox->buf, context, sizeof *context);
	err = mlx4_MODIFY_CQ(dev, mailbox, cq->cqn, modify);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_cq_resize(struct mlx4_dev *dev, struct mlx4_cq *cq,
		   int entries, struct mlx4_mtt *mtt)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_cq_context *cq_context;
	u64 mtt_addr;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	cq_context = (mlx4_cq_context *)mailbox->buf;
	memset(cq_context, 0, sizeof *cq_context);

	cq_context->logsize_usrpage = cpu_to_be32(ilog2(entries) << 24);
	cq_context->log_page_size   = (u8)(mtt->page_shift - 12);
	mtt_addr = mlx4_mtt_addr(dev, mtt);
	cq_context->mtt_base_addr_h = (u8)(mtt_addr >> 32);
	cq_context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);

	err = mlx4_MODIFY_CQ(dev, mailbox, cq->cqn, 0);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_cq_alloc_icm(struct mlx4_dev *dev, int *cqn)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cq_table *cq_table = &priv->cq_table;
	u64 out_param;
	int err;

	if (mlx4_is_slave(dev)) {
		err = mlx4_cmd_imm(dev, 0, &out_param, RES_CQ,
						       ICM_RESERVE_AND_ALLOC,
						       MLX4_CMD_ALLOC_RES,
						       MLX4_CMD_TIME_CLASS_A);
		if (err) {
			*cqn = -1;
			return err;
		} else {
			*cqn = (int)out_param;
			return 0;
		}
	}

	*cqn = mlx4_bitmap_alloc(&cq_table->bitmap);
	if (*cqn == -1)
		return -ENOMEM;

	err = mlx4_table_get(dev, &cq_table->table, *cqn);
	if (err)
		goto err_out;

	err = mlx4_table_get(dev, &cq_table->cmpt_table, *cqn);
	if (err)
		goto err_put;
	return 0;

err_put:
	mlx4_table_put(dev, &cq_table->table, *cqn);

err_out:
	mlx4_bitmap_free(&cq_table->bitmap, *cqn);
	return err;
}

void mlx4_cq_free_icm(struct mlx4_dev *dev, int cqn)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cq_table *cq_table = &priv->cq_table;
	u64 in_param;
	int err;

	if (mlx4_is_slave(dev)) {
		*((u32 *) &in_param) = cqn;
		*(((u32 *) &in_param) + 1) = 0;
		err = mlx4_cmd(dev, in_param, RES_CQ, ICM_RESERVE_AND_ALLOC,
						      MLX4_CMD_FREE_RES,
						      MLX4_CMD_TIME_CLASS_A);
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed freeing cq:%d\n", dev->pdev->name, cqn));
	} else {
		mlx4_table_put(dev, &cq_table->cmpt_table, cqn);
		mlx4_table_put(dev, &cq_table->table, cqn);
		mlx4_bitmap_free(&cq_table->bitmap, cqn);
	}
}

int mlx4_cq_alloc(struct mlx4_dev *dev, int nent, struct mlx4_mtt *mtt,
		  struct mlx4_uar *uar, u64 db_rec, struct mlx4_cq *cq,
		  unsigned vector, int collapsed)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_cq_context *cq_context;
	struct mlx4_eq* eq = &(priv->eq_table.eq[vector]);
	struct mlx4_eq* event_eq = &(priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS ]);
	u64 mtt_addr;
	int err;

#define COLLAPSED_SHIFT	18
#define ENTRIES_SHIFT	24

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	cq->vector = vector;

	err = mlx4_cq_alloc_icm(dev, &cq->cqn);
	if (err)
		return err;

	// Insert the cq into its completion eq cq_tree
	cq->comp_eq_idx = (int)vector;
	spin_lock_irq(&eq->cq_lock);
	err = radix_tree_insert(&eq->cq_tree, cq->cqn, cq);
	spin_unlock_irq(&eq->cq_lock);
	if (err)
	{
		MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_CQ, ("radix_tree_insert into completion eq failed\n"));
		goto err_icm;
	}

	// Insert the cq into the event eq cq_tree
	spin_lock_irq(&event_eq->cq_lock);
	err = radix_tree_insert(&event_eq->cq_tree, cq->cqn, cq);
	spin_unlock_irq(&event_eq->cq_lock);
	if (err)
	{
		MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_CQ, ("radix_tree_insert into event eq failed\n"));
		goto err_comp_eq_radix;
	}

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_CQ, ("mlx4_alloc_cmd_mailbox failed\n"));
		goto err_event_eq_radix;
	}

	cq_context = (struct mlx4_cq_context *)mailbox->buf;
	memset(cq_context, 0, sizeof *cq_context);

	cq_context->flags = cpu_to_be32(!!collapsed << COLLAPSED_SHIFT);
	cq_context->logsize_usrpage = cpu_to_be32(
						(ilog2(nent) << ENTRIES_SHIFT) | uar->index);

	cq_context->comp_eqn	    = (u8)priv->eq_table.eq[vector].eqn;
	cq_context->log_page_size   = (u8)(mtt->page_shift - MLX4_ICM_PAGE_SHIFT);

	mtt_addr = mlx4_mtt_addr(dev, mtt);
	cq_context->mtt_base_addr_h = (u8)(mtt_addr >> 32);
	cq_context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);
	cq_context->db_rec_addr     = cpu_to_be64(db_rec);
	MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_CQ,
		("CQ: cqn %#x, nent %#x, mtt_base %#I64x, db_rec %#I64x, page_shift %d, log_page_size %#hx, uar_index %#x \n",
		cq->cqn, nent, mtt_addr, db_rec, mtt->page_shift, (u16)cq_context->log_page_size, uar->index ));

	err = mlx4_SW2HW_CQ(dev, mailbox, cq->cqn);
	mlx4_free_cmd_mailbox(dev, mailbox);
	if (err)
	{
		MLX4_PRINT( TRACE_LEVEL_ERROR, MLX4_DBG_CQ, ("mlx4_free_cmd_mailbox failed\n"));
		goto err_event_eq_radix;
	}

	priv->eq_table.eq[cq->vector].load++;
	cq->cons_index = 0;
	cq->arm_sn     = 1;
	cq->uar        = uar;
	cq->comp_eq_idx = vector;
	atomic_set(&cq->refcount, 1);
	init_completion(&cq->free);

	return 0;

err_event_eq_radix:
	spin_lock_irq(&event_eq->cq_lock);
	radix_tree_delete(&event_eq->cq_tree, cq->cqn);
	spin_unlock_irq(&event_eq->cq_lock);

err_comp_eq_radix:
	spin_lock_irq(&eq->cq_lock);
	radix_tree_delete(&eq->cq_tree, cq->cqn);
	spin_unlock_irq(&eq->cq_lock);

err_icm:
	mlx4_cq_free_icm(dev, cq->cqn);

	return err;
}

void mlx4_cq_free(struct mlx4_dev *dev, struct mlx4_cq *cq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_eq* eq = &priv->eq_table.eq[cq->comp_eq_idx];
	struct mlx4_eq* event_eq = &(priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS ]);
	int err = 0;

	if (!mlx4_is_barred(dev))
		err = mlx4_HW2SW_CQ(dev, NULL, cq->cqn);
	if (err)
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: HW2SW_CQ failed (%d) for CQN %06x\n", dev->pdev->name, err, cq->cqn));

	synchronize_irq(priv->eq_table.eq[cq->vector].irq);
	priv->eq_table.eq[cq->vector].load--;

	spin_lock_irq(&eq->cq_lock);
	radix_tree_delete(&eq->cq_tree, cq->cqn);
	spin_unlock_irq(&eq->cq_lock);

	spin_lock_irq(&event_eq->cq_lock);
	radix_tree_delete(&event_eq->cq_tree, cq->cqn);
	spin_unlock_irq(&event_eq->cq_lock);

	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
	wait_for_completion(&cq->free);

	mlx4_cq_free_icm(dev, cq->cqn);	
}

int mlx4_init_cq_table(struct mlx4_dev *dev)
{
	struct mlx4_cq_table *cq_table = &mlx4_priv(dev)->cq_table;
	int err;

	if (mlx4_is_slave(dev))
		return 0;

	err = mlx4_bitmap_init(&cq_table->bitmap, dev->caps.num_cqs,
			       dev->caps.num_cqs - 1, dev->caps.reserved_cqs);
	if (err)
		return err;

	return 0;
}

void mlx4_cleanup_cq_table(struct mlx4_dev *dev)
{
	if (mlx4_is_slave(dev))
		return;
	/* Nothing to do to clean up radix_tree */
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->cq_table.bitmap);
}
