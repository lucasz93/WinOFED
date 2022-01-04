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

#pragma once

#include "ndspi.h"
#include "nddebug.h"
#include <iba/ib_al.h>
#include <iba/ib_at_ioctl.h>
#include <ws2tcpip.h>
#include "ual_ci_ca.h"

#include "NdProv.h"

#include "al_dev.h"
#pragma warning( push, 3 )
#include "winternl.h"
#pragma warning( pop )

typedef HRESULT  (*NtDeviceIoControlFile_t)(
  __in   HANDLE FileHandle,
  __in   HANDLE Event,
  __in   PIO_APC_ROUTINE ApcRoutine,
  __in   PVOID ApcContext,
  __out  PIO_STATUS_BLOCK IoStatusBlock,
  __in   ULONG IoControlCode,
  __in   PVOID InputBuffer,
  __in   ULONG InputBufferLength,
  __out  PVOID OutputBuffer,
  __in   ULONG OutputBufferLength
);
extern NtDeviceIoControlFile_t g_NtDeviceIoControlFile;


namespace NetworkDirect
{


	
class CCq;
class CAddr;
class CProvider;

class CAdapter :
    public INDAdapter
{
    friend class CCq;
    friend class CEndpoint;
    friend class CConnector;
    friend class CMw;
    friend class CMr;
    friend class CListen;

private:
    CAdapter(void);
    ~CAdapter(void);

    HRESULT Initialize(
        CProvider* pParent,
        const struct sockaddr* pAddr,
        const IBAT_PORT_RECORD* pPortRecord
        );

public:
    static HRESULT Create(
        CProvider* pParent,
        const struct sockaddr* pAddr,
        const IBAT_PORT_RECORD* pPortRecord,
        __out INDAdapter** ppAdapter
        );

    // *** IUnknown methods ***
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        LPVOID FAR* ppvObj
        );

    ULONG STDMETHODCALLTYPE AddRef(void);

    ULONG STDMETHODCALLTYPE Release(void);

    // *** INDOverlapped methods ***
    HRESULT STDMETHODCALLTYPE CancelOverlappedRequests(void);

    HRESULT STDMETHODCALLTYPE GetOverlappedResult(
        __inout OVERLAPPED *pOverlapped,
        __out SIZE_T *pNumberOfBytesTransferred,
        __in BOOL bWait
        );

    // *** INDAdapter methods ***
    HANDLE STDMETHODCALLTYPE GetFileHandle(void);

    HRESULT STDMETHODCALLTYPE Query(
        __in DWORD VersionRequested,
        __out_bcount_part_opt(*pBufferSize, *pBufferSize) ND_ADAPTER_INFO* pInfo,
        __inout_opt SIZE_T* pBufferSize
        );

    HRESULT STDMETHODCALLTYPE Control(
        __in DWORD IoControlCode,
        __in_bcount_opt(InBufferSize) const void* pInBuffer,
        __in SIZE_T InBufferSize,
        __out_bcount_opt(OutBufferSize) void* pOutBuffer,
        __in SIZE_T OutBufferSize,
        __out SIZE_T* pBytesReturned,
        __inout OVERLAPPED* pOverlapped
        );

    HRESULT STDMETHODCALLTYPE CreateCompletionQueue(
        __in SIZE_T nEntries,
        __deref_out INDCompletionQueue** ppCq
        );

    HRESULT STDMETHODCALLTYPE RegisterMemory(
        __in_bcount(BufferSize) const void* pBuffer,
        __in SIZE_T BufferSize,
        __inout OVERLAPPED* pOverlapped,
        __deref_out ND_MR_HANDLE* phMr
        );

    HRESULT STDMETHODCALLTYPE CompleteRegisterMemory(
        __in ND_MR_HANDLE hMr,
        __in OVERLAPPED* pOverlapped
        );

    HRESULT STDMETHODCALLTYPE DeregisterMemory(
        __in ND_MR_HANDLE hMr,
        __inout OVERLAPPED* pOverlapped
        );

    HRESULT STDMETHODCALLTYPE CompleteDeregisterMemory(
        __in ND_MR_HANDLE hMr,
        __in OVERLAPPED* pOverlapped
        );

    HRESULT STDMETHODCALLTYPE CreateMemoryWindow(
        __out ND_RESULT* pInvalidateResult,
        __deref_out INDMemoryWindow** ppMw
        );

    HRESULT STDMETHODCALLTYPE CreateConnector(
        __deref_out INDConnector** ppConnector
        );

    HRESULT STDMETHODCALLTYPE Listen(
        __in SIZE_T Backlog,
        __in INT Protocol,
        __in USHORT Port,
        __out_opt USHORT* pAssignedPort,
        __deref_out INDListen** ppListen
        );

    HRESULT GetLocalAddress(
        __out_bcount_part(*pAddressLength, *pAddressLength) struct sockaddr* pAddr,
        __inout SIZE_T *pAddressLength );

private:
    HRESULT OpenFiles(void);

    HRESULT OpenCa(
        __in UINT64 CaGuid
        );

    HRESULT QueryCa(
        __out_bcount(*pSize) ib_ca_attr_t* pAttr,
        __inout DWORD* pSize
        );

    void CloseCa(void);

    HRESULT AllocPd(void);

    void DeallocPd(void);

protected:
    volatile LONG m_nRef;

    CProvider* m_pParent;
    UINT64 m_PortGuid;
    UINT8 m_PortNum;

    HANDLE m_hSync;
    HANDLE m_hAsync;

    ual_ci_interface_t m_Ifc;
    // Kernel IBAL handles.
    uint64_t m_hCa;
    uint64_t m_hPd;
    // UVP handles.
    ib_ca_handle_t m_uCa;
    ib_pd_handle_t m_uPd;

    union _addr
    {
        struct sockaddr unspec;
        struct sockaddr_in v4;
        struct sockaddr_in6 v6;

    } m_Addr;
};

} // namespace
