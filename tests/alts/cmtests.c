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
 *	mad test does a data transfer between two queue pairs created one
 *	on each port of the hca. In order for this test to work, two ports of the hca
 *	should be connected in a loop back and must be configured to ACTIVE PORT STATE.
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

/* Parameters */
#define MAX_QPS 8
#define SRC_QP 0
#define DEST_QP 1


#pragma warning(disable:4324)
typedef struct _alts_cm_ca_obj
{
	ib_api_status_t		status;
	ib_qp_type_t		test_type;
	ib_pfn_comp_cb_t	pfn_comp_cb;

	ib_ca_handle_t		h_ca;
	ib_ca_attr_t		*p_ca_attr;
	ib_port_attr_t		*p_src_port_attr;
	ib_port_attr_t		*p_dest_port_attr;

	ib_net32_t			src_qp_num;
	ib_net32_t			dest_qp_num;

	ib_net64_t			src_portguid;
	uint8_t				src_port_num;

	ib_net64_t			dest_portguid;
	uint8_t				dest_port_num;

	ib_net16_t			slid;
	ib_net16_t			dlid;

	ib_pool_key_t		h_src_pool;
	ib_pool_key_t		h_dest_pool;

	ib_mad_svc_handle_t	h_src_mad_svc;
	ib_mad_svc_handle_t	h_dest_mad_svc;

	boolean_t			num_cq;
	ib_cq_handle_t		h_cq;
	ib_cq_handle_t		h_cq_alt;
	uint32_t			cq_size;

	ib_pd_handle_t		h_pd;

	ib_qp_handle_t		h_qp[MAX_QPS];
	uint32_t			qkey;

	ib_qp_attr_t		qp_attr[MAX_QPS];

	ib_send_wr_t		*p_send_wr;
	ib_recv_wr_t		*p_recv_wr;
	size_t				wr_send_size;
	size_t				wr_recv_size;
	uint32_t			num_wrs;
	uint32_t			ds_list_depth;
	uint32_t			msg_size; // Initialize this field

	ib_av_handle_t		h_av_src;
	ib_av_handle_t		h_av_dest;

	uint32_t			send_done;
	uint32_t			recv_done;
	uint32_t			cq_done;		// total completions
	boolean_t			is_src;

	boolean_t			is_loopback;
	boolean_t			reply_requested;
	boolean_t			rdma_enabled;

	// cm stuff
	ib_path_rec_t		path_src;
	ib_path_rec_t		path_dest;

	ib_cm_req_t			req_src;
	ib_cm_req_t			req_dest;

	ib_cm_listen_t		listen;
	ib_listen_handle_t	h_cm_listen;

	ib_cm_rep_t			rep_dest;

	ib_cm_rtu_t			rtu_src;

	uint32_t			cm_cbs;
	uint32_t			cm_errs;

	ib_cm_dreq_t		dreq_src;
	ib_cm_drep_t		drep_dest;

	cl_event_t			mra_event;
	boolean_t			mra_test;
	boolean_t			rej_test;

	ib_cm_handle_t		h_cm_req;

	cl_event_t			destroy_event;

	boolean_t			handoff;
	ib_listen_handle_t	h_cm_listen_handoff;
	ib_net64_t			handoff_svc_id;

	mem_region_t		mem_region[10];

} alts_cm_ca_obj_t;
#pragma warning(default:4324)

#define MAX_SERVER	500

typedef struct _alts_serv_object
{
	alts_cm_ca_obj_t	alts_obj;

	ib_cq_handle_t		h_cq[MAX_SERVER];
	ib_qp_handle_t		h_qp[MAX_SERVER];

}	alts_serv_object_t;


typedef struct	_alts_rmda
{
	char		msg_type;
	uint64_t	vaddr;
	ib_net32_t	rkey;
	char		msg[32];
} alts_rdma_t;

/*
 * Function Prototypes
 */
ib_api_status_t
alts_cm_activate_qp(
	alts_cm_ca_obj_t *p_ca_obj,
	ib_qp_handle_t h_qp );

ib_api_status_t
alts_cm_check_active_ports(
	alts_cm_ca_obj_t *p_ca_obj );

ib_api_status_t
alts_cm_destroy_resources(
	alts_cm_ca_obj_t *p_ca_obj );

ib_api_status_t
alts_rc_deregister_mem(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index );

ib_api_status_t
cm_post_sends(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			num_posts );

void
__mra_thread(
	IN	void*	context );

/*
 * QP Error callback function
 */
