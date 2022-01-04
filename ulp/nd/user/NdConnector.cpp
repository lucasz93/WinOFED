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

#include "NdCq.h"
#include "NdAdapter.h"
#include "NdMw.h"
#include "NdEndpoint.h"
#include "NdProv.h"
#include "NdMr.h"
#include "NdListen.h"
#include "NdConnector.h"

#include "al_dev.h"
#pragma warning( push, 3 )
#include "winternl.h"
#pragma warning( pop )
#include "iba/ibat.h"

#include <assert.h>
#include <limits.h>
#include <strsafe.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "NdConnector.tmh"
#endif


namespace NetworkDirect
{

CConnector::CConnector(void) :
    m_nRef( 1 ),
    m_pParent( NULL ),
    m_pEndpoint( NULL ),
    m_Protocol( 0 ),
    m_LocalPort( 0 ),
    m_cid( 0 ),
    m_fActive( false )
{
}

CConnector::~CConnector(void)
{
    FreeCid();

    if( m_pEndpoint )
        m_pEndpoint->Release();

    if( m_pParent )
        m_pParent->Release();
}

void CConnector::FreeCid(void)
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

        m_cid = 0;
    }
}

HRESULT CConnector::Create(
    __in CAdapter* pParent,
    __deref_out INDConnector** ppConnector
    )
{
    CConnector* pConnector = new CConnector();
    if( pConnector == NULL )
        return ND_NO_MEMORY;

    pConnector->m_pParent = pParent;
    pParent->AddRef();

    *ppConnector = pConnector;
    return S_OK;
}

