/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corp.  All rights reserved.
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


#include "srp_data_path.h"
#include "srp_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srp_connection.tmh"
#endif
#include "srp_event.h"
#include "srp_hca.h"
#include "srp_session.h"

#include "srp.h"
#include "srp_login_req.h"
#include "srp_login_rsp.h"
#include "srp_login_rej.h"

#include "srp_connection.h"

#include <complib/cl_math.h>

#if DBG

extern void* gp_session[SRP_MAX_SERVICE_ENTRIES];

#endif

/* __srp_create_cqs */
/*!
Creates the send/recv completion queues to be used by this connection

@param p_srp_connection - pointer to the connection structure
@param p_hca            - pointer to the hca structure used by the connection
@param p_session        - context passed to callback functions

@return - result of cq creation
*/
static
ib_api_status_t
__srp_create_cqs(
	IN OUT  srp_connection_t    *p_srp_connection,
	IN      srp_hca_t           *p_hca,
	IN      p_srp_session_t     p_session )
{
	ib_api_status_t	status;
	ib_cq_create_t	cq_create;
	ib_al_ifc_t		*p_ifc;

	SRP_ENTER( SRP_DBG_PNP );

	p_ifc = &p_hca->p_hba->ifc;

	// Create Send CQ
	cq_create.size = SRP_DEFAULT_SEND_Q_DEPTH;
	cq_create.pfn_comp_cb = srp_send_completion_cb;
	cq_create.h_wait_obj = NULL;

	status = p_ifc->create_cq( p_hca->h_ca,
						   &cq_create,
						   p_session,
						   srp_async_event_handler_cb,
						   &p_srp_connection->h_send_cq );
	if( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Create Send Completion Queue. Status = %d\n", status) );
		goto exit;
	}

	// Create Receive CQ
	cq_create.size = SRP_DEFAULT_RECV_Q_DEPTH;
	cq_create.pfn_comp_cb = srp_recv_completion_cb;
	cq_create.h_wait_obj = NULL;

	status = p_ifc->create_cq( p_hca->h_ca,
						   &cq_create,
						   p_session,
						   srp_async_event_handler_cb,
						   &p_srp_connection->h_recv_cq );
	if( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Create Receive Completion Queue. Status = %d\n", status) );
	}

exit:
	SRP_EXIT( SRP_DBG_PNP );

	return ( status );
}

/* __srp_create_qp */
/*!
Creates the queue pair to be used by this connection

@param p_srp_connection - pointer to the connection structure
@param p_hca            - pointer to the hca structure used by the connection
@param p_session        - context passed to callback functions

@return - result of qp creation
*/
static
ib_api_status_t
__srp_create_qp(
	IN OUT  srp_connection_t    *p_srp_connection,
	IN      srp_hca_t           *p_hca,
	IN      p_srp_session_t     p_session )
{
	ib_api_status_t	status;
	ib_qp_create_t	qp_create;
	ib_al_ifc_t		*p_ifc;

	SRP_ENTER( SRP_DBG_PNP );

	p_ifc = &p_hca->p_hba->ifc;

	// Create QP
	cl_memclr( &qp_create, sizeof(qp_create) );
	qp_create.qp_type = IB_QPT_RELIABLE_CONN;
	qp_create.sq_depth = SRP_DEFAULT_SEND_Q_DEPTH;
	qp_create.rq_depth = SRP_DEFAULT_RECV_Q_DEPTH;
	qp_create.sq_sge = 1;
	qp_create.rq_sge = 1;
	qp_create.h_sq_cq = p_srp_connection->h_send_cq;
	qp_create.h_rq_cq = p_srp_connection->h_recv_cq;
	qp_create.sq_signaled = FALSE;//TRUE;

	status = p_ifc->create_qp( p_hca->h_pd,
						   &qp_create,
						   p_session,
						   srp_async_event_handler_cb,
						   &p_srp_connection->h_qp );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Create Queue Pair. Status = %d\n", status) );
	}

	SRP_EXIT( SRP_DBG_PNP );

	return ( status );
}