ib_api_status_t
alts_cm_rc_tests (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_cm_rc_rej_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_cm_handoff_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_cm_rc_flush_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_rc_no_cm_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_cm_rc_rdma_tests (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_rc_mra_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_cm_uc_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_cm_sidr_tests (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

#define ALTS_TEST_MGMT_CLASS	0x56
#define ALTS_TEST_MGMT_CLASS_VER 1

/*
 * Gloabal Variables
 */
ib_al_handle_t		h_al;
ib_dgrm_info_t		dgrm_info;
ib_mad_svc_t		mad_svc;
ib_send_wr_t		send_wr;
ib_local_ds_t		send_ds;
ib_recv_wr_t		recv_wr;
ib_local_ds_t		recv_ds;
alts_cm_ca_obj_t	*gp_ca_obj;

extern ib_cq_create_t		cq_create_attr;
extern ib_qp_create_t		qp_create_attr;
extern ib_av_attr_t			av_attr;
extern ib_wc_t				free_wclist;
extern ib_wc_t				free_wcl;

ib_api_status_t		cm_client_server=IB_NOT_FOUND;
ib_api_status_t		cm_client_server_rej=IB_NOT_FOUND;
ib_api_status_t		cm_client_server_flush=IB_NOT_FOUND;
ib_api_status_t		cm_client_server_no_cm=IB_NOT_FOUND;
ib_api_status_t		cm_rdma=IB_NOT_FOUND;
ib_api_status_t		cm_mra=IB_NOT_FOUND;
ib_api_status_t		cm_uc=IB_NOT_FOUND;
ib_api_status_t		cm_sidr=IB_NOT_FOUND;
ib_api_status_t		cm_handoff=IB_NOT_FOUND;

/*	This test case assumes that the HCA has 2 port connected
 *  through the switch. Sends packets from lower port number to higher
 *  port number.
 */
ib_api_status_t
al_test_cm(void)
{
	ib_api_status_t		ib_status = IB_ERROR;
	ib_ca_handle_t		h_ca = NULL;
	uint32_t			bsize;         
	ib_ca_attr_t		*p_ca_attr = NULL;
	//alts_cm_ca_obj_t	ca_obj;	// for testing stack

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	do
	{
		/*
		 * Open the AL interface
		 */
		h_al = NULL;
		ib_status = alts_open_al(&h_al);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_open_al failed status = %s\n",
				ib_get_err_str( ib_status )) );
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
				ib_get_err_str( ib_status )) );
			break;
		}

		/*
		 * Get the CA Attributest
		 * Check for two active ports
		 */

		/*
		 * Query the CA
		 */
		bsize = 0;
		ib_status = ib_query_ca(h_ca, NULL, &bsize);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %s\n",
				ib_get_err_str( ib_status )) );
			break;
		}

		CL_ASSERT(bsize);

		cm_client_server = alts_cm_rc_tests(h_ca, bsize);
		if(cm_client_server != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_rc_tests() failed with status %s\n",
			ib_get_err_str( cm_client_server )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_cm_rc_tests() passed\n"));

		cm_client_server_rej = alts_cm_rc_rej_test(h_ca, bsize);
		if(cm_client_server_rej != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_rc_rej_test() failed with status %s\n",
			ib_get_err_str( cm_client_server_rej )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_cm_rc_rej_test() passed\n"));

		cm_client_server_flush = alts_cm_rc_flush_test(h_ca, bsize);
		if(cm_client_server_flush != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_rc_flush_test() failed with status %s\n",
			ib_get_err_str( cm_client_server_flush )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_cm_rc_flush_test() passed\n"));

		cm_client_server_no_cm = alts_rc_no_cm_test(h_ca, bsize);
		if(cm_client_server_no_cm != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_rc_no_cm_test() failed with status %s\n",
			ib_get_err_str( cm_client_server_no_cm )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_rc_no_cm_test() passed\n"));

		cm_rdma = alts_cm_rc_rdma_tests(h_ca, bsize);
		if(cm_rdma != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_rc_rdma_tests() failed with status %s\n",
			ib_get_err_str( cm_rdma )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_cm_rc_rdma_tests() passed\n"));

		cm_mra = alts_rc_mra_test(h_ca, bsize);
		if(cm_mra != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_rc_mra_test() failed with status %s\n",
			ib_get_err_str( cm_mra )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_rc_mra_test() passed\n"));

		cm_handoff = alts_cm_handoff_test(h_ca, bsize);
		if(cm_handoff != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_handoff_test() failed with status %s\n",
			ib_get_err_str( cm_handoff )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_cm_handoff_test() passed\n"));

		cm_uc = alts_cm_uc_test(h_ca, bsize);
		if(cm_uc != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_uc_test() failed with status %s\n",
			ib_get_err_str( cm_uc )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_cm_uc_test() passed\n"));

		cm_sidr = alts_cm_sidr_tests(h_ca, bsize);
		if(cm_sidr != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_sidr_tests() failed with status %s\n",
			ib_get_err_str( cm_sidr )) );
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_cm_sidr_tests() passed\n"));

	} while (0);

	/*
	 * Destroy the resources
	 */
	if (p_ca_attr)
		cl_free(p_ca_attr);

	ALTS_PRINT(ALTS_DBG_STATUS,
		("Test results (cm):\n"
		"\trc client server......: %s\n"
		"\trc reject.............: %s\n"
		"\tqp flush on disconnect: %s\n"
		"\trc no cm..............: %s\n"
		"\trmda..................: %s\n"
		"\tmra...................: %s\n"
		"\thandoff...............: %s\n"
		"\tuc....................: %s\n"
		"\tsidr..................: %s\n",
		ib_get_err_str(cm_client_server),
		ib_get_err_str(cm_client_server_rej),
		ib_get_err_str(cm_client_server_flush),
		ib_get_err_str(cm_client_server_no_cm),
		ib_get_err_str(cm_rdma),
		ib_get_err_str(cm_mra),
		ib_get_err_str(cm_handoff),
		ib_get_err_str(cm_uc),
		ib_get_err_str(cm_sidr)
		));

	if( h_al )
		alts_close_al(h_al);

	ALTS_EXIT( ALTS_DBG_VERBOSE);

	return ib_status;

}

static void
__alts_cm_destroy_pd_cb(
	IN				void						*context )
{
	cl_event_signal( &((alts_cm_ca_obj_t*)context)->destroy_event );
}

ib_api_status_t
alts_cm_destroy_resources(
	alts_cm_ca_obj_t *p_ca_obj)
{
	uint32_t		i, j;

	/*
	 * Destroy Send QP, Recv QP, CQ and PD
	 */
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if (p_ca_obj->h_qp[SRC_QP])
	{
		ib_status = ib_destroy_qp(p_ca_obj->h_qp[SRC_QP], NULL);
	}

	if (p_ca_obj->is_loopback != TRUE)
	{
		if (p_ca_obj->h_qp[DEST_QP])
		{
			ib_status = ib_destroy_qp(p_ca_obj->h_qp[DEST_QP], NULL);
		}
	}

	if (p_ca_obj->h_cq)
		ib_status = ib_destroy_cq(p_ca_obj->h_cq, NULL);
	if (p_ca_obj->h_cq_alt)
		ib_status = ib_destroy_cq(p_ca_obj->h_cq_alt, NULL);

	// deregister mem
	for (i=0; i < p_ca_obj->num_wrs; i++)
	{
		alts_rc_deregister_mem(p_ca_obj, i);
	}

	// send
	for (j=i; j < i + p_ca_obj->num_wrs; j++)
	{
		ib_status = alts_rc_deregister_mem(p_ca_obj, j);
	}

	if (p_ca_obj->h_pd)
	{
		ib_status = ib_dealloc_pd(p_ca_obj->h_pd,__alts_cm_destroy_pd_cb);
		cl_event_wait_on( &p_ca_obj->destroy_event, EVENT_NO_TIMEOUT, FALSE );
	}

	//cl_thread_suspend( 1000 );
	cl_event_destroy( &p_ca_obj->destroy_event );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


ib_api_status_t
alts_sidr_message_passing(
	alts_cm_ca_obj_t *p_ca_obj)
{
	ib_api_status_t				ib_status = IB_SUCCESS;
	ib_mad_element_t			*p_mad_element;
	ib_mad_t					*p_mad;
	char						*p_buf;
	uint32_t					i;

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	//Create an Address vector
	av_attr.dlid = p_ca_obj->dlid;
	av_attr.port_num = p_ca_obj->src_port_num;
	av_attr.sl = 0;
	av_attr.path_bits = 0;
	av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
	av_attr.grh_valid = FALSE;

	ib_status = ib_create_av(p_ca_obj->h_pd,&av_attr,&p_ca_obj->h_av_src);
	if(ib_status != IB_SUCCESS)
		return ib_status;

	p_ca_obj->send_done = 0;
	p_ca_obj->recv_done = 0;
	p_ca_obj->cq_done = 0;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("++++++ dlid(x%x) src_port(%d) ====\n",
		av_attr.dlid, av_attr.port_num));

	for (i=0; i<p_ca_obj->num_wrs; i++)
	{
		p_mad_element = NULL;
		ib_status = ib_get_mad(
			p_ca_obj->h_src_pool,
			MAD_BLOCK_SIZE,
			&p_mad_element );
		if (ib_status != IB_SUCCESS)
		{
			ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
				("Error in ib_get_mad()! %s\n",	ib_get_err_str(ib_status)));
			return (ib_status);
		}

		// format mad
		p_mad_element->context1 = (void *)1;
		p_mad_element->context2 = p_ca_obj;

		/* Send request information. */
		p_mad_element->h_av = p_ca_obj->h_av_src;
		p_mad_element->send_opt = IB_SEND_OPT_SIGNALED;


		if (p_ca_obj->reply_requested == TRUE)
			p_mad_element->resp_expected = TRUE;
		else
			p_mad_element->resp_expected = FALSE;	//TRUE;

		p_mad_element->remote_qp = p_ca_obj->qp_attr[DEST_QP].num;

		p_mad_element->remote_qkey = p_ca_obj->qkey;
		p_mad_element->timeout_ms = 10;
		p_mad_element->retry_cnt = 1;

		/* Completion information. */
		p_mad_element->status = 0;

		// format mad
		p_mad = p_mad_element->p_mad_buf;

		p_buf = (char *)p_mad;
		cl_memset(p_buf, 0x66, 256);		// set pattern in buffer


		switch (p_ca_obj->qp_attr[SRC_QP].num)
		{
		case IB_QP0:
			ib_mad_init_new(
				p_mad,
				IB_MCLASS_SUBN_LID,
				ALTS_TEST_MGMT_CLASS_VER,
				IB_MAD_METHOD_GET,
				(ib_net64_t) CL_NTOH64(0x666),
				IB_MAD_ATTR_SM_INFO,
				0 );
			break;

		case IB_QP1:
		default:
			ib_mad_init_new(
				p_mad,
				ALTS_TEST_MGMT_CLASS,
				ALTS_TEST_MGMT_CLASS_VER,
				IB_MAD_METHOD_GET,
				(ib_net64_t) CL_NTOH64(0x666),
				IB_MAD_ATTR_CLASS_PORT_INFO,
				0 );
			break;
		}

		// send
		ib_status = ib_send_mad(
			p_ca_obj->h_src_mad_svc,
			p_mad_element,
			NULL );

		if(ib_status != IB_SUCCESS)
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("ib_send_mad failed\n"));

		//cl_thread_suspend(10); // 10 usec
	}

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("sleeping for awhile ...\n"));

	cl_thread_suspend(10000); // 10 seconds

	if (((!p_ca_obj->cq_done) && (p_ca_obj->num_wrs> 2)) ||
		(p_ca_obj->cq_done != p_ca_obj->num_wrs*2))
	{
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sleeping for awhile ...\n"));
		cl_thread_suspend(10000); // 10 seconds
	}


	if (((!p_ca_obj->cq_done) && (p_ca_obj->num_wrs> 2)) ||
		(p_ca_obj->cq_done != p_ca_obj->num_wrs*2))
	{
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sleeping for awhile ...\n"));
		cl_thread_suspend(10000); // 10 seconds
	}

	if (((!p_ca_obj->cq_done) && (p_ca_obj->num_wrs> 2)) ||
		(p_ca_obj->cq_done != p_ca_obj->num_wrs*2))
	{
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sleeping for awhile ...\n"));
		cl_thread_suspend(10000); // 10 seconds
	}


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
cm_post_sends(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			num_posts )
{
	ib_send_wr_t *p_s_wr, *p_send_failure_wr;
	uint32_t msg_size, i;
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if (p_ca_obj->test_type == IB_QPT_UNRELIABLE_DGRM)
		msg_size = p_ca_obj->msg_size - sizeof(ib_grh_t);
	else
		msg_size = 64;

	//msg_size = p_ca_obj->msg_size;

	msg_size = 64;

	p_s_wr = p_ca_obj->p_send_wr;

	p_s_wr->p_next = NULL;
	p_s_wr->ds_array[0].length = msg_size;
	p_s_wr->num_ds = 1;

	p_s_wr->wr_type = WR_SEND;

	if (p_ca_obj->test_type == IB_QPT_UNRELIABLE_DGRM)
	{
		p_s_wr->dgrm.ud.h_av = p_ca_obj->h_av_src;
		p_s_wr->send_opt = IB_SEND_OPT_SIGNALED | IB_SEND_OPT_SOLICITED;
//		p_s_wr->send_opt = IB_SEND_OPT_IMMEDIATE |

		p_s_wr->dgrm.ud.remote_qkey = p_ca_obj->qkey;
		p_s_wr->dgrm.ud.remote_qp = p_ca_obj->qp_attr[DEST_QP].num;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("======= qkey(x%x) qp_num(x%x) ========\n",
			p_s_wr->dgrm.ud.remote_qkey,
			p_s_wr->dgrm.ud.remote_qp));

	}
	else if ( (p_ca_obj->test_type == IB_QPT_RELIABLE_CONN) ||
		(p_ca_obj->test_type == IB_QPT_UNRELIABLE_CONN) )
	{
		p_s_wr->send_opt = IB_SEND_OPT_SIGNALED | IB_SEND_OPT_SOLICITED;
//		p_s_wr->send_opt = IB_SEND_OPT_IMMEDIATE |

		/*
		p_s_wr->send_opt = IB_SEND_OPT_SIGNALED |	\
							IB_SEND_OPT_IMMEDIATE |	\
							IB_SEND_OPT_SOLICITED ;*/

	}


	for (i = 0; i < num_posts; i++)
	{
		p_s_wr->wr_id = i+reg_index;
		p_s_wr->immediate_data = 0xfeedde00 + i;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("******vaddr(x%"PRIx64") lkey(x%x) len(%d)*****\n",
			(void*)(uintn_t)p_s_wr->ds_array[0].vaddr,
			p_s_wr->ds_array[0].lkey,
			p_s_wr->ds_array[0].length));

		ib_status = ib_post_send(
			p_ca_obj->h_qp[SRC_QP],
			p_s_wr,
			&p_send_failure_wr);

	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return	ib_status;
}


ib_api_status_t
alts_cm_check_active_ports(alts_cm_ca_obj_t *p_ca_obj)
{
	ib_api_status_t ib_status;
	ib_ca_attr_t	*p_ca_attr;
	ib_port_attr_t	*p_src_port_attr = NULL;
	ib_port_attr_t	*p_dest_port_attr = NULL;
	uint32_t		i;
	ib_port_attr_t	*p_port_attr;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT(p_ca_obj);

	p_ca_attr = p_ca_obj->p_ca_attr;

	CL_ASSERT(p_ca_attr);

	for(i=0; i< p_ca_attr->num_ports; i++)
	{
		p_port_attr = &p_ca_attr->p_port_attr[i];

		if (p_port_attr->link_state == IB_LINK_ACTIVE)
		{
			if (p_src_port_attr == NULL)
				p_src_port_attr = p_port_attr;
			else
			if(p_dest_port_attr == NULL)
				p_dest_port_attr = p_port_attr;
			else
				break;
		}
	}

	// handle loopback case
	if (p_ca_obj->is_loopback == TRUE)
		p_dest_port_attr = p_src_port_attr;

	if (p_src_port_attr && p_dest_port_attr)
	{
		p_ca_obj->p_dest_port_attr = p_dest_port_attr;
		p_ca_obj->p_src_port_attr = p_src_port_attr;

		p_ca_obj->dlid = p_dest_port_attr->lid;
		p_ca_obj->slid = p_src_port_attr->lid;

		p_ca_obj->dest_portguid = p_dest_port_attr->port_guid;
		p_ca_obj->src_portguid = p_src_port_attr->port_guid;

		p_ca_obj->dest_port_num = p_dest_port_attr->port_num;
		p_ca_obj->src_port_num = p_src_port_attr->port_num;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("**** slid = x%x (x%x) ***dlid = x%x (x%x) ***************\n",
			p_ca_obj->slid,
			CL_NTOH16(p_ca_obj->slid),
			p_ca_obj->dlid,
			CL_NTOH16(p_ca_obj->dlid) ));

		ib_status = IB_SUCCESS;

	}
	else
	{

		ib_status = IB_ERROR;
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

void
rc_cm_cq_comp_cb(
	void			*cq_context,
	ib_qp_type_t	qp_type
	)
{
	ib_api_status_t ib_status;
	uint32_t i = 0, id;
	ib_wc_t *p_free_wcl, *p_done_cl= NULL;
	alts_cm_ca_obj_t *p_ca_obj;
	ib_cq_handle_t	h_cq;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT(cq_context);

	h_cq = *((ib_cq_handle_t*)cq_context);
	p_ca_obj = gp_ca_obj;

	ib_status = ib_rearm_cq(h_cq, FALSE);

	p_free_wcl = &free_wcl;
	p_free_wcl->p_next = NULL;
	p_done_cl = NULL;

	ib_status = ib_poll_cq(h_cq, &p_free_wcl, &p_done_cl);

	while(p_done_cl)
	{

		/*
		 *  print output
		 */
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("Got a completion:\n"
			"\ttype....:%s\n"
			"\twr_id...:%"PRIx64"\n"
			"status....:%s\n",
			ib_get_wc_type_str(p_done_cl->wc_type),
			p_done_cl->wr_id,
			ib_get_wc_status_str(p_done_cl->status) ));


		if (p_done_cl->wc_type == IB_WC_RECV)
		{
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("message length..:%d bytes\n",
				p_done_cl->length ));

			id = (uint32_t)p_done_cl->wr_id;

			if (qp_type == IB_QPT_UNRELIABLE_DGRM)
			{
				ALTS_PRINT(ALTS_DBG_VERBOSE,
					("RecvUD info:\n"
					"\trecv_opt...:x%x\n"
					"\timm_data...:x%x\n"
					"\tremote_qp..:x%x\n"
					"\tpkey_index.:%d\n"
					"\tremote_lid.:x%x\n"
					"\tremote_sl..:x%x\n"
					"\tpath_bits..:x%x\n",
					p_done_cl->recv.ud.recv_opt,
					p_done_cl->recv.ud.immediate_data,
					CL_NTOH32(p_done_cl->recv.ud.remote_qp),
					p_done_cl->recv.ud.pkey_index,
					CL_NTOH16(p_done_cl->recv.ud.remote_lid),
					p_done_cl->recv.ud.remote_sl,
					p_done_cl->recv.ud.path_bits));
			}
			else
			{
				ALTS_PRINT(ALTS_DBG_VERBOSE,
					("RecvRC info:\n"
					"\trecv_opt...:x%x\n"
					"\timm_data...:x%x\n",
					p_done_cl->recv.conn.recv_opt,
					p_done_cl->recv.ud.immediate_data ));
			}

		}

		p_free_wcl = p_done_cl;
		p_free_wcl->p_next = NULL;
		p_done_cl = NULL;
		i++;

		ib_status = ib_poll_cq(h_cq, &p_free_wcl, &p_done_cl);
	}

	p_ca_obj->cq_done += i;

	ALTS_PRINT( ALTS_DBG_INFO,
		("Number of items polled from CQ (in callback=%d) (total=%d)\n",
		i,
		p_ca_obj->cq_done) );


	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

ib_api_status_t
rc_rdma_write_send(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index )
{
	ib_send_wr_t *p_s_wr, *p_send_failure_wr;
	uint32_t msg_size;
	ib_api_status_t ib_status = IB_SUCCESS;
	alts_rdma_t	*p_rdma_req;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	msg_size = 64;
	//msg_size = p_ca_obj->msg_size;

	p_s_wr = p_ca_obj->p_send_wr;

	p_s_wr->p_next = NULL;
	p_s_wr->ds_array[0].length = msg_size;
	p_s_wr->num_ds = 1;

	p_s_wr->wr_type = WR_RDMA_WRITE;

	p_s_wr->send_opt =	IB_SEND_OPT_SOLICITED ;
	//IB_SEND_OPT_IMMEDIATE |
	p_s_wr->send_opt =	IB_SEND_OPT_SIGNALED;

	p_s_wr->ds_array[0].vaddr =
		(uintn_t)p_ca_obj->mem_region[reg_index].buffer;
	p_s_wr->ds_array[0].lkey = p_ca_obj->mem_region[reg_index].lkey;

	p_s_wr->wr_id = reg_index;
	p_s_wr->immediate_data = 0xfeedde00 + reg_index;

	p_rdma_req = (alts_rdma_t*)p_ca_obj->mem_region[reg_index].buffer;
	p_s_wr->remote_ops.vaddr = p_rdma_req->vaddr;
	p_s_wr->remote_ops.rkey = p_rdma_req->rkey;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("******vaddr(x%"PRIx64") lkey(x%x) len(%d)*****\n",
		(void*)(uintn_t)p_s_wr->ds_array[0].vaddr,
		p_s_wr->ds_array[0].lkey,
		p_s_wr->ds_array[0].length));

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("******remote:vaddr(x%"PRIx64") rkey(x%x) len(%d)*****\n",
		p_s_wr->remote_ops.vaddr,
		p_s_wr->remote_ops.rkey,
		p_s_wr->ds_array[0].length));

	ib_status = ib_post_send(
		p_ca_obj->h_qp[DEST_QP],
		p_s_wr,
		&p_send_failure_wr);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return	ib_status;
}

