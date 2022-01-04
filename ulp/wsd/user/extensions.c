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
#include "extensions.tmh"
#endif

#include "ibspdll.h"


/* Function: IBSPRegisterMemory
 *  Description:
 *    Registers buffer memory
 */
HANDLE WSPAPI
IBSPRegisterMemory(
	IN				SOCKET						s,
	IN				PVOID						lpBuffer,
	IN				DWORD						dwBufferLength,
	IN				DWORD						dwFlags,
		OUT			LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	ib_access_t access_ctrl;
	struct memory_node *node;

	IBSP_ENTER( IBSP_DBG_MEM );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	if( lpBuffer == NULL )
	{
		IBSP_ERROR_EXIT( ( "invalid buffer %p\n", lpBuffer ) );
		*lpErrno = WSAEFAULT;
		return NULL;
	}

	if( dwBufferLength > socket_info->socket_options.max_msg_size )
	{
		IBSP_ERROR_EXIT( ( "invalid buffer length %d\n", dwBufferLength ) );
		*lpErrno = WSAEFAULT;
		return NULL;
	}

	switch( dwFlags )
	{
	case MEM_READ:
		access_ctrl = 0;
		break;

	case MEM_WRITE:
		access_ctrl = IB_AC_LOCAL_WRITE;
		break;

	case MEM_READWRITE:
		access_ctrl = IB_AC_LOCAL_WRITE;
		break;

	default:
		IBSP_ERROR_EXIT( ("invalid flags %x\n", dwFlags) );
		*lpErrno = WSAEINVAL;
		return NULL;
	}

	node = ibsp_reg_mem( socket_info, socket_info->hca_pd,
		lpBuffer, dwBufferLength, access_ctrl, lpErrno );

	fzprint(("%s():%d:0x%x:0x%x: registering MEM from %p to %p, len %d, handle %p\n",
			 __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(),
			 lpBuffer, (unsigned char *)lpBuffer + dwBufferLength, dwBufferLength, node));


	if( node == NULL )
	{
		IBSP_ERROR( ("ibsp_reg_mem failed (pd=%p)\n", socket_info->hca_pd) );
		*lpErrno = WSAENOBUFS;
	}
	else
	{
		IBSP_PRINT(TRACE_LEVEL_INFORMATION,
			IBSP_DBG_MEM, ("returning node %p\n", node) );
		*lpErrno = 0;
	}

	IBSP_EXIT( IBSP_DBG_MEM );

	return (HANDLE) node;
}

/* Function: IBSPDeregisterMemory
 *  Description:
 *    This is our provider's DeregisterMemory function.
 */
