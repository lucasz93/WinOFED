/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2005 Hewlett Packard, Inc (Grant Grundler)
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include "getopt.h"
#include "get_clock.h"
#include "perf_defs.h"
#include "l2w.h"



static int	page_size;
cycles_t	*tstamp;

struct user_parameters {
	const char	*servername;
	int 		connection_type;
	int 		mtu;
	int 		signal_comp;
	int 		all; /* run all msg size */
	int 		iters;
	int 		tx_depth;
	int			use_grh;
	int			use_event;    
};



void
pp_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	UNUSED_PARAM( h_cq );
	UNUSED_PARAM( cq_context);
	CL_ASSERT(FALSE);
	return ;
}


static struct pingpong_context *pp_init_ctx(unsigned int size,int port,struct user_parameters *user_parm, char* ib_devguid) {

	struct pingpong_context	*ctx;
	ib_api_status_t 		ib_status = IB_SUCCESS;	
	size_t 					guid_count;
	ib_net64_t				*ca_guid_array;
	int 					guid_index = 0;


	
	ctx = malloc(sizeof *ctx);
	if (!ctx)
		return NULL;

	ctx->qp = malloc(sizeof (ib_qp_handle_t));
	if (!ctx->qp) {
		perror("malloc");
		return NULL;
	}

	ctx->qp_attr = malloc(sizeof (ib_qp_attr_t));
	if (!ctx->qp_attr) {
		perror("malloc");
		return NULL;
	}

	ctx->size = size;
	ctx->tx_depth = user_parm->tx_depth;
	/* in case of UD need space for the GRH */
	if (user_parm->connection_type==UD) {
		ctx->buf = malloc(( size + 40 ) * 2); //PORTED ALINGED
		if (!ctx->buf) {
			fprintf(stderr, "Couldn't allocate work buf.\n");
			return NULL;
		}
		memset(ctx->buf, 0, ( size + 40 ) * 2);
	} else {
		ctx->buf = malloc( size * 2); //PORTED ALINGED
		if (!ctx->buf) {
			fprintf(stderr, "Couldn't allocate work buf.\n");
			return NULL;
		}
		memset(ctx->buf, 0, size * 2);
	}


	ctx->post_buf = (char*)ctx->buf + (size - 1);
	ctx->poll_buf = (char*)ctx->buf + (2 * size - 1);

		/*
	 * Open the AL instance
	 */
	ib_status = ib_open_al(&ctx->al);
	if(ib_status != IB_SUCCESS)
	{
		fprintf(stderr,"ib_open_al failed status = %d\n", ib_status);
		return NULL;
	}

	/*
	 * Get the Local CA Guids
	 */
	ib_status = ib_get_ca_guids(ctx->al, NULL, &guid_count);
	if(ib_status != IB_INSUFFICIENT_MEMORY)
	{
		fprintf(stderr,"ib_get_ca_guids1 failed status = %d\n", (uint32_t)ib_status);
		return NULL;
	}
	
	/*
	 * If no CA's Present then return
	 */

	if(guid_count == 0)
		return NULL;

	
	ca_guid_array = (ib_net64_t*)malloc(sizeof(ib_net64_t) * guid_count);
	
	ib_status = ib_get_ca_guids(ctx->al, ca_guid_array, &guid_count);
	if(ib_status != IB_SUCCESS)
	{
		fprintf(stderr,"ib_get_ca_guids2 failed with status = %d\n", ib_status);
		free(ca_guid_array);
		return NULL;
	}

	/*
	 * Open only the first HCA
	 */
	/* Open the CA */
	if (ib_devguid) 
	{
		unsigned int i = 0;
		guid_index = -1;
		for (i = 0; i < guid_count; i++)
		{
			char curr_dev[20];
			sprintf_s(curr_dev, 20, "0x%016I64x", ntohll(ca_guid_array[i]));
			if (!_stricmp(ib_devguid, curr_dev))
			{
				guid_index = i;
				break;
			}
		}
		if (guid_index == -1)
		{
			fprintf(stderr,"device with guid %s was not found\n", ib_devguid);
			free(ca_guid_array);
			return NULL;
		}
	}

	
	ib_status = ib_open_ca(ctx->al ,ca_guid_array[guid_index] ,NULL,
		NULL,	//ca_context
		&ctx->ca);

	if(ib_status != IB_SUCCESS)
	{
		fprintf(stderr,"ib_open_ca failed with status = %d\n", ib_status);
		free(ca_guid_array);
		return NULL;
	}

	//xxx
	//printf("ib_open_ca passed i=%d\n",i); 
	//xxx
	
	free(ca_guid_array);


	{


		/* Query the CA */
		uint32_t bsize = 0;
		ib_status = ib_query_ca(ctx->ca, NULL, &bsize);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			fprintf(stderr, "Failed to query device props");
			return NULL;
		}

		ctx->ca_attr = (ib_ca_attr_t *)malloc(bsize);

