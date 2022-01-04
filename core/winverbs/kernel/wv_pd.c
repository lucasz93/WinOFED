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

#include "wv_pd.h"
#include "wv_srq.h"
#include "wv_qp.h"
#include "wv_ioctl.h"

void WvPdGet(WV_PROTECTION_DOMAIN *pPd)
{
	InterlockedIncrement(&pPd->Ref);
}

void WvPdPut(WV_PROTECTION_DOMAIN *pPd)
{
	if (InterlockedDecrement(&pPd->Ref) == 0) {
		KeSetEvent(&pPd->Event, 0, FALSE);
	}
}

WV_PROTECTION_DOMAIN *WvPdAcquire(WV_PROVIDER *pProvider, UINT64 Id)
{
	WV_PROTECTION_DOMAIN *pd;

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	pd = IndexListAt(&pProvider->PdIndex, (SIZE_T) Id);
	if (pd != NULL && pd->hVerbsPd != NULL) {
		WvPdGet(pd);
	} else {
		pd = NULL;
		WvProviderEnableRemove(pProvider);
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	return pd;
}

void WvPdRelease(WV_PROTECTION_DOMAIN *pPd)
{
	WvProviderEnableRemove(pPd->pDevice->pProvider);
	WvPdPut(pPd);
}

static NTSTATUS WvPdAlloc(WV_DEVICE *pDevice, WV_PROTECTION_DOMAIN **ppPd,
						  ci_umv_buf_t *pVerbsData)
{
	ib_api_status_t			ib_status;
	WV_PROTECTION_DOMAIN	*pd;

	pd = ExAllocatePoolWithTag(NonPagedPool, sizeof(WV_PROTECTION_DOMAIN), 'dpvw');
	if (pd == NULL) {
		return STATUS_NO_MEMORY;
	}

	pd->Ref = 1;
	KeInitializeEvent(&pd->Event, NotificationEvent, FALSE);
	InitializeListHead(&pd->QpList);
	InitializeListHead(&pd->SrqList);
	InitializeListHead(&pd->MwList);
	InitializeListHead(&pd->AhList);
	cl_qmap_init(&pd->MrMap);
	KeInitializeGuardedMutex(&pd->Lock);

	ib_status = pDevice->pVerbs->allocate_pd(pDevice->hVerbsDevice, IB_PDT_NORMAL,
											 &pd->hVerbsPd, pVerbsData);
	if (ib_status != IB_SUCCESS) {
		goto err;
	}

	WvDeviceGet(pDevice);
	pd->pDevice = pDevice;
	pd->pVerbs = pDevice->pVerbs;
	*ppPd = pd;
	return STATUS_SUCCESS;

err:
	ExFreePoolWithTag(pd, 'dpvw');
	return STATUS_UNSUCCESSFUL;
}

void WvPdAllocate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID				*inid, *outid;
	size_t					inlen, outlen;
	WV_DEVICE				*dev;
	WV_PROTECTION_DOMAIN	*pd;
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
	status = WvPdAlloc(dev, &pd, &verbsData);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	outid->Id = IndexListInsertHead(&pProvider->PdIndex, pd);
	if (outid->Id == 0) {
		status = STATUS_NO_MEMORY;
		goto err3;
	}
	InsertHeadList(&dev->PdList, &pd->Entry);
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvDeviceRelease(dev);
	outid->VerbInfo = verbsData.status;
	WdfRequestCompleteWithInformation(Request, status, outlen);
	return;

err3:
	KeReleaseGuardedMutex(&pProvider->Lock);
	WvPdFree(pd);
err2:
	WvDeviceRelease(dev);
err1:
	WdfRequestComplete(Request, status);
}

void WvPdDeallocate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_PROTECTION_DOMAIN	*pd;
	UINT64					*id;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	pd = IndexListAt(&pProvider->PdIndex, (SIZE_T) *id);
	if (pd == NULL) {
		status = STATUS_NOT_FOUND;
	} else if (pd->Ref > 1) {
		status = STATUS_ACCESS_DENIED;
	} else {
		IndexListRemove(&pProvider->PdIndex, (SIZE_T) *id);
		RemoveEntryList(&pd->Entry);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WvPdFree(pd);
	}
	WvProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

