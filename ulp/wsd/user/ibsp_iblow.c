/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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
#include "ibspdebug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ibsp_iblow.tmh"
#endif
#include <complib/cl_thread.h>
#include "ibspdll.h"

#ifdef PERFMON_ENABLED
#include "ibsp_perfmon.h"
#endif


typedef struct _io_comp_info
{
	struct ibsp_socket_info *p_socket;
	LPWSAOVERLAPPED			p_ov;

} io_comp_info_t;


/* Work queue entry completion routine. */
static void
complete_wq(
	IN		const	ib_wc_t						*wc,
		OUT			io_comp_info_t				*p_io_info )
{
	struct _wr				*wr = NULL;
	struct _recv_wr			*p_recv_wr = NULL;
	LPWSAOVERLAPPED			lpOverlapped = NULL;
	struct ibsp_socket_info	*socket_info = NULL;

	IBSP_ENTER( IBSP_DBG_IO );

	wr = (struct _wr *)(ULONG_PTR)wc->wr_id;
	p_recv_wr = (struct _recv_wr *)(ULONG_PTR)wc->wr_id;

	CL_ASSERT( wr );

	socket_info = wr->socket_info;
	p_io_info->p_socket = socket_info;

	lpOverlapped = wr->lpOverlapped;

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
		("socket %p, ov %p, work completion status=%s, wc_type=%s\n",
		socket_info, lpOverlapped, ib_get_wc_status_str( wc->status ),
		ib_get_wc_type_str( wc->wc_type )) );

	/* Set the windows error code. It's not easy to find an easy
	 * correspondence between the IBAL error codes and windows error
	 * codes; but it probably does not matter, as long as it returns an
	 * error. */
	switch( wc->status )
	{
	case IB_WCS_SUCCESS:
		/*
		 * Set the length of the operation. Under Infiniband, the work
		 * completion length is only valid for a receive
		 * operation. Fortunately we had already set the length during the
		 * send operation. 
		 *
		 * lpWPUCompleteOverlappedRequest is supposed to store the length
		 * into InternalHigh, however it will not be called if the low
		 * order bit of lpOverlapped->hEvent is set. So we do it and hope
		 * for the best. 
		 *
		 * NOTE: Without a valid length, the switch doesn't seem to call 
		 * GetOverlappedResult() even if we call lpWPUCompleteOverlappedRequest()
		 */
		switch ( wc->wc_type ) 
		{
		case IB_WC_RECV:
			CL_ASSERT(wc->length != 0);
			lpOverlapped->InternalHigh = wc->length;
#ifdef IBSP_LOGGING
			cl_spinlock_acquire( &socket_info->recv_lock );
			DataLogger_WriteData(&socket_info->RecvDataLogger,
				p_recv_wr->idx, (void *)p_recv_wr->ds_array[0].vaddr,
				wc->length);
			cl_spinlock_release( &socket_info->recv_lock );
#endif
#ifdef PERFMON_ENABLED
			InterlockedIncrement64( &g_pm_stat.pdata[COMP_RECV] );
			InterlockedExchangeAdd64( &g_pm_stat.pdata[BYTES_RECV],
				lpOverlapped->InternalHigh );
#endif
#ifdef _DEBUG_
			cl_atomic_inc(&g_ibsp.total_recv_compleated);
#endif
			break;

		case IB_WC_RDMA_READ:
			CL_ASSERT(wc->length != 0);
			lpOverlapped->InternalHigh = wc->length;
#ifdef PERFMON_ENABLED
			InterlockedIncrement64( &g_pm_stat.pdata[COMP_RECV] );
			InterlockedExchangeAdd64( &g_pm_stat.pdata[BYTES_READ],
				lpOverlapped->InternalHigh );
#endif /* PERFMON_ENABLED */
			break;

#ifdef PERFMON_ENABLED
		case IB_WC_SEND:
			InterlockedIncrement64( &g_pm_stat.pdata[COMP_SEND] );
			InterlockedExchangeAdd64( &g_pm_stat.pdata[BYTES_SEND],
				lpOverlapped->InternalHigh );
			break;

		case IB_WC_RDMA_WRITE:
			InterlockedIncrement64( &g_pm_stat.pdata[COMP_SEND] );
			InterlockedExchangeAdd64( &g_pm_stat.pdata[BYTES_WRITE],
				lpOverlapped->InternalHigh );
#endif /* PERFMON_ENABLED */
		default:
			break;
		}


		lpOverlapped->OffsetHigh = 0;
		break;

	case IB_WCS_WR_FLUSHED_ERR:
		cl_spinlock_acquire( &socket_info->mutex1 );

		if( socket_info->socket_state == IBSP_DUPLICATING_REMOTE &&
			wc->wc_type == IB_WC_RECV )
		{
			/*
			 * Take the wr off the wr_list, and place onto the
			 * dup_wr_list.  We will post them later on the new QP. 
			 */
			cl_spinlock_acquire( &socket_info->recv_lock );

			/* Copy to the duplicate WR array. */
			socket_info->dup_wr[socket_info->dup_idx] = *p_recv_wr;

#if QP_ATTRIB_RQ_DEPTH == 256 || QP_ATTRIB_RQ_DEPTH == 128 || \
	QP_ATTRIB_RQ_DEPTH == 64 || QP_ATTRIB_RQ_DEPTH == 32 || \
	QP_ATTRIB_RQ_DEPTH == 16 || QP_ATTRIB_RQ_DEPTH == 8
			socket_info->dup_idx++;
			socket_info->dup_idx &= (QP_ATTRIB_RQ_DEPTH - 1);
#else
			if( ++socket_info->dup_idx == QP_ATTRIB_RQ_DEPTH )
				socket_info->dup_idx = 0;
#endif

			cl_atomic_inc( &socket_info->dup_cnt );
			/* ib_cq_comp will decrement the receive count. */
			cl_atomic_dec( &socket_info->recv_cnt );

			cl_spinlock_release( &socket_info->recv_lock );

			cl_spinlock_release( &socket_info->mutex1 );
			p_io_info->p_ov = NULL;
			IBSP_EXIT( IBSP_DBG_IO );
			return;
		}
		
		/* Check for flushing the receive buffers on purpose. */
		if( socket_info->socket_state == IBSP_DUPLICATING_OLD )
			wr->lpOverlapped->OffsetHigh = 0;
		else
			wr->lpOverlapped->OffsetHigh = WSA_OPERATION_ABORTED;

		cl_spinlock_release( &socket_info->mutex1 );

		/* Override the length, as per the WSD specs. */
		wr->lpOverlapped->InternalHigh = 0;
		break;

	case IB_WCS_LOCAL_LEN_ERR:
	case IB_WCS_LOCAL_OP_ERR:
	case IB_WCS_LOCAL_PROTECTION_ERR:
	case IB_WCS_MEM_WINDOW_BIND_ERR:
	case IB_WCS_REM_ACCESS_ERR:
	case IB_WCS_REM_OP_ERR:
	case IB_WCS_RNR_RETRY_ERR:
	case IB_WCS_TIMEOUT_RETRY_ERR:
	case IB_WCS_REM_INVALID_REQ_ERR:
	default:
		{
			char	comp_name[MAX_COMPUTERNAME_LENGTH + 1] = {0};
			DWORD	len = sizeof(comp_name);
			GetComputerName( comp_name, &len );
			IBSP_ERROR( ("%s (%s:%d to ",
				comp_name, inet_ntoa( socket_info->local_addr.sin_addr ),
				socket_info->local_addr.sin_port) );
			IBSP_ERROR( ("%s:%d) %s error: %s (vendor specific %I64x)\n",
				inet_ntoa( socket_info->peer_addr.sin_addr ),
				socket_info->peer_addr.sin_port,
				ib_get_wc_type_str( wc->wc_type ),
				ib_get_wc_status_str( wc->status ),
				wc->vendor_specific) );
			lpOverlapped->OffsetHigh = WSAECONNABORTED;
			wr->lpOverlapped->InternalHigh = 0;
			socket_info->qp_error = WSAECONNABORTED;
			break;
		}
	}