		ib_status = ib_query_ca(ctx->ca, ctx->ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			printf("ib_query_ca failed with status = %d\n", ib_status);
			return NULL;
		}
		if (user_parm->mtu == 0) {/*user did not ask for specific mtu */
			if (ctx->ca_attr->dev_id == 23108) {
				user_parm->mtu = 1024;
			} else {
				user_parm->mtu = 2048;
			}
		}
	}

	ib_status = ib_alloc_pd(ctx->ca ,
						IB_PDT_NORMAL,
						ctx, //pd_context
						&ctx->pd);
	if (ib_status != IB_SUCCESS) {
		fprintf(stderr, "Couldn't allocate PD\n");
		return NULL;
	}


	{
		ib_mr_create_t			mr_create;
		ib_cq_create_t			cq_create;
		/* We dont really want IBV_ACCESS_LOCAL_WRITE, but IB spec says:
		 * The Consumer is not allowed to assign Remote Write or Remote Atomic to
		 * a Memory Region that has not been assigned Local Write. */
		if (user_parm->connection_type==UD) {
			mr_create.length = (size + 40 ) * 2;
		} else {
			mr_create.length = size * 2;
		}
			
		mr_create.vaddr = ctx->buf;
		mr_create.access_ctrl = IB_AC_RDMA_WRITE| IB_AC_LOCAL_WRITE;
		
		ib_status = ib_reg_mem(ctx->pd ,&mr_create ,&ctx->lkey ,&ctx->rkey ,&ctx->mr);
		if (ib_status != IB_SUCCESS) {
			fprintf(stderr, "Couldn't allocate MR\n");
			return NULL;
		}

		cq_create.size = user_parm->tx_depth*2;
		if (user_parm->use_event) {
			cl_status_t cl_status;

			cl_status = cl_waitobj_create( FALSE, &ctx->cq_waitobj );
			if( cl_status != CL_SUCCESS ) {
				ctx->cq_waitobj = NULL;
				fprintf(stderr, "cl_waitobj_create() returned %s\n", CL_STATUS_MSG(cl_status) );
				return NULL;
			}

			cq_create.h_wait_obj = ctx->cq_waitobj;
			cq_create.pfn_comp_cb = NULL;
		} else {
			cq_create.h_wait_obj = NULL;
			cq_create.pfn_comp_cb = pp_cq_comp_cb;
		}



		
		ib_status = ib_create_cq(ctx->ca,&cq_create ,ctx, NULL, &ctx->rcq);
		if (ib_status != IB_SUCCESS) {
			fprintf(stderr, "Couldn't create CQ\n");
			return NULL;
		}

		if (user_parm->use_event) {
			ib_status = ib_rearm_cq( ctx->rcq, FALSE );
			if( ib_status )
			{
				ib_destroy_cq( ctx->scq, NULL );
				fprintf(stderr,"ib_rearm_cq returned %s\n", ib_get_err_str( ib_status ));
				return NULL;
			}
		}

		cq_create.size = user_parm->tx_depth*2;
		cq_create.h_wait_obj = NULL;
		cq_create.pfn_comp_cb = pp_cq_comp_cb;
		ib_status = ib_create_cq(ctx->ca,&cq_create ,ctx, NULL, &ctx->scq);
		if (ib_status != IB_SUCCESS) {
			fprintf(stderr, "Couldn't create CQ\n");
			return NULL;
		}
	}

	{
		ib_qp_create_t	qp_create;
		memset(&qp_create, 0, sizeof(ib_qp_create_t));
		qp_create.h_sq_cq	= ctx->scq;
		qp_create.h_rq_cq	= ctx->rcq;
		qp_create.sq_depth	= user_parm->tx_depth;
		qp_create.rq_depth	= user_parm->tx_depth;
		qp_create.sq_sge	= 1;
		qp_create.rq_sge	= 1;
		//TODO MAX_INLINE
		
		switch (user_parm->connection_type) {
		case RC :
			qp_create.qp_type= IB_QPT_RELIABLE_CONN;
			break;
		case UC :
			qp_create.qp_type = IB_QPT_UNRELIABLE_CONN;
			break;
		case UD :
			qp_create.qp_type = IB_QPT_UNRELIABLE_DGRM;
			break;
		default:
			fprintf(stderr, "Unknown connection type %d \n",user_parm->connection_type);
			return NULL;
		}

		qp_create.sq_signaled = FALSE;
		/*attr.sq_sig_all = 0;*/

		ib_status = ib_create_qp(ctx->pd, &qp_create,NULL,NULL,&ctx->qp[0]);
		if (ib_status != IB_SUCCESS){
			fprintf(stderr, "Couldn't create QP\n");
			return NULL;
		}
	}

	{
		ib_qp_mod_t	qp_modify;
		ib_qp_attr_t	qp_attr;
		memset(&qp_modify, 0, sizeof(ib_qp_mod_t));
		qp_modify.req_state = IB_QPS_INIT;
		qp_modify.state.init.pkey_index = 0 ;
		qp_modify.state.init.primary_port = (uint8_t)port;
		if (user_parm->connection_type==UD) {
			qp_modify.state.init.qkey = 0x11111111;
		} else {
			qp_modify.state.init.access_ctrl = IB_AC_RDMA_WRITE | IB_AC_LOCAL_WRITE;
		}
		
		ib_status = ib_modify_qp(ctx->qp[0], &qp_modify);
		if (ib_status != IB_SUCCESS){
			fprintf(stderr, "Failed to modify QP to INIT\n");
			return NULL;
		}


		memset(&qp_attr, 0, sizeof(ib_qp_attr_t));
		ib_status = ib_query_qp(ctx->qp[0], &ctx->qp_attr[0]);
		if (ib_status != IB_SUCCESS){
			fprintf(stderr, "Failed to modify QP to INIT\n");
			return NULL;
		}
	}

	
	//send
	ctx->wr.wr_id = PINGPONG_SEND_WRID;
	ctx->wr.ds_array = &ctx->list;
	ctx->wr.num_ds = 1;
	ctx->wr.wr_type = WR_SEND;
	ctx->wr.p_next       = NULL;	

	//receive
	ctx->rwr.wr_id      = PINGPONG_RECV_WRID;
	ctx->rwr.ds_array = &ctx->recv_list;
	ctx->rwr.num_ds = 1;
	ctx->rwr.p_next = NULL;
	return ctx;
}

