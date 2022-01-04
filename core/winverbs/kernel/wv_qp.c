/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
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

#include "wv_qp.h"
#include "wv_ioctl.h"

typedef struct _WV_MULTICAST
{
	LIST_ENTRY			Entry;
	WV_IO_GID			Gid;
	NET16				Lid;
	ib_mcast_handle_t	hVerbsMc;

}	WV_MULTICAST;

static void WvVerbsConvertCreate(ib_qp_create_t *pVerbsAttr,
								 WV_IO_QP_CREATE *pAttr, WV_QUEUE_PAIR *pQp)
{
	pVerbsAttr->h_sq_cq = pQp->pSendCq->hVerbsCq;
	pVerbsAttr->h_rq_cq = pQp->pReceiveCq->hVerbsCq;
	pVerbsAttr->h_srq = (pQp->pSrq != NULL) ? pQp->pSrq->hVerbsSrq : NULL;

	pVerbsAttr->qp_type = (ib_qp_type_t) pAttr->QpType;
	pVerbsAttr->sq_max_inline = pAttr->MaxInlineSend;
	pVerbsAttr->sq_depth = pAttr->SendDepth;
	pVerbsAttr->rq_depth = pAttr->ReceiveDepth;
	pVerbsAttr->sq_sge = pAttr->SendSge;
	pVerbsAttr->rq_sge = pAttr->ReceiveSge;

	pVerbsAttr->sq_signaled = ((pAttr->QpFlags & WV_IO_QP_SIGNAL_SENDS) != 0);
}

static UINT8 WvQpStateConvertVerbs(ib_qp_state_t State)
{
	switch (State) {
	case IB_QPS_RESET:	return WV_IO_QP_STATE_RESET;
	case IB_QPS_INIT:	return WV_IO_QP_STATE_INIT;
	case IB_QPS_RTR:	return WV_IO_QP_STATE_RTR;
	case IB_QPS_RTS:	return WV_IO_QP_STATE_RTS;
	case IB_QPS_SQD:	return WV_IO_QP_STATE_SQD;
	case IB_QPS_SQERR:	return WV_IO_QP_STATE_SQERROR;
	default:			return WV_IO_QP_STATE_ERROR;
	}
}

static UINT8 WvVerbsConvertMtu(UINT32 Mtu)
{
	switch (Mtu) {
	case 256:	return IB_MTU_LEN_256;
	case 512:	return IB_MTU_LEN_512;
	case 1024:	return IB_MTU_LEN_1024;
	case 2048:	return IB_MTU_LEN_2048;
	case 4096:	return IB_MTU_LEN_4096;
	default:	return 0;
	}
}
static void WvAvConvertVerbs(WV_IO_AV *pAv, ib_av_attr_t *pVerbsAv)
{
	pAv->NetworkRouteValid = (uint8_t) pVerbsAv->grh_valid;
	if (pAv->NetworkRouteValid) {
		pAv->FlowLabel = pVerbsAv->grh.ver_class_flow & RtlUlongByteSwap(0x000FFFFF);
		pAv->TrafficClass = (UINT8) (RtlUlongByteSwap(pVerbsAv->grh.ver_class_flow) >> 20);
		pAv->HopLimit = pVerbsAv->grh.hop_limit;
		RtlCopyMemory(pAv->SGid, &pVerbsAv->grh.src_gid, sizeof(pAv->SGid));
		RtlCopyMemory(pAv->DGid, &pVerbsAv->grh.dest_gid, sizeof(pAv->DGid));
	}

	pAv->PortNumber = pVerbsAv->port_num;
	pAv->ServiceLevel = pVerbsAv->sl;
	pAv->DLid = pVerbsAv->dlid;
	pAv->StaticRate = pVerbsAv->static_rate;
	pAv->SourcePathBits = pVerbsAv->path_bits;
}

