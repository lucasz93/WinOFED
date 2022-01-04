/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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
#include "sockinfo.tmh"
#endif

#include "ibspdll.h"


static void
free_socket_info(
	IN				struct ibsp_socket_info		*socket_info );

/* 
 * Function: create_socket_info
 * 
 *  Description:
 *    Allocates a new socket info context structure and initializes some fields.
*/
struct ibsp_socket_info *
create_socket_info(
	OUT				LPINT						lpErrno )
{
	struct ibsp_socket_info	*socket_info;

	IBSP_ENTER( IBSP_DBG_SI );

	socket_info = HeapAlloc( g_ibsp.heap,
		HEAP_ZERO_MEMORY, sizeof(struct ibsp_socket_info) );
	if( socket_info == NULL )
	{
		IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_SI,
			("HeapAlloc() failed: %d\n", GetLastError()) );
		*lpErrno = WSAENOBUFS;
		return NULL;
	}

	cl_spinlock_construct( &socket_info->mutex1 );
	cl_spinlock_construct( &socket_info->send_lock );
	cl_spinlock_construct( &socket_info->recv_lock );
	cl_qlist_init( &socket_info->mr_list );
	cl_qlist_init( &socket_info->listen.list );

	if( cl_spinlock_init( &socket_info->mutex1 ) != CL_SUCCESS )
		goto err;

	if( cl_spinlock_init( &socket_info->send_lock ) != CL_SUCCESS )
		goto err;

	if( cl_spinlock_init( &socket_info->recv_lock ) != CL_SUCCESS )
		goto err;

#ifdef _DEBUG_
	memset( socket_info->send_wr, 0x38, sizeof(socket_info->send_wr) );
	memset( socket_info->recv_wr, 0x38, sizeof(socket_info->recv_wr) );
	memset( socket_info->dup_wr, 0x38, sizeof(socket_info->dup_wr) );
#endif

	socket_info->switch_socket =
		g_ibsp.up_call_table.lpWPUCreateSocketHandle(
		0, (DWORD_PTR)socket_info, lpErrno );

	if( socket_info->switch_socket == INVALID_SOCKET )
	{
		IBSP_ERROR( ("WPUCreateSocketHandle() failed: %d", *lpErrno) );
err:
		free_socket_info( socket_info );
		IBSP_EXIT( IBSP_DBG_SI );
		return NULL;
	}

	STAT_INC( wpusocket_num );

	/*
	 * Preset to 1, IBSPCloseSocket will decrement it, and switch socket
	 * will be freed once it goes to zero.
	 */
	socket_info->ref_cnt1 = 1;

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_SI, ("socket_info (%p), switch socket (%p)\n",
		socket_info, (void*)socket_info->switch_socket) );

	IBSP_EXIT( IBSP_DBG_SI );
	return socket_info;
}


static void
free_socket_info(
	IN				struct ibsp_socket_info		*p_socket )
{
	int		ret, error;

	if( p_socket->switch_socket != INVALID_SOCKET )
	{
		/* ref_cnt hit zero - destroy the switch socket. */
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_SI,
			("socket=0x%p calling lpWPUCloseSocketHandle=0x%p\n",
			p_socket, (void*)p_socket->switch_socket) );

		ret = g_ibsp.up_call_table.lpWPUCloseSocketHandle(
			p_socket->switch_socket, &error );
		if( ret == SOCKET_ERROR )
		{
			IBSP_ERROR( ("WPUCloseSocketHandle failed: %d\n", error) );
		}
		else
		{
			STAT_DEC( wpusocket_num );
		}
		p_socket->switch_socket = INVALID_SOCKET;
	}

	CL_ASSERT( !p_socket->qp );
	CL_ASSERT( !p_socket->conn_item.p_map );
	CL_ASSERT(!p_socket->send_cnt && !p_socket->recv_cnt);
	cl_spinlock_destroy( &p_socket->mutex1 );

	cl_spinlock_destroy( &p_socket->send_lock );
	cl_spinlock_destroy( &p_socket->recv_lock );

	HeapFree( g_ibsp.heap, 0, p_socket );
}


/* 
 * Function: deref_sock_info
 * 
 * Description:
 *	This routine decrements a socket context's reference count, and if
 *	it reaches zero, frees the socket context structure.
 */
void AL_API
deref_socket_info(
	IN				struct ibsp_socket_info		*p_socket )
{
	IBSP_ENTER( IBSP_DBG_SI );

	if( !cl_atomic_dec( &p_socket->ref_cnt1 ) )
	{
		free_socket_info( p_socket );
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_SI, ("Freed socket_info (%p)\n", p_socket) );
	}
	IBSP_EXIT( IBSP_DBG_SI );
}
