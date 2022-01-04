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
#include "al_mgr.h"
#include "al_debug.h"
#if WINVER <= 0x501
#include "csq.h"
#endif
#include <kernel\iba\ibat.h>
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_ndi_cm.tmh"
#endif

#include "al_dev.h"
/* Get the internal definitions of apis for the proxy */
#include "al_ca.h"
#include "ib_common.h"
#include "al_qp.h"
#include "al_cm_conn.h"
#include "al_cm_cep.h"
#include "al_ndi_cm.h"

uint32_t		g_sa_timeout = 500;
uint32_t		g_sa_retries = 4;
uint8_t			g_qp_retries = QP_ATTRIB_RETRY_COUNT;
uint8_t			g_pkt_life_modifier = 0;
uint8_t			g_max_cm_retries = CM_RETRIES;

NTSTATUS
__ndi_ats_query(
	IN		IRP*								p_irp
	);

NTSTATUS
__ndi_pr_query(
	IN		IRP*								p_irp
	);

NTSTATUS
__ndi_send_req(
	IN		IRP*								p_irp
	);

NTSTATUS
__ndi_send_rep(
	IN		nd_csq_t							*p_csq,
	IN		PIRP								p_irp
	);

NTSTATUS
__ndi_send_dreq(
	IN		IRP*								p_irp
	);

NTSTATUS
__ndi_get_req(
	IN		nd_csq_t							*p_csq,
	IN		IRP*								p_irp
	);

static void
__ndi_queue_drep(
	IN				IRP							*p_irp
	);


/*******************************************************************
 *
 * Helpers
 *
 ******************************************************************/

static char * State2String(ndi_cm_state_t state) 
{
	switch (state) 
	{
		case NDI_CM_IDLE					: return "NDI_CM_IDLE";
		case NDI_CM_CONNECTING_QPR_SENT		: return "NDI_CM_CONNECTING_QPR_SENT";
		case NDI_CM_CONNECTING_REQ_SENT		: return "NDI_CM_CONNECTING_REQ_SENT";
		case NDI_CM_CONNECTING_REP_SENT		: return "NDI_CM_CONNECTING_REP_SENT";
		case NDI_CM_CONNECTING_REP_RCVD		: return "NDI_CM_CONNECTING_REP_RCVD";
		case NDI_CM_CONNECTED				: return "NDI_CM_CONNECTED";
		case NDI_CM_DISCONNECTING			: return "NDI_CM_DISCONNECTING";
		case NDI_CM_CONNECTED_DREQ_RCVD		: return "NDI_CM_CONNECTED_DREQ_RCVD";
		case NDI_CM_INVALID					: return "NDI_CM_CONNECTING_REP_SENT";
		default : 
			ASSERT(FALSE);
	}
	return "Unknown state";
}


static inline void
__ndi_complete_irp(
	IN	nd_csq_t*								p_csq,
	IN	PIRP									p_irp,
	IN	NTSTATUS								status
	)
{
	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI, ("[ CID = %d\n", p_csq->cid) );

	CL_ASSERT( p_irp );
	CL_ASSERT( p_irp->Tail.Overlay.DriverContext[1] == NULL );

	p_irp->IoStatus.Status = status;
	if( status == STATUS_SUCCESS )
	{
		p_irp->IoStatus.Information = cl_ioctl_out_size( p_irp );
		IoCompleteRequest( p_irp, IO_NETWORK_INCREMENT );
	}
	else
	{
		p_irp->IoStatus.Information = 0;
		IoCompleteRequest( p_irp, 0 );
	}
	nd_csq_release( p_csq ); /* Release IRP reference */

	AL_EXIT( AL_DBG_NDI );
}


/*
 * Transition the QP to the error state to flush all oustanding work
 * requests and sets the timewait time.  This function may be called
 * when destroying the QP in order to flush all work requests, so we
 * cannot call through the main API, or the call will fail since the
 * QP is no longer in the initialize state.
 */
static void
__cep_timewait_qp(
	IN				nd_csq_t					*p_csq )
{
	uint64_t			timewait = 0;
	ib_qp_handle_t		h_qp;
	ib_qp_mod_t			qp_mod;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_csq != NULL );

	/*
	 * The CM should have set the proper timewait time-out value.  Reset
	 * the QP and let it enter the timewait state.
	 */
	if( al_cep_get_timewait( p_csq->h_al, p_csq->cid, &timewait ) == IB_SUCCESS )
	{
		h_qp = CONTAINING_RECORD(
			al_hdl_ref( p_csq->h_al, p_csq->h_qp, AL_OBJ_TYPE_H_QP ),
			ib_qp_t,
			obj );

		/* Special checks on the QP state for error handling - see above. */
		if( !h_qp || !AL_OBJ_IS_TYPE( h_qp, AL_OBJ_TYPE_H_QP ) ||
			( (h_qp->obj.state != CL_INITIALIZED) && 
			(h_qp->obj.state != CL_DESTROYING) ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
			return;
		}

		cl_memclr( &qp_mod, sizeof(ib_qp_mod_t) );
		qp_mod.req_state = IB_QPS_ERROR;

		/* Modify to error state using function pointers - see above. */
		status = h_qp->pfn_modify_qp( h_qp, &qp_mod, NULL );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("pfn_modify_qp to IB_QPS_ERROR returned %s\n",
				ib_get_err_str( status )) );
		}
		else
		{
			/* Store the timestamp after which the QP exits timewait. */
			h_qp->timewait = cl_get_time_stamp() + timewait;
		}
		deref_al_obj( &h_qp->obj );
	}

	AL_EXIT( AL_DBG_CM );
}

static ib_api_status_t
__ndi_qp2rts(
	IN		nd_csq_t*							p_csq,
	IN		PIRP								p_irp
	)
{
	ib_api_status_t status;
	ib_qp_handle_t h_qp;
	ib_qp_mod_t qp_mod;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI,
		("[ CID = %d\n", p_csq->cid) );

	h_qp = CONTAINING_RECORD(
		al_hdl_ref( p_csq->h_al, p_csq->h_qp, AL_OBJ_TYPE_H_QP ),
		ib_qp_t,
		obj );
	if( h_qp == NULL )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Bad QP %I64d\n", p_csq->h_qp) );
		status = IB_INVALID_HANDLE;
		goto err;
	}

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI,
		("QP %p state %d\n", h_qp, h_qp->state) );

	/* fill required qp attributes */
	status = al_cep_get_rtr_attr( p_csq->h_al, p_csq->cid, &qp_mod );
	if ( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_get_rtr_attr for CID %d returned %s\n",
			p_csq->cid, ib_get_err_str( status )) );
		goto exit;
	}

	/* perform the request: INIT->RTR */
	status = ndi_modify_qp( h_qp, &qp_mod, 
		cl_ioctl_out_size( p_irp ), cl_ioctl_out_buf( p_irp ) );
	if ( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ndi_modify_qp %p from %d to RTR returned %s.\n",
			h_qp, h_qp->state, ib_get_err_str(status) ) );
		goto exit;
	}

	/* fill required qp attributes */
	status = al_cep_get_rts_attr( p_csq->h_al, p_csq->cid, &qp_mod );
	if ( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_get_rts_attr for CID %d returned %s\n",
			p_csq->cid, ib_get_err_str( status )) );
		goto exit;
	}

	/* perform the request: RTR->RTS */
	status = ndi_modify_qp( h_qp, &qp_mod, 
		cl_ioctl_out_size( p_irp ), cl_ioctl_out_buf( p_irp ) );
	if ( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ndi_modify_qp %p from %d to RTS returned %s.\n",
			h_qp, h_qp->state, ib_get_err_str(status) ) );
	}

exit:
	deref_al_obj( &h_qp->obj );
err:
	AL_EXIT( AL_DBG_NDI );
	return status;
}


/*******************************************************************
 *
 * CSQ
 *
 ******************************************************************/
#ifdef NTDDI_WIN8
static IO_CSQ_INSERT_IRP_EX __ndi_insert_irp_ex;
#endif

