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

#include "ibsp_duplicate.tmh"
#endif

#include "ibspdll.h"
#include "rpc.h"


/* 
http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dllproc/base/using_shared_memory_in_a_dynamic_link_library.asp
*/


static void
create_name(
		OUT			char						*fname,
	IN		const	DWORD						dwProcessId,
	IN		const	GUID						*p_guid )
{
	sprintf( fname, "Global\\%s-WSD-%08lx-"
		"%08lx-%04hx-%04hx-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x",
		VER_PROVIDER, dwProcessId, p_guid->Data1, p_guid->Data2, p_guid->Data3,
		(int)p_guid->Data4[0], (int)p_guid->Data4[1],
		(int)p_guid->Data4[2], (int)p_guid->Data4[3],
		(int)p_guid->Data4[4], (int)p_guid->Data4[5],
		(int)p_guid->Data4[6], (int)p_guid->Data4[7] );
}


/* Create a duplicated socket. param is given by the other process through the 
 * lpProtocolInfo->dwProviderReserved field.
 * This function is called by the next-controlling process. */
int
setup_duplicate_socket(
	IN				struct ibsp_socket_info		*socket_info,
	IN				HANDLE						h_dup_info )
{
	int ret, err;
	struct ibsp_duplicate_info *dup_info;
	ib_net64_t dest_port_guid;
	ib_path_rec_t path_rec;
	ib_path_rec_t alt_path_rec, *palt_path_rec = NULL;

	IBSP_ENTER( IBSP_DBG_DUP );

	CL_ASSERT( socket_info->socket_state == IBSP_CREATE );

	/* Get a pointer to the file-mapped shared memory. */
	dup_info = MapViewOfFile( h_dup_info, FILE_MAP_READ, 0, 0, 0 );
	if( dup_info == NULL )
	{
		IBSP_ERROR( ("MapViewOfFile failed with %d\n", GetLastError()) );
		ret = WSAENETDOWN;
		goto err1;
	}

	socket_info->peer_addr = dup_info->peer_addr;
	socket_info->local_addr = dup_info->local_addr;
	socket_info->socket_options = dup_info->socket_options;
	socket_info->duplicate.dwProcessId = dup_info->dwProcessId;
	socket_info->duplicate.identifier = dup_info->identifier;

	socket_info->port = get_port_from_ip_address( dup_info->local_addr.sin_addr );
	if( socket_info->port == NULL )
	{
		IBSP_ERROR( ("incoming destination IP address not local (%s)\n",
			inet_ntoa( dup_info->local_addr.sin_addr )) );
		ret = WSAENETDOWN;
		goto err1;
	}

	/* Get the GUID for the remote IP address. */
	ret = query_guid_address(
        (struct sockaddr*)&socket_info->local_addr,
        (struct sockaddr*)&socket_info->peer_addr,
        &dest_port_guid );
	if( ret )
	{
		IBSP_ERROR( ("query_guid_address failed for IP %08x\n",
			socket_info->peer_addr.sin_addr.s_addr) );
		ret = WSAENETDOWN;
		goto err1;
	}

	/* Get the path record */
	ret = query_pr( socket_info->port->guid, dest_port_guid, socket_info->port->hca->dev_id, &path_rec );
	if( ret )
	{
		IBSP_ERROR( ("query_pr failed for IP %08x\n",
			socket_info->peer_addr.sin_addr.s_addr) );
		ret = WSAENETDOWN;
		goto err1;
	}
	/* Get the alternate path record */
	if (g_use_APM) 
	{
		ret = query_pr(GetOtherPortGuid(socket_info->port->guid), GetOtherPortGuid(dest_port_guid), socket_info->port->hca->dev_id, &alt_path_rec );
		if( ret )
		{	
			// We can ignore a failure here, since APM is not a MUST
			IBSP_ERROR( ("QPR for alternate path failed (error ignored)\n") );
		}
		else 
		{
			palt_path_rec = &alt_path_rec;
		}
	}


	IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_DUPLICATING_NEW );
	socket_info->h_event = CreateEvent( NULL, FALSE, FALSE, NULL );
	if( !socket_info->h_event )
	{
		IBSP_ERROR( ("CreateEvent failed (%d)\n", GetLastError()) );
		goto err1;
	}

	ret = ib_create_socket( socket_info );
	if( ret )
	{
		IBSP_ERROR( ("ib_create socket failed with %d\n", ret) );
		goto err1;
	}

	/* Connects the QP. */
	ret = ib_connect( socket_info, &path_rec, palt_path_rec );
	if( ret != WSAEWOULDBLOCK )
	{
		IBSP_ERROR( ("ib_connect failed (%d)\n", ret) );
		goto err2;
	}

	if( WaitForSingleObject( socket_info->h_event, INFINITE ) != WAIT_OBJECT_0 )
		IBSP_ERROR( ("WaitForSingleObject failed\n") );

	cl_spinlock_acquire( &socket_info->mutex1 );
	if( socket_info->socket_state != IBSP_CONNECTED )
	{
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_ERROR( ("Failed to connect\n") );
		ret = WSAENETDOWN;
err2:
		g_ibsp.up_call_table.lpWPUCloseSocketHandle(
			socket_info->switch_socket, &err );
		socket_info->switch_socket = INVALID_SOCKET;
		STAT_DEC( wpusocket_num );

		ib_destroy_socket( socket_info );
	}
	else
	{
		ret = 0;
		cl_spinlock_release( &socket_info->mutex1 );
	}

