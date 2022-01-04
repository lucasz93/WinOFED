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

MwOpContext::~MwOpContext()
{
    m_pMw->Release();
}


HRESULT MwOpContext::Complete( HRESULT workRequestStatus )
{
    if( m_RequestType == Nd2RequestTypeInvalidate && SUCCEEDED(workRequestStatus) )
    {
        return m_pMw->Invalidate();
    }
    return workRequestStatus;
}


// {47D27C8F-9E28-4D44-AECD-C31E13E81D7E}
const GUID Qp::_Guid = 
{ 0x47d27c8f, 0x9e28, 0x4d44, { 0xae, 0xcd, 0xc3, 0x1e, 0x13, 0xe8, 0x1d, 0x7e } };

Qp::Qp(Adapter& adapter, Cq& receiveCq, Cq& initiatorCq, Srq* pSrq)
    : m_Adapter(adapter),
    m_ReceiveCq(receiveCq),
    m_InitiatorCq(initiatorCq),
    m_pSrq(pSrq),
    m_hQp(0),
    m_uQp(NULL)
{
    adapter.AddRef();
    initiatorCq.AddRef();
    receiveCq.AddRef();
    if( pSrq != NULL )
    {
        pSrq->AddRef();
    }
#if DBG
    InterlockedIncrement(&g_nRef[NdQp]);
#endif
}


Qp::~Qp()
{
    if( m_hQp != 0 )
    {
        m_Adapter.GetProvider().FreeHandle(m_hQp, IOCTL_ND_QP_FREE);
        mlx4_post_destroy_qp(m_uQp, IB_SUCCESS);
    }

    if( m_pSrq != NULL )
    {
        m_pSrq->Release();
    }

    m_ReceiveCq.Release();
    m_InitiatorCq.Release();
    m_Adapter.Release();
#if DBG
    InterlockedDecrement(&g_nRef[NdQp]);
#endif
}


