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

#include <precomp.h>

#define CM_LOCAL_TIMEOUT		(1)
#define CM_REMOTE_TIMEOUT		(2)

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ND_ENDPOINT, NdEpGetContext);


ULONG ND_ENDPOINT::s_RandomSeed;


inline bool
IsLoopback(const SOCKADDR_INET& addr)
{
    if (addr.si_family == AF_INET) {
        if (addr.Ipv4.sin_addr.s_addr == _byteswap_ulong(INADDR_LOOPBACK)) {
            return true;
        } else {
            return false;
        }
    }

    return IN6_IS_ADDR_LOOPBACK(&addr.Ipv6.sin6_addr) == TRUE;
}


ND_ENDPOINT::ND_ENDPOINT(ND_ADAPTER* pAdapter, ND_ENDPOINT::TYPE Type, WDFQUEUE Queue)
    : _Base(pAdapter, &pAdapter->GetDevice()->CmInterface),
    m_pProvider(pAdapter->GetProvider()),
    m_pQp(NULL),
    m_Type(Type),
    m_State(NdEpIdle),
    m_Backlog(0),
    m_PrivateDataLength(0),
    m_Queue(Queue),
    m_StartingPsn(RtlRandomEx(&s_RandomSeed))
{
    m_pWork = reinterpret_cast<PIO_WORKITEM>(this + 1);
    IoInitializeWorkItem(WdfDeviceWdmGetDeviceObject(pAdapter->GetDevice()->WdfDev), m_pWork);
    RtlZeroMemory(&m_DestinationHwAddress, sizeof(m_DestinationHwAddress));
    RtlZeroMemory(&m_ReadLimits, sizeof(m_ReadLimits));
    pAdapter->AddEp(&m_Entry);
}


ND_ENDPOINT::~ND_ENDPOINT()
{
    WdfObjectAcquireLock(m_Queue);
    STATE prevState = m_State;
    m_State = NdEpDestroying;
    WdfObjectReleaseLock(m_Queue);

    UnbindQp(true, NULL, 0);

    if( prevState == NdEpResolving ) {
        NTSTATUS status = m_pParent->GetProvider()->GetPartition()->CancelQuery(
            m_LocalAddress,
            m_PeerAddress,
            m_DestinationHwAddress,
            ND_ENDPOINT::QueryPathHandler,
            this
            );
        if( status == STATUS_SUCCESS )
        {
            Dereference();
        }
    }

    _Base::RunDown();

    if (m_hIfc != NULL) {
        m_pIfc->CM.destroy_id(m_hIfc);
    }

    WdfIoQueuePurgeSynchronously(m_Queue);

    IoUninitializeWorkItem(m_pWork);
    m_pParent->RemoveEp(&m_Entry);
}


//static
NTSTATUS
ND_ENDPOINT::Create(
    __in ND_ADAPTER* pAdapter,
    KPROCESSOR_MODE /*RequestorMode*/,
    __in ND_ENDPOINT::TYPE Type,
    __out ND_ENDPOINT** ppEp
    )
{
    WDF_IO_QUEUE_CONFIG config;
    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    config.EvtIoCanceledOnQueue = ND_ENDPOINT::EvtIoCanceled;

    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, ND_ENDPOINT);
    attr.ExecutionLevel = WdfExecutionLevelDispatch;
    attr.SynchronizationScope = WdfSynchronizationScopeNone;
    attr.ContextSizeOverride = sizeof(ND_ENDPOINT) + IoSizeofWorkItem();

    WDFQUEUE queue;
    NTSTATUS status = WdfIoQueueCreate(ControlDevice, &config, &attr, &queue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    *ppEp = new(NdEpGetContext(queue)) ND_ENDPOINT(pAdapter, Type, queue);
    return STATUS_SUCCESS;
}


void ND_ENDPOINT::Dispose()
{
    this->ND_ENDPOINT::~ND_ENDPOINT();
    WdfObjectDelete(m_Queue);
}


void ND_ENDPOINT::RemoveHandler()
{
    WdfObjectAcquireLock(m_Queue);
    m_State = NdEpDestroying;
    WdfObjectReleaseLock(m_Queue);

    if (m_hIfc != NULL) {
        m_pIfc->CM.destroy_id(m_hIfc);
    }
    _Base::RemoveHandler();

    WDFREQUEST    request;
    NTSTATUS status = WdfIoQueueRetrieveNextRequest(m_Queue, &request);

    while (NT_SUCCESS(status)) {
        FlushRequest(request, STATUS_DEVICE_REMOVED);
        status = WdfIoQueueRetrieveNextRequest(m_Queue, &request);
    }
}


NTSTATUS ND_ENDPOINT::BindQp(ND_QUEUE_PAIR* pQp)
{
    NT_ASSERT(m_pQp == NULL);

    //
    // We need to protect against a user trying to use the same QP
    // simultaneously for two connections.  The QP's EP pointer is
    // only ever set through the connection paths.  If we succeed
    // in setting the QP's EP pointer, then we are safe to set our
    // QP pointer.
    //
    NTSTATUS status = pQp->BindEp(this);
    if (NT_SUCCESS(status)) {
        pQp->AddRef();
        m_pQp = pQp;
    }

    return status;
}


void ND_ENDPOINT::UnbindQp(bool FlushQp, __out_opt BYTE* pOutBuf, ULONG OutLen)
{
    ND_QUEUE_PAIR* qp = reinterpret_cast<ND_QUEUE_PAIR*>(
        InterlockedExchangePointer(reinterpret_cast<VOID**>(&m_pQp), NULL)
        );

    if (qp != NULL) {
        ND_ENDPOINT* ep = qp->ClearEp();
        if (ep != NULL) {
            NT_ASSERT(ep == this);
            ep->Dereference();
        }
        if (FlushQp) {
            NT_ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
            qp->Flush(pOutBuf, OutLen);
        }
        qp->UnbindEp();
        qp->Release();
    }
}


void NdEpCreate(ND_PROVIDER *pProvider, ND_ENDPOINT::TYPE EpType, WDFREQUEST Request)
{
    NDFLTR_EP_CREATE* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Header.Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    if (in->Ndv1TimeoutSemantics == TRUE && EpType == ND_ENDPOINT::NdEpConnector) {
        EpType = ND_ENDPOINT::NdEpConnectorV1;
    }

    UINT64* out;
    size_t outLen;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*out),
        reinterpret_cast<VOID**>(&out),
        &outLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    pProvider->LockShared();

    ND_ADAPTER* adapter;
    status = pProvider->GetAdapter(in->Header.Handle, &adapter);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_ENDPOINT* ep;
    status = ND_ENDPOINT::Create(
        adapter,
        WdfRequestGetRequestorMode(Request),
        EpType,
        &ep
        );

    if (!NT_SUCCESS(status)) {
        goto err3;
    }

    *out = pProvider->AddEp(ep);
    if (*out == 0) {
        ep->Dispose();
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        WdfRequestSetInformation(Request, outLen);
    }

