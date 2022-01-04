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

#include <ntifs.h>
#include <iba/ib_rdma_cm.h>

#include "wv_ep.h"
#include "wv_qp.h"
#include "wv_ioctl.h"
#include "wv_driver.h"

#define WV_AF_INET	2
#define WV_AF_INET6	23

static void WvEpWorkHandler(WORK_ENTRY *pWork);

static void WvEpGet(WV_ENDPOINT *pEndpoint)
{
	InterlockedIncrement(&pEndpoint->Ref);
}

static void WvEpPut(WV_ENDPOINT *pEndpoint)
{
	if (InterlockedDecrement(&pEndpoint->Ref) == 0) {
		KeSetEvent(&pEndpoint->Event, 0, FALSE);
	}
}

WV_ENDPOINT *WvEpAcquire(WV_PROVIDER *pProvider, UINT64 Id)
{
	WV_ENDPOINT *ep;

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	ep = IndexListAt(&pProvider->EpIndex, (SIZE_T) Id);
	if (ep != NULL && ep->State != WvEpDestroying) {
		WvEpGet(ep);
	} else {
		ep = NULL;
		WvProviderEnableRemove(pProvider);
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	return ep;
}

void WvEpRelease(WV_ENDPOINT *pEndpoint)
{
	WvProviderEnableRemove(pEndpoint->pProvider);
	WvEpPut(pEndpoint);
}

static NTSTATUS WvEpAllocate(WV_PROVIDER *pProvider, UINT16 EpType,
							 WV_ENDPOINT **ppEndpoint)
{
	WV_ENDPOINT			*ep;
	NTSTATUS			status;
	WDF_IO_QUEUE_CONFIG	config;

	ep = ExAllocatePoolWithTag(NonPagedPool, sizeof(WV_ENDPOINT), 'pevw');
	if (ep == NULL) {
		return STATUS_NO_MEMORY;
	}

	RtlZeroMemory(ep, sizeof(WV_ENDPOINT));
	ep->pWork = ExAllocatePoolWithTag(NonPagedPool, sizeof(WV_WORK_ENTRY), 'wevw');
	if (ep->pWork == NULL) {
		status = STATUS_NO_MEMORY;
		goto err1;
	}

	ep->Ref = 1;
	ep->pProvider = pProvider;
	ep->EpType = EpType;
	KeInitializeEvent(&ep->Event, NotificationEvent, FALSE);
	InitializeListHead(&ep->Entry);

	ASSERT(ControlDevice != NULL);
	
	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
	status = WdfIoQueueCreate(ControlDevice, &config,
							  WDF_NO_OBJECT_ATTRIBUTES, &ep->Queue);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	*ppEndpoint = ep;
	return STATUS_SUCCESS;

err2:
	ExFreePoolWithTag(ep->pWork, 'wevw');
err1:
	ExFreePoolWithTag(ep, 'pevw');
	return status;
}

void WvEpCreate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64				*pId;
	UINT64				*type;
	WV_ENDPOINT			*ep;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &type, NULL);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(UINT64), &pId, NULL);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	status = WvEpAllocate(pProvider, (UINT16) *type, &ep);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	*pId = IndexListInsertHead(&pProvider->EpIndex, ep);
	if (*pId == 0) {
		status = STATUS_NO_MEMORY;
		goto err2;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvWorkEntryInit(ep->pWork, *pId, WvEpWorkHandler, pProvider);
	WdfRequestCompleteWithInformation(Request, status, sizeof(UINT64));
	return;

err2:
	KeReleaseGuardedMutex(&pProvider->Lock);
	WvEpFree(ep);
err1:
	WdfRequestComplete(Request, status);
}

void WvEpDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_ENDPOINT		*ep;
	UINT64			*id;
	NTSTATUS		status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	ep = IndexListAt(&pProvider->EpIndex, (SIZE_T) *id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
	} else if (ep->Ref > 1) {
		status = STATUS_ACCESS_DENIED;
	} else {
		IndexListRemove(&pProvider->EpIndex, (SIZE_T) *id);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WvEpFree(ep);
	}
	WvProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

void WvEpFree(WV_ENDPOINT *pEndpoint)
{
	WdfObjectAcquireLock(pEndpoint->Queue);
	pEndpoint->State = WvEpDestroying;
	WdfObjectReleaseLock(pEndpoint->Queue);

	if (InterlockedDecrement(&pEndpoint->Ref) > 0) {
		KeWaitForSingleObject(&pEndpoint->Event, Executive, KernelMode, FALSE, NULL);
	}

	if (pEndpoint->pIbCmId != NULL) {
		IbCmInterface.CM.destroy_id(pEndpoint->pIbCmId);
	}

	WdfIoQueuePurgeSynchronously(pEndpoint->Queue);
	WdfObjectDelete(pEndpoint->Queue);
	if (pEndpoint->pWork != NULL) {
		ExFreePoolWithTag(pEndpoint->pWork, 'wevw');
	}
	ExFreePoolWithTag(pEndpoint, 'pevw');
}

void WvEpQuery(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64				*id;
	WV_IO_EP_ATTRIBUTES	*pattr;
	WV_ENDPOINT			*ep;
	size_t				len = 0;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_EP_ATTRIBUTES),
											&pattr, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	ep = WvEpAcquire(pProvider, *id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	*pattr = ep->Attributes;
	WvEpRelease(ep);
	len = sizeof(WV_IO_EP_ATTRIBUTES);

complete:
	WdfRequestCompleteWithInformation(Request, status, len);
}

