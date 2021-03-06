/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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
#include <mlx4_debug.h>
#include <../net/mlx4.h>
#include "mlx4_ib.h"
#include "cq.h"
#include "qp.h"
#include "mx_abi.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "cq.tmh"
#endif

/* Which firmware version adds support for Resize CQ */
#define MLX4_FW_VER_RESIZE_CQ  mlx4_fw_ver(2, 5, 0)


static void mlx4_ib_cq_comp(struct mlx4_cq *cq)
{
	struct ib_cq *ibcq = &to_mibcq(cq)->ibcq;
	ibcq->comp_handler(ibcq->cq_context);
}

static void mlx4_ib_cq_event(struct mlx4_cq *cq, enum mlx4_event type)
{
	ib_event_rec_t event;
	struct ib_cq *ibcq;

    if (type == MLX4_EVENT_TYPE_CQ_ERROR) {
        event.type = IB_AE_CQ_ERROR;
    }
    else if (type == MLX4_EVENT_TYPE_CQ_OVERFLOW) {
        event.type = IB_AE_CQ_OVERFLOW;
    }
    else {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "mlx4_ib: Unexpected event type %d "
		       "on CQ %06x\n", type, cq->cqn));
		return;
	}

	ibcq = &to_mibcq(cq)->ibcq;
	if (ibcq->event_handler) {
		event.context = ibcq->cq_context;
		event.vendor_specific = type;
		ibcq->event_handler(&event);
	}
}

static void *get_cqe_from_buf(struct mlx4_ib_cq_buf *buf, int n)
{
	return mlx4_buf_offset(&buf->buf, n * buf->entry_size);
}

static void *get_cqe(struct mlx4_ib_cq *cq, int n)
{
	return get_cqe_from_buf(&cq->buf, n);
}

static void *get_sw_cqe(struct mlx4_ib_cq *cq, int n)
{
	struct mlx4_cqe *cqe = (mlx4_cqe *)get_cqe(cq, n & cq->ibcq.cqe);
	struct mlx4_cqe *tcqe = ((cq->buf.entry_size == 64) ? (cqe + 1) : cqe);

	return (!!(tcqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK) ^
		!!(n & (cq->ibcq.cqe + 1))) ? NULL : cqe;
}

static struct mlx4_cqe *next_cqe_sw(struct mlx4_ib_cq *cq)
{
	return (mlx4_cqe *)get_sw_cqe(cq, cq->mcq.cons_index);
}

int mlx4_ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period)
{
	struct mlx4_ib_cq *mcq = to_mcq(cq);
	struct mlx4_ib_dev *dev = to_mdev(cq->device);
	struct mlx4_cq_context *context;
	int err;

	if (mlx4_is_barred(dev->dev)){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
		( "%s: MLX4_FLAG_RESET_DRIVER (mlx4_is_barred) flag is set.\n",
			dev->ib_dev.name));
		
		return -EFAULT;
	}
	
	context = (mlx4_cq_context *)kzalloc(sizeof *context, GFP_KERNEL);
	if (!context){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
		( "%s: mem alloc for context failed.\n",
			dev->ib_dev.name));
	
		return -ENOMEM;
	}
	context->cq_period = cpu_to_be16(cq_period);
	context->cq_max_count = cpu_to_be16(cq_count);
	err = mlx4_cq_modify(dev->dev, &mcq->mcq, context, 1);

	kfree(context);
	return err;
}

