/*
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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
 #include "hca_driver.h"
#include "mthca_dev.h"
#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mt_memory.tmh"
#endif 

#include "mt_pa_cash.h"


/*
*	Function: map user buffer to kernel and lock it
*
* 	Return: 
*/
int get_user_pages(
	IN		struct mthca_dev *dev,	/* device */
	IN		u64 start, 							/* address in user space */
	IN		int npages, 						/* size in pages */
	IN		int write_access,				/* access rights */
	OUT	struct scatterlist *sg			/* s/g list */
	)
{
	PMDL mdl_p;
	int size = npages << PAGE_SHIFT;
	int access = (write_access) ? IoWriteAccess : IoReadAccess;
	int err;
	void * kva;	/* kernel virtual address */

	UNREFERENCED_PARAMETER(dev);
	
	HCA_ENTER(HCA_DBG_MEMORY);
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	
	/* allocate MDL */
	mdl_p = IoAllocateMdl( (PVOID)(ULONG_PTR)start, (ULONG)size, 
		FALSE,
		FALSE,		/* not charge quota */
		NULL);
	if (mdl_p == NULL) {
		err = -ENOMEM;	
		goto err0;
	}

	/* lock memory */
	__try 	{ 	
		MmProbeAndLockPages( mdl_p, UserMode, 	access ); 
	} 
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		NTSTATUS Status = GetExceptionCode();
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY ,("Exception 0x%x on MmProbeAndLockPages(), addr 0x%I64x, size %d\n", Status, start, size));
		switch(Status){
			case STATUS_WORKING_SET_QUOTA:
				err = -ENOMEM;break;
			case STATUS_ACCESS_VIOLATION:
				err = -EACCES;break;
			default :
				err = -EINVAL;
			}

		goto err1;
	}

	/* map it to kernel */
	kva = MmMapLockedPagesSpecifyCache( mdl_p, 
		KernelMode, MmNonCached, 
		NULL, FALSE, NormalPagePriority );
	if (kva == NULL) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY ,("MmMapLockedPagesSpecifyCache failed\n"));
		err = -EFAULT;
		goto err2;
	}

	sg->page = kva;
	sg->length = size;
	sg->offset = (unsigned int)(start & ~PAGE_MASK);
	sg->p_mdl = mdl_p;	
	sg->dma_address = MmGetPhysicalAddress(kva).QuadPart;
	return 0;	
	
err2:	
	MmUnlockPages(mdl_p);
err1:		
    IoFreeMdl(mdl_p);
err0:
	HCA_EXIT(HCA_DBG_MEMORY);
	return err;
		
 }

void put_page(struct scatterlist *sg)
{
	if (sg->p_mdl) {
		MmUnmapLockedPages( sg->page, sg->p_mdl );
		MmUnlockPages(sg->p_mdl);
		IoFreeMdl(sg->p_mdl);
	}
}

VOID
  AdapterListControl(
    IN PDEVICE_OBJECT  DeviceObject,
    IN PIRP  Irp,
    IN PSCATTER_GATHER_LIST  ScatterGather,
    IN PVOID  Context
    )
{
	struct scatterlist *p_sg = (struct scatterlist *)Context;

	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	// sanity checks
	if (!ScatterGather || !Context) {
		HCA_PRINT(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("AdapterListControl failed: invalid parameters\n"));
		return;
	}
	if (ScatterGather->NumberOfElements > 1) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_LOW ,("AdapterListControl failed: unexpected sg size; %d elements \n",
			ScatterGather->NumberOfElements ));
	}
	if (ScatterGather->Elements[0].Length != p_sg->length) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_LOW ,("AdapterListControl failed: unexpected buffer size %#x (expected %#x) \n",
			ScatterGather->Elements[0].Length, p_sg->length ));
	}

	// results	
	p_sg->dma_address = ScatterGather->Elements[0].Address.QuadPart;	// get logical address
	p_sg->p_os_sg = ScatterGather;		// store sg list address for releasing
	//NB: we do not flush the buffers by FlushAdapterBuffers(), because we don't really transfer data
}

