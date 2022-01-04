/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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
#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mcg.tmh"
#endif


static const u8 zero_gid[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static int mlx4_READ_ENTRY(struct mlx4_dev *dev, int index,    
            struct mlx4_cmd_mailbox *mailbox)
{
    return mlx4_cmd_box(dev, 0, mailbox->dma.da, index, 0,
                MLX4_CMD_READ_MCG, MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_WRITE_ENTRY(struct mlx4_dev *dev, int index,
            struct mlx4_cmd_mailbox *mailbox)
{
    return mlx4_cmd(dev, mailbox->dma.da, index, 0,
                MLX4_CMD_WRITE_MCG, MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_WRITE_PROMISC(struct mlx4_dev *dev, u8 vep_num, u8 port, u8 steer,
                  struct mlx4_cmd_mailbox *mailbox)
{
    u32 in_mod;

    in_mod = (u32) vep_num << 24 | (u32) port << 16 | steer << 1;
    return mlx4_cmd(dev, mailbox->dma.da, in_mod, 0x1,
            MLX4_CMD_WRITE_MCG, MLX4_CMD_TIME_CLASS_A);
}



static int mlx4_GID_HASH(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
            u16 *hash, u8 op_mod)
{
    u64 imm;
    int err;

    err = mlx4_cmd_imm(dev, mailbox->dma.da, &imm, 0, op_mod,
                MLX4_CMD_MGID_HASH, MLX4_CMD_TIME_CLASS_A);

    if (!err)
        *hash = (u16)imm;

    return err;
}

/*
 * Helper functions to manage multifunction steering data structures.
 * Used only for Ethernet steering.
 */
 
static struct mlx4_promisc_qp *get_promisc_qp(struct mlx4_dev *dev, u8 pf_num,
                          enum mlx4_steer_type steer,
                          u32 qpn)
{
    struct mlx4_steer *s_steer = &mlx4_priv(dev)->steer[pf_num];
    struct mlx4_promisc_qp *pqp;

    list_for_each_entry(pqp, &s_steer->promisc_qps[steer], list, struct mlx4_promisc_qp) {
        if (pqp->qpn == qpn)
            return pqp;
    }
    /* not found */
    return NULL;
}

/*
 * Add new entry to steering data structure.
 * All promisc QPs should be added as well
 */
static int new_steering_entry(struct mlx4_dev *dev, u8 vep_num, u8 port,

                  enum mlx4_steer_type steer,
                  unsigned int index, u32 qpn)
{
    struct mlx4_steer *s_steer;
    struct mlx4_cmd_mailbox *mailbox;
    struct mlx4_mgm *mgm;
    u32 members_count;
    struct mlx4_steer_index *new_entry;
    struct mlx4_promisc_qp *pqp;
    struct mlx4_promisc_qp *dqp = NULL;
    u32 prot;
    int err;
    u8 pf_num; 

    pf_num = (dev->caps.num_ports == 1) ? vep_num : (vep_num << 1) | (port - 1);
    s_steer = &mlx4_priv(dev)->steer[pf_num];

    new_entry = (mlx4_steer_index *)kzalloc(sizeof *new_entry, GFP_KERNEL);
    if (!new_entry)
        return -ENOMEM;

    INIT_LIST_HEAD(&new_entry->duplicates);
    new_entry->index = index;
    list_add_tail(&new_entry->list, &s_steer->steer_entries[steer]);

    /* If the given qpn is also a promisc qp,
     * it should be inserted to duplicates list
     */
    pqp = get_promisc_qp(dev, pf_num, steer, qpn);
    if (pqp) {
        dqp = (mlx4_promisc_qp *)kmalloc(sizeof *dqp, GFP_KERNEL);
        if (!dqp) {
            err = -ENOMEM;
            goto out_alloc;
        }
        dqp->qpn = qpn;
        list_add_tail(&dqp->list, &new_entry->duplicates);
    }

    /* if no promisc qps for this vep, we are done */
    if (list_empty(&s_steer->promisc_qps[steer]))
        return 0;

    /* now need to add all the promisc qps to the new
     * steering entry, as they should also receive the packets
     * destined to this address */
    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox)) {
        err = -ENOMEM;
        goto out_alloc;
    }
    mgm = (mlx4_mgm *)mailbox->buf;

    err = mlx4_READ_ENTRY(dev, index, mailbox);
    if (err)
        goto out_mailbox;

    members_count = be32_to_cpu(mgm->members_count) & 0xffffff;
    prot = be32_to_cpu(mgm->members_count) >> 30;
    list_for_each_entry(pqp, &s_steer->promisc_qps[steer], list, struct mlx4_promisc_qp) {
        /* don't add already existing qpn */
        if (pqp->qpn == qpn)
            continue;
        if (members_count == MLX4_QP_PER_MGM) {
            /* out of space */
            err = -ENOMEM;
            goto out_mailbox;
        }

        /* add the qpn */
        mgm->qp[members_count++] = cpu_to_be32(pqp->qpn & MGM_QPN_MASK);
    }
    /* update the qps count and update the entry with all the promisc qps*/
    mgm->members_count = cpu_to_be32(members_count | (prot << 30));
    err = mlx4_WRITE_ENTRY(dev, index, mailbox);

out_mailbox:
    mlx4_free_cmd_mailbox(dev, mailbox);
    if (!err)
        return 0;
out_alloc:
    if (dqp) {
        list_del(&dqp->list);
        kfree(&dqp);
    }
    list_del(&new_entry->list);
    kfree(new_entry);
    return err;
}


/* update the data structures with existing steering entry */
static int existing_steering_entry(struct mlx4_dev *dev, u8 vep_num, u8 port,

                   enum mlx4_steer_type steer,
                   unsigned int index, u32 qpn)
{
    struct mlx4_steer *s_steer;
    struct mlx4_steer_index *tmp_entry, *entry = NULL;
    struct mlx4_promisc_qp *pqp;
    struct mlx4_promisc_qp *dqp;
    u8 pf_num;

    pf_num = (dev->caps.num_ports == 1) ? vep_num : (vep_num << 1) | (port - 1);
    s_steer = &mlx4_priv(dev)->steer[pf_num];

    pqp = get_promisc_qp(dev, pf_num, steer, qpn);
    if (!pqp)
        return 0; /* nothing to do */

    list_for_each_entry(tmp_entry, &s_steer->steer_entries[steer], list, struct mlx4_steer_index) {
        if (tmp_entry->index == index) {
            entry = tmp_entry;
            break;
        }
    }
    if (unlikely(!entry)) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Steering entry at index %x is not registered\n", index));
        return -EINVAL;
    }

    /* the given qpn is listed as a promisc qpn
     * we need to add it as a duplicate to this entry
     * for future refernce */
    list_for_each_entry(dqp, &entry->duplicates, list, struct mlx4_promisc_qp) {
        if (qpn == pqp->qpn)
            return 0; /* qp is already duplicated */
    }

    /* add the qp as a duplicate on this index */
    dqp = (mlx4_promisc_qp *)kmalloc(sizeof *dqp, GFP_KERNEL);
    if (!dqp)
        return -ENOMEM;
    dqp->qpn = qpn;
    list_add_tail(&dqp->list, &entry->duplicates);
    return 0;
}

/* Check whether a qpn is a duplicate on steering entry
 * If so, it should not be removed from mgm */
static u8 check_duplicate_entry(struct mlx4_dev *dev, u8 vep_num, u8 port,

                  enum mlx4_steer_type steer,
                  unsigned int index, u32 qpn)
{
    u8 pf_num;
    struct mlx4_steer *s_steer;
    struct mlx4_steer_index *tmp_entry, *entry = NULL;
    struct mlx4_promisc_qp *dqp, *tmp_dqp;

    pf_num = (dev->caps.num_ports == 1) ? vep_num : (vep_num << 1) | (port - 1);
    s_steer = &mlx4_priv(dev)->steer[pf_num];

    /* if qp is not promisc, it cannot be duplicated */
    if (!get_promisc_qp(dev, pf_num, steer, qpn))
        return false;

    /* The qp is promisc qp so it is a duplicate on this index
     * Find the index entry, and remove the duplicate */
    list_for_each_entry(tmp_entry, &s_steer->steer_entries[steer], list, struct mlx4_steer_index) {
        if (tmp_entry->index == index) {
            entry = tmp_entry;
            break;
        }
    }
    if (unlikely(!entry)) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Steering entry for index %x is not registered\n", index));
        return false;
    }
    list_for_each_entry_safe(dqp, tmp_dqp, &entry->duplicates, list, 
                            struct mlx4_promisc_qp, struct mlx4_promisc_qp) {
        if (dqp->qpn == qpn) {
            list_del(&dqp->list);
            kfree(dqp);
        }
    }
    return true;
}

/* I a steering entry contains only promisc QPs, it can be removed. */
static u8 can_remove_steering_entry(struct mlx4_dev *dev, u8 vep_num, u8 port,

                      enum mlx4_steer_type steer,
                      unsigned int index, u32 tqpn)
{
    struct mlx4_steer *s_steer;
    struct mlx4_cmd_mailbox *mailbox;
    struct mlx4_mgm *mgm;
    struct mlx4_steer_index *entry = NULL, *tmp_entry;
    u32 qpn;
    u32 members_count;
    bool ret = false;
    u32 i;
    u8 pf_num;

    pf_num = (dev->caps.num_ports == 1) ? vep_num : (vep_num << 1) | (port - 1);
    s_steer = &mlx4_priv(dev)->steer[pf_num];

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return false;
    mgm = (mlx4_mgm *)mailbox->buf;

    if (mlx4_READ_ENTRY(dev, index, mailbox))
        goto out;
    members_count = be32_to_cpu(mgm->members_count) & 0xffffff;
    for (i = 0;  i < members_count; i++) {
        qpn = be32_to_cpu(mgm->qp[i]) & MGM_QPN_MASK;
        if (!get_promisc_qp(dev, pf_num, steer, qpn) && qpn != tqpn) {
            /* the qp is not promisc, the entry can't be removed */
            goto out;
        }
    }
     /* All the qps currently registered for this entry are promiscuous,
      * it can be removed */
    ret = true;
    list_for_each_entry_safe(entry, tmp_entry, &s_steer->steer_entries[steer], list, 
                    struct mlx4_steer_index, struct mlx4_steer_index) {
        if (entry->index == index) {
            list_del(&entry->list);
            kfree(entry);
        }
    }

out:
    mlx4_free_cmd_mailbox(dev, mailbox);
    return ret;
}


static int add_promisc_qp(struct mlx4_dev *dev, u8 vep_num, u8 port,

              enum mlx4_steer_type steer, u32 qpn)
{
    struct mlx4_steer *s_steer;
    struct mlx4_cmd_mailbox *mailbox;
    struct mlx4_mgm *mgm;
    struct mlx4_steer_index *entry;
    struct mlx4_promisc_qp *pqp;
    struct mlx4_promisc_qp *dqp;
    u32 members_count;
    u32 prot;
    u32 i;
    bool found;
    int last_index;
    int err;
    u8 pf_num;

    ASSERT(port==1 || port ==2);
    pf_num = (dev->caps.num_ports == 1) ? vep_num : (vep_num << 1) | (port - 1);
    s_steer = &mlx4_priv(dev)->steer[pf_num];

    if (get_promisc_qp(dev, pf_num, steer, qpn))
        return 0; /* Noting to do, already exists */

    pqp = (mlx4_promisc_qp *)kmalloc(sizeof *pqp, GFP_KERNEL);
    if (!pqp)
        return -ENOMEM;
    pqp->qpn = qpn;

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox)) {
        err = -ENOMEM;
        goto out_alloc;
    }
    mgm = (mlx4_mgm *)mailbox->buf;

    /* the promisc qp needs to be added for each one of the steering
     * entries, if it already exists, needs to be added as a duplicate
     * for this entry */
    list_for_each_entry(entry, &s_steer->steer_entries[steer], list, struct mlx4_steer_index) {
        err = mlx4_READ_ENTRY(dev, entry->index, mailbox);
        if (err)
            goto out_mailbox;

        members_count = be32_to_cpu(mgm->members_count) & 0xffffff;
        prot = be32_to_cpu(mgm->members_count) >> 30;
        found = false;
        for (i = 0; i < members_count; i++) {
            if ((be32_to_cpu(mgm->qp[i]) & MGM_QPN_MASK) == qpn) {
                /* Entry already exists, add to duplicates */
                dqp = (mlx4_promisc_qp *)kmalloc(sizeof *dqp, GFP_KERNEL);
                if (!dqp)
                    goto out_mailbox;
                dqp->qpn = qpn;
                list_add_tail(&dqp->list, &entry->duplicates);
                found = true;
            }
        }
        if (!found) {
            /* Need to add the qpn to mgm */
            if (members_count ==MLX4_QP_PER_MGM ) {
                /* entry is full */
                err = -ENOMEM;
                goto out_mailbox;
            }
            mgm->qp[members_count++] = cpu_to_be32(qpn & MGM_QPN_MASK);
            mgm->members_count = cpu_to_be32(members_count | (prot << 30));
            err = mlx4_WRITE_ENTRY(dev, entry->index, mailbox);
            if (err) {
                goto out_mailbox;
            }
        }
        last_index = entry->index;
    }

    /* add the new qpn to list of promisc qps */
    list_add_tail(&pqp->list, &s_steer->promisc_qps[steer]);
    /* now need to add all the promisc qps to default entry */
    memset(mgm, 0, sizeof *mgm);