struct ib_cq *mlx4_ib_create_cq_internal(struct ib_device *ibdev, int entries, int vector,
				struct ib_ucontext *context,
				struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_cq *cq;
	struct mlx4_uar *uar;
	ULONG buf_size;
	int err;
	int print_cqe = 0;

	if ( entries & IB_CQ_PRINT_CQE_FLAG ) {
		entries &= ~IB_CQ_PRINT_CQE_FLAG;
		print_cqe = 1;
	}
	
	if (mlx4_is_barred(ibdev->dma_device)){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: MLX4_FLAG_RESET_DRIVER (mlx4_is_barred) flag is set.\n",
				ibdev->name));
		
		return (ib_cq *)ERR_PTR(-EFAULT);
	}
	
	if (entries < 1 || entries > dev->dev->caps.max_cqes){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: failed with invalid param: entries. val:%d, must be between 1 and %d\n",
				ibdev->name, entries, dev->dev->caps.max_cqes));
		
		return (ib_cq *)ERR_PTR(-EINVAL);
	}
	
	cq = (mlx4_ib_cq *)kzalloc(sizeof *cq, GFP_KERNEL);
	if (!cq) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			( "%s: failed to alloc mem for cq.\n", 
				ibdev->name));
		
		return (ib_cq *)ERR_PTR(-ENOMEM);
	}

	entries      = roundup_pow_of_two(entries + 1);
	cq->ibcq.cqe = entries - 1;
	buf_size     = entries * dev->dev->caps.cqe_size;
	spin_lock_init(&cq->lock);
	cq->print_cqe = print_cqe;

	if (context != NULL) {
		struct ibv_create_cq ucmd;

        NT_ASSERT(udata != NULL);

		if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: ib_copy_from_udata() failed, trying to copy more mem from user than availiable.\n",
				 	ibdev->name));			

			err = -EFAULT;
			goto err_cq;
		}

        if( NdValidateMemoryMapping( &ucmd.mappings[mlx4_ib_create_cq_buf],
                                     NdModifyAccess, buf_size ) != STATUS_SUCCESS )
        {
            err = -EFAULT;
            goto err_cq;
        }

        if( NdValidateCoallescedMapping( &ucmd.mappings[mlx4_ib_create_cq_db],
                                         NdWriteAccess, sizeof(UINT64) ) != STATUS_SUCCESS )
        {
            err = -EFAULT;
            goto err_cq;
        }

        if( NdValidateMemoryMapping( &ucmd.mappings[mlx4_ib_create_cq_arm_sn],
                                     NdModifyAccess, sizeof(int) ) != STATUS_SUCCESS )
        {
            err = -EFAULT;
            goto err_cq;
        }

		cq->umem = ib_umem_get(context, ucmd.mappings[mlx4_ib_create_cq_buf].MapMemory.Address,
                               ucmd.mappings[mlx4_ib_create_cq_buf].MapMemory.CbLength,
				               (ib_access_flags)(IB_ACCESS_LOCAL_WRITE | IB_ACCESS_NO_SECURE));
		if (IS_ERR(cq->umem)) {
			err = PTR_ERR(cq->umem);
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: ib_umem_get() failed with error:%d.\n",
					ibdev->name, err));
			
			goto err_cq;
		}

		err = mlx4_mtt_init(dev->dev, ib_umem_page_count(cq->umem),
				    ilog2(cq->umem->page_size), &cq->buf.mtt);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: mlx4_mtt_init() failed - mem alloc related.\n",
					ibdev->name));
			
			goto err_buf;
		}

		err = mlx4_ib_umem_write_mtt(dev, &cq->buf.mtt, cq->umem);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: mlx4_ib_umem_write_mtt() failed with error:%d.\n",
					ibdev->name, err));
			
			goto err_mtt;
		}

		err = mlx4_ib_db_map_user(to_mucontext(context), ucmd.mappings[mlx4_ib_create_cq_db].MapMemory.Address,
					  &cq->db);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: mlx4_ib_db_map_user() failed with error:%d.\n", 
					ibdev->name, err));
			
			goto err_mtt;
		}

		// add mapping to user's arm_sn variable
		// we have no way pass the completion event to provider library
		// so we'll increment user's arm_sn in kernel
		err = ib_umem_map( context, ucmd.mappings[mlx4_ib_create_cq_arm_sn].MapMemory.Address, 
            ucmd.mappings[mlx4_ib_create_cq_arm_sn].MapMemory.CbLength, 
			IB_ACCESS_LOCAL_WRITE, &cq->mcq.mdl, &cq->mcq.p_u_arm_sn );
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: ib_umem_map() failed with error:%d.\n",
					ibdev->name, err));
			
			goto err_dbmap;
		}

		uar = &to_mucontext(context)->uar;
	} else {
		err = mlx4_ib_db_alloc(dev, &cq->db, 1);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: mlx4_ib_db_alloc() failed  - mem alloc related.\n",
				ibdev->name));

			goto err_cq;
		}

		cq->mcq.set_ci_db  = cq->db.db;
		cq->mcq.arm_db     = cq->db.db + 1;
		*cq->mcq.set_ci_db = 0;
		*cq->mcq.arm_db    = 0;

		if (mlx4_buf_alloc(dev->dev, buf_size, MAX_DIRECT_ALLOC_SIZE, &cq->buf.buf)) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: mlx4_buf_alloc() failed for buf_size %d (entries %d) "
				   "on CQ %06x\n", ibdev->name, buf_size, entries, cq->mcq.cqn));
			
			err = -ENOMEM;
			goto err_db;
		}
		cq->buf.entry_size = dev->dev->caps.cqe_size;

		err = mlx4_mtt_init(dev->dev, cq->buf.buf.npages, cq->buf.buf.page_shift,
				    &cq->buf.mtt);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: mlx4_mtt_init() failed - mem alloc related.\n",
					ibdev->name));

			goto err_buf;
		}

		err = mlx4_buf_write_mtt(dev->dev, &cq->buf.mtt, &cq->buf.buf);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				( "%s: mlx4_buf_write_mtt() failed with error:%d.\n",
					ibdev->name, err));

			goto err_mtt;
		}

		cq->mcq.p_u_arm_sn = NULL;
		uar = &dev->priv_uar;
	}

	err = mlx4_cq_alloc(dev->dev, entries, &cq->buf.mtt, uar,
		cq->db.dma.da, &cq->mcq, vector, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			( "%s: mlx4_cq_alloc() failed with error:%d.\n",
				ibdev->name, err));

		goto err_dbmap;
	}

	cq->mcq.comp  = mlx4_ib_cq_comp;
	cq->mcq.event = mlx4_ib_cq_event;

	if (context != NULL) {
        struct ibv_create_cq_resp resp;
        RtlZeroMemory(&resp, sizeof(resp));
        resp.cqn = cq->mcq.cqn;
        resp.cqe = cq->ibcq.cqe;
		if (ib_copy_to_udata(udata, &resp, sizeof(resp))) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: ib_copy_to_udata() failed, trying to copy more mem to user than possible.\n",
				 	ibdev->name));	
			err = -EFAULT;
			goto err_dbmap;
		}
    }
	return &cq->ibcq;