ib_api_status_t
rc_rdma_read_send(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index )
{
	ib_send_wr_t *p_s_wr, *p_send_failure_wr;
	uint32_t msg_size;
	ib_api_status_t ib_status = IB_SUCCESS;
	alts_rdma_t	*p_rdma_req;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	msg_size = 64;
	//msg_size = p_ca_obj->msg_size;

	p_s_wr = p_ca_obj->p_send_wr;

	p_s_wr->p_next = NULL;
	p_s_wr->ds_array[0].length = msg_size;
	p_s_wr->num_ds = 1;

	p_s_wr->wr_type = WR_RDMA_READ;

	//p_s_wr->send_opt =	IB_SEND_OPT_SOLICITED ;
	//IB_SEND_OPT_IMMEDIATE |
	p_s_wr->send_opt =	IB_SEND_OPT_SIGNALED;

	p_s_wr->ds_array[0].vaddr =
		(uintn_t)p_ca_obj->mem_region[reg_index].buffer;
	p_s_wr->ds_array[0].lkey = p_ca_obj->mem_region[reg_index].lkey;

	p_s_wr->wr_id = reg_index;
	p_s_wr->immediate_data = 0xfeedde00 + reg_index;

	p_rdma_req = (alts_rdma_t*)p_ca_obj->mem_region[reg_index].buffer;
	cl_memclr( p_rdma_req->msg, 64 );
	p_s_wr->remote_ops.vaddr = p_rdma_req->vaddr;
	p_s_wr->remote_ops.rkey = p_rdma_req->rkey;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("******vaddr(x%"PRIx64") lkey(x%x) len(%d)*****\n",
		(void*)(uintn_t)p_s_wr->ds_array[0].vaddr,
		p_s_wr->ds_array[0].lkey,
		p_s_wr->ds_array[0].length));

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("******remote:vaddr(x%"PRIx64") rkey(x%x) len(%d)*****\n",
		p_s_wr->remote_ops.vaddr,
		p_s_wr->remote_ops.rkey,
		p_s_wr->ds_array[0].length));

	ib_status = ib_post_send(
		p_ca_obj->h_qp[DEST_QP],
		p_s_wr,
		&p_send_failure_wr);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return	ib_status;
}

void
process_response(
	IN	alts_cm_ca_obj_t	*p_ca_obj,
	IN	alts_rdma_t			*p_data,
	IN	uint32_t			reg_index )
{

	switch(p_data->msg_type)
	{
	case 'W':
		ALTS_PRINT( ALTS_DBG_INFO, ("RDMA Write requested\n" ) );
		p_data->msg_type = 'C';
		rc_rdma_write_send( p_ca_obj, reg_index );
		break;
	case 'R':
		ALTS_PRINT( ALTS_DBG_INFO, ("RDMA Read requested\n" ) );
		p_data->msg_type = 'C';
		rc_rdma_read_send( p_ca_obj, reg_index );
		break;

	case 'C':
		ALTS_PRINT( ALTS_DBG_INFO, ("Msg completed. [%s]\n",
			p_data->msg ) );
		break;

	default:
		ALTS_PRINT(ALTS_DBG_ERROR, ("Bad RDMA msg!!!\n"));
		break;
	}


}

void
rdma_cq_comp_cb(
	void			*cq_context,
	ib_qp_type_t	qp_type
	)
{
	ib_api_status_t ib_status;
	uint32_t i = 0, id;
	ib_wc_t *p_free_wcl, *p_done_cl= NULL;
	alts_cm_ca_obj_t *p_ca_obj;
	ib_cq_handle_t	h_cq;
	alts_rdma_t		*p_data;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( qp_type );

	CL_ASSERT(cq_context);

	h_cq = *((ib_cq_handle_t*)cq_context);
	p_ca_obj = gp_ca_obj;

	ib_status = ib_rearm_cq(h_cq, FALSE);

	p_free_wcl = &free_wcl;
	p_free_wcl->p_next = NULL;
	p_done_cl = NULL;

	ib_status = ib_poll_cq(h_cq, &p_free_wcl, &p_done_cl);

	while(p_done_cl)
	{
		/*
		 *  print output
		 */
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("Got a completion:\n"
			"\ttype....:%s\n"
			"\twr_id...:%"PRIx64"\n"
			"status....:%s\n",
			ib_get_wc_type_str(p_done_cl->wc_type),
			p_done_cl->wr_id,
			ib_get_wc_status_str(p_done_cl->status) ));

		if( p_done_cl->status == IB_WCS_SUCCESS )
		{
			if (p_done_cl->wc_type == IB_WC_RECV)
			{
				ALTS_PRINT(ALTS_DBG_VERBOSE,
					("message length..:%d bytes\n",
					p_done_cl->length ));

				id = (uint32_t)p_done_cl->wr_id;

				ALTS_PRINT(ALTS_DBG_VERBOSE,
					("RecvRC info:\n"
					"\trecv_opt...:x%x\n"
					"\timm_data...:x%x\n",
					p_done_cl->recv.conn.recv_opt,
					p_done_cl->recv.ud.immediate_data ));

				if( p_ca_obj->rdma_enabled == TRUE )
				{
					process_response( p_ca_obj,
					(alts_rdma_t*)p_ca_obj->mem_region[p_done_cl->wr_id].buffer,
					(uint32_t)p_done_cl->wr_id );
				}
			}
			else
			if (p_done_cl->wc_type == IB_WC_RDMA_WRITE)
			{
				// convert request to read now
				p_data =
					(alts_rdma_t*)p_ca_obj->mem_region[p_done_cl->wr_id].buffer;
				p_data->msg_type = 'R';
				process_response( p_ca_obj,
					p_data,
					(uint32_t)p_done_cl->wr_id );
			}
			else
			if (p_done_cl->wc_type == IB_WC_RDMA_READ)
			{
				id = (uint32_t)p_done_cl->wr_id;
				process_response( p_ca_obj,
					(alts_rdma_t*)p_ca_obj->mem_region[p_done_cl->wr_id].buffer,
					(uint32_t)p_done_cl->wr_id );
			}
		}

		p_free_wcl = p_done_cl;
		p_free_wcl->p_next = NULL;
		p_done_cl = NULL;
		i++;

		ib_status = ib_poll_cq(h_cq, &p_free_wcl, &p_done_cl);
	}

	p_ca_obj->cq_done += i;

	ALTS_PRINT( ALTS_DBG_INFO,
		("Number of items polled from CQ (in callback=%d) (total=%d)\n",
		i,
		p_ca_obj->cq_done) );


	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
cm_rc_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	UNUSED_PARAM( h_cq );
	rc_cm_cq_comp_cb (cq_context, IB_QPT_RELIABLE_CONN);
}

void
cm_ud_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	UNUSED_PARAM( h_cq );
	rc_cm_cq_comp_cb(cq_context, IB_QPT_UNRELIABLE_DGRM);
}

void
cm_rdma_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	UNUSED_PARAM( h_cq );
	rdma_cq_comp_cb(cq_context, IB_QPT_RELIABLE_CONN);
}

void
rc_cm_cq_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
rc_cm_qp_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
rc_cm_qp_destroy_cb(
	void	*context
	)
{
	/*
	* QP destroy call back
	*/
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( context );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

ib_api_status_t
alts_create_test_resources( alts_cm_ca_obj_t *p_ca_obj )
{
	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	gp_ca_obj = p_ca_obj;

	cl_event_init( &p_ca_obj->destroy_event, FALSE );

	cl_memclr(&qp_create_attr, sizeof(ib_qp_create_t));

	/*
	 * Allocate a PD
	 */
	ib_status = ib_alloc_pd(
		p_ca_obj->h_ca,
		IB_PDT_NORMAL,
		p_ca_obj, //pd_context
		&p_ca_obj->h_pd);

	CL_ASSERT(ib_status == IB_SUCCESS);

	/*
	 * Create CQ Attributes
	 */
	cq_create_attr.size = p_ca_obj->cq_size;
	cq_create_attr.pfn_comp_cb = p_ca_obj->pfn_comp_cb;
	cq_create_attr.h_wait_obj = NULL;

	if( p_ca_obj->rdma_enabled )
	{
		cq_create_attr.pfn_comp_cb = cm_rdma_cq_comp_cb;
	}

	ib_status = ib_create_cq(
		p_ca_obj->h_ca,
		&cq_create_attr,
		&p_ca_obj->h_cq,
		rc_cm_cq_err_cb,
		&p_ca_obj->h_cq );
	CL_ASSERT(ib_status == IB_SUCCESS);


	p_ca_obj->cq_size = cq_create_attr.size;

	if( p_ca_obj->num_cq > 1 )
	{
		ib_status = ib_create_cq(
			p_ca_obj->h_ca,
			&cq_create_attr,
			&p_ca_obj->h_cq_alt,
			rc_cm_cq_err_cb,
			&p_ca_obj->h_cq_alt );

		CL_ASSERT(ib_status == IB_SUCCESS);
		CL_ASSERT(p_ca_obj->cq_size == cq_create_attr.size);
	}

	/*
	 * Create QP Attributes
	 */
	qp_create_attr.sq_depth	= p_ca_obj->num_wrs;
	qp_create_attr.rq_depth	= p_ca_obj->num_wrs;
	qp_create_attr.sq_sge	= 1;
	qp_create_attr.rq_sge	= 1;

	if( p_ca_obj->num_cq > 1 )
		qp_create_attr.h_sq_cq	= p_ca_obj->h_cq_alt;
	else
		qp_create_attr.h_sq_cq	= p_ca_obj->h_cq;

	qp_create_attr.h_rq_cq	= p_ca_obj->h_cq;

	qp_create_attr.sq_signaled = TRUE;
	//qp_create_attr.sq_signaled = FALSE;

	qp_create_attr.qp_type = p_ca_obj->test_type;

	ib_status = ib_create_qp(
		p_ca_obj->h_pd,
		&qp_create_attr,
		p_ca_obj,
		rc_cm_qp_err_cb,
		&p_ca_obj->h_qp[SRC_QP]);

	if (ib_status != IB_SUCCESS)
	{
		ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
			("Error in ib_create_qp()! %s\n",
			ib_get_err_str(ib_status)));

		return (ib_status);
	}

	ib_status = ib_query_qp(p_ca_obj->h_qp[SRC_QP],
						&p_ca_obj->qp_attr[SRC_QP]);

	if (ib_status != IB_SUCCESS)
	{
		ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
			("Error in query_qp()! %s\n",
			ib_get_err_str(ib_status)));

		ib_destroy_qp(p_ca_obj->h_qp[SRC_QP],rc_cm_qp_destroy_cb);
		return (ib_status);
	}

	if (p_ca_obj->is_loopback == TRUE)
	{
		// do loopback on same QP
		p_ca_obj->h_qp[DEST_QP] = p_ca_obj->h_qp[SRC_QP];
	}
	else
	{
		ib_status = ib_create_qp(
			p_ca_obj->h_pd,
			&qp_create_attr,
			p_ca_obj,
			rc_cm_qp_err_cb,
			&p_ca_obj->h_qp[DEST_QP]);

		if (ib_status != IB_SUCCESS)
		{
			ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
				("Error in ib_create_qp()! %s\n",
				ib_get_err_str(ib_status)));

			ib_destroy_qp(p_ca_obj->h_qp[SRC_QP],rc_cm_qp_destroy_cb);
			return (ib_status);
		}

		ib_status = ib_query_qp(p_ca_obj->h_qp[DEST_QP],
							&p_ca_obj->qp_attr[DEST_QP]);

		//CL_ASSERT(ib_status == IB_SUCCESS);
		if (ib_status != IB_SUCCESS)
		{
			ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
				("Error in query_qp()! %s\n",
				ib_get_err_str(ib_status)));

			ib_destroy_qp(p_ca_obj->h_qp[DEST_QP],rc_cm_qp_destroy_cb);
			return (ib_status);
		}
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_rc_register_mem(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			size )
{
	ib_mr_create_t	mr_create = {0};
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	cl_memclr(&mr_create, sizeof(ib_mr_create_t));
	p_ca_obj->mem_region[reg_index].buffer = cl_zalloc(size);
	CL_ASSERT (p_ca_obj->mem_region[reg_index].buffer);

	mr_create.vaddr = p_ca_obj->mem_region[reg_index].buffer;
	mr_create.length = size;
	mr_create.access_ctrl =
		IB_AC_LOCAL_WRITE | IB_AC_RDMA_READ | IB_AC_RDMA_WRITE;

	ib_status = ib_reg_mem(
		p_ca_obj->h_pd,
		&mr_create,
		&p_ca_obj->mem_region[reg_index].lkey,
		&p_ca_obj->mem_region[reg_index].rkey,
		&p_ca_obj->mem_region[reg_index].mr_h);

	ALTS_EXIT( ALTS_DBG_VERBOSE);

	return ib_status;
}