HRESULT CConnector::QueryInterface(
    REFIID riid,
    LPVOID FAR* ppvObj
    )
{
    if( IsEqualIID( riid, IID_IUnknown ) )
    {
        *ppvObj = this;
        return S_OK;
    }

    if( IsEqualIID( riid, IID_INDConnector ) )
    {
        *ppvObj = this;
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG CConnector::AddRef(void)
{
    return InterlockedIncrement( &m_nRef );
}

ULONG CConnector::Release(void)
{
    ULONG ref = InterlockedDecrement( &m_nRef );
    if( ref == 0 )
        delete this;

    return ref;
}

// *** INDOverlapped methods ***
HRESULT CConnector::CancelOverlappedRequests(void)
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

HRESULT CConnector::GetOverlappedResult(
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

HRESULT CConnector::CreateEndpoint(
    __in INDCompletionQueue* pInboundCq,
    __in INDCompletionQueue* pOutboundCq,
    __in SIZE_T nInboundEntries,
    __in SIZE_T nOutboundEntries,
    __in SIZE_T nInboundSge,
    __in SIZE_T nOutboundSge,
    __in SIZE_T InboundReadLimit,
    __in SIZE_T OutboundReadLimit,
    __out_opt SIZE_T* pMaxInlineData,
    __deref_out INDEndpoint** ppEndpoint
    )
{
    ND_ENTER( ND_DBG_NDI );

    //
    // Check that the verb provider supports user-mode operations.
    // If not, a different class should be created.
    //
    if( m_pParent->m_Ifc.user_verbs.pre_create_qp == NULL ||
        m_pParent->m_Ifc.user_verbs.post_create_qp == NULL ||
        m_pParent->m_Ifc.user_verbs.nd_modify_qp == NULL ||
        m_pParent->m_Ifc.user_verbs.nd_get_qp_state == NULL ||
        m_pParent->m_Ifc.user_verbs.pre_destroy_qp == NULL ||
        m_pParent->m_Ifc.user_verbs.post_destroy_qp == NULL ||
        m_pParent->m_Ifc.user_verbs.post_send == NULL ||
        m_pParent->m_Ifc.user_verbs.post_recv == NULL /*||
        m_pParent->m_Ifc.user_verbs.bind_mw == NULL*/ )
    {
        return ND_NOT_SUPPORTED;
    }

    if( m_pEndpoint != NULL )
        return ND_NOT_SUPPORTED;

    HRESULT hr = CEndpoint::Create(
        m_pParent,
        static_cast<CCq*>(pInboundCq),
        static_cast<CCq*>(pOutboundCq),
        nInboundEntries,
        nOutboundEntries,
        nInboundSge,
        nOutboundSge,
        InboundReadLimit,
        OutboundReadLimit,
        pMaxInlineData,
        (INDEndpoint**)&m_pEndpoint
        );

    if( SUCCEEDED( hr ) )
    {
        m_pEndpoint->AddRef();
        *ppEndpoint = m_pEndpoint;
    }

    return hr;
}

//
// Connects an endpoint to the specified destinaton address.
//
// Only basic checks on input data are performed in user-mode,
// and request is handed to the kernel, where:
//  1. the destination address is resolved to a destination GID
//  2. the destination GID is used to get a path record
//  3. the path record is used to issue a CM REQ
//  4. a CM REP is received (or REJ or timeout).
//  5. the request is completed.
//
HRESULT CConnector::Connect(
    __inout INDEndpoint* pEndpoint,
    __in_bcount(AddressLength) const struct sockaddr* pAddress,
    __in SIZE_T AddressLength,
    __in INT Protocol,
    __in_opt USHORT LocalPort,
    __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
    __in SIZE_T PrivateDataLength,
    __inout OVERLAPPED* pOverlapped
    )
{
    HRESULT hr;

    ND_ENTER( ND_DBG_NDI );

    if( AddressLength < sizeof(struct sockaddr) )
        return ND_INVALID_ADDRESS;

    if( Protocol > UCHAR_MAX || Protocol < 0 )
        return ND_INVALID_PARAMETER_4;

    m_Protocol = (UINT8)Protocol;

    //
    // CM REQ supports 92 bytes of private data.  We expect to use IP
    // addressing annex, which eats up 36 bytes, leaving us with 56.
    //
    if( PrivateDataLength > 56 )
        return ND_BUFFER_OVERFLOW;

    //
    // Only support connecting the endpoint we created.
    //
    if( pEndpoint != m_pEndpoint )
        return ND_INVALID_PARAMETER_1;

    //
    // Format the private data to match the RDMA CM annex spec as we
    // check the address validity.
    //
    ual_ndi_req_cm_ioctl_in_t ioctl;
    ioctl.pdata.maj_min_ver = 0;
    if( LocalPort == 0 )
    {
        m_LocalPort = (USHORT)_byteswap_ulong( m_pEndpoint->m_Qpn );
    }
    else
    {
        m_LocalPort = LocalPort;
    }
    ioctl.pdata.src_port = m_LocalPort;

    switch( ((struct sockaddr_in*)pAddress)->sin_family )
    {
    case AF_INET:
        if( AddressLength != sizeof(struct sockaddr_in) )
            return ND_INVALID_ADDRESS;

        if( m_pParent->m_Addr.v4.sin_family != AF_INET )
            return ND_INVALID_ADDRESS;

        //
        // Copy the destination address so we can satisfy a
        // GetPeerAddress request.
        //
        m_PeerAddr.v4 = *(struct sockaddr_in*)pAddress;

        ioctl.dst_port = m_PeerAddr.v4.sin_port;
        ioctl.pdata.ipv = 0x40;

        // Local address.
        RtlZeroMemory( &ioctl.pdata.src_ip_addr, ATS_IPV4_OFFSET );
        RtlCopyMemory(
            &ioctl.pdata.src_ip_addr[ATS_IPV4_OFFSET>>2],
            (uint8_t*)&m_pParent->m_Addr.v4.sin_addr,
            sizeof( m_pParent->m_Addr.v4.sin_addr )
            );

        // Destination address.
        RtlZeroMemory( &ioctl.pdata.dst_ip_addr, ATS_IPV4_OFFSET );
        RtlCopyMemory(
            &ioctl.pdata.dst_ip_addr[ATS_IPV4_OFFSET>>2],
            (uint8_t*)&m_PeerAddr.v4.sin_addr,
            sizeof( m_PeerAddr.v4.sin_addr )
            );

        ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
            ("local address: IP %#x, port %#hx, dest address: IP %#x, port %#hx\n", 
            _byteswap_ulong(m_pParent->m_Addr.v4.sin_addr.S_un.S_addr),
            _byteswap_ushort(m_pParent->m_Addr.v4.sin_port),
            _byteswap_ulong(m_PeerAddr.v4.sin_addr.S_un.S_addr),
            _byteswap_ushort(m_PeerAddr.v4.sin_port) ) );
        break;

    case AF_INET6:
        if( AddressLength != sizeof(struct sockaddr_in6) )
            return ND_INVALID_ADDRESS;

        //
        // Copy the destination address so we can satisfy a
        // GetPeerAddress request.
        //
        m_PeerAddr.v6 = *(struct sockaddr_in6*)pAddress;

        ioctl.dst_port = m_PeerAddr.v6.sin6_port;
        ioctl.pdata.ipv = 0x60;

        // Local address.
        RtlCopyMemory(
            ioctl.pdata.src_ip_addr,
            m_pParent->m_Addr.v6.sin6_addr.u.Byte,
            sizeof(ioctl.pdata.src_ip_addr)
            );

        // Destination address.
        RtlCopyMemory( ioctl.pdata.dst_ip_addr,
            m_PeerAddr.v6.sin6_addr.u.Byte,
            sizeof(ioctl.pdata.dst_ip_addr)
            );
        break;

    default:
        return ND_INVALID_ADDRESS;
    }

    // Copy the user's private data.
    CopyMemory( ioctl.pdata.pdata, pPrivateData, PrivateDataLength );

    //
    // It's only valid to call Connect with a new or reused endpoint:
    // the QP must be in the INIT state.
    //
    switch( m_pParent->m_Ifc.user_verbs.nd_get_qp_state( m_pEndpoint->m_uQp ) )
    {
    case IB_QPS_INIT:
        break;

    case IB_QPS_RTS:
        return ND_CONNECTION_ACTIVE;

    default:
        return ND_UNSUCCESSFUL;
    }

    //
    // Create the CEP.  We do this so that we have a valid CID in user-mode
    // in case the user calls CancelOverlappedRequests or releases the object.
    //
    UINT64 context = 0;
    ual_create_cep_ioctl_t create;
    DWORD bytes_ret;
    BOOL fSuccess = DeviceIoControl(
        m_pParent->m_hSync,
        UAL_CREATE_CEP,
        &context,
        sizeof(context),
        &create,
        sizeof(create),
        &bytes_ret,
        NULL
        );
    if( !fSuccess ||
        bytes_ret != sizeof(create) ||
        create.status != IB_SUCCESS )
    {
        return ND_INSUFFICIENT_RESOURCES;
    }

    m_cid = create.cid;

    //
    // Fill in the rest of the input buffer and send the request down
    // to the kernel and let everything else be done there.
    //
    ioctl.h_qp = m_pEndpoint->m_hQp;
    ioctl.cid = m_cid;
    ioctl.prot = m_Protocol;
    ioctl.pdata_size = sizeof(ioctl.pdata);
    ioctl.init_depth = m_pEndpoint->m_Ord;
    ioctl.resp_res = m_pEndpoint->m_Ird;

    ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
        ("Connect QP %#I64x, QPn %#x, Guid %#I64x \n",
        m_pEndpoint->m_hQp,
        m_pEndpoint->m_Qpn,
        m_pParent->m_PortGuid ) );

    m_fActive = true;

    pOverlapped->Internal = ND_PENDING;
    hr = g_NtDeviceIoControlFile(
        m_pParent->GetFileHandle(),
        pOverlapped->hEvent,
        NULL,
        (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
        (IO_STATUS_BLOCK*)&pOverlapped->Internal,
        UAL_NDI_REQ_CM,
        &ioctl,
        sizeof(ioctl),
        NULL,
        0 );

    if( FAILED( hr ) )
    {
        FreeCid();
        m_pEndpoint->Release();
        m_pEndpoint = NULL;
    }

    return hr;
}

HRESULT CConnector::CompleteConnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( !m_fActive )
        return ND_CONNECTION_INVALID;

    if( m_pEndpoint == NULL )
        return ND_CONNECTION_INVALID;

    //
    // Get the UVP's buffer for modify operations.
    //
    void* pOutbuf;
    DWORD szOutbuf;

    m_pParent->m_Ifc.user_verbs.nd_modify_qp(
        (const ib_qp_handle_t)m_pEndpoint->m_uQp,
        &pOutbuf,
        &szOutbuf
        );

    ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
        ("CompleteConnect QP %#I64x, QPn %#x\n", m_pEndpoint->m_hQp, m_pEndpoint->m_Qpn ) );

    pOverlapped->Internal = ND_PENDING;
    return g_NtDeviceIoControlFile(
        m_pParent->GetFileHandle(),
        pOverlapped->hEvent,
        NULL,
        (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
        (IO_STATUS_BLOCK*)&pOverlapped->Internal,
        UAL_NDI_RTU_CM,
        &m_cid,
        sizeof(m_cid),
        pOutbuf,
        szOutbuf );
}

HRESULT CConnector::Accept(
    __in INDEndpoint* pEndpoint,
    __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
    __in SIZE_T PrivateDataLength,
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( PrivateDataLength > IB_REJ_PDATA_SIZE )
        return ND_BUFFER_OVERFLOW;

    //
    // Only support connecting the endpoint we created.
    //
    if( pEndpoint != m_pEndpoint )
        return ND_INVALID_PARAMETER_1;

    //
    // Get the UVP's buffer for modify operations.
    //
    void* pOutbuf;
    DWORD szOutbuf;

    m_pParent->m_Ifc.user_verbs.nd_modify_qp(
        static_cast<CEndpoint*>(pEndpoint)->m_uQp,
        &pOutbuf,
        &szOutbuf
        );

    ual_ndi_rep_cm_ioctl_in_t ioctl;
    ioctl.h_qp = m_pEndpoint->m_hQp;
    ioctl.cid = m_cid;
    ioctl.init_depth = m_pEndpoint->m_Ord;
    ioctl.resp_res = m_pEndpoint->m_Ird;
    ioctl.pdata_size = (uint8_t)PrivateDataLength;
    CopyMemory( &ioctl.pdata, pPrivateData, PrivateDataLength );

    ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
        ("Accept QP %#I64x, cid %d \n", ioctl.h_qp, ioctl.cid ) );

    pOverlapped->Internal = ND_PENDING;
    HRESULT hr = g_NtDeviceIoControlFile(
        m_pParent->GetFileHandle(),
        pOverlapped->hEvent,
        NULL,
        (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
        (IO_STATUS_BLOCK*)&pOverlapped->Internal,
        UAL_NDI_REP_CM,
        &ioctl,
        sizeof(ioctl),
        pOutbuf,
        szOutbuf );

    if( FAILED( hr ) )
    {
        m_pEndpoint->Release();
        m_pEndpoint = NULL;
    }
    return hr;
}

HRESULT CConnector::Reject(
    __in_bcount_opt(PrivateDataLength) const void* pPrivateData,
    __in SIZE_T PrivateDataLength
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( PrivateDataLength > IB_REJ_PDATA_SIZE )
        return ND_BUFFER_OVERFLOW;

    ual_ndi_rej_cm_ioctl_in_t ioctl;
    ioctl.cid = m_cid;
    ioctl.pdata_size = (uint8_t)PrivateDataLength;
    if( pPrivateData != NULL )
    {
        CopyMemory( &ioctl.pdata, pPrivateData, PrivateDataLength );
    }

    ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI, 
        ("Connection rejected with pdata_size %d, cid %d\n", 
        (int)PrivateDataLength, ioctl.cid ));

    IO_STATUS_BLOCK IoStatus;
    return g_NtDeviceIoControlFile(
        m_pParent->m_hSync,
        NULL,
        NULL,
        NULL,
        &IoStatus,
        UAL_NDI_REJ_CM,
        &ioctl,
        sizeof(ioctl),
        NULL,
        0 );
}

