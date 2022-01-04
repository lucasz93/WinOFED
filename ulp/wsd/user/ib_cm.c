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
#include "ib_cm.tmh"
#endif

#include "ibspdll.h"

static void AL_API cm_req_callback(IN ib_cm_req_rec_t * p_cm_req_rec);
static void AL_API cm_rep_callback(IN ib_cm_rep_rec_t * p_cm_rep_rec);
static void AL_API cm_rtu_callback(IN ib_cm_rtu_rec_t * p_cm_rtu_rec);
static void AL_API cm_rej_callback(IN ib_cm_rej_rec_t * p_cm_rej_rec);
static void AL_API cm_mra_callback(IN ib_cm_mra_rec_t * p_cm_mra_rec);
static void AL_API cm_dreq_callback(IN ib_cm_dreq_rec_t * p_cm_dreq_rec);
void AL_API cm_apr_callback(IN ib_cm_apr_rec_t * p_cm_apr_rec);


/* Computes a service ID for a port. */
static inline ib_net64_t
get_service_id_for_port(
					ib_net16_t					ip_port)
{
	return BASE_LISTEN_ID | ip_port;
}


/* Signals a select event to the switch. */
void
ibsp_post_select_event(
					struct ibsp_socket_info		*socket_info,
					int							event,
					int							error )
{
	HANDLE		h_event;

	IBSP_ENTER( IBSP_DBG_NEV );

	CL_ASSERT( socket_info );
	CL_ASSERT( event );

	switch( event )
	{
	case FD_CONNECT:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_NEV,
			("socket %p FD_CONNECT\n", socket_info) );
		socket_info->errno_connect = error;
		break;

	case FD_ACCEPT:
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_NEV,
			("socket %p FD_ACCEPT\n", socket_info) );
		break;

	default:
		CL_ASSERT( 0 );
		break;
	}

	_InterlockedOr( &socket_info->network_events, event );

	h_event = InterlockedCompareExchangePointer(
		&socket_info->event_select, NULL, NULL );
	/* Check for event notification request and signal as needed. */
	if( (socket_info->event_mask & event) && h_event )
	{
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_NEV,
			("Signaling eventHandle %p at time %I64d.\n",
			h_event, cl_get_time_stamp() ) );
		SetEvent( h_event );
	}

	IBSP_EXIT( IBSP_DBG_NEV );
}


/*
 * A user-specified callback that is invoked after receiving a connection
 * request message (REQ).
 */
static void AL_API
cm_req_callback(
	IN				ib_cm_req_rec_t				*p_cm_req_rec )
{
	struct ibsp_socket_info *socket_info =
		(struct ibsp_socket_info *)p_cm_req_rec->context;
	struct listen_incoming *incoming;

	IBSP_ENTER( IBSP_DBG_CM );

	CL_ASSERT( socket_info );
	CL_ASSERT( p_cm_req_rec->p_req_pdata );

	cl_spinlock_acquire( &socket_info->mutex1 );

	switch( socket_info->socket_state )
	{
	case IBSP_LISTEN:
		if( cl_qlist_count( &socket_info->listen.list ) >=
			socket_info->listen.backlog )
		{
			/* Already too many connection requests are queued */
			IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CM,
				("already too many incoming connections, rejecting\n") );
			ib_reject( p_cm_req_rec->h_cm_req, IB_REJ_USER_DEFINED );
			break;
		}

		incoming = HeapAlloc( g_ibsp.heap, 0, sizeof(struct listen_incoming) );
		if( !incoming )
		{
			/* Low on memory. */
			IBSP_ERROR( ("HeapAlloc failed, rejecting\n") );
			ib_reject( p_cm_req_rec->h_cm_req, IB_REJ_INSUF_RESOURCES );
			IBSP_EXIT( IBSP_DBG_CM );
			return;
		}

		incoming->cm_req_received = *p_cm_req_rec;
		cl_memcpy( &incoming->params, p_cm_req_rec->p_req_pdata,
			sizeof(struct cm_req_params) );
		incoming->cm_req_received.p_req_pdata = (const uint8_t*)&incoming->params;

		/* Add to the waiting list */
		cl_qlist_insert_tail( &socket_info->listen.list, &incoming->item );

		ibsp_post_select_event( socket_info, FD_ACCEPT, 0 );
		break;

	case IBSP_DUPLICATING_REMOTE:
		{
			int ret;

			/* Non-blocking cancel since we're in CM callback context */
			ib_cm_cancel( socket_info->listen.handle, NULL );
			socket_info->listen.handle = NULL;
			cl_spinlock_release( &socket_info->mutex1 );

			wait_cq_drain( socket_info );

			cl_spinlock_acquire( &socket_info->mutex1 );
			ret = ib_accept( socket_info, p_cm_req_rec );
			if( ret )
			{
				cl_spinlock_release( &socket_info->mutex1 );
				IBSP_ERROR( (
					"ib_accept for duplicate socket returned %d, rejecting\n",
					ret) );
				/* Call ib_destroy_socket for above ib_create_socket() call */
				ib_destroy_socket( socket_info );
				ib_reject( p_cm_req_rec->h_cm_req, IB_REJ_USER_DEFINED );
				ibsp_dup_overlap_abort( socket_info );
				IBSP_EXIT( IBSP_DBG_CM );
				return;
			}
		}
		break;

	default:
		IBSP_ERROR( ("socket is not listening anymore\n") );
		/* We're closing down - let some other listen match. */
		ib_reject( p_cm_req_rec->h_cm_req, IB_REJ_INVALID_SID );
		break;
	}

	cl_spinlock_release( &socket_info->mutex1 );

	IBSP_EXIT( IBSP_DBG_CM );
}


