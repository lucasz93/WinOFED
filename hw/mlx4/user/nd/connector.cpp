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

#include "precomp.h"

namespace ND
{
namespace v2
{

// {4DF1A76E-2A27-428A-8482-AA66C4DADFAE}
const GUID Connector::_Guid = 
{ 0x4df1a76e, 0x2a27, 0x428a, { 0x84, 0x82, 0xaa, 0x66, 0xc4, 0xda, 0xdf, 0xae } };


bool CopyAddress(
    __in_bcount(cbAddress) const struct sockaddr* pAddress,
    ULONG cbAddress,
    __out SOCKADDR_INET* pInetAddress
    )
{
    switch( pAddress->sa_family )
    {
    case AF_INET:
        if( cbAddress < sizeof(pInetAddress->Ipv4) )
        {
            return false;
        }
        RtlCopyMemory(&pInetAddress->Ipv4, pAddress, sizeof(pInetAddress->Ipv4));
        break;

    case AF_INET6:
        if( cbAddress < sizeof(pInetAddress->Ipv6) )
        {
            return false;
        }
        RtlCopyMemory(&pInetAddress->Ipv6, pAddress, sizeof(pInetAddress->Ipv6));
        break;

    default:
        return false;
    }
    return true;
}


HRESULT CopyAddress(
    __in SOCKADDR_INET& inetAddress,
    __out_bcount_part_opt(*pcbAddress, *pcbAddress) struct sockaddr* pAddress,
    __inout ULONG* pcbAddress
    )
{
    if( pAddress == NULL && *pcbAddress != 0 )
    {
        return E_INVALIDARG;
    }

    ULONG cbReq;
    switch( inetAddress.si_family )
    {
    case AF_INET:
        cbReq = sizeof(inetAddress.Ipv4);
        break;

    case AF_INET6:
        cbReq = sizeof(inetAddress.Ipv6);
        break;

    default:
        __assume(0);
    }

    if( *pcbAddress < cbReq )
    {
        *pcbAddress = cbReq;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(pAddress, &inetAddress, cbReq);
    *pcbAddress = cbReq;
    return STATUS_SUCCESS;
}


Connector::Connector(Adapter& adapter)
    : m_Adapter(adapter),
    m_hConnector(0),
    m_pQp(NULL)
{
    adapter.AddRef();
#if DBG
    InterlockedIncrement(&g_nRef[NdConnector]);
#endif
}


Connector::~Connector()
{
    m_Adapter.GetProvider().FreeHandle(m_hConnector, IOCTL_ND_CONNECTOR_FREE);

    if( m_pQp != NULL )
    {
        m_pQp->Release();
    }
    m_Adapter.Release();
#if DBG
    InterlockedDecrement(&g_nRef[NdConnector]);
#endif
}


HRESULT Connector::Initialize(HANDLE hFile, BOOLEAN Ndv1TimeoutSemantics)
{
    HRESULT hr = _Base::Initialize(hFile);
    if( FAILED(hr) )
    {
        return hr;
    }

    NDFLTR_EP_CREATE in;
    in.Header.Version = ND_IOCTL_VERSION;
    in.Header.Reserved = 0;
    in.Header.Handle = m_Adapter.GetHandle();
    in.Ndv1TimeoutSemantics = Ndv1TimeoutSemantics;

    ULONG cbOut = sizeof(m_hConnector);
    return m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_CONNECTOR_CREATE,
        &in,
        sizeof(in),
        &m_hConnector,
        &cbOut
        );
}


void* Connector::GetInterface(REFIID riid)
{
    if( riid == IID_IND2Connector )
    {
        return static_cast<IND2Connector*>(this);
    }

    if( riid == _Guid )
    {
        return this;
    }

    return _Base::GetInterface(riid);
}


HRESULT Connector::PostProcessOverlapped(__in OVERLAPPED* pOverlapped)
{
    if( static_cast<NTSTATUS>(pOverlapped->Internal) == STATUS_IO_TIMEOUT
        && m_pQp != NULL )
    {
        m_pQp->Release();
        m_pQp = NULL;
    }

    return _Base::PostProcessOverlapped(pOverlapped);
}


STDMETHODIMP Connector::Bind(
    __in_bcount(cbAddress) const struct sockaddr* pAddress,
    ULONG cbAddress
    )
{
    ND_BIND bind;
    bind.Version = ND_IOCTL_VERSION;
    bind.Reserved = 0;
    bind.Handle = m_hConnector;
    if( CopyAddress(pAddress, cbAddress, &bind.Address) == false )
    {
        return STATUS_INVALID_ADDRESS;
    }

    return m_Adapter.GetProvider().Ioctl(IOCTL_ND_CONNECTOR_BIND, &bind, sizeof(bind));
}


STDMETHODIMP Connector::Connect(
    __in IUnknown* pQueuePair,
    __in_bcount(cbDestAddress) const struct sockaddr* pDestAddress,
    ULONG cbDestAddress,
    ULONG inboundReadLimit,
    ULONG outboundReadLimit,
    __in_bcount_opt(cbPrivateData) const VOID* pPrivateData,
    ULONG cbPrivateData,
    __inout OVERLAPPED* pOverlapped
    )
{
    if( m_pQp != NULL )
    {
        return STATUS_CONNECTION_ACTIVE;
    }

    NDFLTR_CONNECT in;
    in.Header.Version = ND_IOCTL_VERSION;
    in.Header.Reserved = 0;
    in.Header.ReadLimits.Inbound = inboundReadLimit;
    in.Header.ReadLimits.Outbound = outboundReadLimit;

    if( cbPrivateData > sizeof(in.PrivateData) )
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    in.Header.CbPrivateDataLength = cbPrivateData;
    in.Header.CbPrivateDataOffset = FIELD_OFFSET(NDFLTR_CONNECT, PrivateData);

    RtlCopyMemory(in.PrivateData, pPrivateData, cbPrivateData);

    in.Header.ConnectorHandle = m_hConnector;

    if( CopyAddress(pDestAddress, cbDestAddress, &in.Header.DestinationAddress) == false )
    {
        return STATUS_INVALID_ADDRESS;
    }

    HRESULT hr = pQueuePair->QueryInterface(
        Qp::_Guid,
        reinterpret_cast<void**>(&m_pQp)
        );
    if( FAILED(hr) )
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    in.Header.QpHandle = m_pQp->GetHandle();

    in.RetryCount = _MaxRetryCount;
    in.RnrRetryCount = 0;

    hr = ::IoctlAsync(
        m_hFile,
        IOCTL_ND_CONNECTOR_CONNECT,
        &in,
        sizeof(in),
        NULL,
        0,
        pOverlapped
        );
    if( FAILED(hr) )
    {
        m_pQp->Release();
        m_pQp = NULL;
    }
    return hr;
}


STDMETHODIMP Connector::CompleteConnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    if( m_pQp == NULL )
    {
        return STATUS_CONNECTION_INVALID;
    }

