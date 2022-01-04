/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2008-2009 Intel Corporation.  All rights reserved.
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

#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <netinet/in.h>

#include <infiniband/verbs.h>
#include <comp_channel.h>
#include "ibverbs.h"


__declspec(dllexport)
int ibv_rate_to_mult(enum ibv_rate rate)
{
	switch (rate) {
	case IBV_RATE_2_5_GBPS: return  1;
	case IBV_RATE_5_GBPS:   return  2;
	case IBV_RATE_10_GBPS:  return  4;
	case IBV_RATE_20_GBPS:  return  8;
	case IBV_RATE_30_GBPS:  return 12;
	case IBV_RATE_40_GBPS:  return 16;
	case IBV_RATE_60_GBPS:  return 24;
	case IBV_RATE_80_GBPS:  return 32;
	case IBV_RATE_120_GBPS: return 48;
	default:				return -1;
	}
}

__declspec(dllexport)
enum ibv_rate mult_to_ibv_rate(int mult)
{
	switch (mult) {
	case 1:  return IBV_RATE_2_5_GBPS;
	case 2:  return IBV_RATE_5_GBPS;
	case 4:  return IBV_RATE_10_GBPS;
	case 8:  return IBV_RATE_20_GBPS;
	case 12: return IBV_RATE_30_GBPS;
	case 16: return IBV_RATE_40_GBPS;
	case 24: return IBV_RATE_60_GBPS;
	case 32: return IBV_RATE_80_GBPS;
	case 48: return IBV_RATE_120_GBPS;
	default: return IBV_RATE_MAX;
	}
}

static int ibv_find_gid_index(struct ibv_context *context, uint8_t port_num,
							  union ibv_gid *gid)
{
	union ibv_gid sgid;
	int i = 0, ret;

	do {
		ret = ibv_query_gid(context, port_num, i++, &sgid);
	} while (!ret && memcmp(&sgid, gid, sizeof *gid));

	return ret ? -1 : i - 1;
}

static void ibv_convert_ah_attr(struct ibv_context *context,
								WV_ADDRESS_VECTOR *av, struct ibv_ah_attr *attr)
{
	WV_GID gid;

	av->Route.Valid = attr->is_global;
	if (av->Route.Valid) {
		context->cmd_if->QueryGid(attr->port_num, attr->grh.sgid_index, &gid);

		memcpy(&av->Route.DGid, &attr->grh.dgid, sizeof(av->Route.DGid));
		memcpy(&av->Route.SGid, &gid, sizeof(av->Route.SGid));
		av->Route.TrafficClass = attr->grh.traffic_class;
		av->Route.FlowLabel =  htonl(attr->grh.flow_label);
		av->Route.HopLimit = attr->grh.hop_limit;
	}

	av->DLid = htons(attr->dlid);
	av->ServiceLevel = attr->sl;
	av->SourcePathBits = attr->src_path_bits;
	av->StaticRate = attr->static_rate;
	av->PortNumber = attr->port_num;
}

static void ibv_convert_av(struct ibv_context *context,
						   struct ibv_ah_attr *attr, WV_ADDRESS_VECTOR *av)
{
	WV_GID gid;

	attr->is_global = av->Route.Valid;
	if (attr->is_global) {
		memcpy(&attr->grh.dgid, &av->Route.DGid, sizeof(attr->grh.dgid));
		attr->grh.flow_label = ntohl(av->Route.FlowLabel);
		attr->grh.traffic_class = av->Route.TrafficClass;
		attr->grh.hop_limit = av->Route.HopLimit;
		attr->grh.sgid_index = (uint8_t) ibv_find_gid_index(context, av->PortNumber,
															(ibv_gid *) &av->Route.SGid);
	}

	attr->dlid = ntohs(av->DLid);
	attr->sl = av->ServiceLevel;
	attr->src_path_bits	= av->SourcePathBits;
	attr->static_rate = av->StaticRate;
	attr->port_num = av->PortNumber;
}

__declspec(dllexport)
int ibv_query_device(struct ibv_context *context,
					 struct ibv_device_attr *device_attr)
{
	WV_DEVICE_ATTRIBUTES attr;
	HRESULT hr;