static void WvVerbsConvertAv(ib_av_attr_t *pVerbsAv, WV_IO_AV *pAv,
							 WV_IO_QP_ATTRIBUTES *pAttr)
{
	pVerbsAv->grh_valid = pAv->NetworkRouteValid;
	if (pVerbsAv->grh_valid) {
		pVerbsAv->grh.ver_class_flow =
			RtlUlongByteSwap(RtlUlongByteSwap(pAv->FlowLabel) | (pAv->TrafficClass << 20));
		pVerbsAv->grh.hop_limit = pAv->HopLimit;
		RtlCopyMemory(&pVerbsAv->grh.src_gid, pAv->SGid, sizeof(pAv->SGid));
		RtlCopyMemory(&pVerbsAv->grh.dest_gid, pAv->DGid, sizeof(pAv->DGid));
	}

	pVerbsAv->port_num = pAv->PortNumber;
	pVerbsAv->sl = pAv->ServiceLevel;
	pVerbsAv->dlid = pAv->DLid;
	pVerbsAv->static_rate = pAv->StaticRate;
	pVerbsAv->path_bits = pAv->SourcePathBits;

	pVerbsAv->conn.path_mtu = WvVerbsConvertMtu(pAttr->PathMtu);
	pVerbsAv->conn.local_ack_timeout = pAttr->LocalAckTimeout;
	pVerbsAv->conn.seq_err_retry_cnt = pAttr->SequenceErrorRetryCount;
	pVerbsAv->conn.rnr_retry_cnt = pAttr->RnrRetryCount;
}

static void WvQpAttrConvertVerbs(WV_IO_QP_ATTRIBUTES *pAttr, ib_qp_attr_t *pVerbsAttr)
{
	pAttr->MaxInlineSend = pVerbsAttr->sq_max_inline;
	pAttr->SendDepth = pVerbsAttr->sq_depth;
	pAttr->ReceiveDepth = pVerbsAttr->rq_depth;
	pAttr->SendSge = pVerbsAttr->sq_sge;
	pAttr->ReceiveSge = pVerbsAttr->rq_sge;
	pAttr->InitiatorDepth = pVerbsAttr->init_depth;
	pAttr->ResponderResources = pVerbsAttr->resp_res;

	pAttr->Options = 0;
	pAttr->QpType = pVerbsAttr->qp_type;
	pAttr->CurrentQpState = WvQpStateConvertVerbs(pVerbsAttr->state);
	pAttr->QpState = pAttr->CurrentQpState;
	pAttr->ApmState = pVerbsAttr->apm_state;
	pAttr->Qpn = pVerbsAttr->num;
	pAttr->DestinationQpn = pVerbsAttr->dest_num;
	pAttr->Qkey = pVerbsAttr->qkey;
	pAttr->SendPsn = pVerbsAttr->sq_psn;
	pAttr->ReceivePsn = pVerbsAttr->rq_psn;

	pAttr->QpFlags = pVerbsAttr->sq_signaled ? WV_IO_QP_SIGNAL_SENDS : 0;
	pAttr->AccessFlags = (UINT8) pVerbsAttr->access_ctrl;

	pAttr->AddressVector.PortNumber = pVerbsAttr->primary_port;
	pAttr->AlternateAddressVector.PortNumber = pVerbsAttr->alternate_port;
	WvAvConvertVerbs(&pAttr->AddressVector, &pVerbsAttr->primary_av);
	WvAvConvertVerbs(&pAttr->AlternateAddressVector, &pVerbsAttr->alternate_av);
	pAttr->PathMtu = 0x80 << pVerbsAttr->primary_av.conn.path_mtu;
	pAttr->AlternatePathMtu	= 0x80 << pVerbsAttr->alternate_av.conn.path_mtu;
	pAttr->PkeyIndex = pVerbsAttr->pkey_index;
	pAttr->AlternatePkeyIndex = pAttr->PkeyIndex;	// TODO: missing in ib_qp_attr_t
	pAttr->LocalAckTimeout = pVerbsAttr->primary_av.conn.local_ack_timeout;
	pAttr->AlternateLocalAckTimeout	= pVerbsAttr->alternate_av.conn.local_ack_timeout;

	pAttr->RnrNakTimeout = 0;						// TODO: missing in ib_ap_attr_t
	pAttr->SequenceErrorRetryCount = pVerbsAttr->primary_av.conn.seq_err_retry_cnt;
	pAttr->RnrRetryCount = pVerbsAttr->primary_av.conn.rnr_retry_cnt;
}

