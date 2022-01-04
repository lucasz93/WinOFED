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

#include <assert.h>
#include <limits.h>
#include <strsafe.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "NdAdapter.tmh"
#endif


namespace NetworkDirect
{

///////////////////////////////////////////////////////////////////////////////
//
// HPC Pack 2008 Beta 2 SPI
//
///////////////////////////////////////////////////////////////////////////////


CAdapter::CAdapter(void) :
    m_nRef( 1 ),
    m_hSync( INVALID_HANDLE_VALUE ),
    m_hAsync( INVALID_HANDLE_VALUE ),
    m_hCa( 0 ),
    m_hPd( 0 ),
    m_uCa( NULL ),
    m_uPd( NULL ),
    m_pParent( NULL )
{
    m_Ifc.h_uvp_lib = NULL;
}

CAdapter::~CAdapter(void)
{
    if( m_hPd )
        DeallocPd();

    if( m_hCa )
        CloseCa();

    if( m_hSync != INVALID_HANDLE_VALUE )
        CloseHandle( m_hSync );

    if( m_hAsync != INVALID_HANDLE_VALUE )
        CloseHandle( m_hAsync );

    if( m_pParent )
        m_pParent->Release();
}

HRESULT CAdapter::Initialize(
    CProvider* pParent,
    const struct sockaddr* pAddr,
    const IBAT_PORT_RECORD* pPortRecord )
{
    m_pParent = pParent;
    m_pParent->AddRef();

    HRESULT hr = OpenFiles();
    if( FAILED( hr ) )
        return hr;

    hr = OpenCa( pPortRecord->CaGuid );
    if( FAILED( hr ) )
        return hr;

    hr = AllocPd();
    if( FAILED( hr ) )
        return hr;

    m_PortNum = pPortRecord->PortNum;
    m_PortGuid = pPortRecord->PortGuid;

    switch( pAddr->sa_family )
    {
    case AF_INET:
        RtlCopyMemory( &m_Addr.v4, pAddr, sizeof(m_Addr.v4) );
        ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
            ("Local address: IP %#x, port %#hx\n", 
            _byteswap_ulong(m_Addr.v4.sin_addr.S_un.S_addr), _byteswap_ushort(m_Addr.v4.sin_port) ) );
        break;
    case AF_INET6:
        RtlCopyMemory( &m_Addr.v6, pAddr, sizeof(m_Addr.v6) );
        break;
    default:
        return ND_INVALID_ADDRESS;
    }
    return S_OK;
}

HRESULT CAdapter::Create(
    CProvider* pParent,
    const struct sockaddr* pAddr,
    const IBAT_PORT_RECORD* pPortRecord,
    __out INDAdapter** ppAdapter
    )
{
    CAdapter* pAdapter = new CAdapter();
    if( pAdapter == NULL )
        return ND_NO_MEMORY;

    HRESULT hr = pAdapter->Initialize( pParent, pAddr, pPortRecord );
    if( FAILED( hr ) )
    {
        delete pAdapter;
        return hr;
    }

    *ppAdapter = pAdapter;
    return ND_SUCCESS;
}

