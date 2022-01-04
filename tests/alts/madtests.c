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

#define TestUD1 1
#define TestRC1 2

#define MAX_QPS 8
#define SRC_QP 0
#define DEST_QP 1


typedef struct _alts_mad_ca_obj
{
	ib_api_status_t		status;
	uint32_t			test_type;

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

	ib_cq_handle_t		h_cq;
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
	uint32_t			send_done_error;
	uint32_t			recv_done;
	uint32_t			recv_done_error;
	uint32_t			cq_done;		// total completions
	boolean_t			is_src;

	boolean_t			is_loopback;
	boolean_t			reply_requested;

} alts_mad_ca_obj_t;



/*
 * Function Prototypes
 */

ib_api_status_t
alts_mad_check_active_ports(
	alts_mad_ca_obj_t *p_ca_obj );

ib_api_status_t
mad_create_resources(
	alts_mad_ca_obj_t *p_ca_obj );

ib_api_status_t
mad_activate_svc(
	alts_mad_ca_obj_t *p_ca_obj,
	ib_qp_handle_t h_qp );

ib_api_status_t
alts_spl_destroy_resources(
	alts_mad_ca_obj_t *p_ca_obj );

ib_api_status_t
mad_post_sends(
	alts_mad_ca_obj_t	*p_ca_obj,
	uint32_t		reg_index,
	uint32_t		num_post );

ib_api_status_t
mad_post_recvs(
	alts_mad_ca_obj_t	*p_ca_obj,
	uint32_t		reg_index,
	uint32_t		num_post );

void
mad_cq_destroy_cb(
	void	*context );

void
mad_pd_destroy_cb(
	void	*context );

void
mad_qp_destroy_cb(
	void	*context );

/*
 * CQ completion callback function
 */

void
mad_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

/*
 * CQ Error callback function
 */

void
mad_cq_err_cb(
	ib_async_event_rec_t	*p_err_rec	);

/*
 * QP Error callback function
 */
void
mad_qp_err_cb(
	ib_async_event_rec_t	*p_err_rec	);

void
mad_svc_send_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element );

void
mad_svc_recv_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element );

void
mad_svc_qp0_recv_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element );

