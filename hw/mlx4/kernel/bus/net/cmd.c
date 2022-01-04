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
#include <complib/cl_thread.h>
#include "mlx4.h"
#include "fw.h"
#include "en_port.h"
#include "stat.h"
#include <mlx4_debug.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "cmd.tmh"
#endif

#define CMD_POLL_TOKEN 0xffff
#define INBOX_MASK  0xffffffffffffff00ULL

enum {
    /* command completed successfully: */
    CMD_STAT_OK     = 0x00,
    /* Internal error (such as a bus error) occurred while processing command: */
    CMD_STAT_INTERNAL_ERR   = 0x01,
    /* Operation/command not supported or opcode modifier not supported: */
    CMD_STAT_BAD_OP     = 0x02,
    /* Parameter not supported or parameter out of range: */
    CMD_STAT_BAD_PARAM  = 0x03,
    /* System not enabled or bad system state: */
    CMD_STAT_BAD_SYS_STATE  = 0x04,
    /* Attempt to access reserved or unallocaterd resource: */
    CMD_STAT_BAD_RESOURCE   = 0x05,
    /* Requested resource is currently executing a command, or is otherwise busy: */
    CMD_STAT_RESOURCE_BUSY  = 0x06,
    /* Required capability exceeds device limits: */
    CMD_STAT_EXCEED_LIM = 0x08,
    /* Resource is not in the appropriate state or ownership: */
    CMD_STAT_BAD_RES_STATE  = 0x09,
    /* Index out of range: */
    CMD_STAT_BAD_INDEX  = 0x0a,
    /* FW image corrupted: */
    CMD_STAT_BAD_NVMEM  = 0x0b,
    /* Error in ICM mapping (e.g. not enough auxiliary ICM pages to execute command): */
    CMD_STAT_ICM_ERROR  = 0x0c,
    /* Attempt to modify a QP/EE which is not in the presumed state: */
    CMD_STAT_BAD_QP_STATE   = 0x10,
    /* Bad segment parameters (Address/Size): */
    CMD_STAT_BAD_SEG_PARAM  = 0x20,
    /* Memory Region has Memory Windows bound to: */
    CMD_STAT_REG_BOUND  = 0x21,
    /* HCA local attached memory not present: */
    CMD_STAT_LAM_NOT_PRE    = 0x22,
    /* Bad management packet (silently discarded): */
    CMD_STAT_BAD_PKT    = 0x30,
    /* More outstanding CQEs in CQ than new CQ size: */
    CMD_STAT_BAD_SIZE   = 0x40,
    /* Multi Function device support required: */
    CMD_STAT_MULTI_FUNC_REQ = 0x50,
    /* must be the last and have max value */
    CMD_STAT_SIZE       = CMD_STAT_MULTI_FUNC_REQ + 1
};

enum {
    HCR_IN_PARAM_OFFSET = 0x00,
    HCR_IN_MODIFIER_OFFSET  = 0x08,
    HCR_OUT_PARAM_OFFSET    = 0x0c,
    HCR_TOKEN_OFFSET    = 0x14,
    HCR_STATUS_OFFSET   = 0x18,

    HCR_OPMOD_SHIFT     = 12,
    HCR_T_BIT       = 21,
    HCR_E_BIT       = 22,
    HCR_GO_BIT      = 23
};

enum {
    GO_BIT_TIMEOUT_MSECS    = 10000
};

struct mlx4_cmd_context {
    struct completion   done;
    int         result;
    int         next;
    u64         out_param;
    u16         token;
    u8          fw_status;
};


void  debug_busy_wait(struct mlx4_dev *dev)
{
    if (g.busy_wait_behavior == RESUME_RUNNING) {
        return;
    }

    if (g.busy_wait_behavior == IMMIDIATE_BREAK) {
        DbgBreakPoint();
        return;
    }

    while (dev) {
        u32 wait_ms =2000; /* wait interval in msecs */
        cl_thread_suspend( wait_ms ); 
    }
}

static int mlx4_status_to_errno(u8 status) {
    static int trans_table[CMD_STAT_SIZE];
    static int filled = 0;

    if ( !filled ) {
        memset( (char*)trans_table, 0, sizeof(trans_table) );
        trans_table[CMD_STAT_INTERNAL_ERR]    = -EIO;
        trans_table[CMD_STAT_BAD_OP]      = -EPERM;
        trans_table[CMD_STAT_BAD_PARAM]   = -EINVAL;
        trans_table[CMD_STAT_BAD_SYS_STATE]  = -ENXIO;
        trans_table[CMD_STAT_BAD_RESOURCE]    = -EBADF;
        trans_table[CMD_STAT_RESOURCE_BUSY]  = -EBUSY;
        trans_table[CMD_STAT_EXCEED_LIM]      = -ENOMEM;
        trans_table[CMD_STAT_BAD_RES_STATE]  = -EBADF;
        trans_table[CMD_STAT_BAD_INDEX]   = -EBADF;
        trans_table[CMD_STAT_BAD_NVMEM]   = -EFAULT;
        trans_table[CMD_STAT_BAD_QP_STATE]   = -EINVAL;
        trans_table[CMD_STAT_BAD_SEG_PARAM]  = -EFAULT;
        trans_table[CMD_STAT_REG_BOUND]   = -EBUSY;
        trans_table[CMD_STAT_LAM_NOT_PRE]     = -EAGAIN;
        trans_table[CMD_STAT_BAD_PKT]     = -EINVAL;
        trans_table[CMD_STAT_BAD_SIZE]    = -ENOMEM;
        trans_table[CMD_STAT_MULTI_FUNC_REQ]  = -EACCES;
        filled = 1;
    }

    if (status >= ARRAY_SIZE(trans_table) ||
        (status != CMD_STAT_OK && trans_table[status] == 0))
        return -EIO;

    ASSERT((status == CMD_STAT_OK) || (status == CMD_STAT_MULTI_FUNC_REQ) || status == CMD_STAT_EXCEED_LIM);
    return trans_table[status];
}

static int comm_pending(struct mlx4_dev *dev)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    __be32 status = readl(&priv->mfunc.comm->slave_read);

    return (be32_to_cpu(status) >> 30) != priv->cmd.comm_toggle;
}


static void mlx4_comm_cmd_post(struct mlx4_dev *dev, u8 cmd, u16 param)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    u32 val;
    
    ASSERT(SLAVE_COMM_INFO(dev->pdev->pci_func).slaveIssued == 0);
    SLAVE_COMM_INFO(dev->pdev->pci_func).slaveIssued = 1;
    SLAVE_COMM_INFO(dev->pdev->pci_func).time = jiffies;

    /* Write command */
    if (cmd == MLX4_COMM_CMD_RESET)
        priv->cmd.comm_toggle = 0;
    else if (++priv->cmd.comm_toggle > 2)
        priv->cmd.comm_toggle = 1;
    val = param | (cmd << 16) | (priv->cmd.comm_toggle << 30);
    __raw_writel((__force u32) cpu_to_be32(val), &priv->mfunc.comm->slave_write);
    wmb();
    if ( g.mode_flags & MLX4_MODE_COMM_TSTAMP ) 
        priv->mfunc.slave.issue_stamp = (u32) jiffies;
    // debug
    priv->mfunc.slave.last_cmd = val;
    priv->mfunc.slave.p_thread = KeGetCurrentThread();

    if (cmd != MLX4_COMM_CMD_RESET) {
        priv->mfunc.slave.cmd_num++;        
        SLAVE_COMM_INFO(dev->pdev->pci_func).cmd_count = priv->mfunc.slave.cmd_num;
    }
}

static int mlx4_comm_cmd_poll(struct mlx4_dev *dev, u8 cmd, u16 param, unsigned long timeout)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    u64 end, start, ct;
    int err = 0;

    /* First, verify that the master reports correct status */
    if (comm_pending(dev)) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Communication channel is not idle\n", dev->pdev->name));
        ASSERT(FALSE);
        return -EAGAIN;
    }

    down(&priv->cmd.poll_sem);

    mlx4_comm_cmd_post(dev, cmd, param);
    
    start = jiffies;
    end = timeout + start;
    while (comm_pending(dev)) {
        ct = jiffies;
        if ((ct-start) > 3000) {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Comm channel takes %d msecs, thread %p\n", 
                dev->pdev->name, (int)(ct-start),  priv->mfunc.slave.p_thread ));
        }
            
        if (!time_before(jiffies, end))
            break;
        cond_resched();
    }

    if (comm_pending(dev)) {
        if ( g.mode_flags & MLX4_MODE_COMM_TSTAMP ) 
            priv->mfunc.slave.to_stamp = (u32)jiffies;
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Comm channel timed out after %d msecs: last_cmd %02x, toggle %d, cmd_num %d, thread %p\n", 
            dev->pdev->name, timeout, cmd, priv->cmd.comm_toggle,
            priv->mfunc.slave.cmd_num, priv->mfunc.slave.p_thread ));
        ASSERT(FALSE);        
        debug_busy_wait(dev);
        err = -ETIMEDOUT;
    }

    ASSERT(SLAVE_COMM_INFO(dev->pdev->pci_func).slaveIssued == 1);
    SLAVE_COMM_INFO(dev->pdev->pci_func).slaveIssued = 0;

    up(&priv->cmd.poll_sem);
    return err;
}

#define DEV_ASSERT


static int mlx4_comm_cmd_wait(struct mlx4_dev *dev, u8 op, u16 param, unsigned long timeout)
{
    struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
    struct mlx4_cmd_context *context;
    int err = 0;
    
    down(&cmd->event_sem);
    if ( dev->flags & MLX4_FLAG_RESET_DRIVER ) {
        err = -EBUSY;
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Command %02x is skipped because the card is stuck \n", 
            dev->pdev->name, op));
        goto exit;
    }
    spin_lock(&cmd->context_lock);
    BUG_ON(cmd->free_head < 0);
    context = &cmd->context[cmd->free_head];
    context->token += cmd->token_mask + 1;
    cmd->free_head = context->next;
    spin_unlock(&cmd->context_lock);
    
    init_completion(&context->done);
    
    mlx4_comm_cmd_post(dev, op, param);
    
    if (wait_for_completion_timeout(&context->done, jiffies_to_usecs(timeout))) {
        dev->pdev->p_stat->cmd_n_events_to++;
        if (!context->done.done) {

            /* report failure */
            err = -EBUSY;
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Command %02x completed with timeout after %d msecs \n", dev->pdev->name, op, timeout));
            ASSERT(FALSE);
            debug_busy_wait(dev);

            if ( dev->flags & MLX4_FLAG_UNLOAD)
                goto out;

            //BUGBUG: do we want to reset the slave
        }
        else {
            err = -EFAULT;            
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Unexpected end of waiting for a command \n", dev->pdev->name));
            ASSERT(0);
        }
    }
        
