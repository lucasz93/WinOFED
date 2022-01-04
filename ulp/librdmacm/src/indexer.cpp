/*
 * Copyright (c) 2011 Intel Corporation.  All rights reserved.
 * Copyright (c) 2012 Oce Printing Systems GmbH.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
 *      - Neither the name Oce Printing Systems GmbH nor the names
 *        of the authors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 * THIS SOFTWARE IS PROVIDED  “AS IS” AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
 * OR CONTRIBUTOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE. 
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <windows.h>

#include <sys/types.h>
#include <stdlib.h>

#include "indexer.h"
#include "cma.h"

/*
 * Indexer - to find a structure given an index
 *
 * We store pointers using a double lookup and return an index to the
 * user which is then used to retrieve the pointer.  The upper bits of
 * the index are itself an index into an array of memory allocations.
 * The lower bits specify the offset into the allocated memory where
 * the pointer is stored.
 *
 * This allows us to adjust the number of pointers stored by the index
 * list without taking a lock during data lookups.
 */

static int idx_grow(struct indexer *idx)
{
	union idx_entry *entry;
	int i, start_index;

	if (idx->size >= IDX_ARRAY_SIZE)
		goto nomem;

	idx->array[idx->size] = (union idx_entry *)calloc(IDX_ENTRY_SIZE, sizeof(union idx_entry));
	if (!idx->array[idx->size])
		goto nomem;

	entry = idx->array[idx->size];
	start_index = idx->size << IDX_ENTRY_BITS;
	entry[IDX_ENTRY_SIZE - 1].next = idx->free_list;

	for (i = IDX_ENTRY_SIZE - 2; i >= 0; i--)
		entry[i].next = start_index + i + 1;

	/* Index 0 is reserved */
	if (start_index == 0)
		start_index++;
	idx->free_list = start_index;
	idx->size++;
	return start_index;

nomem:
	errno = ENOMEM;
	return -1;
}

int idx_insert(struct indexer *idx, void *item)
{
	union idx_entry *entry;
	int index;

	if ((index = idx->free_list) == 0) {
		if ((index = idx_grow(idx)) <= 0)
			return index;
	}

	entry = idx->array[idx_array_index(index)];
	idx->free_list = entry[idx_entry_index(index)].next;
	entry[idx_entry_index(index)].item = item;
	return index;
}

void *idx_remove(struct indexer *idx, int index)
{
	union idx_entry *entry;
	void *item;

	entry = idx->array[idx_array_index(index)];
	item = entry[idx_entry_index(index)].item;
	entry[idx_entry_index(index)].next = idx->free_list;
	idx->free_list = index;
	return item;
}

void idx_replace(struct indexer *idx, int index, void *item)
{
	union idx_entry *entry;

	entry = idx->array[idx_array_index(index)];
	entry[idx_entry_index(index)].item = item;
}


static int idm_grow(struct index_map *idm, int index)
{
	idm->array[idx_array_index(index)] = (void **)calloc(IDX_ENTRY_SIZE, sizeof(void *));
	if (!idm->array[idx_array_index(index)])
		goto nomem;

	return index;

nomem:
	errno = ENOMEM;
	return -1;
}

int idm_set(struct index_map *idm, int index, void *item)
{
	void **entry;

	if (index > IDX_MAX_INDEX) {
		errno = ENOMEM;
		return -1;
	}

	if (!idm->array[idx_array_index(index)]) {
		if (idm_grow(idm, index) < 0)
			return -1;
	}

	entry = idm->array[idx_array_index(index)];
	entry[idx_entry_index(index)] = item;
	return index;
}

void *idm_clear(struct index_map *idm, int index)
{
	void **entry;
	void *item;

	entry = idm->array[idx_array_index(index)];
	item = entry[idx_entry_index(index)];
	entry[idx_entry_index(index)] = NULL;
	return item;
}
