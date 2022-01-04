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

class MwOpContext
{
    VOID* m_RequestContext;
    Mw* m_pMw;
    ND2_REQUEST_TYPE m_RequestType;

private:
    MwOpContext();

public:
    MwOpContext( VOID* requestContext, Mw* pMw, ND2_REQUEST_TYPE requestType )
        : m_RequestContext( requestContext ),
        m_pMw( pMw ),
        m_RequestType( requestType )
    {
    }

    ~MwOpContext();

    HRESULT Complete( HRESULT workRequestStatus );
    inline ND2_REQUEST_TYPE GetRequestType() const { return m_RequestType; }
    inline VOID* GetRequestContext() const { return m_RequestContext; }
};


class Qp
    : public Unknown<Qp, IND2QueuePair>
{
    typedef Unknown<Qp, IND2QueuePair> _Base;

public:
    static const GUID _Guid;

private:
    Adapter& m_Adapter;
    Cq& m_ReceiveCq;
    Cq& m_InitiatorCq;
    Srq* m_pSrq;

    UINT64 m_hQp;
    ib_qp_handle_t m_uQp;

private:
    Qp& operator =(Qp& rhs);

public:
    Qp(Adapter& adapter, Cq& receiveCq, Cq& initiatorCq, Srq* pSrq);
    ~Qp();

    HRESULT Initialize(
        __in_opt VOID* context,
        ULONG receiveQueueDepth,
        ULONG initiatorQueueDepth,
        ULONG maxReceiveRequestSge,
        ULONG maxInitiatorRequestSge,
        ULONG inlineDataSize
        );

    void* GetInterface(REFIID riid);

    ib_qp_handle_t GetUvpQp() const { return m_uQp; }

    UINT64 GetHandle() const { return m_hQp; }

    HRESULT PostSend( ib_send_wr_t* pWr );
    //
    // The following method is to workaround lack of MW support in the HW.
    //
    HRESULT PostNoop(__in MwOpContext* requestContext, ULONG flags);

public:
    // *** IND2QueuePair methods ***
    STDMETHODIMP Flush();

    STDMETHODIMP Send(
        __in_opt VOID* requestContext,
        __in_ecount_opt(nSge) const ND2_SGE sge[],
        ULONG nSge,
        ULONG flags
        );

    STDMETHODIMP Receive(
        __in_opt VOID* requestContext,
        __in_ecount_opt(nSge) const ND2_SGE sge[],
        ULONG nSge
        );

    // RemoteToken available thorugh IND2Mw::GetRemoteToken.
    STDMETHODIMP Bind(
        __in_opt VOID* requestContext,
        __in IUnknown* pMemoryRegion,
        __inout IUnknown* pMemoryWindow,
        __in_bcount(cbBuffer) const VOID* pBuffer,
        SIZE_T cbBuffer,
        ULONG flags
        );

    STDMETHODIMP Invalidate(
        __in_opt VOID* requestContext,
        __in IUnknown* pMemoryWindow,
        ULONG flags
        );

    STDMETHODIMP Read(
        __in_opt VOID* requestContext,
        __in_ecount_opt(nSge) const ND2_SGE sge[],
        ULONG nSge,
        UINT64 remoteAddress,
        UINT32 remoteToken,
        ULONG flags
        );

    STDMETHODIMP Write(
        __in_opt VOID* requestContext,
        __in_ecount_opt(nSge) const ND2_SGE sge[],
        ULONG nSge,
        UINT64 remoteAddress,
        UINT32 remoteToken,
        ULONG flags
        );

};

} // namespace ND::v2

namespace v1
{


///////////////////////////////////////////////////////////////////////////////
//
// HPC Pack Beta 2 SPI
//
///////////////////////////////////////////////////////////////////////////////


class Endpoint
    : public Unknown<Endpoint, INDEndpoint>
{
    typedef Unknown<Endpoint, INDEndpoint> _Base;

    IND2QueuePair& m_Qp;
    //Adapter& m_Adapter;
    //Cq& m_InboundCq;
    //Cq& m_OutboundCq;

    //uint64_t m_hQp;
    //ib_qp_handle_t m_uQp;

    //net32_t m_Qpn;

    //UINT8 m_Ird;
    //UINT8 m_Ord;
    //UINT32 m_MaxInlineSize;

public:
    static const ULONG _MaxInline = 160;

private:
    Endpoint& operator =(Endpoint& rhs);

public:
    Endpoint(IND2QueuePair& qp);
    ~Endpoint(void);

    void* GetInterface(REFIID riid);

    IND2QueuePair* GetQp() const { return &m_Qp; }

public:
    static HRESULT Create(
        __in IND2QueuePair& qp,
        __out_opt SIZE_T* pMaxInlineData,
        __out INDEndpoint** ppEndpoint
        );

public:
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

    HRESULT QueryQp(
        __out ib_qp_attr_t *qp_attr
        );

    HRESULT ModifyQp(
        __in ib_qp_state_t NewState
        );

};

} // namespace ND::v1
} // namespace ND