void WvPdCancel(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_PROTECTION_DOMAIN	*pd;
	UINT64					*id;
	NTSTATUS				status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	pd = WvPdAcquire(pProvider, *id);
	if (pd == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	// Registration is currently synchronous - nothing to do.
	WvPdRelease(pd);

complete:
	WdfRequestComplete(Request, status);
}

void WvPdFree(WV_PROTECTION_DOMAIN *pPd)
{
	WV_MEMORY_REGION	*mr;
	cl_map_item_t		*item;

	if (InterlockedDecrement(&pPd->Ref) > 0) {
		KeWaitForSingleObject(&pPd->Event, Executive, KernelMode, FALSE, NULL);
	}

	for (item = cl_qmap_head(&pPd->MrMap); item != cl_qmap_end(&pPd->MrMap);
		 item = cl_qmap_head(&pPd->MrMap)) {
		mr = CONTAINING_RECORD(item, WV_MEMORY_REGION, Item);
		if (mr->hVerbsMr != NULL) {
			pPd->pVerbs->deregister_mr(mr->hVerbsMr);
		}

		cl_qmap_remove_item(&pPd->MrMap, &mr->Item);
		ExFreePoolWithTag(mr, 'rmvw');
	}

	if (pPd->hVerbsPd != NULL) {
		pPd->pVerbs->deallocate_pd(pPd->hVerbsPd);
	}

	WvDevicePut(pPd->pDevice);
	ExFreePoolWithTag(pPd, 'dpvw');
}

void WvPdRemoveHandler(WV_PROTECTION_DOMAIN *pPd)
{
	WV_QUEUE_PAIR			*qp;
	WV_SHARED_RECEIVE_QUEUE	*srq;
	WV_MEMORY_REGION		*mr;
	WV_ADDRESS_HANDLE		*ah;
	WV_MEMORY_WINDOW		*mw;
	cl_map_item_t			*item;
	LIST_ENTRY				*entry;

	for (entry = pPd->MwList.Flink; entry != &pPd->MwList; entry = entry->Flink) {
		mw = CONTAINING_RECORD(entry, WV_MEMORY_WINDOW, Entry);
		pPd->pVerbs->destroy_mw(mw->hVerbsMw);
		mw->hVerbsMw = NULL;
	}

	for (entry = pPd->AhList.Flink; entry != &pPd->AhList; entry = entry->Flink) {
		ah = CONTAINING_RECORD(entry, WV_ADDRESS_HANDLE, Entry);
		pPd->pVerbs->destroy_av(ah->hVerbsAh);
		ah->hVerbsAh = NULL;
	}

	for (item = cl_qmap_head(&pPd->MrMap); item != cl_qmap_end(&pPd->MrMap);
		 item = cl_qmap_next(item)) {
		mr = CONTAINING_RECORD(item, WV_MEMORY_REGION, Item);
		pPd->pVerbs->deregister_mr(mr->hVerbsMr);
		mr->hVerbsMr = NULL;
	}

	for (entry = pPd->QpList.Flink; entry != &pPd->QpList; entry = entry->Flink) {
		qp = CONTAINING_RECORD(entry, WV_QUEUE_PAIR, Entry);
		WvQpRemoveHandler(qp);
	}

	for (entry = pPd->SrqList.Flink; entry != &pPd->SrqList; entry = entry->Flink) {
		srq = CONTAINING_RECORD(entry, WV_SHARED_RECEIVE_QUEUE, Entry);
		pPd->pVerbs->destroy_srq(srq->hVerbsSrq);
		srq->hVerbsSrq = NULL;
		srq->pVerbs = NULL;
	}

	pPd->pVerbs->deallocate_pd(pPd->hVerbsPd);
	pPd->pVerbs = NULL;
	pPd->hVerbsPd = NULL;
}

void WvMrRegister(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_MEMORY_REGISTER	*reg;
	WV_IO_MEMORY_KEYS		*keys;
	WV_PROTECTION_DOMAIN	*pd;
	WV_MEMORY_REGION		*mr;
	ib_mr_create_t			attr;
	NTSTATUS				status;
	ib_api_status_t			ib_status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_MEMORY_REGISTER),
										   &reg, NULL);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_MEMORY_KEYS),
											&keys, NULL);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	pd = WvPdAcquire(pProvider, reg->Id);
	if (pd == NULL) {
		status = STATUS_NOT_FOUND;
		goto err1;
	}

	mr = ExAllocatePoolWithTag(PagedPool, sizeof(WV_MEMORY_REGION), 'rmvw');
	if (mr == NULL) {
		status = STATUS_NO_MEMORY;
		goto err2;
	}

	attr.access_ctrl = reg->AccessFlags;
	attr.length = reg->BufferLength;
	attr.vaddr = (void *) (ULONG_PTR) reg->Address;
	ib_status = pd->pVerbs->register_mr(pd->hVerbsPd, &attr, &keys->Lkey,
										&keys->Rkey, &mr->hVerbsMr, TRUE);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err3;
	}

	KeAcquireGuardedMutex(&pd->Lock);
	cl_qmap_insert(&pd->MrMap, keys->Lkey, &mr->Item);
	KeReleaseGuardedMutex(&pd->Lock);

	WvPdRelease(pd);
	WdfRequestCompleteWithInformation(Request, status, sizeof(WV_IO_MEMORY_KEYS));
	return;