/* Returns: the number of mapped sg elements */
int pci_map_sg(struct mthca_dev *dev, 
	struct scatterlist *sg, 	int nents, int direction)
{
#ifndef USE_GET_SG_LIST

	UNREFERENCED_PARAMETER(dev);
	UNREFERENCED_PARAMETER(sg);
	UNREFERENCED_PARAMETER(direction);

	// mapping was performed in alloc_dma_mem
	return nents;

#else

	int i;
	NTSTATUS status;
	hca_dev_ext_t *p_ext = dev->ext;
	struct scatterlist *p_sg = sg;
	KIRQL irql = KeRaiseIrqlToDpcLevel();

	for (i=0; i<nents; ++i, ++p_sg) {
		status =	p_ext->p_dma_adapter->DmaOperations->GetScatterGatherList( 
			p_ext->p_dma_adapter, p_ext->cl_ext.p_self_do, p_sg->p_mdl, p_sg->page, 
			p_sg->length, AdapterListControl, sg, (BOOLEAN)direction );
		if (!NT_SUCCESS(status)) {
			HCA_PRINT(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("GetScatterGatherList failed %#x\n", status)));
			break;
		}
	}
	KeLowerIrql(irql);
	return i; /* i.e., we mapped all the entries */

#endif	
}

/* Returns: the number of unmapped sg elements */
int pci_unmap_sg(struct mthca_dev *dev, 
	struct scatterlist *sg, 	int nents, int direction)
{
#ifndef USE_GET_SG_LIST
	
		UNREFERENCED_PARAMETER(dev);
		UNREFERENCED_PARAMETER(sg);
		UNREFERENCED_PARAMETER(direction);
		// mapping was performed in alloc_dma_mem
		return nents;
	
#else

	int i;
	hca_dev_ext_t *p_ext = dev->ext;
	struct scatterlist *p_sg = sg;
	KIRQL irql = KeRaiseIrqlToDpcLevel();
	void *p_os_sg = p_sg->p_os_sg;

	for (i=0; i<nents; ++i, ++p_sg) {
		if (p_os_sg)
			p_sg->p_os_sg = NULL;
			p_ext->p_dma_adapter->DmaOperations->PutScatterGatherList( 
				p_ext->p_dma_adapter, p_os_sg, (BOOLEAN)direction );
	}
	KeLowerIrql(irql);
	return i; /* i.e., we mapped all the entries */

#endif	
}

/* The function zeroes 'struct scatterlist' and then fills it with values.
 On error 'struct scatterlist' is returned zeroed */
void *alloc_dma_mem(
	IN		struct mthca_dev *dev, 
	IN		unsigned long size,
	OUT	struct scatterlist *p_sg)
{
	void *va;
	DMA_ADAPTER *p_dma = dev->ext->p_dma_adapter;

#ifndef USE_GET_SG_LIST

	PHYSICAL_ADDRESS  pa = {0};
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	RtlZeroMemory(p_sg,sizeof *p_sg);
	if (!size)
		return NULL;

	va  = p_dma->DmaOperations->AllocateCommonBuffer(
		p_dma, size, &pa, FALSE );
	if (va) {
		p_sg->length	= size;
		p_sg->dma_address = pa.QuadPart;
		p_sg->page = va;
	}

#else

	int err;
	PHYSICAL_ADDRESS la = {0}, ba = {0}, ha = {(u64)(-1I64)};
	PMDL p_mdl;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	RtlZeroMemory(p_sg,sizeof *p_sg);
	if (!size)
		return NULL;

	// allocate memory
	va = MmAllocateContiguousMemorySpecifyCache(
		size, la, ha, ba, MmNonCached );
	if (!va) {
		HCA_PRINT(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("MmAllocateContiguousMemorySpecifyCache failed on %#x size\n", size )));
		goto err_alloc;
	}

	// allocate MDL	
	p_mdl = IoAllocateMdl( va, size, FALSE, FALSE, NULL );
	if (!p_mdl) {
		HCA_PRINT(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("MmAllocateContiguousMemorySpecifyCache failed on %#x size\n", size )));
		goto err_mdl;
	}
	MmBuildMdlForNonPagedPool( p_mdl );

	p_sg->p_mdl = p_mdl;
	p_sg->length	= size;
	p_sg->page = va;

	goto end;