static ib_qp_opts_t WvVerbsConvertOptions(UINT32 Options)
{
	UINT32 opt = 0;

	if ((Options & WV_IO_QP_ATTR_CAPABILITIES) != 0) {
		opt |= IB_MOD_QP_SQ_DEPTH | IB_MOD_QP_RQ_DEPTH;
	}
	if ((Options & WV_IO_QP_ATTR_INITIATOR_DEPTH) != 0) {
		opt |= IB_MOD_QP_INIT_DEPTH;
	}
	if ((Options & WV_IO_QP_ATTR_RESPONDER_RESOURCES) != 0) {
		opt |= IB_MOD_QP_RESP_RES;
	}
	if ((Options & WV_IO_QP_ATTR_CURRENT_STATE) != 0) {
		opt |= IB_MOD_QP_CURRENT_STATE;
	}
	if ((Options & WV_IO_QP_ATTR_PATH_MIG_STATE) != 0) {
		opt |= IB_MOD_QP_APM_STATE;
	}
	if ((Options & WV_IO_QP_ATTR_QKEY) != 0) {
		opt |= IB_MOD_QP_QKEY;
	}
	if ((Options & WV_IO_QP_ATTR_ACCESS_FLAGS) != 0) {
		opt |= IB_MOD_QP_ACCESS_CTRL;
	}
	if ((Options & WV_IO_QP_ATTR_AV) != 0) {
		opt |= IB_MOD_QP_PRIMARY_AV;
	}
	if ((Options & WV_IO_QP_ATTR_ALTERNATE_AV) != 0) {
		opt |= IB_MOD_QP_ALTERNATE_AV;
	}
	if ((Options & WV_IO_QP_ATTR_PORT_NUMBER) != 0) {
		opt |= IB_MOD_QP_PRIMARY_PORT;
	}
	if ((Options & WV_IO_QP_ATTR_PKEY_INDEX) != 0) {
		opt |= IB_MOD_QP_PKEY;
	}
	if ((Options & WV_IO_QP_ATTR_ACK_TIMEOUT) != 0) {
		opt |= IB_MOD_QP_LOCAL_ACK_TIMEOUT;
	}
	if ((Options & WV_IO_QP_ATTR_RNR_NAK_TIMEOUT) != 0) {
		opt |= IB_MOD_QP_RNR_NAK_TIMEOUT;
	}
	if ((Options & WV_IO_QP_ATTR_ERROR_RETRY_COUNT) != 0) {
		opt |= IB_MOD_QP_RETRY_CNT;
	}
	if ((Options & WV_IO_QP_ATTR_RNR_RETRY_COUNT) != 0) {
		opt |= IB_MOD_QP_RNR_RETRY_CNT;
	}

	return (ib_qp_opts_t) opt;
}

static ib_qp_state_t WvVerbsConvertState(UINT8 State)
{
	switch (State) {
	case WV_IO_QP_STATE_RESET:		return IB_QPS_RESET;
	case WV_IO_QP_STATE_INIT:		return IB_QPS_INIT;
	case WV_IO_QP_STATE_RTR:		return IB_QPS_RTR;
	case WV_IO_QP_STATE_RTS:		return IB_QPS_RTS;
	case WV_IO_QP_STATE_SQD:		return IB_QPS_SQD;
	case WV_IO_QP_STATE_SQERROR:	return IB_QPS_SQERR;
	default:						return IB_QPS_ERROR;
	}
}