ib_api_status_t
alts_qp1_loopback(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_qp1_2_ports(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_qp1_2_ports_100(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_qp1_pingpong(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_qp0_loopback(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_qp0_2_ports(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_qp0_2_ports_100(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_qp0_pingpong(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_qp0_ping_switch (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

#define ALTS_TEST_MGMT_CLASS	0x56
#define ALTS_TEST_MGMT_CLASS_VER 1
#define ALTS_TEST_MGMT_METHOD	0x56

/*
 * Gloabal Variables
 */
ib_al_handle_t		h_al;
ib_dgrm_info_t		dgrm_info;
ib_mad_svc_t		mad_svc;
ib_send_wr_t		send_wr;
ib_recv_wr_t		recv_wr;

extern ib_cq_create_t		cq_create_attr;
extern ib_qp_create_t		qp_create_attr;
extern ib_av_attr_t			av_attr;
extern ib_wc_t				free_wclist;
extern ib_wc_t				free_wcl;

ib_api_status_t		qp1_loopback=IB_NOT_FOUND, qp1_2_ports=IB_NOT_FOUND;
ib_api_status_t		qp1_2_ports_100=IB_NOT_FOUND, qp1_pingpong=IB_NOT_FOUND;

ib_api_status_t		qp0_loopback=IB_NOT_FOUND, qp0_2_ports=IB_NOT_FOUND;
ib_api_status_t		qp0_2_ports_100=IB_NOT_FOUND, qp0_pingpong=IB_NOT_FOUND;

ib_api_status_t		qp0_ping_switch=IB_NOT_FOUND;


/*	This test case assumes that the HCA has 2 port connected
 *  through the switch. Sends packets from lower port number to higher
 *  port number.
 */
ib_api_status_t
al_test_mad(void)
{
	ib_api_status_t		ib_status = IB_ERROR;
	ib_ca_handle_t		h_ca = NULL;
	uint32_t			bsize; 
	ib_ca_attr_t		*p_ca_attr = NULL;
	//alts_mad_ca_obj_t	ca_obj;	// for testing stack

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
				("alts_open_al failed status = %d", ib_status) );
			break;
		}

		/*
		 * Default opens the first CA
		 */
		ib_status = alts_open_ca(h_al, &h_ca);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_open_ca failed status = %d", ib_status) );
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
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}

		CL_ASSERT(bsize);

		// run tests
		qp1_loopback = alts_qp1_loopback(h_ca, bsize);
		if(qp1_loopback != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("qp1_loopback() failed with status %d\n", qp1_loopback));
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("qp1_loopback() passed\n"));

		qp1_2_ports = alts_qp1_2_ports(h_ca, bsize);
		if(qp1_2_ports != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_qp1_2_ports() failed with status %d\n", qp1_2_ports));
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_qp1_2_ports() passed\n"));

		qp1_2_ports_100 = alts_qp1_2_ports_100(h_ca, bsize);
		if(qp1_2_ports_100 != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_qp1_2_ports_100() failed with status %d\n",
			qp1_2_ports_100));
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_qp1_2_ports_100() passed\n"));

		qp1_pingpong = alts_qp1_pingpong(h_ca, bsize);
		if(qp1_pingpong != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_qp1_pingpong() failed with status %d\n",
			qp1_pingpong));
			//break;
		}
		else
		{
			ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_qp1_pingpong() passed\n"));
		}

		// run tests
		qp0_loopback = alts_qp0_loopback(h_ca, bsize);
		if(qp0_loopback != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("qp0_loopback() failed with status %d\n", qp0_loopback));
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("qp0_loopback() passed\n"));

		qp0_2_ports = alts_qp0_2_ports(h_ca, bsize);
		if(qp0_2_ports != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_qp0_2_ports() failed with status %d\n", qp0_2_ports));
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_qp0_2_ports() passed\n"));

		qp0_2_ports_100 = alts_qp0_2_ports_100(h_ca, bsize);
		if(qp0_2_ports_100 != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_qp0_2_ports_100() failed with status %d\n",
			qp0_2_ports_100));
			break;
		}
		ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_qp0_2_ports_100() passed\n"));

		qp0_pingpong = alts_qp0_pingpong(h_ca, bsize);
		if(qp0_pingpong != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_qp0_pingpong() failed with status %d\n",
			qp0_pingpong));
			//break;
		}
		else
		{
			ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_qp0_pingpong() passed\n"));
		}

		qp0_ping_switch = alts_qp0_ping_switch(h_ca, bsize);
		if(qp0_ping_switch != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_qp0_ping_switch() failed with status %d\n",
			qp0_ping_switch));
			//break;
		}
		else
		{
			ALTS_PRINT( ALTS_DBG_VERBOSE,
			("alts_qp0_ping_switch() passed\n"));
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	if (p_ca_attr)
		cl_free(p_ca_attr);

	ALTS_PRINT(ALTS_DBG_STATUS,
		("Test results (mad):\n"
		"\tqp1_loopback..........: %s\n"
		"\tqp1_2_ports...........: %s\n"
		"\tqp1_2_ports_100_msgs..: %s\n"
		"\tqp1_pingpong..........: %s\n",
		ib_get_err_str(qp1_loopback),
		ib_get_err_str(qp1_2_ports),
		ib_get_err_str(qp1_2_ports_100),
		ib_get_err_str(qp1_pingpong)
		));

	ALTS_PRINT(ALTS_DBG_STATUS,
		(
		"\tqp0_loopback..........: %s\n"
		"\tqp0_2_ports...........: %s\n"
		"\tqp0_2_ports_100_msgs..: %s\n"
		"\tqp0_pingpong..........: %s\n"
		"\tqp0_ping_switch.......: %s\n",
		ib_get_err_str(qp0_loopback),
		ib_get_err_str(qp0_2_ports),
		ib_get_err_str(qp0_2_ports_100),
		ib_get_err_str(qp0_pingpong),
		ib_get_err_str(qp0_ping_switch)
		));

	if( h_al )
		alts_close_al(h_al);

	ALTS_EXIT( ALTS_DBG_VERBOSE);

	return ib_status;

}

