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


inline
bool
IsEqual(const SOCKADDR_IN6& lhs, const SOCKADDR_IN6& rhs )
{
    if (IN6_IS_ADDR_LOOPBACK(&lhs.sin6_addr) ||
        IN6_IS_ADDR_LOOPBACK(&rhs.sin6_addr))
    {
        return true;
    }
    return IN6_ADDR_EQUAL( &lhs.sin6_addr, &rhs.sin6_addr ) == TRUE;
}


inline void MapTo6(__in const SOCKADDR_INET& in, __out SOCKADDR_IN6* out)
{
    if (in.si_family == AF_INET) {
        if (in.Ipv4.sin_addr.s_addr == _byteswap_ulong(INADDR_LOOPBACK)) {
            IN6ADDR_SETLOOPBACK(out);
        } else {
            out->sin6_family = AF_INET6;
            out->sin6_port = 0;
            out->sin6_flowinfo = 0;
            out->sin6_addr = in6addr_v4mappedprefix;
            out->sin6_addr.u.Word[6] = in.Ipv4.sin_addr.S_un.S_un_w.s_w1;
            out->sin6_addr.u.Word[7] = in.Ipv4.sin_addr.S_un.S_un_w.s_w2;
            out->sin6_scope_id = 0;
        }
    } else {
        *out = in.Ipv6;
    }
}

inline
bool
IsEqual(const SOCKADDR_INET& lhs, const SOCKADDR_INET& rhs)
{
    SOCKADDR_IN6 lhs6;
    SOCKADDR_IN6 rhs6;

    MapTo6(lhs, &lhs6);
    MapTo6(rhs, &rhs6);

    return IsEqual(lhs6, rhs6 );
}


inline UINT64 HwAddressToMac(__in const IF_PHYSICAL_ADDRESS& HwAddress)
{
    UINT64 mac = 0;
    if (HwAddress.Length <= sizeof(mac)) {
        RtlCopyMemory(&mac, HwAddress.Address, HwAddress.Length);
    }
    NT_ASSERT(mac != 0);
    return mac;
}


ND_PARTITION::ND_PARTITION(UINT64 AdapterId)
    : m_AdapterId(AdapterId)
{
    InitializeListHead(&m_ProvList);
    InitializeListHead(&m_AddrList);
    ExInitializeResourceLite(&m_Lock);
}


ND_PARTITION::~ND_PARTITION()
{
    _Base::RunDown();
    ExDeleteResourceLite(&m_Lock);
}


/*static*/
NTSTATUS
ND_PARTITION::Create(
    __in UINT64 AdapterId,
    __out ND_PARTITION** ppPartition
    )
{
    ND_PARTITION* part = reinterpret_cast<ND_PARTITION*>(
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*part), ND_PARTITION_POOL_TAG)
        );
    if (part == NULL) {
        return STATUS_NO_MEMORY;
    }

    new(part) ND_PARTITION(AdapterId);
    *ppPartition = part;
    return STATUS_SUCCESS;
}


void ND_PARTITION::Dispose()
{
    this->ND_PARTITION::~ND_PARTITION();
    ExFreePoolWithTag(this, ND_PARTITION_POOL_TAG);
}


void ND_PARTITION::LockShared()
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&m_Lock, TRUE);
}


void ND_PARTITION::LockExclusive()
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&m_Lock, TRUE);
}


void ND_PARTITION::Unlock()
{
    ExReleaseResourceLite(&m_Lock);
    KeLeaveCriticalRegion();
}


NTSTATUS
ND_PARTITION::PurgeAddressUnsafe(
    __in const SOCKADDR_INET& Addr
    )
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    for (ADDRESS_ENTRY* entry = static_cast<ADDRESS_ENTRY*>(m_AddrList.Flink);
        entry != &m_AddrList;
        entry = static_cast<ADDRESS_ENTRY*>(entry->Flink))
    {
        if (IsEqual(entry->Addr, Addr) == true) {
            RemoveEntryList(entry);
            ExFreePoolWithTag(entry, ND_PARTITION_ADDRESS_TAG);
            status = STATUS_SUCCESS;
            break;
        }
    }
    return status;
}