static
cl_status_t
__srp_create_wc_free_list(
	IN OUT  srp_connection_t    *p_connection,
	IN      uint32_t            completion_count )
{
	cl_status_t status = CL_SUCCESS;
	ib_wc_t     *p_wc;
	uint32_t    i;

	SRP_ENTER( SRP_DBG_PNP );

	p_connection->p_wc_array = cl_zalloc( sizeof( ib_wc_t ) * completion_count );
	if ( p_connection->p_wc_array == NULL )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Failed to allocate %d work completions.\n",  completion_count) );
		status = CL_INSUFFICIENT_MEMORY;
		goto exit;
	}

	p_wc = p_connection->p_wc_array;

	for ( i = 1; i < completion_count; i++, p_wc++ )
	{
		p_wc->p_next = (p_wc + 1);
	}

	p_connection->p_wc_free_list = p_connection->p_wc_array;

exit:
	SRP_EXIT( SRP_DBG_PNP );

	return ( status );
}


/* __srp_cm_request_cb */
/*!
Callback for a connect request - not used by SRP - We initiate connections

@param p_cm_request - pointer to the connect request structure

@return - none
*/
static
void
__srp_cm_request_cb(
	IN  ib_cm_req_rec_t     *p_cm_request)
{
	SRP_ENTER( SRP_DBG_PNP );

	UNUSED_PARAM ( p_cm_request );

	SRP_EXIT( SRP_DBG_PNP );
}

/* __srp_cm_apr_cb */
/*!
Callback for alternate path response - not used by SRP

@param p_cm_apr_rec - pointer to the alternate path response structure

@return - none
*/
static
void
__srp_cm_apr_cb(
	IN  ib_cm_apr_rec_t *p_cm_apr_rec )
{
	SRP_ENTER( SRP_DBG_PNP );

	UNUSED_PARAM( p_cm_apr_rec );

	SRP_EXIT( SRP_DBG_PNP );
}

/* __srp_cm_mra_cb */
/*!
Callback for message received acknowledgement - ignored by SRP - wait for connect reply

@param p_cm_mra_rec - pointer to the message received acknowledgement structure

@return - none
*/
static
void
__srp_cm_mra_cb(
	IN  ib_cm_mra_rec_t     *p_cm_mra_rec)
{
	SRP_ENTER( SRP_DBG_PNP );

	UNUSED_PARAM ( p_cm_mra_rec );

	SRP_EXIT( SRP_DBG_PNP );
}

/* __srp_cm_dreq_cb */
/*!
Callback for disconnect request from the target
Initiates the disconnect for the session

TODO:

@param p_cm_dreq_rec - pointer to the disconnect request structure

@return - none
*/
static
void
__srp_cm_dreq_cb(
	IN  ib_cm_dreq_rec_t    *p_cm_dreq_rec )
{
	srp_session_t   *p_srp_session = (srp_session_t*)p_cm_dreq_rec->qp_context;
	srp_hba_t       *p_hba = p_srp_session->p_hba;

	SRP_ENTER( SRP_DBG_PNP );

	cl_obj_lock( &p_srp_session->obj );

	if (p_srp_session->connection.state == SRP_CONNECTED)
	{
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
			("**** SRP_CONNECTED => SRP_CONNECT_FAILURE. \n") );
		p_srp_session->connection.state = SRP_CONNECT_FAILURE;
		cl_obj_unlock( &p_srp_session->obj );
	}
	else  // since the connection is no longer there, just exit
	{
		cl_obj_unlock( &p_srp_session->obj );
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
			("**** NOT SRP_CONNECTED *****. connection state = %d\n", p_srp_session->connection.state) );
		SRP_EXIT( SRP_DBG_PNP );
		return;	
	}

	SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_PNP,
		("Target has issued a disconnect request for Session %d ref_cnt = %d.\n",
						p_srp_session->target_id,
						p_srp_session->obj.ref_cnt) );

	if( !p_hba->adapter_stopped )
	{
		srp_session_failed( p_srp_session );
	}

	SRP_EXIT( SRP_DBG_PNP );
}