/*
 * A user-specified callback that is invoked after receiving a connection
 * request reply message (REP).
 */
static void AL_API
cm_rep_callback(
	IN				ib_cm_rep_rec_t				*p_cm_rep_rec )
{
	struct ibsp_socket_info *socket_info =
		(struct ibsp_socket_info *)p_cm_rep_rec->qp_context;
	ib_cm_rtu_t cm_rtu;
	ib_api_status_t status;

	IBSP_ENTER( IBSP_DBG_CM );

	memset( &cm_rtu, 0, sizeof(cm_rtu) );

	cm_rtu.access_ctrl = IB_AC_RDMA_READ | IB_AC_RDMA_WRITE | IB_AC_LOCAL_WRITE;
#if 0
	// Bug in TAVOR
	cm_rtu.sq_depth = QP_ATTRIB_SQ_DEPTH;
	cm_rtu.rq_depth = QP_ATTRIB_RQ_DEPTH;
#endif
	cm_rtu.pfn_cm_apr_cb = cm_apr_callback;
	cm_rtu.pfn_cm_dreq_cb = cm_dreq_callback;

	cl_spinlock_acquire( &socket_info->mutex1 );

	switch( socket_info->socket_state )
	{
	case IBSP_CONNECT:
		status = ib_cm_rtu( p_cm_rep_rec->h_cm_rep, &cm_rtu );
		if( status != IB_SUCCESS )
		{
			/* Note: a REJ has been automatically sent. */
			IBSP_ERROR( ("ib_cm_rtu returned %s\n", ib_get_err_str( status )) );
			IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_BIND );

			/* We changed the state - remove from connection map. */
			ibsp_conn_remove( socket_info );

			ibsp_post_select_event( socket_info, FD_CONNECT, WSAETIMEDOUT );
		}
		else
		{
			IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_CONNECTED );
			ibsp_post_select_event( socket_info, FD_CONNECT, 0 );
		}
		break;

	case IBSP_DUPLICATING_NEW:
		status = ib_cm_rtu( p_cm_rep_rec->h_cm_rep, &cm_rtu );
		if( status != IB_SUCCESS )
		{
			IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_BIND );

			/* We changed the state - remove from connection map. */
			ibsp_conn_remove( socket_info );

			/* Note: a REJ has been automatically sent. */
			IBSP_ERROR( ("ib_cm_rtu returned %s\n", ib_get_err_str( status )) );
		}
		else
		{
			IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_CONNECTED );
		}
		SetEvent( socket_info->h_event );
		break;

	default:
		/* The socket might be closing */
		IBSP_ERROR( ("socket %p not in connecting state (%s)\n",
			socket_info, IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );

		ib_reject( p_cm_rep_rec->h_cm_rep, IB_REJ_USER_DEFINED );
	}

	cl_spinlock_release( &socket_info->mutex1 );

	IBSP_EXIT( IBSP_DBG_CM );
}