static void WvVerbsConvertAttr(ib_qp_mod_t *pVerbsAttr, WV_IO_QP_ATTRIBUTES *pAttr)
{
	switch (pAttr->QpState) {
	case WV_IO_QP_STATE_RESET:
		pVerbsAttr->req_state = IB_QPS_RESET;
		break;
	case WV_IO_QP_STATE_INIT:
		pVerbsAttr->req_state = IB_QPS_INIT;

		pVerbsAttr->state.init.primary_port = pAttr->AddressVector.PortNumber;
		pVerbsAttr->state.init.qkey = pAttr->Qkey;
		pVerbsAttr->state.init.pkey_index = pAttr->PkeyIndex;
		pVerbsAttr->state.init.access_ctrl = pAttr->AccessFlags;
		break;
	case WV_IO_QP_STATE_RTR:
		pVerbsAttr->req_state = IB_QPS_RTR;

		pVerbsAttr->state.rtr.rq_psn = pAttr->ReceivePsn;
		pVerbsAttr->state.rtr.dest_qp = pAttr->DestinationQpn;
		WvVerbsConvertAv(&pVerbsAttr->state.rtr.primary_av,
						 &pAttr->AddressVector, pAttr);
		pVerbsAttr->state.rtr.resp_res = (UINT8) pAttr->ResponderResources;
		pVerbsAttr->state.rtr.rnr_nak_timeout = pAttr->RnrNakTimeout;

		pVerbsAttr->state.rtr.opts = WvVerbsConvertOptions(pAttr->Options);
		WvVerbsConvertAv(&pVerbsAttr->state.rtr.alternate_av,
						 &pAttr->AlternateAddressVector, pAttr);
		pVerbsAttr->state.rtr.qkey = pAttr->Qkey;
		pVerbsAttr->state.rtr.pkey_index = pAttr->PkeyIndex;
		pVerbsAttr->state.rtr.access_ctrl = pAttr->AccessFlags;
		pVerbsAttr->state.rtr.sq_depth = pAttr->SendDepth;
		pVerbsAttr->state.rtr.rq_depth = pAttr->ReceiveDepth;
		break;
	case WV_IO_QP_STATE_RTS:
		pVerbsAttr->req_state = IB_QPS_RTS;

		pVerbsAttr->state.rts.sq_psn = pAttr->SendPsn;
		pVerbsAttr->state.rts.retry_cnt = pAttr->SequenceErrorRetryCount;
		pVerbsAttr->state.rts.rnr_retry_cnt	= pAttr->RnrRetryCount;
		pVerbsAttr->state.rts.local_ack_timeout = pAttr->LocalAckTimeout;
		pVerbsAttr->state.rts.init_depth = (UINT8) pAttr->InitiatorDepth;

		pVerbsAttr->state.rts.opts = WvVerbsConvertOptions(pAttr->Options);
		pVerbsAttr->state.rts.rnr_nak_timeout = pAttr->RnrNakTimeout;
		pVerbsAttr->state.rts.current_state = WvVerbsConvertState(pAttr->CurrentQpState);
		pVerbsAttr->state.rts.qkey = pAttr->Qkey;
		pVerbsAttr->state.rts.access_ctrl = pAttr->AccessFlags;
		pVerbsAttr->state.rts.resp_res = (UINT8) pAttr->ResponderResources;

		WvVerbsConvertAv(&pVerbsAttr->state.rts.primary_av,
						 &pAttr->AddressVector, pAttr);
		WvVerbsConvertAv(&pVerbsAttr->state.rts.alternate_av,
						 &pAttr->AlternateAddressVector, pAttr);

		pVerbsAttr->state.rts.sq_depth = pAttr->SendDepth;
		pVerbsAttr->state.rts.rq_depth = pAttr->ReceiveDepth;

		pVerbsAttr->state.rts.apm_state = pAttr->ApmState;
		pVerbsAttr->state.rts.primary_port = pAttr->AddressVector.PortNumber;
		pVerbsAttr->state.rts.pkey_index = pAttr->PkeyIndex;
		break;
	case WV_IO_QP_STATE_SQD:
		pVerbsAttr->req_state = IB_QPS_SQD;
		pVerbsAttr->state.sqd.sqd_event = 1;
		break;
	case WV_IO_QP_STATE_SQERROR:
		pVerbsAttr->req_state = IB_QPS_SQERR;
		break;
	default:
		pVerbsAttr->req_state = IB_QPS_ERROR;
		break;
	}
}

static void WvQpGet(WV_QUEUE_PAIR *pQp)
{
	InterlockedIncrement(&pQp->Ref);
}

static void WvQpPut(WV_QUEUE_PAIR *pQp)
{
	if (InterlockedDecrement(&pQp->Ref) == 0) {
		KeSetEvent(&pQp->Event, 0, FALSE);
	}
}