//  mgm->gid[7] = pf_num << 4;
    members_count = 0;
    list_for_each_entry(dqp, &s_steer->promisc_qps[steer], list, struct mlx4_promisc_qp)
        mgm->qp[members_count++] = cpu_to_be32(dqp->qpn & MGM_QPN_MASK);
    mgm->members_count = cpu_to_be32(members_count | MLX4_PROT_ETH << 30);

    err = mlx4_WRITE_PROMISC(dev, vep_num, port, (u8)steer, mailbox);
    if (err)
        goto out_list;

    mlx4_free_cmd_mailbox(dev, mailbox);
    return 0;

out_list:
    list_del(&pqp->list);
out_mailbox:
    /* TODO: undo partial addition of promisc qps */
    mlx4_free_cmd_mailbox(dev, mailbox);
out_alloc:
    kfree(pqp);
    return err;
}

static int remove_promisc_qp(struct mlx4_dev *dev, u8 vep_num, u8 port,
                 enum mlx4_steer_type steer, u32 qpn)
{
    struct mlx4_steer *s_steer;
    struct mlx4_cmd_mailbox *mailbox;
    struct mlx4_mgm *mgm;
    struct mlx4_steer_index *entry;
    struct mlx4_promisc_qp *pqp;
    struct mlx4_promisc_qp *dqp;
    u32 members_count;
    bool found;
    bool back_to_list = false;
    int loc;
    u32 i;
    int err;
    u8 pf_num;