err_dbmap:
	ib_umem_unmap( cq->mcq.mdl, cq->mcq.p_u_arm_sn );
	if (context)
		mlx4_ib_db_unmap_user(to_mucontext(context), &cq->db);

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &cq->buf.mtt);

err_buf:
	if (context)
		ib_umem_release(cq->umem);
	else
		mlx4_buf_free(dev->dev, entries * dev->dev->caps.cqe_size,
			      &cq->buf.buf);

err_db:
	if (!context)
		mlx4_ib_db_free(dev, &cq->db);

err_cq:
	kfree(cq);

	return (ib_cq *)ERR_PTR(err);
}

struct ib_cq *mlx4_ib_create_cq(struct ib_device *ibdev, int entries, int vector,
				struct ib_ucontext *context,
				struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_priv *priv = mlx4_priv(dev->dev);
	struct ib_cq* cq;

	UNUSED_PARAM(vector);

	cq = mlx4_ib_create_cq_internal(ibdev, entries, priv->eq_table.num_eth_eqs + MLX4_EQ_IB_COMP,
		context, udata);

	return cq;
}

int mlx4_cq_group_affinity_to_vector(struct ib_device *ibdev, ib_group_affinity_t *affinity)
{
	int i;
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_priv *priv = mlx4_priv(dev->dev);
	
	for(i = 0; i < priv->eq_table.num_eqs; ++i)
	{
		if((priv->eq_table.eq[i].cpu & affinity->mask) != 0)
		{
			break;
		}
	}

	if(i == priv->eq_table.num_eqs)
	{
		i = priv->eq_table.num_eth_eqs + MLX4_EQ_IB_COMP;
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			( "Requested CQ affinity group=%d mask=%I64x cannot be satisfied. Using IB default group=0 CPU=%d\n",
				affinity->group, affinity->mask, i));	
	}
	
	return i;
}

