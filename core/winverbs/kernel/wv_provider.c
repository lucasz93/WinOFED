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

#include <ntstatus.h>

#include "work_queue.c"
#include <rdma\wvstatus.h>
#include "wv_driver.h"
#include "wv_ioctl.h"
#include "wv_provider.h"
#include "wv_device.h"
#include "wv_pd.h"
#include "wv_srq.h"
#include "wv_cq.h"
#include "wv_qp.h"
#include "wv_ep.h"

LONG WvProviderGet(WV_PROVIDER *pProvider)
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

void WvProviderPut(WV_PROVIDER *pProvider)
{
	if (InterlockedDecrement(&pProvider->Ref) == 0) {
		KeSetEvent(&pProvider->Event, 0, FALSE);
	}
}

// See comment above WvProviderRemoveHandler.
static void WvProviderLockRemove(WV_PROVIDER *pProvider)
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

// See comment above WvProviderRemoveHandler.
static void WvProviderUnlockRemove(WV_PROVIDER *pProvider)
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

NTSTATUS WvProviderInit(WDFDEVICE Device, WV_PROVIDER *pProvider)
{
	NTSTATUS status;

	status = WorkQueueInit(&pProvider->WorkQueue, WdfDeviceWdmGetDeviceObject(Device), 0);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	IndexListInit(&pProvider->DevIndex);
	IndexListInit(&pProvider->CqIndex);
	IndexListInit(&pProvider->PdIndex);
	IndexListInit(&pProvider->SrqIndex);
	IndexListInit(&pProvider->QpIndex);
	IndexListInit(&pProvider->MwIndex);
	IndexListInit(&pProvider->AhIndex);
	IndexListInit(&pProvider->EpIndex);

	KeInitializeGuardedMutex(&pProvider->Lock);
	pProvider->Ref = 1;
	KeInitializeEvent(&pProvider->Event, NotificationEvent, FALSE);

	pProvider->Pending = 0;
	pProvider->Active = 0;
	KeInitializeEvent(&pProvider->SharedEvent, NotificationEvent, FALSE);
	pProvider->Exclusive = 0;
	KeInitializeEvent(&pProvider->ExclusiveEvent, SynchronizationEvent, FALSE);
	KeInitializeSpinLock(&pProvider->SpinLock);
	return STATUS_SUCCESS;
}

/*
 * We could be processing asynchronous requests or have them queued
 * when cleaning up.  To synchronize with async request processing,
 * acquire the provider lock with exclusive access until we're done
 * destroying all resoureces.  This will allow active requests to
 * complete their processing, but prevent queued requests from
 * running until cleanup is done.  At that point, queue requests will
 * be unable to acquire any winverbs resources.
 */

void WvProviderCleanup(WV_PROVIDER *pProvider)
{
	WV_DEVICE				*dev;
	WV_COMPLETION_QUEUE		*cq;
	WV_PROTECTION_DOMAIN	*pd;
	WV_SHARED_RECEIVE_QUEUE	*srq;
	WV_QUEUE_PAIR			*qp;
	WV_MEMORY_WINDOW		*mw;
	WV_ADDRESS_HANDLE		*ah;
	WV_ENDPOINT				*ep;
	SIZE_T					i;

	WvProviderLockRemove(pProvider);
	IndexListForEach(&pProvider->EpIndex, i) {
		ep = (WV_ENDPOINT *) IndexListAt(&pProvider->EpIndex, i);
		if (ep->State == WvEpListening) {
			WvEpCancelListen(ep);
		}
	}
	while ((ep = IndexListRemoveHead(&pProvider->EpIndex)) != NULL) {
		WvEpFree(ep);
	}
	while ((ah = IndexListRemoveHead(&pProvider->AhIndex)) != NULL) {
		RemoveEntryList(&ah->Entry);
		WvAhFree(ah);
	}
	while ((mw = IndexListRemoveHead(&pProvider->MwIndex)) != NULL) {
		RemoveEntryList(&mw->Entry);
		WvMwFree(mw);
	}
	while ((qp = IndexListRemoveHead(&pProvider->QpIndex)) != NULL) {
		RemoveEntryList(&qp->Entry);
		WvQpFree(qp);
	}
	while ((srq = IndexListRemoveHead(&pProvider->SrqIndex)) != NULL) {
		RemoveEntryList(&srq->Entry);
		WvSrqFree(srq);
	}
	while ((pd = IndexListRemoveHead(&pProvider->PdIndex)) != NULL) {
		RemoveEntryList(&pd->Entry);
		WvPdFree(pd);
	}
	while ((cq = IndexListRemoveHead(&pProvider->CqIndex)) != NULL) {
		RemoveEntryList(&cq->Entry);
		WvCqFree(cq);
	}
	while ((dev = IndexListRemoveHead(&pProvider->DevIndex)) != NULL) {
		WvDeviceFree(dev);
	}
	WvProviderUnlockRemove(pProvider);

	if (InterlockedDecrement(&pProvider->Ref) > 0) {
		KeWaitForSingleObject(&pProvider->Event, Executive, KernelMode, FALSE, NULL);
	}

	IndexListDestroy(&pProvider->EpIndex);
	IndexListDestroy(&pProvider->AhIndex);
	IndexListDestroy(&pProvider->MwIndex);
	IndexListDestroy(&pProvider->QpIndex);
	IndexListDestroy(&pProvider->SrqIndex);
	IndexListDestroy(&pProvider->PdIndex);
	IndexListDestroy(&pProvider->CqIndex);
	IndexListDestroy(&pProvider->DevIndex);
	WorkQueueDestroy(&pProvider->WorkQueue);
}

/*
 * Must hold pProvider->Lock.  Function may release and re-acquire.
 * See comment above WvProviderRemoveHandler.
 */
void WvProviderDisableRemove(WV_PROVIDER *pProvider)
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
 * See comment above WvProviderRemoveHandler.
 */
void WvProviderEnableRemove(WV_PROVIDER *pProvider)
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
void WvProviderRemoveHandler(WV_PROVIDER *pProvider, WV_RDMA_DEVICE *pDevice)
{
	WV_DEVICE *dev;
	SIZE_T i;

	WvProviderLockRemove(pProvider);
	IndexListForEach(&pProvider->DevIndex, i) {
		dev = IndexListAt(&pProvider->DevIndex, i);
		if (dev->pDevice == pDevice) {
			WvDeviceRemoveHandler(dev);
		}
	}
	WvProviderUnlockRemove(pProvider);
}