out:
    ASSERT(SLAVE_COMM_INFO(dev->pdev->pci_func).slaveIssued == 1);
    SLAVE_COMM_INFO(dev->pdev->pci_func).slaveIssued = 0;

    spin_lock(&cmd->context_lock);
    context->next = cmd->free_head;
    cmd->free_head = (int)(context - cmd->context);
    spin_unlock(&cmd->context_lock);

exit:   
    up(&cmd->event_sem);
    return err;
}


int mlx4_comm_cmd(struct mlx4_dev *dev, u8 cmd, u16 param, unsigned long timeout)
{    
    if (mlx4_priv(dev)->cmd.use_events)
        return mlx4_comm_cmd_wait(dev, cmd, param, timeout);

    return mlx4_comm_cmd_poll(dev, cmd, param, timeout);
}

static int cmd_pending(struct mlx4_dev *dev, u32 *p_status)
{
    *p_status = readl(mlx4_priv(dev)->cmd.hcr + HCR_STATUS_OFFSET);

    if (*p_status == CMD_STAT_MULTI_FUNC_REQ)
        return 0;

    return (*p_status & swab32(1 << HCR_GO_BIT)) ||
        (mlx4_priv(dev)->cmd.toggle ==
        !!(*p_status & swab32(1 << HCR_T_BIT)));
}

static int mlx4_cmd_post(struct mlx4_dev *dev, u64 in_param, u64 out_param,
             u32 in_modifier, u8 op_modifier, u16 op, u16 token,
             int event)
{
    struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
    u32 __iomem *hcr = (u32 __iomem *)cmd->hcr;
    int ret = -EAGAIN;
    u64 end;
    u32 hcr_status;

    mutex_lock(&cmd->hcr_mutex);

    end = jiffies;
    if (event)
        end += GO_BIT_TIMEOUT_MSECS;

    while (cmd_pending(dev, &hcr_status)) {
        if (time_after_eq(jiffies, end)) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                      ("%s: Failed to post command %02x during %d msecs, hcr_status %#x, toggle %#x\n",                      
                       dev->pdev->name, op, GO_BIT_TIMEOUT_MSECS, 
                       hcr_status, mlx4_priv(dev)->cmd.toggle ));
                      
            goto out;
        }
        cond_resched();
    }

    /*
     * We use writel (instead of something like memcpy_toio)
     * because writes of less than 32 bits to the HCR don't work
     * (and some architectures such as ia64 implement memcpy_toio
     * in terms of writeb).
     */
    __raw_writel((__force u32) cpu_to_be32(in_param >> 32),       hcr + 0);
    __raw_writel((__force u32) cpu_to_be32(in_param & 0xfffffffful),  hcr + 1);
    __raw_writel((__force u32) cpu_to_be32(in_modifier),          hcr + 2);
    __raw_writel((__force u32) cpu_to_be32(out_param >> 32),      hcr + 3);
    __raw_writel((__force u32) cpu_to_be32(out_param & 0xfffffffful), hcr + 4);
    __raw_writel((__force u32) cpu_to_be32(token << 16),          hcr + 5);

    /* __raw_writel may not order writes. */
    wmb();

    __raw_writel((__force u32) cpu_to_be32((1 << HCR_GO_BIT)        |
                           (cmd->toggle << HCR_T_BIT)   |
                           (event ? (1 << HCR_E_BIT) : 0)   |
                           (op_modifier << HCR_OPMOD_SHIFT) |
                           op),           hcr + 6);

    /*
     * Make sure that our HCR writes don't get mixed in with
     * writes from another CPU starting a FW command.
     */
    mmiowb();

    cmd->toggle = cmd->toggle ^ 1;
    ret = 0;

out:
    mutex_unlock(&cmd->hcr_mutex);
    return ret;
}

static int mlx4_slave_cmd(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
             int out_is_imm, u32 in_modifier, u8 op_modifier,
             u16 op, unsigned long timeout)
{
    struct mlx4_priv *priv = mlx4_priv(dev); 
    struct mlx4_vhcr *vhcr = priv->mfunc.vhcr;
    int ret;

    MLX4_PRINT( TRACE_LEVEL_VERBOSE, MLX4_DBG_COMM,
        ( "%s: Issuing command with op %#hx, in_param %#I64x, timeout %d.\n", 
        dev->pdev->name, op, in_param, timeout) );

    down(&priv->cmd.slave_sem);
    vhcr->in_param = in_param;
    vhcr->out_param = out_param ? *out_param : 0;
    vhcr->in_modifier = in_modifier;
    vhcr->timeout = timeout;
    vhcr->op = op;
    vhcr->token = CMD_POLL_TOKEN;
    vhcr->op_modifier = op_modifier;
    vhcr->err = 0;
    ret = mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR_POST, 0, MLX4_COMM_TIME + timeout);
    if (!ret) {
        if (out_is_imm && out_param != NULL)
            *out_param = vhcr->out_param;
        ret = vhcr->err;
    }
    up(&priv->cmd.slave_sem);
    return ret;
}

static int mlx4_cmd_poll(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
             int out_is_imm, u32 in_modifier, u8 op_modifier,
             u16 op, unsigned long timeout)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    u8 __iomem *hcr = priv->cmd.hcr;
    int err = 0;
    u64 end;
    u8 status = 0xff;   /* means "unknown" */
    long do_reset;
    u32 hcr_status;

    down(&priv->cmd.poll_sem);

    err = mlx4_cmd_post(dev, in_param, out_param ? *out_param : 0,
                in_modifier, op_modifier, op, CMD_POLL_TOKEN, 0);
    if (err)
        goto out;

    end = timeout + jiffies;
    while (cmd_pending(dev, &hcr_status) && time_before(jiffies, end))
        cond_resched();

    if (cmd_pending(dev, &hcr_status)) {
        err = -ETIMEDOUT;
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: Command %02x: timeout after %d msecs, hcr_status %#x, toggle %#x \n", 
            dev->pdev->name, op, timeout, hcr_status, mlx4_priv(dev)->cmd.toggle));
        st_et_event("%s: Command %02x: timeout after %d msecs, hcr_status %#x, toggle %#x \n", 
            dev->pdev->name, op, timeout, hcr_status, mlx4_priv(dev)->cmd.toggle);

        debug_busy_wait(dev);

        if (( dev->flags & MLX4_FLAG_UNLOAD) || !(g.mode_flags & MLX4_MODE_RESET_AFTER_CMD_TO))
            goto out;

        do_reset = InterlockedCompareExchange(&dev->reset_pending, 1, 0);
        if (!do_reset) {
            NTSTATUS status1;

            if (!mlx4_is_slave(dev)) {
                status1 = mlx4_reset(dev);
                if ( !NT_SUCCESS( status1 ) ) {
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                        ("%s: Failed to reset HCA, aborting.(status %#x)\n",dev->pdev->name, status1));
                }
                else
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: HCA has been reset.\n", dev->pdev->name));
            }
            
            dev->flags |= MLX4_FLAG_RESET_DRIVER;   // bar the device
        }

        if (dev->pdev->ib_dev) {
			st_et_event("Bus: mlx4_cmd_poll sent IB_EVENT_RESET_DRIVER\n");
            mlx4_dispatch_reset_event(dev->pdev->ib_dev, IB_EVENT_RESET_DRIVER);
       	}
        goto out;
    }

    if (out_is_imm && out_param != NULL)
        *out_param =
            (u64) be32_to_cpu((__force __be32)
                      __raw_readl(hcr + HCR_OUT_PARAM_OFFSET)) << 32 |
            (u64) be32_to_cpu((__force __be32)
                      __raw_readl(hcr + HCR_OUT_PARAM_OFFSET + 4));

    status = (u8)(be32_to_cpu((__force __be32)__raw_readl(hcr + HCR_STATUS_OFFSET)) >> 24);
    err = mlx4_status_to_errno(status);

out:
    if (status && status != CMD_STAT_MULTI_FUNC_REQ) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Command failed: op %#hx, status %#02x, errno %d.\n", 
                    dev->pdev->name, op, status, err));
        st_et_event("%s: Command failed: op %#hx, status %#02x, errno %d.\n", 
                    dev->pdev->name, op, status, err); 
    }
    up(&priv->cmd.poll_sem);
    return err;
}

void mlx4_cmd_event(struct mlx4_dev *dev, u16 token, u8 status, u64 out_param)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_cmd_context *context =
        &priv->cmd.context[token & priv->cmd.token_mask];

    /* previously timed out command completing at long last */
    if (token != context->token) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Command token %x, expected %x", dev->pdev->name, token, context->token));
        st_et_event("%s: Command token %x, expected %x", dev->pdev->name, token, context->token); 
        return;
    }

    context->fw_status = status;
    context->result    = mlx4_status_to_errno(status);
    context->out_param = out_param;

    complete(&context->done);
}

