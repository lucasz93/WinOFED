/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
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



/*
 * Abstract:
 *	This header file defines data structures for the user-mode proxy
 *	and UAL support
 *
 * Environment:
 *	Kernel Mode.
 */


#ifndef _ALPROXY_H_
#define _ALPROXY_H_



/* Just include everything */
#include <complib/comp_lib.h>
#include <complib/cl_ioctl.h>

#include "al_proxy_ioctl.h"
#include "al_mcast.h"

#define AL_CB_POOL_START_SIZE			10
#define AL_CB_POOL_GROW_SIZE			5


#define PROXY_PNP_TIMEOUT_US	(5000000)


typedef struct _proxy_pnp_evt
{
	cl_event_t			event;
	ib_api_status_t		evt_status;
	void*				evt_context;
	size_t				rec_size;

}	proxy_pnp_evt_t;


typedef union _proxy_pnp_recs
{
	ib_pnp_rec_t			pnp;
	ib_pnp_ca_rec_t			ca;
	ib_pnp_port_rec_t		port;
	ib_pnp_iou_rec_t		iou;
	ib_pnp_ioc_rec_t		ioc;
	ib_pnp_ioc_path_rec_t	ioc_path;

}	proxy_pnp_recs_t;

typedef struct _al_dev_open_context al_dev_open_context_t;

typedef struct _al_csq
{
	IO_CSQ						csq;
	KSPIN_LOCK					lock;
	LIST_ENTRY					queue;
	al_dev_open_context_t		*dev_ctx;
} al_csq_t;


/**********************************************************
 *
 * Per-process device context.
 *
 **********************************************************/
typedef struct _al_dev_open_context
{
	volatile boolean_t	closing;
	atomic32_t			ref_cnt;
	cl_event_t			close_event;

	/* General purpose pool of list objects */
	cl_qpool_t			cb_pool;
	cl_spinlock_t		cb_pool_lock;

	/* User-mode callback queues. */
	cl_qlist_t			cm_cb_list;
	cl_qlist_t			comp_cb_list;
	cl_qlist_t			misc_cb_list;
	cl_spinlock_t		cb_lock;

	/* PnP synchronization mutex. */
	cl_mutex_t			pnp_mutex;

	/* Pending IOCTLs. */
	al_csq_t			al_csq;

	/* Per-process AL handle. */
	ib_al_handle_t		h_al;

	/* Process name */
	UNICODE_STRING		pcs_name;
	WCHAR				pcs_name_buffer[256];

}	al_dev_open_context_t;



/****f* Access Layer - Proxy/proxy_context_ref
* NAME
*	proxy_context_ref
*
* DESCRIPTION
*	Function to reference the open context.
*	It fails if the context is closing.
*
* SYNOPSIS
*/
inline boolean_t
proxy_context_ref(
	IN				al_dev_open_context_t		*p_context )
{
	cl_atomic_inc( &p_context->ref_cnt );

	return( !p_context->closing );
}
/*********/


/****f* Access Layer - Proxy/proxy_context_deref
* NAME
*	proxy_context_deref
*
* DESCRIPTION
*	Releases a reference on an open context acquired via a call to
*	proxy_context_ref.
*
* SYNOPSIS
*/
inline void
proxy_context_deref(
	IN				al_dev_open_context_t		*p_context )
{
	cl_atomic_dec( &p_context->ref_cnt );
	cl_event_signal( &p_context->close_event );
}
/*********/



/*
 * Generic callback information.  Used to report callbacks from kernel to
 * user-mode.
 */
typedef struct _al_proxy_cb_info
{
	cl_pool_item_t				pool_item;	/* must be first */
	al_dev_open_context_t		*p_context;

	union _cb_type
	{
		comp_cb_ioctl_info_t	comp;
		misc_cb_ioctl_info_t	misc;

	}	cb_type;

	/*
	 * AL object to dereference after processing callback.  We use this to
	 * ensure that a kernel object is not destroyed while a callback is in
	 * progress to user-mode.  Since user-mode objects are not destroyed until
	 * the associated kernel objects are, this ensures that all callbacks
	 * from the kernel reference valid user-mode objects.
	 */
	al_obj_t					*p_al_obj;
	boolean_t					reported;	

}	al_proxy_cb_info_t;

#pragma warning(disable:4706)
static inline void al_csq_flush_que(
	IN	al_csq_t*							p_al_csq,
	IN	NTSTATUS							completion_code
	)
{
	PIRP Irp;
	while( Irp = IoCsqRemoveNextIrp( &p_al_csq->csq, NULL ) )
	{
		cl_ioctl_complete( Irp, completion_code, 0 );
		proxy_context_deref( p_al_csq->dev_ctx );
	}
}
#pragma warning(default:4706)

#ifdef NTDDI_WIN8
static IO_CSQ_INSERT_IRP __insert_irp;
#endif

static VOID __insert_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									Irp
	)
{
	al_csq_t *p_al_csq = (al_csq_t*)Csq;
	InsertTailList( &p_al_csq->queue, &Irp->Tail.Overlay.ListEntry );
}

#ifdef NTDDI_WIN8
static IO_CSQ_REMOVE_IRP __remove_irp;
#endif

static VOID __remove_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									Irp
	)
{
	UNUSED_PARAM( Csq );
	RemoveEntryList( &Irp->Tail.Overlay.ListEntry );
}