ib_api_status_t
alts_spl_destroy_resources(
	alts_mad_ca_obj_t *p_ca_obj)
{
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

	if (p_ca_obj->h_pd)
		ib_status = ib_dealloc_pd(p_ca_obj->h_pd,NULL);

	cl_thread_suspend( 1000 );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


ib_api_status_t
alts_spl_message_passing(
	alts_mad_ca_obj_t *p_ca_obj)
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
	p_ca_obj->send_done_error = 0;
	p_ca_obj->recv_done = 0;
	p_ca_obj->recv_done_error = 0;
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
		p_mad_element->timeout_ms = 20;
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
				ALTS_TEST_MGMT_METHOD,
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
				ALTS_TEST_MGMT_METHOD,
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
				("ib_send_mad failed"));

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
alts_qp0_msg_at_hc (
	alts_mad_ca_obj_t *p_ca_obj,
	IN const uint8_t hop_count	)
{
	ib_api_status_t				ib_status = IB_SUCCESS;
	ib_mad_element_t			*p_mad_element;
	ib_mad_t					*p_mad;
	char						*p_buf;
	uint32_t							i;
	uint8_t						path_out[IB_SUBNET_PATH_HOPS_MAX];

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	//Create an Address vector
	av_attr.dlid = IB_LID_PERMISSIVE;
	av_attr.port_num = p_ca_obj->src_port_num;
	av_attr.sl = 0;
	av_attr.path_bits = 0;
	av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
	av_attr.grh_valid = FALSE;

	ib_status = ib_create_av(p_ca_obj->h_pd,&av_attr,&p_ca_obj->h_av_src);
	if(ib_status != IB_SUCCESS)
		return ib_status;

	p_ca_obj->send_done = 0;
	p_ca_obj->send_done_error = 0;
	p_ca_obj->recv_done = 0;
	p_ca_obj->recv_done_error = 0;
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
		p_mad_element->timeout_ms = 20;
		p_mad_element->retry_cnt = 1;

		/* Completion information. */
		p_mad_element->status = 0;

		// format mad
		p_mad = p_mad_element->p_mad_buf;

		p_buf = (char *)p_mad;
		cl_memset(p_buf, 0x66, 256);		// set pattern in buffer

		path_out[1] = p_ca_obj->src_port_num;

		ib_smp_init_new(
			(ib_smp_t *)p_mad,
			ALTS_TEST_MGMT_METHOD,
			(ib_net64_t) CL_NTOH64(0x666),
			IB_MAD_ATTR_NODE_DESC,
			0,
			hop_count,
			0,
			(const uint8_t *)&path_out,
			IB_LID_PERMISSIVE,
			IB_LID_PERMISSIVE );

		// send
		ib_status = ib_send_mad(
			p_ca_obj->h_src_mad_svc,
			p_mad_element,
			NULL );

		if(ib_status != IB_SUCCESS)
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("ib_send_mad failed"));



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
mad_post_sends(
	alts_mad_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			num_posts )
{
	ib_send_wr_t *p_s_wr, *p_send_failure_wr;
	uint32_t msg_size, i;
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if (p_ca_obj->test_type == TestUD1)
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

	if (p_ca_obj->test_type == TestUD1)
	{
		p_s_wr->dgrm.ud.h_av = p_ca_obj->h_av_src;
		p_s_wr->send_opt = IB_SEND_OPT_IMMEDIATE | \
			IB_SEND_OPT_SIGNALED | IB_SEND_OPT_SOLICITED;

		p_s_wr->dgrm.ud.remote_qkey = p_ca_obj->qkey;
		p_s_wr->dgrm.ud.remote_qp = p_ca_obj->qp_attr[DEST_QP].num;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("======= qkey(x%x) qp_num(x%x) ========\n",
			p_s_wr->dgrm.ud.remote_qkey,
			p_s_wr->dgrm.ud.remote_qp));

	}
	else if(p_ca_obj->test_type == TestRC1)
	{
		p_s_wr->send_opt = IB_SEND_OPT_SIGNALED |	\
							IB_SEND_OPT_IMMEDIATE |	\
							IB_SEND_OPT_SOLICITED ;

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
mad_post_recvs(
	alts_mad_ca_obj_t	*p_ca_obj,
	uint32_t			reg_index,
	uint32_t			num_posts )
{
	ib_recv_wr_t *p_r_wr, *p_failure_wr;
	uint32_t msg_size, i;
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	if (p_ca_obj->test_type == TestUD1)
		msg_size = p_ca_obj->msg_size;
	else
		msg_size = 64;
	//msg_size = p_ca_obj->msg_size;

	if (p_ca_obj->test_type == TestUD1)
		msg_size = 64 + sizeof(ib_grh_t);
	else
		msg_size = 64;

	p_r_wr = p_ca_obj->p_recv_wr;

	p_r_wr->p_next = NULL;
	p_r_wr->ds_array[0].length = msg_size;
	p_r_wr->num_ds = 1;

	for (i = 0; i < num_posts; i++)
	{

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
mad_activate_svc(
	alts_mad_ca_obj_t *p_ca_obj,
	ib_qp_handle_t h_qp )
{
	ib_api_status_t ib_status;
	ib_mad_svc_handle_t	h_mad_svc;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	// init dgrm svc
	if(p_ca_obj->is_src == 1)
		dgrm_info.port_guid = p_ca_obj->src_portguid;
	else
		dgrm_info.port_guid = p_ca_obj->dest_portguid;

	dgrm_info.qkey = p_ca_obj->qkey;
	dgrm_info.pkey_index = 0;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("******** port num = %d ***************\n",
		p_ca_obj->src_port_num));

	ib_status = ib_init_dgrm_svc(
		h_qp,
		&dgrm_info );

	if (ib_status != IB_SUCCESS)
		return ib_status;

	// create svc
	cl_memclr(&mad_svc, sizeof(ib_mad_svc_t));

	mad_svc.mad_svc_context = p_ca_obj;
	mad_svc.pfn_mad_send_cb = mad_svc_send_cb;
	mad_svc.pfn_mad_recv_cb = mad_svc_recv_cb;

	mad_svc.support_unsol = TRUE;

	mad_svc.mgmt_class = ALTS_TEST_MGMT_CLASS;
	mad_svc.mgmt_version = ALTS_TEST_MGMT_CLASS_VER;

	// fill in methods supported
	mad_svc.method_array[ALTS_TEST_MGMT_METHOD] = TRUE;

	ib_status = ib_reg_mad_svc(
		h_qp,
		&mad_svc,
		&h_mad_svc );


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return IB_SUCCESS;
}

ib_api_status_t
alts_mad_check_active_ports(alts_mad_ca_obj_t *p_ca_obj)
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

/*
 * Create the CQ, PD and QP
 */
ib_api_status_t
mad_create_resources( alts_mad_ca_obj_t *p_ca_obj )
{
	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

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
	cq_create_attr.pfn_comp_cb = mad_cq_comp_cb;
	cq_create_attr.h_wait_obj = NULL;

	ib_status = ib_create_cq(
		p_ca_obj->h_ca,
		&cq_create_attr,
		p_ca_obj,
		mad_cq_err_cb,
		&p_ca_obj->h_cq );
	CL_ASSERT(ib_status == IB_SUCCESS);

	p_ca_obj->cq_size = cq_create_attr.size;

	/*
	 * Create QP Attributes
	 */
	qp_create_attr.sq_depth	= p_ca_obj->num_wrs;
	qp_create_attr.rq_depth	= p_ca_obj->num_wrs;
	qp_create_attr.sq_sge	= 1;
	qp_create_attr.rq_sge	= 1;
	qp_create_attr.h_sq_cq	= p_ca_obj->h_cq;
	qp_create_attr.h_rq_cq	= p_ca_obj->h_cq;

	qp_create_attr.sq_signaled = TRUE;

	qp_create_attr.qp_type = IB_QPT_MAD;

	ib_status = ib_create_qp(
		p_ca_obj->h_pd,
		&qp_create_attr,
		p_ca_obj,
		mad_qp_err_cb,
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

		ib_destroy_qp(p_ca_obj->h_qp[SRC_QP],mad_qp_destroy_cb);
		return (ib_status);
	}

	if (p_ca_obj->is_loopback == TRUE)
	{
		// do loopback on same QP
		p_ca_obj->h_qp[DEST_QP] = p_ca_obj->h_qp[SRC_QP];
		p_ca_obj->qp_attr[DEST_QP] = p_ca_obj->qp_attr[SRC_QP];
	}
	else
	{
		ib_status = ib_create_qp(
			p_ca_obj->h_pd,
			&qp_create_attr,
			p_ca_obj,
			mad_qp_err_cb,
			&p_ca_obj->h_qp[DEST_QP]);

		if (ib_status != IB_SUCCESS)
		{
			ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
				("Error in ib_create_qp()! %s\n",
				ib_get_err_str(ib_status)));

			ib_destroy_qp(p_ca_obj->h_qp[SRC_QP],mad_qp_destroy_cb);
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

			ib_destroy_qp(p_ca_obj->h_qp[DEST_QP],mad_qp_destroy_cb);
			return (ib_status);
		}
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


void
alts_send_mad_resp(
	alts_mad_ca_obj_t			*p_ca_obj,
	ib_mad_element_t			*p_gmp )
{
	ib_api_status_t				ib_status = IB_SUCCESS;
	ib_mad_element_t			*p_resp;
	ib_mad_t					*p_mad;

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	//Create an Address vector
	av_attr.dlid = p_gmp->remote_lid;
	av_attr.port_num = p_ca_obj->dest_port_num;
	av_attr.sl = p_gmp->remote_sl;
	av_attr.path_bits = p_gmp->path_bits;
	av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;

	av_attr.grh_valid = p_gmp->grh_valid;
	if (p_gmp->grh_valid == TRUE)
		av_attr.grh = *p_gmp->p_grh;

	ib_status = ib_create_av(p_ca_obj->h_pd,&av_attr,&p_ca_obj->h_av_dest);
	if(ib_status != IB_SUCCESS)
		return;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("++++++ dlid(x%x) src_port(%d) ====\n",
		av_attr.dlid, av_attr.port_num));

	ib_status = ib_get_mad(
		p_ca_obj->h_dest_pool,
		MAD_BLOCK_SIZE,
		&p_resp );
	if (ib_status != IB_SUCCESS)
	{
		ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
			("Error in ib_get_mad()! %s\n",	ib_get_err_str(ib_status)));

		return;
	}

	// format mad
	p_resp->context1 = (void *)1;
	p_resp->context2 = p_ca_obj;

	/* Send request information. */
	p_resp->h_av = p_ca_obj->h_av_dest;
	p_resp->send_opt = IB_SEND_OPT_SIGNALED;
	p_resp->resp_expected = FALSE;	//TRUE;

	p_resp->remote_qp = p_gmp->remote_qp;
	p_resp->remote_qkey = p_ca_obj->qkey;
	p_resp->timeout_ms = 0;
	p_resp->retry_cnt = 0;

	/* Completion information. */
	p_resp->status = 0;

	// format mad
	p_mad = p_resp->p_mad_buf;

	// copy msg received as response
	ib_mad_init_response(
		p_gmp->p_mad_buf,
		p_mad,
		0 );

	// send
	ib_status = ib_send_mad(
		p_ca_obj->h_dest_mad_svc,
		p_resp,
		NULL );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ;
}

void
mad_svc_send_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element )
{
	ib_mad_element_t			*p_gmp;
	uint32_t					i = 0;
	alts_mad_ca_obj_t			*p_ca_obj;
	ib_mad_t					*p_mad;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( h_mad_svc );

	CL_ASSERT (mad_svc_context);
	CL_ASSERT (p_mad_element);

	p_gmp = p_mad_element;

	p_ca_obj = (alts_mad_ca_obj_t*)mad_svc_context;

	do
	{
		p_mad = p_gmp->p_mad_buf;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("Send completed:\n"
			"\tstatus......:%s\n"
			"\tremote_qp...:x%x\n"
			"\tremote_qkey.:x%x\n"
			"\ttid.........:x%"PRIx64"\n",
			ib_get_wc_status_str(p_gmp->status),
			CL_NTOH32(p_gmp->remote_qp),
			CL_NTOH32(p_gmp->remote_qkey),
			p_mad->trans_id
			));

		if( p_gmp->status == IB_WCS_SUCCESS )
		{
			i++;
			p_ca_obj->send_done++;
		}
		else
		{
			p_ca_obj->send_done_error++;
		}

		// loop
		p_gmp = p_gmp->p_next;

	} while (p_gmp);

	p_ca_obj->cq_done += i;

	ALTS_PRINT( ALTS_DBG_INFO,
		("Number of items polled from CQ (in callback=%d) (send=%d) (total=%d)\n",
		i,
		p_ca_obj->send_done,
		p_ca_obj->cq_done) );

	// put it back in the mad owner's pool
	ib_put_mad(p_mad_element);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
