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

#include "NdListen.h"
#include "NdAdapter.h"
#include "NdEndpoint.h"
#include "NdConnector.h"
#include "al_dev.h"
#pragma warning( push, 3 )
#include "winternl.h"
#pragma warning( pop )
#include <complib/cl_byteswap.h>
#include <limits.h>
#include "nddebug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "NdListen.tmh"
#endif

namespace NetworkDirect
{

HRESULT GetPdataForPassive(
    __in UINT8* pSrc,
    __in SIZE_T SrcLength,
    __out_bcount_part_opt(*pPrivateDataLength, *pPrivateDataLength) void* pPrivateData,
    __inout SIZE_T* pPrivateDataLength
    )
{
    if( SrcLength != IB_REQ_PDATA_SIZE )
    {
        ND_PRINT( TRACE_LEVEL_ERROR, ND_DBG_NDI, 
            ("Connection aborted: incorrect pdata_len %d \n", (int)SrcLength ));
        return ND_CONNECTION_ABORTED;
    }

    if( pPrivateDataLength == NULL )
        return S_OK;

    ib_cm_rdma_req_t* pIpData = (ib_cm_rdma_req_t*)pSrc;
    CL_ASSERT( pIpData->maj_min_ver == 0 );
    CL_ASSERT( pIpData->ipv == 0x40 || pIpData->ipv == 0x60 );

    if( *pPrivateDataLength == 0 )
    {
        *pPrivateDataLength = sizeof(pIpData->pdata);
        return ND_BUFFER_OVERFLOW;
    }

    CopyMemory(
        pPrivateData,
        pIpData->pdata,
        min( *pPrivateDataLength, sizeof(pIpData->pdata) )
        );

    HRESULT hr;
    if( *pPrivateDataLength < sizeof(pIpData->pdata) )
    {
        hr = ND_BUFFER_OVERFLOW;
    }
    else
    {
        hr = S_OK;
    }

    *pPrivateDataLength = sizeof(pIpData->pdata);
    return hr;
}

HRESULT GetPdataForActive(
    __in UINT8* pSrc,
    __in SIZE_T SrcLength,
    __out_bcount_part_opt(*pPrivateDataLength, *pPrivateDataLength) void* pPrivateData,
    __inout SIZE_T* pPrivateDataLength
    )
{
    if( pPrivateDataLength == NULL )
        return S_OK;

    if( *pPrivateDataLength == 0 )
    {
        *pPrivateDataLength = SrcLength;
        return ND_BUFFER_OVERFLOW;
    }

    CopyMemory(
        pPrivateData,
        pSrc,
        min( *pPrivateDataLength, SrcLength ) );

    HRESULT hr;
    if( *pPrivateDataLength < IB_REJ_PDATA_SIZE )
    {
        hr = ND_BUFFER_OVERFLOW;
    }
    else
    {
        hr = S_OK;
    }

    *pPrivateDataLength = SrcLength;
    return hr;
}

    CListen::CListen(void) :
        m_nRef( 1 ),
        m_pParent( NULL ),
        m_cid( 0 )
    {
    }

    CListen::~CListen(void)
    {
        if( m_cid != 0 )
        {
            DWORD bytes_ret;
            DeviceIoControl(
                m_pParent->m_hSync,
                UAL_DESTROY_CEP,
                &m_cid,
                sizeof(m_cid),
                NULL,
                0,
                &bytes_ret,
                NULL );
        }

        if( m_pParent )
            m_pParent->Release();
    }

    HRESULT CListen::Initialize(
        __in CAdapter* pParent,
        __in SIZE_T Backlog,
        __in INT Protocol,
        __in USHORT Port,
        __out_opt USHORT* pAssignedPort
        )
    {
        ND_ENTER( ND_DBG_NDI );

        m_pParent = pParent;
        m_pParent->AddRef();

        UNREFERENCED_PARAMETER( Backlog );

        if( Port == 0 && pAssignedPort == NULL )
            return ND_INVALID_PARAMETER_MIX;

        //
        // IP Addressing Annex only supports a single byte for protocol.
        //
        if( Protocol > UCHAR_MAX || Protocol < 0 )
            return ND_INVALID_PARAMETER_3;

        ual_cep_listen_ioctl_t listen;
        listen.cid = 0;

        listen.cep_listen.svc_id = ib_cm_rdma_sid(static_cast<UINT8>(Protocol), Port);

        listen.cep_listen.port_guid = m_pParent->m_PortGuid;

        ib_cm_rdma_req_t* pdata = reinterpret_cast<ib_cm_rdma_req_t*>(listen.compare);
        pdata->maj_min_ver = IB_REQ_CM_RDMA_VERSION;
        pdata->src_port = 0;
        ZeroMemory( &pdata->src_ip_addr, sizeof(pdata->src_ip_addr) );

        switch( m_pParent->m_Addr.v4.sin_family )
        {
        case AF_INET:
            pdata->ipv = IB_REQ_CM_RDMA_IPV4;
            pdata->dst_ip_addr[0] = pdata->dst_ip_addr[1] = pdata->dst_ip_addr[2] = 0;
            pdata->dst_ip_addr[3] = m_pParent->m_Addr.v4.sin_addr.s_addr;
            ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
                ("Listen for: IP %#x, port %#hx\n", 
                _byteswap_ulong(m_pParent->m_Addr.v4.sin_addr.S_un.S_addr),
                _byteswap_ushort(m_pParent->m_Addr.v4.sin_port) ) );
            break;
        case AF_INET6:
            pdata->ipv = IB_REQ_CM_RDMA_IPV6;
            CopyMemory( &pdata->dst_ip_addr,
                &m_pParent->m_Addr.v6.sin6_addr,
                sizeof(m_pParent->m_Addr.v6.sin6_addr) );
            break;
        }
        listen.cep_listen.p_cmp_buf = listen.compare;
        listen.cep_listen.cmp_len = FIELD_OFFSET(ib_cm_rdma_req_t, pdata);
        listen.cep_listen.cmp_offset = IB_REQ_CM_RDMA_CMP_DST_IP;