static int mlx4_cmd_wait(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
             int out_is_imm, u32 in_modifier, u8 op_modifier,
             u16 op, unsigned long timeout)
{
    struct mlx4_cmd *cmd = &mlx4_priv(dev)->cmd;
    struct mlx4_cmd_context *context;
    int err = 0;
    u64 out_prm = out_param ? *out_param : 0;
    long do_reset;
    u8 status = 0xff;   /* means "unknown" */

    down(&cmd->event_sem);
    if ( dev->flags & MLX4_FLAG_RESET_DRIVER ) {
        err = -EBUSY;
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Command %02x is skipped because the card is stuck \n", 
            dev->pdev->name, op));
		st_et_event("%s: Command %02x is skipped because the card is stuck \n", 
            dev->pdev->name, op);
        goto exit;
    }
    spin_lock(&cmd->context_lock);
    BUG_ON(cmd->free_head < 0);
    context = &cmd->context[cmd->free_head];
    context->token += cmd->token_mask + 1;
    cmd->free_head = context->next;
    spin_unlock(&cmd->context_lock);

    init_completion(&context->done);

    err = mlx4_cmd_post(dev, in_param, out_prm,
        in_modifier, op_modifier, op, context->token, 1);
    if (err)
        goto out;

    if (wait_for_completion_timeout(&context->done, jiffies_to_usecs(timeout))) {
        dev->pdev->p_stat->cmd_n_events_to++;
        if (!context->done.done) {

            /* report failure */
            err = -EBUSY;
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Command %02x completed with timeout after %d msecs \n", 
                        dev->pdev->name, op, timeout));
            st_et_event("%s: Command %02x completed with timeout after %d msecs \n",dev->pdev->name, op, timeout);
            ASSERT(FALSE);
            debug_busy_wait(dev);

            if (( dev->flags & MLX4_FLAG_UNLOAD) || !(g.mode_flags & MLX4_MODE_RESET_AFTER_CMD_TO))
                goto out;
            
            do_reset = InterlockedCompareExchange(&dev->reset_pending, 1, 0);
            if (!do_reset) {
                NTSTATUS status1;
                
                if (!mlx4_is_slave(dev)) {
                    status1 = mlx4_reset(dev);
                    if ( !NT_SUCCESS( status1 ) ) {
                        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed to reset HCA, aborting.(status %#x)\n", 
                                    dev->pdev->name, status1));
                    }
                    else
                        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: HCA has been reset.\n", dev->pdev->name));
                }
                
                dev->flags |= MLX4_FLAG_RESET_DRIVER;   // bar the device
            }

            /* try to solve the problem */
            if (dev->pdev->ib_dev) {
                st_et_event("Bus: mlx4_cmd_wait sent IB_EVENT_RESET_DRIVER\n");
                mlx4_dispatch_reset_event(dev->pdev->ib_dev, IB_EVENT_RESET_DRIVER);
           	}
        }
        else {
            err = -EFAULT;
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Unexpected end of waiting for a command \n", dev->pdev->name));
            ASSERT(0);
        }
    }
    else {
        err = context->result;
        status = context->fw_status;
        dev->pdev->p_stat->cmd_n_events++;
    }

    
    
    if (err)
        goto out;

    if (out_is_imm && out_param != NULL)
        *out_param = context->out_param;

out:
    spin_lock(&cmd->context_lock);
    context->next = cmd->free_head;
    cmd->free_head = (int)(context - cmd->context);
    spin_unlock(&cmd->context_lock);
    if (status && status != CMD_STAT_MULTI_FUNC_REQ) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Command failed: op %#hx, status %#02x, errno %d, token %#hx.\n",
            dev->pdev->name, op, status, err, context->token));
        st_et_event("%s: Command failed: op %#hx, status %#02x, errno %d, token %#hx.\n",
            dev->pdev->name, op, status, err, context->token);
    }

exit:   
    up(&cmd->event_sem);
    return err;
}

static char *__print_opcode(int opcode)
{
    char *str = NULL;
    switch (opcode) {
        case MLX4_CMD_SYS_EN    : str = "MLX4_CMD_SYS_EN    "; break;
        case MLX4_CMD_SYS_DIS: str = "MLX4_CMD_SYS_DIS"; break;
        case MLX4_CMD_MAP_FA    : str = "MLX4_CMD_MAP_FA    "; break;
        case MLX4_CMD_UNMAP_FA: str = "MLX4_CMD_UNMAP_FA"; break;
        case MLX4_CMD_RUN_FW    : str = "MLX4_CMD_RUN_FW    "; break;
        case MLX4_CMD_MOD_STAT_CFG: str = "MLX4_CMD_MOD_STAT_CFG"; break;
        case MLX4_CMD_QUERY_DEV_CAP: str = "MLX4_CMD_QUERY_DEV_CAP"; break;
        case MLX4_CMD_QUERY_FW: str = "MLX4_CMD_QUERY_FW"; break;
        case MLX4_CMD_ENABLE_LAM: str = "MLX4_CMD_ENABLE_LAM"; break;
        case MLX4_CMD_DISABLE_LAM: str = "MLX4_CMD_DISABLE_LAM"; break;
        case MLX4_CMD_QUERY_DDR: str = "MLX4_CMD_QUERY_DDR"; break;
        case MLX4_CMD_QUERY_ADAPTER: str = "MLX4_CMD_QUERY_ADAPTER"; break;
        case MLX4_CMD_INIT_HCA: str = "MLX4_CMD_INIT_HCA"; break;
        case MLX4_CMD_CLOSE_HCA: str = "MLX4_CMD_CLOSE_HCA"; break;
        case MLX4_CMD_INIT_PORT: str = "MLX4_CMD_INIT_PORT"; break;
        case MLX4_CMD_CLOSE_PORT: str = "MLX4_CMD_CLOSE_PORT"; break;
        case MLX4_CMD_QUERY_HCA: str = "MLX4_CMD_QUERY_HCA"; break;
        case MLX4_CMD_QUERY_PORT: str = "MLX4_CMD_QUERY_PORT"; break;
        case MLX4_CMD_SET_PORT: str = "MLX4_CMD_SET_PORT"; break;
        case MLX4_CMD_ACCESS_DDR: str = "MLX4_CMD_ACCESS_DDR"; break;
        case MLX4_CMD_MAP_ICM: str = "MLX4_CMD_MAP_ICM"; break;
        case MLX4_CMD_UNMAP_ICM: str = "MLX4_CMD_UNMAP_ICM"; break;
        case MLX4_CMD_MAP_ICM_AUX: str = "MLX4_CMD_MAP_ICM_AUX"; break;
        case MLX4_CMD_UNMAP_ICM_AUX: str = "MLX4_CMD_UNMAP_ICM_AUX"; break;
        case MLX4_CMD_SET_ICM_SIZE: str = "MLX4_CMD_SET_ICM_SIZE"; break;
        case MLX4_CMD_SW2HW_MPT: str = "MLX4_CMD_SW2HW_MPT"; break;
        case MLX4_CMD_QUERY_MPT: str = "MLX4_CMD_QUERY_MPT"; break;
        case MLX4_CMD_HW2SW_MPT: str = "MLX4_CMD_HW2SW_MPT"; break;
        case MLX4_CMD_READ_MTT: str = "MLX4_CMD_READ_MTT"; break;
        case MLX4_CMD_WRITE_MTT: str = "MLX4_CMD_WRITE_MTT"; break;
        case MLX4_CMD_SYNC_TPT: str = "MLX4_CMD_SYNC_TPT"; break;
        case MLX4_CMD_MAP_EQ    : str = "MLX4_CMD_MAP_EQ    "; break;
        case MLX4_CMD_SW2HW_EQ: str = "MLX4_CMD_SW2HW_EQ"; break;
        case MLX4_CMD_HW2SW_EQ: str = "MLX4_CMD_HW2SW_EQ"; break;
        case MLX4_CMD_QUERY_EQ: str = "MLX4_CMD_QUERY_EQ"; break;
        case MLX4_CMD_SW2HW_CQ: str = "MLX4_CMD_SW2HW_CQ"; break;
        case MLX4_CMD_HW2SW_CQ: str = "MLX4_CMD_HW2SW_CQ"; break;
        case MLX4_CMD_QUERY_CQ: str = "MLX4_CMD_QUERY_CQ"; break;
        case MLX4_CMD_MODIFY_CQ: str = "MLX4_CMD_MODIFY_CQ"; break;
        case MLX4_CMD_SW2HW_SRQ: str = "MLX4_CMD_SW2HW_SRQ"; break;
        case MLX4_CMD_HW2SW_SRQ: str = "MLX4_CMD_HW2SW_SRQ"; break;
        case MLX4_CMD_QUERY_SRQ: str = "MLX4_CMD_QUERY_SRQ"; break;
        case MLX4_CMD_ARM_SRQ: str = "MLX4_CMD_ARM_SRQ"; break;
        case MLX4_CMD_RST2INIT_QP: str = "MLX4_CMD_RST2INIT_QP"; break;
        case MLX4_CMD_INIT2RTR_QP: str = "MLX4_CMD_INIT2RTR_QP"; break;
        case MLX4_CMD_RTR2RTS_QP: str = "MLX4_CMD_RTR2RTS_QP"; break;
        case MLX4_CMD_RTS2RTS_QP: str = "MLX4_CMD_RTS2RTS_QP"; break;
        case MLX4_CMD_SQERR2RTS_QP: str = "MLX4_CMD_SQERR2RTS_QP"; break;
        case MLX4_CMD_2ERR_QP: str = "MLX4_CMD_2ERR_QP"; break;
        case MLX4_CMD_RTS2SQD_QP: str = "MLX4_CMD_RTS2SQD_QP"; break;
        case MLX4_CMD_SQD2SQD_QP: str = "MLX4_CMD_SQD2SQD_QP"; break;
        case MLX4_CMD_SQD2RTS_QP: str = "MLX4_CMD_SQD2RTS_QP"; break;
        case MLX4_CMD_2RST_QP: str = "MLX4_CMD_2RST_QP"; break;
        case MLX4_CMD_QUERY_QP: str = "MLX4_CMD_QUERY_QP"; break;
        case MLX4_CMD_INIT2INIT_QP: str = "MLX4_CMD_INIT2INIT_QP"; break;
        case MLX4_CMD_SUSPEND_QP: str = "MLX4_CMD_SUSPEND_QP"; break;
        case MLX4_CMD_UNSUSPEND_QP: str = "MLX4_CMD_UNSUSPEND_QP"; break;
        case MLX4_CMD_CONF_SPECIAL_QP: str = "MLX4_CMD_CONF_SPECIAL_QP"; break;
        case MLX4_CMD_MAD_IFC: str = "MLX4_CMD_MAD_IFC"; break;
        case MLX4_CMD_READ_MCG: str = "MLX4_CMD_READ_MCG"; break;
        case MLX4_CMD_WRITE_MCG: str = "MLX4_CMD_WRITE_MCG"; break;
        case MLX4_CMD_MGID_HASH: str = "MLX4_CMD_MGID_HASH"; break;
        case MLX4_CMD_DIAG_RPRT: str = "MLX4_CMD_DIAG_RPRT"; break;
        case MLX4_CMD_NOP   : str = "MLX4_CMD_NOP   "; break;
        case MLX4_CMD_QUERY_DEBUG_MSG: str = "MLX4_CMD_QUERY_DEBUG_MSG"; break;
        case MLX4_CMD_SET_DEBUG_MSG: str = "MLX4_CMD_SET_DEBUG_MSG"; break;
    }
    return str;
}

