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
// {3186027E-65EB-4EA4-B469-D6B50D7F5AE6}
const GUID Cq::_Guid = 
{ 0x3186027e, 0x65eb, 0x4ea4, { 0xb4, 0x69, 0xd6, 0xb5, 0xd, 0x7f, 0x5a, 0xe6 } };

Cq::Cq(Adapter& adapter)
    : m_Adapter(adapter),
    m_hCq(0)
{
    adapter.AddRef();
#if DBG
    InterlockedIncrement(&g_nRef[NdCq]);
#endif
}


Cq::~Cq(void)
{
    if( m_hCq != 0 )
    {
        FreeCq(IB_SUCCESS);
    }
    m_Adapter.Release();
#if DBG
    InterlockedDecrement(&g_nRef[NdCq]);
#endif
}


HRESULT
Cq::Initialize(
    HANDLE hFile,
    ULONG queueDepth,
    USHORT group,
    KAFFINITY affinity
    )
{
    HRESULT hr = _Base::Initialize(hFile);
    if( FAILED(hr) )
    {
        return hr;
    }

    struct _createCq
    {
        union _ioctl
        {
            ND_CREATE_CQ in;
            ND_RESOURCE_DESCRIPTOR out;
        } ioctl;
        union _dev
        {
            struct ibv_create_cq req;
            struct ibv_create_cq_resp resp;
        } dev;
    } createCq;

    createCq.ioctl.in.Version = ND_IOCTL_VERSION;
    createCq.ioctl.in.QueueDepth = queueDepth;
    createCq.ioctl.in.CeMappingCount = RTL_NUMBER_OF_V2(createCq.dev.req.mappings);
    createCq.ioctl.in.CbMappingsOffset = FIELD_OFFSET(struct _createCq, dev.req.mappings);
    createCq.ioctl.in.AdapterHandle = m_Adapter.GetHandle();
    createCq.ioctl.in.Affinity.Group = group;
    createCq.ioctl.in.Affinity.Mask = affinity;

    ci_umv_buf_t umvBuf;
    uint32_t size = queueDepth;
    ib_api_status_t ibStatus = mlx4_pre_create_cq(
        m_Adapter.GetUvpCa(),
        &size,
        &umvBuf,
        &m_uCq
        );
    if( ibStatus != IB_SUCCESS )
    {
        return ConvertIbStatus( ibStatus );
    }

    createCq.ioctl.in.QueueDepth = size;

    if( umvBuf.input_size != sizeof(createCq.dev.req) ||
        umvBuf.output_size != sizeof(createCq.dev.resp) )
    {
        mlx4_post_create_cq(
            m_Adapter.GetUvpCa(),
            IB_INSUFFICIENT_RESOURCES,
            queueDepth,
            &m_uCq,
            &umvBuf
            );
        return E_UNEXPECTED;
    }

    RtlCopyMemory(
        &createCq.dev.req,
        reinterpret_cast<void*>(umvBuf.p_inout_buf),
        sizeof(createCq.dev.req)
        );

    ULONG cbOut = sizeof(createCq.ioctl) + sizeof(createCq.dev.resp);
    hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_CQ_CREATE,
        &createCq,
        sizeof(createCq.ioctl) + sizeof(createCq.dev.req),
        &createCq,
        &cbOut
        );
    if( FAILED(hr) )
    {
        mlx4_post_create_cq(
            m_Adapter.GetUvpCa(),
            IB_INSUFFICIENT_RESOURCES,
            queueDepth,
            &m_uCq,
            &umvBuf
            );
        return hr;
    }

    m_hCq = createCq.ioctl.out.Handle;

    if( cbOut != sizeof(createCq.ioctl) + sizeof(createCq.dev.resp) )
    {
        FreeCq(IB_INSUFFICIENT_RESOURCES);
        mlx4_post_create_cq(
            m_Adapter.GetUvpCa(),
            IB_INSUFFICIENT_RESOURCES,
            queueDepth,
            &m_uCq,
            &umvBuf
            );
        return E_UNEXPECTED;
    }

    RtlCopyMemory(
        reinterpret_cast<void*>(umvBuf.p_inout_buf),
        &createCq.dev.resp,
        sizeof(createCq.dev.resp)
        );
    mlx4_post_create_cq(
        m_Adapter.GetUvpCa(),
        IB_SUCCESS,
        queueDepth,
        &m_uCq,
        &umvBuf
        );

    return hr;
}


