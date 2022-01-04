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
#include "ibspdll.tmh"
#endif

 
#include <tchar.h>
#include <stdlib.h>
#include "ibspdll.h"

#ifdef PERFMON_ENABLED
#include "ibsp_perfmon.h"
#endif /* PERFMON_ENABLED */

/* Globals */
struct ibspdll_globals g_ibsp;

/* Defines */
static const WCHAR *Description = L"Winsock Service Provider for Infiniband Transport";

/* Unique provider GUID generated with "uuidgen -s". Same as in installsp.c. */
static const GUID provider_guid = {
	/* c943654d-2c84-4db7-af3e-fdf1c5322458 */
	0xc943654d, 0x2c84, 0x4db7,
	{0xaf, 0x3e, 0xfd, 0xf1, 0xc5, 0x32, 0x24, 0x58}
};

static DWORD	no_read = 0;
uint32_t		g_max_inline = 0xFFFFFFFF;
uint32_t		g_max_poll = 500;
uint32_t		g_sa_timeout = 500;
uint32_t		g_sa_retries = 4;
int				g_connect_err = WSAEADDRNOTAVAIL;
uint8_t			g_max_cm_retries = CM_RETRIES;
int8_t			g_pkt_life_modifier = 0;
uint8_t			g_qp_retries = QP_ATTRIB_RETRY_COUNT;
uint8_t			g_use_APM = 0;
DWORD_PTR		g_dwPollThreadAffinityMask = 0;

uint32_t				g_ibsp_dbg_level = TRACE_LEVEL_ERROR;
uint32_t				g_ibsp_dbg_flags = 0x1;

BOOL InitApmLib();
VOID ShutDownApmLib();

/*
 * Function: DllMain
 * 
 *  Description:
 *    Provides initialization when the ibspdll DLL is loaded. 
 */
#pragma auto_inline( off )
static BOOL
_DllMain(
	IN				HINSTANCE					hinstDll,
	IN				DWORD						dwReason,
	IN				LPVOID						lpvReserved )
{
	TCHAR	env_var[16];
	DWORD	i;

	IBSP_ENTER( IBSP_DBG_DLL );

	UNUSED_PARAM( hinstDll );
	UNUSED_PARAM( lpvReserved );

	fzprint(("%s():%d:0x%x:0x%x: hinstDll=%d dwReason=%d lpvReserved=0x%p\n",
			 __FUNCTION__,
			 __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), hinstDll, dwReason, lpvReserved));

//#ifdef _DEBUG_
#if 0
	{
		char buf[64];
		if( GetEnvironmentVariable( "IBSPLOAD", buf, sizeof(buf) ) == 0 )
		{
			IBSP_ERROR_EXIT( ("IBSPLOAD not defined:\n") );

			return FALSE;
		}
	}
#endif

	switch( dwReason )
	{
	case DLL_PROCESS_ATTACH:


#if defined(EVENT_TRACING)
#if DBG
		WPP_INIT_TRACING(L"ibspdll.dll");
#else
		WPP_INIT_TRACING(L"ibspdll.dll");
#endif
#endif		



		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_DLL, ("DllMain: DLL_PROCESS_ATTACH\n") );


#if !defined(EVENT_TRACING)
#if DBG 

		i = GetEnvironmentVariable( "IBWSD_DBG_LEVEL", env_var, sizeof(env_var) );
		if( i && i <= 16 )
		{
			g_ibsp_dbg_level = _tcstoul( env_var, NULL, 16 );
		}

		i = GetEnvironmentVariable( "IBWSD_DBG_FLAGS", env_var, sizeof(env_var) );
		if( i && i <= 16 )
		{
			g_ibsp_dbg_flags = _tcstoul( env_var, NULL, 16 );
		}

		if( g_ibsp_dbg_flags & IBSP_DBG_ERR )
			g_ibsp_dbg_flags |= CL_DBG_ERROR;

		IBSP_PRINT(TRACE_LEVEL_INFORMATION ,IBSP_DBG_DLL ,
			("Given IBAL_UAL_DBG debug level:%d  debug flags 0x%x\n",
			g_ibsp_dbg_level ,g_ibsp_dbg_flags) );

#endif
#endif


		/* See if the user wants to disable RDMA reads. */
		no_read = GetEnvironmentVariable( "IBWSD_NO_READ", NULL, 0 );

		i = GetEnvironmentVariable( "IBWSD_INLINE", env_var, sizeof(env_var) );
		if( i && i <= 16 )
			g_max_inline = _tcstoul( env_var, NULL, 10 );

		i = GetEnvironmentVariable( "IBWSD_POLL", env_var, sizeof(env_var) );
		if( i && i <= 16 )
			g_max_poll = _tcstoul( env_var, NULL, 10 );

		i = GetEnvironmentVariable( "IBWSD_POLL_THREAD_AFFINITY_MASK", env_var, sizeof(env_var) );
		if( i && i <= 16 )
			g_dwPollThreadAffinityMask = _tcstoul( env_var, NULL, 10 );
		else 
		{
			DWORD_PTR xx;
			BOOL ret = GetProcessAffinityMask(GetCurrentProcess(), &g_dwPollThreadAffinityMask, &xx);
			CL_ASSERT(ret != 0);
			if (ret == 0) {
				IBSP_ERROR( ("GetProcessAffinityMask Failed (not a fatal error)\n") );
			}
			ret = ret; 
		}    

		i = GetEnvironmentVariable( "IBWSD_SA_RETRY", env_var, sizeof(env_var) );
		if( i && i <= 16 )
			g_sa_retries = _tcstoul( env_var, NULL, 10 );

		i = GetEnvironmentVariable( "IBWSD_SA_TIMEOUT", env_var, sizeof(env_var) );
		if( i && i <= 16 )
			g_sa_timeout = _tcstoul( env_var, NULL, 10 );

		i = GetEnvironmentVariable( "IBWSD_NO_IPOIB", env_var, sizeof(env_var) );
		if( i )
			g_connect_err = WSAEHOSTUNREACH;

		i = GetEnvironmentVariable( "IBWSD_CM_RETRY", env_var, sizeof(env_var) );
		if( i && i <= 16 )
		{
			g_max_cm_retries = (uint8_t)_tcstoul( env_var, NULL, 0 );
			if( g_max_cm_retries < 4 )
				g_max_cm_retries = 4;
			else if( g_max_cm_retries > 0xF )
				g_max_cm_retries = 0xFF;
		}

		i = GetEnvironmentVariable( "IBWSD_PKT_LIFE", env_var, sizeof(env_var) );
		if( i && i <= 16 )
		{
			g_pkt_life_modifier = (int8_t)_tcstoul( env_var, NULL, 0 );
			if( g_pkt_life_modifier > 0x1F )
				g_pkt_life_modifier = 0x1F;
		}

		i = GetEnvironmentVariable( "IBWSD_QP_RETRY", env_var, sizeof(env_var) );
		if( i && i <= 16 )
		{
			g_qp_retries = (uint8_t)_tcstoul( env_var, NULL, 0 );
			if( g_qp_retries > 7 )
				g_qp_retries = 7;
		}

		i = GetEnvironmentVariable( "IBWSD_USE_APM", env_var, sizeof(env_var) );
		if( i && i <= 16 )
		{
			g_use_APM = (uint8_t)_tcstoul( env_var, NULL, 0 );
		}

		if( init_globals() )
			return FALSE;

		if (g_use_APM)
		{
			InitApmLib();
			// We continue weather it succeeded or not
		}
#ifdef PERFMON_ENABLED
		IBSPPmInit();
#endif
		break;

	case DLL_THREAD_ATTACH:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_DLL, ("DllMain: DLL_THREAD_ATTACH\n") );
		break;

	case DLL_THREAD_DETACH:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_DLL, ("DllMain: DLL_THREAD_DETACH\n") );
		break;

	case DLL_PROCESS_DETACH:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_DLL, ("DllMain: DLL_PROCESS_DETACH\n") );

#ifdef _DEBUG_
		{
			cl_list_item_t *socket_item = NULL;

			cl_spinlock_acquire( &g_ibsp.socket_info_mutex );

			for( socket_item = cl_qlist_head( &g_ibsp.socket_info_list );
				socket_item != cl_qlist_end( &g_ibsp.socket_info_list );
				socket_item = cl_qlist_next( socket_item ) )
			{
				struct ibsp_socket_info *socket_info = NULL;
				socket_info = PARENT_STRUCT(socket_item, struct ibsp_socket_info, item);

#ifdef IBSP_LOGGING
				DataLogger_Shutdown(&socket_info->SendDataLogger);
				DataLogger_Shutdown(&socket_info->RecvDataLogger);
#endif
			}

			cl_spinlock_release( &g_ibsp.socket_info_mutex );

			IBSP_ERROR( ("Statistics:\n") );
			IBSP_ERROR( (
					 "  overlap_h0_count = %d\n", g_ibsp.overlap_h0_count) );
			IBSP_ERROR( (
					 "  max_comp_count = %d\n", g_ibsp.max_comp_count) );
			IBSP_ERROR( (
					 "  overlap_h1_count = %d\n", g_ibsp.overlap_h1_count) );

			IBSP_ERROR( ("  send_count = %d\n", g_ibsp.send_count) );

			IBSP_ERROR( ("  total_send_count = %d\n", g_ibsp.total_send_count) );

			IBSP_ERROR( ("  total_recv_count = %d\n", g_ibsp.total_recv_count) );

			IBSP_ERROR( ("  total_recv_compleated = %d\n", g_ibsp.total_recv_compleated) );

			IBSP_ERROR( (
					"  number of QPs left = %d\n", g_ibsp.qp_num) );
			IBSP_ERROR( (
					"  number of CQs left = %d\n", g_ibsp.cq_num) );
			IBSP_ERROR( (
					"  number of PDs left = %d\n", g_ibsp.pd_num) );
			IBSP_ERROR( (
					"  number of ALs left = %d\n", g_ibsp.al_num) );
			IBSP_ERROR( (
					 "  number of MRs left = %d\n", g_ibsp.mr_num) );
			IBSP_ERROR( (
					"  number of listens left = %d\n", g_ibsp.listen_num) );
			IBSP_ERROR( (
					"  number of PNPs left = %d\n", g_ibsp.pnp_num) );
			IBSP_ERROR( (
					"  number of threads left = %d\n", g_ibsp.thread_num) );
			IBSP_ERROR( (
					"  number of WPU sockets left = %d\n", g_ibsp.wpusocket_num) );

			IBSP_ERROR( (
					"  CloseSocket_count = %d\n", g_ibsp.CloseSocket_count) );

		}