err3:
    adapter->Release();
err2:
    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


void
NdConnectorCreate(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    NdEpCreate(pProvider, ND_ENDPOINT::NdEpConnector, Request);
}


void
NdListenerCreate(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    NdEpCreate(pProvider, ND_ENDPOINT::NdEpListener, Request);
}


void
NdEpFree(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    pProvider->LockShared();
    status = pProvider->FreeEp(in->Handle);
    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


void
NdEpCancelIo(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Handle, &ep);
    if (NT_SUCCESS(status)) {
        ep->CancelIo();
        ep->Release();
    }

    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


NTSTATUS
ND_ENDPOINT::Bind(
    __in const SOCKADDR_INET& address,
    __in LUID /*AuthenticationId*/,
    __in bool /*IsAdmin*/
    )
{
    IBAT_PORT_RECORD deviceAddress;
    NTSTATUS status = m_pParent->GetDeviceAddress(address, &deviceAddress);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WdfObjectAcquireLock(m_Queue);
    if (m_State != NdEpIdle) {
        WdfObjectReleaseLock(m_Queue);
        return STATUS_ADDRESS_ALREADY_ASSOCIATED;
    }

    m_LocalAddress = address;
    m_DeviceAddress = deviceAddress;

    NT_ASSERT(m_hIfc == NULL);
    if (m_Type == NdEpListener) {
        status = m_pIfc->CM.create_id(ListenHandler, this, &m_hIfc);
    } else {
        status = m_pIfc->CM.create_id(CmHandler, this, &m_hIfc);
    }
    if (NT_SUCCESS(status)) {
        //
        // Note that the port number is in the same location in both the IPv4 and IPv6
        // addresses.
        //
        //TODO: Sort port allocation/validation.
        //
        if (m_LocalAddress.Ipv4.sin_port == 0) {
            m_LocalAddress.Ipv4.sin_port =
                static_cast<USHORT>(m_hIfc->cid) ^
                static_cast<USHORT>(m_hIfc->cid >> 16);
        }
        m_State = ND_ENDPOINT::NdEpAddressBound;
    }

    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpBind(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    LUID luid = {0,0};
    bool isAdmin = false;

    ND_BIND* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    if (WdfRequestGetRequestorMode(Request) == UserMode) {
        SECURITY_SUBJECT_CONTEXT context;
        SeCaptureSubjectContext(&context);
        SeLockSubjectContext(&context);
        PACCESS_TOKEN token = SeQuerySubjectContextToken(&context);
        status = SeQueryAuthenticationIdToken(token, &luid);
        isAdmin = (SeTokenIsAdmin(token) == TRUE);
        SeUnlockSubjectContext(&context);
    } else {
        NDK_BIND* in;
        NTSTATUS status = WdfRequestRetrieveInputBuffer(
            Request,
            sizeof(*in),
            reinterpret_cast<VOID**>(&in),
            NULL
            );
        if (NT_SUCCESS(status)) {
            luid = in->AuthenticationId;
            isAdmin = (in->IsAdmin == TRUE);
        }
    }

    if (!NT_SUCCESS(status)) {
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Handle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->Bind(in->Address, luid, isAdmin);
        ep->Release();
    }
    pProvider->Unlock();

err:
    WdfRequestComplete(Request, status);
}


void
ND_ENDPOINT::QueryPathHandler(
    __in void* CompletionContext,
    __in NTSTATUS QueryStatus,
    __in ib_path_rec_t* const pPath
    )
{
    ND_ENDPOINT* ep = reinterpret_cast<ND_ENDPOINT*>(CompletionContext);

    if (NT_SUCCESS(QueryStatus)) {
        RtlCopyMemory(&ep->m_Route, pPath, sizeof(ep->m_Route));
    }
    ep->SendConnectionRequest(QueryStatus);
}


void
ND_ENDPOINT::SendConnectionRequest(NTSTATUS RouteStatus)
{
    WDFREQUEST request;
    NTSTATUS status = WdfIoQueueRetrieveNextRequest(m_Queue, &request);
    if (!NT_SUCCESS(status)) {
        //
        // EvtIoCanceled will have been called.
        //
        goto err1;
    }

    NDFLTR_CONNECT* in;
    size_t inLen;
    status = WdfRequestRetrieveInputBuffer(
        request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    //
    // We retrieved the input buffer successfully before, and should still be able to now.
    //
    NT_ASSERT(NT_SUCCESS(status));
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    NT_ASSERT(in->Header.CbPrivateDataOffset + in->Header.CbPrivateDataLength <= inLen);

    WdfObjectAcquireLock(m_Queue);
    if (m_State != NdEpResolving) {
        status = STATUS_CANCELLED;
        goto err2;
    }

    if (!NT_SUCCESS(RouteStatus)) {
        m_State = NdEpAddressBound;
        status = RouteStatus;
        goto err2;
    }

    status = SendIbCmReq(in);
    if (!NT_SUCCESS(status)) {
        m_State = NdEpAddressBound;
        goto err2;
    }

    m_State = NdEpReqSent;
    WdfRequestRequeue(request);
    WdfObjectReleaseLock(m_Queue);
    Dereference();
    return;

err2:
    UnbindQp(false, NULL, 0);
    WdfObjectReleaseLock(m_Queue);
    WdfRequestComplete(request, status);
err1:
    Dereference();
}


NTSTATUS
ND_ENDPOINT::Connect(
    WDFREQUEST Request,
    __inout ND_QUEUE_PAIR* pQp,
    __in const SOCKADDR_INET& DestinationAddress,
    __in const IF_PHYSICAL_ADDRESS& DestinationHwAddress
    )
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State)
    {
    case NdEpAddressBound:
        if (m_Type == NdEpListener) {
            status = STATUS_INVALID_DEVICE_REQUEST;
        } else {
            status = BindQp(pQp);
        }
        break;
    case NdEpIdle:
        status = STATUS_ADDRESS_NOT_ASSOCIATED;
        break;
    case NdEpQueued:
    case NdEpDisconnected:
    case NdEpDestroying:
        status = STATUS_INVALID_DEVICE_STATE;
        break;
    default:
        status = STATUS_CONNECTION_ACTIVE;
        break;
    }
    if (!NT_SUCCESS(status)) {
        WdfObjectReleaseLock(m_Queue);
        return status;
    }
    status = WdfRequestForwardToIoQueue(Request, m_Queue);
    if (!NT_SUCCESS(status)) {
        UnbindQp(false, NULL, 0);
        WdfObjectReleaseLock(m_Queue);
        return status;
    }

    m_State = NdEpResolving;

    if (IsLoopback(DestinationAddress) == true) {
        m_PeerAddress = m_LocalAddress;
        //
        // Port number is in the same place for IPv4 and IPv6 addresses.
        //
        m_PeerAddress.Ipv4.sin_port = DestinationAddress.Ipv4.sin_port;
    } else {
        m_PeerAddress = DestinationAddress;
    }
    m_DestinationHwAddress = DestinationHwAddress;
    WdfObjectReleaseLock(m_Queue);

    Reference();
    status = m_pParent->GetProvider()->GetPartition()->QueryPath(
        m_LocalAddress,
        m_PeerAddress,
        m_DestinationHwAddress,
        ND_ENDPOINT::QueryPathHandler,
        this,
        &m_Route
        );
    if (status != STATUS_PENDING) {
        SendConnectionRequest(status);
        status = STATUS_PENDING;
    }
    return status;
}


UINT64 ND_ENDPOINT::GetServiceId(const SOCKADDR_INET& Address)
{
    return ib_cm_rdma_cm_sid(IPPROTO_TCP, Address.Ipv4.sin_port);
}


void
ND_ENDPOINT::FormatRdmaCmHeader(
    __out ib_cm_rdma_req_t* pHeader,
    __in const SOCKADDR_INET& LocalAddress,
    __in const SOCKADDR_INET& PeerAddress
    )
{
    pHeader->maj_min_ver = IB_REQ_CM_RDMA_VERSION;
    if (LocalAddress.si_family == AF_INET) {
        pHeader->ipv = 4 << 4;
        pHeader->src_ip_addr[0] = pHeader->src_ip_addr[1] = pHeader->src_ip_addr[2] = 0;
        pHeader->src_ip_addr[3] = LocalAddress.Ipv4.sin_addr.s_addr;
        pHeader->dst_ip_addr[0] = pHeader->dst_ip_addr[1] = pHeader->dst_ip_addr[2] = 0;
        pHeader->dst_ip_addr[3] = PeerAddress.Ipv4.sin_addr.s_addr;
        pHeader->src_port = LocalAddress.Ipv4.sin_port;
    } else {
        pHeader->ipv = 6 << 4;
        RtlCopyMemory(pHeader->src_ip_addr, &LocalAddress.Ipv6.sin6_addr, sizeof(IN6_ADDR));
        RtlCopyMemory(pHeader->dst_ip_addr, &PeerAddress.Ipv6.sin6_addr, sizeof(IN6_ADDR));
        pHeader->src_port = LocalAddress.Ipv6.sin6_port;
    }
}


void ND_ENDPOINT::ProcessCmRep(iba_cm_rep_event* pReply)
{
    WdfObjectAcquireLock(m_Queue);
    if (m_State == NdEpReqSent) {
        //
        // We don't distinguish between REP and REJ sizes - both are 'CalleeData'
        // and REJ is smaller.
        //
        m_PrivateDataLength = IB_REJ_PDATA_SIZE;
        RtlCopyMemory(m_PrivateData, pReply->rep.p_pdata, IB_REJ_PDATA_SIZE);

        NT_ASSERT(pReply->rep.init_depth <= m_pParent->GetDevice()->Info.MaxInboundReadLimit);
        m_ReadLimits.Inbound = pReply->rep.init_depth;

        NT_ASSERT(pReply->rep.resp_res <= m_pParent->GetDevice()->Info.MaxOutboundReadLimit);
        m_ReadLimits.Outbound = pReply->rep.resp_res;

        m_State = NdEpRepReceived;
        NdCompleteRequests(m_Queue, STATUS_SUCCESS);
    }
    WdfObjectReleaseLock(m_Queue);
}


void ND_ENDPOINT::ProcessCmRtu()
{
    WdfObjectAcquireLock(m_Queue);
    if (m_State == NdEpRepSent) {
        m_State = NdEpConnected;
        //
        // We no longer need the QP's back pointer to us, as we are now established.
        //
        ND_ENDPOINT* pEp = m_pQp->ClearEp();
        if (pEp != NULL) {
            NT_ASSERT(pEp == this);
            pEp->Dereference();
        }
        NdCompleteRequests(m_Queue, STATUS_SUCCESS);
    }
    WdfObjectReleaseLock(m_Queue);
}


void ND_ENDPOINT::ProcessCmDreq()
{
    WdfObjectAcquireLock(m_Queue);
    if (m_State == NdEpConnected) {
        m_State = NdEpPassiveDisconnect;
        //
        // The only requests that can be queued in the NdEpConnected state are NotifyDisconnect
        // requests.  We flush the request queue and the QP.
        //
        // Note that AsyncFlushQp will remove the first request in the queue, so flush the queue
        // first to ensure timely delivery of the NotifyDisconnect.
        //
        NdCompleteRequests(m_Queue, STATUS_SUCCESS);
        AsyncFlushQp(STATUS_SUCCESS);
    } else if (m_State == NdEpRepSent) {
        m_State = NdEpPassiveDisconnect;
        //
        // Note that the Accept() call is still oustanding at this point, but reception
        // of the DREQ indicates that the RTU was sent (but must have been dropped).
        // We complete the Accept() request with STATUS_SUCCESS since we were successfully
        // connected.
        //
        NdCompleteRequests(m_Queue, STATUS_SUCCESS);
    } else if (m_State == NdEpActiveDisconnect) {
        m_State = NdEpDisconnected;
        m_pIfc->CM.send_drep(m_hIfc, NULL, 0);
        AsyncFlushQp(STATUS_SUCCESS);
    }
    WdfObjectReleaseLock(m_Queue);
}


void ND_ENDPOINT::CompleteDisconnect(NTSTATUS Status)
{
    WdfObjectAcquireLock(m_Queue);
    if (m_State == NdEpActiveDisconnect) {
        m_State = NdEpDisconnected;
        AsyncFlushQp(Status);
    }
    WdfObjectReleaseLock(m_Queue);
}


void ND_ENDPOINT::ProcessCmRej(iba_cm_rej_event* pReject)
{
    WdfObjectAcquireLock(m_Queue);
    if (m_State == NdEpReqSent) {
        m_State = NdEpAddressBound;
        m_PrivateDataLength = IB_REJ_PDATA_SIZE;
        RtlCopyMemory(m_PrivateData, pReject->p_pdata, IB_REJ_PDATA_SIZE);
        NdCompleteRequests(m_Queue, STATUS_CONNECTION_REFUSED);
    } else if (m_State == NdEpRepSent) {
        m_State = NdEpDisconnected;
        m_PrivateDataLength = IB_REJ_PDATA_SIZE;
        RtlCopyMemory(m_PrivateData, pReject->p_pdata, IB_REJ_PDATA_SIZE);
        AsyncFlushQp(STATUS_CONNECTION_REFUSED);
    } else if (m_State == NdEpConnected) {
        m_State = NdEpPassiveDisconnect;
        NdCompleteRequests(m_Queue, STATUS_SUCCESS);
    }
    WdfObjectReleaseLock(m_Queue);
}


void ND_ENDPOINT::CmReqError()
{
    WdfObjectAcquireLock(m_Queue);
    if (m_State == NdEpReqSent) {
        UnbindQp(false, NULL, 0);
        m_State = NdEpAddressBound;
        if (m_Type == NdEpConnectorV1) {
            //
            // NDSPIv1 wants STATUS_TIMEOUT for operations that can be retried.
            //
            NdCompleteRequests(m_Queue, STATUS_TIMEOUT);
        } else {
            NdCompleteRequests(m_Queue, STATUS_IO_TIMEOUT);
        }
    }
    WdfObjectReleaseLock(m_Queue);
}


void ND_ENDPOINT::CmRepError()
{
    WdfObjectAcquireLock(m_Queue);
    if (m_State != NdEpRepSent) {
        WdfObjectReleaseLock(m_Queue);
        return;
    }
    m_State = NdEpDisconnected;
    AsyncFlushQp(STATUS_IO_TIMEOUT);
    WdfObjectReleaseLock(m_Queue);
}


void
ND_ENDPOINT::FlushQpWorker(
    __in void* /*IoObject*/,
    __in_opt void* Context,
    __in PIO_WORKITEM IoWorkItem
    )
{
    ND_ENDPOINT* ep = reinterpret_cast<ND_ENDPOINT*>(IoWorkItem) - 1;

    ep->UnbindQp(true, NULL, 0);

    WDFREQUEST request = static_cast<WDFREQUEST>(Context);
    if (request != NULL) {
        WdfRequestComplete(request, ep->AsyncFlushStatus);
    }

    InterlockedExchangePointer(reinterpret_cast<VOID**>(&ep->m_pWork), IoWorkItem);
    ep->Dereference();
}


void ND_ENDPOINT::AsyncFlushQp(NTSTATUS Status)
{
    WDFREQUEST request;
    WdfIoQueueRetrieveNextRequest(m_Queue, &request);

    PIO_WORKITEM workItem = reinterpret_cast<PIO_WORKITEM>(
        InterlockedExchangePointer(reinterpret_cast<VOID**>(&m_pWork), NULL)
        );
    NT_ASSERT(workItem != NULL);

    if (workItem == NULL && request != NULL) {
        WdfRequestComplete(request, Status);
        return;
    }

    Reference();
    AsyncFlushStatus = Status;
    IoQueueWorkItemEx(
        workItem,
        FlushQpWorker,
        DelayedWorkQueue,
        request
        );
}


NTSTATUS ND_ENDPOINT::CmHandler(iba_cm_id *pId, iba_cm_event *pEvent)
{
    ND_ENDPOINT *ep = reinterpret_cast<ND_ENDPOINT*>(pId->context);

    switch (pEvent->type) {
    case iba_cm_req_error:
        ep->CmReqError();
        break;
    case iba_cm_rep_error:
        ep->CmRepError();
        break;
    case iba_cm_dreq_error:
        ep->CompleteDisconnect(STATUS_TIMEOUT);
        break;
    case iba_cm_rep_received:
        ep->ProcessCmRep(&pEvent->data.rep);
        break;
    case iba_cm_rtu_received:
        ep->ProcessCmRtu();
        break;
    case iba_cm_dreq_received:
        ep->ProcessCmDreq();
        break;
    case iba_cm_drep_received:
        ep->CompleteDisconnect(STATUS_SUCCESS);
        break;
    case iba_cm_rej_received:
        ep->ProcessCmRej(&pEvent->data.rej);
        break;
    case iba_cm_lap_received:
    case iba_cm_sidr_req_received:
        //
        // We don't support alternate paths or SIDR, we'll let the remote peer timeout.
        // No need to assert, though, since we might interop with some peer that does
        // support these.  It would be nicer to reject the request rather than letting
        // it time out, but we wouldn't get test coverage without a bunch more effort.
        //
        break;
    default:
        NT_ASSERT(
            pEvent->type != iba_cm_req_received &&      // ListenHandler for REQs
            pEvent->type != iba_cm_lap_error &&         // Never sent a LAP
            pEvent->type != iba_cm_apr_received &&      // Never sent a LAP
            pEvent->type != iba_cm_sidr_req_error &&    // Don't support SIDR
            pEvent->type != iba_cm_sidr_rep_received    // Don't support SIDR
            );
        break;
    }

    return STATUS_SUCCESS;
}


NTSTATUS
ND_ENDPOINT::SendIbCmReq(const NDFLTR_CONNECT* pConnect)
{
    ib_cm_rdma_req_t data;
    FormatRdmaCmHeader(&data, m_LocalAddress, m_PeerAddress);

    iba_cm_req req;
    req.service_id = GetServiceId(m_PeerAddress);
    req.p_primary_path = &m_Route;
    req.p_alt_path = NULL;
    req.qpn = m_pQp->Qpn();
    req.qp_type = IB_QPT_RELIABLE_CONN;
    req.starting_psn = static_cast<net32_t>(m_StartingPsn);
    req.p_pdata = &data;
    RtlCopyMemory(
        data.pdata,
        reinterpret_cast<const BYTE*>(pConnect) + pConnect->Header.CbPrivateDataOffset,
        pConnect->Header.CbPrivateDataLength
        );
    req.pdata_len = static_cast<UINT8>(sizeof(IB_CMA_HEADER) + pConnect->Header.CbPrivateDataLength);
    req.max_cm_retries = IB_CMA_MAX_CM_RETRIES;

    if (pConnect->Header.ReadLimits.Inbound > m_pParent->GetDevice()->Info.MaxInboundReadLimit) {
        req.resp_res = static_cast<UINT8>(m_pParent->GetDevice()->Info.MaxInboundReadLimit);
    } else {
        req.resp_res = static_cast<UINT8>(pConnect->Header.ReadLimits.Inbound);
    }

    if (pConnect->Header.ReadLimits.Outbound > m_pParent->GetDevice()->Info.MaxOutboundReadLimit) {
        req.init_depth = static_cast<UINT8>(m_pParent->GetDevice()->Info.MaxOutboundReadLimit);
    } else {
        req.init_depth = static_cast<UINT8>(pConnect->Header.ReadLimits.Outbound);
    }

    req.remote_resp_timeout = ib_path_rec_pkt_life(&m_Route) + CM_REMOTE_TIMEOUT;
    if( req.remote_resp_timeout > 0x1F ) {
        req.remote_resp_timeout = 0x1F;
    } else if( req.remote_resp_timeout < IB_CMA_CM_RESPONSE_TIMEOUT ) {
        req.remote_resp_timeout = IB_CMA_CM_RESPONSE_TIMEOUT;
    }

    req.flow_ctrl = TRUE;

    req.local_resp_timeout = ib_path_rec_pkt_life(&m_Route) + CM_LOCAL_TIMEOUT;
    if( req.local_resp_timeout > 0x1F ) {
        req.local_resp_timeout = 0x1F;
    } else if( req.local_resp_timeout < IB_CMA_CM_RESPONSE_TIMEOUT ) {
        req.local_resp_timeout = IB_CMA_CM_RESPONSE_TIMEOUT;
    }

    req.rnr_retry_cnt = pConnect->RnrRetryCount;
    req.retry_cnt = pConnect->RetryCount;
    req.srq = m_pQp->HasSrq();

    NTSTATUS status = m_pIfc->CM.send_req(
        m_hIfc,
        &req
        );
    return status;
}


void
NdEpConnect(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    NDFLTR_CONNECT* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Header.Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    if (in->Header.CbPrivateDataLength > IB_REQ_CM_RDMA_PDATA_SIZE) {
        status = STATUS_INVALID_BUFFER_SIZE;
        goto err1;
    }

    if (in->Header.CbPrivateDataOffset + in->Header.CbPrivateDataLength > inLen) {
        status = STATUS_INVALID_PARAMETER;
        goto err1;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Header.ConnectorHandle, &ep);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_QUEUE_PAIR* qp;
    status = pProvider->GetQp(in->Header.QpHandle, &qp);
    if (NT_SUCCESS(status)) {
        status = ep->Connect(
            Request,
            qp,
            in->Header.DestinationAddress,
            in->Header.DestinationHwAddress
            );
        qp->Release();
    }

    ep->Release();
err2:
    pProvider->Unlock();
err1:
    if (status != STATUS_PENDING) {
        WdfRequestComplete(Request, status);
    }
}


NTSTATUS ND_ENDPOINT::ModifyQp(ib_qp_state_t State, UINT8 RnrNakTimeout, BYTE* pOutBuf, ULONG OutLen)
{
    ib_qp_mod_t attr;
    NTSTATUS status = m_pIfc->CM.get_qp_attr(m_hIfc, State, &attr);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    if (State == IB_QPS_RTR) {
        attr.state.rtr.resp_res = static_cast<UINT8>(m_ReadLimits.Inbound);
        attr.state.rtr.rnr_nak_timeout = RnrNakTimeout;
    } else if (State == IB_QPS_RTS) {
        attr.state.rts.init_depth = static_cast<UINT8>(m_ReadLimits.Outbound);
    }

    status = m_pQp->Modify(&attr, pOutBuf, OutLen);
    return status;
}


void
NdEpCompleteConnect(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    NDFLTR_COMPLETE_CONNECT* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Header.Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    BYTE* out;
    size_t outLen;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        0,
        reinterpret_cast<VOID**>(&out),
        &outLen
        );
    if (status == STATUS_BUFFER_TOO_SMALL) {
        out = NULL;
        outLen = 0;
    } else if (!NT_SUCCESS(status)) {
        goto err;
    } else if (outLen > ULONG_MAX) {
        status = STATUS_INVALID_PARAMETER;
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Header.Handle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->CompleteConnect(in->RnrNakTimeout, out, static_cast<ULONG>(outLen));
        ep->Release();
        if (NT_SUCCESS(status)) {
            WdfRequestSetInformation(Request, outLen);
        }
    }

    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_ENDPOINT::CompleteConnect(UINT8 RnrNakTimeout, __out BYTE* pOutBuf, ULONG OutLen)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpRepReceived:
        WdfObjectReleaseLock(m_Queue);
        break;

    case NdEpDisconnected:
        WdfObjectReleaseLock(m_Queue);
        return STATUS_CONNECTION_ABORTED;

    default:
        WdfObjectReleaseLock(m_Queue);
        return STATUS_CONNECTION_INVALID;
    }

    status = ModifyQp(IB_QPS_INIT, 0, pOutBuf, OutLen);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = ModifyQp(IB_QPS_RTR, RnrNakTimeout, pOutBuf, OutLen);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = ModifyQp(IB_QPS_RTS, 0, pOutBuf, OutLen);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WdfObjectAcquireLock(m_Queue);
    if (m_State != NdEpRepReceived) {
        m_State = NdEpDisconnected;
        WdfObjectReleaseLock(m_Queue);
        return STATUS_IO_TIMEOUT;
    }

    status = m_pIfc->CM.send_rtu(m_hIfc, NULL, 0);
    if (!NT_SUCCESS(status)) {
        m_State = NdEpDisconnected;
    } else {
        m_State = NdEpConnected;
    }

    WdfObjectReleaseLock(m_Queue);
    return status;
}


