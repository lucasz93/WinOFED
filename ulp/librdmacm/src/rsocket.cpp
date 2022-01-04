/*
 * Copyright (c) 2008-2012 Intel Corporation.  All rights reserved.
 * Copyright (c) 2012 Oce Printing Systems GmbH.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
 *      - Neither the name Oce Printing Systems GmbH nor the names
 *        of the authors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 * THIS SOFTWARE IS PROVIDED  “AS IS” AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
 * OR CONTRIBUTOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE. 
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <windows.h>
#include "openib_osd.h"
#include <sys/time.h>
#include <stdarg.h>
#include <netdb.h>
#include <unistd.h>
#include <_fcntl.h>
#include <stdio.h>
#include <string.h>
#include <_errno.h>
#include <complib/cl_atomic.h>
#include <complib/cl_byteswap.h>
#include <dlist.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <rdma/rsocket.h>
#include "cma.h"
#include "indexer.h"

#define dlist_entry       DLIST_ENTRY
#define dlist_init        DListInit
#define dlist_empty       DListEmpty
#define dlist_remove      DListRemove
#define dlist_insert_head DListInsertBefore
#define dlist_insert_tail DListInsertAfter

#define RS_OLAP_START_SIZE 2048
#define RS_MAX_TRANSFER 65536
#define RS_SNDLOWAT 64
#define RS_MAX_BACKLOG  256
#define RS_QP_MAX_SIZE 0xFFFE
#define RS_QP_CTRL_SIZE 4
#define RS_CONN_RETRIES 6
#define RS_SGL_SIZE 2

static struct index_map idm;
static uint16_t def_iomap_size = 0;
static uint16_t def_inline = 64;
static uint16_t def_sqsize = 384;
static uint16_t def_rqsize = 384;
static uint32_t def_mem = (1 << 17);
static uint32_t def_wmem = (1 << 17);
static uint32_t polling_time = 10;

extern __declspec(thread) int WSAErrno;

extern fastlock_t		mut;
extern BOOL				gDetached;			// Indicates if process is detaching from DLL
extern CRITICAL_SECTION gCriticalSection;   // Critical section for initialization and 
extern WSPUPCALLTABLE   gMainUpCallTable;   // Upcall functions given to us by Winsock
extern WSAPROTOCOL_INFOW gProtocolInfo;

/*
 * Immediate data format is determined by the upper bits
 * bit 31: message type, 0 - data, 1 - control
 * bit 30: buffers updated, 0 - target, 1 - direct-receive
 * bit 29: more data, 0 - end of transfer, 1 - more data available
 *
 * for data transfers:
 * bits [28:0]: bytes transferred
 * for control messages:
 * SGL, CTRL
 * bits [28-0]: receive credits granted
 * IOMAP_SGL
 * bits [28-16]: reserved, bits [15-0]: index
 */

enum {
	RS_OP_DATA,
	RS_OP_RSVD_DATA_MORE,
	RS_OP_WRITE, /* opcode is not transmitted over the network */
	RS_OP_RSVD_DRA_MORE,
	RS_OP_SGL,
	RS_OP_RSVD,
	RS_OP_IOMAP_SGL,
	RS_OP_CTRL
};
#define rs_msg_set(op, data)  ((op << 29) | (uint32_t) (data))
#define rs_msg_op(imm_data)   (imm_data >> 29)
#define rs_msg_data(imm_data) (imm_data & 0x1FFFFFFF)

enum {
	RS_CTRL_DISCONNECT,
	RS_CTRL_SHUTDOWN
};

struct rs_msg {
	uint32_t op;
	uint32_t data;
};

struct rs_sge {
	uint64_t addr;
	uint32_t key;
	uint32_t length;
};

struct rs_iomap {
	uint64_t offset;
	struct rs_sge sge;
};

struct rs_iomap_mr {
	uint64_t offset;
	struct ibv_mr *mr;
	dlist_entry entry;
	atomic32_t refcnt;
	int index;	/* -1 if mapping is local and not in iomap_list */
};

#define RS_MIN_INLINE      (sizeof(struct rs_sge))
#define rs_host_is_net()   (1 == htonl(1))
#define RS_CONN_FLAG_NET   (1 << 0)
#define RS_CONN_FLAG_IOMAP (1 << 1)

struct rs_conn_data {
	uint8_t       version;
	uint8_t       flags;
	uint16_t      credits;
	uint8_t       reserved[3];
	uint8_t       target_iomap_size;
	struct rs_sge target_sgl;
	struct rs_sge data_buf;
};

#define RS_RECV_WR_ID (~((uint64_t) 0))

/*
 * rsocket states are ordered as passive, connecting, connected, disconnected.
 */
enum rs_state {
	rs_init,
	rs_bound	   =		    0x0001,
	rs_listening	   =		    0x0002,
	rs_opening	   =		    0x0004,
	rs_resolving_addr  = rs_opening |   0x0010,
	rs_resolving_route = rs_opening |   0x0020,
	rs_connecting      = rs_opening |   0x0040,
	rs_accepting       = rs_opening |   0x0080,
	rs_connected	   =		    0x0100,
	rs_connect_wr 	   =		    0x0200,
	rs_connect_rd	   =		    0x0400,
	rs_connect_rdwr    = rs_connected | rs_connect_rd | rs_connect_wr,
	rs_connect_error   =		    0x0800,
	rs_disconnected	   =		    0x1000,
	rs_error	   =		    0x2000,
};

#define RS_OPT_SWAP_SGL 1

struct rsocket {
	struct rdma_cm_id *cm_id;
	fastlock_t	  slock;
	fastlock_t	  rlock;
	fastlock_t	  cq_lock;
	fastlock_t	  cq_wait_lock;
	fastlock_t	  iomap_lock;

	int		  opts;
	long		  fd_flags;
	uint64_t	  so_opts;
	uint64_t	  tcp_opts;
	uint64_t	  ipv6_opts;
	int		  state;
	int		  cq_armed;
	int		  retries;
	int		  err;
	int		  index;
	int		  ctrl_avail;
	int		  sqe_avail;
	int		  sbuf_bytes_avail;
	uint16_t	  sseq_no;
	uint16_t	  sseq_comp;
	uint16_t	  sq_size;
	uint16_t	  sq_inline;

	uint16_t	  rq_size;
	uint16_t	  rseq_no;
	uint16_t	  rseq_comp;
	int		  rbuf_bytes_avail;
	int		  rbuf_free_offset;
	int		  rbuf_offset;
	int		  rmsg_head;
	int		  rmsg_tail;
	struct rs_msg	  *rmsg;

	int		  remote_sge;
	struct rs_sge	  remote_sgl;
	struct rs_sge	  remote_iomap;

	struct rs_iomap_mr *remote_iomappings;
	dlist_entry	  iomap_list;
	dlist_entry	  iomap_queue;
	int		  iomap_pending;

	struct ibv_mr          *target_mr;
	int                     target_sge;
	int                     target_iomap_size;
	void		           *target_buffer_list;
	volatile struct rs_sge *target_sgl;
	struct rs_iomap        *target_iomap;

	uint32_t	  rbuf_size;
	struct ibv_mr	 *rmr;
	uint8_t		  *rbuf;

	uint32_t	  sbuf_size;
	struct ibv_mr	 *smr;
	struct ibv_sge	  ssgl[2];
	uint8_t		  *sbuf;
	RS_NETSTAT_ENTRY *pNetstatEntry;
};

static char* rs_state_string (int state)
{
	char *pst;
	
	switch (state) {
	case rs_init:			pst = "INIT";			break;
	case rs_bound:			pst = "BOUND";			break;
	case rs_listening:		pst = "LISTENING";		break;
	case rs_resolving_addr:	pst = "RESOLVING_ADDR";	break;
	case rs_resolving_route:pst = "RESOLVING_ROUTE";break;
	case rs_connecting:		pst = "CONNECTING";		break;
	case rs_accepting:		pst = "ACCEPTING";		break;
	case rs_opening:		pst = "OPENING";		break;
	case rs_connect_rdwr:	pst = "CONNECT_RDWR";	break;
	case rs_connected:		pst = "CONNECTED";		break;
	case rs_connect_wr:		pst = "CONNECT_WR";		break;
	case rs_connect_rd:		pst = "CONNECT_RD";		break;
	case rs_connect_error:	pst = "CONNECT_ERROR";	break;
	case rs_disconnected:	pst = "DISCONNECTED";	break;
	case rs_error:			pst = "ERROR";			break;
	default:				pst = "UNKNOWN";
	}
	return pst;
}

static void rs_set_state (struct rsocket *rs, int state)
{
	rs->state = state;
	if (rs->pNetstatEntry) {
		strncpy(
			rs->pNetstatEntry->szState,
			rs_state_string(state),
			sizeof(rs->pNetstatEntry->szState)-1
		);
	}
}

static int rs_value_to_scale(int value, int bits)
{
	return value <= (1 << (bits - 1)) ?
	       value : (1 << (bits - 1)) | (value >> bits);
}

static int rs_scale_to_value(int value, int bits)
{
	return value <= (1 << (bits - 1)) ?
	       value : (value & ~(1 << (bits - 1))) << bits;
}

static void rs_configure(void)
{
	FILE *f;
	static int init;

	if (init)
		return;

	fastlock_acquire(&mut);
	if (init)
		goto out;

	if ((f = fopen(RS_CONF_DIR "/polling_time", "r"))) {
		fscanf(f, "%u", &polling_time);
		fclose(f);
	}

	if ((f = fopen(RS_CONF_DIR "/inline_default", "r"))) {
		fscanf(f, "%hu", &def_inline);
		fclose(f);

		if (def_inline < RS_MIN_INLINE)
			def_inline = RS_MIN_INLINE;
	}

	if ((f = fopen(RS_CONF_DIR "/sqsize_default", "r"))) {
		fscanf(f, "%hu", &def_sqsize);
		fclose(f);
	}

	if ((f = fopen(RS_CONF_DIR "/rqsize_default", "r"))) {
		fscanf(f, "%hu", &def_rqsize);
		fclose(f);
	}

	if ((f = fopen(RS_CONF_DIR "/mem_default", "r"))) {
		fscanf(f, "%u", &def_mem);
		fclose(f);

		if (def_mem < 1)
			def_mem = 1;
	}

	if ((f = fopen(RS_CONF_DIR "/wmem_default", "r"))) {
		fscanf(f, "%u", &def_wmem);
		fclose(f);
		if (def_wmem < RS_SNDLOWAT)
			def_wmem = RS_SNDLOWAT << 1;
	}

	if ((f = fopen(RS_CONF_DIR "/iomap_size", "r"))) {
		fscanf(f, "%hu", &def_iomap_size);
		fclose(f);

		/* round to supported values */
		def_iomap_size = (uint8_t) rs_value_to_scale(
			(uint16_t) rs_scale_to_value(def_iomap_size, 8), 8);
	}
	init = 1;
out:
	fastlock_release(&mut);
}

static int rs_insert(struct rsocket *rs)
{
	fastlock_acquire(&mut);
	rs->index = idm_set(&idm, ((int)rs->cm_id->channel->channel.Event >> 2), rs);
	fastlock_release(&mut);
	return rs->index;
}

