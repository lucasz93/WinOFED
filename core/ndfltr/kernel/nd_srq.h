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

#ifndef _ND_SRQ_H_
#define _ND_SRQ_H_

#define ND_SHARED_RECEIVE_Q_POOL_TAG 'rsdn'

class ND_SHARED_RECEIVE_QUEUE
    : public RdmaResource<ND_PROTECTION_DOMAIN, ib_srq_handle_t, ci_interface_t>
{
    typedef RdmaResource<ND_PROTECTION_DOMAIN, ib_srq_handle_t, ci_interface_t> _Base;

    WDFQUEUE m_Queue;

private:
    ND_SHARED_RECEIVE_QUEUE(
        __in ND_PROTECTION_DOMAIN* pPd,
        WDFQUEUE queue
        );
    ~ND_SHARED_RECEIVE_QUEUE();

    NTSTATUS Initialize(
        KPROCESSOR_MODE RequestorMode,
        ULONG QueueDepth,
        ULONG MaxRequestSge,
        ULONG NotifyThreshold,
        __inout ci_umv_buf_t* pVerbsData
        );

public:
    static
    NTSTATUS
    Create(
        __in ND_PROTECTION_DOMAIN* pPd,
        KPROCESSOR_MODE RequestorMode,
        ULONG QueueDepth,
        ULONG MaxRequestSge,
        ULONG NotifyThreshold,
        __inout ci_umv_buf_t* pVerbsData,
        __out ND_SHARED_RECEIVE_QUEUE** ppSrq
        );

    void Dispose();

    NTSTATUS
    Modify(
        ULONG QueueDepth,
        ULONG NotifyThreshold,
        ci_umv_buf_t* pVerbsData
        );

    void CancelIo();

    NTSTATUS Notify(WDFREQUEST Request);

    void RemoveHandler();

public:
    static void EventHandler(ib_event_rec_t *pEvent);
};

FN_REQUEST_HANDLER NdSrqCreate;
FN_REQUEST_HANDLER NdSrqFree;
FN_REQUEST_HANDLER NdSrqCancelIo;
FN_REQUEST_HANDLER NdSrqGetAffinity;
FN_REQUEST_HANDLER NdSrqModify;
FN_REQUEST_HANDLER NdSrqNotify;

#endif // _ND_SRQ_H_
