/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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

#include "mthca_dev.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mthca_mr.tmh"
#endif
#include "mthca_cmd.h"
#include "mthca_memfree.h"

static int mthca_buddy_init(struct mthca_buddy *buddy, int max_order);
static void mthca_buddy_cleanup(struct mthca_buddy *buddy);

struct mthca_mtt {
	struct mthca_buddy *buddy;
	int                 order;
	u32                 first_seg;
};

/*
 * Must be packed because mtt_seg is 64 bits but only aligned to 32 bits.
 */
#pragma pack(push,1)
struct mthca_mpt_entry {
	__be32 flags;
	__be32 page_size;
	__be32 key;
	__be32 pd;
	__be64 start;
	__be64 length;
	__be32 lkey;
	__be32 window_count;
	__be32 window_count_limit;
	__be64 mtt_seg;
	__be32 mtt_sz;		/* Arbel only */
	u32    reserved[2];
} ;
#pragma pack(pop)

#define MTHCA_MPT_FLAG_SW_OWNS       (0xfUL << 28)
#define MTHCA_MPT_FLAG_MIO           (1 << 17)
#define MTHCA_MPT_FLAG_BIND_ENABLE   (1 << 15)
#define MTHCA_MPT_FLAG_PHYSICAL      (1 <<  9)
#define MTHCA_MPT_FLAG_REGION        (1 <<  8)

#define MTHCA_MTT_FLAG_PRESENT       1

#define MTHCA_MPT_STATUS_SW 0xF0
#define MTHCA_MPT_STATUS_HW 0x00

#define SINAI_FMR_KEY_INC 0x1000000

static void dump_mtt(u32 print_lvl, __be64 *mtt_entry ,int list_len)
{
	int i;
	UNUSED_PARAM_WOWPP(mtt_entry);		// for release version
	UNUSED_PARAM_WOWPP(print_lvl);
	HCA_PRINT(print_lvl ,HCA_DBG_MEMORY ,("Dumping MTT entry len %d :\n",list_len));
	for (i = 0; i < list_len && i < MTHCA_MAILBOX_SIZE / 8 - 2; i=i+4) {
		HCA_PRINT(print_lvl ,HCA_DBG_MEMORY ,("[%02x]  %016I64x %016I64x %016I64x %016I64x\n",i,
			cl_ntoh64(mtt_entry[i]),
			cl_ntoh64(mtt_entry[i+1]),
			cl_ntoh64(mtt_entry[i+2]),
			cl_ntoh64(mtt_entry[i+3])));
	}
}


static void dump_mpt(u32 print_lvl, struct mthca_mpt_entry *mpt_entry )
{
	int i;
	UNUSED_PARAM_WOWPP(mpt_entry);		// for release version
	UNUSED_PARAM_WOWPP(print_lvl);
	HCA_PRINT(print_lvl ,HCA_DBG_MEMORY ,("Dumping MPT entry %08x :\n", mpt_entry->key));
	for (i = 0; i < sizeof (struct mthca_mpt_entry) / 4; i=i+4) {
	HCA_PRINT(print_lvl ,HCA_DBG_MEMORY ,("[%02x]  %08x %08x %08x %08x \n",i,
			cl_ntoh32(((__be32 *) mpt_entry)[i]),
			cl_ntoh32(((__be32 *) mpt_entry)[i+1]),
			cl_ntoh32(((__be32 *) mpt_entry)[i+2]),
			cl_ntoh32(((__be32 *) mpt_entry)[i+3])));
	}
}








/*
 * Buddy allocator for MTT segments (currently not very efficient
 * since it doesn't keep a free list and just searches linearly
 * through the bitmaps)
 */

static u32 mthca_buddy_alloc(struct mthca_buddy *buddy, int order)
{
	int o;
	u32 m;
	u32 seg;
	SPIN_LOCK_PREP(lh);

	spin_lock(&buddy->lock, &lh);

	for (o = order; o <= buddy->max_order; ++o) {
		m = 1 << (buddy->max_order - o);
		seg = find_first_bit(buddy->bits[o], m);
		if (seg < m)
			goto found;
	}

	spin_unlock(&lh);
	return (u32)-1;

 found:
	clear_bit(seg, (long*)buddy->bits[o]);

	while (o > order) {
		--o;
		seg <<= 1;
		set_bit(seg ^ 1, (long*)buddy->bits[o]);
	}

	spin_unlock(&lh);

	seg <<= order;

	return seg;
}

