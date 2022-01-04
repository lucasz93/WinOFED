/*
* Copyright (c) 2005 Mellanox Technologies.  All rights reserved.
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
 *	Multisendrecv test does a data transfer between two queue pairs created one
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


typedef struct _alts_multisr_ca_obj
{
	ib_api_status_t		status;
	uint32_t			test_type;

	ib_ca_handle_t		h_ca;
	ib_ca_attr_t		*p_ca_attr;
	ib_port_attr_t		*p_src_port_attr;
	ib_port_attr_t		*p_dest_port_attr;

	ib_net64_t			src_portguid;
	uint8_t				src_port_num;

	ib_net64_t			dest_portguid;
	uint8_t				dest_port_num;

	ib_net16_t			slid;
	ib_net16_t			dlid;

	ib_cq_handle_t		h_cq;
	uint32_t			cq_size;

	ib_pd_handle_t		h_pd;

	ib_qp_handle_t		h_qp[MAX_QPS];
	ib_net32_t			qkey;

	ib_qp_attr_t		qp_attr[MAX_QPS];


	ib_send_wr_t		*p_send_wr;
	ib_recv_wr_t		*p_recv_wr;
	size_t				wr_send_size;
	size_t				wr_recv_size;
	uint32_t			num_wrs;
	uint32_t			ds_list_depth;
	uint32_t			msg_size; // Initialize this field

	ib_av_handle_t		h_av_src;
	mem_region_t		mem_region[200];

	uint32_t			cq_done;		// total completions
	boolean_t			is_src;
	boolean_t			is_loopback;

} alts_multisr_ca_obj_t;



/*
 * Function Prototypes
 */

ib_api_status_t
alts_check_active_ports(
	alts_multisr_ca_obj_t *p_ca_obj );

ib_api_status_t
alts_create_resources(
	alts_multisr_ca_obj_t *p_ca_obj );

ib_api_status_t
alts_activate_qp(
	alts_multisr_ca_obj_t *p_ca_obj,
	ib_qp_handle_t h_qp );

ib_api_status_t
alts_destroy_resources(
	alts_multisr_ca_obj_t *p_ca_obj );

ib_api_status_t
alts_register_mem(
	alts_multisr_ca_obj_t *p_ca_obj,
	uint32_t		reg_index,
	uint32_t		size );

ib_api_status_t
alts_deregister_mem(
	alts_multisr_ca_obj_t	*p_ca_obj,
	uint32_t		reg_index );

ib_api_status_t
multisend_post_sends(
	alts_multisr_ca_obj_t	*p_ca_obj,
	uint32_t		reg_index,
	uint32_t		num_post );

ib_api_status_t
multisend_post_recvs(
	alts_multisr_ca_obj_t	*p_ca_obj,
	uint32_t		reg_index,
	uint32_t		num_post );

void
multisend_cq_destroy_cb(
	void	*context );

void
multisend_pd_destroy_cb(
	void	*context );

void
multisend_qp_destroy_cb(
	void	*context );

/*
 * CQ completion callback function
 */

void
multisend_cq_comp_cb(
	void	*cq_context,
	ib_qp_type_t	qp_type);

void
ud_multisend_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

void
rc_multisend_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );
/*
 * CQ Error callback function
 */

void
multisend_cq_err_cb(
	ib_async_event_rec_t	*p_err_rec	);

/*
 * QP Error callback function
 */
void
multisend_qp_err_cb(
	ib_async_event_rec_t	*p_err_rec	);

