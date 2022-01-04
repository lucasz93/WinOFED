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

#include <iba/ib_al.h>
#include <complib/cl_timer.h>

#include "al.h"
#include "al_ca.h"
#include "al_common.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_query.tmh"
#endif
#include "al_mgr.h"
#include "al_query.h"
#include "ib_common.h"


#define PR102982


static ib_api_status_t
query_sa(
	IN				al_query_t					*p_query,
	IN		const	ib_query_req_t* const		p_query_req,
	IN		const	ib_al_flags_t				flags );

void
query_req_cb(
	IN				al_sa_req_t					*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response );


ib_api_status_t
ib_query(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_query_req_t* const		p_query_req,
		OUT			ib_query_handle_t* const	ph_query OPTIONAL )
{
	al_query_t				*p_query;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QUERY );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_query_req )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}
	if( (p_query_req->flags & IB_FLAGS_SYNC) && !cl_is_blockable() )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_UNSUPPORTED\n") );
		return IB_UNSUPPORTED;
	}

	/* Allocate a new query. */
	p_query = cl_zalloc( sizeof( al_query_t ) );
	if( !p_query )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("insufficient memory\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Copy the query context information. */
	p_query->sa_req.pfn_sa_req_cb = query_req_cb;
	p_query->sa_req.user_context = p_query_req->query_context;
	p_query->pfn_query_cb = p_query_req->pfn_query_cb;
	p_query->query_type = p_query_req->query_type;

	/* Track the query with the AL instance. */
	al_insert_query( h_al, p_query );

	/* Issue the MAD to the SA. */
	status = query_sa( p_query, p_query_req, p_query_req->flags );
	if( status != IB_SUCCESS && status != IB_INVALID_GUID )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("query_sa failed: %s\n", ib_get_err_str(status) ) );
	}

	/* Cleanup from issuing the query if it failed or was synchronous. */
	if( status != IB_SUCCESS )
	{
		al_remove_query( p_query );
		cl_free( p_query );
	}
	else if( ph_query )
	{
		*ph_query = p_query;
	}

	AL_EXIT( AL_DBG_QUERY );
	return status;
}



/*
 * Query the SA based on the user's request.
 */
