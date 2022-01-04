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
// {CD4D60B4-CEBC-49F5-A78E-C234659EC7EF}
const GUID Mr::_Guid = 
{ 0xcd4d60b4, 0xcebc, 0x49f5, { 0xa7, 0x8e, 0xc2, 0x34, 0x65, 0x9e, 0xc7, 0xef } };

Mr::Mr(Adapter& adapter)
    : m_Adapter(adapter),
    m_hMr(0),
    m_pBuffer(NULL),
    m_cbBuffer(0),
    m_nBoundWindows(0)
{
    adapter.AddRef();
#if DBG
    InterlockedIncrement(&g_nRef[NdMr]);
#endif
}


Mr::~Mr()
{
    if( m_hMr != 0 )
    {
        m_Adapter.GetProvider().FreeHandle(m_hMr, IOCTL_ND_MR_FREE);
    }

    m_Adapter.Release();
#if DBG
    InterlockedDecrement(&g_nRef[NdMr]);
#endif
}


HRESULT Mr::Initialize(HANDLE hFile)
{
    HRESULT hr = _Base::Initialize(hFile);
    if( FAILED(hr) )
    {
        return hr;
    }

    union _createMr
    {
        ND_HANDLE   in;
        UINT64      out;
    } createMr;

    createMr.in.Version = ND_IOCTL_VERSION;
    createMr.in.Reserved = 0;
    createMr.in.Handle = m_Adapter.GetPdHandle();

    //
    // TODO: At some point, a MPT entry should be allocated here, which would give us the keys.
    //
    ULONG cbOut = sizeof(createMr);
    hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_MR_CREATE,
        &createMr,
        sizeof(createMr),
        &createMr,
        &cbOut);
    if( SUCCEEDED(hr) )
    {
        m_hMr = createMr.out;
    }
    return hr;
}


void* Mr::GetInterface(REFIID riid)
{
    if( riid == IID_IND2MemoryRegion )
    {
        return static_cast<IND2MemoryRegion*>(this);
    }

    if( riid == _Guid )
    {
        return this;
    }

    return _Base::GetInterface(riid);
}


HRESULT Mr::BindMr(
    __in Mr* pMr,
    __in_bcount(cbBuffer) const VOID* pBuffer,
    SIZE_T cbBuffer,
    ULONG flags
    )
{
    if( pBuffer < m_pBuffer )
    {
        return STATUS_INVALID_PARAMETER;
    }

    SIZE_T offset = reinterpret_cast<SIZE_T>(pBuffer) - reinterpret_cast<SIZE_T>(m_pBuffer);
    if( offset + cbBuffer > m_cbBuffer )
    {
        return STATUS_INVALID_PARAMETER;
    }

    ULONG mrFlags = ND_MR_FLAG_DO_NOT_SECURE_VM;
    if( flags & ND_OP_FLAG_ALLOW_READ )
    {
        mrFlags |= ND_MR_FLAG_ALLOW_REMOTE_READ;
    }
    if( flags & ND_OP_FLAG_ALLOW_WRITE )
    {
        mrFlags |= ND_MR_FLAG_ALLOW_REMOTE_WRITE;
    }

    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if( ov.hEvent == NULL )
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    ov.hEvent = reinterpret_cast<HANDLE>(
        reinterpret_cast<SIZE_T>(ov.hEvent) | 1
        );

    HRESULT hr = pMr->Register(pBuffer, cbBuffer, mrFlags, &ov);
    if( hr == STATUS_PENDING )
    {
        WaitForSingleObject(ov.hEvent, INFINITE);
    }
    CloseHandle(ov.hEvent);
    hr = static_cast<HRESULT>(ov.Internal);
    if( SUCCEEDED(hr) )
    {
        InterlockedIncrement(&m_nBoundWindows);
    }
    return hr;
}


HRESULT Mr::InvalidateMr(Mr* pMr)
{
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if( ov.hEvent == NULL )
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    ov.hEvent = reinterpret_cast<HANDLE>(
        reinterpret_cast<SIZE_T>(ov.hEvent) | 1
        );

    HRESULT hr = pMr->Deregister(&ov);
    if( hr == STATUS_PENDING )
    {
        WaitForSingleObject(ov.hEvent, INFINITE);
    }
    CloseHandle(ov.hEvent);
    hr = static_cast<HRESULT>(ov.Internal);
    if( SUCCEEDED(hr) )
    {
        InterlockedDecrement(&m_nBoundWindows);
    }
    return hr;
}



STDMETHODIMP Mr::Register(
    __in_bcount(cbBuffer) const VOID* pBuffer,
    SIZE_T cbBuffer,
    ULONG flags,
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_MR_REGISTER in;
    in.Header.Version = ND_IOCTL_VERSION;
    in.Header.Flags = flags;
    in.Header.CbLength = cbBuffer;
    in.Header.TargetAddress = reinterpret_cast<ULONG_PTR>(pBuffer);
    in.Header.MrHandle = m_hMr;
    in.Address = in.Header.TargetAddress;

    HRESULT hr = ::IoctlAsync(
        m_hFile,
        IOCTL_ND_MR_REGISTER,
        &in,
        sizeof(in),
        &m_Keys,
        sizeof(m_Keys),
        pOverlapped
        );
    if( SUCCEEDED(hr) )
    {
        m_pBuffer = pBuffer;
        m_cbBuffer = cbBuffer;
    }
    return hr;
}


STDMETHODIMP Mr::Deregister(
    __inout OVERLAPPED* pOverlapped
    )
{
    if( m_nBoundWindows != 0 )
    {
        return STATUS_DEVICE_BUSY;
    }

    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hMr;

    HRESULT hr = ::IoctlAsync(
        m_hFile,
        IOCTL_ND_MR_DEREGISTER,
        &in,
        sizeof(in),
        NULL,
        0,
        pOverlapped
        );
    if( SUCCEEDED(hr) )
    {
        m_pBuffer = NULL;
        m_cbBuffer = 0;
    }
    return hr;
}

} // namespace ND::v2

namespace v1
{

Mr::Mr(INDAdapter& adapter, IND2MemoryRegion& mr)
    : m_Adapter(adapter),
    m_Mr(mr)
{
    adapter.AddRef();
    mr.AddRef();
}


Mr::~Mr(void)
{
    m_Adapter.Release();
    m_Mr.Release();
}


HRESULT Mr::Register(const void* pBuf, SIZE_T cbBuf, __in OVERLAPPED* pOverlapped)
{
    return m_Mr.Register(
        pBuf,
        cbBuf,
        ND_MR_FLAG_ALLOW_LOCAL_WRITE,
        pOverlapped
        );
}


HRESULT Mr::Deregister(__in OVERLAPPED* pOverlapped)
{
    return m_Mr.Deregister(pOverlapped);
}


void Mr::CancelOverlappedRequests()
{
    m_Mr.CancelOverlappedRequests();
}

} // namespace ND::v1
} // namespace ND