mad_svc_recv_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element )
{
	ib_mad_element_t			*p_gmp;
	uint32_t					i = 0;
	alts_mad_ca_obj_t			*p_ca_obj;
	ib_mad_t					*p_mad;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( h_mad_svc );

	CL_ASSERT (mad_svc_context);
	CL_ASSERT (p_mad_element);

	p_gmp = p_mad_element;
	p_ca_obj = (alts_mad_ca_obj_t*)mad_svc_context;

	do
	{
		p_mad = p_gmp->p_mad_buf;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("Recv completed:\n"
			"\tstatus......:%s\n"
			"\tremote_qp...:x%x\n"
			"\tremote_lid..:x%x\n"
			"\ttid.........:x%"PRIx64"\n",
			ib_get_wc_status_str(p_gmp->status),
			CL_NTOH32(p_gmp->remote_qp),
			p_gmp->remote_lid,
			p_mad->trans_id
			));

		if( p_gmp->status == IB_WCS_SUCCESS )
		{
			i++;
			p_ca_obj->recv_done++;

			// process received mad
			if (p_ca_obj->reply_requested == TRUE)
			{
				// is it a request?
				if (ib_mad_is_response(p_mad) != TRUE)
					alts_send_mad_resp(p_ca_obj, p_gmp);
			}
		}
		else
		{
			p_ca_obj->recv_done_error++;
		}


		// loop
		p_gmp = p_gmp->p_next;

	} while (p_gmp);

	p_ca_obj->cq_done += i;

	ALTS_PRINT( ALTS_DBG_INFO,
		("Number of items polled from CQ (in callback=%d) (recv=%d) (total=%d)\n",
		i,
		p_ca_obj->recv_done,
		p_ca_obj->cq_done) );

	// put it back in the mad owner's pool
	ib_put_mad(p_mad_element);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