static NTSTATUS __ndi_insert_irp_ex(
	IN	PIO_CSQ									pCsq,
	IN	PIRP									pIrp,
	IN	VOID									*Context
	)
{
	NTSTATUS status;
	nd_csq_t *p_ndi_csq = CONTAINING_RECORD( pCsq, nd_csq_t, csq );

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI, ("[ CID = %d\n", p_ndi_csq->cid) );
	switch( (ULONG_PTR)Context )
	{
	case NDI_CM_LISTEN:
		status = __ndi_get_req( p_ndi_csq, pIrp );
		break;

	case NDI_CM_CONNECTING_QPR_SENT:
		status = __ndi_pr_query( pIrp );
        if( STATUS_SUCCESS != status )
        {
		break;
        }
        Context = (VOID*)NDI_CM_CONNECTING_REQ_SENT;
        __fallthrough;

	case NDI_CM_CONNECTING_REQ_SENT:
		status = __ndi_send_req( pIrp );
		break;

	case NDI_CM_DISCONNECTING:
		status = __ndi_send_dreq( pIrp );
		break;

	case NDI_CM_CONNECTED_DREQ_RCVD:
		if( p_ndi_csq->state == NDI_CM_LISTEN )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_NDI,
				("] Invalid state (%d).\n", p_ndi_csq->state) );
			return STATUS_INVALID_DEVICE_REQUEST;
		}

		/*
		 * Overwrite the context so that the state change
		 * below turns into a noop.
		 */
		Context = (VOID*)(ULONG_PTR)p_ndi_csq->state;
		status = STATUS_PENDING;
		break;

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		ASSERT( FALSE );
	}

	ASSERT( status == STATUS_PENDING || !NT_SUCCESS( status ) );
	if( NT_SUCCESS( status ) )
	{
		AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI, ("] Queueing IRP\n") );
		p_ndi_csq->state = (ndi_cm_state_t)(ULONG_PTR)Context;
		InsertTailList( &p_ndi_csq->queue, &pIrp->Tail.Overlay.ListEntry );
		nd_csq_ref( p_ndi_csq ); /* Take IRP reference. */
	}
	AL_EXIT( AL_DBG_NDI );
	return status;
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
    ULONG_PTR code;
	PIRP nextIrp = NULL;
	PLIST_ENTRY nextEntry;
	PLIST_ENTRY listHead;
	nd_csq_t *p_ndi_csq = (nd_csq_t*)Csq;

	AL_ENTER( AL_DBG_NDI );

	listHead = &p_ndi_csq->queue;

	// 
	// If the IRP is NULL, we will start peeking from the listhead, else
	// we will start from that IRP onwards. This is done under the
	// assumption that new IRPs are always inserted at the tail.
	//

	if(Irp == NULL)
    {
		nextEntry = listHead->Flink;
    }
	else
    {
		nextEntry = Irp->Tail.Overlay.ListEntry.Flink;
    }


		//
    // We will return the next irp that matches the following criteria:
    //  1.  Has IOCTL that matches the value in context
    //      This is the normal case where we pull an expected item from the queue
    //  2.  No context specified and Irp does not have IOCTL code UAL_NDI_NOTIFY_DREQ
    //      We special case the DREQ notify and don't remove it until it actually
    //      happens (hits case 2).  This allows the client to post the pending IO
    //      and it will complete when the disconnect happens.
		//
    while(nextEntry != listHead)
    {
        nextIrp = CONTAINING_RECORD(nextEntry, IRP, Tail.Overlay.ListEntry);

        code = cl_ioctl_ctl_code( nextIrp );
        CL_ASSERT( code != 0 );

        if( (ULONG_PTR)PeekContext == code )
		{
				break;
		}
        else if( NULL == PeekContext && code != UAL_NDI_NOTIFY_DREQ )
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
#endif


#ifdef NTDDI_WIN8
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
//TOD __drv_at(p_ndi_csq->lock, __drv_acquiresExclusiveResource(KSPIN_LOCK))
__drv_at(pIrql, __drv_savesIRQL)
#endif
#pragma prefast(suppress: 28167, "The irql level is saved by cl_spinlock_acquire, impossible to annotate by driver directives")
static VOID __ndi_acquire_lock(
	IN	PIO_CSQ									Csq,
	OUT	PKIRQL									pIrql
	)
{
	nd_csq_t *p_ndi_csq = (nd_csq_t*)Csq;

	KeAcquireSpinLock( &p_ndi_csq->lock, pIrql );
}


#ifdef NTDDI_WIN8
static IO_CSQ_RELEASE_LOCK __ndi_release_lock;
#endif

#ifdef NTDDI_WIN8
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
//TODO __drv_at(p_ndi_csq->lock, __drv_restoresExclusiveResource(KSPIN_LOCK))
__drv_at(Irql, __drv_restoresIRQL)
#endif
#pragma prefast(suppress: 28167, "The irql level is restored by KeReleaseSpinLock, impossible to annotate by driver directives")
static VOID __ndi_release_lock(
	IN	PIO_CSQ									Csq,
	IN	KIRQL									Irql
	)
{
	nd_csq_t *p_ndi_csq = (nd_csq_t*)Csq;

	KeReleaseSpinLock( &p_ndi_csq->lock, Irql );
}

#ifdef NTDDI_WIN8
static IO_CSQ_COMPLETE_CANCELED_IRP __ndi_complete_cancelled_irp;
#endif

static VOID __ndi_complete_cancelled_irp(
	IN	PIO_CSQ									Csq,
	IN	PIRP									p_irp
	)
{
	nd_csq_t *p_ndi_csq = (nd_csq_t*)Csq;
	KIRQL irql;
	ib_query_handle_t h_query;

	AL_ENTER( AL_DBG_NDI );

	switch( cl_ioctl_ctl_code( p_irp ) )
	{
	case UAL_NDI_REQ_CM:
		__ndi_acquire_lock( Csq, &irql );
		/*
		 * Note that al_cancel_sa_req is synchronized with any potential
		 * SA callback by the CSQ lock.
		 */
#pragma warning( disable:4305 )
		h_query = InterlockedExchangePointer( &p_ndi_csq->h_query, NULL );
#pragma warning( default:4305 )
		if( h_query != NULL )
			al_cancel_sa_req( &h_query->sa_req );

		if( p_ndi_csq->state != NDI_CM_INVALID )
			p_ndi_csq->state = NDI_CM_IDLE;

		__ndi_release_lock( Csq, irql );

		__fallthrough;

	case UAL_NDI_NOTIFY_DREQ:
	case UAL_NDI_GET_REQ_CM:
		__ndi_complete_irp( p_ndi_csq, p_irp, STATUS_CANCELLED );
		break;

	case UAL_NDI_DREQ_CM:
		__ndi_queue_drep( p_irp );
		break;
	}

	AL_EXIT( AL_DBG_NDI );
}


NTSTATUS
nd_csq_init(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				uint64_t					h_qp,
		OUT			nd_csq_t					**pp_csq )
{
	nd_csq_t *p_nd_csq;
	NTSTATUS status;
	ib_api_status_t ib_status;

	AL_ENTER( AL_DBG_NDI );

	p_nd_csq = (nd_csq_t*)cl_zalloc( sizeof(*p_nd_csq) );
	if( p_nd_csq == NULL )
	{
		status = STATUS_NO_MEMORY;
		goto exit;
	}

	KeInitializeSpinLock( &p_nd_csq->lock );
	InitializeListHead( &p_nd_csq->queue );
	p_nd_csq->h_al = h_al;
	p_nd_csq->h_qp = h_qp;
	p_nd_csq->h_query = NULL;
	p_nd_csq->state = NDI_CM_IDLE;
	p_nd_csq->cid = cid;

	status = IoCsqInitializeEx( &p_nd_csq->csq, 
		__ndi_insert_irp_ex, __ndi_remove_irp,
		__ndi_peek_next_irp, __ndi_acquire_lock,
		__ndi_release_lock, __ndi_complete_cancelled_irp );
	if ( !NT_SUCCESS( status ) )
	{
		cl_free( p_nd_csq );
		goto exit;
	}

	/*
	 * One reference for the CEP, one for the caller (so that if the CEP
	 * gets destroyed we don't blow up.)
	 */
	p_nd_csq->ref_cnt = 2;

	ib_status = kal_cep_config(
		h_al, cid, nd_cm_handler, p_nd_csq, nd_csq_release );

	if( ib_status != IB_SUCCESS )
	{
		status = STATUS_UNSUCCESSFUL;
		cl_free( p_nd_csq );
		goto exit;
	}

	*pp_csq = p_nd_csq;
	status = STATUS_SUCCESS;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
		("Creating CSQ %p, uhdl %#I64x \n", p_nd_csq, h_qp) );

exit:
	AL_EXIT( AL_DBG_NDI );
	return status;
}