int WSPAPI
IBSPDeregisterMemory(
	IN				SOCKET						s,
	IN				HANDLE						handle,
		OUT			LPINT						lpErrno )
{
	struct memory_node *node = handle;
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	int ret;

	IBSP_ENTER( IBSP_DBG_MEM );

	fzprint(("%s():%d:0x%x:0x%x: handle=0x%p socket=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), handle, s));

	if( s == INVALID_SOCKET )
	{
		IBSP_ERROR_EXIT( ("invalid socket handle %Ix\n", s) );
		*lpErrno = WSAENOTSOCK;
		return SOCKET_ERROR;
	}

	ret = ibsp_dereg_mem( socket_info, node, lpErrno );

	fzprint(("%s():%d:0x%x:0x%x: unregistering MEM %p, mr_num=%d, ret=%d\n",
			 __FUNCTION__,
			 __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), node, g_ibsp.mr_num, ret));

	IBSP_EXIT( IBSP_DBG_MEM );
	return ret;
}

/* Function: IBSPRegisterRdmaMemory
 *  Description:
 *    This is our provider's RegisterRdmaMemory function.
*/
int WSPAPI
IBSPRegisterRdmaMemory(
	IN				SOCKET						s,
	IN				PVOID						lpBuffer,
	IN				DWORD						dwBufferLength,
	IN				DWORD						dwFlags,
		OUT			LPVOID						lpRdmaBufferDescriptor,
	IN	OUT			LPDWORD						lpdwDescriptorLength,
		OUT			LPINT						lpErrno )
{
	struct memory_node *node2;
	struct rdma_memory_desc *desc;
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	ib_access_t access_ctrl;
	struct ibsp_hca *hca;

	IBSP_ENTER( IBSP_DBG_MEM );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	if( *lpdwDescriptorLength < sizeof(struct rdma_memory_desc) )
	{
		/* This is the probe from the switch to learn the length of the descriptor. */
		IBSP_ERROR_EXIT( ("invalid descriptor length %d (usually not an error)\n",
			*lpdwDescriptorLength) );
		*lpdwDescriptorLength = sizeof(struct rdma_memory_desc);
		*lpErrno = WSAEFAULT;
		return SOCKET_ERROR;
	}

	if( lpBuffer == NULL )
	{
		IBSP_ERROR_EXIT( ("invalid buffer %p\n", lpBuffer) );
		*lpErrno = WSAEFAULT;
		return SOCKET_ERROR;
	}

	if( dwBufferLength > socket_info->socket_options.max_msg_size )
	{
		IBSP_ERROR_EXIT( ("invalid buffer length %d\n", dwBufferLength) );
		*lpErrno = WSAEFAULT;
		return SOCKET_ERROR;
	}

	switch( dwFlags )
	{
	case MEM_READ:
		access_ctrl = IB_AC_RDMA_READ;
		break;

	case MEM_WRITE:
		access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE;
		break;

	case MEM_READWRITE:
		access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_RDMA_READ | IB_AC_RDMA_WRITE;
		break;

	default:
		IBSP_ERROR_EXIT( ("invalid flags %x\n", dwFlags) );
		*lpErrno = WSAEINVAL;
		return SOCKET_ERROR;
	}

	hca = socket_info->port->hca;

	/** TODO: Fix locking so we dont' dereference node outside of mutex. */
	node2 = ibsp_reg_mem( socket_info, hca->pd,
		lpBuffer, dwBufferLength, access_ctrl, lpErrno );

	if( !node2 )
	{
		IBSP_ERROR_EXIT( ("ibsp_reg_mem failed %d\n", *lpErrno) );
		*lpErrno = WSAENOBUFS;
		return SOCKET_ERROR;
	}

	desc = lpRdmaBufferDescriptor;

	desc->iova = (uint64_t) (uintptr_t) lpBuffer;
	desc->lkey = node2->p_reg1->lkey;
	desc->rkey = node2->p_reg1->rkey;
	desc->node1 = node2;

	*lpErrno = 0;

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_MEM,
		("Socket %Ix registered RDMA MEM at %p, len %d, for access %d, "
		"returning handle %p, rkey %08x\n",
		s, lpBuffer, dwBufferLength, dwFlags, node2, desc->rkey));


	IBSP_EXIT( IBSP_DBG_MEM );

	return 0;
}

/* Function: IBSPDeregisterRdmaMemory
 *  Description:
 *    This is our provider's DeregisterRdmaMemory function.
 */
int WSPAPI
IBSPDeregisterRdmaMemory(
	IN				SOCKET						s,
	IN				LPVOID						lpRdmaBufferDescriptor,
	IN				DWORD						dwDescriptorLength,
		OUT			LPINT						lpErrno )
{
	struct rdma_memory_desc *desc;
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	int ret;

	IBSP_ENTER( IBSP_DBG_MEM );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	if( s == INVALID_SOCKET )
	{
		/* Seen in real life with overlap/client test.
		 * The switch closes a socket then calls this. Why? */
		IBSP_ERROR_EXIT( ("invalid socket handle %Ix\n", s) );
		*lpErrno = WSAENOTSOCK;
		return SOCKET_ERROR;
	}

	CL_ASSERT( lpRdmaBufferDescriptor );

	if( dwDescriptorLength < sizeof(struct rdma_memory_desc) )
	{
		IBSP_ERROR_EXIT( ("invalid descriptor length %d)\n", dwDescriptorLength) );
		*lpErrno = WSAEINVAL;
		return SOCKET_ERROR;
	}

	desc = lpRdmaBufferDescriptor;

	ret = ibsp_dereg_mem( socket_info, desc->node1, lpErrno );

	fzprint(("%s():%d:0x%x:0x%x: Unregistering RDMA MEM %p\n",
			 __FUNCTION__, __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), desc->node));

	IBSP_EXIT( IBSP_DBG_MEM );
	return ret;
}