mad_svc_qp0_recv_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element )
{
	ib_mad_element_t			*p_gmp;
	uint32_t					i = 0;
	alts_mad_ca_obj_t			*p_ca_obj;
	ib_mad_t					*p_mad;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( h_mad_svc );

	CL_ASSERT (mad_svc_context);
	CL_ASSERT (p_mad_element);

	p_gmp = p_mad_element;
	p_ca_obj = (alts_mad_ca_obj_t*)mad_svc_context;

	do
	{
		p_mad = p_gmp->p_mad_buf;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("Recv completed:\n"
			"\tstatus......:%s\n"
			"\tremote_qp...:x%x\n"
			"\tremote_lid..:x%x\n"
			"\ttid.........:x%"PRIx64"\n",
			ib_get_wc_status_str(p_gmp->status),
			CL_NTOH32(p_gmp->remote_qp),
			p_gmp->remote_lid,
			p_mad->trans_id
			));

		if( p_gmp->status == IB_WCS_SUCCESS )
		{
			i++;
			p_ca_obj->recv_done++;

			// process received mad
			if (p_ca_obj->reply_requested == TRUE)
			{
				// is it a request?
				//if (ib_mad_is_response(p_mad) != TRUE)
				//	alts_send_mad_resp(p_ca_obj, p_gmp);
			}
		}
		else
		{
			p_ca_obj->recv_done_error++;
		}



		// loop
		p_gmp = p_gmp->p_next;

	} while (p_gmp);

	p_ca_obj->cq_done += i;

	ALTS_PRINT( ALTS_DBG_INFO,
		("Number of items polled from CQ (in callback=%d) (recv=%d) (total=%d)\n",
		i,
		p_ca_obj->recv_done,
		p_ca_obj->cq_done) );

	// put it back in the mad owner's pool
	ib_put_mad(p_mad_element);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