HRESULT Qp::Initialize(
    __in_opt VOID* context,
    ULONG receiveQueueDepth,
    ULONG initiatorQueueDepth,
    ULONG maxReceiveRequestSge,
    ULONG maxInitiatorRequestSge,
    ULONG inlineDataSize
    )
{
    uvp_qp_create_t attr;
    attr.context = context;
    attr.qp_create.qp_type = IB_QPT_RELIABLE_CONN;
    attr.qp_create.sq_max_inline = inlineDataSize;
    attr.qp_create.sq_depth = initiatorQueueDepth;
    attr.qp_create.rq_depth = receiveQueueDepth;
    attr.qp_create.sq_sge = maxInitiatorRequestSge;
    attr.qp_create.rq_sge = maxReceiveRequestSge;
    attr.qp_create.h_sq_cq = m_InitiatorCq.GetUvpCq();
    attr.qp_create.h_rq_cq = m_ReceiveCq.GetUvpCq();
    attr.qp_create.sq_signaled = FALSE;

    struct _createQp
    {
        union _ioctl
        {
            ND_CREATE_QP_HDR hdr;
            ND_CREATE_QP in;
            ND_CREATE_QP_WITH_SRQ inSrq;
            ND_RESOURCE_DESCRIPTOR out;
        } ioctl;
        union _dev
        {
            ibv_create_qp req;
            ibv_create_qp_resp resp;
        } dev;
    } createQp;

    createQp.ioctl.hdr.Version = ND_IOCTL_VERSION;
    createQp.ioctl.hdr.CbMaxInlineData = inlineDataSize;
    createQp.ioctl.hdr.CeMappingCount = RTL_NUMBER_OF_V2(createQp.dev.req.mappings);
    createQp.ioctl.hdr.CbMappingsOffset = FIELD_OFFSET(struct _createQp, dev.req.mappings);
    createQp.ioctl.hdr.InitiatorQueueDepth = initiatorQueueDepth;
    createQp.ioctl.hdr.MaxInitiatorRequestSge = maxInitiatorRequestSge;
    createQp.ioctl.hdr.ReceiveCqHandle = m_ReceiveCq.GetHandle();
    createQp.ioctl.hdr.InitiatorCqHandle = m_InitiatorCq.GetHandle();;
    createQp.ioctl.hdr.PdHandle = m_Adapter.GetPdHandle();

    DWORD ioctlCode;
    if( m_pSrq != NULL )
    {
        ioctlCode = IOCTL_ND_QP_CREATE_WITH_SRQ;
        attr.qp_create.h_srq = m_pSrq->GetUvpSrq();
        createQp.ioctl.inSrq.SrqHandle = m_pSrq->GetHandle();
    }
    else
    {
        ioctlCode = IOCTL_ND_QP_CREATE;
        attr.qp_create.h_srq = NULL;
        createQp.ioctl.in.ReceiveQueueDepth = receiveQueueDepth;
        createQp.ioctl.in.MaxReceiveRequestSge = maxReceiveRequestSge;
    }

    ci_umv_buf_t umvBuf;
    ib_api_status_t ibStatus = mlx4_wv_pre_create_qp(
        m_Adapter.GetUvpPd(),
        &attr,
        &umvBuf,
        &m_uQp
        );
    if( ibStatus != IB_SUCCESS )
    {
        return ConvertIbStatus( ibStatus );
    }

    if( umvBuf.input_size != sizeof(createQp.dev.req) ||
        umvBuf.output_size != sizeof(createQp.dev.resp) )
    {
        mlx4_post_create_qp(
            m_Adapter.GetUvpPd(),
            IB_INSUFFICIENT_RESOURCES,
            &m_uQp,
            &umvBuf
            );
        return E_UNEXPECTED;
    }

    RtlCopyMemory(
        &createQp.dev.req,
        reinterpret_cast<void*>(umvBuf.p_inout_buf),
        sizeof(createQp.dev.req)
        );

    ULONG cbOut = sizeof(createQp.ioctl) + sizeof(createQp.dev.resp);
    HRESULT hr = m_Adapter.GetProvider().Ioctl(
        ioctlCode,
        &createQp,
        sizeof(createQp.ioctl) + sizeof(createQp.dev.req),
        &createQp,
        &cbOut
        );
    if( FAILED(hr) )
    {
        mlx4_post_create_qp(
            m_Adapter.GetUvpPd(),
            IB_INSUFFICIENT_RESOURCES,
            &m_uQp,
            &umvBuf
            );
        return hr;
    }

    m_hQp = createQp.ioctl.out.Handle;

    if( cbOut != sizeof(createQp.ioctl) + sizeof(createQp.dev.resp) )
    {
        m_Adapter.GetProvider().FreeHandle(m_hQp, IOCTL_ND_QP_FREE);
        mlx4_post_create_qp(
            m_Adapter.GetUvpPd(),
            IB_INSUFFICIENT_RESOURCES,
            &m_uQp,
            &umvBuf
            );
        m_hQp = 0;
        return E_UNEXPECTED;
    }

    RtlCopyMemory(
        reinterpret_cast<void*>(umvBuf.p_inout_buf),
        &createQp.dev.resp,
        sizeof(createQp.dev.resp)
        );
    mlx4_post_create_qp(
        m_Adapter.GetUvpPd(),
        IB_SUCCESS,
        &m_uQp,
        &umvBuf
        );

    //
    // QPs are created in the INIT state by the kernel filter, but there isn't a way
    // to have the kernel call update the QP state automatically, so update it here.
    //
    enum ibv_qp_state* pState;
    mlx4_nd_modify_qp(m_uQp, reinterpret_cast<void**>(&pState), &cbOut );
    *pState = IBV_QPS_INIT;

    return hr;
}


void* Qp::GetInterface(REFIID riid)
{
    if( riid == IID_IND2QueuePair )
    {
        return static_cast<IND2QueuePair*>(this);
    }

    if( riid == _Guid )
    {
        return this;
    }

    return _Base::GetInterface(riid);
}


HRESULT Qp::PostSend(ib_send_wr_t* pWr)
{
    ib_api_status_t ibStatus = mlx4_post_send( m_uQp, pWr, NULL );
    switch( ibStatus )
    {
    case IB_SUCCESS:
        return S_OK;
    case IB_INSUFFICIENT_RESOURCES:
        return ND_NO_MORE_ENTRIES;
    case IB_INVALID_MAX_SGE:
        return ND_DATA_OVERRUN;
    case IB_INVALID_QP_STATE:
        return ND_CONNECTION_INVALID;
    default:
        return ND_UNSUCCESSFUL;
    }
}