#ifdef PERFMON_ENABLED
	InterlockedIncrement64( &g_pm_stat.pdata[COMP_TOTAL] );
#endif

#ifdef _DEBUG_
	if( wc->wc_type == IB_WC_RECV )
	{
		// This code requires the recv count to be decremented here, but it needs
		// to be decremented after any callbacks are invoked so socket destruction
		// gets delayed until all callbacks have been invoked.
		//{
		//	uint8_t	idx;

		//	cl_spinlock_acquire( &socket_info->recv_lock );
		//	idx = socket_info->recv_idx - (uint8_t)socket_info->recv_cnt;
		//	if( idx >= QP_ATTRIB_RQ_DEPTH )
		//		idx += QP_ATTRIB_RQ_DEPTH;

		//	CL_ASSERT( wc->wr_id == (ULONG_PTR)&socket_info->recv_wr[idx] );
		//	cl_atomic_dec( &socket_info->recv_cnt );
		//	cl_spinlock_release( &socket_info->recv_lock );
		//}

		if( wc->status == IB_SUCCESS && p_recv_wr->ds_array[0].length >= 40 )
		{
			debug_dump_buffer( IBSP_DBG_WQ, "RECV",
				(void *)(ULONG_PTR)p_recv_wr->ds_array[0].vaddr, 40 );
		}

		cl_atomic_dec( &g_ibsp.recv_count );
		cl_atomic_inc( &socket_info->recv_comp );

		memset( p_recv_wr, 0x33, sizeof(struct _recv_wr) );
	}
	else
	{
		// This code requires the send count to be decremented here, but it needs
		// to be decremented after any callbacks are invoked so socket destruction
		// gets delayed until all callbacks have been invoked.
		//{
		//	uint8_t	idx;

		//	cl_spinlock_acquire( &socket_info->send_lock );
		//	idx = socket_info->send_idx - (uint8_t)socket_info->send_cnt;
		//	if( idx >= QP_ATTRIB_SQ_DEPTH )
		//		idx += QP_ATTRIB_SQ_DEPTH;
		//	CL_ASSERT( wc->wr_id == (ULONG_PTR)&socket_info->send_wr[idx] );
		//	cl_atomic_dec( &socket_info->send_cnt );
		//	cl_spinlock_release( &socket_info->send_lock );
		//}

		if( wc->wc_type == IB_WC_SEND )
		{
			cl_atomic_dec( &g_ibsp.send_count );
			cl_atomic_inc( &socket_info->send_comp );

			fzprint(("%s():%d:0x%x:0x%x: send_count=%d\n",
				__FUNCTION__,
				__LINE__, GetCurrentProcessId(), GetCurrentThreadId(), g_ibsp.send_count));
		}

		memset( wr, 0x33, sizeof(struct _wr) );
	}