NTSTATUS
ND_ENDPOINT::FormatIbCmRep(
    __in const NDFLTR_ACCEPT* pAccept,
    __out_opt ib_qp_mod_t* pQpMod
    )
{
    iba_cm_rep rep;
    rep.qpn = m_pQp->Qpn();
    rep.starting_psn = static_cast<net32_t>(m_StartingPsn);
    rep.p_pdata = reinterpret_cast<const BYTE*>(pAccept) + pAccept->Header.CbPrivateDataOffset;
    rep.pdata_len = static_cast<UINT8>(pAccept->Header.CbPrivateDataLength);
    rep.failover_accepted = IB_FAILOVER_ACCEPT_UNSUPPORTED;

    if (pAccept->Header.ReadLimits.Inbound > m_ReadLimits.Inbound) {
        rep.resp_res = static_cast<UINT8>(m_ReadLimits.Inbound);
    } else {
        rep.resp_res = static_cast<UINT8>(pAccept->Header.ReadLimits.Inbound);
    }

    if (pAccept->Header.ReadLimits.Outbound > m_ReadLimits.Outbound) {
        if (m_ReadLimits.Outbound == 0) {
            //
            // Caller wants to issue RDMA reads, but the connected peer didn't
            // give us any resources to do so.
            //
            return STATUS_INVALID_PARAMETER;
        }
        rep.init_depth = static_cast<UINT8>(m_ReadLimits.Outbound);
    } else {
        rep.init_depth = static_cast<UINT8>(pAccept->Header.ReadLimits.Outbound);
    }

    rep.flow_ctrl = TRUE;
    rep.rnr_retry_cnt = pAccept->RnrRetryCount;
    rep.srq = m_pQp->HasSrq();

    return m_pIfc->CM.format_rep(
        m_hIfc,
        &rep,
        pQpMod
        );
}