    ASSERT(port==1 || port ==2);

    pf_num = (dev->caps.num_ports == 1) ? vep_num : (vep_num << 1) | (port - 1);
    s_steer = &mlx4_priv(dev)->steer[pf_num];

    pqp = get_promisc_qp(dev, pf_num, steer, qpn);
    if (unlikely(!pqp)) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "QP %x is not promiscuous QP\n", qpn));
        /* nothing to do */
        return 0;
    }

    /*remove from list of promisc qps */
    list_del(&pqp->list);

    /* set the default entry not to include the removed one */
    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox)) {
        err = -ENOMEM;
        back_to_list = true;
        goto out_list;
    }

    mgm = (mlx4_mgm *)mailbox->buf;
//  mgm->gid[7] = pf_num << 4;
    members_count = 0;
    list_for_each_entry(dqp, &s_steer->promisc_qps[steer], list, struct mlx4_promisc_qp)
        mgm->qp[members_count++] = cpu_to_be32(dqp->qpn & MGM_QPN_MASK);
    mgm->members_count = cpu_to_be32(members_count | MLX4_PROT_ETH << 30);

    err = mlx4_WRITE_PROMISC(dev,  vep_num, port, (u8)steer, mailbox);
    if (err)
        goto out_mailbox;

    /* remove the qp from all the steering entries*/
    list_for_each_entry(entry, &s_steer->steer_entries[steer], list, struct mlx4_steer_index) {
        found = false;
        list_for_each_entry(dqp, &entry->duplicates, list, struct mlx4_promisc_qp) {
            if (dqp->qpn == qpn) {
                found = true;
                break;
            }
        }
        if (found) {
            /* a duplicate, no need to change the mgm,
             * only update the duplicates list */
            list_del(&dqp->list);
            kfree(dqp);
        } else {
            err = mlx4_READ_ENTRY(dev, entry->index, mailbox);
            if (err) {
                goto out_mailbox;
            }
            members_count = be32_to_cpu(mgm->members_count) & 0xffffff;
            for (loc = -1, i = 0; i < members_count; ++i)
                if ((be32_to_cpu(mgm->qp[i]) & MGM_QPN_MASK) == qpn)
                    loc = i;

            ASSERT(loc >=0);
            mgm->members_count = cpu_to_be32(--members_count |
                             (MLX4_PROT_ETH << 30));
            mgm->qp[loc] = mgm->qp[i - 1];
            mgm->qp[i - 1] = 0;

            err = mlx4_WRITE_ENTRY(dev, entry->index, mailbox);
            if (err)
                goto out_mailbox;
        }

    }