static void rs_remove(struct rsocket *rs)
{
	fastlock_acquire(&mut);
	idm_clear(&idm, rs->index);
	fastlock_release(&mut);
}

static struct rsocket *rs_alloc(struct rsocket *inherited_rs)
{
	struct rsocket *rs;

	rs = (struct rsocket *)calloc(1, sizeof *rs);
	if (!rs)
		return NULL;

	rs->index = -1;
	if (inherited_rs) {
		rs->sbuf_size = inherited_rs->sbuf_size;
		rs->rbuf_size = inherited_rs->rbuf_size;
		rs->sq_inline = inherited_rs->sq_inline;
		rs->sq_size = inherited_rs->sq_size;
		rs->rq_size = inherited_rs->rq_size;
		rs->ctrl_avail = inherited_rs->ctrl_avail;
		rs->target_iomap_size = inherited_rs->target_iomap_size;
	} else {
		rs->sbuf_size = def_wmem;
		rs->rbuf_size = def_mem;
		rs->sq_inline = def_inline;
		rs->sq_size = def_sqsize;
		rs->rq_size = def_rqsize;
		rs->ctrl_avail = RS_QP_CTRL_SIZE;
		rs->target_iomap_size = def_iomap_size;
	}
	fastlock_init(&rs->slock);
	fastlock_init(&rs->rlock);
	fastlock_init(&rs->cq_lock);
	fastlock_init(&rs->cq_wait_lock);
	fastlock_init(&rs->iomap_lock);
	dlist_init(&rs->iomap_list);
	dlist_init(&rs->iomap_queue);
	return rs;
}

static int rs_set_nonblocking(struct rsocket *rs, long arg)
{
	if (rs->cm_id->recv_cq_channel)
		InterlockedExchange(&rs->cm_id->recv_cq_channel->comp_channel.Milliseconds, arg ? 0 : INFINITE);

	if (rs->state < rs_connected)
		InterlockedExchange(&rs->cm_id->channel->channel.Milliseconds, arg ? 0 : INFINITE);

	return 0;
}

static void rs_set_qp_size(struct rsocket *rs)
{
	uint16_t max_size;

	max_size = min(ucma_max_qpsize(rs->cm_id), RS_QP_MAX_SIZE);

	if (rs->sq_size > max_size)
		rs->sq_size = max_size;
	else if (rs->sq_size < 2)
		rs->sq_size = 2;
	if (rs->sq_size <= (RS_QP_CTRL_SIZE << 2))
		rs->ctrl_avail = 1;

	if (rs->rq_size > max_size)
		rs->rq_size = max_size;
	else if (rs->rq_size < 2)
		rs->rq_size = 2;
}

static int rs_init_bufs(struct rsocket *rs)
{
	size_t len;

	rs->rmsg = (struct rs_msg *)calloc(rs->rq_size + 1, sizeof(*rs->rmsg));
	if (!rs->rmsg)
		return -1;

	rs->sbuf = (uint8_t *)calloc(rs->sbuf_size, sizeof(*rs->sbuf));
	if (!rs->sbuf)
		return -1;

	rs->smr = rdma_reg_msgs(rs->cm_id, rs->sbuf, rs->sbuf_size);
	if (!rs->smr)
		return -1;

	len = sizeof(*rs->target_sgl) * RS_SGL_SIZE +
	      sizeof(*rs->target_iomap) * rs->target_iomap_size;
	rs->target_buffer_list = malloc(len);
	if (!rs->target_buffer_list)
		return -1;

	rs->target_mr = rdma_reg_write(rs->cm_id, rs->target_buffer_list, len);
	if (!rs->target_mr)
		return -1;

	memset(rs->target_buffer_list, 0, len);
	rs->target_sgl = (rs_sge *)rs->target_buffer_list;
	if (rs->target_iomap_size)
		rs->target_iomap = (struct rs_iomap *) (rs->target_sgl + RS_SGL_SIZE);

	rs->rbuf = (uint8_t *)calloc(rs->rbuf_size, sizeof(*rs->rbuf));
	if (!rs->rbuf)
		return -1;

	rs->rmr = rdma_reg_write(rs->cm_id, rs->rbuf, rs->rbuf_size);
	if (!rs->rmr)
		return -1;

	rs->ssgl[0].addr = rs->ssgl[1].addr = (uintptr_t) rs->sbuf;
	rs->sbuf_bytes_avail = rs->sbuf_size;
	rs->ssgl[0].lkey = rs->ssgl[1].lkey = rs->smr->lkey;

	rs->rbuf_free_offset = rs->rbuf_size >> 1;
	rs->rbuf_bytes_avail = rs->rbuf_size >> 1;
	rs->sqe_avail = rs->sq_size - rs->ctrl_avail;
	rs->rseq_comp = rs->rq_size >> 1;
	return 0;
}

static int rs_create_cq(struct rsocket *rs)
{
	rs->cm_id->recv_cq_channel = ibv_create_comp_channel(rs->cm_id->verbs);
	if (!rs->cm_id->recv_cq_channel)
		return -1;

	rs->cm_id->recv_cq = ibv_create_cq(rs->cm_id->verbs, rs->sq_size + rs->rq_size,
					   rs->cm_id, rs->cm_id->recv_cq_channel, 0);
	if (!rs->cm_id->recv_cq)
		goto err1;

	if (rs->fd_flags & O_NONBLOCK) {
		if (rs_set_nonblocking(rs, 1))
			goto err2;
	}

	rs->cm_id->send_cq_channel = rs->cm_id->recv_cq_channel;
	rs->cm_id->send_cq = rs->cm_id->recv_cq;
	return 0;

err2:
	ibv_destroy_cq(rs->cm_id->recv_cq);
	rs->cm_id->recv_cq = NULL;
err1:
	ibv_destroy_comp_channel(rs->cm_id->recv_cq_channel);
	rs->cm_id->recv_cq_channel = NULL;
	return -1;
}

static __inline int
rs_post_recv(struct rsocket *rs)
{
	struct ibv_recv_wr wr, *bad;

	wr.wr_id = RS_RECV_WR_ID;
	wr.next = NULL;
	wr.sg_list = NULL;
	wr.num_sge = 0;

	return rdma_seterrno(ibv_post_recv(rs->cm_id->qp, &wr, &bad));
}

static int rs_create_ep(struct rsocket *rs)
{
	struct ibv_qp_init_attr qp_attr;
	int i, ret;

	rs_set_qp_size(rs);

	ret = rs_create_cq(rs);
	if (ret)
		return ret;

	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.qp_context = rs;
	qp_attr.send_cq = rs->cm_id->send_cq;
	qp_attr.recv_cq = rs->cm_id->recv_cq;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.sq_sig_all = 1;
	qp_attr.cap.max_send_wr = rs->sq_size;
	qp_attr.cap.max_recv_wr = rs->rq_size;
	qp_attr.cap.max_send_sge = 2;
	qp_attr.cap.max_recv_sge = 1;
	qp_attr.cap.max_inline_data = rs->sq_inline;

	ret = rdma_create_qp(rs->cm_id, NULL, &qp_attr);
	if (ret)
		return ret;

	ret = rs_init_bufs(rs); 
	if (ret)
		return ret;

	for (i = 0; i < rs->rq_size; i++) {
		ret = rs_post_recv(rs);
		if (ret)
			return ret;
	}
	return 0;
}

static void rs_release_iomap_mr(struct rs_iomap_mr *iomr)
{
	if (cl_atomic_dec(&iomr->refcnt))
		return;

	dlist_remove(&iomr->entry);
	ibv_dereg_mr(iomr->mr);
	if (iomr->index >= 0)
		iomr->mr = NULL;
	else
		free(iomr);
}

static void rs_free_iomappings(struct rsocket *rs)
{
	struct rs_iomap_mr *iomr;

	while (!dlist_empty(&rs->iomap_list)) {
		iomr = container_of(rs->iomap_list.Next,
				    struct rs_iomap_mr, entry);
		riounmap(rs->index, iomr->mr->addr, iomr->mr->length);
	}
	while (!dlist_empty(&rs->iomap_queue)) {
		iomr = container_of(rs->iomap_queue.Next,
				    struct rs_iomap_mr, entry);
		riounmap(rs->index, iomr->mr->addr, iomr->mr->length);
	}
}

static void rs_free(struct rsocket *rs)
{
	if (rs->index >= 0)
		rs_remove(rs);

	if (rs->rmsg)
		free(rs->rmsg);

	if (rs->sbuf) {
		if (rs->smr)
			rdma_dereg_mr(rs->smr);
		free(rs->sbuf);
	}

	if (rs->rbuf) {
		if (rs->rmr)
			rdma_dereg_mr(rs->rmr);
		free(rs->rbuf);
	}

	if (rs->target_buffer_list) {
		if (rs->target_mr)
			rdma_dereg_mr(rs->target_mr);
		free(rs->target_buffer_list);
	}

	if (rs->cm_id) {
		rs_free_iomappings(rs);
		if (rs->cm_id->qp)
			rdma_destroy_qp(rs->cm_id);
		rdma_destroy_id(rs->cm_id);
	}

	if (rs->pNetstatEntry)
		rs->pNetstatEntry->s = (int) INVALID_SOCKET;
	
	fastlock_destroy(&rs->iomap_lock);
	fastlock_destroy(&rs->cq_wait_lock);
	fastlock_destroy(&rs->cq_lock);
	fastlock_destroy(&rs->rlock);
	fastlock_destroy(&rs->slock);
	free(rs);
}

static void rs_set_conn_data(struct rsocket *rs, struct rdma_conn_param *param,
			     struct rs_conn_data *conn)
{
	conn->version = 1;
	conn->flags = RS_CONN_FLAG_IOMAP |
		      (rs_host_is_net() ? RS_CONN_FLAG_NET : 0);
	conn->credits = htons(rs->rq_size);
	memset(conn->reserved, 0, sizeof conn->reserved);
	conn->target_iomap_size = (uint8_t) rs_value_to_scale(rs->target_iomap_size, 8);

	conn->target_sgl.addr = htonll((uintptr_t) rs->target_sgl);
	conn->target_sgl.length = htonl(RS_SGL_SIZE);
	conn->target_sgl.key = htonl(rs->target_mr->rkey);

	conn->data_buf.addr = htonll((uintptr_t) rs->rbuf);
	conn->data_buf.length = htonl(rs->rbuf_size >> 1);
	conn->data_buf.key = htonl(rs->rmr->rkey);

	param->private_data = conn;
	param->private_data_len = sizeof *conn;
}

static void rs_save_conn_data(struct rsocket *rs, struct rs_conn_data *conn)
{
	rs->remote_sgl.addr = ntohll(conn->target_sgl.addr);
	rs->remote_sgl.length = ntohl(conn->target_sgl.length);
	rs->remote_sgl.key = ntohl(conn->target_sgl.key);
	rs->remote_sge = 1;
	if ((rs_host_is_net() && !(conn->flags & RS_CONN_FLAG_NET)) ||
	    (!rs_host_is_net() && (conn->flags & RS_CONN_FLAG_NET)))
		rs->opts = RS_OPT_SWAP_SGL;

	if (conn->flags & RS_CONN_FLAG_IOMAP) {
		rs->remote_iomap.addr = rs->remote_sgl.addr +
					sizeof(rs->remote_sgl) * rs->remote_sgl.length;
		rs->remote_iomap.length = rs_scale_to_value(conn->target_iomap_size, 8);
		rs->remote_iomap.key = rs->remote_sgl.key;
	}

	rs->target_sgl[0].addr = ntohll(conn->data_buf.addr);
	rs->target_sgl[0].length = ntohl(conn->data_buf.length);
	rs->target_sgl[0].key = ntohl(conn->data_buf.key);

	rs->sseq_comp = ntohs(conn->credits);
}