#endif
		release_globals();
#ifdef PERFMON_ENABLED
		IBSPPmClose();
#endif


#if defined(EVENT_TRACING)
		WPP_CLEANUP();
#endif
		break;
	}

	IBSP_EXIT( IBSP_DBG_DLL );

	return TRUE;
}
#pragma auto_inline( off )


extern BOOL APIENTRY
_DllMainCRTStartupForGS(
	IN				HINSTANCE					h_module,
	IN				DWORD						ul_reason_for_call, 
	IN				LPVOID						lp_reserved );


BOOL APIENTRY
DllMain(
	IN				HINSTANCE					h_module,
	IN				DWORD						ul_reason_for_call, 
	IN				LPVOID						lp_reserved )
{
	switch( ul_reason_for_call )
	{
	case DLL_PROCESS_ATTACH:
		if( !_DllMainCRTStartupForGS(
			h_module, ul_reason_for_call, lp_reserved ) )
		{
			return FALSE;
		}

		return _DllMain( h_module, ul_reason_for_call, lp_reserved );

	case DLL_PROCESS_DETACH:
		_DllMain( h_module, ul_reason_for_call, lp_reserved );

		return _DllMainCRTStartupForGS(
			h_module, ul_reason_for_call, lp_reserved );
	}
	return TRUE;
}


static SOCKET
accept_socket(
	IN				struct ibsp_socket_info		*p_socket,
	IN				struct listen_incoming		*p_incoming,
	IN				struct ibsp_port			*p_port,
		OUT			LPINT						lpErrno )
{
	struct ibsp_socket_info *new_socket_info;
	int ret;

	IBSP_ENTER( IBSP_DBG_CONN );

	/* Create a new socket here  */
	new_socket_info = create_socket_info( lpErrno );
	if( !new_socket_info )
	{
		ib_reject(
			p_incoming->cm_req_received.h_cm_req, IB_REJ_INSUF_RESOURCES );

		IBSP_ERROR_EXIT( ("create_socket_info failed (%d)\n", *lpErrno) );
		return INVALID_SOCKET;
	}

	/* Time to allocate our IB QP */
	new_socket_info->port = p_port;
	*lpErrno = ib_create_socket( new_socket_info );
	if( *lpErrno )
	{
		deref_socket_info( new_socket_info );

		ib_reject(
			p_incoming->cm_req_received.h_cm_req, IB_REJ_INSUF_QP );

		IBSP_ERROR_EXIT( ("ib_create_socket failed (%d)\n", *lpErrno) );
		return INVALID_SOCKET;
	}

	/* Store the IP address and port number in the socket context */
	new_socket_info->local_addr = p_incoming->params.dest;

	/* Copy the socket context info from parent socket context */
	new_socket_info->socket_options = p_socket->socket_options;

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN,
		("The socket address of connecting entity is\n") );
	DebugPrintSockAddr( IBSP_DBG_CONN, &p_incoming->params.source );

	new_socket_info->peer_addr = p_incoming->params.source;

#ifdef IBSP_LOGGING
	DataLogger_Init( &new_socket_info->SendDataLogger, "Send",
		&new_socket_info->peer_addr, &new_socket_info->local_addr );
	DataLogger_Init( &new_socket_info->RecvDataLogger, "Recv",
		&new_socket_info->local_addr, &new_socket_info->peer_addr );
#endif

	cl_spinlock_acquire( &new_socket_info->mutex1 );
	/* Update the state of the socket context */
	IBSP_CHANGE_SOCKET_STATE( new_socket_info, IBSP_CONNECTED );

	*lpErrno = ib_accept( new_socket_info, &p_incoming->cm_req_received );
	if( *lpErrno )
	{
		IBSP_CHANGE_SOCKET_STATE( new_socket_info, IBSP_CREATE );
		cl_spinlock_release( &new_socket_info->mutex1 );

		if( *lpErrno == WSAEADDRINUSE )
		{
			/* Be nice and reject that connection. */
			ib_reject( p_incoming->cm_req_received.h_cm_req, IB_REJ_INSUF_QP );
		}

		g_ibsp.up_call_table.lpWPUCloseSocketHandle(
			new_socket_info->switch_socket, &ret );
		new_socket_info->switch_socket = INVALID_SOCKET;
		STAT_DEC( wpusocket_num );

		ib_destroy_socket( new_socket_info );
		deref_socket_info( new_socket_info );
		return INVALID_SOCKET;
	}

	cl_spinlock_acquire( &g_ibsp.socket_info_mutex );
	cl_qlist_insert_tail(
		&g_ibsp.socket_info_list, &new_socket_info->item );
	cl_spinlock_release( &g_ibsp.socket_info_mutex );

	cl_spinlock_release( &new_socket_info->mutex1 );

	IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN,
		("returns new socket (0x%p)\n", new_socket_info) );
	return (SOCKET)new_socket_info;
}


/* Function: IBSPAccept
 *
 *  Description:
 *    Handle the WSAAccept function. The only special consideration here
 *    is the conditional accept callback. You can choose to intercept
 *    this by substituting your own callback (you'll need to keep track
 *    of the user supplied callback so you can trigger that once your
 *    substituted function is triggered).
 */
static SOCKET WSPAPI
IBSPAccept(
	IN				SOCKET						s,
		OUT			struct sockaddr FAR			*addr,
	IN	OUT			LPINT						addrlen,
	IN				LPCONDITIONPROC				lpfnCondition,
	IN				DWORD_PTR					dwCallbackData,
		OUT			LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	WSABUF caller_id;
	WSABUF callee_id;
	struct listen_incoming *incoming;
	struct ibsp_port *port;
	ib_cm_mra_t		mra;

	IBSP_ENTER( IBSP_DBG_CONN );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	CL_ASSERT( lpfnCondition );

	if( *addrlen < sizeof(struct sockaddr_in) )
	{
		IBSP_ERROR_EXIT( ("invalid addrlen (%d, %d)\n",
			*addrlen, sizeof(struct sockaddr_in)) );
		*lpErrno = WSAEFAULT;
		return INVALID_SOCKET;
	}

	/* Check if there is any pending connection for this socket. If
	 * there is one, create a socket, and then query the switch about
	 * the pending connection */

	cl_spinlock_acquire( &socket_info->mutex1 );

	/* Verify the state of the socket */
	if( socket_info->socket_state != IBSP_LISTEN )
	{
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_ERROR_EXIT( ("Socket is not in right socket_state (%s)\n",
			IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );
		*lpErrno = WSAEINVAL;
		return INVALID_SOCKET;
	}

	if( cl_qlist_count( &socket_info->listen.list ) == 0 )
	{
		cl_spinlock_release( &socket_info->mutex1 );

		IBSP_ERROR_EXIT( ("No pending connection found for this socket\n") );
		*lpErrno = WSAEWOULDBLOCK;
		return INVALID_SOCKET;
	}

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN,
		("IBSPAccept: Found pending connection on this socket\n") );

	incoming = PARENT_STRUCT(cl_qlist_remove_head( &socket_info->listen.list ),
							 struct listen_incoming, item);

	/* Signal the event again if there are more connection requests. */
	if( cl_qlist_count( &socket_info->listen.list ) )
		ibsp_post_select_event( socket_info, FD_ACCEPT, 0 );
	cl_spinlock_release( &socket_info->mutex1 );

	port = socket_info->port;

	/* Find the destination IP address */
	if( !port )
	{
		/* The socket was bound to INADDR_ANY. We must find the correct port
		 * for the new socket. */
		port = get_port_from_ip_address( incoming->params.dest.sin_addr );
		if( !port )
		{
			IBSP_ERROR( ("incoming destination IP address not local (%s)\n",
				inet_ntoa( incoming->params.dest.sin_addr )) );
			goto reject;
		}
	}

	/* Cross-check with the path info to make sure we are conectiong correctly */
	if( port->guid != ib_gid_get_guid( &incoming->cm_req_received.primary_path.sgid ) )
	{
		IBSP_ERROR( (
			"GUIDs of port for destination IP address and primary path do not match (%016I64x, %016I64x)\n",
			port->guid,
			ib_gid_get_guid( &incoming->cm_req_received.primary_path.sgid )) );

reject:
		ib_reject( incoming->cm_req_received.h_cm_req, IB_REJ_INSUF_QP );

		HeapFree( g_ibsp.heap, 0, incoming );
		IBSP_ERROR_EXIT( ("bad incoming parameter\n") );
		*lpErrno = WSAECONNREFUSED;
		return INVALID_SOCKET;
	}

	/*
	 * Check against the conditional routine if socket can be created
	 * or not 
	 */

	/* Set the caller and callee data buffer */
	caller_id.buf = (char *)&incoming->params.source;
	caller_id.len = sizeof(incoming->params.source);

	callee_id.buf = (char *)&incoming->params.dest;
	callee_id.len = sizeof(incoming->params.dest);

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN,
		("Got incoming conn from %s/%d-%d to %s/%d-%d\n",
		inet_ntoa( incoming->params.source.sin_addr ),
		cl_ntoh16( incoming->params.source.sin_port ),
		incoming->params.source.sin_family,
		inet_ntoa( incoming->params.dest.sin_addr ),
		cl_ntoh16( incoming->params.dest.sin_port ),
		incoming->params.dest.sin_family) );

	/* Call the conditional function */
	switch( lpfnCondition( &caller_id, NULL, NULL, NULL,
		&callee_id, NULL, NULL, dwCallbackData ) )
	{
	default:
		/* Should never happen */
		IBSP_ERROR( ("Conditional routine returned undocumented code\n") );
		/* Fall through. */

	case CF_REJECT:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN,
			("Conditional routine returned CF_REJECT\n") );

		ib_reject( incoming->cm_req_received.h_cm_req, IB_REJ_USER_DEFINED );

		HeapFree( g_ibsp.heap, 0, incoming );
		*lpErrno = WSAECONNREFUSED;
		IBSP_EXIT( IBSP_DBG_CONN );
		return INVALID_SOCKET;

	case CF_DEFER:
		/* Send MRA */
		mra.mra_length = 0;
		mra.p_mra_pdata = NULL;
		mra.svc_timeout = 0x15;
		ib_cm_mra( incoming->cm_req_received.h_cm_req, &mra );

		/* Put the item back at the head of the list. */
		cl_spinlock_acquire( &socket_info->mutex1 );
		cl_qlist_insert_head( &socket_info->listen.list, &incoming->item );
		cl_spinlock_release( &socket_info->mutex1 );

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN,
			("Conditional routine returned CF_DEFER\n") );

		*lpErrno = WSATRY_AGAIN;
		IBSP_EXIT( IBSP_DBG_CONN );
		return INVALID_SOCKET;

	case CF_ACCEPT:
		break;
	}

	s = accept_socket( socket_info, incoming, port, lpErrno );
	if( s != INVALID_SOCKET )
	{
		/* Store the client socket address information */
		memcpy( addr, &incoming->params.source, sizeof(struct sockaddr_in) );
		*addrlen = sizeof(struct sockaddr_in);
	}

	HeapFree( g_ibsp.heap, 0, incoming );

	IBSP_EXIT( IBSP_DBG_CONN );
	return s;
}