ib_api_status_t
alts_ud_loopback (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_ud_2_ports (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_ud_2_ports_100_msgs (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_rc_loopback (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_rc_2_ports (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

ib_api_status_t
alts_rc_2_ports_100_msgs (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size );

/*
 * Gloabal Variables
 */
ib_send_wr_t		send_wr;
ib_recv_wr_t		recv_wr;
ib_local_ds_t		send_ds;
ib_local_ds_t		recv_ds;
ib_cq_create_t		cq_create_attr;
ib_qp_create_t		qp_create_attr;
ib_av_attr_t		av_attr;
ib_mr_create_t		mr_create = {0};
ib_wc_t				free_wclist;
ib_wc_t				free_wcl;

ib_api_status_t		ud_loopback=IB_NOT_FOUND, ud_2_ports=IB_NOT_FOUND;
ib_api_status_t		rc_loopback=IB_NOT_FOUND, rc_2_ports=IB_NOT_FOUND;
ib_api_status_t		ud_2_ports_100=IB_NOT_FOUND, rc_2_ports_100=IB_NOT_FOUND;

//extern uint32_t g_al_dbg_lvl;

/*	This test case assumes that the HCA has 2 port connected
 *  through the switch. Sends packets from lower port number to higher
 *  port number.
 */
ib_api_status_t
al_test_multi_send_recv(void)
{
	ib_api_status_t		ib_status = IB_ERROR;
	ib_al_handle_t		h_al;
	ib_ca_handle_t		h_ca = NULL;
	uint32_t			bsize; 
	ib_ca_attr_t		*p_ca_attr = NULL;
	//alts_multisr_ca_obj_t	ca_obj;	// for testing stack

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

		/* run all tests in succession */
		ib_status = alts_ud_loopback(h_ca, bsize);
		ud_loopback = ib_status;
		if (ib_status != IB_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("alts_ud_loopback failed with status = %d\n", ib_status) );

			break;
		}
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("alts_ud_loopback passed.\n") );

		ib_status = alts_ud_2_ports(h_ca, bsize);
		ud_2_ports = ib_status;
		if (ib_status != IB_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("alts_ud_2_ports failed with status = %d\n", ib_status) );
			break;
		}
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("alts_ud_2_ports passed.\n") );

		ib_status = alts_ud_2_ports_100_msgs(h_ca, bsize);
		ud_2_ports_100 = ib_status;
		if (ib_status != IB_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("alts_ud_2_ports_100_msgs failed with status = %d\n",
				ib_status) );
			break;
		}
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("alts_ud_2_ports_100_msgs passed.\n") );

//#if 0
		/*********
		 *********
		 Note for Mellanox & other HCA's:
		 + enable this test to test rc loopback on the same QP
		 ********/

		ib_status = alts_rc_loopback(h_ca, bsize);
		rc_loopback = ib_status;
		if (ib_status != IB_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("alts_rc_loopback failed with status = %d\n", ib_status) );
			break;
		}
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("alts_rc_loopback passed.\n") );
//#endif

		ib_status = alts_rc_2_ports(h_ca, bsize);
		rc_2_ports = ib_status;
		if (ib_status != IB_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("alts_rc_2_ports failed with status = %d\n", ib_status) );
			break;
		}
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("alts_rc_2_ports passed.\n") );

		ib_status = alts_rc_2_ports_100_msgs(h_ca, bsize);
		rc_2_ports_100 = ib_status;
		if (ib_status != IB_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("alts_rc_2_ports_100_msgs failed with status = %d\n",
				ib_status) );
			break;
		}
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("alts_rc_2_ports_100_msgs passed.\n") );

		} while (0);

	/* Destroy the resources*/
	if (p_ca_attr)
		cl_free(p_ca_attr);

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("Test results (MultiSend):\n"
		"\tud_loopback..........: %s\n"
		"\tud_2_ports...........: %s\n"
		"\tud_2_ports_100_msgs..: %s\n"
		"\trc_loopback..........: %s\n"
		"\trc_2_ports...........: %s\n"
		"\trc_2_ports_100_msgs..: %s\n",
		ib_get_err_str(ud_loopback),
		ib_get_err_str(ud_2_ports),
		ib_get_err_str(ud_2_ports_100),
		ib_get_err_str(rc_loopback),
		ib_get_err_str(rc_2_ports),
		ib_get_err_str(rc_2_ports_100)
		));

	alts_close_ca(h_ca);
	alts_close_al(h_al);

	ALTS_EXIT( ALTS_DBG_VERBOSE);

	return ib_status;

}