	hr = context->cmd_if->Query(&attr);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	sprintf(device_attr->fw_ver, "0x%I64x", attr.FwVersion);
	device_attr->node_guid = attr.NodeGuid;
	device_attr->sys_image_guid = attr.SystemImageGuid;
	device_attr->max_mr_size = attr.MaxMrSize;
	device_attr->page_size_cap = attr.PageSizeCapabilityFlags;
	device_attr->vendor_id = attr.VendorId;
	device_attr->vendor_part_id = attr.VendorPartId;
	device_attr->hw_ver = attr.HwVersion;
	device_attr->max_qp = (int) attr.MaxQp;
	device_attr->max_qp_wr = (int) attr.MaxQpWr;
	device_attr->device_cap_flags = (int) attr.CapabilityFlags;
	device_attr->max_sge = (int) attr.MaxSge;
	device_attr->max_sge_rd = 0;
	device_attr->max_cq = (int) attr.MaxCq;
	device_attr->max_cqe = (int) attr.MaxCqEntries;
	device_attr->max_mr = (int) attr.MaxMr;
	device_attr->max_pd = (int) attr.MaxPd;
	device_attr->max_qp_rd_atom = (int) attr.MaxQpResponderResources;;
	device_attr->max_ee_rd_atom = 0;
	device_attr->max_res_rd_atom = (int) attr.MaxResponderResources;
	device_attr->max_qp_init_rd_atom = (int) attr.MaxQpInitiatorDepth;
	device_attr->max_ee_init_rd_atom = 0;
	device_attr->atomic_cap = (enum ibv_atomic_cap) attr.AtomicCapability;
	device_attr->max_ee = 0;
	device_attr->max_rdd = 0;
	device_attr->max_mw = (int) attr.MaxMw;
	device_attr->max_raw_ipv6_qp = 0;
	device_attr->max_raw_ethy_qp = 0;
	device_attr->max_mcast_grp = (int) attr.MaxMulticast;
	device_attr->max_mcast_qp_attach = (int) attr.MaxQpAttach;
	device_attr->max_total_mcast_qp_attach = (int) attr.MaxMulticastQp;
	device_attr->max_ah = (int) attr.MaxAh;
	device_attr->max_fmr = (int) attr.MaxFmr;
	device_attr->max_map_per_fmr = (int) attr.MaxMapPerFmr;
	device_attr->max_srq = (int) attr.MaxSrq;
	device_attr->max_srq_wr = (int) attr.MaxSrqWr;
	device_attr->max_srq_sge = (int) attr.MaxSrqSge;
	device_attr->max_pkeys = (uint16_t) attr.MaxPkeys;
	device_attr->local_ca_ack_delay = attr.LocalAckDelay;
	device_attr->phys_port_cnt = attr.PhysPortCount;

	return 0;
}

static enum ibv_mtu ibv_convert_mtu(UINT32 mtu)
{
	switch (mtu) {
	case 256:	return IBV_MTU_256;
	case 512:	return IBV_MTU_512;
	case 1024:	return IBV_MTU_1024;
	case 2048:	return IBV_MTU_2048;
	case 4096:	return IBV_MTU_4096;
	default:	return (ibv_mtu) mtu;
	}
}

__declspec(dllexport)
int ibv_query_port(struct ibv_context *context, uint8_t port_num,
				   struct ibv_port_attr *port_attr)
{
	WV_PORT_ATTRIBUTES attr;
	HRESULT hr;
	
	hr = context->cmd_if->QueryPort(port_num, &attr);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	port_attr->state = (enum ibv_port_state) attr.State;
	port_attr->max_mtu = ibv_convert_mtu(attr.MaxMtu);
	port_attr->active_mtu = ibv_convert_mtu(attr.ActiveMtu);
	port_attr->gid_tbl_len = attr.GidTableLength;
	port_attr->port_cap_flags = attr.PortCabilityFlags;
	port_attr->max_msg_sz = attr.MaxMessageSize;
	port_attr->bad_pkey_cntr = attr.BadPkeyCounter;
	port_attr->qkey_viol_cntr = attr.QkeyViolationCounter;
	port_attr->pkey_tbl_len = attr.PkeyTableLength;
	port_attr->lid = ntohs(attr.Lid);
	port_attr->sm_lid = ntohs(attr.SmLid);
	port_attr->lmc = attr.Lmc;
	port_attr->max_vl_num = attr.MaxVls;
	port_attr->sm_sl = attr.SmSl;
	port_attr->subnet_timeout = attr.SubnetTimeout;
	port_attr->init_type_reply = attr.InitTypeReply;
	port_attr->active_width = attr.ActiveWidth;
	port_attr->active_speed = attr.ActiveSpeed;
	port_attr->ext_active_speed = attr.ExtActiveSpeed;
	port_attr->link_encoding = attr.LinkEncoding;
	port_attr->phys_state = attr.PhysicalState;
	port_attr->transport = attr.Transport;