/* Function: IBSPBind
 *  
 *  Description:
 *    Bind the socket to a local address. 
 *   
*/
static int WSPAPI
IBSPBind(
	IN				SOCKET						s,
	IN		const	struct sockaddr FAR			*name,
	IN				int							namelen,
		OUT			LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	struct sockaddr_in *addr = (struct sockaddr_in *)name;
	struct ibsp_port *port;
	int ret;

	IBSP_ENTER( IBSP_DBG_CONN );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN, ("Address to bind to:\n") );
	DebugPrintSockAddr( IBSP_DBG_CONN, addr );

	fzprint(("binding to IP %s\n", inet_ntoa( addr->sin_addr )));

	/* Sanity checks */
	if( namelen != sizeof(struct sockaddr_in) )
	{
		IBSP_ERROR( ("invalid namelen (%d instead of %d)\n",
			namelen, sizeof(struct sockaddr_in)) );
		*lpErrno = WSAEFAULT;
		goto error;
	}

	if( addr->sin_family != AF_INET )
	{
		IBSP_ERROR( ("bad family for socket\n") );
		*lpErrno = WSAEFAULT;
		goto error;
	}

	/* Check if the ip address is assigned to one of our IBoIB HCA. */
	if( addr->sin_addr.S_un.S_addr != INADDR_ANY )
	{
		port = get_port_from_ip_address( addr->sin_addr );
		if( port == NULL )
		{
			IBSP_ERROR( (
				"This IP address does not belong to that host (%08x)\n",
				addr->sin_addr.S_un.S_addr) );
			*lpErrno = WSAEADDRNOTAVAIL;
			goto error;
		}
	}
	else
	{
		port = NULL;
	}

	/* We are going to take this mutex for some time, 
	 * but at this stage, it shouldn't impact anything. */
	cl_spinlock_acquire( &socket_info->mutex1 );

	/* Verify the state of the socket */
	if( socket_info->socket_state != IBSP_CREATE )
	{
		cl_spinlock_release( &socket_info->mutex1 );
		IBSP_ERROR( (
			"Invalid socket state (%s)\n",
			IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );
		*lpErrno = WSAEINVAL;
		goto error;
	}

	if( addr->sin_addr.S_un.S_addr != INADDR_ANY )
	{
		/* Time to allocate our IB QP */
		socket_info->port = port;
		ret = ib_create_socket( socket_info );
		if( ret )
		{
			socket_info->port = NULL;
			cl_spinlock_release( &socket_info->mutex1 );
			IBSP_ERROR( ("ib_create socket failed with %d\n", ret) );
			*lpErrno = WSAENOBUFS;
			goto error;
		}
	}

	/* Success */
	socket_info->local_addr = *addr;

	IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_BIND );

	cl_spinlock_release( &socket_info->mutex1 );

	IBSP_EXIT( IBSP_DBG_CONN );
	return 0;

error:
	CL_ASSERT( *lpErrno != 0 );
	IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN, ("failed with error %d\n", *lpErrno) );
	return SOCKET_ERROR;
}


/* Function: IBSPCloseSocket
 * 
 * Description:
 *   Close the socket handle of the app socket as well as the provider socket.
 *   However, if there are outstanding async IO requests on the app socket
 *   we only close the provider socket. Only when all the IO requests complete
 *   (with error) will we then close the app socket.
 */
static int WSPAPI
IBSPCloseSocket(
					SOCKET						s,
					LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;

	IBSP_ENTER( IBSP_DBG_CONN );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	if( s == INVALID_SOCKET )
	{
		IBSP_ERROR_EXIT( ("invalid socket handle %Ix\n", s) );
		*lpErrno = WSAENOTSOCK;
		return SOCKET_ERROR;
	}
#ifdef _DEBUG_
	cl_atomic_inc( &g_ibsp.CloseSocket_count );
#endif

	shutdown_and_destroy_socket_info( socket_info );

	IBSP_EXIT( IBSP_DBG_CONN );

	*lpErrno = 0;
	return 0;
}


/* Function: IBSPConnect
 *
 *  Description:
 *    Performs a connect call. The only thing we need to do is translate
 *    the socket handle.
 */
static int WSPAPI
IBSPConnect(
					SOCKET						s,
					const struct sockaddr FAR	*name,
					int							namelen,
					LPWSABUF					lpCallerData,
					LPWSABUF					lpCalleeData,
					LPQOS						lpSQOS,
					LPQOS						lpGQOS,
					LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	struct sockaddr_in *addr = (struct sockaddr_in *)name;
	int ret;
	ib_net64_t dest_port_guid;
	ib_path_rec_t path_rec;
	ib_path_rec_t alt_path_rec, *palt_path_rec = NULL;


	IBSP_ENTER( IBSP_DBG_CONN );

	UNUSED_PARAM( lpCalleeData );
	UNUSED_PARAM( lpSQOS );
	UNUSED_PARAM( lpGQOS );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p state=%s\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), s, IBSP_SOCKET_STATE_STR( socket_info->socket_state )));

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN,
		("lpCallerData=%p, lpCalleeData=%p\n", lpCallerData, lpCalleeData) );


	socket_info->active_side = TRUE;
	/* Sanity checks */
	if( lpCallerData )
	{
		/* We don't support that. The current switch does not use it. */
		IBSP_ERROR_EXIT( ("lpCallerData.len=%d\n", lpCallerData->len) );
		*lpErrno = WSAEINVAL;
		return SOCKET_ERROR;
	}

	if( namelen < sizeof(struct sockaddr_in) )
	{
		IBSP_ERROR_EXIT( (
			"invalid remote address (%d)\n", socket_info->socket_state) );
		*lpErrno = WSAEFAULT;
		return SOCKET_ERROR;
	}

	/* Check if the name (actually address) of peer entity is correct */
	if( addr->sin_family != AF_INET ||
		addr->sin_port == 0 || addr->sin_addr.s_addr == INADDR_ANY )
	{
		IBSP_ERROR_EXIT( ("peer entity address is invalid (%d, %d, %x)\n",
			addr->sin_family, addr->sin_port, addr->sin_addr.s_addr) );
		*lpErrno = WSAEADDRNOTAVAIL;
		return SOCKET_ERROR;
	}

	if( socket_info->local_addr.sin_addr.S_un.S_addr == addr->sin_addr.S_un.S_addr )
	{
		/* Loopback - let the regular stack take care of that. */
		IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN, ("Loopback!\n") );
		*lpErrno = WSAEADDRNOTAVAIL;
		return SOCKET_ERROR;
	}

	/* Get the GUID for that IP address. */
	ret = query_guid_address(
        (struct sockaddr*)&socket_info->local_addr, name, &dest_port_guid );
	if( ret )
	{
		IBSP_ERROR_EXIT( ("query_guid_address failed for IP %08x\n",
			addr->sin_addr.s_addr) );
		*lpErrno = g_connect_err;
		return SOCKET_ERROR;
	}
	socket_info->dest_port_guid = dest_port_guid;

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN, ("got GUID %I64x for IP %s\n",
		CL_NTOH64( dest_port_guid ), inet_ntoa( addr->sin_addr )) );

	if( dest_port_guid == socket_info->port->guid )
	{
		IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN, ("Loopback!\n") );
		*lpErrno = WSAEADDRNOTAVAIL;
		return SOCKET_ERROR;
	}

	/* Get the path record */
	ret = query_pr( socket_info->port->guid, dest_port_guid,socket_info->port->hca->dev_id, &path_rec );
	if( ret )
	{
		IBSP_ERROR_EXIT( (
			"query_pr failed for IP %08x\n", addr->sin_addr.s_addr) );
		*lpErrno = g_connect_err;
		return SOCKET_ERROR;
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
	

	cl_spinlock_acquire( &socket_info->mutex1 );

	/* Verify the state of the socket */
	switch( socket_info->socket_state )
	{
	case IBSP_BIND:
		/* Good. That's the only valid state we want. */
		break;

	case IBSP_CONNECTED:
		IBSP_ERROR( ("Socket is already connected\n") );
		*lpErrno = WSAEISCONN;
		goto done;

	case IBSP_LISTEN:
		IBSP_ERROR( ("Socket is a listening socket\n") );
		*lpErrno = WSAEINVAL;
		goto done;

	default:
		IBSP_ERROR( ("Socket is not in the bound state (%s)\n",
			IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );
		*lpErrno = WSAEINVAL;
		goto done;
	}

	/* Store the peer entity's address in socket context */
	socket_info->peer_addr = *addr;

#ifdef IBSP_LOGGING
	DataLogger_Init( &socket_info->SendDataLogger, "Send",
		&socket_info->peer_addr, &socket_info->local_addr );
	DataLogger_Init( &socket_info->RecvDataLogger, "Recv",
		&socket_info->local_addr, &socket_info->peer_addr );
#endif

	/* Update the socket state */
	IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_CONNECT );

	/* Connect */
	*lpErrno = ib_connect( socket_info, &path_rec, palt_path_rec );
	if( *lpErrno != WSAEWOULDBLOCK )
	{
		/* We must be sure none destroyed our socket */
		IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_BIND );
		memset( &socket_info->peer_addr, 0, sizeof(struct sockaddr_in) );

		IBSP_ERROR( ("ib_connect failed (%d)\n", *lpErrno) );
	}

