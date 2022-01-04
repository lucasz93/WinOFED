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

class Listener
    : public Overlapped<Listener, IND2Listener>
{
    typedef Overlapped<Listener, IND2Listener> _Base;

    Adapter& m_Adapter;

    UINT64 m_hListener;

private:
    Listener& operator =(Listener& rhs);

public:
    Listener(Adapter& adapter);
    ~Listener();

    HRESULT Initialize(HANDLE hFile);

    void* GetInterface(REFIID riid);

    UINT64 GetHandle() const { return m_hListener; }
    static ULONG GetCancelIoctlCode() { return IOCTL_ND_LISTENER_CANCEL_IO; }

public:
    // *** IND2Listen methods ***
    STDMETHODIMP Bind(
        __in_bcount(cbAddress) const struct sockaddr* pAddress,
        ULONG cbAddress
        );

    STDMETHODIMP Listen(
        ULONG backlog
        );

    STDMETHODIMP GetLocalAddress(
        __out_bcount_part_opt(*pcbAddress, *pcbAddress) struct sockaddr* pAddress,
        __inout ULONG* pcbAddress
        );

    STDMETHODIMP GetConnectionRequest(
        __inout IUnknown* pConnector,
        __inout OVERLAPPED* pOverlapped
        );
};

} // namespace ND::v2

namespace v1
{

class Listener
    : public Unknown<Listener, INDListen>
{
    typedef Unknown<Listener, INDListen> _Base;

    IND2Listener& m_Listener;

private:
    Listener(IND2Listener& listener);

    HRESULT Initialize(
        __in Adapter& adapter,
        __in SIZE_T Backlog,
        __in USHORT Port,
        __out_opt USHORT* pAssignedPort
        );

    Listener& operator =(Listener& rhs);

public:
    ~Listener(void);

    static HRESULT Create(
        __in Adapter& adapter,
        __in IND2Listener& listener,
        __in SIZE_T Backlog,
        __in USHORT Port,
        __out_opt USHORT* pAssignedPort,
        __deref_out INDListen** ppListen
        );

    void* GetInterface(REFIID riid);

public:
    // *** INDOverlapped methods ***
    HRESULT STDMETHODCALLTYPE CancelOverlappedRequests(void)
    {
        return m_Listener.CancelOverlappedRequests();
    }

    HRESULT STDMETHODCALLTYPE GetOverlappedResult(
        __inout_opt OVERLAPPED *pOverlapped,
        __out SIZE_T *pNumberOfBytesTransferred,
        __in BOOL bWait
        )
    {
        *pNumberOfBytesTransferred = 0;
        HRESULT hr = m_Listener.GetOverlappedResult(pOverlapped, bWait);
        if( hr == STATUS_CONNECTION_DISCONNECTED )
        {
            hr = STATUS_CANCELLED;
        }
        return hr;
    }

    // *** INDListen methods ***
    HRESULT STDMETHODCALLTYPE GetConnectionRequest(
        __inout INDConnector* pConnector,
        __inout OVERLAPPED* pOverlapped
        );
};

} // namespace ND::v1
} // namespace ND