#endif
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
		("overlapped=%p, InternalHigh=%Id, hEvent=%p\n",
		lpOverlapped, lpOverlapped->InternalHigh,
		 lpOverlapped->hEvent) );
	


	/* Don't notify the switch for that completion only if:
	 *   - the switch don't want a notification
	 *   - the wq completed with success
	 *   - the socket is still connected
	 */
	if( ((uintptr_t) lpOverlapped->hEvent) & 0x00000001 )
	{
		/* Indicate this operation is complete. The switch will poll
		 * with calls to WSPGetOverlappedResult(). */

#ifdef _DEBUG_
		cl_atomic_dec( &g_ibsp.overlap_h1_comp_count );

		fzprint(("%s():%d:0x%x:0x%x: ov=0x%p h0=%d h1=%d h1_c=%d send=%d recv=%d\n",
				 __FUNCTION__, __LINE__, GetCurrentProcessId(),
				 GetCurrentThreadId(), lpOverlapped,
				 g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count,
				 g_ibsp.overlap_h1_comp_count, g_ibsp.send_count, g_ibsp.recv_count));
#endif

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
			("Not calling lpWPUCompleteOverlappedRequest: "
			"socket=%p, ov=%p OffsetHigh=%d, InternalHigh=%Id hEvent=%p\n",
			socket_info, lpOverlapped, lpOverlapped->OffsetHigh,
			lpOverlapped->InternalHigh, lpOverlapped->hEvent) );

		lpOverlapped->Internal = 0;
		p_io_info->p_ov = NULL;
	}
	else
	{
#ifdef _DEBUG_
		cl_atomic_dec( &g_ibsp.overlap_h0_count );

		fzprint(("%s():%d:0x%x:0x%x: ov=0x%p h0=%d h1=%d h1_c=%d send=%d recv=%d\n",
				 __FUNCTION__, __LINE__, GetCurrentProcessId(),
				 GetCurrentThreadId(), lpOverlapped,
				 g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count,
				 g_ibsp.overlap_h1_comp_count, g_ibsp.send_count, g_ibsp.recv_count));
#endif

		p_io_info->p_ov = lpOverlapped;
		cl_atomic_inc( &socket_info->ref_cnt1 );
	}

	if( wc->wc_type == IB_WC_RECV )
	{
		cl_atomic_dec( &socket_info->recv_cnt );
	}
	else
	{
		cl_atomic_dec( &socket_info->send_cnt );
	}

	IBSP_EXIT( IBSP_DBG_IO );
}


/* CQ completion handler. */
int
ib_cq_comp(
					void						*cq_context )
{
	struct cq_thread_info	*cq_tinfo = cq_context;
	ib_api_status_t			status;
	ib_wc_t					wclist[WC_LIST_SIZE];
	ib_wc_t					*free_wclist;
	ib_wc_t					*done_wclist;
	io_comp_info_t			info[WC_LIST_SIZE];
	int						cb_idx;
	int						i;
	int						n_comp = 0;
#ifdef _DEBUG_
	int						comp_count;
#endif

	IBSP_ENTER( IBSP_DBG_WQ );

	CL_ASSERT( WC_LIST_SIZE >= 1 );

	do
	{
		/* Try to retrieve up to WC_LIST_SIZE completions at a time. */
		for( i = 0; i < (WC_LIST_SIZE - 1); i++ )
		{
			wclist[i].p_next = &wclist[i + 1];
		}
		wclist[(WC_LIST_SIZE - 1)].p_next = NULL;

		free_wclist = &wclist[0];
		done_wclist = NULL;

		cl_spinlock_acquire(&cq_tinfo->cq_spinlock);

		status = ib_poll_cq( cq_tinfo->cq, &free_wclist, &done_wclist );

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_WQ,
			("poll CQ got status %d, free=%p, done=%p\n",
			status, free_wclist, done_wclist) );

		switch( status )
		{
		case IB_NOT_FOUND:
		case IB_SUCCESS:
			break;

		case IB_INVALID_CQ_HANDLE:
			/* This happens when the switch closes the socket while the 
			 * execution thread was calling lpWPUCompleteOverlappedRequest. */
			IBSP_ERROR( (
				"ib_poll_cq returned IB_INVLALID_CQ_HANDLE\n") );
			cl_spinlock_release(&cq_tinfo->cq_spinlock);
			goto done;

		default:
			IBSP_ERROR( (
				"ib_poll_cq failed returned %s\n", ib_get_err_str( status )) );
			break;
		}

#ifdef _DEBUG_
		comp_count = 0;
#endif

		/* We have some completions. */
		cb_idx = 0;
		while( done_wclist )
		{
#ifdef _DEBUG_
			comp_count++;
#endif
			complete_wq( done_wclist, &info[cb_idx++] );

			done_wclist = done_wclist->p_next;
		}
		cl_spinlock_release(&cq_tinfo->cq_spinlock);

		for( i = 0; i < cb_idx; i++ )
		{
			int error;
			int ret;

			if( info[i].p_ov )
			{
				IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
					("Calling lpWPUCompleteOverlappedRequest: "
					"socket=%p, ov=%p OffsetHigh=%d "
					"InternalHigh=%Id hEvent=%p\n",
					info[i].p_socket, info[i].p_ov, info[i].p_ov->OffsetHigh,
					info[i].p_ov->InternalHigh, info[i].p_ov->hEvent) );

				ret = g_ibsp.up_call_table.lpWPUCompleteOverlappedRequest(
					info[i].p_socket->switch_socket, info[i].p_ov,
					info[i].p_ov->OffsetHigh,
					(DWORD)info[i].p_ov->InternalHigh, &error );
				if( ret != 0 )
				{
					IBSP_ERROR( ("WPUCompleteOverlappedRequest for ov=%p "
						"returned %d err %d\n", info[i].p_ov, ret, error) );
				}
				deref_socket_info( info[i].p_socket );
			}
		}

		n_comp += i;

#ifdef _DEBUG_
		if( comp_count > g_ibsp.max_comp_count )
		{
			g_ibsp.max_comp_count = comp_count;
		}
#endif
	} while( !free_wclist );

done:

#ifdef _DEBUG_
	fzprint(("%s():%d:0x%x:0x%x: overlap_h0_count=%d overlap_h1_count=%d\n",
			 __FUNCTION__,
			 __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count));
#endif

	IBSP_EXIT( IBSP_DBG_WQ );
	return n_comp;
}