static int pp_destroy_ctx(struct pingpong_context *ctx, struct user_parameters *user_parm)
{
	int destroy_result = 0;
		
	if (ib_destroy_qp(ctx->qp[0], NULL)) {
		fprintf(stderr, "Failed to destroy QP\n");
		destroy_result = 1;
	}

	if (ib_destroy_cq(ctx->scq, NULL)) {
		fprintf(stderr, "Failed to destroy CQ\n");
		destroy_result = 1;
	}

	if (ib_dereg_mr(ctx->mr)) {
		fprintf(stderr, "Failed to deregister MR\n");
		destroy_result = 1;
	}

	if (user_parm->connection_type == UD) {
		if (ib_destroy_av(ctx->av)) {
			fprintf(stderr, "Failed to destroy AV\n");
			destroy_result = 1;
		}
	}

	if (ib_dealloc_pd(ctx->pd, NULL)) {
		fprintf(stderr, "Failed to deallocate PD\n");
		destroy_result = 1;
	}

	if (ib_close_ca(ctx->ca, NULL)) {
		fprintf(stderr, "Failed to close ca\n");
		destroy_result = 1;
	}
	
	if (ib_close_al(ctx->al)) {
		fprintf(stderr, "Failed to close al\n");
		destroy_result = 1;
	}
	
	if (ctx)
	{
		if (ctx->qp)		free(ctx->qp);
		if (ctx->qp_attr)	free(ctx->qp_attr);
		if (ctx->buf)		free(ctx->buf);
		if (ctx->ca_attr)	free(ctx->ca_attr);

		free(ctx);
		ctx = NULL;
	}

	return destroy_result;
}

static int pp_connect_ctx(struct pingpong_context *ctx, int ib_port, int my_psn,
			  struct pingpong_dest *dest,struct user_parameters *user_parm,int index)
{
	ib_api_status_t	ib_status;
	ib_qp_mod_t	attr;
	memset(&attr, 0, sizeof(ib_qp_mod_t));

	attr.req_state 		= IB_QPS_RTR;
	switch (user_parm->mtu) {
	case 256 : 
		attr.state.rtr.primary_av.conn.path_mtu = IB_MTU_LEN_256;
		break;
	case 512 :
		attr.state.rtr.primary_av.conn.path_mtu = IB_MTU_LEN_512;
		break;
	case 1024 :
		attr.state.rtr.primary_av.conn.path_mtu = IB_MTU_LEN_1024;
		break;
	case 2048 :
		attr.state.rtr.primary_av.conn.path_mtu = IB_MTU_LEN_2048;
		break;
	}
	printf("Mtu : %d\n", user_parm->mtu);
	attr.state.rtr.dest_qp	= (dest->qpn);
	attr.state.rtr.rq_psn 	= (dest->psn);
	if (user_parm->connection_type==RC) {
		attr.state.rtr.resp_res = 1;
		attr.state.rtr.rnr_nak_timeout = 12;
	}
	attr.state.rtr.primary_av.grh_valid = 0;
	attr.state.rtr.primary_av.dlid = dest->lid;
	attr.state.rtr.primary_av.sl = 0;
	attr.state.rtr.primary_av.path_bits = 0;
	attr.state.rtr.primary_av.port_num = (uint8_t)ib_port;
	attr.state.rtr.primary_av.static_rate = IB_PATH_RECORD_RATE_10_GBS;
	attr.state.rtr.opts = IB_MOD_QP_LOCAL_ACK_TIMEOUT |
					IB_MOD_QP_RESP_RES |
					IB_MOD_QP_PRIMARY_AV;

	if(user_parm->use_grh)
	{
		attr.state.rtr.primary_av.grh_valid = 1;
		attr.state.rtr.primary_av.grh.hop_limit = 1;
		attr.state.rtr.primary_av.grh.dest_gid =dest->gid;
		attr.state.rtr.primary_av.grh.src_gid = ctx->ca_attr->p_port_attr[ib_port-1].p_gid_table[0];
	}