out_mailbox:
    mlx4_free_cmd_mailbox(dev, mailbox);
out_list:
    if (back_to_list)
        list_add_tail(&pqp->list, &s_steer->promisc_qps[steer]);

    kfree(pqp);

    return err;
}


/*
 * Caller must hold MCG table semaphore.  gid and mgm parameters must
 * be properly aligned for command interface.
 *
 *  Returns 0 unless a firmware command error occurs.
 *
 * If GID is found in MGM or MGM is empty, *index = *hash, *prev = -1
 * and *mgm holds MGM entry.
 *
 * if GID is found in AMGM, *index = index in AMGM, *prev = index of
 * previous entry in hash chain and *mgm holds AMGM entry.
 *
 * If no AMGM exists for given gid, *index = -1, *prev = index of last
 * entry in hash chain and *mgm holds end of hash chain.
 */
static int find_entry(struct mlx4_dev *dev,
            u8 *gid, enum mlx4_protocol prot,
            struct mlx4_cmd_mailbox *mgm_mailbox,
            u16 *hash, int *prev, int *index)
{
    struct mlx4_cmd_mailbox *mailbox;
    struct mlx4_mgm *mgm = (mlx4_mgm *)mgm_mailbox->buf;
    u8 *mgid;
    int err;
    u8 op_mod = (prot == MLX4_PROT_ETH)? (u8)!!(dev->caps.vep_mc_steering) : 0;

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return -ENOMEM;
    mgid = (u8*)mailbox->buf;

    memcpy(mgid, gid, 16);

    err = mlx4_GID_HASH(dev, mailbox, hash, op_mod);
    mlx4_free_cmd_mailbox(dev, mailbox);
    if (err)
        return err;
    
//  MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Hash for %pI6 is %04x\n", dev->pdev->name, gid, *hash));

    *index = *hash;
    *prev  = -1;

    do {
        err = mlx4_READ_ENTRY(dev, *index, mgm_mailbox);
        if (err)
            return err;

        if (!(be32_to_cpu(mgm->members_count) & 0xffffff)) {
            if (*index != *hash) {                
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Found zero MGID in AMGM.\n", dev->pdev->name));
                err = -EINVAL;
            }
            return err;
        }

        if (!memcmp(mgm->gid, gid, 16) &&
            (prot == (enum mlx4_protocol)(be32_to_cpu(mgm->members_count) >> 30)))
            return err;

        *prev = *index;
        *index = be32_to_cpu(mgm->next_gid_index) >> 6;
    } while (*index);

    *index = -1;
    return err;
}

/* Attach mailbox filled with mgm at the tail of the list.
index - index of the mgm
link - true if the new mgm will be linked to another mgm with the same hash
prev - if link is true, index of the previous mgm in the list
*/
int attach_mgm_at_tail(struct mlx4_dev *dev, int index, int link, int prev, 
	struct mlx4_cmd_mailbox *mailbox)
{
    int err;
    struct mlx4_mgm *mgm;

