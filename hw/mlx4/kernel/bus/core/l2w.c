#include "l2w.h"
#include "core.h"
#include "pa_cash.h"
#include "mlx4.h"

#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "l2w.tmh"
#endif 

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
	strncpy( pool->name, name, sizeof pool->name );

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
	SIZE_T ret = strlen(src);

	if (size) {
		SIZE_T len = (ret >= size) ? size-1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
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

#ifdef USE_WDM_INTERRUPTS

void free_irq(struct mlx4_dev *dev)
{
	if (!dev->pdev->int_obj)
		return;
	
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
	// Vista build environment
	if (dev->pdev->legacy_connect)
		IoDisconnectInterrupt( dev->pdev->int_obj );
	else {
		IO_DISCONNECT_INTERRUPT_PARAMETERS ctx;
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			("IoDisconnectInterrupt: Version %d\n", dev->pdev->version)); 
		ctx.Version = dev->pdev->version;
		ctx.ConnectionContext.InterruptObject = dev->pdev->int_obj;
		IoDisconnectInterruptEx( &ctx );
	}

#else
	// legacy build environment
	IoDisconnectInterrupt( dev->pdev->int_obj );
#endif
	dev->pdev->int_obj = NULL;
}


int request_irq(
	IN		struct mlx4_dev *	dev,		
	IN		PKSERVICE_ROUTINE	isr,		/* Line ISR */
	IN		PVOID				isr_ctx,	/* ISR context */
	IN		PKMESSAGE_SERVICE_ROUTINE	misr,		/* Message ISR */
	OUT		PKINTERRUPT		*	int_obj		/* interrupt object */
	)
{
	NTSTATUS status;
	struct pci_dev *pdev = dev->pdev;		/* interrupt resources */

#if (NTDDI_VERSION >= NTDDI_LONGHORN)

	IO_CONNECT_INTERRUPT_PARAMETERS params;
	PIO_INTERRUPT_MESSAGE_INFO p_msi_info;

	KeInitializeSpinLock( &pdev->isr_lock );
	pdev->n_msi_vectors = 0;  // not using MSI/MSI-X

	//
	// Vista and later platforms build environment
	//

	RtlZeroMemory( &params, sizeof(IO_CONNECT_INTERRUPT_PARAMETERS) );
	if ( !(dev->flags & MLX4_FLAG_MSI_X) ) {
		params.Version = CONNECT_FULLY_SPECIFIED;
		goto get_legacy_int;
	}
		
	//
	// try to connect our Interrupt Message Service Rotuine to
	// all Message-Signaled Interrupts our device has been granted,
	// with automatic fallback to a single line-based interrupt.
	//
	
	params.Version = CONNECT_MESSAGE_BASED;
	params.MessageBased.PhysicalDeviceObject = pdev->pdo;
	params.MessageBased.ConnectionContext.Generic = &p_msi_info;
	params.MessageBased.MessageServiceRoutine = misr;
	params.MessageBased.ServiceContext = isr_ctx;
	params.MessageBased.SpinLock = NULL;
	params.MessageBased.SynchronizeIrql = 0;
	params.MessageBased.FloatingSave = FALSE;
	// fallback to line-based ISR if there is no MSI support
	params.MessageBased.FallBackServiceRoutine = isr;
	
	status = IoConnectInterruptEx(&params);

	pdev->version = params.Version;
	*int_obj = (PVOID)p_msi_info;
	
	if ( NT_SUCCESS(status) ) {
	
		//
		// It worked, so we're running on Vista or later.
		//
	
		if(params.Version == CONNECT_MESSAGE_BASED) {
			ULONG i;
		
			//
			// Because we succeeded in connecting to one or more Message-Signaled
			// Interrupts, the connection context that was returned was
			// a pointer to an IO_INTERRUPT_MESSAGE_INFO structure.
			//
			pdev->n_msi_vectors = (u8)p_msi_info->MessageCount;  // not using MSI/MSI-X
			// print it 
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
				("request_irq: Granted %d MSI vectors ( UnifiedIrql %#x)\n", 
				p_msi_info->MessageCount, p_msi_info->UnifiedIrql ));
			for (i=0; i < p_msi_info->MessageCount; ++i) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
					("*** Vector %#x, Affinity %#x, Irql %#x, MsgAddr %I64x, MsgData %#x, Mode %d\n", 
					p_msi_info->MessageInfo[i].Vector,
					(ULONG)p_msi_info->MessageInfo[i].TargetProcessorSet,
					p_msi_info->MessageInfo[i].Irql,
					p_msi_info->MessageInfo[i].MessageAddress.QuadPart,
					p_msi_info->MessageInfo[i].MessageData,
					p_msi_info->MessageInfo[i].Mode ));
			}

			// sanity check
			if (pdev->n_msi_vectors_alloc != pdev->n_msi_vectors) {
				MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_INIT ,
					("Connected to %d interrupts from %d allocated to us !!!\n",
					pdev->n_msi_vectors, pdev->n_msi_vectors_alloc ));
				ASSERT(pdev->n_msi_vectors_alloc == pdev->n_msi_vectors);
				*int_obj = NULL;
				return -EFAULT; 	/* failed to connect interrupt */
			}

			// fill MSI-X map table
			for (i=0; i < p_msi_info->MessageCount; ++i) {
				pdev->p_msix_map[i].cpu = p_msi_info->MessageInfo[i].TargetProcessorSet;
			}

		} else {
			//
			// We are on Vista, but there is no HW MSI support
			// So we are connected to line interrupt
			ASSERT(params.Version == CONNECT_LINE_BASED);
		}
	
	
	} else {
	
		//
		// We are on a legacy system and maybe can proceed
		//

		if (params.Version == CONNECT_FULLY_SPECIFIED) {
	
			//
			// use IoConnectInterruptEx to connect our ISR to a
			// line-based interrupt.
			//
get_legacy_int:
			params.FullySpecified.PhysicalDeviceObject = pdev->pdo;
			params.FullySpecified.InterruptObject  = int_obj;
			params.FullySpecified.ServiceRoutine  = isr;
			params.FullySpecified.ServiceContext = isr_ctx;
			params.FullySpecified.FloatingSave = FALSE;
			params.FullySpecified.SpinLock = NULL;

			if (pdev->int_info.Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
				// The resource is for a message-based interrupt. Use the u.MessageInterrupt.Translated member of IntResource.
				
				params.FullySpecified.Vector = pdev->int_info.u.MessageInterrupt.Translated.Vector;
				params.FullySpecified.Irql = (KIRQL)pdev->int_info.u.MessageInterrupt.Translated.Level;
				params.FullySpecified.SynchronizeIrql = (KIRQL)pdev->int_info.u.MessageInterrupt.Translated.Level;
				params.FullySpecified.ProcessorEnableMask = g.mod_affinity ? 
					g.mod_affinity : pdev->int_info.u.MessageInterrupt.Translated.Affinity;
			} else {
				// The resource is for a line-based interrupt. Use the u.Interrupt member of IntResource.
				
				params.FullySpecified.Vector = pdev->int_info.u.Interrupt.Vector;
				params.FullySpecified.Irql = (KIRQL)pdev->int_info.u.Interrupt.Level;
				params.FullySpecified.SynchronizeIrql = (KIRQL)pdev->int_info.u.Interrupt.Level;
				params.FullySpecified.ProcessorEnableMask = g.mod_affinity ? 
					g.mod_affinity : pdev->int_info.u.Interrupt.Affinity;
			}
			
			params.FullySpecified.InterruptMode = (pdev->int_info.Flags & CM_RESOURCE_INTERRUPT_LATCHED ? Latched : LevelSensitive);
			params.FullySpecified.ShareVector = (BOOLEAN)(pdev->int_info.ShareDisposition == CmResourceShareShared);

			status = IoConnectInterruptEx(&params);
			pdev->version = params.Version;
		}
		else {

			// Something wrong with IoConnectInterruptEx.
			// Lets try the usual way
			status = IoConnectInterrupt(
				int_obj,										/* InterruptObject */
				isr,											/* ISR */ 
				isr_ctx,										/* ISR context */
				&pdev->isr_lock,								/* spinlock */
				pdev->int_info.u.Interrupt.Vector,				/* interrupt vector */
				(KIRQL)pdev->int_info.u.Interrupt.Level,		/* IRQL */
				(KIRQL)pdev->int_info.u.Interrupt.Level,		/* Synchronize IRQL */
				(BOOLEAN)((pdev->int_info.Flags == CM_RESOURCE_INTERRUPT_LATCHED) ? 
					Latched : LevelSensitive),					/* interrupt type: LATCHED or LEVEL */
				(BOOLEAN)(pdev->int_info.ShareDisposition == CmResourceShareShared),	/* vector shared or not */
				g.mod_affinity ? g.mod_affinity : (KAFFINITY)pdev->int_info.u.Interrupt.Affinity,	/* interrupt affinity */
				FALSE															/* whether to save Float registers */
				);
			pdev->legacy_connect = TRUE;
		}

	}

