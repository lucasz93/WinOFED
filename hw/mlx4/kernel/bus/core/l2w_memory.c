/*
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies.  All rights reserved.
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
#include "l2w.h"
#include <mlx4_debug.h>

#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "l2w_memory.tmh"
#endif 



void *alloc_cont_mem(
	IN		struct pci_dev *pdev, 
	IN		unsigned long size,
	OUT		dma_addr_t*p_dma_addr)
{
	void *va;
	DMA_ADAPTER *p_adapter = pdev->p_dma_adapter;
	PHYSICAL_ADDRESS  pa = {0};

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	memset( p_dma_addr, 0, sizeof(dma_addr_t) );

	if (!size)
		return NULL;

	va	= p_adapter->DmaOperations->AllocateCommonBuffer(
		p_adapter, (ULONG)size, &pa, FALSE );
	if (va) {
		p_dma_addr->da = pa.QuadPart;
		p_dma_addr->va = va;
		p_dma_addr->sz	= (ULONG)size;
	}

	return va;
}

void free_cont_mem(
	IN		struct pci_dev *pdev, 
	IN		dma_addr_t*p_dma_addr)
{
	PHYSICAL_ADDRESS  pa;
	DMA_ADAPTER *p_adapter = pdev->p_dma_adapter;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	pa.QuadPart = p_dma_addr->da;
	p_adapter->DmaOperations->FreeCommonBuffer( 
		p_adapter, (ULONG)p_dma_addr->sz, pa, p_dma_addr->va, FALSE );
}

void *
dma_alloc_coherent( struct mlx4_dev **dev, size_t size, 
	dma_addr_t *p_dma_addr, gfp_t gfp )
{
	UNUSED_PARAM(gfp);

	if (!size)
		return NULL;
	return alloc_cont_mem( (*dev)->pdev, (unsigned long)size, p_dma_addr );
}

void 
dma_free_coherent( struct mlx4_dev **dev, size_t size, 
	void *vaddr, dma_addr_t dma_addr)
{
	UNUSED_PARAM(size);
	UNUSED_PARAM(vaddr);
	ASSERT(size == dma_addr.sz);
	ASSERT(vaddr == dma_addr.va);
	free_cont_mem( (*dev)->pdev, &dma_addr );
}

void 
pci_free_consistent( struct pci_dev *pdev, size_t size, 
	void *vaddr, dma_addr_t dma_addr)
{
	dma_free_coherent( &pdev->dev, size, vaddr, dma_addr );
}



