/*
 * Copyright (c) Microsoft Corporation.  All rights reserved.
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

namespace ND
{
namespace v2
{

class Srq
    : public Overlapped<Srq, IND2SharedReceiveQueue>
{
    typedef Overlapped<Srq, IND2SharedReceiveQueue> _Base;

    Adapter& m_Adapter;

    UINT64 m_hSrq;
    ib_srq_handle_t m_uSrq;

public:
    static const GUID _Guid;

private:
    Srq& operator =(Srq& rhs);

public:
    Srq(Adapter& adapter);
    ~Srq();

    HRESULT Initialize(
        HANDLE hFile,
        ULONG queueDepth,
        ULONG maxRequestSge,
        ULONG notifyThreshold,
        USHORT group,
        KAFFINITY affinity
        );

    void* GetInterface(REFIID riid);

    UINT64 GetHandle() const { return m_hSrq; }
    ib_srq_handle_t GetUvpSrq() const { return m_uSrq; }

    static ULONG GetCancelIoctlCode() { return IOCTL_ND_SRQ_CANCEL_IO; }

public:
    // *** IND2SharedReceiveQueue methods ***
    STDMETHODIMP GetNotifyAffinity(
        __out USHORT* pGroup,
        __out KAFFINITY* pAffinity
        );

    STDMETHODIMP Modify(
        ULONG queueDepth,
        ULONG notifyThreshold
        );

    STDMETHODIMP Notify(
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP Receive(
        __in_opt VOID* requestContext,
        __in_ecount_opt(nSge) const ND2_SGE sge[],
        ULONG nSge
        );

private:
    void FreeSrq(ib_api_status_t ibStatus);
};

} // namespace ND::v2
} // namespace ND
