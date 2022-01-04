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

// {22681265-CE87-401F-83B7-EE8DF85C270A}
const GUID Adapter::_Guid = 
    { 0x22681265, 0xce87, 0x401f, { 0x83, 0xb7, 0xee, 0x8d, 0xf8, 0x5c, 0x27, 0xa } };

Adapter::Adapter(Provider& provider)
    : m_hCa(0),
    m_hPd(0),
    m_uCa(NULL),
    m_uPd(NULL),
    m_Provider(provider)
{
    provider.AddRef();
#if DBG
    InterlockedIncrement(&g_nRef[NdAdapter]);
#endif
}


Adapter::~Adapter(void)
{
    if( m_hPd )
    {
        DeallocPd();
    }

    if( m_hCa )
    {
        CloseCa();
    }

    m_Provider.Release();
#if DBG
    InterlockedDecrement(&g_nRef[NdAdapter]);
#endif
}


HRESULT Adapter::Initialize(
    UINT64 adapterId
    )
{
    HRESULT hr = OpenCa(adapterId);
    if( FAILED(hr) )
    {
        return hr;
    }

    hr = AllocPd();
    if( FAILED(hr) )
    {
        return hr;
    }

    return S_OK;
}


void* Adapter::GetInterface(const IID &riid)
{
    if( riid == IID_IND2Adapter )
    {
        return static_cast<IND2Adapter*>(this);
    }

    if( riid == _Guid )
    {
        return this;
    }

    return _Base::GetInterface(riid);
}