void WvEpModify(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID				*pId;
	size_t					inlen;
	WV_ENDPOINT				*ep;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &pId, &inlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	if (pId->Data != WV_IO_EP_OPTION_ROUTE) {
		status = STATUS_INVALID_PARAMETER;
		goto complete;
	}

	if (inlen < sizeof(WV_IO_ID) + sizeof(ib_path_rec_t)) {
		status = STATUS_BUFFER_TOO_SMALL;
		goto complete;
	}

	ep = WvEpAcquire(pProvider, pId->Id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	WdfObjectAcquireLock(ep->Queue);
	if (ep->State != WvEpAddressBound) {
		status = STATUS_NOT_SUPPORTED;
		goto release;
	}

	RtlCopyMemory(&ep->Route, pId + 1, sizeof ep->Route);
	ep->State = WvEpRouteResolved;

release:
	WdfObjectReleaseLock(ep->Queue);
	WvEpRelease(ep);
complete:
	WdfRequestComplete(Request, status);
}

void WvEpBind(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_EP_BIND		*pattr;
	WV_ENDPOINT			*ep;
	size_t				len = 0;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_BIND),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_EP_BIND),
											&pattr, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	ep = WvEpAcquire(pProvider, pattr->Id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	WdfObjectAcquireLock(ep->Queue);
	if (ep->State != WvEpIdle) {
		status = STATUS_NOT_SUPPORTED;
		goto release;
	}

	ep->Attributes.LocalAddress = pattr->Address;
	ep->Attributes.Device = pattr->Device;
	len = sizeof(WV_IO_EP_BIND);
	ep->State = WvEpAddressBound;

release:
	WdfObjectReleaseLock(ep->Queue);
	WvEpRelease(ep);
complete:
	WdfRequestCompleteWithInformation(Request, status, len);
}

static UINT64 WvGetServiceId(UINT16 EpType, WV_IO_SOCKADDR_DATA *pAddress)
{
	return RtlUlonglongByteSwap(((UINT64)EpType << 16) +
		   RtlUshortByteSwap(pAddress->SockAddr.In.SinPort));
}

static int WvAnyAddress(WV_IO_SOCKADDR_DATA *pAddress)
{
	if (pAddress->SockAddr.Sa.SaFamily == WV_AF_INET) {
		return (pAddress->SockAddr.In.SinAddr == 0) ||
			   ((pAddress->SockAddr.In.SinAddr & 0xff) == 0x7f);
	} else {
		return (RtlCompareMemoryUlong(pAddress->SockAddr.In6.Sin6Addr, 16, 0) == 16);
	}
}

static void WvFormatCmaHeader(IB_CMA_HEADER *pHeader,
							  WV_IO_SOCKADDR_DATA *pLocalAddress,
							  WV_IO_SOCKADDR_DATA *pPeerAddress)
{
	pHeader->CmaVersion = IB_CMA_VERSION;
	if (pLocalAddress->SockAddr.Sa.SaFamily == WV_AF_INET) {
		pHeader->IpVersion = IB_REQ_CM_RDMA_IPV4;
		RtlZeroMemory(pHeader->SrcAddress.Ip4.Pad, sizeof(pHeader->SrcAddress.Ip4.Pad));
		pHeader->SrcAddress.Ip4.Address = pLocalAddress->SockAddr.In.SinAddr;
		RtlZeroMemory(pHeader->DstAddress.Ip4.Pad, sizeof(pHeader->DstAddress.Ip4.Pad));
		pHeader->DstAddress.Ip4.Address = pPeerAddress->SockAddr.In.SinAddr;
		pHeader->Port = pLocalAddress->SockAddr.In.SinPort;
	} else {
		pHeader->IpVersion = IB_REQ_CM_RDMA_IPV6;
		RtlCopyMemory(pHeader->SrcAddress.Ip6Address,
					  pLocalAddress->SockAddr.In6.Sin6Addr,
					  sizeof(pHeader->SrcAddress.Ip6Address));
		RtlCopyMemory(pHeader->DstAddress.Ip6Address,
					  pPeerAddress->SockAddr.In6.Sin6Addr,
					  sizeof(pHeader->DstAddress.Ip6Address));
		pHeader->Port = pLocalAddress->SockAddr.In6.Sin6Port;
	}
}

static void WvEpSaveReply(WV_ENDPOINT *pEndpoint, iba_cm_rep_event *pReply)
{
	UINT8	len;

	len = sizeof(pEndpoint->Attributes.Param.Connect.Data);
	RtlCopyMemory(pEndpoint->Attributes.Param.Connect.Data, pReply->rep.p_pdata, len);
	pEndpoint->Attributes.Param.Connect.DataLength = len;
	pEndpoint->Attributes.Param.Connect.InitiatorDepth = pReply->rep.resp_res;
	pEndpoint->Attributes.Param.Connect.ResponderResources = pReply->rep.init_depth;
	pEndpoint->Attributes.Param.Connect.RnrRetryCount = pReply->rep.rnr_retry_cnt;
}

static void WvEpSaveReject(WV_ENDPOINT *pEndpoint, iba_cm_rej_event *pReject)
{
	UINT8	len;

	len = sizeof(pEndpoint->Attributes.Param.Connect.Data);
	RtlCopyMemory(pEndpoint->Attributes.Param.Connect.Data, pReject->p_pdata, len);
	pEndpoint->Attributes.Param.Connect.DataLength = len;
}

