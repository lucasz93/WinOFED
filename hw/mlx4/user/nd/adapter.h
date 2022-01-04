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

class Adapter
    : public Unknown<Adapter, IND2Adapter>
{
    typedef Unknown<Adapter, IND2Adapter> _Base;

public:
    static const GUID _Guid;

private:
    Provider& m_Provider;

    // Kernel handles.
    UINT64 m_hCa;
    UINT64 m_hPd;

    // UVP handles.
    ib_ca_handle_t m_uCa;
    ib_pd_handle_t m_uPd;

private:
    Adapter& operator =(Adapter& rhs);

public:
    Adapter(Provider& provider);
    ~Adapter(void);

    HRESULT Initialize(
        UINT64 adapterId
        );

public:
    STDMETHODIMP CreateOverlappedFile(
        __deref_out HANDLE* phOverlappedFile
        );

    STDMETHODIMP Query(
        __inout_bcount_opt(*pcbInfo) ND2_ADAPTER_INFO* pInfo,
        __inout ULONG* pcbInfo
        );

    STDMETHODIMP QueryAddressList(
        __out_bcount_part_opt(*pcbAddressList, *pcbAddressList) SOCKET_ADDRESS_LIST* pAddressList,
        __inout ULONG* pcbAddressList
        );

    STDMETHODIMP CreateCompletionQueue(
        __in REFIID riid,
        __in HANDLE hOverlappedFile,
        ULONG queueDepth,
        USHORT group,
        KAFFINITY affinity,
        __deref_out VOID** ppCompletionQueue
        );

    STDMETHODIMP CreateMemoryRegion(
        __in REFIID riid,
        __in HANDLE hOverlappedFile,
        __deref_out VOID** ppMemoryRegion
        );

    STDMETHODIMP CreateMemoryWindow(
        __in REFIID riid,
        __deref_out VOID** ppMemoryWindow
        );

    STDMETHODIMP CreateSharedReceiveQueue(
        __in REFIID riid,
        __in HANDLE hOverlappedFile,
        ULONG queueDepth,
        ULONG maxSge,
        ULONG notifyThreshold,
        USHORT group,
        KAFFINITY affinity,
        __deref_out VOID** ppSharedReceiveQueue
        );

    STDMETHODIMP CreateQueuePair(
        __in REFIID riid,
        __in IUnknown* pReceiveCompletionQueue,
        __in IUnknown* pInitiatorCompletionQueue,
        __in_opt VOID* context,
        ULONG receiveQueueDepth,
        ULONG initiatorQueueDepth,
        ULONG maxReceiveRequestSge,
        ULONG maxInitiatorRequestSge,
        ULONG inlineDataSize,
        __deref_out VOID** ppQueuePair
        );

    STDMETHODIMP CreateQueuePairWithSrq(
        __in REFIID riid,
        __in IUnknown* pReceiveCompletionQueue,
        __in IUnknown* pInitiatorCompletionQueue,
        __in IUnknown* pSharedReceiveQueue,
        __in_opt VOID* context,
        ULONG initiatorQueueDepth,
        ULONG maxInitiatorRequestSge,
        ULONG inlineDataSize,
        __deref_out VOID** ppQueuePair
        );

    STDMETHODIMP CreateConnector(
        __in REFIID riid,
        __in HANDLE hOverlappedFile,
        __deref_out VOID** ppConnector
        );

    STDMETHODIMP CreateListener(
        __in REFIID riid,
        __in HANDLE hOverlappedFile,
        __deref_out VOID** ppListener
        );

public:
    void* Adapter::GetInterface(const IID &riid);

    HRESULT CreateOverlappedFile(
       UCHAR flags,
        __deref_out HANDLE* phOverlappedFile
        );

    HRESULT CreateConnector(
        __in REFIID riid,
        __in HANDLE hOverlappedFile,
        BOOLEAN Ndv1TimeoutSemantics,
        __deref_out VOID** ppConnector
        );

    Provider& GetProvider() const { return m_Provider; }

    ib_ca_handle_t GetUvpCa() const { return m_uCa; }

    UINT64 GetHandle() const { return m_hCa; }

    ib_pd_handle_t GetUvpPd() const { return m_uPd; }

    UINT64 GetPdHandle() const { return m_hPd; }

private:
    HRESULT OpenCa(__in UINT64 caGuid);

    void CloseCa(void);

    HRESULT AllocPd(void);

    void DeallocPd(void);

};

} // namespace ND::v2


namespace v1
{

class Adapter
    : public Unknown<Adapter, INDAdapter>
{
    typedef Unknown<Adapter, INDAdapter> _Base;

    Provider& m_Provider;
    v2::Adapter& m_Adapter;

    HANDLE m_hAsync;

    SOCKADDR_INET m_Addr;

    CRITICAL_SECTION m_MrLock;
    ListEntry m_MrList;

public:
    static const SIZE_T _MaxSge = 4;

private:
    Adapter& operator =(Adapter& rhs);

private:
    Adapter(Provider& provider, v2::Adapter& adapter);

    HRESULT Initialize(
        __in_bcount(addressLength) const struct sockaddr* pAddress,
        __in SIZE_T addressLength
        );

public:
    static HRESULT Create(
        __in Provider& provider,
        __in v2::Adapter& adapter,
        __in_bcount(addressLength) const struct sockaddr* pAddress,
        __in SIZE_T addressLength,
        __out INDAdapter** ppAdapter
        );
    ~Adapter(void);

    void* GetInterface(REFIID riid);

    IND2Adapter* GetAdapter() const
    {
        return static_cast<IND2Adapter*>(&m_Adapter);
    }

    const SOCKADDR_INET& GetAddress() const { return m_Addr; }

public:
    // *** INDOverlapped methods ***
    STDMETHODIMP CancelOverlappedRequests();

    STDMETHODIMP GetOverlappedResult(
        __inout OVERLAPPED *pOverlapped,
        __out SIZE_T *pNumberOfBytesTransferred,
        BOOL bWait
        );

    // *** INDAdapter methods ***
    STDMETHODIMP_(HANDLE) GetFileHandle();

    STDMETHODIMP Query(
        DWORD VersionRequested,
        __out_bcount_part_opt(*pBufferSize, *pBufferSize) ND_ADAPTER_INFO* pInfo,
        __inout_opt SIZE_T* pBufferSize
        );

    STDMETHODIMP Control(
        DWORD IoControlCode,
        __in_bcount_opt(InBufferSize) const void* pInBuffer,
        SIZE_T InBufferSize,
        __out_bcount_opt(OutBufferSize) void* pOutBuffer,
        SIZE_T OutBufferSize,
        __out SIZE_T* pBytesReturned,
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP CreateCompletionQueue(
        SIZE_T nEntries,
        __deref_out INDCompletionQueue** ppCq
        );

    STDMETHODIMP RegisterMemory(
        __in_bcount(BufferSize) const void* pBuffer,
        SIZE_T BufferSize,
        __inout OVERLAPPED* pOverlapped,
        __deref_out ND_MR_HANDLE* phMr
        );

    STDMETHODIMP DeregisterMemory(
        __in ND_MR_HANDLE hMr,
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP CreateMemoryWindow(
        __out ND_RESULT* pInvalidateResult,
        __deref_out INDMemoryWindow** ppMw
        );

    STDMETHODIMP CreateConnector(
        __deref_out INDConnector** ppConnector
        );

    STDMETHODIMP Listen(
        SIZE_T Backlog,
        INT Protocol,
        USHORT Port,
        __out_opt USHORT* pAssignedPort,
        __deref_out INDListen** ppListen
        );
};

} // namespace ND::v1
} // namespace ND