    mgm = (struct mlx4_mgm *) mailbox->buf;

	err = mlx4_WRITE_ENTRY(dev, index, mailbox);
    if (err)
        goto out;

    if (!link)
        goto out;

    err = mlx4_READ_ENTRY(dev, prev, mailbox);
    if (err)
        goto out;

    mgm->next_gid_index = cpu_to_be32(index << 6);

    err = mlx4_WRITE_ENTRY(dev, prev, mailbox); 
    if (err)
        goto out;
out:
	return err;
}

/* Attach mailbox filled with mgm at the head of the list.
index - index of the mgm
link - true if the new mgm will be linked to another mgm with the same hash
head - if link is true, index of the head mgm in the list
*/
int attach_mgm_at_head(struct mlx4_dev *dev, int index, int link, int head, 
	struct mlx4_cmd_mailbox *mailbox)
{
    int err = 0;
    struct mlx4_mgm *mgm, *head_mgm;
    struct mlx4_cmd_mailbox *head_mgm_mailbox;

    mgm = (struct mlx4_mgm *) mailbox->buf;

	if(! link)
	{
	    err = mlx4_WRITE_ENTRY(dev, index, mailbox);
	    if (err)
	        goto out;
	}
	
    if (!link)
        goto out;

    head_mgm_mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(head_mgm_mailbox))
	{
	   err = PTR_ERR(head_mgm_mailbox);
	   goto out;
	}
	
    head_mgm = (struct mlx4_mgm *) head_mgm_mailbox->buf;

	err = mlx4_READ_ENTRY(dev, head, head_mgm_mailbox);
	if (err)
		goto out;

	err = mlx4_WRITE_ENTRY(dev, index, head_mgm_mailbox); 
	if (err)
		goto out;

	mgm->next_gid_index = cpu_to_be32(index << 6);

	err = mlx4_WRITE_ENTRY(dev, head, mailbox); 
	if (err)
		goto out;

	mlx4_free_cmd_mailbox(dev, head_mgm_mailbox);

out:
	return err;
}

int mlx4_qp_attach_common(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
    int block_mcast_loopback, enum mlx4_protocol prot,
    enum mlx4_steer_type steer, int attach_at_head, 
    int block_lb)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mgm *mgm;
	u32 members_count;
    u16 hash;
    int index, prev;
    int link = 0;
    unsigned i;
    int err;
    u8 vep_num = gid[4];
    u8 port = gid[5];

    u8 new_entry = 0;

    UNUSED_PARAM(block_mcast_loopback);
    
    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    mgm = (mlx4_mgm *)mailbox->buf;

    mutex_lock(&priv->mcg_table.mutex);

    err = find_entry(dev, gid, prot, mailbox, &hash, &prev, &index);
    if (err)
        goto out;

    if (index != -1) {
        if (!(be32_to_cpu(mgm->members_count) & 0xffffff)) {
            new_entry = 1;
            memcpy(mgm->gid, gid, 16);
        }
    } else {
        link = 1;

        index = mlx4_bitmap_alloc(&priv->mcg_table.bitmap);
        if (index == -1) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: No AMGM entries left\n", dev->pdev->name));
            err = -ENOMEM;
            goto out;
        }
        index += dev->caps.num_mgms;

        memset(mgm, 0, sizeof *mgm);
        memcpy(mgm->gid, gid, 16);
    }

    members_count = be32_to_cpu(mgm->members_count) & 0xffffff;
    if (members_count == MLX4_QP_PER_MGM) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: MGM at index %x is full.\n", dev->pdev->name, index));
        err = -ENOMEM;
        goto out;
    }

    for (i = 0; i < members_count; ++i)
        if ((int)(be32_to_cpu(mgm->qp[i]) & MGM_QPN_MASK) == qp->qpn) {
            MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: QP %06x already a member of MGM\n", 
                dev->pdev->name, qp->qpn));
            err = 0;
            goto out;
        }

	mgm->qp[members_count++] = cpu_to_be32((qp->qpn & MGM_QPN_MASK) |
					   (!!g.mlx4_blck_lb << MGM_BLCK_LB_BIT) |
					   (!!(block_lb && (dev->pdev->revision_id != 0xA0)) << MGM_BLCK_LB_BIT));
	
	mgm->members_count		 = cpu_to_be32(members_count | ((u32) prot << 30));

	if(! attach_at_head)
	{// new entry should be attached at the tail of the list
		err = attach_mgm_at_tail(dev, index, link, prev, mailbox);
	    if (err)
	        goto out;
	}
	else
	{// new entry should be attached at the head of the list	
		attach_mgm_at_head(dev, index, link, hash, mailbox);
	}
out:
    if (prot == MLX4_PROT_ETH) {
        /* manage the steering entry for promisc mode */
        if (new_entry)
            err = new_steering_entry(dev, vep_num, port, steer, index, qp->qpn);
        else
            err = existing_steering_entry(dev, vep_num, port, steer, index, qp->qpn);
        /* TODO handle an error flow here, need to clean the MGMS */
    }
    
    if (err && link && index != -1) {
        if (index < dev->caps.num_mgms) {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Got AMGM index %d < %d",
                dev->pdev->name, index, dev->caps.num_mgms));
        }
        else
            mlx4_bitmap_free(&priv->mcg_table.bitmap,
                     index - dev->caps.num_mgms);
    }
    mutex_unlock(&priv->mcg_table.mutex);

    mlx4_free_cmd_mailbox(dev, mailbox);
    return err;
}