HRESULT CConnector::GetConnectionData(
    __out_opt SIZE_T* pInboundReadLimit,
    __out_opt SIZE_T* pOutboundReadLimit,
    __out_bcount_part_opt(*pPrivateDataLength, *pPrivateDataLength) void* pPrivateData,
    __inout SIZE_T* pPrivateDataLength
    )
{
    ND_ENTER( ND_DBG_NDI );

    IO_STATUS_BLOCK IoStatus;
    ual_cep_get_pdata_ioctl_t IoctlBuf;

    IoctlBuf.in.cid = m_cid;
    HRESULT hr = g_NtDeviceIoControlFile(
        m_pParent->m_hSync,
        NULL,
        NULL,
        NULL,
        &IoStatus,
        UAL_CEP_GET_PDATA,
        &IoctlBuf,
        sizeof(IoctlBuf.in),
        &IoctlBuf,
        sizeof(IoctlBuf.out) );

    // UAL_CEP_GET_PDATA never returns pending.
    if( FAILED( hr ) )
        return hr;

    // On the passive side, check that we got the REQ private data.
    if( !m_fActive )
    {
        hr = GetPdataForPassive(
            IoctlBuf.out.pdata,
            IoctlBuf.out.pdata_len,
            pPrivateData,
            pPrivateDataLength
            );
    }
    else
    {
        hr = GetPdataForActive(
            IoctlBuf.out.pdata,
            IoctlBuf.out.pdata_len,
            pPrivateData,
            pPrivateDataLength
            );
    }

    if( FAILED( hr ) && hr != ND_BUFFER_OVERFLOW )
        return hr;

    if( pInboundReadLimit )
    {
        *pInboundReadLimit = IoctlBuf.out.resp_res;
    }

    if( pOutboundReadLimit )
    {
        *pOutboundReadLimit = IoctlBuf.out.init_depth;
    }

    return hr;
}

