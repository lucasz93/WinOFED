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
#include "perf_defs.h"
#include "get_clock.h"
#include "l2w.h"



struct user_parameters {
	const char	*servername;
	int 		connection_type;
	int 		mtu;
	int 		all; /* run all msg size */
	int 		iters;
	int 		tx_depth;
	int 		stamp_freq; /* to measure once in 'stamp_freq' iterations */
	int			use_grh;
};

static int page_size;

cycles_t                *tstamp;



void
pp_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	UNUSED_PARAM( h_cq );
	UNUSED_PARAM( cq_context);
	return ;
}


static struct pingpong_context *pp_init_ctx(unsigned size, int port, struct user_parameters *user_parm, char* ib_devguid)
{


	struct pingpong_context	*ctx;
	ib_api_status_t 			ib_status = IB_SUCCESS;	
	size_t 					guid_count;
	ib_net64_t				*ca_guid_array;
	int						guid_index = 0;

	ctx = malloc(sizeof *ctx);
	if (!ctx){
		perror("malloc");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct pingpong_context));
	ctx->size = size;
	ctx->tx_depth = user_parm->tx_depth;

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

	ctx->buf = malloc( size * 2);
	if (!ctx->buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		return NULL;
	}

	memset(ctx->buf, 0, size * 2);
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

		mr_create.length = size * 2;
			
		mr_create.vaddr = ctx->buf;
		mr_create.access_ctrl = IB_AC_RDMA_WRITE| IB_AC_LOCAL_WRITE;
		
		ib_status = ib_reg_mem(ctx->pd ,&mr_create ,&ctx->lkey ,&ctx->rkey ,&ctx->mr);
		if (ib_status != IB_SUCCESS) {
			fprintf(stderr, "Couldn't allocate MR\n");
			return NULL;
		}
	}
	
	{
		ib_cq_create_t			cq_create;
		
		cq_create.size = user_parm->tx_depth;
		cq_create.h_wait_obj = NULL;
		cq_create.pfn_comp_cb = pp_cq_comp_cb;
		ib_status = ib_create_cq(ctx->ca,&cq_create ,ctx, NULL, &ctx->scq);
		if (ib_status != IB_SUCCESS) {
			fprintf(stderr, "Couldn't create CQ ib_status = %d\n",ib_status);
			return NULL;
		}
	}
	
	{

		ib_qp_create_t	qp_create;
		ib_qp_mod_t	qp_modify;
		ib_qp_attr_t	qp_attr;
		
		memset(&qp_create, 0, sizeof(ib_qp_create_t));
		qp_create.h_sq_cq	= ctx->scq;
		qp_create.h_rq_cq	= ctx->scq;
		qp_create.sq_depth	= user_parm->tx_depth;
		qp_create.rq_depth	= 1;
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
	

	
	
		memset(&qp_modify, 0, sizeof(ib_qp_mod_t));
		qp_modify.req_state = IB_QPS_INIT;
		qp_modify.state.init.pkey_index = 0 ;
		qp_modify.state.init.primary_port = (uint8_t)port;
		qp_modify.state.init.access_ctrl = IB_AC_RDMA_WRITE | IB_AC_LOCAL_WRITE;

		
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
		fprintf(stderr, "max inline size %d\n",ctx->qp_attr[0].sq_max_inline);
	}

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
		if (ctx->qp) 		free(ctx->qp);
		if (ctx->qp_attr) 	free(ctx->qp_attr);
		if (ctx->buf) 		free(ctx->buf);
		if (ctx->ca_attr)	free(ctx->ca_attr);

		free(ctx);
		ctx = NULL;
	}

	return destroy_result;
}


static int pp_connect_ctx(struct pingpong_context *ctx, int ib_port, int my_psn,
			  struct pingpong_dest *dest, struct user_parameters *user_parm,int qpindex)
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
	attr.state.rtr.dest_qp	= dest->qpn;;
	attr.state.rtr.rq_psn 	= dest->psn;
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
			fprintf(stderr, "Failed to modify QP to RTR\n");
			return 1;
	}

	memset(&attr, 0, sizeof(ib_qp_mod_t));
	attr.req_state  = IB_QPS_RTS;
	attr.state.rts.sq_psn = my_psn;

	if (user_parm->connection_type == RC) {
		attr.state.rts.init_depth = 1;
		attr.state.rts.local_ack_timeout = 14;
		attr.state.rts.retry_cnt = 7;
		attr.state.rts.rnr_retry_cnt = 7;
		attr.state.rts.opts = IB_MOD_QP_RNR_RETRY_CNT |
						IB_MOD_QP_RETRY_CNT |
						IB_MOD_QP_INIT_DEPTH |
						IB_MOD_QP_LOCAL_ACK_TIMEOUT;
						
	}		
	ib_status = ib_modify_qp(ctx->qp[0], &attr);
	if(ib_status != IB_SUCCESS){
		fprintf(stderr, "Failed to modify QP to RTS\n");
		return 1;
	}

	return 0;

}