int __mlx4_cmd(struct mlx4_dev *dev, u64 in_param, u64 *out_param,
           int out_is_imm, u32 in_modifier, u8 op_modifier,
           u16 op, unsigned long timeout)
{
	static NTSTATUS status = STATUS_SUCCESS;

	static int  g_cur_cmd_num = 0;

#if 0
    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_CMD,("%s: op %s, ev %d, in_param %#I64x, in_param %#I64x, out_is_imm %d, in_modifier %#x, op_modifier %d\n",
        dev->pdev->name, 
        __print_opcode(op), mlx4_priv(dev)->cmd.use_events, in_param, out_param, 
        out_is_imm, in_modifier, (int)op_modifier));

#endif

#if DBG

	g_cur_cmd_num++;

	/* If command number  registry exists, fail (don't execute) all the commands which are equal to it or higher */
	if (g.cmd_num_to_fail > 0 && g_cur_cmd_num >= g.cmd_num_to_fail){
	
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			("Failing command test: cmd to fail = %d, cur cmd = %d, \n", g.cmd_num_to_fail, g_cur_cmd_num));
		
		return -EFAULT;	
	}
#endif

    if ( mlx4_is_barred(dev) )
        return -EFAULT;
    
    if (mlx4_is_slave(dev))
        return mlx4_slave_cmd(dev, in_param, out_param, out_is_imm,
                     in_modifier, op_modifier, op, timeout);

    if (mlx4_priv(dev)->cmd.use_events)
        return mlx4_cmd_wait(dev, in_param, out_param, out_is_imm,
                     in_modifier, op_modifier, op, timeout);
    else
        return mlx4_cmd_poll(dev, in_param, out_param, out_is_imm,
                     in_modifier, op_modifier, op, timeout);
}

static int mlx4_ARM_COMM_CHANNEL(struct mlx4_dev *dev)
{
    return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_ARM_COMM_CHANNEL, MLX4_CMD_TIME_CLASS_B);
}

int mlx4_GEN_EQE(struct mlx4_dev* dev, int slave, struct mlx4_eqe* eqe)
{
    struct mlx4_priv* priv = mlx4_priv(dev);
    struct slave_event_eq_info* event_eq = &priv->mfunc.master.slave_state[slave].event_eq;
    struct mlx4_cmd_mailbox *mailbox;
    u32 in_modifier = 0;
    int err;

    ASSERT(mlx4_is_master(dev));
    ASSERT(slave <= 0xFF);

    if (!event_eq->f_use_int)
        return 0;
    
    if ((event_eq->event_type & eqe->type) == 0)
        return 0;

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    if (eqe->type == MLX4_EVENT_TYPE_CMD) {
        ++event_eq->token;
        eqe->event.cmd.token = cpu_to_be16(event_eq->token);
    }
    
    memcpy(mailbox->buf, (u8*)eqe, 28);

    in_modifier = (slave & 0xFF) | ((event_eq->eqn & 0xFF) << 16);

    err = mlx4_cmd(dev, mailbox->dma.da, in_modifier, 0, 
                            MLX4_CMD_GEN_EQE, MLX4_CMD_TIME_CLASS_B);
    
    mlx4_free_cmd_mailbox(dev, mailbox);
    return err;
}


static int mlx4_ACCESS_MEM(struct mlx4_dev *dev, u64 master_addr,
               int slave, u64 slave_addr,
               int size, int is_read)
{
    u64 in_param;
    u64 out_param;

    if ((slave_addr & 0xfff) | (master_addr & 0xfff) |
        (slave & ~0x7f) | (size & 0xff)) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: Bad access mem params - slave_addr:0x%I64x master_addr:0x%I64x slave:%d size:%d\n",
            dev->pdev->name, slave_addr, master_addr, slave, size));
        
        return -EINVAL;
    }

    if (is_read) {
        in_param = (u64) slave | slave_addr;
        out_param = (u64) dev->caps.function | master_addr;        
    } else {
        in_param = (u64) dev->caps.function | master_addr;
        out_param = (u64) slave | slave_addr;
    }

    return mlx4_cmd_imm(dev, in_param, &out_param, size, 0,
                       MLX4_CMD_ACCESS_MEM,
                       MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_RESOURCE_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
                               struct mlx4_cmd_mailbox *inbox,
                               struct mlx4_cmd_mailbox *outbox)
{
    u32 param1 = *((u32 *) &vhcr->in_param);
    u32 param2 = *(((u32 *) &vhcr->in_param) + 1);
    int ret;
    int rt_res;
    u8 vep_num = mlx4_priv(dev)->mfunc.master.slave_state[slave].vep_num;
    u32 res_bit_mask = 0 ;
    int res_id ;
    struct slave_register_mac_and_vlan* p_slave_in = NULL;

    UNUSED_PARAM(inbox);
    UNUSED_PARAM(outbox);

#if 0
    char *res[] = {"QP", "CQ", "SRQ", "MPT", "MTT"};
    mlx4_warn(dev, "%s: resource wrapper - %s (mode: %s) type:%s param1:%d param2:%d\n",
            dev->pdev->name, 
            vhcr->op == MLX4_CMD_ALLOC_RES ? "allocate" : "free",
            vhcr->op_modifier == ICM_RESERVE ? "reserve" :
                (vhcr->op_modifier == ICM_ALLOC ? "alloc" : "reserve+alloc"),
            res[vhcr->in_modifier], param1, param2);
#endif

    vhcr->err = 0;
    switch (vhcr->in_modifier) {
    case RES_QP:
        switch (vhcr->op_modifier) {
        case ICM_RESERVE:
            if (vhcr->op == MLX4_CMD_ALLOC_RES) {
                vhcr->err = mlx4_qp_reserve_range(dev, param1, param2, &ret);
                if (!vhcr->err) {
                    vhcr->out_param = ret;
                    rt_res = mlx4_add_range_resource_for_slave(dev, RES_QP, slave, ret, param1);
                    if (rt_res) {
                        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                        mlx4_qp_release_range(dev, ret, param1);
                        return rt_res;
                    }
                }
            } else {
                mlx4_qp_release_range(dev, param1, param2);
                mlx4_delete_range_resource_for_slave(dev, RES_QP, slave, param1, param2);
            }
            break;
        case ICM_ALLOC:
            if (vhcr->op == MLX4_CMD_ALLOC_RES) {
                vhcr->err = mlx4_qp_alloc_icm(dev, param1);
                if (!vhcr->err) {
                    rt_res = mlx4_add_resource_for_slave(dev, RES_QP, slave, param1, RES_ALLOCATED_AFTER_RESERVATION);
                    if (rt_res) {
                        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                        mlx4_qp_free_icm(dev, param1);
                        return rt_res; 
                        }
                    }
                 
                } else {
                    mlx4_qp_free_icm(dev, param1);
                    mlx4_delete_resource_for_slave(dev, RES_QP, slave, param1);
            }
            break;
        default:
            vhcr->err = -EINVAL;
        }
        break;
    case RES_CQ:
        if (vhcr->op == MLX4_CMD_ALLOC_RES) {
            vhcr->err = mlx4_cq_alloc_icm(dev, &ret);
            if (!vhcr->err) {
                vhcr->out_param = ret;
                rt_res = mlx4_add_resource_for_slave(dev, RES_CQ, slave, ret, RES_INIT);
                if (rt_res) {
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                    mlx4_cq_free_icm(dev, ret);
                    return rt_res;
                }
            }
        } else {
            mlx4_cq_free_icm(dev, param1);
            mlx4_delete_resource_for_slave(dev, RES_CQ, slave, param1);
        }
        break;
    case RES_SRQ:
        if (vhcr->op == MLX4_CMD_ALLOC_RES) {
            vhcr->err = mlx4_srq_alloc_icm(dev, &ret);
            if (!vhcr->err) {
                vhcr->out_param = ret;
                rt_res = mlx4_add_resource_for_slave(dev, RES_SRQ, slave, ret, RES_INIT); 
                if (rt_res) {
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                    mlx4_srq_free_icm(dev, ret);
                    return rt_res;
                }
            }
        } else {
            mlx4_srq_free_icm(dev, param1);
            mlx4_delete_resource_for_slave(dev, RES_SRQ, slave, param1);
        }
        break;
    case RES_MPT:
        switch (vhcr->op_modifier) {
        case ICM_RESERVE:
            if (vhcr->op == MLX4_CMD_ALLOC_RES) {
                ret = mlx4_mr_reserve(dev);
                if (ret == -1)
                    vhcr->err = -ENOMEM;
                else {
                    res_bit_mask = calculate_bitmap_mask(dev, RES_MPT);
                    vhcr->out_param = ret;
                    res_id = ret & res_bit_mask;
                    rt_res = mlx4_add_resource_for_slave(dev, RES_MPT, slave, res_id, RES_INIT);
                    if (rt_res) {
                        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                        mlx4_mr_release(dev, ret);
                        return rt_res; 
                    } 
                }
            } else {
                mlx4_mr_release(dev, param1);
                res_bit_mask = calculate_bitmap_mask(dev, RES_MPT);
                res_id = param1 & res_bit_mask ;
                mlx4_delete_resource_for_slave(dev, RES_MPT, slave, res_id);
            }
            break;
        case ICM_ALLOC:
            if (vhcr->op == MLX4_CMD_ALLOC_RES) {
                vhcr->err = mlx4_mr_alloc_icm(dev, param1);
                if (!vhcr->err)  {
                    res_bit_mask = calculate_bitmap_mask(dev, RES_MPT);
                    res_id = param1 & res_bit_mask ;
                    rt_res = mlx4_add_resource_for_slave(dev, RES_MPT, slave, 
                                    res_id, RES_ALLOCATED_AFTER_RESERVATION);
                    if (rt_res) {
                        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                        mlx4_mr_free_icm(dev, param1);
                        return rt_res;
                    }
                }
            }
            else {
                mlx4_mr_free_icm(dev, param1);
                res_bit_mask = calculate_bitmap_mask(dev, RES_MPT);
                res_id = param1 & res_bit_mask ;
                mlx4_delete_resource_for_slave(dev, RES_MPT, slave, res_id);                
            }
            break;
        default:
            vhcr->err = -EINVAL;
        }
        break;
    case RES_MTT:
        if (vhcr->op == MLX4_CMD_ALLOC_RES) {
            ret = mlx4_alloc_mtt_range(dev, param1 /* order */);
            if (ret == -1) {
                vhcr->err = -ENOMEM;
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_alloc_mtt_range failed, ret: %d for slave: %d", ret, slave));
            }
            else {
                vhcr->out_param = ret;
                rt_res = mlx4_add_mtt_resource_for_slave(dev, slave, ret, RES_INIT, param1); 
                if (rt_res) {
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                    mlx4_free_mtt_range(dev, ret /* first */, param1 /* order */);
                    return rt_res;
                }                
            }
        } else {
            mlx4_free_mtt_range(dev, param1 /* first */, param2 /* order */);
            mlx4_delete_resource_for_slave(dev, RES_MTT, slave, param1);
        }
        break;
    case RES_MAC_AND_VLAN:
        switch (vhcr->op) {
        case MLX4_CMD_ALLOC_RES:
            p_slave_in = (struct slave_register_mac_and_vlan*)(ULONG_PTR) vhcr->in_param;
            p_slave_in->mac |= (u64) (vep_num) << 48;
            ret = mlx4_register_mac_and_vlan(dev, vhcr->op_modifier,
                        p_slave_in->mac, p_slave_in->check_vlan, p_slave_in->vlan_valid, p_slave_in->vlan, p_slave_in->qpn, true, p_slave_in->reserve);
            vhcr->err = ret; 
            if (!ret) {
                rt_res = mlx4_add_resource_for_slave(dev, RES_MAC_AND_VLAN, slave, *p_slave_in->qpn, RES_INIT);
                if (rt_res) {
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                    mlx4_unregister_mac_and_vlan(dev, vhcr->op_modifier, ret, p_slave_in->reserve);
                    return rt_res;
                }
                rt_res = mlx4_add_port_to_tracked_mac(dev, *p_slave_in->qpn, vhcr->op_modifier);
                if (rt_res) {
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_port_to_tracked_mac failed, ret: %d for slave: %d", rt_res, slave));
                    mlx4_unregister_mac_and_vlan(dev, vhcr->op_modifier, ret, p_slave_in->reserve);
                    return rt_res;
                } 
                if(p_slave_in->reserve) {
                    /*When asking for mac the master already reserved that qp, the slave needs to allocate it */
                    rt_res = mlx4_add_resource_for_slave(dev, RES_QP, slave, *p_slave_in->qpn, RES_ALLOCATED_WITH_MASTER_RESERVATION);
                    if (rt_res) {
                        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("mlx4_add_resource_for_slave failed, ret: %d for slave: %d", rt_res, slave));
                        mlx4_unregister_mac_and_vlan(dev, vhcr->op_modifier, ret, p_slave_in->reserve);
                        return rt_res;
                    }                
                }
           }            
            break;
        case MLX4_CMD_FREE_RES:
            p_slave_in = (struct slave_register_mac_and_vlan*)(ULONG_PTR) vhcr->in_param;
            *p_slave_in->qpn |= (u64) (vep_num) << 48;
            mlx4_unregister_mac_and_vlan(dev, vhcr->op_modifier, *p_slave_in->qpn, !p_slave_in->reserve);
			mlx4_delete_resource_for_slave(dev, RES_MAC_AND_VLAN, slave, *p_slave_in->qpn);
            if (!p_slave_in->reserve) {
                /*When removing mac the master also de-allocates qp*/
                mlx4_delete_resource_for_slave(dev, RES_QP, slave, *p_slave_in->qpn);
            }
            break;
        case MLX4_CMD_REPLACE_RES:
            vhcr->in_param |= (u64) (vep_num) << 48;
            ret = mlx4_replace_mac(dev, vhcr->op_modifier,
                (int) vhcr->out_param, vhcr->in_param);
            vhcr->err = ret;
            break;
        default:
            vhcr->err = -EINVAL;
        }
        break;
    default:
        vhcr->err = -EINVAL;
    }
    return 0;
}