static void mthca_buddy_free(struct mthca_buddy *buddy, u32 seg, int order)
{
	SPIN_LOCK_PREP(lh);

	seg >>= order;

	spin_lock(&buddy->lock, &lh);

	while (test_bit(seg ^ 1, buddy->bits[order])) {
		clear_bit(seg ^ 1, (long*)buddy->bits[order]);
		seg >>= 1;
		++order;
	}

	set_bit(seg, (long*)buddy->bits[order]);

	spin_unlock(&lh);
}

static int mthca_buddy_init(struct mthca_buddy *buddy, int max_order)
{
	int i, s;

	buddy->max_order = max_order;
	spin_lock_init(&buddy->lock);

	buddy->bits = kmalloc((buddy->max_order + 1) * sizeof (long *),
			      GFP_KERNEL);
	if (!buddy->bits)
		goto err_out;

	RtlZeroMemory(buddy->bits, (buddy->max_order + 1) * sizeof (long *));

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->bits[i] = kmalloc(s * sizeof (long), GFP_KERNEL);
		if (!buddy->bits[i])
			goto err_out_free;
		bitmap_zero(buddy->bits[i],
			    1 << (buddy->max_order - i));
	}

	set_bit(0, (long*)buddy->bits[buddy->max_order]);

	return 0;

err_out_free:
	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

	kfree(buddy->bits);

err_out:
	return -ENOMEM;
}

static void mthca_buddy_cleanup(struct mthca_buddy *buddy)
{
	int i;

	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

	kfree(buddy->bits);
}

static u32 mthca_alloc_mtt_range(struct mthca_dev *dev, int order,
				 struct mthca_buddy *buddy)
{
	u32 seg = mthca_buddy_alloc(buddy, order);

	if (seg == -1)
		return (u32)-1;

	if (mthca_is_memfree(dev))
		if (mthca_table_get_range(dev, dev->mr_table.mtt_table, seg,
					  seg + (1 << order) - 1)) {
			mthca_buddy_free(buddy, seg, order);
			seg = (u32)-1;
		}

	return seg;
}

static struct mthca_mtt *__mthca_alloc_mtt(struct mthca_dev *dev, int size,
					   struct mthca_buddy *buddy)
{
	struct mthca_mtt *mtt;
	int i;
	HCA_ENTER(HCA_DBG_MEMORY);
	if (size <= 0)
		return ERR_PTR(-EINVAL);

	mtt = kmalloc(sizeof *mtt, GFP_KERNEL);
	if (!mtt)
		return ERR_PTR(-ENOMEM);

	mtt->buddy = buddy;
	mtt->order = 0;
	for (i = MTHCA_MTT_SEG_SIZE / 8; i < size; i <<= 1)
		++mtt->order;

	mtt->first_seg = mthca_alloc_mtt_range(dev, mtt->order, buddy);
	if (mtt->first_seg == -1) {
		kfree(mtt);
		return ERR_PTR(-ENOMEM);
	}
	HCA_EXIT(HCA_DBG_MEMORY);
	return mtt;
}

struct mthca_mtt *mthca_alloc_mtt(struct mthca_dev *dev, int size)
{
	return __mthca_alloc_mtt(dev, size, &dev->mr_table.mtt_buddy);
}

void mthca_free_mtt(struct mthca_dev *dev, struct mthca_mtt *mtt)
{
	if (!mtt)
		return;

	mthca_buddy_free(mtt->buddy, mtt->first_seg, mtt->order);

	mthca_table_put_range(dev, dev->mr_table.mtt_table,
			      mtt->first_seg,
			      mtt->first_seg + (1 << mtt->order) - 1);

	kfree(mtt);
}

