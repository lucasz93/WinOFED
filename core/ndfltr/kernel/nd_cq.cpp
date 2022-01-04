/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions (c) Microsoft Corporation.  All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "precomp.h"


void ND_COMPLETION_QUEUE::EventHandler(ib_event_rec_t *pEvent)
{
    ND_COMPLETION_QUEUE* cq = reinterpret_cast<ND_COMPLETION_QUEUE*>(pEvent->context);
    if (pEvent->type == IB_AE_CQ_OVERFLOW) {
        cq->m_Status = STATUS_DATA_OVERRUN;
    } else {
        cq->m_Status = STATUS_INTERNAL_ERROR;
    }
    NdFlushQueue(cq->m_Queue, cq->m_Status);
    NdFlushQueue(cq->m_ErrorQueue, cq->m_Status);
}


void ND_COMPLETION_QUEUE::CompletionHandler(void *Context)
{
    ND_COMPLETION_QUEUE* cq = reinterpret_cast<ND_COMPLETION_QUEUE*>(Context);
    NdFlushQueue(cq->m_Queue, STATUS_SUCCESS);
}


ND_COMPLETION_QUEUE::ND_COMPLETION_QUEUE(
    __in ND_ADAPTER* pAdapter,
    WDFQUEUE queue,
    WDFQUEUE errorQueue
    )
    : _Base(pAdapter, pAdapter->GetInterface()),
    m_Queue(queue),
    m_ErrorQueue(errorQueue),
    m_Status(STATUS_SUCCESS)
{
    pAdapter->AddCq(&m_Entry);
}


ND_COMPLETION_QUEUE::~ND_COMPLETION_QUEUE()
{
    _Base::RunDown();

    m_pParent->RemoveCq(&m_Entry);

    if (m_hIfc != NULL) {
        m_pIfc->destroy_cq(m_hIfc);
    }

    WdfIoQueuePurgeSynchronously(m_Queue);
    WdfIoQueuePurgeSynchronously(m_ErrorQueue);
    WdfObjectDelete(m_Queue);
    WdfObjectDelete(m_ErrorQueue);
}


NTSTATUS
ND_COMPLETION_QUEUE::Initialize(
    KPROCESSOR_MODE /*RequestorMode*/,
    ULONG QueueDepth,
    const GROUP_AFFINITY&,
    __inout ci_umv_buf_t* pVerbsData
    )
{
    //TODO: Use affinity settings.
    ib_api_status_t ibStatus = m_pIfc->create_cq(
        m_pParent->GetIfcHandle(),
        this,
        EventHandler,
        CompletionHandler,
        reinterpret_cast<uint32_t*>(&QueueDepth),
        &m_hIfc,
        pVerbsData
        );
    if (ibStatus != IB_SUCCESS) {
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}


//static
NTSTATUS
ND_COMPLETION_QUEUE::Create(
    __in ND_ADAPTER* pAdapter,
    KPROCESSOR_MODE RequestorMode,
    ULONG QueueDepth,
    const GROUP_AFFINITY& Affinity,
    __inout ci_umv_buf_t* pVerbsData,
    __out ND_COMPLETION_QUEUE** ppCq
    )
{
    //TODO: Should really use ExAllocatePoolWithQuotaTag here, since we're allocating this on
    // behalf of the user.
    ND_COMPLETION_QUEUE* cq = reinterpret_cast<ND_COMPLETION_QUEUE*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*cq), ND_COMPLETION_Q_POOL_TAG)
        );
    if (cq == NULL) {
        return STATUS_NO_MEMORY;
    }

    WDFQUEUE queue;
    WDF_IO_QUEUE_CONFIG config;
    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    NTSTATUS status = WdfIoQueueCreate(
        ControlDevice,
        &config,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    WDFQUEUE errorQueue;
    status = WdfIoQueueCreate(
        ControlDevice,
        &config,
        WDF_NO_OBJECT_ATTRIBUTES,
        &errorQueue
        );
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    new(cq) ND_COMPLETION_QUEUE(pAdapter, queue, errorQueue);
    status = cq->Initialize(RequestorMode, QueueDepth, Affinity, pVerbsData);
    if (!NT_SUCCESS(status)) {
        cq->Dispose();
        return status;
    }

    *ppCq = cq;
    return STATUS_SUCCESS;

err2:
    WdfObjectDelete(queue);
err1:
    ExFreePoolWithTag(cq, ND_COMPLETION_Q_POOL_TAG);
    return status;
}


void ND_COMPLETION_QUEUE::Dispose()
{
    this->ND_COMPLETION_QUEUE::~ND_COMPLETION_QUEUE();
    ExFreePoolWithTag(this, ND_COMPLETION_Q_POOL_TAG);
}