int mlx4_qp_detach_common(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
    enum mlx4_protocol prot, enum mlx4_steer_type steer)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    struct mlx4_cmd_mailbox *mailbox;
    struct mlx4_mgm *mgm;
    u32 members_count;
    u16 hash;
    int prev, index;
    int i, loc;
    int err;
    u8 vep_num = gid[4];
    u8 port = gid[5];


    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    mgm = (mlx4_mgm *)mailbox->buf;

    mutex_lock(&priv->mcg_table.mutex);

    err = find_entry(dev, gid, prot, mailbox, &hash, &prev, &index);
    if (err)
        goto out;

    if (index == -1) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: MGID %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x "
              "not found\n", dev->pdev->name,
              be16_to_cpu(((__be16 *) gid)[0]),
              be16_to_cpu(((__be16 *) gid)[1]),
              be16_to_cpu(((__be16 *) gid)[2]),
              be16_to_cpu(((__be16 *) gid)[3]),
              be16_to_cpu(((__be16 *) gid)[4]),
              be16_to_cpu(((__be16 *) gid)[5]),
              be16_to_cpu(((__be16 *) gid)[6]),
              be16_to_cpu(((__be16 *) gid)[7])));
        err = -EINVAL;
        goto out;
    }

    /* if this pq is also a promisc qp, it shouldn't be removed */
    if (prot == MLX4_PROT_ETH &&
        check_duplicate_entry(dev, vep_num, port, steer, index, qp->qpn))
        goto out;

    members_count = be32_to_cpu(mgm->members_count) & 0xffffff;
    for (loc = -1, i = 0; i < (int)members_count; ++i)
        if ((int)(be32_to_cpu(mgm->qp[i]) & MGM_QPN_MASK) == qp->qpn)
            loc = i;

    if (loc == -1) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: QP %06x not found in MGM\n", dev->pdev->name, qp->qpn));
        err = -EINVAL;
        goto out;
    }


    mgm->members_count = cpu_to_be32(--members_count | ((u32) prot << 30));
    mgm->qp[loc]       = mgm->qp[i - 1];
    mgm->qp[i - 1]     = 0;

    err = mlx4_WRITE_ENTRY(dev, index, mailbox);
    if (err)
        goto out;

    if (prot == MLX4_PROT_ETH) {
        if (!can_remove_steering_entry(dev, vep_num, port, steer, index, qp->qpn) && i != 1) {
            goto out;
        }
    } else {
        if (i != 1)
            goto out;
    }

    /* We are going to delete the entry, members count should be 0 */
    mgm->members_count = cpu_to_be32((u32) prot << 30);

    if (prev == -1) {
        /* Remove entry from MGM */
        int amgm_index = be32_to_cpu(mgm->next_gid_index) >> 6;
        if (amgm_index) {
            err = mlx4_READ_ENTRY(dev, amgm_index, mailbox);
            if (err)
                goto out;
        } else {
            memset(mgm->gid, 0, 16);
        } 

        err = mlx4_WRITE_ENTRY(dev, index, mailbox);
        if (err)
            goto out;

        if (amgm_index) {
            if (amgm_index < dev->caps.num_mgms) {                
                MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: MGM entry %d had AMGM index %d < %d",
                    dev->pdev->name, index, amgm_index, dev->caps.num_mgms));
            }
            else
                mlx4_bitmap_free(&priv->mcg_table.bitmap,
                         amgm_index - dev->caps.num_mgms);
        }
    } else {
        /* Remove entry from AMGM */
        int cur_next_index = be32_to_cpu(mgm->next_gid_index);
        err = mlx4_READ_ENTRY(dev, prev, mailbox);
        if (err)
            goto out;

        mgm->next_gid_index = cpu_to_be32(cur_next_index);

        err = mlx4_WRITE_ENTRY(dev, prev, mailbox);
        if (err)
            goto out;

        if (index < dev->caps.num_mgms) {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: entry %d had next AMGM index %d < %d",
                dev->pdev->name, prev, index, dev->caps.num_mgms));
        }
        else
            mlx4_bitmap_free(&priv->mcg_table.bitmap,
                     index - dev->caps.num_mgms);
    }

out:
    mutex_unlock(&priv->mcg_table.mutex);

    mlx4_free_cmd_mailbox(dev, mailbox);
    return err;
}

int mlx4_MCAST_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
                             struct mlx4_cmd_mailbox *inbox,
                             struct mlx4_cmd_mailbox *outbox)
{
    struct mlx4_qp qp; /* dummy for calling attach/detach */
    u8 *gid = (u8*)inbox->buf;
    enum mlx4_protocol prot = (mlx4_protocol)((vhcr->in_modifier >> 28) & 0x7);
    u8 vep_num = mlx4_priv(dev)->mfunc.master.slave_state[slave].vep_num;
    int err =0;