static ib_api_status_t
query_sa(
	IN				al_query_t					*p_query,
	IN		const	ib_query_req_t* const		p_query_req,
	IN		const	ib_al_flags_t				flags )
{
	ib_user_query_t				sa_req, *p_sa_req;
	union _query_sa_recs
	{
		ib_service_record_t		svc;
		ib_node_record_t		node;
		ib_portinfo_record_t	portinfo;
		ib_path_rec_t			path;
		ib_class_port_info_t	class_port_info;
	}	rec;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QUERY );

	cl_memclr( &rec, sizeof(rec) );

	/* Set the request information. */
	p_sa_req = &sa_req;
	sa_req.method = IB_MAD_METHOD_GETTABLE;

	/* Set the MAD attributes and component mask correctly. */
	switch( p_query_req->query_type )
	{
	case IB_QUERY_USER_DEFINED:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("USER_DEFINED\n") );
		p_sa_req = (ib_user_query_t*)p_query_req->p_query_input;
		if( !p_sa_req->method )
		{
			AL_EXIT( AL_DBG_QUERY );
			return IB_INVALID_SETTING;
		}
		break;

	case IB_QUERY_ALL_SVC_RECS:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("IB_QUERY_ALL_SVC_RECS\n") );
		sa_req.attr_id = IB_MAD_ATTR_SERVICE_RECORD;
		sa_req.attr_size = sizeof( ib_service_record_t );
		sa_req.comp_mask = 0;
		sa_req.p_attr = &rec.svc;
		break;

	case IB_QUERY_SVC_REC_BY_NAME:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("SVC_REC_BY_NAME\n") );
		sa_req.attr_id = IB_MAD_ATTR_SERVICE_RECORD;
		sa_req.attr_size = sizeof( ib_service_record_t );
		sa_req.comp_mask = IB_SR_COMPMASK_SNAME;
		sa_req.p_attr = &rec.svc;
		cl_memcpy( rec.svc.service_name, p_query_req->p_query_input,
			sizeof( ib_svc_name_t ) );
		break;

	case IB_QUERY_SVC_REC_BY_ID:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("SVC_REC_BY_ID\n") );
		sa_req.attr_id = IB_MAD_ATTR_SERVICE_RECORD;
		sa_req.attr_size = sizeof( ib_service_record_t );
		sa_req.comp_mask = IB_SR_COMPMASK_SID;
		sa_req.p_attr = &rec.svc;
		rec.svc.service_id = *(ib_net64_t*)(p_query_req->p_query_input);
		break;

	case IB_QUERY_CLASS_PORT_INFO:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("IB_QUERY_CLASS_PORT_INFO\n") );
		sa_req.method = IB_MAD_METHOD_GET;
		sa_req.attr_id = IB_MAD_ATTR_CLASS_PORT_INFO;
		sa_req.attr_size = sizeof( ib_class_port_info_t );
		sa_req.comp_mask = 0;
		sa_req.p_attr = &rec.class_port_info;
		break;

	case IB_QUERY_NODE_REC_BY_NODE_GUID:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("NODE_REC_BY_NODE_GUID\n") );
		/*
		 *	15.2.5.2:
		 * if >1 ports on of a CA/RTR the subnet return multiple
		 * record
		 */
		sa_req.attr_id = IB_MAD_ATTR_NODE_RECORD;
		sa_req.attr_size = sizeof( ib_node_record_t );
		sa_req.comp_mask = IB_NR_COMPMASK_NODEGUID;
		sa_req.p_attr = &rec.node;
		rec.node.node_info.node_guid =
			*(ib_net64_t*)(p_query_req->p_query_input);
		break;

	case IB_QUERY_PORT_REC_BY_LID:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("PORT_REC_BY_LID\n") );
		sa_req.attr_id = IB_MAD_ATTR_PORTINFO_RECORD;
		sa_req.attr_size = sizeof( ib_portinfo_record_t );
		sa_req.comp_mask = IB_PIR_COMPMASK_BASELID;
		sa_req.p_attr = &rec.portinfo;
		rec.portinfo.port_info.base_lid =
			*(ib_net16_t*)(p_query_req->p_query_input);
		break;

	case IB_QUERY_PATH_REC_BY_PORT_GUIDS:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("PATH_REC_BY_PORT_GUIDS\n") );
		sa_req.attr_id = IB_MAD_ATTR_PATH_RECORD;
		sa_req.attr_size = sizeof( ib_path_rec_t );
		sa_req.comp_mask = (IB_PR_COMPMASK_DGID |
			IB_PR_COMPMASK_SGID | IB_PR_COMPMASK_NUMBPATH);
		sa_req.p_attr = &rec.path;
		ib_gid_set_default( &rec.path.dgid, ((ib_guid_pair_t*)
			(p_query_req->p_query_input))->dest_guid );
		ib_gid_set_default( &rec.path.sgid, ((ib_guid_pair_t*)
			(p_query_req->p_query_input))->src_guid );
		rec.path.num_path = 1;
		break;

	case IB_QUERY_PATH_REC_BY_GIDS:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("PATH_REC_BY_GIDS\n") );
		sa_req.attr_id = IB_MAD_ATTR_PATH_RECORD;
		sa_req.attr_size = sizeof( ib_path_rec_t );
		sa_req.comp_mask = (IB_PR_COMPMASK_DGID |
			IB_PR_COMPMASK_SGID | IB_PR_COMPMASK_NUMBPATH);
		sa_req.p_attr = &rec.path;
		cl_memcpy( &rec.path.dgid, &((ib_gid_pair_t*)
			(p_query_req->p_query_input))->dest_gid, sizeof( ib_gid_t ) );
		cl_memcpy( &rec.path.sgid, &((ib_gid_pair_t*)
			(p_query_req->p_query_input))->src_gid, sizeof( ib_gid_t ) );
		rec.path.num_path = 1;
		break;

	case IB_QUERY_PATH_REC_BY_LIDS:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY, ("PATH_REC_BY_LIDS\n") );
		/* SGID must be provided for GET_TABLE requests. */
		sa_req.method = IB_MAD_METHOD_GET;
		sa_req.attr_id = IB_MAD_ATTR_PATH_RECORD;
		sa_req.attr_size = sizeof( ib_path_rec_t );
		sa_req.comp_mask =
			(IB_PR_COMPMASK_DLID | IB_PR_COMPMASK_SLID);
		sa_req.p_attr = &rec.path;
		rec.path.dlid = 
			((ib_lid_pair_t*)(p_query_req->p_query_input))->dest_lid;
		rec.path.slid =
			((ib_lid_pair_t*)(p_query_req->p_query_input))->src_lid;