done:
	cl_spinlock_release( &socket_info->mutex1 );
	IBSP_EXIT( IBSP_DBG_CONN );
	return SOCKET_ERROR;
}


/* Function: IBSPEnumNetworkEvents
 * 
 *  Description:
 *    Enumerate the network events for a socket. We only need to
 *    translate the socket handle.
*/
static int WSPAPI
IBSPEnumNetworkEvents(
					SOCKET						s,
					WSAEVENT					hEventObject,
					LPWSANETWORKEVENTS			lpNetworkEvents,
					LPINT						lpErrno )
{
	struct ibsp_socket_info		*socket_info = (struct ibsp_socket_info *)s;

	IBSP_ENTER( IBSP_DBG_NEV );

	if( hEventObject != NULL )
	{
		ResetEvent( hEventObject );
	}

	lpNetworkEvents->lNetworkEvents =
		InterlockedExchange( &socket_info->network_events, 0 );

	if( lpNetworkEvents->lNetworkEvents & FD_ACCEPT )
	{
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_NEV,
			("socket %p notify FD_ACCEPT at time %I64d\n",
			socket_info, cl_get_time_stamp()) );
		lpNetworkEvents->iErrorCode[FD_ACCEPT_BIT] = 0;
	}

	if( lpNetworkEvents->lNetworkEvents & FD_CONNECT )
	{
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_NEV,
			("socket %p notify FD_CONNECT %d at time %I64d\n",
			socket_info, socket_info->errno_connect, cl_get_time_stamp()) );
		lpNetworkEvents->iErrorCode[FD_CONNECT_BIT] = socket_info->errno_connect;
	}

	*lpErrno = 0;
	IBSP_EXIT( IBSP_DBG_NEV );
	return 0;
}


/* Function: IBSPEventSelect
 *
 *  Description:
 *    Register the specified events on the socket with the given event handle.
 *    All we need to do is translate the socket handle.
 */
static int WSPAPI
IBSPEventSelect(
					SOCKET						s,
					WSAEVENT					hEventObject,
					long						lNetworkEvents,
					LPINT						lpErrno )
{
	struct ibsp_socket_info	*socket_info = (struct ibsp_socket_info *)s;
	long					events;

	IBSP_ENTER( IBSP_DBG_NEV );

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_NEV,
		("Socket %Ix requesting notifiction of %d on event %p.\n",
		s, lNetworkEvents, hEventObject));

	if( (lNetworkEvents & ~(FD_ACCEPT | FD_CONNECT)) != 0 )
	{
		IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION,IBSP_DBG_NEV,
			("Unknown lNetworkEvents flag given (%x)\n", lNetworkEvents) );
		*lpErrno = WSAEINVAL;
		return SOCKET_ERROR;
	}

	CL_ASSERT( lpErrno );

	socket_info->event_mask = lNetworkEvents;
	InterlockedExchangePointer( &socket_info->event_select, hEventObject );

	events = InterlockedCompareExchange( &socket_info->network_events, 0, 0 );
	/* Check for existing events and signal as appropriate. */
	if( (socket_info->event_mask & events) && hEventObject )
	{
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_NEV,
			("Signaling eventHandle %p .\n", socket_info->event_select) );
		SetEvent( hEventObject );
	}

	IBSP_EXIT( IBSP_DBG_NEV );
	return 0;
}


/* Function: IBSPGetOverlappedResult
 *
 *  Description:
 *    This function reports whether the specified overlapped call has
 *    completed. If it has, return the requested information. If not,
 *    and fWait is true, wait until completion. Otherwise return an
 *    error immediately.
 */
static BOOL WSPAPI
IBSPGetOverlappedResult(
	IN				SOCKET						s,
	IN				LPWSAOVERLAPPED				lpOverlapped,
		OUT			LPDWORD						lpcbTransfer,
	IN				BOOL						fWait,
		OUT			LPDWORD						lpdwFlags,
		OUT			LPINT						lpErrno )
{
	struct ibsp_socket_info	*p_socket_info;
	BOOL					rc;

	IBSP_ENTER( IBSP_DBG_IO );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p overlapped=0x%p Internal=%d InternalHigh=%d OffsetHigh=%d hEvent=%d\n", __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s, lpOverlapped, lpOverlapped->Internal, lpOverlapped->InternalHigh, lpOverlapped->OffsetHigh, lpOverlapped->hEvent));

	CL_ASSERT( fWait == FALSE );
	if( fWait == TRUE )
	{
		IBSP_ERROR_EXIT( ("fWait not supported\n") );
		*lpErrno = WSAENETDOWN;
		return FALSE;
	}

	if( s == INVALID_SOCKET )
	{
		/* Seen in real life with overlap/client test.
		 * The switch closes a socket then calls this. Why? */
		IBSP_ERROR_EXIT( ("invalid socket handle %Ix\n", s) );
		*lpErrno = WSAENOTSOCK;
		return SOCKET_ERROR;
	}

	if( lpOverlapped->Internal == WSS_OPERATION_IN_PROGRESS )
	{
		p_socket_info = (struct ibsp_socket_info*)s;
		/* Poll just in case it's done. */
		ib_cq_comp( p_socket_info->cq_tinfo );
	}

	if( lpOverlapped->Internal != WSS_OPERATION_IN_PROGRESS )
	{
		/* Operation has completed, perhaps with error */
		*lpdwFlags = lpOverlapped->Offset;
		*lpErrno = lpOverlapped->OffsetHigh;

#ifdef _DEBUG_
		if( ((uintptr_t) lpOverlapped->hEvent) & 0x00000001 )
		{
			cl_atomic_dec( &g_ibsp.overlap_h1_count );

			fzprint(("%s():%d:0x%x:0x%x: ov=0x%p h0=%d h1=%d h1_c=%d send=%d recv=%d\n",
					 __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(),
					 lpOverlapped, g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count,
					 g_ibsp.overlap_h1_comp_count, g_ibsp.send_count, g_ibsp.recv_count));
		}

		fzprint(("%s():%d:0x%x:0x%x: socket=0x%p completed overlap=0x%x overlap_h0_count=%d overlap_h1_count=%d\n", __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s, lpOverlapped, g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count));
#endif
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
			("socket=%p completed ov=%p\n", (void*)s, lpOverlapped));
	}
	else
	{
		/* Operation is still in progress */
		*lpErrno = WSA_IO_INCOMPLETE;
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
			("socket=%p ov=%p hEvent=%p, operation in progress\n",
			(void*)s, lpOverlapped, lpOverlapped->hEvent));
	}

	*lpcbTransfer = (DWORD)lpOverlapped->InternalHigh;

	if( *lpErrno == 0 )
		rc = TRUE;
	else
		rc = FALSE;

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p overlapped=0x%p lpErrno=%d rc=%d\n",
			 __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s,
			 lpOverlapped, *lpErrno, rc));

	if (rc == TRUE) CL_ASSERT(*lpcbTransfer > 0);

	IBSP_EXIT( IBSP_DBG_IO );
	return rc;
}


/* Function: IBSPGetSockOpt
 *
 *  Description:
 *    Get the specified socket option. All we need to do is translate the
 *    socket handle.
 */
static int WSPAPI
IBSPGetSockOpt(
					SOCKET						s,
					int							level,
					int							optname,
					char FAR					*optval,
					LPINT						optlen,
					LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;

	IBSP_ENTER( IBSP_DBG_OPT );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	if( level != SOL_SOCKET )
	{
		IBSP_ERROR_EXIT( ("invalid level %d", level) );
		*lpErrno = WSAENOPROTOOPT;
		return SOCKET_ERROR;
	}

	if( optval == NULL || optlen == NULL )
	{
		IBSP_ERROR_EXIT( ("invalid optval=%p or optlen=%p", optval, optlen) );
		*lpErrno = WSAEFAULT;
		return SOCKET_ERROR;
	}

	switch( optname )
	{
	case SO_DEBUG:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Option name SO_DEBUG\n") );
		if( *optlen < sizeof(BOOL) )
		{
			IBSP_ERROR_EXIT( ("option len is invalid (0x%x)\n", *optlen) );
			*optlen = sizeof(BOOL);
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}

		memcpy( optval, &socket_info->socket_options.debug, sizeof(BOOL) );
		*optlen = sizeof(BOOL);
		break;

	case SO_GROUP_ID:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Option name SO_GROUP_ID\n") );
		if( *optlen < sizeof(GROUP) )
		{
			IBSP_ERROR_EXIT( ("option len is invalid (0x%x)\n", *optlen) );
			*optlen = sizeof(GROUP);
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}

		memcpy( optval, &socket_info->socket_options.group_id, sizeof(GROUP) );
		*optlen = sizeof(GROUP);
		break;

	case SO_GROUP_PRIORITY:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Option name SO_GROUP_PRIORITY\n") );

		if( *optlen < sizeof(int) )
		{
			IBSP_ERROR_EXIT( ("option len is invalid (0x%x)\n", *optlen) );
			*optlen = sizeof(int);
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}

		memcpy( optval, &socket_info->socket_options.group_priority, sizeof(int) );
		*optlen = sizeof(int);
		break;

	case SO_MAX_MSG_SIZE:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Option name SO_MAX_MSG_SIZE\n") );

		if( *optlen < sizeof(unsigned int) )
		{
			IBSP_ERROR_EXIT( ("option len is invalid (0x%x)\n", *optlen) );
			*optlen = sizeof(unsigned int);
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}

		memcpy( optval, &socket_info->socket_options.max_msg_size, sizeof(unsigned int) );
		*optlen = sizeof(unsigned int);
		break;

	case SO_MAX_RDMA_SIZE:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Option name SO_MAX_RDMA_SIZE\n") );

		if( *optlen < sizeof(unsigned int) )
		{
			IBSP_ERROR_EXIT( ("option len is invalid (0x%x)\n", *optlen) );
			*optlen = sizeof(unsigned int);
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}

		memcpy( optval, &socket_info->socket_options.max_rdma_size, sizeof(unsigned int) );
		*optlen = sizeof(unsigned int);
		break;

	case SO_RDMA_THRESHOLD_SIZE:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Option name SO_RDMA_THRESHOLD_SIZE\n") );

		if( *optlen < sizeof(unsigned int) )
		{
			IBSP_ERROR_EXIT( ("option len is invalid (0x%x)\n", *optlen) );
			*optlen = sizeof(unsigned int);
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}

		memcpy( optval, &socket_info->socket_options.rdma_threshold_size,
			sizeof(unsigned int) );
		*optlen = sizeof(unsigned int);
		break;

	default:
		*lpErrno = WSAENOPROTOOPT;

		IBSP_ERROR_EXIT( ("unknown option 0x%x\n", optname) );

		return SOCKET_ERROR;
		break;
	}

	IBSP_EXIT( IBSP_DBG_OPT );
	return 0;
}