void
NdEpAccept(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    NDFLTR_ACCEPT* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Header.Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    if (in->Header.CbPrivateDataLength > IB_REJ_PDATA_SIZE) {
        status = STATUS_INVALID_BUFFER_SIZE;
        goto err1;
    }

    if (in->Header.CbPrivateDataOffset + in->Header.CbPrivateDataLength > inLen) {
        status = STATUS_INVALID_PARAMETER;
        goto err1;
    }

    BYTE* out;
    size_t outLen;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        0,
        reinterpret_cast<VOID**>(&out),
        &outLen
        );
    if (status == STATUS_BUFFER_TOO_SMALL) {
        out = NULL;
        outLen = 0;
    } else if (!NT_SUCCESS(status)) {
        goto err1;
    } else if (outLen > ULONG_MAX) {
        status = STATUS_INVALID_PARAMETER;
        goto err1;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Header.ConnectorHandle, &ep);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_QUEUE_PAIR* qp;
    status = pProvider->GetQp(in->Header.QpHandle, &qp);
    if (NT_SUCCESS(status)) {
        status = ep->Accept(Request, qp, in, out, static_cast<ULONG>(outLen));
        qp->Release();
    }

    ep->Release();
err2:
    pProvider->Unlock();
err1:
    if (status != STATUS_PENDING) {
        WdfRequestComplete(Request, status);
    }
}