int mthca_write_mtt(struct mthca_dev *dev, struct mthca_mtt *mtt,
		    int start_index, u64 *buffer_list, int list_len)
{
	struct mthca_mailbox *mailbox;
	__be64 *mtt_entry;
	int err = 0;
	u8 status;
	int i;
	u64 val = 1;

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	mtt_entry = mailbox->buf;

	while (list_len > 0) {
		val = dev->mr_table.mtt_base +
			mtt->first_seg * MTHCA_MTT_SEG_SIZE + start_index * 8;
		//TODO: a workaround of bug in _byteswap_uint64
		// in release version optimizer puts the above expression into the function call and generates incorrect code
		// so we call the macro to work around that
		mtt_entry[0] = CL_HTON64(val); 
		mtt_entry[1] = 0;
		for (i = 0; i < list_len && i < MTHCA_MAILBOX_SIZE / 8 - 2; ++i) {
			val = buffer_list[i];
			// BUG in compiler:  it can't perform OR on u64 !!! We perform OR on the low dword
			*(PULONG)&val |= MTHCA_MTT_FLAG_PRESENT;
			mtt_entry[i + 2] = cl_hton64(val);
		}

		/*
		 * If we have an odd number of entries to write, add
		 * one more dummy entry for firmware efficiency.
		 */
		if (i & 1)
			mtt_entry[i + 2] = 0;
		
		dump_mtt(TRACE_LEVEL_VERBOSE, mtt_entry ,i);
		
		err = mthca_WRITE_MTT(dev, mailbox, (i + 1) & ~1, &status);
		if (err) {
			HCA_PRINT(TRACE_LEVEL_WARNING  ,HCA_DBG_MEMORY  ,("WRITE_MTT failed (%d)\n", err));
			goto out;
		}
		if (status) {
			HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_MEMORY,("WRITE_MTT returned status 0x%02x\n",
				   status));
			err = -EINVAL;
			goto out;
		}

		list_len    -= i;
		start_index += i;
		buffer_list += i;
	}

out:
	mthca_free_mailbox(dev, mailbox);
	return err;
}

static inline u32 tavor_hw_index_to_key(u32 ind)
{
	return ind;
}

static inline u32 tavor_key_to_hw_index(u32 key)
{
	return key;
}

static inline u32 arbel_hw_index_to_key(u32 ind)
{
	return (ind >> 24) | (ind << 8);
}

static inline u32 arbel_key_to_hw_index(u32 key)
{
	return (key << 24) | (key >> 8);
}

static inline u32 hw_index_to_key(struct mthca_dev *dev, u32 ind)
{
	if (mthca_is_memfree(dev))
		return arbel_hw_index_to_key(ind);
	else
		return tavor_hw_index_to_key(ind);
}

static inline u32 key_to_hw_index(struct mthca_dev *dev, u32 key)
{
	if (mthca_is_memfree(dev))
		return arbel_key_to_hw_index(key);
	else
		return tavor_key_to_hw_index(key);
}


static inline u32 adjust_key(struct mthca_dev *dev, u32 key)
{
	if (dev->mthca_flags & MTHCA_FLAG_SINAI_OPT)
		return ((key << 20) & 0x800000) | (key & 0x7fffff);
	else
		return key;
}

