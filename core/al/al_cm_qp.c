/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#include <iba/ib_al.h>
#include "al.h"
#include "al_qp.h"
#include "al_cm_cep.h"
#include "al_cm_conn.h"
#include "al_cm_sidr.h"
#include "al_mgr.h"
#include "al_debug.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_cm_qp.tmh"
#endif

typedef struct _al_listen
{
	al_obj_t					obj;
	net32_t						cid;

	ib_pfn_cm_req_cb_t			pfn_cm_req_cb;

	/* valid for ud qp_type only */
	const void*					sidr_context;

}	al_listen_t;


#ifdef CL_KERNEL

/*
 * Structure for queuing received MADs to the asynchronous processing
 * manager.
 */
typedef struct _cep_async_mad
{
	cl_async_proc_item_t	item;
	ib_al_handle_t			h_al;
	net32_t					cid;

}	cep_async_mad_t;

#endif /* CL_KERNEL */


/*
 * Transition the QP to the error state to flush all oustanding work
 * requests and sets the timewait time.  This function may be called
 * when destroying the QP in order to flush all work requests, so we
 * cannot call through the main API, or the call will fail since the
 * QP is no longer in the initialize state.
 */
static void
__cep_timewait_qp(
	IN		const	ib_qp_handle_t				h_qp )
{
	uint64_t			timewait = 0;
	ib_qp_mod_t			qp_mod;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( h_qp );

	/*
	 * The CM should have set the proper timewait time-out value.  Reset
	 * the QP and let it enter the timewait state.
	 */
	if( al_cep_get_timewait( h_qp->obj.h_al,
		((al_conn_qp_t*)h_qp)->cid, &timewait ) == IB_SUCCESS )
	{
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
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("pfn_modify_qp to IB_QPS_ERROR returned %s\n",
				ib_get_err_str( status )) );
			return;
		}

#ifdef CL_KERNEL
		/* Store the timestamp after which the QP exits timewait. */
		h_qp->timewait = cl_get_time_stamp() + timewait;
#endif	/* CL_KERNEL */
	}

	AL_EXIT( AL_DBG_CM );
}


static void
__format_req_path_rec(
	IN		const	mad_cm_req_t* const			p_req,
	IN		const	req_path_info_t* const		p_path,
		OUT			ib_path_rec_t* const		p_path_rec )
{
	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_req );
	CL_ASSERT( p_path );
	CL_ASSERT( p_path_rec );

	/*
	 * Format a local path record. The local ack timeout specified in the
	 * REQ is twice the packet life plus the sender's CA ACK delay.  When
	 * reporting the packet life, we divide the local ack timeout by 2 to
	 * approach the path's packet lifetime.  Since local ack timeout is
	 * expressed as 4.096 * 2^x, subtracting 1 is equivalent to dividing the
	 * time in half.
	 */
	ib_path_rec_init_local( p_path_rec,
		&p_path->local_gid,
		&p_path->remote_gid,
		p_path->local_lid,
		p_path->remote_lid,
		1, p_req->pkey,
		conn_req_path_get_svc_lvl( p_path ), 0,
		IB_PATH_SELECTOR_EXACTLY, conn_req_get_mtu( p_req ),
		IB_PATH_SELECTOR_EXACTLY,
		conn_req_path_get_pkt_rate( p_path ),
		IB_PATH_SELECTOR_EXACTLY,
		(uint8_t)( conn_req_path_get_lcl_ack_timeout( p_path ) - 1 ),
		0 );

	/* Add global routing info as necessary. */
	if( !conn_req_path_get_subn_lcl( p_path ) )
	{
		ib_path_rec_set_hop_flow_raw( p_path_rec, p_path->hop_limit,
			conn_req_path_get_flow_lbl( p_path ), FALSE );
		p_path_rec->tclass = p_path->traffic_class;
	}

	AL_EXIT( AL_DBG_CM );
}


static void
__format_req_rec(
	IN		const	mad_cm_req_t* const			p_req,
		OUT			ib_cm_req_rec_t				*p_req_rec )
{
	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_req );
	CL_ASSERT( p_req_rec );

	cl_memclr( p_req_rec, sizeof(ib_cm_req_rec_t) );

	/* format version specific data */
	p_req_rec->p_req_pdata = p_req->pdata;

	p_req_rec->qp_type = conn_req_get_qp_type( p_req );

	p_req_rec->resp_res = conn_req_get_resp_res( p_req );
	p_req_rec->flow_ctrl = conn_req_get_flow_ctrl( p_req );
	p_req_rec->rnr_retry_cnt = conn_req_get_rnr_retry_cnt( p_req );

	__format_req_path_rec( p_req, &p_req->primary_path,
		&p_req_rec->primary_path );
	__format_req_path_rec( p_req, &p_req->alternate_path,
		&p_req_rec->alt_path );

	/* These values are filled in later based on listen or peer connections
	p_req_rec->context = ;
	p_req_rec->h_cm_req = ;
	p_req_rec->h_cm_listen = ;
	*/

	AL_EXIT( AL_DBG_CM );
}


/******************************************************************************
* Functions that handle incoming REQs that matched to an outstanding listen.
*
*/


static void
__listen_req(
	IN				al_listen_t* const			p_listen,
	IN		const	net32_t						new_cid,
	IN		const	mad_cm_req_t* const			p_req )
{
	ib_cm_req_rec_t		req_rec;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_listen );
	CL_ASSERT( new_cid != AL_INVALID_CID );
	CL_ASSERT( p_req );

	/* Format the callback record. */
	__format_req_rec( p_req, &req_rec );

	/* update listen based rec */
	req_rec.context = p_listen->obj.context;

	req_rec.h_cm_req.cid = new_cid;
	req_rec.h_cm_req.h_al = p_listen->obj.h_al;
	req_rec.h_cm_req.h_qp = NULL;

	req_rec.h_cm_listen = p_listen;

	/* Invoke the user's callback. */
	p_listen->pfn_cm_req_cb( &req_rec );

	AL_EXIT( AL_DBG_CM );
}


static void
__proc_listen(
	IN				al_listen_t* const			p_listen,
	IN				net32_t						new_cid,
	IN		const	ib_mad_t* const				p_mad )
{
	AL_ENTER( AL_DBG_CM );

	/* Context is a listen - MAD must be a REQ or SIDR REQ */
	switch( p_mad->attr_id )
	{
	case CM_REQ_ATTR_ID:
		__listen_req(
			p_listen, new_cid, (mad_cm_req_t*)p_mad );
		break;

	case CM_SIDR_REQ_ATTR_ID:
		/* TODO - implement SIDR. */
	default:
		CL_ASSERT( p_mad->attr_id == CM_REQ_ATTR_ID ||
			p_mad->attr_id == CM_SIDR_REQ_ATTR_ID );
		/* Destroy the new CEP as it won't ever be reported to the user. */
		al_destroy_cep( p_listen->obj.h_al, &new_cid, FALSE );
	}

	AL_EXIT( AL_DBG_CM );
}


