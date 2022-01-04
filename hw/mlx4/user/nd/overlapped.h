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

template<
    typename ThisT,
    typename InterfaceT
    >
class __declspec(novtable) Overlapped
    : public Unknown<ThisT, InterfaceT>
{
    friend typename ThisT;
    typedef Unknown<ThisT, InterfaceT> _Base;

protected:
    HANDLE m_hFile;

public:
    Overlapped()
        : m_hFile(INVALID_HANDLE_VALUE)
    {
    }

    ~Overlapped()
    {
        if( m_hFile != INVALID_HANDLE_VALUE )
        {
            CloseHandle(m_hFile);
        }
    }

    HRESULT Initialize(HANDLE hFile)
    {
        BOOL ret = DuplicateHandle(
            GetCurrentProcess(),
            hFile,
            GetCurrentProcess(),
            &m_hFile,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS
            );
        if( ret == FALSE )
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

    void* GetInterface(REFIID riid)
    {
        ThisT* pThis = static_cast<ThisT*>( this );
        if( riid == IID_IND2Overlapped )
        {
            return static_cast<IND2Overlapped*>( pThis );
        }

        return _Base::GetInterface(riid);
    }

public:
    STDMETHODIMP CancelOverlappedRequests()
    {
        ND_HANDLE in;
        in.Version = ND_IOCTL_VERSION;
        in.Reserved = 0;
        in.Handle = static_cast<ThisT*>(this)->GetHandle();

        ULONG cbOut = 0;
        return ::Ioctl(m_hFile, ThisT::GetCancelIoctlCode(), &in, sizeof(in), NULL, &cbOut);
    }

    STDMETHODIMP GetOverlappedResult(
        __in OVERLAPPED* pOverlapped,
        BOOL wait
        )
    {
        ULONG cbOut;
        ::GetOverlappedResult(m_hFile, pOverlapped, &cbOut, wait);
        return static_cast<ThisT*>(this)->PostProcessOverlapped( pOverlapped );
    }

    HRESULT PostProcessOverlapped(__in OVERLAPPED* pOverlapped)
    {
        return static_cast<HRESULT>(pOverlapped->Internal);
    }
};

} // namespace ND::v2
} // namespace ND