/* Function: IBSPGetQOSByName
 *
 *  Description:
 *    Get a QOS template by name. All we need to do is translate the socket
 *    handle.
 */
static BOOL WSPAPI
IBSPGetQOSByName(
					SOCKET						s,
					LPWSABUF					lpQOSName,
					LPQOS						lpQOS,
					LPINT						lpErrno )
{
	IBSP_ENTER( IBSP_DBG_OPT );

	UNUSED_PARAM( s );
	UNUSED_PARAM( lpQOSName );
	UNUSED_PARAM( lpQOS );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	*lpErrno = WSAEOPNOTSUPP;

	IBSP_ERROR_EXIT( ("not supported\n") );

	return FALSE;
}


/* Function: IBSPIoctl
 *
 * Description:
 *   Invoke an ioctl. In most cases, we just need to translate the socket
 *   handle. However, if the dwIoControlCode is SIO_GET_EXTENSION_FUNCTION_POINTER,
 *   we'll need to intercept this and return our own function pointers.
 */
static int WSPAPI
IBSPIoctl(
	IN				SOCKET						s,
	IN				DWORD						dwIoControlCode,
	IN				LPVOID						lpvInBuffer,
	IN				DWORD						cbInBuffer,
		OUT			LPVOID						lpvOutBuffer,
	IN				DWORD						cbOutBuffer,
		OUT			LPDWORD						lpcbBytesReturned,
	IN				LPWSAOVERLAPPED				lpOverlapped,
	IN				LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
	IN				LPWSATHREADID				lpThreadId,
		OUT			LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info;
	GUID SANRegisterMemory = WSAID_REGISTERMEMORY;
	GUID SANDeregisterMemory = WSAID_DEREGISTERMEMORY;
	GUID SANRegisterRDMAMemory = WSAID_REGISTERRDMAMEMORY;
	GUID SANDeregisterRDMAMemory = WSAID_DEREGISTERRDMAMEMORY;
	GUID SANRDMAWrite = WSAID_RDMAWRITE;
	GUID SANRDMARead = WSAID_RDMAREAD;
	GUID SANMemoryRegistrationCacheCallback = WSAID_MEMORYREGISTRATIONCACHECALLBACK;

	IBSP_ENTER( IBSP_DBG_OPT );

	UNUSED_PARAM( cbInBuffer );
	UNUSED_PARAM( lpOverlapped );
	UNUSED_PARAM( lpCompletionRoutine );
	UNUSED_PARAM( lpThreadId );

	if( dwIoControlCode == SIO_GET_EXTENSION_FUNCTION_POINTER )
	{
		/* This a special case. The socket handle passed is not valid. */
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Get extension function pointer\n") );

		if( memcmp( lpvInBuffer, &SANRegisterMemory, sizeof(GUID) ) == 0 )
		{
			/* Return a pointer to our intermediate extension function */
			*((LPFN_WSPREGISTERMEMORY *) lpvOutBuffer) = IBSPRegisterMemory;
		}
		else if( memcmp( lpvInBuffer, &SANDeregisterMemory, sizeof(GUID) ) == 0 )
		{
			/* Return a pointer to our intermediate extension function */
			*((LPFN_WSPDEREGISTERMEMORY *) lpvOutBuffer) = IBSPDeregisterMemory;
		}
		else if( memcmp( lpvInBuffer, &SANRegisterRDMAMemory, sizeof(GUID) ) == 0 )
		{
			/* Return a pointer to our intermediate extension function */
			*((LPFN_WSPREGISTERRDMAMEMORY *) lpvOutBuffer) = IBSPRegisterRdmaMemory;
		}
		else if( memcmp( lpvInBuffer, &SANDeregisterRDMAMemory, sizeof(GUID) ) == 0 )
		{
			/* Return a pointer to our intermediate extension function */
			*((LPFN_WSPDEREGISTERRDMAMEMORY *) lpvOutBuffer) = IBSPDeregisterRdmaMemory;
		}
		else if( memcmp( lpvInBuffer, &SANRDMAWrite, sizeof(GUID) ) == 0 )
		{
			/* Return a pointer to our intermediate extension function */
			*((LPFN_WSPRDMAWRITE *) lpvOutBuffer ) = IBSPRdmaWrite;
		}
		else if( memcmp( lpvInBuffer, &SANRDMARead, sizeof(GUID) ) == 0 )
		{
			if( no_read )
			{
				IBSP_PRINT(TRACE_LEVEL_WARNING, IBSP_DBG_OPT,
					("RDMA_READ disabled.\n") );
				*lpErrno = WSAEOPNOTSUPP;
				return SOCKET_ERROR;
			}
			else
			{
				/* Return a pointer to our intermediate extension function */
				*((LPFN_WSPRDMAREAD *) lpvOutBuffer ) = IBSPRdmaRead;
			}
		}
		else if( memcmp( lpvInBuffer, &SANMemoryRegistrationCacheCallback,
						  sizeof(GUID) ) == 0 )
		{
			/* Return a pointer to our intermediate extension function */
			*((LPFN_WSPMEMORYREGISTRATIONCACHECALLBACK *) lpvOutBuffer ) =
				IBSPMemoryRegistrationCacheCallback;
		}
		else
		{
			IBSP_ERROR_EXIT( ("invalid extension GUID\n") );
			*lpErrno = WSAEINVAL;
			return SOCKET_ERROR;
		}
		IBSP_EXIT( IBSP_DBG_OPT );
		return 0;
	}

	socket_info = (struct ibsp_socket_info *)s;

	/* Verify the state of the socket */
	/* Not sure which state socket should be in to receive this call */
	DebugPrintIBSPIoctlParams( IBSP_DBG_OPT, 
							  dwIoControlCode,
							  lpvInBuffer,
							  cbInBuffer,
							  lpvOutBuffer,
							  cbOutBuffer, lpOverlapped, lpCompletionRoutine, lpThreadId );

	switch( dwIoControlCode )
	{
	case SIO_GET_QOS:
	case SIO_GET_GROUP_QOS:
	case SIO_SET_QOS:
	case SIO_SET_GROUP_QOS:
		/* We don't support that. dwServiceFlags1 in installSP 
		 * wasn't set. */
		IBSP_ERROR_EXIT( ("unsupported dwIoControlCode %d\n", dwIoControlCode) );
		*lpErrno = WSAENOPROTOOPT;
		return SOCKET_ERROR;
		break;

	case SIO_ADDRESS_LIST_QUERY:
		{
			int ret;

			*lpcbBytesReturned = cbOutBuffer;
			ret = build_ip_list( (LPSOCKET_ADDRESS_LIST)lpvOutBuffer,
								lpcbBytesReturned, lpErrno );

			IBSP_EXIT( IBSP_DBG_OPT );
			return ret;
		}
		break;

	default:
		IBSP_ERROR_EXIT( ("invalid dwIoControlCode %d\n", dwIoControlCode) );

		*lpErrno = WSAENOPROTOOPT;
		return SOCKET_ERROR;
		break;
	}

	/* unreachable */
}


/* Function: IBSPListen
 * 
 * Description:
 *   This function establishes a socket to listen for incoming connections. It sets
 *   the backlog value on a listening socket.
 */
static int WSPAPI
IBSPListen(
					SOCKET						s,
					int							backlog,
					LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;
	int ret;

	IBSP_ENTER( IBSP_DBG_CONN );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	cl_spinlock_acquire( &socket_info->mutex1 );

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CONN, ("socket_state is %s\n",
		IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );

	/* Verify the state of the socket */
	switch( socket_info->socket_state )
	{
	case IBSP_BIND:

		/* Store the backlog value in the context */
		socket_info->listen.backlog = backlog;
		IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_LISTEN );

		socket_info->listen.listen_req_param.dwProcessId = 0;
		cl_memclr( &socket_info->listen.listen_req_param.identifier,
			sizeof(socket_info->listen.listen_req_param.identifier) );

		ret = ib_listen( socket_info );
		if( ret )
		{
			IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_BIND );
			IBSP_ERROR_EXIT( ("ib_listen failed with %d\n", ret) );
		}
		break;

	case IBSP_LISTEN:
		/* Change the backlog */
		ib_listen_backlog( socket_info, backlog );
		ret = 0;
		break;

	default:
		IBSP_ERROR( ("Invalid socket_state (%s)\n",
			IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );
		ret = WSAEINVAL;
		break;
	}

	cl_spinlock_release( &socket_info->mutex1 );

	*lpErrno = ret;

	IBSP_EXIT( IBSP_DBG_CONN );
	if( ret )
		return SOCKET_ERROR;
	else
		return 0;
}