/******************************************************************************
* Functions that handle send timeouts:
*
*/

/*
 * callback to process a connection establishment timeout due to reply not
 * being received.  The connection object has a reference
 * taken when the timer is set or when the send is sent.
 */
static void
__proc_conn_timeout(
	IN		const	ib_qp_handle_t				h_qp )
{
	ib_cm_rej_rec_t		rej_rec;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( h_qp );

	/*
	 * Format the reject record before aborting the connection since
	 * we need the QP context.
	 */
	cl_memclr( &rej_rec, sizeof(ib_cm_rej_rec_t) );
	rej_rec.h_qp = h_qp;
	rej_rec.qp_context = h_qp->obj.context;
	rej_rec.rej_status = IB_REJ_TIMEOUT;

	/* Unbind the QP from the CEP. */
	__cep_timewait_qp( h_qp );

	al_destroy_cep( h_qp->obj.h_al, &((al_conn_qp_t*)h_qp)->cid, TRUE );

	/* Invoke the callback. */
	((al_conn_qp_t*)h_qp)->pfn_cm_rej_cb( &rej_rec );

	AL_EXIT( AL_DBG_CM );
}


/*
 * callback to process a LAP timeout due to APR not being received.
 */
static void
__proc_lap_timeout(
	IN		const	ib_qp_handle_t				h_qp )
{
	ib_cm_apr_rec_t		apr_rec;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( h_qp );

	/* Report the timeout. */
	cl_memclr( &apr_rec, sizeof(ib_cm_apr_rec_t) );
	apr_rec.h_qp = h_qp;
	apr_rec.qp_context = h_qp->obj.context;
	apr_rec.cm_status = IB_TIMEOUT;
	apr_rec.apr_status = IB_AP_REJECT;

	/* Notify the user that the LAP failed. */
	((al_conn_qp_t*)h_qp)->pfn_cm_apr_cb( &apr_rec );

	AL_EXIT( AL_DBG_CM );
}


/*
 * Callback to process a disconnection timeout due to not receiving the DREP
 * within allowable time.
 */
static void
__proc_dconn_timeout(
	IN		const	ib_qp_handle_t				h_qp )
{
	ib_cm_drep_rec_t	drep_rec;

	AL_ENTER( AL_DBG_CM );

	/* No response.  We're done.  Deliver a DREP callback. */
	cl_memclr( &drep_rec, sizeof(ib_cm_drep_rec_t) );
	drep_rec.h_qp = h_qp;
	drep_rec.qp_context = h_qp->obj.context;
	drep_rec.cm_status = IB_TIMEOUT;

	__cep_timewait_qp( h_qp );

	al_destroy_cep( h_qp->obj.h_al, &((al_conn_qp_t*)h_qp)->cid, TRUE );

	/* Call the user back. */
	((al_conn_qp_t*)h_qp)->pfn_cm_drep_cb( &drep_rec );

	AL_EXIT( AL_DBG_CM );
}


static void
__proc_failed_send(
	IN				ib_qp_handle_t				h_qp,
	IN		const	ib_mad_t* const				p_mad )
{
	AL_ENTER( AL_DBG_CM );

	/* Failure indicates a send. */
	switch( p_mad->attr_id )
	{
	case CM_REQ_ATTR_ID:
	case CM_REP_ATTR_ID:
		__proc_conn_timeout( h_qp );
		break;
	case CM_LAP_ATTR_ID:
		__proc_lap_timeout( h_qp );
		break;
	case CM_DREQ_ATTR_ID:
		__proc_dconn_timeout( h_qp );
		break;
	default:
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid CM send MAD attribute ID %d.\n", p_mad->attr_id) );
		break;
	}

	AL_EXIT( AL_DBG_CM );
}


/******************************************************************************
* Functions that handle received MADs on a connection (not listen)
*
*/


