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


ND_MEMORY_WINDOW::ND_MEMORY_WINDOW(__in ND_PROTECTION_DOMAIN* pPd)
    : _Base(pPd, pPd->GetInterface()),
    m_RKey(0)
{
    pPd->AddMw(&m_Entry);
}


ND_MEMORY_WINDOW::~ND_MEMORY_WINDOW()
{
    _Base::RunDown();

    m_pParent->RemoveMw(&m_Entry);

    if (m_hIfc != NULL) {
        m_pIfc->destroy_mw(m_hIfc);
    }
}


//
//NOTE: Current OFED drivers do *not* support MWs.
//
NTSTATUS
ND_MEMORY_WINDOW::Initialize(
    KPROCESSOR_MODE /*RequestorMode*/,
    __inout ci_umv_buf_t* /*pVerbsData*/
    )
{
    //ib_api_status_t ibStatus = pPd->pVerbs->create_mw(pPd->hVerbsPd, &mw->RKey,
    //                                        &mw->hVerbsMw, pVerbsData);
    //if (ibStatus != IB_SUCCESS) {
    //    return STATUS_UNSUCCESSFUL;
    //}

    return STATUS_SUCCESS;
}


//static
NTSTATUS
ND_MEMORY_WINDOW::Create(
    __in ND_PROTECTION_DOMAIN* pPd,
    KPROCESSOR_MODE RequestorMode,
    __inout ci_umv_buf_t* pVerbsData,
    __out ND_MEMORY_WINDOW** ppMw
    )
{
    //TODO: Evaluate whether we can allocate some of these objects as PagedPool.
    ND_MEMORY_WINDOW* mw = reinterpret_cast<ND_MEMORY_WINDOW*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*mw), ND_MEMORY_WINDOW_TAG)
        );
    if (mw == NULL) {
        return STATUS_NO_MEMORY;
    }

    new(mw) ND_MEMORY_WINDOW(pPd);

    NTSTATUS status = mw->Initialize(RequestorMode, pVerbsData);
    if (!NT_SUCCESS(status)) {
        mw->Dispose();
        return status;
    }

    *ppMw = mw;
    return STATUS_SUCCESS;
}


void ND_MEMORY_WINDOW::Dispose()
{
    this->ND_MEMORY_WINDOW::~ND_MEMORY_WINDOW();
    ExFreePoolWithTag(this, ND_MEMORY_WINDOW_TAG);
}


void ND_MEMORY_WINDOW::RemoveHandler()
{
    //TODO: OFED drivers don't support memory windows.
    //pVerbs->destroy_mw(hVerbsMw);
    _Base::RemoveHandler();
}


void
NdMwCreate(
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

    ND_PROTECTION_DOMAIN *pd;
    status = pProvider->GetPd(in->Handle, &pd);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_MEMORY_WINDOW *mw;
    status = ND_MEMORY_WINDOW::Create(
        pd,
        WdfRequestGetRequestorMode(Request),
        &verbsData,
        &mw
        );

    if (!NT_SUCCESS(status)) {
        goto err3;
    }

    *out = pProvider->AddMw(mw);
    if (*out == 0) {
        mw->Dispose();
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        WdfRequestSetInformation(Request, outLen);
    }

err3:
    pd->Release();
err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


void
NdMwFree(
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
    status = pProvider->FreeMw(in->Handle);
    pProvider->Unlock();
out:
    WdfRequestComplete(Request, status);
}