/* IB completion thread */
static DWORD WINAPI
ib_cq_thread(
					LPVOID						lpParameter )
{
	struct cq_thread_info	*cq_tinfo = (struct cq_thread_info *)lpParameter;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	int						i;
	DWORD_PTR old_afinity;

	IBSP_ENTER( IBSP_DBG_HW );

	fzprint(("%s():%d:0x%x:0x%x: cq_tinfo=0x%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), cq_tinfo));

	old_afinity = SetThreadAffinityMask (GetCurrentThread (),g_dwPollThreadAffinityMask);
	if (old_afinity == 0) {
		IBSP_ERROR(("SetThreadAffinityMask failed\n"));
	} else {
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_DLL,("SetThreadAffinityMask succeeded\n"));
	}


	do
	{
		cl_status = cl_waitobj_wait_on( cq_tinfo->cq_waitobj, EVENT_NO_TIMEOUT, TRUE );
		if( cl_status != CL_SUCCESS )
		{
			IBSP_ERROR( (
				"cl_waitobj_wait_on() (%d)\n", cl_status) );
		}

		/* 
		 * TODO: By rearranging thread creation and cq creation, this check
		 * may be eliminated.
		 */
		if( cq_tinfo->cq != NULL )
		{
			fzprint(("%s():%d:0x%x:0x%x: Calling ib_cq_comp().\n", __FUNCTION__,
					 __LINE__, GetCurrentProcessId(), GetCurrentThreadId()));

#ifdef PERFMON_ENABLED
			InterlockedIncrement64( &g_pm_stat.pdata[INTR_TOTAL] );
#endif
			i = g_max_poll;
			do
			{
				if( ib_cq_comp( cq_tinfo ) )
					i = g_max_poll;

			} while( i-- );

			fzprint(("%s():%d:0x%x:0x%x: Done calling ib_cq_comp().\n", __FUNCTION__,
					 __LINE__, GetCurrentProcessId(), GetCurrentThreadId()));

			status = ib_rearm_cq( cq_tinfo->cq, FALSE );
			if( status != IB_SUCCESS )
			{
				IBSP_ERROR( (
					"ib_rearm_cq returned %s)\n", ib_get_err_str( status )) );
			}
		}

	} while( !cq_tinfo->ib_cq_thread_exit_wanted );

	cl_status = cl_waitobj_destroy( cq_tinfo->cq_waitobj );
	if( cl_status != CL_SUCCESS )
	{
		IBSP_ERROR( (
			"cl_waitobj_destroy() returned %s\n", CL_STATUS_MSG(cl_status)) );
	}
	HeapFree( g_ibsp.heap, 0, cq_tinfo );

	/* No special exit code, even on errors. */
	IBSP_EXIT( IBSP_DBG_HW );
	ExitThread( 0 );
}


/* Called with the HCA's CQ lock held. */
static struct cq_thread_info *
ib_alloc_cq_tinfo(
					struct ibsp_hca				*hca )
{
	struct cq_thread_info *cq_tinfo = NULL;
	ib_cq_create_t cq_create;
	ib_api_status_t status;
	cl_status_t cl_status;

	IBSP_ENTER( IBSP_DBG_HW );

	cq_tinfo = HeapAlloc(
		g_ibsp.heap, HEAP_ZERO_MEMORY, sizeof(struct cq_thread_info) );

	if( !cq_tinfo )
	{
		IBSP_ERROR_EXIT( ("HeapAlloc() Failed.\n") );
		return NULL;
	}

	cl_status = cl_waitobj_create( FALSE, &cq_tinfo->cq_waitobj );
	if( cl_status != CL_SUCCESS )
	{
		cq_tinfo->cq_waitobj = NULL;
		ib_destroy_cq_tinfo( cq_tinfo );
		IBSP_ERROR_EXIT( (
			"cl_waitobj_create() returned %s\n", CL_STATUS_MSG(cl_status)) );
		return NULL;
	}

	cq_tinfo->hca = hca;
	cq_tinfo->ib_cq_thread_exit_wanted = FALSE;

	cq_tinfo->ib_cq_thread = CreateThread( NULL, 0, ib_cq_thread, cq_tinfo, 0,
		(LPDWORD)&cq_tinfo->ib_cq_thread_id );

	if( cq_tinfo->ib_cq_thread == NULL )
	{
		ib_destroy_cq_tinfo( cq_tinfo );
		IBSP_ERROR_EXIT( ("CreateThread failed (%d)", GetLastError()) );
		return NULL;
	}

	STAT_INC( thread_num );

	/* Completion queue */
	cq_create.size = IB_INIT_CQ_SIZE;

	cq_create.pfn_comp_cb = NULL;
	cq_create.h_wait_obj = cq_tinfo->cq_waitobj;

	status = ib_create_cq( hca->hca_handle, &cq_create, cq_tinfo,
		NULL, &cq_tinfo->cq );
	if( status )
	{
		ib_destroy_cq_tinfo( cq_tinfo );
		IBSP_ERROR_EXIT( (
			"ib_create_cq returned %s\n", ib_get_err_str( status )) );
		return NULL;
	}

	STAT_INC( cq_num );

	status = ib_rearm_cq( cq_tinfo->cq, FALSE );
	if( status )
	{
		ib_destroy_cq_tinfo( cq_tinfo );
		IBSP_ERROR_EXIT( (
			"ib_rearm_cq returned %s\n", ib_get_err_str( status )) );
		return NULL;
	}

	cq_tinfo->cqe_size = cq_create.size;

	if( hca->cq_tinfo )
	{
		__cl_primitive_insert(
			&hca->cq_tinfo->list_item, &cq_tinfo->list_item );
	}
	else
	{
		/* Setup the list entry to point to itself. */
		cq_tinfo->list_item.p_next = &cq_tinfo->list_item;
		cq_tinfo->list_item.p_prev = &cq_tinfo->list_item;
	}

	/* We will be assigned to a QP - set the QP count. */
	cq_tinfo->qp_count = 1;

	/* Upon allocation, the new CQ becomes the primary. */
	hca->cq_tinfo = cq_tinfo;

	cl_spinlock_init(&cq_tinfo->cq_spinlock);

	IBSP_EXIT( IBSP_DBG_HW );
	return (cq_tinfo);
}


void
ib_destroy_cq_tinfo(
					struct cq_thread_info		*cq_tinfo )
{
	ib_wc_t wclist;
	ib_wc_t *free_wclist;
	ib_wc_t *done_wclist;
	ib_api_status_t status;
	HANDLE h_cq_thread;
	DWORD cq_thread_id;

	IBSP_ENTER( IBSP_DBG_HW );

	CL_ASSERT( cq_tinfo );
	CL_ASSERT( cq_tinfo->qp_count == 0 );

	cl_spinlock_destroy(&cq_tinfo->cq_spinlock);

	if( cq_tinfo->cq )
	{
		wclist.p_next = NULL;
		free_wclist = &wclist;

		while( ib_poll_cq(
			cq_tinfo->cq, &free_wclist, &done_wclist ) == IB_SUCCESS )
		{
			IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_WQ,
				("free=%p, done=%p\n", free_wclist, done_wclist) );
		}

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_WQ, ("ib_destroy_cq() start..\n") );

		/*
		 * Called from cleanup thread, okay to block.
		 */
		status = ib_destroy_cq( cq_tinfo->cq, ib_sync_destroy );
		if( status )
		{
			IBSP_ERROR( (
				"ib_destroy_cq returned %s\n", ib_get_err_str( status )) );
		}
		else
		{
			IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_WQ, ("ib_destroy_cq() finished.\n") );

			cq_tinfo->cq = NULL;

			STAT_DEC( cq_num );
		}
	}

	if( cq_tinfo->ib_cq_thread )
	{
		/* ib_cq_thread() will release the cq_tinfo before exit. Don't
		   reference cq_tinfo after signaling  */
		h_cq_thread = cq_tinfo->ib_cq_thread;
		cq_tinfo->ib_cq_thread = NULL;
		cq_thread_id = cq_tinfo->ib_cq_thread_id;

		cq_tinfo->ib_cq_thread_exit_wanted = TRUE;
		cl_waitobj_signal( cq_tinfo->cq_waitobj );

		/* Wait for ib_cq_thread to die, if we are not running on it */
		if( GetCurrentThreadId() != cq_thread_id )
		{
			fzprint(("%s():%d:0x%x:0x%x: Waiting for ib_cq_thread=0x%x to die\n",
					 __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(),
					 cq_thread_id ));
			if( WaitForSingleObject( h_cq_thread, INFINITE ) != WAIT_OBJECT_0 )
			{
				IBSP_ERROR( ("WaitForSingleObject failed\n") );
			}
			else
			{
				STAT_DEC( thread_num );
			}
		}
		else
		{
			fzprint(("%s():%d:0x%x:0x%x: Currently on ib_cq_thread.\n", __FUNCTION__,
					 __LINE__, GetCurrentProcessId(), GetCurrentThreadId()));
			STAT_DEC( thread_num );
		}
		CloseHandle( h_cq_thread );
	}
	else
	{
		/* There was no thread created, destroy cq_waitobj and
		   free memory */
		if( cq_tinfo->cq_waitobj )
		{
			cl_waitobj_destroy( cq_tinfo->cq_waitobj );
			cq_tinfo->cq_waitobj = NULL;
		}
		HeapFree( g_ibsp.heap, 0, cq_tinfo );
	}

	IBSP_EXIT( IBSP_DBG_HW );
}


