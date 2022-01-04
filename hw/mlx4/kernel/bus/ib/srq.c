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

#include "mlx4_ib.h"
#include "qp.h"
#include "srq.h"
#include "mx_abi.h"

#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srq.tmh"
#endif


static void *get_wqe(struct mlx4_ib_srq *srq, int n)
{
	int offset = n << srq->msrq.wqe_shift;

	if (srq->buf.is_direct)
		return srq->buf.direct.buf + offset;
	else
		return srq->buf.page_list[offset >> PAGE_SHIFT].buf +
			(offset & (PAGE_SIZE - 1));
}

static void mlx4_ib_srq_event(struct mlx4_srq *srq, enum mlx4_event type)
{
	ib_event_rec_t event;
	struct ib_srq *ibsrq = &to_mibsrq(srq)->ibsrq;

	switch (type) {
	case MLX4_EVENT_TYPE_SRQ_LIMIT:
		event.type = (ib_async_event_t)IB_EVENT_SRQ_LIMIT_REACHED;
		break;
	case MLX4_EVENT_TYPE_SRQ_CATAS_ERROR:
		event.type = (ib_async_event_t)IB_EVENT_SRQ_ERR;
		break;
	default:
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "mlx4_ib: Unexpected event type %d "
		       "on SRQ %06x\n", type, srq->srqn));
		return;
	}

	event.context = ibsrq->srq_context;
	ibsrq->event_handler(&event);
}

struct ib_srq *mlx4_ib_create_srq(struct ib_pd *pd,
				  struct ib_srq_init_attr *init_attr,
				  struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_srq *srq;
	struct mlx4_wqe_srq_next_seg *next;
	ULONG desc_size;
	ULONG buf_size;
	int err;
	int i;
	u32 cqn = 0;
	u16 xrcd = 0;

	if (mlx4_is_barred(pd->device->dma_device)){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: MLX4_FLAG_RESET_DRIVER (mlx4_is_barred) flag is set.\n",
				pd->device->dma_device->pdev->name));

		return (ib_srq *)ERR_PTR(-EFAULT);
	}
	
	/* Sanity check SRQ size before proceeding */
	if ((int)init_attr->attr.max_wr  >= dev->dev->caps.max_srq_wqes ||
	    (int)init_attr->attr.max_sge >  dev->dev->caps.max_srq_sge){

		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: Sanity check SRQ size before proceeding failed.\n",
				pd->device->dma_device->pdev->name));
		
		return (ib_srq *)ERR_PTR(-EINVAL);
	}
	
	srq = (mlx4_ib_srq *)kzalloc(sizeof *srq, GFP_KERNEL);
	if (!srq){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: mem alloc for srq failed.\n",
				pd->device->dma_device->pdev->name));	

		return (ib_srq *)ERR_PTR(-ENOMEM);
	}
	mutex_init(&srq->mutex);
	spin_lock_init(&srq->lock);
	srq->msrq.max    = roundup_pow_of_two(init_attr->attr.max_wr + 1);
	srq->msrq.max_gs = init_attr->attr.max_sge;

	desc_size = max(32UL,
			roundup_pow_of_two(sizeof (struct mlx4_wqe_srq_next_seg) +
					   srq->msrq.max_gs *
					   sizeof (struct mlx4_wqe_data_seg)));
	srq->msrq.wqe_shift = ilog2(desc_size);

	buf_size = srq->msrq.max * desc_size;

	if (pd->p_uctx != NULL) {
		struct ibv_create_srq ucmd;

        NT_ASSERT(udata != NULL);

		if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd)) {
			err = -EFAULT;
			goto err_srq;
		}

        if( NdValidateMemoryMapping( &ucmd.mappings[mlx4_ib_create_srq_buf],
                                     NdModifyAccess, buf_size ) != STATUS_SUCCESS )
        {
			err = -EFAULT;
			goto err_srq;
		}

        if( NdValidateCoallescedMapping( &ucmd.mappings[mlx4_ib_create_srq_db],
                                         NdWriteAccess, sizeof(UINT32) ) != STATUS_SUCCESS )
        {
            err = -EFAULT;
            goto err_srq;
        }

		srq->umem = ib_umem_get(pd->p_uctx, ucmd.mappings[mlx4_ib_create_srq_buf].MapMemory.Address,
					ucmd.mappings[mlx4_ib_create_srq_buf].MapMemory.CbLength, IB_ACCESS_NO_SECURE);
		if (IS_ERR(srq->umem)) {
			err = PTR_ERR(srq->umem);
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: ib_umem_get() for srq->umem failed with error:%d.\n",
					pd->device->dma_device->pdev->name, PTR_ERR(srq->umem)));
			
			goto err_srq;
		}

		err = mlx4_mtt_init(dev->dev, ib_umem_page_count(srq->umem),
				    ilog2(srq->umem->page_size), &srq->mtt);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_mtt_init() failed - mem alloc related.\n", 
					pd->device->dma_device->pdev->name));

			goto err_buf;
		}
		err = mlx4_ib_umem_write_mtt(dev, &srq->mtt, srq->umem);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_ib_umem_write_mtt() failed with error:%d.\n",
					pd->device->dma_device->pdev->name, err));	
		
			goto err_mtt;
		}

		err = mlx4_ib_db_map_user(to_mucontext(pd->p_uctx),
					  ucmd.mappings[mlx4_ib_create_srq_db].MapMemory.Address, &srq->db);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_ib_db_map_user() failed with error:%d.\n", 
					pd->device->dma_device->pdev->name, err));	
			
			goto err_mtt;
		}

	} else {
		err = mlx4_ib_db_alloc(dev, &srq->db, 0);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_ib_db_alloc() failed to alloc mem for srq->db.\n", 
					pd->device->dma_device->pdev->name));
			
			goto err_srq;
		}
		
		*srq->db.db = 0;

		if (mlx4_buf_alloc(dev->dev, buf_size, MAX_DIRECT_ALLOC_SIZE, &srq->buf)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_buf_alloc() failed to alloc mem for srq->buf.\n", 
					pd->device->dma_device->pdev->name));
			
			err = -ENOMEM;
			goto err_db;
		}

		srq->head    = 0;
		srq->tail    = srq->msrq.max - 1;
		srq->wqe_ctr = 0;

		for (i = 0; i < srq->msrq.max; ++i) {
			next = (mlx4_wqe_srq_next_seg *)get_wqe(srq, i);
			next->next_wqe_index =
				cpu_to_be16((i + 1) & (srq->msrq.max - 1));
		}

		err = mlx4_mtt_init(dev->dev, srq->buf.npages, srq->buf.page_shift,
				    &srq->mtt);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_mtt_init() failed - mem alloc related.\n",
					pd->device->dma_device->pdev->name));
			
			goto err_buf;
		}

		err = mlx4_buf_write_mtt(dev->dev, &srq->mtt, &srq->buf);
		if (err){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_ib_umem_write_mtt() failed with error:%d.\n", 
				pd->device->dma_device->pdev->name, err));	
			
			goto err_mtt;
		}

		srq->wrid = (u64*)kmalloc(srq->msrq.max * sizeof (u64), GFP_KERNEL);
		if (!srq->wrid) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mem alloc failed for srq.wrid.\n",
					pd->device->dma_device->pdev->name));
			
			err = -ENOMEM;
			goto err_mtt;
		}

	}
	err = mlx4_srq_alloc(dev->dev, to_mpd(pd)->pdn, cqn, xrcd, &srq->mtt,
			     srq->db.dma.da, &srq->msrq);
	if (err){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: mem alloc failed for srq->msrq.\n",
				pd->device->dma_device->pdev->name));
		
		err = -ENOMEM;
		goto err_wrid;
	}

	srq->msrq.event = mlx4_ib_srq_event;

	if (pd->p_uctx != NULL) {
        struct ibv_create_srq_resp resp;
        RtlZeroMemory(&resp, sizeof(resp));
		if (ib_copy_to_udata(udata, &resp, sizeof(resp))) {
			err = -EFAULT;
			goto err_wrid;
		}
#ifdef XRC_SUPPORT	
	} else {
		srq->ibsrq.xrc_srq_num = srq->msrq.srqn;
#endif	
    }

	init_attr->attr.max_wr = srq->msrq.max - 1;

	return &srq->ibsrq;