	return 0;
}

__declspec(dllexport)
int ibv_query_gid(struct ibv_context *context, uint8_t port_num,
				  int index, union ibv_gid *gid)
{
	return ibvw_wv_errno(context->cmd_if->QueryGid(port_num, index, (WV_GID *) gid));
}

__declspec(dllexport)
int ibv_query_pkey(struct ibv_context *context, uint8_t port_num,
				   int index, uint16_t *pkey)
{
	return ibvw_wv_errno(context->cmd_if->QueryPkey(port_num, (UINT16) index, pkey));
}

__declspec(dllexport)
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context)
{
	struct ibv_pd *pd;
	HRESULT hr;

	pd = new struct ibv_pd;
	if (pd == NULL) {
		return NULL;
	}

	pd->context = context;
	hr = context->cmd_if->AllocateProtectionDomain(&pd->handle);
	if (FAILED(hr)) {
		delete pd;
		return NULL;
	}
	return pd;
}

__declspec(dllexport)
int ibv_dealloc_pd(struct ibv_pd *pd)
{
	pd->handle->Release();
	delete pd;
	return 0;
}

__declspec(dllexport)
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr,
						  size_t length, int access)
{
	struct ibv_mr *mr;
	HRESULT hr;

	mr = new struct ibv_mr;
	if (mr == NULL) {
		return NULL;
	}

	mr->context = pd->context;
	mr->pd = pd;
	mr->addr = addr;
	mr->length = length;
	hr = pd->handle->RegisterMemory(addr, length, access, NULL,
									(WV_MEMORY_KEYS *) &mr->lkey);
	if (FAILED(hr)) {
		delete mr;
		return NULL;
	}
	mr->rkey = ntohl(mr->rkey);
	return mr;
}

__declspec(dllexport)
int ibv_dereg_mr(struct ibv_mr *mr)
{
	HRESULT hr;

	hr = mr->pd->handle->DeregisterMemory(mr->lkey, NULL);
	if (SUCCEEDED(hr)) {
		delete mr;
	}
	return ibvw_wv_errno(hr);
}

__declspec(dllexport)
struct ibv_comp_channel *ibv_create_comp_channel(struct ibv_context *context)
{
	struct ibv_comp_channel *channel;

	channel = new struct ibv_comp_channel;
	if (channel == NULL) {
		return NULL;
	}

	CompChannelInit(&comp_mgr, &channel->comp_channel, INFINITE);
	channel->context = context;
	return channel;
}

__declspec(dllexport)
int ibv_destroy_comp_channel(struct ibv_comp_channel *channel)
{
	CompChannelCleanup(&channel->comp_channel);
	delete channel;
	return 0;
}

__declspec(dllexport)
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe, void *cq_context,
							 struct ibv_comp_channel *channel, int comp_vector)
{
	struct ibv_cq *cq;
	HRESULT hr;
	SIZE_T entries;

	cq = new struct ibv_cq;
	if (cq == NULL) {
		return NULL;
	}

	cq->context = context;
	cq->channel = channel;
	cq->cq_context = cq_context;
	cq->notify_cnt = 0;
	cq->ack_cnt = 0;

	entries = cqe;
	hr = context->cmd_if->CreateCompletionQueue(&entries, &cq->handle);
	if (FAILED(hr)) {
		goto err;
	}

	if (channel != NULL) {
		CompEntryInit(&channel->comp_channel, &cq->comp_entry);
	} else {
		memset(&cq->comp_entry, 0, sizeof cq->comp_entry);
	}

	cq->cqe = (uint32_t) entries;
	return cq;

err:
	delete cq;
	return NULL;
}

__declspec(dllexport)
int ibv_resize_cq(struct ibv_cq *cq, int cqe)
{
	HRESULT hr;
	SIZE_T entries = cqe;

	hr = cq->handle->Resize(&entries);
	if (SUCCEEDED(hr)) {
		cq->cqe = (int) entries;
	}
	return ibvw_wv_errno(hr);
}

