#pragma once

#include "iobuf.h"
#include "complib\cl_debug.h"
#include "complib\cl_memory.h"

////////////////////////////////////////////////////////
//
// CONSTANTS
//
////////////////////////////////////////////////////////

#define MT_TAG_ATOMIC		'MOTA'
#define MT_TAG_KERNEL		'LNRK'
#define MT_TAG_HIGH			'HGIH'
#define MT_TAG_PCIPOOL		'PICP'
#define MT_TAG_IOMAP		'PAMI'

////////////////////////////////////////////////////////
//
// SUBSTITUTIONS
//
////////////////////////////////////////////////////////


////////////////////////////////////////////////////////
//
// MACROS
//
////////////////////////////////////////////////////////

#define PAGE_MASK						(~(PAGE_SIZE-1))


 ////////////////////////////////////////////////////////
 //
 // Helper functions
 //
 ////////////////////////////////////////////////////////

// returns log of number of pages, i.e
// for size <= 4096 ==> 0
// for size <= 8192 ==> 1
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


////////////////////////////////////////////////////////
//
// SYSTEM MEMORY
//
////////////////////////////////////////////////////////

typedef enum _gfp {
	__GFP_NOWARN		= 0,	/* Suppress page allocation failure warning */
	__GFP_HIGHMEM		= 0,	/* high memory */
	GFP_ATOMIC			= 1,	/* can't wait (i.e. DPC or higher) */
	GFP_KERNEL			= 2,	/* can wait (npaged) */
	GFP_HIGHUSER 		= 4	/* GFP_KERNEL, that can be in HIGH memory */
}
gfp_t;

struct vm_area_struct {
	void *	ptr;
};

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CALLED_FROM __FILE__ ":" TOSTRING(__LINE__)

static inline void * __kmalloc(__in SIZE_T bsize,__in gfp_t gfp_mask,__in char *str)
{
	void *ptr;
	ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL);
	ASSERT(bsize);
#ifdef CL_TRACK_MEM
	UNREFERENCED_PARAMETER(gfp_mask);
	ptr = cl_malloc_ex( bsize, str );
#else
	UNREFERENCED_PARAMETER(str);
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
#endif	
	return ptr;
}

#define kmalloc(_size,_flag)		\
	__kmalloc(_size,_flag, CALLED_FROM )

static inline void * __kzalloc(__in SIZE_T bsize,__in gfp_t gfp_mask,__in char *str)
{
	void* va = __kmalloc(bsize, gfp_mask, str);
	if (va)
		RtlZeroMemory(va, bsize);
	return va;
}

#define kzalloc(_size,_flag)		\
	__kzalloc(_size,_flag, CALLED_FROM )

static inline void *__kcalloc(__in size_t n,__in size_t size,__in gfp_t flags,__in char *str)
{
	if (n != 0 && size > ULONG_MAX / n)
		return NULL;
	return __kzalloc(n * size, flags, str);
}

#define kcalloc(_n,_size,_flag)		\
	__kcalloc(_n,_size,_flag, CALLED_FROM)


static inline void kfree (const void *pobj)
{
	ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	if (pobj)
	{
#ifdef CL_TRACK_MEM
		cl_free((void *)pobj);
#else
		ExFreePool((void *)pobj);
#endif
	}
}

#define vmalloc(size)         kmalloc(size, GFP_KERNEL)
#define vfree                 kfree

#define get_zeroed_page(mask)			kzalloc(PAGE_SIZE, mask)
#define free_page(ptr) 					kfree(ptr)


////////////////////////////////////////////////////////
//
// IO SPACE <==> SYSTEM MEMORY
//
////////////////////////////////////////////////////////

/**
* ioremap     -   map bus memory into CPU space
* @addr:      bus address of the memory
* @size:      size of the resource to map
*
* ioremap performs a platform specific sequence of operations to
* make bus memory CPU accessible via the readb/readw/readl/writeb/
* writew/writel functions and the other mmio helpers. The returned
* address is not guaranteed to be usable directly as a virtual
* address. 
*/
static inline 	void *ioremap(io_addr_t addr, SIZE_T size, MEMORY_CACHING_TYPE cache_type)
{
	PHYSICAL_ADDRESS pa;
	void *va;
	
	ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	pa.QuadPart = addr;
	va = MmMapIoSpace( pa, size, cache_type ); 
	return va;
}

static inline void iounmap(void *va, SIZE_T size)
{
	MmUnmapIoSpace( va, size );
}


struct io_mapping {
        resource_size_t base;
#ifndef MAP_WC_EVERY_TIME		
		void *va;
#endif
        SIZE_T size;
};

// for some reason with MAP_WC_EVERY_TIME it works a bit better
// theoretically it should be the same:
// with MAP_WC_EVERY_TIME we map one page of BF space upon create QP
// w/o MAP_WC_EVERY_TIME we map all BF area on start up.
// I saw that in second case OS uses large pages. Maybe it's the reason ? Not clear.

#ifdef MAP_WC_EVERY_TIME