err1:
	if( socket_info->h_event )
	{
		CloseHandle( socket_info->h_event );
		socket_info->h_event = NULL;
	}

	CloseHandle( h_dup_info );

	IBSP_EXIT( IBSP_DBG_DUP );
	return ret;
}


/* Function: IBSPDuplicateSocket

 Description:
    This function provides a WSAPROTOCOL_INFOW structure which can be passed
    to another process to open a handle to the same socket. First we need
    to translate the user socket into the provider socket and call the underlying
    WSPDuplicateSocket. Note that the lpProtocolInfo structure passed into us
    is an out parameter only!
*/
int WSPAPI
IBSPDuplicateSocket(
					SOCKET						s,
					DWORD						dwProcessId,
					LPWSAPROTOCOL_INFOW			lpProtocolInfo,
					LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	struct ibsp_duplicate_info *dup_info = NULL;
	char fname[100];
	GUID guid;
	HANDLE h_dup_info, h_target_process, h_target_dup_info;
	struct disconnect_reason reason;

	IBSP_ENTER( IBSP_DBG_DUP );

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_DUP,
		("Duplicating socket=0x%p to dwProcessId=0x%x \n",
		socket_info, dwProcessId) );

	cl_spinlock_acquire( &socket_info->mutex1 );
	if( socket_info->socket_state != IBSP_CONNECTED )
	{
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_DUP,
			("Socket state not IBSP_CONNECTED, state=%s.\n",
			IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );
		*lpErrno = WSAENOTCONN;
		return SOCKET_ERROR;
	}

	/* Create a GUID to use as unique identifier for this duplication. */
	UuidCreate( &guid );
	create_name( fname, dwProcessId, &guid );

	h_dup_info = CreateFileMapping( INVALID_HANDLE_VALUE, NULL,
		PAGE_READWRITE, 0, sizeof(struct ibsp_duplicate_info), fname );
	if( !h_dup_info )
	{
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_ERROR_EXIT( ("CreateFileMapping for %s failed with %d\n",
			fname, GetLastError()) );
		*lpErrno = WSAENETDOWN;
		return SOCKET_ERROR;
	}

	/* Get a pointer to the file-mapped shared memory. */
	dup_info = MapViewOfFile( h_dup_info, FILE_MAP_WRITE, 0, 0, 0 );
	if( !dup_info )
	{
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_ERROR_EXIT( ("MapViewOfFile failed with %d\n", GetLastError()) );
		CloseHandle( h_dup_info );
		*lpErrno = WSAENETDOWN;
		return SOCKET_ERROR;
	}

	/* 
	 * Store addressing information so that the duplicating
	 * process can reconnect.
	 */
	dup_info->identifier = guid;
	dup_info->socket_options = socket_info->socket_options;
	dup_info->peer_addr = socket_info->peer_addr;
	dup_info->local_addr = socket_info->local_addr;
	dup_info->dwProcessId = dwProcessId;

	/* Release the reference on the underlying file */
	UnmapViewOfFile( dup_info );

	/* Open the target process. */
	h_target_process = OpenProcess( PROCESS_DUP_HANDLE, FALSE, dwProcessId );
	if( !h_target_process )
	{
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_ERROR_EXIT( ("OpenProcess failed with %d\n", GetLastError()) );
		CloseHandle( h_dup_info );
		*lpErrno = WSAENETDOWN;
		return SOCKET_ERROR;
	}

	if( !DuplicateHandle( GetCurrentProcess(), h_dup_info,
		h_target_process, &h_target_dup_info, 0, TRUE,
		DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS ) )
	{
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_ERROR_EXIT( ("DuplicateHandle failed with %d\n", GetLastError()) );
		CloseHandle( h_target_process );
		*lpErrno = WSAENETDOWN;
		return SOCKET_ERROR;
	}

	CloseHandle( h_target_process );
#if defined(_WIN64)
	CL_ASSERT( !((ULONG_PTR)h_target_dup_info >> 32) );
#endif
	lpProtocolInfo->dwProviderReserved = (DWORD)(ULONG_PTR)h_target_dup_info;

	socket_info->duplicate.identifier = guid;

	IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_DUPLICATING_OLD );

	memset( &reason, 0, sizeof(reason) );
	reason.type = DISC_DUPLICATING;
	reason.duplicating.identifier = guid;
	reason.duplicating.dwProcessId = dwProcessId;

	/*
	 * Flush all the receive buffers. There should be no
	 * send/rdma buffers left.
	 */
	ib_disconnect( socket_info, &reason );

	/* We changed the state - remove from connection map. */
	ibsp_conn_remove( socket_info );

	cl_spinlock_release( &socket_info->mutex1 );

	wait_cq_drain( socket_info );

	cl_spinlock_acquire( &socket_info->mutex1 );
	ib_destroy_socket( socket_info );
	cl_spinlock_release( &socket_info->mutex1 );

	/* And that's it */
	IBSP_EXIT( IBSP_DBG_DUP );
	*lpErrno = 0;
	return 0;
}