__declspec(dllexport)
int ibv_req_notify_cq(struct ibv_cq *cq, int solicited_only)
{
	HRESULT hr;

	if (InterlockedCompareExchange(&cq->comp_entry.Busy, 1, 0) == 0) {
		InterlockedIncrement(&cq->notify_cnt);
		hr = cq->handle->Notify(solicited_only ? WvCqSolicited : WvCqNextCompletion,
								&cq->comp_entry.Overlap);
		if (SUCCEEDED(hr) || hr == WV_IO_PENDING) {
			hr = 0;
		} else {
			InterlockedExchange(&cq->comp_entry.Busy, 0);
			InterlockedDecrement(&cq->notify_cnt);
		}
	} else {
		hr = 0;
	}
	return ibvw_wv_errno(hr);
}

__declspec(dllexport)
int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
	int n;

	num_entries = (int) cq->handle->Poll((WV_COMPLETION *) wc, num_entries);
	for (n = 0; n < num_entries; n++) {
		wc[n].qp_num = ((ibv_qp *) wc[n].reserved)->qp_num;
		wc[n].src_qp = ntohl(wc[n].src_qp);
		wc[n].slid = ntohs(wc[n].slid);
	}
	return num_entries;
}

__declspec(dllexport)
int ibv_destroy_cq(struct ibv_cq *cq)
{
	cq->handle->CancelOverlappedRequests();
	if (cq->channel != NULL) {
		if (CompEntryCancel(&cq->comp_entry)) {
			InterlockedIncrement(&cq->ack_cnt);
		}
	}

	while (cq->ack_cnt < cq->notify_cnt)
		Sleep(0);
	cq->handle->Release();
	delete cq;
	return 0;
}

__declspec(dllexport)
int ibv_get_cq_event(struct ibv_comp_channel *channel,
					 struct ibv_cq **cq, void **cq_context)
{
	COMP_ENTRY *entry;
	DWORD ret;

	ret = CompChannelPoll(&channel->comp_channel, &entry);
	if (!ret) {
		*cq = CONTAINING_RECORD(entry, struct ibv_cq, comp_entry);
		*cq_context = (*cq)->cq_context;
	}

	return ret;
}

__declspec(dllexport)
void ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
	InterlockedExchangeAdd(&cq->ack_cnt, (LONG) nevents);
}

__declspec(dllexport)
struct ibv_srq *ibv_create_srq(struct ibv_pd *pd,
							   struct ibv_srq_init_attr *srq_init_attr)
{
	struct ibv_srq *srq;
	HRESULT hr;

	srq = new struct ibv_srq;
	if (srq == NULL) {
		return NULL;
	}

	srq->context = pd->context;
	srq->srq_context = srq_init_attr->srq_context;
	srq->pd = pd;

	hr = pd->handle->CreateSharedReceiveQueue(srq_init_attr->attr.max_wr,
											  srq_init_attr->attr.max_sge,
											  srq_init_attr->attr.srq_limit,
											  &srq->handle);
	if (FAILED(hr)) {
		delete srq;
		return NULL;
	}

	return srq;
}

__declspec(dllexport)
int ibv_modify_srq(struct ibv_srq *srq,
				   struct ibv_srq_attr *srq_attr,
				   int srq_attr_mask)
{
	ibv_srq_attr attr;

	ibv_query_srq(srq, &attr);
	if (srq_attr_mask & IBV_SRQ_MAX_WR) {
		attr.max_wr = srq_attr->max_wr;
	}
	if (srq_attr_mask & IBV_SRQ_LIMIT) {
		attr.srq_limit = srq_attr->srq_limit;
	}

	return ibvw_wv_errno(srq->handle->Modify(attr.max_wr, attr.srq_limit));
}

__declspec(dllexport)
int ibv_post_srq_recv(struct ibv_srq *srq,
					  struct ibv_recv_wr *recv_wr,
					  struct ibv_recv_wr **bad_recv_wr)
{
	HRESULT hr = 0;

	for (*bad_recv_wr = recv_wr; *bad_recv_wr != NULL && SUCCEEDED(hr);
		 *bad_recv_wr = (*bad_recv_wr)->next) {
		hr = srq->handle->PostReceive((*bad_recv_wr)->wr_id,
									  (WV_SGE *) (*bad_recv_wr)->sg_list,
									  (*bad_recv_wr)->num_sge);
	}
	return ibvw_wv_errno(hr);
}

__declspec(dllexport)
int ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
	SIZE_T max_wr, max_sge, srq_limit;
	HRESULT hr;

	hr = srq->handle->Query(&max_wr, &max_sge, &srq_limit);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	srq_attr->max_wr = (uint32_t) max_wr;
	srq_attr->max_sge = (uint32_t) max_sge;
	srq_attr->srq_limit = (uint32_t) srq_limit;
	return 0;
}

