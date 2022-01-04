/*
 * Copyright (c) 2008-2010 Intel Corporation. All rights reserved.
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

#include "index_list.c"
#include "wm_driver.h"
#include "wm_ioctl.h"
#include "wm_provider.h"
#include "wm_reg.h"

LONG WmProviderGet(WM_PROVIDER *pProvider)
{
	LONG val;
	KIRQL irql;

	KeAcquireSpinLock(&pProvider->SpinLock, &irql);
	val = InterlockedIncrement(&pProvider->Ref);
	if (val == 1) {
		pProvider->Ref = 0;
		val = 0;
	}
	KeReleaseSpinLock(&pProvider->SpinLock, irql);
	return val;
}

void WmProviderPut(WM_PROVIDER *pProvider)
{
	if (InterlockedDecrement(&pProvider->Ref) == 0) {
		KeSetEvent(&pProvider->Event, 0, FALSE);
	}
}

NTSTATUS WmProviderInit(WM_PROVIDER *pProvider)
{
	WDF_IO_QUEUE_CONFIG	config;
	NTSTATUS status;

	IndexListInit(&pProvider->RegIndex);
	pProvider->MadHead = NULL;
	pProvider->MadTail = NULL;

	KeInitializeGuardedMutex(&pProvider->Lock);
	pProvider->Ref = 1;
	KeInitializeEvent(&pProvider->Event, NotificationEvent, FALSE);

	pProvider->Pending = 0;
	pProvider->Active = 0;
	KeInitializeEvent(&pProvider->SharedEvent, NotificationEvent, FALSE);
	pProvider->Exclusive = 0;
	KeInitializeEvent(&pProvider->ExclusiveEvent, SynchronizationEvent, FALSE);
	KeInitializeSpinLock(&pProvider->SpinLock);

	ASSERT(ControlDevice != NULL);
	
	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
	status = WdfIoQueueCreate(ControlDevice, &config,
							  WDF_NO_OBJECT_ATTRIBUTES, &pProvider->ReadQueue);
	return status;
}

static void WmInsertMad(WM_PROVIDER *pProvider, ib_mad_element_t *pMad)
{
	if (pProvider->MadHead == NULL) {
		pProvider->MadHead = pMad;
	} else {
		pProvider->MadTail->p_next = pMad;
	}
	pProvider->MadTail = pMad;
}

static ib_mad_element_t *WmRemoveMad(WM_PROVIDER *pProvider)
{
	ib_mad_element_t *mad;

	mad = pProvider->MadHead;
	if (mad != NULL) {
		pProvider->MadHead = (ib_mad_element_t *) mad->p_next;
		mad->p_next = NULL;
	}
	return mad;
}

void WmProviderDeregister(WM_PROVIDER *pProvider, WM_REGISTRATION *pRegistration)
{
	ib_mad_element_t	*mad, *next, *list;

	WdfObjectAcquireLock(pProvider->ReadQueue);
	pRegistration->hService = NULL;

	list = pProvider->MadHead;
	pProvider->MadHead = NULL;

	for (mad = list; mad != NULL; mad = next) {
		next = mad->p_next;
		mad->p_next = NULL;

		if (mad->context1 == pRegistration) {
			pRegistration->pDevice->IbInterface.put_mad(__FILE__, __LINE__, mad);
		} else {
			WmInsertMad(pProvider, mad);
		}
	}
	WdfObjectReleaseLock(pProvider->ReadQueue);
}

void WmProviderCleanup(WM_PROVIDER *pProvider)
{
	WM_REGISTRATION		*reg;

	while ((reg = IndexListRemoveHead(&pProvider->RegIndex)) != NULL) {
		WmRegFree(reg);
	}

	if (InterlockedDecrement(&pProvider->Ref) > 0) {
		KeWaitForSingleObject(&pProvider->Event, Executive, KernelMode, FALSE, NULL);
	}

	WdfIoQueuePurgeSynchronously(pProvider->ReadQueue);
	WdfObjectDelete(pProvider->ReadQueue);

	IndexListDestroy(&pProvider->RegIndex);
}

// See comment above WmProviderRemoveHandler.
static void WmProviderLockRemove(WM_PROVIDER *pProvider)
{
	KeAcquireGuardedMutex(&pProvider->Lock);
	pProvider->Exclusive++;
	KeClearEvent(&pProvider->SharedEvent);
	while (pProvider->Active > 0) {
		KeReleaseGuardedMutex(&pProvider->Lock);
		KeWaitForSingleObject(&pProvider->ExclusiveEvent, Executive, KernelMode,
							  FALSE, NULL);
		KeAcquireGuardedMutex(&pProvider->Lock);
	}
	pProvider->Active++;
	KeReleaseGuardedMutex(&pProvider->Lock);
}

// See comment above WmProviderRemoveHandler.
static void WmProviderUnlockRemove(WM_PROVIDER *pProvider)
{
	KeAcquireGuardedMutex(&pProvider->Lock);
	pProvider->Exclusive--;
	pProvider->Active--;
	if (pProvider->Exclusive > 0) {
		KeSetEvent(&pProvider->ExclusiveEvent, 0, FALSE);
	} else if (pProvider->Pending > 0) {
		KeSetEvent(&pProvider->SharedEvent, 0, FALSE);
	}
	KeReleaseGuardedMutex(&pProvider->Lock);
}

/*
 * Must hold pProvider->Lock.  Function may release and re-acquire.
 * See comment above WmProviderRemoveHandler.
 */