void
ndi_cancel_cm_irps(
	IN				nd_csq_t					*p_nd_csq )
{
	PIRP Irp;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI,
		("[ CSQ %p (CID = %d)\n", 
		p_nd_csq, p_nd_csq->cid ) );

	/* cancel pending IRPS for NDI type CQ */
	AL_ENTER( AL_DBG_NDI );
	for( Irp = IoCsqRemoveNextIrp( &p_nd_csq->csq, NULL );
		Irp != NULL;
		Irp = IoCsqRemoveNextIrp( &p_nd_csq->csq, NULL ) )
	{
		__ndi_complete_cancelled_irp( &p_nd_csq->csq, Irp );
	}
	for( Irp = IoCsqRemoveNextIrp( &p_nd_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_NOTIFY_DREQ );
		Irp != NULL;
		Irp = IoCsqRemoveNextIrp( &p_nd_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_NOTIFY_DREQ ) )
	{
		__ndi_complete_cancelled_irp( &p_nd_csq->csq, Irp );
	}

	AL_EXIT( AL_DBG_NDI );
}


void
nd_csq_destroy(
	IN				nd_csq_t					*p_nd_csq )
{
	KIRQL irql;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI,
		("[ CSQ %p (CID = %d)\n", 
		p_nd_csq, p_nd_csq->cid ) );

	/* Move the state before flushing, so that all new IRPs fail to queue. */
	__ndi_acquire_lock( &p_nd_csq->csq, &irql );
	p_nd_csq->state = NDI_CM_INVALID;
	__ndi_release_lock( &p_nd_csq->csq, irql );

	/* cancel pending IRPS */
	ndi_cancel_cm_irps( p_nd_csq );

	cl_free( p_nd_csq );

	AL_EXIT( AL_DBG_NDI );
}


void
nd_csq_ref( nd_csq_t* p_csq )
{
	InterlockedIncrement( &p_csq->ref_cnt );
}


void
nd_csq_release( nd_csq_t* p_csq )
{
	if( InterlockedDecrement( &p_csq->ref_cnt ) == 0 )
	{
		nd_csq_destroy( p_csq );
	}
}


static inline void
__ndi_timeout_req_irp(
	__in nd_csq_t* p_csq )
{
	PIRP Irp;
	KIRQL irql;

	AL_ENTER( AL_DBG_NDI );
	Irp = IoCsqRemoveNextIrp( &p_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_REQ_CM );
	if( Irp )
	{
		__ndi_acquire_lock( &p_csq->csq, &irql );
		if( p_csq->state != NDI_CM_INVALID )
			p_csq->state = NDI_CM_IDLE;
		__ndi_release_lock( &p_csq->csq, irql );
		__ndi_complete_irp( p_csq, Irp, STATUS_TIMEOUT );
	}
	AL_EXIT( AL_DBG_NDI );
}

/*******************************************************************
 *
 * REQ CM request
 *
 ******************************************************************/

static void
__ndi_notify_dreq(
	__in nd_csq_t* p_csq )
{
	IRP *p_irp;
	do
	{
		p_irp = IoCsqRemoveNextIrp(
			&p_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_NOTIFY_DREQ );

		if( p_irp )
		{
			__ndi_complete_irp( p_csq, p_irp, STATUS_SUCCESS );
		}

	} while ( p_irp );
}


static void
__ndi_proc_dreq(
	IN				nd_csq_t					*p_csq)
{
	IRP *p_irp;
	KIRQL irql;

	__ndi_notify_dreq( p_csq );

	__ndi_acquire_lock( &p_csq->csq, &irql );
	if( p_csq->state == NDI_CM_CONNECTED )
	{
		p_csq->state = NDI_CM_CONNECTED_DREQ_RCVD;
	}
	__ndi_release_lock( &p_csq->csq, irql );

	p_irp = IoCsqRemoveNextIrp(
		&p_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_DREQ_CM );
	if( p_irp != NULL )
	{
		__ndi_queue_drep( p_irp );
	}
}


/*
 * A user-specified callback that is invoked after receiving a connection
 * rejection message (REJ).
 */
static void
__ndi_proc_rej(
	IN				nd_csq_t*					p_csq,
	IN		const	mad_cm_rej_t* const			p_rej )
{
	KIRQL irql;
	IRP* p_irp;

	AL_ENTER( AL_DBG_NDI );

	AL_PRINT(TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
		("p_rej %p, CID=%d, uhdl %#I64x, connect reject, reason=%hd\n", 
		p_rej, p_csq->cid, p_csq->h_qp, cl_ntoh16(p_rej->reason) ) );

	p_irp = IoCsqRemoveNextIrp( &p_csq->csq, NULL );
	__ndi_notify_dreq( p_csq );
	__ndi_acquire_lock( &p_csq->csq, &irql );
	if( p_irp != NULL )
	{
		switch( cl_ioctl_ctl_code( p_irp ) )
		{
		case UAL_NDI_REQ_CM:
			if( p_csq->state != NDI_CM_INVALID )
				p_csq->state = NDI_CM_IDLE;
			if( p_rej->reason == IB_REJ_USER_DEFINED )
				__ndi_complete_irp( p_csq, p_irp, STATUS_CONNECTION_REFUSED );
			else
				__ndi_complete_irp( p_csq, p_irp, STATUS_TIMEOUT );

			/* We leave the CEP active so that the private data can be retrieved. */
			break;

		case UAL_NDI_DREQ_CM:
			__ndi_queue_drep( p_irp );
			break;

		case UAL_NDI_NOTIFY_DREQ:
			__ndi_complete_irp( p_csq, p_irp, STATUS_CONNECTION_ABORTED );
			break;

		default:
			ASSERT( cl_ioctl_ctl_code( p_irp ) == UAL_NDI_REQ_CM ||
				cl_ioctl_ctl_code( p_irp ) == UAL_NDI_DREQ_CM ||
				cl_ioctl_ctl_code( p_irp ) == UAL_NDI_NOTIFY_DREQ );
		}
	}
	else if( p_csq->state == NDI_CM_CONNECTED || p_csq->state == NDI_CM_CONNECTING_REQ_RCVD )
	{
		p_csq->state = NDI_CM_CONNECTED_DREQ_RCVD;
	}
	__ndi_release_lock( &p_csq->csq, irql );

	AL_EXIT( AL_DBG_NDI );
}


static void
__ndi_proc_req(
	IN				nd_csq_t*					p_csq,
	IN				net32_t						new_cid,
	IN				ib_mad_element_t			*p_mad )
{
	IRP* p_irp;
	KIRQL irql;
	NTSTATUS status;
	nd_csq_t* p_new_csq;

	AL_ENTER( AL_DBG_NDI );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI ,("CID = %d\n", p_csq->cid));

	status = nd_csq_init( p_csq->h_al, new_cid, 0, &p_new_csq );
	if( status != STATUS_SUCCESS )
	{
		// Ignore the request.
		kal_cep_destroy( p_csq->h_al, new_cid, STATUS_NO_MORE_ENTRIES );
		ib_put_mad( p_mad );
		return;
	}

	__ndi_acquire_lock( &p_new_csq->csq, &irql );
	p_new_csq->state = NDI_CM_CONNECTING_REQ_RCVD;
	__ndi_release_lock( &p_new_csq->csq, irql );
	nd_csq_release( p_new_csq );

	p_irp = IoCsqRemoveNextIrp( &p_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_GET_REQ_CM );
	if( p_irp == NULL )
	{
		p_mad->send_context1 = (VOID*)(ULONG_PTR)new_cid;
		__ndi_acquire_lock( &p_csq->csq, &irql );
		if( p_csq->p_mad_head == NULL )
		{
			p_csq->p_mad_head = p_mad;
		}
		else
		{
			p_csq->p_mad_tail->p_next = p_mad;
		}
		p_csq->p_mad_tail = p_mad;
		__ndi_release_lock( &p_csq->csq, irql );
	}
	else
	{
		*(net32_t*)cl_ioctl_out_buf( p_irp ) = new_cid;
		__ndi_complete_irp( p_csq, p_irp, STATUS_SUCCESS );
		ib_put_mad( p_mad );
	}

	AL_EXIT( AL_DBG_NDI );
	return;
}