__declspec(dllexport)
int ibv_destroy_srq(struct ibv_srq *srq)
{
	srq->handle->Release();
	delete srq;
	return 0;
}

__declspec(dllexport)
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd,
							 struct ibv_qp_init_attr *qp_init_attr)
{
	WV_QP_CREATE create;
	ibv_qp_attr attr;
	struct ibv_qp *qp;
	HRESULT hr;

	qp = new struct ibv_qp;
	if (qp == NULL) {
		return NULL;
	}

	create.pSendCq = qp_init_attr->send_cq->handle;
	create.pReceiveCq = qp_init_attr->recv_cq->handle;
	create.pSharedReceiveQueue = (qp_init_attr->srq != NULL) ?
								 qp_init_attr->srq->handle : NULL;
	create.Context = qp;
	create.SendDepth = qp_init_attr->cap.max_send_wr;
	create.SendSge = qp_init_attr->cap.max_send_sge;
	create.ReceiveDepth = qp_init_attr->cap.max_recv_wr;
	create.ReceiveSge = qp_init_attr->cap.max_recv_sge;
	create.MaxInlineSend = qp_init_attr->cap.max_inline_data;
	create.InitiatorDepth = 0;
	create.ResponderResources = 0;
	create.QpType = (WV_QP_TYPE) qp_init_attr->qp_type;
	create.QpFlags = qp_init_attr->sq_sig_all ? WV_QP_SIGNAL_SENDS : 0;

	if (qp_init_attr->qp_type == IBV_QPT_UD) {
		hr = pd->handle->CreateDatagramQueuePair(&create, &qp->ud_handle);
	} else {
		hr = pd->handle->CreateConnectQueuePair(&create, &qp->conn_handle);
	}
	if (FAILED(hr)) {
		goto err;
	}

	if (qp_init_attr->qp_type == IBV_QPT_UD) {
		qp->ud_handle->QueryInterface(IID_IWVQueuePair, (LPVOID *) &qp->handle);
	} else {
		qp->conn_handle->QueryInterface(IID_IWVQueuePair, (LPVOID *) &qp->handle);
	}

	qp->context = pd->context;
	qp->qp_context = qp_init_attr->qp_context;
	qp->pd = pd;
	qp->send_cq = qp_init_attr->send_cq;
	qp->recv_cq = qp_init_attr->recv_cq;
	qp->srq = qp_init_attr->srq;
	qp->state = IBV_QPS_RESET;
	/* qp_num set by ibv_query_qp */
	qp->qp_type = qp_init_attr->qp_type;

	hr = ibv_query_qp(qp, &attr, 0xFFFFFFFF, qp_init_attr);
	if (FAILED(hr)) {
		goto err;
	}

	return qp;

err:
	delete qp;
	return NULL;
}

__declspec(dllexport)
int ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
				 int attr_mask,
				 struct ibv_qp_init_attr *init_attr)
{
	WV_QP_ATTRIBUTES wv_attr;
	HRESULT hr;

	hr = qp->handle->Query(&wv_attr);
	if (FAILED(hr)) {
		return ibvw_wv_errno(hr);
	}

	/* ibv_qp exposes qp_num.  Save qp_num from query. */
	qp->qp_num = ntohl(wv_attr.Qpn);

	init_attr->qp_context = qp->context;
	init_attr->send_cq = qp->send_cq;
	init_attr->recv_cq = qp->recv_cq;
	init_attr->srq = qp->srq;
	init_attr->cap.max_send_wr = (uint32_t) wv_attr.SendDepth;
	init_attr->cap.max_recv_wr = (uint32_t) wv_attr.ReceiveDepth;
	init_attr->cap.max_send_sge = (uint32_t) wv_attr.SendSge;
	init_attr->cap.max_recv_sge = (uint32_t) wv_attr.ReceiveSge;
	init_attr->cap.max_inline_data = (uint32_t) wv_attr.MaxInlineSend;
	init_attr->qp_type = (enum ibv_qp_type) wv_attr.QpType;
	init_attr->sq_sig_all = wv_attr.QpFlags & WV_QP_SIGNAL_SENDS;