struct ib_cq *mlx4_ib_create_cq_ex(struct ib_device *ibdev, int entries, ib_group_affinity_t *affinity,
				struct ib_ucontext *context,
				struct ib_udata *udata)
{
	int vector = mlx4_cq_group_affinity_to_vector(ibdev, affinity);
	ib_cq *cq;

	cq = mlx4_ib_create_cq_internal(ibdev, entries, vector, context, udata);

	return cq;
	
}

int mlx4_ib_destroy_cq(struct ib_cq *cq)
{
	struct mlx4_ib_dev *dev = to_mdev(cq->device);
	struct mlx4_ib_cq *mcq = to_mcq(cq);

	mlx4_cq_free(dev->dev, &mcq->mcq);
	mlx4_mtt_cleanup(dev->dev, &mcq->buf.mtt);

	if (cq->p_uctx) {
		ib_umem_unmap( mcq->mcq.mdl, mcq->mcq.p_u_arm_sn );
		mlx4_ib_db_unmap_user(to_mucontext(cq->p_uctx), &mcq->db);
		ib_umem_release(mcq->umem);
	} else {
		mlx4_buf_free(dev->dev, (cq->cqe + 1) * mcq->buf.entry_size,
			      &mcq->buf.buf);
		mlx4_ib_db_free(dev, &mcq->db);
	}

	kfree(mcq);

	return 0;
}

static void dump_cqe(void *cqe)
{
	__be32 *buf = (__be32 *)cqe;

	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV, 
		(KERN_DEBUG "CQE contents %08x %08x %08x %08x %08x %08x %08x %08x\n",
			   be32_to_cpu(buf[0]), be32_to_cpu(buf[1]), be32_to_cpu(buf[2]),
			   be32_to_cpu(buf[3]), be32_to_cpu(buf[4]), be32_to_cpu(buf[5]),
			   be32_to_cpu(buf[6]), be32_to_cpu(buf[7])));
}

static void mlx4_ib_handle_error_cqe(struct mlx4_err_cqe *cqe,
				     ib_wc_t *wc)
{
	if (cqe->syndrome == MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "local QP operation err "
		       "(QPN 0x%06x, WQE index 0x%x, vendor syndrome 0x%02x, "
		       "opcode = 0x%02x)\n",
		       be32_to_cpu(cqe->my_qpn), be16_to_cpu(cqe->wqe_index),
		       cqe->vendor_err_syndrome,
		       cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK));
		dump_cqe(cqe);
	}

	switch (cqe->syndrome) {
	case MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		wc->status = IB_WCS_LOCAL_LEN_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		wc->status = IB_WCS_LOCAL_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_PROT_ERR:
		wc->status = IB_WCS_LOCAL_PROTECTION_ERR;
		break;
	case MLX4_CQE_SYNDROME_WR_FLUSH_ERR:
		wc->status = IB_WCS_WR_FLUSHED_ERR;
		break;
	case MLX4_CQE_SYNDROME_MW_BIND_ERR:
		wc->status = IB_WCS_MEM_WINDOW_BIND_ERR;
		break;
	case MLX4_CQE_SYNDROME_BAD_RESP_ERR:
		wc->status = IB_WCS_BAD_RESP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		wc->status = IB_WCS_LOCAL_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		wc->status = IB_WCS_REM_INVALID_REQ_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		wc->status = IB_WCS_REM_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_OP_ERR:
		wc->status = IB_WCS_REM_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IB_WCS_TIMEOUT_RETRY_ERR;
		break;
	case MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		wc->status = IB_WCS_RNR_RETRY_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		wc->status = IB_WCS_REM_ABORT_ERR;
		break;
	default:
		wc->status = (ib_wc_status_t)IB_WC_GENERAL_ERR;
		break;
	}

	wc->vendor_specific = cqe->vendor_err_syndrome;
}