static struct cq_thread_info *
ib_acquire_cq_tinfo(
					struct ibsp_hca				*hca )
{
	struct cq_thread_info	*cq_tinfo = NULL, *cq_end;
	uint32_t				cqe_size;
	ib_api_status_t			status;

	IBSP_ENTER( IBSP_DBG_HW );

	cl_spinlock_acquire( &hca->cq_lock );

	if( !hca->cq_tinfo )
	{
		cq_tinfo = ib_alloc_cq_tinfo( hca );
		if( !cq_tinfo )
			IBSP_ERROR( ("ib_alloc_cq_tinfo() failed\n") );
		cl_spinlock_release( &hca->cq_lock );
		IBSP_EXIT( IBSP_DBG_HW );
		return cq_tinfo;
	}

	cq_tinfo = hca->cq_tinfo;
	cq_end = cq_tinfo;
	cqe_size = (cq_tinfo->qp_count + 1) * IB_CQ_SIZE;

	do
	{
		if( cq_tinfo->cqe_size >= cqe_size )
		{
			cq_tinfo->qp_count++;
			cl_spinlock_release( &hca->cq_lock );
			IBSP_EXIT( IBSP_DBG_HW );
			return (cq_tinfo);
		}

		status = ib_modify_cq( cq_tinfo->cq, &cqe_size );
		switch( status )
		{
		case IB_SUCCESS:
			cq_tinfo->cqe_size = cqe_size;
			cq_tinfo->qp_count++;
			break;

		default:
			IBSP_ERROR_EXIT( (
				"ib_modify_cq() returned %s\n", ib_get_err_str(status)) );
		case IB_INVALID_CQ_SIZE:
		case IB_UNSUPPORTED:
			cq_tinfo = PARENT_STRUCT(
				cl_qlist_next( &cq_tinfo->list_item ), struct cq_thread_info,
				list_item );
			cqe_size = (cq_tinfo->qp_count + 1) * IB_CQ_SIZE;
		}

	} while( cq_tinfo != cq_end );