NTSTATUS
ND_ENDPOINT::Accept(
    WDFREQUEST Request,
    __inout ND_QUEUE_PAIR* pQp,
    __in const NDFLTR_ACCEPT* pAccept,
    __out BYTE* pOutBuf,
    ULONG OutLen
    )
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch(m_State)
    {
    case NdEpReqReceived:
        status = BindQp(pQp);
        break;

    case NdEpDisconnected:
        status = STATUS_CONNECTION_ABORTED;
        break;

    default:
        status = STATUS_CONNECTION_ACTIVE;
        break;
    }
    if (!NT_SUCCESS(status)) {
        WdfObjectReleaseLock(m_Queue);
        return status;
    }

    m_State = NdEpRepSent;
    WdfObjectReleaseLock(m_Queue);

    ib_qp_mod_t qpMod;
    status = FormatIbCmRep(pAccept, &qpMod);
    if (!NT_SUCCESS(status)) {
        UnbindQp(false, NULL, 0);
        goto err;
    }

    status = m_pQp->Modify(&qpMod, pOutBuf, OutLen);
    if (!NT_SUCCESS(status)) {
        UnbindQp(false, NULL, 0);
        goto err;
    }

    status = ModifyQp(IB_QPS_RTR, pAccept->RnrNakTimeout, pOutBuf, OutLen);
    if (!NT_SUCCESS(status)) {
        UnbindQp(false, NULL, 0);
        goto err;
    }

    //
    // TODO: Delay RTS until RTU/comm established event.
    //
    status = ModifyQp(IB_QPS_RTS, 0, pOutBuf, OutLen);
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    WdfObjectAcquireLock(m_Queue);
    status = m_pIfc->CM.send_formatted_rep(m_hIfc);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    WdfRequestSetInformation(Request, OutLen);
    status = WdfRequestForwardToIoQueue(Request, m_Queue);
    if (!NT_SUCCESS(status)) {
        WdfRequestSetInformation(Request, 0);
        goto err2;
    }
    WdfObjectReleaseLock(m_Queue);

    return STATUS_PENDING;