static int mlx4_DMA_wrapper(struct mlx4_dev *dev, int slave,
                struct mlx4_vhcr *vhcr,
                struct mlx4_cmd_mailbox *inbox,
                struct mlx4_cmd_mailbox *outbox)
{
    u64 in_param = inbox ? inbox->dma.da : vhcr->in_param;

    UNUSED_PARAM(outbox);

    in_param |= (u64) slave;
    return mlx4_cmd(dev, in_param, vhcr->in_modifier,
            vhcr->op_modifier, vhcr->op, MLX4_CMD_TIME_CLASS_C);
}

static int mlx4_DMA_outbox_wrapper(struct mlx4_dev *dev, int slave,
                   struct mlx4_vhcr *vhcr,
                   struct mlx4_cmd_mailbox *inbox,
                   struct mlx4_cmd_mailbox *outbox)
{
    u64 in_param = inbox ? inbox->dma.da : vhcr->in_param;
    u64 out_param = outbox ? outbox->dma.da : vhcr->out_param;

    in_param |= (u64) slave;
    return mlx4_cmd_box(dev, in_param, out_param,
                vhcr->in_modifier, vhcr->op_modifier, vhcr->op,
                MLX4_CMD_TIME_CLASS_C);
}

static struct mlx4_cmd_info {
    u16 opcode;
    u8 has_inbox;
    u8 has_outbox;
    u8 out_is_imm;
    int (*verify)(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
                        struct mlx4_cmd_mailbox *inbox);
    int (*wrapper)(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
                         struct mlx4_cmd_mailbox *inbox,
                         struct mlx4_cmd_mailbox *outbox);
} cmd_info[] = {
    {MLX4_CMD_QUERY_FW,        0, 1, 0, NULL, NULL},
    {MLX4_CMD_QUERY_SLAVE_CAP, 0, 1, 0, NULL, mlx4_QUERY_SLAVE_CAP_wrapper},
    {MLX4_CMD_QUERY_ADAPTER,   0, 1, 0, NULL, NULL},
    {MLX4_CMD_GET_SLAVE_SQP,   0, 1, 0, NULL, mlx4_GET_SLAVE_SQP_wrapper},
    {MLX4_CMD_COMM_INT,        0, 0, 0, NULL, mlx4_COMM_INT_wrapper}, 
    {MLX4_CMD_INIT_PORT,       0, 0, 0, NULL, mlx4_INIT_PORT_wrapper},
    {MLX4_CMD_CLOSE_PORT,      0, 0, 0, NULL, mlx4_CLOSE_PORT_wrapper},
    {MLX4_CMD_QUERY_PORT,      0, 1, 0, NULL, mlx4_QUERY_PORT_wrapper},
    {MLX4_CMD_SET_PORT,        1, 0, 0, NULL, mlx4_SET_PORT_wrapper},

    {MLX4_CMD_SW2HW_EQ,        1, 0, 0, NULL, mlx4_DMA_wrapper},
    {MLX4_CMD_NOP,             0, 0, 0, NULL, NULL},
    {MLX4_CMD_ALLOC_RES,       0, 0, 1, mlx4_verify_resource_wrapper, mlx4_RESOURCE_wrapper},
    {MLX4_CMD_FREE_RES,        0, 0, 0, NULL, mlx4_RESOURCE_wrapper},
    {MLX4_CMD_REPLACE_RES,     0, 0, 1, NULL, mlx4_RESOURCE_wrapper},
    {MLX4_CMD_GET_EVENT,       0, 0, 1, NULL, mlx4_GET_EVENT_wrapper},

    {MLX4_CMD_SW2HW_MPT,       1, 0, 0, NULL, mlx4_DMA_wrapper},
    {MLX4_CMD_QUERY_MPT,       0, 1, 0, mlx4_verify_mpt_index, NULL},
    {MLX4_CMD_HW2SW_MPT,       0, 0, 0, mlx4_verify_mpt_index, NULL},
    {MLX4_CMD_READ_MTT,        0, 1, 0, NULL, NULL}, /* need verifier */
    {MLX4_CMD_WRITE_MTT,       1, 0, 0, NULL, mlx4_WRITE_MTT_wrapper},
    {MLX4_CMD_SYNC_TPT,        1, 0, 0, NULL, NULL}, /* need verifier */
    {MLX4_CMD_MAP_EQ,          0, 0, 0, NULL, mlx4_MAP_EQ_wrapper},
    {MLX4_CMD_HW2SW_EQ,        0, 1, 0, NULL, mlx4_DMA_outbox_wrapper},
    {MLX4_CMD_QUERY_EQ,        0, 1, 0, NULL, NULL}, /* need verifier */
    {MLX4_CMD_SW2HW_CQ,        1, 0, 0, mlx4_verify_cq_index, mlx4_DMA_wrapper},
    {MLX4_CMD_HW2SW_CQ,        0, 0, 0, mlx4_verify_cq_index, mlx4_DMA_wrapper},
    {MLX4_CMD_QUERY_CQ,        0, 1, 0, mlx4_verify_cq_index, NULL}, /* need verifier */
    {MLX4_CMD_MODIFY_CQ,       1, 0, 1, mlx4_verify_cq_index, NULL}, /* need verifier */
    {MLX4_CMD_SW2HW_SRQ,       1, 0, 0, mlx4_verify_srq_index, mlx4_DMA_wrapper},
    {MLX4_CMD_HW2SW_SRQ,       0, 0, 0, mlx4_verify_srq_index, NULL}, /* need verifier */
    {MLX4_CMD_QUERY_SRQ,       0, 1, 0, mlx4_verify_srq_index, NULL}, /* need verifier */
    {MLX4_CMD_ARM_SRQ,         0, 0, 0, mlx4_verify_srq_aram, NULL}, /* need verifier */
    {MLX4_CMD_RST2INIT_QP,     1, 0, 0, mlx4_verify_qp_index, mlx4_DMA_wrapper},
    {MLX4_CMD_INIT2RTR_QP,     1, 0, 0, mlx4_verify_qp_index, mlx4_INIT2RTR_QP_wrapper},
    {MLX4_CMD_RTR2RTS_QP,      1, 0, 0, mlx4_verify_qp_index, mlx4_RTR2RTS_QP_wrapper},
    {MLX4_CMD_RTS2RTS_QP,      1, 0, 0, mlx4_verify_qp_index, mlx4_RTS2RTS_QP_wrapper},
    {MLX4_CMD_SQERR2RTS_QP,    1, 0, 0, mlx4_verify_qp_index, mlx4_DMA_wrapper},
    {MLX4_CMD_2ERR_QP,         0, 0, 0, mlx4_verify_qp_index, mlx4_DMA_wrapper},
    {MLX4_CMD_RTS2SQD_QP,      0, 0, 0, mlx4_verify_qp_index, mlx4_DMA_wrapper},
    {MLX4_CMD_SQD2SQD_QP,      1, 0, 0, mlx4_verify_qp_index, mlx4_DMA_wrapper},
    {MLX4_CMD_SQD2RTS_QP,      1, 0, 0, mlx4_verify_qp_index, mlx4_DMA_wrapper},
    {MLX4_CMD_2RST_QP,         0, 0, 0, mlx4_verify_qp_index, NULL}, /* need verifier */
    {MLX4_CMD_QUERY_QP,        0, 1, 0, mlx4_verify_qp_index, NULL}, /* need verifier */
    {MLX4_CMD_INIT2INIT_QP,    1, 0, 0, mlx4_verify_qp_index, NULL}, /* need verifier */
    {MLX4_CMD_SUSPEND_QP,      0, 0, 0, mlx4_verify_qp_index, NULL}, /* need verifier */
    {MLX4_CMD_UNSUSPEND_QP,    0, 0, 0, mlx4_verify_qp_index, NULL}, /* need verifier */
    {MLX4_CMD_CONF_SPECIAL_QP, 0, 0, 0, NULL, mlx4_CONF_SPECIAL_QP_wrapper},
    {MLX4_CMD_MAD_IFC,         1, 1, 0, NULL, NULL}, /* need verifier */

    /* Native multicast commands are not available for guests */
    {MLX4_CMD_MCAST_ATTACH,    1, 0, 0, NULL, mlx4_MCAST_wrapper},
    {MLX4_CMD_PROMISC,         0, 0, 0, NULL, mlx4_PROMISC_wrapper},
    {MLX4_CMD_DIAG_RPRT,       0, 1, 0, NULL, NULL}, /* need verifier */

    /* Ethernet specific commands */
    {MLX4_CMD_SET_VLAN_FLTR,   1, 0, 0, NULL, mlx4_SET_VLAN_FLTR_wrapper},
    {MLX4_CMD_SET_MCAST_FLTR,  0, 0, 0, NULL, mlx4_SET_MCAST_FLTR_wrapper},
    {MLX4_CMD_QUERY_IF_STAT,   0, 1, 0, NULL, mlx4_QUERY_IF_STAT_wrapper}, 

};