/*
 * Create the Spl PD and QP
 */
ib_api_status_t
mad_create_spl_resources(
	alts_mad_ca_obj_t			*p_ca_obj,
	ib_qp_type_t				qp_type,
	uint8_t						mgmt_class,
	uint8_t						class_ver,
	ib_pfn_mad_comp_cb_t		pfn_mad_svc_send_cb,
	ib_pfn_mad_comp_cb_t		pfn_mad_svc_recv_cb )
{
	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );


	/*
	 * Allocate a PD
	 */
	ib_status = ib_alloc_pd(
		p_ca_obj->h_ca,
		IB_PDT_ALIAS,
		p_ca_obj,	//pd_context
		&p_ca_obj->h_pd);

	CL_ASSERT(ib_status == IB_SUCCESS);

	/*
	 * Create QP Attributes
	 */
	cl_memclr(&qp_create_attr, sizeof(ib_qp_create_t));

	qp_create_attr.sq_depth	= p_ca_obj->num_wrs;
	qp_create_attr.rq_depth	= p_ca_obj->num_wrs;
	qp_create_attr.sq_sge	= 1;
	qp_create_attr.rq_sge	= 1;
	qp_create_attr.h_sq_cq	= NULL;
	qp_create_attr.h_rq_cq	= NULL;

	qp_create_attr.sq_signaled = TRUE;

	qp_create_attr.qp_type = qp_type;	// IB_QPT_QP1_ALIAS or IB_QPT_QP0_ALIAS;

	ib_status = ib_get_spl_qp(
		p_ca_obj->h_pd,
		p_ca_obj->src_portguid,
		&qp_create_attr,
		p_ca_obj,		// context
		mad_qp_err_cb,
		&p_ca_obj->h_src_pool,
		&p_ca_obj->h_qp[SRC_QP]);

	if (ib_status != IB_SUCCESS)
	{
		ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
			("Error in ib_get_spl_qp()! %s\n", ib_get_err_str(ib_status)));
		return (ib_status);
	}

	ib_status = ib_query_qp(p_ca_obj->h_qp[SRC_QP],
		&p_ca_obj->qp_attr[SRC_QP]);

	// create svc
	cl_memclr(&mad_svc, sizeof(ib_mad_svc_t));

	mad_svc.mad_svc_context = p_ca_obj;
	mad_svc.pfn_mad_send_cb = pfn_mad_svc_send_cb;
	mad_svc.pfn_mad_recv_cb = pfn_mad_svc_recv_cb;

	mad_svc.support_unsol = TRUE;

	mad_svc.mgmt_class = mgmt_class;
	mad_svc.mgmt_version = class_ver;


	// fill in methods supported
	mad_svc.method_array[ALTS_TEST_MGMT_METHOD] = TRUE;

	ib_status = ib_reg_mad_svc(
		p_ca_obj->h_qp[SRC_QP],
		&mad_svc,
		&p_ca_obj->h_src_mad_svc );
	if (ib_status != IB_SUCCESS)
	{
		ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
			("Error in ib_reg_mad_svc()! %s\n", ib_get_err_str(ib_status)));
		return (ib_status);
	}

	// do the server side too if we are not doing loopback
	if (p_ca_obj->is_loopback == TRUE)
	{
		// do loopback on same QP
		p_ca_obj->h_qp[DEST_QP] = p_ca_obj->h_qp[SRC_QP];
		p_ca_obj->qp_attr[DEST_QP] = p_ca_obj->qp_attr[SRC_QP];
	}
	else
	{
		ib_status = ib_get_spl_qp(
			p_ca_obj->h_pd,
			p_ca_obj->dest_portguid,
			&qp_create_attr,
			p_ca_obj,		// context
			mad_qp_err_cb,
			&p_ca_obj->h_dest_pool,
			&p_ca_obj->h_qp[DEST_QP]);

		if (ib_status != IB_SUCCESS)
		{
			ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
				("Error in ib_get_spl_qp()! %s\n",
				ib_get_err_str(ib_status)));

			ib_destroy_qp(p_ca_obj->h_qp[SRC_QP],mad_qp_destroy_cb);
			return (ib_status);
		}

		ib_status = ib_query_qp(p_ca_obj->h_qp[DEST_QP],
			&p_ca_obj->qp_attr[DEST_QP]);

		// create svc
		cl_memclr(&mad_svc, sizeof(ib_mad_svc_t));

		mad_svc.mad_svc_context = p_ca_obj;
		mad_svc.pfn_mad_send_cb = pfn_mad_svc_send_cb;
		mad_svc.pfn_mad_recv_cb = pfn_mad_svc_recv_cb;

		mad_svc.support_unsol = TRUE;

		if (qp_type == IB_QPT_QP0_ALIAS)
		{
			mad_svc.mgmt_class = IB_MCLASS_SUBN_LID;
			mad_svc.mgmt_version = ALTS_TEST_MGMT_CLASS_VER;
		}
		else
		{
			mad_svc.mgmt_class = ALTS_TEST_MGMT_CLASS;
			mad_svc.mgmt_version = ALTS_TEST_MGMT_CLASS_VER;
		}

		// fill in methods supported
		mad_svc.method_array[ALTS_TEST_MGMT_METHOD] = TRUE;

		ib_status = ib_reg_mad_svc(
			p_ca_obj->h_qp[DEST_QP],
			&mad_svc,
			&p_ca_obj->h_dest_mad_svc );
		if (ib_status != IB_SUCCESS)
		{
			ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
				("Error in ib_reg_mad_svc()! %s\n", ib_get_err_str(ib_status)));
			return (ib_status);
		}

	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