HRESULT CConnector::GetLocalAddress(
    __out_bcount_part_opt(*pAddressLength, *pAddressLength) struct sockaddr* pAddress,
    __inout SIZE_T* pAddressLength
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( m_pEndpoint == NULL )
        return ND_CONNECTION_INVALID;

    ib_qp_state_t state =
        m_pParent->m_Ifc.user_verbs.nd_get_qp_state( m_pEndpoint->m_uQp );
    if( state != IB_QPS_RTS )
        return ND_CONNECTION_INVALID;

    HRESULT hr = m_pParent->GetLocalAddress( pAddress, pAddressLength );
    if( FAILED(hr) )
        return hr;

    // V4 and V6 addresses have the port number in the same place.
    ((struct sockaddr_in*)pAddress)->sin_port = m_LocalPort;
#if DBG || defined(EVENT_TRACING)
    struct sockaddr_in*pAddrV4 = (struct sockaddr_in*)pAddress;
#endif        
    ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
        ("Local address: IP %#x, port %#hx\n", 
        _byteswap_ulong(pAddrV4->sin_addr.S_un.S_addr),
        _byteswap_ushort(pAddrV4->sin_port) ) );

    return S_OK;
}

HRESULT CConnector::GetAddressFromPdata(
    __out_bcount_part(*pAddressLength, *pAddressLength) struct sockaddr* pAddress,
    __inout SIZE_T* pAddressLength
    )
{
    ND_ENTER( ND_DBG_NDI );

    IO_STATUS_BLOCK IoStatus;
    ual_cep_get_pdata_ioctl_t IoctlBuf;

    IoctlBuf.in.cid = m_cid;

    HRESULT hr = g_NtDeviceIoControlFile(
        m_pParent->m_hSync,
        NULL,
        NULL,
        NULL,
        &IoStatus,
        UAL_CEP_GET_PDATA,
        &IoctlBuf,
        sizeof(IoctlBuf.in),
        &IoctlBuf,
        sizeof(IoctlBuf.out) );

    // UAL_CEP_GET_PDATA never returns pending.
    if( FAILED( hr ) )
    {
        CL_ASSERT( hr != ND_PENDING );
        return hr;
    }

    if( IoctlBuf.out.pdata_len != IB_REQ_PDATA_SIZE )
        return ND_CONNECTION_ABORTED;

    ib_cm_rdma_req_t* pIpData = (ib_cm_rdma_req_t*)&IoctlBuf.out.pdata;
    CL_ASSERT( pIpData->maj_min_ver == 0 );

    SIZE_T len;
    switch( pIpData->ipv )
    {
    case 0x40:
        len = 4;
        break;
    case 0x60:
        len = 16;
        break;
    default:
        CL_ASSERT( pIpData->ipv == 0x40 || pIpData->ipv == 0x60 );
        len = 0;
        break;
    }

    if( len > *pAddressLength )
    {
        *pAddressLength = len;
        return ND_BUFFER_OVERFLOW;
    }

    ((struct sockaddr_in*)pAddress)->sin_port =
        _byteswap_ushort( pIpData->src_port );

    switch( pIpData->ipv )
    {
    case 0x40:
        ((struct sockaddr_in*)pAddress)->sin_family = AF_INET;
        ((struct sockaddr_in*)pAddress)->sin_addr.s_addr = pIpData->src_ip_addr[3];
        ZeroMemory( ((struct sockaddr_in*)pAddress)->sin_zero,
            sizeof( ((struct sockaddr_in*)pAddress)->sin_zero ) );
        break;

    case 0x60:
        ((struct sockaddr_in6*)pAddress)->sin6_family = AF_INET6;
        ((struct sockaddr_in6*)pAddress)->sin6_flowinfo = 0;
        RtlCopyMemory( (char*)((struct sockaddr_in6*)pAddress)->sin6_addr.s6_bytes,
            (char *const)pIpData->src_ip_addr, sizeof(pIpData->src_ip_addr) );
        ((struct sockaddr_in6*)pAddress)->sin6_scope_id = 0;
        break;
    }

    *pAddressLength = len;
    return hr;
}

