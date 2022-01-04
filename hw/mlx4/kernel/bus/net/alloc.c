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
#include <mlx4_debug.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "alloc.tmh"
#endif


u32 mlx4_bitmap_alloc(struct mlx4_bitmap *bitmap)
{
	u32 obj;

	spin_lock(&bitmap->lock);

	obj = find_next_zero_bit(bitmap->table,
				 bitmap->effective_max,
				 bitmap->last);
	if (obj >= bitmap->effective_max) {
		bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
		obj = find_first_zero_bit(bitmap->table, bitmap->effective_max);
	}

	if (obj < bitmap->effective_max) {
		set_bit(obj, bitmap->table);
		bitmap->last = (obj + 1);
		if (bitmap->last == bitmap->effective_max)
			bitmap->last = 0;
		obj |= bitmap->top;
	} else
		obj = (u32)-1;

	if (obj != (u32)-1)
		--bitmap->avail;

	spin_unlock(&bitmap->lock);

	return obj;
}

void mlx4_bitmap_free(struct mlx4_bitmap *bitmap, u32 obj)
{
	obj &= bitmap->max - 1;

	spin_lock(&bitmap->lock);
	clear_bit(obj, bitmap->table);
	bitmap->last = min(bitmap->last, obj);
	bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
	++bitmap->avail;
	spin_unlock(&bitmap->lock);
}

static unsigned long find_next_zero_string_aligned(unsigned long *bitmap,
						   u32 start, u32 nbits,
						   int len, int align)
{
	unsigned long end, i;

again:
	start = ALIGN(start, align);
	while ((start < nbits) && test_bit(start, bitmap))
		start += align;
	if (start >= nbits)
		return ULONG_MAX;

	end = start+len;
	if (end > nbits)
		return ULONG_MAX;
	for (i = start+1; i < end; i++) {
		if (test_bit(i, bitmap)) {
			start = i+1;
			goto again;
		}
	}
	return start;
}

u32 mlx4_bitmap_alloc_range(struct mlx4_bitmap *bitmap, int cnt, int align)
{
	u32 obj;
	int i;

	if (likely(cnt == 1 && align == 1))
		return mlx4_bitmap_alloc(bitmap);

	spin_lock(&bitmap->lock);

	obj = find_next_zero_string_aligned(bitmap->table, bitmap->last,
					    bitmap->effective_max, cnt, align);
	if (obj >= bitmap->effective_max) {
//		bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
		obj = find_next_zero_string_aligned(bitmap->table, 0,
						    bitmap->effective_max,
						    cnt, align);
	}

	if (obj < bitmap->effective_max) {
		for (i = 0; i < cnt; i++)
			set_bit(obj+i, bitmap->table);
		if (obj == bitmap->last) {
			bitmap->last = (obj + cnt);
			if (bitmap->last >= bitmap->effective_max)
				bitmap->last = 0;
		}
		obj |= bitmap->top;
	} else
		obj = ULONG_MAX;

		if (obj != ULONG_MAX)
			bitmap->avail -= cnt;
	
	spin_unlock(&bitmap->lock);


	return obj;
}

u32 mlx4_bitmap_avail(struct mlx4_bitmap *bitmap)
{
	return bitmap->avail;
}

void mlx4_bitmap_free_range(struct mlx4_bitmap *bitmap, u32 obj, int cnt)
{
	int	i;

	obj &= bitmap->max - 1;

	spin_lock(&bitmap->lock);
	for (i = 0; i < cnt; i++) {
		ASSERT(test_bit(obj+i , bitmap->table));
		clear_bit(obj+i, bitmap->table);
	}
    
	bitmap->last = min(bitmap->last, obj);
	//bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
	bitmap->avail += cnt;
	spin_unlock(&bitmap->lock);
}
int mlx4_bitmap_init_with_effective_max(struct mlx4_bitmap *bitmap,
					u32 num, u32 mask, u32 reserved,
					u32 effective_max)
{
	int i;

	/* num must be a power of 2 */
	if (num != roundup_pow_of_two(num))
		return -EINVAL;

	bitmap->last = 0;
	bitmap->top  = 0;
	bitmap->max  = num;
	bitmap->mask = mask;
	bitmap->effective_max = effective_max;
	spin_lock_init(&bitmap->lock);
	bitmap->avail = effective_max - reserved;
	bitmap->table = (unsigned long *)kzalloc(BITS_TO_LONGS(num) * sizeof (long), GFP_KERNEL);
	if (!bitmap->table)
		return -ENOMEM;

	for (i = 0; i < (int)reserved; ++i)
		set_bit(i, bitmap->table);

	return 0;
}