int mthca_mr_alloc(struct mthca_dev *dev, u32 pd, int buffer_size_shift,
		   u64 iova, u64 total_size, mthca_mpt_access_t access, struct mthca_mr *mr)
{
	struct mthca_mailbox *mailbox;
	struct mthca_mpt_entry *mpt_entry;
	u32 key;
	int err;
	u8 status;
	CPU_2_BE64_PREP;

	WARN_ON(buffer_size_shift >= 32);

	key = mthca_alloc(&dev->mr_table.mpt_alloc);
	if (key == -1)
		return -ENOMEM;
	mr->ibmr.rkey = mr->ibmr.lkey = hw_index_to_key(dev, key);

	if (mthca_is_memfree(dev)) {
		err = mthca_table_get(dev, dev->mr_table.mpt_table, key);
		if (err)
			goto err_out_mpt_free;
	}

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_out_table;
	}
	mpt_entry = mailbox->buf;

	mpt_entry->flags = cl_hton32(MTHCA_MPT_FLAG_SW_OWNS     |
				       MTHCA_MPT_FLAG_MIO         |
				       MTHCA_MPT_FLAG_REGION      |
				       access);
	if (!mr->mtt)
		mpt_entry->flags |= cl_hton32(MTHCA_MPT_FLAG_PHYSICAL);

	mpt_entry->page_size = cl_hton32(buffer_size_shift - 12);
	mpt_entry->key       = cl_hton32(key);
	mpt_entry->pd        = cl_hton32(pd);
	mpt_entry->start     = cl_hton64(iova);
	mpt_entry->length    = cl_hton64(total_size);

	RtlZeroMemory(&mpt_entry->lkey, 
		sizeof *mpt_entry - offsetof(struct mthca_mpt_entry, lkey));

	if (mr->mtt)
		mpt_entry->mtt_seg =
			CPU_2_BE64(dev->mr_table.mtt_base +
				    mr->mtt->first_seg * MTHCA_MTT_SEG_SIZE);

	{
		dump_mpt(TRACE_LEVEL_VERBOSE, mpt_entry);
	}

	err = mthca_SW2HW_MPT(dev, mailbox,
			      key & (dev->limits.num_mpts - 1),
			      &status);
	if (err) {
		HCA_PRINT(TRACE_LEVEL_WARNING  ,HCA_DBG_MEMORY  ,("SW2HW_MPT failed (%d)\n", err));
		goto err_out_mailbox;
	} else if (status) {
		HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_MEMORY,("SW2HW_MPT returned status 0x%02x\n",
			   status));
		err = -EINVAL;
		goto err_out_mailbox;
	}

	mthca_free_mailbox(dev, mailbox);
	return err;

err_out_mailbox:
	mthca_free_mailbox(dev, mailbox);

err_out_table:
	mthca_table_put(dev, dev->mr_table.mpt_table, key);

err_out_mpt_free:
	mthca_free(&dev->mr_table.mpt_alloc, key);
	return err;
}

int mthca_mr_alloc_notrans(struct mthca_dev *dev, u32 pd,
			   mthca_mpt_access_t access, struct mthca_mr *mr)
{
	mr->mtt = NULL;
	return mthca_mr_alloc(dev, pd, 12, 0, ~0Ui64, access, mr);
}

int mthca_mr_alloc_phys(struct mthca_dev *dev, u32 pd,
			u64 *buffer_list, int buffer_size_shift,
			int list_len, u64 iova, u64 total_size,
			mthca_mpt_access_t access, struct mthca_mr *mr)
{
	int err;
	HCA_ENTER(HCA_DBG_MEMORY);
	mr->mtt = mthca_alloc_mtt(dev, list_len);
	if (IS_ERR(mr->mtt)){
		err= PTR_ERR(mr->mtt);
		goto out;
	}

	err = mthca_write_mtt(dev, mr->mtt, 0, buffer_list, list_len);
	if (err) {
		mthca_free_mtt(dev, mr->mtt);
		goto out;
	}

	err = mthca_mr_alloc(dev, pd, buffer_size_shift, iova,
			     total_size, access, mr);
	if (err)
		mthca_free_mtt(dev, mr->mtt);

out:
	HCA_EXIT(HCA_DBG_MEMORY);
	return err;
}

/* Free mr or fmr */
static void mthca_free_region(struct mthca_dev *dev, u32 lkey)
{
	mthca_table_put(dev, dev->mr_table.mpt_table, 	key_to_hw_index(dev, lkey));
	mthca_free(&dev->mr_table.mpt_alloc, key_to_hw_index(dev, lkey));
}

void mthca_free_mr(struct mthca_dev *dev, struct mthca_mr *mr)
{
	int err;
	u8 status;

	err = mthca_HW2SW_MPT(dev, NULL,
			      key_to_hw_index(dev, mr->ibmr.lkey) &
			      (dev->limits.num_mpts - 1),
			      &status);
	if (err){
		HCA_PRINT(TRACE_LEVEL_WARNING  ,HCA_DBG_MEMORY  ,("HW2SW_MPT failed (%d)\n", err));
	}else if (status){
		HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_MEMORY,("HW2SW_MPT returned status 0x%02x\n",
			   status));
	}

	mthca_free_region(dev, mr->ibmr.lkey);
	mthca_free_mtt(dev, mr->mtt);
}