HRESULT CConnector::GetPeerAddress(
    __out_bcount_part_opt(*pAddressLength, *pAddressLength) struct sockaddr* pAddress,
    __inout SIZE_T* pAddressLength
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( !m_fActive )
        return GetAddressFromPdata( pAddress, pAddressLength );

    if( m_pEndpoint == NULL )
        return ND_CONNECTION_INVALID;

    ib_qp_state_t state =
        m_pParent->m_Ifc.user_verbs.nd_get_qp_state( m_pEndpoint->m_uQp );
    if( state != IB_QPS_RTS )
        return ND_CONNECTION_INVALID;

    switch( m_PeerAddr.v4.sin_family )
    {
    case AF_INET:
        if( *pAddressLength < sizeof(struct sockaddr_in) )
        {
            *pAddressLength = sizeof(struct sockaddr_in);
            return ND_BUFFER_OVERFLOW;
        }
        *(struct sockaddr_in*)pAddress = m_PeerAddr.v4;
        ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
            ("Peer address: IP %#x, port %#hx\n", 
            _byteswap_ulong(m_PeerAddr.v4.sin_addr.S_un.S_addr),
            _byteswap_ushort(m_PeerAddr.v4.sin_port) ) );
        break;

    case AF_INET6:
        if( *pAddressLength < sizeof(struct sockaddr_in6) )
        {
            *pAddressLength = sizeof(struct sockaddr_in6);
            return ND_BUFFER_OVERFLOW;
        }
        *(struct sockaddr_in6*)pAddress = m_PeerAddr.v6;
        break;

    default:
        return ND_CONNECTION_INVALID;
    }

    return S_OK;
}