static uint32_t mlx4_ib_ipoib_csum_ok(__be32 status, __be16 checksum) {
	
	#define CSUM_VALID_NUM 0xffff
	uint32_t res = 0;

	// Verify that IP_OK bit is set and the packet is pure IPv4 packet
	if ((status & cpu_to_be32(MLX4_CQE_IPOIB_STATUS_IPV4		|
							MLX4_CQE_IPOIB_STATUS_IPV4F		|
							MLX4_CQE_IPOIB_STATUS_IPV4OPT	|
							MLX4_CQE_IPOIB_STATUS_IPV6		|
							MLX4_CQE_IPOIB_STATUS_IPOK))	==
				cpu_to_be32(MLX4_CQE_IPOIB_STATUS_IPV4		|
							MLX4_CQE_IPOIB_STATUS_IPOK))
	{
		// IP checksum calculated by MLX4 matched the checksum in the receive packet's 
		res |= MLX4_NdisPacketIpChecksumSucceeded;
		if (checksum == CSUM_VALID_NUM) {
				// TCP or UDP checksum calculated by MLX4 matched the checksum in the receive packet's 
				res |= (MLX4_NdisPacketUdpChecksumSucceeded |
						MLX4_NdisPacketTcpChecksumSucceeded );
		}
	}
	return (( res << 8 ) & IB_RECV_OPT_CSUM_MASK );
}

static int mlx4_ib_poll_one(struct mlx4_ib_cq *cq,
			    struct mlx4_ib_qp **cur_qp,
			    ib_wc_t *wc)
{
	struct mlx4_cqe *cqe;
	struct mlx4_qp *mqp;
	struct mlx4_ib_wq *wq;
	struct mlx4_ib_srq *srq;
	int is_send;
	int is_error;
	u16 wqe_ctr;
#if 0	
	struct mlx4_srq *msrq;
	u32 g_mlpath_rqpn;
	int is_xrc_recv = 0;
#endif	

	cqe = next_cqe_sw(cq);
	if (!cqe)
		return -EAGAIN;

	if (cq->buf.entry_size == 64)
		cqe++;
	
	++cq->mcq.cons_index;

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	is_send  = cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK;
	is_error = (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
		MLX4_CQE_OPCODE_ERROR;

	if ( cq->print_cqe ) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
			( "cq %p, qpn %#x, cqe %p, ci %d, is_send %d, is_error %d\n",
			   cq, be32_to_cpu(cqe->my_qpn), cqe,  cq->mcq.cons_index-1,
			   is_send, is_error));
		dump_cqe(cqe);
	}

#if 0
	// XRC
	if ((be32_to_cpu(cqe->vlan_my_qpn) & (1 << 23)) && !is_send) {
		 /*
		  * We do not have to take the XRC SRQ table lock here,
		  * because CQs will be locked while XRC SRQs are removed
		  * from the table.
		  */
		 msrq = __mlx4_srq_lookup(to_mdev(cq->ibcq.device)->dev,
					 be32_to_cpu(cqe->g_mlpath_rqpn) &
					 0xffffff);
		 if (unlikely(!msrq)) {
			 printk(KERN_WARNING "CQ %06x with entry for unknown "
				"XRC SRQ %06x\n", cq->mcq.cqn,
				be32_to_cpu(cqe->g_mlpath_rqpn) & 0xffffff);
			 return -EINVAL;
		 }
		 is_xrc_recv = 1;
		 srq = to_mibsrq(msrq);
	} else 