int mthca_fmr_alloc(struct mthca_dev *dev, u32 pd,
		    mthca_mpt_access_t access, struct mthca_fmr *fmr)
{
	struct mthca_mpt_entry *mpt_entry;
	struct mthca_mailbox *mailbox;
	u64 mtt_seg;
	u32 key, idx;
	u8 status;
	int list_len = fmr->attr.max_pages;
	int err = -ENOMEM;
	int i;
	CPU_2_BE64_PREP;
	
	if (fmr->attr.page_shift < 12 || fmr->attr.page_shift >= 32)
		return -EINVAL;

	/* For Arbel, all MTTs must fit in the same page. */
	if (mthca_is_memfree(dev) &&
	    fmr->attr.max_pages * sizeof *fmr->mem.arbel.mtts > PAGE_SIZE)
		return -EINVAL;

	fmr->maps = 0;

	key = mthca_alloc(&dev->mr_table.mpt_alloc);
	if (key == -1)
		return -ENOMEM;
	key = adjust_key(dev, key);

	idx = key & (dev->limits.num_mpts - 1);
	fmr->ibfmr.rkey = fmr->ibfmr.lkey = hw_index_to_key(dev, key);

	if (mthca_is_memfree(dev)) {
		err = mthca_table_get(dev, dev->mr_table.mpt_table, key);
		if (err)
			goto err_out_mpt_free;

		fmr->mem.arbel.mpt = mthca_table_find(dev->mr_table.mpt_table, key);
		BUG_ON(!fmr->mem.arbel.mpt);
	} else
		fmr->mem.tavor.mpt = (struct mthca_mpt_entry*)((u8*)dev->mr_table.tavor_fmr.mpt_base +
			sizeof *(fmr->mem.tavor.mpt) * idx);

	fmr->mtt = __mthca_alloc_mtt(dev, list_len, dev->mr_table.fmr_mtt_buddy);
	if (IS_ERR(fmr->mtt))
		goto err_out_table;

	mtt_seg =fmr->mtt->first_seg * MTHCA_MTT_SEG_SIZE;

	if (mthca_is_memfree(dev)) {
		fmr->mem.arbel.mtts = mthca_table_find(dev->mr_table.mtt_table,
						      fmr->mtt->first_seg);
		BUG_ON(!fmr->mem.arbel.mtts);
	} else
		fmr->mem.tavor.mtts = (u64*)((u8*)dev->mr_table.tavor_fmr.mtt_base + mtt_seg);

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		goto err_out_free_mtt;

	mpt_entry = mailbox->buf;

	mpt_entry->flags = cl_hton32(MTHCA_MPT_FLAG_SW_OWNS     |
				       MTHCA_MPT_FLAG_MIO         |
				       MTHCA_MPT_FLAG_REGION      |
				       access);

	mpt_entry->page_size = cl_hton32(fmr->attr.page_shift - 12);
	mpt_entry->key       = cl_hton32(key);
	mpt_entry->pd        = cl_hton32(pd);
	RtlZeroMemory(&mpt_entry->start, 
		sizeof *mpt_entry - offsetof(struct mthca_mpt_entry, start));
	mpt_entry->mtt_seg   = CPU_2_BE64(dev->mr_table.mtt_base + mtt_seg);

	{
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_MEMORY  ,("Dumping MPT entry %08x:\n", fmr->ibfmr.lkey));
		for (i = 0; i < sizeof (struct mthca_mpt_entry) / 4; i=i+4) {
				HCA_PRINT(TRACE_LEVEL_INFORMATION   ,HCA_DBG_MEMORY   ,("[%02x]  %08x %08x %08x %08x \n",i,
					cl_ntoh32(((__be32 *) mpt_entry)[i]),
					cl_ntoh32(((__be32 *) mpt_entry)[i+1]),
					cl_ntoh32(((__be32 *) mpt_entry)[i+2]),
					cl_ntoh32(((__be32 *) mpt_entry)[i+3])));
		}
	}

	err = mthca_SW2HW_MPT(dev, mailbox,
			      key & (dev->limits.num_mpts - 1),
			      &status);

	if (err) {
		HCA_PRINT(TRACE_LEVEL_WARNING  ,HCA_DBG_MEMORY  ,("SW2HW_MPT failed (%d)\n", err));
		goto err_out_mailbox_free;
	}
	if (status) {
		HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_MEMORY,("SW2HW_MPT returned status 0x%02x\n",
			   status));
		err = -EINVAL;
		goto err_out_mailbox_free;
	}

	mthca_free_mailbox(dev, mailbox);
	return 0;

