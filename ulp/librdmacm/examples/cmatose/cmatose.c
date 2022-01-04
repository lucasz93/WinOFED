/*
 * Copyright (c) 2005-2009 Intel Corporation.  All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws2tcpip.h>
#include <winsock2.h>
#include <time.h>

#include "..\..\..\..\etc\user\getopt.c"
#include <rdma/rdma_cma.h>

struct cmatest_node {
	int					id;
	struct rdma_cm_id	*cma_id;
	int					connected;
	struct ibv_pd		*pd;
	struct ibv_cq		*cq[2];
	struct ibv_mr		*mr;
	void				*mem;
	LARGE_INTEGER		start_time[4];
	LARGE_INTEGER		end_time[4];
};

enum cq_index {
	SEND_CQ_INDEX,
	RECV_CQ_INDEX
};

struct cmatest {
	struct rdma_event_channel	*channel;
	struct cmatest_node			*nodes;
	int							conn_index;
	int							connects_left;
	int							disconnects_left;

	struct sockaddr_in			dst_in;
	struct sockaddr				*dst_addr;
	struct sockaddr_in			src_in;
	struct sockaddr				*src_addr;
};

static struct cmatest test;
static int connections = 1;
static int message_size = 100;
static int message_count = 10;
static uint16_t port = 7471;
static uint8_t set_tos = 0;
static uint8_t tos;
static uint8_t migrate = 0;
static char *dst_addr;
static char *src_addr;
static LARGE_INTEGER start_time[2], end_time[2];

static int create_message(struct cmatest_node *node)
{
	if (!message_size)
		message_count = 0;

	if (!message_count)
		return 0;

	node->mem = malloc(message_size);
	if (!node->mem) {
		printf("failed message allocation\n");
		return -1;
	}
	node->mr = ibv_reg_mr(node->pd, node->mem, message_size,
			     IBV_ACCESS_LOCAL_WRITE);
	if (!node->mr) {
		printf("failed to reg MR\n");
		goto err;
	}
	return 0;
err:
	free(node->mem);
	return -1;
}

static int init_node(struct cmatest_node *node)
{
	struct ibv_qp_init_attr init_qp_attr;
	int cqe, ret;

	node->pd = ibv_alloc_pd(node->cma_id->verbs);
	if (!node->pd) {
		ret = -1;
		printf("cmatose: unable to allocate PD\n");
		goto out;
	}

	cqe = message_count ? message_count : 1;
	node->cq[SEND_CQ_INDEX] = ibv_create_cq(node->cma_id->verbs, cqe, node, 0, 0);
	node->cq[RECV_CQ_INDEX] = ibv_create_cq(node->cma_id->verbs, cqe, node, 0, 0);
	if (!node->cq[SEND_CQ_INDEX] || !node->cq[RECV_CQ_INDEX]) {
		ret = -1;
		printf("cmatose: unable to create CQs\n");
		goto out;
	}

	memset(&init_qp_attr, 0, sizeof init_qp_attr);
	init_qp_attr.cap.max_send_wr = cqe;
	init_qp_attr.cap.max_recv_wr = cqe;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.qp_context = node;
	init_qp_attr.sq_sig_all = 1;
	init_qp_attr.qp_type = IBV_QPT_RC;
	init_qp_attr.send_cq = node->cq[SEND_CQ_INDEX];
	init_qp_attr.recv_cq = node->cq[RECV_CQ_INDEX];
	ret = rdma_create_qp(node->cma_id, node->pd, &init_qp_attr);
	if (ret) {
		printf("cmatose: unable to create QP: 0x%x\n", ret);
		goto out;
	}

	ret = create_message(node);
	if (ret) {
		printf("cmatose: failed to create messages: 0x%x\n", ret);
		goto out;
	}
out:
	return ret;
}

static int post_recvs(struct cmatest_node *node)
{
	struct ibv_recv_wr recv_wr, *recv_failure;
	struct ibv_sge sge;
	int i, ret = 0;

	if (!message_count)
		return 0;

	recv_wr.next = NULL;
	recv_wr.sg_list = &sge;
	recv_wr.num_sge = 1;
	recv_wr.wr_id = (uintptr_t) node;

	sge.length = message_size;
	sge.lkey = node->mr->lkey;
	sge.addr = (uintptr_t) node->mem;

	for (i = 0; i < message_count && !ret; i++ ) {
		ret = ibv_post_recv(node->cma_id->qp, &recv_wr, &recv_failure);
		if (ret) {
			printf("failed to post receives: 0x%x\n", ret);
			break;
		}
	}
	return ret;
}

static int post_sends(struct cmatest_node *node)
{
	struct ibv_send_wr send_wr, *bad_send_wr;
	struct ibv_sge sge;
	int i, ret = 0;

	if (!node->connected || !message_count)
		return 0;

	send_wr.next = NULL;
	send_wr.sg_list = &sge;
	send_wr.num_sge = 1;
	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = 0;
	send_wr.wr_id = (ULONG_PTR) node;

	sge.length = message_size;
	sge.lkey = node->mr->lkey;
	sge.addr = (uintptr_t) node->mem;

	for (i = 0; i < message_count && !ret; i++) {
		ret = ibv_post_send(node->cma_id->qp, &send_wr, &bad_send_wr);
		if (ret) 
			printf("failed to post sends: 0x%x\n", ret);
	}
	return ret;
}

static void connect_error(void)
{
	test.disconnects_left--;
	test.connects_left--;
}

static int addr_handler(struct cmatest_node *node)
{
	int ret;

	QueryPerformanceCounter(&node->end_time[0]);
	if (set_tos) {
		ret = rdma_set_option(node->cma_id, RDMA_OPTION_ID,
				      RDMA_OPTION_ID_TOS, &tos, sizeof tos);
		if (ret)
			printf("cmatose: set TOS option failed: 0x%x\n", ret);
	}

	QueryPerformanceCounter(&node->start_time[1]);
	ret = rdma_resolve_route(node->cma_id, 2000);
	if (ret) {
		printf("cmatose: resolve route failed: 0x%x\n", ret);
		connect_error();
	}
	return ret;
}

static int route_handler(struct cmatest_node *node)
{
	struct rdma_conn_param conn_param;
	int ret;

	QueryPerformanceCounter(&node->end_time[1]);
	QueryPerformanceCounter(&node->start_time[2]);
	ret = init_node(node);
	if (ret)
		goto err;

	ret = post_recvs(node);
	if (ret)
		goto err;
	QueryPerformanceCounter(&node->end_time[2]);

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = 5;
	QueryPerformanceCounter(&node->start_time[3]);
	ret = rdma_connect(node->cma_id, &conn_param);
	if (ret) {
		printf("cmatose: failure connecting: 0x%x\n", ret);
		goto err;
	}
	return 0;
err:
	connect_error();
	return ret;
}

static int connect_handler(struct rdma_cm_id *cma_id)
{
	struct cmatest_node *node;
	struct rdma_conn_param conn_param;
	int ret;

	if (test.conn_index == connections) {
		ret = -1;
		goto err1;
	}
	node = &test.nodes[test.conn_index++];

	node->cma_id = cma_id;
	cma_id->context = node;

	QueryPerformanceCounter(&node->start_time[2]);
	ret = init_node(node);
	if (ret)
		goto err2;

	ret = post_recvs(node);
	if (ret)
		goto err2;
	QueryPerformanceCounter(&node->end_time[2]);

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	QueryPerformanceCounter(&node->start_time[3]);
	ret = rdma_accept(node->cma_id, &conn_param);
	if (ret) {
		printf("cmatose: failure accepting: 0x%x\n", ret);
		goto err2;
	}
	return 0;

err2:
	node->cma_id = NULL;
	connect_error();
err1:
	printf("cmatose: failing connection request\n");
	rdma_reject(cma_id, NULL, 0);
	return ret;
}

static int cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	int ret = 0;

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		ret = addr_handler(cma_id->context);
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ret = route_handler(cma_id->context);
		break;
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = connect_handler(cma_id);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		((struct cmatest_node *) cma_id->context)->connected = 1;
		QueryPerformanceCounter(&((struct cmatest_node *) cma_id->context)->end_time[3]);
		test.connects_left--;
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		printf("cmatose: event: %s, error: 0x%x\n",
		       rdma_event_str(event->event), event->status);
		connect_error();
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		rdma_disconnect(cma_id);
		test.disconnects_left--;
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		/* Cleanup will occur after test completes. */
		break;
	default:
		break;
	}
	return ret;
}

