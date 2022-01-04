/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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
#include "mlx4_debug.h"
#include "icm.h"
#include "cmd.h"
#include "qp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "qp.tmh"
#endif


void mlx4_qp_event(struct mlx4_dev *dev, u32 qpn, int event_type)
{
    struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
    struct mlx4_qp *qp;

    spin_lock_dpc(&qp_table->lock);

    qp = __mlx4_qp_lookup(dev, qpn);
    if (qp)
        atomic_inc(&qp->refcount);

    spin_unlock_dpc(&qp_table->lock);

    if (!qp) {
        MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Async event %d for bogus QP %08x\n",
              dev->pdev->name, event_type, qpn));
        return;
    }

    qp->event((mlx4_qp*)qp, (mlx4_event)event_type);

    if (atomic_dec_and_test(&qp->refcount))
        complete(&qp->free);
}

int mlx4_RTR2RTS_QP_wrapper(struct mlx4_dev *dev, int slave, 
    struct mlx4_vhcr *vhcr,
    struct mlx4_cmd_mailbox *inbox,
    struct mlx4_cmd_mailbox *outbox)
{
    struct mlx4_qp_context *context = (struct mlx4_qp_context *)((char*)inbox->buf + 8);
    u8 vep_num = mlx4_priv(dev)->mfunc.master.slave_state[slave].vep_num;
    u8 port = ((context->pri_path.sched_queue >> 6) & 1) + 1;

    UNUSED_PARAM(outbox);
    
    if (mlx4_priv(dev)->vep_mode[port])
        context->pri_path.sched_queue = (context->pri_path.sched_queue & 0xc3 ) |
                                        (vep_num << 3);

    inbox->dma.da |= (u64) slave;
    return mlx4_cmd(dev, inbox->dma.da, vhcr->in_modifier,
                        vhcr->op_modifier, vhcr->op, MLX4_CMD_TIME_CLASS_C);
}

int mlx4_INIT2RTR_QP_wrapper(struct mlx4_dev *dev, int slave, 
    struct mlx4_vhcr *vhcr,
    struct mlx4_cmd_mailbox *inbox,
    struct mlx4_cmd_mailbox *outbox)
{
    enum mlx4_qp_optpar *optpar = (enum mlx4_qp_optpar *)((char*)inbox->buf);
    struct mlx4_qp_context *context = (struct mlx4_qp_context *)((char*)inbox->buf + 8);
    u8 function = mlx4_priv(dev)->mfunc.master.slave_state[slave].function;

    UNUSED_PARAM(outbox);
    
    if(context->pri_path.if_counter_index)
    {
       context->pri_path.if_counter_index = function;
       *optpar = (mlx4_qp_optpar)((int)(*optpar) | MLX4_QP_OPTPAR_COUNTER_INDEX);
    }

    inbox->dma.da |= (u64) slave;
    return mlx4_cmd(dev, inbox->dma.da, vhcr->in_modifier,
                        vhcr->op_modifier, vhcr->op, MLX4_CMD_TIME_CLASS_C);
}

int mlx4_RTS2RTS_QP_wrapper(struct mlx4_dev *dev, int slave, 
    struct mlx4_vhcr *vhcr,
    struct mlx4_cmd_mailbox *inbox,
    struct mlx4_cmd_mailbox *outbox)
{
    enum mlx4_qp_optpar *optpar = (enum mlx4_qp_optpar *)((char*)inbox->buf);
    struct mlx4_qp_context *context = (struct mlx4_qp_context *)((char*)inbox->buf + 8);
    u8 function = mlx4_priv(dev)->mfunc.master.slave_state[slave].function;

    UNUSED_PARAM(outbox);
    
    if(context->pri_path.if_counter_index)
    {
       context->pri_path.if_counter_index = function;
       *optpar = (mlx4_qp_optpar)((int)(*optpar) | MLX4_QP_OPTPAR_COUNTER_INDEX);
    }

    inbox->dma.da |= (u64) slave;
    return mlx4_cmd(dev, inbox->dma.da, vhcr->in_modifier,
                        vhcr->op_modifier, vhcr->op, MLX4_CMD_TIME_CLASS_C);
}

    