/*
 * A user-specified callback that is invoked after receiving a connection
 * ready to use message (RTU).
 */
static void AL_API
cm_rtu_callback(
	IN				ib_cm_rtu_rec_t				*p_cm_rtu_rec )
{
	struct ibsp_socket_info *socket_info =
		(struct ibsp_socket_info *)p_cm_rtu_rec->qp_context;

	IBSP_ENTER( IBSP_DBG_CM );

	cl_spinlock_acquire( &socket_info->mutex1 );

	if( socket_info->socket_state == IBSP_DUPLICATING_REMOTE )
	{
		struct _recv_wr		*wr;
		ib_api_status_t		status;
		uint8_t				idx;

		/* Repost all the WR to the new QP */
		cl_spinlock_acquire( &socket_info->recv_lock );

		while( socket_info->dup_cnt )
		{
			if( (socket_info->recv_cnt + socket_info->dup_cnt) >
				QP_ATTRIB_RQ_DEPTH )
			{
				CL_ASSERT( (socket_info->recv_cnt + socket_info->dup_cnt) <=
					QP_ATTRIB_RQ_DEPTH );
				/* TODO: Flag the socket as having failed. */
				break;
			}


			/* Figure out the starting index in the duplicate array. */
			idx = socket_info->dup_idx - (uint8_t)socket_info->dup_cnt;
			if( idx >= QP_ATTRIB_RQ_DEPTH )
			{
				/* The duplicates wrap over the end of the array. */
				idx += QP_ATTRIB_RQ_DEPTH;
			}

			/*
			 * Copy the duplicate work request from the duplicate array
			 * to the receive array.
			 */
			socket_info->recv_wr[socket_info->recv_idx] =
				socket_info->dup_wr[idx];

			wr = &socket_info->recv_wr[socket_info->recv_idx];

			/* Update the work request ID. */
			wr->recv.wr_id = (ULONG_PTR)wr;

			/*
			 * Increment the count before posting so it doesn't go
			 * negative in the completion path.
			 */
			cl_atomic_inc( &socket_info->recv_cnt );

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

				cl_atomic_dec( &socket_info->dup_cnt );
			}
			else
			{
				IBSP_ERROR( (
					"ib_post_recv returned %s for reposted buffer\n",
					ib_get_err_str( status )) );

				cl_atomic_dec( &socket_info->recv_cnt );
				CL_ASSERT( status == IB_SUCCESS );
				/* TODO: Flag the socket as having failed. */
				break;
			}
		}

		cl_spinlock_release( &socket_info->recv_lock );

		socket_info->qp_error = 0;
		IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_CONNECTED );
	}
	else if( socket_info->socket_state != IBSP_CONNECTED )
	{
		/* The Socket might be closing */
		IBSP_ERROR( ("Got RTU while in socket_state %s - ignoring\n",
			IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );
	}

	cl_spinlock_release( &socket_info->mutex1 );

	IBSP_EXIT( IBSP_DBG_CM );
}


/* Force the QP to error state to flush posted work requests. */
static inline void
__flush_qp(
	IN				struct ibsp_socket_info		*p_socket )
{
	ib_qp_mod_t			qp_mod;
	ib_api_status_t		status;

	memset( &qp_mod, 0, sizeof(qp_mod) );
	qp_mod.req_state = IB_QPS_ERROR;
	status = ib_modify_qp( p_socket->qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		IBSP_ERROR( ("ib_modify_qp returned %s\n", ib_get_err_str( status )) );
		p_socket->send_cnt = 0;
		p_socket->recv_cnt = 0;
	}
}


/*
 * A user-specified callback that is invoked after receiving a connection
 * rejection message (REJ).
 */
