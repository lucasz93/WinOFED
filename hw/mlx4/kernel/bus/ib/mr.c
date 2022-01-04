/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include "mlx4_ib.h"
#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mr.tmh"
#endif


static u32 convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ? MLX4_PERM_ATOMIC	   : 0) |
		   (acc & IB_ACCESS_REMOTE_WRITE  ? MLX4_PERM_REMOTE_WRITE : 0) |
		   (acc & IB_ACCESS_REMOTE_READ   ? MLX4_PERM_REMOTE_READ  : 0) |
		   (acc & IB_ACCESS_LOCAL_WRITE   ? MLX4_PERM_LOCAL_WRITE  : 0) |
		   MLX4_PERM_LOCAL_READ;
}

struct ib_mr *mlx4_ib_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct mlx4_ib_mr *mr;
	int err;

	if (mlx4_is_barred(pd->device->dma_device))
		return (ib_mr *)ERR_PTR(-EFAULT);

	mr = (mlx4_ib_mr*)kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return (ib_mr *)ERR_PTR(-ENOMEM);

	err = mlx4_mr_alloc(to_mdev(pd->device)->dev, to_mpd(pd)->pdn, 0,
				~0ull, convert_access(acc), 0, 0, &mr->mmr);
	if (err)
		goto err_free;

	err = mlx4_mr_enable(to_mdev(pd->device)->dev, &mr->mmr);
	if (err)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;
	mr->umem = NULL;

	return &mr->ibmr;

err_mr:
	mlx4_mr_free(to_mdev(pd->device)->dev, &mr->mmr);

err_free:
	kfree(mr);

	return (ib_mr *)ERR_PTR(err);
}

int mlx4_ib_umem_write_mtt(struct mlx4_ib_dev *dev, struct mlx4_mtt *mtt,
			   struct ib_umem *p_ib_umem)
{
	u64 *pages;
	iobuf_iter_t iobuf_iter;
	u32 i, n; 
	int err;

	pages = (u64 *) __get_free_page(GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	i = n = err = 0;

	iobuf_iter_init( &p_ib_umem->iobuf, &iobuf_iter );
	for (;;) {
		// get up to  max_buf_list_size page physical addresses
		i = iobuf_get_tpt_seg( &p_ib_umem->iobuf, &iobuf_iter, 
			PAGE_SIZE / sizeof (u64), pages );
		if (!i)
			break;

		// TODO: convert physical adresses to dma one's

		// write 'i' dma addresses
		err = mlx4_write_mtt(dev->dev, mtt, n, i, pages);
		if (err)
			goto out;
		n += i;
		if (n >= p_ib_umem->iobuf.nr_pages)
			break;
	}

	CL_ASSERT(n == p_ib_umem->iobuf.nr_pages);

out:
	free_page(pages);
	return err;
}

struct ib_mr *mlx4_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_mr *mr;
	int shift;
	int err;
	int n;

	if (mlx4_is_barred(pd->device->dma_device))
		return (ib_mr *)ERR_PTR(-EFAULT);
	
	mr = (mlx4_ib_mr*)kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return (ib_mr *)ERR_PTR(-ENOMEM);

	mr->umem = ib_umem_get(pd->p_uctx, start, (size_t)length, (ib_access_flags)access_flags);
	if (IS_ERR(mr->umem)) {
		// there can be also second reason of failue - insufficient memory,
		// but we can't get awared of that without changing ib_umem_get prototype
		err = -EACCES;
		goto err_free;
	}

	n = ib_umem_page_count(mr->umem);
	shift = ilog2(mr->umem->page_size);

	err = mlx4_mr_alloc(dev->dev, to_mpd(pd)->pdn, virt_addr, length,
				convert_access(access_flags), n, shift, &mr->mmr);
	if (err)
		goto err_umem;

	err = mlx4_ib_umem_write_mtt(dev, &mr->mmr.mtt, mr->umem);
	if (err)
		goto err_mr;

	err = mlx4_mr_enable(dev->dev, &mr->mmr);
	if (err)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;

	return &mr->ibmr;

err_mr:
	mlx4_mr_free(to_mdev(pd->device)->dev, &mr->mmr);

err_umem:
	ib_umem_release(mr->umem);

err_free:
	kfree(mr);

	return (ib_mr *)ERR_PTR(err);
}