void WmProviderDisableRemove(WM_PROVIDER *pProvider)
{
	while (pProvider->Exclusive > 0) {
		pProvider->Pending++;
		KeReleaseGuardedMutex(&pProvider->Lock);
		KeWaitForSingleObject(&pProvider->SharedEvent, Executive, KernelMode,
							  FALSE, NULL);
		KeAcquireGuardedMutex(&pProvider->Lock);
		pProvider->Pending--;
	}
	InterlockedIncrement(&pProvider->Active);
}

/*
 * No need to hold pProvider->Lock when releasing.
 * See comment above WmProviderRemoveHandler.
 */
void WmProviderEnableRemove(WM_PROVIDER *pProvider)
{
	InterlockedDecrement(&pProvider->Active);
	if (pProvider->Exclusive > 0) {
		KeSetEvent(&pProvider->ExclusiveEvent, 0, FALSE);
	}
}

/*
 * The remove handler blocks all other threads executing through this
 * provider until the remove has been processed.  Because device removal is
 * rare, we want a simple, optimized code path for all calls that access
 * the underlying hardware device, making use of any locks that we would
 * have to acquire anyway.  The locking for exclusive access can be
 * as ugly and slow as needed.
 */
void WmProviderRemoveHandler(WM_PROVIDER *pProvider, WM_IB_DEVICE *pDevice)
{
	WM_REGISTRATION *reg;
	SIZE_T i;

	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"REF_CNT_DBG: WmProviderRemoveHandler enter\n");

	WmProviderLockRemove(pProvider);
	IndexListForEach(&pProvider->RegIndex, i) {
		reg = IndexListAt(&pProvider->RegIndex, i);
		if (reg->pDevice == pDevice) {
			WmRegRemoveHandler(reg);
		}
		else {
			WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
				"REF_CNT_DBG: WmProviderRemoveHandler reg->pDevice != pDevice\n");
		}
	}
	WmProviderUnlockRemove(pProvider);

	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"REF_CNT_DBG: WmProviderRemoveHandler exit\n");
}

