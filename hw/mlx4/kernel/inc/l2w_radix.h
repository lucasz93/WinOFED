#pragma once

#include <complib/cl_map.h>

struct radix_tree_root {
	cl_map_t	map;
};

int radix_tree_insert(struct radix_tree_root *root,
	unsigned long index, void *item);

void *radix_tree_lookup(struct radix_tree_root *root, 
	unsigned long index);

void *radix_tree_delete(struct radix_tree_root *root, 
	unsigned long index);


cl_status_t radix_tree_create(struct radix_tree_root *root,
	gfp_t gfp_mask);

void radix_tree_destroy(struct radix_tree_root *root );

#define INIT_RADIX_TREE(root, mask)	radix_tree_create(root, mask)
#define RMV_RADIX_TREE(root)		radix_tree_destroy(root)

