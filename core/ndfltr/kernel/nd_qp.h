/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions (c) Microsoft Corporation.  All rights reserved.
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

#ifndef _ND_QP_H_
#define _ND_QP_H_

#define ND_QUEUE_PAIR_TAG 'pqdn'

class ND_QUEUE_PAIR
    : public RdmaResource<ND_PROTECTION_DOMAIN, ib_qp_handle_t, ci_interface_t>
{
    typedef RdmaResource<ND_PROTECTION_DOMAIN, ib_qp_handle_t, ci_interface_t> _Base;

    ND_COMPLETION_QUEUE                 *m_pInitiatorCq;
    ND_COMPLETION_QUEUE                 *m_pReceiveCq;
    ND_SHARED_RECEIVE_QUEUE             *m_pSrq;
    ND_ENDPOINT                         *m_pEp;
    UINT32                              m_Qpn;

    LONG                                m_EpBound;

private:
    ND_QUEUE_PAIR(
        __in ND_PROTECTION_DOMAIN* pPd,
        __in ND_COMPLETION_QUEUE* pReceiveCq,
        __in ND_COMPLETION_QUEUE* pInitiatorCq,
        __in ND_SHARED_RECEIVE_QUEUE* pSrq
        );
    ~ND_QUEUE_PAIR();
    NTSTATUS Initialize(
        KPROCESSOR_MODE RequestorMode,
        ULONG InitiatorQueueDepth,
        ULONG MaxInitiatorRequestSge,
        ULONG ReceiveQueueDepth,
        ULONG MaxReceiveRequestSge,
        ULONG MaxInlineDataSize,
        __inout ci_umv_buf_t* pVerbsData
        );

public:
    static
    NTSTATUS
    Create(
        __in ND_PROTECTION_DOMAIN* pPd,
        KPROCESSOR_MODE RequestorMode,
        __in ND_COMPLETION_QUEUE* pReceiveCq,
        __in ND_COMPLETION_QUEUE* pInitiatorCq,
        __in ND_SHARED_RECEIVE_QUEUE* pSrq,
        ULONG InitiatorQueueDepth,
        ULONG MaxInitiatorRequestSge,
        ULONG ReceiveQueueDepth,
        ULONG MaxReceiveRequestSge,
        ULONG MaxInlineDataSize,
        __inout ci_umv_buf_t* pVerbsData,
        __out ND_QUEUE_PAIR** ppQp
        );

    void Dispose();

    void RemoveHandler();

    NTSTATUS Modify(ib_qp_mod_t* pMod, BYTE* pOutBuf, ULONG OutLen);
    NTSTATUS Flush(__out_opt BYTE* pOutBuf, ULONG OutLen);

    NTSTATUS BindEp(__in ND_ENDPOINT* pEp)
    {
        LONG bound = InterlockedCompareExchange(&m_EpBound, 1, 0);
        if (bound != 0) {
            return STATUS_CONNECTION_ACTIVE;
        }

        pEp->Reference();
        m_pEp = pEp;
        return STATUS_SUCCESS;
    }

    ND_ENDPOINT* ClearEp()
    {
        ND_ENDPOINT* ep = reinterpret_cast<ND_ENDPOINT*>(
            InterlockedExchangePointer(reinterpret_cast<VOID**>(&m_pEp), NULL)
            );
        return ep;
    }

    void UnbindEp()
    {
        InterlockedExchange(&m_EpBound, 0);
    }

    UINT32 Qpn() const { return m_Qpn; }

    bool HasSrq() const { return m_pSrq != NULL; }

private:
    static void EventHandler(ib_event_rec_t* pEvent);
};

FN_REQUEST_HANDLER NdQpCreate;
FN_REQUEST_HANDLER NdQpCreateWithSrq;
FN_REQUEST_HANDLER NdQpFree;
FN_REQUEST_HANDLER NdQpFlush;

#endif // _ND_QP_H_