static void AL_API
cm_rej_callback(
	IN				ib_cm_rej_rec_t				*p_cm_rej_rec )
{
	struct ibsp_socket_info *socket_info =
		(struct ibsp_socket_info *)p_cm_rej_rec->qp_context;

	IBSP_ENTER( IBSP_DBG_CM );

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CM, ("socket %p connect reject, reason=%d\n",
		socket_info, cl_ntoh16(p_cm_rej_rec->rej_status)) );

	cl_spinlock_acquire( &socket_info->mutex1 );

	switch( socket_info->socket_state )
	{
	case IBSP_CONNECT:
		/* Remove from connection map. */
		ibsp_conn_remove( socket_info );

		IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_BIND );
		if( p_cm_rej_rec->rej_status == IB_REJ_TIMEOUT )
			ibsp_post_select_event( socket_info, FD_CONNECT, WSAETIMEDOUT );
		else
			ibsp_post_select_event( socket_info, FD_CONNECT, WSAECONNREFUSED );
		break;

	case IBSP_CONNECTED:
		/*
		 * DISCONNECTED is a terminal state.  We'll remove the connection
		 * when the socket gets destroyed.
		 */
		IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_DISCONNECTED );

		socket_info->qp_error = WSAECONNABORTED;

		__flush_qp( socket_info );
		break;

	case IBSP_DUPLICATING_NEW:
		/* Leave in that state. IBSPSocket will eventually return 
		 * an error becaus the socket is not connected. */
		ibsp_conn_remove( socket_info );
		SetEvent( socket_info->h_event );
		break;

	default:
		IBSP_ERROR( ("socket %p got an REJ reason %d in state %s\n",
			socket_info, cl_ntoh16( p_cm_rej_rec->rej_status ),
			IBSP_SOCKET_STATE_STR(socket_info->socket_state)) );
		break;
	}

	cl_spinlock_release( &socket_info->mutex1 );

	IBSP_EXIT( IBSP_DBG_CM );
}


/*
 * A user-specified callback that is invoked after receiving a message
 * received acknowledgement.
 */
static void AL_API
cm_mra_callback(
	IN				ib_cm_mra_rec_t				*p_cm_mra_rec )
{
	/* TODO */
	IBSP_ENTER( IBSP_DBG_CM );

	UNUSED_PARAM( p_cm_mra_rec );

	IBSP_EXIT( IBSP_DBG_CM );
}


/*
 * A user-specified callback that is invoked after receiving a disconnect
 * request message (DREQ).
 */
static void AL_API
cm_dreq_callback(
	IN				ib_cm_dreq_rec_t			*p_cm_dreq_rec )
{
	ib_api_status_t status;
	ib_cm_drep_t cm_drep;
	struct disconnect_reason *reason;
	struct ibsp_socket_info *socket_info =
		(struct ibsp_socket_info *)p_cm_dreq_rec->qp_context;

	IBSP_ENTER( IBSP_DBG_CM );
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CM,
		("socket=%p state=%s\n",
		socket_info, IBSP_SOCKET_STATE_STR( socket_info->socket_state )) );

	reason = (struct disconnect_reason *)p_cm_dreq_rec->p_dreq_pdata;

	cl_spinlock_acquire( &socket_info->mutex1 );

	if( socket_info->socket_state == IBSP_CONNECTED )
	{
		switch( reason->type )
		{
		case DISC_DUPLICATING:
			{
				int ret;

				IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_DUPLICATING_REMOTE );
				socket_info->qp_error = -1;
				socket_info->duplicate.identifier = reason->duplicating.identifier;
				socket_info->duplicate.dwProcessId = reason->duplicating.dwProcessId;

				/* Now, setup our listening callback. */
				socket_info->listen.listen_req_param.dwProcessId =
					reason->duplicating.dwProcessId;
				socket_info->listen.listen_req_param.identifier =
					reason->duplicating.identifier;

				ret = ib_listen( socket_info );
				if( !ret )
				{
					/* We changed the state - remove from connection map. */
					ibsp_conn_remove( socket_info );
					break;
				}

				IBSP_ERROR_EXIT( ("ib_listen failed with %d\n", ret) );
				/* Fall through. */
			}
		default:
			/* Right now, treat anything as a normal disconnect. */
			IBSP_CHANGE_SOCKET_STATE( socket_info, IBSP_DISCONNECTED );
			/*
			 * DISCONNECTED is a terminal state.  We'll remove the connection
			 * when the socket gets destroyed.
			 */
			socket_info->qp_error = WSAECONNRESET;
		}

		memset( &cm_drep, 0, sizeof(cm_drep) );

		status = ib_cm_drep( p_cm_dreq_rec->h_cm_dreq, &cm_drep );
		if( status != IB_SUCCESS )
			IBSP_ERROR( ("ib_cm_drep returned %s\n", ib_get_err_str( status )) );
	}
	cl_spinlock_release( &socket_info->mutex1 );

	IBSP_EXIT( IBSP_DBG_CM );
}


