/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "l2w.h"
#include "errno.h"

int radix_tree_insert(struct radix_tree_root *root,
	unsigned long index, void *item)
{
	if ( NULL == cl_map_insert( &root->map, (const uint64_t)index, item ) )
		return -EFAULT;
	return 0;
}

void *radix_tree_lookup(struct radix_tree_root *root, 
	unsigned long index)
{
	void* item = cl_map_get( &root->map, (const uint64_t)index );
	return item;
}

void *radix_tree_delete(struct radix_tree_root *root, 
	unsigned long index)
{
	void* item = cl_map_remove( &root->map, (const uint64_t)index );
	return item;
}

cl_status_t radix_tree_create(struct radix_tree_root *root,
	gfp_t gfp_mask)
{
#define MIN_ITEMS		32
	cl_status_t cl_status;
	UNUSED_PARAM(gfp_mask);

	cl_map_construct( &root->map );
	cl_status = cl_map_init( &root->map, MIN_ITEMS );
	return cl_status;
}

void radix_tree_destroy(struct radix_tree_root *root )
{
	cl_map_destroy( &root->map );
}