/* Function: IBSPRecv
 * 
 * Description:
 *   This function receives data on a given socket and also allows for asynchronous
 *   (overlapped) operation. First translate the socket handle to the lower provider
 *   handle and then make the receive call. If called with overlap, post the operation
 *   to our IOCP or completion routine.
*/
static int WSPAPI
IBSPRecv(
					SOCKET						s,
					LPWSABUF					lpBuffers,
					DWORD						dwBufferCount,
					LPDWORD						lpNumberOfBytesRecvd,
					LPDWORD						lpFlags,
					LPWSAOVERLAPPED				lpOverlapped,
					LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
					LPWSATHREADID				lpThreadId,
					LPINT						lpErrno )
{
	struct ibsp_socket_info		*socket_info = (struct ibsp_socket_info *)s;
	ib_api_status_t				status;
	struct memory_node			*node;
	struct _recv_wr				*wr;
	DWORD						ds_idx;

	IBSP_ENTER( IBSP_DBG_IO );

	UNUSED_PARAM( lpNumberOfBytesRecvd );
	UNUSED_PARAM( lpCompletionRoutine );
	UNUSED_PARAM( lpThreadId );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	CL_ASSERT( lpCompletionRoutine == NULL );
	CL_ASSERT( lpOverlapped != NULL );

	if( s == INVALID_SOCKET )
	{
		/* Seen in real life with overlap/client test. 
		 * The switch closes a socket then calls this. Why? */
		IBSP_PRINT_EXIT(TRACE_LEVEL_WARNING, IBSP_DBG_IO,
			("invalid socket handle %Ix\n", s) );
		*lpErrno = WSAENOTSOCK;
		return SOCKET_ERROR;
	}

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

	if( socket_info->qp_error != 0 )
	{
		IBSP_ERROR_EXIT( ("QP is in error state %d\n", socket_info->qp_error) );
		*lpErrno = socket_info->qp_error;
		return SOCKET_ERROR;
	}

	/* This function only works for that case. Right now the switch is
	 * only using that. */
	if( dwBufferCount > QP_ATTRIB_RQ_SGE )
	{
		CL_ASSERT( dwBufferCount <= QP_ATTRIB_RQ_SGE );
		IBSP_ERROR_EXIT( ("dwBufferCount is greater than %d\n", 
			QP_ATTRIB_RQ_SGE) );
		*lpErrno = WSAEINVAL;
		return SOCKET_ERROR;
	}

	cl_spinlock_acquire( &socket_info->recv_lock );
	if( socket_info->recv_cnt == QP_ATTRIB_RQ_DEPTH )
	{
		/* This should never happen */
		cl_spinlock_release( &socket_info->recv_lock );
		IBSP_ERROR_EXIT( ("not enough wr on the free list\n") );
		*lpErrno = WSAENETDOWN;
		return SOCKET_ERROR;
	}

	wr = &socket_info->recv_wr[socket_info->recv_idx];

	wr->wr.lpOverlapped = lpOverlapped;
	wr->wr.socket_info = socket_info;

	/* Looks good. Post the receive buffer. */
	wr->recv.p_next = NULL;
	wr->recv.wr_id = (ULONG_PTR)wr;
	wr->recv.num_ds = dwBufferCount;
	wr->recv.ds_array = wr->ds_array;

	for( ds_idx = 0; ds_idx < dwBufferCount; ds_idx++ )
	{
		/* Get the memory region node */
		node = lookup_partial_mr( socket_info, IB_AC_LOCAL_WRITE,
			lpBuffers[ds_idx].buf, lpBuffers[ds_idx].len );
		if( !node )
		{
			cl_spinlock_release( &socket_info->recv_lock );
			/*
			 * No mr fits. This should never happen. This error is not 
			 * official, but seems to be the closest.
			 */
			IBSP_ERROR_EXIT( ("no MR found\n") );
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}

		wr->ds_array[ds_idx].vaddr =
			(ULONG_PTR)lpBuffers[ds_idx].buf;
		wr->ds_array[ds_idx].length = lpBuffers[ds_idx].len;
		wr->ds_array[ds_idx].lkey = node->p_reg1->lkey;
	}

	/*
	 * We must set this now, because the operation could complete
	 * before ib_post_Recv returns.
	 */
	lpOverlapped->Internal = WSS_OPERATION_IN_PROGRESS;

	/* Store the flags for reporting back in IBSPGetOverlappedResult */
	lpOverlapped->Offset = *lpFlags;

	cl_atomic_inc( &socket_info->recv_cnt );

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

	cl_atomic_inc( &g_ibsp.recv_count );
	cl_atomic_inc( &g_ibsp.total_recv_count );

	fzprint(("%s():%d:0x%x:0x%x: ov=0x%p h0=%d h1=%d h1_c=%d send=%d recv=%d\n",
			 __FUNCTION__, __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), lpOverlapped,
			 g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count,
			 g_ibsp.overlap_h1_comp_count, g_ibsp.send_count, g_ibsp.recv_count));
#endif

#ifdef IBSP_LOGGING
	wr->idx = socket_info->recv_log_idx++;
#endif

	fzprint(("%s():%d:0x%x:0x%x: posting RECV socket=0x%p overlap=%p wr=0x%p\n",
			 __FUNCTION__, __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s,
			 lpOverlapped, wr));

	status = ib_post_recv( socket_info->qp, &wr->recv, NULL );

	if( status == IB_SUCCESS )
	{
		/* Update the index and wrap as needed */
#if QP_ATTRIB_RQ_DEPTH == 256 || QP_ATTRIB_RQ_DEPTH == 128 || \
	QP_ATTRIB_RQ_DEPTH == 64 || QP_ATTRIB_RQ_DEPTH == 32 || \
	QP_ATTRIB_RQ_DEPTH == 16 || QP_ATTRIB_RQ_DEPTH == 8
		socket_info->recv_idx++;
		socket_info->recv_idx &= (QP_ATTRIB_RQ_DEPTH - 1);
#else
		if( ++socket_info->recv_idx == QP_ATTRIB_RQ_DEPTH )
			socket_info->recv_idx = 0;
#endif

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
			("Posted RECV: socket=%p, ov=%p, addr=%p, len=%d\n",
			(void*)s, lpOverlapped, lpBuffers[0].buf, lpBuffers[0].len));

		*lpErrno = WSA_IO_PENDING;
	}
	else
	{
		IBSP_ERROR( ("ib_post_recv returned %s\n", ib_get_err_str( status )) );
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

		cl_atomic_dec( &g_ibsp.recv_count );

		memset( wr, 0x33, sizeof(struct _recv_wr) );
#endif

		cl_atomic_dec( &socket_info->recv_cnt );
		*lpErrno = ibal_to_wsa_error( status );
	}

	cl_spinlock_release( &socket_info->recv_lock );

	/* We never complete the operation here. */
	IBSP_EXIT( IBSP_DBG_IO );
	return SOCKET_ERROR;
}


void print_cur_apm_state(ib_qp_handle_t h_qp)
{
	ib_qp_attr_t qp_attr;
	char *apm_states[] = { "IB_APM_MIGRATED", "IB_APM_REARM", "IB_APM_ARMED" };

	if (!ib_query_qp(h_qp, &qp_attr)) {
		IBSP_ERROR(("Querying QP returned that APM FSM is %s (%d)\n", 
			(qp_attr.apm_state<1 || qp_attr.apm_state>3) ? "UNKNOWN" : 
			apm_states[qp_attr.apm_state-1], qp_attr.apm_state));
	}
	Sleep(10);
}

/* Function: IBSPSend
 *
 *  Description:
 *    This function sends data on a given socket and also allows for asynchronous
 *    (overlapped) operation. First translate the socket handle to the lower provider
 *    handle and then make the send call.
*/
static int WSPAPI
IBSPSend(
	IN				SOCKET						s,
	IN				LPWSABUF					lpBuffers,
	IN				DWORD						dwBufferCount,
		OUT			LPDWORD						lpNumberOfBytesSent,
	IN				DWORD						dwFlags,
	IN				LPWSAOVERLAPPED				lpOverlapped,
	IN				LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
	IN				LPWSATHREADID				lpThreadId,
		OUT			LPINT						lpErrno )
{
	struct ibsp_socket_info		*socket_info = (struct ibsp_socket_info *)s;
	ib_api_status_t				status;
	struct memory_node			*node;
	struct _wr					*wr;
	ib_send_wr_t				send_wr;
	ib_local_ds_t				local_ds[QP_ATTRIB_SQ_SGE];
	DWORD						ds_idx;

	IBSP_ENTER( IBSP_DBG_IO );

	UNUSED_PARAM( lpNumberOfBytesSent );
	UNUSED_PARAM( lpCompletionRoutine );
	UNUSED_PARAM( lpThreadId );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p overlap=%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s, lpOverlapped));

	if( s == INVALID_SOCKET )
	{
		IBSP_ERROR_EXIT( ("invalid socket handle %Ix\n", s) );
		*lpErrno = WSAENOTSOCK;
		return SOCKET_ERROR;
	}

	CL_ASSERT( lpCompletionRoutine == NULL );
	CL_ASSERT( lpOverlapped != NULL );

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
		IBSP_ERROR_EXIT( ("dwBufferCount is greater than %d\n", QP_ATTRIB_SQ_SGE) );
		*lpErrno = WSAEINVAL;
		return SOCKET_ERROR;
	}

	/* The send lock is only used to serialize posting. */
	cl_spinlock_acquire( &socket_info->send_lock );
	if( socket_info->send_cnt == QP_ATTRIB_SQ_DEPTH )
	{
		/* This should never happen */
		cl_spinlock_release( &socket_info->send_lock );
		IBSP_ERROR_EXIT( ("not enough wr on the free list\n") );
		*lpErrno = WSAENETDOWN;
		return SOCKET_ERROR;
	}

	wr = &socket_info->send_wr[socket_info->send_idx];

	wr->lpOverlapped = lpOverlapped;
	wr->socket_info = socket_info;

	/* Looks good. Post the send buffer. */
	send_wr.p_next = NULL;
	send_wr.wr_id = (uint64_t) (uintptr_t) wr;
	send_wr.wr_type = WR_SEND;
	send_wr.send_opt = socket_info->send_opt;
	socket_info->send_opt = 0;

	send_wr.num_ds = dwBufferCount;
	send_wr.ds_array = local_ds;

	lpOverlapped->InternalHigh = 0;
	for( ds_idx = 0; ds_idx < dwBufferCount; ds_idx++ )
	{
		local_ds[ds_idx].vaddr = (ULONG_PTR)lpBuffers[ds_idx].buf;
		local_ds[ds_idx].length = lpBuffers[ds_idx].len;

		lpOverlapped->InternalHigh += lpBuffers[ds_idx].len;
	}

	if( lpOverlapped->InternalHigh <= socket_info->max_inline )
	{
		send_wr.send_opt |= IB_SEND_OPT_INLINE;
	}
	else
	{
		for( ds_idx = 0; ds_idx < dwBufferCount; ds_idx++ )
		{
			/* Get the memory region node */
			node = lookup_partial_mr( socket_info, 0,	/* READ */
				lpBuffers[ds_idx].buf, lpBuffers[ds_idx].len );
			if( !node )
			{
				cl_spinlock_release( &socket_info->send_lock );
				/*
				 * No mr fits. This error is not official, but seems to be the
				 * closest.
				 */
				IBSP_ERROR_EXIT( ("mr lookup failed\n") );
				*lpErrno = WSAEFAULT;
				return SOCKET_ERROR;
			}

			local_ds[ds_idx].lkey = node->p_reg1->lkey;
		}
	}

	/*
	 * We must set this now, because the operation could complete
	 * before ib_post_send returns.
	 */
	lpOverlapped->Internal = WSS_OPERATION_IN_PROGRESS;

	/* Store the flags for reporting back in IBSPGetOverlappedResult */
	lpOverlapped->Offset = dwFlags;

	cl_atomic_inc( &socket_info->send_cnt );