ib_api_status_t
alts_rc_deregister_mem(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index
	)
{
	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if ( p_ca_obj->mem_region[reg_index].buffer != NULL )
	{
		/*
		 * At times the buffer may have been allocated without a register
		 */
		if (p_ca_obj->mem_region[reg_index].mr_h)
			ib_status = ib_dereg_mr(p_ca_obj->mem_region[reg_index].mr_h);
		else
			ib_status = IB_ERROR;

		CL_ASSERT(ib_status == IB_SUCCESS);

		if ( ib_status != IB_SUCCESS )
		{
			//PRINT the error msg
		}

		cl_free(p_ca_obj->mem_region[reg_index].buffer);

		p_ca_obj->mem_region[reg_index].buffer = NULL;
	}
	else
	{
		ib_status = IB_ERROR;

	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


ib_api_status_t
rc_multisend_post_sends(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			num_posts )
{
	ib_send_wr_t *p_s_wr, *p_send_failure_wr;
	uint32_t msg_size, i;
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	msg_size = 64;
	//msg_size = p_ca_obj->msg_size;

	p_s_wr = p_ca_obj->p_send_wr;

	p_s_wr->p_next = NULL;
	p_s_wr->ds_array[0].length = msg_size;
	p_s_wr->num_ds = 1;

	p_s_wr->wr_type = WR_SEND;

	if( num_posts > 1 )
	{
		p_s_wr->send_opt = IB_SEND_OPT_SOLICITED ;
	}
	else
	{
		p_s_wr->send_opt = IB_SEND_OPT_SIGNALED |	\
							IB_SEND_OPT_IMMEDIATE |	\
							IB_SEND_OPT_SOLICITED ;
	}

			p_s_wr->send_opt = IB_SEND_OPT_SIGNALED |	\
							IB_SEND_OPT_IMMEDIATE |	\
							IB_SEND_OPT_SOLICITED ;


	for (i = 0; i < num_posts; i++)
	{
		sprintf_s((char *)p_ca_obj->mem_region[i+reg_index].buffer,4096, "hello %d", i);

		p_s_wr->ds_array[0].vaddr =
			(uintn_t)p_ca_obj->mem_region[i+reg_index].buffer;
		p_s_wr->ds_array[0].lkey = p_ca_obj->mem_region[i+reg_index].lkey;

		p_s_wr->wr_id = i+reg_index;
		p_s_wr->immediate_data = 0xfeedde00 + i;

		p_s_wr->remote_ops.vaddr = 0;
		p_s_wr->remote_ops.rkey = 0;

		//p_s_wr->dgrm.ud.h_av
		if( (i > 0) && (i == ( num_posts - 1 )))
		{
			p_s_wr->send_opt = IB_SEND_OPT_SIGNALED |	\
								IB_SEND_OPT_IMMEDIATE |	\
								IB_SEND_OPT_SOLICITED ;
		}

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("******vaddr(x%"PRIx64") lkey(x%x) len(%d)  send_opts(x%x)*****\n",
			(void*)(uintn_t)p_s_wr->ds_array[0].vaddr,
			p_s_wr->ds_array[0].lkey,
			p_s_wr->ds_array[0].length,
			p_s_wr->send_opt ));

		ib_status = ib_post_send(
			p_ca_obj->h_qp[SRC_QP],
			p_s_wr,
			&p_send_failure_wr);

	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return	ib_status;
}

ib_api_status_t
rc_multisend_post_recvs(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			num_posts )
{
	ib_recv_wr_t *p_r_wr, *p_failure_wr;
	uint32_t msg_size, i;
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	msg_size = 64;
	//msg_size = p_ca_obj->msg_size;

	p_r_wr = p_ca_obj->p_recv_wr;

	p_r_wr->p_next = NULL;
	p_r_wr->ds_array[0].length = msg_size;
	p_r_wr->num_ds = 1;

	for (i = 0; i < num_posts; i++)
	{
		p_r_wr->ds_array[0].vaddr =
			(uintn_t)p_ca_obj->mem_region[i+reg_index].buffer;
		p_r_wr->ds_array[0].lkey = p_ca_obj->mem_region[i+reg_index].lkey;

		p_r_wr->wr_id = i+reg_index;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("******vaddr(x%"PRIx64") lkey(x%x) len(%d)*****\n",
			(void*)(uintn_t)p_r_wr->ds_array[0].vaddr,
			p_r_wr->ds_array[0].lkey,
			p_r_wr->ds_array[0].length));

		if (p_ca_obj->is_loopback == TRUE)
		{
			ib_status = ib_post_recv(
				p_ca_obj->h_qp[SRC_QP],
				p_r_wr,
				&p_failure_wr);
		}
		else
		{
			ib_status = ib_post_recv(
				p_ca_obj->h_qp[DEST_QP],
				p_r_wr,
				&p_failure_wr);
		}
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_rc_message_passing(
	alts_cm_ca_obj_t *p_ca_obj,
	ib_qp_type_t	 qp_type)
{
	uint32_t i,j, k;
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_wc_t		*p_free_wclist;
	ib_wc_t		*p_done_cl;
	uint32_t	id;
	char		*buff;

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	p_ca_obj->wr_send_size = sizeof(ib_send_wr_t) + \
		(sizeof(ib_local_ds_t) * p_ca_obj->ds_list_depth);
	p_ca_obj->wr_recv_size = sizeof(ib_recv_wr_t) + \
		(sizeof(ib_local_ds_t) * p_ca_obj->ds_list_depth);

	p_ca_obj->p_send_wr = &send_wr;
	p_ca_obj->p_recv_wr = &recv_wr;

	p_ca_obj->p_send_wr->ds_array = &send_ds;
	p_ca_obj->p_recv_wr->ds_array = &recv_ds;

	// receive
	for (i=0; i < p_ca_obj->num_wrs; i++)
	{
		ib_status = alts_rc_register_mem( p_ca_obj, i, 4096);

		if ( ib_status != IB_SUCCESS )
		{
			while( i-- )
				alts_rc_deregister_mem(p_ca_obj, i);

			return ib_status;
		}
		else
		{
			p_ca_obj->mem_region[i].my_lid = p_ca_obj->dlid;
		}
	}

	// send
	for (k=0; k < p_ca_obj->num_wrs; k++)
	{
		ib_status = 
			alts_rc_register_mem( p_ca_obj, k + p_ca_obj->num_wrs, 4096);

		if ( ib_status != IB_SUCCESS )
		{
			while( k-- )
				alts_rc_deregister_mem(p_ca_obj, k + p_ca_obj->num_wrs);

			while( i-- )
				alts_rc_deregister_mem(p_ca_obj, i);

			return ib_status;
		}
		else
		{
			p_ca_obj->mem_region[k].my_lid = p_ca_obj->slid;
		}
	}

	p_ca_obj->cq_done = 0;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("++++++ dlid(x%x) src_port(%d) ====\n",
		p_ca_obj->dlid, p_ca_obj->src_port_num));

	if(ib_status == IB_SUCCESS)
	{
		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);
		rc_multisend_post_recvs( p_ca_obj, 0, p_ca_obj->num_wrs );

		if( p_ca_obj->num_cq > 1 )
			ib_status = ib_rearm_cq(p_ca_obj->h_cq_alt, FALSE);
		else
			ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);
		rc_multisend_post_sends( p_ca_obj, p_ca_obj->num_wrs, p_ca_obj->num_wrs );
	}

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("sleeping for awhile ...\n"));

	cl_thread_suspend(3000); // 10 seconds

//#if 0

	if (!p_ca_obj->cq_done)
	{
		p_free_wclist = &free_wclist;
		p_free_wclist->p_next = NULL;
		p_done_cl = NULL;
		j = 0;

		ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wclist, &p_done_cl);

		while(p_done_cl)
		{
			/*
			 *  print output
			 */
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("Got a completion:\n"
				"\ttype....:%s\n"
				"\twr_id...:%"PRIx64"\n"
				"\tstatus..:%s\n",
				ib_get_wc_type_str(p_done_cl->wc_type),
				p_done_cl->wr_id,
				ib_get_wc_status_str(p_done_cl->status)));

			if (p_done_cl->wc_type == IB_WC_RECV)
			{
				id = (uint32_t)p_done_cl->wr_id;
				buff = (char *)p_ca_obj->mem_region[id].buffer;
				if (qp_type == IB_QPT_UNRELIABLE_DGRM)
				{
					ALTS_PRINT(ALTS_DBG_VERBOSE,
						("---MSG--->%s\n",&buff[40]));
					ALTS_PRINT(ALTS_DBG_VERBOSE,
						("RecvUD info:\n"
						"\trecv_opt...:x%x\n"
						"\timm_data...:x%x\n"
						"\tremote_qp..:x%x\n"
						"\tpkey_index.:%d\n"
						"\tremote_lid.:x%x\n"
						"\tremote_sl..:x%x\n"
						"\tpath_bits..:x%x\n"
						"\tsrc_lid....:x%x\n",
						p_done_cl->recv.ud.recv_opt,
						p_done_cl->recv.ud.immediate_data,
						CL_NTOH32(p_done_cl->recv.ud.remote_qp),
						p_done_cl->recv.ud.pkey_index,
						CL_NTOH16(p_done_cl->recv.ud.remote_lid),
						p_done_cl->recv.ud.remote_sl,
						p_done_cl->recv.ud.path_bits,
						CL_NTOH16(p_ca_obj->mem_region[id].my_lid)));
				}
				else
				{
					ALTS_PRINT(ALTS_DBG_VERBOSE,
						("RecvRC info:\n"
						"\trecv_opt...:x%x\n"
						"\timm_data...:x%x\n",
						p_done_cl->recv.conn.recv_opt,
						p_done_cl->recv.ud.immediate_data ));
				}

			}

			p_free_wclist = p_done_cl;
			p_free_wclist->p_next = NULL;
			p_done_cl = NULL;
			j++;
			p_done_cl = NULL;
			ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wclist, &p_done_cl);
		}

		ALTS_PRINT( ALTS_DBG_INFO,
				("Number of items polled from CQ is  = %d\n", j) );

		p_ca_obj->cq_done += i;

		ib_status = IB_SUCCESS;
	}