static NTSTATUS WvEpModifyQpErr(WV_QUEUE_PAIR *pQp,
								UINT8 *pVerbsData, UINT32 VerbsSize)
{
	ib_qp_mod_t			attr;
	ib_api_status_t		ib_status;
	NTSTATUS			status;

	attr.req_state = IB_QPS_ERROR;
	ib_status = pQp->pVerbs->ndi_modify_qp(pQp->hVerbsQp, &attr, NULL,
										   VerbsSize, pVerbsData);
	if (ib_status == IB_SUCCESS) {
		status = STATUS_SUCCESS;
	} else {	
		status = STATUS_UNSUCCESSFUL;
	}

	return status;
}

static NTSTATUS WvEpDisconnectQp(WV_PROVIDER *pProvider, UINT64 QpId,
								 UINT8 *pVerbsData, UINT32 VerbsSize)
{
	WV_QUEUE_PAIR	*qp;
	NTSTATUS		status;

	if (QpId == 0) {
		return STATUS_SUCCESS;
	}

	qp = WvQpAcquire(pProvider, QpId);
	if (qp == NULL) {
		return STATUS_NOT_FOUND;
	}

	status = WvEpModifyQpErr(qp, pVerbsData, VerbsSize);
	WvQpRelease(qp);

	return status;
}

static NTSTATUS WvEpAsyncDisconnect(WV_ENDPOINT *pEndpoint, WDFREQUEST Request)
{
	WV_IO_EP_DISCONNECT	*pattr;
	UINT8				*out;
	size_t				outlen = 0;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_DISCONNECT),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = WdfRequestRetrieveOutputBuffer(Request, 0, &out, &outlen);
	if (!NT_SUCCESS(status) && status != STATUS_BUFFER_TOO_SMALL) {
		return status;
	}

	status = (NTSTATUS) WdfRequestGetInformation(Request);
	if (NT_SUCCESS(status)) {
		status = WvEpDisconnectQp(pEndpoint->pProvider, pattr->QpId, out, outlen);
	} else {
		WvEpDisconnectQp(pEndpoint->pProvider, pattr->QpId, out, outlen);
	}

	WdfRequestCompleteWithInformation(Request, status, outlen);
	return STATUS_SUCCESS;
}

static void WvEpCompleteDisconnect(WV_ENDPOINT *pEndpoint, NTSTATUS DiscStatus)
{
	WDFREQUEST				request;
	WDFREQUEST				disc_req = NULL;
	WDF_REQUEST_PARAMETERS	param;
	NTSTATUS				status;

	WdfObjectAcquireLock(pEndpoint->Queue);
	if (pEndpoint->State == WvEpDestroying || !pEndpoint->pWork) {
		goto release;
	}
	pEndpoint->State = WvEpDisconnected;

	status = WdfIoQueueRetrieveNextRequest(pEndpoint->Queue, &request);
	while (NT_SUCCESS(status)) {

		WDF_REQUEST_PARAMETERS_INIT(&param);
		WdfRequestGetParameters(request, &param);
		if (param.Parameters.DeviceIoControl.IoControlCode == WV_IOCTL_EP_DISCONNECT) {
            ASSERT(pEndpoint->pWork != NULL);
			WdfRequestSetInformation(request, DiscStatus);
			WvProviderGet(pEndpoint->pProvider);
			WorkQueueInsert(&pEndpoint->pProvider->WorkQueue, &pEndpoint->pWork->Work);
			pEndpoint->pWork = NULL;
			disc_req = request;
		} else {
			WdfRequestComplete(request, DiscStatus);
		}

		status = WdfIoQueueRetrieveNextRequest(pEndpoint->Queue, &request);
	}

	if (disc_req != NULL) {
		WdfRequestRequeue(disc_req);
	}
release:
	WdfObjectReleaseLock(pEndpoint->Queue);

}

