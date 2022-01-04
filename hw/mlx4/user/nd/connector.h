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

bool CopyAddress(
    __in_bcount(cbAddress) const struct sockaddr* pAddress,
    ULONG cbAddress,
    __out SOCKADDR_INET* pInetAddress
    );

HRESULT CopyAddress(
    __in SOCKADDR_INET& inetAddress,
    __out_bcount_part_opt(*pcbAddress, *pcbAddress) struct sockaddr* pAddress,
    __inout ULONG* pcbAddress
    );

class Connector
    : public Overlapped<Connector, IND2Connector>
{
    typedef Overlapped<Connector, IND2Connector> _Base;

    Adapter& m_Adapter;

    UINT64 m_hConnector;
    Qp* m_pQp;

    static const UINT8 _MaxRetryCount = 6;

public:
    static const GUID _Guid;

private:
    Connector& operator =(Connector& rhs);

public:
    Connector(Adapter& adapter);
    ~Connector();

    HRESULT Initialize(HANDLE hFile, BOOLEAN v1);

    void* GetInterface(REFIID riid);

    HRESULT PostProcessOverlapped(__in OVERLAPPED* pOverlapped);

    UINT64 GetHandle() const { return m_hConnector; }
    static ULONG GetCancelIoctlCode() { return IOCTL_ND_CONNECTOR_CANCEL_IO; }

public:
    // *** IND2Connector methods ***
    STDMETHODIMP Bind(
        __in_bcount(cbAddress) const struct sockaddr* pAddress,
        ULONG cbAddress
        );

    STDMETHODIMP Connect(
        __in IUnknown* pQueuePair,
        __in_bcount(cbDestAddress) const struct sockaddr* pDestAddress,
        ULONG cbDestAddress,
        ULONG inboundReadLimit,
        ULONG outboundReadLimit,
        __in_bcount_opt(cbPrivateData) const VOID* pPrivateData,
        ULONG cbPrivateData,
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP CompleteConnect(
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP Accept(
        __in IUnknown* pQueuePair,
        ULONG inboundReadLimit,
        ULONG outboundReadLimit,
        __in_bcount_opt(cbPrivateData) const VOID* pPrivateData,
        ULONG cbPrivateData,
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP Reject(
        __in_bcount_opt(cbPrivateData) const VOID* pPrivateData,
        ULONG cbPrivateData
        );

    STDMETHODIMP GetReadLimits(
        __out_opt ULONG* pInboundReadLimit,
        __out_opt ULONG* pOutboundReadLimit
        );

    STDMETHODIMP GetPrivateData(
        __out_bcount_opt(*pcbPrivateData) VOID* pPrivateData,
        __inout ULONG* pcbPrivateData
        );

    STDMETHODIMP GetLocalAddress(
        __out_bcount_part_opt(*pcbAddress, *pcbAddress) struct sockaddr* pAddress,
        __inout ULONG* pcbAddress
        );

    STDMETHODIMP GetPeerAddress(
        __out_bcount_part_opt(*pcbAddress, *pcbAddress) struct sockaddr* pAddress,
        __inout ULONG* pcbAddress
        );

    STDMETHODIMP NotifyDisconnect(
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP Disconnect(
        __inout OVERLAPPED* pOverlapped
        );
};

} // namespace ND::v2

namespace v1
{

class Connector
    : public Unknown<Connector, INDConnector>
{
    typedef Unknown<Connector, INDConnector> _Base;

    Adapter& m_Adapter;
    IND2Connector& m_Connector;

    ULONG m_InboundReadLimit;
    ULONG m_OutboundReadLimit;

private:
    Connector& operator =(Connector& rhs);

public:
    Connector(Adapter& adapter, IND2Connector& connector);
    ~Connector(void);

    void* GetInterface(REFIID riid);

    IND2Connector* GetConnector() const { return &m_Connector; }

public:
    static HRESULT Create(
        __in Adapter& adapter,
        __in IND2Connector& connector,
        __deref_out INDConnector** ppConnector
        );

public:
    // *** INDOverlapped methods ***
    HRESULT STDMETHODCALLTYPE CancelOverlappedRequests(void)
    {
        return m_Connector.CancelOverlappedRequests();
    }

    HRESULT STDMETHODCALLTYPE GetOverlappedResult(
        __inout_opt OVERLAPPED *pOverlapped,
        __out SIZE_T *pNumberOfBytesTransferred,
        __in BOOL bWait
        )
    {
        *pNumberOfBytesTransferred = 0;
        return m_Connector.GetOverlappedResult(pOverlapped, bWait);
    }

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
};

} // namespace ND::v1
} // namespace ND