    UNUSED_PARAM(outbox);
    ASSERT(mlx4_is_master(dev));
    
    if (prot == MLX4_PROT_ETH) {
        gid[4] = vep_num;
        gid[7] |= (MLX4_MC_STEER << 1);
    }

    qp.qpn = vhcr->in_modifier & 0xffffff;
    if (vhcr->op_modifier)
        err = mlx4_qp_attach_common(dev, &qp, gid,
                vhcr->in_modifier >> 31, prot, MLX4_MC_STEER, 
                FALSE /* attach at the end */, FALSE /* block loopback */);
    else
        err = mlx4_qp_detach_common(dev, &qp, gid, prot, MLX4_MC_STEER);

    if (!err)
        if (vhcr->op_modifier) {
            err = mlx4_add_mcg_to_tracked_qp(dev, qp.qpn, gid, prot) ;
            if (err) {
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Failed to mlx4_add_mcg_to_tracked_qp\n"));
                /*return it back*/
                mlx4_qp_detach_common(dev, &qp, gid, prot, MLX4_MC_STEER);
            }
        }
        else
            err = mlx4_remove_mcg_from_tracked_qp(dev, qp.qpn, gid);

    return err;
}

static int mlx4_MCAST(struct mlx4_dev *dev, struct mlx4_qp *qp,
              u8 gid[16], u8 attach, u8 block_loopback,
              enum mlx4_protocol prot)
{
    struct mlx4_cmd_mailbox *mailbox;
    int err;
    int qpn;

    if (!mlx4_is_slave(dev))
        return -EBADF;

    mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    memcpy(mailbox->buf, gid, 16);
    qpn = qp->qpn;
    qpn |= (prot << 28);
    if (attach && block_loopback)
        qpn |= (1 << 31);

    err = mlx4_cmd(dev, mailbox->dma.da, qpn, attach, MLX4_CMD_MCAST_ATTACH,
                               MLX4_CMD_TIME_CLASS_A);
    mlx4_free_cmd_mailbox(dev, mailbox);
    return err;
}


int mlx4_multicast_attach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
              int block_mcast_loopback, enum mlx4_protocol prot, int block_lb)
{
    if (prot == MLX4_PROT_ETH && !dev->caps.vep_mc_steering)
        return 0;

    if (mlx4_is_slave(dev))
        return mlx4_MCAST(dev, qp, gid, 1, (u8)block_mcast_loopback, prot);

    if (prot == MLX4_PROT_ETH) {
        gid[4] = (u8)(dev->caps.vep_num);
        gid[7] |= (MLX4_MC_STEER << 1);
    }

    return mlx4_qp_attach_common(dev, qp, gid, block_mcast_loopback, prot, MLX4_MC_STEER,
		FALSE /* attach at the end */, block_lb /* block loopback */);
}

int mlx4_multicast_detach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
                        enum mlx4_protocol prot)
{
    if (prot == MLX4_PROT_ETH && !dev->caps.vep_mc_steering)
        return 0;
    
    if (mlx4_is_slave(dev))
        return mlx4_MCAST(dev, qp, gid, 0, 0, prot);

    if (prot == MLX4_PROT_ETH) {
        gid[4] = (u8)(dev->caps.vep_num);
        gid[7] |= (MLX4_MC_STEER << 1);
    }

    return mlx4_qp_detach_common(dev, qp, gid, prot, MLX4_MC_STEER);
}

int mlx4_PROMISC_wrapper(struct mlx4_dev *dev, int slave,
             struct mlx4_vhcr *vhcr,
             struct mlx4_cmd_mailbox *inbox,
             struct mlx4_cmd_mailbox *outbox)
{
    u8 vep_num = mlx4_priv(dev)->mfunc.master.slave_state[slave].vep_num;

    u32 qpn = (u32) vhcr->in_param & 0xffffffff;
    u8 port = (u8)(vhcr->in_param >> 62);
    enum mlx4_steer_type steer = (mlx4_steer_type)vhcr->in_modifier;
    ASSERT(port==1 || port ==2);

    UNUSED_PARAM(inbox);
    UNUSED_PARAM(outbox);
    
    if (vhcr->op_modifier)
        return add_promisc_qp(dev, vep_num, port, steer, qpn);
    else
        return remove_promisc_qp(dev, vep_num, port, steer, qpn);
}

static int mlx4_PROMISC(struct mlx4_dev *dev, u32 qpn,
            enum mlx4_steer_type steer, u8 add, u8 port)
{
    ASSERT(port==1 || port ==2);
    return mlx4_cmd(dev, ((u64) qpn | (u64) port << 62), (u32) steer, add,
            MLX4_CMD_PROMISC, MLX4_CMD_TIME_CLASS_A);
}

int mlx4_multicast_promisc_add(struct mlx4_dev *dev, u32 qpn, u8 port)
{   
    if (!dev->caps.vep_mc_steering)
        return 0;
    ASSERT(port==1 || port ==2);
    
    if (mlx4_is_slave(dev))
        return mlx4_PROMISC(dev, qpn, MLX4_MC_STEER, 1, port  );

    return add_promisc_qp(dev, (u8)(dev->caps.vep_num), port, MLX4_MC_STEER, qpn);
}