void
mad_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	ib_api_status_t ib_status;
	uint32_t i = 0, id;
	ib_wc_t *p_free_wcl, *p_done_cl= NULL;
	alts_mad_ca_obj_t *p_ca_obj;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( h_cq );

	CL_ASSERT(cq_context);

	p_ca_obj = (alts_mad_ca_obj_t *)cq_context;


	ib_status = ib_rearm_cq(p_ca_obj->h_cq, TRUE);

	p_free_wcl = &free_wcl;
	p_free_wcl->p_next = NULL;

	ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wcl, &p_done_cl);

	while(p_done_cl)
	{

		/*
		 *  print output
		 */
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("Got a completion:\n"
			"\ttype....:%s\n"
			"\twr_id...:%"PRIx64"\n",
			ib_get_wc_type_str(p_done_cl->wc_type),
			p_done_cl->wr_id ));


		if (p_done_cl->wc_type == IB_WC_RECV)
		{
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("message length..:%d bytes\n",
				p_done_cl->length ));

			id = (uint32_t)p_done_cl->wr_id;
			ALTS_PRINT(ALTS_DBG_VERBOSE,
				("RecvUD info:\n"
				"\trecv_opt...:x%x\n"
				"\timm_data...:x%x\n",
				p_done_cl->recv.conn.recv_opt,
				p_done_cl->recv.ud.immediate_data ));
		}

		p_free_wcl = p_done_cl;
		p_free_wcl->p_next = NULL;
		p_done_cl = NULL;
		i++;
		ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wcl, &p_done_cl);
	}

	p_ca_obj->cq_done += i;

	ALTS_PRINT( ALTS_DBG_INFO,
		("Number of items polled from CQ (in callback=%d) (total=%d)\n",
		i,
		p_ca_obj->cq_done) );


	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
