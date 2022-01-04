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


ND_MEMORY_REGION::ND_MEMORY_REGION(__in ND_PROTECTION_DOMAIN* pPd)
    : _Base(pPd, pPd->GetInterface()),
    m_LKey(0),
    m_RKey(0)
{
    KeInitializeGuardedMutex(&m_Lock);
    pPd->AddMr(&m_Entry);
}


ND_MEMORY_REGION::~ND_MEMORY_REGION()
{
    _Base::RunDown();

    m_pParent->RemoveMr(&m_Entry);

    if (m_hIfc != NULL) {
        m_pIfc->deregister_mr(m_hIfc);
    }
}


//NOTE: OFED drivers do not expose memory region objects.  The object is implicitly
// created when memory is registered.
NTSTATUS
ND_MEMORY_REGION::Initialize(
    KPROCESSOR_MODE /*RequestorMode*/,
    __inout ci_umv_buf_t* /*pVerbsData*/
    )
{
    //ib_api_status_t ibStatus = pPd->m_pIfc->create_mr(pPd->hVerbsPd, &mr->RKey,
    //                                        &mr->m_hIfc, pVerbsData);
    //if (ibStatus != IB_SUCCESS) {
    //    return STATUS_UNSUCCESSFUL;
    //}
    return STATUS_SUCCESS;
}


//static
NTSTATUS
ND_MEMORY_REGION::Create(
    __in ND_PROTECTION_DOMAIN* pPd,
    KPROCESSOR_MODE RequestorMode,
    __inout ci_umv_buf_t* pVerbsData,
    __out ND_MEMORY_REGION** ppMr
    )
{
    ND_MEMORY_REGION* mr = reinterpret_cast<ND_MEMORY_REGION*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*mr), ND_MEMORY_REGION_TAG)
        );
    if (mr == NULL) {
        return STATUS_NO_MEMORY;
    }

    new(mr) ND_MEMORY_REGION(pPd);
    NTSTATUS status = mr->Initialize(RequestorMode, pVerbsData);
    if (!NT_SUCCESS(status)) {
        mr->Dispose();
        return status;
    }

    *ppMr = mr;
    return STATUS_SUCCESS;
}


void ND_MEMORY_REGION::Dispose()
{
    this->ND_MEMORY_REGION::~ND_MEMORY_REGION();
    ExFreePoolWithTag(this, ND_MEMORY_REGION_TAG);
}


void ND_MEMORY_REGION::RemoveHandler()
{
    if (m_hIfc != NULL) {
        m_pIfc->deregister_mr(m_hIfc);
    }
    _Base::RemoveHandler();
}


