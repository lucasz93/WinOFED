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

Provider::Provider()
    : _Base(),
    m_hProvider(INVALID_HANDLE_VALUE)
{
    InterlockedIncrement(&g_nRef[NdProvider]);
}


Provider::~Provider()
{
    if( m_hProvider != INVALID_HANDLE_VALUE )
    {
        CloseHandle(m_hProvider);
    }
    InterlockedDecrement(&g_nRef[NdProvider]);
}


HRESULT Provider::Create( __in REFIID riid, __out void** ppProvider )
{
    Provider* pProvider = new Provider();
    if( pProvider == NULL )
    {
        return E_OUTOFMEMORY;
    }

    pProvider->m_hProvider = CreateFileW(
        ND_WIN32_DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
    if( pProvider->m_hProvider == INVALID_HANDLE_VALUE )
    {
        delete pProvider;
        return HRESULT_FROM_WIN32(GetLastError());
    }

    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = 0;
    HRESULT hr = pProvider->Ioctl(IOCTL_ND_PROVIDER_INIT, &in, sizeof(in));
    if( SUCCEEDED(hr) )
    {
        hr = pProvider->QueryInterface(riid, ppProvider);
    }
    pProvider->Release();
    return hr;
}


void* Provider::GetInterface( REFIID riid )
{
    if( IsEqualIID(riid, IID_IND2Provider) )
    {
        return static_cast<IND2Provider*>(this);
    }

    return _Base::GetInterface(riid);
}


HRESULT Provider::Ioctl(
    __in ULONG IoControlCode,
    __in_bcount(cbInput) void* pInput,
    __in ULONG cbInput,
    __out_bcount_part_opt(*pcbOutput,*pcbOutput) void* pOutput,
    __inout ULONG* pcbOutput
    )
{
    return ::Ioctl(
        m_hProvider,
        IoControlCode,
        pInput,
        cbInput,
        pOutput,
        pcbOutput
        );
}


HRESULT Provider::Ioctl(
    __in ULONG IoControlCode,
    __in_bcount(cbInput) void* pInput,
    __in ULONG cbInput
    )
{
    ULONG cbOut = 0;
    return ::Ioctl(
        m_hProvider,
        IoControlCode,
        pInput,
        cbInput,
        NULL,
        &cbOut
        );
}


void Provider::FreeHandle( UINT64 handle, ULONG ctlCode )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = handle;
    HRESULT hr = Ioctl(ctlCode, &in, sizeof(in));
    ASSERT( SUCCEEDED(hr) );
    UNREFERENCED_PARAMETER(hr);
}


HRESULT Provider::QueryAddressList(
        __out_bcount_part_opt(*pcbAddressList, *pcbAddressList) SOCKET_ADDRESS_LIST* pAddressList,
        __inout ULONG* pcbAddressList )
{
    NDFLTR_QUERY_ADDRESS_LIST query;
    query.Header.Version = ND_IOCTL_VERSION;
    query.Header.Reserved = 0;
    query.Header.Handle = 0;
    query.DriverId = GUID_MLX4_DRIVER;

    ULONG cbAddresses = 0;
    HRESULT hr = Ioctl(
        IOCTL_ND_PROVIDER_QUERY_ADDRESS_LIST,
        &query,
        sizeof(query),
        NULL,
        &cbAddresses
        );
    if( hr != STATUS_BUFFER_OVERFLOW )
    {
        return hr;
    }

    INT nAddresses = cbAddresses / sizeof(SOCKADDR_INET);
    ULONG cbListHeader = 
        sizeof(SOCKET_ADDRESS_LIST) +
        (sizeof(SOCKET_ADDRESS) * (nAddresses - 1));
    ULONG cbAddressList = cbListHeader + cbAddresses;

    if( *pcbAddressList < cbAddressList )
    {
        *pcbAddressList = cbAddressList;
        return STATUS_BUFFER_OVERFLOW;
    }

    SOCKADDR_INET* pAddresses = reinterpret_cast<SOCKADDR_INET*>(
        reinterpret_cast<ULONG_PTR>(pAddressList) + cbListHeader
        );

    hr = Ioctl(
        IOCTL_ND_PROVIDER_QUERY_ADDRESS_LIST,
        &query,
        sizeof(query),
        pAddresses,
        &cbAddresses
        );
    if( SUCCEEDED(hr) )
    {
        pAddressList->iAddressCount = nAddresses;
        for( INT i = 0; i < nAddresses; i++ )
        {
            pAddressList->Address[i].iSockaddrLength = sizeof(SOCKADDR_INET);
            pAddressList->Address[i].lpSockaddr = reinterpret_cast<SOCKADDR*>(&pAddresses[i]);
        }
    }
    return hr;
}


