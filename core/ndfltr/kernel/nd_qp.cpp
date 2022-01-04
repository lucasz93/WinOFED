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


void ND_QUEUE_PAIR::EventHandler(ib_event_rec_t* pEvent)
{
    if (pEvent->type != IB_AE_QP_COMM) {
        return;
    }

    ND_QUEUE_PAIR* qp = reinterpret_cast<ND_QUEUE_PAIR*>(pEvent->context);
    ND_ENDPOINT* ep = qp->ClearEp();

    if (ep != NULL) {
        ND_ENDPOINT::Established(ep);
        ep->Dereference();
    }
}


ND_QUEUE_PAIR::ND_QUEUE_PAIR(
    __in ND_PROTECTION_DOMAIN* pPd,
    __in ND_COMPLETION_QUEUE* pReceiveCq,
    __in ND_COMPLETION_QUEUE* pInitiatorCq,
    __in ND_SHARED_RECEIVE_QUEUE* pSrq
    )
    : _Base(pPd, pPd->GetInterface()),
    m_pReceiveCq(pReceiveCq),
    m_pInitiatorCq(pInitiatorCq),
    m_pSrq(pSrq),
    m_pEp(NULL),
    m_EpBound(0)
{
    pReceiveCq->AddRef();
    pInitiatorCq->AddRef();
    if (pSrq != NULL) {
        pSrq->AddRef();
    }

    pPd->AddQp(&m_Entry);
}


ND_QUEUE_PAIR::~ND_QUEUE_PAIR()
{
    _Base::RunDown();

    m_pParent->RemoveQp(&m_Entry);

    if (m_hIfc != NULL) {
        m_pIfc->destroy_qp(m_hIfc, 0);
    }

    if (m_pSrq != NULL) {
        m_pSrq->Release();
    }
    m_pReceiveCq->Release();
    m_pInitiatorCq->Release();
}


NTSTATUS
ND_QUEUE_PAIR::Initialize(
    KPROCESSOR_MODE /*RequestorMode*/,
    ULONG InitiatorQueueDepth,
    ULONG MaxInitiatorRequestSge,
    ULONG ReceiveQueueDepth,
    ULONG MaxReceiveRequestSge,
    ULONG MaxInlineDataSize,
    __inout ci_umv_buf_t* pVerbsData
    )
{
    ib_qp_create_t create;
    create.qp_type =        IB_QPT_RELIABLE_CONN;
    create.sq_max_inline =  MaxInlineDataSize;
    create.sq_depth =       InitiatorQueueDepth;
    create.rq_depth =       ReceiveQueueDepth;
    create.sq_sge =         MaxInitiatorRequestSge;
    create.rq_sge =         MaxReceiveRequestSge;
    create.h_sq_cq =        m_pInitiatorCq->GetIfcHandle();
    create.h_rq_cq =        m_pReceiveCq->GetIfcHandle();
    create.h_srq =          (m_pSrq != NULL) ? m_pSrq->GetIfcHandle() : NULL;
    create.sq_signaled =    FALSE;

    ib_qp_attr_t attr;
    ib_api_status_t ibStatus = m_pIfc->create_qp(
        m_pParent->GetIfcHandle(),
        this,
        EventHandler,
        &create,
        &attr,
        &m_hIfc,
        pVerbsData
        );
    switch (ibStatus) {
    case IB_SUCCESS:
        break;
    case IB_INVALID_PARAMETER:
    case IB_INVALID_SETTING:
        return STATUS_INVALID_PARAMETER;
    case IB_INSUFFICIENT_MEMORY:
        return STATUS_INSUFFICIENT_RESOURCES;
    default:
        return STATUS_UNSUCCESSFUL;
    }

    m_Qpn = attr.num;

    ib_qp_mod_t mod;
    mod.req_state = IB_QPS_INIT;
    mod.state.init.primary_port = 1;
    mod.state.init.qkey = 0;
    mod.state.init.pkey_index = 0;
    mod.state.init.access_ctrl = 0;
    return Modify(&mod, NULL, 0);
}


//static
NTSTATUS
ND_QUEUE_PAIR::Create(
    __in ND_PROTECTION_DOMAIN* pPd,
    KPROCESSOR_MODE RequestorMode,
    __in ND_COMPLETION_QUEUE* pReceiveCq,
    __in ND_COMPLETION_QUEUE* pInitiatorCq,
    __in ND_SHARED_RECEIVE_QUEUE* pSrq,
    ULONG InitiatorQueueDepth,
    ULONG MaxInitiatorRequestSge,
    ULONG ReceiveQueueDepth,
    ULONG MaxReceiveRequestSge,
    ULONG MaxInlineDataSize,
    __inout ci_umv_buf_t* pVerbsData,
    __out ND_QUEUE_PAIR** ppQp
    )
{
    ND_QUEUE_PAIR* qp = reinterpret_cast<ND_QUEUE_PAIR*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*qp), ND_QUEUE_PAIR_TAG)
        );
    if (qp == NULL) {
        return STATUS_NO_MEMORY;
    }

    new(qp) ND_QUEUE_PAIR(pPd, pReceiveCq, pInitiatorCq, pSrq);
    NTSTATUS status = qp->Initialize(
        RequestorMode,
        InitiatorQueueDepth,
        MaxInitiatorRequestSge,
        ReceiveQueueDepth,
        MaxReceiveRequestSge,
        MaxInlineDataSize,
        pVerbsData
        );

    if (!NT_SUCCESS(status)) {
        qp->Dispose();
        return status;
    }

    *ppQp = qp;
    return STATUS_SUCCESS;
}


