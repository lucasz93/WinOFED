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


#include <complib/comp_lib.h>
#include <iba/ib_al.h>
#include <iba/ib_al_ioctl.h>
#include "al.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_ndi_cq.tmh"
#endif

#include "al_dev.h"
/* Get the internal definitions of apis for the proxy */
#include "al_ca.h"
#include "al_cq.h"
#include "ib_common.h"

/*******************************************************************
 *
 * Helpers
 *
 ******************************************************************/

#pragma warning(disable:4706)
static inline void __ndi_flush_que(
	IN	ndi_cq_csq_t*							p_ndi_csq,
	IN	NTSTATUS								completion_code
	)
{
	PIRP Irp;
	while( Irp = IoCsqRemoveNextIrp( &p_ndi_csq->csq, NULL ) )
	{
		cl_ioctl_complete( Irp, completion_code, 0 );
		deref_al_obj( &p_ndi_csq->h_cq->obj );
	}
}
#pragma warning(default:4706)

/*******************************************************************
 *
 * Callbacks
 *
 ******************************************************************/

#pragma warning(disable:4706)
void ndi_cq_compl_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	PIRP Irp;
	ndi_cq_csq_t*p_ndi_csq = &h_cq->compl;
	UNUSED_PARAM( cq_context );

	AL_ENTER( AL_DBG_NDI );

	while( Irp = IoCsqRemoveNextIrp( &p_ndi_csq->csq, NULL ) )
	{
		Irp->IoStatus.Status = STATUS_SUCCESS;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest( Irp, IO_NETWORK_INCREMENT );
		deref_al_obj( &p_ndi_csq->h_cq->obj );
	}

	AL_EXIT( AL_DBG_NDI );
}
#pragma warning(default:4706)

void ndi_cq_error_cb(
	IN				ib_async_event_rec_t		*p_err_rec)
{
	ib_cq_handle_t h_cq = p_err_rec->handle.h_cq;
	AL_ENTER( AL_DBG_NDI );
	__ndi_flush_que( &h_cq->compl, STATUS_INTERNAL_ERROR );
	__ndi_flush_que( &h_cq->error, STATUS_INTERNAL_ERROR );
	AL_EXIT( AL_DBG_NDI );
}

/*******************************************************************
 *
 * Public routines
 *
 ******************************************************************/

/* flush a queue of pending requests */
void
ndi_cq_flush_ques(
	IN	ib_cq_handle_t							h_cq
	)
{
	AL_ENTER( AL_DBG_NDI );
	if ( h_cq->pfn_user_comp_cb == ndi_cq_compl_cb )
	{
		__ndi_flush_que( &h_cq->compl, STATUS_CANCELLED );
		__ndi_flush_que( &h_cq->error, STATUS_CANCELLED );
	}
	AL_EXIT( AL_DBG_NDI );
}


/*******************************************************************
 *
 * CSQ
 *
 ******************************************************************/
#ifdef NTDDI_WIN8
static IO_CSQ_INSERT_IRP __ndi_insert_irp;
#endif

static VOID __ndi_insert_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									Irp
	)
{
	ndi_cq_csq_t *p_ndi_csq = (ndi_cq_csq_t*)Csq;

	AL_ENTER( AL_DBG_NDI );
	InsertTailList( &p_ndi_csq->queue, &Irp->Tail.Overlay.ListEntry );
	AL_EXIT( AL_DBG_NDI );
}

#ifdef NTDDI_WIN8
static IO_CSQ_REMOVE_IRP __ndi_remove_irp;
#endif

static VOID __ndi_remove_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									Irp
	)
{
	UNUSED_PARAM( Csq );

	AL_ENTER( AL_DBG_NDI );
	RemoveEntryList( &Irp->Tail.Overlay.ListEntry );
	AL_EXIT( AL_DBG_NDI );
}

#ifdef NTDDI_WIN8
static IO_CSQ_PEEK_NEXT_IRP __peek_next_irp;
#endif

static PIRP __ndi_peek_next_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									Irp,
	IN	PVOID									PeekContext
	)
{
	PIRP nextIrp = NULL;
	PLIST_ENTRY nextEntry;
	PLIST_ENTRY listHead;
	ndi_cq_csq_t *p_ndi_csq = (ndi_cq_csq_t*)Csq;

	AL_ENTER( AL_DBG_NDI );

	listHead = &p_ndi_csq->queue;

	// 
	// If the IRP is NULL, we will start peeking from the listhead, else
	// we will start from that IRP onwards. This is done under the
	// assumption that new IRPs are always inserted at the tail.
	//

	if(Irp == NULL)
		nextEntry = listHead->Flink;
	else
		nextEntry = Irp->Tail.Overlay.ListEntry.Flink;

	while(nextEntry != listHead) {
		nextIrp = CONTAINING_RECORD(nextEntry, IRP, Tail.Overlay.ListEntry);

		//
		// If context is present, continue until you find a matching one.
		// Else you break out as you got next one.
		//

		if(PeekContext) 
		{
			/* for now PeekContext is not used */
		} 
		else
		{
			break;
		}

		nextIrp = NULL;
		nextEntry = nextEntry->Flink;
	}

	AL_EXIT( AL_DBG_NDI );
	return nextIrp;
}

