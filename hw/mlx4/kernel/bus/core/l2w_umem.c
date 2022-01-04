#include <mlx4_debug.h>
#include "l2w.h"
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
	MLX4_ENTER(MLX4_DBG_MEMORY);
	if (p_ib_umem->secure_handle) {
		__try {
			MmUnsecureVirtualMemory( p_ib_umem->secure_handle );
			p_ib_umem->secure_handle = NULL;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			NTSTATUS Status = GetExceptionCode();
			UNUSED_PARAM_WOWPP(Status);
			MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_MEMORY ,
				("Exception 0x%x on MmUnsecureVirtualMemory(), addr %I64x, size %I64x, seg_num %d, nr_pages %d\n", 
				Status, p_ib_umem->iobuf.va, (u64)p_ib_umem->iobuf.size, 
				p_ib_umem->iobuf.seg_num, p_ib_umem->iobuf.nr_pages ));
		}
	}
	if (p_ib_umem->iobuf_used)
		iobuf_deregister_with_cash(&p_ib_umem->iobuf);
	kfree(p_ib_umem);
	MLX4_EXIT(MLX4_DBG_MEMORY);
}


/**
 * ib_umem_get - Pin and DMA map userspace memory.
 * @context: userspace context to pin memory for
 * @addr: userspace virtual address to start at
 * @size: length of region to pin
 * @access: IB_ACCESS_xxx flags for memory being pinned
 */
struct ib_umem *ib_umem_get(struct ib_ucontext *context, u64 addr,
			    size_t size, enum ib_access_flags access, boolean_t secure)
{
	int err;
	struct ib_umem *p_ib_umem;

	MLX4_ENTER(MLX4_DBG_MEMORY);

	// create the object
	p_ib_umem = kzalloc(sizeof *p_ib_umem, GFP_KERNEL);
	if (!p_ib_umem)
		goto err_nomem;

	p_ib_umem->p_uctx = context;
	p_ib_umem->page_size = PAGE_SIZE;
	
	// register the memory 
	iobuf_init( addr, (u64)size, !!context, &p_ib_umem->iobuf);
	err =  iobuf_register_with_cash( addr, (u64)size, !!context, 
		&access, &p_ib_umem->iobuf );
	if (err)
		goto err_reg_mem;
	p_ib_umem->iobuf_used = TRUE;

	// TODO: map the memory for DMA
	
	// secure memory
	if (!context || !secure)
		goto done;
	__try {
		p_ib_umem->secure_handle = MmSecureVirtualMemory ( 
			(PVOID)(ULONG_PTR)addr, size,
			(access & IB_ACCESS_LOCAL_WRITE) ? PAGE_READWRITE : PAGE_READONLY );
		if (p_ib_umem->secure_handle == NULL) 
			goto err_secure;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		NTSTATUS Status = GetExceptionCode();
		UNUSED_PARAM_WOWPP(Status);
		MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_MEMORY ,
			("Exception 0x%x on MmSecureVirtualMemory(), addr %I64x, size %I64x, access %#x\n", 
			Status, addr, (u64)size, access ));
		goto err_secure;
	}
	goto done;

err_secure:
	iobuf_deregister(&p_ib_umem->iobuf);

err_reg_mem:	
	kfree(p_ib_umem);

err_nomem:	
	p_ib_umem = ERR_PTR(-ENOMEM);

done:	
	MLX4_EXIT(MLX4_DBG_MEMORY);
	return p_ib_umem;
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
	IN		u64 va,
	IN		u64 size,
	IN		ib_access_t acc,
	OUT		PMDL *mdl,
	OUT		void **kva)
{
	PMDL p_mdl;
	int rc = 0;
	LOCK_OPERATION lock_op = (acc & IB_AC_LOCAL_WRITE) ? IoModifyAccess : IoReadAccess;

	p_mdl = IoAllocateMdl( (PVOID)(ULONG_PTR)va, (ULONG)size, FALSE,FALSE,NULL);
	if (p_mdl == NULL) {
		rc = -ENOMEM;
		goto err_alloc_mdl;
	}

	__try { 
		MmProbeAndLockPages( p_mdl, UserMode, lock_op );   /* lock memory */
	} 
	__except (EXCEPTION_EXECUTE_HANDLER)	{
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_MEMORY, 
			("MOSAL_iobuf_register: Exception 0x%x on MmProbeAndLockPages(), va %I64d, sz %I64d\n", 
			GetExceptionCode(), va, size));
		rc = -EACCES;
		goto err_probe;
	}

	*kva = MmMapLockedPagesSpecifyCache( p_mdl, 
		KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority );
	if (*kva == NULL) {
		MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_MEMORY ,("MmMapLockedPagesSpecifyCache failed\n"));
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
	IN void *kva)
{
	if (kva) {
		MmUnmapLockedPages( kva, p_mdl );
		MmUnlockPages(p_mdl);
		IoFreeMdl(p_mdl);
	}
}