/* __srp_cm_reply_cb */
/*!
Callback for connect reply from the target
The target has accepted our connect/login request

@param p_cm_reply - pointer to the connect reply structure

@return - none
*/
static
void
__srp_cm_reply_cb(
	IN  ib_cm_rep_rec_t     *p_cm_reply)
{
	srp_session_t		*p_srp_session = (srp_session_t*)p_cm_reply->qp_context;
	srp_connection_t	*p_connection;
	srp_login_rsp_t		*p_srp_login_rsp = (srp_login_rsp_t*)p_cm_reply->p_rep_pdata;
	ib_api_status_t		status;
	union
	{
		ib_cm_mra_t		cm_mra;
		ib_cm_rtu_t		cm_rtu;
		ib_cm_rej_t		cm_rej;

	}	u;
	cl_status_t			cl_status;
	ib_al_ifc_t			*p_ifc;

	SRP_ENTER( SRP_DBG_PNP );

	p_ifc = &p_srp_session->p_hba->ifc;
	p_connection = &p_srp_session->connection;

	set_srp_login_response_from_network_to_host( p_srp_login_rsp );
	p_connection->descriptor_format = get_srp_login_response_supported_data_buffer_formats( p_srp_login_rsp );

	p_connection->request_limit      =
		MIN( get_srp_login_response_request_limit_delta( p_srp_login_rsp ), SRP_DEFAULT_RECV_Q_DEPTH );
	
	if( ib_ioc_profile_get_vend_id( &p_srp_session->p_hba->ioc_info.profile) == 0x00066a &&
		cl_ntoh32( p_srp_session->p_hba->ioc_info.profile.subsys_id ) == 0x38 )
	{
		/* workaround for FVIC */
		p_connection->request_limit /= 2;
	}

	p_connection->max_limit = p_connection->request_limit;
	p_connection->request_threashold = 2;
#if DBG
	p_srp_session->x_req_limit = p_connection->request_limit;
#endif

	p_connection->send_queue_depth   = p_connection->request_limit;
	p_connection->recv_queue_depth   = p_connection->request_limit;
	p_connection->init_to_targ_iu_sz = get_srp_login_response_max_init_to_targ_iu( p_srp_login_rsp );
	p_connection->targ_to_init_iu_sz = get_srp_login_response_max_targ_to_init_iu( p_srp_login_rsp );

	p_connection->signaled_send_completion_count = 32;
	
	if (( p_connection->descriptor_format & DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS ) == DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS )
	{
		p_connection->max_scatter_gather_entries =
				( MIN( SRP_MAX_IU_SIZE, p_connection->init_to_targ_iu_sz ) - offsetof( srp_cmd_t, additional_cdb )- sizeof(srp_memory_table_descriptor_t)) / sizeof( srp_memory_descriptor_t );
	}
	else if (( p_connection->descriptor_format & DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR ) == DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR )
	{
		p_connection->max_scatter_gather_entries =
				((p_connection->init_to_targ_iu_sz - offsetof( srp_cmd_t, additional_cdb )) / sizeof( srp_memory_descriptor_t )) ? 1 : 0;
	}
	else /*	not reported any descriptor format */
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("Target does not support valid descriptor formats\n") );
		goto rej;
	}
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("Request Limit = %d, SendQ Depth = %d, RecvQDepth = %d, "
			"ItoT size = %d, TtoI size = %d, Max S/G = %d\n",
			p_connection->request_limit,
			p_connection->send_queue_depth,
			p_connection->recv_queue_depth,
			p_connection->init_to_targ_iu_sz,
			p_connection->targ_to_init_iu_sz,
			p_connection->max_scatter_gather_entries) );

	/* will be used in srp_find_adapter to calculate NumberOfPhysicalBreaks */
	p_srp_session->p_hba->max_sg = p_connection->max_scatter_gather_entries;

	u.cm_mra.svc_timeout = 0x08;
	u.cm_mra.p_mra_pdata = NULL;
	u.cm_mra.mra_length = 0;

	status = p_ifc->cm_mra( p_cm_reply->h_cm_rep, &u.cm_mra );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Send MRA. Status = %d\n", status) );
		goto rej;
	}

	status = p_ifc->modify_cq( p_connection->h_send_cq, &p_connection->send_queue_depth );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_PNP,
			("Cannot Modify Send Completion Queue Depth. Status = %d\n", status) );
	}

	status = p_ifc->modify_cq( p_connection->h_recv_cq, &p_connection->recv_queue_depth );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_PNP,
			("Cannot Modify Recv Completion Queue Depth. Status = %d\n", status) );
	}

	cl_status = __srp_create_wc_free_list( p_connection, (p_connection->request_limit * 2) );/* Send/Recv */
	if ( cl_status != CL_SUCCESS )
	{
		goto rej;
	}

	status = srp_init_descriptors( &p_srp_session->descriptors,
								   p_connection->request_limit,
								   p_connection->targ_to_init_iu_sz,
								   &p_srp_session->p_hba->ifc,
								   p_srp_session->hca.h_pd,
								   p_connection->h_qp,
								   p_srp_session->hca.lkey );
	if ( status != IB_SUCCESS )
	{
		goto err_init_desc;
	}

	u.cm_rtu.access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_RDMA_READ | IB_AC_RDMA_WRITE;

	/* Have to set to 0 to indicate not to modify because Tavor doesn't support this */
	u.cm_rtu.sq_depth = 0 /*p_connection->request_limit*/;
	u.cm_rtu.rq_depth = 0 /*p_connection->request_limit*/;

	u.cm_rtu.p_rtu_pdata = NULL;
	u.cm_rtu.rtu_length = 0;
	u.cm_rtu.pfn_cm_apr_cb = __srp_cm_apr_cb;
	u.cm_rtu.pfn_cm_dreq_cb = __srp_cm_dreq_cb;

	status = p_ifc->cm_rtu( p_cm_reply->h_cm_rep, &u.cm_rtu );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Send RTU. Status = %d\n", status) );
		goto err_send_rtu;
	}

	p_connection->state = SRP_CONNECTED;

	status = p_ifc->rearm_cq( p_connection->h_send_cq, FALSE );
	if ( status != IB_SUCCESS)
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("ib_rearm_cq() for send cq failed!, status 0x%x", status) );

		// TODO: Kill session and inform port driver link down storportnotification
		goto err_send_rtu;
	}

	status = p_ifc->rearm_cq( p_connection->h_recv_cq, FALSE );
	if ( status != IB_SUCCESS)
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("ib_rearm_cq() for recv failed!, status 0x%x", status) );

		// TODO: Kill session and inform port driver link down storportnotification
		goto err_send_rtu;
	}
	goto exit;