static void
__ndi_proc_rep(
	IN				nd_csq_t*					p_csq )
{
	IRP* p_irp;
	KIRQL irql;

	AL_ENTER( AL_DBG_NDI );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI ,("CID = %d\n", p_csq->cid));

	p_irp = IoCsqRemoveNextIrp( &p_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_REQ_CM );
	__ndi_acquire_lock( &p_csq->csq, &irql );
	if( p_irp == NULL )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("Not the expected state %s\n", State2String( p_csq->state )));
		CL_ASSERT( IsListEmpty( &p_csq->queue ) );
		al_cep_rej( p_csq->h_al, p_csq->cid, IB_REJ_INVALID_COMM_INSTANCE, NULL, 0, NULL, 0 );
	}
	else
	{
		p_csq->state = NDI_CM_CONNECTING_REP_RCVD;

		__ndi_complete_irp( p_csq, p_irp, STATUS_SUCCESS );
	}
	__ndi_release_lock( &p_csq->csq, irql );

	AL_EXIT( AL_DBG_NDI );
	return;
}

IO_WORKITEM_ROUTINE __ndi_do_drep;
void
__ndi_do_drep(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				PIRP						p_irp )
{
	nd_csq_t* p_csq = p_irp->Tail.Overlay.DriverContext[0];
	ib_qp_handle_t h_qp;
	ib_qp_mod_t qp_mod;
	ib_api_status_t status;
	uint64_t timewait_us;
	KIRQL irql;
	NTSTATUS nt_status;

	UNREFERENCED_PARAMETER( p_dev_obj );

	AL_ENTER( AL_DBG_NDI );

	CL_ASSERT( p_irp->Tail.Overlay.DriverContext[1] );
	IoFreeWorkItem( p_irp->Tail.Overlay.DriverContext[1] );
	p_irp->Tail.Overlay.DriverContext[1] = NULL;

	status = al_cep_get_timewait( p_csq->h_al, p_csq->cid, &timewait_us );
	if (status != IB_SUCCESS)
	{
		nt_status = STATUS_CONNECTION_INVALID;
		goto exit;
	}

	/* Store the timestamp after which the QP exits timewait. */
	h_qp = CONTAINING_RECORD(
		al_hdl_ref( p_csq->h_al, p_csq->h_qp, AL_OBJ_TYPE_H_QP ),
		ib_qp_t,
		obj );
	if( h_qp != NULL )
	{
		h_qp->timewait = cl_get_time_stamp() + timewait_us;
	}

	/* Send the DREP. */
	al_cep_drep( p_csq->h_al, p_csq->cid, NULL, 0 );

	/* bring QP to error state */
	if( h_qp != NULL )
	{
		cl_memclr( &qp_mod, sizeof(qp_mod) );
		qp_mod.req_state = IB_QPS_ERROR;
		
		status = ndi_modify_qp( h_qp, &qp_mod, 
			cl_ioctl_out_size( p_irp ), cl_ioctl_out_buf( p_irp ) );
		if ( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ndi_modify_qp to ERROR returned %s.\n", ib_get_err_str(status) ) );
		}
		deref_al_obj( &h_qp->obj );
	}

	nt_status = ib_to_ntstatus( status );

exit:
	__ndi_acquire_lock( &p_csq->csq, &irql );
	if( p_csq->state != NDI_CM_INVALID )
		p_csq->state = NDI_CM_IDLE;
	__ndi_release_lock( &p_csq->csq, irql );

	__ndi_complete_irp( p_csq, p_irp, nt_status );
	nd_csq_release( p_csq ); /* Release work item reference. */
	AL_EXIT( AL_DBG_NDI );
}


static void
__ndi_queue_drep(
	IN				IRP							*p_irp )
{
	CL_ASSERT( p_irp->Tail.Overlay.DriverContext[1] != NULL );
	IoQueueWorkItem( p_irp->Tail.Overlay.DriverContext[1],
		__ndi_do_drep, DelayedWorkQueue, p_irp );
}


static void
__ndi_proc_drep(
	IN				nd_csq_t*					p_csq )
{
	IRP* p_irp;

	AL_ENTER( AL_DBG_NDI );

	p_irp = IoCsqRemoveNextIrp(
		&p_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_DREQ_CM );
	if( p_irp != NULL )
	{
		CL_ASSERT( p_irp->Tail.Overlay.DriverContext[0] == p_csq );
		__ndi_queue_drep( p_irp );
	}

	AL_EXIT( AL_DBG_NDI );
}


void
nd_cm_handler(
	IN		const	ib_al_handle_t				h_al,
	IN		const	net32_t						cid )
{
	void*				context;
	net32_t				new_cid;
	ib_mad_element_t	*p_mad_el;

	AL_ENTER( AL_DBG_NDI );

	while( al_cep_poll( h_al, cid, &context, &new_cid, &p_mad_el ) == IB_SUCCESS )
	{
		ib_mad_t* p_mad = ib_get_mad_buf( p_mad_el );
		nd_csq_t* p_csq = (nd_csq_t*)context;

		CL_ASSERT( p_csq != NULL );
		CL_ASSERT( p_csq->cid == cid );

		if( p_mad_el->status != IB_SUCCESS )
		{
			switch( p_mad->attr_id )
			{
			case CM_REQ_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("REQ timed out for CEP with cid %d, h_al %p, context %p.\n", 
					cid, h_al, p_csq ) );
				__ndi_timeout_req_irp( p_csq );
				break;

			case CM_REP_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("REP timed out for CEP with cid %d, h_al %p, context %p.\n", 
					cid, h_al, p_csq ) );
				break;

			case CM_DREQ_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("DREQ timed out for CEP with cid %d, h_al %p, context %p.\n", 
					cid, h_al, p_csq ) );
				__ndi_proc_drep( p_csq );
				break;

			default:
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("Unhandled failed MAD attr ID %d for CEP with cid %d, h_al %p, context %p, new_cid %d .\n", 
					p_mad->attr_id, cid, h_al, p_csq, new_cid ) );
				break;
			}
		}
		else
		{
			switch( p_mad->attr_id )
			{
			case CM_REQ_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("REQ received for CEP with cid %d, h_al %p, context %p.\n", 
					cid, h_al, p_csq ) );
				__ndi_proc_req( p_csq, new_cid, p_mad_el );
				continue;

			case CM_REP_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("REP received for CEP with cid %d, h_al %p, context %p.\n", 
					cid, h_al, p_csq ) );
				__ndi_proc_rep( p_csq );
				break;
			
			case CM_REJ_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("REJ received for CEP with cid %d, h_al %p, context %p.\n", 
					cid, h_al, p_csq ) );
				__ndi_proc_rej( p_csq, (mad_cm_rej_t*)p_mad );
				break;
		
			case CM_DREQ_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("DREQ received for CEP with cid %d, h_al %p, context %p.\n", 
					cid, h_al, p_csq ) );
				__ndi_proc_dreq( p_csq );
				break;

			case CM_DREP_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("DREP received for CEP with cid %d, h_al %p, context %p.\n", 
					cid, h_al, p_csq ) );
				__ndi_proc_drep( p_csq );
				break;

			case CM_RTU_ATTR_ID:
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
					("RTU received for CEP with cid %d, h_al %p, context %p.\n",
					cid, h_al, p_csq ) );
				break;

			default:
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("Unhandled MAD attr ID %d for CEP with cid %d, h_al %p, context %p, new_cid %d .\n", 
					p_mad->attr_id, cid, h_al, p_csq, new_cid ) );
			}
		}

		ib_put_mad( p_mad_el );
	}

	AL_EXIT( AL_DBG_NDI );
}