void
__proc_peer_req(
	IN		const	ib_cm_handle_t* const		p_cm,
	IN		const	mad_cm_req_t* const			p_req )
{
	ib_cm_req_rec_t	req_rec;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cm );
	CL_ASSERT( p_cm->h_qp );
	/* Must be peer-to-peer. */
	CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_req_cb );
	CL_ASSERT( p_req );

	/* Format the callback record. */
	__format_req_rec( p_req, &req_rec );

	/* update peer based rec handles and context values */
	req_rec.context = p_cm->h_qp->obj.context;
	req_rec.h_cm_req = *p_cm;
	req_rec.h_cm_listen = NULL;

	/* Invoke the user's callback.  User must call ib_cm_rep or ib_cm_rej. */
	((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_req_cb( &req_rec );

	AL_EXIT( AL_DBG_CM );
}


void
__proc_mra(
	IN		const	ib_cm_handle_t* const		p_cm,
	IN		const	mad_cm_mra_t* const			p_mra )
{
	ib_cm_mra_rec_t	mra_rec;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cm->h_qp );
	CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_mra_cb );

	/* Format the MRA callback record. */
	cl_memclr( &mra_rec, sizeof(ib_cm_mra_rec_t) );

	mra_rec.h_qp = p_cm->h_qp;
	mra_rec.qp_context = p_cm->h_qp->obj.context;
	mra_rec.p_mra_pdata = p_mra->pdata;

	/*
	 * Call the user back. Note that users will get a callback only
	 * for the first MRA received in response to a REQ, REP, or LAP.
	 */
	((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_mra_cb( &mra_rec );

	AL_EXIT( AL_DBG_CM );
}


void
__proc_rej(
	IN		const	ib_cm_handle_t* const		p_cm,
	IN		const	mad_cm_rej_t* const			p_rej )
{
	ib_cm_rej_rec_t	rej_rec;

	AL_ENTER( AL_DBG_CM );

	if( p_cm->h_qp )
	{
		/* Format the REJ callback record. */
		cl_memclr( &rej_rec, sizeof(ib_cm_rej_rec_t) );

		rej_rec.h_qp = p_cm->h_qp;
		rej_rec.qp_context = p_cm->h_qp->obj.context;

		rej_rec.p_rej_pdata = p_rej->pdata;
		rej_rec.p_ari = p_rej->ari;
		rej_rec.ari_length = conn_rej_get_ari_len( p_rej );
		rej_rec.rej_status = p_rej->reason;

		/*
		 * Unbind the QP from the connection object.  This allows the QP to
		 * be immediately reused in another connection request.
		 */
		__cep_timewait_qp( p_cm->h_qp );

		al_destroy_cep( p_cm->h_al, &((al_conn_qp_t*)p_cm->h_qp)->cid, TRUE );

		/* Call the user back. */
		((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_rej_cb( &rej_rec );
	}

	AL_EXIT( AL_DBG_CM );
}


static void
__proc_rep(
	IN				ib_cm_handle_t* const		p_cm,
	IN				mad_cm_rep_t* const			p_rep )
{
	ib_cm_rep_rec_t		rep_rec;

	AL_ENTER( AL_DBG_CM );

	cl_memclr( &rep_rec, sizeof(ib_cm_rep_rec_t) );

	/* fill the rec callback data */
	rep_rec.p_rep_pdata = p_rep->pdata;
	rep_rec.qp_type = p_cm->h_qp->type;

	rep_rec.h_cm_rep = *p_cm;
	rep_rec.qp_context = p_cm->h_qp->obj.context;
	rep_rec.resp_res = p_rep->resp_resources;
	rep_rec.flow_ctrl = conn_rep_get_e2e_flow_ctl( p_rep );
	rep_rec.apr_status = conn_rep_get_failover( p_rep );

	/* Notify the user of the reply. */
	((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_rep_cb( &rep_rec );

	AL_EXIT( AL_DBG_CM );
}


static void
__proc_rtu(
	IN				ib_cm_handle_t* const		p_cm,
	IN				mad_cm_rtu_t* const			p_rtu )
{
	ib_cm_rtu_rec_t			rtu_rec;

	AL_ENTER( AL_DBG_CM );

	rtu_rec.p_rtu_pdata = p_rtu->pdata;
	rtu_rec.h_qp = p_cm->h_qp;
	rtu_rec.qp_context = p_cm->h_qp->obj.context;

	((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_rtu_cb( &rtu_rec );

	AL_EXIT( AL_DBG_CM );
}


static void
__proc_dreq(
	IN				ib_cm_handle_t* const		p_cm,
	IN				mad_cm_dreq_t* const		p_dreq )
{
	ib_cm_dreq_rec_t	dreq_rec;

	AL_ENTER( AL_DBG_CM );

	cl_memclr( &dreq_rec, sizeof(ib_cm_dreq_rec_t) );

	dreq_rec.h_cm_dreq = *p_cm;
	dreq_rec.p_dreq_pdata = p_dreq->pdata;

	dreq_rec.qp_context = p_cm->h_qp->obj.context;

	((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_dreq_cb( &dreq_rec );

	AL_EXIT( AL_DBG_CM );
}


void
__proc_drep(
	IN				ib_cm_handle_t* const		p_cm,
	IN				mad_cm_drep_t* const		p_drep )
{
	ib_cm_drep_rec_t	drep_rec;

	AL_ENTER( AL_DBG_CM );

	cl_memclr( &drep_rec, sizeof(ib_cm_drep_rec_t) );

	/* Copy qp context before the connection is released */
	drep_rec.cm_status = IB_SUCCESS;
	drep_rec.p_drep_pdata = p_drep->pdata;
	drep_rec.h_qp = p_cm->h_qp;
	drep_rec.qp_context = p_cm->h_qp->obj.context;

	__cep_timewait_qp( p_cm->h_qp );

	al_destroy_cep( p_cm->h_al, &((al_conn_qp_t*)p_cm->h_qp)->cid, TRUE );

	((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_drep_cb( &drep_rec );

	AL_EXIT( AL_DBG_CM );
}


void
__proc_lap(
	IN				ib_cm_handle_t* const		p_cm,
	IN		const	mad_cm_lap_t* const			p_lap )
{
	ib_cm_lap_rec_t	lap_rec;
	const lap_path_info_t* const	p_path = &p_lap->alternate_path;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cm );
	CL_ASSERT( p_cm->h_qp );
	CL_ASSERT( p_lap );

	cl_memclr( &lap_rec, sizeof(ib_cm_lap_rec_t) );
	lap_rec.qp_context = p_cm->h_qp->obj.context;
	lap_rec.h_cm_lap = *p_cm;

	/*
	 * Format the path record. The local ack timeout specified in the
	 * LAP is twice the packet life plus the sender's CA ACK delay.  When
	 * reporting the packet life, we divide the local ack timeout by 2 to
	 * approach the path's packet lifetime.  Since local ack timeout is
	 * expressed as 4.096 * 2^x, subtracting 1 is equivalent to dividing the
	 * time in half.
	 */
	ib_path_rec_init_local( &lap_rec.alt_path,
		&p_lap->alternate_path.local_gid,
		&p_lap->alternate_path.remote_gid,
		p_lap->alternate_path.local_lid,
		p_lap->alternate_path.remote_lid,
		1, IB_DEFAULT_PKEY,
		conn_lap_path_get_svc_lvl( &p_lap->alternate_path ), 0,
		IB_PATH_SELECTOR_EXACTLY,
		IB_MTU_LEN_2048,
		IB_PATH_SELECTOR_EXACTLY,
		conn_lap_path_get_pkt_rate( p_path ),
		IB_PATH_SELECTOR_EXACTLY,
		(uint8_t)( conn_lap_path_get_lcl_ack_timeout( p_path ) - 1 ),
		0 );

	/* Add global routing info as necessary. */
	if( !conn_lap_path_get_subn_lcl( &p_lap->alternate_path ) )
	{
		ib_path_rec_set_hop_flow_raw( &lap_rec.alt_path,
			p_lap->alternate_path.hop_limit,
			conn_lap_path_get_flow_lbl( &p_lap->alternate_path ),
			FALSE );
		lap_rec.alt_path.tclass =
			conn_lap_path_get_tclass( &p_lap->alternate_path );
	}

	((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_lap_cb( &lap_rec );

	AL_EXIT( AL_DBG_CM );
}


static ib_api_status_t
__cep_lap_qp(
	IN				ib_cm_handle_t* const		p_cm )
{
	ib_api_status_t		status;
	ib_qp_mod_t			qp_mod;

	AL_ENTER( AL_DBG_CM );

	status = al_cep_get_rts_attr( p_cm->h_al, p_cm->cid, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_get_rts_attr returned %s.\n", ib_get_err_str(status)) );
		goto done;
	}

	status = ib_modify_qp( p_cm->h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_modify_qp for LAP returned %s.\n", ib_get_err_str(status)) );
	}

done:
	AL_EXIT( AL_DBG_CM );
	return status;
}


static void
__proc_apr(
	IN				ib_cm_handle_t* const		p_cm,
	IN				mad_cm_apr_t* const			p_apr )
{
	ib_cm_apr_rec_t	apr_rec;

	AL_ENTER( AL_DBG_CM );

	apr_rec.h_qp = p_cm->h_qp;
	apr_rec.qp_context = p_cm->h_qp->obj.context;
	apr_rec.p_info = (const uint8_t*)&p_apr->info;
	apr_rec.info_length = p_apr->info_len;
	apr_rec.p_apr_pdata = p_apr->pdata;
	apr_rec.apr_status = p_apr->status;

	if( apr_rec.apr_status == IB_AP_SUCCESS )
	{
		apr_rec.cm_status = __cep_lap_qp( p_cm );
	}
	else
	{
		apr_rec.cm_status = IB_ERROR;
	}

	((al_conn_qp_t*)p_cm->h_qp)->pfn_cm_apr_cb( &apr_rec );

	AL_EXIT( AL_DBG_CM );
}


static void
__proc_conn(
	IN				ib_cm_handle_t* const		p_cm,
	IN				ib_mad_t* const				p_mad )
{
	AL_ENTER( AL_DBG_CM );

	/* Success indicates a receive. */
	switch( p_mad->attr_id )
	{
	case CM_REQ_ATTR_ID:
		CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->cid == (int32_t)p_cm->cid ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_RESERVED_CID ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_INVALID_CID );
		__proc_peer_req( p_cm, (mad_cm_req_t*)p_mad );
		break;

	case CM_MRA_ATTR_ID:
		CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->cid == (int32_t)p_cm->cid ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_RESERVED_CID ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_INVALID_CID );
		__proc_mra( p_cm, (mad_cm_mra_t*)p_mad );
		break;

	case CM_REJ_ATTR_ID:
		__proc_rej( p_cm, (mad_cm_rej_t*)p_mad );
		break;

	case CM_REP_ATTR_ID:
		CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->cid == (int32_t)p_cm->cid ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_RESERVED_CID ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_INVALID_CID );
		__proc_rep( p_cm, (mad_cm_rep_t*)p_mad );
		break;

	case CM_RTU_ATTR_ID:
		CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->cid == (int32_t)p_cm->cid ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_RESERVED_CID ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_INVALID_CID );
		__proc_rtu( p_cm, (mad_cm_rtu_t*)p_mad );
		break;

	case CM_DREQ_ATTR_ID:
		CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->cid == (int32_t)p_cm->cid ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_INVALID_CID );
		__proc_dreq( p_cm, (mad_cm_dreq_t*)p_mad );
		break;

	case CM_DREP_ATTR_ID:
		CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->cid == (int32_t)p_cm->cid ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_RESERVED_CID ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_INVALID_CID );
		__proc_drep( p_cm, (mad_cm_drep_t*)p_mad );
		break;

	case CM_LAP_ATTR_ID:
		CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->cid == (int32_t)p_cm->cid ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_RESERVED_CID ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_INVALID_CID );
		__proc_lap( p_cm, (mad_cm_lap_t*)p_mad );
		break;

	case CM_APR_ATTR_ID:
		CL_ASSERT( ((al_conn_qp_t*)p_cm->h_qp)->cid == (int32_t)p_cm->cid ||
			((al_conn_qp_t*)p_cm->h_qp)->cid == AL_INVALID_CID );
		__proc_apr( p_cm, (mad_cm_apr_t*)p_mad );
		break;

	//case CM_SIDR_REQ_ATTR_ID:
	//	p_async_mad->item.pfn_callback = __process_cm_sidr_req;
	//	break;

	//case CM_SIDR_REP_ATTR_ID:
	//	p_async_mad->item.pfn_callback = __process_cm_sidr_rep;
	//	break;

	default:
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid CM recv MAD attribute ID %d.\n", p_mad->attr_id) );
	}

	AL_EXIT( AL_DBG_CM );
}

/******************************************************************************
* CEP callback handler.
*
*/

#ifdef CL_KERNEL
static void
__process_cep_cb(
#else
static void
__cm_handler(
#endif
	IN		const	ib_al_handle_t				h_al,
	IN		const	net32_t						cid )
{
	ib_api_status_t		status;
	void*				context;
	net32_t				new_cid;
	ib_mad_element_t	*p_mad;
	ib_cm_handle_t		h_cm;

	AL_ENTER( AL_DBG_CM );

	for( status = al_cep_poll( h_al, cid, &context, &new_cid, &p_mad );
		status == IB_SUCCESS;
		status = al_cep_poll( h_al, cid, &context, &new_cid, &p_mad ) )
	{
		/* Something to do - WOOT!!! */
		if( new_cid != AL_INVALID_CID )
		{
			__proc_listen( (al_listen_t*)context,
				new_cid, ib_get_mad_buf( p_mad ) );
		}
		else if( p_mad->status != IB_SUCCESS )
		{
			/* Context is a QP handle, and a sent MAD timed out. */
			__proc_failed_send(
				(ib_qp_handle_t)context, ib_get_mad_buf( p_mad ) );
		}
		else
		{
			h_cm.h_al = h_al;
			h_cm.cid = cid;
			h_cm.h_qp = (ib_qp_handle_t)context;
			__proc_conn( &h_cm, ib_get_mad_buf( p_mad ) );
		}
		ib_put_mad( p_mad );
	}
}


#ifdef CL_KERNEL

static void
__process_cep_async(
	IN				cl_async_proc_item_t		*p_item )
{
	cep_async_mad_t	*p_async_mad;

	AL_ENTER( AL_DBG_CM );

	p_async_mad = PARENT_STRUCT( p_item, cep_async_mad_t, item );

	__process_cep_cb( p_async_mad->h_al, p_async_mad->cid );

	cl_free( p_async_mad );

	AL_EXIT( AL_DBG_CM );
}


/*
 * The handler is invoked at DISPATCH_LEVEL in kernel mode.  We need to switch
 * to a passive level thread context to perform QP modify and invoke user
 * callbacks.
 */
static void
__cm_handler(
	IN		const	ib_al_handle_t				h_al,
	IN		const	net32_t						cid )
{
	cep_async_mad_t	*p_async_mad;

	AL_ENTER( AL_DBG_CM );

	p_async_mad = (cep_async_mad_t*)cl_zalloc( sizeof(cep_async_mad_t) );
	if( !p_async_mad )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("failed to cl_zalloc cm_async_mad_t (%d bytes)\n",
			sizeof(cep_async_mad_t)) );
		return;
	}

	p_async_mad->h_al = h_al;
	p_async_mad->cid = cid;
	p_async_mad->item.pfn_callback = __process_cep_async;

	/* Queue the MAD for asynchronous processing. */
	cl_async_proc_queue( gp_async_proc_mgr, &p_async_mad->item );

	AL_EXIT( AL_DBG_CM );
}
#endif	/* CL_KERNEL */


/*
 * Transition the QP to the INIT state, if it is not already in the
 * INIT state.
 */
ib_api_status_t
__cep_init_qp(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_qp_mod_t* const			p_init )
{
	ib_qp_mod_t			qp_mod;
	ib_api_status_t		status;

	/*
	 * Move to the init state to allow posting of receive buffers.
	 * Chech the current state of the QP.  The user may have already
	 * transitioned it and posted some receives to the QP, so we
	 * should not reset the QP if it is already in the INIT state.
	 */
	if( h_qp->state != IB_QPS_INIT )
	{
		/* Reset the QP. */
		cl_memclr( &qp_mod, sizeof(ib_qp_mod_t) );
		qp_mod.req_state = IB_QPS_RESET;

		status = ib_modify_qp( h_qp, &qp_mod );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_modify_qp to IB_QPS_RESET returned %s\n",
				ib_get_err_str(status) ) );
		}

		/* Initialize the QP. */
		status = ib_modify_qp( h_qp, p_init );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_modify_qp returned %s.\n", ib_get_err_str(status) ) );
			return status;
		}
	}

	return IB_SUCCESS;
}