int rsocket(int domain, int type, int protocol)
{
	struct rsocket *rs;
	int ret;

	if ((domain != PF_INET && domain != PF_INET6) ||
	    (type != SOCK_STREAM) || (protocol && protocol != IPPROTO_TCP)) {
		ret = ERR(ENOTSUP);
        return ret;
    }

	rs_configure();
	rs = rs_alloc(NULL);
	if (!rs) {
		ret = ERR(ENOMEM);
        return ret;
    }

	ret = rdma_create_id(NULL, &rs->cm_id, rs, RDMA_PS_TCP);
	if (ret)
		goto err;

	ret = rs_insert(rs);
	if (ret < 0)
		goto err;

	rs->cm_id->route.addr.src_addr.sa_family = (ADDRESS_FAMILY)domain;
	rs->pNetstatEntry = rsNetstatEntryCreate(rs->index, &WSAErrno);
	if (rs->pNetstatEntry)
		rs->pNetstatEntry->saSrc = rs->cm_id->route.addr.src_addr;

	rs_set_state(rs, rs_init);
	return rs->index;

err:
	rs_free(rs);
	return ERR(ret);
}

int rbind(int socket, const struct sockaddr *addr, socklen_t addrlen)
{
	struct rsocket *rs;
	int ret;

	rs = (struct rsocket *)idm_at(&idm, socket);
	ret = rdma_bind_addr(rs->cm_id, (struct sockaddr *) addr);
	if (!ret) {
		rs_set_state(rs, rs_bound);
		if (rs->pNetstatEntry)
			rs->pNetstatEntry->saSrc = rs->cm_id->route.addr.src_addr;
	} else
		wsa_setlasterror();
	
	return ret;
}

int rlisten(int socket, int backlog)
{
	struct rsocket *rs;
	int ret;

	rs = (struct rsocket *)idm_at(&idm, socket);

	if (backlog > RS_MAX_BACKLOG || backlog == SOMAXCONN)
		backlog = RS_MAX_BACKLOG;

	ret = rdma_listen(rs->cm_id, backlog);
	if (!ret)
		rs_set_state(rs, rs_listening);
	else
		wsa_setlasterror();
	
	return ret;
}

static int rs_do_connect(struct rsocket *rs)
{
	struct rdma_conn_param param;
	struct rs_conn_data creq, *cresp;
	struct sockaddr_storage dst_addr;
	int to, ret;

	switch (rs->state) {
	case rs_init:
	case rs_bound:
resolve_addr:
		to = 1000 << rs->retries++;
		RtlCopyMemory(
			&dst_addr,
			&rs->cm_id->route.addr.dst_addr,
			rs->cm_id->route.addr.dst_addr.sa_family == AF_INET
			? sizeof(struct sockaddr_in)
			: sizeof(struct sockaddr_in6)
		);
		ret = rdma_resolve_addr(rs->cm_id, NULL,
					(struct sockaddr *)&dst_addr, to);
		if (!ret)
			goto resolve_route;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			rs_set_state(rs, rs_resolving_addr);
		break;
	case rs_resolving_addr:
		ret = ucma_complete(rs->cm_id);
		if (ret) {
			if (errno == ETIMEDOUT && rs->retries <= RS_CONN_RETRIES)
				goto resolve_addr;
			break;
		}

		rs->retries = 0;
resolve_route:
		to = 1000 << rs->retries++;
		ret = rdma_resolve_route(rs->cm_id, to);
		if (!ret)
			goto do_connect;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			rs_set_state(rs, rs_resolving_route);
		break;
	case rs_resolving_route:
		ret = ucma_complete(rs->cm_id);
		if (ret) {
			if (errno == ETIMEDOUT && rs->retries <= RS_CONN_RETRIES)
				goto resolve_route;
			break;
		}
do_connect:
		ret = rs_create_ep(rs);
		if (ret)
			break;

		memset(&param, 0, sizeof param);
		rs_set_conn_data(rs, &param, &creq);
		param.flow_control = 1;
		param.retry_count = 7;
		param.rnr_retry_count = 7;
		rs->retries = 0;

		ret = rdma_connect(rs->cm_id, &param);
		if (!ret)
			goto connected;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			rs_set_state(rs, rs_connecting);
		break;
	case rs_connecting:
		ret = ucma_complete(rs->cm_id);
		if (ret)
			break;
connected:
		cresp = (struct rs_conn_data *) rs->cm_id->event->param.conn.private_data;
		if (cresp->version != 1) {
			ret = ERR(ENOTSUP);
			break;
		}

		rs_save_conn_data(rs, cresp);
		rs_set_state(rs, rs_connect_rdwr);
		break;
	case rs_accepting:
		if (!(rs->fd_flags & O_NONBLOCK))
			rs_set_nonblocking(rs, 0);

		ret = ucma_complete(rs->cm_id);
		if (ret)
			break;

		rs_set_state(rs, rs_connect_rdwr);
		break;
	default:
		ret = ERR(EINVAL);
		break;
	}

	if (ret) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			errno = EINPROGRESS;
		} else {
			rs_set_state(rs, rs_connect_error);
			rs->err = errno;
		}
		wsa_setlasterror();
	}
	return ret;
}

int rconnect(int socket, const struct sockaddr *addr, socklen_t addrlen)
{
	struct rsocket *rs;
	int             ret;
	
	rs = (struct rsocket *)idm_at(&idm, socket);
	memcpy(&rs->cm_id->route.addr.dst_addr, addr, addrlen);
	ret = rs_do_connect(rs);
	if (rs->pNetstatEntry) {
		rs->pNetstatEntry->saDst = *addr;
		rs->pNetstatEntry->saSrc = rs->cm_id->route.addr.src_addr;
	}

	return ret;
}

static int rs_post_write_msg(struct rsocket *rs,
			 struct ibv_sge *sgl, int nsge,
			 uint32_t imm_data, int flags,
			 uint64_t addr, uint32_t rkey)
{
	struct ibv_send_wr wr, *bad;

	wr.wr_id = (uint64_t) imm_data;
	wr.next = NULL;
	wr.sg_list = sgl;
	wr.num_sge = nsge;
	wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
	wr.send_flags = flags;
	wr.imm_data = htonl(imm_data);
	wr.wr.rdma.remote_addr = addr;
	wr.wr.rdma.rkey = rkey;

	return rdma_seterrno(ibv_post_send(rs->cm_id->qp, &wr, &bad));
}

static int rs_post_write(struct rsocket *rs,
			 struct ibv_sge *sgl, int nsge,
			 uint64_t wr_id, int flags,
			 uint64_t addr, uint32_t rkey)
{
	struct ibv_send_wr wr, *bad;

	wr.wr_id = wr_id;
	wr.next = NULL;
	wr.sg_list = sgl;
	wr.num_sge = nsge;
	wr.opcode = IBV_WR_RDMA_WRITE;
	wr.send_flags = flags;
	wr.wr.rdma.remote_addr = addr;
	wr.wr.rdma.rkey = rkey;

	return rdma_seterrno(ibv_post_send(rs->cm_id->qp, &wr, &bad));
}

/*
 * Update target SGE before sending data.  Otherwise the remote side may
 * update the entry before we do.
 */
static int rs_write_data(struct rsocket *rs,
			 struct ibv_sge *sgl, int nsge,
			 uint32_t length, int flags)
{
	uint64_t addr;
	uint32_t rkey;

	rs->sseq_no++;
	rs->sqe_avail--;
	rs->sbuf_bytes_avail -= length;

	addr = rs->target_sgl[rs->target_sge].addr;
	rkey = rs->target_sgl[rs->target_sge].key;

	rs->target_sgl[rs->target_sge].addr += length;
	rs->target_sgl[rs->target_sge].length -= length;

	if (!rs->target_sgl[rs->target_sge].length) {
		if (++rs->target_sge == RS_SGL_SIZE)
			rs->target_sge = 0;
	}

	return rs_post_write_msg(rs, sgl, nsge, rs_msg_set(RS_OP_DATA, length),
			     flags, addr, rkey);
}

static int rs_write_direct(struct rsocket *rs, struct rs_iomap *iom, uint64_t offset,
			   struct ibv_sge *sgl, int nsge, uint32_t length, int flags)
{
	uint64_t addr;

	rs->sqe_avail--;
	rs->sbuf_bytes_avail -= length;

	addr = iom->sge.addr + offset - iom->offset;
	return rs_post_write(rs, sgl, nsge, rs_msg_set(RS_OP_WRITE, length),
			     flags, addr, iom->sge.key);
}

static int rs_write_iomap(struct rsocket *rs, struct rs_iomap_mr *iomr,
			  struct ibv_sge *sgl, int nsge, int flags)
{
	uint64_t addr;

	rs->sseq_no++;
	rs->sqe_avail--;
	rs->sbuf_bytes_avail -= sizeof(struct rs_iomap);

	addr = rs->remote_iomap.addr + iomr->index * sizeof(struct rs_iomap);
	return rs_post_write_msg(rs, sgl, nsge, rs_msg_set(RS_OP_IOMAP_SGL, iomr->index),
			         flags, addr, rs->remote_iomap.key);
}

static uint32_t rs_sbuf_left(struct rsocket *rs)
{
	return (uint32_t) (((uint64_t) (uintptr_t) &rs->sbuf[rs->sbuf_size]) -
			   rs->ssgl[0].addr);
}

static void rs_send_credits(struct rsocket *rs)
{
	struct ibv_sge ibsge;
	struct rs_sge sge;

	rs->ctrl_avail--;
	rs->rseq_comp = rs->rseq_no + (rs->rq_size >> 1);
	if ((uint32_t)rs->rbuf_bytes_avail >= (rs->rbuf_size >> 1)) {
		if (!(rs->opts & RS_OPT_SWAP_SGL)) {
			sge.addr = (uintptr_t) &rs->rbuf[rs->rbuf_free_offset];
			sge.key = rs->rmr->rkey;
			sge.length = rs->rbuf_size >> 1;
		} else {
			sge.addr = cl_ntoh64((uintptr_t) &rs->rbuf[rs->rbuf_free_offset]);
			sge.key = cl_ntoh32(rs->rmr->rkey);
			sge.length = cl_ntoh32(rs->rbuf_size >> 1);
		}

		ibsge.addr = (uintptr_t) &sge;
		ibsge.lkey = 0;
		ibsge.length = sizeof(sge);

		rs_post_write_msg(rs, &ibsge, 1,
			      rs_msg_set(RS_OP_SGL, rs->rseq_no + rs->rq_size),
			      IBV_SEND_INLINE,
			      rs->remote_sgl.addr +
			      rs->remote_sge * sizeof(struct rs_sge),
			      rs->remote_sgl.key);

		rs->rbuf_bytes_avail -= rs->rbuf_size >> 1;
		rs->rbuf_free_offset += rs->rbuf_size >> 1;
		if ((uint32_t)rs->rbuf_free_offset >= rs->rbuf_size)
			rs->rbuf_free_offset = 0;
		if (++rs->remote_sge == rs->remote_sgl.length)
			rs->remote_sge = 0;
	} else {
		rs_post_write_msg(rs, NULL, 0,
			      rs_msg_set(RS_OP_SGL, rs->rseq_no + rs->rq_size),
				  0, 0, 0);
	}
}