static void
__ndi_fill_cm_req(
	IN		net32_t								qpn,
	IN		ual_ndi_req_cm_ioctl_in_t			*p_req,
	IN		ib_path_rec_t						*p_path_rec,
		OUT	iba_cm_req							*p_cm_req)
{
	AL_ENTER( AL_DBG_NDI );

	memset( p_cm_req, 0, sizeof(*p_cm_req) );

	p_cm_req->service_id = ib_cm_rdma_sid( p_req->prot, p_req->dst_port );
	p_cm_req->p_primary_path = p_path_rec;

	p_cm_req->qpn = qpn;
	p_cm_req->qp_type = IB_QPT_RELIABLE_CONN;
	p_cm_req->starting_psn = qpn;

	p_cm_req->p_pdata = (uint8_t *)&p_req->pdata;
	p_cm_req->pdata_len = sizeof(p_req->pdata);

	p_cm_req->max_cm_retries = g_max_cm_retries;
	p_cm_req->resp_res = p_req->resp_res;
	p_cm_req->init_depth = p_req->init_depth;

	p_cm_req->remote_resp_timeout =
		ib_path_rec_pkt_life( p_path_rec ) + CM_REMOTE_TIMEOUT;
	if( p_cm_req->remote_resp_timeout > 0x1F )
		p_cm_req->remote_resp_timeout = 0x1F;
	else if( p_cm_req->remote_resp_timeout < CM_MIN_REMOTE_TIMEOUT )
		p_cm_req->remote_resp_timeout = CM_MIN_REMOTE_TIMEOUT;

	p_cm_req->flow_ctrl = TRUE;	/* HCAs must support end-to-end flow control. */

	p_cm_req->local_resp_timeout =
		ib_path_rec_pkt_life( p_path_rec ) + CM_LOCAL_TIMEOUT;
	if( p_cm_req->local_resp_timeout > 0x1F )
		p_cm_req->local_resp_timeout = 0x1F;
	else if( p_cm_req->local_resp_timeout < CM_MIN_LOCAL_TIMEOUT )
		p_cm_req->local_resp_timeout = CM_MIN_LOCAL_TIMEOUT;

	p_cm_req->rnr_retry_cnt = QP_ATTRIB_RNR_RETRY;
	p_cm_req->retry_cnt = g_qp_retries;

	AL_EXIT( AL_DBG_NDI );
}


NTSTATUS
__ndi_send_req(
	IN		IRP*								p_irp
	)
{
    uint8_t pkt_life;
	ib_api_status_t status;
	nd_csq_t* p_csq = (nd_csq_t*)p_irp->Tail.Overlay.DriverContext[0];
	ual_ndi_req_cm_ioctl_in_t *p_req = 
		(ual_ndi_req_cm_ioctl_in_t*)cl_ioctl_in_buf( p_irp );
	NTSTATUS nt_status;
	ib_qp_handle_t h_qp;
	iba_cm_req cm_req;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI,
		("[ CID = %d, h_al %p, context %p\n",
		p_req->cid, p_csq->h_al, p_csq) );

	if( p_csq->state != NDI_CM_CONNECTING_QPR_SENT &&
		p_csq->state != NDI_CM_IDLE )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Unexpected state: %d\n", p_csq->state) );
		return STATUS_CONNECTION_ACTIVE;
	}

	h_qp = CONTAINING_RECORD(
		al_hdl_ref( p_csq->h_al, p_req->h_qp, AL_OBJ_TYPE_H_QP ),
		ib_qp_t,
		obj );
	if( !h_qp )
	{
		/* The QP was valid when the IOCTL first came in... */
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid QP: %I64d\n", p_req->h_qp) );
		return STATUS_CONNECTION_ABORTED;
	}

    //
    // Fixup the packet life
    //
    pkt_life = ib_path_rec_pkt_life( &p_req->path ) + g_pkt_life_modifier;
    if( pkt_life > 0x1F )
    {
        pkt_life = 0x1F;
    }
    p_req->path.pkt_life &= IB_PATH_REC_SELECTOR_MASK;
    p_req->path.pkt_life |= pkt_life;

	/* Format ib_cm_req_t structure */
    __ndi_fill_cm_req( h_qp->num, p_req, &p_req->path, &cm_req );
	deref_al_obj( &h_qp->obj );

	/* prepare CEP for connection */
	status = kal_cep_pre_req(
		p_csq->h_al, p_csq->cid, &cm_req, QP_ATTRIB_RNR_NAK_TIMEOUT, NULL );
	if( status != STATUS_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_pre_req returned %s.\n", ib_get_err_str( status )) );
		goto error;
	}

	/* send CM REQ */
	status = al_cep_send_req( p_csq->h_al, p_csq->cid );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_send_req returned %s.\n", ib_get_err_str( status )) );
		goto error;
	}

	/* SUCCESS ! */
	AL_EXIT( AL_DBG_NDI );
	return STATUS_PENDING;

error:
	switch( status )
	{
	case IB_INVALID_HANDLE:
		nt_status = STATUS_CANCELLED;
		break;

	case IB_INVALID_STATE:
		nt_status = STATUS_CONNECTION_ABORTED;
		break;

	default:
		nt_status = ib_to_ntstatus( status );
	}

	p_csq->state = NDI_CM_IDLE;
	AL_EXIT( AL_DBG_NDI );
	return nt_status;
}


static void
__ndi_pr_query_cb(
    __in VOID* pCompletionContext,
    __in NTSTATUS status,
    __in ib_path_rec_t* const pPath
    )
{
	cl_ioctl_handle_t p_irp;
    ual_ndi_req_cm_ioctl_in_t *p_req;
    nd_csq_t* p_csq = (nd_csq_t*)pCompletionContext;

	KIRQL irql;

	AL_ENTER( AL_DBG_NDI );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
        ("PR Query Async: status is %d, context %p\n", status, pCompletionContext) );

	p_irp = IoCsqRemoveNextIrp( &p_csq->csq, (VOID*)(ULONG_PTR)UAL_NDI_REQ_CM );
	if( p_irp == NULL )
	{
		goto exit;
	}

    if( status != IB_SUCCESS )
	{
		__ndi_acquire_lock( &p_csq->csq, &irql );
		if( p_csq->state != NDI_CM_INVALID )
		{
            p_csq->state = NDI_CM_IDLE;
		}
        __ndi_release_lock( &p_csq->csq, irql );
		__ndi_complete_irp( p_csq, p_irp, status );
		goto exit;
	}

    p_req = (ual_ndi_req_cm_ioctl_in_t*)cl_ioctl_in_buf( p_irp );

    RtlCopyMemory(
        &p_req->path,
        pPath,
        sizeof(*pPath)
        );

	status = IoCsqInsertIrpEx(
		&p_csq->csq,
		p_irp,
		NULL,
		(VOID*)(ULONG_PTR)NDI_CM_CONNECTING_REQ_SENT
		);
	if( !NT_SUCCESS( status ) )
	{
		__ndi_complete_irp( p_csq, p_irp, status );
	}
	else
	{
		/*
		 * Release the previous reference because IoCsqInsertIrpEx
		 * took a new one.
		 */
		nd_csq_release( p_csq ); /* Release IRP reference. */
	}


exit:
	nd_csq_release( p_csq );	/* release path query reference */
	AL_EXIT( AL_DBG_NDI );
}



/*
 * Send asynchronous query to the SA for a path record.
 *
 * Called from the __ndi_insert_irp_ex function, so the CSQ lock is held.
 */
