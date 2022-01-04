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

#ifndef _ND_ADAPTER_H_
#define _ND_ADAPTER_H_


#define ND_ADAPTER_POOL_TAG 'dadn'

class ND_ADAPTER
    : public RdmaParent<ND_PROVIDER, ib_ca_handle_t, ci_interface_t>
{
    typedef RdmaParent<ND_PROVIDER, ib_ca_handle_t, ci_interface_t> _Base;

    ND_RDMA_DEVICE  *m_pDevice;

    LIST_ENTRY      m_PdList;
    LIST_ENTRY      m_CqList;
    LIST_ENTRY      m_EpList;

    UINT64          m_Id;

private:
    ND_ADAPTER(
        __in ND_PROVIDER* pProvider,
        __in ND_RDMA_DEVICE* pDevice,
        __in UINT64 AdapterId
        );
    ~ND_ADAPTER();

    NTSTATUS Initialize(
        KPROCESSOR_MODE RequestorMode,
        __inout ci_umv_buf_t* pVerbsData
        );

public:
    static
    NTSTATUS
    Create(
        __in ND_PROVIDER* pProvider,
        UINT64 AdapterId,
        KPROCESSOR_MODE RequestorMode,
        __inout ci_umv_buf_t* pVerbsData,
        __out ND_ADAPTER** ppAdapter
        );

    void Dispose();

    void RemoveHandler(__in ND_RDMA_DEVICE* pDevice);

    ND_PROVIDER* GetProvider() const { return m_pParent; }
    ND_RDMA_DEVICE* GetDevice() const { return m_pDevice; }

    void AddCq(LIST_ENTRY* entry) { AddResource(&m_CqList, entry); }
    void RemoveCq(LIST_ENTRY* entry) { RemoveResource(entry); }
    void AddPd(LIST_ENTRY* entry) { AddResource(&m_PdList, entry); }
    void RemovePd(LIST_ENTRY* entry) { RemoveResource(entry); }
    void AddEp(LIST_ENTRY* entry) { AddResource(&m_EpList, entry); }
    void RemoveEp(LIST_ENTRY* entry) { RemoveResource(entry); }

    NTSTATUS
    GetDeviceAddress(
        __in const SOCKADDR_INET& Addr,
        __out IBAT_PORT_RECORD* pDeviceAddress
        );

public:
    static FN_REQUEST_HANDLER QueryAddressList;
    static FN_REQUEST_HANDLER Query;

};

FN_REQUEST_HANDLER NdAdapterOpen;
FN_REQUEST_HANDLER NdAdapterClose;

#endif // __ND_ADAPTER_H_