static void destroy_node(struct cmatest_node *node)
{
	if (!node->cma_id)
		return;

	if (node->cma_id->qp)
		rdma_destroy_qp(node->cma_id);

	if (node->cq[SEND_CQ_INDEX])
		ibv_destroy_cq(node->cq[SEND_CQ_INDEX]);

	if (node->cq[RECV_CQ_INDEX])
		ibv_destroy_cq(node->cq[RECV_CQ_INDEX]);

	if (node->mem) {
		ibv_dereg_mr(node->mr);
		free(node->mem);
	}

	if (node->pd)
		ibv_dealloc_pd(node->pd);

	/* Destroy the RDMA ID after all device resources */
	rdma_destroy_id(node->cma_id);
}

static int alloc_nodes(void)
{
	int ret, i;

	test.nodes = malloc(sizeof *test.nodes * connections);
	if (!test.nodes) {
		printf("cmatose: unable to allocate memory for test nodes\n");
		return -1;
	}
	memset(test.nodes, 0, sizeof *test.nodes * connections);

	for (i = 0; i < connections; i++) {
		test.nodes[i].id = i;
		if (dst_addr) {
			ret = rdma_create_id(test.channel,
					     &test.nodes[i].cma_id,
					     &test.nodes[i], RDMA_PS_TCP);
			if (ret)
				goto err;
		}
	}
	return 0;
err:
	while (--i >= 0)
		rdma_destroy_id(test.nodes[i].cma_id);
	free(test.nodes);
	return ret;
}