mad_cq_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
mad_qp_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
mad_pd_destroy_cb(
	void	*context
	)
{
/*
 * PD destroy call back
 */
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( context );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}


void
mad_qp_destroy_cb(
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

void
mad_cq_destroy_cb(
	void	*context
	)
{
/*
 * CQ destroy call back
 */
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( context );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

ib_api_status_t
alts_qp1_loopback(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize; 
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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
		p_ca_obj->qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_mad_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP1_ALIAS,
			ALTS_TEST_MGMT_CLASS,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_spl_message_passing(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
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
alts_qp1_2_ports(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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
		p_ca_obj->qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_mad_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP1_ALIAS,
			ALTS_TEST_MGMT_CLASS,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_spl_message_passing(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
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
alts_qp1_2_ports_100(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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
		p_ca_obj->qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 100;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_mad_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP1_ALIAS,
			ALTS_TEST_MGMT_CLASS,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_spl_message_passing(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 200)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
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
alts_qp1_pingpong(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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
		p_ca_obj->qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP1;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->reply_requested = TRUE;		// we need a reply

		/*
		 * get an active port
		 */
		ib_status = alts_mad_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP1_ALIAS,
			ALTS_TEST_MGMT_CLASS,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_spl_message_passing(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 4)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
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
alts_qp0_loopback(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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

		p_ca_obj->src_qp_num = IB_QP0;
		p_ca_obj->is_loopback = TRUE;

		p_ca_obj->reply_requested = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_mad_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP0_ALIAS,
			IB_MCLASS_SUBN_LID,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_spl_message_passing(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
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
alts_qp0_2_ports(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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

		p_ca_obj->src_qp_num = IB_QP0;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->reply_requested = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_mad_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP0_ALIAS,
			IB_MCLASS_SUBN_LID,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_spl_message_passing(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
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
alts_qp0_2_ports_100(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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
		p_ca_obj->num_wrs = 100;
		p_ca_obj->msg_size = 256;

		p_ca_obj->src_qp_num = IB_QP0;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->reply_requested = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_mad_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP0_ALIAS,
			IB_MCLASS_SUBN_LID,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_spl_message_passing(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 200)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
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
alts_qp0_pingpong(
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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

		p_ca_obj->src_qp_num = IB_QP0;
		p_ca_obj->is_loopback = FALSE;

		p_ca_obj->reply_requested = TRUE;		// we need a reply

		/*
		 * get an active port
		 */
		ib_status = alts_mad_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP0_ALIAS,
			IB_MCLASS_SUBN_LID,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_spl_message_passing(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 4)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
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
alts_qp0_ping_switch (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_mad_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ib_port_attr_t		*p_src_port_attr = NULL;
	ib_port_attr_t		*p_dest_port_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_mad_ca_obj_t*)cl_zalloc(sizeof(alts_mad_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_mad_ca_obj_t!\n") );
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

		p_ca_obj->src_qp_num = IB_QP0;
		p_ca_obj->is_loopback = TRUE;

		p_ca_obj->reply_requested = TRUE;

		// set the out port to be the last port
		p_src_port_attr = &p_ca_attr->p_port_attr[p_ca_attr->num_ports-1];

		if (p_src_port_attr->link_state == 0)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test requires the last port of HCA connected to a switch.\n"));

			ib_status = IB_ERROR;
			break;
		}

		// reset src and dest
		p_dest_port_attr = p_src_port_attr;

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


		/*
		 * Create the necessary resource PD/QP/QP
		 */
		ib_status = mad_create_spl_resources(
			p_ca_obj,
			IB_QPT_QP0_ALIAS,
			IB_MCLASS_SUBN_DIR,
			ALTS_TEST_MGMT_CLASS_VER,
			mad_svc_send_cb,
			mad_svc_recv_cb );
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("mad_create_spl_resources() failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_qp0_msg_at_hc(p_ca_obj, 1);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_spl_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("errors: send(%d) recv(%d)\n",
				p_ca_obj->send_done_error, p_ca_obj->recv_done_error));

			ib_status = IB_ERROR;
		}

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_spl_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