err_send_rtu:	
	// the rest will be cleaned up in srp_session_login

err_init_desc:
	cl_free( p_connection->p_wc_array );
	p_connection->p_wc_array = NULL;
	p_connection->p_wc_free_list = NULL;

rej:
	p_connection->state = SRP_CONNECT_FAILURE;
	cl_memclr( &u.cm_rej, sizeof(u.cm_rej) );
	u.cm_rej.rej_status = IB_REJ_INSUF_RESOURCES;
	p_ifc->cm_rej( p_cm_reply->h_cm_rep, &u.cm_rej );

exit:
	cl_status = cl_event_signal( &p_connection->conn_req_event );

	SRP_EXIT( SRP_DBG_PNP );
}


/* __srp_cm_rej_cb */
/*!
Callback for connect reject from the target
The target has rejected our connect/login request

@param p_cm_reject - pointer to the connect reject structure

@return - none
*/
static
void
__srp_cm_rej_cb(
	IN  ib_cm_rej_rec_t     *p_cm_reject)
{
	srp_session_t       *p_srp_session = (srp_session_t*)p_cm_reject->qp_context;
	srp_connection_t    *p_connection;
	srp_login_rej_t     *p_srp_login_rej = (srp_login_rej_t*)p_cm_reject->p_rej_pdata;
	cl_status_t         cl_status;

	SRP_ENTER( SRP_DBG_PNP );

	p_connection = &p_srp_session->connection;

	if( p_srp_login_rej )
	{
		set_srp_login_reject_from_network_to_host( p_srp_login_rej ); // <-- Is this coming back NULL?
		p_connection->reject_reason = get_srp_login_reject_reason( p_srp_login_rej );

		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Login Rejected. IBT Code = 0x%x, SRP Code = 0x%x\n",
			p_cm_reject->rej_status, p_connection->reject_reason ) );
		switch( p_connection->reject_reason )
		{
			case LIREJ_INIT_TO_TARG_IU_LENGTH_TOO_LARGE:
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("REQUESTED IU_SIZE %d\n",
					 p_connection->req_max_iu_msg_size ));
				break;
			case LIREJ_UNSUPPORTED_DATA_BUFFER_DESCRIPTOR_FORMAT:
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("REQUESTED DESC FORMAT: %#x, SUPPORTED FORMAT %#x\n",
					p_connection->descriptor_format, 
					get_srp_login_reject_supported_data_buffer_formats(p_srp_login_rej) ));
					__srp_issue_session_login( p_connection, (srp_hca_t *)&p_srp_session->hca, p_connection->ioc_max_send_msg_depth );
					return;
			default:
				break;
		}
	}
	else
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Login Rejected. IBT Code = 0x%x\n",
			p_cm_reject->rej_status) );
}
	p_connection->state = SRP_CONNECT_FAILURE;

	cl_status = cl_event_signal( &p_connection->conn_req_event );

	SRP_EXIT( SRP_DBG_PNP );
}