NTSTATUS
ND_MEMORY_REGION::Register(
    UINT64 Address,
    UINT64 TargetAddress,
    UINT64 Length,
    ULONG Flags,
    KPROCESSOR_MODE RequestorMode,
    __out NDFLTR_MR_KEYS* pKeys
    )
{
    if (Address == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Length == 0) {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    ib_mr_create_t attr;
    attr.vaddr_padding = Address;
    attr.length = Length;
    attr.access_ctrl = 0;

    if (Flags & ND_MR_FLAG_ALLOW_LOCAL_WRITE) {
        attr.access_ctrl |= IB_AC_LOCAL_WRITE;
    }
    if (Flags & ND_MR_FLAG_ALLOW_REMOTE_READ) {
        attr.access_ctrl |= IB_AC_RDMA_READ;
    }
    if (Flags & ND_MR_FLAG_ALLOW_REMOTE_WRITE) {
        attr.access_ctrl |= IB_AC_RDMA_WRITE;
    }
    //NOTE: OFED drivers do not support iWARP.  Because iWARP performs an RDMA write to
    // satisfy the peer's RDMA read request, and because there can only be a single
    // target buffer, the read is limited to a single sink buffer.  These buffers must
    // be registered to allow the remote peer write access.
    //if (Flags & ND_MR_FLAG_RDMA_READ_SINK) {
    //    attr.access_ctrl |=
    //}
    if (Flags & ND_MR_FLAG_DO_NOT_SECURE_VM) {
        attr.access_ctrl |= IB_AC_NOT_CACHABLE;
    }

    boolean_t umCall = (RequestorMode == UserMode);

    NTSTATUS status;
    KeAcquireGuardedMutex(&m_Lock);
    if (m_pIfc == NULL) {
        status = STATUS_DEVICE_REMOVED;
    } else if (m_hIfc != NULL) {
        status = STATUS_INVALID_DEVICE_STATE;
    } else {
        ib_api_status_t ibStatus = m_pIfc->register_mr_remap(
            m_pParent->GetIfcHandle(),
            &attr,
            TargetAddress,
            &m_LKey,
            &m_RKey,
            &m_hIfc,
            umCall
            );
        if (ibStatus == IB_INVALID_PERMISSION) {
            status = STATUS_ACCESS_VIOLATION;
        } else if (ibStatus != IB_SUCCESS) {
            status = STATUS_UNSUCCESSFUL;
        } else {
            status = STATUS_SUCCESS;
            pKeys->LKey = m_LKey;
            pKeys->RKey = m_RKey;
        }
    }
    KeReleaseGuardedMutex(&m_Lock);
    return status;
}


NTSTATUS ND_MEMORY_REGION::Deregister()
{
    NTSTATUS status;
    KeAcquireGuardedMutex(&m_Lock);
    if (m_pIfc == NULL) {
        status = STATUS_DEVICE_REMOVED;
    } else if (m_hIfc == NULL) {
        status = STATUS_INVALID_DEVICE_STATE;
    } else {
        ib_api_status_t ibStatus = m_pIfc->deregister_mr(m_hIfc);
        if (ibStatus == IB_RESOURCE_BUSY) {
            status = STATUS_DEVICE_BUSY;
        } else if (ibStatus != IB_SUCCESS) {
            status = STATUS_UNSUCCESSFUL;
        } else {
            m_hIfc = NULL;
            status = STATUS_SUCCESS;
        }
    }
    KeReleaseGuardedMutex(&m_Lock);
    return status;
}


bool ND_MEMORY_REGION::Busy()
{
    if (_Base::Busy()) {
        return true;
    }

    NTSTATUS status = Deregister();
    return (status == STATUS_DEVICE_BUSY);
}


void
NdMrCreate(
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

    ND_PROTECTION_DOMAIN* pd;
    status = pProvider->GetPd(in->Handle, &pd);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_MEMORY_REGION* mr;
    status = ND_MEMORY_REGION::Create(
        pd,
        WdfRequestGetRequestorMode(Request),
        &verbsData,
        &mr
        );

    if (!NT_SUCCESS(status)) {
        goto err3;
    }

    *out = pProvider->AddMr(mr);
    if (*out == 0) {
        mr->Dispose();
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
NdMrFree(
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
    status = pProvider->FreeMr(in->Handle);
    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


void
NdMrCancelIo(
    __in ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    WdfRequestComplete(Request, STATUS_SUCCESS);
}


void
NdMrRegister(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_MR_REGISTER* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Header.Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    NDFLTR_MR_KEYS* out;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*out),
        reinterpret_cast<VOID**>(&out),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    pProvider->LockShared();

    ND_MEMORY_REGION* mr;
    status = pProvider->GetMr(in->Header.MrHandle, &mr);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    status = mr->Register(
        in->Address,
        in->Header.TargetAddress,
        in->Header.CbLength,
        in->Header.Flags,
        WdfRequestGetRequestorMode(Request),
        out
        );

    if (NT_SUCCESS(status)) {
        WdfRequestSetInformation(Request, sizeof(*out));
    }

    mr->Release();
err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


void
NdMrDeregister(
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

    ND_MEMORY_REGION* mr;
    status = pProvider->GetMr(in->Handle, &mr);
    if (NT_SUCCESS(status)) {
        status = mr->Deregister();
        mr->Release();
    }

    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


void
NdMrRegisterPages(
    __in ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    if (WdfRequestGetRequestorMode(Request) == UserMode) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
    }

    WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
}