static int rs_give_credits(struct rsocket *rs)
{
	return (((uint32_t)rs->rbuf_bytes_avail >= (rs->rbuf_size >> 1)) ||
	        ((short) ((short) rs->rseq_no - (short) rs->rseq_comp) >= 0)) &&
	       rs->ctrl_avail && (rs->state & rs_connected);
}

static void rs_update_credits(struct rsocket *rs)
{
	if (rs_give_credits(rs))
		rs_send_credits(rs);
}

static int rs_poll_cq(struct rsocket *rs)
{
	struct ibv_wc wc;
	uint32_t imm_data;
	int ret, rcnt = 0;

	while ((ret = ibv_poll_cq(rs->cm_id->recv_cq, 1, &wc)) > 0) {
		if (wc.wr_id == RS_RECV_WR_ID) {
			if (wc.status != IBV_WC_SUCCESS)
				continue;
			rcnt++;

			imm_data = ntohl(wc.imm_data);
			switch (rs_msg_op(imm_data)) {
			case RS_OP_SGL:
				rs->sseq_comp = (uint16_t) rs_msg_data(imm_data);
				break;
			case RS_OP_IOMAP_SGL:
				/* The iomap was updated, that's nice to know. */
				break;
			case RS_OP_CTRL:
				if (rs_msg_data(imm_data) == RS_CTRL_DISCONNECT) {
					rs_set_state(rs, rs_disconnected);
					return 0;
				} else if (rs_msg_data(imm_data) == RS_CTRL_SHUTDOWN) {
					rs_set_state(rs, rs->state & ~rs_connect_rd);
				}
				break;
			case RS_OP_WRITE:
				/* We really shouldn't be here. */
				break;
			default:
				rs->rmsg[rs->rmsg_tail].op = rs_msg_op(imm_data);
				rs->rmsg[rs->rmsg_tail].data = rs_msg_data(imm_data);
				if (++rs->rmsg_tail == rs->rq_size + 1)
					rs->rmsg_tail = 0;
				break;
			}
		} else {
			switch (rs_msg_op((uint32_t) wc.wr_id)) {
			case RS_OP_SGL:
				rs->ctrl_avail++;
				break;
			case RS_OP_CTRL:
				rs->ctrl_avail++;
				if (rs_msg_data((uint32_t) wc.wr_id) == RS_CTRL_DISCONNECT)
					rs_set_state(rs, rs_disconnected);
				break;
			case RS_OP_IOMAP_SGL:
				rs->sqe_avail++;
				rs->sbuf_bytes_avail += sizeof(struct rs_iomap);
				break;
			default:
				rs->sqe_avail++;
				rs->sbuf_bytes_avail += rs_msg_data((uint32_t) wc.wr_id);
				break;
			}
			if (wc.status != IBV_WC_SUCCESS && (rs->state & rs_connected)) {
				rs_set_state(rs, rs_error);
				rs->err = EIO;
			}
		}
	}

	if (rs->state & rs_connected) {
		while (!ret && rcnt--)
			ret = rs_post_recv(rs);
		
		if (ret) {
			rs_set_state(rs, rs_error);
			rs->err = errno;
		}
	}
	return ret;
}

static int rs_get_cq_event(struct rsocket *rs)
{
	struct ibv_cq *cq;
	void *context;
	int ret;

	if (!rs->cq_armed)
		return 0;

	ret = ibv_get_cq_event(rs->cm_id->recv_cq_channel, &cq, &context);
	switch (ret) {
	case 0:
		ibv_ack_cq_events(rs->cm_id->recv_cq, 1);
		rs->cq_armed = 0;
		break;
	case WAIT_TIMEOUT:
		if (rs->fd_flags & O_NONBLOCK)
			ret = ERR(EWOULDBLOCK);
		else {
			ret = ERR(ETIMEDOUT);
			rs_set_state(rs, rs_error);
		}

		break;
	case ERROR_CANCELLED:
		ret = ERR(ECANCELED);
		rs_set_state(rs, rs_error);
		break;
	default:
		ret = ERR(ret); //???
		rs_set_state(rs, rs_error);
	}

	return ret;
}

/*
 * Although we serialize rsend and rrecv calls with respect to themselves,
 * both calls may run simultaneously and need to poll the CQ for completions.
 * We need to serialize access to the CQ, but rsend and rrecv need to
 * allow each other to make forward progress.
 *
 * For example, rsend may need to wait for credits from the remote side,
 * which could be stalled until the remote process calls rrecv.  This should
 * not block rrecv from receiving data from the remote side however.
 *
 * We handle this by using two locks.  The cq_lock protects against polling
 * the CQ and processing completions.  The cq_wait_lock serializes access to
 * waiting on the CQ.
 */
static int rs_process_cq(struct rsocket *rs, int nonblock, int (*test)(struct rsocket *rs))
{
	int ret;

	fastlock_acquire(&rs->cq_lock);
	do {
		rs_update_credits(rs);
		ret = rs_poll_cq(rs);
		if (test(rs)) {
			ret = 0;
			break;
		} else if (ret) {
			break;
		} else if (nonblock) {
			ret = ERR(EWOULDBLOCK);
		} else if (!rs->cq_armed) {
			ibv_req_notify_cq(rs->cm_id->recv_cq, 0);
			rs->cq_armed = 1;
		} else {
			rs_update_credits(rs);
			fastlock_acquire(&rs->cq_wait_lock);
			fastlock_release(&rs->cq_lock);

			ret = rs_get_cq_event(rs);
			fastlock_release(&rs->cq_wait_lock);
			fastlock_acquire(&rs->cq_lock);
		}
	} while (!ret);

	rs_update_credits(rs);
	fastlock_release(&rs->cq_lock);
	return ret;
}

static int rs_get_comp(struct rsocket *rs, int nonblock, int (*test)(struct rsocket *rs))
{
	struct timeval s, e;
	uint32_t poll_time = 0;
	int ret;

	do {
		ret = rs_process_cq(rs, 1, test);
		if (!ret || nonblock || (errno != EWOULDBLOCK && errno != EAGAIN))
			return ret;

		if (!poll_time)
			gettimeofday(&s, NULL);

		gettimeofday(&e, NULL);
		poll_time = (e.tv_sec - s.tv_sec) * 1000000 +
			    (e.tv_usec - s.tv_usec) + 1;
	} while (poll_time <= polling_time);

	ret = rs_process_cq(rs, 0, test);
	return ret;
}

static int rs_nonblocking(struct rsocket *rs)
{
	return (rs->fd_flags & O_NONBLOCK);
}

static int rs_is_cq_armed(struct rsocket *rs)
{
	return rs->cq_armed;
}

static int rs_poll_all(struct rsocket *rs)
{
	return 1;
}

/*
 * We use hardware flow control to prevent over running the remote
 * receive queue.  However, data transfers still require space in
 * the remote rmsg queue, or we risk losing notification that data
 * has been transfered.
 *
 * Be careful with race conditions in the check below.  The target SGL
 * may be updated by a remote RDMA write.
 */
static int rs_can_send(struct rsocket *rs)
{
	return rs->sqe_avail && (rs->sbuf_bytes_avail >= RS_SNDLOWAT) &&
	       (rs->sseq_no != rs->sseq_comp) &&
	       (rs->target_sgl[rs->target_sge].length != 0);
}

static int rs_conn_can_send(struct rsocket *rs)
{
	return rs_can_send(rs) || !(rs->state & rs_connect_wr);
}

static int rs_conn_can_send_ctrl(struct rsocket *rs)
{
	return rs->ctrl_avail || !(rs->state & rs_connected);
}

static int rs_have_rdata(struct rsocket *rs)
{
	return (rs->rmsg_head != rs->rmsg_tail);
}

static int rs_conn_have_rdata(struct rsocket *rs)
{
	return rs_have_rdata(rs) || !(rs->state & rs_connect_rd);
}

static int rs_conn_all_sends_done(struct rsocket *rs)
{
	return ((rs->sqe_avail + rs->ctrl_avail) == rs->sq_size) ||
	       !(rs->state & rs_connected);
}

static ssize_t rs_peek(struct rsocket *rs, void *buf, size_t len)
{
	size_t left = len;
	uint32_t end_size, rsize;
	int rmsg_head, rbuf_offset;
	uint8_t *bufb = (uint8_t *)buf;

	rmsg_head = rs->rmsg_head;
	rbuf_offset = rs->rbuf_offset;

	for (; left && (rmsg_head != rs->rmsg_tail); left -= rsize) {
		if (left < rs->rmsg[rmsg_head].data) {
			rsize = left;
		} else {
			rsize = rs->rmsg[rmsg_head].data;
			if (++rmsg_head == rs->rq_size + 1)
				rmsg_head = 0;
		}

		end_size = rs->rbuf_size - rbuf_offset;
		if (rsize > end_size) {
			memcpy(bufb, &rs->rbuf[rbuf_offset], end_size);
			rbuf_offset = 0;
			bufb += end_size;
			rsize -= end_size;
			left -= end_size;
		}
		memcpy(bufb, &rs->rbuf[rbuf_offset], rsize);
		rbuf_offset += rsize;
		bufb += rsize;
	}

	return len - left;
}

/*
 * Continue to receive any queued data even if the remote side has disconnected.
 */
 ssize_t rrecv(int socket, void *buf, size_t len, int flags)
{
	struct rsocket *rs;
	size_t left = len;
	uint32_t end_size, rsize;
	int ret;
	uint8_t *bufb = (uint8_t *)buf;
	
	rs = (struct rsocket *)idm_at(&idm, socket);
	if (rs->state & rs_opening) {
		ret = rs_do_connect(rs);
		if (ret) {
			if (errno == EINPROGRESS)
				errno = EAGAIN;
			goto out;
		}
	}
	fastlock_acquire(&rs->rlock);
	do {
		if (!rs_have_rdata(rs)) {
			ret = rs_get_comp(rs, rs_nonblocking(rs),
								rs_conn_have_rdata);
			if (ret)
				break;
		}

		ret = 0;
		if (flags & MSG_PEEK) {
			left = len - rs_peek(rs, buf, left);
			break;
		}

		for (; left && rs_have_rdata(rs); left -= rsize) {
			if (left < rs->rmsg[rs->rmsg_head].data) {
				rsize = left;
				rs->rmsg[rs->rmsg_head].data -= left;
			} else {
				rs->rseq_no++;
				rsize = rs->rmsg[rs->rmsg_head].data;
				if (++rs->rmsg_head == rs->rq_size + 1)
					rs->rmsg_head = 0;
			}

			end_size = rs->rbuf_size - rs->rbuf_offset;
			if (rsize > end_size) {
				memcpy(bufb, &rs->rbuf[rs->rbuf_offset], end_size);
				rs->rbuf_offset = 0;
				bufb += end_size;
				rsize -= end_size;
				left -= end_size;
				rs->rbuf_bytes_avail += end_size;
			}
			memcpy(bufb, &rs->rbuf[rs->rbuf_offset], rsize);
			rs->rbuf_offset += rsize;
			bufb += rsize;
			rs->rbuf_bytes_avail += rsize;
		}

	} while (left && (flags & MSG_WAITALL) && (rs->state & rs_connect_rd));
	
	fastlock_release(&rs->rlock);

out:
	if (ret) {
		wsa_setlasterror();
		return ret;
	} else
		return len - left;
}