	ib_status = ib_modify_qp(ctx->qp[0], &attr);
	if(ib_status != IB_SUCCESS){
		fprintf(stderr, "Failed to modify UC QP to RTR\n");
		return 1;
	}


	if (user_parm->connection_type == UD) {
		ib_av_attr_t	av_attr;

		if(user_parm->use_grh){
			av_attr.grh_valid = 1;
			av_attr.grh.ver_class_flow = ib_grh_set_ver_class_flow(6, 0 ,0);
			av_attr.grh.resv1 = 0;
			av_attr.grh.resv2 = 0;
			av_attr.grh.hop_limit = 1;
			av_attr.grh.src_gid = ctx->ca_attr->p_port_attr[ib_port-1].p_gid_table[0];
			av_attr.grh.dest_gid = dest->gid;
		} else {
			av_attr.grh_valid = 0;
		}
		
		av_attr.dlid = dest->lid;
		av_attr.dlid = dest->lid;
		av_attr.sl = 0;
		av_attr.path_bits = 0;
		av_attr.port_num = (uint8_t)ib_port;
		av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
		ib_status = ib_create_av(ctx->pd,&av_attr, &ctx->av);
		if (ib_status != IB_SUCCESS) {
			fprintf(stderr, "Failed to create AH for UD\n");
			return 1;
		}
	}
	memset(&attr, 0, sizeof(ib_qp_mod_t));
	attr.req_state  = IB_QPS_RTS;
	attr.state.rts.sq_psn = my_psn;

	if (user_parm->connection_type == RC) {
		attr.state.rts.resp_res = 1;
		attr.state.rts.local_ack_timeout = 14;
		attr.state.rts.retry_cnt = 7;
		attr.state.rts.rnr_retry_cnt = 7;
		attr.state.rts.opts = IB_MOD_QP_RNR_RETRY_CNT |
						IB_MOD_QP_RETRY_CNT |
						IB_MOD_QP_LOCAL_ACK_TIMEOUT;
						
	}	
	ib_status = ib_modify_qp(ctx->qp[index], &attr);
	if(ib_status != IB_SUCCESS){
		fprintf(stderr, "Failed to modify UC QP to RTS\n");
		return 1;
	}

	
	
		/* post receive max msg size*/
	{
		int i;
		ib_recv_wr_t      *bad_wr_recv;
		//receive
		ctx->rwr.wr_id      = PINGPONG_RECV_WRID;
		ctx->rwr.ds_array = &ctx->recv_list;
		ctx->rwr.num_ds = 1;
		ctx->rwr.p_next = NULL;
		ctx->recv_list.vaddr = (uintptr_t) ctx->buf;
		if (user_parm->connection_type==UD) {
			ctx->recv_list.length = ctx->size + 40;
		} else {
			ctx->recv_list.length = ctx->size;
		}
		ctx->recv_list.lkey = ctx->lkey;
		for (i = 0; i < user_parm->tx_depth / 2; ++i) {
			if (ib_post_recv(ctx->qp[index], &ctx->rwr, &bad_wr_recv)) {
				fprintf(stderr, "Couldn't post recv: counter=%d\n", i);
				return 14;
			}
		}
	}
	return 0;
}

static SOCKET pp_open_port(struct pingpong_context *ctx, const char * servername,
			int ib_port, int port, struct pingpong_dest **p_my_dest,
			struct pingpong_dest **p_rem_dest,struct user_parameters *user_parm)
{
	struct pingpong_dest	*my_dest;
	struct pingpong_dest	*rem_dest;
	SOCKET					sockfd;
	int						rc;
	int 					i;
	int 					numofqps = 1;
	
	/* Create connection between client and server.
	 * We do it by exchanging data over a TCP socket connection. */

	
	my_dest = malloc( sizeof (struct pingpong_dest) * numofqps);
	if (!my_dest){
		perror("malloc");
		return INVALID_SOCKET;
	}
	memset( my_dest, 0, sizeof (struct pingpong_dest) * numofqps );

	rem_dest = malloc(sizeof (struct pingpong_dest) * numofqps );
	if (!rem_dest){
		perror("malloc");
		return INVALID_SOCKET;
	}
	memset( rem_dest, 0, sizeof (struct pingpong_dest) * numofqps );

	sockfd = servername ? pp_client_connect(servername, port) :
		pp_server_connect(port);

