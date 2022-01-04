#pragma once

#include "l2w_memory.h"
#include "iobuf.h"

struct ib_umem {
	struct ib_ucontext *p_uctx;
	int					page_size;
	iobuf_t				iobuf;
	int					iobuf_used;
	void *				secure_handle;
};

	
void ib_umem_release(struct ib_umem *p_ib_umem);

struct ib_umem *ib_umem_get(struct ib_ucontext *context, u64 addr,
			    size_t size, enum ib_access_flags access);

int ib_umem_page_count(struct ib_umem *p_ib_umem);

dma_addr_t ib_umem_get_dma(struct ib_umem *p_ib_umem);

int ib_umem_map(
	IN		struct ib_ucontext *context,
	IN		u64 va,
	IN		u64 size,
	IN		ib_access_t acc,
	OUT		PMDL *mdl,
	OUT		volatile u32 **kva);

void ib_umem_unmap(
	IN PMDL p_mdl,
	IN volatile u32 *kva);

