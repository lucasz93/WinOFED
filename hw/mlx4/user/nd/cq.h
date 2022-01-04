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

class Cq
    : public Overlapped<Cq, IND2CompletionQueue>
{
    typedef Overlapped<Cq, IND2CompletionQueue> _Base;

public:
    static const GUID _Guid;

private:
    Adapter& m_Adapter;

    UINT64 m_hCq;
    ib_cq_handle_t m_uCq;

private:
    Cq& operator =(Cq& rhs);

public:
    Cq(Adapter& adapter);
    ~Cq(void);

    HRESULT Initialize(
        HANDLE hFile,
        ULONG queueDepth,
        USHORT group,
        KAFFINITY affinity
        );

    void* GetInterface(REFIID riid);

    UINT64 GetHandle() const { return m_hCq; }
    ib_cq_handle_t GetUvpCq() const { return m_uCq; }

    static ULONG GetCancelIoctlCode() { return IOCTL_ND_CQ_CANCEL_IO; }

public:
    // *** IND2CompletionQueue methods ***
    STDMETHODIMP GetNotifyAffinity(
        __out USHORT* pGroup,
        __out KAFFINITY* pAffinity
        );

    STDMETHODIMP Resize(
        ULONG queueDepth
        );

    STDMETHODIMP Notify(
        ULONG type,
        __inout OVERLAPPED* pOverlapped
        );

    STDMETHODIMP_(ULONG) GetResults(
        THIS_
        __out_ecount_part(nResults,return) ND2_RESULT results[],
        ULONG nResults
        );

private:
    void FreeCq(ib_api_status_t ibStatus);

    static __forceinline ND2_REQUEST_TYPE DecodeCompletionType(ib_wc_type_t type)
    {
        switch( type )
        {
        case IB_WC_SEND:
            return Nd2RequestTypeSend;
        case IB_WC_RDMA_WRITE:
            return Nd2RequestTypeWrite;
        case IB_WC_RDMA_READ:
            return Nd2RequestTypeRead;
        //case IB_WC_COMPARE_SWAP:
        //case IB_WC_FETCH_ADD:
        case IB_WC_MW_BIND:
            return Nd2RequestTypeBind;
        case IB_WC_RECV:
            return Nd2RequestTypeReceive;
        //case IB_WC_RECV_RDMA_WRITE:
        //case IB_WC_LSO:
        case IB_WC_LOCAL_INV:
            return Nd2RequestTypeInvalidate;
        //case IB_WC_FAST_REG_MR:
        default:
            __assume(0);
        }
    }

    static __forceinline HRESULT DecodeCompletionStatus(ib_wc_status_t status)
    {
        switch( status )
        {
        case IB_WCS_SUCCESS:
            return STATUS_SUCCESS;
        case IB_WCS_LOCAL_LEN_ERR:
            return STATUS_DATA_OVERRUN;
        case IB_WCS_LOCAL_OP_ERR:
            return STATUS_INTERNAL_ERROR;
        case IB_WCS_LOCAL_PROTECTION_ERR:
            return STATUS_ACCESS_VIOLATION;
        case IB_WCS_WR_FLUSHED_ERR:
            return STATUS_CANCELLED;
        case IB_WCS_MEM_WINDOW_BIND_ERR:
            return STATUS_ACCESS_VIOLATION;
        case IB_WCS_REM_ACCESS_ERR:
            return STATUS_ACCESS_VIOLATION;
        case IB_WCS_REM_OP_ERR:
            return STATUS_DATA_ERROR;
        case IB_WCS_RNR_RETRY_ERR:
            return STATUS_IO_TIMEOUT;
        case IB_WCS_TIMEOUT_RETRY_ERR:
            return STATUS_IO_TIMEOUT;
        case IB_WCS_REM_INVALID_REQ_ERR:
            return STATUS_DATA_ERROR;
        case IB_WCS_BAD_RESP_ERR:
            return STATUS_DATA_ERROR;
        case IB_WCS_LOCAL_ACCESS_ERR:
            return STATUS_ACCESS_VIOLATION;
        case IB_WCS_GENERAL_ERR:
            return STATUS_INTERNAL_ERROR;
        case IB_WCS_REM_ABORT_ERR:
            return STATUS_REQUEST_ABORTED;
        default:
            return E_UNEXPECTED;
        }
    }
};

} // namespace ND::v2

namespace v1
{
class Cq
    : public Unknown<Cq, INDCompletionQueue>
{
    typedef Unknown<Cq, INDCompletionQueue> _Base;

    IND2CompletionQueue& m_Cq;

private:
    Cq& operator =(Cq& rhs);

public:
    Cq(IND2CompletionQueue& cq);
    ~Cq(void)
    {
        m_Cq.Release();
    }

    void* GetInterface(REFIID riid);

    IND2CompletionQueue* GetCq() const { return &m_Cq; }

public:
    static HRESULT Create(IND2CompletionQueue& cq, INDCompletionQueue** ppCq);

public:
    // *** INDOverlapped methods ***
    HRESULT STDMETHODCALLTYPE CancelOverlappedRequests(void)
    {
        return m_Cq.CancelOverlappedRequests();
    }

    HRESULT STDMETHODCALLTYPE GetOverlappedResult(
        __inout_opt OVERLAPPED *pOverlapped,
        __out SIZE_T *pNumberOfBytesTransferred,
        __in BOOL bWait
        )
    {
        *pNumberOfBytesTransferred = 0;
        return m_Cq.GetOverlappedResult(pOverlapped, bWait);
    }

    // *** INDCompletionQueue methods ***
    HRESULT STDMETHODCALLTYPE Resize(
        __in SIZE_T nEntries
        )
    {
        if( nEntries > ULONG_MAX )
        {
            return STATUS_INVALID_PARAMETER;
        }

        return m_Cq.Resize(static_cast<ULONG>(nEntries));
    }

    HRESULT STDMETHODCALLTYPE Notify(
        __in DWORD Type,
        __inout_opt OVERLAPPED* pOverlapped
        )
    {
        return m_Cq.Notify(Type, pOverlapped);
    }

    SIZE_T STDMETHODCALLTYPE GetResults(
        __out_ecount(nResults) ND_RESULT* pResults[],
        __in SIZE_T nResults
        );
};

} // namespace ND::v1
} // namespace ND