	if (sockfd  == INVALID_SOCKET) {
		printf("pp_connect_sock(%s,%d) failed (%d)!\n",
		       servername, port, sockfd);
		return INVALID_SOCKET;
	}

	
	for (i =0 ;i<numofqps;i ++) 
	{
		/* Create connection between client and server.
		* We do it by exchanging data over a TCP socket connection. */
		
		my_dest[i].lid = ctx->ca_attr->p_port_attr[ib_port-1].lid;
		my_dest[i].psn = cl_hton32(rand() & 0xffffff);
		if (ctx->ca_attr->p_port_attr[ib_port-1].transport == RDMA_TRANSPORT_IB && !my_dest[i].lid) {
			fprintf(stderr, "Local lid 0x0 detected. Is an SM running?\n");
			return 1;
		}
		my_dest[i].qpn = ctx->qp_attr[i].num;
		/* TBD this should be changed inot VA and different key to each qp */
		my_dest[i].rkey = ctx->rkey;
		my_dest[i].vaddr = (uintptr_t)ctx->buf + ctx->size;

		my_dest[i].gid = ctx->ca_attr->p_port_attr[ib_port-1].p_gid_table[0];

		printf("  local address:  LID %#04x, QPN %#06x, PSN %#06x, "
		"RKey %#08x VAddr %#016Lx\n",
		my_dest[i].lid, my_dest[i].qpn, my_dest[i].psn,
		my_dest[i].rkey, my_dest[i].vaddr);

		rc = servername ? pp_client_exch_dest(sockfd, &my_dest[i],&rem_dest[i]):
						pp_server_exch_dest(sockfd, &my_dest[i],&rem_dest[i]);
		if (rc)
			return INVALID_SOCKET;
		printf("  remote address: LID %#04x, QPN %#06x, PSN %#06x, "
		"RKey %#08x VAddr %#016Lx\n",
		rem_dest[i].lid, rem_dest[i].qpn, rem_dest[i].psn,
		rem_dest[i].rkey, rem_dest[i].vaddr);

		if(ctx->ca_attr->p_port_attr[ib_port-1].transport == RDMA_TRANSPORT_RDMAOE && user_parm->use_grh == 0) {
			printf("Using grh is forced due to the use of RoCE\n");
			user_parm->use_grh = 1;
		}

		if (pp_connect_ctx(ctx, ib_port, my_dest[i].psn, &rem_dest[i], user_parm, i))
			return INVALID_SOCKET;
		/* An additional handshake is required *after* moving qp to RTR.
		Arbitrarily reuse exch_dest for this purpose. */
		rc = servername ? pp_client_exch_dest(sockfd, &my_dest[i],&rem_dest[i]):
						pp_server_exch_dest(sockfd, &my_dest[i],&rem_dest[i]);
		if (rc)
			return INVALID_SOCKET;
	}
	*p_rem_dest = rem_dest;
	*p_my_dest = my_dest;
	return sockfd;
}



static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s            start a server and wait for connection\n", argv0);
	printf("  %s <host>     connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -p, --port=<port>            listen on/connect to port <port> (default 18515)\n");
	printf("  -d, --ib-dev=<dev>           use IB device <device guid> (default first device found)\n");
	printf("  -c, --connection=<RC/UC>     connection type RC/UC (default RC)\n");
	printf("  -m, --mtu=<mtu>              mtu size (256 - 4096. default for hermon is 2048)\n");
	printf("  -i, --ib-port=<port>         use port <port> of IB device (default 1)\n");
	printf("  -s, --size=<size>            size of message to exchange (default 1)\n");
	printf("  -t, --tx-depth=<dep>         size of tx queue (default 50)\n");
	printf("  -l, --signal                 signal completion on each msg\n");
	printf("  -a, --all                    Run sizes from 2 till 2^23 (use this parameter on both sides only)\n");
	printf("  -n, --iters=<iters>          number of exchanges (at least 2, default 1000)\n");
	printf("  -C, --report-cycles          report times in cpu cycle units (default microseconds)\n");
	printf("  -H, --report-histogram       print out all results (default print summary only)\n");
	printf("  -U, --report-unsorted        (implies -H) print out unsorted results (default sorted)\n");
	printf("  -g, --grh                    Use GRH with packets (mandatory for RoCE)\n");
	printf("  -e, --events                 Use events instead of polling\n");    
	printf("  -V, --version                display version number\n");
}



static void print_report(struct report_options * options,
			 unsigned int iters, cycles_t *tstamp,int size)
{
	double cycles_to_units;
	cycles_t median;
	unsigned int i;
	const char* units;
	cycles_t *delta = malloc(iters * sizeof *delta);

	if (!delta) {
		perror("malloc");
		return;
	}

	for (i = 0; i < iters - 1; ++i)
		delta[i] = tstamp[i + 1] - tstamp[i];


	if (options->cycles) {
		cycles_to_units = 1;
		units = "cycles";
	} else {
		cycles_to_units = get_cpu_mhz()/1000000;
		units = "usec";
	}

	if (options->unsorted) {
		printf("#, %s\n", units);
		for (i = 0; i < iters - 1; ++i)
			printf("%d, %g\n", i + 1, delta[i] / cycles_to_units / 2);
	}

	qsort(delta, iters - 1, sizeof *delta, cycles_compare);

	if (options->histogram) {
		printf("#, %s\n", units);
		for (i = 0; i < iters - 1; ++i)
			printf("%d, %g\n", i + 1, delta[i] / cycles_to_units / 2);
	}

	median = get_median(iters - 1, delta);
	printf("%7d        %d        %7.2f        %7.2f          %7.2f\n",
	       size,iters,delta[0] / cycles_to_units / 2,
	       delta[iters - 2] / cycles_to_units / 2,median / cycles_to_units / 2);
	free(delta);
}