WV_QUEUE_PAIR *WvQpAcquire(WV_PROVIDER *pProvider, UINT64 Id)
{
	WV_QUEUE_PAIR *qp;

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	qp = IndexListAt(&pProvider->QpIndex, (SIZE_T) Id);
	if (qp != NULL && qp->hVerbsQp != NULL) {
		WvQpGet(qp);
	} else {
		qp = NULL;
		WvProviderEnableRemove(pProvider);
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	return qp;
}

void WvQpRelease(WV_QUEUE_PAIR *pQp)
{
	WvProviderEnableRemove(pQp->pProvider);
	WvQpPut(pQp);
}

static NTSTATUS WvQpCreateAcquire(WV_PROVIDER *pProvider, WV_QUEUE_PAIR *pQp,
								  WV_IO_QP_CREATE *pAttributes)
{
	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	pQp->pPd = IndexListAt(&pProvider->PdIndex, (SIZE_T) pAttributes->Id.Id);
	if (pQp->pPd != NULL && pQp->pPd->hVerbsPd != NULL) {
		WvPdGet(pQp->pPd);
		pQp->pVerbs = pQp->pPd->pVerbs;
	} else {
		goto err1;
	}

	pQp->pSendCq = IndexListAt(&pProvider->CqIndex, (SIZE_T) pAttributes->SendCqId);
	if (pQp->pSendCq != NULL && pQp->pSendCq->hVerbsCq != NULL) {
		WvCqGet(pQp->pSendCq);
	} else {
		goto err2;
	}

	pQp->pReceiveCq = IndexListAt(&pProvider->CqIndex,
								  (SIZE_T) pAttributes->ReceiveCqId);
	if (pQp->pReceiveCq != NULL && pQp->pReceiveCq->hVerbsCq != NULL) {
		WvCqGet(pQp->pReceiveCq);
	} else {
		goto err3;
	}

	if (pAttributes->SrqId != 0) {
		pQp->pSrq = IndexListAt(&pProvider->SrqIndex, (SIZE_T) pAttributes->SrqId);
		if (pQp->pSrq != NULL && pQp->pSrq->hVerbsSrq != NULL) {
			WvSrqGet(pQp->pSrq);
		} else {
			goto err4;
		}
	} else {
		pQp->pSrq = NULL;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	return STATUS_SUCCESS;

err4:
	WvCqPut(pQp->pReceiveCq);
err3:
	WvCqPut(pQp->pSendCq);
err2:
	WvPdPut(pQp->pPd);
err1:
	WvProviderEnableRemove(pProvider);
	KeReleaseGuardedMutex(&pProvider->Lock);
	return STATUS_NOT_FOUND;
}

static void WvQpCreateRelease(WV_PROVIDER *pProvider, WV_QUEUE_PAIR *pQp)
{
	WvProviderEnableRemove(pProvider);
	if (pQp->pSrq != NULL) {
		WvSrqPut(pQp->pSrq);
	}
	WvCqPut(pQp->pReceiveCq);
	WvCqPut(pQp->pSendCq);
	WvPdPut(pQp->pPd);
}

static void WvQpEventHandler(ib_event_rec_t *pEvent)
{
	// TODO: Handle QP events when adding connection handling
	// IB_AE_QP_FATAL
	// IB_AE_QP_COMM
	// IB_AE_QP_APM
	UNREFERENCED_PARAMETER(pEvent);
}

void WvQpCreate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_QP_CREATE			*inattr, *outattr;
	size_t					inlen, outlen;
	WV_QUEUE_PAIR			*qp;
	ib_qp_create_t			create;
	ib_qp_attr_t			ib_attr;
	NTSTATUS				status;
	ib_api_status_t			ib_status;
	ci_umv_buf_t			verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_QP_CREATE),
										   &inattr, &inlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_QP_CREATE),
											&outattr, &outlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	qp = ExAllocatePoolWithTag(NonPagedPool, sizeof(WV_QUEUE_PAIR), 'pqvw');
	if (qp == NULL) {
		status = STATUS_NO_MEMORY;
		goto err1;
	}

	qp->Ref = 1;
	qp->pProvider = pProvider;
	KeInitializeEvent(&qp->Event, NotificationEvent, FALSE);
	InitializeListHead(&qp->McList);
	KeInitializeGuardedMutex(&qp->Lock);
	status = WvQpCreateAcquire(pProvider, qp, inattr);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	WvVerbsConvertCreate(&create, inattr, qp);
	WvInitVerbsData(&verbsData, inattr->Id.VerbInfo, inlen - sizeof(WV_IO_QP_CREATE),
					outlen - sizeof(WV_IO_QP_CREATE), inattr + 1);
	ib_status = qp->pVerbs->create_qp(qp->pPd->hVerbsPd, qp, WvQpEventHandler,
									  &create, &ib_attr, &qp->hVerbsQp, &verbsData);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err3;
	}

	qp->Qpn = ib_attr.num;
	KeAcquireGuardedMutex(&pProvider->Lock);
	outattr->Id.Id = IndexListInsertHead(&pProvider->QpIndex, qp);
	if (outattr->Id.Id == 0) {
		status = STATUS_NO_MEMORY;
		goto err4;
	}
	InsertHeadList(&qp->pPd->QpList, &qp->Entry);
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvProviderEnableRemove(pProvider);
	outattr->Id.VerbInfo = verbsData.status;
	WdfRequestCompleteWithInformation(Request, status, outlen);
	return;