#endif
	if (!*cur_qp || (be32_to_cpu(cqe->my_qpn) & 0xffffff) != (u32)(*cur_qp)->mqp.qpn) {
		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
#if 1
		// radix_tree_insert in current implementation seems like
		// can cause radix_tree_lookup to miss an existing QP
		// so we call qp_lookup under the spinlock
		mqp = mlx4_qp_lookup_locked( to_mdev(cq->ibcq.device)->dev, be32_to_cpu(cqe->my_qpn));
#else
		mqp = __mlx4_qp_lookup( to_mdev(cq->ibcq.device)->dev, be32_to_cpu(cqe->my_qpn));
#endif

		if (unlikely(!mqp)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "CQ %06x with entry for unknown QPN %06x\n",
				cq->mcq.cqn, be32_to_cpu(cqe->my_qpn) & 0xffffff));
			return -EINVAL;
		}

		*cur_qp = to_mibqp(mqp);
	}

#if 0
	// XRC
	wc->qp = is_xrc_recv ? NULL: &(*cur_qp)->ibqp;
#endif	

	if (is_send) {
		wq = &(*cur_qp)->sq;
		//TODO: do we need this if
		if (!(*cur_qp)->sq_signal_bits) {	
			wqe_ctr = be16_to_cpu(cqe->wqe_index);
			wq->tail += (u16) (wqe_ctr - (u16) wq->tail);
		}
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
#if 0		
	} else if (is_xrc_recv) {
		// XRC
		wqe_ctr = be16_to_cpu(cqe->wqe_index);
		wc->wr_id = srq->wrid[wqe_ctr];
		mlx4_ib_free_srq_wqe(srq, wqe_ctr);
#endif		
	} else if ((*cur_qp)->ibqp.srq) {
		srq = to_msrq((*cur_qp)->ibqp.srq);
		wqe_ctr = be16_to_cpu(cqe->wqe_index);
		wc->wr_id = srq->wrid[wqe_ctr];
		mlx4_ib_free_srq_wqe(srq, wqe_ctr);
	} else {
		wq	  = &(*cur_qp)->rq;
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	}

	if (is_send) {
		wc->recv.ud.recv_opt = 0;
		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_OPCODE_RDMA_WRITE_IMM:
			wc->recv.ud.recv_opt |= IB_RECV_OPT_IMMEDIATE;
		case MLX4_OPCODE_RDMA_WRITE:
			wc->wc_type    = IB_WC_RDMA_WRITE;
			break;
		case MLX4_OPCODE_SEND_IMM:
			wc->recv.ud.recv_opt |= IB_RECV_OPT_IMMEDIATE;
		case MLX4_OPCODE_SEND:
			wc->wc_type    = IB_WC_SEND;
			break;
		case MLX4_OPCODE_RDMA_READ:
			wc->wc_type    = IB_WC_RDMA_READ;
			wc->length  = be32_to_cpu(cqe->byte_cnt);
			break;
		case MLX4_OPCODE_ATOMIC_CS:
			wc->wc_type    = IB_WC_COMPARE_SWAP;
			wc->length  = 8;
			break;
		case MLX4_OPCODE_ATOMIC_FA:
			wc->wc_type    = IB_WC_FETCH_ADD;
			wc->length  = 8;
			break;
		case MLX4_OPCODE_BIND_MW:
			wc->wc_type    = IB_WC_MW_BIND;
			break;
		case MLX4_OPCODE_LSO:
			wc->wc_type    = IB_WC_LSO;
			break;
		case MLX4_OPCODE_FMR:
			wc->wc_type    = IB_WC_FAST_REG_MR;
			break;
		case MLX4_OPCODE_LOCAL_INVAL:
			wc->wc_type    = IB_WC_LOCAL_INV;
			break;
		default:
			wc->wc_type	  = IB_WC_SEND;
			break;
		}
	} else {
		wc->length = be32_to_cpu(cqe->byte_cnt);

		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_RECV_OPCODE_RDMA_WRITE_IMM:
			wc->wc_type   = IB_WC_RECV_RDMA_WRITE;
			wc->recv.ud.recv_opt = IB_RECV_OPT_IMMEDIATE;
			wc->recv.ud.immediate_data = cqe->immed_rss_invalid;
			break;
#ifdef SUPPORT_SEND_INVAL			
		case MLX4_RECV_OPCODE_SEND_INVAL:
			wc->wc_type	= IB_WC_RECV;
			wc->recv.ud.recv_opt	= IB_RECV_OPT_INVALIDATE;
			wc->ex.invalidate_rkey = be32_to_cpu(cqe->immed_rss_invalid);
			break;
#endif			
		case MLX4_RECV_OPCODE_SEND:
			wc->wc_type   = IB_WC_RECV;
			wc->recv.ud.recv_opt = 0;
			break;
		case MLX4_RECV_OPCODE_SEND_IMM:
			wc->wc_type   = IB_WC_RECV;
			wc->recv.ud.recv_opt = IB_RECV_OPT_IMMEDIATE;
			wc->recv.ud.immediate_data = cqe->immed_rss_invalid;
			break;
		default:
			wc->recv.ud.recv_opt = 0;
			wc->wc_type = IB_WC_RECV;
			break;
		}

		wc->recv.ud.remote_lid	= cqe->rlid;
		wc->recv.ud.remote_sl		= cqe->sl >> 4;
		wc->recv.ud.remote_qp	= cqe->g_mlpath_rqpn & 0xffffff00;
		wc->recv.ud.path_bits		= (u8)(cqe->g_mlpath_rqpn & 0x7f);
		wc->recv.ud.recv_opt		|= cqe->g_mlpath_rqpn & 0x080 ? IB_RECV_OPT_GRH_VALID : 0;
		wc->recv.ud.pkey_index	= (u16)(be32_to_cpu(cqe->immed_rss_invalid)  & 0x7f);
		wc->recv.ud.recv_opt |= mlx4_ib_ipoib_csum_ok(cqe->ipoib_status,cqe->checksum);
	}
	if (!is_send && cqe->rlid == 0){
		MLX4_PRINT(TRACE_LEVEL_INFORMATION,MLX4_DBG_CQ,("found rlid == 0 \n "));
		wc->recv.ud.recv_opt         |= IB_RECV_OPT_FORWARD;
	}

	if (unlikely(is_error))
		mlx4_ib_handle_error_cqe((struct mlx4_err_cqe *) cqe, wc);
	else
		wc->status = IB_WCS_SUCCESS;

	return 0;
}