int mlx4_qp_modify(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
           enum mlx4_qp_state cur_state, enum mlx4_qp_state new_state,
           struct mlx4_qp_context *context, enum mlx4_qp_optpar optpar,
           int sqd_event, struct mlx4_qp *qp, int is_rdma_qp)
{
    struct mlx4_cmd_mailbox *mailbox;
    int ret = 0;
    static u16 op[MLX4_QP_NUM_STATE][MLX4_QP_NUM_STATE];
    static int op_inited = 0;
    u8 port;    
    u8 vep_num;

    if ( mlx4_is_barred(dev) )
        return -EFAULT;

    if (!op_inited) {
        op[MLX4_QP_STATE_RST][MLX4_QP_STATE_RST]    = MLX4_CMD_2RST_QP;
        op[MLX4_QP_STATE_RST][MLX4_QP_STATE_ERR]    = MLX4_CMD_2ERR_QP;
        op[MLX4_QP_STATE_RST][MLX4_QP_STATE_INIT]   = MLX4_CMD_RST2INIT_QP;

        op[MLX4_QP_STATE_INIT][MLX4_QP_STATE_RST]   = MLX4_CMD_2RST_QP;
        op[MLX4_QP_STATE_INIT][MLX4_QP_STATE_ERR]   = MLX4_CMD_2ERR_QP;
        op[MLX4_QP_STATE_INIT][MLX4_QP_STATE_INIT]  = MLX4_CMD_INIT2INIT_QP;
        op[MLX4_QP_STATE_INIT][MLX4_QP_STATE_RTR]   = MLX4_CMD_INIT2RTR_QP;

        op[MLX4_QP_STATE_RTR][MLX4_QP_STATE_RST]    = MLX4_CMD_2RST_QP;
        op[MLX4_QP_STATE_RTR][MLX4_QP_STATE_ERR]    = MLX4_CMD_2ERR_QP;
        op[MLX4_QP_STATE_RTR][MLX4_QP_STATE_RTS]    = MLX4_CMD_RTR2RTS_QP;

        op[MLX4_QP_STATE_RTS][MLX4_QP_STATE_RST]    = MLX4_CMD_2RST_QP;
        op[MLX4_QP_STATE_RTS][MLX4_QP_STATE_ERR]    = MLX4_CMD_2ERR_QP;
        op[MLX4_QP_STATE_RTS][MLX4_QP_STATE_RTS]    = MLX4_CMD_RTS2RTS_QP;
        op[MLX4_QP_STATE_RTS][MLX4_QP_STATE_SQD]    = MLX4_CMD_RTS2SQD_QP;

        op[MLX4_QP_STATE_SQD][MLX4_QP_STATE_RST]    = MLX4_CMD_2RST_QP;
        op[MLX4_QP_STATE_SQD][MLX4_QP_STATE_ERR]    = MLX4_CMD_2ERR_QP;
        op[MLX4_QP_STATE_SQD][MLX4_QP_STATE_RTS]    = MLX4_CMD_SQD2RTS_QP;
        op[MLX4_QP_STATE_SQD][MLX4_QP_STATE_SQD]    = MLX4_CMD_SQD2SQD_QP;

        op[MLX4_QP_STATE_SQER][MLX4_QP_STATE_RST]   = MLX4_CMD_2RST_QP;
        op[MLX4_QP_STATE_SQER][MLX4_QP_STATE_ERR]   = MLX4_CMD_2ERR_QP;
        op[MLX4_QP_STATE_SQER][MLX4_QP_STATE_RTS]   = MLX4_CMD_SQERR2RTS_QP;

        op[MLX4_QP_STATE_ERR][MLX4_QP_STATE_RST]    = MLX4_CMD_2RST_QP;
        op[MLX4_QP_STATE_ERR][MLX4_QP_STATE_ERR]    = MLX4_CMD_2ERR_QP;

        op_inited = 1;
    };

    if (cur_state >= MLX4_QP_NUM_STATE || new_state >= MLX4_QP_NUM_STATE ||
        !op[cur_state][new_state])
        return -EINVAL;

    if (op[cur_state][new_state] == MLX4_CMD_2RST_QP)
        return mlx4_cmd(dev, 0, qp->qpn, 2,
                MLX4_CMD_2RST_QP, MLX4_CMD_TIME_CLASS_A);

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    if (cur_state == MLX4_QP_STATE_RST && new_state == MLX4_QP_STATE_INIT) {
        u64 mtt_addr = mlx4_mtt_addr(dev, mtt);
        context->mtt_base_addr_h = (u8)(mtt_addr >> 32);
        context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);
        context->log_page_size   = (u8)(mtt->page_shift - MLX4_ICM_PAGE_SHIFT);
    }

    port = ((context->pri_path.sched_queue >> 6) & 1) + 1;

	if (dev->caps.port_type_final[port] == MLX4_PORT_TYPE_ETH) {
		// Do not override sched_queue (priority) for RoCE QP
		if(! is_rdma_qp)
		{
			vep_num = (u8)dev->caps.vep_num;
			context->pri_path.sched_queue = (context->pri_path.sched_queue & 0xc3) |
							(vep_num << 3);
		}
	}

    *(__be32 *) mailbox->buf = cpu_to_be32(optpar);
    memcpy((u8*)mailbox->buf + 8, context, sizeof *context);

    ((struct mlx4_qp_context *) ((u8*)mailbox->buf + 8))->local_qpn =
        cpu_to_be32(qp->qpn);

    ret = mlx4_cmd(dev, mailbox->dma.da | dev->caps.function, 
               qp->qpn | (!!sqd_event << 31),
               new_state == MLX4_QP_STATE_RST ? 2 : 0,
               op[cur_state][new_state], MLX4_CMD_TIME_CLASS_C);

    mlx4_free_cmd_mailbox(dev, mailbox);
    return ret;
}