err_out_mailbox_free:
	mthca_free_mailbox(dev, mailbox);

err_out_free_mtt:
	mthca_free_mtt(dev, fmr->mtt);

err_out_table:
	mthca_table_put(dev, dev->mr_table.mpt_table, key);

err_out_mpt_free:
	mthca_free(&dev->mr_table.mpt_alloc, fmr->ibfmr.lkey);
	return err;
}


int mthca_free_fmr(struct mthca_dev *dev, struct mthca_fmr *fmr)
{
	if (fmr->maps)
		return -EBUSY;

	mthca_free_region(dev, fmr->ibfmr.lkey);
	mthca_free_mtt(dev, fmr->mtt);

	return 0;
}


static inline int mthca_check_fmr(struct mthca_fmr *fmr, u64 *page_list,
				  int list_len, u64 iova)
{
	int page_mask;
	UNREFERENCED_PARAMETER(page_list);

	if (list_len > fmr->attr.max_pages)
		return -EINVAL;

	page_mask = (1 << fmr->attr.page_shift) - 1;

	/* We are getting page lists, so va must be page aligned. */
	if (iova & page_mask)
		return -EINVAL;

	/* Trust the user not to pass misaligned data in page_list */
	#if 0
		for (i = 0; i < list_len; ++i) {
			if (page_list[i] & ~page_mask)
				return -EINVAL;
		}
	#endif	

	if (fmr->maps >= fmr->attr.max_maps)
		return -EINVAL;

	return 0;
}


int mthca_tavor_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
			     int list_len, u64 iova)
{
	struct mthca_fmr *fmr = to_mfmr(ibfmr);
	struct mthca_dev *dev = to_mdev(ibfmr->device);
	struct mthca_mpt_entry mpt_entry;
	u32 key;
	int i, err;
	CPU_2_BE64_PREP;

	err = mthca_check_fmr(fmr, page_list, list_len, iova);
	if (err)
		return err;

	++fmr->maps;

	key = tavor_key_to_hw_index(fmr->ibfmr.lkey);
	key += dev->limits.num_mpts;
	fmr->ibfmr.lkey = fmr->ibfmr.rkey = tavor_hw_index_to_key(key);

	writeb(MTHCA_MPT_STATUS_SW, fmr->mem.tavor.mpt);

	for (i = 0; i < list_len; ++i) {
		__be64 mtt_entry;
		u64 val = page_list[i];
		// BUG in compiler:  it can't perform OR on u64 !!! We perform OR on the low dword
		*(PULONG)&val |= MTHCA_MTT_FLAG_PRESENT;
		mtt_entry = cl_hton64(val);
		mthca_write64_raw(mtt_entry, fmr->mem.tavor.mtts + i);
	}

	mpt_entry.lkey   = cl_hton32(key);
	mpt_entry.length = CPU_2_BE64(list_len * (1Ui64 << fmr->attr.page_shift));
	mpt_entry.start  = cl_hton64(iova);

	__raw_writel((u32) mpt_entry.lkey, &fmr->mem.tavor.mpt->key);
	memcpy_toio(&fmr->mem.tavor.mpt->start, &mpt_entry.start,
		    offsetof(struct mthca_mpt_entry, window_count) -
		    offsetof(struct mthca_mpt_entry, start));

	writeb(MTHCA_MPT_STATUS_HW, fmr->mem.tavor.mpt);

	return 0;
}