	if( cq_tinfo == cq_end )
		cq_tinfo = ib_alloc_cq_tinfo( hca );

	cl_spinlock_release( &hca->cq_lock );
	IBSP_EXIT( IBSP_DBG_HW );
	return (cq_tinfo);
}

void
ib_release_cq_tinfo(
					struct cq_thread_info		*cq_tinfo )
{
	IBSP_ENTER( IBSP_DBG_HW );

	CL_ASSERT( cq_tinfo );
	CL_ASSERT( cq_tinfo->hca );

	cl_spinlock_acquire( &cq_tinfo->hca->cq_lock );
	/* If this CQ now has fewer QPs than the primary, make it the primary. */
	if( --cq_tinfo->qp_count < cq_tinfo->hca->cq_tinfo->qp_count )
		cq_tinfo->hca->cq_tinfo = cq_tinfo;
	cl_spinlock_release( &cq_tinfo->hca->cq_lock );

	IBSP_EXIT( IBSP_DBG_HW );
}


/* Release IB ressources. */
void
ib_release(void)
{
	cl_fmap_item_t			*p_item;

	IBSP_ENTER( IBSP_DBG_HW );

	if( g_ibsp.al_handle )
	{
		cl_list_item_t *item;
		ib_api_status_t status;

		unregister_pnp();

		while( (item = cl_qlist_head( &g_ibsp.hca_list )) != cl_qlist_end( &g_ibsp.hca_list ) )
		{
			struct ibsp_hca *hca = PARENT_STRUCT(item, struct ibsp_hca, item);

			pnp_ca_remove( hca );
		}

		fzprint(("%s():%d:0x%x:0x%x: Calling ib_close_al...\n", __FUNCTION__,
				 __LINE__, GetCurrentProcessId(), GetCurrentThreadId()));

		status = ib_close_al( g_ibsp.al_handle );

		fzprint(("%s():%d:0x%x:0x%x: Done calling ib_close_al, status=%d.\n",
				 __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(),
				 status));
		if( status != IB_SUCCESS )
		{
			IBSP_ERROR( (
				"ib_close_al returned %s\n", ib_get_err_str( status )) );
		}
		else
		{
			IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW, ("ib_close_al success\n") );
			STAT_DEC( al_num );
		}
		g_ibsp.al_handle = NULL;
	}

	for( p_item = cl_fmap_head( &g_ibsp.ip_map );
		p_item != cl_fmap_end( &g_ibsp.ip_map );
		p_item = cl_fmap_head( &g_ibsp.ip_map ) )
	{
		cl_fmap_remove_item( &g_ibsp.ip_map, p_item );

		HeapFree( g_ibsp.heap, 0,
			PARENT_STRUCT(p_item, struct ibsp_ip_addr, item) );
	}

	IBSP_EXIT( IBSP_DBG_HW );
}


/* Initialize IB ressources. */
int
ibsp_initialize(void)
{
	ib_api_status_t status;
	int ret;

	IBSP_ENTER( IBSP_DBG_HW );

	CL_ASSERT( g_ibsp.al_handle == NULL );
	CL_ASSERT( cl_qlist_count( &g_ibsp.hca_list ) == 0 );

	/* Open the IB library */
	status = ib_open_al( &g_ibsp.al_handle );

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW, ("open is %d %p\n", status, g_ibsp.al_handle) );

	if( status != IB_SUCCESS )
	{
		IBSP_ERROR( ("ib_open_al failed (%d)\n", status) );
		ret = WSAEPROVIDERFAILEDINIT;
		goto done;
	}

	STAT_INC( al_num );

	/* Register for PNP events */
	status = register_pnp();
	if( status )
	{
		IBSP_ERROR( ("register_pnp failed (%d)\n", status) );
		ret = WSAEPROVIDERFAILEDINIT;
		goto done;
	}

	STAT_INC( thread_num );

	ret = 0;
done:
	if( ret )
	{
		/* Free up resources. */
		ib_release();
	}

	IBSP_EXIT( IBSP_DBG_HW );

	return ret;
}


/* Destroys the infiniband ressources of a socket. */
void
ib_destroy_socket(
	IN	OUT			struct ibsp_socket_info		*socket_info )
{
	ib_api_status_t status;

	IBSP_ENTER( IBSP_DBG_EP );

	if( socket_info->qp )
	{


		ib_qp_mod_t		qp_mod;
		
		cl_atomic_inc( &socket_info->ref_cnt1 );

		cl_memclr( &qp_mod, sizeof(ib_qp_mod_t) );
		qp_mod.req_state = IB_QPS_ERROR;
		status = ib_modify_qp(socket_info->qp, &qp_mod);
		if( status != IB_SUCCESS )
		{
			IBSP_ERROR( ("ib_modify_qp returned %s\n",
				ib_get_err_str( status )) );
			deref_socket_info( socket_info );
		}

			
		/* Wait for all work requests to get flushed. */
		while( socket_info->send_cnt || socket_info->send_cnt  )
			cl_thread_suspend( 0 );

		status = ib_destroy_qp( socket_info->qp, deref_socket_info );
		if( status != IB_SUCCESS )
		{
			IBSP_ERROR( ("ib_destroy_qp returned %s\n",
				ib_get_err_str( status )) );
			deref_socket_info( socket_info );
		}

		ib_release_cq_tinfo( socket_info->cq_tinfo );

		socket_info->qp = NULL;
	}

	IBSP_EXIT( IBSP_DBG_EP );
}


/*
 * Creates the necessary IB ressources for a socket
 */
