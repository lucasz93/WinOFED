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
#ifdef offsetof
#undef offsetof
#endif
#include "misc.tmh"
#endif

#include "ibspdll.h"

char *ibsp_socket_state_str[IBSP_NUM_STATES] = {
	"IBSP_CREATE",
	"IBSP_BIND",
	"IBSP_CONNECT",
	"IBSP_LISTEN",
	"IBSP_CONNECTED",
	"IBSP_DUPLICATING_OLD",
	"IBSP_DUPLICATING_NEW",
	"IBSP_DUPLICATING_REMOTE",
	"IBSP_DISCONNECTED",
	"IBSP_CLOSED"
};

/* Convert an IBAL error to a Winsock error. */
int
ibal_to_wsa_error(
			const	ib_api_status_t				status )
{
	switch( status )
	{
	case IB_SUCCESS:
		return 0;

	case IB_INVALID_QP_HANDLE:
		return WSAENOTCONN;

	case IB_INVALID_PARAMETER:
		return WSAEINVAL;

	case IB_INSUFFICIENT_RESOURCES:
		return WSAENOBUFS;

	case IB_INVALID_WR_TYPE:
		return WSAEINVAL;

	case IB_INVALID_QP_STATE:
		return WSAENOTCONN;

	default:
		return WSAEINVAL;
	}
}


/* Initialize the global structure. Only the mutex and entry_count fields 
 * have been initialized so far. */
int
init_globals( void )
{
	/* Set everything to 0 */
	memset( &g_ibsp, 0, sizeof(g_ibsp) );

	/* Create our private heap */
	g_ibsp.heap = HeapCreate( 0, 128000, 0 );
	if( g_ibsp.heap == NULL)
	{
		IBSP_ERROR_EXIT( ("HeapCreate() failed: %d\n", GetLastError()) );
		return 1;
	}

	/* Initialize our various lock and lists */
	cl_spinlock_init( &g_ibsp.mutex );

	cl_qlist_init( &g_ibsp.hca_list );
	cl_spinlock_init( &g_ibsp.hca_mutex );

	g_ibsp.h_ibat_dev = INVALID_HANDLE_VALUE;
	cl_fmap_init( &g_ibsp.ip_map, ip_cmp );
	cl_spinlock_init( &g_ibsp.ip_mutex );

	cl_qlist_init( &g_ibsp.socket_info_list );
	cl_rbmap_init( &g_ibsp.conn_map );
	cl_spinlock_init( &g_ibsp.socket_info_mutex );

	return 0;
}


/* Free ressources allocated in our global structure. */
void
release_globals( void )
{
	HeapDestroy( g_ibsp.heap );

	if( g_ibsp.h_ibat_dev != INVALID_HANDLE_VALUE )
		CloseHandle( g_ibsp.h_ibat_dev );

	cl_spinlock_destroy( &g_ibsp.socket_info_mutex );
	cl_spinlock_destroy( &g_ibsp.hca_mutex );
	cl_spinlock_destroy( &g_ibsp.ip_mutex );
	cl_spinlock_destroy( &g_ibsp.mutex );
}
