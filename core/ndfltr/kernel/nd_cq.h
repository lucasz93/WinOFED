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

#ifndef _ND_CQ_H_
#define _ND_CQ_H_

#define ND_COMPLETION_Q_POOL_TAG 'qcdn'

class ND_COMPLETION_QUEUE
    : public RdmaResource<ND_ADAPTER, ib_cq_handle_t, ci_interface_t>
{
    typedef RdmaResource<ND_ADAPTER, ib_cq_handle_t, ci_interface_t> _Base;

    WDFQUEUE                                    m_Queue;
    WDFQUEUE                                    m_ErrorQueue;
    NTSTATUS                                    m_Status;

private:
    ND_COMPLETION_QUEUE(__in ND_ADAPTER* pAdapter, WDFQUEUE queue, WDFQUEUE errorQueue);
    ~ND_COMPLETION_QUEUE();

    NTSTATUS Initialize(
        KPROCESSOR_MODE RequestorMode,
        ULONG QueueDepth,
        const GROUP_AFFINITY& Affinity,
        __inout ci_umv_buf_t* pVerbsData
        );

public:
    static
    NTSTATUS
    Create(
        __in ND_ADAPTER* pAdapter,
        KPROCESSOR_MODE RequestorMode,
        ULONG QueueDepth,
        const GROUP_AFFINITY& Affinity,
        __inout ci_umv_buf_t* pVerbsData,
        __out ND_COMPLETION_QUEUE** ppCq
        );

    NTSTATUS Modify(
        ULONG QueueDepth,
        __in ci_umv_buf_t* pVerbsData
        );

    void Dispose();

    void RemoveHandler();

    void CancelIo();

    NTSTATUS Notify(WDFREQUEST Request, ULONG Type);

private:
    static void EventHandler(ib_event_rec_t *pEvent);
    static void CompletionHandler(void* Context);

};

FN_REQUEST_HANDLER NdCqCreate;
FN_REQUEST_HANDLER NdCqFree;
FN_REQUEST_HANDLER NdCqCancelIo;
FN_REQUEST_HANDLER NdCqGetAffinity;
FN_REQUEST_HANDLER NdCqModify;
FN_REQUEST_HANDLER NdCqNotify;

#endif //_ND_CQ_H_
