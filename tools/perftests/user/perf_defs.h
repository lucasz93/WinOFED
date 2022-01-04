/*
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
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
 *
 * Author: Yossi Leybovich  <sleybo@mellanox.co.il>
 */

#ifndef H_PERF_SOCK_H
#define H_PERF_SOCK_H


#include <WINSOCK2.h>
#include <Ws2tcpip.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <limits.h>


#include <iba/ib_types.h>
#include <iba/ib_al.h>

#define KEY_MSG_SIZE (sizeof "0000:000000:000000:00000000:0000000000000000")
#define KEY_PRINT_FMT "%04x:%06x:%06x:%08x:%016I64x"

// The Format of the message we pass through sockets (With Gid).
#define KEY_PRINT_FMT_GID "%04x:%06x:%06x:%08x:%016I64x:%016I64x:%016I64x"

#define KEY_MSG_SIZE_GID    98 // Message size with gid (MGID as well).


#define KEY_SCAN_FMT "%x:%x:%x:%x:%x"

#define VERSION 2.0
#define ALL 1

#define RC 0
#define UC 1
#define UD 3


#define PINGPONG_SEND_WRID  1
#define PINGPONG_RECV_WRID  2
#define PINGPONG_RDMA_WRID	3


#define SIGNAL 1
#define MAX_INLINE 400


#if 0
#define PERF_ENTER 	printf("%s: ===>\n",__FUNCTION__);
#define PERF_EXIT printf("%s: <===\n",__FUNCTION__);
#define PERF_DEBUG	printf
#else
#define PERF_ENTER
#define PERF_EXIT
#define PERF_DEBUG //
#endif

struct pingpong_context {
	ib_ca_handle_t	context;
	ib_ca_handle_t	ca;
	ib_ca_attr_t		*ca_attr;
	ib_al_handle_t		al;
//PORTED	struct ibv_comp_channel *channel;
	void* 				channel; //PORTED REMOVE
	ib_pd_handle_t	pd;
	ib_mr_handle_t	mr;
	uint32_t			rkey;
	uint32_t			lkey;
	ib_cq_handle_t	scq;
	ib_cq_handle_t	rcq;
	ib_qp_handle_t	*qp;
	ib_qp_attr_t		*qp_attr;
	void				*buf;
	unsigned			size;
	int				tx_depth;

	ib_local_ds_t		list;
	ib_local_ds_t		recv_list;
	ib_send_wr_t		wr;
	ib_recv_wr_t		rwr;

	ib_av_handle_t	av;

	volatile char		*post_buf;
	volatile char		*poll_buf;

	int				*scnt,*ccnt;
	cl_waitobj_handle_t		cq_waitobj;
};


struct pingpong_dest {
	ib_net16_t	lid;
	ib_net32_t	qpn;
	ib_net32_t	psn;
	uint32_t	rkey;
	uint64_t	vaddr;
	ib_gid_t	gid;
};


struct report_options {
	int unsorted;
	int histogram;
	int cycles;   /* report delta's in cycles, not microsec's */
};


static int 
pp_write_keys(SOCKET sockfd, const struct pingpong_dest *my_dest);

static int 
pp_read_keys(SOCKET sockfd, struct pingpong_dest *rem_dest);

 SOCKET 
 pp_client_connect(const char *servername, int port);

int
pp_client_exch_dest(SOCKET sockfd, const struct pingpong_dest *my_dest,
			 	struct pingpong_dest *rem_dest);

SOCKET
pp_server_connect(int port);

int
pp_server_exch_dest(SOCKET sockfd, const struct pingpong_dest *my_dest,
 	struct pingpong_dest* rem_dest);

#endif
