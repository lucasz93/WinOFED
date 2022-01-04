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

#pragma once

#ifndef _ND_PARTITION_H_
#define _ND_PARTITION_H_


#define ND_PARTITION_POOL_TAG 'pvdn'
#define ND_PARTITION_ADDRESS_TAG 'apdn'

class ND_PARTITION : public ObjBase
{
    typedef ObjBase _Base;

    ERESOURCE           m_Lock;
    LIST_ENTRY          m_ProvList;
    LIST_ENTRY          m_AddrList;
    UINT64              m_AdapterId;

    struct ADDRESS_ENTRY : public LIST_ENTRY {
        SOCKADDR_INET       Addr;
        UINT64              GuestMac;
        UINT64              Mac;
    };

public:
    static
    NTSTATUS
    Create(
        __in UINT64 AdapterId,
        __out ND_PARTITION** ppPartition
        );

    NTSTATUS
    PurgeAddressUnsafe(
        __in const SOCKADDR_INET& Addr
        );

public:
    ND_PARTITION(UINT64 AdapterId);
    ~ND_PARTITION();

    void Dispose();

    NTSTATUS
    BindAddress(
        __in const SOCKADDR_INET& Addr,
        __in const UINT64 GuestMac,
        __in const UINT64 Mac
        );

    NTSTATUS
    UnbindAddress(
        __in const SOCKADDR_INET& Addr
        );

    NTSTATUS ResolveAddress(const SOCKADDR_INET& Addr, const GUID& DriverId, UINT64* pId);

    void AddProvider(LIST_ENTRY* entry)
    {
        LockExclusive();
        InsertTailList(&m_ProvList, entry);
        Unlock();
    }

    void RemoveProvider(LIST_ENTRY* entry)
    {
        LockExclusive();
        RemoveEntryList(entry);
        Unlock();
    }

    NTSTATUS GetIpList(
        __in REFGUID DriverId,
        __in UINT64 AdapterId,
        __inout ULONG* pnAddrs,
        __out SOCKADDR_INET* pAddrs
        );

    NTSTATUS GetDeviceAddress(
        __in const SOCKADDR_INET& Addr,
        __in REFGUID DriverId,
        __in UINT64 AdapterId,
        __out IBAT_PORT_RECORD* pDeviceAddress
        );

    NTSTATUS
    QueryPath(
        __in const SOCKADDR_INET& pLocalAddress,
        __in const SOCKADDR_INET& pRemoteAddress,
        __in const IF_PHYSICAL_ADDRESS& RemoteHwAddress,
        __in FN_IBAT_QUERY_PATH_CALLBACK* CompletionCallback,
        __in VOID* CompletionContext,
        __out ib_path_rec_t* pPath
        );

    NTSTATUS
    CancelQuery(
        __in const SOCKADDR_INET& pLocalAddress,
        __in const SOCKADDR_INET& pRemoteAddress,
        __in const IF_PHYSICAL_ADDRESS& RemoteHwAddress,
        __in FN_IBAT_QUERY_PATH_CALLBACK* CompletionCallback,
        __in VOID* CompletionContext
        );

public:
    static FN_REQUEST_HANDLER ResolveAdapterId;
    static FN_REQUEST_HANDLER Free;
    static FN_REQUEST_HANDLER BindAddress;
    static FN_REQUEST_HANDLER UnbindAddress;
    static FN_REQUEST_HANDLER BindLuid;

private:
    void LockExclusive();

    void LockShared();

    void Unlock();
};


#endif // _ND_PARTITION_H_