static int mlx4_master_process_vhcr(struct mlx4_dev *dev, int slave)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_cmd_info *cmd = NULL;
    struct mlx4_vhcr *vhcr = priv->mfunc.vhcr;
    struct mlx4_cmd_mailbox *inbox = NULL;
    struct mlx4_cmd_mailbox *outbox = NULL;
    u64 in_param;
    u64 out_param;
    int ret;
    int i;

    /* DMA in the vHCR */
    ret = mlx4_ACCESS_MEM(dev, priv->mfunc.vhcr_dma.da, slave,
                  priv->mfunc.master.slave_state[slave].vhcr_dma.da,
                  ALIGN(sizeof(struct mlx4_vhcr),
                    MLX4_ACCESS_MEM_ALIGN), 1);
    if (ret) {        
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed reading vhcr\n", dev->pdev->name));
        return ret;
    }

    /* Lookup command */
    for (i = 0; i < ARRAY_SIZE(cmd_info); ++i) {
        if (vhcr->op == cmd_info[i].opcode) {
            cmd = &cmd_info[i];
            break;
        }
    }
    if (!cmd) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Unknown command:0x%x accepted from slave:%d\n", dev->pdev->name, vhcr->op, slave));
        vhcr->err = -EINVAL;
        goto out_status;
    }

    /* Read inbox */
    if (cmd->has_inbox) {
        vhcr->in_param &= INBOX_MASK;        
        inbox = mlx4_alloc_cmd_mailbox(dev);
        if (IS_ERR(inbox)) {
            ret = PTR_ERR(inbox);
            inbox = NULL;
            goto out;
        }

        /* FIXME: add mailbox size per-command */
        ret = mlx4_ACCESS_MEM(dev, inbox->dma.da, slave,
                      vhcr->in_param,
                      MLX4_MAILBOX_SIZE, 1);
        if (ret) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed reading inbox\n", dev->pdev->name));
            goto out;
        }
    }

    /* Apply permission and bound checks if applicable */
    if (cmd->verify && cmd->verify(dev, slave, vhcr, inbox)) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Command:0x%x failed protection checks for resource_id:%d\n", 
            dev->pdev->name, vhcr->op, vhcr->op_modifier));
        vhcr->err = -EPERM;
        goto out_status;
    }

    /* Allocate outbox */
    if (cmd->has_outbox) {
        outbox = mlx4_alloc_cmd_mailbox(dev);
        if (IS_ERR(outbox)) {
            ret = PTR_ERR(outbox);
            outbox = NULL;
            goto out;
        }
    }

    /* Execute the command! */
    if (cmd->wrapper)
        vhcr->err = cmd->wrapper(dev, slave, vhcr, inbox, outbox);
    else {
        in_param = cmd->has_inbox ? (u64) inbox->dma.da : vhcr->in_param;
        out_param = cmd->has_outbox ? (u64) outbox->dma.da : vhcr->out_param;
        vhcr->err = __mlx4_cmd(dev, in_param, &out_param,
                            cmd->out_is_imm,
                            vhcr->in_modifier,
                            vhcr->op_modifier,
                            vhcr->op,
                            vhcr->timeout);
        if (cmd->out_is_imm)
            vhcr->out_param = out_param;
    }

    /* Write outbox if command completed successfully */
    if (cmd->has_outbox && !vhcr->err) {
        ret = mlx4_ACCESS_MEM(dev, outbox->dma.da, slave,
                      vhcr->out_param,
                      MLX4_MAILBOX_SIZE, 0);
        if (ret) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed writing outbox\n", dev->pdev->name));
            goto out;
        }
    }

out_status:
    /* DMA back vhcr result */
    ret = mlx4_ACCESS_MEM(dev, priv->mfunc.vhcr_dma.da, slave,
                  priv->mfunc.master.slave_state[slave].vhcr_dma.da,
                  ALIGN(sizeof(struct mlx4_vhcr),
                    MLX4_ACCESS_MEM_ALIGN), 0);
    if (ret)
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed writing vhcr result\n", dev->pdev->name));

    if (vhcr->err)
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: vhcr command:0x%x slave:%d failed with error:%d\n",
            dev->pdev->name, vhcr->op, slave, vhcr->err));
    /* Fall through... */

out:
    mlx4_free_cmd_mailbox(dev, inbox);
    mlx4_free_cmd_mailbox(dev, outbox);
    return ret;
}

char *slave_cmds[] = {
    "MLX4_COMM_CMD_RESET",
    "MLX4_COMM_CMD_VHCR0",
    "MLX4_COMM_CMD_VHCR1",
    "MLX4_COMM_CMD_VHCR2",
    "MLX4_COMM_CMD_VHCR_EN",
    "MLX4_COMM_CMD_VHCR_POST"
};

//BUGBUG: SRIOV is not supported
static void mlx4_add_slave_to_removal_rel(struct mlx4_dev *dev, int slave)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PMLX4_ST_DEVICE p_stat;
    // add it to device removal relations
    p_stat = st_dev_get( dev->pdev->pci_bus, slave);
    ASSERT( p_stat && p_stat->pdo );
    if ( p_stat && p_stat->pdo ) 
        status = WdfDeviceAddRemovalRelationsPhysicalDevice(
            (WDFDEVICE)dev->pdev->p_wdf_device, p_stat->pdo );
    if (!status) {
        MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: added slave %d (PDO %p) to removal relations.\n", 
            dev->pdev->name, slave, p_stat->pdo ));        
        p_stat->added_to_removal_dep = TRUE;
    }
    else
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: failed to add slave %d to removal relations. Status %#x\n", 
            dev->pdev->name, slave, status));
}