#ifdef NTDDI_WIN8
static IO_CSQ_PEEK_NEXT_IRP __peek_next_irp;
#endif

static PIRP __peek_next_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									Irp,
	IN	PVOID									PeekContext
	)
{
	PIRP nextIrp = NULL;
	PLIST_ENTRY nextEntry;
	PLIST_ENTRY listHead;
	al_csq_t *p_al_csq = (al_csq_t*)Csq;

	listHead = &p_al_csq->queue;

	// 
	// If the IRP is NULL, we will start peeking from the listhead, else
	// we will start from that IRP onwards. This is done under the
	// assumption that new IRPs are always inserted at the tail.
	//

	if(Irp == NULL)
		nextEntry = listHead->Flink;
	else
		nextEntry = Irp->Tail.Overlay.ListEntry.Flink;

	while(nextEntry != listHead) 
	{
		nextIrp = CONTAINING_RECORD(nextEntry, IRP, Tail.Overlay.ListEntry);

		//
		// If context is present, continue until you find a matching one.
		// Else you break out as you got next one.
		//

		if(PeekContext) 
		{
			if( cl_ioctl_ctl_code( nextIrp ) == (ULONG_PTR)PeekContext )
				break;
		} 
		else
		{
			break;
		}

		nextIrp = NULL;
		nextEntry = nextEntry->Flink;
	}

	return nextIrp;
}

#ifdef NTDDI_WIN8
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
//TOD __drv_at(p_ndi_csq->lock, __drv_acquiresExclusiveResource(KSPIN_LOCK))
__drv_at(pIrql, __drv_savesIRQL)
#endif
#pragma prefast(suppress: 28167, "The irql level is saved by cl_spinlock_acquire, impossible to annotate by driver directives")
#pragma prefast(suppress: 28158, "The irql level is stored by cl_spinlock_acquire, impossible to annotate by driver directives")
static VOID __acquire_lock(
	IN	PIO_CSQ									Csq,
	OUT	PKIRQL									Irql
	)
{
	al_csq_t *p_al_csq = (al_csq_t*)Csq;
	KeAcquireSpinLock( &p_al_csq->lock, Irql );
}

#ifdef NTDDI_WIN8
static IO_CSQ_ACQUIRE_LOCK __acquire_lock;

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
//TODO __drv_at(p_ndi_csq->lock, __drv_restoresExclusiveResource(KSPIN_LOCK))
__drv_at(Irql, __drv_restoresIRQL)
#endif
#pragma prefast(suppress: 28167, "The irql level is restored by KeReleaseSpinLock, impossible to annotate by driver directives")
static VOID __release_lock(
	IN	PIO_CSQ									Csq,
	IN	KIRQL									Irql
	)
{
	al_csq_t *p_al_csq = (al_csq_t*)Csq;
	KeReleaseSpinLock( &p_al_csq->lock, Irql );
}

#ifdef NTDDI_WIN8
static IO_CSQ_COMPLETE_CANCELED_IRP __complete_cancelled_irp;
#endif

static VOID __complete_cancelled_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									Irp
	)
{
	al_csq_t *p_al_csq = (al_csq_t*)Csq;
	
	cl_ioctl_complete( Irp, CL_CANCELED, 0 );
	proxy_context_deref( p_al_csq->dev_ctx );
}

static inline NTSTATUS
al_csq_init(
	IN				al_dev_open_context_t * dev_ctx,
	IN				al_csq_t *				p_al_csq)
{
	NTSTATUS status;

	status = IoCsqInitialize( &p_al_csq->csq, 
		__insert_irp, __remove_irp,
		__peek_next_irp, __acquire_lock,
		__release_lock, __complete_cancelled_irp );
	if ( !NT_SUCCESS( status ) )
		goto exit;

	InitializeListHead( &p_al_csq->queue );
	KeInitializeSpinLock( &p_al_csq->lock );
	p_al_csq->dev_ctx = dev_ctx;
	status = STATUS_SUCCESS;

exit:
	return status;
}


al_proxy_cb_info_t*
proxy_cb_get(
	IN	al_dev_open_context_t	*p_context );


void
proxy_cb_put(
	IN	al_proxy_cb_info_t		*p_cbinfo );



void
proxy_cb_put_list(
	IN				al_dev_open_context_t		*p_context,
	IN				cl_qlist_t					*p_cb_list );


cl_status_t proxy_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes );

cl_status_t al_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes );

cl_status_t verbs_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes );

cl_status_t subnet_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes );

cl_status_t cep_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes );

cl_status_t ioc_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes );

cl_status_t ndi_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes );

NTSTATUS ibat_ioctl(
    __in IRP* pIrp
    );

boolean_t
proxy_queue_cb_buf(
	IN		uintn_t					cb_type,
	IN		al_dev_open_context_t	*p_context,
	IN		void					*p_cb_data,
	IN		al_obj_t				*p_al_obj		OPTIONAL );


ib_api_status_t
proxy_pnp_ca_cb(
	IN	ib_pnp_rec_t	*p_pnp_rec );

ib_api_status_t
proxy_pnp_port_cb(
	IN	ib_pnp_rec_t	*p_pnp_rec );

NTSTATUS
ib_to_ntstatus(
    IN  ib_api_status_t ib_status );
#endif	/* _AL_PROXY_H_ */