int mlx4_ib_krnl_write_mtt(struct mlx4_ib_dev *dev, struct mlx4_mtt *mtt,
			   PMDL p_mdl, u64 length, u32 page_size)
{
	u64 *pages, *p_pa;
	u32 i, j; 
	int err;
	ULONG rdc = (ULONG)length;
	ULONG n_pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(p_mdl), rdc);

	UNUSED_PARAM(page_size);

	pages = (u64 *) kmalloc(n_pages * sizeof(u64), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;
	p_pa = pages;

	for ( j = 0; p_mdl != NULL && rdc; p_mdl = p_mdl->Next )
	{
		PPFN_NUMBER	p_pfn = MmGetMdlPfnArray(p_mdl);
		ULONG size = min(rdc, MmGetMdlByteCount(p_mdl));
		ULONG n_mdl_pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(p_mdl), size);

		rdc -= size;
		
		for (i = 0; i < n_mdl_pages; ++i, ++p_pfn)
		{
			// if not the first MDL *starts* in the middle of the page, its first PFN should be skipped
			if ( i == 0 && j > 0 && MmGetMdlByteOffset(p_mdl) )
					continue;

			p_pa[j++] = (ULONGLONG)*p_pfn << PAGE_SHIFT;;
			// TODO: convert physical adresses to dma one's
		}
	}

	CL_ASSERT( j == n_pages );

	// write 'i' dma addresses
	err = mlx4_write_mtt(dev->dev, mtt, 0, j, pages);
	kfree(pages);
	return err;
}

struct ib_mr *mlx4_ib_reg_krnl_mr(struct ib_pd *pd, PMDL p_mdl, u64 length,
				  int access_flags)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_mr *mr;
	int shift;
	int err;
	int n;

	if (mlx4_is_barred(pd->device->dma_device))
		return (ib_mr *)ERR_PTR(-EFAULT);

	if ( !length )
		return (ib_mr *)ERR_PTR(-EINVAL);
		
	mr = (mlx4_ib_mr*)kzalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return (ib_mr *)ERR_PTR(-ENOMEM);

	shift = PAGE_SHIFT;
	n = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(p_mdl),length);

	err = mlx4_mr_alloc(dev->dev, to_mpd(pd)->pdn, 
		(u64)(ULONG_PTR)MmGetMdlVirtualAddress(p_mdl), length,
		convert_access(access_flags), n, shift, &mr->mmr);
	if (err)
		goto err_alloc;

	err = mlx4_ib_krnl_write_mtt(dev, &mr->mmr.mtt, p_mdl, length, PAGE_SIZE);
	if (err)
		goto err_write_mtt;

	err = mlx4_mr_enable(dev->dev, &mr->mmr);
	if (err)
		goto err_mr_enable;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;

	return &mr->ibmr;

err_mr_enable:
	mlx4_mr_free(to_mdev(pd->device)->dev, &mr->mmr);

err_write_mtt:
err_alloc:
	kfree(mr);

	return (ib_mr *)ERR_PTR(err);
}

//extern int g_print_mpt;

int mlx4_ib_dereg_mr(struct ib_mr *ibmr)
{
	struct mlx4_ib_mr *mr = to_mmr(ibmr);

	mlx4_mr_free(to_mdev(ibmr->device)->dev, &mr->mmr);
	if (mr->umem)
		ib_umem_release(mr->umem);
	kfree(mr);
//	g_print_mpt = 0;

	return 0;
}


struct ib_mr *mlx4_ib_alloc_fast_reg_mr(struct ib_pd *pd,
					int max_page_list_len)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_mr *mr;
	int err;

//	g_print_mpt = 1;

	mr = (mlx4_ib_mr*)kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return (ib_mr *)ERR_PTR(-ENOMEM);

	err = mlx4_mr_alloc(dev->dev, to_mpd(pd)->pdn, 0, 0, 0,
				max_page_list_len, 0, &mr->mmr);
	if (err)
		goto err_free;

	err = mlx4_mr_enable(dev->dev, &mr->mmr);
	if (err)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;
	mr->umem = NULL;

	return &mr->ibmr;

err_mr:
	mlx4_mr_free(dev->dev, &mr->mmr);

err_free:
	kfree(mr);
	return (ib_mr *)ERR_PTR(err);
}

ib_fast_reg_page_list_t *mlx4_ib_alloc_fast_reg_page_list(struct ib_device *ibdev,
								   int page_list_len)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_fast_reg_page_list *mfrpl;
	int size = page_list_len * sizeof (u64);

	if (size > PAGE_SIZE)
		return (ib_fast_reg_page_list_t *)ERR_PTR(-EINVAL);

	mfrpl = (mlx4_ib_fast_reg_page_list *)kmalloc(sizeof *mfrpl, GFP_KERNEL);
	if (!mfrpl)
		return (ib_fast_reg_page_list_t *)ERR_PTR(-ENOMEM);

	mfrpl->ibfrpl.page_list = (uint64_t*)kmalloc(size, GFP_KERNEL);
	if (!mfrpl->ibfrpl.page_list)
		goto err_free;

	mfrpl->mapped_page_list = (__be64*)dma_alloc_coherent(&dev->dev->pdev->dev,
							 size, &mfrpl->map,
							 GFP_KERNEL);
	if (!mfrpl->ibfrpl.page_list)
		goto err_free;

	WARN_ON(mfrpl->map.da & 0x3f);

	return &mfrpl->ibfrpl;

