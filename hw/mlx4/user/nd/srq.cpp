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
const GUID Srq::_Guid = 
{ 0x3186027e, 0x65eb, 0x4ea4, { 0xb4, 0x69, 0xd6, 0xb5, 0xd, 0x7f, 0x5a, 0xe6 } };

Srq::Srq(Adapter& adapter)
    : m_Adapter(adapter),
    m_hSrq(0)
{
    adapter.AddRef();
#if DBG
    InterlockedIncrement(&g_nRef[NdSrq]);
#endif
}


Srq::~Srq(void)
{
    if( m_hSrq != 0 )
    {
        FreeSrq(IB_SUCCESS);
    }
    m_Adapter.Release();
#if DBG
    InterlockedIncrement(&g_nRef[NdSrq]);
#endif
}


HRESULT
Srq::Initialize(
    HANDLE hFile,
    ULONG queueDepth,
    ULONG maxRequestSge,
    ULONG notifyThreshold,
    USHORT group,
    KAFFINITY affinity
    )
{
    HRESULT hr = _Base::Initialize(hFile);
    if( FAILED(hr) )
    {
        return hr;
    }

    struct _createSrq
    {
        union _ioctl
        {
            ND_CREATE_SRQ in;
            ND_RESOURCE_DESCRIPTOR out;
        } ioctl;
        union _dev
        {
            struct ibv_create_srq req;
            struct ibv_create_srq_resp resp;
        } dev;
    } createSrq;

    createSrq.ioctl.in.Version = ND_IOCTL_VERSION;
    createSrq.ioctl.in.QueueDepth = queueDepth;
    createSrq.ioctl.in.CeMappingCount = RTL_NUMBER_OF_V2(createSrq.dev.req.mappings);
    createSrq.ioctl.in.CbMappingsOffset = FIELD_OFFSET(struct _createSrq, dev.req.mappings);
    createSrq.ioctl.in.MaxRequestSge = maxRequestSge;
    createSrq.ioctl.in.NotifyThreshold = notifyThreshold;
    createSrq.ioctl.in.PdHandle = m_Adapter.GetPdHandle();
    createSrq.ioctl.in.Affinity.Group = group;
    createSrq.ioctl.in.Affinity.Mask = affinity;

    ib_srq_attr_t attr;
    attr.max_wr = queueDepth;
    attr.max_sge = maxRequestSge;
    attr.srq_limit = notifyThreshold;

    ci_umv_buf_t umvBuf;
    ib_api_status_t ibStatus = mlx4_pre_create_srq(
        m_Adapter.GetUvpPd(),
        &attr,
        &umvBuf,
        &m_uSrq
        );
    if( ibStatus != IB_SUCCESS )
    {
        return ConvertIbStatus( ibStatus );
    }

    if( umvBuf.input_size != sizeof(createSrq.dev.req) ||
        umvBuf.output_size != sizeof(createSrq.dev.resp) )
    {
        mlx4_post_create_srq(
            m_Adapter.GetUvpPd(),
            IB_INSUFFICIENT_RESOURCES,
            &m_uSrq,
            &umvBuf
            );
        return E_UNEXPECTED;
    }

    RtlCopyMemory(
        &createSrq.dev.req,
        reinterpret_cast<void*>(umvBuf.p_inout_buf),
        sizeof(createSrq.dev.req)
        );

    ULONG cbOut = sizeof(createSrq.ioctl) + sizeof(createSrq.dev.resp);
    hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_SRQ_CREATE,
        &createSrq,
        sizeof(createSrq.ioctl) + sizeof(createSrq.dev.req),
        &createSrq,
        &cbOut
        );
    if( FAILED(hr) )
    {
        mlx4_post_create_srq(
            m_Adapter.GetUvpPd(),
            IB_INSUFFICIENT_RESOURCES,
            &m_uSrq,
            &umvBuf
            );
        return hr;
    }

    m_hSrq = createSrq.ioctl.out.Handle;

    if( cbOut != sizeof(createSrq.ioctl) + sizeof(createSrq.dev.resp) )
    {
        FreeSrq(IB_INSUFFICIENT_RESOURCES);
        mlx4_post_create_srq(
            m_Adapter.GetUvpPd(),
            IB_INSUFFICIENT_RESOURCES,
            &m_uSrq,
            &umvBuf
            );
        return E_UNEXPECTED;
    }

    RtlCopyMemory(
        reinterpret_cast<void*>(umvBuf.p_inout_buf),
        &createSrq.dev.resp,
        sizeof(createSrq.dev.resp)
        );
    mlx4_post_create_srq(
        m_Adapter.GetUvpPd(),
        IB_SUCCESS,
        &m_uSrq,
        &umvBuf
        );

    return hr;
}


void* Srq::GetInterface(REFIID riid)
{
    if( riid == IID_IND2SharedReceiveQueue )
    {
        return static_cast<IND2SharedReceiveQueue*>(this);
    }

    if( riid == _Guid )
    {
        return this;
    }

    return _Base::GetInterface(riid);
}


// *** IND2CompletionQueue methods ***
STDMETHODIMP Srq::GetNotifyAffinity(
    __out USHORT* pGroup,
    __out KAFFINITY* pAffinity
    )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hSrq;

    GROUP_AFFINITY affinity;
    ULONG cbAffinity = sizeof(affinity);
    HRESULT hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_SRQ_GET_AFFINITY,
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


STDMETHODIMP Srq::Modify(
    ULONG /*queueDepth*/,
    ULONG /*notifyThreshold*/
    )
{
    return E_NOTIMPL;
}


STDMETHODIMP Srq::Notify(
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = m_hSrq;

    return ::IoctlAsync(m_hFile, IOCTL_ND_SRQ_NOTIFY, &in, sizeof(in), NULL, 0, pOverlapped);
}


STDMETHODIMP Srq::Receive(
    __in_opt VOID* requestContext,
    __in_ecount_opt(nSge) const ND2_SGE sge[],
    ULONG nSge
    )
{
#ifdef _WIN64
    ib_recv_wr_t wr;
    wr.p_next = NULL;
    wr.wr_id = reinterpret_cast<ULONG_PTR>(requestContext);
    wr.num_ds = nSge;
    wr.ds_array = reinterpret_cast<ib_local_ds_t*>(const_cast<ND2_SGE*>(sge));

    ib_api_status_t ibStatus = mlx4_post_srq_recv( m_uSrq, &wr, NULL );
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
#else
    UNREFERENCED_PARAMETER(requestContext);
    UNREFERENCED_PARAMETER(sge);
    UNREFERENCED_PARAMETER(nSge);
    return E_NOTIMPL;
#endif
}


void Srq::FreeSrq(ib_api_status_t ibStatus)
{
    m_Adapter.GetProvider().FreeHandle(m_hSrq, IOCTL_ND_SRQ_FREE);
    mlx4_post_destroy_srq(m_uSrq, ibStatus);
    m_hSrq = 0;
}

} // namespace ND::v2
} // namespace ND
