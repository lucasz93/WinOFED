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
#include <ws2tcpip.h>


namespace NetworkDirect
{


///////////////////////////////////////////////////////////////////////////////
//
// HPC Pack 2008 Beta 2 SPI
//
///////////////////////////////////////////////////////////////////////////////

class CAdapter;
class CCq;

class CEndpoint :
    public INDEndpoint
{
    friend class CConnector;

private:
    CEndpoint(void);
    ~CEndpoint(void);

    HRESULT Initialize(
        __in CAdapter* pParent,
        __in CCq* pInboundCq,
        __in CCq* pOutboundCq,
        __in SIZE_T nInboundEntries,
        __in SIZE_T nOutboundEntries,
        __in SIZE_T nInboundSge,
        __in SIZE_T nOutboundSge,
        __in SIZE_T InboundReadLimit,
        __in SIZE_T OutboundReadLimit,
        __out_opt SIZE_T* pMaxInlineData
        );

public:
    static HRESULT Create(
        __in CAdapter* pParent,
        __in CCq* pInboundCq,
        __in CCq* pOutboundCq,
        __in SIZE_T nInboundEntries,
        __in SIZE_T nOutboundEntries,
        __in SIZE_T nInboundSge,
        __in SIZE_T nOutboundSge,
        __in SIZE_T InboundReadLimit,
        __in SIZE_T OutboundReadLimit,
        __out_opt SIZE_T* pMaxInlineData,
        __out INDEndpoint** ppEndpoint
        );

    // *** IUnknown methods ***
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        LPVOID FAR* ppvObj
        );

    ULONG STDMETHODCALLTYPE AddRef(void);

    ULONG STDMETHODCALLTYPE Release(void);

    // *** INDEndpoint methods ***
    HRESULT STDMETHODCALLTYPE Flush(void);

    void STDMETHODCALLTYPE StartRequestBatch(void);

    void STDMETHODCALLTYPE SubmitRequestBatch(void);

    HRESULT STDMETHODCALLTYPE Send(
        __out ND_RESULT* pResult,
        __in_ecount(nSge) const ND_SGE* pSgl,
        __in SIZE_T nSge,
        __in DWORD Flags
        );

    HRESULT STDMETHODCALLTYPE SendAndInvalidate(
        __out ND_RESULT* pResult,
        __in_ecount(nSge) const ND_SGE* pSgl,
        __in SIZE_T nSge,
        __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
        __in DWORD Flags
        );

    HRESULT STDMETHODCALLTYPE Receive(
        __out ND_RESULT* pResult,
        __in_ecount(nSge) const ND_SGE* pSgl,
        __in SIZE_T nSge
        );

    HRESULT STDMETHODCALLTYPE Bind(
        __out ND_RESULT* pResult,
        __in ND_MR_HANDLE hMr,
        __in INDMemoryWindow* pMw,
        __in_bcount(BufferSize) const void* pBuffer,
        __in SIZE_T BufferSize,
        __in DWORD Flags,
        __out ND_MW_DESCRIPTOR* pMwDescriptor
        );

    HRESULT STDMETHODCALLTYPE Invalidate(
        __out ND_RESULT* pResult,
        __in INDMemoryWindow* pMw,
        __in DWORD Flags
        );

    HRESULT STDMETHODCALLTYPE Read(
        __out ND_RESULT* pResult,
        __in_ecount(nSge) const ND_SGE* pSgl,
        __in SIZE_T nSge,
        __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
        __in ULONGLONG Offset,
        __in DWORD Flags
        );

    HRESULT STDMETHODCALLTYPE Write(
        __out ND_RESULT* pResult,
        __in_ecount(nSge) const ND_SGE* pSgl,
        __in SIZE_T nSge,
        __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
        __in ULONGLONG Offset,
        __in DWORD Flags
        );

private:
    HRESULT Rdma(
        __out ND_RESULT* pResult,
        __in ib_wr_type_t Type,
        __in_ecount(nSge) const ND_SGE* pSgl,
        __in SIZE_T nSge,
        __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
        __in ULONGLONG Offset,
        __in DWORD Flags
        );

    HRESULT CreateQp(
        __in CCq* pInboundCq,
        __in CCq* pOutboundCq,
        __in SIZE_T nInboundEntries,
        __in SIZE_T nOutboundEntries,
        __in SIZE_T nInboundSge,
        __in SIZE_T nOutboundSge,
        __in SIZE_T InboundReadLimit,
		__in SIZE_T OutboundReadLimit,
		__in SIZE_T MaxInlineData
        );

    void DestroyQp();

    HRESULT QueryQp(
		__out ib_qp_attr_t *qp_attr
		);

    HRESULT ModifyQp(
        __in ib_qp_state_t NewState
        );

protected:
    volatile LONG m_nRef;

    CAdapter* m_pParent;
    CCq* m_pInboundCq;
    CCq* m_pOutboundCq;

    uint64_t m_hQp;
    ib_qp_handle_t m_uQp;

    net32_t m_Qpn;

    UINT8 m_Ird;
    UINT8 m_Ord;
	UINT32 m_MaxInlineSize;
};

} // namespace