err_mdl:
	MmFreeContiguousMemory(va);
	va = NULL;
err_alloc:
end:

#endif

	return va;
}

void free_dma_mem(
	IN		struct mthca_dev *dev, 
	IN		struct scatterlist *p_sg)
{
#ifndef USE_GET_SG_LIST

	PHYSICAL_ADDRESS  pa;
	DMA_ADAPTER *p_dma = dev->ext->p_dma_adapter;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	if (p_sg->length) {
		pa.QuadPart = p_sg->dma_address;
		p_dma->DmaOperations->FreeCommonBuffer( 
			p_dma, p_sg->length, pa, 
			p_sg->page, FALSE );
	}

#else

	PMDL p_mdl = p_sg->p_mdl;
	PVOID page = p_sg->page;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	if (p_mdl) {
		p_sg->p_mdl = NULL;
		IoFreeMdl( p_mdl );
	}
	if (page) {
		p_sg->page = NULL;
		MmFreeContiguousMemory(page);	
	}

#endif
}


typedef struct _mt_iobuf_seg {
	LIST_ENTRY	link;
	PMDL   mdl_p;
	u64 va;  /* virtual address of the buffer */
	u64 size;     /* size in bytes of the buffer */
	u32 nr_pages;
	int	is_user;
} mt_iobuf_seg_t;

// Returns: 0 on success, -ENOMEM or -EACCESS on error
static int register_segment(
	IN		u64 va,
	IN		u64 size,
	IN		int is_user,
	IN		ib_access_t acc,
	OUT mt_iobuf_seg_t **iobuf_seg)
{
	PMDL mdl_p;
	int rc;
	KPROCESSOR_MODE mode;  
	mt_iobuf_seg_t * new_iobuf;
	static ULONG cnt=0;
	LOCK_OPERATION Operation;

	// set Operation
	if (acc & IB_AC_LOCAL_WRITE)
		Operation = IoModifyAccess;
	else
		Operation = IoReadAccess;
	
	// allocate IOBUF segment object
	new_iobuf = (mt_iobuf_seg_t *)kmalloc(sizeof(mt_iobuf_seg_t), GFP_KERNEL );
	if (new_iobuf == NULL) {
		rc = -ENOMEM;
		goto err_nomem;
	}

	// allocate MDL 
	mdl_p = IoAllocateMdl( (PVOID)(ULONG_PTR)va, (ULONG)size, FALSE,FALSE,NULL);
	if (mdl_p == NULL) {
		rc = -ENOMEM;
		goto err_alloc_mdl;
	}

	// make context-dependent things
	if (is_user) {
		ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
		mode = UserMode;
	}
	else {  /* Mapping to kernel virtual address */
		//    MmBuildMdlForNonPagedPool(mdl_p);   // fill MDL ??? - should we do that really ?
		mode = KernelMode;
	}

	__try { /* try */
		MmProbeAndLockPages( mdl_p, mode, Operation );   /* lock memory */
	} /* try */
		
	__except (EXCEPTION_EXECUTE_HANDLER)	{
		HCA_PRINT(TRACE_LEVEL_ERROR, HCA_DBG_MEMORY, 
			("MOSAL_iobuf_register: Exception 0x%x on MmProbeAndLockPages(), va %I64d, sz %I64d\n", 
			GetExceptionCode(), va, size));
		rc = -EACCES;
		goto err_probe;
	}
	
	// fill IOBUF object
	new_iobuf->va = va;
	new_iobuf->size= size;
	new_iobuf->nr_pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES( va, size );
	new_iobuf->mdl_p = mdl_p;
	new_iobuf->is_user = is_user;
	*iobuf_seg = new_iobuf;
	return 0;

err_probe:
	IoFreeMdl(mdl_p);
err_alloc_mdl:  
	ExFreePool((PVOID)new_iobuf);
err_nomem:  
	return rc;
}