int run_iter(struct pingpong_context *ctx, struct user_parameters *user_param,
	     struct pingpong_dest *rem_dest, int size)
{
	ib_api_status_t	ib_status;
	ib_qp_handle_t	qp;
	ib_recv_wr_t		rwr;
	ib_recv_wr_t		*bad_wr_recv;
	volatile char		*poll_buf; 
	volatile char		*post_buf;

	int				scnt, rcnt, ccnt, poll;
	int				iters;
	int				tx_depth;
	iters = user_param->iters;
	tx_depth = user_param->tx_depth;

	///send //
	if (user_param->connection_type==UD) {
		ctx->list.vaddr = (uintptr_t) ctx->buf + 40;
	} else {
		ctx->list.vaddr = (uintptr_t) ctx->buf;
	}
	ctx->list.length = size;
	ctx->list.lkey = ctx->lkey;
	if (user_param->connection_type==UD) {
		ctx->wr.dgrm.ud.h_av = ctx->av;
		ctx->wr.dgrm.ud.remote_qp = rem_dest->qpn;
		ctx->wr.dgrm.ud.remote_qkey = 0x11111111;
	}
	
	/// receive //
	rwr = ctx->rwr;
	ctx->recv_list.vaddr = (uintptr_t) ctx->buf;
	if (user_param->connection_type==UD) {
		ctx->recv_list.length = ctx->size + 40;
	} else {
		ctx->recv_list.length = ctx->size;
	}

	ctx->recv_list.lkey = ctx->lkey;

	scnt = 0;
	rcnt = 0;
	ccnt = 0;
	poll = 0;
	poll_buf = ctx->poll_buf;
	post_buf = ctx->post_buf;
	qp = ctx->qp[0];
	if ((uint32_t)size > ctx->qp_attr[0].sq_max_inline || size == 0) {/* complaince to perf_main  don't signal*/
		ctx->wr.send_opt = 0;
	} else {
		ctx->wr.send_opt = IB_SEND_OPT_INLINE;
	}

	while (scnt < iters || rcnt < iters) {
		if (rcnt < iters && !(scnt < 1 && user_param->servername)) {
			ib_wc_t	wc;
			ib_wc_t	*p_wc_done,*p_wc_free;

			p_wc_free = &wc;
			p_wc_done = NULL;
			p_wc_free->p_next = NULL;
			PERF_DEBUG("rcnt %d\n",rcnt);
			PERF_DEBUG("scnt %d\n",scnt);
			/*Server is polling on receive first */
			++rcnt;
			if (ib_post_recv(qp, &rwr, &bad_wr_recv)) {
				fprintf(stderr, "Couldn't post recv: rcnt=%d\n",
					rcnt);
				return 15;
			}

#if PORTED
			if (user_param->use_event) {
				struct ibv_cq *ev_cq;
				void			*ev_ctx;

				if (ibv_get_cq_event(ctx->channel, &ev_cq, &ev_ctx)) {
					fprintf(stderr, "Failed to get receive cq_event\n");
					return 1;
				}

				if (ev_cq != ctx->rcq) {
					fprintf(stderr, "CQ event for unknown RCQ %p\n", ev_cq);
					return 1;
				}

				if (ibv_req_notify_cq(ctx->rcq, 0)) {
					fprintf(stderr, "Couldn't request RCQ notification\n");
					return 1;
				}
			}
#endif

			if (user_param->use_event) {
				cl_status_t cl_status = cl_waitobj_wait_on( ctx->cq_waitobj, EVENT_NO_TIMEOUT, TRUE );
				if( cl_status != CL_SUCCESS )
				{
					fprintf(stderr, "cl_waitobj_wait_on() (%d)\n", cl_status);
					return 15;
				}
				ib_status = ib_poll_cq(ctx->rcq,&p_wc_free, &p_wc_done);



            } else {

    			do {
    				ib_status = ib_poll_cq(ctx->rcq,&p_wc_free, &p_wc_done);
    			} while (ib_status == IB_NOT_FOUND);
            }

			if (ib_status != IB_SUCCESS) {
				fprintf(stderr, "Poll Receive CQ failed %d\n", ib_status);
				return 12;
			}
			
			if (p_wc_done->status != IB_WCS_SUCCESS) {
				fprintf(stderr, "Receive Completion wth error at %s:\n",
					user_param->servername ? "client" : "server");
				fprintf(stderr, "Failed status %d: wr_id %d\n",
					wc.status, (int) wc.wr_id);
				fprintf(stderr, "scnt=%d, rcnt=%d, ccnt=%d\n",
					scnt, rcnt, ccnt);
				return 13;
			}
		}
		
		if (scnt < iters ) {
			ib_send_wr_t		*bad_wr;

			PERF_DEBUG("rcnt1 %d\n",rcnt);
			PERF_DEBUG("scnt1 %d\n",scnt);
			if (ccnt == (tx_depth - 2) || (user_param->signal_comp == SIGNAL)
			    || (scnt == (iters - 1)) ) {
				ccnt = 0;
				poll=1;
				if ((uint32_t)size > ctx->qp_attr[0].sq_max_inline || size == 0) {/* complaince to perf_main */
					ctx->wr.send_opt = IB_SEND_OPT_SIGNALED;
				} else {
					ctx->wr.send_opt = IB_SEND_OPT_SIGNALED | IB_SEND_OPT_INLINE;
				}

			}
			
			/* client post first */
			*post_buf = (char)++scnt;
			if (ib_post_send(qp,&ctx->wr, &bad_wr)) {
				fprintf(stderr, "Couldn't post send: scnt=%d\n",
					scnt);
				return 11;
			}

			if (user_param->use_event) {
				ib_status = ib_rearm_cq( ctx->rcq, FALSE );
				if( ib_status )
				{
					ib_destroy_cq( ctx->scq, NULL );
					fprintf(stderr,"ib_rearm_cq returned %s\n", ib_get_err_str( ib_status ));
					return 17;
				}
			}
            
			tstamp[scnt-1] = get_cycles();
		}
		if (poll == 1) {
			ib_wc_t	wc;
			ib_wc_t	*p_wc_done,*p_wc_free;

			PERF_DEBUG("rcnt2 %d\n",rcnt);
			PERF_DEBUG("scnt2 %d\n",scnt);
			p_wc_free = &wc;
			p_wc_done = NULL;
			p_wc_free->p_next = NULL;


			/* poll on scq */
			do {
				ib_status = ib_poll_cq(ctx->scq, &p_wc_free, &p_wc_done);
			} while (ib_status == IB_NOT_FOUND);

			if (ib_status != IB_SUCCESS) {
				fprintf(stderr, "Poll Receive CQ failed %d\n", ib_status);
				return 12;
			}
			
			if (wc.status != IB_WCS_SUCCESS) {
				fprintf(stderr, "Receive Completion wth error at %s:\n",
					user_param->servername ? "client" : "server");
				fprintf(stderr, "Failed status %d: wr_id %d\n",
					wc.status, (int) wc.wr_id);
				fprintf(stderr, "scnt=%d, rcnt=%d, ccnt=%d\n",
					scnt, rcnt, ccnt);
				return 13;
			}
			
			poll = 0;
			if ((uint32_t)size > ctx->qp_attr[0].sq_max_inline || size == 0) {/* complaince to perf_main don't signal*/
				ctx->wr.send_opt = 0;
			} else {
				ctx->wr.send_opt = IB_SEND_OPT_INLINE;
			}

		}
		++ccnt;
	}

