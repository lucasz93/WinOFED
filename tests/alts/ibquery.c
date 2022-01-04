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



/*
 * Abstract:
 *	This test validates the ib_query API. ib_query access layer api queries
 *	subnet administrator on behalf of clients.
 *
 *
 * Environment:
 *	All
 */


#include <iba/ib_types.h>
#include <iba/ib_al.h>
#include <complib/cl_memory.h>
#include <complib/cl_thread.h>
#include <alts_debug.h>
#include <alts_common.h>

/*
 * Function prototypes
 */

void
alts_query_cb(
IN ib_query_rec_t	*p_query_rec
);
void
alts_reg_svc_cb(
IN ib_reg_svc_rec_t	*p_reg_svc_rec
);
void
alts_print_port_guid(
IN ib_port_info_t	*p_port_info
);
void
alts_print_node_info(
	ib_node_info_t *p_node_info
);

/*
 *	Globals
 */
ib_net64_t query_portguid;

/*	This test case assumes that the HCA has been configured by running
 *	SM.
 */
ib_api_status_t
al_test_query(void)
{
	ib_api_status_t		ib_status = IB_ERROR;
	ib_al_handle_t		h_al = NULL;

	ib_ca_handle_t		h_ca = NULL;
	uint32_t			bsize;
	uint32_t			i;
	ib_ca_attr_t		*p_ca_attr = NULL;
	//alts_ca_object_t	ca_obj;	// for testing stack
	ib_query_req_t		query_req;
	ib_port_attr_t		*p_port_attr = NULL;
	ib_reg_svc_req_t	reg_svc_req;
	ib_gid_t			port_gid = {0};
	ib_net16_t			pkey=0;
	ib_lid_pair_t		lid_pair;
	ib_guid_pair_t		guid_pair;
	ib_gid_pair_t		gid_pair;
	ib_user_query_t		info;
	ib_path_rec_t		path;
	ib_reg_svc_handle_t	h_reg_svc;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	do
	{
		/*
		 * Open the AL interface
		 */
		ib_status = alts_open_al(&h_al);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_open_al failed status = %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		/*
		 * Default opens the first CA
		 */
		ib_status = alts_open_ca(h_al, &h_ca);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_open_ca failed status = %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		/*
		 * Query the CA
		 */
		bsize = 0;
		ib_status = ib_query_ca(h_ca, NULL, &bsize);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %s\n",
				ib_get_err_str(ib_status)) );
			ib_status = IB_ERROR;
			break;
		}

		CL_ASSERT(bsize);


		/* Allocate the memory needed for query_ca */
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			ib_status = IB_INSUFFICIENT_MEMORY;
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		//Get the Active port GUID
		query_portguid = 0x0;
		for(i=0; i< p_ca_attr->num_ports; i++)
		{
			p_port_attr = &p_ca_attr->p_port_attr[i];

			if (p_port_attr->link_state == IB_LINK_ACTIVE)
			{
				query_portguid = p_port_attr->port_guid;
				port_gid = p_port_attr->p_gid_table[0];
				pkey = p_port_attr->p_pkey_table[0];
				break;
			}
		}

		if(query_portguid == 0x0)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("Atlease one port need to be active\n") );
			ib_status = IB_ERROR;
			break;
		}
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("calling ib_query api\n"));