NTSTATUS
ND_PARTITION::BindAddress(
    __in const SOCKADDR_INET& Addr,
    __in const UINT64 GuestMac,
    __in const UINT64 Mac
    )
{
    if (GuestMac == 0 || Mac == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    ADDRESS_ENTRY* entry = reinterpret_cast<ADDRESS_ENTRY*>(
        ExAllocatePoolWithTag(PagedPool, sizeof(*entry), ND_PARTITION_ADDRESS_TAG)
        );
    if (entry == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    entry->Addr = Addr;
    entry->GuestMac = GuestMac;
    entry->Mac = Mac;

    LockExclusive();
    PurgeAddressUnsafe(Addr);
    InsertTailList(&m_AddrList, entry);
    Unlock();
    return STATUS_SUCCESS;
}


NTSTATUS ND_PARTITION::UnbindAddress(
    __in const SOCKADDR_INET& Addr
    )
{
    LockExclusive();
    NTSTATUS status = PurgeAddressUnsafe(Addr);
    Unlock();
    return status;
}


NTSTATUS
ND_PARTITION::ResolveAddress(
    const SOCKADDR_INET& Addr,
    const GUID& DriverId,
    UINT64* pId
    )
{
    NTSTATUS status = STATUS_INVALID_ADDRESS;
    IBAT_PORT_RECORD info = {0};

    if (m_AdapterId == 0) {
        status = IbatInterface.IpToPort(&Addr, &DriverId, &info);
    } else {
        LockShared();
        for(ADDRESS_ENTRY* entry = static_cast<ADDRESS_ENTRY*>(m_AddrList.Flink);
            entry != &m_AddrList;
            entry = static_cast<ADDRESS_ENTRY*>(entry->Flink))
        {
            if (IsEqual(entry->Addr, Addr) == true) {
                status = IbatInterface.MacToPort(entry->Mac, &DriverId, &info);
                break;
            }
        }
        Unlock();
    }

    if (NT_SUCCESS(status)) {
        *pId = info.CaGuid;
    }
    return status;
}


NTSTATUS
ND_PARTITION::GetIpList(
    __in REFGUID DriverId,
    __in UINT64 AdapterId,
    __inout ULONG* pnAddrs,
    __out SOCKADDR_INET* pAddrs
    )
{
    if (m_AdapterId == 0) {
        return IbatInterface.GetIpList(
            &DriverId,
            AdapterId,
            0,
            0,
            pnAddrs,
            pAddrs
            );
    }

    if (AdapterId != 0 && AdapterId != m_AdapterId) {
        return STATUS_INVALID_PARAMETER;
    }

    ULONG count = 0;
    LockShared();
    ADDRESS_ENTRY* entry;
    for(entry = static_cast<ADDRESS_ENTRY*>(m_AddrList.Flink);
        entry != &m_AddrList;
        entry = static_cast<ADDRESS_ENTRY*>(entry->Flink))
    {
        count++;
        if (pAddrs != NULL) {
            if (count <= *pnAddrs) {
                *pAddrs = entry->Addr;
                pAddrs++;
            } else {
                break;
            }
        }
    }
    Unlock();
    if (entry != static_cast<ADDRESS_ENTRY*>(&m_AddrList)) {
        return STATUS_MORE_ENTRIES;
    }
    *pnAddrs = count;
    if (pAddrs == NULL) {
        return STATUS_BUFFER_OVERFLOW;
    }
    return STATUS_SUCCESS;
}


NTSTATUS
ND_PARTITION::GetDeviceAddress(
    __in const SOCKADDR_INET& Addr,
    __in REFGUID DriverId,
    __in UINT64 AdapterId,
    __out IBAT_PORT_RECORD* pDeviceAddress
    )
{
    NTSTATUS status;
    if (m_AdapterId == 0) {
        status = IbatInterface.IpToPort(&Addr, &DriverId, pDeviceAddress);
        if (NT_SUCCESS(status) && pDeviceAddress->CaGuid != AdapterId) {
            status = STATUS_INVALID_ADDRESS;
        }
        return status;
    }

    status = STATUS_INVALID_ADDRESS;
    if (AdapterId == m_AdapterId) {
        LockShared();
        for(ADDRESS_ENTRY* entry = static_cast<ADDRESS_ENTRY*>(m_AddrList.Flink);
            entry != &m_AddrList;
            entry = static_cast<ADDRESS_ENTRY*>(entry->Flink))
        {
            if (IsEqual(entry->Addr, Addr) == true) {
                status = IbatInterface.MacToPort(entry->Mac, &DriverId, pDeviceAddress);
                break;
            }
        }
        Unlock();
    }
    return status;
}


NTSTATUS
ND_PARTITION::QueryPath(
    __in const SOCKADDR_INET& LocalAddress,
    __in const SOCKADDR_INET& RemoteAddress,
    __in const IF_PHYSICAL_ADDRESS& RemoteHwAddress,
    __in FN_IBAT_QUERY_PATH_CALLBACK* CompletionCallback,
    __in VOID* CompletionContext,
    __out ib_path_rec_t* pPath
    )
{
    if (m_AdapterId == 0) {
        return IbatInterface.QueryPathByIpAddress(
        &LocalAddress,
        &RemoteAddress,
        CompletionCallback,
        CompletionContext,
        pPath
        );
    }

    UINT64 remoteMac = HwAddressToMac(RemoteHwAddress);
    if (remoteMac == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status = STATUS_INVALID_ADDRESS;
    LockShared();
    for(ADDRESS_ENTRY* entry = static_cast<ADDRESS_ENTRY*>(m_AddrList.Flink);
        entry != &m_AddrList;
        entry = static_cast<ADDRESS_ENTRY*>(entry->Flink))
    {
        if (IsEqual(entry->Addr, LocalAddress) == true) {
            status = IbatInterface.QueryPathByPhysicalAddress(
                entry->Mac,
                remoteMac,
                CompletionCallback,
                CompletionContext,
                pPath
                );
            break;
        }
    }
    Unlock();
    return status;
}


NTSTATUS
ND_PARTITION::CancelQuery(
    __in const SOCKADDR_INET& LocalAddress,
    __in const SOCKADDR_INET& RemoteAddress,
    __in const IF_PHYSICAL_ADDRESS& RemoteHwAddress,
    __in FN_IBAT_QUERY_PATH_CALLBACK* CompletionCallback,
    __in VOID* CompletionContext
    )
{
    if (m_AdapterId == 0) {
        return IbatInterface.CancelQueryByIpAddress(
        &LocalAddress,
        &RemoteAddress,
        CompletionCallback,
        CompletionContext
        );
    }

    UINT64 remoteMac = HwAddressToMac(RemoteHwAddress);
    if (remoteMac == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status = STATUS_INVALID_ADDRESS;
    LockShared();
    for(ADDRESS_ENTRY* entry = static_cast<ADDRESS_ENTRY*>(m_AddrList.Flink);
        entry != &m_AddrList;
        entry = static_cast<ADDRESS_ENTRY*>(entry->Flink))
    {
        if (IsEqual(entry->Addr, LocalAddress) == true) {
            status = IbatInterface.CancelQueryByPhysicalAddress(
                entry->Mac,
                remoteMac,
                CompletionCallback,
                CompletionContext
                );
            break;
        }
    }
    Unlock();
    return status;
}


//static
void
ND_PARTITION::ResolveAdapterId(
    ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool InternalDeviceControl
    )
{
    NTSTATUS status;

    if (InternalDeviceControl == false) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

    NDV_RESOLVE_ADAPTER_ID* in;
    status = WdfRequestRetrieveInputBuffer(
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

    UINT64* out;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*out),
        reinterpret_cast<VOID**>(&out),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto out;
    }

    UINT64 mac = HwAddressToMac(in->HwAddress);
    if (mac == 0) {
        status = STATUS_INVALID_PARAMETER;
        goto out;
    }

    IBAT_PORT_RECORD rec;
    status = IbatInterface.MacToPort(mac, NULL, &rec);
    if (!NT_SUCCESS(status)) {
        goto out;
    }

    *out = rec.CaGuid;
    WdfRequestSetInformation(Request, sizeof(*out));

out:
    WdfRequestComplete(Request, status);
}


//static
void
ND_PARTITION::Free(
    ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool InternalDeviceControl
    )
{
    NTSTATUS status;

    if (InternalDeviceControl == false) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

    ND_HANDLE* in;
    status = WdfRequestRetrieveInputBuffer(
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

    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    status = driver->FreePartition(in->Handle);

out:
    WdfRequestComplete(Request, status);
}


//static
void
ND_PARTITION::BindAddress(
    ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool InternalDeviceControl
    )
{
    NTSTATUS status;

    if (InternalDeviceControl == false) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

    NDV_PARTITION_BIND_ADDRESS* in;
    status = WdfRequestRetrieveInputBuffer(
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

    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    ND_PARTITION* part = driver->GetPartition(in->PartitionHandle, true);
    if (part == NULL) {
        status = STATUS_INVALID_HANDLE;
        goto out;
    }

    UINT64 guestMac = HwAddressToMac(in->GuestHwAddress);
    UINT64 mac = HwAddressToMac(in->HwAddress);
    status = part->BindAddress(in->Address, guestMac, mac);
    part->Release();

out:
    WdfRequestComplete(Request, status);
}


//static
void
ND_PARTITION::UnbindAddress(
    ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool InternalDeviceControl
    )
{
    NTSTATUS status;

    if (InternalDeviceControl == false) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

    NDV_PARTITION_UNBIND_ADDRESS* in;
    status = WdfRequestRetrieveInputBuffer(
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

    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    ND_PARTITION* part = driver->GetPartition(in->Handle, true);
    if (part == NULL) {
        status = STATUS_INVALID_HANDLE;
        goto out;
    }

    status = part->UnbindAddress(in->Address);
    part->Release();

out:
    WdfRequestComplete(Request, status);
}


//static
void
ND_PARTITION::BindLuid(
    ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool InternalDeviceControl
    )
{
    NTSTATUS status;

    if (InternalDeviceControl == false) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

    NDV_PARTITION_BIND_LUID* in;
    status = WdfRequestRetrieveInputBuffer(
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

    UINT64 mac = HwAddressToMac(in->HwAddress);
    if (mac == 0) {
        status = STATUS_INVALID_PARAMETER;
        goto out;
    }

    status = IbatInterface.UpdateRegistration(mac, in->Luid.Value);

out:
    WdfRequestComplete(Request, status);
}