/* __srp_issue_session_login */
/*!
Initializes and issues a login/cm connect request to the target

@param p_connection   - pointer to the connection structure
@param p_hca          - pointer to the hca structure used by this connection
@param send_msg_depth - initial request limit delta value

@return - result of login/cm connect request operations
*/
#pragma warning( disable : 4748)
#pragma optimize( "", off )
static
ib_api_status_t
__srp_issue_session_login(
	IN OUT  srp_connection_t    *p_connection,
	IN      srp_hca_t           *p_hca,
	IN      uint8_t             send_msg_depth )
{
	ib_api_status_t status;
	ib_cm_req_t     cm_req;
	srp_login_req_t login_req;

	SRP_ENTER( SRP_DBG_PNP );

	cl_memclr( &cm_req, sizeof(ib_cm_req_t) );

	cm_req.svc_id = p_connection->service_id;

	cm_req.flags = 0; // event used instead of IB_FLAGS_SYNC
	cm_req.max_cm_retries = 8;
	cm_req.p_primary_path = p_connection->p_path_rec;
	
	/*already tried to login before and failed ? */
	if ( !p_connection->reject_reason )
	{
		p_connection->descriptor_format = DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR | DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS;
	}
	else if ( p_connection->reject_reason == LIREJ_UNSUPPORTED_DATA_BUFFER_DESCRIPTOR_FORMAT )
	{
		p_connection->descriptor_format = DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR;
	}
	else
	{
		p_connection->state = SRP_CONNECT_FAILURE;
		status = IB_ERROR;
		goto exit;
	}
	p_connection->req_max_iu_msg_size = ( p_connection->ioc_max_send_msg_size >= SRP_MAX_IU_SIZE ) ? SRP_MAX_IU_SIZE: p_connection->ioc_max_send_msg_size;
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_ERROR, 
		( "(init_to_targ_iu_sz requested)  req_max_iu_msg_size %d, (from profile) ioc_max_send_msg_size %d\n", 
		p_connection->req_max_iu_msg_size, p_connection->ioc_max_send_msg_size ));
	/*
	   Build SRP Login request
	 */
	setup_srp_login_request( &login_req,
							 0, /* tag */
							 p_connection->req_max_iu_msg_size,
							 p_connection->descriptor_format,
							 MCA_TERMINATE_EXISTING,
							 &p_connection->init_port_id,
							 &p_connection->targ_port_id );
	set_srp_login_request_from_host_to_network(&login_req);

	cm_req.p_req_pdata = (const uint8_t *)&login_req;
	cm_req.req_length = (uint8_t)get_srp_login_request_length( &login_req );

	cm_req.qp_type = IB_QPT_RELIABLE_CONN;
	cm_req.h_qp = p_connection->h_qp;

	/* The maximum number of outstanding RDMA read/atomic operations. */
	status = srp_get_responder_resources( p_hca, &cm_req.resp_res );
	if ( status != IB_SUCCESS )
	{
		goto exit;
	}

	cm_req.init_depth = send_msg_depth;

	cm_req.remote_resp_timeout = ib_path_rec_pkt_life( p_connection->p_path_rec ) + 1;
	cm_req.flow_ctrl = FALSE;
	cm_req.local_resp_timeout = ib_path_rec_pkt_life( p_connection->p_path_rec ) + 1;
	cm_req.retry_cnt = 1;
	cm_req.rnr_nak_timeout = 0; /* 655.36 ms */
	cm_req.rnr_retry_cnt = 6;

	cm_req.pfn_cm_rep_cb = __srp_cm_reply_cb;
	cm_req.pfn_cm_req_cb = NULL; /* Set only for P2P */
	cm_req.pfn_cm_mra_cb = __srp_cm_mra_cb;
	cm_req.pfn_cm_rej_cb = __srp_cm_rej_cb;

	cm_req.pkey = p_connection->p_path_rec->pkey;

	status = p_hca->p_hba->ifc.cm_req( &cm_req );
	if ( status == IB_SUCCESS )
	{
		p_connection->state = SRP_CONNECT_REQUESTED;
	}
	else
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Send Connect Request. Status = %d\n", status) );
		p_connection->state = SRP_CONNECT_FAILURE;
	}