ib_api_status_t
alts_destroy_resources(
	alts_multisr_ca_obj_t *p_ca_obj)
{
	uint32_t	j;

	/*
	 * Destroy Send QP, Recv QP, CQ and PD
	 */
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if (p_ca_obj->h_qp[SRC_QP])
	{
		ib_status = ib_destroy_qp(p_ca_obj->h_qp[SRC_QP],multisend_qp_destroy_cb);
	}

	if (p_ca_obj->is_loopback != TRUE)
	{
		if (p_ca_obj->h_qp[DEST_QP])
		{
			ib_status = ib_destroy_qp(
				p_ca_obj->h_qp[DEST_QP],
				multisend_qp_destroy_cb);
		}
	}

	if (p_ca_obj->h_cq)
		ib_status = ib_destroy_cq(p_ca_obj->h_cq,multisend_cq_destroy_cb);

	/*
	 * Deregister the Memeory
	 */
	for(j=0; j < p_ca_obj->num_wrs * 2; j++)
	{
		if(p_ca_obj->mem_region[j].buffer != NULL)
				ib_status = alts_deregister_mem(p_ca_obj, j);
	}

	if (p_ca_obj->h_pd)
		ib_status = ib_dealloc_pd(p_ca_obj->h_pd,multisend_pd_destroy_cb);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_message_passing(
	alts_multisr_ca_obj_t *p_ca_obj,
	ib_qp_type_t	 qp_type )
{
	uint32_t i,j, k;
	ib_api_status_t ib_status = IB_SUCCESS;
//#if 0
	ib_wc_t		*p_free_wclist;
	ib_wc_t		*p_done_cl;
	uint32_t	id;
	char		*buff;
//#endif

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
		ib_status = alts_register_mem( p_ca_obj, i, 4096);

		if ( ib_status != IB_SUCCESS )
		{
			for(j=0; j<i; j++)
				alts_deregister_mem(p_ca_obj, j);

			break;
		}
		else
		{
			p_ca_obj->mem_region[i].my_lid = p_ca_obj->dlid;
		}
	}

	if(ib_status != IB_SUCCESS)
		return ib_status;

	// send
	for (k=i; k < i + p_ca_obj->num_wrs; k++)
	{
		ib_status = alts_register_mem( p_ca_obj, k, 4096);

		if ( ib_status != IB_SUCCESS )
		{
			for(j=i; j<k; j++)
				ib_status = alts_deregister_mem(p_ca_obj, j);

			break;
		}
		else
		{
			p_ca_obj->mem_region[k].my_lid = p_ca_obj->slid;
		}
	}

	if(ib_status != IB_SUCCESS)
		return ib_status;


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

	p_ca_obj->cq_done = 0;

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("++++++ dlid(x%x) src_port(%d) ====\n",
		av_attr.dlid, av_attr.port_num));

	if(ib_status == IB_SUCCESS)
	{
		multisend_post_recvs( p_ca_obj, 0, p_ca_obj->num_wrs );
		multisend_post_sends( p_ca_obj, p_ca_obj->num_wrs, p_ca_obj->num_wrs );
#if 0
		for ( i = 0 ; i < p_ca_obj->num_wrs; ++i)
		{
			multisend_post_recvs( p_ca_obj, i, p_ca_obj->num_wrs );
		}

		for ( k = i ; k < i + p_ca_obj->num_wrs; ++k)
		{
			multisend_post_sends( p_ca_obj, k, p_ca_obj->num_wrs );
		}
#endif
	}

	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("sleeping for awhile ...\n"));

