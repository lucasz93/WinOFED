#ifndef MT_PCIPOOL_H
#define MT_PCIPOOL_H

typedef struct pci_pool {
	size_t								size;
	struct mthca_dev   		*mdev;
	char									name [32];
	NPAGED_LOOKASIDE_LIST  pool_hdr;
} pci_pool_t;
	
// taken from dmapool.c

/**
* pci_pool_create - Creates a pool of consistent memory blocks, for dma.
* @name: name of pool, for diagnostics
* @mdev: device that will be doing the DMA
* @size: size of the blocks in this pool.
* @align: alignment requirement for blocks; must be a power of two
* @allocation: returned blocks won't cross this boundary (or zero)
* Context: !in_interrupt()
*
* Returns a dma allocation pool with the requested characteristics, or
* null if one can't be created.  Given one of these pools, dma_pool_alloc()
* may be used to allocate memory.  Such memory will all have "consistent"
* DMA mappings, accessible by the device and its driver without using
* cache flushing primitives.  The actual size of blocks allocated may be
* larger than requested because of alignment.
*
* If allocation is nonzero, objects returned from dma_pool_alloc() won't
 * cross that size boundary.  This is useful for devices which have
 * addressing restrictions on individual DMA transfers, such as not crossing
 * boundaries of 4KBytes.
 */
 
pci_pool_t *
pci_pool_create (const char *name, struct mthca_dev *mdev,
        size_t size, size_t align, size_t allocation);

/**
 * dma_pool_alloc - get a block of consistent memory
 * @pool: dma pool that will produce the block
 * @mem_flags: GFP_* bitmask
 * @handle: pointer to dma address of block
 *
 * This returns the kernel virtual address of a currently unused block,
 * and reports its dma address through the handle.
 * If such a memory block can't be allocated, null is returned.
 */
static inline void * 
pci_pool_alloc (pci_pool_t *pool, int mem_flags, dma_addr_t *handle)
{
	PHYSICAL_ADDRESS pa;
	void * ptr;
	UNREFERENCED_PARAMETER(mem_flags);

	MT_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

	ptr = ExAllocateFromNPagedLookasideList( &pool->pool_hdr );
	if (ptr != NULL) {
		pa = MmGetPhysicalAddress( ptr );
		*handle = pa.QuadPart;
	}
	return ptr; 
}
	
	
/**
* dma_pool_free - put block back into dma pool
* @pool: the dma pool holding the block
* @vaddr: virtual address of block
* @dma: dma address of block
*
* Caller promises neither device nor driver will again touch this block
* unless it is first re-allocated.
*/
static inline 	void
pci_pool_free (pci_pool_t *pool, void *vaddr, dma_addr_t dma)
{
	UNREFERENCED_PARAMETER(dma);
	MT_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	ExFreeToNPagedLookasideList( &pool->pool_hdr, vaddr );
}
	
	

/**
 * pci_pool_destroy - destroys a pool of dma memory blocks.
 * @pool: dma pool that will be destroyed
 * Context: !in_interrupt()
 *
 * Caller guarantees that no more memory from the pool is in use,
 * and that nothing will try to use the pool after this call.
 */
static inline 	void
pci_pool_destroy (pci_pool_t *pool)
{
	ExDeleteNPagedLookasideList( &pool->pool_hdr );
	ExFreePool( pool);
}



#endif