// IUnknown overrides.
HRESULT CAdapter::QueryInterface(
    const IID &riid,
    LPVOID FAR *ppvObj )
{
    if( IsEqualIID( riid, IID_IUnknown ) )
    {
        *ppvObj = this;
        return S_OK;
    }

    if( IsEqualIID( riid, IID_INDAdapter ) )
    {
        *ppvObj = this;
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG CAdapter::AddRef(void)
{
    return InterlockedIncrement( &m_nRef );
}

ULONG CAdapter::Release(void)
{
    ULONG ref = InterlockedDecrement( &m_nRef );
    if( ref == 0 )
        delete this;

    return ref;
}

// *** INDOverlapped methods ***
HRESULT CAdapter::CancelOverlappedRequests(void)
{
    // Memory registration IOCTLs can't be cancelled.
    return S_OK;
}

HRESULT CAdapter::GetOverlappedResult(
    __inout OVERLAPPED *pOverlapped,
    __out SIZE_T *pNumberOfBytesTransferred,
    __in BOOL bWait
    )
{
    ND_ENTER( ND_DBG_NDI );

    *pNumberOfBytesTransferred = 0;
    ::GetOverlappedResult(
        m_hAsync,
        pOverlapped,
        (DWORD*)pNumberOfBytesTransferred,
        bWait );
    return (HRESULT)pOverlapped->Internal;
}

// INDAdapter overrides.
HANDLE CAdapter::GetFileHandle(void)
{
    return m_hAsync;
}

HRESULT CAdapter::Query(
    __in DWORD VersionRequested,
    __out_bcount_part_opt(*pBufferSize, *pBufferSize) ND_ADAPTER_INFO* pInfo,
    __inout_opt SIZE_T* pBufferSize
    )
{
    ND_ENTER( ND_DBG_NDI );

    if( VersionRequested != 1 )
        return ND_NOT_SUPPORTED;

    if( *pBufferSize < sizeof(ND_ADAPTER_INFO1) )
    {
        *pBufferSize = sizeof(ND_ADAPTER_INFO1);
        return ND_BUFFER_OVERFLOW;
    }

    ib_ca_attr_t* pAttr = NULL;
    DWORD size = 0;
    HRESULT hr = QueryCa( pAttr, &size );
    if( hr != ND_BUFFER_OVERFLOW )
        return hr;

    pAttr = (ib_ca_attr_t*)HeapAlloc( GetProcessHeap(), 0, size );
    if( !pAttr )
        return ND_UNSUCCESSFUL;

    hr = QueryCa( pAttr, &size );
    if( FAILED( hr ) )
    {
        HeapFree( GetProcessHeap(), 0, pAttr );
        return hr;
    }

    pInfo->VendorId = pAttr->vend_id;
    pInfo->DeviceId = pAttr->dev_id;
    pInfo->MaxInboundSge = pAttr->max_sges;
    pInfo->MaxInboundRequests = pAttr->max_wrs;
    pInfo->MaxInboundLength = (SIZE_T)pAttr->p_port_attr[m_PortNum - 1].max_msg_size;
    pInfo->MaxOutboundSge = pAttr->max_sges;
    pInfo->MaxOutboundRequests = pAttr->max_wrs;
    pInfo->MaxOutboundLength = (SIZE_T)pAttr->p_port_attr[m_PortNum - 1].max_msg_size;
    pInfo->MaxInboundReadLimit = pAttr->max_qp_resp_res;
    pInfo->MaxOutboundReadLimit = pAttr->max_qp_init_depth;
    pInfo->MaxCqEntries = pAttr->max_cqes;
#ifndef SIZE_MAX
#ifdef _WIN64
#define SIZE_MAX _UI64_MAX
#else
#define SIZE_MAX UINT_MAX
#endif
#endif
    if( pAttr->init_region_size > SIZE_MAX )
        pInfo->MaxRegistrationSize = SIZE_MAX;
    else
        pInfo->MaxRegistrationSize = (SIZE_T)pAttr->init_region_size;
    pInfo->MaxWindowSize = pInfo->MaxRegistrationSize;
    pInfo->LargeRequestThreshold = 16 * 1024;
    pInfo->MaxCallerData = 56;
    pInfo->MaxCalleeData = 148;
    *pBufferSize = sizeof(ND_ADAPTER_INFO1);
    HeapFree( GetProcessHeap(), 0, pAttr );
    return S_OK;
}

HRESULT CAdapter::Control(
    __in DWORD IoControlCode,
    __in_bcount_opt(InBufferSize) const void* pInBuffer,
    __in SIZE_T InBufferSize,
    __out_bcount_opt(OutBufferSize) void* pOutBuffer,
    __in SIZE_T OutBufferSize,
    __out SIZE_T* pBytesReturned,
    __inout OVERLAPPED* pOverlapped
    )
{
    UNREFERENCED_PARAMETER( IoControlCode );
    UNREFERENCED_PARAMETER( pInBuffer );
    UNREFERENCED_PARAMETER( InBufferSize );
    UNREFERENCED_PARAMETER( pOutBuffer );
    UNREFERENCED_PARAMETER( OutBufferSize );
    UNREFERENCED_PARAMETER( pBytesReturned );
    UNREFERENCED_PARAMETER( pOverlapped );
    
    return ND_NOT_SUPPORTED;
}

HRESULT CAdapter::CreateCompletionQueue(
    __in SIZE_T nEntries,
    __deref_out INDCompletionQueue** ppCq
    )
{
    ND_ENTER( ND_DBG_NDI );

    CCq* pCq = new CCq();
    if( pCq == NULL )
        return ND_NO_MEMORY;

    HRESULT hr = pCq->Initialize( this, nEntries );
    if( FAILED(hr) )
    {
        delete pCq;
        return hr;
    }

    *ppCq = pCq;
    return S_OK;
}


HRESULT CAdapter::RegisterMemory(
    __in_bcount(BufferSize) const void* pBuffer,
    __in SIZE_T BufferSize,
    __inout OVERLAPPED* pOverlapped,
    __deref_out ND_MR_HANDLE* phMr
    )
{
    ND_ENTER( ND_DBG_NDI );

    /* Get a MR tracking structure. */
    CMr* pMr = new CMr();
    if( pMr == NULL )
        return ND_NO_MEMORY;

    pMr->pBase = (const char*)pBuffer;

    if( BufferSize > ULONG_MAX )
        return ND_INVALID_PARAMETER_2;
    pMr->Length = (uint32_t)BufferSize;

    /* Clear the mr_ioctl */
    pMr->mr_ioctl.in.h_pd = m_hPd;
    pMr->mr_ioctl.in.mem_create.vaddr_padding = (ULONG_PTR)pBuffer;
    pMr->mr_ioctl.in.mem_create.length = BufferSize;
    // No support for MWs yet, so simulate it.
    pMr->mr_ioctl.in.mem_create.access_ctrl =
        IB_AC_RDMA_READ | IB_AC_RDMA_WRITE | IB_AC_LOCAL_WRITE;

    *phMr = (ND_MR_HANDLE)pMr;
    //
    // We must issue the IOCTL on the synchronous file handle.
    // If the user bound the async file handle to an I/O completion port
    // DeviceIoControl could succeed, but not return data, so the MR
    // wouldn't be updated.
    //
    // We use a second IOCTL to complet the user's overlapped.
    //
    DWORD bytes_ret = 0;
    BOOL ret = DeviceIoControl( m_hSync, UAL_REG_MR,
        &pMr->mr_ioctl.in, sizeof(pMr->mr_ioctl.in),
        &pMr->mr_ioctl.out, sizeof(pMr->mr_ioctl.out),
        &bytes_ret, NULL );
    if( !ret )
    {
        CL_ASSERT( GetLastError() != ERROR_IO_PENDING );

        delete pMr;
        return ND_UNSUCCESSFUL;
    }
    if( bytes_ret != sizeof(pMr->mr_ioctl.out) || 
        pMr->mr_ioctl.out.status != IB_SUCCESS )
    {
        delete pMr;
        return ND_UNSUCCESSFUL;
    }

    //
    // The registration succeeded.  Now issue the user's IOCTL.
    //
    pOverlapped->Internal = ND_PENDING;
    HRESULT hr = g_NtDeviceIoControlFile(
        m_hAsync,
        pOverlapped->hEvent,
        NULL,
        (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
        (IO_STATUS_BLOCK*)&pOverlapped->Internal,
        UAL_NDI_NOOP,
        NULL,
        0,
        NULL,
        0 );

    if( hr != ND_PENDING && hr != ND_SUCCESS )
    {
        delete pMr;
        return hr;
    }

    AddRef();
    return ND_SUCCESS;
}

HRESULT CAdapter::DeregisterMemory(
    __in ND_MR_HANDLE hMr,
    __inout OVERLAPPED* pOverlapped
    )
{
    ND_ENTER( ND_DBG_NDI );

    CMr* pMr = (CMr*)hMr;

    //
    // We must issue the IOCTL on the synchronous file handle.
    // If the user bound the async file handle to an I/O completion port
    // DeviceIoControl could succeed, but not return data, so the MR
    // wouldn't be updated.
    //
    // We use a second IOCTL to complet the user's overlapped.
    //
    DWORD bytes_ret = 0;
    BOOL ret = DeviceIoControl( m_hSync, UAL_DEREG_MR,
        &pMr->mr_ioctl.out.h_mr, sizeof(pMr->mr_ioctl.out.h_mr),
        &pMr->mr_ioctl.out.status, sizeof(pMr->mr_ioctl.out.status),
        &bytes_ret, NULL );
    if( !ret )
    {
        CL_ASSERT( GetLastError() != ERROR_IO_PENDING );
        pMr->mr_ioctl.out.status = IB_ERROR;
    }
    if( bytes_ret != sizeof(pMr->mr_ioctl.out.status) )
        return ND_UNSUCCESSFUL;

    switch( pMr->mr_ioctl.out.status )
    {
    case IB_SUCCESS:
        {
        delete pMr;
        Release();

        //
        // The deregistration succeeded.  Now issue the user's IOCTL.
        //
        pOverlapped->Internal = ND_PENDING;
        HRESULT hr = g_NtDeviceIoControlFile(
            m_hAsync,
            pOverlapped->hEvent,
            NULL,
            (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
            (IO_STATUS_BLOCK*)&pOverlapped->Internal,
            UAL_NDI_NOOP,
            NULL,
            0,
            NULL,
            0 );
        CL_ASSERT( SUCCEEDED( hr ) );
        return hr;
        }
    case IB_RESOURCE_BUSY:
        return ND_DEVICE_BUSY;

    default:
        return ND_UNSUCCESSFUL;
    }
}

HRESULT CAdapter::CreateMemoryWindow(
    __out ND_RESULT* pInvalidateResult,
    __deref_out INDMemoryWindow** ppMw
    )
{
    ND_ENTER( ND_DBG_NDI );

    CMw* pMw = new CMw();
    if( pMw == NULL )
        return ND_NO_MEMORY;

    HRESULT hr = pMw->Initialize( this, pInvalidateResult );
    if( FAILED(hr) )
    {
        delete pMw;
        return hr;
    }

    *ppMw = pMw;
    return S_OK;
}

HRESULT CAdapter::CreateConnector(
    __deref_out INDConnector** ppConnector
    )
{
    ND_ENTER( ND_DBG_NDI );

    return CConnector::Create( this, ppConnector );
}

HRESULT CAdapter::Listen(
    __in SIZE_T Backlog,
    __in INT Protocol,
    __in USHORT Port,
    __out_opt USHORT* pAssignedPort,
    __deref_out INDListen** ppListen
    )
{
    ND_ENTER( ND_DBG_NDI );

    return CListen::Create(
        this,
        Backlog,
        Protocol,
        Port,
        pAssignedPort,
        ppListen
        );
}

HRESULT CAdapter::GetLocalAddress(
    __out_bcount_part(*pAddressLength, *pAddressLength) struct sockaddr *pAddr,
    __inout SIZE_T *pAddressLength )
{
    ND_ENTER( ND_DBG_NDI );

    switch( m_Addr.v4.sin_family )
    {
    case AF_INET:
        if( *pAddressLength < sizeof(struct sockaddr_in) )
        {
            *pAddressLength = sizeof(struct sockaddr_in);
            return ND_BUFFER_OVERFLOW;
        }
        *(struct sockaddr_in*)pAddr = m_Addr.v4;
        ND_PRINT( TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
            ("Local address: IP %#x, port %#hx\n", 
            _byteswap_ulong(m_Addr.v4.sin_addr.S_un.S_addr), _byteswap_ushort(m_Addr.v4.sin_port) ) );
        return S_OK;

    case AF_INET6:
        if( *pAddressLength < sizeof(struct sockaddr_in6) )
        {
            *pAddressLength = sizeof(struct sockaddr_in6);
            return ND_BUFFER_OVERFLOW;
        }
        *(struct sockaddr_in6*)pAddr = m_Addr.v6;
        return S_OK;

    default:
        return ND_INVALID_ADDRESS;
    }
}

HRESULT CAdapter::OpenFiles(void)
{
    ND_ENTER( ND_DBG_NDI );

    /* First open the kernel device. */
    CL_ASSERT( m_hSync == INVALID_HANDLE_VALUE );
    m_hSync = CreateFile(
        "\\\\.\\ibal",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
        );
    if( m_hSync == INVALID_HANDLE_VALUE )
        return ND_UNSUCCESSFUL;

    // Now issue the BIND IOCTL which associates us as a client.
    ULONG ver = AL_IOCTL_VERSION;
    DWORD BytesRet;
    BOOL fSuccess = DeviceIoControl(
        m_hSync,
        UAL_BIND,
        &ver,
        sizeof(ver),
        NULL,
        0,
        &BytesRet,
        NULL
        );
    if( fSuccess != TRUE )
        return ND_UNSUCCESSFUL;

    // Now create the async file.
    m_hAsync = CreateFile(
        "\\\\.\\ibal",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
        );
    if( m_hAsync == INVALID_HANDLE_VALUE )
        return ND_UNSUCCESSFUL;

    /*
     * Send an IOCTL down on the main file handle to bind this file
     * handle with our proxy context.
     */
    UINT64 hFile = (ULONG_PTR)m_hAsync;
    fSuccess = DeviceIoControl(
        m_hSync,
        UAL_BIND_ND,
        &hFile,
        sizeof(hFile),
        NULL,
        0,
        &BytesRet,
        NULL
        );
    if( fSuccess != TRUE )
        return ND_UNSUCCESSFUL;

    return S_OK;
}

HRESULT CAdapter::OpenCa(
    __in UINT64 CaGuid )
{
    ND_ENTER( ND_DBG_NDI );

    ual_get_uvp_name_ioctl_t	al_ioctl;

    /* Initialize assuming no user-mode support */
    RtlZeroMemory( &al_ioctl, sizeof(al_ioctl) );
    RtlZeroMemory( &m_Ifc, sizeof(m_Ifc) );

    /* init with the guid */
    m_Ifc.guid = CaGuid;

    DWORD BytesRet;
    BOOL fSuccess = DeviceIoControl(
        m_hSync,
        UAL_GET_VENDOR_LIBCFG,
        &CaGuid, sizeof(CaGuid),
        &al_ioctl.out, sizeof(al_ioctl.out),
        &BytesRet,
        NULL
        );

    if( fSuccess != TRUE || BytesRet != sizeof(al_ioctl.out) )
        return ND_INSUFFICIENT_RESOURCES;

    if( !strlen( al_ioctl.out.uvp_lib_name ) )
    {
        /* Vendor does not implement user-mode library */
        return ND_NOT_SUPPORTED;
    }

    /*
     * The vendor supports a user-mode library
     * open the library and get the interfaces supported
     */
    HRESULT hr = StringCbCatA( al_ioctl.out.uvp_lib_name, 32, ".dll" );
    if( FAILED( hr ) )
        return ND_NOT_SUPPORTED;

    m_Ifc.h_uvp_lib = LoadLibrary( al_ioctl.out.uvp_lib_name );
    if( m_Ifc.h_uvp_lib == NULL )
        return ND_NOT_SUPPORTED;

    uvp_get_interface_t pfn_uvp_ifc = (uvp_get_interface_t)GetProcAddress(
        m_Ifc.h_uvp_lib,
        "uvp_get_interface"
        );

    if( pfn_uvp_ifc == NULL )
        return ND_NOT_SUPPORTED;

    /* Query the vendor-supported user-mode functions */
    pfn_uvp_ifc( IID_UVP, &m_Ifc.user_verbs );

    ual_open_ca_ioctl_t ca_ioctl;
    RtlZeroMemory( &ca_ioctl, sizeof(ca_ioctl) );

    /* Pre call to the UVP library */
    ib_api_status_t status = IB_ERROR;
    if( m_Ifc.user_verbs.pre_open_ca )
    {
        status = m_Ifc.user_verbs.pre_open_ca(
            CaGuid,
            &ca_ioctl.in.umv_buf,
            (ib_ca_handle_t*)(ULONG_PTR)&m_uCa
            );
        if( status != IB_SUCCESS )
        {
            CL_ASSERT( status != IB_VERBS_PROCESSING_DONE );
            return ND_INSUFFICIENT_RESOURCES;
        }
    }

    ca_ioctl.in.guid = CaGuid;
    ca_ioctl.in.context = (ULONG_PTR)this;

    fSuccess = DeviceIoControl(
        m_hSync,
        UAL_OPEN_CA,
        &ca_ioctl.in,
        sizeof(ca_ioctl.in),
        &ca_ioctl.out,
        sizeof(ca_ioctl.out),
        &BytesRet,
        NULL
        );

    if( fSuccess != TRUE || BytesRet != sizeof(ca_ioctl.out) )
    {
        status = IB_ERROR;
    }
    else
    {
        status = ca_ioctl.out.status;
        m_hCa = ca_ioctl.out.h_ca;
    }

    /* Post uvp call */
    if( m_Ifc.user_verbs.post_open_ca )
    {
        ib_api_status_t uvpStatus = m_Ifc.user_verbs.post_open_ca(
            CaGuid,
            status,
            (ib_ca_handle_t*)(ULONG_PTR)&m_uCa,
            &ca_ioctl.out.umv_buf );
        if( status == IB_SUCCESS )
        {
            // Don't lose an error status.
            status = uvpStatus;
        }
    }

    // TODO: Does the UVP need a query call to succeed?
    // IBAL always does this internally.

    return (status == IB_SUCCESS? S_OK : ND_INSUFFICIENT_RESOURCES );
}

HRESULT CAdapter::QueryCa(
        __out_bcount(*pSize) ib_ca_attr_t* pAttr,
        __inout DWORD* pSize )
{
    ND_ENTER( ND_DBG_NDI );

    ual_query_ca_ioctl_t ca_ioctl;

    RtlZeroMemory( &ca_ioctl, sizeof(ca_ioctl) );

    ca_ioctl.in.h_ca = m_hCa;
    ca_ioctl.in.p_ca_attr = (ULONG_PTR)pAttr;
    ca_ioctl.in.byte_cnt = *pSize;

    /* Call the uvp pre call if the vendor library provided a valid ca handle */
    ib_api_status_t status;
    if( m_uCa && m_Ifc.user_verbs.pre_query_ca )
    {
        /* Pre call to the UVP library */
        status = m_Ifc.user_verbs.pre_query_ca(
            m_uCa, pAttr, *pSize, &ca_ioctl.in.umv_buf );
        if( status != IB_SUCCESS )
            return ND_UNSUCCESSFUL;
    }

    DWORD BytesRet;
    BOOL fSuccess = DeviceIoControl(
        m_hSync,
        UAL_QUERY_CA,
        &ca_ioctl.in,
        sizeof(ca_ioctl.in),
        &ca_ioctl.out,
        sizeof(ca_ioctl.out),
        &BytesRet,
        NULL
        );
    if( fSuccess != TRUE || BytesRet != sizeof(ca_ioctl.out) )
    {
        status = IB_ERROR;
    }
    else
    {
        *pSize = ca_ioctl.out.byte_cnt;
        status = ca_ioctl.out.status;
    }

    /* The attributes, if any, will be directly copied by proxy */
    /* Post uvp call */
    if( m_uCa && m_Ifc.user_verbs.post_query_ca )
    {
        m_Ifc.user_verbs.post_query_ca( m_uCa,
            status, pAttr, ca_ioctl.out.byte_cnt, &ca_ioctl.out.umv_buf );
    }

    switch( status )
    {
    case IB_SUCCESS:
        return S_OK;
    case IB_INSUFFICIENT_MEMORY:
        return ND_BUFFER_OVERFLOW;
    default:
        return ND_INSUFFICIENT_RESOURCES;
    }
}

void CAdapter::CloseCa(void)
{
    ND_ENTER( ND_DBG_NDI );

    ib_api_status_t status;

    /* Call the uvp pre call if the vendor library provided a valid ca handle */
    if( m_uCa && m_Ifc.user_verbs.pre_close_ca )
    {
        /* Pre call to the UVP library */
        status = m_Ifc.user_verbs.pre_close_ca( m_uCa );
        if( status != IB_SUCCESS )
            return;
    }

    DWORD BytesRet;
    BOOL fSuccess = DeviceIoControl(
        m_hSync,
        UAL_CLOSE_CA,
        &m_hCa,
        sizeof(m_hCa),
        &status,
        sizeof(status),
        &BytesRet,
        NULL
        );

    if( fSuccess != TRUE || BytesRet != sizeof(status) )
        status = IB_ERROR;

    if( m_uCa && m_Ifc.user_verbs.post_close_ca )
        m_Ifc.user_verbs.post_close_ca( m_uCa, status );

    if( m_Ifc.h_uvp_lib )
        FreeLibrary( m_Ifc.h_uvp_lib );

    m_hCa = 0;
}

HRESULT CAdapter::AllocPd(void)
{
    ND_ENTER( ND_DBG_NDI );

    /* Clear the pd_ioctl */
    ual_alloc_pd_ioctl_t pd_ioctl;
    RtlZeroMemory( &pd_ioctl, sizeof(pd_ioctl) );

    /* Pre call to the UVP library */
    ib_api_status_t status;
    if( m_uCa && m_Ifc.user_verbs.pre_allocate_pd )
    {
        status = m_Ifc.user_verbs.pre_allocate_pd(
            m_uCa,
            &pd_ioctl.in.umv_buf,
            (ib_pd_handle_t*)(ULONG_PTR)&m_uPd
            );
        if( status != IB_SUCCESS )
            return ND_INSUFFICIENT_RESOURCES;
    }

    pd_ioctl.in.h_ca = m_hCa;
    pd_ioctl.in.type = IB_PDT_NORMAL;
    pd_ioctl.in.context = (ULONG_PTR)this;

    DWORD BytesRet;
    BOOL fSuccess = DeviceIoControl(
        m_hSync,
        UAL_ALLOC_PD,
        &pd_ioctl.in,
        sizeof(pd_ioctl.in),
        &pd_ioctl.out,
        sizeof(pd_ioctl.out),
        &BytesRet,
        NULL
        );

    if( fSuccess != TRUE || BytesRet != sizeof(pd_ioctl.out) )
    {
        status = IB_ERROR;
    }
    else
    {
        status = pd_ioctl.out.status;
        if( pd_ioctl.out.status == IB_SUCCESS )
            m_hPd = pd_ioctl.out.h_pd;
    }

    /* Post uvp call */
    if( m_uCa && m_Ifc.user_verbs.post_allocate_pd )
    {
        m_Ifc.user_verbs.post_allocate_pd(
            m_uCa,
            status,
            (ib_pd_handle_t*)(ULONG_PTR)&m_uPd,
            &pd_ioctl.out.umv_buf );
    }

    return (status == IB_SUCCESS? S_OK : ND_INSUFFICIENT_RESOURCES);
}

void CAdapter::DeallocPd(void)
{
    ND_ENTER( ND_DBG_NDI );

    ib_api_status_t			status;

    /* Call the uvp pre call if the vendor library provided a valid ca handle */
    if( m_uPd && m_Ifc.user_verbs.pre_deallocate_pd )
    {
        /* Pre call to the UVP library */
        status = m_Ifc.user_verbs.pre_deallocate_pd( m_uPd );
        if( status != IB_SUCCESS )
            return;
    }

    DWORD BytesRet;
    BOOL fSuccess = DeviceIoControl(
        m_hSync,
        UAL_DEALLOC_PD,
        &m_hPd,
        sizeof(m_hPd),
        &status,
        sizeof(status),
        &BytesRet,
        NULL
        );

    if( fSuccess == FALSE || BytesRet != sizeof(status) )
        status = IB_ERROR;

    /* Call vendor's post_close ca */
    if( m_uPd && m_Ifc.user_verbs.post_deallocate_pd )
        m_Ifc.user_verbs.post_deallocate_pd( m_uPd, status );

    m_hPd = 0;
}

} // namespace