//#endif

	while( i-- )
		alts_rc_deregister_mem(p_ca_obj, i);

	while( k-- )
		alts_rc_deregister_mem(p_ca_obj, k + p_ca_obj->num_wrs);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}



// cm cbs
void
alts_cm_apr_cb(
	IN				ib_cm_apr_rec_t				*p_cm_apr_rec )
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_cm_apr_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
alts_cm_dreq_cb(
	IN				ib_cm_dreq_rec_t			*p_cm_dreq_rec )
{
	ib_api_status_t		ib_status;
	alts_cm_ca_obj_t	*p_ca_obj;
	ib_cm_drep_t		drep;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT( p_cm_dreq_rec );

	p_ca_obj = (alts_cm_ca_obj_t*)p_cm_dreq_rec->qp_context;
	CL_ASSERT( p_ca_obj );

	p_ca_obj->cm_cbs++;		// count crows

	// send a drep
	cl_memclr(&drep, sizeof(ib_cm_drep_t));

	ib_status = ib_cm_drep(p_cm_dreq_rec->h_cm_dreq,&drep);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
alts_cm_rep_cb(
	IN				ib_cm_rep_rec_t				*p_cm_rep_rec )
{
	ib_api_status_t		ib_status;
	alts_cm_ca_obj_t	*p_ca_obj;
	ib_cm_rtu_t			*p_cm_rtu;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT( p_cm_rep_rec );

	if(( p_cm_rep_rec->qp_type == IB_QPT_RELIABLE_CONN ) ||
		( p_cm_rep_rec->qp_type == IB_QPT_UNRELIABLE_CONN ))
	{
		p_ca_obj = (alts_cm_ca_obj_t*)p_cm_rep_rec->qp_context;
		CL_ASSERT( p_ca_obj );

		p_ca_obj->cm_cbs++;		// count crows

		p_cm_rtu = &p_ca_obj->rtu_src;

		cl_memclr( p_cm_rtu, sizeof(ib_cm_rtu_t) );

		p_cm_rtu->access_ctrl = IB_AC_LOCAL_WRITE;

		if( p_ca_obj->rdma_enabled == TRUE )
		{
			p_cm_rtu->access_ctrl |= IB_AC_RDMA_READ + IB_AC_RDMA_WRITE;
		}

		if( p_ca_obj->p_ca_attr->modify_wr_depth )
		{
			p_cm_rtu->sq_depth = 16;
			p_cm_rtu->rq_depth = 16;
		}
		p_cm_rtu->pfn_cm_apr_cb = alts_cm_apr_cb;
		p_cm_rtu->pfn_cm_dreq_cb = alts_cm_dreq_cb;

		ib_status = ib_cm_rtu( p_cm_rep_rec->h_cm_rep, p_cm_rtu );

		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("ib_cm_rtu returned %s\n", ib_get_err_str( ib_status )) );
	}
	else
	if ( p_cm_rep_rec->qp_type == IB_QPT_UNRELIABLE_DGRM )
	{
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("sidr rep in\n"
			"\tstatus........:x%x\n"
			"\tremote_qp.....:x%x\n"
			"\tremote_qkey...:x%x\n",
			p_cm_rep_rec->status,
			p_cm_rep_rec->remote_qp,
			p_cm_rep_rec->remote_qkey ));

		p_ca_obj = (alts_cm_ca_obj_t*)p_cm_rep_rec->sidr_context;
		CL_ASSERT( p_ca_obj );

		p_ca_obj->cm_cbs++;		// count crows
	}
	else
	{
		//
		return;
	}


	ALTS_EXIT( ALTS_DBG_VERBOSE);
}


void
alts_cm_rej_cb(
	IN				ib_cm_rej_rec_t				*p_cm_rej_rec )
{
	alts_cm_ca_obj_t	*p_ca_obj;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	p_ca_obj = (alts_cm_ca_obj_t*)p_cm_rej_rec->qp_context;

	// only use context if qp was set up
	if( p_ca_obj )
	{
		p_ca_obj->cm_errs++;		// count crows
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
alts_cm_mra_cb(
	IN				ib_cm_mra_rec_t				*p_cm_mra_rec )
{
	alts_cm_ca_obj_t	*p_ca_obj;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	p_ca_obj = (alts_cm_ca_obj_t*)p_cm_mra_rec->qp_context;
	CL_ASSERT( p_ca_obj );


	ALTS_EXIT( ALTS_DBG_VERBOSE);
}


void
alts_cm_rtu_cb(
	IN				ib_cm_rtu_rec_t				*p_cm_rtu_rec )
{
	alts_cm_ca_obj_t	*p_ca_obj;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT( p_cm_rtu_rec );

	p_ca_obj = (alts_cm_ca_obj_t*)p_cm_rtu_rec->qp_context;
	CL_ASSERT( p_ca_obj );

	p_ca_obj->cm_cbs++;		// count crows

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
alts_cm_lap_cb(
	IN				ib_cm_lap_rec_t				*p_cm_lap_rec )
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_cm_lap_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
alts_cm_req_cb(
	IN				ib_cm_req_rec_t				*p_cm_req_rec )
{
	ib_api_status_t		ib_status;
	alts_cm_ca_obj_t	*p_ca_obj;
	ib_cm_rep_t			*p_cm_rep;
	ib_cm_mra_t			cm_mra;
	ib_cm_rej_t			cm_rej;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT( p_cm_req_rec );

	p_ca_obj = (alts_cm_ca_obj_t*)p_cm_req_rec->context;

	CL_ASSERT( p_ca_obj );

	if ( p_cm_req_rec->qp_type == IB_QPT_RELIABLE_CONN )
	{
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("rc connect request in\n"));
	}
	else
	if ( p_cm_req_rec->qp_type == IB_QPT_UNRELIABLE_DGRM )
	{
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("sidr connect request in\n"));
	}
	else
	if ( p_cm_req_rec->qp_type == IB_QPT_UNRELIABLE_CONN )
	{
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("unreliable connect request in\n"));
	}
	else
	{
		return;
	}

	if( p_ca_obj->rej_test )
	{
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("rejecting request\n"));
		cl_memclr( &cm_rej, sizeof( ib_cm_rej_t ) );
		cm_rej.rej_status = IB_REJ_USER_DEFINED;
		ib_cm_rej( p_cm_req_rec->h_cm_req, &cm_rej );
		return;
	}

	/* initiate handoff process */
	if( p_ca_obj->handoff == TRUE )
	{
		/* set it to false to stop all other transactions that happen
		   in the same cb */
		p_ca_obj->handoff = FALSE;

		ib_status = ib_cm_handoff( p_cm_req_rec->h_cm_req,
			p_ca_obj->handoff_svc_id );
		if( ib_status != IB_SUCCESS )
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("handoff failed with %s!\n", ib_get_err_str(ib_status)) );
		}
		else
		{
			ALTS_PRINT( ALTS_DBG_VERBOSE,
				("CM handoff successful\n") );
		}
		return;
	}


	p_ca_obj->cm_cbs++;		// count crows

	p_cm_rep = &p_ca_obj->rep_dest;
	cl_memclr( p_cm_rep, sizeof(ib_cm_rep_t));

	p_ca_obj->h_cm_req = p_cm_req_rec->h_cm_req;
	p_cm_rep->qp_type = p_cm_req_rec->qp_type;
	p_cm_rep->h_qp = p_ca_obj->h_qp[DEST_QP];

	// class specific
	if (( p_cm_req_rec->qp_type == IB_QPT_RELIABLE_CONN ) ||
		( p_cm_req_rec->qp_type == IB_QPT_UNRELIABLE_CONN ))
	{
		// rc, uc & rd
		p_cm_rep->access_ctrl = IB_AC_LOCAL_WRITE;	// | IB_AC_RDMA_READ | IB_AC_RDMA_WRITE;

		/* Verify that the CA supports modify_wr_depth after QP creation. */
		if( p_ca_obj->p_ca_attr->modify_wr_depth )
		{
			p_cm_rep->sq_depth = p_ca_obj->num_wrs;
			p_cm_rep->rq_depth = p_ca_obj->num_wrs;
		}

		p_cm_rep->init_depth = 1;
		p_cm_rep->target_ack_delay = 10;
		p_cm_rep->failover_accepted = IB_FAILOVER_ACCEPT_UNSUPPORTED;
		p_cm_rep->flow_ctrl = TRUE;
		p_cm_rep->rnr_nak_timeout = 7;
		p_cm_rep->rnr_retry_cnt = 7;
		p_cm_rep->pfn_cm_rej_cb = alts_cm_rej_cb;
		p_cm_rep->pfn_cm_mra_cb = alts_cm_mra_cb;
		p_cm_rep->pfn_cm_rtu_cb = alts_cm_rtu_cb;
		p_cm_rep->pfn_cm_lap_cb = alts_cm_lap_cb;
		p_cm_rep->pfn_cm_dreq_cb = alts_cm_dreq_cb;

		if( p_ca_obj->mra_test == TRUE )
		{
			// send a MRA to test
			cm_mra.mra_length = 0;
			cm_mra.p_mra_pdata = NULL;
			cm_mra.svc_timeout = 21; // equals 8.5 sec wait + packet lifetime

			ib_status = ib_cm_mra( p_cm_req_rec->h_cm_req, &cm_mra );
			ALTS_PRINT( ALTS_DBG_VERBOSE,
				("ib_cm_mra returned %s\n", ib_get_err_str( ib_status )) );
		}
		else
		{
			ib_status = ib_cm_rep( p_cm_req_rec->h_cm_req, p_cm_rep );
			ALTS_PRINT( ALTS_DBG_VERBOSE,
				("ib_cm_rep returned %s\n", ib_get_err_str( ib_status )) );
		}
	}
	else
	{
		// ud
		if( p_cm_req_rec->pkey != p_ca_obj->p_dest_port_attr->p_pkey_table[0])
			p_cm_rep->status = IB_SIDR_REJECT;
		else
			p_cm_rep->status = IB_SIDR_SUCCESS;

		ib_status = ib_cm_rep( p_cm_req_rec->h_cm_req, p_cm_rep );
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

ib_api_status_t
alts_cm_client_server(
	alts_cm_ca_obj_t	*p_ca_obj )
{
	ib_api_status_t		ib_status = IB_ERROR;
	ib_cm_req_t			*p_req_server, *p_req_client;
	ib_path_rec_t		*p_path_server, *p_path_client;
	ib_cm_listen_t		*p_listen;
	cl_status_t			cl_status;
	cl_thread_t			mra_thread;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if( p_ca_obj->mra_test == TRUE )
	{
		// create a thread to process MRA
		cl_status = cl_event_init( &p_ca_obj->mra_event, TRUE );
		if( cl_status != CL_SUCCESS )
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("cl_event_init failed !\n") );
			return IB_ERROR;
		}
		cl_memclr( &mra_thread, sizeof(cl_thread_t) );
		cl_status = cl_thread_init( &mra_thread, __mra_thread, p_ca_obj, "cm_altsTH" );
		if( cl_status != CL_SUCCESS )
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("cl_thread_init failed !\n") );
			return IB_ERROR;
		}
	}

	// setup data pointers
	p_req_server = &p_ca_obj->req_dest;
	p_req_client = &p_ca_obj->req_src;

	p_path_server = &p_ca_obj->path_dest;
	p_path_client = &p_ca_obj->path_src;

	p_listen = &p_ca_obj->listen;

	p_ca_obj->cm_cbs = 0;

	// setup server
	p_req_server->h_qp = p_ca_obj->h_qp[DEST_QP];

	cl_memclr( p_listen, sizeof(ib_cm_listen_t) );

	p_listen->qp_type = p_ca_obj->test_type;
	p_listen->svc_id = 1;
	p_listen->ca_guid = p_ca_obj->p_ca_attr->ca_guid;
	p_listen->port_guid = p_ca_obj->p_dest_port_attr->port_guid;
	p_listen->lid = p_ca_obj->dlid;
	p_listen->pkey = p_ca_obj->p_dest_port_attr->p_pkey_table[0];
	p_listen->pfn_cm_req_cb = alts_cm_req_cb;

	ib_status = ib_cm_listen(h_al, p_listen, p_ca_obj, &p_ca_obj->h_cm_listen );
	if(ib_status != IB_SUCCESS)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_cm_listen failed with status = %d\n", ib_status) );
		goto cm_end;
	}

	// setup handoff server if requested
	if( p_ca_obj->handoff == TRUE )
	{
		p_listen->svc_id = 2;
		p_ca_obj->handoff_svc_id = 2;

		ib_status = ib_cm_listen(h_al, p_listen, p_ca_obj, &p_ca_obj->h_cm_listen_handoff );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_cm_listen failed for handoff with status = %d\n",
				ib_status) );
			goto cm_end;
		}
	}

	// setup client
	cl_memclr( p_path_client, sizeof(ib_path_rec_t) );
	p_path_client->sgid.unicast.interface_id =
		p_ca_obj->p_src_port_attr->p_gid_table->unicast.interface_id;
	p_path_client->sgid.unicast.prefix =
		p_ca_obj->p_src_port_attr->p_gid_table->unicast.prefix;

	p_path_client->dgid.unicast.interface_id =
		p_ca_obj->p_dest_port_attr->p_gid_table->unicast.interface_id;
	p_path_client->dgid.unicast.prefix =
		p_ca_obj->p_dest_port_attr->p_gid_table->unicast.prefix;

	p_path_client->slid = p_ca_obj->slid;
	p_path_client->dlid = p_ca_obj->dlid;
	p_path_client->num_path = 1;
	p_path_client->pkey = p_ca_obj->p_src_port_attr->p_pkey_table[0];
	p_path_client->mtu = IB_MTU_LEN_256;
	p_path_client->pkt_life = 10;

	cl_memclr( p_req_client, sizeof(ib_cm_req_t) );

	p_req_client->qp_type = p_ca_obj->test_type;
	p_req_client->svc_id = 1;

	p_req_client->max_cm_retries = 3;
	p_req_client->p_primary_path = p_path_client;
	p_req_client->pfn_cm_rep_cb = alts_cm_rep_cb;

	if( p_req_client->qp_type == IB_QPT_UNRELIABLE_DGRM )
	{
		p_req_client->h_al = h_al;
		p_req_client->sidr_context = p_ca_obj;
		p_req_client->timeout_ms = 1000;			/* 1 sec */
		p_req_client->pkey = p_ca_obj->p_dest_port_attr->p_pkey_table[0];
	}
	else
	{
		p_req_client->resp_res = 3;
		p_req_client->init_depth = 1;
		p_req_client->remote_resp_timeout = 11;
		p_req_client->retry_cnt = 3;
		p_req_client->rnr_nak_timeout = 7;
		p_req_client->rnr_retry_cnt = 7;
		p_req_client->pfn_cm_rej_cb = alts_cm_rej_cb;
		p_req_client->pfn_cm_mra_cb = alts_cm_mra_cb;
		p_req_client->h_qp = p_ca_obj->h_qp[SRC_QP];
		p_req_client->local_resp_timeout = 12;
	}

	ib_status = ib_cm_req(p_req_client);
	if(ib_status != IB_SUCCESS)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_cm_req failed with status = %d\n", ib_status) );
		goto cm_end;
	}

	if( p_ca_obj->mra_test == TRUE )
		cl_thread_suspend( 10000 );
	else
		cl_thread_suspend( 3000 );


	switch( p_req_client->qp_type )
	{
	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		if( p_ca_obj->rej_test == TRUE )
		{
			if (!p_ca_obj->cm_errs)
				ib_status = IB_ERROR;
		}
		else if( p_ca_obj->cm_cbs != 3 )
			ib_status = IB_ERROR;
		break;

	case IB_QPT_UNRELIABLE_DGRM:
		if( p_ca_obj->cm_cbs != 2 )
			ib_status = IB_ERROR;
		break;

	default:
		ib_status = IB_ERROR;
		break;
	}

	if( ib_status == IB_SUCCESS )
	{
		// query QPs
		ib_status = ib_query_qp(p_ca_obj->h_qp[SRC_QP],
						&p_ca_obj->qp_attr[SRC_QP]);
		ib_status = ib_query_qp(p_ca_obj->h_qp[DEST_QP],
						&p_ca_obj->qp_attr[DEST_QP]);
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("Src qp_state(%d) dest_qp(x%x) dlid(x%x)\n",
			p_ca_obj->qp_attr[SRC_QP].state,
			p_ca_obj->qp_attr[SRC_QP].dest_num,
			p_ca_obj->qp_attr[SRC_QP].primary_av.dlid ) );
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("Dest qp_state(%d) dest_qp(%x) dlid(x%x)\n",
			p_ca_obj->qp_attr[DEST_QP].state,
			p_ca_obj->qp_attr[DEST_QP].dest_num,
			p_ca_obj->qp_attr[DEST_QP].primary_av.dlid ) );

		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("Src sq_psn(x%x) rq_psn(x%x)\n"
			"Dest sq_psn(x%x) rq_psn(x%x)\n",
			p_ca_obj->qp_attr[SRC_QP].sq_psn, p_ca_obj->qp_attr[SRC_QP].rq_psn,
			p_ca_obj->qp_attr[DEST_QP].sq_psn,
			p_ca_obj->qp_attr[DEST_QP].rq_psn ) );

		// return status
		ib_status = IB_SUCCESS;
	}