err_wrid:
	if (pd->p_uctx)
		mlx4_ib_db_unmap_user(to_mucontext(pd->p_uctx), &srq->db);
	else
		kfree(srq->wrid);

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &srq->mtt);

err_buf:
	if (pd->p_uctx)
		ib_umem_release(srq->umem);
	else
		mlx4_buf_free(dev->dev, buf_size, &srq->buf);

err_db:
	if (!pd->p_uctx)
		mlx4_ib_db_free(dev, &srq->db);

err_srq:
	kfree(srq);

	return (ib_srq *)ERR_PTR(err);
}

int mlx4_ib_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		       enum ib_srq_attr_mask attr_mask, struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibsrq->device);
	struct mlx4_ib_srq *srq = to_msrq(ibsrq);
	int ret;

	UNUSED_PARAM(udata);

	if (mlx4_is_barred(ibsrq->device->dma_device)){			
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: MLX4_FLAG_RESET_DRIVER (mlx4_is_barred) flag is set.\n",
				ibsrq->device->dma_device->pdev->name));

		return -EFAULT;
	}	
	/* We don't support resizing SRQs (yet?) */
	if (attr_mask & XIB_SRQ_MAX_WR){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: SRQ resizing is not supported.\n",
				ibsrq->device->dma_device->pdev->name));
	
	/* We don't support resizing SRQs (yet?) */
	if (attr_mask & XIB_SRQ_MAX_WR)
		return -ENOSYS;
	}

	if (attr_mask & XIB_SRQ_LIMIT) {
		if ((int)attr->srq_limit >= srq->msrq.max){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: attr->srq_limit out of range.\n",
					ibsrq->device->dma_device->pdev->name));

			return -ERANGE;
		}
		
		mutex_lock(&srq->mutex);
		ret = mlx4_srq_arm(dev->dev, &srq->msrq, attr->srq_limit);
		mutex_unlock(&srq->mutex);

		if (ret){
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				( "%s: mlx4_srq_arm() failed with error:%d.\n",
					ibsrq->device->dma_device->pdev->name, ret));

			return ret;
		}
	}

	return 0;
}