#else

	//
	// Legacy (before Vista) platform build environment
	//

	UNUSED_PARAM(misr);

	KeInitializeSpinLock( &pdev->isr_lock );
	pdev->n_msi_vectors = 0;  // not using MSI/MSI-X

	status = IoConnectInterrupt(
		int_obj,										/* InterruptObject */
		isr,											/* ISR */ 
		isr_ctx,										/* ISR context */
		&pdev->isr_lock,								/* spinlock */
		pdev->int_info.u.Interrupt.Vector,				/* interrupt vector */
		(KIRQL)pdev->int_info.u.Interrupt.Level,		/* IRQL */
		(KIRQL)pdev->int_info.u.Interrupt.Level,		/* Synchronize IRQL */
		(BOOLEAN)((pdev->int_info.Flags == CM_RESOURCE_INTERRUPT_LATCHED) ? 
			Latched : LevelSensitive),					/* interrupt type: LATCHED or LEVEL */
		(BOOLEAN)(pdev->int_info.ShareDisposition == CmResourceShareShared),	/* vector shared or not */
		g.mod_affinity ? g.mod_affinity : (KAFFINITY)pdev->int_info.u.Interrupt.Affinity,	/* interrupt affinity */
		FALSE															/* whether to save Float registers */
		);

#endif

	if (!NT_SUCCESS(status)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_INIT ,
			("Connect interrupt failed with status %#x, affinity %#x )\n",
			status, g.mod_affinity ? g.mod_affinity : (unsigned int)pdev->int_info.u.Interrupt.Affinity));
		*int_obj = NULL;
		return -EFAULT;		/* failed to connect interrupt */
	} 

	return 0;
}
#endif