HRESULT Qp::PostNoop(__in MwOpContext* requestContext, ULONG flags)
{
    ib_send_wr_t wr;
    wr.p_next = NULL;
    wr.wr_id = reinterpret_cast<ULONG_PTR>(requestContext);
    wr.wr_type = WR_NOP;
    if( (flags & ND_OP_FLAG_SILENT_SUCCESS) != 0 )
    {
        wr.send_opt = 0;
    }
    else
    {
        wr.send_opt = IB_SEND_OPT_SIGNALED;
    }
    if( (flags & ND_OP_FLAG_READ_FENCE) != 0 )
    {
        wr.send_opt |= IB_SEND_OPT_FENCE;
    }
    wr.num_ds = 0;
    wr.ds_array = NULL;
    wr.remote_ops.vaddr = 0;
    wr.remote_ops.rkey = 0;

    return PostSend(&wr);
}


STDMETHODIMP Qp::Flush()
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hQp;

    void* pOut;
    ULONG cbOut;
    mlx4_nd_modify_qp(m_uQp, &pOut, &cbOut );

    return m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_QP_FLUSH,
        &in,
        sizeof(in),
        pOut,
        &cbOut
        );
}


STDMETHODIMP Qp::Send(
    __in_opt VOID* requestContext,
    __in_ecount_opt(nSge) const ND2_SGE sge[],
    ULONG nSge,
    ULONG flags
    )
{
    ib_send_wr_t wr;
    wr.p_next = NULL;
    wr.wr_id = reinterpret_cast<ULONG_PTR>(requestContext);
    wr.wr_type = WR_SEND;
    if( (flags & ND_OP_FLAG_SILENT_SUCCESS) != 0 )
    {
        wr.send_opt = 0;
    }
    else
    {
        wr.send_opt = IB_SEND_OPT_SIGNALED;
    }
    if( (flags & ND_OP_FLAG_READ_FENCE) != 0 )
    {
        wr.send_opt |= IB_SEND_OPT_FENCE;
    }
    if( (flags & ND_OP_FLAG_SEND_AND_SOLICIT_EVENT) != 0 )
    {
        wr.send_opt |= IB_SEND_OPT_SOLICITED;
    }
    if( (flags & ND_OP_FLAG_INLINE) != 0 )
    {
        wr.send_opt |= IB_SEND_OPT_INLINE;
    }
    wr.num_ds = nSge;
#ifdef _WIN64
    wr.ds_array = reinterpret_cast<ib_local_ds_t*>(const_cast<ND2_SGE*>(sge));
#else
    ib_local_ds_t ds[2];
    switch( nSge )
    {
    case 0:
        wr.ds_array = NULL;
        break;

    case 2:
        ds[1].vaddr = reinterpret_cast<ULONG_PTR>(sge[1].Buffer);
        ds[1].length = sge[1].BufferLength;
        ds[1].lkey = sge[1].MemoryRegionToken;
        __fallthrough;

    case 1:
        ds[0].vaddr = reinterpret_cast<ULONG_PTR>(sge[0].Buffer);
        ds[0].length = sge[0].BufferLength;
        ds[0].lkey = sge[0].MemoryRegionToken;
        wr.ds_array = ds;
        break;

    default:
        return E_NOTIMPL;
    }
#endif

    return PostSend(&wr);
}