int mlx4_ib_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr)
{
	struct mlx4_ib_dev *dev = to_mdev(ibsrq->device);
	struct mlx4_ib_srq *srq = to_msrq(ibsrq);
	int ret;
	int limit_watermark;

	if (mlx4_is_barred(ibsrq->device->dma_device)){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: MLX4_FLAG_RESET_DRIVER (mlx4_is_barred) flag is set.\n",
				ibsrq->device->dma_device->pdev->name));
	
		return -EFAULT;
	}	


	ret = mlx4_srq_query(dev->dev, &srq->msrq, &limit_watermark);
	if (ret){
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			( "%s: mlx4_srq_query() failed with error:%d.\n",
				ibsrq->device->dma_device->pdev->name, ret));
	
		return ret;
	}

	srq_attr->srq_limit = limit_watermark;
	srq_attr->max_wr    = srq->msrq.max - 1;
	srq_attr->max_sge   = srq->msrq.max_gs;

	return 0;
}

int mlx4_ib_destroy_srq(struct ib_srq *srq)
{
	struct mlx4_ib_dev *dev = to_mdev(srq->device);
	struct mlx4_ib_srq *msrq = to_msrq(srq);

	if (!mlx4_is_barred(dev->dev))
		mlx4_srq_invalidate(dev->dev, &msrq->msrq);
	mlx4_srq_remove(dev->dev, &msrq->msrq);

	mlx4_srq_free(dev->dev, &msrq->msrq);
	mlx4_mtt_cleanup(dev->dev, &msrq->mtt);

	if (srq->p_uctx) {
		mlx4_ib_db_unmap_user(to_mucontext(srq->p_uctx), &msrq->db);
		ib_umem_release(msrq->umem);
	} else {
		kfree(msrq->wrid);
		mlx4_buf_free(dev->dev, msrq->msrq.max << msrq->msrq.wqe_shift,
			      &msrq->buf);
		mlx4_ib_db_free(dev, &msrq->db);
	}

	kfree(msrq);

	return 0;
}

void mlx4_ib_free_srq_wqe(struct mlx4_ib_srq *srq, int wqe_index)
{
	struct mlx4_wqe_srq_next_seg *next;

	/* always called with interrupts disabled. */
	spin_lock(&srq->lock);

	next = (mlx4_wqe_srq_next_seg *)get_wqe(srq, srq->tail);
	next->next_wqe_index = cpu_to_be16(wqe_index);
	srq->tail = wqe_index;

	spin_unlock(&srq->lock);
}

int mlx4_ib_post_srq_recv(struct ib_srq *ibsrq, ib_recv_wr_t *wr,
			  ib_recv_wr_t **bad_wr)
{
	struct mlx4_ib_srq *srq = to_msrq(ibsrq);
	struct mlx4_wqe_srq_next_seg *next;
	struct mlx4_wqe_data_seg *scat;
	unsigned long flags;
	UNUSED_PARAM(flags);
	int err = 0;
	int nreq;
	int i;

	if (mlx4_is_barred(ibsrq->device->dma_device))
		return -EFAULT;

	spin_lock_irqsave(&srq->lock, &flags);

	for (nreq = 0; wr; ++nreq, wr = wr->p_next) {
		if (unlikely(wr->num_ds > (u32)srq->msrq.max_gs)) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}

		if (unlikely(srq->head == srq->tail)) {
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		srq->wrid[srq->head] = wr->wr_id;

		next      = (mlx4_wqe_srq_next_seg *)get_wqe(srq, srq->head);
		srq->head = be16_to_cpu(next->next_wqe_index);
		scat      = (struct mlx4_wqe_data_seg *) (next + 1);

		for (i = 0; i < (int)wr->num_ds; ++i) {
			scat[i].byte_count = cpu_to_be32(wr->ds_array[i].length);
			scat[i].lkey       = cpu_to_be32(wr->ds_array[i].lkey);
			scat[i].addr       = cpu_to_be64(wr->ds_array[i].vaddr);
		}

		if (i < srq->msrq.max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = cpu_to_be32(MLX4_INVALID_LKEY);
			scat[i].addr       = 0;
		}
	}

	if (likely(nreq)) {
		srq->wqe_ctr = (u16)(srq->wqe_ctr + nreq);

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*srq->db.db = cpu_to_be32(srq->wqe_ctr);
	}

	spin_unlock_irqrestore(&srq->lock, flags);

	return err;
}
