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
#include "wv_cq.h"
#include "wv_ioctl.h"

void WvCqGet(WV_COMPLETION_QUEUE *pCq)
{
	InterlockedIncrement(&pCq->Ref);
}

void WvCqPut(WV_COMPLETION_QUEUE *pCq)
{
	if (InterlockedDecrement(&pCq->Ref) == 0) {
		KeSetEvent(&pCq->Event, 0, FALSE);
	}
}

WV_COMPLETION_QUEUE *WvCqAcquire(WV_PROVIDER *pProvider, UINT64 Id)
{
	WV_COMPLETION_QUEUE *cq;

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	cq = IndexListAt(&pProvider->CqIndex, (SIZE_T) Id);
	if (cq != NULL && cq->hVerbsCq != NULL) {
		WvCqGet(cq);
	} else {
		cq = NULL;
		WvProviderEnableRemove(pProvider);
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	return cq;
}

void WvCqRelease(WV_COMPLETION_QUEUE *pCq)
{
	WvProviderEnableRemove(pCq->pDevice->pProvider);
	WvCqPut(pCq);
}

static void WvCqEventHandler(ib_event_rec_t *pEvent)
{
	WV_COMPLETION_QUEUE	*cq = pEvent->context;
	WvFlushQueue(cq->Queue, STATUS_UNEXPECTED_IO_ERROR);
	WvFlushQueue(cq->ErrorQueue, STATUS_UNEXPECTED_IO_ERROR);
}

static void WvCqHandler(void *Context)
{
	WV_COMPLETION_QUEUE	*cq = Context;

	WvFlushQueue(cq->Queue, STATUS_SUCCESS);
}

static NTSTATUS WvCqAlloc(WV_DEVICE *pDevice, UINT32 *pSize,
						  WV_COMPLETION_QUEUE **ppCq, ci_umv_buf_t *pVerbsData)
{
	ib_api_status_t		ib_status;
	WV_COMPLETION_QUEUE	*cq;
	WDF_IO_QUEUE_CONFIG	config;
	NTSTATUS			status;

	cq = ExAllocatePoolWithTag(NonPagedPool, sizeof(WV_COMPLETION_QUEUE), 'qcvw');
	if (cq == NULL) {
		return STATUS_NO_MEMORY;
	}

	cq->Ref = 1;
	KeInitializeEvent(&cq->Event, NotificationEvent, FALSE);

	ASSERT(ControlDevice != NULL);

	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
	status = WdfIoQueueCreate(ControlDevice, &config,
							  WDF_NO_OBJECT_ATTRIBUTES, &cq->Queue);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	status = WdfIoQueueCreate(ControlDevice, &config,
							  WDF_NO_OBJECT_ATTRIBUTES, &cq->ErrorQueue);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	ib_status = pDevice->pVerbs->create_cq(pDevice->hVerbsDevice, cq,
										   WvCqEventHandler, WvCqHandler,
										   pSize, &cq->hVerbsCq, pVerbsData);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err3;
	}

	cq->pDevice = pDevice;
	cq->pVerbs = pDevice->pVerbs;
	*ppCq = cq;
	return STATUS_SUCCESS;

err3:
	WdfObjectDelete(cq->ErrorQueue);
err2:
	WdfObjectDelete(cq->Queue);
err1:
	ExFreePoolWithTag(cq, 'qcvw');
	return status;
}

void WvCqCreate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID				*inid, *outid;
	size_t					inlen, outlen;
	WV_DEVICE				*dev;
	WV_COMPLETION_QUEUE		*cq;
	NTSTATUS				status;
	ci_umv_buf_t			verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &inid, &inlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_ID), &outid, &outlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	dev = WvDeviceAcquire(pProvider, inid->Id);
	if (dev == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto err1;
	}

	WvInitVerbsData(&verbsData, inid->VerbInfo, inlen - sizeof(WV_IO_ID),
					outlen - sizeof(WV_IO_ID), inid + 1);
	status = WvCqAlloc(dev, &inid->Data, &cq, &verbsData);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	outid->Id = IndexListInsertHead(&pProvider->CqIndex, cq);
	if (outid->Id == 0) {
		status = STATUS_NO_MEMORY;
		goto err3;
	}
	InsertHeadList(&dev->CqList, &cq->Entry);
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvProviderEnableRemove(pProvider);
	outid->Data = inid->Data;
	outid->VerbInfo = verbsData.status;
	WdfRequestCompleteWithInformation(Request, status, outlen);
	return;

