/*
 * Copyright (c) 2005-2009 Intel Corporation.  All rights reserved.
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

#include <windows.h>
#include <winsock2.h>
#include "openib_osd.h"
#include <stdio.h>
#include <iphlpapi.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>
#include <_errno.h>
#include <comp_channel.h>
#include <iba/ibat.h>
#include "cma.h"
#include "..\..\..\etc\user\comp_channel.cpp"

extern __declspec(thread) int WSAErrno;

static struct ibvw_windata windata;

enum cma_state
{
	cma_idle,
	cma_listening,
	cma_get_request,
	cma_addr_bind,
	cma_addr_resolve,
	cma_route_resolve,
	cma_passive_connect,
	cma_active_connect,
	cma_active_accept,
	cma_accepting,
	cma_connected,
	cma_active_disconnect,
	cma_passive_disconnect,
	cma_disconnected,
	cma_destroying
};

#define CMA_DEFAULT_BACKLOG		16

struct cma_id_private
{
	struct rdma_cm_id			id;
	enum cma_state				state;
	struct cma_device			*cma_dev;
	int							sync;
	int							backlog;
	int							index;
	volatile LONG				refcnt;
	struct rdma_cm_id			**req_list;
	struct ibv_pd				*pd;
	struct ibv_qp_init_attr		*qp_init_attr;
	uint8_t						initiator_depth;
	uint8_t						responder_resources;
};

struct cma_device
{
	struct ibv_context	*verbs;
	struct ibv_pd		*pd;
	uint64_t			guid;
	int					port_cnt;
	uint8_t				max_initiator_depth;
	uint8_t				max_responder_resources;
	int					max_qpsize;
};

struct cma_event {
	struct rdma_cm_event	event;
	uint8_t					private_data[RDMA_MAX_PRIVATE_DATA];
	struct cma_id_private	*id_priv;
};

static struct cma_device *cma_dev_array;
static int cma_dev_cnt;
static DWORD ref;

void wsa_setlasterror(void)
{
	int wsa_err = 0;
	switch (errno) {
		case 0:													break;
		case EADDRINUSE:		wsa_err = WSAEADDRINUSE;		break;
		case EADDRNOTAVAIL:		wsa_err = WSAEADDRNOTAVAIL;		break;
		case EAFNOSUPPORT:		wsa_err = WSAEAFNOSUPPORT;		break;
		case EALREADY:			wsa_err = WSAEALREADY;			break;
//		case EBADMSG:			wsa_err = ; break;
		case ECANCELED:			wsa_err = WSAECANCELLED;		break;
		case ECONNABORTED:		wsa_err = WSAECONNABORTED;		break;
		case ECONNREFUSED:		wsa_err = WSAECONNREFUSED;		break;
		case ECONNRESET:		wsa_err = WSAECONNRESET;		break;
		case EDESTADDRREQ:		wsa_err = WSAEDESTADDRREQ;		break;
		case EHOSTUNREACH:		wsa_err = WSAEHOSTUNREACH;		break;
//		case EIDRM:				wsa_err = ; break;
		case EINPROGRESS:		wsa_err = WSAEINPROGRESS;		break;
		case EISCONN:			wsa_err = WSAEISCONN;			break;
		case ELOOP:				wsa_err = WSAELOOP;				break;
		case EMSGSIZE:			wsa_err = WSAEMSGSIZE;			break;
		case ENETDOWN:			wsa_err = WSAENETDOWN;			break;
		case ENETRESET:			wsa_err = WSAENETRESET;			break;
		case ENETUNREACH:		wsa_err = WSAENETUNREACH;		break;
		case ENOBUFS:			wsa_err = WSAENOBUFS;			break;
//		case ENODATA:			wsa_err = ; break;
//		case ENOLINK:			wsa_err = ; break;
//		case ENOMSG:			wsa_err = ; break;
		case ENOPROTOOPT:		wsa_err = WSAENOPROTOOPT;		break;
//		case ENOSR:				wsa_err = ; break;
//		case ENOSTR:			wsa_err = ; break;
		case ENOTCONN:			wsa_err = WSAENOTCONN;			break;
		case ENOTRECOVERABLE:	wsa_err = WSANO_RECOVERY;		break;
		case ENOTSOCK:			wsa_err = WSAENOTSOCK;			break;
		case ENOTSUP:
		case EINVAL:			wsa_err = WSAEINVAL;			break; //???
		case EOPNOTSUPP:		wsa_err = WSAEOPNOTSUPP;		break;
//		case EOTHER:			wsa_err = ; break;
//		case EOVERFLOW:			wsa_err = ; break;
//		case EOWNERDEAD:		wsa_err = ; break;
		case EPROTO:			wsa_err = WSAENOPROTOOPT;		break; //???
		case EPROTONOSUPPORT:	wsa_err = WSAEPROTONOSUPPORT;	break;
		case EPROTOTYPE:		wsa_err = WSAEPROTOTYPE;		break;
//		case ETIME:				wsa_err = ; break;
		case ETIMEDOUT:			wsa_err = WSAETIMEDOUT;			break;
//		case ETXTBSY:			wsa_err = ; break;
		case ENOMEM:			wsa_err = WSA_NOT_ENOUGH_MEMORY;break;
		case EAGAIN:
		case EWOULDBLOCK:		wsa_err = WSAEWOULDBLOCK;		break;
		default:				wsa_err = WSASYSCALLFAILURE;
	}

	if (wsa_err) {
        WSAErrno = wsa_err;
    }
}

static int ucma_acquire(void)
{
	struct ibv_device **dev_list = NULL;
	struct cma_device *cma_dev;
	struct ibv_device_attr attr;
	int i, ret, dev_cnt;

	fastlock_acquire(&lock);
	if (ref++) {
		goto out;
	}

	ret = ibvw_get_windata(&windata, IBVW_WINDATA_VERSION);
	if (ret) {
		goto err1;
	}

	dev_list = ibv_get_device_list(&dev_cnt);
	if (dev_list == NULL) {
		ret = -1;
		goto err2;
	}

	cma_dev_array = new struct cma_device[dev_cnt];
	if (cma_dev_array == NULL) {
		ret = -1;
		goto err3;
	}

	for (i = 0; dev_list[i];) {
		cma_dev = &cma_dev_array[i];

		cma_dev->guid = ibv_get_device_guid(dev_list[i]);
		cma_dev->verbs = ibv_open_device(dev_list[i]);
		if (cma_dev->verbs == NULL) {
			ret = -1;
			goto err4;
		}

		cma_dev->pd = ibv_alloc_pd(cma_dev->verbs);
		if (cma_dev->pd == NULL) {
			ibv_close_device(cma_dev->verbs);
			ret = -1;
			goto err4;
		}

		++i;
		ret = ibv_query_device(cma_dev->verbs, &attr);
		if (ret) {
			goto err4;
		}

		cma_dev->port_cnt = attr.phys_port_cnt;
		cma_dev->max_qpsize = attr.max_qp_wr;
		cma_dev->max_initiator_depth = (uint8_t) attr.max_qp_init_rd_atom;
		cma_dev->max_responder_resources = (uint8_t) attr.max_qp_rd_atom;
	}
	ibv_free_device_list(dev_list);
	cma_dev_cnt = dev_cnt;
out:
	fastlock_release(&lock);
	return 0;

err4:
	while (i--) {
		ibv_dealloc_pd(cma_dev_array[i].pd);
		ibv_close_device(cma_dev_array[i].verbs);
	}
	delete cma_dev_array;
err3:
	ibv_free_device_list(dev_list);
err2:
	ibvw_release_windata(&windata, IBVW_WINDATA_VERSION);
err1:
	ref--;
	fastlock_release(&lock);
	return ret;
}

void ucma_release(void)
{
	int i;

	fastlock_acquire(&lock);
	if (--ref == 0) {
		for (i = 0; i < cma_dev_cnt; i++) {
			ibv_dealloc_pd(cma_dev_array[i].pd);
			ibv_close_device(cma_dev_array[i].verbs);
		}
		delete cma_dev_array;
		cma_dev_cnt = 0;
		ibvw_release_windata(&windata, IBVW_WINDATA_VERSION);
	}
	fastlock_release(&lock);
}

__declspec(dllexport)
struct ibv_context **rdma_get_devices(int *num_devices)
{
	struct ibv_context **devs = NULL;
	int i;

	if (ucma_acquire()) {
		goto out;
	}

	devs = new struct ibv_context *[cma_dev_cnt + 1];
	if (devs == NULL) {
		goto out;
	}

	for (i = 0; i < cma_dev_cnt; i++) {
		devs[i] = cma_dev_array[i].verbs;
	}
	devs[i] = NULL;
out:
	if (num_devices != NULL) {
		*num_devices = devs ? cma_dev_cnt : 0;
	}
	return devs;
}

__declspec(dllexport)
void rdma_free_devices(struct ibv_context **list)
{
	delete list;
	ucma_release();
}

__declspec(dllexport)
struct rdma_event_channel *rdma_create_event_channel(void)
{
	struct rdma_event_channel *channel;

	if (ucma_acquire()) {
		return NULL;
	}

	channel = new struct rdma_event_channel;
	if (channel == NULL) {
		return NULL;
	}

	CompChannelInit(windata.comp_mgr, &channel->channel, INFINITE);
	return channel;
}

__declspec(dllexport)
void rdma_destroy_event_channel(struct rdma_event_channel *channel)
{
	CompChannelCleanup(&channel->channel);
	delete channel;
	ucma_release();
}

__declspec(dllexport)
int rdma_create_id(struct rdma_event_channel *channel,
				   struct rdma_cm_id **id, void *context,
				   enum rdma_port_space ps)
{
	struct cma_id_private *id_priv;
	HRESULT hr;
	int ret;

	ret = ucma_acquire();
	if (ret) {
		return ret;
	}

	id_priv = new struct cma_id_private;
	if (id_priv == NULL) {
		ret = ENOMEM;
		goto err1;
	}

	RtlZeroMemory(id_priv, sizeof(struct cma_id_private));
	id_priv->refcnt = 1;
	id_priv->id.context = context;

	if (!channel) {
		id_priv->id.channel = rdma_create_event_channel();
		if (!id_priv->id.channel) {
			goto err2;
		}
		id_priv->sync = 1;
	} else {
		id_priv->id.channel = channel;
	}
	id_priv->id.ps = ps;
	CompEntryInit(&id_priv->id.channel->channel, &id_priv->id.comp_entry);

	if (ps == RDMA_PS_TCP) {
		hr = windata.prov->CreateConnectEndpoint(&id_priv->id.ep.connect);
	} else {
		hr = windata.prov->CreateDatagramEndpoint(&id_priv->id.ep.datagram);
	}
	if (FAILED(hr)) {
		ret = ibvw_wv_errno(hr);
		goto err2;
	}

	*id = &id_priv->id;
	return 0;

err2:
	delete id_priv;
err1:
	ucma_release();
	return hr;
}

static void ucma_destroy_listen(struct cma_id_private *id_priv)
{
	while (--id_priv->backlog >= 0) {
		if (id_priv->req_list[id_priv->backlog] != NULL) {
			InterlockedDecrement(&id_priv->refcnt);
			rdma_destroy_id(id_priv->req_list[id_priv->backlog]);
		}
	}

	delete id_priv->req_list;
}

__declspec(dllexport)
int rdma_destroy_id(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);

	fastlock_acquire(&lock);
	id_priv->state = cma_destroying;
	fastlock_release(&lock);

	if (id->ps == RDMA_PS_TCP) {
		id->ep.connect->CancelOverlappedRequests();
	} else {
		id->ep.datagram->CancelOverlappedRequests();
	}

	if (CompEntryCancel(&id->comp_entry) != NULL) {
		InterlockedDecrement(&id_priv->refcnt);
	}

	if (id_priv->backlog > 0) {
		ucma_destroy_listen(id_priv);
	}

	if (id_priv->id.ps == RDMA_PS_TCP) {
		id_priv->id.ep.connect->Release();
	} else {
		id_priv->id.ep.datagram->Release();
	}

	if (id->event) {
		rdma_ack_cm_event(id->event);
	}
	InterlockedDecrement(&id_priv->refcnt);
	while (id_priv->refcnt) {
		Sleep(0);
	}
	if (id_priv->sync) {
		rdma_destroy_event_channel(id->channel);
	}
	delete id_priv;
	ucma_release();
	return 0;
}

static int ucma_addrlen(struct sockaddr *addr)
{
	if (addr->sa_family == PF_INET) {
		return sizeof(struct sockaddr_in);
	} else {
		return sizeof(struct sockaddr_in6);
	}
}

static int ucma_get_device(struct cma_id_private *id_priv, uint64_t guid)
{
	struct cma_device *cma_dev;
	int i;

	for (i = 0; i < cma_dev_cnt; i++) {
		cma_dev = &cma_dev_array[i];
		if (cma_dev->guid == guid) {
			id_priv->cma_dev = cma_dev;
			id_priv->id.verbs = cma_dev->verbs;
			id_priv->id.pd = cma_dev->pd;
			return 0;
		}
	}
	return -1;
}

static int ucma_query_connect(struct rdma_cm_id *id, struct rdma_conn_param *param)
{
	struct cma_id_private *id_priv;
	WV_CONNECT_ATTRIBUTES attr;
	HRESULT hr;
	int ret;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	hr = id->ep.connect->Query(&attr);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	RtlCopyMemory(&id->route.addr.src_addr, &attr.LocalAddress,
				  sizeof attr.LocalAddress);
	RtlCopyMemory(&id->route.addr.dst_addr, &attr.PeerAddress,
				  sizeof attr.PeerAddress);

	if (param != NULL) {
		RtlCopyMemory((void *) param->private_data, attr.Param.Data,
					  attr.Param.DataLength);
		param->private_data_len = (uint8_t) attr.Param.DataLength;
		param->responder_resources = (uint8_t) attr.Param.ResponderResources;
		param->initiator_depth = (uint8_t) attr.Param.InitiatorDepth;
		param->flow_control = 1;
		param->retry_count = attr.Param.RetryCount;
		param->rnr_retry_count = attr.Param.RnrRetryCount;
	}

	if (id_priv->cma_dev == NULL && attr.Device.DeviceGuid != 0) {
		ret = ucma_get_device(id_priv, attr.Device.DeviceGuid);
		if (ret) {
			return ret;
		}

		id->route.addr.addr.ibaddr.pkey = attr.Device.Pkey;
		id_priv->id.port_num = attr.Device.PortNumber;
	}

	return 0;
}

static int ucma_query_datagram(struct rdma_cm_id *id, struct rdma_ud_param *param)
{
	struct cma_id_private *id_priv;
	WV_DATAGRAM_ATTRIBUTES attr;
	HRESULT hr;
	int ret;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	hr = id->ep.datagram->Query(&attr);
	if (FAILED(hr)) {
		return hr;
	}

	RtlCopyMemory(&id->route.addr.src_addr, &attr.LocalAddress,
				  sizeof attr.LocalAddress);
	RtlCopyMemory(&id->route.addr.dst_addr, &attr.PeerAddress,
				  sizeof attr.PeerAddress);

	if (param != NULL) {
		RtlCopyMemory((void *) param->private_data, attr.Param.Data,
					  attr.Param.DataLength);
		param->private_data_len = (uint8_t) attr.Param.DataLength;
		// ucma_convert_av(&attr.Param.AddressVector, param->ah_attr)
		param->qp_num = attr.Param.Qpn;
		param->qkey = attr.Param.Qkey;
	}

	if (id_priv->cma_dev == NULL && attr.Device.DeviceGuid != 0) {
		ret = ucma_get_device(id_priv, attr.Device.DeviceGuid);
		if (ret)
			return ret;
		id->route.addr.addr.ibaddr.pkey = attr.Device.Pkey;
		id_priv->id.port_num = attr.Device.PortNumber;
	}
	return 0;
}

__declspec(dllexport)
int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr)
{
	struct cma_id_private *id_priv;
	HRESULT hr;
	int ret;

	if (id->ps == RDMA_PS_TCP) {
		hr = id->ep.connect->BindAddress(addr);
		if (SUCCEEDED(hr)) {
			ret = ucma_query_connect(id, NULL);
		} else {
			ret = ibvw_wv_errno(hr);
		}
	} else {
		hr = id->ep.datagram->BindAddress(addr);
		if (SUCCEEDED(hr)) {
			ret = ucma_query_datagram(id, NULL);
		} else {
			ret = ibvw_wv_errno(hr);
		}
	}

	if (!ret) {
		id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
		id_priv->state = cma_addr_bind;
	}

	return ret;
}

static int ucma_complete_priv(struct cma_id_private *id_priv)
{
	return ucma_complete(&id_priv->id);
}

int ucma_complete(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct cma_id_private, id);

	if (!id_priv->sync)
		return 0;

	if (id_priv->id.event) {
		rdma_ack_cm_event(id_priv->id.event);
		id_priv->id.event = NULL;
	}

	ret = rdma_get_cm_event(id_priv->id.channel, &id_priv->id.event);
	if (ret)
		return ret;

	if (id_priv->id.event->status) {
		if (id_priv->id.event->event == RDMA_CM_EVENT_REJECTED)
			ret = ERR(ECONNREFUSED);
		else if (id_priv->id.event->status < 0)
			ret = ERR(-id_priv->id.event->status);
		else
			ret = ERR(-id_priv->id.event->status);
	}
	return ret;
}

__declspec(dllexport)
int rdma_resolve_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
					  struct sockaddr *dst_addr, int timeout_ms)
{
	struct cma_id_private *id_priv;
	WV_SOCKADDR addr;
	SOCKET s;
	DWORD size;
	HRESULT hr;
	int ret;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	if (id_priv->state == cma_idle) {
		if (src_addr == NULL) {
			if (id->ps == RDMA_PS_TCP) {
				s = socket(dst_addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
			} else {
				s = socket(dst_addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
			}

			if (s == INVALID_SOCKET) {
				return rdmaw_wsa_errno(WSAGetLastError());
			}

			hr = WSAIoctl(s, SIO_ROUTING_INTERFACE_QUERY, dst_addr, ucma_addrlen(dst_addr),
						  &addr, sizeof addr, &size, NULL, NULL);
			closesocket(s);
			if (FAILED(hr)) {
				return rdmaw_wsa_errno(WSAGetLastError());
			}

			src_addr = &addr.Sa;
		}

		ret = rdma_bind_addr(id, src_addr);
		if (ret) {
			return ret;
		}
	}

	if (((struct sockaddr_in *)&id->route.addr.dst_addr)->sin_port == 0) {
		// port = 0 => Assume that entire dst_addr hasn't been set yet
		RtlCopyMemory(&id->route.addr.dst_addr, dst_addr, ucma_addrlen(dst_addr));
	}

	id_priv->state = cma_addr_resolve;
	id_priv->refcnt++;
	CompEntryPost(&id->comp_entry);
	return ucma_complete_priv(id_priv);
}

__declspec(dllexport)
int rdma_resolve_route(struct rdma_cm_id *id, int timeout_ms)
{
	struct cma_id_private *id_priv;
	IBAT_PATH_BLOB path;
	HRESULT hr;

    UNREFERENCED_PARAMETER(timeout_ms);

    hr = IBAT::QueryPath(&id->route.addr.src_addr, &id->route.addr.dst_addr,
                         &path);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	hr = (id->ps == RDMA_PS_TCP) ?
		 id->ep.connect->Modify(WV_EP_OPTION_ROUTE, &path, sizeof path) :
		 id->ep.datagram->Modify(WV_EP_OPTION_ROUTE, &path, sizeof path);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	id_priv->state = cma_route_resolve;

	id_priv->refcnt++;
	CompEntryPost(&id->comp_entry);
	return ucma_complete_priv(id_priv);
}

static int ucma_modify_qp_init(struct cma_id_private *id_priv, struct ibv_qp *qp)
{
	struct ibv_qp_attr qp_attr;
	UINT16 index;
	HRESULT hr;

	RtlZeroMemory(&qp_attr, sizeof qp_attr);
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = id_priv->id.port_num;
	hr = qp->context->cmd_if->FindPkey(id_priv->id.port_num,
									   id_priv->id.route.addr.addr.ibaddr.pkey,
									   &index);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	qp_attr.pkey_index = index;
	return ibv_modify_qp(qp, &qp_attr, (enum ibv_qp_attr_mask)
						 (IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT));
}

static int ucma_init_ud_qp(struct cma_id_private *id_priv, struct ibv_qp *qp)
{
	struct ibv_qp_attr qp_attr;
	int qp_attr_mask, ret;

	ret = ucma_modify_qp_init(id_priv, qp);
	if (ret) {
		return ret;
	}

	qp_attr.qp_state = IBV_QPS_RTR;
	ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE);
	if (ret) {
		return ret;
	}

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	return ibv_modify_qp(qp, &qp_attr, (enum ibv_qp_attr_mask)
						 (IBV_QP_STATE | IBV_QP_SQ_PSN));
}

static void ucma_destroy_cqs(struct rdma_cm_id *id)
{
	if (id->recv_cq)
		ibv_destroy_cq(id->recv_cq);

	if (id->recv_cq_channel)
		ibv_destroy_comp_channel(id->recv_cq_channel);

	if (id->send_cq && id->send_cq != id->recv_cq)
		ibv_destroy_cq(id->send_cq);

	if (id->send_cq_channel && id->send_cq_channel != id->recv_cq_channel)
		ibv_destroy_comp_channel(id->send_cq_channel);
}

static int ucma_create_cqs(struct rdma_cm_id *id, struct ibv_qp_init_attr *attr)
{
	if (!attr->recv_cq) {
		id->recv_cq_channel = ibv_create_comp_channel(id->verbs);
		if (!id->recv_cq_channel)
			goto err;

		id->recv_cq = ibv_create_cq(id->verbs, attr->cap.max_recv_wr,
					    id, id->recv_cq_channel, 0);
		if (!id->recv_cq)
			goto err;

		attr->recv_cq = id->recv_cq;
	}

	if (!attr->send_cq) {
		id->send_cq_channel = ibv_create_comp_channel(id->verbs);
		if (!id->send_cq_channel)
			goto err;

		id->send_cq = ibv_create_cq(id->verbs, attr->cap.max_send_wr,
					    id, id->send_cq_channel, 0);
		if (!id->send_cq)
			goto err;

		attr->send_cq = id->send_cq;
	}

	return 0;
err:
	ucma_destroy_cqs(id);
	return rdma_seterrno(ENOMEM);
}

__declspec(dllexport)
int rdma_create_qp(struct rdma_cm_id *id, struct ibv_pd *pd,
				   struct ibv_qp_init_attr *qp_init_attr)
{
	struct cma_id_private *id_priv;
	struct ibv_qp *qp;
	int ret;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	if (!pd) {
		pd = id_priv->cma_dev->pd;
	} else if (id->verbs != pd->context) {
		return rdma_seterrno(EINVAL);
	}

	ret = ucma_create_cqs(id, qp_init_attr);
	if (ret) {
		return ret;
	}

	qp = ibv_create_qp(pd, qp_init_attr);
	if (!qp) {
		ret = rdma_seterrno(ENOMEM);
		goto err1;
	}

	if (id->ps == RDMA_PS_TCP) {
		ret = ucma_modify_qp_init(id_priv, qp);
	} else {
		ret = ucma_init_ud_qp(id_priv, qp);
	}
	if (ret) {
		goto err2;
	}

	id->qp = qp;
	return 0;
err2:
	ibv_destroy_qp(qp);
err1:
	ucma_destroy_cqs(id);
	return ret;
}

__declspec(dllexport)
void rdma_destroy_qp(struct rdma_cm_id *id)
{
	ibv_destroy_qp(id->qp);
	ucma_destroy_cqs(id);
}

static int ucma_valid_param(struct cma_id_private *id_priv,
							struct rdma_conn_param *param)
{
	if (id_priv->id.ps != RDMA_PS_TCP) {
		return 0;
	}

	if (!id_priv->id.qp && !param) {
		return rdma_seterrno(EINVAL);
	}

	if (!param) {
		return 0;
	}

	if ((param->responder_resources != RDMA_MAX_RESP_RES) &&
		(param->responder_resources > id_priv->cma_dev->max_responder_resources)) {
		return rdma_seterrno(EINVAL);
	}
		
	if ((param->initiator_depth != RDMA_MAX_INIT_DEPTH) &&
		(param->initiator_depth > id_priv->cma_dev->max_initiator_depth)) {
		return rdma_seterrno(EINVAL);
	}

	if (param->private_data_len > sizeof(((WV_CONNECT_PARAM *) NULL)->Data)) {
		return rdma_seterrno(EINVAL);
	}

	return 0;
}

static void ucma_set_connect_attr(struct cma_id_private *id_priv,
								  struct rdma_conn_param *param,
								  WV_CONNECT_PARAM *attr)
{
	RtlZeroMemory(attr, sizeof *attr);

	attr->ResponderResources = id_priv->responder_resources;
	attr->InitiatorDepth = id_priv->initiator_depth;

	if (param) {
		attr->RetryCount = param->retry_count;
		attr->RnrRetryCount = param->rnr_retry_count;
		if ((attr->DataLength = param->private_data_len)) {
			RtlCopyMemory(attr->Data, param->private_data, attr->DataLength);
		}
	} else {
		attr->RetryCount = 7;
		attr->RnrRetryCount = 7;
	}
}

__declspec(dllexport)
int rdma_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct cma_id_private *id_priv;
	WV_CONNECT_PARAM attr;
	HRESULT hr;
	int ret;
	
	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	ret = ucma_valid_param(id_priv, conn_param);
	if (ret) {
		return ret;
	}

	if (conn_param && conn_param->responder_resources != RDMA_MAX_RESP_RES) {
		id_priv->responder_resources = conn_param->responder_resources;
	} else {
		id_priv->responder_resources = id_priv->cma_dev->max_responder_resources;
	}
	if (conn_param && conn_param->initiator_depth != RDMA_MAX_INIT_DEPTH) {
		id_priv->initiator_depth = conn_param->initiator_depth;
	} else {
		id_priv->initiator_depth = id_priv->cma_dev->max_initiator_depth;
	}

	ucma_set_connect_attr(id_priv, conn_param, &attr);

	id_priv->state = cma_active_connect;
	id_priv->refcnt++;
	id->comp_entry.Busy = 1;
	hr = id->ep.connect->Connect(id->qp->conn_handle, &id->route.addr.dst_addr,
								 &attr, &id->comp_entry.Overlap);
	if (FAILED(hr) && hr != WV_IO_PENDING) {
		id_priv->refcnt--;
		id->comp_entry.Busy = 0;
		id_priv->state = cma_route_resolve;
		return ibvw_wv_errno(hr);
	}

	return ucma_complete_priv(id_priv);
}

static int ucma_get_request(struct cma_id_private *listen, int index)
{
	struct cma_id_private *id_priv = NULL;
	HRESULT hr;
	int ret;

	fastlock_acquire(&lock);
	if (listen->state != cma_listening) {
		ret = ibvw_wv_errno(WV_INVALID_PARAMETER);
		goto err1;
	}

	InterlockedIncrement(&listen->refcnt);
	ret = rdma_create_id(listen->id.channel, &listen->req_list[index],
						listen, listen->id.ps);
	if (ret) {
		goto err2;
	}

	id_priv = CONTAINING_RECORD(listen->req_list[index], struct cma_id_private, id);
	id_priv->index = index;
	id_priv->state = cma_get_request;

	id_priv->refcnt++;
	id_priv->id.comp_entry.Busy = 1;
	if (listen->id.ps == RDMA_PS_TCP) {
		hr = listen->id.ep.connect->GetRequest(id_priv->id.ep.connect,
											   &id_priv->id.comp_entry.Overlap);
	} else {
		hr = listen->id.ep.datagram->GetRequest(id_priv->id.ep.datagram,
												&id_priv->id.comp_entry.Overlap);
	}
	if (FAILED(hr) && hr != WV_IO_PENDING) {
		ret = ibvw_wv_errno(hr);
		id_priv->id.comp_entry.Busy = 0;
		id_priv->refcnt--;
		goto err2;
	}
	fastlock_release(&lock);

	return 0;

err2:
	InterlockedDecrement(&listen->refcnt);
err1:
	fastlock_release(&lock);
	if (id_priv != NULL) {
		rdma_destroy_id(&id_priv->id);
	}
	return ret;
}

__declspec(dllexport)
int rdma_listen(struct rdma_cm_id *id, int backlog)
{
	struct cma_id_private *id_priv, *req_id;
	HRESULT hr;
	int i, ret;

	if (backlog <= 0) {
		backlog = CMA_DEFAULT_BACKLOG;
	}

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	id_priv->req_list = new struct rdma_cm_id*[backlog];
	if (id_priv->req_list == NULL) {
		return -1;
	}

	RtlZeroMemory(id_priv->req_list, sizeof(struct rdma_cm_id *) * backlog);
	id_priv->backlog = backlog;

	id_priv->state = cma_listening;
	hr = (id->ps == RDMA_PS_TCP) ?
		 id->ep.connect->Listen(backlog) : id->ep.datagram->Listen(backlog);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	for (i = 0; i < backlog; i++) {
		ret = ucma_get_request(id_priv, i);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

__declspec(dllexport)
int rdma_get_request(struct rdma_cm_id *listen, struct rdma_cm_id **id)
{
	struct cma_id_private *id_priv;
	struct rdma_cm_event *event;
	int ret;

	id_priv = CONTAINING_RECORD(listen, struct cma_id_private, id);
	if (!id_priv->sync) {
		return rdma_seterrno(EINVAL);
	}

	if (listen->event) {
		rdma_ack_cm_event(listen->event);
		listen->event = NULL;
	}

	ret = rdma_get_cm_event(listen->channel, &event);
	if (ret)
		return ret;

	if (event->status) {
		ret = rdma_seterrno(event->status);
		goto err;
	}
	
	if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
		ret = rdma_seterrno(EINVAL);
		goto err;
	}

	if (id_priv->qp_init_attr) {
		ret = rdma_create_qp(event->id, id_priv->pd, id_priv->qp_init_attr);
		if (ret)
			goto err;
	}

	*id = event->id;
	(*id)->event = event;
	return 0;

err:
	listen->event = event;
	return ret;
}

__declspec(dllexport)
int rdma_accept(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct cma_id_private *id_priv;
	WV_CONNECT_PARAM attr;
	HRESULT hr;
	int ret;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	ret = ucma_valid_param(id_priv, conn_param);
	if (ret) {
		return ret;
	}

	if (!conn_param || conn_param->initiator_depth == RDMA_MAX_INIT_DEPTH) {
		id_priv->initiator_depth = min(id_priv->initiator_depth,
									   id_priv->cma_dev->max_initiator_depth);
	} else {
		id_priv->initiator_depth = conn_param->initiator_depth;
	}
	if (!conn_param || conn_param->responder_resources == RDMA_MAX_RESP_RES) {
		id_priv->responder_resources = min(id_priv->responder_resources,
										   id_priv->cma_dev->max_responder_resources);
	} else {
		id_priv->responder_resources = conn_param->responder_resources;
	}

	ucma_set_connect_attr(id_priv, conn_param, &attr);

	id_priv->state = cma_accepting;
	id_priv->refcnt++;
	id->comp_entry.Busy = 1;
	hr = id->ep.connect->Accept(id->qp->conn_handle, &attr,
								&id->comp_entry.Overlap);
	if (FAILED(hr) && hr != WV_IO_PENDING) {
		id_priv->refcnt--;
		id->comp_entry.Busy = 0;
		id_priv->state = cma_disconnected;
		return ibvw_wv_errno(hr);
	}

	return ucma_complete_priv(id_priv);
}

__declspec(dllexport)
int rdma_reject(struct rdma_cm_id *id, const void *private_data,
				uint8_t private_data_len)
{
	struct cma_id_private *id_priv;
	HRESULT hr;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	id_priv->state = cma_disconnected;
	hr = id->ep.connect->Reject(private_data, private_data_len);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}
	return 0;
}

__declspec(dllexport)
int rdma_notify(struct rdma_cm_id *id, enum ibv_event_type event)
{
	return 0;
}

__declspec(dllexport)
int rdma_disconnect(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;
	HRESULT hr;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	if (id_priv->state == cma_connected) {
		id_priv->state = cma_active_disconnect;
	} else {
		id_priv->state = cma_disconnected;
	}
	hr = id->ep.connect->Disconnect(NULL);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	return ucma_complete_priv(id_priv);
}

__declspec(dllexport)
int rdma_ack_cm_event(struct rdma_cm_event *event)
{
	struct cma_event *evt;
	struct cma_id_private *listen;

	evt = CONTAINING_RECORD(event, struct cma_event, event);
	InterlockedDecrement(&evt->id_priv->refcnt);
	if (evt->event.listen_id) {
		listen = CONTAINING_RECORD(evt->event.listen_id, struct cma_id_private, id);
		InterlockedDecrement(&listen->refcnt);
	}
	delete evt;
	return 0;
}

static int ucma_process_conn_req(struct cma_event *event)
{
	struct cma_id_private *listen, *id_priv;
	struct cma_event_channel *chan;

	listen = (struct cma_id_private *) event->id_priv->id.context;
	id_priv = event->id_priv;

	ucma_get_request(listen, id_priv->index);

	if (event->event.status) {
		goto err;
	}

	if (listen->sync) {
		event->event.status = rdma_migrate_id(&id_priv->id, NULL);
		if (event->event.status) {
			goto err;
		}
	}

	event->event.status = ucma_query_connect(&id_priv->id,
											 &event->event.param.conn);
	if (event->event.status) {
		goto err;
	}

	event->event.event = RDMA_CM_EVENT_CONNECT_REQUEST;
	id_priv->state = cma_passive_connect;
	event->event.listen_id = &listen->id;
	id_priv->initiator_depth = event->event.param.conn.initiator_depth;
	id_priv->responder_resources = event->event.param.conn.responder_resources;

	return 0;

err:
	InterlockedDecrement(&listen->refcnt);
	InterlockedDecrement(&id_priv->refcnt);
	rdma_destroy_id(&id_priv->id);
	return event->event.status;
}

static int ucma_process_conn_resp(struct cma_event *event)
{
	struct rdma_cm_id *id;
	WV_CONNECT_PARAM attr;
	HRESULT hr;

	if (event->event.status) {
		goto err;
	}

	RtlZeroMemory(&attr, sizeof(attr));
	event->id_priv->state = cma_accepting;

	id = &event->id_priv->id;
	id->comp_entry.Busy = 1;
	hr = id->ep.connect->Accept(id->qp->conn_handle, &attr,
								&id->comp_entry.Overlap);
	if (FAILED(hr) && hr != WV_IO_PENDING) {
		id->comp_entry.Busy = 0;
		event->event.status = hr;
		goto err;
	}

	return EINPROGRESS;

err:
	event->event.event = (event->event.status == WV_CONNECTION_REFUSED) ?
						 RDMA_CM_EVENT_REJECTED :
						 RDMA_CM_EVENT_CONNECT_ERROR;
	event->id_priv->state = cma_disconnected;
	return 0;
}

static void ucma_process_establish(struct cma_event *event)
{
	struct cma_id_private *id_priv = event->id_priv;

	if (!event->event.status) {
		event->event.status = ucma_query_connect(&id_priv->id,
												 &event->event.param.conn);
	}

	if (!event->event.status) {
		event->event.event = RDMA_CM_EVENT_ESTABLISHED;

		id_priv->state = cma_connected;
		InterlockedIncrement(&id_priv->refcnt);
		id_priv->id.comp_entry.Busy = 1;
		id_priv->id.ep.connect->NotifyDisconnect(&id_priv->id.comp_entry.Overlap);
	} else {
		event->event.event = (event->event.status == WV_CONNECTION_REFUSED) ?
							 RDMA_CM_EVENT_REJECTED :
							 RDMA_CM_EVENT_CONNECT_ERROR;
		event->id_priv->state = cma_disconnected;
	}
}

static int ucma_process_event(struct cma_event *event)
{
	struct cma_id_private *listen, *id_priv;
	WV_CONNECT_ATTRIBUTES attr;
	int ret = 0;

	id_priv = event->id_priv;

	fastlock_acquire(&lock);
	switch (id_priv->state) {
	case cma_get_request:
		listen = (struct cma_id_private *) id_priv->id.context;
		if (listen->state != cma_listening) {
			InterlockedDecrement(&id_priv->refcnt);
			ret = ECANCELED;
			break;
		}

		listen->req_list[id_priv->index] = NULL;
		fastlock_release(&lock);
		return ucma_process_conn_req(event);
	case cma_addr_resolve:
		event->event.event = RDMA_CM_EVENT_ADDR_RESOLVED;
		break;
	case cma_route_resolve:
		event->event.event = RDMA_CM_EVENT_ROUTE_RESOLVED;
		break;
	case cma_active_connect:
		ret = ucma_process_conn_resp(event);
		break;
	case cma_accepting:
		ucma_process_establish(event);
		break;
	case cma_connected:
		event->event.event = RDMA_CM_EVENT_DISCONNECTED;
		id_priv->state = cma_passive_disconnect;
		break;
	case cma_active_disconnect:
		event->event.event = RDMA_CM_EVENT_DISCONNECTED;
		id_priv->state = cma_disconnected;
		break;
	default:
		InterlockedDecrement(&id_priv->refcnt);
		ret = ECANCELED;
	}
	fastlock_release(&lock);

	return ret;
}

__declspec(dllexport)
int rdma_get_cm_event(struct rdma_event_channel *channel,
					  struct rdma_cm_event **event)
{
	struct cma_event *evt;
	struct rdma_cm_id *id;
	COMP_ENTRY *entry;
	DWORD bytes, ret;

	evt = new struct cma_event;
	if (evt == NULL) {
		return -1;
	}

	do {
		RtlZeroMemory(evt, sizeof(struct cma_event));

		ret = CompChannelPoll(&channel->channel, &entry);
		if (ret) {
			switch (ret) {
			case WAIT_TIMEOUT:
				ret = ERR(EWOULDBLOCK);
				break;
			case ERROR_CANCELLED:
				ret = ERR(ECANCELED);
				break;
			default:
				ret = ERR(ret); //???
			}
			delete evt;
			return ret;
		}
		
		id = CONTAINING_RECORD(entry, struct rdma_cm_id, comp_entry);
		evt->id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
		evt->event.id = id;
		evt->event.param.conn.private_data = evt->private_data;
		evt->event.param.conn.private_data_len = RDMA_MAX_PRIVATE_DATA;

		evt->event.status = id->ep.connect->
							GetOverlappedResult(&entry->Overlap, &bytes, FALSE);

		ret = ucma_process_event(evt);
	} while (ret);
	
	*event = &evt->event;
	return 0;
}


__declspec(dllexport)
int rdma_join_multicast(struct rdma_cm_id *id, struct sockaddr *addr,
						void *context)
{
	return rdma_seterrno(ENOSYS);
}

__declspec(dllexport)
int rdma_leave_multicast(struct rdma_cm_id *id, struct sockaddr *addr)
{
	return rdma_seterrno(ENOSYS);
}

__declspec(dllexport)
const char *rdma_event_str(enum rdma_cm_event_type event)
{
	switch (event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		return "RDMA_CM_EVENT_ADDR_RESOLVED";
	case RDMA_CM_EVENT_ADDR_ERROR:
		return "RDMA_CM_EVENT_ADDR_ERROR";
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		return "RDMA_CM_EVENT_ROUTE_RESOLVED";
	case RDMA_CM_EVENT_ROUTE_ERROR:
		return "RDMA_CM_EVENT_ROUTE_ERROR";
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		return "RDMA_CM_EVENT_CONNECT_REQUEST";
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
		return "RDMA_CM_EVENT_CONNECT_RESPONSE";
	case RDMA_CM_EVENT_CONNECT_ERROR:
		return "RDMA_CM_EVENT_CONNECT_ERROR";
	case RDMA_CM_EVENT_UNREACHABLE:
		return "RDMA_CM_EVENT_UNREACHABLE";
	case RDMA_CM_EVENT_REJECTED:
		return "RDMA_CM_EVENT_REJECTED";
	case RDMA_CM_EVENT_ESTABLISHED:
		return "RDMA_CM_EVENT_ESTABLISHED";
	case RDMA_CM_EVENT_DISCONNECTED:
		return "RDMA_CM_EVENT_DISCONNECTED";
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		return "RDMA_CM_EVENT_DEVICE_REMOVAL";
	case RDMA_CM_EVENT_MULTICAST_JOIN:
		return "RDMA_CM_EVENT_MULTICAST_JOIN";
	case RDMA_CM_EVENT_MULTICAST_ERROR:
		return "RDMA_CM_EVENT_MULTICAST_ERROR";
	case RDMA_CM_EVENT_ADDR_CHANGE:
		return "RDMA_CM_EVENT_ADDR_CHANGE";
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		return "RDMA_CM_EVENT_TIMEWAIT_EXIT";
	default:
		return "UNKNOWN EVENT";
	}
}

__declspec(dllexport)
int rdma_set_option(struct rdma_cm_id *id, int level, int optname,
					void *optval, size_t optlen)
{
	return rdma_seterrno(ENOSYS);
}

__declspec(dllexport)
int rdma_migrate_id(struct rdma_cm_id *id, struct rdma_event_channel *channel)
{
	struct cma_id_private *id_priv;
	int sync;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	if (id_priv->sync && !channel) {
		return rdma_seterrno(EINVAL);
	}

	if (id->comp_entry.Busy) {
		return rdma_seterrno(EBUSY);
	}

	if ((sync = (channel == NULL))) {
		channel = rdma_create_event_channel();
		if (!channel) {
			return rdma_seterrno(ENOMEM);
		}
	}

	if (id_priv->sync) {
		if (id->event) {
			rdma_ack_cm_event(id->event);
			id->event = NULL;
		}
		rdma_destroy_event_channel(id->channel);
	}

	id_priv->sync = sync;
	id->channel = channel;
	id->comp_entry.Channel = &channel->channel;
	return 0;
}

static int ucma_passive_ep(struct rdma_cm_id *id, struct rdma_addrinfo *res,
						   struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
	struct cma_id_private *id_priv;
	int ret;

	ret = rdma_bind_addr(id, res->ai_src_addr);
	if (ret)
		return ret;

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	id_priv->pd = pd;

	if (qp_init_attr) {
		id_priv->qp_init_attr = new struct ibv_qp_init_attr;
		if (!id_priv->qp_init_attr)
			return rdma_seterrno(ENOMEM);

		*id_priv->qp_init_attr = *qp_init_attr;
		id_priv->qp_init_attr->qp_type = (enum ibv_qp_type) res->ai_qp_type;
	}

	return 0;
}

__declspec(dllexport)
int rdma_create_ep(struct rdma_cm_id **id, struct rdma_addrinfo *res,
				   struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
	struct rdma_cm_id *cm_id;
	int ret;

	ret = rdma_create_id(NULL, &cm_id, NULL, (enum rdma_port_space) res->ai_port_space);
	if (ret)
		return ret;

	if (res->ai_flags & RAI_PASSIVE) {
		ret = ucma_passive_ep(cm_id, res, pd, qp_init_attr);
		if (ret)
			goto err;
		goto out;
	}

	ret = rdma_resolve_addr(cm_id, res->ai_src_addr, res->ai_dst_addr, 2000);
	if (ret)
		goto err;

	ret = rdma_resolve_route(cm_id, 2000);
	if (ret)
		goto err;

	qp_init_attr->qp_type = (enum ibv_qp_type) res->ai_qp_type;
	ret = rdma_create_qp(cm_id, pd, qp_init_attr);
	if (ret)
		goto err;

out:
	*id = cm_id;
	return 0;

err:
	rdma_destroy_ep(cm_id);
	return ret;
}

__declspec(dllexport)
void rdma_destroy_ep(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;

	if (id->qp)
		rdma_destroy_qp(id);

	id_priv = CONTAINING_RECORD(id, struct cma_id_private, id);
	if (id_priv->qp_init_attr) {
		delete id_priv->qp_init_attr;
	}
	rdma_destroy_id(id);
}

__declspec(dllexport)
int rdmaw_wsa_errno(int wsa_err)
{
	switch (wsa_err) {
	case 0:					return 0;
	case WSAEWOULDBLOCK:	_set_errno(EWOULDBLOCK); break;
	case WSAEINPROGRESS:	_set_errno(EINPROGRESS); break;
	case WSAEALREADY:		_set_errno(EALREADY); break;
	case WSAENOTSOCK:		_set_errno(ENOTSOCK); break;
	case WSAEDESTADDRREQ:	_set_errno(EDESTADDRREQ); break;
	case WSAEMSGSIZE:		_set_errno(EMSGSIZE); break;
	case WSAEPROTOTYPE:		_set_errno(EPROTOTYPE); break;
	case WSAENOPROTOOPT:	_set_errno(ENOPROTOOPT); break;
	case WSAEPROTONOSUPPORT:_set_errno(EPROTONOSUPPORT); break;
	case WSAEOPNOTSUPP:		_set_errno(EOPNOTSUPP); break;
	case WSAEAFNOSUPPORT:	_set_errno(EAFNOSUPPORT); break;
	case WSAEADDRINUSE:		_set_errno(EADDRINUSE); break;
	case WSAEADDRNOTAVAIL:	_set_errno(EADDRNOTAVAIL); break;
	case WSAENETDOWN:		_set_errno(ENETDOWN); break;
	case WSAENETUNREACH:	_set_errno(ENETUNREACH); break;
	case WSAENETRESET:		_set_errno(ENETRESET); break;
	case WSAECONNABORTED:	_set_errno(ECONNABORTED); break;
	case WSAECONNRESET:		_set_errno(ECONNRESET); break;
	case WSAENOBUFS:		_set_errno(ENOBUFS); break;
	case WSAEISCONN:		_set_errno(EISCONN); break;
	case WSAENOTCONN:		_set_errno(ENOTCONN); break;
	case WSAETIMEDOUT:		_set_errno(ETIMEDOUT); break;
	case WSAECONNREFUSED:	_set_errno(ECONNREFUSED); break;
	case WSAELOOP:			_set_errno(ELOOP); break;
	case WSAENAMETOOLONG:	_set_errno(ENAMETOOLONG); break;
	case WSAEHOSTUNREACH:	_set_errno(EHOSTUNREACH); break;
	case WSAENOTEMPTY:		_set_errno(ENOTEMPTY); break;
	}
	return -1;
}

int ucma_max_qpsize(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;

	id_priv = container_of(id, struct cma_id_private, id);
	return id_priv->cma_dev->max_qpsize;
}