void ND_COMPLETION_QUEUE::RemoveHandler()
{
    if (m_hIfc != NULL) {
        m_pIfc->destroy_cq(m_hIfc);
    }
    _Base::RemoveHandler();
}


void
NdCqCreate(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_CREATE_CQ* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(

        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    //
    // We expect the vendor data to be in the same place in the input and
    // output buffers.  The input buffer is larger, so we use it as the
    // minimum length when querying the output buffer.
    //
    ND_RESOURCE_DESCRIPTOR* out;
    size_t outLen;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&out),
        &outLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    ci_umv_buf_t verbsData;
    NdInitVerbsData(&verbsData, inLen - sizeof(*in),
                    outLen - sizeof(*in), in + 1);

    pProvider->LockShared();

    ND_ADAPTER *adapter;
    status = pProvider->GetAdapter(in->AdapterHandle, &adapter);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_COMPLETION_QUEUE *cq;
    status = ND_COMPLETION_QUEUE::Create(
        adapter,
        WdfRequestGetRequestorMode(Request),
        in->QueueDepth,
        in->Affinity,
        &verbsData,
        &cq
        );

    if (!NT_SUCCESS(status)) {
        goto err3;
    }

    out->Handle = pProvider->AddCq(cq);
    if (out->Handle == 0) {
        cq->Dispose();
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        WdfRequestSetInformation(Request, outLen);
    }

err3:
    adapter->Release();
err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


void
NdCqFree(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    pProvider->LockShared();
    status = pProvider->FreeCq(in->Handle);
    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


void ND_COMPLETION_QUEUE::CancelIo()
{
    WdfIoQueuePurgeSynchronously(m_Queue);
    WdfIoQueuePurgeSynchronously(m_ErrorQueue);
    WdfIoQueueStart(m_Queue);
    WdfIoQueueStart(m_ErrorQueue);
}


void
NdCqCancelIo(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    pProvider->LockShared();
    ND_COMPLETION_QUEUE* cq;
    status = pProvider->GetCq(in->Handle, &cq);
    pProvider->Unlock();

    if (NT_SUCCESS(status)) {
        cq->CancelIo();
        cq->Release();
    }

err:
    WdfRequestComplete(Request, status);
}


void
NdCqGetAffinity(
    __in ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
}


NTSTATUS ND_COMPLETION_QUEUE::Modify(
    ULONG QueueDepth,
    __in ci_umv_buf_t* pVerbsData
    )
{
    ib_api_status_t ibStatus = m_pIfc->resize_cq(
        m_hIfc,
        reinterpret_cast<uint32_t*>(QueueDepth),
        pVerbsData
        );

    if (ibStatus != IB_SUCCESS) {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

void
NdCqModify(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_CQ_MODIFY* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    //
    // We expect the vendor data to be in the same place in the input and
    // output buffers.  The input buffer is larger, so we use it as the
    // minimum length when querying the output buffer.
    //
    ND_RESOURCE_DESCRIPTOR* out;
    size_t outLen;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&out),
        &outLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    ci_umv_buf_t verbsData;
    NdInitVerbsData(&verbsData, inLen - sizeof(*in),
                    outLen - sizeof(*in), in + 1);

    pProvider->LockShared();

    ND_COMPLETION_QUEUE* cq;
    status = pProvider->GetCq(in->CqHandle, &cq);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    status = cq->Modify(in->QueueDepth, &verbsData);
    cq->Release();

    if (NT_SUCCESS(status)) {
        out->Handle = 0;
        WdfRequestSetInformation(Request, outLen);
    }

err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_COMPLETION_QUEUE::Notify(WDFREQUEST Request, ULONG Type)
{
    //TODO: Call into the verbs provider to arm the CQ.  Current design requires arming
    // in user-mode.
    WDFQUEUE queue = (Type == ND_CQ_NOTIFY_ERRORS) ? m_ErrorQueue : m_Queue;
    NTSTATUS status = WdfRequestForwardToIoQueue(Request, queue);
    if (NT_SUCCESS(status) && !NT_SUCCESS(m_Status)) {
        NdFlushQueue(queue, m_Status);
    }
    return status;
}


void
NdCqNotify(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_CQ_NOTIFY* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    pProvider->LockShared();

    ND_COMPLETION_QUEUE* cq;
    status = pProvider->GetCq(in->CqHandle, &cq);
    if (NT_SUCCESS(status)) {
        status = cq->Notify(Request, in->Type);
        cq->Release();
    }

    pProvider->Unlock();
err1:
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
    }
}
