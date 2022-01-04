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

class Mr
    : public Overlapped<Mr, IND2MemoryRegion>
{
    typedef Overlapped<Mr, IND2MemoryRegion> _Base;

public:
    static const GUID _Guid;

private:
    Adapter& m_Adapter;

    UINT64 m_hMr;
    const void* m_pBuffer;
    SIZE_T m_cbBuffer;

    NDFLTR_MR_KEYS m_Keys;

    LONG m_nBoundWindows;

private:
    Mr& operator =(Mr& rhs);

public:
    Mr(Adapter& adapter);
    ~Mr();

    HRESULT Initialize(HANDLE hFile);

    void* GetInterface(REFIID riid);

    UINT64 GetHandle() const { return m_hMr; }
    static ULONG GetCancelIoctlCode() { return IOCTL_ND_MR_CANCEL_IO; }

    //
    // The following methods support memory windows.  Once the hardware supports
    // memory windows properly these should go away.
    //
    HRESULT BindMr(
        __in Mr* pMr,
        __in_bcount(cbBuffer) const VOID* pBuffer,
        SIZE_T cbBuffer,
        ULONG flags
        );
    HRESULT InvalidateMr(Mr* pMr);

public:
    // *** IND2MemoryRegion methods ***
    STDMETHODIMP Register(
        __in_bcount(cbBuffer) const VOID* pBuffer,
        SIZE_T cbBuffer,
        ULONG flags,
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP Deregister(
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP_(UINT32) GetLocalToken() { return m_Keys.LKey; }

    STDMETHODIMP_(UINT32) GetRemoteToken() { return m_Keys.RKey; }
};

} // namespace ND::v2

namespace v1
{

class Mr : public ListEntry
{
    INDAdapter& m_Adapter;
    IND2MemoryRegion& m_Mr;

private:
    Mr& operator =(Mr& rhs);

public:
    Mr(INDAdapter& adapter, IND2MemoryRegion& mr);
    ~Mr(void);

    HRESULT Register(const void* pBuf, SIZE_T cbBuf, __inout OVERLAPPED* pOverlapped);
    HRESULT Deregister(__inout OVERLAPPED* pOverlapped);
    void CancelOverlappedRequests();

    IND2MemoryRegion* GetMr() const { return &m_Mr; }
};

} // namespace ND::v1
} // namespace ND