void iobuf_init(
	IN		u64 va,
	IN		u64 size,
	IN		int is_user,
	IN OUT	mt_iobuf_t *iobuf_p)
{
	iobuf_p->va = va;
	iobuf_p->size= size;
	iobuf_p->is_user = is_user;
	InitializeListHead( &iobuf_p->seg_que );
	iobuf_p->seg_num = 0;
	iobuf_p->nr_pages = 0;
	iobuf_p->is_cashed = 0;
}

int iobuf_register(
	IN		u64 va,
	IN		u64 size,
	IN		int is_user,
	IN		ib_access_t acc,
	IN OUT	mt_iobuf_t *iobuf_p)
{
	int rc=0;
	u64 seg_va;	// current segment start
	u64 seg_size;	// current segment size
	u64 rdc;			// remain data counter - what is rest to lock
	u64 delta;				// he size of the last not full page of the first segment
	mt_iobuf_seg_t * new_iobuf;
	unsigned page_size = PAGE_SIZE;

// 32 - for any case  
#define PFNS_IN_PAGE_SIZE_MDL  	((PAGE_SIZE - sizeof(struct _MDL) - 32) / sizeof(long))
#define MIN_IOBUF_SEGMENT_SIZE	(PAGE_SIZE * PFNS_IN_PAGE_SIZE_MDL)	// 4MB	

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	// we'll try to register all at once.
	seg_va = va;
	seg_size = rdc = size;
		
	// allocate segments
	while (rdc > 0) {
		// map a segment
		rc = register_segment(seg_va, seg_size, is_user, acc, &new_iobuf );

		// success - move to another segment
		if (!rc) {
			rdc -= seg_size;
			seg_va += seg_size;
			InsertTailList( &iobuf_p->seg_que, &new_iobuf->link );
			iobuf_p->seg_num++;
			// round the segment size to the next page boundary 
			delta = (seg_va + seg_size) & (page_size - 1);
			if (delta) {
				seg_size -= delta;
				seg_size += page_size;
			}
			if (seg_size > rdc)
				seg_size = rdc;
			continue;
		}

		// failure - too large a buffer: lessen it and try once more
		if (rc == -ENOMEM) {
			// no where to lessen - too low memory
			if (seg_size <= MIN_IOBUF_SEGMENT_SIZE)
				break;
			// lessen the size
			seg_size >>= 1;
			// round the segment size to the next page boundary 
			delta = (seg_va + seg_size) & (page_size - 1);
			if (delta) {
				seg_size -= delta;
				seg_size += page_size;
			}
			if (seg_size > rdc)
				seg_size = rdc;
			continue;
		}

		// got unrecoverable error
		break;
	}

	// SUCCESS
	if (rc) 
		iobuf_deregister( iobuf_p );
	else	 
		iobuf_p->nr_pages += ADDRESS_AND_SIZE_TO_SPAN_PAGES( va, size );

	return rc;
}


static void __iobuf_copy(
	IN OUT	mt_iobuf_t *dst_iobuf_p,
	IN 		mt_iobuf_t *src_iobuf_p
	)
{
	int i;
	mt_iobuf_seg_t *iobuf_seg_p;
	
	*dst_iobuf_p = *src_iobuf_p;
	InitializeListHead( &dst_iobuf_p->seg_que );
	for (i=0; i<src_iobuf_p->seg_num; ++i) {
		iobuf_seg_p = (mt_iobuf_seg_t *)(PVOID)RemoveHeadList( &src_iobuf_p->seg_que );
		InsertTailList( &dst_iobuf_p->seg_que, &iobuf_seg_p->link );
	}
}

