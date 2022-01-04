#include "l2w_precomp.h"

#include "l2w_workqueue.h"

#include "core.h"
#include "pa_cash.h"
#include "mlx4.h"


/* Nth element of the table contains the index of the first set bit of N; 8 - for N=0 */
char g_set_bit_tbl[256];

/* Nth element of the table contains the index of the first 0 bit of N; 8 - for N=255 */
char g_clr_bit_tbl[256];

/* interval for a cmd go-bit waiting */
// TODO: not clear what is to be this value:
// 1. it has to be enough great, so as the tread will go waiting;
// 2. it has to be enough small, so as there is no too large waiting after first command try;
// 3. it has to be enough great, so as not to cause to intensive rescheduling;
#define CMD_WAIT_USECS			2
#define CMD_WAIT_INTERVAL		((-10) * CMD_WAIT_USECS)
LARGE_INTEGER g_cmd_interval = { (ULONG)CMD_WAIT_INTERVAL, 0 };

////////////////////////////////////////////////////////
//
// PCI POOL 
//
////////////////////////////////////////////////////////

pci_pool_t *
pci_pool_create (const char *name, struct pci_dev *pdev,
	size_t size, size_t align, size_t allocation)
{
	pci_pool_t *pool;
	UNREFERENCED_PARAMETER(align);
	UNREFERENCED_PARAMETER(allocation);

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	
	// allocation parameter is not handled yet
	ASSERT(allocation == 0);

	//TODO: not absolutely correct: Linux's pci_pool_alloc provides contiguous physical memory,
	// while default alloc function  - ExAllocatePoolWithTag -doesn't.
	// But for now it is used for elements of size <= PAGE_SIZE
	// Anyway - a sanity check:
	ASSERT(size <= PAGE_SIZE);
	if (size > PAGE_SIZE)
		return NULL;

	// allocate object
	pool = (pci_pool_t *)ExAllocatePoolWithTag( NonPagedPool, sizeof(pci_pool_t), MT_TAG_PCIPOOL );
	if (pool == NULL) 
		return NULL;

	//TODO: not too effective: one can read its own alloc/free functions
	ExInitializeNPagedLookasideList( &pool->pool_hdr, NULL, NULL, 0, size, MT_TAG_PCIPOOL, 0 );
	
	// fill the object
	pool->mdev = pdev->dev;
	pool->size = size;
	RtlStringCchCopyA(pool->name, sizeof(pool->name),name);

	return pool;		
}


////////////////////////////////////////////////////////
//
// BIT TECHNIQUES 
//
////////////////////////////////////////////////////////

void fill_bit_tbls()
{	
	unsigned long i;
	for (i=0; i<256; ++i) {
		g_set_bit_tbl[i] = (char)(_ffs_raw(&i,0) - 1);
		g_clr_bit_tbl[i] = (char)(_ffz_raw(&i,0) - 1);
	}
	g_set_bit_tbl[0] = g_clr_bit_tbl[255] = 8;
}


////////////////////////////////////////////////////////
//
// BIT MAPS
//
////////////////////////////////////////////////////////

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


////////////////////////////////////////////////////////
//
// DEBUG PRINT
//
////////////////////////////////////////////////////////

VOID
WriteEventLogEntry(
	PVOID	pi_pIoObject,
	ULONG	pi_ErrorCode,
	ULONG	pi_UniqueErrorCode,
	ULONG	pi_FinalStatus,
	ULONG	pi_nDataItems,
	...
	)
/*++

Routine Description:
    Writes an event log entry to the event log.

Arguments:

	pi_pIoObject......... The IO object ( driver object or device object ).
	pi_ErrorCode......... The error code.
	pi_UniqueErrorCode... A specific error code.
	pi_FinalStatus....... The final status.
	pi_nDataItems........ Number of data items.
	.
	. data items values
	.

Return Value:

	None .

--*/
{ /* WriteEventLogEntry */

	/* Variable argument list */    
	va_list					l_Argptr;
	/* Pointer to an error log entry */
	PIO_ERROR_LOG_PACKET	l_pErrorLogEntry; 

	/* Init the variable argument list */   
	va_start(l_Argptr, pi_nDataItems);

	/* Allocate an error log entry */ 
	l_pErrorLogEntry = 
	(PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(
								pi_pIoObject,
								(UCHAR)(sizeof(IO_ERROR_LOG_PACKET)+pi_nDataItems*sizeof(ULONG))
								); 
	/* Check allocation */
	if ( l_pErrorLogEntry != NULL) 
	{ /* OK */

		/* Data item index */
		USHORT l_nDataItem ;

		/* Set the error log entry header */
		l_pErrorLogEntry->ErrorCode			= pi_ErrorCode; 
		l_pErrorLogEntry->DumpDataSize		= (USHORT) (pi_nDataItems*sizeof(ULONG)); 
		l_pErrorLogEntry->SequenceNumber	= 0; 
		l_pErrorLogEntry->MajorFunctionCode = 0; 
		l_pErrorLogEntry->IoControlCode		= 0; 
		l_pErrorLogEntry->RetryCount		= 0; 
		l_pErrorLogEntry->UniqueErrorValue	= pi_UniqueErrorCode; 
		l_pErrorLogEntry->FinalStatus		= pi_FinalStatus; 

		/* Insert the data items */
		for (l_nDataItem = 0; l_nDataItem < pi_nDataItems; l_nDataItem++) 
		{ /* Inset a data item */

			/* Current data item */
			int l_CurDataItem ;
				
			/* Get next data item */
			l_CurDataItem = va_arg( l_Argptr, int);

			/* Put it into the data array */
			l_pErrorLogEntry->DumpData[l_nDataItem] = l_CurDataItem ;

		} /* Inset a data item */

		/* Write the packet */
		IoWriteErrorLogEntry(l_pErrorLogEntry);

	} /* OK */

	/* Term the variable argument list */   
	va_end(l_Argptr);

} /* WriteEventLogEntry */


////////////////////////////////////////////////////////
//
// GENERAL 
//
////////////////////////////////////////////////////////

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
SIZE_T strlcpy(char *dest, const void *src, SIZE_T size)
{
	SIZE_T ret = strlen((const char *)src);

	if (size) {
		SIZE_T len = (ret >= size) ? size-1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}

int parse_dev_location(
	const char *buffer,
	const char *format,
	int *bus, int *dev, int *func
)
{
	return sscanf( buffer, format, bus, dev, func );
}

int core_init()
{
	int err;
	
	fill_bit_tbls();
	init_qp_state_tbl();
	err =  ib_core_init();
	if (err)
		return err;
	return pa_cash_init();
}

void core_cleanup()
{
	ib_core_cleanup();
	pa_cash_release();
}

int l2w_init()
{
	fill_bit_tbls();
    init_workqueues();
	return 0;
}

void l2w_cleanup()
{
    shutdown_workqueues();
}

