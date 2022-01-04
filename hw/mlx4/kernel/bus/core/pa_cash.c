/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
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
#include "l2w.h"
#include "pa_cash.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "pa_cash.tmh"
#endif

///////////////////////////////////////////////////////////////////////////
//
// RESTRICTIONS
//
///////////////////////////////////////////////////////////////////////////

#ifdef _WIN64
// be careful with setting it >= 4G. Compiler puts it into an integer, so 4*1024*1024*1024 = 0 !!!
#define MAX_PAGES_SUPPORTED	((u32)2 * 1024 * 1024 * 1024)					// 8 TB
#else
#define MAX_PAGES_SUPPORTED	((u32)64 * 1024 * 1024)							// 256 GB
#endif

#define FREE_LIST_TRESHOLD		256		// max number of pages in free list

///////////////////////////////////////////////////////////////////////////
//
// CONSTANTS
//
///////////////////////////////////////////////////////////////////////////

#define PA_TABLE_ENTRY_SIZE		sizeof(pa_table_entry_t)					// 4

#define PA_DIR_ENTRY_SIZE		sizeof(pa_dir_entry_t)						// 16 for x64


///////////////////////////////////////////////////////////////////////////
//
// STRUCTURES
//
///////////////////////////////////////////////////////////////////////////

typedef struct {
	int	ref_cnt;
} pa_table_entry_t;

typedef struct {
	pa_table_entry_t *pa_te;		/* pointer to one page of pa_table_entry_t elements */
	int used;						/* number of pa_table_entry_t elements, used now. When 0 - the page may be freed */
} pa_dir_entry_t;

typedef struct pa_cash_s {
	pa_dir_entry_t *pa_dir;
	SINGLE_LIST_ENTRY  free_list_hdr;
	uint32_t free_nr_pages;
	uint32_t free_list_threshold;
	uint32_t max_nr_pages;
	uint32_t cur_nr_pages;
} pa_cash_t;



///////////////////////////////////////////////////////////////////////////
//
// GLOBALS
//
///////////////////////////////////////////////////////////////////////////

DEFINE_MUTEX(g_pa_mutex);
u64 g_pa[1024];
pa_cash_t g_cash;
u32 g_max_pages_supported = 0;
u32 g_pa_table_entry_num = 0;
u32 g_pa_table_size = 0;
u32 g_pa_dir_entry_num = 0;
u32 g_pa_dir_size = 0;


///////////////////////////////////////////////////////////////////////////
//
// STATIC FUNCTIONS
//
///////////////////////////////////////////////////////////////////////////

static uint32_t pa_calc_threshold()
{
	// threshold expresses the max length of free pages list, which gets released only at driver unload time
	// so it can be calculated to be proportional to the system memory size
	return FREE_LIST_TRESHOLD;
}

static pa_table_entry_t *pa_alloc_page()
{
	pa_table_entry_t *pa_te;

	/* take from the list of reserved if it is not empty */
	if (g_cash.free_nr_pages) {
		pa_te = (pa_table_entry_t *)PopEntryList( &g_cash.free_list_hdr );
		((SINGLE_LIST_ENTRY*)pa_te)->Next = NULL;
		g_cash.free_nr_pages--;
	}
	else  /* allocate new page */
		pa_te = (pa_table_entry_t *)kzalloc( g_pa_table_size, GFP_KERNEL );

	return pa_te;
}

static void pa_free_page(pa_table_entry_t *pa_te)
{
	if (g_cash.free_nr_pages < g_cash.free_list_threshold) {
		PushEntryList( &g_cash.free_list_hdr, (SINGLE_LIST_ENTRY*)pa_te );
		g_cash.free_nr_pages++;
	}
	else
		kfree(pa_te);
}

static pa_table_entry_t * pa_get_page(uint32_t ix)
{
	pa_table_entry_t *pa_te =  g_cash.pa_dir[ix / g_pa_table_entry_num].pa_te;

	/* no this page_table - add a new one */
	if (!pa_te) {
		pa_te = pa_alloc_page();
		if (!pa_te) 
			return NULL;
		g_cash.pa_dir[ix / g_pa_table_entry_num].pa_te = pa_te;
		g_cash.pa_dir[ix / g_pa_table_entry_num].used = 0;
		g_cash.cur_nr_pages++;
	}

	return pa_te;
}

static void pa_put_page(uint32_t ix)
{
	pa_free_page(g_cash.pa_dir[ix / g_pa_table_entry_num].pa_te);
	g_cash.pa_dir[ix / g_pa_table_entry_num].pa_te = NULL;
	g_cash.cur_nr_pages--;
}

static int pa_add_pa(uint64_t pa)
{
	uint32_t ix = (uint32_t)(pa >> PAGE_SHIFT);
	pa_table_entry_t *pa_te;

	/* or pa is incorrect or memory that big is not supported */
	if (ix > g_cash.max_nr_pages) {
		ASSERT(FALSE);
		return -EFAULT;
	}

	/* get page address */
	pa_te = pa_get_page(ix);
	if (!pa_te)
		return -ENOMEM;
	
	/* register page address */
	if (!pa_te[ix % g_pa_table_entry_num].ref_cnt)
		++g_cash.pa_dir[ix / g_pa_table_entry_num].used;
	++pa_te[ix % g_pa_table_entry_num].ref_cnt;

	return 0;
}