static NTSTATUS WmCopyMad(WM_IO_MAD *pIoMad, ib_mad_element_t *pMad, size_t *pLen)
{
	pIoMad->Id = ((WM_REGISTRATION *) pMad->context1)->Id;
	pIoMad->Status = pMad->status;
	pIoMad->Timeout = pMad->timeout_ms;
	pIoMad->Retries = pMad->retry_cnt;
	pIoMad->Length = pMad->size;

	pIoMad->Address.Qpn = pMad->remote_qp;
	pIoMad->Address.Qkey = pMad->remote_qkey;
	pIoMad->Address.PkeyIndex = pMad->pkey_index;

	if ((pIoMad->Address.GrhValid = (UINT8) pMad->grh_valid)) {
		pIoMad->Address.VersionClassFlow = pMad->p_grh->ver_class_flow;
		pIoMad->Address.HopLimit = pMad->p_grh->hop_limit;
		pIoMad->Address.GidIndex = 0;	// TODO: update IBAL to use SGID index
		RtlCopyMemory(pIoMad->Address.Gid, pMad->p_grh->dest_gid.raw, 16);
	}

	pIoMad->Address.Lid = pMad->remote_lid;
	pIoMad->Address.ServiceLevel = pMad->remote_sl;
	pIoMad->Address.PathBits = pMad->path_bits;
	pIoMad->Address.StaticRate = 0;
	pIoMad->Address.Reserved = 0;

	if (*pLen >= sizeof(WM_IO_MAD) + pMad->size) {
		RtlCopyMemory(pIoMad + 1, pMad->p_mad_buf, pMad->size);
		*pLen = sizeof(WM_IO_MAD) + pMad->size;
		return STATUS_SUCCESS;
	} else {
		*pLen = sizeof(WM_IO_MAD);
		return STATUS_MORE_ENTRIES;
	}
}

void WmProviderRead(WM_PROVIDER *pProvider, WDFREQUEST Request)
{
	WM_REGISTRATION		*reg;
	NTSTATUS			status;
	WM_IO_MAD			*wmad;
	size_t				outlen, len = 0;

	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WM_IO_MAD), &wmad, &outlen);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	WdfObjectAcquireLock(pProvider->ReadQueue);
	if (pProvider->MadHead == NULL) {
		status = WdfRequestForwardToIoQueue(Request, pProvider->ReadQueue);
		WdfObjectReleaseLock(pProvider->ReadQueue);
		if (NT_SUCCESS(status)) {
			return;
		}
		goto out;
	}

	len = outlen;
	status = WmCopyMad(wmad, pProvider->MadHead, &len);
	if (status == STATUS_SUCCESS) {
		reg = (WM_REGISTRATION *) pProvider->MadHead->context1;
		reg->pDevice->IbInterface.put_mad(__FILE__, __LINE__, WmRemoveMad(pProvider));
	}
	WdfObjectReleaseLock(pProvider->ReadQueue);

out:
	WdfRequestCompleteWithInformation(Request, status, len);
}

// If the Version is not set, use umad compatability address format
static void WmConvertGrh(ib_grh_t *pGrh, WM_IO_MAD *pIoMad)
{
	if (RtlUlongByteSwap(pIoMad->Address.VersionClassFlow) >> 28) {
		pGrh->ver_class_flow = pIoMad->Address.VersionClassFlow;
	} else {
		pGrh->ver_class_flow = RtlUlongByteSwap((6 << 28) |
			(((uint32_t) pIoMad->UmadAddress.TrafficClass) << 20) |
			(pIoMad->UmadAddress.FlowLabel & 0x000FFFFF));
	}

	pGrh->hop_limit = pIoMad->Address.HopLimit;
	// TODO: update IBAL to use SGID index
	// pGrh->src_gid_index = pIoMad->Address.GidIndex;
	RtlCopyMemory(pGrh->dest_gid.raw, pIoMad->Address.Gid, 16);
}

static NTSTATUS WmSendMad(WM_REGISTRATION *pRegistration, WM_IO_MAD *pIoMad, UINT32 size)
{
	ib_al_ifc_t			*pifc;
	NTSTATUS			status;
	ib_mad_element_t	*mad;
	ib_api_status_t		ib_status;

	pifc = &pRegistration->pDevice->IbInterface;
	ib_status = pifc->get_mad(__FILE__, __LINE__, pRegistration->hMadPool, size, &mad);
	if (ib_status != IB_SUCCESS) {
		return STATUS_NO_MEMORY;
	}

	RtlCopyMemory(mad->p_mad_buf, pIoMad + 1, size);
	mad->remote_qp = pIoMad->Address.Qpn;
	mad->remote_qkey = pIoMad->Address.Qkey;
	mad->resp_expected = (pIoMad->Timeout > 0);
	mad->timeout_ms = pIoMad->Timeout;
	mad->retry_cnt = pIoMad->Retries;

	if ((mad->grh_valid = pIoMad->Address.GrhValid)) {
		WmConvertGrh(mad->p_grh, pIoMad);
	}

	mad->remote_lid = pIoMad->Address.Lid;
	mad->remote_sl = pIoMad->Address.ServiceLevel;
	mad->pkey_index = pIoMad->Address.PkeyIndex;
	mad->path_bits = pIoMad->Address.PathBits;
	if (!ib_mad_is_response(mad->p_mad_buf)) {
		mad->p_mad_buf->trans_id &= 0xFFFFFFFF00000000;
	}

	ib_status = pifc->send_mad(pRegistration->hService, mad, NULL);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err;
	}

	return STATUS_SUCCESS;