static NTSTATUS WvEpIbCmHandler(iba_cm_id *pId, iba_cm_event *pEvent)
{
	WV_ENDPOINT	*ep;

	ep = pId->context;
	switch (pEvent->type) {
	case iba_cm_req_error:
		WdfObjectAcquireLock(ep->Queue);
		if (ep->State == WvEpActiveConnect) {
			ep->State = WvEpRouteResolved;
			WvCompleteRequests(ep->Queue, STATUS_TIMEOUT);
		}
		WdfObjectReleaseLock(ep->Queue);
		break;
	case iba_cm_rep_error:
		WdfObjectAcquireLock(ep->Queue);
		if (ep->State == WvEpPassiveConnect) {
			ep->State = WvEpDisconnected;
			WvCompleteRequests(ep->Queue, STATUS_IO_TIMEOUT);
		}
		WdfObjectReleaseLock(ep->Queue);
		break;
	case iba_cm_dreq_error:
		WvEpCompleteDisconnect(ep, STATUS_TIMEOUT);
		break;
	case iba_cm_rep_received:
		WdfObjectAcquireLock(ep->Queue);
		if (ep->State == WvEpActiveConnect) {
			WvEpSaveReply(ep, &pEvent->data.rep);
			WvCompleteRequests(ep->Queue, STATUS_SUCCESS);
		}
		WdfObjectReleaseLock(ep->Queue);
		break;
	case iba_cm_rtu_received:
		WdfObjectAcquireLock(ep->Queue);
		if (ep->State == WvEpPassiveConnect) {
			ep->State = WvEpConnected;
			WvCompleteRequestsWithInformation(ep->Queue, STATUS_SUCCESS);
		}
		WdfObjectReleaseLock(ep->Queue);
		break;
	case iba_cm_dreq_received:
		WdfObjectAcquireLock(ep->Queue);
		if (ep->State == WvEpConnected) {
			ep->State = WvEpPassiveDisconnect;
			WvCompleteRequests(ep->Queue, STATUS_SUCCESS);
			WdfObjectReleaseLock(ep->Queue);
		} else if (ep->State == WvEpPassiveConnect) {
			ep->State = WvEpPassiveDisconnect;
			WvCompleteRequestsWithInformation(ep->Queue, STATUS_SUCCESS);
			WdfObjectReleaseLock(ep->Queue);
		} else {
			WdfObjectReleaseLock(ep->Queue);
			WvEpCompleteDisconnect(ep, STATUS_SUCCESS);
		}
		break;
	case iba_cm_drep_received:
		WvEpCompleteDisconnect(ep, STATUS_SUCCESS);
		break;
	case iba_cm_rej_received:
		WdfObjectAcquireLock(ep->Queue);
		if (ep->State == WvEpPassiveConnect || ep->State == WvEpActiveConnect) {
			ep->State = WvEpDisconnected;
			WvEpSaveReject(ep, &pEvent->data.rej);
			WvCompleteRequests(ep->Queue, STATUS_CONNECTION_REFUSED);
		}
		WdfObjectReleaseLock(ep->Queue);
		break;
	case iba_cm_mra_received:
		break;
	default:
		WdfObjectAcquireLock(ep->Queue);
		if (ep->State != WvEpDestroying) {
			ep->State = WvEpDisconnected;
			WvCompleteRequests(ep->Queue, STATUS_NOT_IMPLEMENTED);
		}
		WdfObjectReleaseLock(ep->Queue);
		break;
	}

	return STATUS_SUCCESS;
}

static NTSTATUS WvEpAsyncConnect(WV_ENDPOINT *pEndpoint, WDFREQUEST Request)
{
	WV_IO_EP_CONNECT	*pattr;
	WV_QUEUE_PAIR		*qp;
	iba_cm_req			req;
	NTSTATUS			status;
	UINT8				data[IB_REQ_PDATA_SIZE];

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_CONNECT),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	qp = WvQpAcquire(pEndpoint->pProvider, pattr->QpId);
	if (qp == NULL) {
		return STATUS_NOT_FOUND;
	}

	pEndpoint->Attributes.PeerAddress = pattr->PeerAddress;
	WvFormatCmaHeader((IB_CMA_HEADER *) data, &pEndpoint->Attributes.LocalAddress,
					  &pEndpoint->Attributes.PeerAddress);

	req.service_id = WvGetServiceId(pEndpoint->EpType, &pEndpoint->Attributes.PeerAddress);
	req.p_primary_path = &pEndpoint->Route;
	req.p_alt_path = NULL;
	req.qpn = qp->Qpn;
	req.qp_type = IB_QPT_RELIABLE_CONN;
	req.starting_psn = (net32_t) RtlRandomEx(&RandomSeed);
	req.p_pdata = data;
	RtlCopyMemory(data + sizeof(IB_CMA_HEADER), pattr->Param.Data,
				  pattr->Param.DataLength);
	req.pdata_len = sizeof(IB_CMA_HEADER) + pattr->Param.DataLength;
	req.max_cm_retries = IB_CMA_MAX_CM_RETRIES;
	req.resp_res = (UINT8) pattr->Param.ResponderResources;
	req.init_depth = (UINT8) pattr->Param.InitiatorDepth;
	req.remote_resp_timeout = IB_CMA_CM_RESPONSE_TIMEOUT;
	req.flow_ctrl = 1;
	req.local_resp_timeout = IB_CMA_CM_RESPONSE_TIMEOUT;
	req.rnr_retry_cnt = pattr->Param.RnrRetryCount;
	req.retry_cnt = pattr->Param.RetryCount;
	req.srq = (qp->pSrq != NULL);

	WvQpRelease(qp);
	RtlCopyMemory(&pEndpoint->Attributes.Param.Connect, &pattr->Param,
				  sizeof(pattr->Param));

	WdfObjectAcquireLock(pEndpoint->Queue);
	if (pEndpoint->State != WvEpRouteResolved) {
		status = STATUS_NOT_SUPPORTED;
		goto out;
	}

	status = IbCmInterface.CM.create_id(WvEpIbCmHandler, pEndpoint, &pEndpoint->pIbCmId);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	pEndpoint->State = WvEpActiveConnect;
	status = IbCmInterface.CM.send_req(pEndpoint->pIbCmId, &req);
	if (NT_SUCCESS(status)) {
		status = WdfRequestRequeue(Request);
	}

	if (!NT_SUCCESS(status)) {
		pEndpoint->State = WvEpDisconnected;
	}

out:
	WdfObjectReleaseLock(pEndpoint->Queue);
	return status;
}

static NTSTATUS WvEpProcessAsync(WV_PROVIDER *pProvider, UINT64 Id, WDFREQUEST Request)
{
	WV_ENDPOINT	*ep;
	NTSTATUS	status;

	ep = WvEpAcquire(pProvider, Id);
	if (ep == NULL) {
		return STATUS_NOT_FOUND;
	}

	WdfObjectAcquireLock(ep->Queue);
	if (!ep->pWork) {
		status = STATUS_TOO_MANY_COMMANDS;
		goto out;
	}

	status = WdfRequestForwardToIoQueue(Request, ep->Queue);
	if (NT_SUCCESS(status)) {
		WvProviderGet(pProvider);
		WorkQueueInsert(&pProvider->WorkQueue, &ep->pWork->Work);
		ep->pWork = NULL;
	}

out:
	WdfObjectReleaseLock(ep->Queue);
	WvEpRelease(ep);
	return status;
}

