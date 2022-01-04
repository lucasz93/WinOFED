/*
 * Copyright (c) 2009 Intel Corp., Inc.
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
 *
 */

#include <stdlib.h>
#include <search.h>

static int (*compare)(const void *, const void *);

static intn_t fcompare(const void * const key1, const void * const key2)
{
	return (intn_t) compare((void *) key1, (void *) key2);
}

void *tsearch(const void *key, void **rootp,
			  int (*compar)(const void *, const void *))
{
	cl_fmap_item_t *item, *map_item;

	if (!*rootp) {
		*rootp = malloc(sizeof(cl_fmap_t));
		if (!*rootp)
			return NULL;
		cl_fmap_init((cl_fmap_t *) *rootp, fcompare);
	}

	compare = compar;
	item = malloc(sizeof(cl_fmap_item_t));
	if (!item)
		return NULL;

	map_item = cl_fmap_insert((cl_fmap_t *) *rootp, key, item);
	if (map_item != item)
		free(item);

	return (void *) &map_item->p_key;
}

void *tfind(const void *key, void *const *rootp,
			int (*compar)(const void *, const void *))
{
	cl_fmap_item_t	*item;

	if (!*rootp)
		return NULL;

	compare = compar;
	item = cl_fmap_get((cl_fmap_t *) *rootp, key);
	if (item == cl_fmap_end((cl_fmap_t *) *rootp))
		return NULL;

	return (void *) &item->p_key;
}

/*
 * Returns NULL if item is not found, or the item itself.  This differs
 * from the tdelete call by not retuning the parent item, but works if
 * the user is only checking against NULL.
 */
void *tdelete(const void *key, void **rootp,
			  int (*compar)(const void *, const void *))
{
	cl_fmap_item_t	*item;
	void *map_key;

	if (!*rootp)
		return NULL;

	compare = compar;
	item = cl_fmap_remove((cl_fmap_t *) *rootp, key);
	if (item == cl_fmap_end((cl_fmap_t *) *rootp))
		return NULL;

	map_key = (void *) item->p_key;
	free(item);
	return map_key;
}

//void twalk(const void *root,
//		   void (*action)(const void *, VISIT, int))
//{
//}

//void tdestroy(void *root, void (*free_node)(void *nodep))
//{
//}