/*
 * A user-specified callback that is invoked after receiving a disconnect
 *	reply message.
 */
static void AL_API
cm_drep_callback(
	IN				ib_cm_drep_rec_t			*p_cm_drep_rec )
{
	IBSP_ENTER( IBSP_DBG_CM );
	UNUSED_PARAM( p_cm_drep_rec );
	IBSP_EXIT( IBSP_DBG_CM );
}


/*
 * A user-specified callback that is invoked after receiving a load
 * alternate path response message.
 */
void AL_API
cm_apr_callback(
	IN				ib_cm_apr_rec_t				*p_cm_apr_rec )
{
	struct ibsp_socket_info *socket_info =
		(struct ibsp_socket_info *)p_cm_apr_rec->qp_context;

	IBSP_ENTER( IBSP_DBG_CM );

	IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_APM, ("cm_apr_callback called p_cm_apr_rec->cm_status = %d\n", p_cm_apr_rec->cm_status) );

	cl_spinlock_acquire( &g_ibsp.socket_info_mutex );
	CL_ASSERT(socket_info->apm_state == APM_LAP_SENT);

	if ((p_cm_apr_rec->cm_status == IB_SUCCESS) && 
		(p_cm_apr_rec->apr_status == IB_SUCCESS)){
		socket_info->apm_state = APM_ARMED;
		socket_info->SuccesfulMigrations++;
	} else {
		socket_info->apm_state = APM_MIGRATED;
	}
	cl_spinlock_release( &g_ibsp.socket_info_mutex );




	IBSP_EXIT( IBSP_DBG_CM );
}


/*
 * A user-specified callback that is invoked after receiving a load
 * alternate path message.
 *
 * SYNOPSIS
 */
static void AL_API
cm_lap_callback(
	IN				ib_cm_lap_rec_t				*p_cm_lap_rec )
{
	ib_cm_apr_t cm_apr;
	struct ibsp_socket_info *socket_info =
		(struct ibsp_socket_info *)p_cm_lap_rec->qp_context;
	
	ib_api_status_t	status;

	IBSP_ENTER( IBSP_DBG_CM );

	IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_APM, ("called \n") );


	cl_memclr(&cm_apr,  sizeof(cm_apr));
	cm_apr.qp_type = IB_QPT_RELIABLE_CONN;
	cm_apr.h_qp = socket_info->qp;


	status = ib_cm_apr(p_cm_lap_rec->h_cm_lap, &cm_apr);
	if( status != IB_SUCCESS ) {
		// Actually not much that we can do at this stage.
		// The other side will get timeout and retry
		CL_ASSERT(FALSE);
		IBSP_ERROR( ("ib_cm_apr returned %s\n", ib_get_err_str( status )) );
	}


	IBSP_EXIT( IBSP_DBG_CM );
}