static int pa_rmv_pa(uint64_t pa)
{
	uint32_t ix = (uint32_t)(pa >> PAGE_SHIFT);
	pa_table_entry_t *pa_te;

	/* or pa is incorrect or memory that big is not supported */
	if (ix > g_cash.max_nr_pages) {
		ASSERT(FALSE);
		return -EFAULT;
	}
		
	pa_te =  g_cash.pa_dir[ix / g_pa_table_entry_num].pa_te;

	/* no this page_table - error*/
	if (!pa_te)  {
		ASSERT(FALSE);
		return -EFAULT;
	}

	/* deregister page address */
	--pa_te[ix % g_pa_table_entry_num].ref_cnt;
	ASSERT(pa_te[ix % g_pa_table_entry_num].ref_cnt >= 0);

	/* release the page on need */
	if (!pa_te[ix % g_pa_table_entry_num].ref_cnt)
		--g_cash.pa_dir[ix / g_pa_table_entry_num].used;
	if (!g_cash.pa_dir[ix / g_pa_table_entry_num].used) 
		pa_put_page(ix);
		
	return 0;
}



///////////////////////////////////////////////////////////////////////////
//
// PUBLIC FUNCTIONS
//
///////////////////////////////////////////////////////////////////////////


int pa_register(iobuf_t *iobuf_p)
{
	int i,j,n;
	iobuf_iter_t iobuf_iter;

	iobuf_iter_init( iobuf_p, &iobuf_iter );
	n = 0;
	for (;;) {
		i = iobuf_get_tpt_seg( iobuf_p, &iobuf_iter, 
			sizeof(g_pa) / sizeof (u64), g_pa );
		if (!i)
			break;
		for (j=0; j<i; ++j, ++n)
			if (pa_add_pa(g_pa[j]))
				goto cleanup;
	}
	return 0;

cleanup:	
	iobuf_iter_init( iobuf_p, &iobuf_iter );
	for (;;) {
		i = iobuf_get_tpt_seg( iobuf_p, &iobuf_iter, 
			sizeof(g_pa) / sizeof (u64), g_pa );
		if (!i)
			break;
		for (j=0; j<i; ++j, --n) {
			pa_rmv_pa(g_pa[j]);
			if (n<=0)
				goto done;
		}
	}
done:	
	return -ENOMEM;
}

void pa_deregister(iobuf_t *iobuf_p)
{
	int i,j;
	iobuf_iter_t iobuf_iter;

	iobuf_iter_init( iobuf_p, &iobuf_iter );
	for (;;) {
		i = iobuf_get_tpt_seg( iobuf_p, &iobuf_iter, 
			sizeof(g_pa) / sizeof (u64), g_pa );
		if (!i)
			break;
		for (j=0; j<i; ++j)
			pa_rmv_pa(g_pa[j]);
	}
}

void pa_cash_print()
{
	MLX4_PRINT(TRACE_LEVEL_INFORMATION ,MLX4_DBG_LOW,
		("pa_cash_print: max_nr_pages %d (%#x), cur_nr_pages  %d (%#x), free_list_hdr %d, free_threshold %d\n",
		g_cash.max_nr_pages, g_cash.max_nr_pages, 
		g_cash.cur_nr_pages, g_cash.cur_nr_pages,
		g_cash.free_nr_pages, g_cash.free_list_threshold ));
}


void pa_cash_release()
{
	u32 i;

	pa_cash_print();

	if (!g_cash.pa_dir)
		return;

	/* free cash tables */
	for (i=0; i<g_pa_dir_entry_num; ++i)
		if (g_cash.pa_dir[i].pa_te) {
			kfree(g_cash.pa_dir[i].pa_te);
			g_cash.cur_nr_pages--;
		}
			
	kfree(g_cash.pa_dir);
	g_cash.pa_dir = NULL;

	while (g_cash.free_nr_pages) {
		void *page = PopEntryList( &g_cash.free_list_hdr );
		g_cash.free_nr_pages--;
		kfree(page);
	}
	ASSERT(g_cash.free_list_hdr.Next == NULL);
	ASSERT(g_cash.cur_nr_pages == 0);
}

int pa_is_registered(uint64_t pa)
{
	uint32_t ix = (uint32_t)(pa >> PAGE_SHIFT);
	pa_table_entry_t *pa_te;

	/* or pa is incorrect or memory that big is not supported */
	if (ix > g_cash.max_nr_pages) {
		ASSERT(FALSE);
		return -EFAULT;
	}
		
	pa_te =  g_cash.pa_dir[ix / g_pa_table_entry_num].pa_te;

	/* no this page_table */
	if (!pa_te)
		return 0;

	return pa_te[ix % g_pa_table_entry_num].ref_cnt;
}

int pa_cash_init()
{
	void *pa_dir;

	g_max_pages_supported = MAX_PAGES_SUPPORTED;
	g_pa_table_entry_num = PAGE_SIZE / PA_TABLE_ENTRY_SIZE;
	g_pa_table_size = PA_TABLE_ENTRY_SIZE * g_pa_table_entry_num;
	g_pa_dir_entry_num = g_max_pages_supported /g_pa_table_entry_num;
	g_pa_dir_size = PA_DIR_ENTRY_SIZE * g_pa_dir_entry_num;

	pa_dir = kzalloc(g_pa_dir_size, GFP_KERNEL);

	if (!pa_dir)
		return -ENOMEM;
	g_cash.pa_dir = (pa_dir_entry_t *)pa_dir;
	g_cash.max_nr_pages = g_pa_table_entry_num * g_pa_dir_entry_num;
	g_cash.free_list_hdr.Next = NULL;
	g_cash.cur_nr_pages = 0;
	g_cash.free_nr_pages = 0;
	g_cash.free_list_threshold = pa_calc_threshold();
	mutex_init(&g_pa_mutex);
	return 0;
}
	