int mlx4_bitmap_init(struct mlx4_bitmap *bitmap,
		     u32 num, u32 mask, u32 reserved)
{
	return mlx4_bitmap_init_with_effective_max(bitmap, num, mask,
						   reserved, num);
}

/* Like bitmap_init, but doesn't require 'num' to be a power of 2 or
 * a non-trivial mask */
int mlx4_bitmap_init_no_mask(struct mlx4_bitmap *bitmap, u32 num,
		     u32 reserved_bot, u32 reserved_top)
{
	u32 num_rounded = roundup_pow_of_two(num);
	return mlx4_bitmap_init_with_effective_max(bitmap, num_rounded, num_rounded - 1,
				reserved_bot, num - reserved_top);
}

void mlx4_bitmap_cleanup(struct mlx4_bitmap *bitmap)
{
	kfree(bitmap->table);
}

/*
 * Handling for queue buffers -- we allocate a bunch of memory and
 * register it in a memory region at HCA virtual address 0.  If the
 * requested size is > max_direct, we split the allocation into
 * multiple pages, so we don't require too much contiguous memory.
 */

int mlx4_buf_alloc(struct mlx4_dev *dev, int size, int max_direct,
		   struct mlx4_buf *buf)
{
	dma_addr_t t;

	if (size <= max_direct) {
		buf->is_direct    = 1;
		buf->nbufs        = 1;
		buf->npages       = 1;
		// TODO:  we don't use pages less then PAGE_SIZE
		size = max(size, PAGE_SIZE);
		buf->page_shift   = get_order(size) + PAGE_SHIFT;
		buf->direct.buf = (u8*)dma_alloc_coherent(&dev->pdev->dev,
			size, &t, GFP_KERNEL);
		if (!buf->direct.buf)
			goto indirect;

		buf->direct.map = t;

		while (t.da & ((1 << buf->page_shift) - 1)) {
			--buf->page_shift;
			buf->npages *= 2;
		}
		MLX4_PRINT( TRACE_LEVEL_INFORMATION, MLX4_DBG_CQ,
			("%s: size %#x, nbufs %d, pages %d, page_shift %d, kva %p, da %I64x, buf_size %I64x\n",
			dev->pdev->name, size, buf->nbufs, buf->npages, buf->page_shift, 
			buf->direct.buf, t.da, t.sz ));
		memset(buf->direct.buf, 0, size);        
		return 0;
	}

indirect:
	{
		int i;

		buf->is_direct   = 0;
		buf->nbufs       = (size + PAGE_SIZE - 1) / PAGE_SIZE;
		buf->npages      = buf->nbufs;
		buf->page_shift  = PAGE_SHIFT;
		buf->page_list = (mlx4_buf_list *)kzalloc(buf->nbufs * sizeof *buf->page_list,
					   GFP_KERNEL);
		if (!buf->page_list)
			goto err_kzalloc;

		for (i = 0; i < buf->nbufs; ++i) {
			buf->page_list[i].buf =
				(u8*)dma_alloc_coherent(&dev->pdev->dev, PAGE_SIZE,
						   &t, GFP_KERNEL);
			if (!buf->page_list[i].buf)
				goto err_free;

			buf->page_list[i].map = t;

			memset(buf->page_list[i].buf, 0, PAGE_SIZE);
		}
	}
	return 0;

err_free:
	mlx4_buf_free(dev, size, buf);
err_kzalloc:

    WriteEventLogEntryData( dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_BUF_ALLOC_FAILED, 0, 0, 2,
        L"%S", dev->pdev->name, L"%d", size );


    
	return -ENOMEM;
}

void mlx4_buf_free(struct mlx4_dev *dev, int size, struct mlx4_buf *buf)
{
	int i;

	// TODO:  we don't use pages less then PAGE_SIZE
	size = max(size, PAGE_SIZE);

	if (buf->is_direct)
		dma_free_coherent(&dev->pdev->dev, size, buf->direct.buf,
				  buf->direct.map);
	else {
		for (i = 0; i < buf->nbufs; ++i)
			if (buf->page_list[i].buf)
				dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
					  buf->page_list[i].buf,
					  buf->page_list[i].map);
		kfree(buf->page_list);
	}
}

static struct mlx4_db_pgdir *mlx4_alloc_db_pgdir(struct mlx4_dev *dev)
{
	struct mlx4_db_pgdir *pgdir;

	pgdir = (mlx4_db_pgdir *)kzalloc(sizeof *pgdir, GFP_KERNEL);
	if (!pgdir)
		return NULL;

