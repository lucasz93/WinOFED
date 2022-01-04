/*
 * Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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

#include "ndspi.h"
#include "nddebug.h"
#include <iba/ib_al.h>
#include <iba/ib_at_ioctl.h>
#include <ws2tcpip.h>
#include "ual_ci_ca.h"


namespace NetworkDirect
{

class CConnector :
    public INDConnector
{
    friend class CListen;

private:
    CConnector(void);
    ~CConnector(void);
    void FreeCid(void);

public:
    static HRESULT Create(
        __in CAdapter* pParent,
        __deref_out INDConnector** ppConnector
        );

    // *** IUnknown methods ***
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        LPVOID FAR* ppvObj
        );

    ULONG STDMETHODCALLTYPE AddRef(void);

    ULONG STDMETHODCALLTYPE Release(void);

    // *** INDOverlapped methods ***
    HRESULT STDMETHODCALLTYPE CancelOverlappedRequests(void);

    HRESULT STDMETHODCALLTYPE GetOverlappedResult(
        __inout OVERLAPPED *pOverlapped,
        __out SIZE_T *pNumberOfBytesTransferred,
        __in BOOL bWait
        );

    // *** INDConnector methods ***
    HRESULT STDMETHODCALLTYPE CreateEndpoint(
        __in INDCompletionQueue* pInboundCq,
        __in INDCompletionQueue* pOutboundCq,
        __in SIZE_T nInboundEntries,
        __in SIZE_T nOutboundEntries,
        __in SIZE_T nInboundSge,
        __in SIZE_T nOutboundSge,
        __in SIZE_T InboundReadLimit,
        __in SIZE_T OutboundReadLimit,
        __out_opt SIZE_T* pMaxInlineData,
        __deref_out INDEndpoint** ppEndpoint
        );

    HRESULT STDMETHODCALLTYPE Connect(
        __in INDEndpoint* pEndpoint,
        __in_bcount(AddressLength) const struct sockaddr* pAddress,
        __in SIZE_T AddressLength,
        __in INT Protocol,
        __in_opt USHORT LocalPort,
        __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
        __in SIZE_T PrivateDataLength,
        __inout OVERLAPPED* pOverlapped
        );

    HRESULT STDMETHODCALLTYPE CompleteConnect(
        __inout OVERLAPPED* pOverlapped
        );

    HRESULT STDMETHODCALLTYPE Accept(
        __in INDEndpoint* pEndpoint,
        __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
        __in SIZE_T PrivateDataLength,
        __inout OVERLAPPED* pOverlapped
        );

    HRESULT STDMETHODCALLTYPE Reject(
        __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
        __in SIZE_T PrivateDataLength
        );

    HRESULT STDMETHODCALLTYPE GetConnectionData(
        __out_opt SIZE_T* pInboundReadLimit,
        __out_opt SIZE_T* pOutboundReadLimit,
        __out_bcount_part_opt(*pPrivateDataLength, *pPrivateDataLength) void* pPrivateData,
        __inout SIZE_T* pPrivateDataLength
        );

    HRESULT STDMETHODCALLTYPE GetLocalAddress(
        __out_bcount_part_opt(*pAddressLength, *pAddressLength) struct sockaddr* pAddress,
        __inout SIZE_T* pAddressLength
        );

    HRESULT STDMETHODCALLTYPE GetPeerAddress(
        __out_bcount_part_opt(*pAddressLength, *pAddressLength) struct sockaddr* pAddress,
        __inout SIZE_T* pAddressLength
        );

    HRESULT STDMETHODCALLTYPE NotifyDisconnect(
        __inout_opt OVERLAPPED* pOverlapped
        );

    HRESULT STDMETHODCALLTYPE Disconnect(
        __inout OVERLAPPED* pOverlapped
        );

private:
    HRESULT GetAddressFromPdata(
        __out_bcount_part(*pAddressLength, *pAddressLength) struct sockaddr* pAddress,
        __inout SIZE_T* pAddressLength
        );

protected:
        volatile LONG m_nRef;

        CAdapter* m_pParent;

        CEndpoint* m_pEndpoint;

        UINT8 m_Protocol;
        USHORT m_LocalPort;
        net32_t m_cid;
        bool m_fActive;

        union _addr
        {
            struct sockaddr_in v4;
            struct sockaddr_in6 v6;

        } m_PeerAddr;
};

}