u32 mlx4_get_slave_sqp(struct mlx4_dev *dev, int slave)
{
    if (mlx4_is_master(dev) && slave < (int)dev->num_slaves) {
        if (slave == dev->caps.function)
            return dev->caps.sqp_start;
        else /* FIXME: this distinction won't be necessary when master executes paravirt commands */
            return mlx4_priv(dev)->mfunc.master.slave_state[slave].sqp_start;
    }
    if (mlx4_is_slave(dev) && slave < dev->caps.sqp_demux) {
        return mlx4_priv(dev)->mfunc.demux_sqp[slave];
    }
    return 0;
}

int mlx4_GET_SLAVE_SQP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
                              struct mlx4_cmd_mailbox *inbox,
                              struct mlx4_cmd_mailbox *outbox)
{
    u32 *slave_sqp = (u32*)outbox->buf;
    int i;

    UNUSED_PARAM(vhcr);
    UNUSED_PARAM(inbox);
    UNUSED_PARAM(slave);
    
    for (i = 0; i < 64; i++)
        slave_sqp[i] = mlx4_get_slave_sqp(dev, i);
    return 0;
}

int mlx4_GET_SLAVE_SQP(struct mlx4_dev *dev, u32 *sqp, int num)
{
    struct mlx4_cmd_mailbox *mailbox;
    int err;

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    err = mlx4_cmd_box(dev, 0, mailbox->dma.da, 0, 0, MLX4_CMD_GET_SLAVE_SQP,
               MLX4_CMD_TIME_CLASS_A);
    if (!err)
        memcpy(sqp, mailbox->buf, sizeof (u32) * num);

    mlx4_free_cmd_mailbox(dev, mailbox);
    return err;
}

int mlx4_qp_reserve_range(struct mlx4_dev *dev, int cnt, int align, int *base)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_qp_table *qp_table = &priv->qp_table;
    u64 in_param;
    u64 out_param;
    int err;

    if (mlx4_is_slave(dev)) {
        *((u32 *) &in_param) = cnt;
        *(((u32 *) &in_param) + 1) = align;
        err = mlx4_cmd_imm(dev, in_param, &out_param, RES_QP, ICM_RESERVE,
                                  MLX4_CMD_ALLOC_RES,
                                  MLX4_CMD_TIME_CLASS_A);
        if (err)
            return err;
        *base = (u32)out_param;
    } else {
        *base = mlx4_bitmap_alloc_range(&qp_table->bitmap, cnt, align);
        if (*base == -1)
            return -ENOMEM;
    }

    return 0;
}

void mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_qp_table *qp_table = &priv->qp_table;
    u64 in_param;
    int err;

    if (mlx4_is_slave(dev)) {
        *((u32 *) &in_param) = base_qpn;
        *(((u32 *) &in_param) + 1) = cnt;
        err = mlx4_cmd(dev, in_param, RES_QP, ICM_RESERVE,
                              MLX4_CMD_FREE_RES,
                              MLX4_CMD_TIME_CLASS_A);
        if (err) {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to release qp range base:%d cnt:%d\n",
                dev->pdev->name, base_qpn, cnt));
        }
    } else {
        if (base_qpn < dev->caps.sqp_start + 8)
            return;
        mlx4_bitmap_free_range(&qp_table->bitmap, base_qpn, cnt);
    }
}