#ifdef PR102982
		rec.path.num_path = 1;
#endif
		break;

	default:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("UNKNOWN\n") );
		CL_ASSERT( p_query_req->query_type == IB_QUERY_USER_DEFINED ||
			p_query_req->query_type == IB_QUERY_ALL_SVC_RECS ||
			p_query_req->query_type == IB_QUERY_SVC_REC_BY_NAME ||
			p_query_req->query_type == IB_QUERY_SVC_REC_BY_ID ||
			p_query_req->query_type == IB_QUERY_CLASS_PORT_INFO ||
			p_query_req->query_type == IB_QUERY_NODE_REC_BY_NODE_GUID ||
			p_query_req->query_type == IB_QUERY_PORT_REC_BY_LID ||
			p_query_req->query_type == IB_QUERY_PATH_REC_BY_PORT_GUIDS ||
			p_query_req->query_type == IB_QUERY_PATH_REC_BY_GIDS ||
			p_query_req->query_type == IB_QUERY_PATH_REC_BY_LIDS );

		return IB_ERROR;
	}

	status = al_send_sa_req(
		&p_query->sa_req, p_query_req->port_guid, p_query_req->timeout_ms,
		p_query_req->retry_cnt, p_sa_req, flags );
	AL_EXIT( AL_DBG_QUERY );
	return status;
}



/*
 * Query request completion callback.
 */
void
query_req_cb(
	IN				al_sa_req_t					*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response )
{
	al_query_t			*p_query;
	ib_query_rec_t		query_rec;
	ib_sa_mad_t			*p_sa_mad;

	AL_ENTER( AL_DBG_QUERY );
	p_query = PARENT_STRUCT( p_sa_req, al_query_t, sa_req );

	/* Initialize the results of the query. */
	cl_memclr( &query_rec, sizeof( ib_query_rec_t ) );
	query_rec.status = p_sa_req->status;
	query_rec.query_context = p_query->sa_req.user_context;
	query_rec.query_type = p_query->query_type;

	/* Form the result of the query, if we got one. */
	if( query_rec.status == IB_SUCCESS )
	{

		CL_ASSERT( p_mad_response );
		p_sa_mad = (ib_sa_mad_t*)p_mad_response->p_mad_buf;

		if (ib_get_attr_size( p_sa_mad->attr_offset ) != 0)
		{
			query_rec.result_cnt =
				( ( p_mad_response->size - IB_SA_MAD_HDR_SIZE ) /
				ib_get_attr_size( p_sa_mad->attr_offset ) );
		}
		else
		{
			query_rec.result_cnt = 0;
		}

		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_QUERY,
			("query succeeded with result_cnt = %d\n", query_rec.result_cnt) );

		query_rec.p_result_mad = p_mad_response;
	}
	else
	{
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_QUERY,
			("query failed: %s\n", ib_get_err_str(query_rec.status) ) );
		if( p_mad_response )
			query_rec.p_result_mad = p_mad_response;
	}

	/*
	 * Handing an internal MAD to a client.
	 * Track the MAD element with the client's AL instance.
	 */
	if( p_mad_response )
		al_handoff_mad( p_query->h_al, p_mad_response );

	/* Notify the user of the result. */
	p_query->pfn_query_cb( &query_rec );

	/* Cleanup from issuing the query. */
	al_remove_query( p_query );
	cl_free( p_query );

	AL_EXIT( AL_DBG_QUERY );
}