void WvEpConnect(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_EP_CONNECT	*pattr;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_CONNECT),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	if (pattr->Param.DataLength > sizeof(pattr->Param.Data)) {
		status = STATUS_INVALID_BUFFER_SIZE;
		goto out;
	}

	status = WvEpProcessAsync(pProvider, pattr->Id, Request);

out:
	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
	}
}

static NTSTATUS WvEpModifyQpRtr(WV_ENDPOINT *pEndpoint, WV_QUEUE_PAIR *pQp,
								UINT64 ResponderResources, UINT32 Psn,
								UINT8 *pVerbsData, UINT32 VerbsSize)
{
	ib_qp_mod_t			attr;
	ib_api_status_t		ib_status;
	NTSTATUS			status;

	status =IbCmInterface.CM.get_qp_attr(pEndpoint->pIbCmId, IB_QPS_INIT, &attr);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	
	ib_status = pQp->pVerbs->ndi_modify_qp(pQp->hVerbsQp, &attr, NULL,
										   VerbsSize, pVerbsData);
	if (ib_status != IB_SUCCESS) {
		return STATUS_UNSUCCESSFUL;
	}

	status = IbCmInterface.CM.get_qp_attr(pEndpoint->pIbCmId, IB_QPS_RTR, &attr);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	
	if (pEndpoint->State == WvEpPassiveConnect) {
		attr.state.rtr.resp_res = (UINT8) ResponderResources;
		attr.state.rtr.rq_psn = Psn;
	}

	ib_status = pQp->pVerbs->ndi_modify_qp(pQp->hVerbsQp, &attr, NULL,
										   VerbsSize, pVerbsData);
	if (ib_status != IB_SUCCESS) {
		return STATUS_UNSUCCESSFUL;
	}

	return STATUS_SUCCESS;
}

static NTSTATUS WvEpModifyQpRts(WV_ENDPOINT *pEndpoint, WV_QUEUE_PAIR *pQp,
								UINT64 InitiatorDepth,
								UINT8 *pVerbsData, UINT32 VerbsSize)
{
	ib_qp_mod_t			attr;
	ib_api_status_t		ib_status;
	NTSTATUS			status;

	status = IbCmInterface.CM.get_qp_attr(pEndpoint->pIbCmId, IB_QPS_RTS, &attr);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	
	if (pEndpoint->State == WvEpPassiveConnect) {
		attr.state.rts.init_depth = (UINT8) InitiatorDepth;
	}

	ib_status = pQp->pVerbs->ndi_modify_qp(pQp->hVerbsQp, &attr, NULL,
										   VerbsSize, pVerbsData);
	if (ib_status != IB_SUCCESS) {
		return STATUS_UNSUCCESSFUL;
	}

	return STATUS_SUCCESS;
}

static NTSTATUS WvEpAcceptActive(WDFREQUEST Request, UINT8 *pVerbsData, size_t VerbsSize,
								 WV_ENDPOINT *pEndpoint, WV_IO_EP_ACCEPT *pAttr)
{
	WV_QUEUE_PAIR		*qp;
	NTSTATUS			status;

	qp = WvQpAcquire(pEndpoint->pProvider, pAttr->QpId);
	if (qp == NULL) {
		return STATUS_NOT_FOUND;
	}

	status = WvEpModifyQpRtr(pEndpoint, qp, 0, 0, pVerbsData, VerbsSize);
	if (NT_SUCCESS(status)) {
		status = WvEpModifyQpRts(pEndpoint, qp, 0, pVerbsData, VerbsSize);
	}

	WvQpRelease(qp);

	WdfObjectAcquireLock(pEndpoint->Queue);
	if (pEndpoint->State != WvEpActiveConnect) {
		status = STATUS_NOT_SUPPORTED;
		goto release;
	}

	pEndpoint->State = WvEpConnected;
	status = IbCmInterface.CM.send_rtu(pEndpoint->pIbCmId, pAttr->Param.Data,
									   pAttr->Param.DataLength);
	if (NT_SUCCESS(status)) {
		WdfRequestCompleteWithInformation(Request, status, VerbsSize);
	} else {
		pEndpoint->State = WvEpDisconnected;
	}

release:
	WdfObjectReleaseLock(pEndpoint->Queue);
	return status;
}