static ib_api_status_t
__cep_pre_req(
	IN		const	ib_cm_req_t* const			p_cm_req )
{
	ib_api_status_t		status;
	ib_qp_mod_t			qp_mod;

	AL_ENTER( AL_DBG_CM );

	status = al_cep_pre_req( qp_get_al( p_cm_req->h_qp ),
		((al_conn_qp_t*)p_cm_req->h_qp)->cid, p_cm_req, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_pre_req returned %s.\n", ib_get_err_str( status )) );
		return status;
	}

	/* Transition QP through state machine */
	/*
	 * Warning! Using all access rights.  We need to modify
	 * the ib_cm_req_t to include this.
	 */
	qp_mod.state.init.access_ctrl |=
		IB_AC_RDMA_READ | IB_AC_RDMA_WRITE | IB_AC_ATOMIC;
	status = __cep_init_qp( p_cm_req->h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("__cep_init_qp returned %s\n", ib_get_err_str(status)) );
		return status;
	}

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


static ib_api_status_t
__cep_conn_req(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_cm_req_t* const			p_cm_req )
{
	ib_api_status_t		status;
	//cl_status_t			cl_status;
	//cl_event_t			sync_event;
	//cl_event_t			*p_sync_event = NULL;
	al_conn_qp_t		*p_qp;

	AL_ENTER( AL_DBG_CM );

	/* event based mechanism */
	if( p_cm_req->flags & IB_FLAGS_SYNC )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_UNSUPPORTED;
		//cl_event_construct( &sync_event );
		//cl_status = cl_event_init( &sync_event, FALSE );
		//if( cl_status != CL_SUCCESS )
		//{
		//	__deref_conn( p_conn );
		//	return ib_convert_cl_status( cl_status );
		//}
		//p_conn->p_sync_event = p_sync_event = &sync_event;
	}

	p_qp = (al_conn_qp_t*)p_cm_req->h_qp;

	/* Get a CEP and bind it to the QP. */
	status = al_create_cep( h_al, __cm_handler, p_qp, deref_al_obj_cb, &p_qp->cid );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_create_cep returned %s.\n", ib_get_err_str( status )) );
		goto done;
	}

	/* Take a reference on behalf of the CEP. */
	ref_al_obj( &p_qp->qp.obj );

	status = __cep_pre_req( p_cm_req );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("__cep_pre_req returned %s.\n", ib_get_err_str( status )) );
		goto err;
	}

	/* Store callback pointers. */
	p_qp->pfn_cm_req_cb = p_cm_req->pfn_cm_req_cb;
	p_qp->pfn_cm_rep_cb = p_cm_req->pfn_cm_rep_cb;
	p_qp->pfn_cm_mra_cb = p_cm_req->pfn_cm_mra_cb;
	p_qp->pfn_cm_rej_cb = p_cm_req->pfn_cm_rej_cb;

	/* Send the REQ. */
	status = al_cep_send_req( h_al, p_qp->cid );
	if( status != IB_SUCCESS )
	{
		//if( p_sync_event )
		//	cl_event_destroy( p_sync_event );

		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_send_req returned %s.\n", ib_get_err_str(status)) );
err:
		al_destroy_cep( h_al, &p_qp->cid, TRUE );
	}

	/* wait on event if synchronous operation */
	//if( p_sync_event )
	//{
	//	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
	//		("event blocked on REQ...\n") );
	//	cl_event_wait_on( p_sync_event, EVENT_NO_TIMEOUT, FALSE );

	//	cl_event_destroy( p_sync_event );
	//}