int mlx4_multicast_promisc_remove(struct mlx4_dev *dev, u32 qpn, u8 port)
{

    if (!dev->caps.vep_mc_steering)
        return 0;

   ASSERT(port==1 || port ==2);

    if (mlx4_is_slave(dev))
        return mlx4_PROMISC(dev, qpn, MLX4_MC_STEER, 0, port );

    return remove_promisc_qp(dev, (u8)(dev->caps.vep_num), port, MLX4_MC_STEER, qpn);
}

int mlx4_unicast_promisc_add(struct mlx4_dev *dev, u32 qpn, u8 port)
{
    if (!dev->caps.vep_mc_steering)
        return 0;

    ASSERT(port==1 || port ==2);

    if (mlx4_is_slave(dev))
        return mlx4_PROMISC(dev, qpn, MLX4_UC_STEER, 1, port ); 

    return add_promisc_qp(dev, (u8)(dev->caps.vep_num), port, MLX4_UC_STEER, qpn);
}

int mlx4_unicast_promisc_remove(struct mlx4_dev *dev, u32 qpn, u8 port)
{
    
    if (!dev->caps.vep_mc_steering)
        return 0;

    ASSERT(port==1 || port ==2);

    if (mlx4_is_slave(dev))
        return mlx4_PROMISC(dev, qpn, MLX4_UC_STEER, 0, port );

    return remove_promisc_qp(dev, (u8)(dev->caps.function >> 1), port, MLX4_UC_STEER, qpn);
}

int mlx4_enable_unicast_promisc(struct mlx4_dev *dev, u8 port, u32 qpn)
{
    int err = 0;
    
    if (!dev->caps.vep_uc_steering)
        err = mlx4_SET_PORT_qpn_calc(dev, port, qpn, 1, false);
    else
        err = mlx4_unicast_promisc_add(dev, qpn, port);

    return err;
}

int mlx4_disable_unicast_promisc(struct mlx4_dev *dev, u8 port, u32 qpn)
{
    int err = 0;
    
    if (!dev->caps.vep_uc_steering)
        err = mlx4_SET_PORT_qpn_calc(dev, port, qpn, 0, false);
    else 
        err = mlx4_unicast_promisc_remove(dev, qpn, port);

    return err;
}

int mlx4_init_mcg_table(struct mlx4_dev *dev)
{
    struct mlx4_priv *priv = mlx4_priv(dev);
    int err;

    /* Nothing to do for slaves - mcg handling is para-virtualized */
    if (mlx4_is_slave(dev))
        return 0;

    err = mlx4_bitmap_init(&priv->mcg_table.bitmap,
                   dev->caps.num_amgms, dev->caps.num_amgms - 1, 0);
    if (err)
        return err;

    mutex_init(&priv->mcg_table.mutex);

    return 0;
}

void mlx4_cleanup_mcg_table(struct mlx4_dev *dev)
{
    if (mlx4_is_slave(dev))
        return;
    mlx4_bitmap_cleanup(&mlx4_priv(dev)->mcg_table.bitmap);
}

void mlx4_print_mcg_table(struct mlx4_dev *dev)
{
	int i, j, err;
    struct mlx4_cmd_mailbox *mgm_mailbox;
    struct mlx4_mgm *mgm;
	int members_count;
	
    mgm_mailbox = mlx4_alloc_cmd_mailbox(dev);
    if (IS_ERR(mgm_mailbox))
        return;
	mgm = (struct mlx4_mgm *) mgm_mailbox->buf;

	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
		("======================== MCG Table Start =========================\n"));
	
	for(i = 0; i < dev->caps.num_amgms + dev->caps.num_mgms; i++)
	{
        err = mlx4_READ_ENTRY(dev, i, mgm_mailbox);
        if (err)
            return;

		members_count = (int) be32_to_cpu(mgm->members_count) & 0xffffff;

		if(members_count == 0)
		{
			continue;
		}
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			( "MGM index %d: \n\t\t\tMGID %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n\t\t\t\
			   num members: %d\n\t\t\tnext gid index: %d\n\t\t\tQPs: ",
				i,
				be16_to_cpu(((__be16 *) mgm->gid)[0]),
		  		be16_to_cpu(((__be16 *) mgm->gid)[1]),
		  		be16_to_cpu(((__be16 *) mgm->gid)[2]),
				be16_to_cpu(((__be16 *) mgm->gid)[3]),
				be16_to_cpu(((__be16 *) mgm->gid)[4]),
			    be16_to_cpu(((__be16 *) mgm->gid)[5]),
		   	    be16_to_cpu(((__be16 *) mgm->gid)[6]),
				be16_to_cpu(((__be16 *) mgm->gid)[7]),
				members_count,
				be32_to_cpu(mgm->next_gid_index) >> 6));
		for(j = 0; j < members_count; j++)
		{			
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%d ", be32_to_cpu(mgm->qp[j])));
		}
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, ("\n"));
	}
	
	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
		("======================== MCG Table End =========================\n"));
	mlx4_free_cmd_mailbox(dev, mgm_mailbox);
}

