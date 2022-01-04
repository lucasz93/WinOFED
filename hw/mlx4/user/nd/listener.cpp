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

Listener::Listener(Adapter& adapter)
    : m_Adapter(adapter),
    m_hListener(0)
{
    adapter.AddRef();
#if DBG
    InterlockedIncrement(&g_nRef[NdListener]);
#endif
}


Listener::~Listener()
{
    if( m_hListener != 0 )
    {
        m_Adapter.GetProvider().FreeHandle(m_hListener, IOCTL_ND_LISTENER_FREE);
    }

    m_Adapter.Release();
#if DBG
    InterlockedDecrement(&g_nRef[NdListener]);
#endif
}


HRESULT Listener::Initialize(HANDLE hFile)
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
    in.Ndv1TimeoutSemantics = FALSE;

    ULONG cbOut = sizeof(m_hListener);
    return m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_LISTENER_CREATE,
        &in,
        sizeof(in),
        &m_hListener,
        &cbOut
        );
}


void* Listener::GetInterface(REFIID riid)
{
    if( riid == IID_IND2Listener )
    {
        return static_cast<IND2Listener*>(this);
    }

    return _Base::GetInterface(riid);
}


STDMETHODIMP Listener::Bind(
    __in_bcount(cbAddress) const struct sockaddr* pAddress,
    ULONG cbAddress
    )
{
    ND_BIND bind;
    bind.Version = ND_IOCTL_VERSION;
    bind.Reserved = 0;
    bind.Handle = m_hListener;
    if( CopyAddress(pAddress, cbAddress, &bind.Address) == false )
    {
        return STATUS_INVALID_ADDRESS;
    }

    return m_Adapter.GetProvider().Ioctl(IOCTL_ND_LISTENER_BIND, &bind, sizeof(bind));
}


STDMETHODIMP Listener::Listen(
    ULONG backlog
    )
{
    ND_LISTEN listen;
    listen.Version = ND_IOCTL_VERSION;
    listen.Backlog = backlog;
    listen.ListenerHandle = m_hListener;

    return m_Adapter.GetProvider().Ioctl(IOCTL_ND_LISTENER_LISTEN, &listen, sizeof(listen));
}


STDMETHODIMP Listener::GetLocalAddress(
    __out_bcount_part_opt(*pcbAddress, *pcbAddress) struct sockaddr* pAddress,
    __inout ULONG* pcbAddress
    )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hListener;

    SOCKADDR_INET out;
    ULONG cbOut = sizeof(out);
    HRESULT hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_LISTENER_GET_ADDRESS,
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


STDMETHODIMP Listener::GetConnectionRequest(
    __inout IUnknown* pConnector,
    __inout OVERLAPPED* pOverlapped
    )
{
    Connector* pConn;
    HRESULT hr = pConnector->QueryInterface(
        Connector::_Guid,
        reinterpret_cast<void**>(&pConn)
        );
    if( FAILED(hr) )
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    ND_GET_CONNECTION_REQUEST in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.ListenerHandle = m_hListener;
    in.ConnectorHandle = pConn->GetHandle();
    pConn->Release();

    return ::IoctlAsync(
        m_hFile,
        IOCTL_ND_LISTENER_GET_CONNECTION_REQUEST,
        &in,
        sizeof(in),
        NULL,
        0,
        pOverlapped
        );
}

} // namespace ND::v2

namespace v1
{

Listener::Listener(IND2Listener& listener)
    : m_Listener(listener)
{
    listener.AddRef();
}


Listener::~Listener(void)
{
    m_Listener.Release();
}


HRESULT Listener::Initialize(
    __in Adapter& adapter,
    __in SIZE_T Backlog,
    __in USHORT Port,
    __out_opt USHORT* pAssignedPort
    )
{
    SOCKADDR_INET address = adapter.GetAddress();
    address.Ipv4.sin_port = Port;
    HRESULT hr = m_Listener.Bind(
        reinterpret_cast<struct sockaddr*>(&address),
        sizeof(address)
        );
    if( FAILED(hr) )
    {
        return hr;
    }

    if( pAssignedPort != NULL )
    {
        ULONG cbAddress = sizeof(address);
        hr = m_Listener.GetLocalAddress(
            reinterpret_cast<struct sockaddr*>(&address),
            &cbAddress
            );
        if( FAILED(hr) )
        {
            return hr;
        }

        *pAssignedPort = address.Ipv4.sin_port;
    }

    return m_Listener.Listen(static_cast<ULONG>(Backlog));
}


HRESULT Listener::Create(
    __in Adapter& adapter,
    __in IND2Listener& listener,
    __in SIZE_T Backlog,
    __in USHORT Port,
    __out_opt USHORT* pAssignedPort,
    __deref_out INDListen** ppListen
    )
{
    Listener* pListen = new Listener(listener);
    if( pListen == NULL )
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pListen->Initialize(
        adapter,
        Backlog,
        Port,
        pAssignedPort );
    if( FAILED( hr ) )
    {
        delete pListen;
        return hr;
    }

    *ppListen = pListen;
    return S_OK;
}


void* Listener::GetInterface(REFIID riid)
{
    if( riid == IID_INDListen )
    {
        return static_cast<INDListen*>(this);
    }

    return _Base::GetInterface(riid);
}


// *** INDListen methods ***
HRESULT Listener::GetConnectionRequest(
    __inout INDConnector* pConnector,
    __inout OVERLAPPED* pOverlapped
    )
{
    return m_Listener.GetConnectionRequest(
        static_cast<Connector*>(pConnector)->GetConnector(),
        pOverlapped
        );
}

} // namespace ND::v1
} // namespace ND
