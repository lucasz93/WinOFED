/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "index_list.h"

INDEX_ENTRY EmptyList;

static BOOLEAN IndexListGrow(INDEX_LIST *pIndexList)
{
	INDEX_ENTRY	*array;
	SIZE_T		size, i;

	size = pIndexList->Size + (PAGE_SIZE / sizeof(INDEX_ENTRY));
	array = (INDEX_ENTRY*)ExAllocatePoolWithTag(NonPagedPool, size * sizeof(INDEX_ENTRY), 'xdni');
	if (array == NULL) {
		return FALSE;
	}

	i = size;
	while (i-- > pIndexList->Size) {
		array[i].pItem = NULL;
		array[i].Next = pIndexList->FreeList;
		pIndexList->FreeList = i;
	}

	if (pIndexList->pArray != &EmptyList) {
		RtlCopyMemory(array, pIndexList->pArray, pIndexList->Size * sizeof(INDEX_ENTRY));
		ExFreePool(pIndexList->pArray);
	} else {
		array[0].Next = 0;
		array[0].Prev = 0;
		pIndexList->FreeList = 1;
	}

	pIndexList->Size = size;
	pIndexList->pArray = array;
	return TRUE;
}

SIZE_T IndexListInsertHead(INDEX_LIST *pIndexList, void *pItem)
{
	INDEX_ENTRY	*entry;
	SIZE_T		index;

	if (pIndexList->FreeList == 0 && !IndexListGrow(pIndexList)) {
		return 0;
	}

	index = pIndexList->FreeList;
	entry = &pIndexList->pArray[index];
	pIndexList->FreeList = entry->Next;

	entry->pItem = pItem;
	entry->Next = pIndexList->pArray[0].Next;
	pIndexList->pArray[0].Next = index;
	entry->Prev = 0;
	pIndexList->pArray[entry->Next].Prev = index;

	return index;
}

void *IndexListRemove(INDEX_LIST *pIndexList, SIZE_T Index)
{
	INDEX_ENTRY	*entry;
	void		*item;

	if (Index >= pIndexList->Size) {
		return NULL;
	}

	entry = &pIndexList->pArray[Index];
	if (entry->pItem == NULL) {
		return NULL;
	}

	pIndexList->pArray[entry->Next].Prev = entry->Prev;
	pIndexList->pArray[entry->Prev].Next = entry->Next;

	item = entry->pItem;
	entry->pItem = NULL;
	entry->Next = pIndexList->FreeList;
	pIndexList->FreeList = Index;
	return item;
}
