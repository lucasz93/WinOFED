
#include "l2w_precomp.h"
#include "ib_verbs.h"

#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "l2w_umem.tmh"
#endif 

/**
 * ib_umem_release - release memory pinned with ib_umem_get
 * @umem: umem struct to release
 */
void ib_umem_release(struct ib_umem *p_ib_umem)
{
	if (p_ib_umem->secure_handle) {
		__try {
			MmUnsecureVirtualMemory( p_ib_umem->secure_handle );
			p_ib_umem->secure_handle = NULL;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			NTSTATUS Status = GetExceptionCode();
			UNUSED_PARAM_WOWPP(Status);
			L2W_PRINT(TRACE_LEVEL_ERROR ,L2W ,
				"Exception 0x%x on MmUnsecureVirtualMemory(), addr %I64x, size %I64x, seg_num %d, nr_pages %d\n", 
				Status, p_ib_umem->iobuf.va, (u64)p_ib_umem->iobuf.size, 
				p_ib_umem->iobuf.seg_num, p_ib_umem->iobuf.nr_pages );
		}
	}
	if (p_ib_umem->iobuf_used)
		iobuf_deregister_with_cash(&p_ib_umem->iobuf);
	kfree(p_ib_umem);
}


/**
 * ib_umem_get - Pin and DMA map userspace memory.
 * @context: userspace context to pin memory for
 * @addr: userspace virtual address to start at
 * @size: length of region to pin
 * @access: IB_ACCESS_xxx flags for memory being pinned
 */
struct ib_umem *ib_umem_get(struct ib_ucontext *context, u64 addr,
			    size_t size, enum ib_access_flags access)
{
	int err;
	struct ib_umem *p_ib_umem;
	KPROCESSOR_MODE mode;

	// create the object
	p_ib_umem = (struct ib_umem *)kzalloc(sizeof *p_ib_umem, GFP_KERNEL);
	if (!p_ib_umem) {
		err = -ENOMEM;
		goto err;
	}

	p_ib_umem->p_uctx = context;
	p_ib_umem->page_size = PAGE_SIZE;
	
	if( context == NULL ){
		mode = KernelMode;
	} else {
		mode = context->x.mode;
	}

	// register the memory 
	iobuf_init( addr, (u64)size, &p_ib_umem->iobuf);
	err = iobuf_register_with_cash( addr, (u64)size, mode,
		&access, &p_ib_umem->iobuf );
	if (err)
		goto err_reg_mem;
	p_ib_umem->iobuf_used = TRUE;

	// TODO: map the memory for DMA
	
	// secure memory
	if ( mode == KernelMode || ((access & IB_ACCESS_NO_SECURE) != 0) ) {
        p_ib_umem->secure_handle = NULL;
		goto done;
	}

	__try {
		p_ib_umem->secure_handle = MmSecureVirtualMemory ( 
			(PVOID)(ULONG_PTR)addr, size,
			(access & IB_ACCESS_LOCAL_WRITE) ? PAGE_READWRITE : PAGE_READONLY );
		if (p_ib_umem->secure_handle == NULL) {
			err = -ENOMEM;
			goto err_secure;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		NTSTATUS Status = GetExceptionCode();
		UNUSED_PARAM_WOWPP(Status);
		L2W_PRINT(TRACE_LEVEL_ERROR ,L2W ,
			"Exception 0x%x on MmSecureVirtualMemory(), addr %I64x, size %I64x, access %#x\n", 
			Status, addr, (u64)size, access );
		err = -EACCES;
		goto err_secure;
	}
done:	
	return p_ib_umem;

err_secure:
	iobuf_deregister(&p_ib_umem->iobuf);

err_reg_mem:	
	kfree(p_ib_umem);

err:	
	p_ib_umem = (struct ib_umem *)ERR_PTR(err);

	goto done;
}

int ib_umem_page_count(struct ib_umem *p_ib_umem)
{
	return (int)p_ib_umem->iobuf.nr_pages;
}

dma_addr_t ib_umem_get_dma(struct ib_umem *p_ib_umem)
{
	u64 pages[1] = { 0 };
	iobuf_iter_t iobuf_iter;
	dma_addr_t dma_addr = { 0, 0 , 0 };

	iobuf_iter_init( &p_ib_umem->iobuf, &iobuf_iter );
	iobuf_get_tpt_seg( &p_ib_umem->iobuf, &iobuf_iter, 1, pages );
	// TODO: convert phys address to DMA one
	dma_addr.da = pages[0];

	return dma_addr;
}


// Returns: 0 on success, -ENOMEM or -EACCESS or -EFAULT on error
int ib_umem_map(
	IN		struct ib_ucontext *context,
	IN		u64 va,
	IN		u64 size,
	IN		ib_access_t acc,
	OUT		PMDL *mdl,
	OUT		volatile u32 **kva)
{
	PMDL p_mdl;
	int rc = 0;
	LOCK_OPERATION lock_op = (acc & IB_AC_LOCAL_WRITE) ? IoModifyAccess : IoReadAccess;

	if( context->x.mode == KernelMode )
	{
		*kva = (volatile u32 *)(ULONG_PTR)va;
		*mdl = NULL;
		return 0;
	}

	p_mdl = IoAllocateMdl( (PVOID)(ULONG_PTR)va, (ULONG)size, FALSE,FALSE,NULL);
	if (p_mdl == NULL) {
		rc = -ENOMEM;
		goto err_alloc_mdl;
	}

	__try { 
		MmProbeAndLockPages( p_mdl, UserMode, lock_op );   /* lock memory */
	} 
	__except (EXCEPTION_EXECUTE_HANDLER)	{
		L2W_PRINT(TRACE_LEVEL_ERROR ,L2W , 
			"MOSAL_iobuf_register: Exception 0x%x on MmProbeAndLockPages(), va %I64d, sz %I64d\n", 
			GetExceptionCode(), va, size);
		rc = -EACCES;
		goto err_probe;
	}

	*kva = (volatile u32 *)MmMapLockedPagesSpecifyCache( p_mdl, 
		KernelMode, MmCached, NULL, FALSE, NormalPagePriority );
	if (*kva == NULL) {
		L2W_PRINT(TRACE_LEVEL_ERROR ,L2W ,"MmMapLockedPagesSpecifyCache failed\n");
		rc = -EFAULT;
		goto err_map;
	}
	
	*mdl = p_mdl;
	return 0;

err_map:
	MmUnlockPages(p_mdl);
err_probe:
	IoFreeMdl(p_mdl);
err_alloc_mdl:  
	return rc;
}

void ib_umem_unmap(
	IN PMDL p_mdl,
	IN volatile u32 *kva)
{
	if (p_mdl) {
		MmUnmapLockedPages( (PVOID)kva, p_mdl );
		MmUnlockPages(p_mdl);
		IoFreeMdl(p_mdl);
	}
}