	return(0);
}



int __cdecl main(int argc, char *argv[])
{

	struct pingpong_context		*ctx = NULL;
	struct pingpong_dest		*my_dest;
	struct pingpong_dest		*rem_dest;
	struct user_parameters		user_param;
	char						*ib_devguid = NULL;
	int							port = 18515;
	int							ib_port = 1;
	unsigned					size = 2;
	SOCKET						sockfd = INVALID_SOCKET;
	int							i = 0;
	int							size_max_pow = 24;
	WSADATA						wsaData;
	int							iResult; 


	struct report_options    report = {0};

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	/* init default values to user's parameters */
	memset(&user_param, 0, sizeof(struct user_parameters));
	user_param.mtu = 0;
	user_param.iters = 1000;
	user_param.tx_depth = 50;
	user_param.servername = NULL;
	user_param.use_event = 0;
	/* Parameter parsing. */
	while (1) {
		int c;

		static struct option long_options[] = {
			{ "port",				1,NULL,	'p' },
			{ "connection",			1,NULL,	'c' },
			{ "mtu",				1,NULL,	'm' },
			{ "ib-dev",				1,NULL,	'd' },
			{ "ib-port",			1,NULL,	'i' },
			{ "size",				1,NULL,	's' },
			{ "iters",				1,NULL,	'n' },
			{ "tx-depth",			1,NULL,	't' },
			{ "signal",				0,NULL,	'l' },
			{ "all",				0,NULL,	'a' },
			{ "report-cycles",		0,NULL,	'C' },
			{ "report-histogram",	0,NULL,	'H' },
			{ "report-unsorted",	0,NULL,	'U' },
			{ "events", 			0,	NULL,	'e' },
			{ "version",			0,NULL,	'V' },
			{ "grh",				0,NULL,	'g' },
			{ 0 								}
		};

		c = getopt_long(argc, argv, "p:c:m:d:i:s:n:t:laeCHUVg", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'p':
			port = strtol(optarg, NULL, 0);
			if (port < 0 || port > 65535) {
				usage(argv[0]);
				return 1;
			}
			break;
		case 'c':
			if (strcmp("RC",optarg)==0)
				user_param.connection_type=RC;
			else if (strcmp("UC",optarg)==0)
				user_param.connection_type=UC;
			else if (strcmp("UD",optarg)==0)
				user_param.connection_type=UD;
			else {
				printf("Unknown transport type %s\n", optarg);
				return 1;
			}
			break;
		case 'e':
			++user_param.use_event;
			break;            
		case 'm':
			user_param.mtu = strtol(optarg, NULL, 0);
			break;
		case 'l':
			user_param.signal_comp = SIGNAL;
			break;
		case 'a':
			user_param.all = SIGNAL;
			break;
		case 'V':
			printf("perftest version : %.2f\n",VERSION);
			return 0;
			break;
		case 'd':
			ib_devguid = _strdup(optarg);
			break;

		case 'i':
			ib_port = strtol(optarg, NULL, 0);
			if (ib_port < 0) {
				usage(argv[0]);
				return 2;
			}
			break;

		case 's':
			size = strtol(optarg, NULL, 0);
			if (size < 1) {
				usage(argv[0]); return 3;
			}
			break;

		case 't':
			user_param.tx_depth = strtol(optarg, NULL, 0);
			if (user_param.tx_depth < 1) {
				usage(argv[0]); return 4;
			}
			break;

		case 'n':
			user_param.iters = strtol(optarg, NULL, 0);
			if (user_param.iters < 2) {
				usage(argv[0]);
				return 5;
			}

			break;

		case 'C':
			report.cycles = 1;
			break;

		case 'g':
			user_param.use_grh = 1;
			break;

		case 'H':
			report.histogram = 1;
			break;

		case 'U':
			report.unsorted = 1;
			break;

		default:
			usage(argv[0]);
			return 5;
		}
	}

	if (optind == argc - 1)
		user_param.servername = _strdup(argv[optind]);
	else if (optind < argc) {
		usage(argv[0]);
		return 6;
	}

	/*
	 *  Done with parameter parsing. Perform setup.
	 */
	tstamp = malloc(user_param.iters * sizeof *tstamp);
	if (!tstamp) {
		perror("malloc");
		return 10;
	}
	/* Print header data */
	printf("------------------------------------------------------------------\n");
	printf("                    Send Latency Test\n");
	printf("Inline data is used up to 400 bytes message\n");
	if (user_param.connection_type==RC) {
		printf("Connection type : RC\n");
	} else if (user_param.connection_type==UC) { 
		printf("Connection type : UC\n");
	} else {
		printf("Connection type : UD\n");
	}

	if (user_param.use_event) {
		printf("Test with events.\n");
	}


	/* Done with parameter parsing. Perform setup. */

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != NO_ERROR) {
		printf("Error at WSAStartup()\n");
		return 1;
	}

	
	if (user_param.all == ALL && user_param.connection_type!=UD) {
		/*since we run all sizes */
		printf("test\n");
		size = 8388608; /*2^23 */
	} else if (user_param.connection_type==UD ) {
		printf("Max msg size in UD is 2048 changing to 2048\n");
		size = 2048;
	}
	
	srand(GetCurrentProcessId() * GetTickCount());

	page_size = si.dwPageSize;
	
	ctx = pp_init_ctx( size, ib_port,&user_param, ib_devguid);
	if (!ctx)
	{
		pp_destroy_ctx(ctx, &user_param);
		return 8;
	}

	sockfd = pp_open_port(ctx, user_param.servername, ib_port, port,&my_dest,&rem_dest,&user_param);
	if (sockfd == INVALID_SOCKET)
	{
		pp_destroy_ctx(ctx, &user_param);
		return 9;
	}