err:
	pRegistration->pDevice->IbInterface.put_mad(__FILE__, __LINE__, mad);
	return status;
}

void WmProviderWrite(WM_PROVIDER *pProvider, WDFREQUEST Request)
{
	WM_REGISTRATION		*reg;
	NTSTATUS			status;
	WM_IO_MAD			*wmad;
	size_t				inlen;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WM_IO_MAD) + 24,
										   &wmad, &inlen);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	reg = WmRegAcquire(pProvider, wmad->Id);
	if (reg == NULL) {
		status = STATUS_NOT_FOUND;
		goto out;
	}

	status = WmSendMad(reg, wmad, (UINT32) (inlen - sizeof(WM_IO_MAD)));
	WmRegRelease(reg);

out:
	WdfRequestComplete(Request, status);
}

void WmReceiveHandler(ib_mad_svc_handle_t hService, void *Context,
					  ib_mad_element_t *pMad)
{
	WM_REGISTRATION	*reg = Context;
	WM_PROVIDER		*prov;
	WDFREQUEST		request;
	NTSTATUS		status;
	WM_IO_MAD		*wmad;
	size_t			len = 0;

	UNREFERENCED_PARAMETER(hService);
	prov = reg->pProvider;
	pMad->context1 = reg;

	WdfObjectAcquireLock(prov->ReadQueue);
	if (reg->hService == NULL) {
		reg->pDevice->IbInterface.put_mad(__FILE__, __LINE__, pMad);
		goto unlock;
	}
	
	status = WdfIoQueueRetrieveNextRequest(prov->ReadQueue, &request);
	if (!NT_SUCCESS(status)) {
		WmInsertMad(prov, pMad);
		goto unlock;
	}

	status = WdfRequestRetrieveOutputBuffer(request, sizeof(WM_IO_MAD), &wmad, &len);
	if (!NT_SUCCESS(status)) {
		reg->pDevice->IbInterface.put_mad(__FILE__, __LINE__, pMad);
		goto complete;
	}

	status = WmCopyMad(wmad, pMad, &len);
	if (status == STATUS_SUCCESS) {
		reg->pDevice->IbInterface.put_mad(__FILE__, __LINE__, pMad);
	} else {
		WmInsertMad(prov, pMad);
	}

complete:
	WdfRequestCompleteWithInformation(request, status, len);
unlock:
	WdfObjectReleaseLock(prov->ReadQueue);
}

void WmSendHandler(ib_mad_svc_handle_t hService, void *Context,
				   ib_mad_element_t *pMad)
{
	if (pMad->status == IB_WCS_SUCCESS) {
		((WM_REGISTRATION *) Context)->pDevice->IbInterface.put_mad(__FILE__, __LINE__, pMad);
	} else {
		pMad->status = WM_IO_TIMEOUT;
		WmReceiveHandler(hService, Context, pMad);
	}
}

void WmProviderCancel(WM_PROVIDER *pProvider, WDFREQUEST Request)
{
	WDFREQUEST	request;
	NTSTATUS	status, result;

	WdfObjectAcquireLock(pProvider->ReadQueue);
	status = WdfIoQueueRetrieveNextRequest(pProvider->ReadQueue, &request);
	result = status;

	while (NT_SUCCESS(status)) {
		WdfRequestComplete(request, STATUS_CANCELLED);
		status = WdfIoQueueRetrieveNextRequest(pProvider->ReadQueue, &request);
	}
	WdfObjectReleaseLock(pProvider->ReadQueue);

	WdfRequestComplete(Request, result);
}