int
ib_create_socket(
	IN	OUT			struct ibsp_socket_info		*socket_info)
{
	ib_qp_create_t			qp_create;
	ib_api_status_t			status;
	ib_qp_attr_t			qp_attr;

	IBSP_ENTER( IBSP_DBG_EP );

	CL_ASSERT( socket_info != NULL );
	CL_ASSERT( socket_info->port != NULL );
	CL_ASSERT( socket_info->qp == NULL );

	socket_info->hca_pd = socket_info->port->hca->pd;

	/* Get the completion queue and thread info for this socket */
	socket_info->cq_tinfo = ib_acquire_cq_tinfo( socket_info->port->hca );
	if( !socket_info->cq_tinfo )
	{
		IBSP_ERROR_EXIT( ("ib_acquire_cq_tinfo failed\n") );
		return WSAENOBUFS;
	}

	/* Queue pair */
	cl_memclr(&qp_create, sizeof(ib_qp_create_t));
	qp_create.qp_type = IB_QPT_RELIABLE_CONN;
	qp_create.sq_depth = QP_ATTRIB_SQ_DEPTH;
	qp_create.rq_depth = QP_ATTRIB_RQ_DEPTH;
	qp_create.sq_sge = QP_ATTRIB_SQ_SGE;
	qp_create.rq_sge = 1;
	qp_create.h_rq_cq = socket_info->cq_tinfo->cq;
	qp_create.h_sq_cq = socket_info->cq_tinfo->cq;
	qp_create.sq_signaled = TRUE;

	status = ib_create_qp( socket_info->hca_pd, &qp_create, socket_info,	/* context */
		qp_event_handler,	/* async handler */
		&socket_info->qp );
	if( status )
	{
		ib_release_cq_tinfo( socket_info->cq_tinfo );
		IBSP_ERROR_EXIT( (
			"ib_create_qp returned %s\n", ib_get_err_str( status )) );
		return WSAENOBUFS;
	}

	status = ib_query_qp( socket_info->qp, &qp_attr );
	if( status == IB_SUCCESS )
	{
		socket_info->max_inline = min( g_max_inline, qp_attr.sq_max_inline );
	}
	else
	{
		IBSP_ERROR( ("ib_query_qp returned %s\n", ib_get_err_str( status )) );
		socket_info->max_inline = 0;
	}

	STAT_INC( qp_num );

	IBSP_EXIT( IBSP_DBG_EP );
	return 0;
}


void
wait_cq_drain(
	IN	OUT			struct ibsp_socket_info		*socket_info )
{
	IBSP_ENTER( IBSP_DBG_EP );

	if( socket_info->cq_tinfo == NULL )
	{
		IBSP_EXIT( IBSP_DBG_EP );
		return;
	}

	/* Wait for the QP to be drained. */
	while( socket_info->send_cnt || socket_info->recv_cnt )
	{
		fzprint(("%s():%d:0x%x:0x%x: socket=0x%p wr_list_count=%d qp state=%d\n",
				 __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(),
				 socket_info, cl_qlist_count(&socket_info->wr_list)));

		Sleep(100);
	}

	IBSP_EXIT( IBSP_DBG_EP );
}


void
ibsp_dup_overlap_abort(
	IN	OUT			struct ibsp_socket_info		*socket_info )
{
	LPWSAOVERLAPPED lpOverlapped = NULL;
	int error;
	int ret;
	uint8_t				idx;

	IBSP_ENTER( IBSP_DBG_EP );
	CL_ASSERT( !socket_info->send_cnt && !socket_info->recv_cnt );

	/* Browse the list of all posted overlapped structures
	 * to mark them as aborted. */
	idx = socket_info->dup_idx - (uint8_t)socket_info->dup_cnt;
	if( idx >= QP_ATTRIB_RQ_DEPTH )
		idx += QP_ATTRIB_RQ_DEPTH;

	while( socket_info->dup_cnt )
	{
		lpOverlapped = socket_info->dup_wr[idx].wr.lpOverlapped;

		fzprint(("%s():%d:0x%x:0x%x: socket=0x%p wr=0x%p overlapped=0x%p Internal=%d InternalHigh=%d hEvent=%d\n",
			__FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), socket_info, &socket_info->dup_wr[idx], lpOverlapped, lpOverlapped->Internal, lpOverlapped->InternalHigh, lpOverlapped->hEvent));

		lpOverlapped->OffsetHigh = WSAECONNABORTED;
		lpOverlapped->InternalHigh = 0;

		if( ((uintptr_t) lpOverlapped->hEvent) & 0x00000001 )
		{
			/* Indicate this operation is complete. The switch will poll
			 * with calls to WSPGetOverlappedResult(). */
#ifdef _DEBUG_
			cl_atomic_dec(&g_ibsp.overlap_h1_comp_count);

			fzprint(("%s():%d:0x%x:0x%x: ov=0x%p h0=%d h1=%d h1_c=%d send=%d recv=%d\n",
					 __FUNCTION__, __LINE__, GetCurrentProcessId(),
					 GetCurrentThreadId(), lpOverlapped,
					 g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count,
					 g_ibsp.overlap_h1_comp_count, g_ibsp.send_count, g_ibsp.recv_count));
#endif

			IBSP_PRINT(TRACE_LEVEL_INFORMATION,IBSP_DBG_WQ, 
					 ("set internal overlapped=0x%p Internal=%Id OffsetHigh=%d\n",
					  lpOverlapped, lpOverlapped->Internal,
					  lpOverlapped->OffsetHigh));

			lpOverlapped->Internal = 0;
		}
		else
		{
#ifdef _DEBUG_
			cl_atomic_dec(&g_ibsp.overlap_h0_count);


			fzprint(("%s():%d:0x%x:0x%x: ov=0x%p h0=%d h1=%d h1_c=%d send=%d recv=%d\n",
					 __FUNCTION__, __LINE__, GetCurrentProcessId(),
					 GetCurrentThreadId(), lpOverlapped,
					 g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count,
					 g_ibsp.overlap_h1_comp_count, g_ibsp.send_count, g_ibsp.recv_count));
#endif
			IBSP_PRINT(TRACE_LEVEL_INFORMATION,IBSP_DBG_WQ,
					 (" calls lpWPUCompleteOverlappedRequest, overlapped=0x%p OffsetHigh=%d "
					 "InternalHigh=%Id hEvent=%p\n",
					   lpOverlapped, lpOverlapped->OffsetHigh,
					  lpOverlapped->InternalHigh, lpOverlapped->hEvent));

			ret = g_ibsp.up_call_table.lpWPUCompleteOverlappedRequest
				(socket_info->switch_socket,
				 lpOverlapped,
				 lpOverlapped->OffsetHigh, (DWORD) lpOverlapped->InternalHigh, &error);

			if( ret != 0 )
			{
				IBSP_ERROR( ("lpWPUCompleteOverlappedRequest failed with %d/%d\n", ret, error) );
			}
		}
		cl_atomic_dec( &socket_info->dup_cnt );
	}

	IBSP_EXIT( IBSP_DBG_EP );
}