//BUGBUG: SRIOV is not supported
static void mlx4_rmv_slave_from_removal_rel(struct mlx4_dev *dev, int slave)
{
    PMLX4_ST_DEVICE p_stat;
    // remove it from device removal relations
    p_stat = st_dev_get( dev->pdev->pci_bus, slave);
    ASSERT( p_stat );
    if ( p_stat && p_stat->added_to_removal_dep ) {
        p_stat->added_to_removal_dep = FALSE;
        WdfDeviceRemoveRemovalRelationsPhysicalDevice(
            (WDFDEVICE)dev->pdev->p_wdf_device, p_stat->pdo );
        MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: removed slave %d (PDO %p) from removal relations.\n", 
            dev->pdev->name, slave, p_stat->pdo));
    }
}
static void mlx4_master_do_cmd(struct mlx4_dev *dev, int slave, u8 cmd, u16 param, u8 toggle)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_mfunc_master_ctx* master = &priv->mfunc.master;
    struct mlx4_slave_state *slave_state = master->slave_state;
    u8 toggle_next;
    u32 reply;
    int err;

    slave_state[slave].cmd_num++;

    if ( g.mode_flags & MLX4_MODE_COMM_TSTAMP )
        slave_state[slave].poll_stamp = (u32)jiffies;

    if (cmd == MLX4_COMM_CMD_RESET) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Received reset from slave:%d\n", dev->pdev->name, slave));
        goto reset_slave;
    }

    /* Increment next toggle token */
    toggle_next = slave_state[slave].comm_toggle + 1;
    if (toggle_next > 2)
        toggle_next = 1;
    if (toggle != toggle_next) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Incorrect token:%d from slave:%d expected:%d\n",
            dev->pdev->name, toggle, toggle_next, slave));
        ASSERT(FALSE);
        goto reset_slave;
    }

    if (cmd < MLX4_COMM_CMD_DUMMY) {
        MLX4_PRINT( TRACE_LEVEL_VERBOSE, MLX4_DBG_COMM,
            ( "%s: got from slave %d (%s) a command '%s' (param %#hx, toggle %d) \n",
            dev->pdev->name, slave, dev->pdev->name, slave_cmds[cmd], param, toggle ) );
    }

    switch (cmd) {
    case MLX4_COMM_CMD_VHCR0:
        if (slave_state[slave].last_cmd != MLX4_COMM_CMD_RESET)
        {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Received Illegal MLX4_COMM_CMD_VHCR0 command from slave:%d\n", dev->pdev->name, slave));            
            ASSERT(FALSE);
            goto reset_slave;
        }
        slave_state[slave].vhcr_dma.da = ((u64) param) << 48;
        break;
    case MLX4_COMM_CMD_VHCR1:
        if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR0)
        {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Received Illegal MLX4_COMM_CMD_VHCR1 command from slave:%d\n", dev->pdev->name, slave));            
            ASSERT(FALSE);
            goto reset_slave;
        }
        slave_state[slave].vhcr_dma.da |= ((u64) param) << 32;
        break;
    case MLX4_COMM_CMD_VHCR2:
        if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR1)
        {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Received Illegal MLX4_COMM_CMD_VHCR2 command from slave:%d\n", dev->pdev->name, slave));            
            ASSERT(FALSE);
            goto reset_slave;
        }
        slave_state[slave].vhcr_dma.da |= ((u64) param) << 16;
        break;
    case MLX4_COMM_CMD_VHCR_EN:
        ASSERT(slave_state[slave].active == false);          
        if (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR2)
        {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Received Illegal MLX4_COMM_CMD_VHCR_EN command from slave:%d\n", dev->pdev->name, slave));            
            ASSERT(FALSE);
            goto reset_slave;
        }
        slave_state[slave].vhcr_dma.da |= param;
        if (mlx4_QUERY_FUNC(dev, slave, &slave_state[slave].pf_num)) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                ("Failed to determine physical function number for slave %d\n ", slave));
            ASSERT(FALSE);
            goto reset_slave;
        }

        slave_state[slave].vep_num = slave_state[slave_state[slave].pf_num].vep_num;
        slave_state[slave].port_num = slave_state[slave_state[slave].pf_num].port_num;
        slave_state[slave].active = true;
        master->n_active_slaves++;
        MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: slave %d is active.\n", dev->pdev->name, slave));
        mlx4_add_slave_to_removal_rel(dev, slave);
        
        break;
    case MLX4_COMM_CMD_VHCR_POST:
        if ((slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR_EN) &&
            (slave_state[slave].last_cmd != MLX4_COMM_CMD_VHCR_POST))
        {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Received Illegal MLX4_COMM_CMD_VHCR_POST command from slave:%d\n", dev->pdev->name, slave));            
            ASSERT(FALSE);
            goto reset_slave;
        }

        if (mlx4_master_process_vhcr(dev, slave)) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                ("%s: Failed processing vhcr for slave:%d, reseting slave.\n", dev->pdev->name, slave));
            ASSERT(FALSE);
            goto reset_slave;
        }
        break;
    default:
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Bad comm cmd:%d from slave:%d\n", dev->pdev->name, cmd, slave));
        ASSERT(FALSE);
        goto reset_slave;
    }

    slave_state[slave].last_cmd = cmd;
    slave_state[slave].comm_toggle = toggle_next;
    reply = (u32) toggle_next << 30;
    __raw_writel((__force u32) cpu_to_be32(reply),
             &priv->mfunc.comm[slave].slave_read);
    wmb();

    err = mlx4_GEN_EQE(dev, slave, &master->cmd_eqe);
    ASSERT(err == 0);
    
    if ( g.mode_flags & MLX4_MODE_COMM_TSTAMP )
        slave_state[slave].done_stamp = (u32)jiffies;
    return;

reset_slave:
    /* cleanup any slave resources */
    mlx4_delete_all_resources_for_slave(dev, slave);
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: error while handling a command from slave %d. Slave comm channel has been reset !\n",
        dev->pdev->name, slave ));
    slave_state[slave].last_cmd = MLX4_COMM_CMD_RESET;
    slave_state[slave].comm_toggle = 0;
    __raw_writel((__force u32) 0, &priv->mfunc.comm[slave].slave_write);
    __raw_writel((__force u32) 0, &priv->mfunc.comm[slave].slave_read);
    wmb();

    if (slave_state[slave].active)
    {
        memset(&slave_state[slave].event_eq, 0, sizeof(struct slave_event_eq_info));
        slave_state[slave].active = false;
        master->n_active_slaves--;
        MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: slave %d was deactivated.\n", dev->pdev->name, slave));
        mlx4_rmv_slave_from_removal_rel( dev, slave );
    }
}


/* master command processing */

void mlx4_master_comm_channel(void* ctx)
{
    struct mlx4_priv *priv = (struct mlx4_priv*) ctx;
    struct mlx4_dev *dev = &priv->dev;
    struct working_thread * p_thread = &priv->mfunc.master.comm_channel_thread;
    NTSTATUS status;
    u32 comm_cmd;
    int dbg_indx;
    u32* bit_vec;
    u8 i, j, slave_id;
    u32 vec; 
    int err;    
    KIRQL oldIrql;    

    KeRaiseIrql(APC_LEVEL, &oldIrql);

    ASSERT( KeGetCurrentIrql() == APC_LEVEL );
    
    for(;;) {
        status = KeWaitForSingleObject( &p_thread->trig_event, 
                                        Executive, KernelMode, FALSE, NULL );

        ASSERT(status == STATUS_SUCCESS);
        if ((status != STATUS_SUCCESS) || p_thread->f_stop){
            /* thread stopped */
            break;      
        }

        wmb();        
        bit_vec = priv->mfunc.master.comm_arm_bit_vec;

        for (i = 0; i < COMM_CHANNEL_BIT_ARRAY_SIZE; ++i)
        {
            vec = be32_to_cpu(bit_vec[i]);
            for (j = 0; j < 32; ++j)
            {
                if((vec & (1 << j)) != 0)
                {
                    slave_id = i*32 + j;
                    comm_cmd = be32_to_cpu(readl(&priv->mfunc.comm[slave_id].slave_write));

                    if (comm_cmd >> 30 != priv->mfunc.master.slave_state[slave_id].comm_toggle) {
                        dbg_indx = g_stat.comm_dbg.cmd_indx & (MASTER_POOL_SIZE - 1);
                        g_stat.comm_dbg.cmd_indx++;
                        MASTER_COMM_INFO(dbg_indx).time_to_call = jiffies - SLAVE_COMM_INFO(slave_id).time;
                        MASTER_COMM_INFO(dbg_indx).cmd_count =  SLAVE_COMM_INFO(slave_id).cmd_count;
                        MASTER_COMM_INFO(dbg_indx).slave_id = (u8)slave_id;
                        MASTER_COMM_INFO(dbg_indx).time_to_process = 0;
                        MASTER_COMM_INFO(dbg_indx).cmd = (u8)(comm_cmd >> 16);
                        mlx4_master_do_cmd(dev, slave_id, (u8)(comm_cmd >> 16), (u16)comm_cmd, (u8)(comm_cmd >> 30));
                        MASTER_COMM_INFO(dbg_indx).time_to_process = jiffies - SLAVE_COMM_INFO(slave_id).time;
                    }
                }
            }
        }

        err = mlx4_ARM_COMM_CHANNEL(dev);
        if (err != 0) {            
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                ("%s: Failed to ARM comm channel. err=0x%x\n", dev->pdev->name, err));
            ASSERT(FALSE);
        }
    }

    KeLowerIrql(oldIrql);
    
    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Exit working thread mlx4_master_comm_channel.\n", dev->pdev->name));
    PsTerminateSystemThread(STATUS_SUCCESS);
}

int mlx4_multi_func_init(struct mlx4_dev *dev)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_slave_state *s_state;
    int i, port;
    io_addr_t addr;
    int err;
    NTSTATUS status;
    
    priv->mfunc.vhcr = (mlx4_vhcr *)dma_alloc_coherent(&(dev->pdev->dev), PAGE_SIZE,
                        &priv->mfunc.vhcr_dma,
                        GFP_KERNEL);
    if (!priv->mfunc.vhcr) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Couldn't allocate vhcr.\n", dev->pdev->name));
        return -ENOMEM;
    }

    if (mlx4_is_master(dev)) {
        addr = pci_resource_start(dev->pdev, priv->fw.comm_bar) + priv->fw.comm_base;
        priv->mfunc.comm = (mlx4_comm *)ioremap( addr, MLX4_COMM_PAGESIZE, MmNonCached);
        MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Master comm channel pa %I64x, va %p, fw.comm_base %I64x, fw.comm_bar %d\n",
            dev->pdev->name, addr, priv->mfunc.comm, priv->fw.comm_base, priv->fw.comm_bar ));
    }
    else {
        addr = pci_resource_start(dev->pdev, 2) + MLX4_SLAVE_COMM_BASE;
        priv->mfunc.comm = (mlx4_comm *)ioremap( addr, MLX4_COMM_PAGESIZE, MmNonCached);
        MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Slave comm channel pa %I64x, va %p, fw.comm_base %I64x, fw.comm_bar %d\n",
            dev->pdev->name, addr, priv->mfunc.comm, MLX4_SLAVE_COMM_BASE, 2 ));
    }
    if (!priv->mfunc.comm) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Couldn't map communication vector.\n", dev->pdev->name));
        goto err_vhcr;
    }

    if (mlx4_is_master(dev)) {
        priv->mfunc.master.slave_state = (mlx4_slave_state *)kzalloc(dev->num_slaves *
                   sizeof(struct mlx4_slave_state),
                       GFP_KERNEL);
        if (!priv->mfunc.master.slave_state)
            goto err_comm;

        for (i = 0; i < (int)dev->num_slaves; ++i) {
            s_state = &priv->mfunc.master.slave_state[i];
            s_state->last_cmd = MLX4_COMM_CMD_RESET;
            for (port = 1; port <= dev->caps.num_ports; port++) {
                s_state->vlan_filter[port] =
                    (mlx4_vlan_fltr *)kzalloc(sizeof(struct mlx4_vlan_fltr),
                        GFP_KERNEL);
                if (!s_state->vlan_filter[port]) {
                    if (--port)
                        kfree(s_state->vlan_filter[port]);
                    goto err_slaves;
                }
                INIT_LIST_HEAD(&s_state->mcast_filters[port]);
            }
            
            spin_lock_init(&s_state->lock);
        }

        memset(&priv->mfunc.master.cmd_eqe, 0, sizeof(struct mlx4_eqe));
        priv->mfunc.master.cmd_eqe.type = MLX4_EVENT_TYPE_CMD;
        
        
            
        status = init_working_thread(&priv->mfunc.master.comm_channel_thread, mlx4_master_comm_channel, priv);
        if (!NT_SUCCESS(status)) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed to initilaize comm channel. status=0x%x\n", dev->pdev->name, status));
            goto err_slaves;
        }
        
        status = init_working_thread(&priv->mfunc.master.slave_event_thread, mlx4_gen_slave_eqe , priv);
        if (!NT_SUCCESS(status)) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed to initilaize slave event thread. status=0x%x\n", dev->pdev->name, status));
            goto err_thread;
        }

        spin_lock_init(&priv->mfunc.master.vep_config_lock);			