int mlx4_qp_alloc_icm(struct mlx4_dev *dev, int qpn)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_qp_table *qp_table = &priv->qp_table;
    u64 param;
    int err;

    if (mlx4_is_slave(dev)) {
        *((u32 *) &param) = qpn;
        *(((u32 *) &param) + 1) = 0;
        return mlx4_cmd_imm(dev, param, &param, RES_QP, ICM_ALLOC,
                                MLX4_CMD_ALLOC_RES,
                                MLX4_CMD_TIME_CLASS_A);
    }
    err = mlx4_table_get(dev, &qp_table->qp_table, qpn);
    if (err)
        goto err_out;

    err = mlx4_table_get(dev, &qp_table->auxc_table, qpn);
    if (err)
        goto err_put_qp;

    err = mlx4_table_get(dev, &qp_table->altc_table, qpn);
    if (err)
        goto err_put_auxc;

    err = mlx4_table_get(dev, &qp_table->rdmarc_table, qpn);
    if (err)
        goto err_put_altc;

    err = mlx4_table_get(dev, &qp_table->cmpt_table, qpn);
    if (err)
        goto err_put_rdmarc;

    return 0;

err_put_rdmarc:
    mlx4_table_put(dev, &qp_table->rdmarc_table, qpn);

err_put_altc:
    mlx4_table_put(dev, &qp_table->altc_table, qpn);

err_put_auxc:
    mlx4_table_put(dev, &qp_table->auxc_table, qpn);

err_put_qp:
    mlx4_table_put(dev, &qp_table->qp_table, qpn);

err_out:
    return err;
}

void mlx4_qp_free_icm(struct mlx4_dev *dev, int qpn)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_qp_table *qp_table = &priv->qp_table;
    u64 in_param;
    int err;

    if (mlx4_is_slave(dev)) {
        *((u32 *) &in_param) = qpn;
        *(((u32 *) &in_param) + 1) = 0;
        err = mlx4_cmd(dev, in_param, RES_QP, ICM_ALLOC,
                              MLX4_CMD_FREE_RES,
                              MLX4_CMD_TIME_CLASS_A);
        if (err)
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to free icm of qp:%d\n", dev->pdev->name, qpn));
    } else {
        mlx4_table_put(dev, &qp_table->cmpt_table, qpn);
        mlx4_table_put(dev, &qp_table->rdmarc_table, qpn);
        mlx4_table_put(dev, &qp_table->altc_table, qpn);
        mlx4_table_put(dev, &qp_table->auxc_table, qpn);
        mlx4_table_put(dev, &qp_table->qp_table, qpn);
    }
}

int mlx4_qp_alloc(struct mlx4_dev *dev, int qpn, struct mlx4_qp *qp)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_qp_table *qp_table = &priv->qp_table;
    int err;

    if ( mlx4_is_barred(dev) ){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: MLX4_FLAG_RESET_DRIVER flag is set. aborting QP %d alloc.\n", 
				dev->pdev->name, qpn));

        return -EFAULT;
    }

    if (!qpn){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: qpn=0, aborting QP alloc.\n", 
				dev->pdev->name));

        return -EINVAL;
    }
	
    qp->qpn = qpn;

    err = mlx4_qp_alloc_icm(dev, qpn);
    if (err){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: mlx4_qp_alloc_icm() failed with ERRNO:%d, aborting QP %d alloc.\n", 
				dev->pdev->name, err, qpn));

        return err;
    }
	
    spin_lock_irq(&qp_table->lock);
    err = radix_tree_insert(&dev->qp_table_tree, qp->qpn & (dev->caps.num_qps - 1), qp);
    spin_unlock_irq(&qp_table->lock);
    if (err){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: radix_tree_insert() failed with ERRNO:%d, aborting QP %d alloc.\n", 
				dev->pdev->name, err, qpn));

        goto err_icm;
    }
	
    atomic_set(&qp->refcount, 1);
    init_completion(&qp->free);

    return 0;

err_icm:
    mlx4_qp_free_icm(dev, qpn);
    return err;
}

void mlx4_qp_remove(struct mlx4_dev *dev, struct mlx4_qp *qp)
{
    struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
    unsigned long flags;
	UNUSED_PARAM(flags);

    spin_lock_irqsave(&qp_table->lock, &flags);
    radix_tree_delete(&dev->qp_table_tree, qp->qpn & (dev->caps.num_qps - 1));
    spin_unlock_irqrestore(&qp_table->lock, flags);
}