#if PORTED
	if (user_param.use_event) {
		printf("Test with events.\n");
		if (ibv_req_notify_cq(ctx->rcq, 0)) {
			fprintf(stderr, "Couldn't request RCQ notification\n");
			pp_destroy_ctx(ctx, &user_param);
			return 1;
		} 
		if (ibv_req_notify_cq(ctx->scq, 0)) {
			fprintf(stderr, "Couldn't request SCQ notification\n");
			pp_destroy_ctx(ctx, &user_param);
			return 1;
		}
	}
#endif
 
	printf("------------------------------------------------------------------\n");
	printf(" #bytes #iterations    t_min[usec]    t_max[usec]  t_typical[usec]\n");

	if (user_param.all == 1) {
		if (user_param.connection_type==UD) {
			size_max_pow = 12;
		}
		for (i = 1; i < size_max_pow ; ++i) {
			size = 1 << i;
			if(user_param.connection_type==UD && size > (unsigned) user_param.mtu)
			{
				break;
			}
			if(run_iter(ctx, &user_param, rem_dest, size))
			{
				pp_destroy_ctx(ctx, &user_param);
				return 17;
			}

			print_report(&report, user_param.iters, tstamp, size);
		}
	} else {
		if(run_iter(ctx, &user_param, rem_dest, size))
		{
			pp_destroy_ctx(ctx, &user_param);
			return 18;	
		}
		print_report(&report, user_param.iters, tstamp, size);
	}
	printf("------------------------------------------------------------------\n");

	send(sockfd, "done", sizeof "done",0);
	closesocket(sockfd);

	if (user_param.use_event) {
		cl_status_t cl_status;

		cl_status = cl_waitobj_destroy( ctx->cq_waitobj );
		if( cl_status != CL_SUCCESS )
		{
			fprintf (stderr,
				"cl_waitobj_destroy() returned %s\n", CL_STATUS_MSG(cl_status));
		}
	}


	free(tstamp);
	free(my_dest);
	free(rem_dest);
	return pp_destroy_ctx(ctx, &user_param);;
}