err4:
	KeReleaseGuardedMutex(&pProvider->Lock);
	qp->pVerbs->destroy_qp(qp->hVerbsQp, 0);
err3:
	WvQpCreateRelease(pProvider, qp);
err2:
	ExFreePoolWithTag(qp, 'pqvw');
err1:
	WdfRequestComplete(Request, status);
}

void WvQpDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_QUEUE_PAIR	*qp;
	UINT64			*id;
	NTSTATUS		status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	qp = IndexListAt(&pProvider->QpIndex, (SIZE_T) *id);
	if (qp == NULL) {
		status = STATUS_NOT_FOUND;
	} else if (qp->Ref > 1) {
		status = STATUS_ACCESS_DENIED;
	} else {
		IndexListRemove(&pProvider->QpIndex, (SIZE_T) *id);
		RemoveEntryList(&qp->Entry);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WvQpFree(qp);
	}
	WvProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

void WvQpFree(WV_QUEUE_PAIR *pQp)
{
	WV_MULTICAST		*mc;
	LIST_ENTRY			*entry;

	while ((entry = RemoveHeadList(&pQp->McList)) != &pQp->McList) {
		mc = CONTAINING_RECORD(entry, WV_MULTICAST, Entry);
		if (mc->hVerbsMc != NULL) {
			pQp->pVerbs->detach_mcast(mc->hVerbsMc);
		}
		ExFreePoolWithTag(mc, 'cmvw');
	}

	if (InterlockedDecrement(&pQp->Ref) > 0) {
		KeWaitForSingleObject(&pQp->Event, Executive, KernelMode, FALSE, NULL);
	}

	if (pQp->hVerbsQp != NULL) {
		pQp->pVerbs->destroy_qp(pQp->hVerbsQp, 0);
	}

	if (pQp->pSrq != NULL) {
		WvSrqPut(pQp->pSrq);
	}
	WvCqPut(pQp->pReceiveCq);
	WvCqPut(pQp->pSendCq);
	WvPdPut(pQp->pPd);
	ExFreePoolWithTag(pQp, 'pqvw');
}

void WvQpQuery(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64					*id;
	WV_IO_QP_ATTRIBUTES		*pattr;
	size_t					inlen, outlen, len = 0;
	WV_QUEUE_PAIR			*qp;
	ib_qp_attr_t			attr;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, &inlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_QP_ATTRIBUTES),
											&pattr, &outlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	qp = WvQpAcquire(pProvider, *id);
	if (qp == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	RtlZeroMemory(&attr, sizeof attr);
	qp->pVerbs->query_qp(qp->hVerbsQp, &attr, NULL);
	WvQpRelease(qp);

	WvQpAttrConvertVerbs(pattr, &attr);
	len = outlen;

complete:
	WdfRequestCompleteWithInformation(Request, status, len);
}

void WvQpModify(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_QP_ATTRIBUTES		*pattr;
	UINT8					*out;
	size_t					outlen, len = 0;
	WV_QUEUE_PAIR			*qp;
	ib_qp_mod_t				attr;
	NTSTATUS				status;
	ib_api_status_t			ib_status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_QP_ATTRIBUTES),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, 0, &out, &outlen);
	if (!NT_SUCCESS(status) && status != STATUS_BUFFER_TOO_SMALL) {
		goto complete;
	}

	WvVerbsConvertAttr(&attr, pattr);
	qp = WvQpAcquire(pProvider, pattr->Id.Id);
	if (qp == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	ib_status = qp->pVerbs->ndi_modify_qp(qp->hVerbsQp, &attr, NULL, (UINT32) outlen, out);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
	}
	WvQpRelease(qp);
	len = outlen;