struct mlx4_qp *mlx4_qp_lookup_locked(struct mlx4_dev *dev, u32 qpn)
{
    struct mlx4_qp *qp;
    struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;

    spin_lock_irq(&qp_table->lock);
    qp = (mlx4_qp*)radix_tree_lookup(&dev->qp_table_tree, qpn & (dev->caps.num_qps - 1));
    spin_unlock_irq(&qp_table->lock);
    return qp;
}

void mlx4_qp_free(struct mlx4_dev *dev, struct mlx4_qp *qp)
{
    if (atomic_dec_and_test(&qp->refcount))
        complete(&qp->free);
    wait_for_completion(&qp->free);

    mlx4_qp_free_icm(dev, qp->qpn);
}

int mlx4_CONF_SPECIAL_QP(struct mlx4_dev *dev, u32 base_qpn)
{
    return mlx4_cmd(dev, 0, base_qpn,
            (dev->caps.flags & MLX4_DEV_CAP_FLAG_RAW_ETY) ? 4 : 0,
            MLX4_CMD_CONF_SPECIAL_QP, MLX4_CMD_TIME_CLASS_B);
}

int mlx4_CONF_SPECIAL_QP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
                               struct mlx4_cmd_mailbox *inbox,
                               struct mlx4_cmd_mailbox *outbox)
{
    struct mlx4_priv *priv = mlx4_priv(dev);

    UNUSED_PARAM(inbox);
    UNUSED_PARAM(outbox);

    priv->mfunc.master.slave_state[slave].sqp_start = vhcr->in_modifier & 0xffffff;
    return 0;
}

int mlx4_init_qp_table(struct mlx4_dev *dev)
{
    struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
    int err;
    int reserved_from_top = 0;
// hard to believe, that someone will require less !
#define MIN_QPS_TO_WORK_WITH    16

    spin_lock_init(&qp_table->lock);
    INIT_RADIX_TREE(&dev->qp_table_tree, GFP_ATOMIC);
    if (mlx4_is_slave(dev)) {
        /* For each slave, just allocate a normal 8-byte alligned special-QP
         * range intead of mlx4_init_qp_table() reservation */
        err = mlx4_qp_reserve_range(dev, 8, 8, &dev->caps.sqp_start);
        if (err) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_QP,( "%s: Failed to allocate special QP range\n", dev->pdev->name));
            return err;
        }

        err = mlx4_CONF_SPECIAL_QP(dev, dev->caps.sqp_start);
        if (err) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_QP,( "%s: Failed to configure special QP range\n", dev->pdev->name));
            mlx4_qp_release_range(dev, dev->caps.sqp_start, 8);
            return err;
        }
        return 0;
    }

    /*
     * We reserve 2 extra QPs per port for the special QPs.  The
     * block of special QPs must be aligned to a multiple of 8, so
     * round up.
     * We also reserve the MSB of the 24-bit QP number to indicate
     * an XRC qp.
     */
    dev->caps.sqp_start =
        ALIGN(dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW], 8);

    /* If multi-function is enabled, we reserve an additional QP for qp0/1 tunneling.
     * CX1: slave0 manages tunnel QP */
    dev->caps.tunnel_qpn = mlx4_is_master(dev) ? dev->caps.sqp_start + 8 : 0;

    {
        int sort[MLX4_NUM_QP_REGION];
        int i, j, tmp;
        int last_base = dev->caps.num_qps;

        for (i = 1; i < MLX4_NUM_QP_REGION; ++i)
            sort[i] = i;

        for (i = MLX4_NUM_QP_REGION; i > 0; --i) {
            for (j = 2; j < i; ++j) {
                if (dev->caps.reserved_qps_cnt[sort[j]] >
                    dev->caps.reserved_qps_cnt[sort[j - 1]]) {
                    tmp         = sort[j];
                    sort[j]         = sort[j - 1];
                    sort[j - 1]     = tmp;
                }
            }
        }

        for (i = 1; i < MLX4_NUM_QP_REGION; ++i) {
            last_base -= dev->caps.reserved_qps_cnt[sort[i]];
            dev->caps.reserved_qps_base[sort[i]] = last_base;
            reserved_from_top +=
                dev->caps.reserved_qps_cnt[sort[i]];
        }

    }

    err = mlx4_bitmap_init_with_effective_max(&qp_table->bitmap,
        dev->caps.num_qps, (1 << 23) - 1,
        dev->caps.sqp_start + 8 + 2 * !!dev->caps.tunnel_qpn,
        dev->caps.num_qps - reserved_from_top);
    if (err)
        return err;

    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: ===== QP Allocation =====\n", dev->pdev->name ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t total_num_qps        \t%d\n", dev->caps.num_qps ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t reserved_from_bottom \t%d\n", dev->caps.sqp_start + 8 + 2 * !!dev->caps.tunnel_qpn ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t reserved_from_top    \t%d\n", reserved_from_top ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t special_qps_start    \t%d\n", dev->caps.sqp_start ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t tunnel_qpn           \t%d\n", dev->caps.tunnel_qpn ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t region_fw_base        \t%d\n", dev->caps.reserved_qps_base[MLX4_QP_REGION_FW] ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t region_fw_size        \t%d\n", dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW] ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t region_eth_base       \t%d\n", dev->caps.reserved_qps_base[MLX4_QP_REGION_ETH_ADDR] ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t region_eth_size       \t%d\n", dev->caps.reserved_qps_cnt[MLX4_QP_REGION_ETH_ADDR] ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t region_fc_base        \t%d\n", dev->caps.reserved_qps_base[MLX4_QP_REGION_FC_ADDR] ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t region_fc_size        \t%d\n", dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_ADDR] ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t region_fcx_base       \t%d\n", dev->caps.reserved_qps_base[MLX4_QP_REGION_FC_EXCH] ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("\t region_fcx_size       \t%d\n", dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_EXCH] ));
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("---------------------------------------------\n" ));

    return mlx4_CONF_SPECIAL_QP(dev, dev->caps.sqp_start);
}