cm_end:
	if( p_ca_obj->mra_test == TRUE )
	{
		cl_thread_destroy( &mra_thread );
		cl_event_destroy( &p_ca_obj->mra_event );
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

void
alts_cm_drep_cb(
	IN				ib_cm_drep_rec_t			*p_cm_drep_rec )
{
	alts_cm_ca_obj_t	*p_ca_obj;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT( p_cm_drep_rec );

	p_ca_obj = (alts_cm_ca_obj_t*)p_cm_drep_rec->qp_context;
	CL_ASSERT( p_ca_obj );

	p_ca_obj->cm_cbs++;		// count crows

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
alts_listen_destroy_cb(
	IN				void						*context )
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT( context );
	UNUSED_PARAM( context );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

ib_api_status_t
alts_cm_destroy(
	alts_cm_ca_obj_t	*p_ca_obj )
{
	ib_api_status_t		ib_status = IB_ERROR;
	ib_cm_dreq_t		*p_dreq_client;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	p_ca_obj->cm_cbs = 0;

	// only dreq for connected types
	if( p_ca_obj->test_type != IB_QPT_UNRELIABLE_DGRM )
	{
		// setup data pointers
		p_dreq_client = &p_ca_obj->dreq_src;

		cl_memclr(p_dreq_client, sizeof(ib_cm_dreq_t));
		p_dreq_client->h_qp = p_ca_obj->h_qp[SRC_QP];
		p_dreq_client->pfn_cm_drep_cb = alts_cm_drep_cb;

		ib_status = ib_cm_dreq(p_dreq_client);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_cm_dreq failed with status = %d\n", ib_status) );
			goto cm_destroy_end;
		}

		cl_thread_suspend( 1000 );

		if (p_ca_obj->cm_cbs)
		{
			ALTS_PRINT( ALTS_DBG_VERBOSE,
				("ib_cm_dreq successful\n") );
		}

		p_ca_obj->cm_cbs = 0;
	}

	ib_status = ib_cm_cancel(p_ca_obj->h_cm_listen, alts_listen_destroy_cb);
	if(ib_status != IB_SUCCESS)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_cm_cancel failed with status = %d\n", ib_status) );
	}

cm_destroy_end:
	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_cm_rc_tests (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;         
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_RELIABLE_CONN;
		p_ca_obj->pfn_comp_cb = cm_rc_cq_comp_cb;	// set your cq handler

		p_ca_obj->reply_requested = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}


		/*
		 * Create the necessary resource PD/QP/QP
		 */
		p_ca_obj->num_cq = 2;

		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_cm_client_server(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_client_server failed with status %d\n", ib_status));
			break;
		}

		// run the test
		ib_status = alts_rc_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);

		cl_thread_suspend(1000); /* 1 sec */

		// destroy connection
		ib_status = alts_cm_destroy(p_ca_obj);

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;


	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}



ib_api_status_t
alts_cm_rc_rej_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;         
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_RELIABLE_CONN;
		p_ca_obj->pfn_comp_cb = cm_rc_cq_comp_cb;	// set your cq handler

		p_ca_obj->reply_requested = TRUE;

		p_ca_obj->rej_test = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_cm_client_server(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_client_server failed with status %d\n", ib_status));
			break;
		}

		// destroy connection
		//ib_status = alts_cm_destroy(p_ca_obj);
		ib_status = ib_cm_cancel(p_ca_obj->h_cm_listen, alts_listen_destroy_cb);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_cm_cancel failed with status = %d\n", ib_status) );
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}