static NTSTATUS WvEpAcceptPassive(WDFREQUEST Request, UINT8 *pVerbsData, size_t VerbsSize,
								  WV_ENDPOINT *pEndpoint, WV_IO_EP_ACCEPT *pAttr)
{
	WV_QUEUE_PAIR		*qp;
	iba_cm_rep			rep;
	NTSTATUS			status;

	qp = WvQpAcquire(pEndpoint->pProvider, pAttr->QpId);
	if (qp == NULL) {
		return STATUS_NOT_FOUND;
	}

	rep.qpn = qp->Qpn;
	rep.starting_psn = (net32_t) RtlRandomEx(&RandomSeed);
	rep.p_pdata = pAttr->Param.Data;
	rep.pdata_len = pAttr->Param.DataLength;
	rep.failover_accepted = IB_FAILOVER_ACCEPT_UNSUPPORTED;
	rep.resp_res = (UINT8) pAttr->Param.ResponderResources;
	rep.init_depth = (UINT8) pAttr->Param.InitiatorDepth;
	rep.flow_ctrl = 1;
	rep.rnr_retry_cnt = pAttr->Param.RnrRetryCount;
	rep.srq = (qp->pSrq != NULL);

	status = WvEpModifyQpRtr(pEndpoint, qp, pAttr->Param.ResponderResources,
							 rep.starting_psn, pVerbsData, VerbsSize);
	if (NT_SUCCESS(status)) {
		status = WvEpModifyQpRts(pEndpoint, qp, pAttr->Param.InitiatorDepth,
								 pVerbsData, VerbsSize);
	}

	WvQpRelease(qp);

	if (!NT_SUCCESS(status)) {
		goto out;
	}

	WdfObjectAcquireLock(pEndpoint->Queue);
	if (pEndpoint->State != WvEpPassiveConnect) {
		status = STATUS_NOT_SUPPORTED;
		goto release;
	}

	status = IbCmInterface.CM.send_rep(pEndpoint->pIbCmId, &rep);
	if (NT_SUCCESS(status)) {
		status = WdfRequestRequeue(Request);
	}
	
	if (!NT_SUCCESS(status)) {
		pEndpoint->State = WvEpDisconnected;
	}

release:
	WdfObjectReleaseLock(pEndpoint->Queue);
out:
	return status;
}

static NTSTATUS WvEpAsyncAccept(WV_ENDPOINT *pEndpoint, WDFREQUEST Request)
{
	WV_IO_EP_ACCEPT		*pattr;
	NTSTATUS			status;
	UINT8				*out;
	size_t				outlen;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_ACCEPT),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = WdfRequestRetrieveOutputBuffer(Request, 0, &out, &outlen);
	if (!NT_SUCCESS(status) && status != STATUS_BUFFER_TOO_SMALL) {
		return status;
	}

	/* EP state is re-checked under lock in WvEpAccept* calls */
	switch (pEndpoint->State) {
	case WvEpActiveConnect:
		status = WvEpAcceptActive(Request, out, outlen, pEndpoint, pattr);
		break;
	case WvEpPassiveConnect:
		status = WvEpAcceptPassive(Request, out, outlen, pEndpoint, pattr);
		break;
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	return status;
}

static void WvEpWorkHandler(WORK_ENTRY *pWork)
{
	WV_PROVIDER				*prov;
	WV_ENDPOINT				*ep;
	WV_WORK_ENTRY			*work;
	WDFREQUEST				request;
	WDF_REQUEST_PARAMETERS	param;
	NTSTATUS				status;

	work = CONTAINING_RECORD(pWork, WV_WORK_ENTRY, Work);
	prov = (WV_PROVIDER *) pWork->Context;

	ep = WvEpAcquire(prov, work->Id);
	if (ep == NULL) {
		ExFreePoolWithTag(work, 'wevw');
		goto out;
	}

	WdfObjectAcquireLock(ep->Queue);
	ep->pWork = work;
	status = WdfIoQueueRetrieveNextRequest(ep->Queue, &request);
	WdfObjectReleaseLock(ep->Queue);

	if (!NT_SUCCESS(status)) {
		goto put;
	}

	WDF_REQUEST_PARAMETERS_INIT(&param);
	WdfRequestGetParameters(request, &param);
	switch (param.Parameters.DeviceIoControl.IoControlCode) {
	case WV_IOCTL_EP_CONNECT:
		status = WvEpAsyncConnect(ep, request);
		break;
	case WV_IOCTL_EP_ACCEPT:
		status = WvEpAsyncAccept(ep, request);
		break;
	case WV_IOCTL_EP_DISCONNECT:
		status = WvEpAsyncDisconnect(ep, request);
		break;
	default:
		status = STATUS_NOT_IMPLEMENTED;
	}

	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(request, status);
	}
put:
	WvEpRelease(ep);
out:
	WvProviderPut(prov);
}

void WvEpAccept(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_EP_ACCEPT		*pattr;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_ACCEPT),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	if (pattr->Param.DataLength > sizeof(pattr->Param.Data)) {
		status = STATUS_INVALID_BUFFER_SIZE;
		goto out;
	}

	status = WvEpProcessAsync(pProvider, pattr->Id, Request);

out:
	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
	}
}

void WvEpReject(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID			*id;
	WV_ENDPOINT			*ep;
	NTSTATUS			status;
	size_t				len;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &id, &len);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	ep = WvEpAcquire(pProvider, id->Id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	WdfObjectAcquireLock(ep->Queue);
	if (ep->State != WvEpActiveConnect && ep->State != WvEpPassiveConnect) {
		status = STATUS_NOT_SUPPORTED;
		goto release;
	}

	ep->State = WvEpDisconnected;
	status = IbCmInterface.CM.send_rej(ep->pIbCmId, IB_REJ_USER_DEFINED,
									   NULL, 0, id + 1, len - sizeof(WV_IO_ID));

release:
	WdfObjectReleaseLock(ep->Queue);
	WvEpRelease(ep);
complete:
	WdfRequestComplete(Request, status);
}

