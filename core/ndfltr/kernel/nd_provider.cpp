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


ND_PROVIDER::ND_PROVIDER(__in ND_PARTITION* pPartition)
    : _Base(pPartition),
    m_nFiles(0)
{
    ExInitializeResourceLite(&m_Lock);
    pPartition->AddProvider(&m_Entry);
}

class CancelIoFunctor
{
public:
    bool operator () (ND_ENDPOINT* ep)
    {
        ep->CancelIo();
        return false;
    }
};

ND_PROVIDER::~ND_PROVIDER()
{
    LockExclusive();

    m_EpTable.LockExclusive();
    m_EpTable.Iterate(CancelIoFunctor());
    m_EpTable.Iterate(DisposeFunctor<ND_ENDPOINT>());
    m_EpTable.Unlock();

    m_MwTable.LockExclusive();
    m_MwTable.Iterate(DisposeFunctor<ND_MEMORY_WINDOW>());
    m_MwTable.Unlock();

    m_QpTable.LockExclusive();
    m_QpTable.Iterate(DisposeFunctor<ND_QUEUE_PAIR>());
    m_QpTable.Unlock();

    m_SrqTable.LockExclusive();
    m_SrqTable.Iterate(DisposeFunctor<ND_SHARED_RECEIVE_QUEUE>());
    m_SrqTable.Unlock();

    m_MrTable.LockExclusive();
    m_MrTable.Iterate(DisposeFunctor<ND_MEMORY_REGION>());
    m_MrTable.Unlock();

    m_PdTable.LockExclusive();
    m_PdTable.Iterate(DisposeFunctor<ND_PROTECTION_DOMAIN>());
    m_PdTable.Unlock();

    m_CqTable.LockExclusive();
    m_CqTable.Iterate(DisposeFunctor<ND_COMPLETION_QUEUE>());
    m_CqTable.Unlock();

    m_AdapterTable.LockExclusive();
    m_AdapterTable.Iterate(DisposeFunctor<ND_ADAPTER>());
    m_AdapterTable.Unlock();

    Unlock();

    _Base::RunDown();

    m_pParent->RemoveProvider(&m_Entry);
    ExDeleteResourceLite(&m_Lock);
}


/*static*/
NTSTATUS
ND_PROVIDER::Create(
    __in ND_PARTITION* pPartition,
    __out ND_PROVIDER** ppProvider
    )
{
    ND_PROVIDER* prov = reinterpret_cast<ND_PROVIDER*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*prov), ND_PROVIDER_POOL_TAG)
        );
    if (prov == NULL) {
        return STATUS_NO_MEMORY;
    }

    new(prov) ND_PROVIDER(pPartition);
    *ppProvider = prov;
    return STATUS_SUCCESS;
}


void ND_PROVIDER::LockShared()
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&m_Lock, TRUE);
}


void ND_PROVIDER::LockExclusive()
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&m_Lock, TRUE);
}


void ND_PROVIDER::Unlock()
{
    ExReleaseResourceLite(&m_Lock);
    KeLeaveCriticalRegion();
}


/*
 * We could be processing asynchronous requests or have them queued
 * when cleaning up.  To synchronize with async request processing,
 * acquire the provider lock with exclusive access until we're done
 * destroying all resoureces.  This will allow active requests to
 * complete their processing, but prevent queued requests from
 * running until cleanup is done.  At that point, queued requests will
 * be unable to acquire any resources.
 */

void ND_PROVIDER::Dispose()
{
    this->ND_PROVIDER::~ND_PROVIDER();
    ExFreePoolWithTag(this, ND_PROVIDER_POOL_TAG);
}


void ND_PROVIDER::RemoveHandler(ND_RDMA_DEVICE *pDevice)
{
    LockExclusive();
    m_AdapterTable.LockExclusive();
    m_AdapterTable.Iterate(RemoveDeviceFunctor<ND_ADAPTER>(pDevice));
    m_AdapterTable.Unlock();
    Unlock();
}


void NdProviderBindFile(ND_PROVIDER *pProvider, WDFREQUEST Request, bool /*InternalDeviceControl*/)
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

    FILE_OBJECT* fileObject;
    status = ObReferenceObjectByHandle(
        reinterpret_cast<HANDLE>(in->Handle),
        0,
        *IoFileObjectType,
        WdfRequestGetRequestorMode(Request),
        reinterpret_cast<VOID**>(&fileObject),
        NULL
        );

    if (!NT_SUCCESS(status)) {
        goto out;
    }

    DEVICE_OBJECT* pDevObj = IoGetRelatedDeviceObject(fileObject);
    if (pDevObj != WdfDeviceWdmGetDeviceObject(ControlDevice)) {
        status = STATUS_INVALID_PARAMETER;
    } else {
        status = pProvider->Bind(
            ND_PROVIDER::HandleFromFile(WdfRequestGetFileObject(Request)),
            fileObject);
    }
    ObDereferenceObject(fileObject);

out:
    WdfRequestComplete(Request, status);
}


//static
void
ND_PROVIDER::ResolveAddress(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    NDFLTR_RESOLVE_ADDRESS* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Header.Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    UINT64* out;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*out),
        reinterpret_cast<VOID**>(&out),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    status = pProvider->m_pParent->ResolveAddress(in->Header.Address, in->DriverId, out);
    if (NT_SUCCESS(status)) {
        WdfRequestSetInformation(Request, sizeof(*out));
    }

err:
    WdfRequestComplete(Request, status);
}


//static
void
ND_PROVIDER::QueryAddressList(
    ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    size_t                      outLen = 0;
    ULONG                       count;
    ULONG                       max;
    NTSTATUS                    status;
    NDFLTR_QUERY_ADDRESS_LIST*  in;
    SOCKADDR_INET*              list;
    GUID                        driverId;

    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto out;
    }

    if (in->Header.Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

    driverId = in->DriverId;

    status = WdfRequestRetrieveOutputBuffer(
        Request,
        0,
        reinterpret_cast<VOID**>(&list),
        &outLen
        );
    if (status == STATUS_BUFFER_TOO_SMALL) {
        list = NULL;
    } else if (!NT_SUCCESS(status)) {
        goto out;
    }

    max = static_cast<ULONG>((outLen) / sizeof(SOCKADDR_INET));
    count = max;

    status = pProvider->m_pParent->GetIpList(
        driverId,
        0,
        &count,
        list
        );
    if (status == STATUS_MORE_ENTRIES) {
        outLen = sizeof(*list) * max;
    } else if (!NT_ERROR(status)) {
        outLen = sizeof(*list) * count;
    } else {
        outLen = 0;
    }

out:
    WdfRequestCompleteWithInformation(Request, status, outLen);
}
