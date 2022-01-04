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

#include "wv_driver.h"
#include "wv_srq.h"
#include "wv_ioctl.h"

void WvSrqGet(WV_SHARED_RECEIVE_QUEUE *pSrq)
{
	InterlockedIncrement(&pSrq->Ref);
}

void WvSrqPut(WV_SHARED_RECEIVE_QUEUE *pSrq)
{
	if (InterlockedDecrement(&pSrq->Ref) == 0) {
		KeSetEvent(&pSrq->Event, 0, FALSE);
	}
}

WV_SHARED_RECEIVE_QUEUE *WvSrqAcquire(WV_PROVIDER *pProvider, UINT64 Id)
{
	WV_SHARED_RECEIVE_QUEUE *srq;

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	srq = IndexListAt(&pProvider->SrqIndex, (SIZE_T) Id);
	if (srq != NULL && srq->hVerbsSrq != NULL) {
		WvSrqGet(srq);
	} else {
		srq = NULL;
		WvProviderEnableRemove(pProvider);
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	return srq;
}

void WvSrqRelease(WV_SHARED_RECEIVE_QUEUE *pSrq)
{
	WvProviderEnableRemove(pSrq->pProvider);
	WvSrqPut(pSrq);
}

static void WvVerbsConvertSrq(ib_srq_attr_t *pVerbsAttr,
							  WV_IO_SRQ_ATTRIBUTES *pAttr)
{
	pVerbsAttr->max_wr	  = pAttr->MaxWr;
	pVerbsAttr->max_sge	  = pAttr->MaxSge;
	pVerbsAttr->srq_limit = pAttr->SrqLimit;
}

static void WvSrqEventHandler(ib_event_rec_t *pEvent)
{
	WV_SHARED_RECEIVE_QUEUE *srq = pEvent->context;

	switch (pEvent->type) {
	case IB_AE_SRQ_LIMIT_REACHED:
		WvFlushQueue(srq->Queue, STATUS_ALERTED);
		break;
	case IB_AE_SRQ_QP_LAST_WQE_REACHED:
		WvFlushQueue(srq->Queue, STATUS_NOTIFY_CLEANUP);
		break;
	default:
		WvFlushQueue(srq->Queue, STATUS_UNEXPECTED_IO_ERROR);
		break;
	}
}

static NTSTATUS WvSrqAlloc(WV_PROTECTION_DOMAIN *pPd, WV_IO_SRQ_ATTRIBUTES *pAttr,
						  WV_SHARED_RECEIVE_QUEUE **ppSrq, ci_umv_buf_t *pVerbsData)
{
	WV_SHARED_RECEIVE_QUEUE	*srq;
	ib_srq_attr_t			attr;
	ib_api_status_t			ib_status;
	NTSTATUS				status;
	WDF_IO_QUEUE_CONFIG		config;

	srq = ExAllocatePoolWithTag(NonPagedPool, sizeof(WV_SHARED_RECEIVE_QUEUE), 'rsvw');
	if (srq == NULL) {
		return STATUS_NO_MEMORY;
	}

	srq->Ref = 1;
	KeInitializeEvent(&srq->Event, NotificationEvent, FALSE);

	ASSERT(ControlDevice != NULL);

	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
	status = WdfIoQueueCreate(ControlDevice, &config,
							  WDF_NO_OBJECT_ATTRIBUTES, &srq->Queue);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	WvVerbsConvertSrq(&attr, pAttr);
	ib_status = pPd->pVerbs->create_srq(pPd->hVerbsPd, srq, WvSrqEventHandler,
										&attr, &srq->hVerbsSrq, pVerbsData);
	if (ib_status != IB_SUCCESS) {
		goto err2;
	}

	WvPdGet(pPd);
	srq->pPd = pPd;
	srq->pProvider = pPd->pDevice->pProvider;
	srq->pVerbs = pPd->pVerbs;
	*ppSrq = srq;
	return STATUS_SUCCESS;

err2:
	WdfObjectDelete(srq->Queue);
err1:
	ExFreePoolWithTag(srq, 'rsvw');
	return STATUS_UNSUCCESSFUL;
}

void WvSrqCreate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_SRQ_ATTRIBUTES	*inAttr, *outAttr;
	size_t					inlen, outlen;
	WV_PROTECTION_DOMAIN	*pd;
	WV_SHARED_RECEIVE_QUEUE	*srq;
	NTSTATUS				status;
	ci_umv_buf_t			verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_SRQ_ATTRIBUTES),
										   &inAttr, &inlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_SRQ_ATTRIBUTES),
											&outAttr, &outlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	pd = WvPdAcquire(pProvider, inAttr->Id.Id);
	if (pd == NULL) {
		status = STATUS_NOT_FOUND;
		goto err1;
	}

	WvInitVerbsData(&verbsData, inAttr->Id.VerbInfo, inlen - sizeof(WV_IO_SRQ_ATTRIBUTES),
					outlen - sizeof(WV_IO_SRQ_ATTRIBUTES), inAttr + 1);
	status = WvSrqAlloc(pd, inAttr, &srq, &verbsData);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	outAttr->Id.Id = IndexListInsertHead(&pProvider->SrqIndex, srq);
	if (outAttr->Id.Id == 0) {
		status = STATUS_NO_MEMORY;
		goto err3;
	}
	InsertHeadList(&pd->SrqList, &srq->Entry);
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvPdRelease(pd);
	outAttr->Id.VerbInfo = verbsData.status;
	WdfRequestCompleteWithInformation(Request, status, outlen);
	return;