done:
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_req(
	IN		const	ib_cm_req_t* const			p_cm_req )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_req )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Only supported qp types allowed */
	switch( p_cm_req->qp_type )
	{
	default:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid qp_type.\n") );
		return IB_INVALID_SETTING;

	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		if( AL_OBJ_INVALID_HANDLE( p_cm_req->h_qp, AL_OBJ_TYPE_H_QP ) ||
			(p_cm_req->h_qp->type != p_cm_req->qp_type) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
			return IB_INVALID_QP_HANDLE;
		}

		status = __cep_conn_req( qp_get_al( p_cm_req->h_qp ), p_cm_req );
		break;

	case IB_QPT_UNRELIABLE_DGRM:
		if( AL_OBJ_INVALID_HANDLE( p_cm_req->h_al, AL_OBJ_TYPE_H_AL ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
			return IB_INVALID_AL_HANDLE;
		}
		status = IB_UNSUPPORTED;
//		status = cm_sidr_req( p_cm_req->h_al, p_cm_req );
		break;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


/*
 * Note: we pass in the QP handle separately because it comes form different
 * sources.  It comes from the ib_cm_rep_t structure in the ib_cm_rep path, and
 * from the ib_cm_handle_t structure in the ib_cm_rtu path.
 */
static ib_api_status_t
__cep_rts_qp(
	IN		const	ib_cm_handle_t				h_cm,
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_access_t					access_ctrl,
	IN		const	uint32_t					sq_depth,
	IN		const	uint32_t					rq_depth )
{
	ib_api_status_t	status;
	ib_qp_mod_t		qp_mod;

	AL_ENTER( AL_DBG_CM );

	/* Set the QP to RTR. */
	status = al_cep_get_rtr_attr( h_cm.h_al, h_cm.cid, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_get_rtr_attr returned %s\n", ib_get_err_str( status )) );
		return status;
	}

	if( access_ctrl )
	{
		qp_mod.state.rtr.access_ctrl = access_ctrl;
		qp_mod.state.rtr.opts |= IB_MOD_QP_ACCESS_CTRL;
	}

	if( sq_depth )
	{
		qp_mod.state.rtr.sq_depth = sq_depth;
		qp_mod.state.rtr.opts |= IB_MOD_QP_SQ_DEPTH;
	}

	if( rq_depth )
	{
		qp_mod.state.rtr.rq_depth = rq_depth;
		qp_mod.state.rtr.opts |= IB_MOD_QP_RQ_DEPTH;
	}

	status = ib_modify_qp( h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_modify_qp to RTR returned %s.\n", ib_get_err_str(status) ) );
		return status;
	}

	/* Set the QP to RTS. */
	status = al_cep_get_rts_attr( h_cm.h_al, h_cm.cid, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_get_rts_attr returned %s\n", ib_get_err_str( status )) );
		return status;
	}

	status = ib_modify_qp( h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_modify_qp to RTS returned %s.\n", ib_get_err_str(status) ) );
		return status;
	}

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


static ib_api_status_t
__cep_pre_rep(
	IN		const	ib_cm_handle_t				h_cm,
	IN				ib_qp_mod_t* const			p_qp_mod,
	IN		const	ib_cm_rep_t* const			p_cm_rep )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	/* Transition the QP to the INIT state. */
	p_qp_mod->state.init.access_ctrl = p_cm_rep->access_ctrl;
	status = __cep_init_qp( p_cm_rep->h_qp, p_qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cm_init_qp returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Prepost receives. */
	if( p_cm_rep->p_recv_wr )
	{
		status = ib_post_recv( p_cm_rep->h_qp, p_cm_rep->p_recv_wr,
			(ib_recv_wr_t**)p_cm_rep->pp_recv_failure );
		if( status != IB_SUCCESS )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_post_recv returned %s.\n", ib_get_err_str(status)) );
			return status;
		}
	}

	/* Transition the QP to the RTR and RTS states. */
	status = __cep_rts_qp( h_cm, p_cm_rep->h_qp,
		p_cm_rep->access_ctrl, p_cm_rep->sq_depth, p_cm_rep->rq_depth );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("__cep_rts_qp returned %s.\n", ib_get_err_str(status)) );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


static ib_api_status_t
__cep_conn_rep(
	IN				ib_cm_handle_t				h_cm,
	IN	const		ib_cm_rep_t* const			p_cm_rep )
{
	ib_api_status_t		status;
	ib_qp_mod_t			qp_mod;

	AL_ENTER( AL_DBG_CM );

	status = al_cep_pre_rep(
		h_cm.h_al, h_cm.cid, p_cm_rep->h_qp, deref_al_obj_cb, p_cm_rep,
		&((al_conn_qp_t*)p_cm_rep->h_qp)->cid, &qp_mod );
	switch( status )
	{
	case IB_SUCCESS:
		break;

	case IB_RESOURCE_BUSY:
		/* We don't destroy the CEP to allow the user to retry accepting. */
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("QP already connected.\n") );
		return IB_INVALID_QP_HANDLE;

	default:
		al_cep_rej( h_cm.h_al, h_cm.cid, IB_REJ_INSUF_RESOURCES, NULL, 0, NULL, 0 );
		al_destroy_cep( h_cm.h_al, &h_cm.cid, FALSE );

		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_pre_rep returned %s.\n", ib_get_err_str( status )) );
		return status;
	}

	/* Take a reference on behalf of the CEP. */
	ref_al_obj( &p_cm_rep->h_qp->obj );

	/* Store the CM callbacks. */
	((al_conn_qp_t*)p_cm_rep->h_qp)->pfn_cm_rej_cb = p_cm_rep->pfn_cm_rej_cb;
	((al_conn_qp_t*)p_cm_rep->h_qp)->pfn_cm_mra_cb = p_cm_rep->pfn_cm_mra_cb;
	((al_conn_qp_t*)p_cm_rep->h_qp)->pfn_cm_rtu_cb = p_cm_rep->pfn_cm_rtu_cb;
	((al_conn_qp_t*)p_cm_rep->h_qp)->pfn_cm_lap_cb = p_cm_rep->pfn_cm_lap_cb;
	((al_conn_qp_t*)p_cm_rep->h_qp)->pfn_cm_dreq_cb = p_cm_rep->pfn_cm_dreq_cb;

	/* Transition QP through state machine */
	status = __cep_pre_rep( h_cm, &qp_mod, p_cm_rep );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("__cep_pre_rep returned %s\n", ib_get_err_str(status)) );
		goto err;
	}

	status = al_cep_send_rep( h_cm.h_al, h_cm.cid );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_send_rep returned %s\n", ib_get_err_str(status)) );