exit:
	SRP_EXIT( SRP_DBG_PNP );

	return ( status );
}
#pragma optimize( "", on )
#pragma warning( default : 4748)

/* srp_init_connection */
/*!
Initializes a connection structure

@param p_connection   - pointer to the connection structure
@param p_profile      - Pointer to IOC profile.
@param ca_guid        - Local CA GUID to use in as initiator GUID.
@param ext_id         - Initiator and target extension ID.
@param p_path_rec     - pointer to the path to the target
@param service_id     - service id to which we want to connect

@return - always success (for now)
*/
ib_api_status_t
srp_init_connection(
	IN	OUT	srp_connection_t		*p_connection,
	IN		ib_ioc_profile_t* const	p_profile,
	IN		net64_t					ca_guid,
	IN		net64_t					ext_id,
	IN      ib_path_rec_t           *p_path_rec,
	IN		ib_net64_t				service_id )
{
	SRP_ENTER( SRP_DBG_PNP );

	cl_memclr( p_connection, sizeof(*p_connection) );\

	p_connection->initialized = TRUE;

	p_connection->state = SRP_NOT_CONNECTED;

	p_connection->p_path_rec = p_path_rec;
	switch( p_profile->io_class )
	{
	case SRP_IO_CLASS_R10:
		p_connection->init_port_id.field1 = ca_guid;
		p_connection->init_port_id.field2 = ext_id;
		p_connection->targ_port_id.field1 = p_profile->ioc_guid;
		p_connection->targ_port_id.field2 = ext_id;
		break;

	case SRP_IO_CLASS:
		p_connection->init_port_id.field1 = ext_id;
		p_connection->init_port_id.field2 = ca_guid;
		p_connection->targ_port_id.field1 = ext_id;
		p_connection->targ_port_id.field2 = p_profile->ioc_guid;
		break;

	default:
		return IB_INVALID_PARAMETER;
	}
	p_connection->service_id = service_id;
	p_connection->send_queue_depth = SRP_DEFAULT_SEND_Q_DEPTH;
	p_connection->recv_queue_depth = SRP_DEFAULT_RECV_Q_DEPTH;

	SRP_EXIT( SRP_DBG_PNP );

	return ( IB_SUCCESS );
}

