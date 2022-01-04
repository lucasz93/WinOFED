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


ND_ADAPTER::ND_ADAPTER(
    __in ND_PROVIDER* pProvider,
    __in ND_RDMA_DEVICE* pDevice,
    __in UINT64 AdapterId
    )
    : _Base(pProvider, &pDevice->Interface.Verbs),
    m_pDevice(pDevice),
    m_Id(AdapterId)
{
    InitializeListHead(&m_PdList);
    InitializeListHead(&m_CqList);
    InitializeListHead(&m_EpList);
}


ND_ADAPTER::~ND_ADAPTER()
{
    _Base::RunDown();

    if (m_hIfc != NULL) {
        //m_pDevice->m_pIfc->unregister_event_handler(m_pDevice->m_pDevice->hDevice,
        //                                          &m_pDevice->EventHandler);
        m_pIfc->um_close_ca(m_pDevice->hDevice, m_hIfc);
    }

    if (m_pDevice != NULL) {
        NdRdmaDevicePut(m_pDevice);
    }
}


NTSTATUS
ND_ADAPTER::Initialize(
    KPROCESSOR_MODE RequestorMode,
    __inout ci_umv_buf_t* pVerbsData
    )
{
    ib_api_status_t ibStatus = m_pIfc->um_open_ca(
        m_pDevice->hDevice,
        RequestorMode,
        pVerbsData,
        &m_hIfc
        );
    if (ibStatus != IB_SUCCESS) {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}


//static
NTSTATUS
ND_ADAPTER::Create(
    __in ND_PROVIDER* pProvider,
    UINT64 AdapterId,
    KPROCESSOR_MODE RequestorMode,
    __inout ci_umv_buf_t* pVerbsData,
    __out ND_ADAPTER** ppAdapter
    )
{
    ND_RDMA_DEVICE* device = NdRdmaDeviceGet(AdapterId);
    if (device == NULL) {
        return STATUS_NO_SUCH_DEVICE;
    }

    ND_ADAPTER* adapter = reinterpret_cast<ND_ADAPTER*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*adapter), ND_ADAPTER_POOL_TAG)
        );
    if (adapter == NULL) {
        NdRdmaDevicePut(device);
        return STATUS_NO_MEMORY;
    }

    new(adapter) ND_ADAPTER(pProvider, device, AdapterId);
    NTSTATUS status = adapter->Initialize(RequestorMode, pVerbsData);
    if (!NT_SUCCESS(status)) {
        adapter->Dispose();
        return status;
    }
    *ppAdapter = adapter;
    return STATUS_SUCCESS;
}


void ND_ADAPTER::Dispose()
{
    this->ND_ADAPTER::~ND_ADAPTER();
    ExFreePoolWithTag(this, ND_ADAPTER_POOL_TAG);
}


void ND_ADAPTER::RemoveHandler(__in ND_RDMA_DEVICE* pDevice)
{
    if (m_pDevice != pDevice) {
        return;
    }

    LIST_ENTRY* entry;
    for (entry = m_PdList.Flink; entry != &m_PdList; entry = entry->Flink) {
        ND_PROTECTION_DOMAIN* pd = static_cast<ND_PROTECTION_DOMAIN*>(
            ObjChild<ND_ADAPTER>::FromEntry(entry)
            );
        pd->RemoveHandler();
    }

    for (entry = m_CqList.Flink; entry != &m_CqList; entry = entry->Flink) {
        ND_COMPLETION_QUEUE* cq = static_cast<ND_COMPLETION_QUEUE*>(
            ObjChild<ND_ADAPTER>::FromEntry(entry)
            );
        cq->RemoveHandler();
    }

    for (entry = m_EpList.Flink; entry != &m_EpList; entry = entry->Flink) {
        ND_ENDPOINT* ep = static_cast<ND_ENDPOINT*>(
            ObjChild<ND_ADAPTER>::FromEntry(entry)
            );
        ep->RemoveHandler();
    }

    m_pIfc->um_close_ca(m_pDevice->hDevice, m_hIfc);
    _Base::RemoveHandler();
    NdRdmaDevicePut(m_pDevice);
    m_pDevice = NULL;
}