        IO_STATUS_BLOCK IoStatus;
        IoStatus.Status = g_NtDeviceIoControlFile(
            m_pParent->m_hSync,
            NULL,
            NULL,
            NULL,
            &IoStatus,
            UAL_NDI_LISTEN_CM,
            &listen,
            sizeof(listen),
            &m_cid,
            sizeof(m_cid) );

        switch( IoStatus.Status )
        {
        case ND_SUCCESS:
            ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
                ("Listen for: Guid %#I64x, sid %#I64x\n", 
                m_pParent->m_PortGuid, listen.cep_listen.svc_id ) );
            break;

        case IB_INVALID_SETTING:
            return ND_ADDRESS_ALREADY_EXISTS;

        default:
            return IoStatus.Status;
        }

        ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
            ("Created listen CEP with cid %d \n", m_cid ) );

        //
        // The following port calculation must match what is done in the kernel.
        //
        if( Port == 0 )
            Port = (USHORT)m_cid | (USHORT)(m_cid >> 16);

        if( pAssignedPort )
            *pAssignedPort = Port;

        m_Protocol = (UINT8)Protocol;
        return S_OK;
    }

    HRESULT CListen::Create(
        __in CAdapter* pParent,
        __in SIZE_T Backlog,
        __in INT Protocol,
        __in USHORT Port,
        __out_opt USHORT* pAssignedPort,
        __deref_out INDListen** ppListen
        )
    {
        CListen* pListen = new CListen();
        if( pListen == NULL )
            return ND_NO_MEMORY;

        HRESULT hr = pListen->Initialize(
            pParent,
            Backlog,
            Protocol,
            Port,
            pAssignedPort );
        if( FAILED( hr ) )
        {
            delete pListen;
            return hr;
        }

        *ppListen = pListen;
        return S_OK;
    }

    // *** IUnknown methods ***
    HRESULT CListen::QueryInterface(
        REFIID riid,
        LPVOID FAR* ppvObj
        )
    {
        if( IsEqualIID( riid, IID_IUnknown ) )
        {
            *ppvObj = this;
            return S_OK;
        }

        if( IsEqualIID( riid, IID_INDListen ) )
        {
            *ppvObj = this;
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG CListen::AddRef(void)
    {
        return InterlockedIncrement( &m_nRef );
    }

    ULONG CListen::Release(void)
    {
        ULONG ref = InterlockedDecrement( &m_nRef );
        if( ref == 0 )
            delete this;

        return ref;
    }

    // *** INDOverlapped methods ***
    HRESULT CListen::CancelOverlappedRequests(void)
    {
        ND_ENTER( ND_DBG_NDI );

        DWORD bytes_ret;
        BOOL ret = DeviceIoControl(
            m_pParent->m_hSync,
            UAL_NDI_CANCEL_CM_IRPS,
            &m_cid,
            sizeof(m_cid),
            NULL,
            0,
            &bytes_ret,
            NULL );

        if( ret )
            return S_OK;
        else
            return ND_UNSUCCESSFUL;
    }

    HRESULT CListen::GetOverlappedResult(
        __inout OVERLAPPED *pOverlapped,
        __out SIZE_T *pNumberOfBytesTransferred,
        __in BOOL bWait
        )
    {
        ND_ENTER( ND_DBG_NDI );

        *pNumberOfBytesTransferred = 0;
        ::GetOverlappedResult(
            m_pParent->GetFileHandle(),
            pOverlapped,
            (DWORD*)pNumberOfBytesTransferred,
            bWait );
        return (HRESULT)pOverlapped->Internal;
    }

    // *** INDListen methods ***
    HRESULT CListen::GetConnectionRequest(
        __inout INDConnector* pConnector,
        __inout OVERLAPPED* pOverlapped
        )
    {
        ND_ENTER( ND_DBG_NDI );

        static_cast<CConnector*>(pConnector)->m_Protocol = m_Protocol;

        pOverlapped->Internal = ND_PENDING;
        return g_NtDeviceIoControlFile(
            m_pParent->GetFileHandle(),
            pOverlapped->hEvent,
            NULL,
            (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
            (IO_STATUS_BLOCK*)&pOverlapped->Internal,
            UAL_NDI_GET_REQ_CM,
            &m_cid,
            sizeof(m_cid),
            &static_cast<CConnector*>(pConnector)->m_cid,
            sizeof(static_cast<CConnector*>(pConnector)->m_cid) );
    }

} // namespace