// The IB CM could have received and processed a DREQ that we haven't seen yet.
void WvEpDisconnect(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_EP_DISCONNECT	*pattr;
	WV_ENDPOINT			*ep;
	NTSTATUS			status;
	UINT8				*out;
	size_t				outlen;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_DISCONNECT),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	status = WdfRequestRetrieveOutputBuffer(Request, 0, &out, &outlen);
	if (!NT_SUCCESS(status) && status != STATUS_BUFFER_TOO_SMALL) {
		goto complete;
	}

	ep = WvEpAcquire(pProvider, pattr->Id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	WdfObjectAcquireLock(ep->Queue);
	switch (ep->State) {
	case WvEpConnected:
		status = IbCmInterface.CM.send_dreq(ep->pIbCmId, NULL, 0);
		if (NT_SUCCESS(status)) {
			status = WdfRequestForwardToIoQueue(Request, ep->Queue);
			if (NT_SUCCESS(status)) {
				ep->State = WvEpActiveDisconnect;
				break;
			}
		}
		/* Fall through to passive disconnect case on failure */
	case WvEpActiveDisconnect:
	case WvEpPassiveDisconnect:
		ep->State = WvEpDisconnected;
		WdfObjectReleaseLock(ep->Queue);

		IbCmInterface.CM.send_drep(ep->pIbCmId, NULL, 0);

		status = WvEpDisconnectQp(ep->pProvider, pattr->QpId, out, outlen);
		if (NT_SUCCESS(status)) {
			WdfRequestCompleteWithInformation(Request, status, outlen);
		}
		goto release;
	default:
		status = STATUS_INVALID_DEVICE_STATE;
		break;
	}
	WdfObjectReleaseLock(ep->Queue);

release:
	WvEpRelease(ep);
complete:
	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
	}
}

void WvEpDisconnectNotify(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64				*id;
	WV_ENDPOINT			*ep;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	ep = WvEpAcquire(pProvider, *id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	WdfObjectAcquireLock(ep->Queue);
	switch (ep->State) {
	case WvEpConnected:
	case WvEpActiveDisconnect:
		status = WdfRequestForwardToIoQueue(Request, ep->Queue);
		if (NT_SUCCESS(status)) {
			WdfObjectReleaseLock(ep->Queue);
			WvEpRelease(ep);
			return;
		}
		break;
	case WvEpPassiveDisconnect:
	case WvEpDisconnected:
		status = STATUS_SUCCESS;
		break;
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}
	WdfObjectReleaseLock(ep->Queue);

	WvEpRelease(ep);
complete:
	WdfRequestComplete(Request, status);
}

static void WvEpGetIbRequest(WV_ENDPOINT *pListen)
{
	WV_ENDPOINT		*ep;
	WDFREQUEST		request;
	NTSTATUS		status;
	IB_CMA_HEADER	*hdr;
	iba_cm_id		*id;
	iba_cm_event	event;

	WdfObjectAcquireLock(pListen->Queue);
	while (1) {
		status = WdfIoQueueRetrieveNextRequest(pListen->Queue, &request);
		if (!NT_SUCCESS(status)) {
			break;
		}

		status = IbCmInterface.CM.get_request(pListen->pIbCmId, &id, &event);
		if (!NT_SUCCESS(status)) {
			WdfRequestRequeue(request);
			break;
		}

		ASSERT(!IsListEmpty(&pListen->Entry));
		ep = CONTAINING_RECORD(RemoveHeadList(&pListen->Entry), WV_ENDPOINT, Entry);
		ep->pIbCmId = id;
		id->callback = WvEpIbCmHandler;
		id->context = ep;

		hdr = (IB_CMA_HEADER *) event.data.req.pdata;
		if ((hdr->IpVersion >> 4) == 4) {
			ep->Attributes.LocalAddress.SockAddr.In.SinFamily = WV_AF_INET;
			ep->Attributes.LocalAddress.SockAddr.In.SinAddr = hdr->DstAddress.Ip4.Address;
			ep->Attributes.PeerAddress.SockAddr.In.SinFamily = WV_AF_INET;
			ep->Attributes.PeerAddress.SockAddr.In.SinAddr = hdr->SrcAddress.Ip4.Address;
			ep->Attributes.PeerAddress.SockAddr.In.SinPort = hdr->Port;
		} else {
			ep->Attributes.LocalAddress.SockAddr.In6.Sin6Family = WV_AF_INET6; 
			RtlCopyMemory(ep->Attributes.LocalAddress.SockAddr.In6.Sin6Addr,
						  hdr->DstAddress.Ip6Address, 16);
			ep->Attributes.PeerAddress.SockAddr.In6.Sin6Family = WV_AF_INET6;
			RtlCopyMemory(ep->Attributes.PeerAddress.SockAddr.In6.Sin6Addr,
						  hdr->SrcAddress.Ip6Address, 16);
			ep->Attributes.PeerAddress.SockAddr.In6.Sin6Port = hdr->Port;
		}
		ep->Attributes.Device.DeviceGuid = event.data.req.local_ca_guid;
		ep->Attributes.Device.Pkey = event.data.req.primary_path.pkey;
		ep->Attributes.Device.PortNumber = event.data.req.port_num;
		ep->Attributes.Param.Connect.ResponderResources = event.data.req.resp_res;
		ep->Attributes.Param.Connect.InitiatorDepth = event.data.req.init_depth;
		ep->Attributes.Param.Connect.RetryCount = event.data.req.retry_cnt;
		ep->Attributes.Param.Connect.RnrRetryCount = event.data.req.rnr_retry_cnt;
		ep->Attributes.Param.Connect.DataLength = sizeof(ep->Attributes.Param.Connect.Data);
		RtlCopyMemory(ep->Attributes.Param.Connect.Data, hdr + 1,
					  sizeof(ep->Attributes.Param.Connect.Data));
		ep->Route = event.data.req.primary_path;

		ep->State = WvEpPassiveConnect;
		WvEpPut(ep);

		WdfRequestComplete(request, STATUS_SUCCESS);
	}
	WdfObjectReleaseLock(pListen->Queue);
}

static NTSTATUS WvEpIbListenHandler(iba_cm_id *pId, iba_cm_event *pEvent)
{
	WV_ENDPOINT		*listen;

	listen = pId->context;
	WvEpGetIbRequest(listen);
	return STATUS_SUCCESS;
}

void WvEpListen(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_ENDPOINT			*ep;
	WV_IO_EP_LISTEN		*pattr;
	NTSTATUS			status;
	void				*buf;
	UINT8				len;
	UINT8				mask;
	UINT64				sid;
	IB_CMA_HEADER		hdr;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_LISTEN),
										   &pattr, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	ep = WvEpAcquire(pProvider, pattr->Id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	if (WvAnyAddress(&ep->Attributes.LocalAddress)) {
		buf = NULL;
		len = 0;
		mask = 0;
	} else {
		WvFormatCmaHeader(&hdr, &ep->Attributes.LocalAddress, &ep->Attributes.LocalAddress);
		buf = &hdr;
		len = sizeof(hdr);
		mask = IB_REQ_CM_RDMA_CMP_DST_IP;
	}

	WdfObjectAcquireLock(ep->Queue);
	if (ep->State != WvEpAddressBound) {
		status = STATUS_NOT_SUPPORTED;
		goto release;
	}

	status = IbCmInterface.CM.create_id(WvEpIbListenHandler, ep, &ep->pIbCmId);
	if (!NT_SUCCESS(status)) {
		goto release;
	}

	ep->Attributes.Param.Backlog = pattr->Backlog;
	ep->State = WvEpListening;
	sid = WvGetServiceId(ep->EpType, &ep->Attributes.LocalAddress);
	status = IbCmInterface.CM.listen(ep->pIbCmId, sid, buf, len, mask);

release:
	WdfObjectReleaseLock(ep->Queue);
	WvEpRelease(ep);
complete:
	WdfRequestComplete(Request, status);
}

void WvEpGetRequest(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_ENDPOINT				*listen, *ep;
	WV_IO_EP_GET_REQUEST	*req;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_EP_GET_REQUEST),
										   &req, NULL);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	listen = WvEpAcquire(pProvider, req->Id);
	if (listen == NULL) {
		status = STATUS_NOT_FOUND;
		goto err1;
	}

	if (listen->State != WvEpListening) {
		status = STATUS_NOT_SUPPORTED;
		goto err2;
	}

	ep = WvEpAcquire(pProvider, req->EpId);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto err2;
	}

	WdfObjectAcquireLock(ep->Queue);
	if (ep->State == WvEpIdle) {
		ep->State = WvEpQueued;
	} else {
		status = STATUS_CONNECTION_IN_USE;
	}
	WdfObjectReleaseLock(ep->Queue);
	if (!NT_SUCCESS(status)) {
		goto err3;
	}

	WdfObjectAcquireLock(listen->Queue);
	status = WdfRequestForwardToIoQueue(Request, listen->Queue);
	if (NT_SUCCESS(status)) {
		InsertTailList(&listen->Entry, &ep->Entry);
		WvEpGet(ep);
	}
	WdfObjectReleaseLock(listen->Queue);
	if (!NT_SUCCESS(status)) {
		goto err3;
	}

	WvEpRelease(ep);
	WvEpGetIbRequest(listen);
	WvEpRelease(listen);
	return;