ib_api_status_t
alts_cm_handoff_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;         
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_RELIABLE_CONN;
		p_ca_obj->pfn_comp_cb = cm_rc_cq_comp_cb;	// set your cq handler

		p_ca_obj->reply_requested = TRUE;

		p_ca_obj->rej_test = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Create handoff service
		 */
		p_ca_obj->handoff = TRUE;


		/*
		 * Start Message passing activity
		 */
		ib_status = alts_cm_client_server(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_client_server failed with status %d\n", ib_status));
			break;
		}

		// destroy connection
		ib_status = alts_cm_destroy(p_ca_obj);

		// destroy handoff listen
		if( p_ca_obj->h_cm_listen_handoff )
		{
			ib_status = ib_cm_cancel(p_ca_obj->h_cm_listen_handoff,
				alts_listen_destroy_cb);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_cm_cancel failed with status = %d\n", ib_status) );
			}
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_cm_rc_flush_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;         
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 4;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_RELIABLE_CONN;
		p_ca_obj->pfn_comp_cb = cm_rc_cq_comp_cb;	// set your cq handler

		p_ca_obj->reply_requested = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		p_ca_obj->num_cq = 2;

		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_cm_client_server(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_client_server failed with status %d\n", ib_status));
			break;
		}

		// run the test
		ib_status = alts_rc_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);

		cl_thread_suspend(3000); /* 1 sec */

		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);
		if (p_ca_obj->cq_done == 8)
			rc_multisend_post_recvs( p_ca_obj, 0, p_ca_obj->num_wrs );

		// destroy connection
		ib_status = alts_cm_destroy(p_ca_obj);

		cl_thread_suspend(4000); /* 1 sec */

		/* force a cq completion callback to overcome interrupt issue */
		/* Intel Gen1 hardware does not generate an interrupt cb for a
		   qp set to error state */
		/*
		if(p_ca_obj->cq_done == 8)
			rc_cm_cq_comp_cb(p_ca_obj);
			*/

		if (p_ca_obj->cq_done == 12)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_cm_activate_qp(
	alts_cm_ca_obj_t *p_ca_obj,
	ib_qp_handle_t h_qp
	)
{

	ib_qp_mod_t qp_mod_attr = {0};
	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if(p_ca_obj->is_src == 1)
		qp_mod_attr.state.init.primary_port = p_ca_obj->src_port_num;
	else
		qp_mod_attr.state.init.primary_port = p_ca_obj->dest_port_num;

	qp_mod_attr.state.init.qkey = p_ca_obj->qkey;
	qp_mod_attr.state.init.pkey_index = 0x0;
	qp_mod_attr.state.init.access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_MW_BIND;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("******** port num = %d ***************\n",
		qp_mod_attr.state.init.primary_port));

	qp_mod_attr.req_state = IB_QPS_INIT;
	ib_status = ib_modify_qp(h_qp, &qp_mod_attr);

	CL_ASSERT(ib_status == IB_SUCCESS);

	// Time to query the QP
	if(p_ca_obj->is_src == 1)
	{
		ib_status = ib_query_qp(h_qp,
			&p_ca_obj->qp_attr[SRC_QP]);
		CL_ASSERT(ib_status == IB_SUCCESS);
	}
	else
	{
		ib_status = ib_query_qp(h_qp,
			&p_ca_obj->qp_attr[DEST_QP]);
		CL_ASSERT(ib_status == IB_SUCCESS);
	}


	// transition to RTR
	cl_memclr(&qp_mod_attr, sizeof(ib_qp_mod_t));

	qp_mod_attr.state.rtr.opts = 0;
	qp_mod_attr.state.rtr.rq_psn = CL_NTOH32(0x00000001);

	switch ( p_ca_obj->test_type )
	{
	case IB_QPT_RELIABLE_CONN:
	case IB_QPT_UNRELIABLE_CONN:
		qp_mod_attr.state.rtr.opts = IB_MOD_QP_PRIMARY_AV;
		break;
	default:
		break;
	}

	if (p_ca_obj->is_src == 1)
	{
		if (p_ca_obj->is_loopback == TRUE)
		{
			qp_mod_attr.state.rtr.dest_qp = p_ca_obj->qp_attr[SRC_QP].num;
			qp_mod_attr.state.rtr.primary_av.port_num = p_ca_obj->src_port_num;
			qp_mod_attr.state.rtr.primary_av.dlid = p_ca_obj->slid;
		}
		else
		{
			qp_mod_attr.state.rtr.dest_qp = p_ca_obj->qp_attr[DEST_QP].num;
			qp_mod_attr.state.rtr.primary_av.port_num = p_ca_obj->src_port_num;
			qp_mod_attr.state.rtr.primary_av.dlid = p_ca_obj->dlid;
		}
	}
	else
	{
		qp_mod_attr.state.rtr.dest_qp = p_ca_obj->qp_attr[SRC_QP].num;
		qp_mod_attr.state.rtr.primary_av.port_num = p_ca_obj->dest_port_num;
		qp_mod_attr.state.rtr.primary_av.dlid = p_ca_obj->slid;
	}

	qp_mod_attr.state.rtr.primary_av.sl = 0;
	qp_mod_attr.state.rtr.primary_av.grh_valid = 0; //Set to false

	qp_mod_attr.state.rtr.primary_av.static_rate = IB_PATH_RECORD_RATE_10_GBS;
	qp_mod_attr.state.rtr.primary_av.path_bits = 0;

	qp_mod_attr.state.rtr.primary_av.conn.path_mtu = 1;
	qp_mod_attr.state.rtr.primary_av.conn.local_ack_timeout = 7;
	qp_mod_attr.state.rtr.primary_av.conn.seq_err_retry_cnt = 7;
	qp_mod_attr.state.rtr.primary_av.conn.rnr_retry_cnt = 7;
		qp_mod_attr.state.rtr.rq_psn = CL_NTOH32(0x00000001);
	qp_mod_attr.state.rtr.resp_res = 7;		//32;
	qp_mod_attr.state.rtr.rnr_nak_timeout = 7;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("****RTR***** dlid = x%x (x%x) *port_num = %d *dest_qp = %d ***\n",
		qp_mod_attr.state.rtr.primary_av.dlid,
		CL_NTOH16(qp_mod_attr.state.rtr.primary_av.dlid),
		qp_mod_attr.state.rtr.primary_av.port_num,
		CL_NTOH32(qp_mod_attr.state.rtr.dest_qp) ));

	qp_mod_attr.req_state = IB_QPS_RTR;
	ib_status = ib_modify_qp(h_qp, &qp_mod_attr);

	CL_ASSERT(ib_status == IB_SUCCESS);

	if(p_ca_obj->is_src == 1)
	{
		ib_status = ib_query_qp(h_qp,
			&p_ca_obj->qp_attr[SRC_QP]);
	}
	else
	{
		ib_status = ib_query_qp(h_qp,
			&p_ca_obj->qp_attr[DEST_QP]);
	}

	cl_memclr(&qp_mod_attr, sizeof(ib_qp_mod_t));

	qp_mod_attr.state.rts.sq_psn = CL_NTOH32(0x00000001);

	// NOTENOTE: Confirm the below time out settings
	qp_mod_attr.state.rts.retry_cnt = 7;
	qp_mod_attr.state.rts.rnr_retry_cnt = 7;
	qp_mod_attr.state.rts.rnr_nak_timeout = 7;
	qp_mod_attr.state.rts.local_ack_timeout = 7;
	qp_mod_attr.state.rts.init_depth = 3;		//3;

	qp_mod_attr.req_state = IB_QPS_RTS;
	ib_status = ib_modify_qp(h_qp, &qp_mod_attr);

	CL_ASSERT(ib_status == IB_SUCCESS);

	if(p_ca_obj->is_src == 1)
	{
		ib_status = ib_query_qp(h_qp,
			&p_ca_obj->qp_attr[SRC_QP]);
		CL_ASSERT(ib_status == IB_SUCCESS);
	}
	else
	{
		ib_status = ib_query_qp(h_qp,
			&p_ca_obj->qp_attr[DEST_QP]);
		CL_ASSERT(ib_status == IB_SUCCESS);
	}

	if (p_ca_obj->is_loopback == TRUE)
	{
		ib_status = ib_query_qp(h_qp,
			&p_ca_obj->qp_attr[DEST_QP]);
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return IB_SUCCESS;
}


ib_api_status_t
alts_rc_no_cm_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;         
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_RELIABLE_CONN;
		p_ca_obj->pfn_comp_cb = cm_rc_cq_comp_cb;	// set your cq handler

		p_ca_obj->reply_requested = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}


		/*
		 * Create the necessary resource PD/QP/QP
		 */
		p_ca_obj->num_cq = 2;

		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Time to Activate the QP
		 */
		p_ca_obj->is_src = 1;
		ib_status = alts_cm_activate_qp(p_ca_obj, p_ca_obj->h_qp[SRC_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		p_ca_obj->is_src = 0;
		ib_status = alts_cm_activate_qp(p_ca_obj, p_ca_obj->h_qp[DEST_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */

		// run the test
		ib_status = alts_rc_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);

		cl_thread_suspend(3000); /* 1 sec */

		// destroy connection
		ib_status = alts_cm_destroy(p_ca_obj);

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;


	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}




ib_api_status_t
rc_rdma_post_recvs(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			num_posts )
{
	ib_recv_wr_t *p_r_wr, *p_failure_wr;
	uint32_t msg_size, i;
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	msg_size = 64;
	//msg_size = p_ca_obj->msg_size;

	p_r_wr = p_ca_obj->p_recv_wr;

	p_r_wr->p_next = NULL;
	p_r_wr->ds_array[0].length = msg_size;
	p_r_wr->num_ds = 1;

	// post on recv and send side
	for (i = 0; i < num_posts; i++)
	{
		p_r_wr->ds_array[0].vaddr =
			(uintn_t)p_ca_obj->mem_region[i+reg_index].buffer;
		p_r_wr->ds_array[0].lkey = p_ca_obj->mem_region[i+reg_index].lkey;

		p_r_wr->wr_id = i+reg_index;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("******vaddr(x%"PRIx64") lkey(x%x) len(%d)*****\n",
			(void*)(uintn_t)p_r_wr->ds_array[0].vaddr,
			p_r_wr->ds_array[0].lkey,
			p_r_wr->ds_array[0].length));

		ib_status = ib_post_recv(
			p_ca_obj->h_qp[DEST_QP],
			p_r_wr,
			&p_failure_wr);


		p_r_wr->ds_array[0].vaddr = (uintn_t)
			p_ca_obj->mem_region[i+reg_index+num_posts].buffer;
		p_r_wr->ds_array[0].lkey =
			p_ca_obj->mem_region[i+reg_index+num_posts].lkey;

		p_r_wr->wr_id = i+reg_index+num_posts;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("******vaddr(x%"PRIx64") lkey(x%x) len(%d)*****\n",
			(void*)(uintn_t)p_r_wr->ds_array[0].vaddr,
			p_r_wr->ds_array[0].lkey,
			p_r_wr->ds_array[0].length));

		ib_status = ib_post_recv(
			p_ca_obj->h_qp[SRC_QP],
			p_r_wr,
			&p_failure_wr);

	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
rc_rdma_post_sends(
	alts_cm_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			num_posts,
	uint32_t			rdma_index,
	char				rdma_type )
{
	ib_send_wr_t *p_s_wr, *p_send_failure_wr;
	uint32_t msg_size, i;
	ib_api_status_t ib_status = IB_SUCCESS;
	alts_rdma_t		*p_rdma_req;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	msg_size = 64;
	//msg_size = p_ca_obj->msg_size;

	p_s_wr = p_ca_obj->p_send_wr;

	p_s_wr->p_next = NULL;
	p_s_wr->ds_array[0].length = msg_size;
	p_s_wr->num_ds = 1;

	p_s_wr->wr_type = WR_SEND;

	p_s_wr->send_opt =	IB_SEND_OPT_IMMEDIATE |	\
							IB_SEND_OPT_SOLICITED ;

	p_s_wr->send_opt = IB_SEND_OPT_SIGNALED |
							IB_SEND_OPT_SOLICITED ;


	for (i = 0; i < num_posts; i++)
	{
		p_rdma_req = (alts_rdma_t*)p_ca_obj->mem_region[i+reg_index].buffer;
		p_rdma_req->msg_type = rdma_type;		// write or read

		p_rdma_req->vaddr = (uintn_t)(p_ca_obj->mem_region[i+rdma_index].buffer);
		p_rdma_req->rkey = p_ca_obj->mem_region[i+rdma_index].rkey;
		sprintf_s((char *)p_rdma_req->msg, sizeof(p_rdma_req->msg), "hello %d", i);

		p_s_wr->ds_array[0].vaddr =
			(uintn_t)p_ca_obj->mem_region[i+reg_index].buffer;
		p_s_wr->ds_array[0].lkey = p_ca_obj->mem_region[i+reg_index].lkey;

		p_s_wr->wr_id = i+reg_index;
		p_s_wr->immediate_data = 0xfeedde00 + i;

		p_s_wr->remote_ops.vaddr = 0;
		p_s_wr->remote_ops.rkey = 0;

		//p_s_wr->dgrm.ud.h_av

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("******vaddr(x%"PRIx64") lkey(x%x) len(%d)*****\n",
			(void*)(uintn_t)p_s_wr->ds_array[0].vaddr,
			p_s_wr->ds_array[0].lkey,
			p_s_wr->ds_array[0].length));

		ib_status = ib_post_send(
			p_ca_obj->h_qp[SRC_QP],
			p_s_wr,
			&p_send_failure_wr);

	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return	ib_status;
}

ib_api_status_t
alts_rc_rdma_message_passing(
	alts_cm_ca_obj_t *p_ca_obj,
	ib_qp_type_t	 qp_type)
{
	uint32_t	i, j, k;
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_wc_t		*p_free_wclist;
	ib_wc_t		*p_done_cl;
	uint32_t	id;
	char		*buff;
	alts_rdma_t	*p_rdma_req;

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	p_ca_obj->wr_send_size = sizeof(ib_send_wr_t) + \
		(sizeof(ib_local_ds_t) * p_ca_obj->ds_list_depth);
	p_ca_obj->wr_recv_size = sizeof(ib_recv_wr_t) + \
		(sizeof(ib_local_ds_t) * p_ca_obj->ds_list_depth);

	p_ca_obj->p_send_wr = &send_wr;
	p_ca_obj->p_recv_wr = &recv_wr;

	p_ca_obj->p_send_wr->ds_array = &send_ds;
	p_ca_obj->p_recv_wr->ds_array = &recv_ds;

	// receive
	for (i=0; i < p_ca_obj->num_wrs * 2; i++)
	{
		ib_status = alts_rc_register_mem( p_ca_obj, i, 4096);

		if ( ib_status != IB_SUCCESS )
		{
			while( i-- )
				alts_rc_deregister_mem(p_ca_obj, i);

			return ib_status;
		}
		else
		{
			p_ca_obj->mem_region[i].my_lid = p_ca_obj->dlid;
		}
	}

	// send
	for (k=0; k < p_ca_obj->num_wrs * 2; k++)
	{
		ib_status =
			alts_rc_register_mem( p_ca_obj, k + (p_ca_obj->num_wrs * 2), 4096);

		if ( ib_status != IB_SUCCESS )
		{
			while( k-- )
				alts_rc_deregister_mem(p_ca_obj, k + (p_ca_obj->num_wrs * 2) );

			while( i-- )
				alts_rc_deregister_mem(p_ca_obj, i);

			return ib_status;
		}
		else
		{
			p_ca_obj->mem_region[k].my_lid = p_ca_obj->slid;
		}
	}

	p_ca_obj->cq_done = 0;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("++++++ dlid(x%x) src_port(%d) ====\n",
		p_ca_obj->dlid, p_ca_obj->src_port_num));

	if(ib_status == IB_SUCCESS)
	{
		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);
		rc_rdma_post_recvs( p_ca_obj, 0, 1 );	// p_ca_obj->num_wrs

		if( p_ca_obj->num_cq > 1 )
			ib_status = ib_rearm_cq(p_ca_obj->h_cq_alt, FALSE);
		else
			ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);

		rc_rdma_post_sends( p_ca_obj, p_ca_obj->num_wrs * 2, 1,
			p_ca_obj->num_wrs, 'W' );
			// send only one for now
			//p_ca_obj->num_wrs );

		ALTS_PRINT(ALTS_DBG_VERBOSE,
		("sleeping for awhile ...\n"));

		cl_thread_suspend(1000); // 10 seconds

		// check for rdma recv completion
		p_rdma_req = (alts_rdma_t*)p_ca_obj->mem_region[p_ca_obj->num_wrs].buffer;
		if( p_rdma_req->msg_type != 'C')		// write completed
		{
			ib_status = IB_ERROR;

			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("RDMA_Write failed\n"));
		}
		else
		{
			p_ca_obj->cq_done++;

			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("RDMA_Write success\n"));
		}
	}

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("sleeping for awhile ...\n"));

	cl_thread_suspend(3000); // 10 seconds