	attr->qp_state = (enum ibv_qp_state) wv_attr.QpState;
	attr->cur_qp_state = (enum ibv_qp_state) wv_attr.CurrentQpState;
	attr->path_mtu = ibv_convert_mtu(wv_attr.PathMtu);
	attr->path_mig_state = (enum ibv_mig_state) wv_attr.ApmState;
	attr->qkey = ntohl(wv_attr.Qkey);
	attr->rq_psn = ntohl(wv_attr.ReceivePsn);
	attr->sq_psn = ntohl(wv_attr.SendPsn);
	attr->dest_qp_num = ntohl(wv_attr.DestinationQpn);
	attr->qp_access_flags = wv_attr.AccessFlags;
	attr->cap = init_attr->cap;
	ibv_convert_av(qp->context, &attr->ah_attr, &wv_attr.AddressVector);
	if (wv_attr.AlternateAddressVector.DLid != 0) {
		ibv_convert_av(qp->context, &attr->alt_ah_attr, &wv_attr.AlternateAddressVector);
	}
	attr->pkey_index = wv_attr.PkeyIndex;
	attr->alt_pkey_index = wv_attr.AlternatePkeyIndex;
	attr->en_sqd_async_notify = 0;
	attr->sq_draining = 0;
	attr->max_rd_atomic = (uint8_t) wv_attr.InitiatorDepth;
	attr->max_dest_rd_atomic = (uint8_t) wv_attr.ResponderResources;
	attr->min_rnr_timer = wv_attr.RnrNakTimeout;
	attr->port_num = wv_attr.AddressVector.PortNumber;
	attr->timeout = wv_attr.LocalAckTimeout;
	attr->retry_cnt = wv_attr.SequenceErrorRetryCount;
	attr->rnr_retry = wv_attr.RnrRetryCount;
	attr->alt_port_num = wv_attr.AlternateAddressVector.PortNumber;
	attr->alt_timeout = wv_attr.AlternateLocalAckTimeout;

	return 0;
}

__declspec(dllexport)
int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
				  int attr_mask)
{
	WV_QP_ATTRIBUTES wv_attr;
	HRESULT hr;

	wv_attr.SendDepth = attr->cap.max_send_wr;
	wv_attr.SendSge = attr->cap.max_send_sge;
	wv_attr.ReceiveDepth = attr->cap.max_recv_wr;
	wv_attr.ReceiveSge = attr->cap.max_recv_sge;
	wv_attr.MaxInlineSend = attr->cap.max_inline_data;
	wv_attr.InitiatorDepth = attr->max_rd_atomic;
	wv_attr.ResponderResources = attr->max_dest_rd_atomic;

	wv_attr.CurrentQpState = (WV_QP_STATE) attr->cur_qp_state;
	wv_attr.QpState = (WV_QP_STATE) attr->qp_state;
	wv_attr.ApmState = (WV_APM_STATE) attr->path_mig_state;
	wv_attr.DestinationQpn = htonl(attr->dest_qp_num);
	wv_attr.Qkey = htonl(attr->qkey);
	wv_attr.SendPsn = htonl(attr->sq_psn);
	wv_attr.ReceivePsn = htonl(attr->rq_psn);

	wv_attr.AccessFlags = attr->qp_access_flags;
	wv_attr.QpFlags = 0;

	ibv_convert_ah_attr(qp->context, &wv_attr.AddressVector, &attr->ah_attr);
	wv_attr.AddressVector.PortNumber = attr->port_num;
	wv_attr.PathMtu = 0x80 << attr->path_mtu;
	wv_attr.PkeyIndex = attr->pkey_index;
	wv_attr.LocalAckTimeout = attr->timeout;

	if (attr_mask & IBV_QP_ALT_PATH) {
		ibv_convert_ah_attr(qp->context, &wv_attr.AlternateAddressVector,
							&attr->alt_ah_attr);
		wv_attr.AlternateAddressVector.PortNumber = attr->alt_port_num;
		wv_attr.AlternatePathMtu = 0x80 << attr->path_mtu;
		wv_attr.AlternatePkeyIndex = attr->alt_pkey_index;
		wv_attr.AlternateLocalAckTimeout = attr->alt_timeout;
	}

	wv_attr.RnrNakTimeout = attr->min_rnr_timer;
	wv_attr.SequenceErrorRetryCount = attr->retry_cnt;
	wv_attr.RnrRetryCount = attr->rnr_retry;

	hr = qp->handle->Modify(&wv_attr, attr_mask, NULL);
	if (SUCCEEDED(hr) && (attr_mask & IBV_QP_STATE)) {
		qp->state = attr->qp_state;
	}

	return ibvw_wv_errno(hr);
}