int mlx4_ib_poll_cq_array(
	IN		struct ib_cq *ibcq, 
	IN		const int num_entries, 
	IN	OUT	ib_wc_t* const wc)
{
	struct mlx4_ib_cq *cq = to_mcq(ibcq);
	struct mlx4_ib_qp *cur_qp = NULL;
	unsigned long flags;
	UNUSED_PARAM(flags);
	int npolled;
	int err = 0;

	spin_lock_irqsave(&cq->lock, &flags);
	
 	for (npolled = 0; npolled < num_entries; npolled++) {
		err = mlx4_ib_poll_one(cq, &cur_qp, (ib_wc_t *) &wc[npolled]);
		if (err)
			break;
		wc[npolled].p_next = (_ib_wc *)cur_qp->ibqp.qp_context;
	}

	// update consumer index
	if (npolled)
		mlx4_cq_set_ci(&cq->mcq);

	spin_unlock_irqrestore(&cq->lock, flags);
	return (err == 0 || err == -EAGAIN)? npolled : err;
}


int mlx4_ib_poll_cq(
	IN		struct ib_cq *ibcq, 
	IN	OUT			ib_wc_t** const				pp_free_wclist,
		OUT			ib_wc_t** const				pp_done_wclist )
{
	struct mlx4_ib_cq *cq = to_mcq(ibcq);
	struct mlx4_ib_qp *cur_qp = NULL;
	unsigned long flags;
	UNUSED_PARAM(flags);
	int err = 0;
	int npolled = 0;
	ib_wc_t		*wc_p, **next_pp;

	spin_lock_irqsave(&cq->lock, &flags);

	// loop through CQ
	next_pp = pp_done_wclist;
	wc_p = *pp_free_wclist;
	while( wc_p ) {
		// poll one CQE
		err = mlx4_ib_poll_one(cq, &cur_qp, wc_p);
		if (err)
			break;

		// prepare for the next loop
		*next_pp = wc_p;
		next_pp = &wc_p->p_next;
		wc_p = wc_p->p_next;
		++npolled;
	}

	// prepare the results
	*pp_free_wclist = wc_p;		/* Set the head of the free list. */
	*next_pp = NULL;						/* Clear the tail of the done list. */

	// update consumer index
	if (npolled)
		mlx4_cq_set_ci(&cq->mcq);

	spin_unlock_irqrestore(&cq->lock, flags);
	return (err == 0 || err == -EAGAIN)? npolled : err;
}