#ifdef _DEBUG_
	if( lpOverlapped->hEvent == 0)
	{
		cl_atomic_inc( &g_ibsp.overlap_h0_count );
	}
	else
	{
		cl_atomic_inc( &g_ibsp.overlap_h1_count );
		cl_atomic_inc( &g_ibsp.overlap_h1_comp_count );
	}

	cl_atomic_inc( &g_ibsp.send_count );
	cl_atomic_inc( &g_ibsp.total_send_count );

	fzprint(("%s():%d:0x%x:0x%x: ov=0x%p h0=%d h1=%d h1_c=%d send=%d recv=%d\n",
			 __FUNCTION__, __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), lpOverlapped,
			 g_ibsp.overlap_h0_count, g_ibsp.overlap_h1_count,
			 g_ibsp.overlap_h1_comp_count, g_ibsp.send_count, g_ibsp.recv_count));


#endif


	fzprint(("%s():%d:0x%x:0x%x: posting SEND %p, mr handle=%p, addr=%p, len=%d\n",
			 __FUNCTION__,
			 __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(),
			 lpOverlapped, node, lpBuffers[0].buf, lpBuffers[0].len));

#ifdef _DEBUG_
	if( lpBuffers[0].len >= 40 )
	{
		debug_dump_buffer( IBSP_DBG_WQ , "SEND",
			lpBuffers[0].buf, 40 );
	}
#endif

#ifdef IBSP_LOGGING
	{
		DWORD						i;

		for( i=0; i < dwBufferCount; i++ )
		{
			DataLogger_WriteData( &socket_info->SendDataLogger,
				socket_info->send_log_idx++, lpBuffers[i].buf,
				lpBuffers[i].len);
		}
	}
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


		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_IO,
			("Posted SEND: socket=%p, ov=%p, addr=%p, len=%d\n",
			(void*)s, lpOverlapped, lpBuffers[0].buf, lpBuffers[0].len));

		*lpErrno = WSA_IO_PENDING;
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
		cl_atomic_dec( &g_ibsp.send_count );

		memset( wr, 0x37, sizeof(struct _wr) );
#endif
		cl_atomic_dec( &socket_info->send_cnt );

		*lpErrno = ibal_to_wsa_error( status );
	}
	cl_spinlock_release( &socket_info->send_lock );

	/* We never complete the operation here. */
	IBSP_EXIT( IBSP_DBG_IO );
	return SOCKET_ERROR;
}


/* Function: IBSPSetSockOpt
 * 
 *  Description:
 *    Set a socket option. For most all options we just have to translate the
 *    socket option and call the lower provider. The only special case is for
 *    SO_UPDATE_ACCEPT_CONTEXT in which case a socket handle is passed as the
 *    argument which we need to translate before calling the lower provider.
 */
static int WSPAPI
IBSPSetSockOpt(
					SOCKET						s,
					int							level,
					int							optname,
					const char FAR				*optval,
					int							optlen,
					LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = (struct ibsp_socket_info *)s;

	IBSP_ENTER( IBSP_DBG_OPT );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), s));

	if( level != SOL_SOCKET )
	{
		IBSP_ERROR_EXIT( ("invalid level %d", level) );
		*lpErrno = WSAENOPROTOOPT;
		return SOCKET_ERROR;
	}

	if( optval == NULL )
	{
		IBSP_ERROR_EXIT( ("invalid optval=%p", optval) );
		*lpErrno = WSAEFAULT;
		return SOCKET_ERROR;
	}

	switch( optname )
	{
	case SO_DEBUG:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Option name SO_DEBUG\n") );
		if( optlen != sizeof(BOOL) )
		{
			IBSP_ERROR_EXIT( ("option len is invalid (0x%x)\n", optlen) );
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}
		memcpy( &socket_info->socket_options.debug, optval, sizeof(BOOL) );
		break;

	case SO_GROUP_PRIORITY:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_OPT, ("Option name SO_GROUP_PRIORITY\n") );
		if( optlen != sizeof(int) )
		{
			IBSP_ERROR_EXIT( ("option len is invalid (0x%x)\n", optlen) );
			*lpErrno = WSAEFAULT;
			return SOCKET_ERROR;
		}
		memcpy( &socket_info->socket_options.group_priority, optval, sizeof(int) );
		break;

	default:
		IBSP_ERROR_EXIT( ("invalid option %x\n", optname) );
		*lpErrno = WSAENOPROTOOPT;
		return SOCKET_ERROR;
		break;
	}

	IBSP_EXIT( IBSP_DBG_OPT );

	return 0;
}


/* Function: IBSPSocket
 *
 *  Description:
 *    This function creates a socket. There are two sockets created. The first
 *    socket is created by calling the lower providers WSPSocket. This is the
 *    handle that we use internally within our LSP. We then create a second
 *    socket with WPUCreateSocketHandle which will be returned to the calling
 *    application. We will also create a socket context structure which will
 *    maintain information on each socket. This context is associated with the
 *    socket handle passed to the application.
*/
static SOCKET WSPAPI
IBSPSocket(
					int							af,
					int							type,
					int							protocol,
					LPWSAPROTOCOL_INFOW			lpProtocolInfo,
					GROUP						g,
					DWORD						dwFlags,
					LPINT						lpErrno )
{
	struct ibsp_socket_info *socket_info = NULL;

	IBSP_ENTER( IBSP_DBG_SI );

	UNUSED_PARAM( g );

	if( af != AF_INET )
	{
		IBSP_ERROR_EXIT( ("bad family %d instead of %d\n", af, AF_INET) );
		*lpErrno = WSAEAFNOSUPPORT;
		return INVALID_SOCKET;
	}

	if( type != SOCK_STREAM )
	{
		IBSP_ERROR_EXIT( ("bad type %d instead of %d\n", type, SOCK_STREAM) );
		*lpErrno = WSAEPROTOTYPE;
		return INVALID_SOCKET;
	}

	if( protocol != IPPROTO_TCP )
	{
		IBSP_ERROR_EXIT( ("bad protocol %d instead of %d\n", protocol, IPPROTO_TCP) );
		*lpErrno = WSAEPROTONOSUPPORT;
		return INVALID_SOCKET;
	}

	if( (dwFlags != WSA_FLAG_OVERLAPPED) )
	{
		IBSP_ERROR_EXIT( ("dwFlags is not WSA_FLAG_OVERLAPPED (%x)\n", dwFlags) );
		*lpErrno = WSAEINVAL;
		return INVALID_SOCKET;
	}

	socket_info = create_socket_info( lpErrno );
	if( socket_info == NULL )
	{
		IBSP_ERROR_EXIT( ("create_socket_info return NULL\n") );
		return INVALID_SOCKET;
	}

	if( lpProtocolInfo->dwProviderReserved != 0 )
	{
		/* This is a duplicate socket. */
		*lpErrno = setup_duplicate_socket( socket_info,
			(HANDLE)(ULONG_PTR)lpProtocolInfo->dwProviderReserved );
		if( *lpErrno )
		{
			deref_socket_info( socket_info );
			IBSP_ERROR( ("setup_duplicate_socket failed with %d\n", *lpErrno) );
			return INVALID_SOCKET;
		}
	}
	else
	{
		socket_info->socket_state = IBSP_CREATE;

		/* Set the (non-zero) default socket options for that socket */
		socket_info->socket_options.max_msg_size = IB_MAX_MSG_SIZE;
		socket_info->socket_options.max_rdma_size = IB_MAX_RDMA_SIZE;
		socket_info->socket_options.rdma_threshold_size = IB_RDMA_THRESHOLD_SIZE;
	}

	cl_spinlock_acquire( &g_ibsp.socket_info_mutex );
	cl_qlist_insert_tail( &g_ibsp.socket_info_list, &socket_info->item );
	cl_spinlock_release( &g_ibsp.socket_info_mutex );

	*lpErrno = 0;

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), socket_info));

	IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_SI,
		("returning socket handle %p\n", socket_info) );

	return (SOCKET) socket_info;
}


/* Function: IBSPCleanup
 *
 *  Description:
 *    Decrement the entry count. If equal to zero then we can prepare to have us
 *    unloaded. Close any outstanding sockets and free up allocated memory.
 */
static int WSPAPI
IBSPCleanup(
					LPINT						lpErrno )
{
	int ret = 0;

	IBSP_ENTER( IBSP_DBG_INIT );

	cl_spinlock_acquire( &g_ibsp.mutex );

	if( !g_ibsp.entry_count )
	{
		cl_spinlock_release( &g_ibsp.mutex );

		*lpErrno = WSANOTINITIALISED;

		IBSP_ERROR_EXIT( ("returning WSAENOTINITIALISED\n") );

		return SOCKET_ERROR;
	}

	/* Decrement the entry count */
	g_ibsp.entry_count--;

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_INIT, ("WSPCleanup: %d\n", g_ibsp.entry_count) );

	if( g_ibsp.entry_count == 0 )
	{
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_INIT, ("entry_count is 0 => cleaning up\n") );
		ShutDownApmLib();
		ib_release();

#ifdef PERFMON_ENABLED
		IBSPPmReleaseSlot();
#endif
	}

	cl_spinlock_release( &g_ibsp.mutex );

	IBSP_EXIT( IBSP_DBG_INIT );

	return ret;
}


/*
 * Function: WSPStartupEx
 *
 *  Description:
 *    This function intializes the service provider. We maintain a ref count to keep track
 *    of how many times this function has been called.
 */