/* Closes a connection and release its ressources. */
void
shutdown_and_destroy_socket_info(
	IN	OUT			struct ibsp_socket_info		*socket_info )
{
	enum ibsp_socket_state	old_state;

	IBSP_ENTER( IBSP_DBG_EP );

	cl_spinlock_acquire( &socket_info->mutex1 );
	old_state = socket_info->socket_state;
	IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_CLOSED );
	cl_spinlock_release( &socket_info->mutex1 );

	if( socket_info->listen.handle )
	{
		/* Stop listening and reject queued connections. */
		ib_listen_cancel( socket_info );
	}

	cl_spinlock_acquire( &g_ibsp.socket_info_mutex );
	cl_qlist_remove_item( &g_ibsp.socket_info_list, &socket_info->item );

	switch( old_state )
	{
	case IBSP_CREATE:
	case IBSP_LISTEN:
		/* Nothing to do. */
		break;

	case IBSP_CONNECTED:
		{
			struct disconnect_reason reason;

			memset( &reason, 0, sizeof(reason) );
			reason.type = DISC_SHUTDOWN;
			ib_disconnect( socket_info, &reason );
		}
		/* Fall through. */

	case IBSP_CONNECT:
	case IBSP_DISCONNECTED:
		/* We changed the state - remove from connection map. */
		CL_ASSERT( socket_info->conn_item.p_map );
		cl_rbmap_remove_item( &g_ibsp.conn_map, &socket_info->conn_item );
		break;
	}
	cl_spinlock_release( &g_ibsp.socket_info_mutex );

	/* Flush all completions. */
	if( socket_info->dup_cnt )
		ibsp_dup_overlap_abort( socket_info );

	while( socket_info->send_cnt || socket_info->recv_cnt )
		ib_cq_comp( socket_info->cq_tinfo );

	ibsp_dereg_socket( socket_info );

	ib_destroy_socket( socket_info );

#ifdef IBSP_LOGGING
	DataLogger_Shutdown(&socket_info->SendDataLogger);
	DataLogger_Shutdown(&socket_info->RecvDataLogger);
#endif

	/* Release the initial reference and clean up. */
	deref_socket_info( socket_info );

	IBSP_EXIT( IBSP_DBG_EP );
}


boolean_t
ibsp_conn_insert(
	IN				struct ibsp_socket_info		*s )
{
	struct ibsp_socket_info		*p_sock;
	cl_rbmap_item_t				*p_item, *p_insert_at;
	boolean_t					left = TRUE;

	cl_spinlock_acquire( &g_ibsp.socket_info_mutex );
	p_item = cl_rbmap_root( &g_ibsp.conn_map );
	p_insert_at = p_item;

	CL_ASSERT( !s->conn_item.p_map );
	while( p_item != cl_rbmap_end( &g_ibsp.conn_map ) )
	{
		p_insert_at = p_item;
		p_sock = PARENT_STRUCT( p_item, struct ibsp_socket_info, conn_item );
		if( p_sock->local_addr.sin_family < s->local_addr.sin_family )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_sock->local_addr.sin_family > s->local_addr.sin_family )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else if( p_sock->local_addr.sin_addr.S_un.S_addr < s->local_addr.sin_addr.S_un.S_addr )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_sock->local_addr.sin_addr.S_un.S_addr > s->local_addr.sin_addr.S_un.S_addr )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else if( p_sock->local_addr.sin_port < s->local_addr.sin_port )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_sock->local_addr.sin_port > s->local_addr.sin_port )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else if( p_sock->peer_addr.sin_family < s->peer_addr.sin_family )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_sock->peer_addr.sin_family > s->peer_addr.sin_family )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else if( p_sock->peer_addr.sin_addr.S_un.S_addr < s->peer_addr.sin_addr.S_un.S_addr )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_sock->peer_addr.sin_addr.S_un.S_addr > s->peer_addr.sin_addr.S_un.S_addr )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else if( p_sock->peer_addr.sin_port < s->peer_addr.sin_port )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_sock->peer_addr.sin_port > s->peer_addr.sin_port )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else
			goto done;
	}

	cl_rbmap_insert( &g_ibsp.conn_map, p_insert_at, &s->conn_item, left );

done:
	cl_spinlock_release( &g_ibsp.socket_info_mutex );
	return p_item == cl_rbmap_end( &g_ibsp.conn_map );
}


void
ibsp_conn_remove(
	IN				struct ibsp_socket_info		*s )
{
	cl_spinlock_acquire( &g_ibsp.socket_info_mutex );
	CL_ASSERT( s->conn_item.p_map );
	cl_rbmap_remove_item( &g_ibsp.conn_map, &s->conn_item );
	cl_spinlock_release( &g_ibsp.socket_info_mutex );
}