#if 1
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sending IB_QUERY_NODE_REC_BY_NODE_GUID\n"));

		query_req.query_type = IB_QUERY_NODE_REC_BY_NODE_GUID;
		query_req.p_query_input = &p_ca_attr->ca_guid;  //Node GUID
		query_req.port_guid = query_portguid;
		query_req.timeout_ms = 10000; //milliseconds
		query_req.retry_cnt = 3;
		query_req.flags = IB_FLAGS_SYNC;
		query_req.query_context = NULL;
		query_req.pfn_query_cb = alts_query_cb;

		ib_status = ib_query( h_al, &query_req, NULL );

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query api failed with status %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		cl_thread_suspend( 1000 );

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sending IB_QUERY_PATH_REC_BY_PORT_GUIDS\n"));

		query_req.query_type = IB_QUERY_PATH_REC_BY_PORT_GUIDS;
		query_req.p_query_input = &guid_pair;  //Node GUID
		query_req.port_guid = query_portguid;
		query_req.timeout_ms = 10000; //milliseconds
		query_req.retry_cnt = 3;
		query_req.flags = IB_FLAGS_SYNC;
		query_req.query_context = NULL;
		guid_pair.src_guid = query_portguid;
		guid_pair.dest_guid = query_portguid;
		query_req.pfn_query_cb = alts_query_cb;

		ib_status = ib_query( h_al, &query_req, NULL );

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query api failed with status %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		cl_thread_suspend( 1000 );

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sending IB_QUERY_PATH_REC_BY_GIDS\n"));

		query_req.query_type = IB_QUERY_PATH_REC_BY_GIDS;
		query_req.p_query_input = &gid_pair;  //Node GUID
		query_req.port_guid = query_portguid;
		query_req.timeout_ms = 10000; //milliseconds
		query_req.retry_cnt = 3;
		query_req.flags = IB_FLAGS_SYNC;
		query_req.query_context = NULL;
		ib_gid_set_default( &gid_pair.src_gid, query_portguid );
		ib_gid_set_default( &gid_pair.dest_gid, query_portguid );
		query_req.pfn_query_cb = alts_query_cb;

		ib_status = ib_query( h_al, &query_req, NULL );

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query api failed with status %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		cl_thread_suspend( 1000 );

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sending IB_QUERY_PATH_REC_BY_LIDS\n"));

		query_req.query_type = IB_QUERY_PATH_REC_BY_LIDS;
		query_req.p_query_input = &lid_pair;  //Node GUID
		query_req.port_guid = query_portguid;
		query_req.timeout_ms = 10000; //milliseconds
		query_req.retry_cnt = 3;
		query_req.flags = IB_FLAGS_SYNC;
		query_req.query_context = NULL;
		lid_pair.src_lid = p_port_attr->lid;
		lid_pair.dest_lid = p_port_attr->lid;
		query_req.pfn_query_cb = alts_query_cb;

		ib_status = ib_query( h_al, &query_req, NULL );

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query api failed with status %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		cl_thread_suspend( 1000 );

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sending IB_QUERY_USER_DEFINED\n"));

		query_req.query_type = IB_QUERY_USER_DEFINED;
		query_req.p_query_input = &info;  //Node GUID
		query_req.port_guid = query_portguid;
		query_req.timeout_ms = 10000; //milliseconds
		query_req.retry_cnt = 3;
		query_req.flags = IB_FLAGS_SYNC;
		query_req.query_context = NULL;
		info.method = IB_MAD_METHOD_GET;
		info.attr_id = IB_MAD_ATTR_PATH_RECORD;
		info.attr_size = sizeof(ib_path_rec_t);
		info.comp_mask = IB_PR_COMPMASK_DLID | IB_PR_COMPMASK_SLID;// | IB_PR_COMPMASK_NUM_PATH;
		info.p_attr = &path;

		cl_memclr( &path, sizeof(ib_path_rec_t) );
		path.dlid = p_port_attr->lid;
		path.slid = p_port_attr->lid;
		path.num_path = 0x1;
		query_req.pfn_query_cb = alts_query_cb;

		ib_status = ib_query( h_al, &query_req, NULL );

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query api failed with status %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		cl_thread_suspend( 1000 );

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("registering a service with the SA\n"));

		cl_memclr( &reg_svc_req, sizeof( ib_reg_svc_req_t ) );

		reg_svc_req.svc_rec.service_id = 0x52413;
		reg_svc_req.svc_rec.service_gid = port_gid;
		reg_svc_req.svc_rec.service_pkey = pkey;
		reg_svc_req.svc_rec.service_lease = 0xFFFFFFFF;
		//reg_svc_req.svc_rec.service_key[16];
		strcpy_s( (char*)reg_svc_req.svc_rec.service_name, sizeof(reg_svc_req.svc_rec.service_name), "alts" );
		reg_svc_req.svc_data_mask = IB_SR_COMPMASK_SID |
									IB_SR_COMPMASK_SGID |
									IB_SR_COMPMASK_SPKEY |
									IB_SR_COMPMASK_SLEASE |
									IB_SR_COMPMASK_SKEY |
									IB_SR_COMPMASK_SNAME;

		reg_svc_req.port_guid = query_portguid;
		reg_svc_req.timeout_ms = 10000;
		reg_svc_req.retry_cnt = 3;
		reg_svc_req.flags = IB_FLAGS_SYNC;
		reg_svc_req.svc_context = NULL;
		reg_svc_req.pfn_reg_svc_cb = alts_reg_svc_cb;

		ib_status = ib_reg_svc( h_al, &reg_svc_req, &h_reg_svc );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR, 
				("ib_reg_svc api failed with status %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		/*
		 * Note we leave this registration registered
		 * and let ib_close_al clean it up
		 */

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("registering a service with the SA\n"));

		cl_memclr( &reg_svc_req, sizeof( ib_reg_svc_req_t ) );

		reg_svc_req.svc_rec.service_id = 0x52413;
		reg_svc_req.svc_rec.service_gid = port_gid;
		reg_svc_req.svc_rec.service_pkey = pkey;
		reg_svc_req.svc_rec.service_lease = 0xFFFFFFFF;
		//reg_svc_req.svc_rec.service_key[16];
		strcpy_s( (char*)reg_svc_req.svc_rec.service_name, sizeof(reg_svc_req.svc_rec.service_name), "alts" );
		reg_svc_req.svc_data_mask = IB_SR_COMPMASK_SID |
									IB_SR_COMPMASK_SGID |
									IB_SR_COMPMASK_SPKEY |
									IB_SR_COMPMASK_SLEASE |
									IB_SR_COMPMASK_SKEY |
									IB_SR_COMPMASK_SNAME;

		reg_svc_req.port_guid = query_portguid;
		reg_svc_req.timeout_ms = 10000;
		reg_svc_req.retry_cnt = 3;
		reg_svc_req.flags = IB_FLAGS_SYNC;
		reg_svc_req.svc_context = NULL;
		reg_svc_req.pfn_reg_svc_cb = alts_reg_svc_cb;

		ib_status = ib_reg_svc( h_al, &reg_svc_req, &h_reg_svc );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR, 
				("ib_reg_svc api failed with status %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}

		ib_status = ib_dereg_svc( h_reg_svc, ib_sync_destroy );
		if( ib_status != IB_SUCCESS )
		{
			ALTS_PRINT( ALTS_DBG_ERROR, 
				("ib_dereg_svc api failed with status %s\n",
				ib_get_err_str(ib_status)) );
			break;
		}
#endif

	}while (0);

	if( p_ca_attr )
		cl_free( p_ca_attr );

	if( h_al )
		alts_close_al( h_al );

	ALTS_EXIT( ALTS_DBG_VERBOSE);

	return ib_status;

}