complete:
	WdfRequestCompleteWithInformation(Request, status, len);
}

void WvQpAttach(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_MULTICAST			*pmc;
	WV_IO_QP_MULTICAST		*mc;
	WV_QUEUE_PAIR			*qp;
	NTSTATUS				status;
	ib_api_status_t			ib_status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_QP_MULTICAST),
										   &mc, NULL);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	pmc = ExAllocatePoolWithTag(PagedPool, sizeof(WV_MULTICAST), 'cmvw');
	if (pmc == NULL) {
		status = STATUS_NO_MEMORY;
		goto err1;
	}

	qp = WvQpAcquire(pProvider, mc->Id.Id);
	if (qp == NULL) {
		status = STATUS_NOT_FOUND;
		goto err2;
	}

	pmc->Gid = mc->Gid;
	pmc->Lid = (NET16) mc->Id.Data;
	ib_status = qp->pVerbs->attach_mcast(qp->hVerbsQp, (ib_gid_t *) &mc->Gid,
										 (NET16) mc->Id.Data, &pmc->hVerbsMc, NULL);

	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err3;
	}

	KeAcquireGuardedMutex(&qp->Lock);
	InsertHeadList(&qp->McList, &pmc->Entry);
	KeReleaseGuardedMutex(&qp->Lock);

	WvQpRelease(qp);
	WdfRequestComplete(Request, STATUS_SUCCESS);
	return;

err3:
	WvQpRelease(qp);
err2:
	ExFreePoolWithTag(pmc, 'cmvw');
err1:
	WdfRequestComplete(Request, status);
}

void WvQpDetach(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_MULTICAST			*pmc;
	WV_IO_QP_MULTICAST		*mc;
	WV_QUEUE_PAIR			*qp;
	LIST_ENTRY				*entry;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_QP_MULTICAST),
										   &mc, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	qp = WvQpAcquire(pProvider, mc->Id.Id);
	if (qp == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	KeAcquireGuardedMutex(&qp->Lock);
	for (entry = qp->McList.Flink; entry != &qp->McList; entry = entry->Flink) {
		pmc = CONTAINING_RECORD(entry, WV_MULTICAST, Entry);

		if (RtlCompareMemory(&pmc->Gid, &mc->Gid, sizeof(pmc->Gid)) == sizeof(pmc->Gid) &&
			pmc->Lid == (NET16) mc->Id.Data) {
			RemoveEntryList(&pmc->Entry);
			KeReleaseGuardedMutex(&qp->Lock);

			if (pmc->hVerbsMc != NULL) {
				qp->pVerbs->detach_mcast(pmc->hVerbsMc);
			}

			ExFreePoolWithTag(pmc, 'cmvw');
			status = STATUS_SUCCESS;
			goto release;
		}
	}
	KeReleaseGuardedMutex(&qp->Lock);
	status = STATUS_NOT_FOUND;

release:
	WvQpRelease(qp);
complete:
	WdfRequestComplete(Request, status);
}

void WvQpRemoveHandler(WV_QUEUE_PAIR *pQp)
{
	WV_MULTICAST		*mc;
	LIST_ENTRY			*entry;

	for (entry = pQp->McList.Flink; entry != &pQp->McList; entry = entry->Flink) {
		mc = CONTAINING_RECORD(entry, WV_MULTICAST, Entry);
		pQp->pVerbs->detach_mcast(mc->hVerbsMc);
		mc->hVerbsMc = NULL;
	}

	pQp->pVerbs->destroy_qp(pQp->hVerbsQp, 0);
	pQp->hVerbsQp = NULL;
}

void WvQpCancel(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64				*id;
	WV_QUEUE_PAIR		*qp;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	qp = WvQpAcquire(pProvider, *id);
	if (qp == NULL) {
		status = STATUS_NOT_FOUND;
		goto out;
	}

	// CI does not yet support async operations
	WvQpRelease(qp);

out:
	WdfRequestComplete(Request, status);
}