static SOCKET pp_open_port(struct pingpong_context *ctx, const char * servername,
			int ib_port, int port, struct pingpong_dest **p_my_dest,
			struct pingpong_dest **p_rem_dest,struct user_parameters *user_parm)
{
	struct pingpong_dest	*my_dest;
	struct pingpong_dest	*rem_dest;
	SOCKET				sockfd;
	int					rc;
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
		if (ctx->ca_attr->p_port_attr[ib_port-1].transport == RDMA_TRANSPORT_IB &&  !my_dest[i].lid) {
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
	printf("  -m, --mtu=<mtu>              mtu size (default 1024)\n");
	printf("  -i, --ib-port=<port>         use port <port> of IB device (default 1)\n");
	printf("  -s, --size=<size>            size of message to exchange (default 1)\n");
	printf("  -a, --all                    Run sizes from 2 till 2^23 (use this parameter on both sides only)\n");
	printf("  -t, --tx-depth=<dep>         size of tx queue (default 50)\n");
	printf("  -f, --freq=<dep>             frequence of taking of time stamp\n");
	printf("  -n, --iters=<iters>          number of exchanges (at least 2, default 1000)\n");
	printf("  -C, --report-cycles          report times in cpu cycle units (default microseconds)\n");
	printf("  -H, --report-histogram       print out all results (default print summary only)\n");
	printf("  -U, --report-unsorted        (implies -H) print out unsorted results (default sorted)\n");
	printf("  -g, --grh                    Use GRH with packets (mandatory for RoCE)\n");
	printf("  -V, --version                display version number\n");
}



static void print_report(struct report_options * options,
			 unsigned int full_iters, cycles_t *tstamp, int size, int freq)
{
	double cycles_to_units;
	cycles_t median;
	unsigned int i;
	const char* units;
	unsigned int iters = full_iters / freq;
	cycles_t *delta = malloc(iters * sizeof *delta);

	if (!delta) {
		perror("malloc");
		return;
	}

	for (i = 0; i < iters - 1; ++i)
		delta[i] = (tstamp[i + 1] - tstamp[i]);

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
			printf("%d, %g\n", i + 1, delta[i]  / freq / cycles_to_units / 2);
	}

	median = get_median(iters - 1, delta);
	printf("%7d        %d        %7.2f        %7.2f          %7.2f\n",
	       size,
	       iters,delta[0]  / freq / cycles_to_units / 2,
	       delta[iters - 2]  / freq / cycles_to_units / 2,
	       median  / freq / cycles_to_units / 2);

	free(delta);
}



int run_iter(struct pingpong_context *ctx, struct user_parameters *user_param,
	     struct pingpong_dest *rem_dest, int size)
{
	ib_api_status_t		ib_status;
	int                      scnt, ccnt, rcnt;
	ib_send_wr_t			*bad_wr;
	volatile char           *poll_buf; 
	volatile char           *post_buf;
	int freq = 0, tcnt;





	ctx->list.vaddr = (uintptr_t) ctx->buf ;
	ctx->list.length = size;
	ctx->list.lkey = ctx->lkey;
	ctx->wr.remote_ops.vaddr = rem_dest->vaddr;
	ctx->wr.remote_ops.rkey = rem_dest->rkey;
	ctx->wr.wr_id      = PINGPONG_RDMA_WRID;
	ctx->wr.ds_array    = &ctx->list;
	ctx->wr.num_ds    = 1;
	ctx->wr.wr_type     = WR_RDMA_WRITE;

	if ((uint32_t)size > ctx->qp_attr[0].sq_max_inline) {/* complaince to perf_main */
		ctx->wr.send_opt = IB_SEND_OPT_SIGNALED;
	} else {
		ctx->wr.send_opt = IB_SEND_OPT_SIGNALED | IB_SEND_OPT_INLINE;
	}
	scnt = 0;
	rcnt = 0;
	ccnt = 0;
	tcnt = 0;

	if(user_param->all == ALL) {
		post_buf = (char*)ctx->buf + size - 1;
		poll_buf = (char*)ctx->buf + 8388608 + size - 1;
	} else {
		poll_buf = ctx->poll_buf;
		post_buf = ctx->post_buf;
	}    