__declspec(dllexport)
int ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
				  struct ibv_send_wr **bad_wr)
{
	struct ibv_send_wr *cur_wr;
	HRESULT hr = 0;
	struct ibv_ah *ah = NULL;

	if ((qp->qp_type == IBV_QPT_UD) && (wr->next != NULL))
		return ibvw_wv_errno(WV_NOT_SUPPORTED);

	for (cur_wr = wr; cur_wr != NULL; cur_wr = cur_wr->next) {
		if (qp->qp_type == IBV_QPT_UD) {
			ah = cur_wr->wr.ud.ah;
			cur_wr->wr.ud.ah = (struct ibv_ah *) ah->key;
			cur_wr->wr.ud.remote_qkey = htonl(cur_wr->wr.ud.remote_qkey);
			cur_wr->wr.ud.remote_qpn = htonl(cur_wr->wr.ud.remote_qpn);
		}

		if ((cur_wr->opcode & 0x80000000) != 0) {
			cur_wr->opcode = (ibv_wr_opcode) (cur_wr->opcode & ~0x80000000);
			cur_wr->send_flags = (ibv_send_flags) (cur_wr->send_flags | WV_SEND_IMMEDIATE);
		}

		if (cur_wr->opcode != 0) {
			cur_wr->wr.rdma.remote_addr = htonll(cur_wr->wr.rdma.remote_addr);
			cur_wr->wr.rdma.rkey = htonl(cur_wr->wr.rdma.rkey);
		}
	}

	hr = qp->handle->PostSend((WV_SEND_REQUEST *) wr, (WV_SEND_REQUEST **) bad_wr);

	for (cur_wr = wr; cur_wr != NULL; cur_wr = cur_wr->next) {
		if (cur_wr->opcode != 0) {
			cur_wr->wr.rdma.rkey = ntohl(cur_wr->wr.rdma.rkey);
			cur_wr->wr.rdma.remote_addr = htonll(cur_wr->wr.rdma.remote_addr);
		}

		if ((cur_wr->send_flags & WV_SEND_IMMEDIATE) != 0) {
			cur_wr->send_flags = (ibv_send_flags) (cur_wr->send_flags & ~WV_SEND_IMMEDIATE);
			cur_wr->opcode = (ibv_wr_opcode) (cur_wr->opcode | 0x80000000);
		}

		if (qp->qp_type == IBV_QPT_UD) {
			cur_wr->wr.ud.ah = ah;
			cur_wr->wr.ud.remote_qkey = ntohl(cur_wr->wr.ud.remote_qkey);
			cur_wr->wr.ud.remote_qpn = ntohl(cur_wr->wr.ud.remote_qpn);
		}
	}

	return ibvw_wv_errno(hr);
}

__declspec(dllexport)
int ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
				  struct ibv_recv_wr **bad_wr)
{
	HRESULT hr = 0;

	for (*bad_wr = wr; *bad_wr != NULL && SUCCEEDED(hr); *bad_wr = (*bad_wr)->next) {
		hr = qp->handle->PostReceive((*bad_wr)->wr_id, (WV_SGE *) (*bad_wr)->sg_list,
									 (*bad_wr)->num_sge);
	}
	return ibvw_wv_errno(hr);
}

__declspec(dllexport)
int ibv_destroy_qp(struct ibv_qp *qp)
{
	qp->handle->CancelOverlappedRequests();
	if (qp->qp_type == IBV_QPT_UD) {
		qp->ud_handle->Release();
	} else {
		qp->conn_handle->Release();
	}

	qp->handle->Release();
	delete qp;
	return 0;
}

__declspec(dllexport)
struct ibv_ah *ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
	WV_ADDRESS_VECTOR av;
	struct ibv_ah *ah;
	HRESULT hr;

	ah = new struct ibv_ah;
	if (ah == NULL) {
		return NULL;
	}

	ah->context = pd->context;
	ah->pd = pd;
	ibv_convert_ah_attr(pd->context, &av, attr);
	hr = pd->handle->CreateAddressHandle(&av, &ah->handle, &ah->key);
	if (FAILED(hr)) {
		delete ah;
		return NULL;
	}
	return ah;
}