void ND_QUEUE_PAIR::Dispose()
{
    this->ND_QUEUE_PAIR::~ND_QUEUE_PAIR();
    ExFreePoolWithTag(this, ND_QUEUE_PAIR_TAG);
}


void ND_QUEUE_PAIR::RemoveHandler()
{
    if (m_hIfc != NULL) {
        m_pIfc->destroy_qp(m_hIfc, 0);
    }
    _Base::RemoveHandler();
}


void
NdQpCreate(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_CREATE_QP* in;
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

    if (in->Header.Version != ND_IOCTL_VERSION) {
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
    status = pProvider->GetPd(in->Header.PdHandle, &pd);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_COMPLETION_QUEUE* recvCq;
    status = pProvider->GetCq(in->Header.ReceiveCqHandle, &recvCq);
    if (!NT_SUCCESS(status)) {
        goto err3;
    }

    ND_COMPLETION_QUEUE* initiatorCq;
    status = pProvider->GetCq(in->Header.InitiatorCqHandle, &initiatorCq);
    if (!NT_SUCCESS(status)) {
        goto err4;
    }

    ND_QUEUE_PAIR* qp;
    status = ND_QUEUE_PAIR::Create(
        pd,
        WdfRequestGetRequestorMode(Request),
        recvCq,
        initiatorCq,
        NULL,
        in->Header.InitiatorQueueDepth,
        in->Header.MaxInitiatorRequestSge,
        in->ReceiveQueueDepth,
        in->MaxReceiveRequestSge,
        in->Header.CbMaxInlineData,
        &verbsData,
        &qp
        );

    if (NT_SUCCESS(status)) {
        out->Handle = pProvider->AddQp(qp);
        if (out->Handle == 0) {
            qp->Dispose();
            status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            WdfRequestSetInformation(Request, outLen);
        }
    }

    initiatorCq->Release();
err4:
    recvCq->Release();
err3:
    pd->Release();
err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


void
NdQpCreateWithSrq(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_CREATE_QP_WITH_SRQ* in;
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

    if (in->Header.Version != ND_IOCTL_VERSION) {
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
    status = pProvider->GetPd(in->Header.PdHandle, &pd);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_COMPLETION_QUEUE* recvCq;
    status = pProvider->GetCq(in->Header.ReceiveCqHandle, &recvCq);
    if (!NT_SUCCESS(status)) {
        goto err3;
    }

    ND_COMPLETION_QUEUE* initiatorCq;
    status = pProvider->GetCq(in->Header.InitiatorCqHandle, &initiatorCq);
    if (!NT_SUCCESS(status)) {
        goto err4;
    }

    ND_SHARED_RECEIVE_QUEUE* srq;
    status = pProvider->GetSrq(in->SrqHandle, &srq);
    if (!NT_SUCCESS(status)) {
        goto err5;
    }

    ND_QUEUE_PAIR* qp;
    status = ND_QUEUE_PAIR::Create(
        pd,
        WdfRequestGetRequestorMode(Request),
        recvCq,
        initiatorCq,
        srq,
        in->Header.InitiatorQueueDepth,
        in->Header.MaxInitiatorRequestSge,
        0,
        0,
        in->Header.CbMaxInlineData,
        &verbsData,
        &qp
        );

    if (!NT_SUCCESS(status)) {
        goto err6;
    }

    out->Handle = pProvider->AddQp(qp);
    if (out->Handle == 0) {
        qp->Dispose();
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        WdfRequestSetInformation(Request, outLen);
    }

err6:
    srq->Release();
err5:
    initiatorCq->Release();
err4:
    recvCq->Release();
err3:
    pd->Release();
err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


void
NdQpFree(
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
    status = pProvider->FreeQp(in->Handle);
    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


void
NdQpFlush(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    BYTE* out;
    size_t outLen;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        0,
        reinterpret_cast<VOID**>(&out),
        &outLen
        );
    if (status == STATUS_BUFFER_TOO_SMALL) {
        out = NULL;
        outLen = 0;
    } else if (!NT_SUCCESS(status)) {
        goto err;
    } else if (outLen > ULONG_MAX) {
        status = STATUS_INVALID_PARAMETER;
        goto err;
    }

    pProvider->LockShared();

    ND_QUEUE_PAIR* qp;
    status = pProvider->GetQp(in->Handle, &qp);
    if (NT_SUCCESS(status)) {
        status = qp->Flush(out, static_cast<ULONG>(outLen));
        qp->Release();
        WdfRequestSetInformation(Request, outLen);
    }

    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_QUEUE_PAIR::Modify(ib_qp_mod_t* pMod, BYTE* pOutBuf, ULONG OutLen)
{
    if (Removed()) {
        return STATUS_DEVICE_REMOVED;
    }

    ib_api_status_t ibStatus = m_pIfc->ndi_modify_qp(m_hIfc, pMod, NULL, OutLen, pOutBuf);
    //
    // TODO: report finer grained errors.
    //
    if (ibStatus != IB_SUCCESS) {
        return STATUS_UNSUCCESSFUL;
    } else {
        return STATUS_SUCCESS;
    }
}


NTSTATUS ND_QUEUE_PAIR::Flush(__out_opt BYTE* pOutBuf, ULONG OutLen)
{
    ib_qp_mod_t mod;
    mod.req_state = IB_QPS_ERROR;
    return Modify(&mod, pOutBuf, OutLen);
}
