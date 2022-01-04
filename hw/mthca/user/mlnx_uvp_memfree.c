/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
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

#include <mt_l2w.h>

#include "mlnx_uvp.h"

#define MTHCA_FREE_MAP_SIZE (MTHCA_DB_REC_PER_PAGE / BITS_PER_LONG)

struct mthca_db_page {
	unsigned long free[MTHCA_FREE_MAP_SIZE];
	uint64_t     *db_rec;
};

struct mthca_db_table {
	int 	       	     npages;
	int 	       	     max_group1;
	int 	       	     min_group2;
	HANDLE      mutex;
	struct mthca_db_page page[];
};

int mthca_alloc_db(struct mthca_db_table *db_tab, enum mthca_db_type type,
		  volatile uint32_t **db)
{
	int i, j, k;
	int group, start, end, dir;
	int ret = 0;

	WaitForSingleObject( db_tab->mutex, INFINITE );

	switch (type) {
	case MTHCA_DB_TYPE_CQ_ARM:
	case MTHCA_DB_TYPE_SQ:
		group = 0;
		start = 0;
		end   = db_tab->max_group1;
		dir   = 1;
		break;

	case MTHCA_DB_TYPE_CQ_SET_CI:
	case MTHCA_DB_TYPE_RQ:
	case MTHCA_DB_TYPE_SRQ:
		group = 1;
		start = db_tab->npages - 1;
		end   = db_tab->min_group2;
		dir   = -1;
		break;

	default:
		ret = -1;
		goto out;
	}

	for (i = start; i != end; i += dir)
		if (db_tab->page[i].db_rec)
			for (j = 0; j < MTHCA_FREE_MAP_SIZE; ++j)
				if (db_tab->page[i].free[j]) 
					goto found;

	if (db_tab->max_group1 >= db_tab->min_group2 - 1) {
		ret = -1;
		goto out;
	}

	if (posix_memalign((void **) &db_tab->page[i].db_rec, MTHCA_DB_REC_PAGE_SIZE,
			   MTHCA_DB_REC_PAGE_SIZE)) {
		ret = -1;
		goto out;
	}

	memset(db_tab->page[i].db_rec, 0, MTHCA_DB_REC_PAGE_SIZE);
	memset(db_tab->page[i].free, 0xff, sizeof db_tab->page[i].free);

	if (group == 0)
		++db_tab->max_group1;
	else
		--db_tab->min_group2;

found:
	for (j = 0; j < MTHCA_FREE_MAP_SIZE; ++j) {
		k = ffsl(db_tab->page[i].free[j]);
		if (k)
			break;
	}

	if (!k) {
		ret = -1;
		goto out;
	}

	--k;
	db_tab->page[i].free[j] &= ~(1UL << k);

	j = j * BITS_PER_LONG + k;
	if (group == 1)
		j = MTHCA_DB_REC_PER_PAGE - 1 - j;

	ret = i * MTHCA_DB_REC_PER_PAGE + j;
	*db = (volatile uint32_t *) &db_tab->page[i].db_rec[j];
	
out:
	ReleaseMutex( db_tab->mutex );
	return ret;
}

void mthca_set_db_qn(uint32_t *db, enum mthca_db_type type, uint32_t qn)
{
	db[1] = cl_hton32((qn << 8) | (type << 5));
}

void mthca_free_db(struct mthca_db_table *db_tab, enum mthca_db_type type, int db_index)
{
	int i, j;
	struct mthca_db_page *page;

	i = db_index / MTHCA_DB_REC_PER_PAGE;
	j = db_index % MTHCA_DB_REC_PER_PAGE;

	page = db_tab->page + i;

	WaitForSingleObject( db_tab->mutex, INFINITE );
	page->db_rec[j] = 0;

	if (i >= db_tab->min_group2)
		j = MTHCA_DB_REC_PER_PAGE - 1 - j;

	page->free[j / BITS_PER_LONG] |= 1UL << (j % BITS_PER_LONG);

	ReleaseMutex( db_tab->mutex );
}

struct mthca_db_table *mthca_alloc_db_tab(int uarc_size)
{
	struct mthca_db_table *db_tab;
	int npages;
	int i;

	npages = uarc_size / MTHCA_DB_REC_PAGE_SIZE;
	db_tab = cl_malloc(sizeof (struct mthca_db_table) +
			npages * sizeof (struct mthca_db_page));
	if (!db_tab)
		goto err_malloc;

	db_tab->mutex = CreateMutex( NULL, FALSE, NULL );
	if (!db_tab->mutex)
		goto err_mutex;
	db_tab->npages     = npages;
	db_tab->max_group1 = 0;
	db_tab->min_group2 = npages - 1;

	for (i = 0; i < npages; ++i)
		db_tab->page[i].db_rec = NULL;

	goto end;

err_mutex:
	cl_free(db_tab);
err_malloc:
end:	
	return db_tab;
}

void mthca_free_db_tab(struct mthca_db_table *db_tab)
{
	int i;

	if (!db_tab)
		return;

	for (i = 0; i < db_tab->npages; ++i)
		if (db_tab->page[i].db_rec)
			posix_memfree( db_tab->page[i].db_rec);

	cl_free(db_tab);
}