// Create the io_mapping object
static inline struct io_mapping *
io_mapping_create_wc(resource_size_t base, SIZE_T size)
{
	struct io_mapping *iomap;

	iomap = (io_mapping *)kzalloc(sizeof(*iomap), GFP_KERNEL);
	if (!iomap)
			goto out_err;
	iomap->base = base;
	iomap->size = size;
out_err:	
	return iomap;
}

// free io_mapping object
static inline void
io_mapping_free(struct io_mapping *mapping)
{
	if ( mapping )
		kfree( mapping );
}

static inline void __iomem *
io_mapping_map_wc(struct io_mapping *mapping, SIZE_T offset, unsigned *p_size )
{
	resource_size_t phys_addr;
	void __iomem *va;

	BUG_ON(offset >= mapping->size);
	phys_addr = mapping->base + offset;
	va = ioremap(phys_addr, PAGE_SIZE, MmWriteCombined);
	if (!va)
		goto out_free;
	*p_size = PAGE_SIZE;
		
out_free:
	return va;
}

static inline void
io_mapping_unmap(void __iomem *vaddr, unsigned size)
{
	if ( vaddr )
		iounmap( vaddr, size );
}

#else


// Create the io_mapping object
static inline struct io_mapping *
io_mapping_create_wc(resource_size_t base, SIZE_T size)
{
	struct io_mapping *iomap;

	iomap = (struct io_mapping *)kmalloc(sizeof(*iomap), GFP_KERNEL);
	if (!iomap)
			goto out_err;

	iomap->va = ioremap(base, size, MmWriteCombined);
	if (!iomap->va)
			goto out_free;

	iomap->base = base;
	iomap->size = size;
	return iomap;
	
out_free:
		kfree(iomap);
out_err:
		return NULL;
}

// free io_mapping object
static inline void
io_mapping_free(struct io_mapping *mapping)
{
	if ( mapping ) {
		iounmap( mapping->va, mapping->size );
		kfree( mapping );
	}
}

static inline void __iomem *
io_mapping_map_wc(struct io_mapping *mapping, SIZE_T offset)
{
		return ((char *)mapping->va) + offset;
}

static inline void
io_mapping_unmap(void __iomem *vaddr)
{
	UNUSED_PARAM(vaddr);
}

#endif

////////////////////////////////////////////////////////
//
// DMA SUPPORT
//
////////////////////////////////////////////////////////

enum dma_data_direction {
	PCI_DMA_BIDIRECTIONAL = 0,
	PCI_DMA_TODEVICE = 1,
	PCI_DMA_FROMDEVICE = 2,
    PCI_DMA_NONE = 3,
	DMA_TO_DEVICE = PCI_DMA_TODEVICE,
    DMA_FROM_DEVICE = PCI_DMA_FROMDEVICE,
    DMA_BIDIRECTIONAL = PCI_DMA_BIDIRECTIONAL,
    DMA_NONE = PCI_DMA_NONE,
};

#define dma_get_cache_alignment		(int)KeGetRecommendedSharedDataAlignment

// wrapper to DMA address
typedef struct _dma_addr
{
	// TODO: in some cases it is still physical address today
	io_addr_t			da;		/* logical (device) address */
	void *				va;		/* kernel virtual address */
	size_t		sz; 	/* buffer size */
} dma_addr_t;

#define lowmem_page_address(dma_addr)	((dma_addr).va)

struct mlx4_dev;

void *alloc_cont_mem(
	IN		struct pci_dev *pdev, 
	IN		unsigned long size,
	OUT		dma_addr_t*p_dma_addr);

void free_cont_mem(
	IN		struct pci_dev *pdev, 
	IN		dma_addr_t*p_dma_addr);

// TODO: translate to DMA space - for now is not done anything
static inline dma_addr_t pci_map_page(struct pci_dev *pdev, 
	dma_addr_t dma_addr, unsigned long offset, SIZE_T size, int direction)
{
	UNUSED_PARAM(pdev);
	UNUSED_PARAM(offset);
	UNUSED_PARAM(size);
	UNUSED_PARAM(direction);

	return dma_addr; 
}

static inline dma_addr_t 
alloc_pages( struct pci_dev *pdev, gfp_t gfp, int order )
{
	dma_addr_t dma_addr;
	UNUSED_PARAM(gfp);
	alloc_cont_mem( pdev, PAGE_SIZE << order, &dma_addr );
	return dma_addr;
}

#define alloc_page(pdev, mask)		alloc_pages(pdev, (mask), 0)
#define __get_free_page(mask)		kzalloc(PAGE_SIZE, mask)

static inline void 
__free_pages( struct pci_dev *pdev, dma_addr_t dma_addr, int order )
{
	UNUSED_PARAM(order);
	ASSERT((PAGE_SIZE << order) == (int)dma_addr.sz);
	free_cont_mem( pdev, &dma_addr );
}

#define __free_page(pdev, dma_addr)	__free_pages(pdev, (dma_addr), 0)



static inline int pci_dma_mapping_error(dma_addr_t dma_addr)
{
	return !dma_addr.sz;
}