STDMETHODIMP Qp::Receive(
    __in_opt VOID* requestContext,
    __in_ecount_opt(nSge) const ND2_SGE sge[],
    ULONG nSge
    )
{
    ib_recv_wr_t wr;
    wr.p_next = NULL;
    wr.wr_id = reinterpret_cast<ULONG_PTR>(requestContext);
    wr.num_ds = nSge;
#ifdef _WIN64
    wr.ds_array = reinterpret_cast<ib_local_ds_t*>(const_cast<ND2_SGE*>(sge));
#else
    ib_local_ds_t ds;
    switch( nSge )
    {
    case 0:
        wr.ds_array = NULL;
        break;

    case 1:
        ds.vaddr = reinterpret_cast<ULONG_PTR>(sge[0].Buffer);
        ds.length = sge[0].BufferLength;
        ds.lkey = sge[0].MemoryRegionToken;
        wr.ds_array = &ds;
        break;

    default:
        return E_NOTIMPL;
    }
#endif

    ib_api_status_t ibStatus = mlx4_post_recv( m_uQp, &wr, NULL );
    switch( ibStatus )
    {
    case IB_SUCCESS:
        return S_OK;
    case IB_INSUFFICIENT_RESOURCES:
        return ND_NO_MORE_ENTRIES;
    case IB_INVALID_MAX_SGE:
        return ND_DATA_OVERRUN;
    case IB_INVALID_QP_STATE:
        return ND_CONNECTION_INVALID;
    default:
        return ND_UNSUCCESSFUL;
    }
}