	/* Done with setup. Start the test. */
	while (scnt < user_param->iters || ccnt < user_param->iters || rcnt < user_param->iters) {

		/* Wait till buffer changes. */
		if (rcnt < user_param->iters && !(scnt < 1 && user_param->servername)) {
			++rcnt;
			while (*poll_buf != (char)rcnt)
				;
			/* Here the data is already in the physical memory.
			   If we wanted to actually use it, we may need
			   a read memory barrier here. */
		}

		if (scnt < user_param->iters) {

			*post_buf = (char)++scnt;

			ib_status = ib_post_send(ctx->qp[0], &ctx->wr, &bad_wr);
			if (ib_status != IB_SUCCESS) 
			{
				fprintf(stderr, "Couldn't post send:scnt %d ccnt=%d \n",scnt,ccnt);
				return 1;
			}
			if (++freq >= user_param->stamp_freq) {
				tstamp[tcnt++] = get_cycles();
				freq = 0;
			}
		}

		if (ccnt < user_param->iters) {
			ib_wc_t	wc;
			ib_wc_t	*p_wc_done,*p_wc_free;

			p_wc_free = &wc;
			p_wc_done = NULL;
			p_wc_free->p_next = NULL;

			do{
				ib_status = ib_poll_cq(ctx->scq, &p_wc_free, &p_wc_done);
			} while (ib_status == IB_NOT_FOUND);
			
			if (ib_status != IB_SUCCESS) {
				fprintf(stderr, "Poll Send CQ failed %d\n", ib_status);
				return 12;
			}

			if (p_wc_done->status != IB_WCS_SUCCESS) {
				fprintf(stderr, "Completion wth error at %s:\n",
				user_param->servername ? "client" : "server");
				fprintf(stderr, "Failed status %d: wr_id %d syndrom 0x%x\n",
				p_wc_done->status, (int) p_wc_done->wr_id, p_wc_done->vendor_specific);
				return 1;
			}

			++ccnt;
		}
		PERF_DEBUG("ccnt = %d \n",ccnt);
	}
	return(0);
}






int __cdecl main(int argc, char *argv[])
{
	struct pingpong_context *ctx = NULL;
	struct pingpong_dest     *my_dest;
	struct pingpong_dest     *rem_dest;
	struct user_parameters  user_param;
	char				*ib_devguid = NULL;
	int				port = 18515;
	int				ib_port = 1;
	unsigned			size = 2;
	SOCKET			sockfd = INVALID_SOCKET;
	WSADATA		wsaData;
	int				i = 0;
	int				iResult; 
	struct report_options    report = {0};

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	/* init default values to user's parameters */
	memset(&user_param, 0, sizeof(struct user_parameters));
	user_param.mtu = 0; /* signal choose default by device */
	user_param.iters = 1000;
	user_param.tx_depth = 50;
	user_param.stamp_freq = 1;
	user_param.servername = NULL;
	/* Parameter parsing. */
	while (1) {
		int c;

		static struct option long_options[] = {
			{  "port",				1, NULL, 'p' },
			{  "connection",		1, NULL, 'c' },
			{  "mtu",				1, NULL, 'm' },
			{  "ib-dev",			1, NULL, 'd' },
			{  "ib-port",			1, NULL, 'i' },
			{  "size",				1, NULL, 's' },
			{  "iters",				1, NULL, 'n' },
			{  "tx-depth",			1, NULL, 't' },
			{  "stamp_freq",		1, NULL, 'f' },
			{  "all",				0, NULL, 'a' },
			{  "report-cycles",		0, NULL, 'C' },
			{  "report-histogram",	0, NULL, 'H' },
			{  "report-unsorted",	0, NULL, 'U' },
			{  "version",			0, NULL, 'V' },
			{  "grh",				0, NULL, 'g' },
			{ 0 }
		};

		c = getopt_long(argc, argv, "p:c:m:d:i:s:n:t:f:aCHUVg", long_options, NULL);
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
			if (strcmp("UC",optarg)==0)
				user_param.connection_type=UC;
			break;

		case 'm':
			user_param.mtu = strtol(optarg, NULL, 0);
			break;
		case 'a':
			user_param.all = ALL;
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

		case 'H':
			report.histogram = 1;
			break;

		case 'U':
			report.unsorted = 1;
			break;

		case 'f':
			user_param.stamp_freq = strtol(optarg, NULL, 0);
			break;

		case 'g':
			user_param.use_grh = 1;
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
	printf("------------------------------------------------------------------\n");
	printf("                    RDMA_Write Latency Test\n");
	if (user_param.connection_type==0) {
		printf("Connection type : RC\n");
	} else {
		printf("Connection type : UC\n");
	}



	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != NO_ERROR) {
		printf("Error at WSAStartup()\n");
		return 1;
	}


	if (user_param.all == ALL) {
		/*since we run all sizes lets allocate big enough buffer */
		size = 8388608; /*2^23 */
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

	printf("------------------------------------------------------------------\n");
	printf(" #bytes #iterations    t_min[usec]    t_max[usec]  t_typical[usec]\n");

	if (user_param.all == ALL) {
		for (i = 1; i < 24 ; ++i) {
			size = 1 << i;
			if(run_iter(ctx, &user_param, rem_dest, size))
			{
				pp_destroy_ctx(ctx, &user_param);
				return 17;
			}
			print_report(&report, user_param.iters, tstamp, size, user_param.stamp_freq);
		}
	} else {
		if(run_iter(ctx, &user_param, rem_dest, size))
		{
			pp_destroy_ctx(ctx, &user_param);
			return 18;
		}
		print_report(&report, user_param.iters, tstamp, size, user_param.stamp_freq);
	}
	send(sockfd, "done", sizeof "done",0);
	closesocket(sockfd);
	
	
	printf("------------------------------------------------------------------\n");
	free(tstamp);
	free(my_dest);
	free(rem_dest);
	return pp_destroy_ctx(ctx, &user_param);
}