int mthca_arbel_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
			     int list_len, u64 iova)
{
	struct mthca_fmr *fmr = to_mfmr(ibfmr);
	struct mthca_dev *dev = to_mdev(ibfmr->device);
	u32 key;
	int i, err;
	CPU_2_BE64_PREP;

	err = mthca_check_fmr(fmr, page_list, list_len, iova);
	if (err)
		return err;

	++fmr->maps;

	key = arbel_key_to_hw_index(fmr->ibfmr.lkey);
	if (dev->mthca_flags & MTHCA_FLAG_SINAI_OPT)
		key += SINAI_FMR_KEY_INC;
	else
		key += dev->limits.num_mpts;
	fmr->ibfmr.lkey = fmr->ibfmr.rkey = arbel_hw_index_to_key(key);

	*(u8 *) fmr->mem.arbel.mpt = MTHCA_MPT_STATUS_SW;

	wmb();

	for (i = 0; i < list_len; ++i) {
		// BUG in compiler:  it can't perform OR on u64 !!! We perform OR on the low dword
		u64 val = page_list[i];
		*(PULONG)&val |= MTHCA_MTT_FLAG_PRESENT;
		fmr->mem.arbel.mtts[i] = cl_hton64(val);
	}

	fmr->mem.arbel.mpt->key    = cl_hton32(key);
	fmr->mem.arbel.mpt->lkey   = cl_hton32(key);
	fmr->mem.arbel.mpt->length = CPU_2_BE64(list_len * (1Ui64 << fmr->attr.page_shift));
	fmr->mem.arbel.mpt->start  = cl_hton64(iova);

	wmb();

	*(u8 *) fmr->mem.arbel.mpt = MTHCA_MPT_STATUS_HW;

	wmb();

	return 0;
}


void mthca_tavor_fmr_unmap(struct mthca_dev *dev, struct mthca_fmr *fmr)
{
	u32 key;

	if (!fmr->maps)
		return;

	key = tavor_key_to_hw_index(fmr->ibfmr.lkey);
	key &= dev->limits.num_mpts - 1;
	fmr->ibfmr.lkey = fmr->ibfmr.rkey = tavor_hw_index_to_key(key);

	fmr->maps = 0;

	writeb(MTHCA_MPT_STATUS_SW, fmr->mem.tavor.mpt);
}


void mthca_arbel_fmr_unmap(struct mthca_dev *dev, struct mthca_fmr *fmr)
{
	u32 key;

	if (!fmr->maps)
		return;

	key = arbel_key_to_hw_index(fmr->ibfmr.lkey);
	key &= dev->limits.num_mpts - 1;
	key = adjust_key(dev, key);
	fmr->ibfmr.lkey = fmr->ibfmr.rkey = arbel_hw_index_to_key(key);

	fmr->maps = 0;

	*(u8 *) fmr->mem.arbel.mpt = MTHCA_MPT_STATUS_SW;
}

