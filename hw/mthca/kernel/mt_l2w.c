#include <mt_l2w.h>
#include <hca_data.h>
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mt_l2w.tmh"
#endif

pci_pool_t *
pci_pool_create (const char *name, struct mthca_dev *mdev,
        size_t size, size_t align, size_t allocation)
{
	pci_pool_t *pool;
	UNREFERENCED_PARAMETER(align);
	UNREFERENCED_PARAMETER(allocation);

	MT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	
	// allocation parameter is not handled yet
	ASSERT(allocation == 0);

	// allocate object
	pool = (pci_pool_t *)ExAllocatePoolWithTag( NonPagedPool, sizeof(pci_pool_t), MT_TAG_PCIPOOL );
	if (pool == NULL) 
		return NULL;

	//TODO: not absolutely correct: Linux's pci_pool_alloc provides contiguous physical memory,
	// while default alloc function  - ExAllocatePoolWithTag -doesn't.
	// But for now it is used for elements of size <= PAGE_SIZE
	// Anyway - a sanity check:
	ASSERT(size <= PAGE_SIZE);
	if (size > PAGE_SIZE)
		return NULL;

	//TODO: not too effective: one can read its own alloc/free functions
	ExInitializeNPagedLookasideList( &pool->pool_hdr, NULL, NULL, 0, size, MT_TAG_PCIPOOL, 0 );
	
	// fill the object
	pool->mdev = mdev;
	pool->size = size;
	strncpy( pool->name, name, sizeof pool->name );

	return pool;		
}

// from lib/string.c
/**
* strlcpy - Copy a %NUL terminated string into a sized buffer
* @dest: Where to copy the string to
* @src: Where to copy the string from
* @size: size of destination buffer
*
* Compatible with *BSD: the result is always a valid
* NUL-terminated string that fits in the buffer (unless,
* of course, the buffer size is zero). It does not pad
* out the result like strncpy() does.
*/
SIZE_T strlcpy(char *dest, const char *src, SIZE_T size)
{
        SIZE_T ret = strlen(src);

        if (size) {
                SIZE_T len = (ret >= size) ? size-1 : ret;
                memcpy(dest, src, len);
                dest[len] = '\0';
        }
        return ret;
}


int __bitmap_full(const unsigned long *bitmap, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (~bitmap[k])
	    	return 0;

	if (bits % BITS_PER_LONG)
		if (~bitmap[k] & BITMAP_LAST_WORD_MASK(bits))
	    	return 0;

	return 1;
}

int __bitmap_empty(const unsigned long *bitmap, int bits)
{
       int k, lim = bits/BITS_PER_LONG;
       for (k = 0; k < lim; ++k)
               if (bitmap[k])
                       return 0;

       if (bits % BITS_PER_LONG)
               if (bitmap[k] & BITMAP_LAST_WORD_MASK(bits))
                       return 0;

       return 1;
}

int request_irq(
	IN 	CM_PARTIAL_RESOURCE_DESCRIPTOR	*int_info,	/* interrupt resources */
	IN		KSPIN_LOCK  	*isr_lock,		/* spin lock for ISR */			
	IN		PKSERVICE_ROUTINE isr,		/* ISR */
	IN		void *isr_ctx,						/* ISR context */
	OUT	PKINTERRUPT *int_obj			/* interrupt object */
	)
{
	NTSTATUS		status;

	status = IoConnectInterrupt(
		int_obj, 														/* InterruptObject */
		isr,																/* ISR */ 
		isr_ctx, 														/* ISR context */
		isr_lock, 													/* spinlock */
		int_info->u.Interrupt.Vector,					/* interrupt vector */
		(KIRQL)int_info->u.Interrupt.Level,		/* IRQL */
		(KIRQL)int_info->u.Interrupt.Level,		/* Synchronize IRQL */
		(BOOLEAN)((int_info->Flags == CM_RESOURCE_INTERRUPT_LATCHED) ? 
		Latched : LevelSensitive),							/* interrupt type: LATCHED or LEVEL */
		(BOOLEAN)(int_info->ShareDisposition == CmResourceShareShared), 	/* vector shared or not */
		(KAFFINITY)int_info->u.Interrupt.Affinity,	/* interrupt affinity */
		FALSE 															/* whether to save Float registers */
		);

	if (!NT_SUCCESS(status)) {
        HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_INIT ,("IoConnectInterrupt  failed status %d (did you change the processor_affinity ? )\n",status));
		return -EFAULT;		/* failed to connect interrupt */
    } 
	else
		return 0;
}