err:
    WdfObjectAcquireLock(m_Queue);
err2:
    m_State = NdEpDisconnected;
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpReject(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_REJECT* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    if (in->CbPrivateDataLength > IB_REJ_PDATA_SIZE) {
        status = STATUS_INVALID_BUFFER_SIZE;
        goto err1;
    }

    if (in->CbPrivateDataOffset + in->CbPrivateDataLength > inLen) {
        status = STATUS_INVALID_PARAMETER;
        goto err1;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->ConnectorHandle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->Reject(
            reinterpret_cast<BYTE*>(in) + in->CbPrivateDataOffset,
            in->CbPrivateDataLength
            );
        ep->Release();
    }

    pProvider->Unlock();
err1:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_ENDPOINT::Reject(BYTE* PrivateData, ULONG PrivateDataLength)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpReqReceived:
    case NdEpRepReceived:
        status = m_pIfc->CM.send_rej(
            m_hIfc,
            IB_REJ_USER_DEFINED,
            NULL,
            0,
            PrivateData,
            static_cast<UINT8>(PrivateDataLength)
            );
        if (status == STATUS_SUCCESS) {
            m_State = NdEpDisconnected;
            break;
        }
        __fallthrough;

    case NdEpDisconnected:
        //
        // Potentially here because the connection was aborted.
        //
        status = STATUS_CONNECTION_INVALID;
        break;

    default:
        status = STATUS_INVALID_DEVICE_STATE;
    }
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpGetReadLimits(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    ND_READ_LIMITS* out;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*out),
        reinterpret_cast<VOID**>(&out),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Handle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->GetReadLimits(out);
        ep->Release();
        if (NT_SUCCESS(status)) {
            WdfRequestSetInformation(Request, sizeof(*out));
        }
    }

    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_ENDPOINT::GetReadLimits(__out ND_READ_LIMITS* pReadLimits)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpDisconnected:
        status = STATUS_CONNECTION_INVALID;
        break;

    case NdEpReqReceived:
    case NdEpRepReceived:
        RtlCopyMemory(pReadLimits, &m_ReadLimits, sizeof(m_ReadLimits));
        status = STATUS_SUCCESS;
        break;

    default:
        status = STATUS_INVALID_DEVICE_STATE;
    }
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpGetPrivateData(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    VOID* out;
    size_t outLen;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        0,
        &out,
        &outLen
        );
    if (status == STATUS_BUFFER_TOO_SMALL) {
        outLen = 0;
        out = NULL;
    } else if (!NT_SUCCESS(status)) {
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Handle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->GetPrivateData(out, &outLen);
        if (!NT_ERROR(status)) {
            WdfRequestSetInformation(Request, outLen);
        }
        ep->Release();
    }

    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_ENDPOINT::GetPrivateData(__out VOID* pPrivateData, __inout size_t* pPrivateDataLength)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpAddressBound:
    case NdEpDisconnected:
        if (m_PrivateDataLength == 0) {
            status = STATUS_CONNECTION_INVALID;
            break;
        }
        //
        // Reject() can provide private data.  We need to provide a means of retrieving it,
        // though reception of a CM REJ will change our state to either NdEpAddressBound in
        // the case the CM REQ was rejected, or NdEpDisconnected in the case the CM REP or
        // RTU was rejected (the latter due to timeout).
        //
        __fallthrough;

    case NdEpReqReceived:
    case NdEpRepReceived:
        if (*pPrivateDataLength == 0) {
            if (pPrivateData != NULL) {
                status = STATUS_INVALID_PARAMETER;
            }

            *pPrivateDataLength = m_PrivateDataLength;
            status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        if (*pPrivateDataLength < m_PrivateDataLength) {
            RtlCopyMemory(pPrivateData, m_PrivateData, *pPrivateDataLength);
            status = STATUS_BUFFER_OVERFLOW;
        } else {
            *pPrivateDataLength = m_PrivateDataLength;
            RtlCopyMemory(pPrivateData, m_PrivateData, m_PrivateDataLength);
            status = STATUS_SUCCESS;
        }
        break;

    default:
        status = STATUS_INVALID_DEVICE_STATE;
    }
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpGetPeerAddress(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    SOCKADDR_INET* out;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*out),
        reinterpret_cast<VOID**>(&out),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Handle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->GetPeerAddress(out);
        if (NT_SUCCESS(status)) {
            WdfRequestSetInformation(Request, sizeof(*out));
        }
        ep->Release();
    }

    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_ENDPOINT::GetPeerAddress(__out SOCKADDR_INET* pAddress)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpIdle:
    case NdEpQueued:
    case NdEpAddressBound:
        status = STATUS_CONNECTION_INVALID;
        break;

    case NdEpListening:
    case NdEpDestroying:
        status = STATUS_INVALID_DEVICE_STATE;
        break;

    default:
        *pAddress = m_PeerAddress;
        status = STATUS_SUCCESS;
        break;
    }
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpGetAddress(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    SOCKADDR_INET* out;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*out),
        reinterpret_cast<VOID**>(&out),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Handle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->GetAddress(out);
        if (NT_SUCCESS(status)) {
            WdfRequestSetInformation(Request, sizeof(*out));
        }
        ep->Release();
    }

    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_ENDPOINT::GetAddress(__out SOCKADDR_INET* pAddress)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpIdle:
    case NdEpQueued:
        status = STATUS_ADDRESS_NOT_ASSOCIATED;
        break;

    case NdEpDestroying:
        status = STATUS_INVALID_DEVICE_STATE;

    default:
        *pAddress = m_LocalAddress;
        status = STATUS_SUCCESS;
        break;
    }
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpDisconnect(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    BYTE* out;
    size_t outLen;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        0,
        reinterpret_cast<VOID**>(&out),
        &outLen
        );
    if (status == STATUS_BUFFER_TOO_SMALL) {
        out = NULL;
        outLen = 0;
    } else if (!NT_SUCCESS(status)) {
        goto err;
    } else if (outLen > ULONG_MAX) {
        status = STATUS_INVALID_PARAMETER;
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Handle, &ep);
    if (NT_SUCCESS(status)) {
        WdfRequestSetInformation(Request, outLen);
        status = ep->Disconnect(Request, out, static_cast<ULONG>(outLen));
        ep->Release();
        if (!NT_SUCCESS(status)) {
            WdfRequestSetInformation(Request, 0);
        }
    }

    pProvider->Unlock();