/*
 * Do a RDMA read or write operation since the code for both is very close. 
 */
static int
do_rdma_op(
	IN				SOCKET						s,
	IN				LPWSABUFEX					lpBuffers,
	IN				DWORD						dwBufferCount,
	IN				LPVOID						lpTargetBufferDescriptor,
	IN				DWORD						dwTargetDescriptorLength,
	IN				DWORD						dwTargetBufferOffset,
	IN				LPWSAOVERLAPPED				lpOverlapped,
	IN				ib_wr_type_t				wr_type,
		OUT			LPINT						lpErrno )
{
	struct ibsp_socket_info	*socket_info = (struct ibsp_socket_info *)s;
	ib_api_status_t			status;
	struct rdma_memory_desc	*desc;	/* remote descriptor */
	struct _wr				*wr;
	ib_send_wr_t			send_wr;
	ib_local_ds_t			local_ds[QP_ATTRIB_SQ_SGE];
	DWORD					ds_idx;

	IBSP_ENTER( IBSP_DBG_IO );

	CL_ASSERT( wr_type == WR_RDMA_WRITE || wr_type == WR_RDMA_READ );

	cl_spinlock_acquire( &socket_info->mutex1 );
	switch( socket_info->socket_state )
	{
	case IBSP_CONNECTED:
	case IBSP_DISCONNECTED:
		break;

	default:
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_ERROR_EXIT( ("Socket is not in connected socket_state state=%s\n",
			IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );
		*lpErrno = WSAENOTCONN;
		return SOCKET_ERROR;
	}
	cl_spinlock_release( &socket_info->mutex1 );

	if( socket_info->qp_error )
	{
		IBSP_ERROR_EXIT( ("QP is in error state %d\n", socket_info->qp_error) );
		*lpErrno = socket_info->qp_error;
		return SOCKET_ERROR;
	}

	/* This function only works for that case. */
	if( dwBufferCount > QP_ATTRIB_SQ_SGE )
	{
		CL_ASSERT( dwBufferCount <= QP_ATTRIB_SQ_SGE );
		/* TODO - support splitting large requests into multiple RDMA operations. */
		IBSP_ERROR_EXIT( 
			("dwBufferCount is greater than %d\n", QP_ATTRIB_SQ_SGE) );
		*lpErrno = WSAEINVAL;
		return SOCKET_ERROR;
	}

	if( dwTargetDescriptorLength != sizeof(struct rdma_memory_desc) )
	{
		IBSP_ERROR_EXIT( (
			"invalid descriptor length %d)\n", dwTargetDescriptorLength) );
		*lpErrno = WSAEINVAL;
		return SOCKET_ERROR;
	}

	desc = lpTargetBufferDescriptor;

	/* The send lock is only used to serialize posting. */
	cl_spinlock_acquire( &socket_info->send_lock );
	if( socket_info->send_cnt == QP_ATTRIB_SQ_DEPTH )
	{
		/* TODO: queue requests. */
		cl_spinlock_release( &socket_info->send_lock );
		IBSP_ERROR_EXIT( ("not enough wr on the free list\n") );
		*lpErrno = WSAENETDOWN;
		return SOCKET_ERROR;
	}

	wr = &socket_info->send_wr[socket_info->send_idx];

	wr->lpOverlapped = lpOverlapped;
	wr->socket_info = socket_info;

	/* Format the send work request and post. */
	send_wr.p_next = NULL;
	send_wr.wr_id = (ULONG_PTR)wr;
	send_wr.wr_type = wr_type;
	send_wr.send_opt = 0;
	send_wr.num_ds = dwBufferCount;
	send_wr.ds_array = local_ds;

	send_wr.remote_ops.vaddr = desc->iova + dwTargetBufferOffset;
	send_wr.remote_ops.rkey = desc->rkey;

	lpOverlapped->InternalHigh = 0;
	for( ds_idx = 0; ds_idx < dwBufferCount; ds_idx++ )
	{
		local_ds[ds_idx].vaddr = (ULONG_PTR)lpBuffers[ds_idx].buf;
		local_ds[ds_idx].length = lpBuffers[ds_idx].len;
		local_ds[ds_idx].lkey =
			((struct memory_node*)lpBuffers[ds_idx].handle)->p_reg1->lkey;

		lpOverlapped->InternalHigh += lpBuffers[ds_idx].len;
	}

	if( wr_type == WR_RDMA_READ )
	{
		/*
		 * Next send must be fenced since it could indicate that this
		 * RDMA READ is complete.
		 */
		socket_info->send_opt = IB_SEND_OPT_FENCE;
	}
	else if( lpOverlapped->InternalHigh <= socket_info->max_inline )
	{
		send_wr.send_opt |= IB_SEND_OPT_INLINE;
	}

	/*
	 * We must set this now, because the operation could complete
	 * before ib_post_send returns.
	 */
	lpOverlapped->Internal = WSS_OPERATION_IN_PROGRESS;

	cl_atomic_inc( &socket_info->send_cnt );

#ifdef _DEBUG_
	if( lpOverlapped->hEvent == 0 )
	{
		cl_atomic_inc( &g_ibsp.overlap_h0_count );
	}
	else
	{
		cl_atomic_inc( &g_ibsp.overlap_h1_count );
		cl_atomic_inc( &g_ibsp.overlap_h1_comp_count );
	}

	fzprint(("%s():%d:0x%x:0x%x: ov=0x%p h0_cnt=%d h1_cnt=%d\n",
			 __FUNCTION__, __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), lpOverlapped,
			 g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count));

#endif

	status = ib_post_send( socket_info->qp, &send_wr, NULL );

	if( status == IB_SUCCESS )
	{
		/* Update the index and wrap as needed */
#if QP_ATTRIB_SQ_DEPTH == 256 || QP_ATTRIB_SQ_DEPTH == 128 || \
	QP_ATTRIB_SQ_DEPTH == 64 || QP_ATTRIB_SQ_DEPTH == 32 || \
	QP_ATTRIB_SQ_DEPTH == 16 || QP_ATTRIB_SQ_DEPTH == 8
		socket_info->send_idx++;
		socket_info->send_idx &= (QP_ATTRIB_SQ_DEPTH - 1);
#else
		if( ++socket_info->send_idx == QP_ATTRIB_SQ_DEPTH )
			socket_info->send_idx = 0;
#endif

		*lpErrno = WSA_IO_PENDING;

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
			("Posted RDMA: socket=%Ix, ov=%p, type=%d, local=%p, len=%d, "
			"dest=%016I64x, rkey=%08x\n",
			s, lpOverlapped, wr_type, lpBuffers[0].buf, lpBuffers[0].len,
			send_wr.remote_ops.vaddr, send_wr.remote_ops.rkey) );

		fzprint(("posted RDMA %p, len=%d, op=%d, mr handle=%p\n",
				lpOverlapped, lpBuffers[0].len, wr_type, node));
	}
	else
	{
		IBSP_ERROR( ("ib_post_send returned %s\n", ib_get_err_str( status )) );

#ifdef _DEBUG_

		if( lpOverlapped->hEvent == 0 )
		{
			cl_atomic_dec( &g_ibsp.overlap_h0_count );
		}
		else
		{
			cl_atomic_dec( &g_ibsp.overlap_h1_count );
			cl_atomic_dec( &g_ibsp.overlap_h1_comp_count );
		}

		memset( wr, 0x44, sizeof(struct _wr) );
#endif
		cl_atomic_dec( &socket_info->send_cnt );

		*lpErrno = ibal_to_wsa_error( status );
	}

	cl_spinlock_release( &socket_info->send_lock );

	/* We never complete the operation here. */
	IBSP_EXIT( IBSP_DBG_IO );
	return SOCKET_ERROR;
}