HRESULT Provider::ResolveAddress(
    __in_bcount(cbAddress) const struct sockaddr* pAddress,
    ULONG cbAddress,
    __out UINT64* pAdapterId
    )
{
    NDFLTR_RESOLVE_ADDRESS resolve;
    resolve.Header.Version = ND_IOCTL_VERSION;
    resolve.Header.Reserved = 0;

    if( cbAddress > sizeof(resolve.Header.Address) )
    {
        return STATUS_INVALID_ADDRESS;
    }

    CopyMemory(&resolve.Header.Address, pAddress, cbAddress);
    resolve.DriverId = GUID_MLX4_DRIVER;

    ULONG outLen = sizeof(*pAdapterId);
    return Ioctl(
        IOCTL_ND_PROVIDER_RESOLVE_ADDRESS,
        &resolve,
        sizeof(resolve),
        pAdapterId,
        &outLen
        );
}


HRESULT Provider::OpenAdapter(
    __in REFIID iid,
    UINT64 adapterId,
    __deref_out VOID** ppAdapter
    )
{
    Adapter* pAdapter = new Adapter(*this);
    if( pAdapter == NULL )
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pAdapter->Initialize(adapterId);
    if( SUCCEEDED(hr) )
    {
        hr = pAdapter->QueryInterface(iid, ppAdapter);
        pAdapter->Release();
    }
    return hr;
}


}   // namespace ND::v2

namespace v1
{

Provider::Provider()
    : _Base(),
    m_pProvider(NULL)
{
    InterlockedIncrement(&g_nRef[NdProvider]);
}


Provider::~Provider()
{
    if( m_pProvider != NULL )
    {
        m_pProvider->Release();
    }
    InterlockedDecrement(&g_nRef[NdProvider]);
}


HRESULT Provider::Create(__out void** ppProvider)
{
    Provider* pProvider = new Provider();
    if( pProvider == NULL )
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = v2::Provider::Create(
        IID_IND2Provider,
        reinterpret_cast<void**>(&pProvider->m_pProvider)
        );
    if( SUCCEEDED(hr) )
    {
        hr = pProvider->QueryInterface(IID_INDProvider, ppProvider);
    }

    pProvider->Release();
    return hr;
}


void* Provider::GetInterface( REFIID riid )
{
    if( IsEqualIID(riid, IID_INDProvider) )
    {
        return static_cast<INDProvider*>(this);
    }

    return _Base::GetInterface(riid);
}


HRESULT Provider::QueryAddressList(
        __out_bcount_part_opt(*pBufferSize, *pBufferSize) SOCKET_ADDRESS_LIST* pAddressList,
        __inout SIZE_T* pBufferSize )
{
    ULONG cbBuffer = static_cast<ULONG>(min(ULONG_MAX, *pBufferSize));

    HRESULT hr = m_pProvider->QueryAddressList(pAddressList, &cbBuffer);
    *pBufferSize = cbBuffer;
    if( SUCCEEDED(hr) )
    {
        //
        // For NDv1 the address lengths are expected to be exact.
        //
        for( INT i = 0; i < pAddressList->iAddressCount; i++ )
        {
            if( pAddressList->Address[i].lpSockaddr->sa_family == AF_INET )
            {
                pAddressList->Address[i].iSockaddrLength = sizeof(sockaddr_in);
            }
        }
    }
    return hr;
}


HRESULT Provider::OpenAdapter(
    __in_bcount(AddressLength) const struct sockaddr* pAddress,
    __in SIZE_T AddressLength,
    __deref_out INDAdapter** ppAdapter
    )
{
    UINT64 adapterId;
    HRESULT hr = m_pProvider->ResolveAddress(
        pAddress,
        static_cast<ULONG>(AddressLength),
        &adapterId
        );
    if( FAILED(hr) )
    {
        return hr;
    }

    v2::Adapter* pAdapter;
    hr = m_pProvider->OpenAdapter(
        v2::Adapter::_Guid,
        adapterId,
        reinterpret_cast<void**>(&pAdapter)
        );
    if( SUCCEEDED(hr) )
    {
        hr = Adapter::Create(*this, *pAdapter, pAddress, AddressLength, ppAdapter);
        pAdapter->Release();
    }

    return hr;
}

} // namespace ND::v1
} // namespace ND