err_free:
	kfree(mfrpl->ibfrpl.page_list);
	kfree(mfrpl);
	return (ib_fast_reg_page_list_t *)ERR_PTR(-ENOMEM);
}

void mlx4_ib_free_fast_reg_page_list(ib_fast_reg_page_list_t *page_list)
{
	struct mlx4_ib_dev *dev = to_mdev((ib_device *)page_list->device);
	struct mlx4_ib_fast_reg_page_list *mfrpl = to_mfrpl(page_list);
	int size = page_list->max_page_list_len * sizeof (u64);

	dma_free_coherent(&dev->dev->pdev->dev, size, mfrpl->mapped_page_list,
			  mfrpl->map);
	kfree(mfrpl->ibfrpl.page_list);
	kfree(mfrpl);
}

struct ib_fmr *mlx4_ib_fmr_alloc(struct ib_pd *pd, int acc,
				 struct ib_fmr_attr *fmr_attr)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_fmr *fmr;
	int err = -ENOMEM;

	if (mlx4_is_barred(pd->device->dma_device))
		return (ib_fmr*)ERR_PTR(-EFAULT);

	fmr = (mlx4_ib_fmr *)kmalloc(sizeof *fmr, GFP_KERNEL);
	if (!fmr)
		return (ib_fmr*)ERR_PTR(-ENOMEM);

	err = mlx4_fmr_alloc(dev->dev, to_mpd(pd)->pdn, convert_access(acc),
				 fmr_attr->max_pages, fmr_attr->max_maps,
				 fmr_attr->page_shift, &fmr->mfmr);
	if (err)
		goto err_free;

	err = mlx4_fmr_enable(to_mdev(pd->device)->dev, &fmr->mfmr);
	if (err)
		goto err_mr;

	fmr->ibfmr.rkey = fmr->ibfmr.lkey = fmr->mfmr.mr.key;

	return &fmr->ibfmr;

err_mr:
	mlx4_mr_free(to_mdev(pd->device)->dev, &fmr->mfmr.mr);

err_free:
	kfree(fmr);

	return (ib_fmr*)ERR_PTR(err);
}

int mlx4_ib_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
			  int npages, u64 iova)
{
	struct mlx4_ib_fmr *ifmr = to_mfmr(ibfmr);
	struct mlx4_ib_dev *dev = to_mdev(ifmr->ibfmr.device);

	if (mlx4_is_barred(ifmr->ibfmr.device->dma_device))
		return -EFAULT;

	return mlx4_map_phys_fmr(dev->dev, &ifmr->mfmr, page_list, npages, iova,
				 &ifmr->ibfmr.lkey, &ifmr->ibfmr.rkey);
}

int mlx4_ib_unmap_fmr(struct list_head *fmr_list)
{
	struct ib_fmr *ibfmr;
	int err = 0;
	struct mlx4_dev *mdev = NULL;

	list_for_each_entry(ibfmr, fmr_list, list, struct ib_fmr) {
		if (mdev && to_mdev(ibfmr->device)->dev != mdev)
			return -EINVAL;
		mdev = to_mdev(ibfmr->device)->dev;
	}

	if (!mdev)
		return 0;

	list_for_each_entry(ibfmr, fmr_list, list, struct ib_fmr) {
		struct mlx4_ib_fmr *ifmr = to_mfmr(ibfmr);

		mlx4_fmr_unmap(mdev, &ifmr->mfmr, &ifmr->ibfmr.lkey, &ifmr->ibfmr.rkey);
	}

	/*
	 * Make sure all MPT status updates are visible before issuing
	 * SYNC_TPT firmware command.
	 */
	wmb();

	if (!mlx4_is_barred(mdev))
		err = mlx4_SYNC_TPT(mdev);
	if (err)
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "mlx4_ib: SYNC_TPT error %d when "
			   "unmapping FMRs\n", err));

	return 0;
}

int mlx4_ib_fmr_dealloc(struct ib_fmr *ibfmr)
{
	struct mlx4_ib_fmr *ifmr = to_mfmr(ibfmr);
	struct mlx4_ib_dev *dev = to_mdev(ibfmr->device);
	int err;

	err = mlx4_fmr_free(dev->dev, &ifmr->mfmr);

	if (!err)
		kfree(ifmr);

	return err;
}
