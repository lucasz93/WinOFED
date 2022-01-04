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


void ND_SHARED_RECEIVE_QUEUE::EventHandler(ib_event_rec_t *pEvent)
{
    ND_SHARED_RECEIVE_QUEUE *srq = reinterpret_cast<ND_SHARED_RECEIVE_QUEUE*>(pEvent->context);

    switch (pEvent->type) {
    case IB_AE_SRQ_QP_LAST_WQE_REACHED:
    case IB_AE_SRQ_LIMIT_REACHED:
        NdFlushQueue(srq->m_Queue, STATUS_SUCCESS);
        break;
    default:
        NdFlushQueue(srq->m_Queue, STATUS_INTERNAL_ERROR);
        break;
    }
}


ND_SHARED_RECEIVE_QUEUE::ND_SHARED_RECEIVE_QUEUE(
    __in ND_PROTECTION_DOMAIN* pPd,
    WDFQUEUE queue
    )
    : _Base(pPd, pPd->GetInterface()),
    m_Queue(queue)
{
    pPd->AddSrq(&m_Entry);
}


ND_SHARED_RECEIVE_QUEUE::~ND_SHARED_RECEIVE_QUEUE()
{
    _Base::RunDown();

    m_pParent->RemoveSrq(&m_Entry);

    if (m_hIfc != NULL) {
        m_pIfc->destroy_srq(m_hIfc);
    }

    WdfIoQueuePurgeSynchronously(m_Queue);
    WdfObjectDelete(m_Queue);
}


NTSTATUS
ND_SHARED_RECEIVE_QUEUE::Initialize(
    KPROCESSOR_MODE /*RequestorMode*/,
    ULONG QueueDepth,
    ULONG MaxRequestSge,
    ULONG NotifyThreshold,
    __inout ci_umv_buf_t* pVerbsData
    )
{
    ib_srq_attr_t attr;
    attr.max_wr =       QueueDepth;
    attr.max_sge =      MaxRequestSge;
    attr.srq_limit =    NotifyThreshold;

    ib_api_status_t ibStatus = m_pIfc->create_srq(
        m_pParent->GetIfcHandle(),
        this,
        EventHandler,
        &attr,
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
ND_SHARED_RECEIVE_QUEUE::Create(
    __in ND_PROTECTION_DOMAIN* pPd,
    KPROCESSOR_MODE RequestorMode,
    ULONG QueueDepth,
    ULONG MaxRequestSge,
    ULONG NotifyThreshold,
    __inout ci_umv_buf_t* pVerbsData,
    __out ND_SHARED_RECEIVE_QUEUE** ppSrq
    )
{
    ND_SHARED_RECEIVE_QUEUE* srq = reinterpret_cast<ND_SHARED_RECEIVE_QUEUE*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*srq), ND_SHARED_RECEIVE_Q_POOL_TAG)
        );
    if (srq == NULL) {
        return STATUS_NO_MEMORY;
    }

    WDFQUEUE queue;
    WDF_IO_QUEUE_CONFIG config;
    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    NTSTATUS status = WdfIoQueueCreate(ControlDevice, &config,
                              WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(srq, ND_SHARED_RECEIVE_Q_POOL_TAG);
        return status;
    }

    new(srq) ND_SHARED_RECEIVE_QUEUE(pPd, queue);
    status = srq->Initialize(
        RequestorMode,
        QueueDepth,
        MaxRequestSge,
        NotifyThreshold,
        pVerbsData
        );
    if (!NT_SUCCESS(status)) {
        srq->Dispose();
        return status;
    }

    *ppSrq = srq;
    return STATUS_SUCCESS;
}


void ND_SHARED_RECEIVE_QUEUE::Dispose()
{
    this->ND_SHARED_RECEIVE_QUEUE::~ND_SHARED_RECEIVE_QUEUE();
    ExFreePoolWithTag(this, ND_SHARED_RECEIVE_Q_POOL_TAG);
}