/* if the buffer to be registered overlaps a buffer, already registered, 
	a race can happen between HCA, writing to the previously registered
	buffer and the probing functions (MmProbeAndLockPages, MmSecureVirtualMemory),
	used in the algorithm of memory registration.
	To prevent the race we maintain reference counters for the physical pages, being registered, 
	and register every physical page FOR THE WRITE ACCESS only once.*/

int iobuf_register_with_cash(
	IN		u64 vaddr,
	IN		u64 size,
	IN		int is_user,
	IN OUT	ib_access_t *acc_p,
	IN OUT	mt_iobuf_t *iobuf_p)
{
	int rc, pa_in;
	mt_iobuf_t sec_iobuf;
	int i, page_in , page_out, page_in_total;
	int nr_pages;
	char *subregion_start, *va;
	u64 subregion_size;
	u64 rdc;					// remain data counter - what is rest to lock
	u64 delta;				// he size of the last not full page of the first segment
	ib_access_t acc;

	down(&g_pa_mutex);

	// register memory for read access to bring pages into the memory
	rc = iobuf_register( vaddr, size, is_user, 0, iobuf_p);

	// on error or read access - exit
	if (rc || !(*acc_p & IB_AC_LOCAL_WRITE))
		goto exit;

	// re-register buffer with the correct access rights
	iobuf_init( (u64)vaddr, size, is_user, &sec_iobuf );
	nr_pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES( vaddr, size );
	subregion_start = va = (char*)(ULONG_PTR)vaddr;
	rdc = size;
	pa_in = page_in = page_in_total = page_out = 0;

	for (i=0; i<nr_pages; ++i, va+=PAGE_SIZE) {
		// check whether a phys page is to be registered
		PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(va);
		pa_in = pa_is_registered(pa.QuadPart);
		if (pa_in) {
			++page_in;
			++page_in_total;
		}
		else
			++page_out;

		// check whether we get at the end of a subregion with the same rights wrt cash
		if (page_in && page_out) {
			// prepare to registration of the subregion
			if (pa_in) { 		// SUBREGION WITH WRITE ACCESS
				acc = IB_AC_LOCAL_WRITE;
				subregion_size = (u64)page_out * PAGE_SIZE;
				page_out = 0;
			}
			else {		// SUBREGION WITH READ ACCESS
				acc = 0;
				subregion_size = (u64)page_in * PAGE_SIZE;
				page_in = 0;
			}
			
			// round the subregion size to the page boundary 
			delta = (ULONG_PTR)(subregion_start + subregion_size) & (PAGE_SIZE - 1);
			subregion_size -= delta;
			if (subregion_size > rdc)
				subregion_size = rdc;

			// register the subregion
			rc = iobuf_register( (ULONG_PTR)subregion_start, subregion_size, is_user, acc, &sec_iobuf);
			if (rc)
				goto cleanup;

			// prepare to the next loop
			rdc -= subregion_size;
			subregion_start +=subregion_size;
		}
	}

	// prepare to registration of the subregion
	if (pa_in) {		// SUBREGION WITH READ ACCESS
		acc = 0;
		subregion_size = (u64)page_in * PAGE_SIZE;
	}
	else {		// SUBREGION WITH WRITE ACCESS
		acc = IB_AC_LOCAL_WRITE;
		subregion_size = (u64)page_out * PAGE_SIZE;
	}
	
	// round the subregion size to the page boundary 
	delta = (ULONG_PTR)(subregion_start + subregion_size) & (PAGE_SIZE - 1);
	subregion_size -= delta;
	if (subregion_size > rdc)
		subregion_size = rdc;
	
	// register the subregion
	rc = iobuf_register( (ULONG_PTR)subregion_start, subregion_size, is_user, acc, &sec_iobuf);
	if (rc)
		goto cleanup;

	// cash phys pages
	rc = pa_register(iobuf_p);
	if (rc)
		goto err_pa_reg;

	// replace the iobuf
	iobuf_deregister( iobuf_p );
	sec_iobuf.is_cashed = TRUE;
	__iobuf_copy( iobuf_p, &sec_iobuf );
	
	// buffer is a part of also registered buffer - change the rights 
	if (page_in_total)
		*acc_p = MTHCA_ACCESS_REMOTE_READ;

	goto exit;
	
err_pa_reg:	
	iobuf_deregister( &sec_iobuf );
cleanup:
	iobuf_deregister( iobuf_p );
exit:	
	up(&g_pa_mutex);
	return rc;
}