    NDFLTR_COMPLETE_CONNECT in;
    in.Header.Version = ND_IOCTL_VERSION;
    in.Header.Reserved = 0;
    in.Header.Handle = m_hConnector;
    in.RnrNakTimeout = 0;

    void* pOut;
    ULONG cbOut;
    mlx4_nd_modify_qp(m_pQp->GetUvpQp(), &pOut, &cbOut );

    return ::IoctlAsync(
        m_hFile,
        IOCTL_ND_CONNECTOR_COMPLETE_CONNECT,
        &in,
        sizeof(in),
        pOut,
        cbOut,
        pOverlapped
        );
}


STDMETHODIMP Connector::Accept(
    __in IUnknown* pQueuePair,
    ULONG inboundReadLimit,
    ULONG outboundReadLimit,
    __in_bcount_opt(cbPrivateData) const VOID* pPrivateData,
    ULONG cbPrivateData,
    __inout OVERLAPPED* pOverlapped
    )
{
    if( m_pQp != NULL )
    {
        return STATUS_CONNECTION_ACTIVE;
    }

    NDFLTR_ACCEPT in;
    in.Header.Version = ND_IOCTL_VERSION;
    in.Header.Reserved = 0;
    in.Header.ReadLimits.Inbound = inboundReadLimit;
    in.Header.ReadLimits.Outbound = outboundReadLimit;

    if( cbPrivateData > sizeof(in.PrivateData) )
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    in.Header.CbPrivateDataLength = cbPrivateData;
    in.Header.CbPrivateDataOffset = FIELD_OFFSET(NDFLTR_ACCEPT, PrivateData);

    RtlCopyMemory(in.PrivateData, pPrivateData, cbPrivateData);

    in.Header.ConnectorHandle = m_hConnector;

    HRESULT hr = pQueuePair->QueryInterface(
        Qp::_Guid,
        reinterpret_cast<void**>(&m_pQp)
        );
    if( FAILED(hr) )
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    in.Header.QpHandle = m_pQp->GetHandle();

    in.RnrRetryCount = 0;
    in.RnrNakTimeout = 0;

    void* pOut;
    ULONG cbOut;
    mlx4_nd_modify_qp(m_pQp->GetUvpQp(), &pOut, &cbOut );

    hr = ::IoctlAsync(
        m_hFile,
        IOCTL_ND_CONNECTOR_ACCEPT,
        &in,
        sizeof(in),
        pOut,
        cbOut,
        pOverlapped
        );
    if( FAILED(hr) )
    {
        m_pQp->Release();
        m_pQp = NULL;
    }
    return hr;
}


STDMETHODIMP Connector::Reject(
    __in_bcount_opt(cbPrivateData) const VOID* pPrivateData,
    ULONG cbPrivateData
    )
{

    NDFLTR_REJECT in;
    in.Header.Version = ND_IOCTL_VERSION;
    in.Header.Reserved = 0;

    if( cbPrivateData > sizeof(in.PrivateData) )
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    in.Header.CbPrivateDataLength = cbPrivateData;
    in.Header.CbPrivateDataOffset = FIELD_OFFSET(NDFLTR_REJECT, PrivateData);

    RtlCopyMemory(in.PrivateData, pPrivateData, cbPrivateData);

    in.Header.ConnectorHandle = m_hConnector;

    return m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_CONNECTOR_REJECT,
        &in,
        sizeof(in)
        );
}