__declspec(dllexport)
int ibv_init_ah_from_wc(struct ibv_context *context, uint8_t port_num,
						struct ibv_wc *wc, struct ibv_grh *grh,
						struct ibv_ah_attr *ah_attr)
{
	uint32_t flow_class;
	int ret;

	memset(ah_attr, 0, sizeof *ah_attr);
	ah_attr->dlid = wc->slid;
	ah_attr->sl = wc->sl;
	ah_attr->src_path_bits = wc->dlid_path_bits;
	ah_attr->port_num = port_num;

	if (wc->wc_flags & IBV_WC_GRH) {
		ah_attr->is_global = 1;
		ah_attr->grh.dgid = grh->sgid;

		ret = ibv_find_gid_index(context, port_num, &grh->dgid);
		if (ret < 0) {
			return ret;
		}

		ah_attr->grh.sgid_index = (uint8_t) ret;
		flow_class = ntohl(grh->version_tclass_flow);
		ah_attr->grh.flow_label = flow_class & 0xFFFFF;
		ah_attr->grh.hop_limit = grh->hop_limit;
		ah_attr->grh.traffic_class = (flow_class >> 20) & 0xFF;
	}
	return 0;
}

__declspec(dllexport)
struct ibv_ah *ibv_create_ah_from_wc(struct ibv_pd *pd, struct ibv_wc *wc,
									 struct ibv_grh *grh, uint8_t port_num)
{
	struct ibv_ah_attr ah_attr;
	int ret;

	ret = ibv_init_ah_from_wc(pd->context, port_num, wc, grh, &ah_attr);
	if (ret != 0) {
		return NULL;
	}

	return ibv_create_ah(pd, &ah_attr);
}

__declspec(dllexport)
int ibv_destroy_ah(struct ibv_ah *ah)
{
	ah->handle->Release();
	delete ah;
	return 0;
}

__declspec(dllexport)
int ibv_attach_mcast(struct ibv_qp *qp, union ibv_gid *gid, uint16_t lid)
{
	return ibvw_wv_errno(qp->ud_handle->AttachMulticast((WV_GID *) gid, lid, NULL));
}

__declspec(dllexport)
int ibv_detach_mcast(struct ibv_qp *qp, union ibv_gid *gid, uint16_t lid)
{
	return ibvw_wv_errno(qp->ud_handle->DetachMulticast((WV_GID *) gid, lid, NULL));
}

__declspec(dllexport)
int ibvw_wv_errno(HRESULT hr)
{
	switch (hr) {
	case WV_SUCCESS:			return 0;
	case WV_PENDING:			_set_errno(EINPROGRESS); break;
    case WV_FILE_NOT_FOUND:     _set_errno(ENOENT); break;
	case WV_IO_PENDING:			_set_errno(EINPROGRESS); break;
	case WV_TIMEOUT:			_set_errno(ETIMEDOUT); break;
	case WV_BUFFER_OVERFLOW:	_set_errno(EOVERFLOW); break;
	case WV_DEVICE_BUSY:		_set_errno(EBUSY); break;
	case WV_ACCESS_VIOLATION:	_set_errno(EACCES); break;
	case WV_INVALID_HANDLE:		_set_errno(EINVAL); break;
	case WV_INVALID_PARAMETER:	_set_errno(EINVAL); break;
	case WV_NO_MEMORY:			_set_errno(ENOMEM); break;
	case WV_INSUFFICIENT_RESOURCES: _set_errno(ENOSPC); break;
	case WV_IO_TIMEOUT:			_set_errno(ETIMEDOUT); break;
	case WV_NOT_SUPPORTED:		_set_errno(ENOSYS); break;
	case WV_CANCELLED:			_set_errno(ECANCELED); break;
    case WV_NOT_FOUND:
    case WV_BAD_NETPATH:
	case WV_INVALID_ADDRESS:	_set_errno(EADDRNOTAVAIL); break;
	case WV_ADDRESS_ALREADY_EXISTS: _set_errno(EADDRINUSE); break;
	case WV_CONNECTION_REFUSED:	_set_errno(ECONNREFUSED); break;
	case WV_CONNECTION_INVALID:	_set_errno(ENOTCONN); break;
	case WV_CONNECTION_ACTIVE:	_set_errno(EISCONN); break;
	case WV_HOST_UNREACHABLE:	_set_errno(ENETUNREACH); break;
	case WV_CONNECTION_ABORTED:	_set_errno(ECONNABORTED); break;
	case WV_UNKNOWN_ERROR:		_set_errno(EIO); break;
	}
	return -1;
}