err3:
	WvEpRelease(ep);
err2:
	WvEpRelease(listen);
err1:
	WdfRequestComplete(Request, status);
}

void WvEpLookup(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UNUSED_PARAM(pProvider);
	WdfRequestComplete(Request, STATUS_NOT_IMPLEMENTED);
}

void WvEpMulticastJoin(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UNUSED_PARAM(pProvider);
	WdfRequestComplete(Request, STATUS_NOT_IMPLEMENTED);
}

void WvEpMulticastLeave(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UNUSED_PARAM(pProvider);
	WdfRequestComplete(Request, STATUS_NOT_IMPLEMENTED);
}

//
// Note that the framework may have already canceled outstanding requests.
//
void WvEpCancelListen(WV_ENDPOINT *pListen)
{
	WV_ENDPOINT			*ep;

	WdfObjectAcquireLock(pListen->Queue);
	WvCompleteRequests(pListen->Queue, STATUS_CANCELLED);

	while (!IsListEmpty(&pListen->Entry)) {
		ep = CONTAINING_RECORD(RemoveHeadList(&pListen->Entry), WV_ENDPOINT, Entry);
		ep->State = WvEpIdle;
		WvEpPut(ep);
	}
	WdfObjectReleaseLock(pListen->Queue);
}

void WvEpCancel(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64				*id;
	WV_ENDPOINT			*ep;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	ep = WvEpAcquire(pProvider, *id);
	if (ep == NULL) {
		status = STATUS_NOT_FOUND;
		goto out;
	}

	if (ep->State == WvEpListening) {
		WvEpCancelListen(ep);
	} else {
		WvFlushQueue(ep->Queue, STATUS_CANCELLED);
	}
	WvEpRelease(ep);

out:
	WdfRequestComplete(Request, status);
}