err3:
	ExFreePoolWithTag(mr, 'rmvw');
err2:
	WvPdRelease(pd);
err1:
	WdfRequestComplete(Request, status);
}

void WvMrDeregister(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID				*id;
	WV_PROTECTION_DOMAIN	*pd;
	WV_MEMORY_REGION		*mr;
	cl_map_item_t			*item;
	NTSTATUS				status;
	ib_api_status_t			ib_status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	pd = WvPdAcquire(pProvider, id->Id);
	if (pd == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	KeAcquireGuardedMutex(&pd->Lock);
	item = cl_qmap_remove(&pd->MrMap, id->Data);
	KeReleaseGuardedMutex(&pd->Lock);

	if (item == cl_qmap_end(&pd->MrMap)) {
		status = STATUS_NOT_FOUND;
		goto release;
	}

	mr = CONTAINING_RECORD(item, WV_MEMORY_REGION, Item);

	ib_status = pd->pVerbs->deregister_mr(mr->hVerbsMr);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		KeAcquireGuardedMutex(&pd->Lock);
		cl_qmap_insert(&pd->MrMap, id->Data, &mr->Item);
		KeReleaseGuardedMutex(&pd->Lock);
		goto release;
	}

	ExFreePoolWithTag(mr, 'rmvw');
release:
	WvPdRelease(pd);
complete:
	WdfRequestComplete(Request, status);
}

void WvMwAllocate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID				*inid, *outid;
	size_t					inlen, outlen;
	WV_PROTECTION_DOMAIN	*pd;
	WV_MEMORY_WINDOW		*mw;
	NTSTATUS				status;
	ib_api_status_t			ib_status;
	ci_umv_buf_t			verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &inid, &inlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_ID), &outid, &outlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	pd = WvPdAcquire(pProvider, inid->Id);
	if (pd == NULL) {
		status = STATUS_NOT_FOUND;
		goto err1;
	}

    #pragma prefast(suppress:28197, "The memory is freed on WvMwDeallocate function");
	mw = ExAllocatePoolWithTag(PagedPool, sizeof(WV_MEMORY_WINDOW), 'wmvw');
	if (mw == NULL) {
		status = STATUS_NO_MEMORY;
		goto err2;
	}

	WvPdGet(pd);
	mw->pPd = pd;

	WvInitVerbsData(&verbsData, inid->VerbInfo, inlen - sizeof(WV_IO_ID),
					outlen - sizeof(WV_IO_ID), inid + 1);
	ib_status = pd->pVerbs->create_mw(pd->hVerbsPd, &outid->Data, &mw->hVerbsMw,
									  &verbsData);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err3;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	outid->Id = IndexListInsertHead(&pProvider->MwIndex, mw);
	if (outid->Id == 0) {
		status = STATUS_NO_MEMORY;
		goto err4;
	}
	InsertHeadList(&pd->MwList, &mw->Entry);
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvPdRelease(pd);
	outid->VerbInfo = verbsData.status;
	WdfRequestCompleteWithInformation(Request, status, outlen);
	return;