/* Listen for an incoming connection. */
int
ib_listen(
	IN				struct ibsp_socket_info		*socket_info )
{
	ib_cm_listen_t param;
	ib_api_status_t status;

	IBSP_ENTER( IBSP_DBG_CM );

	memset( &param, 0, sizeof(param) );

	param.svc_id = get_service_id_for_port( socket_info->local_addr.sin_port );
	if( socket_info->port )
	{
		/* The socket is bound to an IP address */
		param.ca_guid = socket_info->port->hca->guid;
		param.port_guid = socket_info->port->guid;
	}
	else
	{
		/* The socket is bound to INADDR_ANY */
		param.ca_guid = IB_ALL_CAS;
		param.port_guid = IB_ALL_PORTS;
	}
	param.lid = IB_ALL_LIDS;

	param.p_compare_buffer = (uint8_t *) & socket_info->listen.listen_req_param;
	param.compare_length = sizeof(struct listen_req_param);
	param.compare_offset = offsetof(struct cm_req_params, listen_req_param);

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p params: %x %x\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), socket_info,
			 socket_info->listen.listen_req_param.dwProcessId,
			 socket_info->listen.listen_req_param.identifier));

	param.pfn_cm_req_cb = cm_req_callback;

	param.qp_type = IB_QPT_RELIABLE_CONN;

	status = ib_cm_listen( g_ibsp.al_handle, &param, socket_info,	/* context */
		&socket_info->listen.handle );

	if( status != IB_SUCCESS )
	{
		IBSP_ERROR_EXIT( ("ib_cm_listen failed (0x%d)\n", status) );
		return ibal_to_wsa_error( status );
	}

	STAT_INC( listen_num );

	IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CM,
		("started listening for port %d\n",
		CL_HTON16( socket_info->local_addr.sin_port )) );

	return 0;
}


/* Reject all the queued incoming connection requests. */
void
ib_listen_backlog(
	IN				struct ibsp_socket_info		*socket_info,
	IN				int							backlog )
{
	cl_list_item_t *item;
	struct listen_incoming *incoming;

	socket_info->listen.backlog = backlog;

	while(
		cl_qlist_count( &socket_info->listen.list ) > (uint32_t)backlog )
	{
		item = cl_qlist_remove_tail( &socket_info->listen.list );

		incoming = PARENT_STRUCT(item, struct listen_incoming, item);

		ib_reject( incoming->cm_req_received.h_cm_req, IB_REJ_USER_DEFINED );

		HeapFree( g_ibsp.heap, 0, incoming );
	}
}


/* Stop listening on the socket. */
void
ib_listen_cancel(
	IN				struct ibsp_socket_info		*socket_info )
{
	ib_api_status_t status;

	IBSP_ENTER( IBSP_DBG_CM );

	status = ib_cm_cancel( socket_info->listen.handle, ib_sync_destroy );
	if( status )
	{
		IBSP_ERROR( (
			"ib_cm_cancel returned %s\n", ib_get_err_str( status )) );
	}
	else
	{
		STAT_DEC( listen_num );
	}

	/* We can empty the queue now. Since we are closing, 
	 * no new entry will be added. */
	cl_spinlock_acquire( &socket_info->mutex1 );
	ib_listen_backlog( socket_info, 0 );
	cl_spinlock_release( &socket_info->mutex1 );

	socket_info->listen.handle = NULL;

	IBSP_EXIT( IBSP_DBG_CM );
}


int
ib_connect(
	IN				struct ibsp_socket_info		*socket_info,
	IN				ib_path_rec_t				*path_rec, 
	IN				ib_path_rec_t				*alt_path_rec )