ssize_t rrecvfrom(int socket, void *buf, size_t len, int flags,
		  struct sockaddr *src_addr, socklen_t *addrlen)
{
	ssize_t ret;

	ret = rrecv(socket, buf, len, flags);
	if (ret > 0 && src_addr)
		rgetpeername(socket, src_addr, addrlen);

	return ret;
}

/*
 * Simple, straightforward implementation for now that only tries to fill
 * in the first vector.
 */
static ssize_t rrecvv(int socket, const struct iovec *iov, int iovcnt, int flags)
{
	return rrecv(socket, iov[0].iov_base, iov[0].iov_len, flags);
}

ssize_t rrecvmsg(int socket, struct msghdr *msg, int flags)
{
	if (msg->msg_control && msg->msg_controllen)
		return ERR(ENOTSUP);

	return rrecvv(socket, msg->msg_iov, (int) msg->msg_iovlen, msg->msg_flags);
}

ssize_t rread(int socket, void *buf, size_t count)
{
	return rrecv(socket, buf, count, 0);
}

ssize_t rreadv(int socket, const struct iovec *iov, int iovcnt)
{
	return rrecvv(socket, iov, iovcnt, 0);
}

static int rs_send_iomaps(struct rsocket *rs, int flags)
{
	struct rs_iomap_mr *iomr;
	struct ibv_sge sge;
	struct rs_iomap iom;
	int ret;

	fastlock_acquire(&rs->iomap_lock);
	while (!dlist_empty(&rs->iomap_queue)) {
		if (!rs_can_send(rs)) {
			ret = rs_get_comp(rs, rs_nonblocking(rs),
					  rs_conn_can_send);
			if (ret)
				break;
			if (!(rs->state & rs_connect_wr)) {
				ret = ERR(ECONNRESET);
				break;
			}
		}

		iomr = container_of(rs->iomap_queue.Next, struct rs_iomap_mr, entry);
		if (!(rs->opts & RS_OPT_SWAP_SGL)) {
			iom.offset = iomr->offset;
			iom.sge.addr = (uintptr_t) iomr->mr->addr;
			iom.sge.length = iomr->mr->length;
			iom.sge.key = iomr->mr->rkey;
		} else {
			iom.offset = cl_ntoh64(iomr->offset);
			iom.sge.addr = cl_ntoh64((uintptr_t) iomr->mr->addr);
			iom.sge.length = cl_ntoh32(iomr->mr->length);
			iom.sge.key = cl_ntoh32(iomr->mr->rkey);
		}

		if (rs->sq_inline >= sizeof iom) {
			sge.addr = (uintptr_t) &iom;
			sge.length = sizeof iom;
			sge.lkey = 0;
			ret = rs_write_iomap(rs, iomr, &sge, 1, IBV_SEND_INLINE);
		} else if (rs_sbuf_left(rs) >= sizeof iom) {
			memcpy((void *) (uintptr_t) rs->ssgl[0].addr, &iom, sizeof iom);
			rs->ssgl[0].length = sizeof iom;
			ret = rs_write_iomap(rs, iomr, rs->ssgl, 1, 0);
			if (rs_sbuf_left(rs) > sizeof iom)
				rs->ssgl[0].addr += sizeof iom;
			else
				rs->ssgl[0].addr = (uintptr_t) rs->sbuf;
		} else {
			rs->ssgl[0].length = rs_sbuf_left(rs);
			memcpy((void *) (uintptr_t) rs->ssgl[0].addr, &iom,
				rs->ssgl[0].length);
			rs->ssgl[1].length = sizeof iom - rs->ssgl[0].length;
			memcpy(rs->sbuf, ((char *) &iom) + rs->ssgl[0].length,
			       rs->ssgl[1].length);
			ret = rs_write_iomap(rs, iomr, rs->ssgl, 2, 0);
			rs->ssgl[0].addr = (uintptr_t) rs->sbuf + rs->ssgl[1].length;
		}
		dlist_remove(&iomr->entry);
		dlist_insert_tail(&iomr->entry, &rs->iomap_list);
		if (ret)
			break;
	}

	rs->iomap_pending = !dlist_empty(&rs->iomap_queue);
	fastlock_release(&rs->iomap_lock);
	return ret;
}

/*
 * We overlap sending the data, by posting a small work request immediately,
 * then increasing the size of the send on each iteration.
 */
ssize_t rsend(int socket, const void *buf, size_t len, int flags)
{
	struct rsocket *rs;
	struct ibv_sge sge;
	size_t left = len;
	uint32_t xfer_size, olen = RS_OLAP_START_SIZE;
	int ret = 0;
	uint8_t *bufb = (uint8_t *)buf;

	rs = (struct rsocket *)idm_at(&idm, socket);
	if (rs->state & rs_opening) {
		ret = rs_do_connect(rs);
		if (ret) {
			if (errno == EINPROGRESS)
				errno = EAGAIN;
			wsa_setlasterror();
			return ret;
		}
	}

	fastlock_acquire(&rs->slock);
	if (rs->iomap_pending) {
		ret = rs_send_iomaps(rs, flags);
		if (ret)
			goto out;
	}
	for (; left; left -= xfer_size, bufb += xfer_size) {
		if (!rs_can_send(rs)) {
			ret = rs_get_comp(rs, rs_nonblocking(rs),
					  rs_conn_can_send);
			if (ret)
				break;
			if (!(rs->state & rs_connect_wr)) {
				ret = ERR(ECONNRESET);
				break;
			}
		}

		if (olen < left) {
			xfer_size = olen;
			if (olen < RS_MAX_TRANSFER)
				olen <<= 1;
		} else {
			xfer_size = left;
		}

		if (xfer_size > (uint32_t)rs->sbuf_bytes_avail)
			xfer_size = rs->sbuf_bytes_avail;
		if (xfer_size > rs->target_sgl[rs->target_sge].length)
			xfer_size = rs->target_sgl[rs->target_sge].length;

		if (xfer_size <= rs->sq_inline) {
			sge.addr = (uintptr_t)bufb;
			sge.length = xfer_size;
			sge.lkey = 0;
			ret = rs_write_data(rs, &sge, 1, xfer_size, IBV_SEND_INLINE);
		} else if (xfer_size <= rs_sbuf_left(rs)) {
			memcpy((void *) (uintptr_t) rs->ssgl[0].addr, bufb, xfer_size);
			rs->ssgl[0].length = xfer_size;
			ret = rs_write_data(rs, rs->ssgl, 1, xfer_size, 0);
			if (xfer_size < rs_sbuf_left(rs))
				rs->ssgl[0].addr += xfer_size;
			else
				rs->ssgl[0].addr = (uintptr_t) rs->sbuf;
		} else {
			rs->ssgl[0].length = rs_sbuf_left(rs);
			memcpy((void *) (uintptr_t) rs->ssgl[0].addr, bufb,
				rs->ssgl[0].length);
			rs->ssgl[1].length = xfer_size - rs->ssgl[0].length;
			memcpy(rs->sbuf, bufb + rs->ssgl[0].length, rs->ssgl[1].length);
			ret = rs_write_data(rs, rs->ssgl, 2, xfer_size, 0);
			rs->ssgl[0].addr = (uintptr_t) rs->sbuf + rs->ssgl[1].length;
		}
		if (ret)
			break;
	}
out:
	fastlock_release(&rs->slock);

	if (ret && left == len)	{
		wsa_setlasterror();
		return ret;
	} else
		return len - left;
}

ssize_t rsendto(int socket, const void *buf, size_t len, int flags,
		const struct sockaddr *dest_addr, socklen_t addrlen)
{
/*
 * In Windows on a connection-oriented socket,
 * the dest_addr and addrlen parameters are just ignored,
 * making sendto() equivalent to send().
 */
	return rsend(socket, buf, len, flags);
}

static void rs_copy_iov(void *dst, const struct iovec **iov, size_t *offset, size_t len)
{
	size_t size;
	uint8_t *dstb = (uint8_t *)dst;

	while (len) {
		size = (*iov)->iov_len - *offset;
		if (size > len) {
			memcpy (dstb, (*iov)->iov_base + *offset, len);
			*offset += len;
			break;
		}

		memcpy(dstb, (*iov)->iov_base + *offset, size);
		len -= size;
		dstb += size;
		(*iov)++;
		*offset = 0;
	}
}

static ssize_t rsendv(int socket, const struct iovec *iov, int iovcnt, int flags)
{
	struct rsocket *rs;
	const struct iovec *cur_iov;
	size_t left, len, offset = 0;
	uint32_t xfer_size, olen = RS_OLAP_START_SIZE;
	int i, ret = 0;

	rs = (struct rsocket *)idm_at(&idm, socket);
	if (rs->state & rs_opening) {
		ret = rs_do_connect(rs);
		if (ret) {
			if (errno == EINPROGRESS)
				errno = EAGAIN;
			return ret;
		}
	}

	cur_iov = iov;
	len = iov[0].iov_len;
	for (i = 1; i < iovcnt; i++)
		len += iov[i].iov_len;
	left = len;

	fastlock_acquire(&rs->slock);
	if (rs->iomap_pending) {
		ret = rs_send_iomaps(rs, flags);
		if (ret)
			goto out;
	}
	for (; left; left -= xfer_size) {
		if (!rs_can_send(rs)) {
			ret = rs_get_comp(rs, rs_nonblocking(rs),
					  rs_conn_can_send);
			if (ret)
				break;
			if (!(rs->state & rs_connect_wr)) {
				ret = ERR(ECONNRESET);
				break;
			}
		}

		if (olen < left) {
			xfer_size = olen;
			if (olen < RS_MAX_TRANSFER)
				olen <<= 1;
		} else {
			xfer_size = left;
		}

		if (xfer_size > (uint32_t)rs->sbuf_bytes_avail)
			xfer_size = rs->sbuf_bytes_avail;
		if (xfer_size > rs->target_sgl[rs->target_sge].length)
			xfer_size = rs->target_sgl[rs->target_sge].length;

		if (xfer_size <= rs_sbuf_left(rs)) {
			rs_copy_iov((void *) (uintptr_t) rs->ssgl[0].addr,
				    &cur_iov, &offset, xfer_size);
			rs->ssgl[0].length = xfer_size;
			ret = rs_write_data(rs, rs->ssgl, 1, xfer_size,
					    xfer_size <= rs->sq_inline ? IBV_SEND_INLINE : 0);
			if (xfer_size < rs_sbuf_left(rs))
				rs->ssgl[0].addr += xfer_size;
			else
				rs->ssgl[0].addr = (uintptr_t) rs->sbuf;
		} else {
			rs->ssgl[0].length = rs_sbuf_left(rs);
			rs_copy_iov((void *) (uintptr_t) rs->ssgl[0].addr, &cur_iov,
				    &offset, rs->ssgl[0].length);
			rs->ssgl[1].length = xfer_size - rs->ssgl[0].length;
			rs_copy_iov(rs->sbuf, &cur_iov, &offset, rs->ssgl[1].length);
			ret = rs_write_data(rs, rs->ssgl, 2, xfer_size,
					    xfer_size <= rs->sq_inline ? IBV_SEND_INLINE : 0);
			rs->ssgl[0].addr = (uintptr_t) rs->sbuf + rs->ssgl[1].length;
		}
		if (ret)
			break;
	}
out:
	fastlock_release(&rs->slock);

	return (ret && left == len) ? ret : len - left;
}