NTSTATUS
__ndi_pr_query(
	IN		IRP*								p_irp
	)
{
	ib_api_status_t status;
	ual_ndi_req_cm_ioctl_in_t *p_req = 
		(ual_ndi_req_cm_ioctl_in_t*)cl_ioctl_in_buf( p_irp );
	nd_csq_t* p_csq = (nd_csq_t*)p_irp->Tail.Overlay.DriverContext[0];

	AL_ENTER( AL_DBG_NDI );

	if ( p_csq->state != NDI_CM_IDLE )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("STATUS_CONNECTION_ACTIVE: CID=%d, uhdl %#I64x, ref_cnt %d\n",
			p_csq->cid, p_csq->h_qp, p_csq->ref_cnt ) );
		return STATUS_CONNECTION_ACTIVE;
	}


    nd_csq_ref( p_csq );        /* take path query reference */

    status = IbatQueryPathByPhysicalAddress(
        p_req->src_mac,
        p_req->dest_mac,
        __ndi_pr_query_cb,
        p_csq,
        &p_req->path
        );

    if( !NT_SUCCESS( status ) )
	{
		p_csq->state = NDI_CM_IDLE;
        AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IbatQueryPathByPhysicalAddress failed (%d)\n", status) );
		nd_csq_release( p_csq );	/* release path query reference */
	}
    else if( STATUS_PENDING != status )
    {
        CL_ASSERT( STATUS_SUCCESS == status );

        AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
        ("PR Query Sync: status is %d\n", status ) );
        nd_csq_release( p_csq );    /* release path query reference */
    }
	AL_EXIT( AL_DBG_NDI );
    return status;
}


static inline void
NdInitializeSockAddrInet(
    __out SOCKADDR_INET*            pAddr,
    __in    uint8_t                 ipVersion,
    __in_ecount(4)const uint32_t*   pBuffer
    )
{
    switch( ipVersion )
    {
    case IB_REQ_CM_RDMA_IPV6:
        {
            pAddr->Ipv6.sin6_family = AF_INET6;
            RtlCopyMemory(
                &pAddr->Ipv6.sin6_addr,
                pBuffer,
                sizeof(pAddr->Ipv6.sin6_addr)
                );
        }
        break;
    case IB_REQ_CM_RDMA_IPV4:
        {
            pAddr->Ipv4.sin_family = AF_INET;
            pAddr->Ipv4.sin_addr.S_un.S_addr = pBuffer[3];
        }
        break;
    default:
        {
            pAddr->si_family = AF_UNSPEC;
        }
        break;
    }
}


static inline NTSTATUS
NdResolvePhysicalAddresses(
    ual_ndi_req_cm_ioctl_in_t* pReq
    )
{
    SOCKADDR_INET localAddress;
    SOCKADDR_INET remoteAddress;

    NdInitializeSockAddrInet(&localAddress, pReq->pdata.ipv, pReq->pdata.src_ip_addr);
    NdInitializeSockAddrInet(&remoteAddress, pReq->pdata.ipv, pReq->pdata.dst_ip_addr);

    return IbatResolvePhysicalAddress(
        &localAddress,
        &remoteAddress,
        &pReq->src_mac,
        &pReq->dest_mac
        );
}


NTSTATUS
ndi_req_cm(
	IN		ib_al_handle_t						h_al,
	IN		IRP									*p_irp
	)
{
	NTSTATUS status;
	nd_csq_t* p_csq;
	ual_ndi_req_cm_ioctl_in_t *p_req = 
		(ual_ndi_req_cm_ioctl_in_t*)cl_ioctl_in_buf( p_irp );

	AL_ENTER( AL_DBG_NDI );

    //
    // Before we can build the ND Path, we need to resolve the local port
    //  and remote mac address inforation.  We do that here because the
    //  ARP calls require IRQL < dispatch, but once inside the Csq callbacks,
    //  we will be running IRQL == dispatch.
    //
    status = NdResolvePhysicalAddresses(p_req);
    if( !NT_SUCCESS( status ) )
    {
        goto err;
    }

	p_csq = kal_cep_get_context( h_al, p_req->cid, nd_cm_handler, nd_csq_ref );
	if( p_csq == NULL )
	{
		status = nd_csq_init( h_al, p_req->cid, p_req->h_qp, &p_csq );
		if( status != STATUS_SUCCESS )
        {
			goto err;
        }
	}

	p_irp->Tail.Overlay.DriverContext[0] = p_csq;

		status = IoCsqInsertIrpEx(
			&p_csq->csq,
			p_irp,
			NULL,
			(VOID*)(ULONG_PTR)NDI_CM_CONNECTING_QPR_SENT
			);

	nd_csq_release( p_csq );
err:
	AL_EXIT( AL_DBG_NDI );
	return status;
}


/*******************************************************************
 *
 * RTU CM request
 *
 ******************************************************************/
	

static IO_WORKITEM_ROUTINE __ndi_do_rtu;
static void
__ndi_do_rtu(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				PIRP						p_irp )
{
	ib_api_status_t status;
	nd_csq_t* p_csq = p_irp->Tail.Overlay.DriverContext[0];
	KIRQL irql;
	NTSTATUS nt_status;

	UNUSED_PARAM(p_dev_obj);

	AL_ENTER( AL_DBG_NDI );

	/* free the work item if any */
	if ( p_irp->Tail.Overlay.DriverContext[1] )
	{
		IoFreeWorkItem( p_irp->Tail.Overlay.DriverContext[1] );
		p_irp->Tail.Overlay.DriverContext[1] = NULL;
		nd_csq_release( p_csq ); /* Release work item reference. */
	}

	__ndi_acquire_lock( &p_csq->csq, &irql );
	if( p_csq->state != NDI_CM_CONNECTING_REP_RCVD )
	{
		__ndi_release_lock( &p_csq->csq, irql );
		nt_status = STATUS_CONNECTION_ABORTED;
		goto exit;
	}
	__ndi_release_lock( &p_csq->csq, irql );

	/* change the QP state to RTS */
	status = __ndi_qp2rts( p_csq, p_irp );
	if ( status != IB_SUCCESS )
	{
		goto err;
	}
	
	/* send RTU */
	status = al_cep_rtu( p_csq->h_al, p_csq->cid, NULL, 0 );
	if( status != IB_SUCCESS )
	{
err:
		/*
		 * Reject the connection.  Note that we don't free the CEP since the
		 * usermode INDConnector object references it, and the CEP will be
		 * freed when that object is freed.
		 */
		al_cep_rej( p_csq->h_al, p_csq->cid, IB_REJ_INSUF_QP, NULL, 0, NULL, 0 );

		__cep_timewait_qp( p_csq );

		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
			("al_cep_rtu returned %s.\n", ib_get_err_str( status )) );

		__ndi_acquire_lock( &p_csq->csq, &irql );
		if( p_csq->state != NDI_CM_INVALID )
			p_csq->state = NDI_CM_IDLE;
		__ndi_release_lock( &p_csq->csq, irql );

		nt_status = STATUS_CONNECTION_ABORTED;
		goto exit;
	}

	__ndi_acquire_lock( &p_csq->csq, &irql );
	if( p_csq->state == NDI_CM_CONNECTING_REP_RCVD )
		p_csq->state = NDI_CM_CONNECTED;
	__ndi_release_lock( &p_csq->csq, irql );

	nt_status = STATUS_SUCCESS;

exit:
	__ndi_complete_irp( p_csq, p_irp, nt_status );
	AL_EXIT( AL_DBG_NDI );
}


cl_status_t
ndi_rtu_cm(
	IN		nd_csq_t							*p_csq,
	IN		PIRP								p_irp
	)
{
	IO_STACK_LOCATION	*p_io_stack;

	AL_ENTER( AL_DBG_NDI );

	p_irp->Tail.Overlay.DriverContext[0] = p_csq;
	nd_csq_ref( p_csq ); /* Take IRP reference. */
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	p_irp->Tail.Overlay.DriverContext[1] = IoAllocateWorkItem( p_io_stack->DeviceObject );

	IoMarkIrpPending( p_irp );
	if ( p_irp->Tail.Overlay.DriverContext[1] )
	{ /* asyncronous performing */
		/* take a ref to prevent QP destroy before calling work item */
		nd_csq_ref( p_csq ); /* Take work item reference. */
		IoQueueWorkItem( p_irp->Tail.Overlay.DriverContext[1],
			__ndi_do_rtu, DelayedWorkQueue, p_irp );
	}
	else
	{ /* syncronous performing */
		__ndi_do_rtu( p_io_stack->DeviceObject, p_irp );
	}

	AL_EXIT( AL_DBG_NDI );
	return CL_PENDING;
}


