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


ND_PROTECTION_DOMAIN::ND_PROTECTION_DOMAIN(__in ND_ADAPTER* pAdapter)
    : _Base(pAdapter, pAdapter->GetInterface())
{
    InitializeListHead(&m_QpList);
    InitializeListHead(&m_SrqList);
    InitializeListHead(&m_MrList);
    InitializeListHead(&m_MwList);

    pAdapter->AddPd(&m_Entry);
}


ND_PROTECTION_DOMAIN::~ND_PROTECTION_DOMAIN()
{
    _Base::RunDown();

    if (m_hIfc != NULL) {
        m_pIfc->deallocate_pd(m_hIfc);
    }

    m_pParent->RemovePd(&m_Entry);
}


NTSTATUS
ND_PROTECTION_DOMAIN::Initialize(
    KPROCESSOR_MODE /*RequestorMode*/,
    __inout ci_umv_buf_t* pVerbsData
    )
{
    ib_api_status_t ibStatus = m_pIfc->allocate_pd(
        m_pParent->GetIfcHandle(),
        IB_PDT_NORMAL,
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
ND_PROTECTION_DOMAIN::Create(
    __in ND_ADAPTER* pAdapter,
    KPROCESSOR_MODE RequestorMode,
    __inout ci_umv_buf_t* pVerbsData,
    __out ND_PROTECTION_DOMAIN** ppPd
    )
{
    ND_PROTECTION_DOMAIN* pd = reinterpret_cast<ND_PROTECTION_DOMAIN*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*pd), ND_PD_POOL_TAG)
        );
    if (pd == NULL) {
        return STATUS_NO_MEMORY;
    }

    new(pd) ND_PROTECTION_DOMAIN(pAdapter);
    NTSTATUS status = pd->Initialize(RequestorMode, pVerbsData);
    if (!NT_SUCCESS(status)) {
        pd->Dispose();
        return status;
    }

    *ppPd = pd;
    return STATUS_SUCCESS;
}


void ND_PROTECTION_DOMAIN::Dispose()
{
    this->ND_PROTECTION_DOMAIN::~ND_PROTECTION_DOMAIN();
    ExFreePoolWithTag(this, ND_PD_POOL_TAG);
}


void ND_PROTECTION_DOMAIN::RemoveHandler()
{
    ND_MEMORY_WINDOW        *mw;
    ND_MEMORY_REGION        *mr;
    ND_QUEUE_PAIR           *qp;
    ND_SHARED_RECEIVE_QUEUE *srq;
    LIST_ENTRY              *entry;

    for (entry = m_MwList.Flink; entry != &m_MwList; entry = entry->Flink) {
        mw = static_cast<ND_MEMORY_WINDOW*>(
            ObjChild<ND_PROTECTION_DOMAIN>::FromEntry(entry)
            );
        mw->RemoveHandler();
    }

    for (entry = m_MrList.Flink; entry != &m_MrList; entry = entry->Flink) {
        mr = static_cast<ND_MEMORY_REGION*>(
            ObjChild<ND_PROTECTION_DOMAIN>::FromEntry(entry)
            );
        mr->RemoveHandler();
    }

    for (entry = m_QpList.Flink; entry != &m_QpList; entry = entry->Flink) {
        qp = static_cast<ND_QUEUE_PAIR*>(
            ObjChild<ND_PROTECTION_DOMAIN>::FromEntry(entry)
            );
        qp->RemoveHandler();
    }

    for (entry = m_SrqList.Flink; entry != &m_SrqList; entry = entry->Flink) {
        srq = static_cast<ND_SHARED_RECEIVE_QUEUE*>(
            ObjChild<ND_PROTECTION_DOMAIN>::FromEntry(entry)
            );
        srq->RemoveHandler();
    }

    m_pIfc->deallocate_pd(m_hIfc);
    _Base::RemoveHandler();
}


void
NdPdCreate(
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
    UINT64* out;
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

    ND_ADAPTER* adapter;
    status = pProvider->GetAdapter(in->Handle, &adapter);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_PROTECTION_DOMAIN* pd;
    status = ND_PROTECTION_DOMAIN::Create(
        adapter,
        WdfRequestGetRequestorMode(Request),
        &verbsData,
        &pd
        );

    if (!NT_SUCCESS(status)) {
        goto err3;
    }

    *out = pProvider->AddPd(pd);
    if (*out == 0) {
        pd->Dispose();
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
NdPdFree(
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
        goto out;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

    pProvider->LockShared();
    status = pProvider->FreePd(in->Handle);
    pProvider->Unlock();
out:
    WdfRequestComplete(Request, status);
}