void* Cq::GetInterface(REFIID riid)
{
    if( riid == IID_IND2CompletionQueue )
    {
        return static_cast<IND2CompletionQueue*>(this);
    }

    if( riid == _Guid )
    {
        return this;
    }

    return _Base::GetInterface(riid);
}


// *** IND2CompletionQueue methods ***
STDMETHODIMP Cq::GetNotifyAffinity(
    __out USHORT* pGroup,
    __out KAFFINITY* pAffinity
    )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hCq;

    GROUP_AFFINITY affinity;
    ULONG cbAffinity = sizeof(affinity);
    HRESULT hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_CQ_GET_AFFINITY,
        &in,
        sizeof(in),
        &affinity,
        &cbAffinity
        );
    if( SUCCEEDED(hr) )
    {
        *pGroup = affinity.Group;
        *pAffinity = affinity.Mask;
    }
    return hr;
}


STDMETHODIMP Cq::Resize(
    ULONG /*queueDepth*/
    )
{
    return E_NOTIMPL;
}


STDMETHODIMP Cq::Notify(
    ULONG type,
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_CQ_NOTIFY in;
    in.Version = ND_IOCTL_VERSION;
    in.Type = type;
    in.CqHandle = m_hCq;

    HRESULT hr = ::IoctlAsync(
        m_hFile,
        IOCTL_ND_CQ_NOTIFY,
        &in,
        sizeof(in),
        NULL,
        0,
        pOverlapped
        );
    if( hr == STATUS_PENDING )
    {
        mlx4_arm_cq(
            m_uCq,
            type == ND_CQ_NOTIFY_SOLICITED? TRUE : FALSE
            );
    }
    return hr;
}


STDMETHODIMP_(ULONG) Cq::GetResults(
    THIS_
    __out_ecount_part(nResults,return) ND2_RESULT results[],
    ULONG nResults
    )
{
    uvp_wc_t wc;

    ULONG i;
    for( i = 0; i < nResults; i++ )
    {
        if( mlx4_poll_cq_array(m_uCq, 1, &wc) != 1 )
        {
            break;
        }

        results[i].Status = DecodeCompletionStatus(wc.status);
        results[i].BytesTransferred = wc.length;
        results[i].QueuePairContext = wc.qp_context;
        if( wc.wc_type == IB_WC_NOP )
        {
            MwOpContext* opContext = reinterpret_cast<MwOpContext*>(wc.wr_id);
            results[i].RequestType = opContext->GetRequestType();
            results[i].RequestContext = opContext->GetRequestContext();
            results[i].Status = opContext->Complete( results[i].Status );
            delete opContext;
        }
        else
        {
            results[i].RequestContext = reinterpret_cast<void*>(wc.wr_id);
            results[i].RequestType = DecodeCompletionType(wc.wc_type);
        }
    }
    return i;
}


void Cq::FreeCq(ib_api_status_t ibStatus)
{
    m_Adapter.GetProvider().FreeHandle(m_hCq, IOCTL_ND_CQ_FREE);

    mlx4_post_destroy_cq(m_uCq, ibStatus);

    m_hCq = 0;
}

} // namespace ND::v2

namespace v1
{

Cq::Cq(IND2CompletionQueue& cq)
    : _Base(),
    m_Cq(cq)
{
    cq.AddRef();
}


/*static*/
HRESULT Cq::Create(IND2CompletionQueue& cq, INDCompletionQueue** ppCq)
{
    Cq* pCq = new Cq(cq);
    if( pCq == NULL )
    {
        return E_OUTOFMEMORY;
    }

    *ppCq = pCq;
    return S_OK;
}


void* Cq::GetInterface(REFIID riid)
{
    if( riid == IID_INDOverlapped )
    {
        return static_cast<INDOverlapped*>(this);
    }

    if( riid == IID_INDCompletionQueue )
    {
        return static_cast<INDCompletionQueue*>(this);
    }

    return _Base::GetInterface(riid);
}


SIZE_T Cq::GetResults(
    __out_ecount(nResults) ND_RESULT* pResults[],
    __in SIZE_T nResults
    )
{
    ND2_RESULT result;

    ULONG i;
    for( i = 0; i < nResults; i++ )
    {
        if( m_Cq.GetResults(&result, 1) != 1 )
        {
            break;
        }

        pResults[i] = reinterpret_cast<ND_RESULT*>(result.RequestContext);
        pResults[i]->Status = result.Status;
        if( result.RequestType == Nd2RequestTypeReceive )
        {
            pResults[i]->BytesTransferred = result.BytesTransferred;
        }
    }
    return i;
}

} // namespace ND::v1
} // namespace ND