STDMETHODIMP Connector::GetReadLimits(
    __out_opt ULONG* pInboundReadLimit,
    __out_opt ULONG* pOutboundReadLimit
    )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hConnector;

    ND_READ_LIMITS limits;
    ULONG cbLimits = sizeof(limits);

    HRESULT hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_CONNECTOR_GET_READ_LIMITS,
        &in,
        sizeof(in),
        &limits,
        &cbLimits
        );
    if( SUCCEEDED(hr) )
    {
        if( pInboundReadLimit != NULL )
        {
            *pInboundReadLimit = limits.Inbound;
        }
        if( pOutboundReadLimit != NULL )
        {
            *pOutboundReadLimit = limits.Outbound;
        }
    }
    return hr;
}


STDMETHODIMP Connector::GetPrivateData(
    __out_bcount_opt(*pcbPrivateData) VOID* pPrivateData,
    __inout ULONG* pcbPrivateData
    )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hConnector;

    return m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_CONNECTOR_GET_PRIVATE_DATA,
        &in,
        sizeof(in),
        pPrivateData,
        pcbPrivateData
        );
}


STDMETHODIMP Connector::GetLocalAddress(
    __out_bcount_part_opt(*pcbAddress, *pcbAddress) struct sockaddr* pAddress,
    __inout ULONG* pcbAddress
    )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hConnector;

    SOCKADDR_INET out;
    ULONG cbOut = sizeof(out);
    HRESULT hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_CONNECTOR_GET_ADDRESS,
        &in,
        sizeof(in),
        &out,
        &cbOut
        );
    if( SUCCEEDED(hr) )
    {
        hr = CopyAddress(out, pAddress, pcbAddress);
    }
    return hr;
}