ssize_t rsendmsg(int socket, const struct msghdr *msg, int flags)
{
    int ret = 0;

	if (msg->msg_control && msg->msg_controllen) {
		ret = ERR(ENOTSUP);
        return ret;
    }

	return rsendv(socket, msg->msg_iov, (int) msg->msg_iovlen, msg->msg_flags);
}

ssize_t rwrite(int socket, const void *buf, size_t count)
{
	return rsend(socket, buf, count, 0);
}

ssize_t rwritev(int socket, const struct iovec *iov, int iovcnt)
{
	return rsendv(socket, iov, iovcnt, 0);
}

static struct pollfd *rs_fds_alloc(nfds_t nfds)
{
	static __thread struct pollfd *rfds;
	static __thread nfds_t rnfds;

	if (nfds > rnfds) {
		if (rfds)
			free(rfds);

		rfds  = (struct pollfd *)malloc(sizeof *rfds * nfds);
		rnfds = rfds ? nfds : 0;
	}

	return rfds;
}

static int rs_poll_rs(struct rsocket *rs, int events,
		      int nonblock, int (*test)(struct rsocket *rs))
{
	COMP_SET fds;
	short revents;
	int ret;

check_cq:
	if ((rs->state & rs_connected) || (rs->state == rs_disconnected) ||
	    (rs->state & rs_error)) {
		rs_process_cq(rs, nonblock, test);

		revents = 0;
		if ((events & POLLIN) && rs_conn_have_rdata(rs))
			revents |= POLLIN;
		if ((events & POLLOUT) && rs_can_send(rs))
			revents |= POLLOUT;
		if (!(rs->state & rs_connected)) {
			if (rs->state == rs_disconnected)
				revents |= POLLHUP;
			else
				revents |= POLLERR;
		}

		return revents;
	}

	if (rs->state == rs_listening) {
		if (CompSetInit(&fds))
			return 0;

		CompSetZero(&fds);
		CompSetAdd(&rs->cm_id->channel->channel, &fds);
		return CompSetPoll(&fds, 0);
	}

	if (rs->state & rs_opening) {
		ret = rs_do_connect(rs);
		if (ret) {
			if (errno == EINPROGRESS) {
				errno = 0;
				return 0;
			} else {
				return POLLOUT;
			}
		}
		goto check_cq;
	}

	if (rs->state == rs_connect_error)
		return (rs->err && events & POLLOUT) ? POLLOUT : 0;

	return 0;
}

static int rs_poll_check(struct pollfd *fds, nfds_t nfds)
{
	struct rsocket *rs;
	int i, cnt = 0;

	for (i = 0; i < (int)nfds; i++) {
		rs = (struct rsocket *)idm_lookup(&idm, (int)fds[i].fd);
		if (rs)
			fds[i].revents = (SHORT)rs_poll_rs(rs, fds[i].events, 1, rs_poll_all);
		else
			TRACE("No rs entry found for descriptor %d!", fds[i].fd);

		if (fds[i].revents)
			cnt++;
	}
	return cnt;
}

static int rs_poll_arm(struct pollfd *rfds, struct pollfd *fds, nfds_t nfds)
{
	struct rsocket *rs;
	int i;

	for (i = 0; i < (int)nfds; i++) {
		rs = (struct rsocket *)idm_lookup(&idm, (int)fds[i].fd);
		if (rs) {
			fds[i].revents = (SHORT)rs_poll_rs(rs, fds[i].events, 0, rs_is_cq_armed);
			if (fds[i].revents)
				return 1;

			if (rs->state >= rs_connected)
				rfds[i].fd = ((short)rs->cm_id->recv_cq_channel->comp_channel.Event >> 2);
			else
				rfds[i].fd = ((short)rs->cm_id->channel->channel.Event >> 2);

			rfds[i].events = POLLIN;
		} else {
			rfds[i].fd = fds[i].fd;
			rfds[i].events = fds[i].events;
		}
		rfds[i].revents = 0;

	}
	return 0;
}

static int rs_poll_events(struct pollfd *rfds, struct pollfd *fds, nfds_t nfds)
{
	struct rsocket *rs;
	int i, cnt = 0;

	for (i = 0; i < (int)nfds; i++) {
		if (!rfds[i].revents)
			continue;

		rs = (struct rsocket *)idm_lookup(&idm, (int)fds[i].fd);
		if (rs) {
			rs_get_cq_event(rs);
			fds[i].revents = (SHORT)rs_poll_rs(rs, fds[i].events, 1, rs_poll_all);
		} else {
			fds[i].revents = rfds[i].revents;
		}
		if (fds[i].revents)
			cnt++;
	}
	return cnt;
}

/*
 * We need to poll *all* fd's that the user specifies at least once.
 * Note that we may receive events on an rsocket that may not be reported
 * to the user (e.g. connection events or credit updates).  Process those
 * events, then return to polling until we find ones of interest.
 */
int rpoll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	struct timeval s, e;
	struct pollfd *rfds;
	COMP_SET cs;
	struct rsocket *rs;
	uint32_t poll_time = 0;
	int i, ret;
	
	do {
		ret = rs_poll_check(fds, nfds);
		if (ret || !timeout)
			return ret;

		if (!poll_time)
			gettimeofday(&s, NULL);

		gettimeofday(&e, NULL);
		poll_time = (e.tv_sec - s.tv_sec) * 1000000 +
			    (e.tv_usec - s.tv_usec) + 1;
	} while (poll_time <= polling_time);

	rfds = rs_fds_alloc(nfds);
	if (!rfds) {
		ret = ERR(ENOMEM);
        return ret;
    }

	if (CompSetInit(&cs)) {
        ret = ERR(ENOMEM);
        return ret;
    }
	
	do {
		ret = rs_poll_arm(rfds, fds, nfds);
		if (ret)
			break;

		CompSetZero(&cs);
		for (i = 0; i < (int)nfds; i++) {
			if (rs = (struct rsocket *)idm_lookup(&idm, (int)rfds[i].fd))
				CompSetAdd(&rs->cm_id->channel->channel, &cs);
		}
		ret = CompSetPoll(&cs, timeout);
		if (ret <= 0)
			break;

		ret = rs_poll_events(rfds, fds, nfds);
	} while (!ret);

	CompSetCleanup(&cs);
	return ret;
}

static struct pollfd *
rs_select_to_poll(int *nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
	struct pollfd *fds;
	int fd, i = 0;

	fds = (struct pollfd *)calloc(*nfds, sizeof *fds);
	if (!fds)
		return NULL;

	for (fd = 0; fd < *nfds; fd++) {
		if (readfds && FD_ISSET(fd, readfds)) {
			fds[i].fd = fd;
			fds[i].events = POLLIN;
		}

		if (writefds && FD_ISSET(fd, writefds)) {
			fds[i].fd = fd;
			fds[i].events |= POLLOUT;
		}

		if (exceptfds && FD_ISSET(fd, exceptfds))
			fds[i].fd = fd;

		if (fds[i].fd)
			i++;
	}

	*nfds = i;
	return fds;
}

static int
rs_poll_to_select(int nfds, struct pollfd *fds, fd_set *readfds,
		  fd_set *writefds, fd_set *exceptfds)
{
	int i, cnt = 0;

	for (i = 0; i < nfds; i++) {
		if (readfds && (fds[i].revents & (POLLIN | POLLHUP))) {
			FD_SET(fds[i].fd, readfds);
			cnt++;
		}

		if (writefds && (fds[i].revents & POLLOUT)) {
			FD_SET(fds[i].fd, writefds);
			cnt++;
		}

		if (exceptfds && (fds[i].revents & ~(POLLIN | POLLOUT))) {
			FD_SET(fds[i].fd, exceptfds);
			cnt++;
		}
	}
	return cnt;
}

static int rs_convert_timeout(struct timeval *timeout)
{
	return !timeout ? -1 :
		timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
}

int rselect(int nfds, fd_set *readfds, fd_set *writefds,
	    fd_set *exceptfds, struct timeval *timeout)
{
	struct pollfd *fds;
	int ret;
	
	fds = rs_select_to_poll(&nfds, readfds, writefds, exceptfds);
	if (!fds) {
		ret = ERR(ENOMEM);
        return ret;
    }

	ret = rpoll(fds, nfds, rs_convert_timeout(timeout));

	if (readfds)
		FD_ZERO(readfds);
	if (writefds)
		FD_ZERO(writefds);
	if (exceptfds)
		FD_ZERO(exceptfds);

	if (ret > 0)
		ret = rs_poll_to_select(nfds, fds, readfds, writefds, exceptfds);

	free(fds);
	return ret;
}

/*
 * For graceful disconnect, notify the remote side that we're
 * disconnecting and wait until all outstanding sends complete.
 */
int rshutdown(int socket, int how)
{
	struct rsocket *rs;
	int ctrl, ret = 0;

	rs = (struct rsocket *)idm_at(&idm, socket);
	if (how == SHUT_RD) {
		rs_set_state(rs, rs->state & ~rs_connect_rd);
		return 0;
	}

	if (!(rs->fd_flags & O_NONBLOCK))
		rs_set_nonblocking(rs, 1);
 
	if (rs->state & rs_connected) {
		if (how == SHUT_RDWR) {
			ctrl = RS_CTRL_DISCONNECT;
			rs_set_state(rs, rs->state & ~(rs_connect_rd | rs_connect_wr));
		} else {
			rs_set_state(rs, rs->state & ~rs_connect_wr);
			ctrl = (rs->state & rs_connect_rd) ?
				RS_CTRL_SHUTDOWN : RS_CTRL_DISCONNECT;
		}
		if (!rs->ctrl_avail) {
			ret = rs_process_cq(rs, 0, rs_conn_can_send_ctrl);
			if (ret) {
				wsa_setlasterror();
				return ret;
			}
		}

		if ((rs->state & rs_connected) && rs->ctrl_avail) {
			rs->ctrl_avail--;
			ret = rs_post_write_msg(rs, NULL, 0,
					    rs_msg_set(RS_OP_CTRL, ctrl), 0, 0, 0);
		}
	}

	if (rs->state & rs_connected)
		rs_process_cq(rs, 0, rs_conn_all_sends_done);

	if (!(rs->fd_flags & O_NONBLOCK) && (rs->state & rs_connected))
		rs_set_nonblocking(rs, 0);

	return 0;
}

int rclose(int socket)
{
	struct rsocket *rs;
	
	rs = (struct rsocket *)idm_at(&idm, socket);
	if (rs->state & rs_connected)
		rshutdown(socket, SHUT_RDWR);
	
	if (rs->pNetstatEntry)
		rs->pNetstatEntry->s = (int) INVALID_SOCKET;

	rs_free(rs);
	return 0;
}