{
	ib_cm_req_t cm_req;
	ib_api_status_t status;
	struct cm_req_params params;

	IBSP_ENTER( IBSP_DBG_CM );

	fzprint(("%s():%d:0x%x:0x%x: socket=0x%p \n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId(), socket_info));

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CM, ("From:\n") );
	DebugPrintSockAddr( IBSP_DBG_CM, &socket_info->local_addr );
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CM, ("To:\n") );
	DebugPrintSockAddr( IBSP_DBG_CM, &socket_info->peer_addr );

	/* Insert into the connection map. */
	if( !ibsp_conn_insert( socket_info ) )
	{
		IBSP_EXIT( IBSP_DBG_CM );
		return WSAEADDRINUSE;
	}

	memset( &cm_req, 0, sizeof(cm_req) );

	cm_req.svc_id = get_service_id_for_port( socket_info->peer_addr.sin_port );
	cm_req.max_cm_retries = g_max_cm_retries;
	cm_req.p_primary_path = path_rec;
	cm_req.p_alt_path = alt_path_rec;
	cm_req.pfn_cm_rep_cb = cm_rep_callback;

	cm_req.p_req_pdata = (uint8_t *) & params;
	params.source = socket_info->local_addr;
	params.dest = socket_info->peer_addr;
	params.listen_req_param.dwProcessId = socket_info->duplicate.dwProcessId;
	params.listen_req_param.identifier = socket_info->duplicate.identifier;

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CM,
		("ib_connect listen params: %x \n", params.listen_req_param.dwProcessId
		/*params.listen_req_param.identifier*/));
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_CM,
		("connecting to port %d, SID=%016I64x\n", socket_info->peer_addr.sin_port,
		cm_req.svc_id) );

	cm_req.req_length = sizeof(struct cm_req_params);

	cm_req.qp_type = IB_QPT_RELIABLE_CONN;
	cm_req.h_qp = socket_info->qp;
	cm_req.resp_res = QP_ATTRIB_RESPONDER_RESOURCES;
	cm_req.init_depth = QP_ATTRIB_INITIATOR_DEPTH;

	cm_req.remote_resp_timeout =
		ib_path_rec_pkt_life( path_rec ) + CM_REMOTE_TIMEOUT;
	if( cm_req.remote_resp_timeout > 0x1F )
		cm_req.remote_resp_timeout = 0x1F;
	else if( cm_req.remote_resp_timeout < CM_MIN_REMOTE_TIMEOUT )
		cm_req.remote_resp_timeout = CM_MIN_REMOTE_TIMEOUT;

	cm_req.flow_ctrl = TRUE;	/* HCAs must support end-to-end flow control. */

	cm_req.local_resp_timeout =
		ib_path_rec_pkt_life( path_rec ) + CM_LOCAL_TIMEOUT;
	if( cm_req.local_resp_timeout > 0x1F )
		cm_req.local_resp_timeout = 0x1F;
	else if( cm_req.local_resp_timeout < CM_MIN_LOCAL_TIMEOUT )
		cm_req.local_resp_timeout = CM_MIN_LOCAL_TIMEOUT;

	cm_req.rnr_nak_timeout = QP_ATTRIB_RNR_NAK_TIMEOUT;
	cm_req.rnr_retry_cnt = QP_ATTRIB_RNR_RETRY;
	cm_req.retry_cnt = g_qp_retries;
	cm_req.pfn_cm_mra_cb = cm_mra_callback;
	cm_req.pfn_cm_rej_cb = cm_rej_callback;

	status = ib_cm_req( &cm_req );
	if( status != IB_SUCCESS )
	{
		/* Remove from connection map. */
		ibsp_conn_remove( socket_info );

		IBSP_ERROR_EXIT( ("ib_cm_req failed (0x%d)\n", status) );
		return WSAEHOSTUNREACH;
	}

	IBSP_EXIT( IBSP_DBG_CM );
	/* Operation is pending */
	return WSAEWOULDBLOCK;
}


void
ib_reject(
	IN				const ib_cm_handle_t		h_cm,
	IN				const ib_rej_status_t		rej_status )
{
	ib_cm_rej_t cm_rej;
	ib_api_status_t status;

	IBSP_ENTER( IBSP_DBG_CM );

	memset( &cm_rej, 0, sizeof(cm_rej) );
	cm_rej.rej_status = rej_status;

	status = ib_cm_rej( h_cm, &cm_rej );
	if( status != IB_SUCCESS )
		IBSP_ERROR( ("ib_cm_rej returned %s\n", ib_get_err_str( status )) );

	IBSP_EXIT( IBSP_DBG_CM );
}