/* Function: IBSPRdmaWrite
 Description:
    This is our provider's RdmaWrite function. When an app calls WSAIoctl
    to request the function pointer to RdmaWrite, we return pointer to this 
	function and this function is called by application directly using the function pointer.
*/
int WSPAPI
IBSPRdmaWrite(
	IN				SOCKET						s,
	IN				LPWSABUFEX					lpBuffers,
	IN				DWORD						dwBufferCount,
	IN				LPVOID						lpTargetBufferDescriptor,
	IN				DWORD						dwTargetDescriptorLength,
	IN				DWORD						dwTargetBufferOffset,
		OUT			LPDWORD						lpdwNumberOfBytesWritten,
	IN				DWORD						dwFlags,
	IN				LPWSAOVERLAPPED				lpOverlapped,
	IN				LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
	IN				LPWSATHREADID				lpThreadId,
		OUT			LPINT						lpErrno )
{
	int ret;

	IBSP_ENTER( IBSP_DBG_IO );

	UNUSED_PARAM( lpThreadId );
	UNUSED_PARAM( lpCompletionRoutine );
	UNUSED_PARAM( lpdwNumberOfBytesWritten );

	if( s == INVALID_SOCKET )
	{
		IBSP_ERROR_EXIT( ("invalid socket handle %Ix\n", s) );
		*lpErrno = WSAENOTSOCK;
		return SOCKET_ERROR;
	}

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p overlapped=0x%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s, lpOverlapped));

	/* Store the flags for reporting back in IBSPGetOverlappedResult */
	lpOverlapped->Offset = dwFlags;

	ret = do_rdma_op( s, lpBuffers, dwBufferCount, lpTargetBufferDescriptor,
		dwTargetDescriptorLength, dwTargetBufferOffset,
		lpOverlapped, WR_RDMA_WRITE, lpErrno );

	IBSP_EXIT( IBSP_DBG_IO );

	return ret;
}