static void destroy_nodes(void)
{
	int i;

	for (i = 0; i < connections; i++)
		destroy_node(&test.nodes[i]);
	free(test.nodes);
}

static int poll_cqs(enum CQ_INDEX index)
{
	struct ibv_wc wc[8];
	int done, i, ret;

	for (i = 0; i < connections; i++) {
		if (!test.nodes[i].connected)
			continue;

		for (done = 0; done < message_count; done += ret) {
			ret = ibv_poll_cq(test.nodes[i].cq[index], 8, wc);
			if (ret < 0) {
				printf("cmatose: failed polling CQ: 0x%x\n", ret);
				return ret;
			}
			Sleep(0);
		}
	}
	return 0;
}

static int connect_events(void)
{
	struct rdma_cm_event *event;
	int err = 0, ret = 0;

	while (test.connects_left && !err) {
		err = rdma_get_cm_event(test.channel, &event);
		if (!err) {
			if (!dst_addr && !start_time[0].QuadPart)
				QueryPerformanceCounter(&start_time[0]);

			cma_handler(event->id, event);
			rdma_ack_cm_event(event);
		} else {
			printf("cmatose:rdma_get_cm_event connect events error 0x%x\n", err);
			ret = err;
		}
	}
	QueryPerformanceCounter(&end_time[0]);

	return ret;
}

static int disconnect_events(void)
{
	struct rdma_cm_event *event;
	int err = 0, ret = 0;

	while (test.disconnects_left && !err) {
		err = rdma_get_cm_event(test.channel, &event);
		if (!err) {
			if (dst_addr && !start_time[1].QuadPart)
				QueryPerformanceCounter(&start_time[1]);

			cma_handler(event->id, event);
			rdma_ack_cm_event(event);
		} else {
			printf("cmatose: rdma_get_cm_event disconnect events error 0x%x\n", err);
			ret = err;
		}
	}
	QueryPerformanceCounter(&end_time[1]);

	return ret;
}