//#if 0

	if (!p_ca_obj->cq_done)
	{
		p_free_wclist = &free_wclist;
		p_free_wclist->p_next = NULL;
		p_done_cl = NULL;
		j = 0;

		ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wclist, &p_done_cl);

		while(p_done_cl)
		{
			/*
			 *  print output
			 */
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("Got a completion:\n"
				"\ttype....:%s\n"
				"\twr_id...:%"PRIx64"\n"
				"\tstatus..:%s\n",
				ib_get_wc_type_str(p_done_cl->wc_type),
				p_done_cl->wr_id,
				ib_get_wc_status_str(p_done_cl->status)));

			if (p_done_cl->wc_type == IB_WC_RECV)
			{
				id = (uint32_t)p_done_cl->wr_id;
				buff = (char *)p_ca_obj->mem_region[id].buffer;
				if (qp_type == IB_QPT_UNRELIABLE_DGRM)
				{
					ALTS_PRINT(ALTS_DBG_VERBOSE,
						("---MSG--->%s\n",&buff[40]));
					ALTS_PRINT(ALTS_DBG_VERBOSE,
						("RecvUD info:\n"
						"\trecv_opt...:x%x\n"
						"\timm_data...:x%x\n"
						"\tremote_qp..:x%x\n"
						"\tpkey_index.:%d\n"
						"\tremote_lid.:x%x\n"
						"\tremote_sl..:x%x\n"
						"\tpath_bits..:x%x\n"
						"\tsrc_lid....:x%x\n",
						p_done_cl->recv.ud.recv_opt,
						p_done_cl->recv.ud.immediate_data,
						CL_NTOH32(p_done_cl->recv.ud.remote_qp),
						p_done_cl->recv.ud.pkey_index,
						CL_NTOH16(p_done_cl->recv.ud.remote_lid),
						p_done_cl->recv.ud.remote_sl,
						p_done_cl->recv.ud.path_bits,
						CL_NTOH16(p_ca_obj->mem_region[id].my_lid)));
				}
				else
				{
					ALTS_PRINT(ALTS_DBG_VERBOSE,
						("RecvRC info:\n"
						"\trecv_opt...:x%x\n"
						"\timm_data...:x%x\n",
						p_done_cl->recv.conn.recv_opt,
						p_done_cl->recv.ud.immediate_data ));
				}

			}

			p_free_wclist = p_done_cl;
			p_free_wclist->p_next = NULL;
			p_done_cl = NULL;
			j++;
			p_done_cl = NULL;
			ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wclist, &p_done_cl);
		}

		ALTS_PRINT( ALTS_DBG_INFO,
				("Number of items polled from CQ is  = %d\n", j) );

		p_ca_obj->cq_done += j;

		ib_status = IB_SUCCESS;
	}


	while( i-- )
		alts_rc_deregister_mem(p_ca_obj, i);

	while( k-- )
		alts_rc_deregister_mem(p_ca_obj, k + (p_ca_obj->num_wrs * 2));

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_cm_rc_rdma_tests (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;        
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 2;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_RELIABLE_CONN;
		p_ca_obj->pfn_comp_cb = cm_rc_cq_comp_cb;	// set your cq handler

		p_ca_obj->rdma_enabled = TRUE;

		p_ca_obj->reply_requested = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}


		/*
		 * Create the necessary resource PD/QP/QP
		 */
		p_ca_obj->num_cq = 2;

		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		//cl_thread_suspend( 30000 );

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_cm_client_server(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_client_server failed with status %d\n", ib_status));
			break;
		}

		// query qp_info
		ib_status = ib_query_qp(p_ca_obj->h_qp[SRC_QP],
						&p_ca_obj->qp_attr[SRC_QP]);
		ib_status = ib_query_qp(p_ca_obj->h_qp[DEST_QP],
						&p_ca_obj->qp_attr[DEST_QP]);

		ALTS_PRINT( ALTS_DBG_VERBOSE,
			 ("SRC QP Info\n"
			 "\tstate.........:%d\n"
			 "\tpri_port......:%d\n"
			 "\tqp_num........:x%x\n"
			 "\tdest_qp_num...:x%x\n"
			 "\taccess_ctl....:x%x\n"
			 "\tsq_signalled..:%d\n"
			 "\tsq_psn........:x%x\n"
			 "\trq_psn........:x%x\n"
			 "\tresp_res......:x%x\n"
			 "\tinit_depth....:x%x\n"
			 "\tsq_depth......:x%x\n"
			 "\trq_depth......:x%x\n",
			 p_ca_obj->qp_attr[SRC_QP].state,
			 p_ca_obj->qp_attr[SRC_QP].primary_port,
			 p_ca_obj->qp_attr[SRC_QP].num,
			 p_ca_obj->qp_attr[SRC_QP].dest_num,
			 p_ca_obj->qp_attr[SRC_QP].access_ctrl,
			 p_ca_obj->qp_attr[SRC_QP].sq_signaled,
			 p_ca_obj->qp_attr[SRC_QP].sq_psn,
			 p_ca_obj->qp_attr[SRC_QP].rq_psn,
			 p_ca_obj->qp_attr[SRC_QP].resp_res,
			 p_ca_obj->qp_attr[SRC_QP].init_depth,
			 p_ca_obj->qp_attr[SRC_QP].sq_depth,
			 p_ca_obj->qp_attr[SRC_QP].rq_depth ));

		ALTS_PRINT( ALTS_DBG_VERBOSE,
			 ("DEST QP Info\n"
			 "\tstate.........:%d\n"
			 "\tpri_port......:%d\n"
			 "\tqp_num........:x%x\n"
			 "\tdest_qp_num...:x%x\n"
			 "\taccess_ctl....:x%x\n"
			 "\tsq_signalled..:%d\n"
			 "\tsq_psn........:x%x\n"
			 "\trq_psn........:x%x\n"
			 "\tresp_res......:x%x\n"
			 "\tinit_depth....:x%x\n"
			 "\tsq_depth......:x%x\n"
			 "\trq_depth......:x%x\n",
			 p_ca_obj->qp_attr[DEST_QP].state,
			 p_ca_obj->qp_attr[DEST_QP].primary_port,
			 p_ca_obj->qp_attr[DEST_QP].num,
			 p_ca_obj->qp_attr[DEST_QP].dest_num,
			 p_ca_obj->qp_attr[DEST_QP].access_ctrl,
			 p_ca_obj->qp_attr[DEST_QP].sq_signaled,
			 p_ca_obj->qp_attr[DEST_QP].sq_psn,
			 p_ca_obj->qp_attr[DEST_QP].rq_psn,
			 p_ca_obj->qp_attr[DEST_QP].resp_res,
			 p_ca_obj->qp_attr[DEST_QP].init_depth,
			 p_ca_obj->qp_attr[DEST_QP].sq_depth,
			 p_ca_obj->qp_attr[DEST_QP].rq_depth ));

		//cl_thread_suspend( 30000 );

		// run the test
		ib_status = alts_rc_rdma_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);

		ALTS_PRINT( ALTS_DBG_VERBOSE,
			 ("sleep for 3 seconds...\n" ));
		cl_thread_suspend(3000); /* 1 sec */

		// destroy connection
		ib_status = alts_cm_destroy(p_ca_obj);

		if (p_ca_obj->cq_done >= 4)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;


	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}



void
__mra_thread(
	IN	void*	context )
{
	ib_api_status_t		ib_status;
	alts_cm_ca_obj_t	*p_ca_obj;
	ib_cm_rep_t			*p_cm_rep;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	p_ca_obj = (alts_cm_ca_obj_t*)context;

	ALTS_PRINT( ALTS_DBG_VERBOSE,
			("mra_event sleep (30 secs)...\n") );

	cl_event_wait_on( &p_ca_obj->mra_event, 8 *1000 * 1000, TRUE );

	ALTS_PRINT( ALTS_DBG_VERBOSE,
			("mra_event triggered...\n") );

	p_cm_rep = &p_ca_obj->rep_dest;

	ib_status = ib_cm_rep( p_ca_obj->h_cm_req, p_cm_rep );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}


ib_api_status_t
alts_rc_mra_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;        
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_RELIABLE_CONN;
		p_ca_obj->pfn_comp_cb = cm_rc_cq_comp_cb;	// set your cq handler

		p_ca_obj->reply_requested = TRUE;

		p_ca_obj->mra_test = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}


		/*
		 * Create the necessary resource PD/QP/QP
		 */
		p_ca_obj->num_cq = 2;

		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_cm_client_server(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_client_server failed with status %d\n", ib_status));
			break;
		}

		// run the test
		ib_status = alts_rc_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);

		cl_thread_suspend(1000); /* 1 sec */

		// destroy connection
		ib_status = alts_cm_destroy(p_ca_obj);

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;


	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


ib_api_status_t
alts_cm_uc_test (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;         
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_UNRELIABLE_CONN;
		p_ca_obj->pfn_comp_cb = cm_rc_cq_comp_cb;	// set your cq handler

		p_ca_obj->reply_requested = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}


		/*
		 * Create the necessary resource PD/QP/QP
		 */
		p_ca_obj->num_cq = 2;

		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_cm_client_server(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_client_server failed with status %d\n", ib_status));
			break;
		}

		// run the test
		ib_status = alts_rc_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);

		cl_thread_suspend(1000); /* 1 sec */

		// destroy connection
		ib_status = alts_cm_destroy(p_ca_obj);

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;


	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


ib_api_status_t
alts_cm_sidr_tests (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_cm_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_cm_ca_obj_t*)cl_zalloc(sizeof(alts_cm_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_cm_ca_obj_t!\n") );
			break;
		}

		/* Allocate the memory needed for query_ca */
		bsize = ca_attr_size;
		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0x1;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->test_type = IB_QPT_UNRELIABLE_DGRM;
		p_ca_obj->pfn_comp_cb = cm_ud_cq_comp_cb;	// set your cq handler

		p_ca_obj->reply_requested = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_cm_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = alts_create_test_resources( p_ca_obj );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_create_test_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_cm_client_server(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_cm_client_server failed with status %d\n", ib_status));
			break;
		}

		// run the test
		//ib_status = alts_rc_message_passing(p_ca_obj,IB_QPT_UNRELIABLE_DGRM);

		cl_thread_suspend(1000); /* 1 sec */

		// destroy connection
		ib_status = alts_cm_destroy(p_ca_obj);

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_cm_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