// RemoteToken available thorugh IND2Mw::GetRemoteToken.
STDMETHODIMP Qp::Bind(
    __in_opt VOID* requestContext,
    __in IUnknown* pMemoryRegion,
    __inout IUnknown* pMemoryWindow,
    __in_bcount(cbBuffer) const VOID* pBuffer,
    SIZE_T cbBuffer,
    ULONG flags
    )
{
    Mr* pMr;
    HRESULT hr = pMemoryRegion->QueryInterface(Mr::_Guid, reinterpret_cast<void**>(&pMr));
    if( FAILED(hr) )
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    Mw* pMw;
    hr = pMemoryWindow->QueryInterface(Mw::_Guid, reinterpret_cast<void**>(&pMw));
    if( FAILED(hr) )
    {
        pMr->Release();
        return STATUS_INVALID_PARAMETER_3;
    }

    MwOpContext* pOpContext = new MwOpContext( requestContext, pMw, Nd2RequestTypeBind );
    if( pOpContext == NULL )
    {
        pMw->Release();
        pMr->Release();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    hr = pMw->Bind(pMr, pBuffer, cbBuffer, flags);
    pMr->Release();

    if( SUCCEEDED(hr) )
    {
        hr = PostNoop(pOpContext, flags);
        if( FAILED(hr) )
        {
            pMw->Invalidate();
            delete pOpContext;
        }
    }
    else
    {
        delete pOpContext;
    }
    return hr;
}


STDMETHODIMP Qp::Invalidate(
    __in_opt VOID* requestContext,
    __in IUnknown* pMemoryWindow,
    ULONG flags
    )
{
    Mw* pMw;
    HRESULT hr = pMemoryWindow->QueryInterface(Mw::_Guid, reinterpret_cast<void**>(&pMw));
    if( FAILED(hr) )
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    MwOpContext* pOpContext = new MwOpContext(requestContext, pMw, Nd2RequestTypeInvalidate );
    if( pOpContext == NULL )
    {
        pMw->Release();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    hr = PostNoop(pOpContext, flags);
    if( FAILED(hr) )
    {
        delete pOpContext;
    }
    return hr;
}


STDMETHODIMP Qp::Read(
    __in_opt VOID* requestContext,
    __in_ecount_opt(nSge) const ND2_SGE sge[],
    ULONG nSge,
    UINT64 remoteAddress,
    UINT32 remoteToken,
    ULONG flags
    )
{
    ib_send_wr_t wr;
    wr.p_next = NULL;
    wr.wr_id = reinterpret_cast<ULONG_PTR>(requestContext);
    wr.wr_type = WR_RDMA_READ;
    if( (flags & ND_OP_FLAG_SILENT_SUCCESS) != 0 )
    {
        wr.send_opt = 0;
    }
    else
    {
        wr.send_opt = IB_SEND_OPT_SIGNALED;
    }
    if( (flags & ND_OP_FLAG_READ_FENCE) != 0 )
    {
        wr.send_opt |= IB_SEND_OPT_FENCE;
    }
    wr.num_ds = nSge;
#ifdef _WIN64
    wr.ds_array = reinterpret_cast<ib_local_ds_t*>(const_cast<ND2_SGE*>(sge));
#else
    ib_local_ds_t ds;
    switch( nSge )
    {
    case 0:
        wr.ds_array = NULL;
        break;

    case 1:
        ds.vaddr = reinterpret_cast<ULONG_PTR>(sge[0].Buffer);
        ds.length = sge[0].BufferLength;
        ds.lkey = sge[0].MemoryRegionToken;
        wr.ds_array = &ds;
        break;

    default:
        return E_NOTIMPL;
    }
#endif
    wr.remote_ops.vaddr = remoteAddress;
    wr.remote_ops.rkey = remoteToken;

    return PostSend(&wr);
}


STDMETHODIMP Qp::Write(
    __in_opt VOID* requestContext,
    __in_ecount_opt(nSge) const ND2_SGE sge[],
    ULONG nSge,
    UINT64 remoteAddress,
    UINT32 remoteToken,
    ULONG flags
    )
{
    ib_send_wr_t wr;
    wr.p_next = NULL;
    wr.wr_id = reinterpret_cast<ULONG_PTR>(requestContext);
    wr.wr_type = WR_RDMA_WRITE;
    if( (flags & ND_OP_FLAG_SILENT_SUCCESS) != 0 )
    {
        wr.send_opt = 0;
    }
    else
    {
        wr.send_opt = IB_SEND_OPT_SIGNALED;
    }
    if( (flags & ND_OP_FLAG_READ_FENCE) != 0 )
    {
        wr.send_opt |= IB_SEND_OPT_FENCE;
    }
    if( (flags & ND_OP_FLAG_INLINE) != 0 )
    {
        wr.send_opt |= IB_SEND_OPT_INLINE;
    }
    wr.num_ds = nSge;
#ifdef _WIN64
    wr.ds_array = reinterpret_cast<ib_local_ds_t*>(const_cast<ND2_SGE*>(sge));
#else
    ib_local_ds_t ds;
    switch( nSge )
    {
    case 0:
        wr.ds_array = NULL;
        break;

    case 1:
        ds.vaddr = reinterpret_cast<ULONG_PTR>(sge[0].Buffer);
        ds.length = sge[0].BufferLength;
        ds.lkey = sge[0].MemoryRegionToken;
        wr.ds_array = &ds;
        break;

    default:
        return E_NOTIMPL;
    }
#endif
    wr.remote_ops.vaddr = remoteAddress;
    wr.remote_ops.rkey = remoteToken;

    return PostSend(&wr);
}

} // namespace ND::v2

namespace v1
{

///////////////////////////////////////////////////////////////////////////////
//
// HPC Pack Beta 2 SPI
//
///////////////////////////////////////////////////////////////////////////////


Endpoint::Endpoint(IND2QueuePair& qp)
    : m_Qp(qp)
{
}


Endpoint::~Endpoint(void)
{
    m_Qp.Release();
}


HRESULT Endpoint::Create(
    __in IND2QueuePair& qp,
    __out_opt SIZE_T* pMaxInlineData,
    __out INDEndpoint** ppEndpoint
    )
{
    Endpoint* pEp = new Endpoint(qp);
    if( pEp == NULL )
    {
        return E_OUTOFMEMORY;
    }

    if( pMaxInlineData != NULL )
    {
        *pMaxInlineData = _MaxInline;
    }

    *ppEndpoint = pEp;
    return S_OK;
}


void* Endpoint::GetInterface(REFIID riid)
{
    if( riid == IID_INDEndpoint )
    {
        return static_cast<INDEndpoint*>(this);
    }

    return _Base::GetInterface(riid);
}


// *** INDEndpoint methods ***
HRESULT Endpoint::Flush(void)
{
    return m_Qp.Flush();
}


void Endpoint::StartRequestBatch(void)
{
    return;
}


void Endpoint::SubmitRequestBatch(void)
{
    return;
}


static __forceinline
void
TranslateSge(
    __in SIZE_T nSge,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __out_ecount(nSge) ND2_SGE sge[],
    __out ND_RESULT* pResult
    )
{
    ASSERT(nSge <= Adapter::_MaxSge);

    pResult->BytesTransferred = 0;
    for( SIZE_T i = 0; i < nSge; i++ )
    {
        sge[i].Buffer = pSgl[i].pAddr;
        sge[i].BufferLength = static_cast<ULONG>(pSgl[i].Length);
        sge[i].MemoryRegionToken = reinterpret_cast<Mr*>(pSgl[i].hMr)->GetMr()->GetLocalToken();
        // Send completions don't include the length.  It's going to
        // be all or nothing, so store it now and we can reset if the
        // request fails.
        pResult->BytesTransferred += pSgl[i].Length;
    }
}


HRESULT Endpoint::Send(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge,
    __in DWORD Flags
    )
{
    ND2_SGE sge[Adapter::_MaxSge];
    TranslateSge(nSge, pSgl, sge, pResult);

    if( pResult->BytesTransferred <= _MaxInline )
    {
        Flags |= ND_OP_FLAG_INLINE;
    }

    return m_Qp.Send(pResult, sge, static_cast<ULONG>(nSge), Flags);
}


HRESULT Endpoint::SendAndInvalidate(
    __out ND_RESULT* /*pResult*/,
    __in_ecount(nSge) const ND_SGE* /*pSgl*/,
    __in SIZE_T /*nSge*/,
    __in const ND_MW_DESCRIPTOR* /*pRemoteMwDescriptor*/,
    __in DWORD /*Flags*/
    )
{
    return E_NOTIMPL;
}


HRESULT Endpoint::Receive(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge
    )
{
    ND2_SGE sge[Adapter::_MaxSge];
    TranslateSge(nSge, pSgl, sge, pResult);

    return m_Qp.Receive(pResult, sge, static_cast<ULONG>(nSge));
}


HRESULT Endpoint::Bind(
    __out ND_RESULT* pResult,
    __in ND_MR_HANDLE hMr,
    __in INDMemoryWindow* pMw,
    __in_bcount(BufferSize) const void* pBuffer,
    __in SIZE_T BufferSize,
    __in DWORD Flags,
    __out ND_MW_DESCRIPTOR* pMwDescriptor
    )
{
    HRESULT hr = m_Qp.Bind(
        pResult,
        reinterpret_cast<Mr*>(hMr)->GetMr(),
        static_cast<Mw*>(pMw)->GetMw(),
        pBuffer,
        BufferSize,
        Flags
        );
    if( SUCCEEDED(hr) )
    {
        pMwDescriptor->Base = _byteswap_uint64( (UINT64)(ULONG_PTR)pBuffer );
        pMwDescriptor->Length = _byteswap_uint64( BufferSize );
        pMwDescriptor->Token = static_cast<Mw*>(pMw)->GetMw()->GetRemoteToken();
    }
    return hr;
}


HRESULT Endpoint::Invalidate(
    __out ND_RESULT* pResult,
    __in INDMemoryWindow* pMw,
    __in DWORD Flags
    )
{
    return m_Qp.Invalidate(pResult, static_cast<Mw*>(pMw)->GetMw(), Flags);
}


HRESULT Endpoint::Read(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge,
    __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
    __in ULONGLONG Offset,
    __in DWORD Flags
    )
{
    ND2_SGE sge[Adapter::_MaxSge];
    TranslateSge(nSge, pSgl, sge, pResult);

    return m_Qp.Read(
        pResult,
        sge,
        static_cast<ULONG>(nSge),
        _byteswap_uint64(pRemoteMwDescriptor->Base) + Offset,
        pRemoteMwDescriptor->Token,
        Flags
        );
}


HRESULT Endpoint::Write(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge,
    __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
    __in ULONGLONG Offset,
    __in DWORD Flags
    )
{
    ND2_SGE sge[Adapter::_MaxSge];
    TranslateSge(nSge, pSgl, sge, pResult);

    if( pResult->BytesTransferred <= _MaxInline )
    {
        Flags |= ND_OP_FLAG_INLINE;
    }

    return m_Qp.Write(
        pResult,
        sge,
        static_cast<ULONG>(nSge),
        _byteswap_uint64(pRemoteMwDescriptor->Base) + Offset,
        pRemoteMwDescriptor->Token,
        Flags
        );
}

} // namespace ND::v1
} // namespace ND