err3:
	KeReleaseGuardedMutex(&pProvider->Lock);
	WvSrqFree(srq);
err2:
	WvPdRelease(pd);
err1:
	WdfRequestComplete(Request, status);
}

void WvSrqDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_SHARED_RECEIVE_QUEUE	*srq;
	UINT64					*id;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	srq = IndexListAt(&pProvider->SrqIndex, (SIZE_T) *id);
	if (srq == NULL) {
		status = STATUS_NOT_FOUND;
	} else if (srq->Ref > 1) {
		status = STATUS_ACCESS_DENIED;
	} else {
		IndexListRemove(&pProvider->SrqIndex, (SIZE_T) *id);
		RemoveEntryList(&srq->Entry);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WvSrqFree(srq);
	}
	WvProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

void WvSrqFree(WV_SHARED_RECEIVE_QUEUE *pSrq)
{
	if (InterlockedDecrement(&pSrq->Ref) > 0) {
		KeWaitForSingleObject(&pSrq->Event, Executive, KernelMode, FALSE, NULL);
	}

	if (pSrq->hVerbsSrq != NULL) {
		pSrq->pVerbs->destroy_srq(pSrq->hVerbsSrq);
	}

	WdfIoQueuePurgeSynchronously(pSrq->Queue);
	WdfObjectDelete(pSrq->Queue);
	WvPdPut(pSrq->pPd);
	ExFreePoolWithTag(pSrq, 'rsvw');
}

void WvSrqModify(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_SRQ_ATTRIBUTES	*pinAttr, *poutAttr;
	size_t					inlen, outlen, len = 0;
	WV_SHARED_RECEIVE_QUEUE	*srq;
	ib_srq_attr_t			attr;
	NTSTATUS				status;
	ib_api_status_t			ib_status;
	ci_umv_buf_t			verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_SRQ_ATTRIBUTES),
										   &pinAttr, &inlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_SRQ_ATTRIBUTES),
											&poutAttr, &outlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	srq = WvSrqAcquire(pProvider, pinAttr->Id.Id);
	if (srq == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	WvInitVerbsData(&verbsData, pinAttr->Id.VerbInfo, inlen - sizeof(WV_IO_SRQ_ATTRIBUTES),
					outlen - sizeof(WV_IO_SRQ_ATTRIBUTES), pinAttr + 1);
	WvVerbsConvertSrq(&attr, pinAttr);
	ib_status = srq->pVerbs->modify_srq(srq->hVerbsSrq, &attr,
										IB_SRQ_MAX_WR | IB_SRQ_LIMIT, &verbsData);
	WvSrqRelease(srq);

	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
	}

	len = outlen;
	poutAttr->Id.VerbInfo = verbsData.status;
complete:
	WdfRequestCompleteWithInformation(Request, status, len);
}

void WvSrqQuery(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_SRQ_ATTRIBUTES	*pinAttr, *poutAttr;
	size_t					inlen, outlen, len = 0;
	WV_SHARED_RECEIVE_QUEUE	*srq;
	ib_srq_attr_t			attr;
	NTSTATUS				status;
	ci_umv_buf_t			verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_SRQ_ATTRIBUTES),
										   &pinAttr, &inlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_SRQ_ATTRIBUTES),
											&poutAttr, &outlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	srq = WvSrqAcquire(pProvider, pinAttr->Id.Id);
	if (srq == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	WvInitVerbsData(&verbsData, pinAttr->Id.VerbInfo, inlen - sizeof(WV_IO_SRQ_ATTRIBUTES),
					outlen - sizeof(WV_IO_SRQ_ATTRIBUTES), pinAttr + 1);
	srq->pVerbs->query_srq(srq->hVerbsSrq, &attr, &verbsData);
	WvSrqRelease(srq);

	poutAttr->Id.VerbInfo = verbsData.status;
	poutAttr->MaxWr = attr.max_wr;
	poutAttr->MaxSge = attr.max_sge;
	poutAttr->SrqLimit = attr.srq_limit;
	len = outlen;
complete:
	WdfRequestCompleteWithInformation(Request, status, len);
}

void WvSrqNotify(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64					*id;
	WV_SHARED_RECEIVE_QUEUE	*srq;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	srq = WvSrqAcquire(pProvider, *id);
	if (srq == NULL) {
		status = STATUS_NOT_FOUND;
		goto out;
	}

	WdfObjectAcquireLock(srq->Queue);
	status = WdfRequestForwardToIoQueue(Request, srq->Queue);
	WdfObjectReleaseLock(srq->Queue);
	WvSrqRelease(srq);

out:
	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
	}
}

void WvSrqCancel(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64					*id;
	WV_SHARED_RECEIVE_QUEUE	*srq;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	srq = WvSrqAcquire(pProvider, *id);
	if (srq == NULL) {
		status = STATUS_NOT_FOUND;
		goto out;
	}

	WvFlushQueue(srq->Queue, STATUS_CANCELLED);
	WvSrqRelease(srq);

out:
	WdfRequestComplete(Request, status);
}