STDMETHODIMP Connector::GetPeerAddress(
    __out_bcount_part_opt(*pcbAddress, *pcbAddress) struct sockaddr* pAddress,
    __inout ULONG* pcbAddress
    )
{
    if( pAddress == NULL && *pcbAddress != 0 )
    {
        return E_INVALIDARG;
    }

    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hConnector;

    SOCKADDR_INET out;
    ULONG cbOut = sizeof(out);
    HRESULT hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_CONNECTOR_GET_PEER_ADDRESS,
        &in,
        sizeof(in),
        &out,
        &cbOut
        );
    if( SUCCEEDED(hr) )
    {
        hr = CopyAddress(out, pAddress, pcbAddress);
    }
    return hr;
}


STDMETHODIMP Connector::NotifyDisconnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hConnector;

    return ::IoctlAsync(
        m_hFile,
        IOCTL_ND_CONNECTOR_NOTIFY_DISCONNECT,
        &in,
        sizeof(in),
        NULL,
        0,
        pOverlapped
        );
}


STDMETHODIMP Connector::Disconnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    if( m_pQp == NULL )
    {
        return STATUS_CONNECTION_INVALID;
    }

    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hConnector;

    void* pOut;
    ULONG cbOut;
    mlx4_nd_modify_qp(m_pQp->GetUvpQp(), &pOut, &cbOut );

    return ::IoctlAsync(
        m_hFile,
        IOCTL_ND_CONNECTOR_DISCONNECT,
        &in,
        sizeof(in),
        pOut,
        cbOut,
        pOverlapped
        );
}

} // namespace ND::v2

namespace v1
{

Connector::Connector(Adapter& adapter, IND2Connector& connector)
    : m_Adapter(adapter),
    m_Connector(connector)
{
    adapter.AddRef();
    connector.AddRef();
}


Connector::~Connector(void)
{
    m_Connector.Release();
    m_Adapter.Release();
}


/*static*/
HRESULT
Connector::Create(
    __in Adapter& adapter,
    __in IND2Connector& connector,
    __deref_out INDConnector** ppConnector
    )
{
    Connector* pConnector = new Connector(adapter, connector);
    if( pConnector == NULL )
    {
        return E_OUTOFMEMORY;
    }

    *ppConnector = pConnector;
    return S_OK;
}


void* Connector::GetInterface(REFIID riid)
{
    if( riid == IID_INDConnector )
    {
        return static_cast<INDConnector*>(this);
    }

    return _Base::GetInterface(riid);
}


HRESULT Connector::CreateEndpoint(
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
    )
{
    IND2QueuePair* pQp;
    HRESULT hr = m_Adapter.GetAdapter()->CreateQueuePair(
        IID_IND2QueuePair,
        static_cast<Cq*>(pInboundCq)->GetCq(),
        static_cast<Cq*>(pOutboundCq)->GetCq(),
        NULL,
        static_cast<ULONG>(nInboundEntries),
        static_cast<ULONG>(nOutboundEntries),
        static_cast<ULONG>(nInboundSge),
        static_cast<ULONG>(nOutboundSge),
        Endpoint::_MaxInline,
        reinterpret_cast<void**>(&pQp)
        );

    if( FAILED(hr) )
    {
        return hr;
    }

    m_InboundReadLimit = static_cast<ULONG>(InboundReadLimit);
    m_OutboundReadLimit = static_cast<ULONG>(OutboundReadLimit);

    return Endpoint::Create(*pQp, pMaxInlineData, ppEndpoint);
}


HRESULT Connector::Connect(
    __inout INDEndpoint* pEndpoint,
    __in_bcount(AddressLength) const struct sockaddr* pAddress,
    __in SIZE_T AddressLength,
    __in INT /*Protocol*/,
    __in_opt USHORT LocalPort,
    __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
    __in SIZE_T PrivateDataLength,
    __inout OVERLAPPED* pOverlapped
    )
{
    SOCKADDR_INET address = m_Adapter.GetAddress();
    //
    // IPv4 and IPv6 ports are in the same place, thankfully.
    //
    address.Ipv4.sin_port = LocalPort;

    HRESULT hr = m_Connector.Bind(
        reinterpret_cast<struct sockaddr*>(&address),
        sizeof(address)
        );
    if( FAILED(hr) )
    {
        return hr;
    }

    return m_Connector.Connect(
        static_cast<Endpoint*>(pEndpoint)->GetQp(),
        pAddress,
        static_cast<ULONG>(AddressLength),
        m_InboundReadLimit,
        m_OutboundReadLimit,
        pPrivateData,
        static_cast<ULONG>(PrivateDataLength),
        pOverlapped
        );
}


HRESULT Connector::CompleteConnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    return m_Connector.CompleteConnect(pOverlapped);
}