static int migrate_channel(struct rdma_cm_id *listen_id)
{
	struct rdma_event_channel *channel;
	int i, ret;

	printf("migrating to new event channel\n");

	channel = rdma_create_event_channel();
	if (!channel) {
		printf("cmatose: failed to create event channel\n");
		return -1;
	}

	ret = 0;
	if (listen_id)
		ret = rdma_migrate_id(listen_id, channel);

	for (i = 0; i < connections && !ret; i++)
		ret = rdma_migrate_id(test.nodes[i].cma_id, channel);

	if (!ret) {
		rdma_destroy_event_channel(test.channel);
		test.channel = channel;
	} else
		printf("cmatose: failure migrating to channel: 0x%x\n", ret);

	return ret;
}

static int get_addr(char *dst, struct sockaddr_in *addr)
{
	struct addrinfo *res;
	int ret;

	ret = getaddrinfo(dst, NULL, NULL, &res);
	if (ret) {
		printf("getaddrinfo failed - invalid hostname or IP address\n");
		return ret;
	}

	if (res->ai_family != PF_INET) {
		ret = -1;
		goto out;
	}

	*addr = *(struct sockaddr_in *) res->ai_addr;
out:
	freeaddrinfo(res);
	return ret;
}

static int run_server(void)
{
	struct rdma_cm_id *listen_id;
	int i, ret;

	printf("cmatose: starting server\n");
	ret = rdma_create_id(test.channel, &listen_id, &test, RDMA_PS_TCP);
	if (ret) {
		printf("cmatose: listen request failed\n");
		return ret;
	}

	if (src_addr) {
		ret = get_addr(src_addr, &test.src_in);
		if (ret)
			goto out;
	} else
		test.src_in.sin_family = PF_INET;

	test.src_in.sin_port = port;
	ret = rdma_bind_addr(listen_id, test.src_addr);
	if (ret) {
		printf("cmatose: bind address failed: 0x%x\n", ret);
		goto out;
	}

	ret = rdma_listen(listen_id, connections);
	if (ret) {
		printf("cmatose: failure trying to listen: 0x%x\n", ret);
		goto out;
	}

	ret = connect_events();
	if (ret)
		goto out;

	if (message_count) {
		printf("initiating data transfers\n");
		for (i = 0; i < connections; i++) {
			ret = post_sends(&test.nodes[i]);
			if (ret)
				goto out;
		}

		printf("completing sends\n");
		ret = poll_cqs(SEND_CQ_INDEX);
		if (ret)
			goto out;

		printf("receiving data transfers\n");
		ret = poll_cqs(RECV_CQ_INDEX);
		if (ret)
			goto out;
		printf("data transfers complete\n");

	}

	if (migrate) {
		ret = migrate_channel(listen_id);
		if (ret)
			goto out;
	}

	printf("cmatose: disconnecting\n");
	QueryPerformanceCounter(&start_time[1]);

	for (i = 0; i < connections; i++) {
		if (!test.nodes[i].connected)
			continue;

		test.nodes[i].connected = 0;
		rdma_disconnect(test.nodes[i].cma_id);
	}

	ret = disconnect_events();

 	printf("disconnected\n");

out:
	rdma_destroy_id(listen_id);
	return ret;
}

static int run_client(void)
{
	int i, ret, ret2;

	printf("cmatose: starting client\n");
	if (src_addr) {
		ret = get_addr(src_addr, &test.src_in);
		if (ret)
			return ret;
	}

	ret = get_addr(dst_addr, &test.dst_in);
	if (ret)
		return ret;

	test.dst_in.sin_port = port;

	printf("cmatose: connecting\n");
	QueryPerformanceCounter(&start_time[0]);

	for (i = 0; i < connections; i++) {
		QueryPerformanceCounter(&test.nodes[i].start_time[0]);
		ret = rdma_resolve_addr(test.nodes[i].cma_id,
					src_addr ? test.src_addr : NULL,
					test.dst_addr, 2000);
		if (ret) {
			printf("cmatose: failure getting addr: 0x%x\n", ret);
			connect_error();
			return ret;
		}
	}

	ret = connect_events();
	if (ret)
		goto disc;

	if (message_count) {
		printf("receiving data transfers\n");
		ret = poll_cqs(RECV_CQ_INDEX);
		if (ret)
			goto disc;

		printf("sending replies\n");
		for (i = 0; i < connections; i++) {
			ret = post_sends(&test.nodes[i]);
			if (ret)
				goto disc;
		}

		printf("data transfers complete\n");
	}

	ret = 0;

	if (migrate) {
		ret = migrate_channel(NULL);
		if (ret)
			goto out;
	}

disc:
	ret2 = disconnect_events();
	if (ret2)
		ret = ret2;
out:
	return ret;
}