#ifdef NTDDI_WIN8
static IO_CSQ_ACQUIRE_LOCK __ndi_acquire_lock;

_IRQL_requires_max_(DISPATCH_LEVEL)
__drv_raisesIRQL(DISPATCH_LEVEL)
//TODO __drv_at((ndi_cq_csq_t*)Csq->h_cq->obj.lock, __drv_acquiresExclusiveResource(KSPIN_LOCK))
_IRQL_saves_global_(SpinLock, pIrql)
#endif
#pragma prefast(suppress: 28167, "The irql level is restored by cl_spinlock_release, impossible to annotate by driver directives")
static VOID __ndi_acquire_lock(
	IN	PIO_CSQ									Csq,
	OUT	PKIRQL									Irql
	)
{
	ndi_cq_csq_t *p_ndi_csq = (ndi_cq_csq_t*)Csq;
	ib_cq_handle_t h_cq = p_ndi_csq->h_cq;
	UNUSED_PARAM( Irql );

	AL_ENTER( AL_DBG_NDI );
	cl_spinlock_acquire( &h_cq->obj.lock );
	AL_EXIT( AL_DBG_NDI );
}

#ifdef NTDDI_WIN8
static IO_CSQ_RELEASE_LOCK __ndi_release_lock;

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
// TODO __drv_at(((ndi_cq_csq_t*)Csq)->h_cq->obj.lock, __drv_releasesExclusiveResource(KSPIN_LOCK))
_IRQL_restores_global_(SpinLock, pIrql)

#endif
#pragma prefast(suppress: 28167, "The irql level is restored by cl_spinlock_release, impossible to annotate by driver directives")
static VOID __ndi_release_lock(
	IN	PIO_CSQ									Csq,
	IN	KIRQL									Irql
	)
{
	ndi_cq_csq_t *p_ndi_csq = (ndi_cq_csq_t*)Csq;
	ib_cq_handle_t h_cq = p_ndi_csq->h_cq;
	UNUSED_PARAM( Irql );

	AL_ENTER( AL_DBG_NDI );
	cl_spinlock_release( &h_cq->obj.lock );
	AL_EXIT( AL_DBG_NDI );
}

#ifdef NTDDI_WIN8
static IO_CSQ_COMPLETE_CANCELED_IRP __ndi_complete_cancelled_irp;
#endif

static VOID __ndi_complete_cancelled_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									Irp
	)
{
	ndi_cq_csq_t *p_ndi_csq = (ndi_cq_csq_t*)Csq;
	ib_cq_handle_t h_cq = p_ndi_csq->h_cq;

	AL_ENTER( AL_DBG_NDI );
	cl_ioctl_complete( Irp, CL_CANCELED, 0 );
	deref_al_obj( &h_cq->obj );
	AL_EXIT( AL_DBG_NDI );
}

NTSTATUS
ndi_cq_init(
	IN				ib_cq_handle_t				h_cq )
{

	NTSTATUS status;

	AL_ENTER( AL_DBG_NDI );

	status = IoCsqInitialize( &h_cq->compl.csq, 
		__ndi_insert_irp, __ndi_remove_irp,
		__ndi_peek_next_irp, __ndi_acquire_lock,
		__ndi_release_lock, __ndi_complete_cancelled_irp );
	if ( !NT_SUCCESS( status ) )
		goto exit;

	status = IoCsqInitialize( &h_cq->error.csq, 
		__ndi_insert_irp, __ndi_remove_irp,
		__ndi_peek_next_irp, __ndi_acquire_lock,
		__ndi_release_lock, __ndi_complete_cancelled_irp );
	if ( !NT_SUCCESS( status ) )
		goto exit;

	InitializeListHead( &h_cq->compl.queue );
	InitializeListHead( &h_cq->error.queue );
	h_cq->compl.h_cq = h_cq;
	h_cq->error.h_cq = h_cq;
	status = STATUS_SUCCESS;

exit:
	AL_EXIT( AL_DBG_NDI );
	return status;
}