HRESULT Connector::Accept(
    __in INDEndpoint* pEndpoint,
    __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
    __in SIZE_T PrivateDataLength,
    __inout OVERLAPPED* pOverlapped
    )
{
    return m_Connector.Accept(
        static_cast<Endpoint*>(pEndpoint)->GetQp(),
        m_InboundReadLimit,
        m_OutboundReadLimit,
        pPrivateData,
        static_cast<ULONG>(PrivateDataLength),
        pOverlapped
        );
}


HRESULT Connector::Reject(
    __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
    __in SIZE_T PrivateDataLength
    )
{
    return m_Connector.Reject(pPrivateData, static_cast<ULONG>(PrivateDataLength));
}


HRESULT Connector::GetConnectionData(
    __out_opt SIZE_T* pInboundReadLimit,
    __out_opt SIZE_T* pOutboundReadLimit,
    __out_bcount_part_opt(*pPrivateDataLength, *pPrivateDataLength) void* pPrivateData,
    __inout SIZE_T* pPrivateDataLength
    )
{
    HRESULT hr = S_OK;
    if( pInboundReadLimit != NULL || pOutboundReadLimit != NULL )
    {
        //
        // Clear the most significant bits so that we can cast to ULONG* to get the
        // lower bits.
        //
        if( pInboundReadLimit != NULL )
        {
            *pInboundReadLimit = 0;
        }
        if( pOutboundReadLimit != NULL )
        {
            *pOutboundReadLimit = 0;
        }
        hr = m_Connector.GetReadLimits(
            reinterpret_cast<ULONG*>(pInboundReadLimit),
            reinterpret_cast<ULONG*>(pOutboundReadLimit)
            );
        if( FAILED(hr) )
        {
            return hr;
        }
    }

    if( pPrivateDataLength != NULL )
    {
        ULONG len = static_cast<ULONG>(min(ULONG_MAX, *pPrivateDataLength));
        hr = m_Connector.GetPrivateData(
            pPrivateData,
            &len
            );
        *pPrivateDataLength = len;
    }
    return hr;
}


HRESULT Connector::GetLocalAddress(
    __out_bcount_part_opt(*pAddressLength, *pAddressLength) struct sockaddr* pAddress,
    __inout SIZE_T* pAddressLength
    )
{
    *pAddressLength = min(*pAddressLength, ULONG_MAX);
    return m_Connector.GetLocalAddress(
        pAddress,
        reinterpret_cast<ULONG*>(pAddressLength)
        );
}


HRESULT Connector::GetPeerAddress(
    __out_bcount_part_opt(*pAddressLength, *pAddressLength) struct sockaddr* pAddress,
    __inout SIZE_T* pAddressLength
    )
{
    *pAddressLength = min(*pAddressLength, ULONG_MAX);
    return m_Connector.GetPeerAddress(
        pAddress,
        reinterpret_cast<ULONG*>(pAddressLength)
        );
}


HRESULT Connector::NotifyDisconnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    return m_Connector.NotifyDisconnect(pOverlapped);
}


HRESULT Connector::Disconnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    return m_Connector.Disconnect(pOverlapped);
}

} // namespace ND::v1
} // namespace ND