static UINT64 sum_counters(int index)
{
	UINT64 total = 0;
	int i;

	for (i = 0; i < connections; i++) {
		total += (test.nodes[i].end_time[index].QuadPart -
				  test.nodes[i].start_time[index].QuadPart);
	}
	return total;
}

static void show_perf(void)
{
	LARGE_INTEGER freq;
	double run_time;
	int i;

	QueryPerformanceFrequency(&freq);
	run_time = (double) (end_time[0].QuadPart - start_time[0].QuadPart) /
			   (double) freq.QuadPart;
	printf("%d connection%s in %.4f seconds (%.0f connections/second)\n",
		   connections, connections == 1 ? "" : "s", run_time,
		   (double) connections / run_time);

	run_time = (double) (end_time[1].QuadPart - start_time[1].QuadPart) /
			   (double) freq.QuadPart;
	printf("%d disconnect%s in %.4f seconds (%.0f disconnects/second)\n",
		   connections, connections == 1 ? "" : "s", run_time,
		   (double) connections / run_time);

	if (dst_addr) {
		run_time = (double) sum_counters(0) / (double) freq.QuadPart;
		printf("sum resolve address times %.4f seconds (%.2f ms average)\n",
			   run_time, run_time * 1000 / (double) connections);

		run_time = (double) sum_counters(1) / (double) freq.QuadPart;
		printf("sum resolve route times %.4f seconds (%.2f ms average)\n",
			   run_time, run_time * 1000 / (double) connections);
	}

	run_time = (double) sum_counters(2) / (double) freq.QuadPart;
	printf("sum initialize node times %.4f seconds (%.2f ms average)\n",
		   run_time, run_time * 1000 / (double) connections);

	run_time = (double) sum_counters(3) / (double) freq.QuadPart;
	printf("sum connect/accept times %.4f seconds (%.2f ms average)\n",
		   run_time, run_time * 1000 / (double) connections);

	if (dst_addr) {
		run_time = (double) (sum_counters(3) - sum_counters(2)) / (double) freq.QuadPart;
		printf("est. adjusted connect times %.4f seconds (%.2f ms average)\n",
			   run_time, run_time * 1000 / (double) connections);
	}
}

int __cdecl main(int argc, char **argv)
{
	int op, ret;

	while ((op = getopt(argc, argv, "s:b:c:C:S:t:p:m")) != -1) {
		switch (op) {
		case 's':
			dst_addr = optarg;
			break;
		case 'b':
			src_addr = optarg;
			break;
		case 'c':
			connections = atoi(optarg);
			break;
		case 'C':
			message_count = atoi(optarg);
			break;
		case 'S':
			message_size = atoi(optarg);
			break;
		case 't':
			set_tos = 1;
			tos = (uint8_t) atoi(optarg);
			break;
		case 'p':
			port = (uint16_t) atoi(optarg);
			break;
		case 'm':
			migrate = 1;
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-s server_address]\n");
			printf("\t[-b bind_address]\n");
			printf("\t[-c connections]\n");
			printf("\t[-C message_count]\n");
			printf("\t[-S message_size]\n");
			printf("\t[-t type_of_service]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-m(igrate)]\n");
			exit(1);
		}
	}

	test.dst_addr = (struct sockaddr *) &test.dst_in;
	test.src_addr = (struct sockaddr *) &test.src_in;
	test.connects_left = connections;
	test.disconnects_left = connections;

	test.channel = rdma_create_event_channel();
	if (!test.channel) {
		printf("failed to create event channel\n");
		exit(1);
	}

	if (alloc_nodes())
		exit(1);

	if (dst_addr)
		ret = run_client();
	else
		ret = run_server();

	printf("test complete\n");
	show_perf();

	destroy_nodes();
	rdma_destroy_event_channel(test.channel);

	printf("return status 0x%x\n", ret);
	return ret;
}
