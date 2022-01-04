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

#include "NdEndpoint.h"
#include "NdCq.h"
#include "NdAdapter.h"
#include "NdMr.h"
#include "NdListen.h"
#include "limits.h"
#include "al_dev.h"
#pragma warning( push, 3 )
#include "winternl.h"
#pragma warning( pop )
#include "nddebug.h"

extern uint32_t			g_nd_max_inline_size;

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "NdEndpoint.tmh"
#endif

#if DBG
dbg_data g = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
#endif

#ifndef SIZE_MAX
#ifdef _WIN64
#define SIZE_MAX _UI64_MAX
#else
#define SIZE_MAX UINT_MAX
#endif
#endif

namespace NetworkDirect
{

///////////////////////////////////////////////////////////////////////////////
//
// HPC Pack 2008 Beta 2 SPI
//
///////////////////////////////////////////////////////////////////////////////


CEndpoint::CEndpoint(void) :
    m_nRef( 1 ),
    m_pParent( NULL ),
    m_pInboundCq( NULL ),
    m_pOutboundCq( NULL ),
    m_hQp( 0 )
{
}

CEndpoint::~CEndpoint(void)
{
    if( m_hQp )
        DestroyQp();

    if( m_pInboundCq != NULL )
    {
        m_pInboundCq->Release();
    }

    if( m_pOutboundCq != NULL )
    {
        m_pOutboundCq->Release();
    }

    if( m_pParent )
        m_pParent->Release();
}

HRESULT CEndpoint::Initialize(
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
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( InboundReadLimit > UCHAR_MAX )
        return ND_INVALID_PARAMETER_8;

    if( OutboundReadLimit > UCHAR_MAX )
        return ND_INVALID_PARAMETER_9;

    m_pParent = pParent;
    m_pParent->AddRef();

    m_pInboundCq = pInboundCq;
    m_pInboundCq->AddRef();

    m_pOutboundCq = pOutboundCq;
    m_pOutboundCq->AddRef();

    CL_ASSERT(
        m_pParent->m_Ifc.user_verbs.pre_create_qp != NULL ||
        m_pParent->m_Ifc.user_verbs.post_create_qp != NULL ||
        m_pParent->m_Ifc.user_verbs.nd_modify_qp != NULL ||
        m_pParent->m_Ifc.user_verbs.nd_get_qp_state != NULL ||
        m_pParent->m_Ifc.user_verbs.pre_destroy_qp != NULL ||
        m_pParent->m_Ifc.user_verbs.post_destroy_qp != NULL ||
        m_pParent->m_Ifc.user_verbs.post_query_qp != NULL ||
        m_pParent->m_Ifc.user_verbs.post_send != NULL ||
        m_pParent->m_Ifc.user_verbs.post_recv != NULL /*||
        m_pParent->m_Ifc.user_verbs.bind_mw != NULL*/ );

	m_MaxInlineSize = g_nd_max_inline_size;
		
    HRESULT hr = CreateQp(
        pInboundCq,
        pOutboundCq,
        nInboundEntries,
        nOutboundEntries,
        nInboundSge,
        nOutboundSge,
        InboundReadLimit,
        OutboundReadLimit,
        m_MaxInlineSize );

    if( FAILED( hr ) )
        return hr;

	ib_qp_attr_t qp_attr;
	hr = QueryQp(&qp_attr);
	if( FAILED( hr ) ) {
		DestroyQp();
		return hr;
	}
	else
		m_MaxInlineSize = (UINT32)qp_attr.sq_max_inline;
	
	
    m_Ird = (UINT8)InboundReadLimit;
    m_Ord = (UINT8)OutboundReadLimit;

    // Move the QP to the INIT state so users can post receives.
    hr = ModifyQp( IB_QPS_INIT );
    if( FAILED( hr ) )
        DestroyQp();

    if( SUCCEEDED( hr ) && pMaxInlineData != NULL )
        *pMaxInlineData = m_MaxInlineSize;

    return hr;
}

HRESULT CEndpoint::Create(
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
    )
{
    CEndpoint* pEp = new CEndpoint();
    if( pEp == NULL )
        return ND_NO_MEMORY;

    HRESULT hr = pEp->Initialize(
        pParent,
        pInboundCq,
        pOutboundCq,
        nInboundEntries,
        nOutboundEntries,
        nInboundSge,
        nOutboundSge,
        InboundReadLimit,
        OutboundReadLimit,
        pMaxInlineData
        );

    if( FAILED( hr ) )
    {
        pEp->Release();
        return hr;
    }

    *ppEndpoint = pEp;
    return ND_SUCCESS;
}

HRESULT CEndpoint::QueryInterface(
    REFIID riid,
    LPVOID FAR* ppvObj
    )
{
    if( IsEqualIID( riid, IID_IUnknown ) )
    {
        *ppvObj = this;
        return S_OK;
    }

    if( IsEqualIID( riid, IID_INDEndpoint ) )
    {
        *ppvObj = this;
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG CEndpoint::AddRef(void)
{
    return InterlockedIncrement( &m_nRef );
}

ULONG CEndpoint::Release(void)
{
    ULONG ref = InterlockedDecrement( &m_nRef );
    if( ref == 0 )
        delete this;

    return ref;
}

// *** INDEndpoint methods ***
HRESULT CEndpoint::Flush(void)
{
    return ModifyQp( IB_QPS_ERROR );
}

void CEndpoint::StartRequestBatch(void)
{
    return;
}

void CEndpoint::SubmitRequestBatch(void)
{
    return;
}

HRESULT CEndpoint::Send(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge,
    __in DWORD Flags
    )
{
    ib_send_wr_t wr;
    ib_local_ds_t* pDs;
    ib_local_ds_t ds[4];

    if( nSge > UINT_MAX )
        return ND_DATA_OVERRUN;
    else if( nSge <= 4 )
        pDs = ds;
    else
    {
        pDs = new ib_local_ds_t[nSge];
        if( !pDs )
            return ND_NO_MEMORY;
    }

    pResult->BytesTransferred = 0;
    for( SIZE_T i = 0; i < nSge; i++ )
    {
        pDs[i].vaddr = (ULONG_PTR)pSgl[i].pAddr;
        if( pSgl[i].Length > UINT_MAX )
        {
            if( nSge > 4 )
                delete[] pDs;
            return ND_BUFFER_OVERFLOW;
        }
        pDs[i].length = (uint32_t)pSgl[i].Length;
        pDs[i].lkey = ((CMr*)pSgl[i].hMr)->mr_ioctl.out.lkey;

        // Send completions don't include the length.  It's going to
        // be all or nothing, so store it now and we can reset if the
        // request fails.
        pResult->BytesTransferred += pSgl[i].Length;
    }

    wr.p_next = NULL;
    wr.wr_id = (ULONG_PTR)pResult;
    wr.wr_type = WR_SEND;
    if ( pResult->BytesTransferred <= m_MaxInlineSize )
	    wr.send_opt = IB_SEND_OPT_INLINE; 
	else
	    wr.send_opt = 0;
    
    if( !(Flags & ND_OP_FLAG_SILENT_SUCCESS) )
        wr.send_opt |= IB_SEND_OPT_SIGNALED;
    if( Flags & ND_OP_FLAG_READ_FENCE )
        wr.send_opt |= IB_SEND_OPT_FENCE;
    if( Flags & ND_OP_FLAG_SEND_AND_SOLICIT_EVENT )
        wr.send_opt |= IB_SEND_OPT_SOLICITED;
    wr.num_ds = (uint32_t)nSge;
    wr.ds_array = pDs;

    ib_api_status_t status =
        m_pParent->m_Ifc.user_verbs.post_send( m_uQp, &wr, NULL );
    if( nSge > 4 )
        delete[] pDs;

    // leo
    CL_ASSERT( nSge || pSgl == NULL );
#if DBG
    if (!status )
    {
        if (pSgl)
        {
            ++g.snd_pkts;
            g.snd_bytes += pSgl[0].Length;
        }
        else
            ++g.snd_pkts_zero;
    }
    else
        ++g.snd_pkts_err;
#endif

    switch( status )
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

HRESULT CEndpoint::SendAndInvalidate(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge,
    __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
    __in DWORD Flags
    )
{
    ND_ENTER( ND_DBG_NDI );

    ib_send_wr_t wr;
    ib_local_ds_t* pDs;

    if( nSge > UINT_MAX )
        return ND_DATA_OVERRUN;

    pDs = new ib_local_ds_t[nSge];
    if( !pDs )
        return ND_NO_MEMORY;

    pResult->BytesTransferred = 0;
    for( SIZE_T i = 0; i < nSge; i++ )
    {
        pDs[i].vaddr = (ULONG_PTR)pSgl[i].pAddr;
        if( pSgl[i].Length > UINT_MAX )
        {
            delete[] pDs;
            return ND_BUFFER_OVERFLOW;
        }
        pDs[i].length = (uint32_t)pSgl[i].Length;
        pDs[i].lkey = ((CMr*)pSgl[i].hMr)->mr_ioctl.out.lkey;

        // Send completions don't include the length.  It's going to
        // be all or nothing, so store it now and we can reset if the
        // request fails.
        pResult->BytesTransferred += pSgl[i].Length;
    }

    wr.p_next = NULL;
    wr.wr_id = (ULONG_PTR)pResult;
    wr.wr_type = WR_SEND;
    if ( pResult->BytesTransferred <= m_MaxInlineSize )
	    wr.send_opt = IB_SEND_OPT_INLINE; 
	else
	    wr.send_opt = 0;
    // We simulate invalidate operations (since we simulate MW use).  We
    // put the RKey in the immediate data, the recipient will do the
    // lookup of the MW based on that (as they would with a regular
    // invalidate request).
    wr.send_opt |= IB_SEND_OPT_IMMEDIATE;
    if( !(Flags & ND_OP_FLAG_SILENT_SUCCESS) )
        wr.send_opt |= IB_SEND_OPT_SIGNALED;
    if( Flags & ND_OP_FLAG_READ_FENCE )
        wr.send_opt |= IB_SEND_OPT_FENCE;
    if( Flags & ND_OP_FLAG_SEND_AND_SOLICIT_EVENT )
        wr.send_opt |= IB_SEND_OPT_SOLICITED;
    wr.num_ds = (uint32_t)nSge;
    wr.ds_array = pDs;
    // Put the RKey in the immeditate data.
    wr.immediate_data = pRemoteMwDescriptor->Token;

    ib_api_status_t status =
        m_pParent->m_Ifc.user_verbs.post_send( m_uQp, &wr, NULL );
    delete[] pDs;

    switch( status )
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

HRESULT CEndpoint::Receive(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge
    )
{
#if DBG    
    if (!(++g.rcv_cnt % 1000))
        ND_PRINT( TRACE_LEVEL_VERBOSE, ND_DBG_NDI,
            ("==> %s, cnt %I64d, rcv %I64d:%I64d:%I64d:%I64d\n",
            __FUNCTION__, g.rcv_cnt, g.rcv_pkts, g.rcv_bytes, g.rcv_pkts_err, g.rcv_pkts_zero ));
#endif
    ib_recv_wr_t wr;
    ib_local_ds_t* pDs;
    ib_local_ds_t ds[4];

    if( nSge > UINT_MAX )
        return ND_DATA_OVERRUN;
    else if( nSge <= 4 )
        pDs = ds;
    else
    {
        pDs = new ib_local_ds_t[nSge];
        if( !pDs )
            return ND_NO_MEMORY;
    }

    for( SIZE_T i = 0; i < nSge; i++ )
    {
        pDs[i].vaddr = (ULONG_PTR)pSgl[i].pAddr;
        if( pSgl[i].Length > UINT_MAX )
        {
            if( nSge > 4 )
                delete[] pDs;
            return ND_BUFFER_OVERFLOW;
        }
        pDs[i].length = (uint32_t)pSgl[i].Length;
        pDs[i].lkey = ((CMr*)pSgl[i].hMr)->mr_ioctl.out.lkey;
    }

    wr.p_next = NULL;
    wr.wr_id = (ULONG_PTR)pResult;
    wr.num_ds = (uint32_t)nSge;
    wr.ds_array = pDs;

    ib_api_status_t status =
        m_pParent->m_Ifc.user_verbs.post_recv( m_uQp, &wr, NULL );

    if( nSge > 4 )
        delete[] pDs;

    // leo
    CL_ASSERT( nSge || pSgl == NULL );
#if DBG
    if (!status)
    {
        if (pSgl)
        {
            ++g.rcv_pkts;
            g.rcv_bytes += pSgl[0].Length;
        }
        else
            ++g.rcv_pkts_zero;
    }
    else
        ++g.rcv_pkts_err;
#endif

    switch( status )
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

HRESULT CEndpoint::Bind(
    __out ND_RESULT* pResult,
    __in ND_MR_HANDLE hMr,
    __in INDMemoryWindow* pMw,
    __in_bcount(BufferSize) const void* pBuffer,
    __in SIZE_T BufferSize,
    __in DWORD Flags,
    __out ND_MW_DESCRIPTOR* pMwDescriptor
    )
{
    ND_ENTER( ND_DBG_NDI );

    UNREFERENCED_PARAMETER( pMw );
    UNREFERENCED_PARAMETER( Flags );

    CMr* pMr = ((CMr*)hMr);

    if( pBuffer < pMr->pBase ||
        pBuffer > pMr->pBase + pMr->Length )
    {
        return ND_INVALID_PARAMETER_4;
    }

    if( ((const char*)pBuffer + BufferSize) > (pMr->pBase + pMr->Length) )
    {
        return ND_INVALID_PARAMETER_5;
    }

    // Ok, this here is a workaround since the Mellanox HCA driver doesn't
    // support MWs.  This should be pushed into the HCA driver.
    pMwDescriptor->Base = _byteswap_uint64( (UINT64)(ULONG_PTR)pBuffer );
    pMwDescriptor->Length = _byteswap_uint64( BufferSize );
    pMwDescriptor->Token = pMr->mr_ioctl.out.rkey;

    // Zero-byte RDMA write.  Could also be a no-op on the send queue
    // which would be better, but requires changing the HCA driver.
    ib_send_wr_t wr;
    wr.p_next = NULL;
    wr.wr_id = (ULONG_PTR)pResult;
    wr.wr_type = WR_RDMA_WRITE;
    wr.send_opt = IB_SEND_OPT_SIGNALED;
    wr.num_ds = 0;
    wr.ds_array = NULL;

    wr.remote_ops.vaddr = 0;
    wr.remote_ops.rkey = 0;

    pResult->BytesTransferred = 0;

    // TODO: Track the MW by rkey, so we can unbind it.

    ib_api_status_t status =
        m_pParent->m_Ifc.user_verbs.post_send( m_uQp, &wr, NULL );

    switch( status )
    {
    case IB_SUCCESS:
        return S_OK;
    case IB_INSUFFICIENT_RESOURCES:
        return ND_NO_MORE_ENTRIES;
    case IB_INVALID_QP_STATE:
        return ND_CONNECTION_INVALID;
    default:
        return ND_UNSUCCESSFUL;
    }
}

HRESULT CEndpoint::Invalidate(
    __out ND_RESULT* pResult,
    __in INDMemoryWindow* pMw,
    __in DWORD Flags
    )
{
    ND_ENTER( ND_DBG_NDI );

    UNREFERENCED_PARAMETER( pMw );
    UNREFERENCED_PARAMETER( Flags );

    // Zero-byte RDMA write.  Could also be a no-op on the send queue
    // which would be better, but requires changing the HCA driver.
    ib_send_wr_t wr;
    wr.p_next = NULL;
    wr.wr_id = (ULONG_PTR)pResult;
    wr.wr_type = WR_RDMA_WRITE;
    wr.send_opt = IB_SEND_OPT_SIGNALED;
    wr.num_ds = 0;
    wr.ds_array = NULL;

    wr.remote_ops.vaddr = 0;
    wr.remote_ops.rkey = 0;

    pResult->BytesTransferred = 0;

    ib_api_status_t status =
        m_pParent->m_Ifc.user_verbs.post_send( m_uQp, &wr, NULL );

    switch( status )
    {
    case IB_SUCCESS:
        // TODO: Stop trackign MW
        return S_OK;
    case IB_INSUFFICIENT_RESOURCES:
        return ND_NO_MORE_ENTRIES;
    case IB_INVALID_QP_STATE:
        return ND_CONNECTION_INVALID;
    default:
        return ND_UNSUCCESSFUL;
    }
}

HRESULT CEndpoint::Rdma(
    __out ND_RESULT* pResult,
    __in ib_wr_type_t Type,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge,
    __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
    __in ULONGLONG Offset,
    __in DWORD Flags
    )
{
//        ND_ENTER( ND_DBG_NDI );

    ib_send_wr_t wr;
    ib_local_ds_t* pDs;
    ib_local_ds_t ds[4];

    if( nSge > UINT_MAX )
        return ND_DATA_OVERRUN;
    else if( nSge <= 4 )
        pDs = ds;
    else
    {
        pDs = new ib_local_ds_t[nSge];
        if( !pDs )
            return ND_NO_MEMORY;
    }

    pResult->BytesTransferred = 0;
    for( SIZE_T i = 0; i < nSge; i++ )
    {
        pDs[i].vaddr = (ULONG_PTR)pSgl[i].pAddr;
        if( pSgl[i].Length > UINT_MAX )
        {
            if( nSge > 4 )
                delete[] pDs;
            return ND_BUFFER_OVERFLOW;
        }
        pDs[i].length = (uint32_t)pSgl[i].Length;
        pDs[i].lkey = ((CMr*)pSgl[i].hMr)->mr_ioctl.out.lkey;

        //TODO: temporary - a workaround of test bug
        //leo
        if( (int)pSgl[i].Length < 0 )
        {
            pDs[i].length = 0 - (int)pSgl[i].Length;
#if DBG                
            ND_PRINT( TRACE_LEVEL_VERBOSE, ND_DBG_NDI,
                ("nSge %d, i %d, Length %#x\n", nSge, i, pSgl[i].Length ));
            if( nSge > 4 )
                delete[] pDs;
            return ND_BUFFER_OVERFLOW;
#endif                
        }

        // Send completions don't include the length.  It's going to
        // be all or nothing, so store it now and we can reset if the
        // request fails.
        pResult->BytesTransferred += pSgl[i].Length;
    }

    wr.p_next = NULL;
    wr.wr_id = (ULONG_PTR)pResult;
    wr.wr_type = Type;
    if ( (pResult->BytesTransferred <= m_MaxInlineSize) && Type != WR_RDMA_READ)
        wr.send_opt = IB_SEND_OPT_INLINE; 
    else
        wr.send_opt = 0;
    if( !(Flags & ND_OP_FLAG_SILENT_SUCCESS) )
        wr.send_opt |= IB_SEND_OPT_SIGNALED;
    if( Flags & ND_OP_FLAG_READ_FENCE )
        wr.send_opt |= IB_SEND_OPT_FENCE;
    wr.num_ds = (uint32_t)nSge;
    wr.ds_array = pDs;

    UINT64 vaddr = _byteswap_uint64( pRemoteMwDescriptor->Base );
    vaddr += Offset;
    wr.remote_ops.vaddr = vaddr;
    wr.remote_ops.rkey = pRemoteMwDescriptor->Token;

    ib_api_status_t status =
        m_pParent->m_Ifc.user_verbs.post_send( m_uQp, &wr, NULL );

    if( nSge > 4 )
        delete[] pDs;

    switch( status )
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

HRESULT CEndpoint::Read(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge,
    __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
    __in ULONGLONG Offset,
    __in DWORD Flags
    )
{
//        ND_ENTER( ND_DBG_NDI );

    return Rdma( pResult, WR_RDMA_READ, pSgl, nSge,
        pRemoteMwDescriptor, Offset, Flags );
}

HRESULT CEndpoint::Write(
    __out ND_RESULT* pResult,
    __in_ecount(nSge) const ND_SGE* pSgl,
    __in SIZE_T nSge,
    __in const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
    __in ULONGLONG Offset,
    __in DWORD Flags
    )
{
//        ND_ENTER( ND_DBG_NDI );

    return Rdma( pResult, WR_RDMA_WRITE, pSgl, nSge,
        pRemoteMwDescriptor, Offset, Flags );
}

HRESULT CEndpoint::CreateQp(
    __in CCq* pInboundCq,
    __in CCq* pOutboundCq,
    __in SIZE_T nInboundEntries,
    __in SIZE_T nOutboundEntries,
    __in SIZE_T nInboundSge,
    __in SIZE_T nOutboundSge,
    __in SIZE_T InboundReadLimit,
    __in SIZE_T OutboundReadLimit,
    __in SIZE_T MaxInlineData
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( MaxInlineData > UINT_MAX )
        return ND_INVALID_PARAMETER_3;
    if( nInboundEntries > UINT_MAX )
        return ND_INVALID_PARAMETER_4;
    if( nOutboundEntries > UINT_MAX )
        return ND_INVALID_PARAMETER_5;
    if( nInboundSge > UINT_MAX )
        return ND_INVALID_PARAMETER_6;
    if( nOutboundSge > UINT_MAX )
        return ND_INVALID_PARAMETER_7;
    if( InboundReadLimit > UCHAR_MAX )
        return ND_INVALID_PARAMETER_9;
    if( OutboundReadLimit > UCHAR_MAX )
        return ND_INVALID_PARAMETER_10;

    /* Setup the qp_ioctl */
    ual_create_qp_ioctl_t qp_ioctl;
    RtlZeroMemory( &qp_ioctl, sizeof(qp_ioctl) );

    qp_ioctl.in.qp_create.qp_type = IB_QPT_RELIABLE_CONN;
    qp_ioctl.in.qp_create.sq_depth = (uint32_t)nOutboundEntries;
    qp_ioctl.in.qp_create.rq_depth = (uint32_t)nInboundEntries;
    qp_ioctl.in.qp_create.sq_sge = (uint32_t)nOutboundSge;
    qp_ioctl.in.qp_create.rq_sge = (uint32_t)nInboundSge;
    qp_ioctl.in.qp_create.sq_max_inline = (uint32_t)MaxInlineData;
    qp_ioctl.in.qp_create.h_srq = NULL;
    qp_ioctl.in.qp_create.sq_signaled = FALSE;

    /* Pre call to the UVP library */
    CL_ASSERT( m_pParent->m_Ifc.user_verbs.pre_create_qp );
    qp_ioctl.in.qp_create.h_sq_cq = pOutboundCq->m_uCq;
    qp_ioctl.in.qp_create.h_rq_cq = pInboundCq->m_uCq;
    ib_api_status_t status = m_pParent->m_Ifc.user_verbs.pre_create_qp(
        m_pParent->m_uPd,
        &qp_ioctl.in.qp_create,
        &qp_ioctl.in.umv_buf,
        (ib_qp_handle_t*)(ULONG_PTR)&m_uQp
        );
    if( status != IB_SUCCESS )
        return ND_INSUFFICIENT_RESOURCES;

    /*
    * Convert the handles to KAL handles once again starting
    * from the input qp attribute
    */
    qp_ioctl.in.h_pd = m_pParent->m_hPd;
    qp_ioctl.in.qp_create.h_sq_cq = (ib_cq_handle_t)pOutboundCq->m_hCq;
    qp_ioctl.in.qp_create.h_rq_cq = (ib_cq_handle_t)pInboundCq->m_hCq;
    qp_ioctl.in.context = (ULONG_PTR)this;
    qp_ioctl.in.ev_notify = FALSE;

    DWORD bytes_ret;
    BOOL fSuccess = DeviceIoControl(
        m_pParent->m_hSync,
        UAL_CREATE_QP,
        &qp_ioctl.in,
        sizeof(qp_ioctl.in),
        &qp_ioctl.out,
        sizeof(qp_ioctl.out),
        &bytes_ret,
        NULL );

    if( fSuccess != TRUE || bytes_ret != sizeof(qp_ioctl.out) )
        qp_ioctl.out.status = IB_ERROR;

    /* Post uvp call */
    CL_ASSERT( m_pParent->m_Ifc.user_verbs.post_create_qp );
    m_pParent->m_Ifc.user_verbs.post_create_qp(
        m_pParent->m_uPd,
        qp_ioctl.out.status,
        (ib_qp_handle_t*)(ULONG_PTR)&m_uQp,
        &qp_ioctl.out.umv_buf
        );


    switch( qp_ioctl.out.status )
    {
    case IB_SUCCESS:
        m_hQp = qp_ioctl.out.h_qp;
        m_Qpn = qp_ioctl.out.attr.num;
        ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
            ("Created QP %#I64x, QPn %#x, pd %#I64x, context %p \n", 
            m_hQp, m_Qpn, m_pParent->m_hPd, this ) );
        return S_OK;

    case IB_INVALID_MAX_WRS:
        if( nInboundEntries > nOutboundEntries )
            return ND_INVALID_PARAMETER_4;
        else
            return ND_INVALID_PARAMETER_5;

    case IB_INVALID_MAX_SGE:
        if( nInboundSge > nOutboundSge )
            return ND_INVALID_PARAMETER_6;
        else
            return ND_INVALID_PARAMETER_7;

    case IB_INSUFFICIENT_MEMORY:
        return ND_NO_MEMORY;

    default:
        return ND_INSUFFICIENT_RESOURCES;
    }
}

void CEndpoint::DestroyQp()
{
    ND_ENTER( ND_DBG_NDI );

    /* Call the uvp pre call if the vendor library provided a valid QP handle */
    CL_ASSERT( m_pParent->m_Ifc.user_verbs.pre_destroy_qp );
    m_pParent->m_Ifc.user_verbs.pre_destroy_qp( m_uQp );

    ual_destroy_qp_ioctl_t qp_ioctl;
    RtlZeroMemory( &qp_ioctl, sizeof(qp_ioctl) );
    qp_ioctl.in.h_qp = m_hQp;

    ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
        ("Destroy QP %I64x\n", m_hQp) );

    DWORD bytes_ret;
    BOOL fSuccess = DeviceIoControl(
        m_pParent->m_hSync,
        UAL_DESTROY_QP,
        &qp_ioctl.in,
        sizeof(qp_ioctl.in),
        &qp_ioctl.out,
        sizeof(qp_ioctl.out),
        &bytes_ret,
        NULL
        );

    if( fSuccess != TRUE || bytes_ret != sizeof(qp_ioctl.out) )
        qp_ioctl.out.status = IB_ERROR;

    ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
        ("Destroyed QP %#I64x, QPn %#x, pd %#I64x, context %p \n", 
        m_hQp, m_Qpn, m_pParent->m_hPd, this ) );
        
    /* Call vendor's post_destroy_qp */
    CL_ASSERT( m_pParent->m_Ifc.user_verbs.post_destroy_qp );
    m_pParent->m_Ifc.user_verbs.post_destroy_qp(
        m_uQp,
        qp_ioctl.out.status
        );
    m_hQp = NULL;
}

HRESULT CEndpoint::ModifyQp(
    __in ib_qp_state_t NewState )
{
    ND_ENTER( ND_DBG_NDI );

    /* Setup the qp_ioctl */
    ual_ndi_modify_qp_ioctl_in_t qp_ioctl;
    RtlZeroMemory( &qp_ioctl, sizeof(qp_ioctl) );

    switch( NewState )
    {
    case IB_QPS_INIT:
        qp_ioctl.qp_mod.state.init.primary_port = m_pParent->m_PortNum;
        qp_ioctl.qp_mod.state.init.qkey = 0;
        qp_ioctl.qp_mod.state.init.pkey_index = 0;
        qp_ioctl.qp_mod.state.init.access_ctrl =
            IB_AC_LOCAL_WRITE | IB_AC_RDMA_READ | IB_AC_RDMA_WRITE;

        // Fall through.
    case IB_QPS_RESET:
    case IB_QPS_ERROR:
        qp_ioctl.qp_mod.req_state = NewState;
    }

    /* Call the uvp ND modify verb */
    CL_ASSERT( m_pParent->m_Ifc.user_verbs.nd_modify_qp );
    void* pOutbuf;
    DWORD szOutbuf;

    m_pParent->m_Ifc.user_verbs.nd_modify_qp(
        m_uQp,
        &pOutbuf,
        &szOutbuf
        );

    qp_ioctl.h_qp = m_hQp;

    DWORD bytes_ret;
    BOOL fSuccess = DeviceIoControl(
        m_pParent->m_hSync,
        UAL_NDI_MODIFY_QP,
        &qp_ioctl,
        sizeof(qp_ioctl),
        pOutbuf,
        szOutbuf,
        &bytes_ret,
        NULL );

    if( fSuccess != TRUE )
        return ND_UNSUCCESSFUL;

    return S_OK;
}

HRESULT CEndpoint::QueryQp(
	__out ib_qp_attr_t *qp_attr
	)
{
	ib_api_status_t status;
	
	ND_ENTER( ND_DBG_NDI );

	ual_query_qp_ioctl_t qp_ioctl;
	RtlZeroMemory( &qp_ioctl, sizeof(qp_ioctl) );
	qp_ioctl.in.h_qp = m_hQp;

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( m_pParent->m_Ifc.user_verbs.pre_query_qp )
	{
		/* Pre call to the UVP library */
		status = m_pParent->m_Ifc.user_verbs.pre_query_qp( m_uQp, &qp_ioctl.in.umv_buf );
		if( status != IB_SUCCESS )
			goto done;
	}

	DWORD bytes_ret;
	BOOL fSuccess = DeviceIoControl(
		m_pParent->m_hSync,
		UAL_QUERY_QP,
		&qp_ioctl.in,
		sizeof(qp_ioctl.in),
		&qp_ioctl.out,
		sizeof(qp_ioctl.out),
		&bytes_ret,
		NULL
		);

	if( fSuccess != TRUE || bytes_ret != sizeof(qp_ioctl.out) )
		status = IB_ERROR;
	else
		status = qp_ioctl.out.status;

	/* Call vendor's post_query_qp */
	CL_ASSERT( m_pParent->m_Ifc.user_verbs.post_query_qp );
	if( m_pParent->m_Ifc.user_verbs.post_query_qp )
	{
		m_pParent->m_Ifc.user_verbs.post_query_qp( m_uQp, status,
			&qp_ioctl.out.attr, &qp_ioctl.out.umv_buf );
	}

done:
	ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
		("Queried QP %#I64x, QPn %#x, pd %#I64x, context %p, status %#x \n", 
		m_hQp, m_Qpn, m_pParent->m_hPd, this, status ) );

	switch( status )
	{
	case IB_SUCCESS:
		*qp_attr = qp_ioctl.out.attr;
		return S_OK;

	default:
		return ND_UNSUCCESSFUL;
	}
	
}

} // namespace