//	cl_thread_suspend(10000); // 10 seconds

	while (((!p_ca_obj->cq_done) && (p_ca_obj->num_wrs> 2)) ||
		(p_ca_obj->cq_done != p_ca_obj->num_wrs*2))
	{
		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("sleeping for awhile ...\n"));
		cl_thread_suspend(0); // 10 seconds
	}


	//if (((!p_ca_obj->cq_done) && (p_ca_obj->num_wrs> 2)) ||
	//	(p_ca_obj->cq_done != p_ca_obj->num_wrs*2))
	//{
	//	ALTS_PRINT(ALTS_DBG_VERBOSE,
	//		("sleeping for awhile ...\n"));
	//	cl_thread_suspend(10000); // 10 seconds
	//}

	//if (((!p_ca_obj->cq_done) && (p_ca_obj->num_wrs> 2)) ||
	//	(p_ca_obj->cq_done != p_ca_obj->num_wrs*2))
	//{
	//	ALTS_PRINT(ALTS_DBG_VERBOSE,
	//		("sleeping for awhile ...\n"));
	//	cl_thread_suspend(10000); // 10 seconds
	//}

//#if 0

	if (!p_ca_obj->cq_done)
	{
		p_free_wclist = &free_wclist;
		p_free_wclist->p_next = NULL;
		p_done_cl = NULL;
		i = 0;

		ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wclist, &p_done_cl);

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
			i++;
			p_done_cl = NULL;
			ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wclist, &p_done_cl);
		}

		ALTS_PRINT( ALTS_DBG_INFO,
				("Number of items polled from CQ is  = %d\n", i) );

		p_ca_obj->cq_done += i;

		ib_status = IB_SUCCESS;
	}
//#endif

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


