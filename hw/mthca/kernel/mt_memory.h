#ifndef MT_MEMORY_H
#define MT_MEMORY_H

#include "iba/ib_types.h"
#include "complib\cl_debug.h"

// ===========================================
// CONSTANTS
// ===========================================

#define MT_TAG_ATOMIC		'MOTA'
#define MT_TAG_KERNEL		'LNRK'
#define MT_TAG_HIGH			'HGIH'
#define MT_TAG_PCIPOOL		'PICP'
#define MT_TAG_IOMAP			'PAMI'

// ===========================================
// SUBSTITUTIONS
// ===========================================

#define memcpy_toio		memcpy
#define dma_get_cache_alignment		(int)KeGetRecommendedSharedDataAlignment

// ===========================================
// MACROS
// ===========================================

#define PAGE_MASK       			(~(PAGE_SIZE-1))
#define NEXT_PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)


// ===========================================
// SYSTEM MEMORY
// ===========================================

// memory
#define __GFP_NOWARN		0	/* Suppress page allocation failure warning */
#define __GFP_HIGHMEM	0

#define GFP_ATOMIC			1		/* can't wait (i.e. DPC or higher) */
#define GFP_KERNEL			2		/* can wait (npaged) */
#define GFP_HIGHUSER 		4		/* GFP_KERNEL, that can be in HIGH memory */


#define SLAB_ATOMIC		GFP_ATOMIC
#define SLAB_KERNEL		GFP_KERNEL

#if 1
static inline void * kmalloc( SIZE_T bsize, unsigned int gfp_mask)
{
	void *ptr;
	MT_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	MT_ASSERT(bsize);
	if(bsize == 0) {
		return NULL;
	}
	switch (gfp_mask) {
		case GFP_ATOMIC:
			ptr = ExAllocatePoolWithTag( NonPagedPool, bsize, MT_TAG_ATOMIC );
			break;
		case GFP_KERNEL:
			ptr = ExAllocatePoolWithTag( NonPagedPool, bsize, MT_TAG_KERNEL );
			break;
		case GFP_HIGHUSER:
			ptr = ExAllocatePoolWithTag( NonPagedPool, bsize, MT_TAG_HIGH );
			break;
		default:
			cl_dbg_out("kmalloc: unsupported flag %d\n", gfp_mask);
			ptr = NULL;
			break;
	}
	return ptr;
}
#else
#define kmalloc(bsize,flags)	ExAllocatePoolWithTag( NonPagedPool, bsize, MT_TAG_KERNEL ) 
#endif

static inline void * kzalloc( SIZE_T bsize, unsigned int gfp_mask)
{
	void* va = kmalloc(bsize, gfp_mask);
	if (va)
		RtlZeroMemory(va, bsize);
	return va;
}

static inline void kfree (const void *pobj)
{
	MT_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	if (pobj)
		ExFreePool((void *)pobj);
}

#define get_zeroed_page(mask)				kzalloc(PAGE_SIZE, mask)
#define free_page(ptr) 								kfree(ptr)


// ===========================================
// IO SPACE <==> SYSTEM MEMORY
// ===========================================


/**
* ioremap     -   map bus memory into CPU space
* @offset:    bus address of the memory
* @size:      size of the resource to map
*
* ioremap performs a platform specific sequence of operations to
* make bus memory CPU accessible via the readb/readw/readl/writeb/
* writew/writel functions and the other mmio helpers. The returned
* address is not guaranteed to be usable directly as a virtual
* address. 
*/
static inline 	void *ioremap(io_addr_t addr, SIZE_T size, SIZE_T* psize)
{
	PHYSICAL_ADDRESS pa;
	void *va;
	
	MT_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	pa.QuadPart = addr;
	va = MmMapIoSpace( pa, size, MmNonCached ); 
	*psize = size;
	return va;
}

static inline void iounmap(void *va, SIZE_T size)
{
	MmUnmapIoSpace( va, size);
}

 // ===========================================
 // DMA SUPPORT
 // ===========================================

#define PCI_DMA_BIDIRECTIONAL   0
#define PCI_DMA_TODEVICE 			 1
#define PCI_DMA_FROMDEVICE 		 2
#define DMA_TO_DEVICE					PCI_DMA_TODEVICE

 struct scatterlist {
		dma_addr_t		dma_address;	/* logical (device) address */
		void * 				page;				/* kernel virtual address */
		PMDL				p_mdl;					/* MDL, if any (used for user space buffers) */
		PSCATTER_GATHER_LIST p_os_sg;	/* adapter scatter-gather list */
		unsigned int		offset;				/* offset in the first page */
		unsigned int		length;				/* buffer length */
	};

 #define sg_dma_address(sg)     ((sg)->dma_address)
 #define sg_dma_len(sg)         		((sg)->length)

 struct mthca_dev;

 int pci_map_sg(struct mthca_dev *dev, 
		struct scatterlist *sg, 	int nents, int direction);
 
 int pci_unmap_sg(struct mthca_dev *dev, 
		struct scatterlist *sg, 	int nents, int direction);

 void free_dma_mem(
		IN		struct mthca_dev *dev, 
		IN		struct scatterlist *p_sg);
 
 void *alloc_dma_mem(
	 IN 	 struct mthca_dev *dev, 
	 IN 	 unsigned long size,
	 OUT struct scatterlist *p_sg);