/* Function: IBSPRdmaRead
 Description:
    This is our provider's RdmaRead function. When an app calls WSAIoctl
    to request the function pointer to RdmaRead, we return pointer to this 
	function and this function is called by application directly using the function pointer.
*/
int WSPAPI
IBSPRdmaRead(
	IN				SOCKET						s,
	IN				LPWSABUFEX					lpBuffers,
	IN				DWORD						dwBufferCount,
	IN				LPVOID						lpTargetBufferDescriptor,
	IN				DWORD						dwTargetDescriptorLength,
	IN				DWORD						dwTargetBufferOffset,
		OUT			LPDWORD						lpdwNumberOfBytesRead,
	IN				DWORD						dwFlags,
	IN				LPWSAOVERLAPPED				lpOverlapped,
	IN				LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
	IN				LPWSATHREADID				lpThreadId,
		OUT			LPINT						lpErrno )
{
	int ret;

	IBSP_ENTER( IBSP_DBG_IO );

	UNUSED_PARAM( lpThreadId );
	UNUSED_PARAM( lpCompletionRoutine );
	UNUSED_PARAM( lpdwNumberOfBytesRead );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p overlapped=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s, lpOverlapped));

	/* Store the flags for reporting back in IBSPGetOverlappedResult */
	lpOverlapped->Offset = dwFlags;

	ret = do_rdma_op( s, lpBuffers, dwBufferCount, lpTargetBufferDescriptor,
		dwTargetDescriptorLength, dwTargetBufferOffset,
		lpOverlapped, WR_RDMA_READ, lpErrno );

	IBSP_EXIT( IBSP_DBG_IO );

	return ret;
}


/* Function: IBSPMemoryRegistrationCacheCallback
 *  Description:
 *   This is our provider's MemoryRegistrationCacheCallback
 *   function. When an app calls WSAIoctl to request the function
 *   pointer to MemoryRegistrationCacheCallback, we return pointer to
 *   this function and this function is called by application directly
 *   using the function pointer.
 */
int WSPAPI
IBSPMemoryRegistrationCacheCallback(
	IN				LPVOID						lpvAddress,
	IN				SIZE_T						Size,
		OUT			LPINT						lpErrno )
{
	cl_list_item_t		*p_item;

	IBSP_ENTER( IBSP_DBG_MEM );

	UNUSED_PARAM( lpErrno );

	cl_spinlock_acquire( &g_ibsp.hca_mutex );
	for( p_item = cl_qlist_head( &g_ibsp.hca_list );
		p_item != cl_qlist_end( &g_ibsp.hca_list );
		p_item = cl_qlist_next( p_item ) )
	{
		ibsp_hca_flush_mr_cache(
			PARENT_STRUCT( p_item, struct ibsp_hca, item ), lpvAddress, Size );
	}
	cl_spinlock_release( &g_ibsp.hca_mutex );

	IBSP_EXIT( IBSP_DBG_MEM );
	return 0;
}