#pragma prefast(suppress:28197, "The WI is deleted either at bad flow in this function, mlx4_multi_func_cleanup or mlx_remove_one")
        priv->mfunc.master.vep_config_work = IoAllocateWorkItem(dev->pdev->p_self_do);
        if (priv->mfunc.master.vep_config_work == NULL) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed to allocate VEP config work iteam. Insufficient resources \n", dev->pdev->name));
            goto err_event_thread;
        }
        
        if (mlx4_init_resource_tracker(dev))
            goto err_vep_work;

        err = mlx4_set_vep_maps(dev);
        if (err) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed to set veps mapping\n", dev->pdev->name));
            goto err_tracker;
        }
        dev->caps.vep_num = priv->mfunc.master.slave_state[dev->caps.function].vep_num;

        mlx4_QUERY_VEP_CFG(dev, priv->mfunc.master.slave_state[dev->caps.function].vep_num,
            priv->mfunc.master.slave_state[dev->caps.function].port_num,
            &priv->mfunc.master.slave_state[dev->caps.function].vep_cfg); 

        dev->caps.def_mac[priv->mfunc.master.slave_state[dev->caps.function].port_num] =
                priv->mfunc.master.slave_state[dev->caps.function].vep_cfg.mac;


        err = mlx4_ARM_COMM_CHANNEL(dev);
        if (err != 0) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Failed to ARM comm channel. err=0x%x\n", dev->pdev->name, err));
            goto err_tracker;
        }   
        
    } else {
        sema_init(&priv->cmd.slave_sem, 1);
        priv->cmd.comm_toggle = 0;        
    }
    return 0;

err_tracker:
    mlx4_free_resource_tracker(dev);

err_vep_work:
    IoFreeWorkItem(priv->mfunc.master.vep_config_work);
    priv->mfunc.master.vep_config_work = NULL;
    
err_event_thread:
    stop_working_thread(&priv->mfunc.master.slave_event_thread);

err_thread:
    stop_working_thread(&priv->mfunc.master.comm_channel_thread);
    
err_slaves:
    while(--i) {
        for (port = 1; port <= dev->caps.num_ports; port++)
            kfree(priv->mfunc.master.slave_state[i].vlan_filter[port]);
    }
    kfree(priv->mfunc.master.slave_state);

err_comm:
    iounmap(priv->mfunc.comm, MLX4_COMM_PAGESIZE);

err_vhcr:
    dma_free_coherent(&(dev->pdev->dev), PAGE_SIZE,
                         priv->mfunc.vhcr,
                         priv->mfunc.vhcr_dma);
    priv->mfunc.vhcr = NULL;
    return -ENOMEM;
}

int mlx4_cmd_init(struct mlx4_dev *dev)
{
    struct mlx4_priv *priv = mlx4_priv(dev);

    mutex_init(&priv->cmd.hcr_mutex);
    sema_init(&priv->cmd.poll_sem, 1);
    priv->cmd.use_events = 0;
    priv->cmd.toggle     = 1;

    priv->cmd.hcr = NULL;
    priv->mfunc.vhcr = NULL;
    
    if (!mlx4_is_slave(dev)) {
        priv->cmd.hcr = (u8*)ioremap(pci_resource_start(dev->pdev, 0) + MLX4_HCR_BASE,
			MLX4_HCR_SIZE, MmNonCached);
        if (!priv->cmd.hcr) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Couldn't map command register.", dev->pdev->name));
            return -ENOMEM;
        }
    }

    priv->cmd.pool = pci_pool_create("mlx4_cmd", dev->pdev,
            MLX4_MAILBOX_SIZE, MLX4_MAILBOX_SIZE, 0);
    if (!priv->cmd.pool)
        goto err_hcr;

    return 0;

err_hcr:
    if (!mlx4_is_slave(dev))
        iounmap(priv->cmd.hcr, MLX4_HCR_SIZE);
    return -ENOMEM;
}


void mlx4_multi_func_cleanup(struct mlx4_dev *dev)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    int i, port;

    if (priv->mfunc.master.vep_config_work)
    {
        IoFreeWorkItem(priv->mfunc.master.vep_config_work);
        priv->mfunc.master.vep_config_work = NULL;
    }
        
    // resource tracker works only for PPF
    if (mlx4_is_master(dev)) {
        stop_working_thread(&priv->mfunc.master.slave_event_thread);
        stop_working_thread(&priv->mfunc.master.comm_channel_thread);
        mlx4_free_resource_tracker(dev);
        for (i = 0; i < (int)dev->num_slaves; i++) {
            for (port = 1; port <= dev->caps.num_ports; port++)
                kfree(priv->mfunc.master.slave_state[i].vlan_filter[port]);
        }
        kfree(priv->mfunc.master.slave_state);
    }

	// it can be NULL in error flow
    if (priv->mfunc.comm != NULL)
	    iounmap( priv->mfunc.comm, MLX4_COMM_PAGESIZE );
    priv->mfunc.comm = NULL;
    
    ASSERT(priv->mfunc.vhcr != NULL);
    dma_free_coherent(&(dev->pdev->dev), PAGE_SIZE,
                         priv->mfunc.vhcr,
                         priv->mfunc.vhcr_dma);
    priv->mfunc.vhcr = NULL;
}


void mlx4_cmd_cleanup(struct mlx4_dev *dev)
{
    struct mlx4_priv *priv = mlx4_priv(dev);

	ASSERT(priv->cmd.pool);
	if (priv->cmd.pool) {
		pci_pool_destroy(priv->cmd.pool);
		priv->cmd.pool = NULL;
	}

    if (!mlx4_is_slave(dev)) {
		ASSERT(priv->cmd.hcr);
		if (priv->cmd.hcr) {       
			iounmap(priv->cmd.hcr, MLX4_HCR_SIZE);
			priv->cmd.hcr = NULL;
		}
   	}
}

/*
 * Switch to using events to issue FW commands (can only be called
 * after event queue for command events has been initialized).
 */
int mlx4_cmd_use_events(struct mlx4_dev *dev)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    int i;
    int err;

    ASSERT(priv->cmd.context == NULL);
    priv->cmd.context = (mlx4_cmd_context *)kmalloc(priv->cmd.max_cmds *
                   sizeof (struct mlx4_cmd_context),
                   GFP_KERNEL);
    if (!priv->cmd.context)
        return -ENOMEM;

    for (i = 0; i < priv->cmd.max_cmds; ++i) {
        priv->cmd.context[i].token = (u16)i;
        priv->cmd.context[i].next  = i + 1;
    }

    priv->cmd.context[priv->cmd.max_cmds - 1].next = -1;
    priv->cmd.free_head = 0;

    sema_init(&priv->cmd.event_sem, priv->cmd.max_cmds);
    spin_lock_init(&priv->cmd.context_lock);

    for (priv->cmd.token_mask = 1;
         priv->cmd.token_mask < priv->cmd.max_cmds;
         priv->cmd.token_mask <<= 1)
        ; /* nothing */
    --priv->cmd.token_mask;

    priv->cmd.use_events = 1;
    
    if (mlx4_is_slave(dev)) {        
        err = mlx4_cmd(dev, 0, 1, 0, MLX4_CMD_COMM_INT, MLX4_CMD_TIME_CLASS_A);
        ASSERT(!err);
    } 
    
    down(&priv->cmd.poll_sem);
    
    return 0;
}

/*
 * Switch back to polling (used when shutting down the device)
 */
void mlx4_cmd_use_polling(struct mlx4_dev *dev)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    int i, err;

    priv->cmd.use_events = 0;

    for (i = 0; i < priv->cmd.max_cmds; ++i)
        down(&priv->cmd.event_sem);

    kfree(priv->cmd.context);
	priv->cmd.context = NULL;

    up(&priv->cmd.poll_sem);

    if (mlx4_is_slave(dev)) {  
        err = mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_COMM_INT, MLX4_CMD_TIME_CLASS_A);
        ASSERT(!err);
    } 

    
}

struct mlx4_cmd_mailbox *mlx4_alloc_cmd_mailbox(struct mlx4_dev *dev)
{
    struct mlx4_cmd_mailbox *mailbox;

    if ( mlx4_is_barred(dev) )
        return (mlx4_cmd_mailbox *)ERR_PTR(-EFAULT);

    mailbox = (mlx4_cmd_mailbox *)kmalloc(sizeof *mailbox, GFP_KERNEL);
    if (!mailbox)
        return (mlx4_cmd_mailbox *)ERR_PTR(-ENOMEM);

    mailbox->buf = pci_pool_alloc(mlx4_priv(dev)->cmd.pool, GFP_KERNEL,
                      &mailbox->dma);
    if (!mailbox->buf) {
        kfree(mailbox);
        return (mlx4_cmd_mailbox *)ERR_PTR(-ENOMEM);
    }

    return mailbox;
}

void mlx4_free_cmd_mailbox(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox)
{
    if (!mailbox)
        return;

    pci_pool_free(mlx4_priv(dev)->cmd.pool, mailbox->buf, mailbox->dma);
    kfree(mailbox);
}

// This is the interface version of this function
int imlx4_cmd(struct mlx4_dev *dev, u64 in_param, u64 *out_param, int out_is_imm,
        u32 in_modifier, u8 op_modifier, u16 op, unsigned long timeout)
{
    return __mlx4_cmd(dev, in_param, out_param, out_is_imm, in_modifier,
              op_modifier, op, timeout);
}

int mlx4_COMM_INT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
    struct mlx4_cmd_mailbox *inbox,
    struct mlx4_cmd_mailbox *outbox)
{
    struct mlx4_priv* priv = mlx4_priv(dev);
    struct slave_event_eq_info* event_eq = &priv->mfunc.master.slave_state[slave].event_eq;

    UNUSED_PARAM(inbox);
    UNUSED_PARAM(outbox);

    if(vhcr->in_modifier)
        event_eq->f_use_int = true;
    else
        event_eq->f_use_int = false;
    
    return 0;
}

