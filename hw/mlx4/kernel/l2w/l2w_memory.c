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
#include "l2w_precomp.h"

#ifdef offsetof
#undef offsetof
#endif
#if defined(EVENT_TRACING)
#include "l2w_memory.tmh"
#endif

void st_dev_add_cont_mem_stat( PMLX4_ST_DEVICE p_stat, ULONG size );
void st_dev_rmv_cont_mem_stat( PMLX4_ST_DEVICE p_stat, ULONG size );
void st_et_event_func( const char * const  Caller, const char * const  Format, ... );
#define st_et_event(_Format, ...)	st_et_event_func( __FUNCTION__, _Format, __VA_ARGS__ )
PVOID st_mm_alloc(ULONG size);
boolean_t st_mm_free(PVOID ptr, ULONG size);

void *alloc_cont_mem(
	IN		struct pci_dev *pdev, 
	IN		unsigned long size,
	OUT		dma_addr_t*p_dma_addr)
{
	void *va = NULL;
	PHYSICAL_ADDRESS  pa = {0};

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	memset( p_dma_addr, 0, sizeof(dma_addr_t) );
	if (!size)
		goto end;

//
// DmaOperations->AllocateCommonBuffer can get stuck for a long time 
// when there is no enough contiguous memory
//

#ifdef SUPPORT_DMA_MEMORY

	{
		DMA_ADAPTER *p_adapter = pdev->p_dma_adapter;

		va	= p_adapter->DmaOperations->AllocateCommonBuffer(
			p_adapter, size, &pa, FALSE );
		if (va) {
			p_dma_addr->da = pa.QuadPart;
			p_dma_addr->va = va;
			p_dma_addr->sz	= (ULONG)size;
			st_dev_add_cont_mem_stat( pdev->p_stat, size );
		}
	}

#else

	{
		PHYSICAL_ADDRESS la = {0};
		PHYSICAL_ADDRESS ha;
		ha.QuadPart = (-1I64);		

		uint64_t alloc_start_time = get_tickcount_in_ms(), alloc_end_time;
		
		va = st_mm_alloc(size);
		if ( !va )
#if WINVER >= 0x0601	
			va = MmAllocateContiguousMemorySpecifyCacheNode( (SIZE_T)size, la, ha, pa, MmCached, KeGetCurrentNodeNumber() );
		if(! va)
#endif
			va = MmAllocateContiguousMemorySpecifyCache( (SIZE_T)size, la, ha, pa, MmCached );

		alloc_end_time = get_tickcount_in_ms();
		
		if (va) {
			pa = MmGetPhysicalAddress( va );
			// TODO: convert physical adress to dma one 
			p_dma_addr->da = pa.QuadPart;
			p_dma_addr->va = va;
			p_dma_addr->sz	= (ULONG)size;
			st_dev_add_cont_mem_stat( pdev->p_stat, size );
		}

		if(alloc_end_time - alloc_start_time > 100)
		{
			L2W_PRINT(TRACE_LEVEL_ERROR ,L2W ,
							"%s: AllocateCommonBuffer: contiguous memory allocation took too long - %d ms\n", 
							pdev->name, (u32)(alloc_end_time - alloc_start_time) );
			st_et_event("%s: AllocateCommonBuffer: contiguous memory allocation took too long - %d ms\n", 
							pdev->name, (u32)(alloc_end_time - alloc_start_time));
		}
	}

#endif

end:
	if (!va)
		L2W_PRINT(TRACE_LEVEL_ERROR ,L2W ,
			"%s: AllocateCommonBuffer: failed to allocate contiguous %#x bytes\n", 
			pdev->name, size );
	return va;
}

void free_cont_mem(
	IN		struct pci_dev *pdev, 
	IN		dma_addr_t*p_dma_addr)
{
#ifdef SUPPORT_DMA_MEMORY

	{
		DMA_ADAPTER *p_adapter = pdev->p_dma_adapter;
		PHYSICAL_ADDRESS  pa;
		
		ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
		pa.QuadPart = p_dma_addr->da;
		p_adapter->DmaOperations->FreeCommonBuffer( 
			p_adapter, p_dma_addr->sz, pa, p_dma_addr->va, FALSE );
		st_dev_rmv_cont_mem_stat( pdev->p_stat, p_dma_addr->sz );
	}

#else

	{
#ifndef NTDDI_WIN8
        // BUGBUG: We need to verify with ms how we should call this function on windows 8. 
        // Until then we remain in passeive level ...
		KIRQL old_irql = 0, cur_irql = KeGetCurrentIrql();

		ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
		if (cur_irql < APC_LEVEL)
			KeRaiseIrql( APC_LEVEL, &old_irql );       
#endif        

		if ( !st_mm_free(p_dma_addr->va, (ULONG)(p_dma_addr->sz) ) )
			MmFreeContiguousMemory( p_dma_addr->va );
		st_dev_rmv_cont_mem_stat( pdev->p_stat, (ULONG)(p_dma_addr->sz) );
#ifndef NTDDI_WIN8
		if (cur_irql < APC_LEVEL)
			KeLowerIrql( old_irql );
#endif
    }

#endif
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