int mlx4_ib_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	if (!mlx4_is_barred(ibcq->device->dma_device))
		mlx4_cq_arm(&to_mcq(ibcq)->mcq,
		    (flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
		    MLX4_CQ_DB_REQ_NOT_SOL : MLX4_CQ_DB_REQ_NOT,
			to_mdev(ibcq->device)->priv_uar.map,
		    MLX4_GET_DOORBELL_LOCK(&to_mdev(ibcq->device)->uar_lock));

	return 0;
}

void __mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq)
{
	u32 prod_index;
	int nfreed = 0;
	struct mlx4_cqe *cqe, *dest, *tcqe, *tdest;
	u8 owner_bit;

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->mcq.cons_index; get_sw_cqe(cq, prod_index); ++prod_index)
		if (prod_index == cq->mcq.cons_index + cq->ibcq.cqe)
			break;

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->mcq.cons_index >= 0) {
		cqe = (mlx4_cqe *)get_cqe(cq, prod_index & cq->ibcq.cqe);
		tcqe = (cq->buf.entry_size == 64) ? (cqe + 1) : cqe;
		if ((be32_to_cpu(tcqe->my_qpn) & 0xffffff) == qpn) {
			if (srq && !(tcqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK))
				mlx4_ib_free_srq_wqe(srq, be16_to_cpu(tcqe->wqe_index));
			++nfreed;
		} else if (nfreed) {
			dest = (mlx4_cqe *)get_cqe(cq, (prod_index + nfreed) & cq->ibcq.cqe);
			tdest = (cq->buf.entry_size == 64) ? (dest + 1) : dest;
			owner_bit = (u8)(tdest->owner_sr_opcode & MLX4_CQE_OWNER_MASK);
			memcpy(dest, cqe, cq->buf.entry_size);
			tdest->owner_sr_opcode = (u8)(owner_bit |
				(tdest->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK));
		}
	}

	if (nfreed) {
		cq->mcq.cons_index += nfreed;
		/*
		 * Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		wmb();
		mlx4_cq_set_ci(&cq->mcq);
	}
}

void mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq)
{
	spin_lock_irq(&cq->lock);
	__mlx4_ib_cq_clean(cq, qpn, srq);
	spin_unlock_irq(&cq->lock);
}