/* srp_connect */
/*!
Orchestrates the processing required to connect to a target device

@param p_connection        - pointer to the connection structure
@param p_hca               - pointer to the hca structure used by this connection
@param send_msg_depth      - initial request limit delta value
@param p_session           - context passed to callback functions

@return -  result of connect operations
*/
ib_api_status_t
srp_connect(
	IN OUT  srp_connection_t    *p_connection,
	IN      srp_hca_t           *p_hca,
	IN      uint8_t             send_msg_depth,
	IN      p_srp_session_t     p_session )
{
	ib_api_status_t status;
	cl_status_t     cl_status;

	SRP_ENTER( SRP_DBG_PNP );

	p_connection->ioc_max_send_msg_size =
		cl_ntoh32 (p_session->p_hba->ioc_info.profile.send_msg_size);
	p_connection->ioc_max_send_msg_depth = send_msg_depth;
	p_connection->reject_reason = 0;

	status = __srp_create_cqs( p_connection, p_hca, p_session );
	if ( status != IB_SUCCESS )
	{
		goto exit;
	}

	status = __srp_create_qp( p_connection, p_hca, p_session );
	if ( status != IB_SUCCESS )
	{
		goto exit;
	}

	cl_status = cl_event_init( &p_connection->conn_req_event, TRUE );
	if ( cl_status != CL_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Initialize Connect Request Event. Status = %d\n", cl_status) );
		status = cl_status;
		goto exit;
	}

	status = __srp_issue_session_login( p_connection, p_hca, send_msg_depth );
	if ( status != IB_SUCCESS )
	{
		cl_event_destroy( &p_connection->conn_req_event );
		goto exit;
	}

	cl_status = cl_event_wait_on( &p_connection->conn_req_event, EVENT_NO_TIMEOUT, FALSE );
	if ( cl_status != CL_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Wait On Connect Request Event Failed. Status = %d\n", cl_status) );
		status = cl_status;
		cl_event_destroy( &p_connection->conn_req_event );
		goto exit;
	}

	cl_event_destroy( &p_connection->conn_req_event );

	if ( p_connection->state != SRP_CONNECTED )
	{
		status = IB_ERROR;
		goto exit;
	}
	
	cl_thread_init( &p_session->recovery_thread, 
					(cl_pfn_thread_callback_t)srp_session_recovery_thread,
					(void *)p_session, "srp_thread" );
#if DBG
	gp_session[p_session->target_id] = p_session;
#endif

exit:
	SRP_EXIT( SRP_DBG_PNP );

	return ( status );
}

/* srp_free_connection */
/*!
Frees connection resources

@param p_connection  - pointer to the connection structure

@return -  none
*/
void
srp_free_connection(
	IN  srp_connection_t    *p_srp_connection )
{
	SRP_ENTER( SRP_DBG_PNP );

	if ( p_srp_connection->initialized == TRUE )
	{
		if ( p_srp_connection->p_wc_array != NULL )
		{
			cl_free( p_srp_connection->p_wc_array );
		}

		cl_event_destroy( &p_srp_connection->conn_req_event );

		cl_memclr( p_srp_connection, sizeof( *p_srp_connection ) );
	}

	SRP_EXIT( SRP_DBG_PNP );
}