static void deregister_segment(mt_iobuf_seg_t * iobuf_seg_p)
{
	MmUnlockPages( iobuf_seg_p->mdl_p );    // unlock the buffer 
	IoFreeMdl( iobuf_seg_p->mdl_p );        // free MDL
	ExFreePool(iobuf_seg_p);
}

void iobuf_deregister(mt_iobuf_t *iobuf_p)
{
	mt_iobuf_seg_t *iobuf_seg_p; 	// pointer to current segment object

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	// release segments
	while (!IsListEmpty( &iobuf_p->seg_que )) {
		iobuf_seg_p = (mt_iobuf_seg_t *)(PVOID)RemoveTailList( &iobuf_p->seg_que );
		deregister_segment(iobuf_seg_p);
		iobuf_p->seg_num--;
	}
	ASSERT(iobuf_p->seg_num == 0);
}

void iobuf_deregister_with_cash(mt_iobuf_t *iobuf_p)
{
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	down(&g_pa_mutex);
	if (iobuf_p->is_cashed)
		pa_deregister(iobuf_p);
	 iobuf_deregister(iobuf_p);
	up(&g_pa_mutex);
}

void iobuf_iter_init(
	IN 		mt_iobuf_t *iobuf_p, 
	IN OUT	mt_iobuf_iter_t *iterator_p)
{
	iterator_p->seg_p = iobuf_p->seg_que.Flink;
	iterator_p->pfn_ix = 0;
}

// the function returns phys addresses of the pages, also for the first page
// if one wants to get the phys address of the buffer, one has to 
// add the offset from the start of the page to the first phys address
// Returns: the number of entries, filled in page_tbl_p
// Returns 0  while at the end of list.
uint32_t iobuf_get_tpt_seg(
	IN		mt_iobuf_t *iobuf_p, 
	IN OUT	mt_iobuf_iter_t *iterator_p,
	IN		uint32_t n_pages_in, 
	IN OUT	uint64_t *page_tbl_p )
{
	uint32_t i=0;	// has to be initialized here for a premature exit
	mt_iobuf_seg_t *seg_p; 	// pointer to current segment object 
	PPFN_NUMBER	pfn_p; 
	uint32_t	pfn_ix; // index of PFN in PFN array of the current segment
	uint64_t *pa_buf_p = page_tbl_p;

	// prepare to the loop
	seg_p = iterator_p->seg_p; 	// first segment of the first iobuf
	pfn_ix= iterator_p->pfn_ix;

	// check, whether we at the end of the list
	if ((PVOID)seg_p == (PVOID)&iobuf_p->seg_que)
		goto exit;
	pfn_p = MmGetMdlPfnArray( seg_p->mdl_p ) + pfn_ix;

	// pass along all the PFN arrays
	for (; i < n_pages_in; i++, pa_buf_p++) {
		// convert PFN to the physical address
		*pa_buf_p = (uint64_t)*pfn_p++ << PAGE_SHIFT;
	
		// get to the next PFN 
		if (++pfn_ix >= seg_p->nr_pages) {
			seg_p = (mt_iobuf_seg_t*)seg_p->link.Flink;
			pfn_ix = 0;
			if ((PVOID)seg_p == (PVOID)&iobuf_p->seg_que) {
				i++;
				break;
			}
			pfn_p = MmGetMdlPfnArray( seg_p->mdl_p );
		}
	}

exit:
	iterator_p->seg_p = seg_p;
	iterator_p->pfn_ix = pfn_ix;
	return i;
}