HRESULT Adapter::CreateOverlappedFile(
    UCHAR flags,
    __deref_out HANDLE* phOverlappedFile
    )
{
    HANDLE hFile = CreateFileW(
        ND_WIN32_DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
        );
    if( hFile == INVALID_HANDLE_VALUE )
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if( flags != 0 )
    {
        BOOL ret = SetFileCompletionNotificationModes(hFile, flags);
        if( ret == FALSE )
        {
            CloseHandle(hFile);
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    ND_HANDLE in;
    in.Version = ND_IOCTL_VERSION;
    in.Reserved = 0;
    in.Handle = reinterpret_cast<ULONG_PTR>(hFile);

    HRESULT hr = m_Provider.Ioctl(IOCTL_ND_PROVIDER_BIND_FILE, &in, sizeof(in));
    if( FAILED(hr) )
    {
        CloseHandle(hFile);
    }
    else
    {
        *phOverlappedFile = hFile;
    }
    return hr;
}


HRESULT Adapter::CreateOverlappedFile(
    __deref_out HANDLE* phOverlappedFile
    )
{
    return CreateOverlappedFile(
        FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE,
        phOverlappedFile
        );
}


HRESULT Adapter::Query(
    __inout_bcount_opt(*pcbInfo) ND2_ADAPTER_INFO* pInfo,
    __inout ULONG* pcbInfo
    )
{
    ND_ADAPTER_QUERY in;
    in.Version = ND_IOCTL_VERSION;
    if( pInfo == NULL || *pcbInfo < sizeof(pInfo->InfoVersion) )
    {
        in.InfoVersion = ND_VERSION_2;
    }
    else
    {
        in.InfoVersion = pInfo->InfoVersion;
    }
    in.AdapterHandle = m_hCa;

    HRESULT hr = m_Provider.Ioctl(IOCTL_ND_ADAPTER_QUERY, &in, sizeof(in), pInfo, pcbInfo);
    if( SUCCEEDED(hr) )
    {
        TCHAR buf[14];
        DWORD ret = GetEnvironmentVariable( _T("MLX4ND_INLINE_THRESHOLD"), buf, sizeof(buf) );
        if( ret > 0 && ret < sizeof(buf) )
        {
            pInfo->InlineRequestThreshold = _tstol( buf );
        }
    }
    return hr;
}


HRESULT Adapter::QueryAddressList(
    __out_bcount_part_opt(*pcbAddressList, *pcbAddressList) SOCKET_ADDRESS_LIST* pAddressList,
    __inout ULONG* pcbAddressList
    )
{
    NDFLTR_QUERY_ADDRESS_LIST in;
    in.Header.Version = ND_IOCTL_VERSION;
    in.Header.Reserved = 0;
    in.Header.Handle = m_hCa;
    in.DriverId = GUID_MLX4_DRIVER;

    return m_Provider.Ioctl(
        IOCTL_ND_ADAPTER_QUERY_ADDRESS_LIST,
        &in,
        sizeof(in),
        pAddressList,
        pcbAddressList
        );
}


HRESULT Adapter::CreateCompletionQueue(
    __in REFIID riid,
    __in HANDLE hOverlappedFile,
    ULONG queueDepth,
    USHORT group,
    KAFFINITY affinity,
    __deref_out VOID** ppCompletionQueue
    )
{
    Cq* pCq = new Cq(*this);
    if( pCq == NULL )
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pCq->Initialize(hOverlappedFile, queueDepth, group, affinity);
    if( SUCCEEDED(hr) )
    {
        hr = pCq->QueryInterface(riid, ppCompletionQueue);
    }
    pCq->Release();
    return hr;
}


HRESULT Adapter::CreateMemoryRegion(
    __in REFIID iid,
    __in HANDLE hOverlappedFile,
    __deref_out VOID** ppMemoryRegion
    )
{
    Mr* pMr = new Mr(*this);
    if( pMr == NULL )
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pMr->Initialize(hOverlappedFile);
    if( SUCCEEDED(hr) )
    {
        hr = pMr->QueryInterface(iid, ppMemoryRegion);
    }
    pMr->Release();
    return hr;
}


HRESULT Adapter::CreateMemoryWindow(
    __in REFIID iid,
    __deref_out VOID** ppMemoryWindow
    )
{
    Mr* pMr;
    HRESULT hr = CreateMemoryRegion(
        Mr::_Guid,
        GetProvider().GetFileHandle(),
        reinterpret_cast<void**>(&pMr)
        );
    if( FAILED(hr) )
    {
        return hr;
    }

    Mw* pMw = new Mw(*this,*pMr);
    pMr->Release();

    if( pMw == NULL )
    {
        return E_OUTOFMEMORY;
    }

    hr = pMw->Initialize();
    if( SUCCEEDED(hr) )
    {
        hr = pMw->QueryInterface(iid, ppMemoryWindow);
    }
    pMw->Release();
    return hr;
}


HRESULT Adapter::CreateSharedReceiveQueue(
    __in REFIID iid,
    __in HANDLE hOverlappedFile,
    ULONG queueDepth,
    ULONG maxSge,
    ULONG notifyThreshold,
    USHORT group,
    KAFFINITY affinity,
    __deref_out VOID** ppSharedReceiveQueue
    )
{
    UNREFERENCED_PARAMETER(iid);
    UNREFERENCED_PARAMETER(hOverlappedFile);
    UNREFERENCED_PARAMETER(queueDepth);
    UNREFERENCED_PARAMETER(maxSge);
    UNREFERENCED_PARAMETER(notifyThreshold);
    UNREFERENCED_PARAMETER(group);
    UNREFERENCED_PARAMETER(affinity);
    UNREFERENCED_PARAMETER(ppSharedReceiveQueue);
    return E_NOTIMPL;
}


HRESULT Adapter::CreateQueuePair(
    __in REFIID iid,
    __in IUnknown* pReceiveCompletionQueue,
    __in IUnknown* pInitiatorCompletionQueue,
    __in_opt VOID* context,
    ULONG receiveQueueDepth,
    ULONG initiatorQueueDepth,
    ULONG maxReceiveRequestSge,
    ULONG maxInitiatorRequestSge,
    ULONG inlineDataSize,
    __deref_out VOID** ppQueuePair
    )
{
    Cq* pReceiveCq;
    HRESULT hr = pReceiveCompletionQueue->QueryInterface(
        Cq::_Guid,
        reinterpret_cast<void**>(&pReceiveCq)
        );
    if( FAILED(hr) )
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    Cq* pInitiatorCq;
    hr = pInitiatorCompletionQueue->QueryInterface(
        Cq::_Guid,
        reinterpret_cast<void**>(&pInitiatorCq)
        );
    if( FAILED(hr) )
    {
        pReceiveCq->Release();
        return STATUS_INVALID_PARAMETER_3;
    }

    Qp* pQp = new Qp(*this, *pReceiveCq, *pInitiatorCq, NULL);
    pInitiatorCq->Release();
    pReceiveCq->Release();
    if( pQp == NULL )
    {
        return E_OUTOFMEMORY;
    }

    hr = pQp->Initialize(
        context,
        receiveQueueDepth,
        initiatorQueueDepth,
        maxReceiveRequestSge,
        maxInitiatorRequestSge,
        inlineDataSize
        );
    if( SUCCEEDED(hr) )
    {
        hr = pQp->QueryInterface(iid, ppQueuePair);
    }
    pQp->Release();
    return hr;
}


HRESULT Adapter::CreateQueuePairWithSrq(
    __in REFIID iid,
    __in IUnknown* pReceiveCompletionQueue,
    __in IUnknown* pInitiatorCompletionQueue,
    __in IUnknown* pSharedReceiveQueue,
    __in_opt VOID* context,
    ULONG initiatorQueueDepth,
    ULONG maxInitiatorRequestSge,
    ULONG inlineDataSize,
    __deref_out VOID** ppQueuePair
    )
{
    Cq* pReceiveCq;
    HRESULT hr = pReceiveCompletionQueue->QueryInterface(
        Cq::_Guid,
        reinterpret_cast<void**>(&pReceiveCq)
        );
    if( FAILED(hr) )
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    Cq* pInitiatorCq;
    hr = pInitiatorCompletionQueue->QueryInterface(
        Cq::_Guid,
        reinterpret_cast<void**>(&pInitiatorCq)
        );
    if( FAILED(hr) )
    {
        pReceiveCq->Release();
        return STATUS_INVALID_PARAMETER_3;
    }

    Srq* pSrq;
    hr = pSharedReceiveQueue->QueryInterface(
        Srq::_Guid,
        reinterpret_cast<void**>(&pSrq)
        );
    if( FAILED(hr) )
    {
        pInitiatorCq->Release();
        pReceiveCq->Release();
    }

    Qp* pQp = new Qp(*this, *pReceiveCq, *pInitiatorCq, pSrq);
    pSrq->Release();
    pInitiatorCq->Release();
    pReceiveCq->Release();
    if( pQp == NULL )
    {
        return E_OUTOFMEMORY;
    }

    hr = pQp->Initialize(
        context,
        0,
        initiatorQueueDepth,
        0,
        maxInitiatorRequestSge,
        inlineDataSize
        );
    if( SUCCEEDED(hr) )
    {
        hr = pQp->QueryInterface(iid, ppQueuePair);
    }
    pQp->Release();
    return hr;
}


HRESULT Adapter::CreateConnector(
    __in REFIID iid,
    __in HANDLE hOverlappedFile,
    BOOLEAN Ndv1TimeoutSemantics,
    __deref_out VOID** ppConnector
    )
{
    Connector* pConn = new Connector(*this);
    if( pConn == NULL )
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pConn->Initialize(hOverlappedFile, Ndv1TimeoutSemantics);
    if( SUCCEEDED(hr) )
    {
        hr = pConn->QueryInterface(iid, ppConnector);
    }
    pConn->Release();
    return hr;
}


HRESULT Adapter::CreateConnector(
    __in REFIID iid,
    __in HANDLE hOverlappedFile,
    __deref_out VOID** ppConnector
    )
{
    return CreateConnector(iid, hOverlappedFile, FALSE, ppConnector);
}


HRESULT Adapter::CreateListener(
    __in REFIID iid,
    __in HANDLE hOverlappedFile,
    __deref_out VOID** ppListener
    )
{
    Listener* pListener = new Listener(*this);
    if( pListener == NULL )
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pListener->Initialize(hOverlappedFile);
    if( SUCCEEDED(hr) )
    {
        hr = pListener->QueryInterface(iid, ppListener);
    }
    pListener->Release();
    return hr;
}


HRESULT Adapter::OpenCa(__in UINT64 caGuid)
{
    struct _open
    {
        union _ioctl
        {
            ND_OPEN_ADAPTER in;
            ND_RESOURCE_DESCRIPTOR out;
        } ioctl;

        union _dev
        {
            struct ibv_get_context_req req;
            struct ibv_get_context_resp resp;
        } dev;
    } open;

    open.ioctl.in.Version = ND_IOCTL_VERSION;
    open.ioctl.in.CeMappingCount = _countof(open.dev.req.mappings);
    open.ioctl.in.CbMappingsOffset = FIELD_OFFSET(struct _open, dev.req.mappings);
    open.ioctl.in.AdapterId = caGuid;

    ci_umv_buf_t umvBuf;
    ib_api_status_t ibStatus = mlx4_pre_open_ca(caGuid, &umvBuf, &m_uCa);
    if( ibStatus != IB_SUCCESS )
    {
        return ConvertIbStatus( ibStatus );
    }

    if( umvBuf.input_size != sizeof(open.dev.req) ||
        umvBuf.output_size != sizeof(open.dev.resp) )
    {
        mlx4_post_open_ca(caGuid, IB_INSUFFICIENT_RESOURCES, &m_uCa, &umvBuf);
        return E_UNEXPECTED;
    }

    RtlCopyMemory(
        &open.dev.req,
        reinterpret_cast<const void*>(umvBuf.p_inout_buf),
        sizeof(open.dev.req)
        );

    ULONG cbOut = sizeof(open.ioctl) + sizeof(open.dev.resp);
    HRESULT hr = m_Provider.Ioctl(
        IOCTL_ND_ADAPTER_OPEN,
        &open,
        sizeof(open.ioctl) + sizeof(open.dev.req),
        &open,
        &cbOut
        );
    if( FAILED(hr) )
    {
        mlx4_post_open_ca(caGuid, IB_INSUFFICIENT_RESOURCES, &m_uCa, &umvBuf);
        return hr;
    }

    m_hCa = open.ioctl.out.Handle;

    if( cbOut != sizeof(open.ioctl) + sizeof(open.dev.resp) )
    {
        ibStatus = IB_INSUFFICIENT_RESOURCES;
        mlx4_post_open_ca(caGuid, ibStatus, &m_uCa, &umvBuf);
    }
    else
    {
        RtlCopyMemory(
            reinterpret_cast<void*>(umvBuf.p_inout_buf),
            &open.dev.resp,
            sizeof(open.dev.resp)
            );
        ibStatus = mlx4_post_open_ca(caGuid, IB_SUCCESS, &m_uCa, &umvBuf);
    }

    if( ibStatus != IB_SUCCESS )
    {
        m_Provider.FreeHandle(m_hCa, IOCTL_ND_ADAPTER_CLOSE);
        m_hCa = 0;
        return ConvertIbStatus( ibStatus );
    }

    return hr;
}


void Adapter::CloseCa(void)
{
    m_Provider.FreeHandle(m_hCa, IOCTL_ND_ADAPTER_CLOSE);
    mlx4_post_close_ca(m_uCa, IB_SUCCESS);
    m_hCa = 0;
}


HRESULT Adapter::AllocPd(void)
{
    struct _createPd
    {
        union _ioctl
        {
            ND_HANDLE in;
            UINT64 Handle;
        } ioctl;
        struct ibv_alloc_pd_resp resp;
    } createPd;

    createPd.ioctl.in.Version = ND_IOCTL_VERSION;
    createPd.ioctl.in.Reserved = 0;
    createPd.ioctl.in.Handle = m_hCa;

    ci_umv_buf_t umvBuf;
    ib_api_status_t ibStatus = mlx4_pre_alloc_pd(m_uCa, &umvBuf, &m_uPd);
    if( ibStatus != IB_SUCCESS )
    {
        return ConvertIbStatus( ibStatus );
    }

    if( umvBuf.output_size != sizeof(createPd.resp) )
    {
        mlx4_post_alloc_pd(m_uCa, IB_INSUFFICIENT_RESOURCES, &m_uPd, &umvBuf);
        return E_UNEXPECTED;
    }

    ULONG cbOut = sizeof(createPd);
    HRESULT hr = m_Provider.Ioctl(
        IOCTL_ND_PD_CREATE,
        &createPd,
        sizeof(createPd.ioctl.in),
        &createPd,
        &cbOut
        );
    if( FAILED(hr) )
    {
        mlx4_post_alloc_pd(m_uCa, IB_INSUFFICIENT_RESOURCES, &m_uPd, &umvBuf);
        return hr;
    }

    m_hPd = createPd.ioctl.Handle;

    if( cbOut != sizeof(createPd) )
    {
        mlx4_post_alloc_pd(m_uCa, IB_INSUFFICIENT_RESOURCES, &m_uPd, &umvBuf);
        m_Provider.FreeHandle(m_hPd, IOCTL_ND_PD_FREE);
        m_hPd = 0;
        return E_UNEXPECTED;
    }
    else
    {
        RtlCopyMemory(
            reinterpret_cast<void*>(umvBuf.p_inout_buf),
            &createPd.resp,
            sizeof(createPd.resp)
            );
        mlx4_post_alloc_pd(m_uCa, IB_SUCCESS, &m_uPd, &umvBuf);
    }

    return hr;
}


void Adapter::DeallocPd(void)
{
    m_Provider.FreeHandle(m_hPd, IOCTL_ND_PD_FREE);

    mlx4_post_free_pd(m_uPd, IB_SUCCESS);
    m_hPd = 0;
}

} // namespace ND::v2

namespace v1
{

///////////////////////////////////////////////////////////////////////////////
//
// HPC Pack Beta 2 SPI
//
///////////////////////////////////////////////////////////////////////////////


Adapter::Adapter(Provider& provider, v2::Adapter& adapter)
    : m_Provider(provider),
    m_Adapter(adapter),
    m_hAsync(INVALID_HANDLE_VALUE)
{
    provider.AddRef();
    adapter.AddRef();

    InitializeCriticalSection(&m_MrLock);
}


Adapter::~Adapter(void)
{
    if( m_hAsync != INVALID_HANDLE_VALUE )
    {
        CloseHandle( m_hAsync );
    }
    DeleteCriticalSection(&m_MrLock);
    m_Adapter.Release();
    m_Provider.Release();
}


HRESULT Adapter::Initialize(
    __in_bcount(AddressLength) const struct sockaddr* pAddress,
    __in SIZE_T AddressLength
    )
{
    //
    // The address has been resolved and validated to be valid by the caller.
    //
    switch( pAddress->sa_family )
    {
    case AF_INET:
        AddressLength = sizeof(m_Addr.Ipv4);
        break;

    case AF_INET6:
        AddressLength = sizeof(m_Addr.Ipv6);
        break;

    default:
        return STATUS_INVALID_ADDRESS;
    }

    CopyMemory(&m_Addr, pAddress, AddressLength);

    return m_Adapter.CreateOverlappedFile(0, &m_hAsync);
}


HRESULT Adapter::Create(
    __in Provider& provider,
    __in v2::Adapter& adapter,
    __in_bcount(addressLength) const struct sockaddr* pAddress,
    __in SIZE_T addressLength,
    __out INDAdapter** ppAdapter
    )
{
    Adapter* pAdapter = new Adapter(provider, adapter);
    if( pAdapter == NULL )
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pAdapter->Initialize(pAddress, addressLength);
    if( FAILED(hr) ) 
    {
        pAdapter->Release();
    }
    else
    {
        *ppAdapter = pAdapter;
    }
    return hr;
}


void* Adapter::GetInterface(REFIID riid)
{
    if( riid == IID_INDOverlapped )
    {
        return static_cast<INDOverlapped*>(this);
    }

    if( riid == IID_INDAdapter )
    {
        return static_cast<INDAdapter*>(this);
    }

    return _Base::GetInterface(riid);
}


// *** INDOverlapped methods ***
HRESULT Adapter::CancelOverlappedRequests(void)
{
    EnterCriticalSection(&m_MrLock);
    for( ListEntry* pEntry = m_MrList.Next(); pEntry != m_MrList.End(); pEntry = pEntry->Next() )
    {
        static_cast<Mr*>(pEntry)->CancelOverlappedRequests();
    }
    LeaveCriticalSection(&m_MrLock);
    return S_OK;
}


HRESULT Adapter::GetOverlappedResult(
    __inout OVERLAPPED *pOverlapped,
    __out SIZE_T *pNumberOfBytesTransferred,
    __in BOOL bWait
    )
{
    //
    // Clear output param so we can treat it as a DWORD pointer below.
    //
    *pNumberOfBytesTransferred = 0;
    ::GetOverlappedResult(
        m_hAsync,
        pOverlapped,
        reinterpret_cast<DWORD*>(pNumberOfBytesTransferred),
        bWait );
    return static_cast<HRESULT>(pOverlapped->Internal);
}


// INDAdapter overrides.
HANDLE Adapter::GetFileHandle(void)
{
    return m_hAsync;
}


HRESULT Adapter::Query(
    __in DWORD VersionRequested,
    __out_bcount_part_opt(*pBufferSize, *pBufferSize) ND_ADAPTER_INFO* pInfo,
    __inout_opt SIZE_T* pBufferSize
    )
{
    if( VersionRequested != 1 )
        return ND_NOT_SUPPORTED;

    if( *pBufferSize < sizeof(ND_ADAPTER_INFO1) )
    {
        *pBufferSize = sizeof(ND_ADAPTER_INFO1);
        return ND_BUFFER_OVERFLOW;
    }

    ND2_ADAPTER_INFO info;
    info.InfoVersion = ND_VERSION_2;
    ULONG cbInfo = sizeof(info);
    HRESULT hr = m_Adapter.Query(&info, &cbInfo);
    if( SUCCEEDED(hr) )
    {
        pInfo->VendorId = info.VendorId;
        pInfo->DeviceId = info.DeviceId;
        pInfo->MaxInboundSge = min(info.MaxReceiveSge, _MaxSge);
        pInfo->MaxInboundRequests = info.MaxReceiveQueueDepth;
        pInfo->MaxInboundLength = info.MaxTransferLength;
        pInfo->MaxOutboundSge = min(info.MaxInitiatorSge, _MaxSge);
        pInfo->MaxOutboundRequests = info.MaxInitiatorQueueDepth;
        pInfo->MaxOutboundLength = info.MaxTransferLength;
        pInfo->MaxInlineData = info.MaxInlineDataSize;
        pInfo->MaxInboundReadLimit = info.MaxInboundReadLimit;
        pInfo->MaxOutboundReadLimit = info.MaxOutboundReadLimit;
        pInfo->MaxCqEntries = info.MaxCompletionQueueDepth;
        pInfo->MaxRegistrationSize = info.MaxRegistrationSize;
        pInfo->MaxWindowSize = info.MaxWindowSize;
        pInfo->LargeRequestThreshold = info.LargeRequestThreshold;
        pInfo->MaxCallerData = info.MaxCallerData;
        pInfo->MaxCalleeData = info.MaxCalleeData;
    }

    return hr;
}


HRESULT Adapter::Control(
    __in DWORD,
    __in_bcount_opt(InBufferSize) const void*,
    __in SIZE_T,
    __out_bcount_opt(OutBufferSize) void*,
    __in SIZE_T,
    __out SIZE_T*,
    __inout OVERLAPPED*
    )
{
    return E_NOTIMPL;
}


HRESULT Adapter::CreateCompletionQueue(
    __in SIZE_T nEntries,
    __deref_out INDCompletionQueue** ppCq
    )
{
    if( nEntries > ULONG_MAX )
    {
        return STATUS_INVALID_PARAMETER;
    }

    IND2CompletionQueue* pCq;
    HRESULT hr = m_Adapter.CreateCompletionQueue(
        IID_IND2CompletionQueue,
        m_hAsync,
        static_cast<ULONG>(nEntries),
        0,
        0,
        reinterpret_cast<void**>(&pCq)
        );
    if( SUCCEEDED(hr) )
    {
        hr = Cq::Create(*pCq, ppCq);
        pCq->Release();
    }

    return hr;
}


HRESULT Adapter::RegisterMemory(
    __in_bcount(BufferSize) const void* pBuffer,
    __in SIZE_T BufferSize,
    __inout OVERLAPPED* pOverlapped,
    __deref_out ND_MR_HANDLE* phMr
    )
{
    IND2MemoryRegion* pIMr;
    HRESULT hr = m_Adapter.CreateMemoryRegion(
        IID_IND2MemoryRegion,
        m_hAsync,
        reinterpret_cast<void**>(&pIMr)
        );
    if( FAILED(hr) )
    {
        return hr;
    }

    Mr* pMr = new Mr(*this, *pIMr);
    pIMr->Release();

    if( pMr != NULL )
    {
        hr = pMr->Register(pBuffer, BufferSize, pOverlapped);
        if( FAILED(hr) )
        {
            delete pMr;
        }
        else
        {
            m_MrList.InsertTail(pMr);
            *phMr = reinterpret_cast<ND_MR_HANDLE>(pMr);
        }
    }
    return hr;
}


HRESULT Adapter::DeregisterMemory(
    __in ND_MR_HANDLE hMr,
    __inout OVERLAPPED* pOverlapped
    )
{
    Mr* pMr = reinterpret_cast<Mr*>(hMr);

    HRESULT hr = pMr->Deregister(pOverlapped);
    if( SUCCEEDED(hr) )
    {
        pMr->RemoveFromList();
        delete pMr;
    }
    return hr;
}


HRESULT Adapter::CreateMemoryWindow(
    __out ND_RESULT* pInvalidateResult,
    __deref_out INDMemoryWindow** ppMw
    )
{
    IND2MemoryWindow* pIMw;
    HRESULT hr = m_Adapter.CreateMemoryWindow(
        IID_IND2MemoryWindow,
        reinterpret_cast<void**>(&pIMw)
        );
    if( FAILED(hr) )
    {
        return hr;
    }

    Mw* pMw = new Mw(*this, *pIMw, pInvalidateResult);
    pIMw->Release();

    if( pMw == NULL )
    {
        return E_OUTOFMEMORY;
    }

    *ppMw = pMw;
    return S_OK;
}


HRESULT Adapter::CreateConnector(
    __deref_out INDConnector** ppConnector
    )
{
    IND2Connector* pConn;
    HRESULT hr = m_Adapter.CreateConnector(
        IID_IND2Connector,
        m_hAsync,
        TRUE,
        reinterpret_cast<void**>(&pConn)
        );
    if( FAILED(hr) )
    {
        return hr;
    }

    hr = Connector::Create(*this, *pConn, ppConnector);
    pConn->Release();
    return hr;
}


HRESULT Adapter::Listen(
    __in SIZE_T Backlog,
    __in INT /*Protocol*/,
    __in USHORT Port,
    __out_opt USHORT* pAssignedPort,
    __deref_out INDListen** ppListen
    )
{
    IND2Listener* pListener;

    HRESULT hr = m_Adapter.CreateListener(
        IID_IND2Listener,
        m_hAsync,
        reinterpret_cast<void**>(&pListener)
        );
    if( FAILED(hr) )
    {
        return hr;
    }

    hr = Listener::Create(*this, *pListener, Backlog, Port, pAssignedPort, ppListen);
    pListener->Release();
    return hr;
}

} // namespace ND::v1
} // namespace ND