static void rs_copy_addr(struct sockaddr *dst, struct sockaddr *src, socklen_t *len)
{
	socklen_t size;

	if (src->sa_family == AF_INET) {
		size = min(*len, sizeof(struct sockaddr_in));
		*len = sizeof(struct sockaddr_in);
	} else {
		size = min(*len, sizeof(struct sockaddr_in6));
		*len = sizeof(struct sockaddr_in6);
	}
	memcpy(dst, src, size);
}

int rgetpeername(int socket, struct sockaddr *addr, socklen_t *addrlen)
{
	struct rsocket *rs;
	
	rs = (struct rsocket *)idm_at(&idm, socket);
	rs_copy_addr(addr, rdma_get_peer_addr(rs->cm_id), addrlen);
	return 0;
}

int rgetsockname(int socket, struct sockaddr *addr, socklen_t *addrlen)
{
	struct rsocket *rs;
	
	rs = (struct rsocket *)idm_at(&idm, socket);
	rs_copy_addr(addr, rdma_get_local_addr(rs->cm_id), addrlen);
	return 0;
}

#if 0 // Direct Call variant:
/*
 * Nonblocking is usually not inherited between sockets, but we need to
 * inherit it here to establish the connection only.  This is needed to
 * prevent rdma_accept from blocking until the remote side finishes
 * establishing the connection.  If we were to allow rdma_accept to block,
 * then a single thread cannot establish a connection with itself, or
 * two threads which try to connect to each other can deadlock trying to
 * form a connection.
 *
 * Data transfers on the new socket remain blocking unless the user
 * specifies otherwise through rfcntl.
 */
int raccept(int socket, struct sockaddr *addr, socklen_t *addrlen)
{
	struct rsocket *rs, *new_rs;
	struct rdma_conn_param param;
	struct rs_conn_data *creq, cresp;
	int ret;
	
	rs = (struct rsocket *)idm_at(&idm, socket);
	new_rs = rs_alloc(rs);
	if (!new_rs) {
		ret = ERR(ENOMEM);
        return ret;
    }

	ret = rdma_get_request(rs->cm_id, &new_rs->cm_id);
	if (ret)
		goto err;

	ret = rs_insert(new_rs);
	if (ret < 0)
		goto err;

	creq = (struct rs_conn_data *) new_rs->cm_id->event->param.conn.private_data;
	if (creq->version != 1) {
		ret = ERR(ENOTSUP);
		goto err;
	}

	if (rs->fd_flags & O_NONBLOCK)
		rs_set_nonblocking(new_rs, 1);

	ret = rs_create_ep(new_rs);
	if (ret)
		goto err;

	rs_save_conn_data(new_rs, creq);
	param = new_rs->cm_id->event->param.conn;
	rs_set_conn_data(new_rs, &param, &cresp);
	ret = rdma_accept(new_rs->cm_id, &param);
	if (!ret)
		rs_set_state(new_rs, rs_connect_rdwr);
	else if (errno == EAGAIN || errno == EWOULDBLOCK)
		rs_set_state(new_rs, rs_accepting);
	else
		goto err;

	if (addr && addrlen) {
		rgetpeername(new_rs->index, addr, addrlen);
		if (rs->pNetstatEntry)
			rs->pNetstatEntry->saDst = *addr;
	}

	return new_rs->index;

err:
	rs_free(new_rs);
    wsa_setlasterror();
	return ret;
}

#else // Winsock Provider variant:

/*
 * Socket provider's variant:
 */
SOCKET WSPAPI WSPAccept(
		SOCKET			 socket,
		struct sockaddr	*addr,
		LPINT			 addrlen,
		LPCONDITIONPROC	 lpfnCondition,
        DWORD_PTR		 dwCallbackData,
        LPINT			 lpErrno
	)
{
	struct rsocket *rs, *new_rs;
	struct rdma_conn_param param;
	struct rs_conn_data *creq, cresp;
	SOCKET new_socket = INVALID_SOCKET;
	DWORD_PTR	rsock = INVALID_SOCKET;
	int			ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(
						socket,
						(PDWORD_PTR)&rsock,	// __out PDWORD_PTR lpContext
						lpErrno
					);
	if (SOCKET_ERROR == ret)
		return INVALID_SOCKET;
	
	rs = (struct rsocket *)idm_at(&idm, (int)rsock);
	new_rs = rs_alloc(rs);
	if (!new_rs) {
		ret = ERR(ENOMEM);
        return ret;
    }

	ret = rdma_get_request(rs->cm_id, &new_rs->cm_id);
	if (ret)
		goto err;

	ret = rs_insert(new_rs);
	if (ret < 0)
		goto err;

	creq = (struct rs_conn_data *) new_rs->cm_id->event->param.conn.private_data;
	if (creq->version != 1) {
		ret = ERR(ENOTSUP);
		goto err;
	}

	if (rs->fd_flags & O_NONBLOCK)
		rs_set_nonblocking(new_rs, 1);

	ret = rs_create_ep(new_rs);
	if (ret)
		goto err;

	rs_save_conn_data(new_rs, creq);
	param = new_rs->cm_id->event->param.conn;
	rs_set_conn_data(new_rs, &param, &cresp);

	if (addr && addrlen)
		rgetpeername(new_rs->index, addr, addrlen);

	if (lpfnCondition) {
		struct sockaddr local_addr;
		socklen_t       local_addrlen = sizeof(local_addr);
		WSABUF caller_id;
		WSABUF callee_id;
		WSABUF callee_data;

		/* Set the caller and callee data buffer */
		caller_id.buf = (char *)addr;
		caller_id.len = sizeof(*addr);

		rs_copy_addr(&local_addr, rdma_get_local_addr(new_rs->cm_id), &local_addrlen);

		callee_id.buf = (char *)&local_addr;
		callee_id.len = local_addrlen;
		
		callee_data.buf = NULL;
		callee_data.len = 0;

		switch (lpfnCondition(&caller_id, NULL, NULL, NULL, &callee_id, &callee_data, NULL, dwCallbackData)) {
		default:
			/* Should never happen */
			//TRACE("Conditional routine returned undocumented code");
			/* Fall through. */
		case CF_REJECT:
			//TRACE("Conditional routine returned CF_REJECT");
			errno = ECONNREFUSED;
			ret = (int)INVALID_SOCKET;
			goto err;
		case CF_DEFER:
			//TRACE("Conditional routine returned CF_DEFER");
			*lpErrno = WSATRY_AGAIN;
			ret = (int)INVALID_SOCKET;
			goto err;
		case CF_ACCEPT:
			break;
		}
	}
	
	new_rs->pNetstatEntry = rsNetstatEntryCreate(new_rs->index, lpErrno);
	new_socket = new_rs->pNetstatEntry	? new_rs->pNetstatEntry->s 
										: gMainUpCallTable.lpWPUCreateSocketHandle(
											gProtocolInfo.dwCatalogEntryId,
											new_rs->index,	// __in  DWORD_PTR dwContext
											lpErrno
										);
	if (new_socket == INVALID_SOCKET)
		goto err;
	
	rs_set_state(new_rs, rs_init);
	ret = rdma_accept(new_rs->cm_id, &param);
	if (!ret) {
		rs_set_state(new_rs, rs_connect_rdwr);
		if (new_rs->pNetstatEntry) {
			new_rs->pNetstatEntry->saSrc = new_rs->cm_id->route.addr.src_addr;
			new_rs->pNetstatEntry->saDst = new_rs->cm_id->route.addr.dst_addr;
			if (rs->pNetstatEntry) { // Copy portnr. from listening socket:
				((struct sockaddr_in *)&new_rs->pNetstatEntry->saSrc)->sin_port =
				    ((struct sockaddr_in *)&rs->pNetstatEntry->saSrc)->sin_port;
			}
		}
	} else if (errno == EAGAIN || errno == EWOULDBLOCK)
		rs_set_state(new_rs, rs_accepting);
	else
		goto err;

	return new_socket;

err:
	rs_free(new_rs);
    if (WSAErrno)
        *lpErrno = WSAErrno;

	if (new_socket != INVALID_SOCKET)
		gMainUpCallTable.lpWPUCloseSocketHandle(new_socket, lpErrno);

	return INVALID_SOCKET;
}
#endif /* 0 */

