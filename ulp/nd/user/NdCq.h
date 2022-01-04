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
#include <iba/ib_al.h>
#include "al_cq.h"


namespace NetworkDirect
{
    class CAdapter;

    class CCq :
        public INDCompletionQueue
    {
        friend class CEndpoint;
    public:
        CCq(void);
        ~CCq(void);

        HRESULT Initialize(
            CAdapter* pParent,
            SIZE_T nEntries );

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
            __inout_opt OVERLAPPED *pOverlapped,
            __out SIZE_T *pNumberOfBytesTransferred,
            __in BOOL bWait
            );

        // *** INDCompletionQueue methods ***
        HANDLE STDMETHODCALLTYPE GetAdapterFileHandle(void);

        HRESULT STDMETHODCALLTYPE Close(void);

        HRESULT STDMETHODCALLTYPE Resize(
            __in SIZE_T nEntries
            );

        HRESULT STDMETHODCALLTYPE Notify(
            __in DWORD Type,
            __inout_opt OVERLAPPED* pOverlapped
            );

        SIZE_T STDMETHODCALLTYPE GetResults(
            __out_ecount(nResults) ND_RESULT* pResults[],
            __in SIZE_T nResults
            );

    private:
        HRESULT CreateCq(
            __in UINT32 nEntries );

        HRESULT Complete(
            __in HRESULT Status );

        HRESULT ModifyCq(
            __in SIZE_T nEntries );

        void CloseCq(void);

    private:
        volatile LONG m_nRef;

        CAdapter* m_pParent;

        UINT64 m_hCq;
        ib_cq_handle_t m_uCq;
    };

} // namespace