/*******************************************************************
 *
 * REP CM request
 *
 ******************************************************************/

static IO_WORKITEM_ROUTINE __ndi_do_rep;
static void
__ndi_do_rep(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN		PIRP								p_irp )
{
	nd_csq_t* p_csq = p_irp->Tail.Overlay.DriverContext[0];
	ib_api_status_t status;
	KIRQL irql;
	NTSTATUS nt_status;

	UNUSED_PARAM(p_dev_obj);

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI, ("[ CID = %d\n", p_csq->cid) );

	/* free the work item if any */
	CL_ASSERT( p_irp->Tail.Overlay.DriverContext[1] != NULL );
	IoFreeWorkItem( p_irp->Tail.Overlay.DriverContext[1] );
	p_irp->Tail.Overlay.DriverContext[1] = NULL;

	/* change the QP state to RTS */
	status = __ndi_qp2rts( p_csq, p_irp );
	if ( status != IB_SUCCESS )
	{
		goto err;
	}
	
	/* send REP */
	status = al_cep_send_rep ( p_csq->h_al, p_csq->cid );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_send_rep returned %s\n", ib_get_err_str(status)) );
err:
		/*
		 * Reject the connection.  Note that we don't free the CEP since the
		 * usermode INDConnector object references it, and the CEP will be
		 * freed when that object is freed.
		 */
		al_cep_rej( p_csq->h_al, p_csq->cid, IB_REJ_INSUF_QP, NULL, 0, NULL, 0 );

		/* transit QP to error state */
		__cep_timewait_qp( p_csq );

		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_rtu returned %s.\n", ib_get_err_str( status )) );
		__ndi_acquire_lock( &p_csq->csq, &irql );
		if( p_csq->state != NDI_CM_INVALID )
			p_csq->state = NDI_CM_IDLE;
		__ndi_release_lock( &p_csq->csq, irql );
		if (status == IB_INVALID_STATE )
			nt_status = STATUS_CONNECTION_ABORTED;
		/* The HCA driver will return IB_INVALID_PARAMETER if the QP is in the wrong state. */
		else if( status == IB_INVALID_HANDLE || status == IB_INVALID_PARAMETER )
			nt_status = STATUS_CANCELLED;
		else
			nt_status = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	__ndi_acquire_lock( &p_csq->csq, &irql );
	if( p_csq->state == NDI_CM_CONNECTING_REP_SENT )
		p_csq->state = NDI_CM_CONNECTED;
	__ndi_release_lock( &p_csq->csq, irql );
	nt_status = STATUS_SUCCESS;

exit:
	__ndi_complete_irp( p_csq, p_irp, nt_status );
	nd_csq_release( p_csq ); /* Release work item reference. */
	AL_EXIT( AL_DBG_NDI );
}

static void
__ndi_fill_cm_rep(
	IN		net32_t								qpn,
	IN		ual_ndi_rep_cm_ioctl_in_t			*p_rep,
		OUT	iba_cm_rep							*p_cm_rep)
{
	AL_ENTER( AL_DBG_NDI );

	memset( p_cm_rep, 0, sizeof(*p_cm_rep) );

	p_cm_rep->p_pdata = p_rep->pdata;
	p_cm_rep->pdata_len = sizeof(p_rep->pdata);

	p_cm_rep->qpn = qpn;

	p_cm_rep->init_depth = p_rep->init_depth;
    p_cm_rep->resp_res = p_rep->resp_res;
	p_cm_rep->failover_accepted = IB_FAILOVER_ACCEPT_UNSUPPORTED;
	p_cm_rep->flow_ctrl = TRUE;	/* HCAs must support end-to-end flow control. */
	p_cm_rep->rnr_retry_cnt = QP_ATTRIB_RNR_RETRY;

	AL_EXIT( AL_DBG_NDI );
}


NTSTATUS
__ndi_send_rep(
	IN		nd_csq_t							*p_csq,
	IN		PIRP								p_irp )
{
	IO_STACK_LOCATION	*p_io_stack;
	ib_qp_handle_t h_qp;
	iba_cm_rep cm_rep;
	ib_api_status_t status;
	ual_ndi_rep_cm_ioctl_in_t *p_rep = 
		(ual_ndi_rep_cm_ioctl_in_t*)cl_ioctl_in_buf( p_irp );

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI,
		("[ CID = %d\n", p_csq->cid) );

	switch( p_csq->state )
	{
	case NDI_CM_CONNECTING_REQ_RCVD:
		break;

	case NDI_CM_CONNECTED_DREQ_RCVD:
		AL_EXIT( AL_DBG_NDI );
		return STATUS_CONNECTION_ABORTED;

	default:
		AL_EXIT( AL_DBG_NDI );
		return STATUS_CONNECTION_ACTIVE;
	}

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	p_irp->Tail.Overlay.DriverContext[1] = IoAllocateWorkItem( p_io_stack->DeviceObject );
	if( p_irp->Tail.Overlay.DriverContext[1] == NULL )
	{
		AL_EXIT( AL_DBG_NDI );
		return STATUS_NO_MEMORY;
	}
	nd_csq_ref( p_csq ); /* Take work item reference. */

	h_qp = CONTAINING_RECORD(
		al_hdl_ref( p_csq->h_al, p_csq->h_qp, AL_OBJ_TYPE_H_QP ),
		ib_qp_t,
		obj );
	if( !h_qp )
	{
		/* The QP was valid when the IOCTL first came in... */
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid QP: %I64d\n", p_rep->h_qp) );
		status = IB_INVALID_HANDLE;
		goto err;
	}

	/* Format ib_cm_req_t structure */
	__ndi_fill_cm_rep( h_qp->num, p_rep, &cm_rep );
	deref_al_obj( &h_qp->obj );

	/* prepare Passive CEP for connection */
	status = kal_cep_pre_rep(
		p_csq->h_al, p_csq->cid, &cm_rep, QP_ATTRIB_RNR_NAK_TIMEOUT, NULL );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("kal_cep_pre_rep returned %s.\n", ib_get_err_str( status )) );
err:
		IoFreeWorkItem( p_irp->Tail.Overlay.DriverContext[1] );
		p_irp->Tail.Overlay.DriverContext[1] = NULL;
		nd_csq_release( p_csq ); /* Release work item reference. */
		switch (status)
		{
			case IB_INVALID_HANDLE:
				return STATUS_CANCELLED;
			case IB_INVALID_STATE:
				return STATUS_CONNECTION_ABORTED;
			case IB_RESOURCE_BUSY:
				return STATUS_CONNECTION_ACTIVE;
			default:
				return ib_to_ntstatus( status );
		}
	}

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_NDI,
		("Prepared Passive CEP with cid %d, h_al %p, context %p\n",
		p_csq->cid, p_csq->h_al, h_qp ) );

	/*
	 * transfer work to a worker thread so that QP transitions can be done
	 * at PASSIVE_LEVEL
	 */
	IoQueueWorkItem( p_irp->Tail.Overlay.DriverContext[1],
		__ndi_do_rep, DelayedWorkQueue, p_irp );

	AL_EXIT( AL_DBG_NDI );
	return STATUS_PENDING;
}


NTSTATUS
ndi_rep_cm(
	IN		ib_al_handle_t						h_al,
	IN		PIRP								p_irp
	)
{
	NTSTATUS status;
	nd_csq_t* p_csq;
	ual_ndi_rep_cm_ioctl_in_t *p_rep = 
		(ual_ndi_rep_cm_ioctl_in_t*)cl_ioctl_in_buf( p_irp );
	KIRQL irql;

	AL_ENTER( AL_DBG_NDI );

	p_csq = kal_cep_get_context( h_al, p_rep->cid, nd_cm_handler, nd_csq_ref );
	if( p_csq == NULL )
	{
		status = STATUS_CONNECTION_ABORTED;
		goto err;
	}

	p_csq->h_qp = p_rep->h_qp;

	p_irp->Tail.Overlay.DriverContext[0] = p_csq;

	__ndi_acquire_lock( &p_csq->csq, &irql );
	status = __ndi_send_rep( p_csq, p_irp );
	if( status == STATUS_PENDING )
	{
		/*
		 * We're going to keep the IRP dangling for a bit - take a reference
		 * on the QP until it completes.
		 */
		nd_csq_ref( p_csq ); /* Take IRP reference. */
		p_csq->state = NDI_CM_CONNECTING_REP_SENT;
		IoMarkIrpPending( p_irp );
	}
	__ndi_release_lock( &p_csq->csq, irql );

	nd_csq_release( p_csq );
err:
	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI, ("] returning %08x\n", status) );
	return status;
}



