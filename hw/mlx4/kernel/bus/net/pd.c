/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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
#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "pd.tmh"
#endif

enum {
	MLX4_NUM_RESERVED_UARS = 8
};


#define NOT_MASKED_PD_BITS 17

int mlx4_pd_alloc(struct mlx4_dev *dev, u32 *pdn)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	*pdn = mlx4_bitmap_alloc(&priv->pd_bitmap);
	if (*pdn == -1)
		return -ENOMEM;
	if (mlx4_is_mfunc(dev))
		*pdn |= (dev->caps.function + 1) << NOT_MASKED_PD_BITS;
    return 0;
}

void mlx4_pd_free(struct mlx4_dev *dev, u32 pdn)
{
	mlx4_bitmap_free(&mlx4_priv(dev)->pd_bitmap, pdn);
}

int mlx4_init_pd_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	return mlx4_bitmap_init(&priv->pd_bitmap, dev->caps.num_pds,
        (1 << NOT_MASKED_PD_BITS) - 1, dev->caps.reserved_pds);
}

void mlx4_cleanup_pd_table(struct mlx4_dev *dev)
{
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->pd_bitmap);
}


int mlx4_uar_alloc(struct mlx4_dev *dev, struct mlx4_uar *uar)
{
	int offset;

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	uar->index = mlx4_bitmap_alloc(&mlx4_priv(dev)->uar_table.bitmap);
	if (uar->index == -1)
		return -ENOMEM;

	if (mlx4_is_slave(dev))
		offset = uar->index % ((int) pci_resource_len(dev->pdev, 2) /
				       dev->caps.uar_page_size);
	else
		offset = uar->index;
	uar->pfn = (u32)((pci_resource_start(dev->pdev, 2) >> PAGE_SHIFT) + offset);
	uar->map = NULL;

	return 0;
}

void mlx4_uar_free(struct mlx4_dev *dev, struct mlx4_uar *uar)
{
	mlx4_bitmap_free(&mlx4_priv(dev)->uar_table.bitmap, uar->index);
}


int mlx4_bf_alloc(struct mlx4_dev *dev, struct mlx4_bf *bf)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_uar *uar;
	int err = 0;
	int idx;

	if (!priv->bf_mapping)
		return -ENOMEM;

	mutex_lock(&priv->bf_mutex);
	if (!list_empty(&priv->bf_list))
		uar = list_entry(priv->bf_list.Flink, struct mlx4_uar, bf_list);
	else {
		if (mlx4_bitmap_avail(&priv->uar_table.bitmap) < MLX4_NUM_RESERVED_UARS) {
			err = -ENOMEM;
			goto out;
		}
		uar = (mlx4_uar*)kmalloc(sizeof *uar, GFP_KERNEL);
		if (!uar) {
			err = -ENOMEM;
			goto out;
		}
		err = mlx4_uar_alloc(dev, uar);
		if (err)
			goto free_kmalloc;

		uar->map = (u8*)ioremap(uar->pfn << PAGE_SHIFT, PAGE_SIZE, MmNonCached);
		if (!uar->map) {
			err = -ENOMEM;
			goto free_uar;
		}
		MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV,( 
			"UAR: pa 0x%I64x, size %#x, kva 0x%I64x, index 0x%x, num_uars %#x, uar_area 0x%I64x, uar_size %#x \n", 
				uar->pfn << PAGE_SHIFT,
				PAGE_SIZE,
				(UINT64)(ULONG_PTR)uar->map,
				uar->index,
				dev->caps.num_uars,
				pci_resource_start(dev->pdev, 2) >> PAGE_SHIFT,
				(ULONG)pci_resource_len(dev->pdev, 2) >> 1
				));
		
#ifdef MAP_WC_EVERY_TIME
		uar->bf_map = (u8*)io_mapping_map_wc(priv->bf_mapping, 
			(SIZE_T)(uar->index << PAGE_SHIFT), &uar->bf_map_size);
#else
		uar->bf_map = io_mapping_map_wc(priv->bf_mapping, (SIZE_T)(uar->index << PAGE_SHIFT));
#endif
		if (!uar->bf_map) {
			err = -ENOMEM;
			goto unamp_uar;
		}
		MLX4_PRINT(TRACE_LEVEL_INFORMATION, MLX4_DBG_DRV,( 
			"BF:  pa 0x%I64x, size %#x, kva 0x%I64x, index 0x%x, num_uars %#x, bf_area 0x%I64x, bf_size %#x \n", 
				priv->bf_mapping->base + (SIZE_T)(uar->index << PAGE_SHIFT),
				PAGE_SIZE,
				(UINT64)(ULONG_PTR)uar->bf_map,
				uar->index,
				dev->caps.num_uars,
				priv->bf_mapping->base,
				(ULONG)priv->bf_mapping->size
				));

		uar->free_bf_bmap = 0;
		list_add(&uar->bf_list, &priv->bf_list);
	}

	idx = ffz(uar->free_bf_bmap) - 1;
	uar->free_bf_bmap |= 1 << idx;
	bf->uar = uar;
	bf->offset = 0;
	bf->buf_size = dev->caps.bf_reg_size / 2;
	bf->reg = uar->bf_map + idx * dev->caps.bf_reg_size;
	if (uar->free_bf_bmap == (u32)((1 << dev->caps.bf_regs_per_page) - 1))
		list_del_init(&uar->bf_list);

	goto out;

unamp_uar:
	bf->uar = NULL;
	iounmap(uar->map, PAGE_SIZE);

free_uar:
	mlx4_uar_free(dev, uar);

free_kmalloc:
	kfree(uar);

out:
	mutex_unlock(&priv->bf_mutex);
	return err;
}


void mlx4_bf_free(struct mlx4_dev *dev, struct mlx4_bf *bf)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int idx;

	if (!bf->uar || !bf->uar->bf_map)
		return;

	mutex_lock(&priv->bf_mutex);
	idx = (int)((bf->reg - bf->uar->bf_map) / dev->caps.bf_reg_size);
	bf->uar->free_bf_bmap &= ~(1 << idx);
	if (!bf->uar->free_bf_bmap) {
		if (!list_empty(&bf->uar->bf_list))
			list_del(&bf->uar->bf_list);

#ifdef MAP_WC_EVERY_TIME
		io_mapping_unmap(bf->uar->bf_map, bf->uar->bf_map_size);
#else
		io_mapping_unmap(bf->uar->bf_map);
#endif
		iounmap(bf->uar->map, PAGE_SIZE);
		mlx4_uar_free(dev, bf->uar);
		kfree(bf->uar);
	} else if (list_empty(&bf->uar->bf_list))
		list_add(&bf->uar->bf_list, &priv->bf_list);

	mutex_unlock(&priv->bf_mutex);
}

int mlx4_init_uar_table(struct mlx4_dev *dev)
{
	if (dev->caps.num_uars <= 128) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_PD,( "%s: Only %d UAR pages (need more than 128)\n", dev->pdev->name,
			dev->caps.num_uars)); 
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_PD,( "%s: Increase firmware log2_uar_bar_megabytes?\n", dev->pdev->name));
		return -ENODEV;
	}

	return mlx4_bitmap_init_no_mask(&mlx4_priv(dev)->uar_table.bitmap,
		dev->caps.num_uars,
		dev->caps.reserved_uars, 0);
}

void mlx4_cleanup_uar_table(struct mlx4_dev *dev)
{
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->uar_table.bitmap);
}