void ND_SHARED_RECEIVE_QUEUE::RemoveHandler()
{
    if (m_hIfc != NULL) {
        m_pIfc->destroy_srq(m_hIfc);
    }
    _Base::RemoveHandler();
}


void
NdSrqCreate(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_CREATE_SRQ* in;
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

    ND_PROTECTION_DOMAIN* pd;
    status = pProvider->GetPd(in->PdHandle, &pd);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_SHARED_RECEIVE_QUEUE* srq;
    status = ND_SHARED_RECEIVE_QUEUE::Create(
        pd,
        WdfRequestGetRequestorMode(Request),
        in->QueueDepth,
        in->MaxRequestSge,
        in->NotifyThreshold,
        &verbsData,
        &srq
        );

    if (NT_SUCCESS(status)) {
        out->Handle = pProvider->AddSrq(srq);
        if (out->Handle == 0) {
            srq->Dispose();
            status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            WdfRequestSetInformation(Request, outLen);
        }
    }

    pd->Release();
err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


void
NdSrqFree(
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
    status = pProvider->FreeSrq(in->Handle);
    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


void ND_SHARED_RECEIVE_QUEUE::CancelIo()
{
    WdfIoQueuePurgeSynchronously(m_Queue);
    WdfIoQueueStart(m_Queue);
}


void
NdSrqCancelIo(
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
    ND_SHARED_RECEIVE_QUEUE* srq;
    status = pProvider->GetSrq(in->Handle, &srq);
    pProvider->Unlock();

    if (NT_SUCCESS(status)) {
        srq->CancelIo();
        srq->Release();
    }

err:
    WdfRequestComplete(Request, status);
}


void
NdSrqGetAffinity(
    __in ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
}


NTSTATUS
ND_SHARED_RECEIVE_QUEUE::Modify(
    ULONG QueueDepth,
    ULONG NotifyThreshold,
    ci_umv_buf_t* pVerbsData
    )
{
    ib_srq_attr_t attr;
    attr.max_wr = QueueDepth;
    attr.max_sge = 0;
    attr.srq_limit = NotifyThreshold;

    //
    // We don't use ib_srq_attr_mask_t here because it is defined as an enum,
    // so OR-ing values in gives compiler errors.  Lame.
    //
    int mask = 0;
    if (QueueDepth != 0) {
        mask |= IB_SRQ_MAX_WR;
    }
    if (NotifyThreshold != 0) {
        mask |= IB_SRQ_LIMIT;
    }
    ib_api_status_t ibStatus = m_pIfc->modify_srq(
        m_hIfc,
        &attr,
        static_cast<ib_srq_attr_mask_t>(mask),
        pVerbsData
        );

    if (ibStatus != IB_SUCCESS) {
        return STATUS_UNSUCCESSFUL;
    }
    
    return STATUS_SUCCESS;
}


void
NdSrqModify(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_SRQ_MODIFY* in;
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

    ND_SHARED_RECEIVE_QUEUE* srq;
    status = pProvider->GetSrq(in->SrqHandle, &srq);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    status = srq->Modify(in->QueueDepth, in->NotifyThreshold, &verbsData);
    srq->Release();

    if (NT_SUCCESS(status)) {
        out->Handle = 0;
        WdfRequestSetInformation(Request, outLen);
    }

err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_SHARED_RECEIVE_QUEUE::Notify(WDFREQUEST Request)
{
    WdfObjectAcquireLock(m_Queue);
    NTSTATUS status = WdfRequestForwardToIoQueue(Request, m_Queue);
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdSrqNotify(
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
        goto err1;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    pProvider->LockShared();

    ND_SHARED_RECEIVE_QUEUE* srq;
    status = pProvider->GetSrq(in->Handle, &srq);
    if (NT_SUCCESS(status)) {
        status = srq->Notify(Request);
        srq->Release();
    }

    pProvider->Unlock();
err1:
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
    }
}