void mlx4_cleanup_qp_table(struct mlx4_dev *dev)
{
    mlx4_CONF_SPECIAL_QP(dev, 0);
    RMV_RADIX_TREE(&dev->qp_table_tree);
    if (mlx4_is_slave(dev)) {
        mlx4_qp_release_range(dev, dev->caps.sqp_start, 8);
        return;
    }
    mlx4_bitmap_cleanup(&mlx4_priv(dev)->qp_table.bitmap);
}

int mlx4_qp_get_region(struct mlx4_dev *dev,
               enum qp_region region,
               int *base_qpn, int *cnt)
{
    if ((region < 0) || (region >= MLX4_NUM_QP_REGION))
        return -EINVAL;

    *base_qpn   = dev->caps.reserved_qps_base[region];
    *cnt        = dev->caps.reserved_qps_cnt[region];

    return 0;
}

int mlx4_qp_query(struct mlx4_dev *dev, struct mlx4_qp *qp,
          struct mlx4_qp_context *context)
{
    struct mlx4_cmd_mailbox *mailbox;
    int err;

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    err = mlx4_cmd_box(dev, 0, mailbox->dma.da, qp->qpn, 0,
               MLX4_CMD_QUERY_QP, MLX4_CMD_TIME_CLASS_A);
    if (!err)
        memcpy(context, (u8*)mailbox->buf + 8, sizeof *context);

    mlx4_free_cmd_mailbox(dev, mailbox);
    return err;
}

int mlx4_qp_to_ready(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
             struct mlx4_qp_context *context,
             struct mlx4_qp *qp, enum mlx4_qp_state *qp_state)
{
#define STATE_ARR_SIZE 4
    int err = 0;
    int i;
    enum mlx4_qp_state states[STATE_ARR_SIZE] = {
        MLX4_QP_STATE_RST,
        MLX4_QP_STATE_INIT,
        MLX4_QP_STATE_RTR,
        MLX4_QP_STATE_RTS
    };

    for (i = 0; i < STATE_ARR_SIZE - 1; i++) {
        context->flags &= cpu_to_be32(~(0xf << 28));
        context->flags |= cpu_to_be32(states[i + 1] << 28);
        err = mlx4_qp_modify(dev, mtt, states[i], states[i + 1],
                    context, (mlx4_qp_optpar)0, 0, qp, FALSE /* not RDMA QP - called only for VPI */ );
        if (err) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_QP,( "%s: Failed to bring qp to state:"
                      "%d with error: %d\n", dev->pdev->name,
                    states[i + 1], err));
            return err;
        }
        *qp_state = states[i + 1];
    }
    return 0;
}