int rsetsockopt(int socket, int level, int optname,
		const void *optval, socklen_t optlen)
{
	struct rsocket *rs;
	int ret, opt_on = 0;
	uint64_t *opts = NULL;
	
	ret = ERR(ENOTSUP);
	rs = (struct rsocket *)idm_at(&idm, socket);
	switch (level) {
	case SOL_SOCKET:
		opts = &rs->so_opts;
		switch (optname) {
		case SO_REUSEADDR:
			ret = rdma_set_option(rs->cm_id, RDMA_OPTION_ID,
					      RDMA_OPTION_ID_REUSEADDR,
					      (void *) optval, optlen);
			if (ret && ((errno == ENOSYS) || ((rs->state != rs_init) &&
			    rs->cm_id->context &&
			    (rs->cm_id->verbs->device->transport_type == IBV_TRANSPORT_IB))))
				ret = 0;
			opt_on = *(int *) optval;
			break;
		case SO_RCVBUF:
			if (!rs->rbuf)
				rs->rbuf_size = (*(uint32_t *) optval) << 1;
			ret = 0;
			break;
		case SO_SNDBUF:
			if (!rs->sbuf)
				rs->sbuf_size = (*(uint32_t *) optval) << 1;
			if (rs->sbuf_size < RS_SNDLOWAT)
				rs->sbuf_size = RS_SNDLOWAT << 1;
			ret = 0;
			break;
		case SO_LINGER:
			/* Invert value so default so_opt = 0 is on */
			opt_on =  !((struct linger *) optval)->l_onoff;
			ret = 0;
			break;
		case SO_KEEPALIVE:
			opt_on = *(int *) optval;
			ret = 0;
			break;
		case SO_OOBINLINE:
			opt_on = *(int *) optval;
			ret = 0;
			break;
		default:
			break;
		}
		break;
	case IPPROTO_TCP:
		opts = &rs->tcp_opts;
		switch (optname) {
		case TCP_NODELAY:
			opt_on = *(int *) optval;
			ret = 0;
			break;
		case TCP_MAXSEG:
			ret = 0;
			break;
		default:
			break;
		}
		break;
	case IPPROTO_IPV6:
		opts = &rs->ipv6_opts;
		switch (optname) {
		case IPV6_V6ONLY:
			ret = rdma_set_option(rs->cm_id, RDMA_OPTION_ID,
					      RDMA_OPTION_ID_AFONLY,
					      (void *) optval, optlen);
			opt_on = *(int *) optval;
			break;
		default:
			break;
		}
		break;
	case SOL_RDMA:
		if (rs->state >= rs_opening) {
			ret = ERR(EINVAL);
			break;
		}

		switch (optname) {
		case RDMA_SQSIZE:
			rs->sq_size = min((*(uint32_t *) optval), RS_QP_MAX_SIZE);
			ret = 0;
			break;
		case RDMA_RQSIZE:
			rs->rq_size = min((*(uint32_t *) optval), RS_QP_MAX_SIZE);
			ret = 0;
			break;
		case RDMA_INLINE:
			rs->sq_inline = min(*(uint32_t *) optval, RS_QP_MAX_SIZE);
			if (rs->sq_inline < RS_MIN_INLINE)
				rs->sq_inline = RS_MIN_INLINE;
			ret = 0;
			break;
		case RDMA_IOMAPSIZE:
			rs->target_iomap_size = (uint16_t) rs_scale_to_value(
				(uint8_t) rs_value_to_scale(*(int *) optval, 8), 8);
			ret = 0;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	if (!ret && opts) {
		if (opt_on)
			*opts |= (uint64_t)(1 << optname);
		else
			*opts &= ~(1 << optname);
	}

    if (ret)
        wsa_setlasterror();

	return ret;
}

int rgetsockopt(int socket, int level, int optname,
		void *optval, socklen_t *optlen)
{
	struct rsocket *rs;
	int ret = 0;
	
	rs = (struct rsocket *)idm_at(&idm, socket);
	switch (level) {
	case SOL_SOCKET:
		switch (optname) {
		case SO_REUSEADDR:
		case SO_KEEPALIVE:
		case SO_OOBINLINE:
			*((int *) optval) = !!(rs->so_opts & (uint64_t)(1 << optname));
			*optlen = sizeof(int);
			break;
		case SO_RCVBUF:
			*((int *) optval) = rs->rbuf_size;
			*optlen = sizeof(int);
			break;
		case SO_SNDBUF:
			*((int *) optval) = rs->sbuf_size;
			*optlen = sizeof(int);
			break;
		case SO_LINGER:
			/* Value is inverted so default so_opt = 0 is on */
			((struct linger *) optval)->l_onoff =
					!(rs->so_opts & (uint64_t)(1 << optname));
			((struct linger *) optval)->l_linger = 0;
			*optlen = sizeof(struct linger);
			break;
		case SO_ERROR:
			*((int *) optval) = rs->err;
			*optlen = sizeof(int);
			rs->err = 0;
			break;
		default:
			ret = ENOTSUP;
			break;
		}
		break;
	case IPPROTO_TCP:
		switch (optname) {
		case TCP_NODELAY:
			*((int *) optval) = !!(rs->tcp_opts & (uint64_t)(1 << optname));
			*optlen = sizeof(int);
			break;
		case TCP_MAXSEG:
			*((int *) optval) = (rs->cm_id && rs->cm_id->route.num_paths) ?
					    1 << (7 + rs->cm_id->route.path_rec->mtu) :
					    2048;
			*optlen = sizeof(int);
			break;
		default:
			ret = ENOTSUP;
			break;
		}
		break;
	case IPPROTO_IPV6:
		switch (optname) {
		case IPV6_V6ONLY:
			*((int *) optval) = !!(rs->ipv6_opts & (uint64_t)(1 << optname));
			*optlen = sizeof(int);
			break;
		default:
			ret = ENOTSUP;
			break;
		}
		break;
	case SOL_RDMA:
		switch (optname) {
		case RDMA_SQSIZE:
			*((int *) optval) = rs->sq_size;
			*optlen = sizeof(int);
			break;
		case RDMA_RQSIZE:
			*((int *) optval) = rs->rq_size;
			*optlen = sizeof(int);
			break;
		case RDMA_INLINE:
			*((int *) optval) = rs->sq_inline;
			*optlen = sizeof(int);
			break;
		case RDMA_IOMAPSIZE:
			*((int *) optval) = rs->target_iomap_size;
			*optlen = sizeof(int);
			break;
		default:
			ret = ENOTSUP;
			break;
		}
		break;
	default:
		ret = ENOTSUP;
		break;
	}
    
	return ERR(ret);
}

static struct rs_iomap_mr *rs_get_iomap_mr(struct rsocket *rs)
{
	int i;

	if (!rs->remote_iomappings) {
		rs->remote_iomappings = (rs_iomap_mr *)calloc(rs->remote_iomap.length,
					       sizeof(*rs->remote_iomappings));
		if (!rs->remote_iomappings)
			return NULL;

		for (i = 0; i < (int)rs->remote_iomap.length; i++)
			rs->remote_iomappings[i].index = i;
	}

	for (i = 0; i < (int)rs->remote_iomap.length; i++) {
		if (!rs->remote_iomappings[i].mr)
			return &rs->remote_iomappings[i];
	}
	return NULL;
}

/*
 * If an offset is given, we map to it.  If offset is -1, then we map the
 * offset to the address of buf.  We do not check for conflicts, which must
 * be fixed at some point.
 */
off_t riomap(int socket, void *buf, size_t len, int prot, int flags, off_t offset)
{
	struct rsocket *rs;
	struct rs_iomap_mr *iomr;
	int access = IBV_ACCESS_LOCAL_WRITE;
    int ret    = 0;

	rs = (struct rsocket *)idm_at(&idm, socket);
	if (!rs->cm_id->pd || (prot & ~(PROT_WRITE | PROT_NONE))) {
		ret = ERR(EINVAL);
        return ret;
    }

	fastlock_acquire(&rs->iomap_lock);
	if (prot & PROT_WRITE) {
		iomr = rs_get_iomap_mr(rs);
		access |= IBV_ACCESS_REMOTE_WRITE;
	} else {
		iomr = (rs_iomap_mr *)calloc(1, sizeof *iomr);
		iomr->index = -1;
	}
	if (!iomr) {
		offset = ERR(ENOMEM);
		goto out;
	}

	iomr->mr = ibv_reg_mr(rs->cm_id->pd, buf, len, access);
	if (!iomr->mr) {
		if (iomr->index < 0)
			free(iomr);
		offset = ERR(ENOMEM);
		goto out;
	}

	if (offset == -1)
		offset = (off_t) buf;
	iomr->offset = offset;
	cl_atomic_xchg(&iomr->refcnt, 1);

	if (iomr->index >= 0) {
		dlist_insert_tail(&iomr->entry, &rs->iomap_queue);
		rs->iomap_pending = 1;
	} else {
		dlist_insert_tail(&iomr->entry, &rs->iomap_list);
	}
out:
	fastlock_release(&rs->iomap_lock);
	return offset;
}

int riounmap(int socket, void *buf, size_t len)
{
	struct rsocket *rs;
	struct rs_iomap_mr *iomr;
	dlist_entry *entry;
	int ret = 0;

	rs = (struct rsocket *)idm_at(&idm, socket);
	fastlock_acquire(&rs->iomap_lock);

	for (entry = rs->iomap_list.Next; entry != &rs->iomap_list;
	     entry = entry->Next) {
		iomr = container_of(entry, struct rs_iomap_mr, entry);
		if (iomr->mr->addr == buf && iomr->mr->length == len) {
			rs_release_iomap_mr(iomr);
			goto out;
		}
	}

	for (entry = rs->iomap_queue.Next; entry != &rs->iomap_queue;
	     entry = entry->Next) {
		iomr = container_of(entry, struct rs_iomap_mr, entry);
		if (iomr->mr->addr == buf && iomr->mr->length == len) {
			rs_release_iomap_mr(iomr);
			goto out;
		}
	}
	ret = ERR(EINVAL);
out:
	fastlock_release(&rs->iomap_lock);
	return ret;
}

static struct rs_iomap *rs_find_iomap(struct rsocket *rs, off_t offset)
{
	int i;

	for (i = 0; i < rs->target_iomap_size; i++) {
		if (offset >= rs->target_iomap[i].offset &&
		    offset < rs->target_iomap[i].offset + rs->target_iomap[i].sge.length)
			return &rs->target_iomap[i];
	}
	return NULL;
}

size_t riowrite(int socket, const void *buf, size_t count, off_t offset, int flags)
{
	struct rsocket *rs;
	struct rs_iomap *iom = NULL;
	struct ibv_sge sge;
	char          *bufb = (char *)buf;
	size_t left = count;
	uint32_t xfer_size, olen = RS_OLAP_START_SIZE;
	int ret = 0;

	rs = (struct rsocket *)idm_at(&idm, socket);
	fastlock_acquire(&rs->slock);
	if (rs->iomap_pending) {
		ret = rs_send_iomaps(rs, flags);
		if (ret)
			goto out;
	}
	for (; left; left -= xfer_size, bufb += xfer_size, offset += xfer_size) {
		if (!iom || offset > iom->offset + iom->sge.length) {
			iom = rs_find_iomap(rs, offset);
			if (!iom)
				break;
		}

		if (!rs_can_send(rs)) {
			ret = rs_get_comp(rs, rs_nonblocking(rs),
					  rs_conn_can_send);
			if (ret)
				break;
			if (!(rs->state & rs_connect_wr)) {
				ret = ERR(ECONNRESET);
				break;
			}
		}

		if (olen < left) {
			xfer_size = olen;
			if (olen < RS_MAX_TRANSFER)
				olen <<= 1;
		} else {
			xfer_size = left;
		}

		if ((int32_t)xfer_size > rs->sbuf_bytes_avail)
			xfer_size = rs->sbuf_bytes_avail;
		if (xfer_size > iom->offset + iom->sge.length - offset)
			xfer_size = (uint32_t)(iom->offset + iom->sge.length - offset);

		if (xfer_size <= rs->sq_inline) {
			sge.addr = (uintptr_t) bufb;
			sge.length = xfer_size;
			sge.lkey = 0;
			ret = rs_write_direct(rs, iom, offset, &sge, 1,
					      xfer_size, IBV_SEND_INLINE);
		} else if (xfer_size <= rs_sbuf_left(rs)) {
			memcpy((void *) (uintptr_t) rs->ssgl[0].addr, bufb, xfer_size);
			rs->ssgl[0].length = xfer_size;
			ret = rs_write_direct(rs, iom, offset, rs->ssgl, 1, xfer_size, 0);
			if (xfer_size < rs_sbuf_left(rs))
				rs->ssgl[0].addr += xfer_size;
			else
				rs->ssgl[0].addr = (uintptr_t) rs->sbuf;
		} else {
			rs->ssgl[0].length = rs_sbuf_left(rs);
			memcpy((void *) (uintptr_t) rs->ssgl[0].addr, bufb,
				rs->ssgl[0].length);
			rs->ssgl[1].length = xfer_size - rs->ssgl[0].length;
			memcpy(rs->sbuf, bufb + rs->ssgl[0].length, rs->ssgl[1].length);
			ret = rs_write_direct(rs, iom, offset, rs->ssgl, 2, xfer_size, 0);
			rs->ssgl[0].addr = (uintptr_t) rs->sbuf + rs->ssgl[1].length;
		}
		if (ret)
			break;
	}
out:
	fastlock_release(&rs->slock);

	if (ret && left == count) {
		wsa_setlasterror();
		return ret;
	} else
		return count - left;
}

int rioctlsocket(int socket, long cmd, u_long* argp)
{
    int ret = 0;
	struct rsocket *rs;
	
	rs = (struct rsocket *)idm_at(&idm, socket);
	switch (cmd) {
		case FIONBIO:
			if (*argp)
				rs->fd_flags |= O_NONBLOCK;
			ret = rs_set_nonblocking(rs, *argp ? 1 : 0);
            break;
		case FIONREAD:
		case SIOCATMARK:
			ret = ERR(ENOTSUP);
            break;
        default:
			ret = ERR(ENOTSUP);
	}

    return ret;
}