	bitmap_fill(pgdir->order1, MLX4_DB_PER_PAGE / 2);
	pgdir->bits[0] = pgdir->order0;
	pgdir->bits[1] = pgdir->order1;

	
	pgdir->db_page = (__be32 *)dma_alloc_coherent(&dev->pdev->dev, PAGE_SIZE,
					    &pgdir->db_dma, GFP_KERNEL);
	if (!pgdir->db_page) {
		kfree(pgdir);
		return NULL;
	}

	return pgdir;
}

static int mlx4_alloc_db_from_pgdir(struct mlx4_db_pgdir *pgdir,
				    struct mlx4_db *db, int order)
{
	int o;
	int i;

	for (o = order; o <= 1; ++o) {
		i = find_first_bit(pgdir->bits[o], MLX4_DB_PER_PAGE >> o);
		if (i < MLX4_DB_PER_PAGE >> o)
			goto found;
	}

	return -ENOMEM;

found:
	clear_bit(i, pgdir->bits[o]);

	i <<= o;

	if (o > order)
		set_bit(i ^ 1, pgdir->bits[order]);

	db->pgdir = pgdir;
	db->index   = i;
	db->db      = pgdir->db_page + db->index;
	db->dma.da     = pgdir->db_dma.da  + db->index * 4;
	db->dma.va = (VOID *)(UINT_PTR)-1;
	db->dma.sz = ULONG_MAX;
	db->order   = order;

	return 0;
}

int mlx4_db_alloc(struct mlx4_dev *dev, 
				struct mlx4_db *db, int order)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_db_pgdir *pgdir;
	int ret = 0;
	int ret1 = 0;

	mutex_lock(&priv->pgdir_mutex);

	list_for_each_entry(pgdir, &priv->pgdir_list, list, struct mlx4_db_pgdir)
		if (!mlx4_alloc_db_from_pgdir(pgdir, db, order))
			goto out;

	pgdir = mlx4_alloc_db_pgdir(dev);
	if (!pgdir) {
		ret = -ENOMEM;
		goto out;
	}

	list_add(&pgdir->list, &priv->pgdir_list);

	/* This should never fail -- we just allocated an empty page: */
	ret1 = mlx4_alloc_db_from_pgdir(pgdir, db, order);
	ASSERT(ret1 == 0);

out:
	mutex_unlock(&priv->pgdir_mutex);

	return ret;
}

void mlx4_db_free(struct mlx4_dev *dev, struct mlx4_db *db)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int o;
	int i;

	mutex_lock(&priv->pgdir_mutex);

	o = db->order;
	i = db->index;

	if (db->order == 0 && test_bit(i ^ 1, db->pgdir->order0)) {
		clear_bit(i ^ 1, db->pgdir->order0);
		++o;
	}

	i >>= o;
	set_bit(i, db->pgdir->bits[o]);

	if (bitmap_full(db->pgdir->order1, MLX4_DB_PER_PAGE / 2)) {

		dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
				  db->pgdir->db_page, db->pgdir->db_dma);
		list_del(&db->pgdir->list);
		kfree(db->pgdir);
	}

	mutex_unlock(&priv->pgdir_mutex);
}

int mlx4_alloc_hwq_res(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
		       int size, int max_direct)
{
	int err;

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	err = mlx4_db_alloc(dev, &wqres->db, 1);
	if (err)
		return err;
	*wqres->db.db = 0;

	if (mlx4_buf_alloc(dev, size, max_direct, &wqres->buf)) {
		err = -ENOMEM;
		goto err_db;
	}
	if(wqres->buf.is_direct == 0) {
		err = -ENOMEM;
		WriteEventLogEntryData( dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_BUF_ALLOC_FRAGMENTED, 0, 0, 2,
			L"%S", dev->pdev->name, L"%d", size );
		
		goto err_buf;
	}

	err = mlx4_mtt_init(dev, wqres->buf.npages, wqres->buf.page_shift,
			    &wqres->mtt);
	if (err)
		goto err_buf;
	err = mlx4_buf_write_mtt(dev, &wqres->mtt, &wqres->buf);
	if (err)
		goto err_mtt;

	return 0;

err_mtt:
	mlx4_mtt_cleanup(dev, &wqres->mtt);
err_buf:
	mlx4_buf_free(dev, size, &wqres->buf);
err_db:
	mlx4_db_free(dev, &wqres->db);
	return err;
}

void mlx4_free_hwq_res(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
		      int size)
{
	mlx4_mtt_cleanup(dev, &wqres->mtt);
	mlx4_buf_free(dev, size, &wqres->buf);
	mlx4_db_free(dev, &wqres->db);
}