err:
    if (status != STATUS_PENDING) {
        WdfRequestComplete(Request, status);
    }
}


NTSTATUS ND_ENDPOINT::Disconnect(WDFREQUEST Request, __out_opt BYTE* pOutBuf, ULONG OutLen)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpConnected:
        //
        // Flush any NotifyDisconnect requests.
        //
        if (m_Type == NdEpConnectorV1) {
            NdCompleteRequests(m_Queue, STATUS_CANCELLED);
        } else {
            NdCompleteRequests(m_Queue, STATUS_CONNECTION_DISCONNECTED);
        }
        // The IB CM could have received and processed a DREQ that we haven't seen yet.
        status = m_pIfc->CM.send_dreq(m_hIfc, NULL, 0);
        if (NT_SUCCESS(status)) {
            status = WdfRequestForwardToIoQueue(Request, m_Queue);
            if (NT_SUCCESS(status)) {
                m_State = NdEpActiveDisconnect;
                status = STATUS_PENDING;
                break;
            }
        }
        /* Fall through to passive disconnect case on failure */
        __fallthrough;

    case NdEpPassiveDisconnect:
        m_State = NdEpDisconnected;
        WdfObjectReleaseLock(m_Queue);

        m_pIfc->CM.send_drep(m_hIfc, NULL, 0);

        UnbindQp(true, pOutBuf, OutLen);
        return STATUS_SUCCESS;

    case NdEpActiveDisconnect:
        status = STATUS_INVALID_DEVICE_STATE;
        break;

    default:
        status = STATUS_CONNECTION_INVALID;
        break;
    }
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpNotifyDisconnect(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_HANDLE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->Handle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->NotifyDisconnect(Request);
        ep->Release();
    }

    pProvider->Unlock();
err:
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
    }
}


NTSTATUS ND_ENDPOINT::NotifyDisconnect(WDFREQUEST Request)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpConnected:
        status = WdfRequestForwardToIoQueue(Request, m_Queue);
        break;
    case NdEpPassiveDisconnect:
        WdfRequestComplete(Request, STATUS_SUCCESS);
        status = STATUS_SUCCESS;
        break;
    case NdEpDisconnected:
    case NdEpActiveDisconnect:
        status = STATUS_CONNECTION_DISCONNECTED;
        break;
    default:
        status = STATUS_INVALID_DEVICE_STATE;
        break;
    }
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void ND_ENDPOINT::Established(ND_ENDPOINT* pEp)
{
    WdfObjectAcquireLock(pEp->m_Queue);
    if (pEp->m_State == NdEpRepSent) {
        pEp->m_pIfc->CM.established(pEp->m_hIfc);
    }
    WdfObjectReleaseLock(pEp->m_Queue);

    pEp->ProcessCmRtu();
}


NTSTATUS ND_ENDPOINT::ProcessCmReq(ND_ENDPOINT* pEp)
{
    iba_cm_event event;

    NT_ASSERT(pEp->m_hIfc == NULL);
    NTSTATUS status = m_pIfc->CM.get_request_ex(
        m_hIfc,
        CmHandler,
        pEp,
        &pEp->m_hIfc,
        &event
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pEp->m_LocalAddress = m_LocalAddress;
    pEp->m_DeviceAddress = m_DeviceAddress;

    ib_cm_rdma_req_t* hdr = reinterpret_cast<ib_cm_rdma_req_t*>(event.data.req.pdata);
    NT_ASSERT(
        (m_LocalAddress.si_family == AF_INET && hdr->ipv == 4 << 4) ||
        (m_LocalAddress.si_family == AF_INET6 && hdr->ipv == 6 << 4)
        );

    if (hdr->ipv == 4 << 4) {
        pEp->m_PeerAddress.si_family = AF_INET;
        pEp->m_PeerAddress.Ipv4.sin_port = hdr->src_port;
        pEp->m_PeerAddress.Ipv4.sin_addr.s_addr = hdr->src_ip_addr[3];
        RtlZeroMemory(
            pEp->m_PeerAddress.Ipv4.sin_zero,
            sizeof(pEp->m_PeerAddress.Ipv4.sin_zero)
            );
    } else {
        pEp->m_PeerAddress.si_family = AF_INET6;
        pEp->m_PeerAddress.Ipv6.sin6_port = hdr->src_port;
        pEp->m_PeerAddress.Ipv6.sin6_flowinfo = 0;
        RtlCopyMemory(
            &pEp->m_PeerAddress.Ipv6.sin6_addr,
            hdr->src_ip_addr,
            sizeof(pEp->m_PeerAddress.Ipv6.sin6_addr)
            );
        pEp->m_PeerAddress.Ipv6.sin6_scope_id = 0;
    }

    NT_ASSERT(m_DeviceAddress.CaGuid == event.data.req.local_ca_guid);
    NT_ASSERT(m_DeviceAddress.PKey == event.data.req.primary_path.pkey);
    NT_ASSERT(m_DeviceAddress.PortNum == event.data.req.port_num);

    pEp->m_ReadLimits.Inbound = event.data.req.init_depth;
    pEp->m_ReadLimits.Outbound = event.data.req.resp_res;
    pEp->m_PrivateDataLength = IB_REQ_CM_RDMA_PDATA_SIZE;
    RtlCopyMemory(pEp->m_PrivateData, hdr->pdata, sizeof(hdr->pdata));
    pEp->m_Route = event.data.req.primary_path;

    pEp->m_State = NdEpReqReceived;
    return STATUS_SUCCESS;
}


NTSTATUS ND_ENDPOINT::ListenHandler(iba_cm_id *pId, iba_cm_event*)
{
    ND_ENDPOINT* listener = reinterpret_cast<ND_ENDPOINT*>(pId->context);

    WdfObjectAcquireLock(listener->m_Queue);
    if (listener->m_State != NdEpListening) {
        WdfObjectReleaseLock(listener->m_Queue);
        return STATUS_INVALID_DEVICE_STATE;
    }

    WDFREQUEST request;
    NTSTATUS status;
    for (status = WdfIoQueueRetrieveNextRequest(listener->m_Queue, &request);
        NT_SUCCESS(status);
        status = WdfIoQueueRetrieveNextRequest(listener->m_Queue, &request)) {

        ND_ENDPOINT* ep = reinterpret_cast<ND_ENDPOINT*>(WdfRequestGetInformation(request));

        status = listener->ProcessCmReq(ep);
        if (!NT_SUCCESS(status)) {
            WdfRequestRequeue(request);
            break;
        } else {
            ep->Release();
            WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
        }
    }
    WdfObjectReleaseLock(listener->m_Queue);
    return STATUS_SUCCESS;
}


void
NdEpListen(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_LISTEN* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    pProvider->LockShared();
    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->ListenerHandle, &ep);
    if (NT_SUCCESS(status)) {
        status = ep->Listen(in->Backlog);
        ep->Release();
    }

    pProvider->Unlock();
err:
    WdfRequestComplete(Request, status);
}