HRESULT CConnector::NotifyDisconnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_ENTER( ND_DBG_NDI );

    pOverlapped->Internal = ND_PENDING;
    return g_NtDeviceIoControlFile(
        m_pParent->GetFileHandle(),
        pOverlapped->hEvent,
        NULL,
        (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
        (IO_STATUS_BLOCK*)&pOverlapped->Internal,
        UAL_NDI_NOTIFY_DREQ,
        &m_cid,
        sizeof(m_cid),
        NULL,
        0 );
}

HRESULT CConnector::Disconnect(
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( m_pEndpoint == NULL )
        return ND_CONNECTION_INVALID;

    ib_qp_state_t state =
        m_pParent->m_Ifc.user_verbs.nd_get_qp_state( m_pEndpoint->m_uQp );
    if( state != IB_QPS_RTS )
        return ND_CONNECTION_INVALID;

    //
    // Get the UVP's buffer for modify operations.
    //
    void* pOutbuf;
    DWORD szOutbuf;

    m_pParent->m_Ifc.user_verbs.nd_modify_qp(
        m_pEndpoint->m_uQp,
        &pOutbuf,
        &szOutbuf
        );

    ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
        ("Disconnect QP %#I64x, QPn %#x\n", m_pEndpoint->m_hQp, m_pEndpoint->m_Qpn ) );

    pOverlapped->Internal = ND_PENDING;
    HRESULT hr = g_NtDeviceIoControlFile(
        m_pParent->GetFileHandle(),
        pOverlapped->hEvent,
        NULL,
        (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
        (IO_STATUS_BLOCK*)&pOverlapped->Internal,
        UAL_NDI_DREQ_CM,
        &m_cid,
        sizeof(m_cid),
        pOutbuf,
        szOutbuf );

    if( SUCCEEDED( hr ) )
    {
        m_pEndpoint->Release();
        m_pEndpoint = NULL;
    }

    return hr;
}

} // namespace