static inline void *alloc_dma_zmem(
	 IN 	 struct mthca_dev *dev, 
	 IN 	 unsigned long size,
	 OUT struct scatterlist *p_sg)
{
	void *va = alloc_dma_mem( dev, size, p_sg );
	if (va)
		RtlZeroMemory(va, size);
	return va;
}

static inline void *alloc_dma_zmem_map(
	IN		struct mthca_dev *dev, 
	IN		unsigned long size,
	IN		int direction,
	OUT struct scatterlist *p_sg)
{
	void *va = alloc_dma_zmem( dev, size, p_sg );
	if (va) {
	 	if (!pci_map_sg( dev, p_sg, 1, direction )) {
	 		free_dma_mem( dev, p_sg );
	 		va = NULL;
 		}
	}
 	return va;
}
	 
static inline void free_dma_mem_map(
	 IN 	 struct mthca_dev *dev, 
	 IN 	 struct scatterlist *p_sg,
	 IN    int direction )
{
	pci_unmap_sg( dev, p_sg, 1,  direction );
	free_dma_mem( dev, p_sg );
}

 static inline dma_addr_t pci_mape_page(struct mthca_dev *dev, 
	 void *va,	unsigned long offset,  SIZE_T size, int direction)
 {
	 UNREFERENCED_PARAMETER(dev);
	 UNREFERENCED_PARAMETER(va);
	 UNREFERENCED_PARAMETER(offset);
	 UNREFERENCED_PARAMETER(size);
	 UNREFERENCED_PARAMETER(direction);
	 /* suppose, that pages where always translated to DMA space */
	 return 0; /* i.e., we unmapped all the entries */ 
 }

 // ===========================================
 // HELPERS
 // ===========================================
 
 static inline int get_order(unsigned long size)
{
        int order;

        size = (size-1) >> (PAGE_SHIFT-1);
        order = -1;
        do {
                size >>= 1;
                order++;
        } while (size);
        return order;
}

static inline int long_log2(unsigned long x)
{
        int r = 0;
        for (x >>= 1; x > 0; x >>= 1)
                r++;
        return r;
}

static inline unsigned long roundup_pow_of_two(unsigned long x)
{
        return (1UL << fls(x - 1));
}

// ===========================================
// PROTOTYPES
// ===========================================

void put_page(struct scatterlist *sg);
int get_user_pages(
	IN		struct mthca_dev *dev,	/* device */
	IN		u64 start, 							/* address in user space */
	IN		int npages, 						/* size in pages */
	IN		int write_access,				/* access rights */
	OUT	struct scatterlist *sg			/* s/g list */
	);

typedef struct _mt_iobuf {
	u64 va;  /* virtual address of the buffer */
	u64 size;     /* size in bytes of the buffer */
	LIST_ENTRY seg_que;
	u32 nr_pages;
	int is_user;
	int seg_num;
	int is_cashed;
} mt_iobuf_t;

/* iterator for getting segments of tpt */
typedef struct _mt_iobuf_iter {
	void * seg_p;  /* the item from where to take the next translations */
	unsigned int pfn_ix; /* index from where to take the next translation */
} mt_iobuf_iter_t;

void iobuf_deregister_with_cash(IN mt_iobuf_t *iobuf_p);

void iobuf_deregister(IN mt_iobuf_t *iobuf_p);

void iobuf_init(
	IN		u64 va,
	IN		u64 size,
	IN		int is_user,
	IN OUT	mt_iobuf_t *iobuf_p);

int iobuf_register_with_cash(
	IN		u64 vaddr,
	IN		u64 size,
	IN		int is_user,
	IN OUT	ib_access_t *acc_p,
	IN OUT	mt_iobuf_t *iobuf_p);

int iobuf_register(
	IN		u64 va,
	IN		u64 size,
	IN		int is_user,
	IN		ib_access_t acc,
	IN OUT	mt_iobuf_t *iobuf_p);

void iobuf_iter_init(
	IN 		mt_iobuf_t *iobuf_p, 
	IN OUT	mt_iobuf_iter_t *iterator_p);

uint32_t iobuf_get_tpt_seg(
	IN		mt_iobuf_t *iobuf_p, 
	IN OUT	mt_iobuf_iter_t *iterator_p,
	IN		uint32_t n_pages_in, 
	IN OUT	uint64_t *page_tbl_p );

unsigned long copy_from_user(void *to, const void *from, unsigned long n);
unsigned long copy_to_user(void *to, const void *from, unsigned long n);


#endif	
