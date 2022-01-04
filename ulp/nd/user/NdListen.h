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


namespace NetworkDirect
{

HRESULT GetPdataForPassive(
    __in UINT8* pSrc,
    __in SIZE_T SrcLength,
    __out_bcount_part_opt(*pPrivateDataLength, *pPrivateDataLength) void* pPrivateData,
    __inout SIZE_T* pPrivateDataLength
    );

HRESULT GetPdataForActive(
    __in UINT8* pSrc,
    __in SIZE_T SrcLength,
    __out_bcount_part_opt(*pPrivateDataLength, *pPrivateDataLength) void* pPrivateData,
    __inout SIZE_T* pPrivateDataLength
    );

class CAdapter;

class CListen :
    public INDListen
{
private:
    CListen(void);
    ~CListen(void);

    HRESULT Initialize(
        __in CAdapter* pParent,
        __in SIZE_T Backlog,
        __in INT Protocol,
        __in USHORT Port,
        __out_opt USHORT* pAssignedPort
        );

public:
    static HRESULT Create(
        __in CAdapter* pParent,
        __in SIZE_T Backlog,
        __in INT Protocol,
        __in USHORT Port,
        __out_opt USHORT* pAssignedPort,
        __deref_out INDListen** ppListen
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

    // *** INDListen methods ***
    HRESULT STDMETHODCALLTYPE GetConnectionRequest(
        __inout INDConnector* pConnector,
        __inout OVERLAPPED* pOverlapped
        );

private:
    volatile LONG m_nRef;

    CAdapter* m_pParent;

    UINT8 m_Protocol;
    net32_t m_cid;
};

} // namespace