ib_api_status_t
multisend_post_sends(
	alts_multisr_ca_obj_t	*p_ca_obj,
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
		sprintf_s((char *)p_ca_obj->mem_region[i+reg_index].buffer, 4096,"hello %d", i);

		p_s_wr->ds_array[0].vaddr =
			(uintn_t)p_ca_obj->mem_region[i+reg_index].buffer;
		p_s_wr->ds_array[0].lkey = p_ca_obj->mem_region[i+reg_index].lkey;

		p_s_wr->wr_id = i+reg_index;
		p_s_wr->immediate_data = 0xfeedde00 + i;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("***** Send ******vaddr(0x%"PRIx64") lkey(0x%x) len(%d)*****\n",
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
multisend_post_recvs(
	alts_multisr_ca_obj_t	*p_ca_obj,
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
		p_r_wr->ds_array[0].vaddr =
			(uintn_t)p_ca_obj->mem_region[i+reg_index].buffer;
		p_r_wr->ds_array[0].lkey = p_ca_obj->mem_region[i+reg_index].lkey;

		p_r_wr->wr_id = i+reg_index;

		ALTS_PRINT(ALTS_DBG_VERBOSE,
			("***** Recv ******vaddr(0x%"PRIx64") lkey(0x%x) len(%d)*****\n",
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
alts_register_mem(
	alts_multisr_ca_obj_t	*p_ca_obj,
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
	mr_create.access_ctrl = IB_AC_LOCAL_WRITE;

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
alts_deregister_mem(
	alts_multisr_ca_obj_t	*p_ca_obj,
	uint32_t		reg_index
	)
{

	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if ( p_ca_obj->mem_region[reg_index].buffer != NULL )
	{
		ib_status = ib_dereg_mr(p_ca_obj->mem_region[reg_index].mr_h);

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
alts_activate_qp(
	alts_multisr_ca_obj_t *p_ca_obj,
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
		("****INIT***** port num = %d \n",		
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
	case TestRC1:
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
		("****RTR***** dlid = x%x (x%x) port_num = %d dest_qp = %d \n",
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
	ALTS_PRINT(ALTS_DBG_VERBOSE,
		("****RTS*****  \n"));
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
alts_check_active_ports(alts_multisr_ca_obj_t *p_ca_obj)
{
	ib_api_status_t	ib_status;
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
			("****** slid = x%x (x%x) ***dlid = x%x (x%x) ***************\n",
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
alts_create_resources( alts_multisr_ca_obj_t *p_ca_obj )
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
	//cq_create_attr.pfn_comp_cb = multisend_cq_comp_cb;
	switch ( p_ca_obj->test_type )
	{
		case TestUD1:
			cq_create_attr.pfn_comp_cb = ud_multisend_cq_comp_cb;
			break;
		case TestRC1:
			cq_create_attr.pfn_comp_cb = rc_multisend_cq_comp_cb;
			break;
	}
	cq_create_attr.h_wait_obj = NULL;

	ib_status = ib_create_cq(
		p_ca_obj->h_ca,
		&cq_create_attr,
		p_ca_obj,
		multisend_cq_err_cb,
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
	//qp_create_attr.sq_signaled = FALSE;


	switch ( p_ca_obj->test_type )
	{
	case TestUD1:
		qp_create_attr.qp_type = IB_QPT_UNRELIABLE_DGRM;
		break;

	case TestRC1:
		qp_create_attr.qp_type = IB_QPT_RELIABLE_CONN;
		break;

	default:
		break;
	}

	ib_status = ib_create_qp(
		p_ca_obj->h_pd,
		&qp_create_attr,
		p_ca_obj,
		multisend_qp_err_cb,
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

		ib_destroy_qp(p_ca_obj->h_qp[SRC_QP],multisend_qp_destroy_cb);
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
			multisend_qp_err_cb,
			&p_ca_obj->h_qp[DEST_QP]);

		if (ib_status != IB_SUCCESS)
		{
			ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
				("Error in ib_create_qp()! %s\n",
				ib_get_err_str(ib_status)));

			ib_destroy_qp(p_ca_obj->h_qp[SRC_QP],multisend_qp_destroy_cb);
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

			ib_destroy_qp(p_ca_obj->h_qp[DEST_QP],multisend_qp_destroy_cb);
			return (ib_status);
		}
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

void
multisend_cq_comp_cb(
	void			*cq_context,
	ib_qp_type_t	qp_type
	)
{
	ib_api_status_t ib_status;
	uint32_t i = 0, id;
	char *buff;
	ib_wc_t *p_free_wcl, *p_done_cl= NULL;
	alts_multisr_ca_obj_t *p_ca_obj;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT(cq_context);

	p_ca_obj = (alts_multisr_ca_obj_t *)cq_context;


	p_free_wcl = &free_wcl;
	p_free_wcl->p_next = NULL;
	p_done_cl = NULL;

	ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wcl, &p_done_cl);

poll_loop:
	while(p_done_cl)
	{

		if(p_done_cl->status != IB_WCS_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_VERBOSE,
			("Got a completion with error !!!!!!!! status = %s type=%s\n",
				ib_get_wc_status_str(p_done_cl->status),
				ib_get_wc_type_str( p_done_cl->wc_type)));
			
		}
		else
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
		}

		p_free_wcl = p_done_cl;
		p_free_wcl->p_next = NULL;
		p_done_cl = NULL;
		i++;
		ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wcl, &p_done_cl);
	}

	/* poll one more time to avoid a race with hw */
	ib_status = ib_rearm_cq(p_ca_obj->h_cq, TRUE); //TRUE);
	if( IB_SUCCESS == ib_status )
	{
		ib_status = ib_poll_cq(p_ca_obj->h_cq, &p_free_wcl, &p_done_cl);
		if( p_done_cl )
			goto poll_loop;
	}

	p_free_wcl = &free_wcl;

	p_ca_obj->cq_done += i;

	ALTS_PRINT( ALTS_DBG_INFO,
		("Number of items polled from CQ (in callback=%d) (total=%d)\n",
		i,
		p_ca_obj->cq_done) );


	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
ud_multisend_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	UNUSED_PARAM( h_cq );
	multisend_cq_comp_cb (cq_context, IB_QPT_UNRELIABLE_DGRM );
}

void
rc_multisend_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	UNUSED_PARAM( h_cq );
	multisend_cq_comp_cb (cq_context, IB_QPT_RELIABLE_CONN );
}

void
multisend_cq_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_PRINT(ALTS_DBG_VERBOSE,("ERROR: Async CQ error  !!!!!!!!!\n"));

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
multisend_qp_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_PRINT(ALTS_DBG_VERBOSE,("ERROR: Async QP error  !!!!!!!!!\n"));
	
	ALTS_EXIT( ALTS_DBG_VERBOSE);
}


void
multisend_pd_destroy_cb(
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
multisend_qp_destroy_cb(
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
multisend_cq_destroy_cb(
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

/*
 *  The tests
 */
ib_api_status_t
alts_ud_loopback (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_multisr_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_multisr_ca_obj_t*)cl_zalloc(sizeof(alts_multisr_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_multisr_ca_obj_t!\n") );
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
		p_ca_obj->qkey = 0x66;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->test_type = TestUD1;
		p_ca_obj->is_loopback = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test requires atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/CQ/QP/QP
		 */
		ib_status = alts_create_resources(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("Create necessary resource failed with status %d\n", ib_status));
			break;
		}


		/*
		 * Time to Activate the QP
		 */
		p_ca_obj->is_src = 1;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[SRC_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		/*
		 *  Rearm Q
		 */
		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);//TRUE);

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_message_passing(p_ca_obj, IB_QPT_UNRELIABLE_DGRM);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_message_passing failed with status %d\n", ib_status));
			break;
		}


//		cl_thread_suspend(1000 ); /* 1 sec */

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_destroy_resources(p_ca_obj);
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
alts_ud_2_ports (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_multisr_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_multisr_ca_obj_t*)cl_zalloc(sizeof(alts_multisr_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_multisr_ca_obj_t!\n") );
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
		p_ca_obj->qkey = 0x66;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->test_type = TestUD1;
		p_ca_obj->is_loopback = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test requires atleast 2 active ports on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/CQ/QP/QP
		 */
		ib_status = alts_create_resources(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("Create necessary resource failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Time to Activate the QP
		 */
		p_ca_obj->is_src = 1;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[SRC_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		p_ca_obj->is_src = 0;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[DEST_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		/*
		 *  Rearm Q
		 */
		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);//TRUE);

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_message_passing(p_ca_obj, IB_QPT_UNRELIABLE_DGRM);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000 );

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_destroy_resources(p_ca_obj);
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
alts_ud_2_ports_100_msgs (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_multisr_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_multisr_ca_obj_t*)cl_zalloc(sizeof(alts_multisr_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_multisr_ca_obj_t!\n") );
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
		p_ca_obj->qkey = 0x66;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 100;
		p_ca_obj->msg_size = 256;

		p_ca_obj->test_type = TestUD1;
		p_ca_obj->is_loopback = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test requires atleast 2 active ports on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/CQ/QP/QP
		 */
		ib_status = alts_create_resources(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("Create necessary resource failed with status %d\n", ib_status));
			break;
		}


		/*
		 * Time to Activate the QP
		 */
		p_ca_obj->is_src = 1;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[SRC_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		p_ca_obj->is_src = 0;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[DEST_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		/*
		 *  Rearm Q
		 */
		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);//TRUE);

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_message_passing(p_ca_obj, IB_QPT_UNRELIABLE_DGRM);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 200)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_destroy_resources(p_ca_obj);
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
alts_rc_loopback (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_multisr_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_multisr_ca_obj_t*)cl_zalloc(sizeof(alts_multisr_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_multisr_ca_obj_t!\n") );
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
		 * check for Intel early ref hardware
		 */
		if ((p_ca_attr->vend_id == 0x00d0b7) &&
			(p_ca_attr->dev_id == 0x3101) &&
			(p_ca_attr->revision < 2))
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("This test cannot run on this revision of the HCA hardware!!!\n"));
			ib_status = IB_SUCCESS;
			break;
		}

		/*
		 * Initialize the CA Object
		 */
		p_ca_obj->h_ca = h_ca;
		p_ca_obj->p_ca_attr = p_ca_attr;
		p_ca_obj->status = IB_SUCCESS;
		p_ca_obj->cq_size = 255*2;
		p_ca_obj->qkey = 0x66;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->test_type = TestRC1;
		p_ca_obj->is_loopback = TRUE;

		/*
		 * get an active port
		 */
		ib_status = alts_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test routing atleast 1 active port on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/CQ/QP/QP
		 */
		ib_status = alts_create_resources(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("Create necessary resource failed with status %d\n", ib_status));
			break;
		}


		/*
		 * Time to Activate the QP
		 */
		p_ca_obj->is_src = 1;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[SRC_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		/*
		 *  Rearm Q
		 */
		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);//TRUE);

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_destroy_resources(p_ca_obj);
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
alts_rc_2_ports (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_multisr_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_multisr_ca_obj_t*)cl_zalloc(sizeof(alts_multisr_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_multisr_ca_obj_t!\n") );
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
		p_ca_obj->qkey = 0x66;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 1;
		p_ca_obj->msg_size = 256;

		p_ca_obj->test_type = TestRC1;
		p_ca_obj->is_loopback = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test requires atleast 2 active ports on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/CQ/QP/QP
		 */
		ib_status = alts_create_resources(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("Create necessary resource failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Time to Activate the QP
		 */
		p_ca_obj->is_src = 1;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[SRC_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		p_ca_obj->is_src = 0;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[DEST_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		/*
		 *  Rearm Q
		 */
		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);//TRUE);

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 2)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_destroy_resources(p_ca_obj);
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
alts_rc_2_ports_100_msgs (
	ib_ca_handle_t	h_ca,
	uint32_t		ca_attr_size )
{
	ib_api_status_t		ib_status = IB_ERROR, ib_status2;
	uint32_t			bsize;
	alts_multisr_ca_obj_t	*p_ca_obj = NULL;
	ib_ca_attr_t		*p_ca_attr = NULL;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT (h_ca);
	CL_ASSERT (ca_attr_size);

	do
	{
		p_ca_obj = (alts_multisr_ca_obj_t*)cl_zalloc(sizeof(alts_multisr_ca_obj_t));
		if (!p_ca_obj)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for alts_multisr_ca_obj_t!\n") );
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
		p_ca_obj->qkey = 0x66;
		p_ca_obj->ds_list_depth = 1;
		p_ca_obj->num_wrs = 100;
		p_ca_obj->msg_size = 256;

		p_ca_obj->test_type = TestRC1;
		p_ca_obj->is_loopback = FALSE;

		/*
		 * get an active port
		 */
		ib_status = alts_check_active_ports(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("This test requires atleast 2 active ports on the 1st hca\n"));
			break;
		}

		/*
		 * Create the necessary resource PD/CQ/QP/QP
		 */
		ib_status = alts_create_resources(p_ca_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("Create necessary resource failed with status %d\n", ib_status));
			break;
		}

		/*
		 * Time to Activate the QP
		 */
		p_ca_obj->is_src = 1;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[SRC_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		p_ca_obj->is_src = 0;
		ib_status = alts_activate_qp(p_ca_obj, p_ca_obj->h_qp[DEST_QP]);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_activate_qp failed with status %d\n", ib_status));
			break;
		}

		/*
		 *  Rearm Q
		 */
		ib_status = ib_rearm_cq(p_ca_obj->h_cq, FALSE);//TRUE);

		/*
		 * Start Message passing activity
		 */
		ib_status = alts_message_passing(p_ca_obj, IB_QPT_RELIABLE_CONN);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_message_passing failed with status %d\n", ib_status));
			break;
		}

		cl_thread_suspend(1000); /* 1 sec */

		if (p_ca_obj->cq_done == 200)
			ib_status = IB_SUCCESS;
		else
			ib_status = IB_ERROR;

	} while (0);

	/*
	 * Destroy the resources
	 */
	ib_status2 = alts_destroy_resources(p_ca_obj);
	if (ib_status == IB_SUCCESS)
		ib_status = ib_status2;

	if (p_ca_attr)
		cl_free(p_ca_attr);

	if (p_ca_obj)
		cl_free(p_ca_obj);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