static inline void pci_unmap_page(struct pci_dev *pdev, 
	dma_addr_t dma_addr, SIZE_T size, int direction)
{
	UNUSED_PARAM(pdev);
	UNUSED_PARAM(dma_addr);
	UNUSED_PARAM(size);
	UNUSED_PARAM(direction);
}

static inline void
dma_sync_single( void *vdev, dma_addr_t dma_addr,
	size_t size, int direction)
{
	UNUSED_PARAM(vdev);
	UNUSED_PARAM(dma_addr);
	UNUSED_PARAM(size);
	UNUSED_PARAM(direction);
	// TODO: here is to be used FlushAdapterBuffers()
}

struct pci_dev;

void *
dma_alloc_coherent( struct mlx4_dev **dev, size_t size, 
	dma_addr_t *p_dma, gfp_t gfp );

void dma_free_coherent( struct mlx4_dev **dev, size_t size, 
	void *vaddr, dma_addr_t dma_handle);

 void pci_free_consistent( struct pci_dev *pdev, size_t size, 
	 void *vaddr, dma_addr_t dma_handle);

static inline dma_addr_t pci_map_single(struct pci_dev *pdev, void *buf, size_t buf_size, int direction)
{
	dma_addr_t dma;
	PHYSICAL_ADDRESS pa;

	UNUSED_PARAM(pdev);
	UNUSED_PARAM(direction);

	pa = MmGetPhysicalAddress(buf);
	dma.da = pa.QuadPart;
	dma.va = buf;
	dma.sz = buf_size;
	return dma;
}

static inline void pci_unmap_single(struct pci_dev *hwdev, u64 ba, size_t size,
     int direction)
{
	UNUSED_PARAM(hwdev);
	UNUSED_PARAM(ba);
	UNUSED_PARAM(size);
	UNUSED_PARAM(direction);
}

////////////////////////////////////////////////////////
//
// SG lists
//
////////////////////////////////////////////////////////

#define sg_dma_addr(sg)					((sg)->dma_addr)
#define sg_dma_address(sg)				((sg)->dma_addr.da)
#define sg_dma_len(sg)					((sg)->dma_addr.sz)
#define sg_dma_address_inc(p_dma,val)	(p_dma)->da += val
#define sg_page(sg)						((sg)->dma_addr)

struct scatterlist {
	dma_addr_t 			dma_addr;	/* logical (device) address */
	unsigned int		offset;		/* offset in the first page */
	unsigned int		length;		
	PMDL				p_mdl;		/* MDL, if any (used for user space buffers) */
};

struct sg_table {
    struct scatterlist *sgl;        /* the list */
    unsigned int nents;             /* number of mapped entries */
};

#define offset_in_page(va)	((ULONG)((ULONG_PTR)(va) & ~PAGE_MASK))

#define sg_next(sg)     (sg)++

/*
* Loop over each sg element, following the pointer to a new list if necessary
*/
#define for_each_sg(sglist, sg, nr, __i)        \
        for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

#define scsi_for_each_sg(cmd, sg, nseg, __i)                    \
                for_each_sg(scsi_sglist(cmd), sg, nseg, __i)

static inline void sg_init_table(struct scatterlist *sgl, unsigned int nents)
{
	memset(sgl, 0, sizeof(*sgl) * nents);
}

static inline void sg_set_buf(struct scatterlist *sg, void *buf,
	unsigned int buflen)
{
    PHYSICAL_ADDRESS pa = {0};
    
	sg->offset = offset_in_page(buf);
    sg->length = buflen;

    pa = MmGetPhysicalAddress(buf);
    sg->dma_addr.da = pa.QuadPart;
    sg->dma_addr.va = buf;
    sg->dma_addr.sz = buflen;
}

static inline void sg_set_page(struct scatterlist *sg, 
	dma_addr_t dma_addr, unsigned int len, unsigned int offset)
{
	sg->offset = offset;
	sg->dma_addr = dma_addr;
    sg->length = len;
}

static inline void sg_init_one(struct scatterlist *sg, void *buf, unsigned int buflen)
{
	sg_init_table(sg, 1);
	sg_set_buf(sg, buf, buflen);
}

/* Returns: the number of unmapped sg elements */
static inline int pci_map_sg(struct pci_dev *pdev, 
	struct scatterlist *sg, int nents, int direction)
{
	UNUSED_PARAM(pdev);
	UNUSED_PARAM(sg);
	UNUSED_PARAM(direction);
	return nents;
}

/* Returns: the number of unmapped sg elements */
static inline int pci_unmap_sg(struct pci_dev *pdev, 
	struct scatterlist *sg, 	int nents, int direction)
{
	UNUSED_PARAM(pdev);
	UNUSED_PARAM(sg);
	UNUSED_PARAM(direction);
	return nents;
}

/* highmem mapping */
enum km_type {
    KM_BOUNCE_READ,
    KM_SKB_SUNRPC_DATA,
    KM_SKB_DATA_SOFTIRQ, 
    KM_USER0,
    KM_USER1,
    KM_BIO_SRC_IRQ,
    KM_BIO_DST_IRQ,
    KM_IRQ0,
    KM_IRQ1,
    KM_SOFTIRQ0,
    KM_SOFTIRQ1,
    KM_TYPE_NR
};