NTSTATUS
ND_ADAPTER::GetDeviceAddress(
    __in const SOCKADDR_INET& Addr,
    __out IBAT_PORT_RECORD* pDeviceAddress
    )
{
    return GetProvider()->GetPartition()->GetDeviceAddress(
        Addr,
        GetInterface()->driver_id,
        GetInterface()->guid,
        pDeviceAddress
        );
}


void
NdAdapterOpen(
    __in ND_PROVIDER *pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_OPEN_ADAPTER* in;
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

    ND_ADAPTER* adapter;
    status = ND_ADAPTER::Create(
        pProvider,
        in->AdapterId,
        WdfRequestGetRequestorMode(Request),
        &verbsData,
        &adapter
        );
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    out->Handle = pProvider->AddAdapter(adapter);
    if (out->Handle == 0) {
        adapter->Dispose();
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        WdfRequestSetInformation(Request, outLen);
    }

err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


void
NdAdapterClose(
    __in ND_PROVIDER *pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE *in;
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
    status = pProvider->FreeAdapter(in->Handle);
    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


//static
void
ND_ADAPTER::Query(
    __in ND_PROVIDER *pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_ADAPTER_QUERY* in;
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

    if (in->InfoVersion != ND_VERSION_2) {
        status = STATUS_NOT_SUPPORTED;
        goto err;
    }

    pProvider->LockShared();

    ND_ADAPTER* adapter;
    status = pProvider->GetAdapter(in->AdapterHandle, &adapter);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

#ifdef _WIN64
    if( WdfRequestIsFrom32BitProcess(Request) == TRUE) {
        ND2_ADAPTER_INFO32* out;
        status = WdfRequestRetrieveOutputBuffer(
            Request,
            sizeof(*out),
            reinterpret_cast<VOID**>(&out),
            NULL
            );
        if (!NT_SUCCESS(status)) {
            goto err3;
        }

        NdThunkAdapterInfo(out, &adapter->GetDevice()->Info);
        out->AdapterId = adapter->m_Id;
        WdfRequestSetInformation(Request, sizeof(*out));
    } else {
#endif // _WIN64
        ND2_ADAPTER_INFO* out;
        status = WdfRequestRetrieveOutputBuffer(
            Request,
            sizeof(*out),
            reinterpret_cast<VOID**>(&out),
            NULL
            );
        if (!NT_SUCCESS(status)) {
            goto err3;
        }

        RtlCopyMemory(out, &adapter->GetDevice()->Info, sizeof(*out));
        out->AdapterId = adapter->m_Id;
        WdfRequestSetInformation(Request, sizeof(*out));
#ifdef _WIN64
    }
#endif // _WIN64

err3:
    adapter->Release();
err2:
    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


void
ND_ADAPTER::QueryAddressList(
    __in ND_PROVIDER *pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    NDFLTR_QUERY_ADDRESS_LIST* in;
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

    SOCKADDR_INET* out;
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
    }

    pProvider->LockShared();

    ND_ADAPTER* adapter;
    status = pProvider->GetAdapter(in->Header.Handle, &adapter);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ULONG max = static_cast<ULONG>((outLen) / sizeof(SOCKADDR_INET));
    ULONG count = max;

    status = adapter->GetProvider()->GetPartition()->GetIpList(
        adapter->GetInterface()->driver_id,
        adapter->m_Id,
        &count,
        out
        );
    if (status == STATUS_MORE_ENTRIES) {
        outLen = sizeof(*out) * max;
    } else if (!NT_ERROR(status)) {
        outLen = sizeof(*out) * count;
    } else {
        outLen = 0;
    }
    WdfRequestSetInformation(Request, outLen);

    adapter->Release();
err2:
    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}