err:

		/* Reject and abort the connection. */
		al_cep_rej( h_cm.h_al, h_cm.cid, IB_REJ_INSUF_QP, NULL, 0, NULL, 0 );
		al_destroy_cep(
			h_cm.h_al, &((al_conn_qp_t*)p_cm_rep->h_qp)->cid, TRUE );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_rep(
	IN		const	ib_cm_handle_t				h_cm_req,
	IN		const	ib_cm_rep_t* const			p_cm_rep )
{
	ib_api_status_t		status;
	net32_t				cid;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_rep )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Only supported qp types allowed */
	status = IB_SUCCESS;
	switch( p_cm_rep->qp_type )
	{
	default:
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid qp_type.\n") );
		status = IB_INVALID_SETTING;
		break;

	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		if( AL_OBJ_INVALID_HANDLE( p_cm_rep->h_qp, AL_OBJ_TYPE_H_QP ) ||
			(p_cm_rep->h_qp->type != p_cm_rep->qp_type) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
			status = IB_INVALID_QP_HANDLE;
		}
		else if( p_cm_rep->h_qp->obj.h_al != h_cm_req.h_al )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
			status = IB_INVALID_QP_HANDLE;
		}
		break;

	case IB_QPT_UNRELIABLE_DGRM:
		if( ( p_cm_rep->status == IB_SIDR_SUCCESS ) &&
			(AL_OBJ_INVALID_HANDLE( p_cm_rep->h_qp, AL_OBJ_TYPE_H_QP ) ||
			(p_cm_rep->h_qp->type != p_cm_rep->qp_type) ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
			status = IB_INVALID_QP_HANDLE;
		}
		break;
	}

	if( status != IB_SUCCESS )
	{
		cid = h_cm_req.cid;
		al_cep_rej(
			h_cm_req.h_al, h_cm_req.cid, IB_REJ_INSUF_QP, NULL, 0, NULL, 0 );
		al_destroy_cep( h_cm_req.h_al, &cid, FALSE );

		AL_EXIT( AL_DBG_CM );
		return status;
	}

	if( p_cm_rep->qp_type == IB_QPT_UNRELIABLE_DGRM )
		status = IB_UNSUPPORTED;//status = cm_sidr_rep( p_conn, p_cm_rep );
	else
		status = __cep_conn_rep( h_cm_req, p_cm_rep );

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_rtu(
	IN		const	ib_cm_handle_t				h_cm_rep,
	IN		const	ib_cm_rtu_t* const			p_cm_rtu )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_rtu )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	///*
	// * Call invalid if event is still processed.
	// * User may have called rtu in rep callback.
	// */
	//if( p_conn->p_sync_event )
	//{
	//	AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
	//		("Connection in invalid state. Sync call in progress.\n" ) );

	//	cm_res_release( p_conn );
	//	__deref_conn( p_conn );
	//	return IB_INVALID_STATE;
	//}
	((al_conn_qp_t*)h_cm_rep.h_qp)->pfn_cm_apr_cb = p_cm_rtu->pfn_cm_apr_cb;
	((al_conn_qp_t*)h_cm_rep.h_qp)->pfn_cm_dreq_cb = p_cm_rtu->pfn_cm_dreq_cb;

	/* Transition QP through state machine */
	status = __cep_rts_qp( h_cm_rep, h_cm_rep.h_qp,
		p_cm_rtu->access_ctrl, p_cm_rtu->sq_depth, p_cm_rtu->rq_depth );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("__cep_rts_qp returned %s.\n", ib_get_err_str( status )) );
		goto err;
	}

	status = al_cep_rtu( h_cm_rep.h_al, h_cm_rep.cid,
		p_cm_rtu->p_rtu_pdata, p_cm_rtu->rtu_length );
	if( status != IB_SUCCESS && status != IB_INVALID_STATE )
	{
err:
		/* Reject and abort the connection. */
		al_cep_rej(
			h_cm_rep.h_al, h_cm_rep.cid, IB_REJ_INSUF_QP, NULL, 0, NULL, 0 );

		__cep_timewait_qp( h_cm_rep.h_qp );

		al_destroy_cep(
			h_cm_rep.h_al, &((al_conn_qp_t*)h_cm_rep.h_qp)->cid, TRUE );

		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_rtu returned %s.\n", ib_get_err_str( status )) );
		return status;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_mra(
	IN		const	ib_cm_handle_t				h_cm,
	IN		const	ib_cm_mra_t* const			p_cm_mra )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_mra )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = al_cep_mra( h_cm.h_al, h_cm.cid, p_cm_mra );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_mra returned %s\n", ib_get_err_str( status )) );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_rej(
	IN		const	ib_cm_handle_t				h_cm,
	IN		const	ib_cm_rej_t* const			p_cm_rej )
{
	ib_api_status_t		status;
	net32_t				cid;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_rej )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = al_cep_rej( h_cm.h_al, h_cm.cid, p_cm_rej->rej_status,
		p_cm_rej->p_ari->data, p_cm_rej->ari_length,
		p_cm_rej->p_rej_pdata, p_cm_rej->rej_length );

	if( h_cm.h_qp )
	{
		__cep_timewait_qp( h_cm.h_qp );

		al_destroy_cep(
			h_cm.h_al, &((al_conn_qp_t*)h_cm.h_qp)->cid, TRUE );
	}
	else
	{
		cid = h_cm.cid;
		al_destroy_cep( h_cm.h_al, &cid, FALSE );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_dreq(
	IN		const	ib_cm_dreq_t* const			p_cm_dreq )
{
	ib_api_status_t	status;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_dreq )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Only supported qp types allowed */
	switch( p_cm_dreq->qp_type )
	{
	default:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid qp_type.\n") );
		return IB_INVALID_SETTING;

	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		if( AL_OBJ_INVALID_HANDLE( p_cm_dreq->h_qp, AL_OBJ_TYPE_H_QP ) ||
			(p_cm_dreq->h_qp->type != p_cm_dreq->qp_type) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
			return IB_INVALID_QP_HANDLE;
		}
		break;
	}

	/* Store the callback pointers. */
	((al_conn_qp_t*)p_cm_dreq->h_qp)->pfn_cm_drep_cb =
		p_cm_dreq->pfn_cm_drep_cb;

	status = al_cep_dreq( p_cm_dreq->h_qp->obj.h_al,
		((al_conn_qp_t*)p_cm_dreq->h_qp)->cid,
		p_cm_dreq->p_dreq_pdata, p_cm_dreq->dreq_length );
	switch( status )
	{
	case IB_INVALID_STATE:
	case IB_INVALID_HANDLE:
	case IB_INVALID_PARAMETER:
	case IB_INVALID_SETTING:
		/* Bad call - don't touch the QP. */
		break;

	case IB_SUCCESS:
		/* Wait for the DREP or timeout. */
		break;

	default:
		/*
		 * If we failed to send the DREQ, just release the connection.  It's
		 * unreliable anyway.  The local port may be down.  Note that we could
		 * not send the DREQ, but we still could have received one.  The DREQ
		 * will have a reference on the connection until the user calls
		 * ib_cm_drep.
		 */
		__cep_timewait_qp( p_cm_dreq->h_qp );

		al_destroy_cep( p_cm_dreq->h_qp->obj.h_al,
			&((al_conn_qp_t*)p_cm_dreq->h_qp)->cid, TRUE );
		status = IB_SUCCESS;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}



ib_api_status_t
ib_cm_drep(
	IN		const	ib_cm_handle_t				h_cm_dreq,
	IN		const	ib_cm_drep_t* const			p_cm_drep )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_drep )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = al_cep_drep( h_cm_dreq.h_al, h_cm_dreq.cid,
		p_cm_drep->p_drep_pdata, p_cm_drep->drep_length );
	switch( status )
	{
	case IB_INVALID_SETTING:
	case IB_INVALID_HANDLE:
	case IB_INVALID_PARAMETER:
	case IB_INVALID_STATE:
		/* Bad call - don't touch the QP. */
		break;

	default:
		/*
		 * Some other out-of-resource error - continue as if we succeeded in
		 * sending the DREP.
		 */
		status = IB_SUCCESS;
		/* Fall through */
	case IB_SUCCESS:
		__cep_timewait_qp( h_cm_dreq.h_qp );

		al_destroy_cep( h_cm_dreq.h_al,
			&((al_conn_qp_t*)h_cm_dreq.h_qp)->cid, TRUE );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_lap(
	IN		const	ib_cm_lap_t* const			p_cm_lap )
{
	ib_api_status_t	status;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_lap )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Only supported qp types allowed */
	switch( p_cm_lap->qp_type )
	{
	default:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid qp_type.\n") );
		return IB_INVALID_SETTING;

	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		if( AL_OBJ_INVALID_HANDLE( p_cm_lap->h_qp, AL_OBJ_TYPE_H_QP ) ||
			(p_cm_lap->h_qp->type != p_cm_lap->qp_type) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
			return IB_INVALID_QP_HANDLE;
		}
		break;
	}

	status = al_cep_lap( p_cm_lap->h_qp->obj.h_al,
		((al_conn_qp_t*)p_cm_lap->h_qp)->cid, p_cm_lap );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_lap returned %s.\n", ib_get_err_str( status )) );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_apr(
	IN		const	ib_cm_handle_t				h_cm_lap,
	IN		const	ib_cm_apr_t* const			p_cm_apr )
{
	ib_api_status_t		status;
	ib_qp_mod_t			qp_mod;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_apr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Only supported qp types allowed */
	switch( p_cm_apr->qp_type )
	{
	default:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid qp_type.\n") );
		return IB_INVALID_SETTING;

	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		if( AL_OBJ_INVALID_HANDLE( p_cm_apr->h_qp, AL_OBJ_TYPE_H_QP ) ||
			(p_cm_apr->h_qp->type != p_cm_apr->qp_type) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
			return IB_INVALID_QP_HANDLE;
		}
		break;
	}

	status = al_cep_pre_apr( h_cm_lap.h_al, h_cm_lap.cid, p_cm_apr, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_pre_apr returned %s.\n", ib_get_err_str( status )) );
		return status;
	}

	/* Load alt path into QP */
	status = ib_modify_qp( h_cm_lap.h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_modify_qp for LAP returned %s.\n",
			ib_get_err_str( status )) );
		return status;
	}
	
	status = al_cep_send_apr( h_cm_lap.h_al, h_cm_lap.cid );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_send_apr returned %s.\n",
			ib_get_err_str( status )) );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_force_apm(
	IN		const	ib_qp_handle_t				h_qp )
{
	ib_api_status_t	status;
	al_conn_qp_t	*p_conn_qp;
	ib_qp_mod_t		qp_mod;

	AL_ENTER( AL_DBG_CM );

	if( AL_OBJ_INVALID_HANDLE( h_qp, AL_OBJ_TYPE_H_QP ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		return IB_INVALID_QP_HANDLE;
	}

	p_conn_qp = PARENT_STRUCT( h_qp, al_conn_qp_t, qp );
	cl_memclr( &qp_mod, sizeof(ib_qp_mod_t) );
	qp_mod.req_state = IB_QPS_RTS;
	qp_mod.state.rts.apm_state = IB_APM_MIGRATED;
	qp_mod.state.rts.opts = IB_MOD_QP_APM_STATE;

	/* Set the QP to RTS. */
	status = ib_modify_qp( h_qp, &qp_mod );

	AL_EXIT( AL_DBG_CM );
	return status;
}


static void
__destroying_listen(
	IN				al_obj_t*					p_obj )
{
	al_listen_t		*p_listen;

	p_listen = PARENT_STRUCT( p_obj, al_listen_t, obj );

	/* Destroy the listen's CEP. */
	al_destroy_cep( p_obj->h_al, &p_listen->cid, TRUE );
}



static void
__free_listen(
	IN				al_obj_t*					p_obj )
{
	destroy_al_obj( p_obj );
	cl_free( PARENT_STRUCT( p_obj, al_listen_t, obj ) );
}


static ib_api_status_t
__cep_listen(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_cm_listen_t* const		p_cm_listen,
	IN		const	void* const					listen_context,
		OUT			ib_listen_handle_t* const	ph_cm_listen )
{
	ib_api_status_t		status;
	al_listen_t			*p_listen;
	ib_cep_listen_t		cep_listen;

	AL_ENTER( AL_DBG_CM );

	/* Allocate the listen object. */
	p_listen = (al_listen_t*)cl_zalloc( sizeof(al_listen_t) );
	if( !p_listen )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Copy the listen request information for matching incoming requests. */
	p_listen->pfn_cm_req_cb = p_cm_listen->pfn_cm_req_cb;

	/* valid for ud qp_type only */
	p_listen->sidr_context = p_cm_listen->sidr_context;

	construct_al_obj( &p_listen->obj, AL_OBJ_TYPE_H_LISTEN );
	status = init_al_obj( &p_listen->obj, listen_context, TRUE,
		__destroying_listen, NULL, __free_listen );
	if( status != IB_SUCCESS )
	{
		__free_listen( &p_listen->obj );
		AL_EXIT( AL_DBG_CM );
		return status;
	}

	/* Add the listen to the AL instance's object list. */
	status = attach_al_obj( &h_al->obj, &p_listen->obj );
	if( status != IB_SUCCESS )
	{
		p_listen->obj.pfn_destroy( &p_listen->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Create a CEP to listen on. */
    p_listen->cid = AL_INVALID_CID;
	status = al_create_cep( h_al, __cm_handler, p_listen, deref_al_obj_cb, &p_listen->cid );
	if( status != IB_SUCCESS )
	{
		p_listen->obj.pfn_destroy( &p_listen->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_create_cep returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Take a reference on behalf of the CEP. */
	ref_al_obj( &p_listen->obj );

	cep_listen.cmp_len = p_cm_listen->compare_length;
	cep_listen.cmp_offset = p_cm_listen->compare_offset;
	cep_listen.p_cmp_buf = p_cm_listen->p_compare_buffer;
	cep_listen.port_guid = p_cm_listen->port_guid;
	cep_listen.svc_id = p_cm_listen->svc_id;

	status = al_cep_listen( h_al, p_listen->cid, &cep_listen );
	if( status != IB_SUCCESS )
	{
		p_listen->obj.pfn_destroy( &p_listen->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_cep_listen returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	*ph_cm_listen = p_listen;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_listen->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


ib_api_status_t
ib_cm_listen(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_cm_listen_t* const		p_cm_listen,
	IN		const	void* const					listen_context,
		OUT			ib_listen_handle_t* const	ph_cm_listen )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_cm_listen || !ph_cm_listen )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = __cep_listen(h_al, p_cm_listen, listen_context, ph_cm_listen );

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
ib_cm_cancel(
	IN		const	ib_listen_handle_t			h_cm_listen,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_CM );

	if( AL_OBJ_INVALID_HANDLE( h_cm_listen, AL_OBJ_TYPE_H_LISTEN ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	ref_al_obj( &h_cm_listen->obj );
	h_cm_listen->obj.pfn_destroy( &h_cm_listen->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


ib_api_status_t
ib_cm_handoff(
	IN		const	ib_cm_handle_t				h_cm_req,
	IN		const	ib_net64_t					svc_id )
{
	UNUSED_PARAM( h_cm_req );
	UNUSED_PARAM( svc_id );
	return IB_UNSUPPORTED;
}