int mthca_init_mr_table(struct mthca_dev *dev)
{
	int mpts, mtts, err, i;

	err = mthca_alloc_init(&dev->mr_table.mpt_alloc,
			       (u32)dev->limits.num_mpts,
			       (u32)~0, (u32)dev->limits.reserved_mrws);
	if (err)
		return err;

	if (!mthca_is_memfree(dev) &&
	    (dev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN))
		dev->limits.fmr_reserved_mtts = 0;
	else
		dev->mthca_flags |= MTHCA_FLAG_FMR;

	if (dev->mthca_flags & MTHCA_FLAG_SINAI_OPT)
		HCA_PRINT(TRACE_LEVEL_INFORMATION  ,HCA_DBG_MEMORY	,("Memory key throughput optimization activated.\n"));

	err = mthca_buddy_init(&dev->mr_table.mtt_buddy,
			       fls(dev->limits.num_mtt_segs - 1));

	if (err)
		goto err_mtt_buddy;

	dev->mr_table.tavor_fmr.mpt_base = NULL;
	dev->mr_table.tavor_fmr.mtt_base = NULL;

	if (dev->limits.fmr_reserved_mtts) {
		i = fls(dev->limits.fmr_reserved_mtts - 1);

		if (i >= 31) {
			HCA_PRINT(TRACE_LEVEL_WARNING  ,HCA_DBG_MEMORY  ,("Unable to reserve 2^31 FMR MTTs.\n"));
			err = -EINVAL;
			goto err_fmr_mpt;
		}

		mpts = mtts = 1 << i;

	} else {
		mpts = dev->limits.num_mtt_segs;
		mtts = dev->limits.num_mpts;
	}
	
	if (!mthca_is_memfree(dev) &&
		(dev->mthca_flags & MTHCA_FLAG_FMR)) {

		dev->mr_table.tavor_fmr.mpt_base =
		       	ioremap(dev->mr_table.mpt_base,
				mpts * sizeof (struct mthca_mpt_entry), 
				&dev->mr_table.tavor_fmr.mpt_base_size);

		if (!dev->mr_table.tavor_fmr.mpt_base) {
			HCA_PRINT(TRACE_LEVEL_WARNING  ,HCA_DBG_MEMORY  ,("MPT ioremap for FMR failed.\n"));
			err = -ENOMEM;
			goto err_fmr_mpt;
		}

		dev->mr_table.tavor_fmr.mtt_base =
			ioremap(dev->mr_table.mtt_base,
				mtts * MTHCA_MTT_SEG_SIZE,
				&dev->mr_table.tavor_fmr.mtt_base_size );

		if (!dev->mr_table.tavor_fmr.mtt_base) {
			HCA_PRINT(TRACE_LEVEL_WARNING  ,HCA_DBG_MEMORY  ,("MTT ioremap for FMR failed.\n"));
			err = -ENOMEM;
			goto err_fmr_mtt;
		}
	}

	if (dev->limits.fmr_reserved_mtts) {
		err = mthca_buddy_init(&dev->mr_table.tavor_fmr.mtt_buddy, fls(mtts - 1));
		if (err)
			goto err_fmr_mtt_buddy;

		/* Prevent regular MRs from using FMR keys */
		err = mthca_buddy_alloc(&dev->mr_table.mtt_buddy, fls(mtts - 1));
		if (err)
			goto err_reserve_fmr;

		dev->mr_table.fmr_mtt_buddy =
			&dev->mr_table.tavor_fmr.mtt_buddy;
	} else
		dev->mr_table.fmr_mtt_buddy = &dev->mr_table.mtt_buddy;

	/* FMR table is always the first, take reserved MTTs out of there */
	if (dev->limits.reserved_mtts) {
		i = fls(dev->limits.reserved_mtts - 1);

		if (mthca_alloc_mtt_range(dev, i,
					  dev->mr_table.fmr_mtt_buddy) == -1) {
			HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_MEMORY,("MTT table of order %d is too small.\n",
				  dev->mr_table.fmr_mtt_buddy->max_order));
			err = -ENOMEM;
			goto err_reserve_mtts;
		}
	}

	return 0;

err_reserve_mtts:
err_reserve_fmr:
	if (dev->limits.fmr_reserved_mtts)
		mthca_buddy_cleanup(&dev->mr_table.tavor_fmr.mtt_buddy);

err_fmr_mtt_buddy:
	if (dev->mr_table.tavor_fmr.mtt_base)
		iounmap(dev->mr_table.tavor_fmr.mtt_base,
			dev->mr_table.tavor_fmr.mtt_base_size);

err_fmr_mtt:
	if (dev->mr_table.tavor_fmr.mpt_base)
		iounmap(dev->mr_table.tavor_fmr.mpt_base,
			dev->mr_table.tavor_fmr.mpt_base_size);

err_fmr_mpt:
	mthca_buddy_cleanup(&dev->mr_table.mtt_buddy);

err_mtt_buddy:
	mthca_alloc_cleanup(&dev->mr_table.mpt_alloc);

	return err;
}

void mthca_cleanup_mr_table(struct mthca_dev *dev)
{
	/* XXX check if any MRs are still allocated? */
	if (dev->limits.fmr_reserved_mtts)
		mthca_buddy_cleanup(&dev->mr_table.tavor_fmr.mtt_buddy);

	mthca_buddy_cleanup(&dev->mr_table.mtt_buddy);

	if (dev->mr_table.tavor_fmr.mtt_base)
		iounmap(dev->mr_table.tavor_fmr.mtt_base,
			dev->mr_table.tavor_fmr.mtt_base_size);
	if (dev->mr_table.tavor_fmr.mpt_base)
		iounmap(dev->mr_table.tavor_fmr.mpt_base,
			dev->mr_table.tavor_fmr.mpt_base_size);

	mthca_alloc_cleanup(&dev->mr_table.mpt_alloc);
}