NTSTATUS ND_ENDPOINT::Listen(ULONG)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    switch (m_State) {
    case NdEpAddressBound:
        break;

    default:
        status = STATUS_INVALID_DEVICE_STATE;
        goto unlock;
    }

    ib_cm_rdma_req_t pdata;
    FormatRdmaCmHeader(&pdata, m_LocalAddress, m_LocalAddress);

    //
    // TODO: This call should take as input the port GUID to bind the listen to that single port.
    //
    status = m_pIfc->CM.listen(
        m_hIfc,
        GetServiceId(m_LocalAddress),
        &pdata,
        FIELD_OFFSET(ib_cm_rdma_req_t, pdata),
        IB_REQ_CM_RDMA_CMP_DST_IP
        );
    if (NT_SUCCESS(status)) {
        m_State = NdEpListening;
    }

unlock:
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void
NdEpGetConnectionRequest(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    ND_GET_CONNECTION_REQUEST* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto err1;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err1;
    }

    pProvider->LockShared();
    ND_ENDPOINT* listener;
    status = pProvider->GetEp(in->ListenerHandle, &listener);
    if (!NT_SUCCESS(status)) {
        goto err2;
    }

    ND_ENDPOINT* ep;
    status = pProvider->GetEp(in->ConnectorHandle, &ep);
    if (NT_SUCCESS(status)) {
        status = listener->GetConnectionRequest(Request, ep);
        ep->Release();
    }

    listener->Release();
err2:
    pProvider->Unlock();
err1:
    if (status != STATUS_PENDING) {
        WdfRequestComplete(Request, status);
    }
}


NTSTATUS ND_ENDPOINT::GetConnectionRequest(WDFREQUEST Request, ND_ENDPOINT* pEp)
{
    NTSTATUS status;
    WdfObjectAcquireLock(m_Queue);
    if (m_State != NdEpListening) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto err1;
    }

    WdfObjectAcquireLock(pEp->m_Queue);
    if (pEp->m_State != NdEpIdle) {
        status = STATUS_CONNECTION_ACTIVE;
        goto err2;
    }

    status = ProcessCmReq(pEp);
    if (!NT_SUCCESS(status)) {
        pEp->m_State = NdEpQueued;
        pEp->AddRef();
        WdfRequestSetInformation(Request, reinterpret_cast<ULONG_PTR>(pEp));
        status = WdfRequestForwardToIoQueue(Request, m_Queue);
        if (!NT_SUCCESS(status)) {
            WdfRequestSetInformation(Request, 0);
            pEp->Release();
        } else {
            status = STATUS_PENDING;
        }
    }

err2:
    WdfObjectReleaseLock(pEp->m_Queue);
err1:
    WdfObjectReleaseLock(m_Queue);
    return status;
}


void ND_ENDPOINT::CancelIo()
{
    WdfIoQueuePurgeSynchronously(m_Queue);
    WdfIoQueueStart(m_Queue);
}


void ND_ENDPOINT::FlushRequest(WDFREQUEST Request, NTSTATUS Status)
{
    NT_ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    if (m_Type == NdEpListener) {
        ND_ENDPOINT* connector = reinterpret_cast<ND_ENDPOINT*>(
            WdfRequestGetInformation(Request)
            );
        WdfObjectAcquireLock(connector->m_Queue);
        if (connector->m_State == NdEpQueued) {
            connector->m_State = NdEpIdle;
        }
        WdfObjectReleaseLock(connector->m_Queue);
        connector->Release();
    } else {
        WdfObjectAcquireLock(m_Queue);
        switch (m_State) {
        case NdEpAddressBound:
            //
            // We could have raced with ProcessCmRej, where the IRP was removed from the
            // queue due to cancellation, ProcessCmRej flips the state to NdEpAddressBound
            // but does not complete the IRP (since it was removed from the queue).
            //
            break;

        case NdEpResolving:
            //
            // Because address resolution is asnychronous, going back to NdEpAddressBound
            // would require proper synchronization with the resolution callback.  For now
            // just fall through.
            //
            //m_State = NdEpAddressBound;
            //break;

        case NdEpReqSent:
        case NdEpRepSent:
        case NdEpActiveDisconnect:
            m_State = NdEpDisconnected;
            break;

        case NdEpConnected:
            //
            // No state change, this is a NotifyDisconnect.
            //
            break;

        default:
            NT_ASSERT(
                m_State != NdEpIdle &&
                m_State != NdEpListening &&
                m_State != NdEpQueued &&
                m_State != NdEpReqReceived &&
                m_State != NdEpPassiveDisconnect &&
                m_State != NdEpDisconnected
                );
            break;
        }
        WdfObjectReleaseLock(m_Queue);
    }

    WdfRequestCompleteWithInformation(Request, Status, 0);
}


void ND_ENDPOINT::EvtIoCanceled(WDFQUEUE Queue, WDFREQUEST Request)
{
    ND_ENDPOINT* ep = NdEpGetContext(Queue);

    ep->FlushRequest(Request, STATUS_CANCELLED);
}