err3:
	KeReleaseGuardedMutex(&pProvider->Lock);
	WvCqFree(cq);
err2:
	WvDeviceRelease(dev);
err1:
	WdfRequestComplete(Request, status);
}

void WvCqDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_COMPLETION_QUEUE		*cq;
	UINT64					*id;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	cq = IndexListAt(&pProvider->CqIndex, (SIZE_T) *id);
	if (cq == NULL) {
		status = STATUS_NOT_FOUND;
	} else if (cq->Ref > 1) {
		status = STATUS_ACCESS_DENIED;
	} else {
		IndexListRemove(&pProvider->CqIndex, (SIZE_T) *id);
		RemoveEntryList(&cq->Entry);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WvCqFree(cq);
	}
	WvProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

void WvCqFree(WV_COMPLETION_QUEUE *pCq)
{
	if (InterlockedDecrement(&pCq->Ref) > 0) {
		KeWaitForSingleObject(&pCq->Event, Executive, KernelMode, FALSE, NULL);
	}

	if (pCq->hVerbsCq != NULL) {
		pCq->pVerbs->destroy_cq(pCq->hVerbsCq);
	}

	WdfIoQueuePurgeSynchronously(pCq->Queue);
	WdfIoQueuePurgeSynchronously(pCq->ErrorQueue);
	WdfObjectDelete(pCq->Queue);
	WdfObjectDelete(pCq->ErrorQueue);
	WvDevicePut(pCq->pDevice);
	ExFreePoolWithTag(pCq, 'qcvw');
}

void WvCqResize(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID				*inid, *outid;
	size_t					inlen, outlen, len = 0;
	WV_COMPLETION_QUEUE		*cq;
	NTSTATUS				status;
	ib_api_status_t			ib_status;
	ci_umv_buf_t			verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &inid, &inlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_ID), &outid, &outlen);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	cq = WvCqAcquire(pProvider, inid->Id);
	if (cq == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	WvInitVerbsData(&verbsData, inid->VerbInfo, inlen - sizeof(WV_IO_ID),
					outlen - sizeof(WV_IO_ID), inid + 1);
	ib_status = cq->pVerbs->resize_cq(cq->hVerbsCq, &inid->Data, &verbsData);
	WvCqRelease(cq);

	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
	}

	len = outlen;
	outid->Data = inid->Data;
	outid->VerbInfo = verbsData.status;

complete:
	WdfRequestCompleteWithInformation(Request, status, len);
}

void WvCqNotify(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID				*id;
	WV_COMPLETION_QUEUE		*cq;
	NTSTATUS				status;
	WDFQUEUE				queue;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	cq = WvCqAcquire(pProvider, id->Id);
	if (cq == NULL) {
		status = STATUS_NOT_FOUND;
		goto out;
	}

	queue = (id->Data == WV_CQ_ERROR) ? cq->ErrorQueue : cq->Queue;
	WdfObjectAcquireLock(queue);
	status = WdfRequestForwardToIoQueue(Request, queue);
	WdfObjectReleaseLock(queue);
	WvCqRelease(cq);

out:
	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
	}
}

void WvCqCancel(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64					*id;
	WV_COMPLETION_QUEUE		*cq;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	cq = WvCqAcquire(pProvider, *id);
	if (cq == NULL) {
		status = STATUS_NOT_FOUND;
		goto out;
	}

	WvFlushQueue(cq->Queue, STATUS_CANCELLED);
	WvFlushQueue(cq->ErrorQueue, STATUS_CANCELLED);
	WvCqRelease(cq);

out:
	WdfRequestComplete(Request, status);
}
