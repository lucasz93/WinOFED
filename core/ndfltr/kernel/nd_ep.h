/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
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

#ifndef _ND_EP_H_
#define _ND_EP_H_

#define ND_ENDPOINT_POOL_TAG 'pedn'

class ND_QUEUE_PAIR;

class ND_ENDPOINT
    : public RdmaResource<ND_ADAPTER, iba_cm_id*, INFINIBAND_INTERFACE_CM>
{
    typedef RdmaResource<ND_ADAPTER, iba_cm_id*, INFINIBAND_INTERFACE_CM> _Base;

    ND_PROVIDER                         *m_pProvider;
    ND_QUEUE_PAIR                       *m_pQp;
    PIO_WORKITEM                        m_pWork;

    // TODO: Where is this used?
    IBAT_PORT_RECORD                    m_DeviceAddress;
    SOCKADDR_INET                       m_LocalAddress;
    SOCKADDR_INET                       m_PeerAddress;
    ib_path_rec_t                       m_Route;

    WDFQUEUE                            m_Queue;
    ULONG                               m_Backlog;
    NTSTATUS                            AsyncFlushStatus;

public:
    enum TYPE {
        NdEpConnector,
        NdEpConnectorV1,
        NdEpListener
    }                                   m_Type;

private:
    //
    // State diagram
    //
    // NdEpIdle -----> NdEpAddressBound ----------------> NdEpListening
    //     |                   |                                   |
    //     V                   V                                   |
    // NdEpQueued        NdEpResolving*                            |
    //     |                   |                                   |
    //     V                   V                                   |
    // NdEpReqReceived    NdEpReqSent*                             |
    //     |                   |                                   |
    //     V                   V                                   |
    // NdEpRepSent*     NdEpRepReceived                            |
    //     |                   |                                   |
    //     \___________________/                                   |
    //               |                                             |
    //               V                                             |
    //         NdEpConnected -----> NdEpActiveDisconnect*          |
    //               |                        |                    |
    //               V                        V                    V
    //     NdEpPassiveDisconnect -----> NdDisconnected ----> NdEpDestroying
    //
    // NdEpIdle: via NdEpCreate()
    // NdEpAddressBound:
    //  - from NdEpAddressBound via NdEpBind()
    //  - from NdEpResolving due to path resolution error
    //  - from NdEpReqSent due to timeout or rejection
    // NdEpListening: via NdEpListen()
    // NdEpQueued: via NdEpGetConnectionRequest()
    // NdEpReqReceived: via CM REQ callback
    // NdEpRepSent: via NdEpAccept()
    // NdEpResolving: via NdEpConnect()
    // NdEpReqSent: via IBAT path query callback
    // NdEpRepReceived: enter via CM REP callback, completes NdEpConnect()
    // NdEpConnected:
    //  - from NdEpRepReceived via NdEpCompleteConnect()
    //  - from NdEpRepSent via CM RTU callback or QP established event from hardware,
    //    completes NdAccept()
    // NdEpActiveDisconnect: via NdEpDisconnect()
    // NdEpPassiveDisconnect: via CM DREQ callback
    // NdEpDisconnected:
    //  - from NdEpPassiveDisconnect via NdEpDisconnect()
    //  - from NdEpActiveDisconnect via CM DREP callback or DREQ timeout,
    //    completes NdEpDisconnect()
    //  - from NdEpReqReceived due to REJ received
    //  - from NdEpRepSent due to REP timeout
    //  - from NdEpRepReceived due to REJ received
    //  - from NdEpConnected due to REJ received
    // NdEpDestroying: enter via NdEpFree()
    enum STATE
    {
        NdEpIdle,
        NdEpAddressBound,
        NdEpListening,
        NdEpQueued,
        NdEpReqReceived,
        NdEpRepSent,
        NdEpResolving,
        NdEpReqSent,
        NdEpRepReceived,
        NdEpConnected,
        NdEpActiveDisconnect,
        NdEpPassiveDisconnect,
        NdEpDisconnected,
        NdEpDestroying
    }                                   m_State;

    ULONG                               m_StartingPsn;
    ND_READ_LIMITS                      m_ReadLimits;

    IF_PHYSICAL_ADDRESS                 m_DestinationHwAddress;

    UINT8                               m_PrivateDataLength;
    BYTE                                m_PrivateData[IB_REJ_PDATA_SIZE];

    static ULONG                        s_RandomSeed;

private:
    ND_ENDPOINT(__in ND_ADAPTER* pAdapter, ND_ENDPOINT::TYPE Type, WDFQUEUE Queue);
    ~ND_ENDPOINT();

public:
    static
    NTSTATUS
    Create(
        __in ND_ADAPTER* pAdapter,
        KPROCESSOR_MODE RequestorMode,
        ND_ENDPOINT::TYPE Type,
        __out ND_ENDPOINT** ppEp
        );

    void Dispose();

    void RemoveHandler();

    NTSTATUS Bind(
        __in const SOCKADDR_INET& address,
        __in LUID AuthenticationId,
        __in bool IsAdmin
        );

    NTSTATUS Connect(
        WDFREQUEST Request,
        __inout ND_QUEUE_PAIR* pQp,
        __in const SOCKADDR_INET& DestinationAddress,
        __in const IF_PHYSICAL_ADDRESS& DestinationHwAddress
        );

    NTSTATUS CompleteConnect(UINT8 RnrNakTimeout, __out BYTE* pOutBuf, ULONG OutLen);

    NTSTATUS
    Accept(
        WDFREQUEST Request,
        __inout ND_QUEUE_PAIR* pQp,
        __in const NDFLTR_ACCEPT* pAccept,
        __out BYTE* pOutBuf,
        ULONG OutLen
        );

    NTSTATUS Reject(BYTE* PrivateData, ULONG PrivateDataLength);
    NTSTATUS GetReadLimits(__out ND_READ_LIMITS* pReadLimits);
    NTSTATUS GetPrivateData(__out VOID* pPrivateData, __inout size_t* pPrivateDataLength);
    NTSTATUS GetPeerAddress(__out SOCKADDR_INET* pAddress);
    NTSTATUS GetAddress(__out SOCKADDR_INET* pAddress);
    NTSTATUS NotifyDisconnect(WDFREQUEST Request);
    NTSTATUS Disconnect(WDFREQUEST Request, __out_opt BYTE* pOutBuf, ULONG OutLen);
    NTSTATUS Listen(ULONG Backlog);
    void CancelIo();
    NTSTATUS GetConnectionRequest(WDFREQUEST Request, ND_ENDPOINT* pEp);

    static void Established(ND_ENDPOINT* pEp);
    static void SetStartingPsn(ULONG RandomSeed) { s_RandomSeed = RandomSeed; }

    void FlushRequest(WDFREQUEST Request, NTSTATUS Status);

private:
    static EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE EvtIoCanceled;
    static FN_IBAT_QUERY_PATH_CALLBACK QueryPathHandler;
    static NTSTATUS CmHandler(iba_cm_id *pIbCmId, iba_cm_event *pEvent);
    static NTSTATUS ListenHandler(iba_cm_id *pIbCmId, iba_cm_event *pEvent);

    static
    void
    FlushQpWorker(
        __in void* IoObject,
        __in_opt void* Context,
        __in PIO_WORKITEM IoWorkItem
        );

private:
    NTSTATUS BindQp(ND_QUEUE_PAIR* pQp);
    void UnbindQp(bool FlushQp, __out_opt BYTE* pOutBuf, ULONG OutLen);
    NTSTATUS ModifyQp(
        ib_qp_state_t State,
        UINT8 RnrNakTimeout,
        __out BYTE* pOutBuf,
        ULONG OutLen
        );

    static
    void
    FormatRdmaCmHeader(
        __out ib_cm_rdma_req_t* pHeader,
        __in const SOCKADDR_INET& LocalAddress,
        __in const SOCKADDR_INET& PeerAddress
        );
    static UINT64 GetServiceId(__in const SOCKADDR_INET& pAddress);
    NTSTATUS SendIbCmReq(__in const NDFLTR_CONNECT* pConnect);
    void SendConnectionRequest(NTSTATUS RouteStatus);
    NTSTATUS FormatIbCmRep(__in const NDFLTR_ACCEPT* pAccept, __out_opt ib_qp_mod_t* pQpMod);

    NTSTATUS ProcessCmReq(ND_ENDPOINT* pEp);
    void ProcessCmRep(iba_cm_rep_event* pReply);
    void ProcessCmRtu();
    void ProcessCmDreq();
    void ProcessCmRej(iba_cm_rej_event* pReject);
    void CmReqError();
    void CmRepError();
    void CompleteDisconnect(NTSTATUS Status);

    void AsyncFlushQp(NTSTATUS Status);
};

void NdEpCreate(ND_PROVIDER *pProvider, ND_ENDPOINT::TYPE EpType, WDFREQUEST Request);

FN_REQUEST_HANDLER NdConnectorCreate;
FN_REQUEST_HANDLER NdListenerCreate;
FN_REQUEST_HANDLER NdEpFree;
FN_REQUEST_HANDLER NdEpCancelIo;
FN_REQUEST_HANDLER NdEpBind;
FN_REQUEST_HANDLER NdEpConnect;
FN_REQUEST_HANDLER NdEpCompleteConnect;
FN_REQUEST_HANDLER NdEpAccept;
FN_REQUEST_HANDLER NdEpReject;
FN_REQUEST_HANDLER NdEpGetReadLimits;
FN_REQUEST_HANDLER NdEpGetPrivateData;
FN_REQUEST_HANDLER NdEpGetPeerAddress;
FN_REQUEST_HANDLER NdEpGetAddress;
FN_REQUEST_HANDLER NdEpNotifyDisconnect;
FN_REQUEST_HANDLER NdEpDisconnect;
FN_REQUEST_HANDLER NdEpListen;
FN_REQUEST_HANDLER NdEpGetConnectionRequest;

#endif // _ND_EP_H_