int WSPAPI
WSPStartupEx(
					WORD						wVersion,
					LPWSPDATA					lpWSPData,
					LPWSAPROTOCOL_INFOW			lpProtocolInfo,
					LPWSPUPCALLTABLEEX			UpCallTable,
					LPWSPPROC_TABLE				lpProcTable )
{
	static WSPPROC_TABLE gProcTable;
	static WSPDATA gWSPData;

	IBSP_ENTER( IBSP_DBG_INIT );

	/* Make sure that the version requested is >= 2.2. The low byte is the 
	   major version and the high byte is the minor version. */
	if( (LOBYTE(wVersion) < 2) || ((LOBYTE(wVersion) == 2) && (HIBYTE(wVersion) < 2)) )
	{
		IBSP_ERROR_EXIT( ("Invalid winsock version requested %x\n", wVersion) );

		return WSAVERNOTSUPPORTED;
	}

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_INIT, ("entry_count=%d)\n", g_ibsp.entry_count) );

	cl_spinlock_acquire( &g_ibsp.mutex );

	if( g_ibsp.entry_count == 0 )
	{
		int ret;

		/* Save the global WSPData */
		gWSPData.wVersion = MAKEWORD(2, 2);
		gWSPData.wHighVersion = MAKEWORD(2, 2);
		wcscpy( gWSPData.szDescription, Description );

		/* provide Service provider's entry points in proc table */
		memset( &gProcTable, 0, sizeof(gProcTable) );
		gProcTable.lpWSPAccept = IBSPAccept;
		gProcTable.lpWSPBind = IBSPBind;
		gProcTable.lpWSPCleanup = IBSPCleanup;
		gProcTable.lpWSPCloseSocket = IBSPCloseSocket;
		gProcTable.lpWSPConnect = IBSPConnect;
		gProcTable.lpWSPDuplicateSocket = IBSPDuplicateSocket;
		gProcTable.lpWSPEnumNetworkEvents = IBSPEnumNetworkEvents;
		gProcTable.lpWSPEventSelect = IBSPEventSelect;
		gProcTable.lpWSPGetOverlappedResult = IBSPGetOverlappedResult;
		gProcTable.lpWSPGetSockOpt = IBSPGetSockOpt;
		gProcTable.lpWSPGetQOSByName = IBSPGetQOSByName;
		gProcTable.lpWSPIoctl = IBSPIoctl;
		gProcTable.lpWSPListen = IBSPListen;
		gProcTable.lpWSPRecv = IBSPRecv;
		gProcTable.lpWSPSend = IBSPSend;
		gProcTable.lpWSPSetSockOpt = IBSPSetSockOpt;
		gProcTable.lpWSPSocket = IBSPSocket;

		/* Since we only support 2.2, set both wVersion and wHighVersion to 2.2. */
		lpWSPData->wVersion = MAKEWORD(2, 2);
		lpWSPData->wHighVersion = MAKEWORD(2, 2);
		wcscpy( lpWSPData->szDescription, Description );

#ifdef LATER
		/* TODO: remove? */
		cl_qlist_init( &g_ibsp.cq_thread_info_list );
		cl_spinlock_init( &g_ibsp.cq_thread_info_mutex );
#endif

		g_ibsp.protocol_info = *lpProtocolInfo;

		/* Initialize Infiniband */
		ret = ibsp_initialize();
		if( ret )
		{
			IBSP_ERROR_EXIT( ("ibsp_initialize failed (%d)\n", ret) );
			return ret;
		}
	}
	g_ibsp.entry_count++;

	cl_spinlock_release( &g_ibsp.mutex );

	/* Set the return parameters */
	*lpWSPData = gWSPData;
	*lpProcTable = gProcTable;

	/* store the upcall function table */
	g_ibsp.up_call_table = *UpCallTable;

	IBSP_EXIT( IBSP_DBG_INIT );

#ifdef PERFMON_ENABLED
	/* Socket application register with perfmon */
	IBSPPmGetSlot();
#endif /* PERFMON_ENABLED */

	return 0;
}


// TRUE means that all is well with socket, no need to recall it
BOOL rearm_socket(struct ibsp_socket_info *socket_info)
{


	ib_path_rec_t path_rec;
	ib_cm_lap_t cm_lap;
	int ret;
	ib_api_status_t	status;

	ib_net64_t dest_port_guid; 
	ib_net64_t src_port_guid;

	CL_ASSERT(socket_info->active_side == TRUE);
	// Try to send the LAP message:


	if ((socket_info->SuccesfulMigrations & 1) == 0)
	{
		src_port_guid = socket_info->port->guid;
		dest_port_guid = socket_info->dest_port_guid;
	}
	else 
	{
		src_port_guid = GetOtherPortGuid(socket_info->port->guid);
		dest_port_guid = GetOtherPortGuid(socket_info->dest_port_guid);
	}
		/* Get the path record */
	ret = query_pr( src_port_guid, dest_port_guid, socket_info->port->hca->dev_id, &path_rec );
	if(ret != IB_SUCCESS)
	{
		IBSP_ERROR( ("query_pr for apm failed\n") );
		return FALSE;
	}

	cl_memclr(&cm_lap,  sizeof(cm_lap));
	cm_lap.qp_type = IB_QPT_RELIABLE_CONN;
	cm_lap.h_qp = socket_info->qp;
	cm_lap.remote_resp_timeout = ib_path_rec_pkt_life( &path_rec ) + CM_REMOTE_TIMEOUT;
	cm_lap.p_alt_path = &path_rec;
	cm_lap.pfn_cm_apr_cb = cm_apr_callback;
	status = ib_cm_lap(&cm_lap);
	if( status != IB_SUCCESS )
	{
		/* Note: a REJ has been automatically sent. */
		IBSP_ERROR( ("ib_cm_lap returned %s\n", ib_get_err_str( status )) );
		return FALSE;
	} 
	else 
	{
		IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_APM, ("ib_cm_lap returned succesfuly\n") );
		socket_info->apm_state = APM_LAP_SENT;
	}
		

	// Actually we always return false, since we need to make sure that the lap 
	// was realy successfull.
	return FALSE;
}



VOID APMCallback(struct ibsp_socket_info *apm_socket_info)
{
	cl_list_item_t *socket_item = NULL;
	BOOL found = FALSE;

	if (g_ibsp.apm_data.hEvent== 0) {
		// This means that we have failed to start our timer, not much
		// that we can do.
		return;
	}

	
	// Find our socket and mark it as needs to load a new path.
	// Avoid race by searching in the list
	// BUGBUG: Need to have a better solution than this
	cl_spinlock_acquire( &g_ibsp.socket_info_mutex );
	for( socket_item = cl_qlist_head( &g_ibsp.socket_info_list );
		socket_item != cl_qlist_end( &g_ibsp.socket_info_list );
		socket_item = cl_qlist_next( socket_item ) )
	{
		struct ibsp_socket_info *socket_info = NULL;
		socket_info = PARENT_STRUCT(socket_item, struct ibsp_socket_info, item);
		if (apm_socket_info == socket_info) {
			if (apm_socket_info->active_side) {
				CL_ASSERT(apm_socket_info->apm_state == APM_ARMED);
				apm_socket_info->apm_state = APM_MIGRATED ;
			}
			found = TRUE;
			break;
		}
	}
	CL_ASSERT(found == TRUE); // The case that we are not found is very rare
	                          // and is probably a bug

	SetEvent(g_ibsp.apm_data.hEvent);

	cl_spinlock_release( &g_ibsp.socket_info_mutex );

}

DWORD WINAPI ApmThreadProc(
  LPVOID lpParameter
)
{
	DWORD dwTimeOut = INFINITE;
	DWORD ret;
	cl_list_item_t *socket_item = NULL;

	UNREFERENCED_PARAMETER(lpParameter);

	for(;;) {
		BOOL AllSocketsDone = TRUE;
		ret = WaitForSingleObject(g_ibsp.apm_data.hEvent, dwTimeOut);
		if (g_ibsp.apm_data.ThreadExit) {
			return 0;
		}		
		cl_spinlock_acquire( &g_ibsp.socket_info_mutex );
		for( socket_item = cl_qlist_head( &g_ibsp.socket_info_list );
			socket_item != cl_qlist_end( &g_ibsp.socket_info_list );
			socket_item = cl_qlist_next( socket_item ) )
		{
			struct ibsp_socket_info *socket_info = NULL;
			socket_info = PARENT_STRUCT(socket_item, struct ibsp_socket_info, item);
			if(socket_info->apm_state == APM_MIGRATED)
			{
				AllSocketsDone &= rearm_socket(socket_info);
			} else 	if(socket_info->apm_state == APM_LAP_SENT) {
				AllSocketsDone = FALSE;
			}
		}
		if (AllSocketsDone) 
		{
			dwTimeOut = INFINITE;
		} 
		else 
		{
			dwTimeOut = 2000;
		}

		cl_spinlock_release( &g_ibsp.socket_info_mutex );

	
	}
}


void qp_event_handler(ib_async_event_rec_t *p_event)
{

	if (p_event->code == IB_AE_QP_APM) 
	{
		struct ibsp_socket_info		*socket_info = (struct ibsp_socket_info		*)p_event->context;
		IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_APM,("Received an APM event\n"));
		APMCallback(socket_info);
	}
}



BOOL InitApmLib()
{
	IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_APM,("called\n"));

	g_ibsp.apm_data.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_ibsp.apm_data.hEvent == NULL) {
		IBSP_ERROR_EXIT( ("CreateEvent failed with error %d\n", GetLastError()));
		return FALSE;
	}

	g_ibsp.apm_data.hThread =  CreateThread(
			NULL,                   // Default security attributes
			0,
			ApmThreadProc,
			NULL,
			0,
			NULL
			);      
	if (g_ibsp.apm_data.hThread == NULL) {
		IBSP_ERROR_EXIT( ("CreateThread failed with error %d\n", GetLastError()));
		CloseHandle(g_ibsp.apm_data.hEvent);
		g_ibsp.apm_data.hEvent = NULL;
		return FALSE;
	}


	return TRUE;



}


VOID ShutDownApmLib()
{
	DWORD dwRet;

	if (g_ibsp.apm_data.hEvent== 0) {
		// This means that we have failed to start our timer, not much
		// that we can do.
		return;
	}

	g_ibsp.apm_data.ThreadExit = TRUE;
	SetEvent(g_ibsp.apm_data.hEvent);

	dwRet = WaitForSingleObject(g_ibsp.apm_data.hThread, INFINITE);
	CL_ASSERT(dwRet == WAIT_OBJECT_0);

	dwRet = CloseHandle(g_ibsp.apm_data.hThread);
	CL_ASSERT(dwRet != 0);

	dwRet = CloseHandle(g_ibsp.apm_data.hEvent);
	CL_ASSERT(dwRet != 0);
}