err4:
	KeReleaseGuardedMutex(&pProvider->Lock);
err3:
	WvMwFree(mw);
err2:
	WvPdRelease(pd);
err1:
	WdfRequestComplete(Request, status);
}

void WvMwDeallocate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_MEMORY_WINDOW	*mw;
	UINT64				*id;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	mw = IndexListAt(&pProvider->MwIndex, (SIZE_T) *id);
	if (mw == NULL) {
		status = STATUS_NOT_FOUND;
	} else {
		IndexListRemove(&pProvider->MwIndex, (SIZE_T) *id);
		RemoveEntryList(&mw->Entry);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WvMwFree(mw);
	}
	WvProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

void WvMwFree(WV_MEMORY_WINDOW *pMw)
{
	if (pMw->hVerbsMw != NULL) {
		pMw->pPd->pVerbs->destroy_mw(pMw->hVerbsMw);
	}

	WvPdPut(pMw->pPd);
	ExFreePoolWithTag(pMw, 'wmvw');
}

static void WvVerbsConvertAv(ib_av_attr_t *pVerbsAv, WV_IO_AV *pAv)
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
}

void WvAhCreate(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	size_t					inlen, outlen;
	WV_PROTECTION_DOMAIN	*pd;
	WV_ADDRESS_HANDLE		*ah;
	WV_IO_AH_CREATE			*pinAv, *poutAv;
	ib_av_attr_t			av;
	NTSTATUS				status;
	ib_api_status_t			ib_status;
	ci_umv_buf_t			verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_AH_CREATE),
										   &pinAv, &inlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_AH_CREATE),
											&poutAv, &outlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	pd = WvPdAcquire(pProvider, pinAv->Id.Id);
	if (pd == NULL) {
		status = STATUS_NOT_FOUND;
		goto err1;
	}

	#pragma prefast(suppress:28197, "The memory is freed on WvAhFree function");
	ah = ExAllocatePoolWithTag(PagedPool, sizeof(WV_ADDRESS_HANDLE), 'havw');
	if (ah == NULL) {
		status = STATUS_NO_MEMORY;
		goto err2;
	}

	WvPdGet(pd);
	ah->pPd = pd;

	WvVerbsConvertAv(&av, &pinAv->AddressVector);
	WvInitVerbsData(&verbsData, pinAv->Id.VerbInfo, inlen - sizeof(WV_IO_AH_CREATE),
					outlen - sizeof(WV_IO_AH_CREATE), pinAv + 1);
	ib_status = pd->pVerbs->create_av(pd->hVerbsPd, &av, &ah->hVerbsAh, &verbsData);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err3;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	poutAv->Id.Id = IndexListInsertHead(&pProvider->AhIndex, ah);
	if (poutAv->Id.Id == 0) {
		status = STATUS_NO_MEMORY;
		goto err4;
	}
	InsertHeadList(&pd->AhList, &ah->Entry);
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvPdRelease(pd);
	poutAv->Id.VerbInfo = verbsData.status;
	WdfRequestCompleteWithInformation(Request, status, outlen);
	return;

err4:
	KeReleaseGuardedMutex(&pProvider->Lock);
err3:
	WvAhFree(ah);
err2:
	WvPdRelease(pd);
err1:
	WdfRequestComplete(Request, status);
}

void WvAhDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_ADDRESS_HANDLE	*ah;
	UINT64				*id;
	NTSTATUS			status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	ah = IndexListAt(&pProvider->AhIndex, (SIZE_T) *id);
	if (ah == NULL) {
		status = STATUS_NOT_FOUND;
	} else {
		IndexListRemove(&pProvider->AhIndex, (SIZE_T) *id);
		RemoveEntryList(&ah->Entry);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WvAhFree(ah);
	}
	WvProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

void WvAhFree(WV_ADDRESS_HANDLE *pAh)
{
	if (pAh->hVerbsAh != NULL) {
		pAh->pPd->pVerbs->destroy_av(pAh->hVerbsAh);
	}

	WvPdPut(pAh->pPd);
	ExFreePoolWithTag(pAh, 'havw');
}