int
ib_accept(
	IN				struct ibsp_socket_info		*socket_info,
	IN				ib_cm_req_rec_t				*cm_req_received )
{
	ib_cm_rep_t cm_rep;
	ib_api_status_t status;

	IBSP_ENTER( IBSP_DBG_CM );

	/* Insert into the connection map. */
	if( !ibsp_conn_insert( socket_info ) )
	{
		IBSP_EXIT( IBSP_DBG_CM );
		return WSAEADDRINUSE;
	}

	memset( &cm_rep, 0, sizeof(cm_rep) );

	cm_rep.qp_type = IB_QPT_RELIABLE_CONN;
	cm_rep.h_qp = socket_info->qp;
	cm_rep.access_ctrl = IB_AC_RDMA_READ | IB_AC_RDMA_WRITE | IB_AC_LOCAL_WRITE;
#if 0
	// Bug in TAVOR
	cm_rep.sq_depth = QP_ATTRIB_SQ_DEPTH;
	cm_rep.rq_depth = QP_ATTRIB_RQ_DEPTH;
#endif
	cm_rep.init_depth = QP_ATTRIB_INITIATOR_DEPTH;
	cm_rep.target_ack_delay = 10;
	cm_rep.failover_accepted = g_use_APM ? IB_FAILOVER_ACCEPT_SUCCESS : IB_FAILOVER_ACCEPT_UNSUPPORTED;
	cm_rep.flow_ctrl = cm_req_received->flow_ctrl;
	cm_rep.rnr_nak_timeout = QP_ATTRIB_RNR_NAK_TIMEOUT;
	cm_rep.rnr_retry_cnt = cm_req_received->rnr_retry_cnt;
	cm_rep.pfn_cm_mra_cb = cm_mra_callback;
	cm_rep.pfn_cm_rej_cb = cm_rej_callback;
	cm_rep.pfn_cm_rtu_cb = cm_rtu_callback;
	cm_rep.pfn_cm_lap_cb = cm_lap_callback;
	cm_rep.pfn_cm_dreq_cb = cm_dreq_callback;

	fzprint(("%s():%d:0x%x:0x%x: flow_ctrl=%d rnr_retry_cnt=%d\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(),
			 GetCurrentThreadId(), cm_rep.flow_ctrl, cm_rep.rnr_retry_cnt));

	status = ib_cm_rep( cm_req_received->h_cm_req, &cm_rep );
	if( status != IB_SUCCESS )
	{
		/* Remove from connection map. */
		ibsp_conn_remove( socket_info );

		IBSP_ERROR_EXIT( ("ib_cm_rep failed (0x%s) at time %I64d\n",
			ib_get_err_str( status ), cl_get_time_stamp()) );
		return WSAEACCES;
	}

	IBSP_EXIT( IBSP_DBG_CM );
	return 0;
}


void
ib_disconnect(
	IN				struct ibsp_socket_info		*socket_info,
	IN				struct disconnect_reason	*reason )
{
	ib_api_status_t		status;
	ib_cm_dreq_t		cm_dreq;

	IBSP_ENTER( IBSP_DBG_CM );

	memset( &cm_dreq, 0, sizeof(cm_dreq) );

	cm_dreq.qp_type = IB_QPT_RELIABLE_CONN;
	cm_dreq.h_qp = socket_info->qp;
	cm_dreq.pfn_cm_drep_cb = cm_drep_callback;

	cm_dreq.p_dreq_pdata = (uint8_t *) reason;
	cm_dreq.dreq_length = sizeof(struct disconnect_reason);

	status = ib_cm_dreq( &cm_dreq );

	/*
	 * If both sides initiate disconnection, we might get
	 * an invalid state or handle here.
	 */
	if( status != IB_SUCCESS && status != IB_INVALID_STATE &&
		status != IB_INVALID_HANDLE )
	{
		IBSP_ERROR( ("ib_cm_dreq returned %s\n", ib_get_err_str( status )) );
	}

	/*
	 * Note that we don't care about getting the DREP - we move the QP to the
	 * error state now and flush all posted work requests.  If the
	 * disconnection was graceful, we'll only have the pre-posted receives to
	 * flush.  If the disconnection is ungraceful, we don't care if we
	 * interrupt transfers.
	 */

	/* Move the QP to error to flush any work requests. */
	__flush_qp( socket_info );

	IBSP_EXIT( IBSP_DBG_CM );
}