void
alts_print_port_guid(
ib_port_info_t	*p_port_info)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_port_info );

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("ib_port_attr_t info:\n"
		"\tsubnet_timeout...:x%x\n"
		"\tlocal_port_num....:x%x\n"
		"\tmtu_smsl.........:x%x\n"
		"\tbase_lid.........:x%x\n",
		p_port_info->subnet_timeout,
		p_port_info->local_port_num,
		p_port_info->mtu_smsl,
		p_port_info->base_lid
		));

	ALTS_EXIT( ALTS_DBG_VERBOSE);

}

void
alts_print_node_info(
	ib_node_info_t *p_node_info
)
{
	UNUSED_PARAM( p_node_info );

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("alts_print_node_info info:\n"
		"\tnode_type...:x%x\n"
		"\tnum_ports....:x%x\n"
		"\tnode_guid.........:x%"PRIx64"\n"
		"\tport_guid.........:x%"PRIx64"\n",
		p_node_info->node_type,
		p_node_info->num_ports,
		p_node_info->node_guid,
		p_node_info->port_guid
		));
}

void
alts_query_cb(
IN ib_query_rec_t	*p_query_rec
)
{
	uint32_t		i;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("ib_query_rec_t info:\n"
		"\tstatus...:x%x\n"
		"\tquery_type...:x%x\n",
		p_query_rec->status,
		p_query_rec->query_type
		));

	if(p_query_rec->status == IB_SUCCESS)
	{
		ib_node_record_t * p_node_rec;

		switch(p_query_rec->query_type)
		{
#if 1
		case IB_QUERY_NODE_REC_BY_NODE_GUID:
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("returning IB_QUERY_NODE_REC_BY_NODE_GUID\n"));
			for( i=0; i<p_query_rec->result_cnt; i++ )
			{
				p_node_rec = (ib_node_record_t *)ib_get_query_result(
					p_query_rec->p_result_mad, i );
				alts_print_node_info(&p_node_rec->node_info);
			}
			break;

		case IB_QUERY_PATH_REC_BY_PORT_GUIDS:
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("returning IB_QUERY_PATH_REC_BY_PORT_GUIDS\n"));
			break;

		case IB_QUERY_PATH_REC_BY_GIDS:
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("returning IB_QUERY_PATH_REC_BY_GIDS\n"));
			break;

		case IB_QUERY_PATH_REC_BY_LIDS:
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("returning IB_QUERY_PATH_REC_BY_LIDS\n"));
			break;

		case IB_QUERY_USER_DEFINED:
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("returning IB_QUERY_USER_DEFINED\n"));
			break;


#endif
			break;

		default:
			break;

		}
	}
	else
	{
		ALTS_PRINT(ALTS_DBG_ERROR,
			("p_query_rec->status failed\n"));
	}

	if( p_query_rec->p_result_mad )
		ib_put_mad( p_query_rec->p_result_mad );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}



void
alts_dereg_svc_cb(
	IN void *context )
{
	UNUSED_PARAM( context );
	ALTS_ENTER( ALTS_DBG_VERBOSE );
	ALTS_PRINT( ALTS_DBG_VERBOSE, ("ib_dereg_svc done\n") );
	ALTS_EXIT( ALTS_DBG_VERBOSE);
}


void
alts_reg_svc_cb(
	IN ib_reg_svc_rec_t	*p_reg_svc_rec )
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

#if defined( CL_KERNEL ) && !defined( _DEBUG_ )
	UNUSED_PARAM( p_reg_svc_rec );
#endif

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("ib_reg_svc_rec_t info:\n"
		"\treq_status...:x%x\n"
		"\tresp_status...:x%x\n",
		p_reg_svc_rec->req_status,
		p_reg_svc_rec->resp_status
		));
	
	ALTS_EXIT( ALTS_DBG_VERBOSE);
}