/*******************************************************************
 *
 * DREQ CM request
 *
 ******************************************************************/


NTSTATUS
__ndi_send_dreq(
	IN		IRP*								p_irp
	)
{
	nd_csq_t *p_csq = (nd_csq_t*)p_irp->Tail.Overlay.DriverContext[0];
	IO_STACK_LOCATION	*p_io_stack;
	ib_api_status_t status;
	NTSTATUS nt_status;

	AL_ENTER( AL_DBG_NDI );

	if ( p_csq->state != NDI_CM_CONNECTED &&
		p_csq->state != NDI_CM_CONNECTED_DREQ_RCVD )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("STATUS_CONNECTION_ACTIVE: CID = %d, uhdl %#I64x, ref_cnt %d\n",
			p_csq->cid, p_csq->h_qp, p_csq->ref_cnt ) );
		return STATUS_CONNECTION_INVALID;
	}

	/*
	 * Allocate a work item to perform the QP transition when disconnection
	 * completes (or the IRP is cancelled).  We allocate here to trap errors
	 * properly.
	 */
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	p_irp->Tail.Overlay.DriverContext[1] = IoAllocateWorkItem( p_io_stack->DeviceObject );
	if( p_irp->Tail.Overlay.DriverContext[1] == NULL )
	{
		AL_EXIT( AL_DBG_NDI );
		return STATUS_NO_MEMORY;
	}
	nd_csq_ref( p_csq ); /* Take work item reference. */

	status = al_cep_dreq( p_csq->h_al, p_csq->cid, NULL, 0 );
	switch( status )
	{
	case IB_INVALID_STATE:
		/*
		 * We're going to keep the IRP dangling for a bit - take a reference
		 * on the CSQ until it completes.
		 */
		nd_csq_ref( p_csq ); /* Take IRP reference. */
		/* We might have just received a DREQ, so try sending a DREP. */
		IoMarkIrpPending( p_irp );
		__ndi_queue_drep( p_irp );
		AL_EXIT( AL_DBG_NDI );
		return STATUS_INVALID_DEVICE_STATE;

	case IB_SUCCESS:
		AL_EXIT( AL_DBG_NDI );
		return STATUS_PENDING;

	case IB_INVALID_HANDLE:
		nt_status = STATUS_CONNECTION_INVALID;
		break;
	default:
		nt_status = ib_to_ntstatus( status );
	}
	IoFreeWorkItem( p_irp->Tail.Overlay.DriverContext[1] );
	p_irp->Tail.Overlay.DriverContext[1] = NULL;
	nd_csq_release( p_csq ); /* Release work item reference. */
	AL_EXIT( AL_DBG_NDI );
	return nt_status;
}


NTSTATUS
ndi_dreq_cm(
	IN		nd_csq_t*							p_csq,
	IN		PIRP								p_irp
	)
{
	NTSTATUS status;

	AL_ENTER( AL_DBG_NDI );

	p_irp->Tail.Overlay.DriverContext[0] = p_csq;

	status = IoCsqInsertIrpEx(
		&p_csq->csq,
		p_irp,
		NULL,
		(VOID*)(ULONG_PTR)NDI_CM_DISCONNECTING
		);
	/*
	 * Note that if al_cep_dreq returned IB_INVALID_STATE, we queued the
	 * work item and will try sending the DREP and move the QP to error.
	 *
	 * The IRP should never be queued if the work item is queued, so 
	 * we trap the special error code for INVALID_STATE.
	 */
	if( status == STATUS_INVALID_DEVICE_STATE )
		status = STATUS_PENDING;

	AL_EXIT( AL_DBG_NDI );
	return status;
}


NTSTATUS
ndi_listen_cm(
	IN		ib_al_handle_t					h_al,
	IN		ib_cep_listen_t					*p_listen,
		OUT	net32_t							*p_cid,
		OUT	size_t							*p_ret_bytes
	)
{
	NTSTATUS status;
	net32_t cid;
	ib_api_status_t ib_status;
	nd_csq_t *p_csq;
	KIRQL irql;

	AL_ENTER( AL_DBG_NDI );

	ib_status = al_create_cep( h_al, NULL, NULL, NULL, &cid );
	if( ib_status != IB_SUCCESS )
	{
		AL_EXIT( AL_DBG_NDI );
		return ib_to_ntstatus( ib_status );
	}

	status = nd_csq_init( h_al, cid, 0, &p_csq );
	if( status != STATUS_SUCCESS )
	{
		kal_cep_destroy( h_al, cid, STATUS_SUCCESS );
		AL_EXIT( AL_DBG_NDI );
		return status;
	}

	__ndi_acquire_lock( &p_csq->csq, &irql );
	p_csq->state = NDI_CM_LISTEN;
	__ndi_release_lock( &p_csq->csq, irql );

	if( ib_cm_rdma_sid_port( p_listen->svc_id ) == 0 )
	{
		p_listen->svc_id = ib_cm_rdma_sid(
			ib_cm_rdma_sid_protocol( p_listen->svc_id ),
			(USHORT)cid | (USHORT)(cid >> 16)
			);
	}

	ib_status = al_cep_listen( h_al, cid, p_listen );
	if( ib_status == IB_SUCCESS )
	{
		*p_cid = cid;
		*p_ret_bytes = sizeof(*p_cid);
	}

	nd_csq_release( p_csq );
	status = ib_to_ntstatus( ib_status );
	AL_EXIT( AL_DBG_NDI );
	return status;
}


NTSTATUS
__ndi_get_req(
	IN		nd_csq_t							*p_csq,
	IN		IRP*								p_irp
	)
{
	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI, ("[ CID = %d\n", p_csq->cid) );

	if( p_csq->state != NDI_CM_LISTEN )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_NDI,
			("] Invalid state (%d).\n", p_csq->state) );
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	/* Check the MAD list. */
	if( p_csq->p_mad_head != NULL )
	{
		ib_mad_element_t* p_mad = p_csq->p_mad_head;
		net32_t cid = (net32_t)(ULONG_PTR)p_mad->send_context1;
		p_csq->p_mad_head = p_mad->p_next;
		p_mad->p_next = NULL;

		*(net32_t*)cl_ioctl_out_buf( p_irp ) = cid;
		p_irp->IoStatus.Information = sizeof(net32_t);
		p_irp->IoStatus.Status = STATUS_SUCCESS;
		IoMarkIrpPending( p_irp );
		IoCompleteRequest( p_irp, IO_NETWORK_INCREMENT );
		ib_put_mad( p_mad );
		AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI, ("] Returned new CID = %d\n", cid) );
		return STATUS_INVALID_DEVICE_STATE;
	}

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_NDI, ("] Queueing IRP\n") );
	return STATUS_PENDING;
}


NTSTATUS
ndi_get_req_cm(
	IN		nd_csq_t						*p_csq,
	IN		PIRP							p_irp
	)
{
	NTSTATUS status;

	AL_ENTER( AL_DBG_NDI );

	status = IoCsqInsertIrpEx(
		&p_csq->csq,
		p_irp,
		NULL,
		(VOID*)(ULONG_PTR)NDI_CM_LISTEN
		);

	/*
	 * __ndi_get_req will return STATUS_INVALID_DEVICE_STATE to prevent the IRP
	 * from being inserted into the CSQ because the IRP was immediately completed.
	 * In this case, we need to return STATUS_PENDING.
	 */
	if( status == STATUS_INVALID_DEVICE_STATE )
	{
		status = STATUS_PENDING;
	}

	AL_EXIT( AL_DBG_NDI );
	return status;
}



