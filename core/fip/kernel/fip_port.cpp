/*
 * 
 * Copyright (c) 2011-2012 Mellanox Technologies. All rights reserved.
 * 
 * This software is licensed under the OpenFabrics BSD license below:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *  conditions are met:
 *
 *       - Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *       - Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

/*


Module Name:
    fip_thread.cpp

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/
#include "precomp.h"

#include <initguid.h>
#include "public.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif

#include "fip_port.tmh"
#endif

UCHAR fip_discover_mgid[GID_LEN] = {
    0xFF, 0x12, 0xE0, 0x1B,
    0x00, 0x06, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};
UCHAR fip_solicit_mgid[GID_LEN] = {
    0xFF, 0x12, 0xE0, 0x1B,
    0x00, 0x07, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};


static 
NTSTATUS
__get_bus_ifc(
    DEVICE_OBJECT* const pDevObj,
    const GUID* const pGuid,
    USHORT size,
    USHORT Version,
    INTERFACE *pBusIfc 
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    IO_STATUS_BLOCK ioStatus;
    IO_STACK_LOCATION *pIoStack;
    KEVENT event;

    ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    KeInitializeEvent( &event, NotificationEvent, FALSE );

    // Build the IRP for the HCA. 
    IRP* pIrp = IoBuildSynchronousFsdRequest( IRP_MJ_PNP, pDevObj, NULL, 0, NULL, &event, &ioStatus );
    if( !pIrp ) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "IoBuildSynchronousFsdRequest failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Copy the request query parameters. 
    pIoStack = IoGetNextIrpStackLocation( pIrp );
    pIoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    pIoStack->Parameters.QueryInterface.Size = size;
    pIoStack->Parameters.QueryInterface.Version = Version;
    pIoStack->Parameters.QueryInterface.InterfaceType = pGuid;
    pIoStack->Parameters.QueryInterface.Interface = (INTERFACE*)pBusIfc;
    pIoStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    // Send the IRP. 
    status = IoCallDriver( pDevObj, pIrp );
    if( status == STATUS_PENDING ) {
        KeWaitForSingleObject( &event, Executive, KernelMode, FALSE, NULL );
        status = ioStatus.Status;
    }
    
    if( NT_SUCCESS( status ) ) {
        if( Version != pBusIfc->Version ) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "interface with guid 0x%I64x version mismatch: requested version=%d, returned version=%d.\n", 
                      *(uint64_t*)(PVOID)pGuid, Version, pBusIfc->Version);
            pBusIfc->InterfaceDereference(pBusIfc->Context);
            status = STATUS_NOT_SUPPORTED;
        }
    }

    return status;
}


VOID
DerefBusInterface(
    INTERFACE *pBusIfc 
)
{
    FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_PORT, "InterfaceDereference 0x%p\n", pBusIfc);
    pBusIfc->InterfaceDereference(pBusIfc->Context);
}



FipPort::FipPort()
{
    m_State = PORT_STATE_INIT;
    m_Shutdown = false;

    m_McastSolicit = NULL;
    m_McastDiscover = NULL;
    m_McastSolicitConnected = false;
    m_McastDiscoverConnected = false;
    m_LastMcastTime = 0;
    m_LastSendTime = 0;
    m_PortEvent = NULL;
    m_LocalPkeyIndex = 0;
    m_LocalLid = 0;
    m_BusInterfaceUpdated = false;
}



NTSTATUS

FipPort::Init(
    PDEVICE_OBJECT pDeviceObj,
    KEVENT *PortEvent
    )
{
    NTSTATUS Status = STATUS_SUCCESS;


    if(PortEvent != NULL) {
        ASSERT(m_PortEvent == NULL);
        m_PortEvent = PortEvent;
    }

    m_PkeyIndex = 0; //????

    m_pDeviceObj = pDeviceObj;
    if (m_BusInterfaceUpdated == false) {
        Status = __get_bus_ifc(
                        m_pDeviceObj,
                        &MLX4_BUS_IB_INTERFACE_GUID,
                        sizeof(MLX4_BUS_IB_INTERFACE), 
                        MLX4_BUS_IB_INTERFACE_VERSION, 
                        (INTERFACE *)&m_BusIbInterface
                        );
        if(!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "Getting MLX4 BUS interface failed: Status=0x%x\n", Status);
            goto Cleanup;
        }
        m_BusInterfaceUpdated = true;
    }

    Status = GetCaGuidAndPortNumFromPortGuid(
        g_pFipGlobals->AlHandle,
        m_PortGuid,
        m_PkeyIndex,
        NULL,
        NULL,
        &m_Pkey,
        &m_LocalLid,    
        &m_EnumMTU
        );

    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "GetCaGuidAndPortNumFromPortGuid failed Status = 0x%x\n", Status);
        goto Cleanup;
    }

    m_Pkey |= 0x8000;
    if(m_LocalLid == 0) {
        // This means that the network is not up yet...
        goto Cleanup;
    }

    Status = FipGws.Init(INITIAL_ARRAY_SIZE, true);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "FipGws.Initt failed Status = 0x%x\n", Status);
        goto Cleanup;
    }

    Status = m_SenderReciever.Init(
        g_pFipGlobals->AlHandle, 
        m_PortGuid, 
        g_pFipGlobals->SendRingSize, 
        g_pFipGlobals->RecvRingSize, 
        cl_hton32(VNIC_FIP_QKEY),
        m_PkeyIndex,
        m_PortEvent,
        m_PortEvent);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "m_SenderReciever.Init failed Status = 0x%x\n", Status);
        goto SenderReciever;
    }

    m_State = PORT_STATE_JOINING;

    Status = JoinMcasts();
    if (!NT_SUCCESS(Status)) 
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "JoinMcasts failed Status = 0x%x\n", Status);
        goto Cleanup;
    }

    for (int i = 0; i < MAXDEBUGGWS; i++) {
        FipGateWays[i] = NULL;
    }

    ASSERT(NT_SUCCESS(Status));
    return Status;

SenderReciever:
    FipGws.Shutdown();

Cleanup:
    return Status;
}



VOID
FipPort::DestroyGateway( int GwIdx )
{
    FipGws[GwIdx]->Shutdown();
    delete FipGws[GwIdx];
    FipGws[GwIdx] = NULL;

    if (GwIdx < MAXDEBUGGWS) {
        FipGateWays[GwIdx] = NULL;
    }
}


VOID 
FipPort::Shutdown()
{
    ASSERT(m_Shutdown == false);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "FipPort::Shutdown called\n");

    m_Shutdown = true;

    if (m_McastSolicitConnected) {
        m_SenderReciever.DestroyMcastAv(m_McastSolicitAv);
    }
    DestroyMcast(
        &m_McastSolicitConnected,
        &m_McastSolicitData,
        &m_McastSolicit,
        g_pFipWorkerThread
        );

    DetachMcast(m_McastDiscoverConnected, &m_SenderReciever, &m_McastDiscoverAattach);
    DestroyMcast(
        &m_McastDiscoverConnected,
        &m_McastDiscoverData,
        &m_McastDiscover,
        g_pFipWorkerThread
        );

    for (int i=0; i < FipGws.GetMaxSize(); i++) {
        if(FipGws[i] != NULL) {
            DestroyGateway(i);
        }
    }
    FipGws.Shutdown();

    if (m_BusInterfaceUpdated == true) {
        DerefBusInterface((INTERFACE *)&m_BusIbInterface);
        m_BusInterfaceUpdated = false;
    }

    if (m_State != PORT_STATE_INIT) {
        m_SenderReciever.ShutDown();
    }

}



// This function is being called by the normal thread
VOID
FipPort::JoinMcastCallBack(McastMessageData *pMcastMessageData) 
{
    bool WasFound = false;

    ASSERT(m_State == PORT_STATE_JOINING);
    ASSERT(pMcastMessageData->ClassType == PORT_CLASS);

    WasFound |= GenericJoinMcastCallBack(
        &m_McastSolicitConnected,
        pMcastMessageData,
        (VhubMgid_t*)fip_solicit_mgid,
        &m_McastSolicit,
        NULL,
        &m_McastSolicitAv,
        &m_SenderReciever
        );

    WasFound |= GenericJoinMcastCallBack(
        &m_McastDiscoverConnected,
        pMcastMessageData,
        (VhubMgid_t*)fip_discover_mgid,
        &m_McastDiscover,
        &m_McastDiscoverAattach,
        NULL,
        &m_SenderReciever
        );

    ASSERT(WasFound == true);
    if(m_McastDiscoverConnected && m_McastSolicitConnected) {
        m_State = PORT_STATE_CONNECTED;
        SendSolicited();
    } 

}

VOID
FipPort::FillPortMcastReq(
    ib_mcast_req_t* pMcastReq,
    void* mgid,
    bool fCreate,
    void* context
    )
{
// BUGBUG: Fill the rest of the creation parameters

    char port_gid[GID_LEN] = {0};

    memset(pMcastReq, 0, sizeof(*pMcastReq));

    
    memcpy(&pMcastReq->member_rec.mgid, mgid, sizeof(pMcastReq->member_rec.mgid));
    memcpy(&pMcastReq->member_rec.port_gid, port_gid, sizeof(pMcastReq->member_rec.port_gid));

    pMcastReq->create = fCreate;

    pMcastReq->member_rec.qkey = cl_hton32(VNIC_FIP_QKEY);
    pMcastReq->member_rec.mtu = (IB_PATH_SELECTOR_EXACTLY << 6) | m_EnumMTU;
    pMcastReq->member_rec.pkey = m_Pkey;
    pMcastReq->member_rec.sl_flow_hop = ib_member_set_sl_flow_hop(0, 0, 0);
    pMcastReq->member_rec.scope_state = ib_member_set_scope_state(2, IB_MC_REC_STATE_FULL_MEMBER);
    
    pMcastReq->mcast_context = context;
    pMcastReq->pfn_mcast_cb = mcast_cb;
    pMcastReq->timeout_ms = 1000;
    pMcastReq->retry_cnt  = 10;
    pMcastReq->port_guid  = m_PortGuid;
    pMcastReq->pkey_index = m_PkeyIndex;
}


NTSTATUS 
FipPort::JoinMcasts()
{

    if (m_Shutdown) {
        ASSERT(FALSE);
        return STATUS_SHUTDOWN_IN_PROGRESS;
    }

    ASSERT(m_State == PORT_STATE_JOINING);

    if (GetTickCountInMsec() - m_LastMcastTime < 990) {
        return STATUS_DEVICE_NOT_READY;
    }

    m_LastMcastTime = GetTickCountInMsec() + (GetTickCountInMsec() % RANDOM_TIME_PERIOD);;

    ib_api_status_t     ib_status = IB_SUCCESS;
    ib_mcast_req_t      McastReq;


    // Join solicit mcast
    if (!m_McastSolicitConnected && m_McastSolicit == NULL) {
        m_McastSolicitData.Data.Mcast.pFipClass = this;
        m_McastSolicitData.Data.Mcast.ClassType = PORT_CLASS;
        FillPortMcastReq(&McastReq, fip_solicit_mgid, false,&m_McastSolicitData);

        ib_status = mcast_mgr_join_mcast(&McastReq, &m_McastSolicit);

        if (ib_status != IB_SUCCESS) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "mcast_mgr_join_mcast_no_qp failed ib_status = %d\n", ib_status);
            ASSERT(m_McastSolicit == NULL);
            // We can continue and try to connect the second mcast
        }
    }

    // Join discover mcast .......
    if (!m_McastDiscoverConnected && m_McastDiscover == NULL) {
        m_McastDiscoverData.Data.Mcast.pFipClass = this;
        m_McastDiscoverData.Data.Mcast.ClassType = PORT_CLASS;
        FillPortMcastReq(&McastReq, fip_discover_mgid, false,&m_McastDiscoverData);

        ib_status = mcast_mgr_join_mcast(&McastReq, &m_McastDiscover);

        if (ib_status != IB_SUCCESS) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "mcast_mgr_join_mcast_no_qp failed ib_status = %d\n", ib_status);
            ASSERT(m_McastDiscover == NULL);
            // We can continue and try to connect the second mcast
        }
    }


    return STATUS_SUCCESS;
}


VOID
FipPort::HandleGWTimeOut( VOID )
{
    for (int i=0; i < FipGws.GetMaxSize(); i++) {
        if ((FipGws[i] != NULL) &&
            (FipGws[i]->IsGwTimeOut())) {
            DestroyGateway(i);
        }
    }
}


VOID 
FipPort::HandleRecvMessage()
{
    NTSTATUS Status;

    RcvMsg *pRcvMsg = NULL;
    
    int FreeLocation = NO_FREE_LOCATION;

    m_SenderReciever.Recv(&pRcvMsg);
    if(pRcvMsg == NULL) {
        return;
    }
    FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_PORT, "New Message Recived grh_valid = %d\n", pRcvMsg->m_RecvOpt & IB_RECV_OPT_GRH_VALID);
    UniqeIdentifyParams_t UniqeParams;
    Status = GetParamsFromAdvMsg(
        pRcvMsg->m_AlignedData, 
        pRcvMsg->m_Len, 
        &UniqeParams);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "GetParamsFromAdvMsg failed Status = 0x%x\n", Status);
        goto Cleanup;
    }

    // We have recieved a new messge, let's find the gw:
    for (int i=0; i < FipGws.GetMaxSize(); i++) {
        if ((FipGws[i]!= NULL) && 
            (FipGws[i]->IsMatch(&UniqeParams))) {
            FipGws[i]->AddMessage(pRcvMsg, &UniqeParams);
            pRcvMsg = NULL; // GW/Vnic will handle this msg
            goto Cleanup;
        }
    }

    if (UniqeParams.MsgType == LOGIN_ACK_MSG) {
        ASSERT(FALSE);
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "Couldn't find match this packet to any Gw/Vnic, silently drop\n");
        goto Cleanup;
    }

    // We haven't found the GW so this is a new GW
    FreeLocation = FipGws.GetFreeLocation();
    if (FreeLocation == NO_FREE_LOCATION) {
        // The array we have is full and allocation failed
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "Failed to GetFreeLocation");
        ASSERT(FALSE);
        goto Cleanup;
    }
    ASSERT(FipGws[FreeLocation] == NULL);

    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "Creating a new Gateway (%s)!!!\n", (char*)UniqeParams.Gw.PortName);

    FipGws[FreeLocation] = NEW Gateway;
    if(FipGws[FreeLocation] == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "NEW FipGw failed, will be tryied again soon\n");
        goto Cleanup;
    }
    FipGws[FreeLocation]->Init(
        &m_SenderReciever,
        m_PortGuid,
        m_EnumMTU,
        VNIC_FIP_QKEY,
        m_LocalPkeyIndex,
        m_LocalLid,
        UniqeParams.Guid,
        UniqeParams.PortId,
        UniqeParams.Gw.PortName,
        UniqeParams.Gw.SystemGuid,
        UniqeParams.Gw.SystemName,
        &m_BusIbInterface,
        m_PortEvent,
        m_PortEvent
        );

    if(FreeLocation < MAXDEBUGGWS) {
        FipGateWays[FreeLocation] = FipGws[FreeLocation];
    }

    FipGws[FreeLocation]->AddMessage(pRcvMsg, &UniqeParams);
    pRcvMsg = NULL; // GW will handle this msg

Cleanup:
    if (pRcvMsg != NULL) {
        // If we got from any error flow and still didn't free the buffer it's time to do this now
        m_SenderReciever.ReturnBuffer(pRcvMsg);
    }
}


/*
 * Parse the incoming msg to identify whether it's a Vnic-Login-Ack or Adv Msg
 */
MsgType_t
FipPort::IdentifyMsg(
    PVOID pAdvMsg,
    ULONG Length
    )
{
    MsgType_t MsgType = UNKNOWN_MSG;

    // Check For Avd Msg
    if (Length == (sizeof(FipAvd_t) + GRH_LEN)) {
        FipAvd_t *Msg = (FipAvd_t *)((UCHAR *)pAdvMsg + GRH_LEN);

        if (Msg->fip.type.type != FIP_FIP_HDR_TYPE ||
            Msg->fip.type.length != FIP_FIP_HDR_LENGTH||
            Msg->base.type.type != FIP_BASIC_TYPE ||
            Msg->base.type.length != FIP_BASIC_LENGTH ||
            Msg->FipGwInfo.type.type != FIP_ADVERTISE_TYPE_1 ||
            Msg->FipGwInfo.type.length != FIP_ADVERTISE_LENGTH_1 ||
            Msg->gw_info.type.type != FIP_ADVERTISE_GW_TYPE ||
            Msg->gw_info.type.length != FIP_ADVERTISE_GW_LENGTH ||
            Msg->ka_info.type.type != FIP_ADVERTISE_KA_TYPE ||
            Msg->ka_info.type.length != FIP_ADVERTISE_KA_LENGTH) {
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, 
                     "fip_advertise_parse Msg format err."
                     "(TLV,LNTH,rcvd TLV, rcvd LNTH): "
                     "(%d,%d,%d,%d), (%d,%d,%d,%d),"
                     "(%d,%d,%d,%d), (%d,%d,%d,%d)\n",
                     FIP_BASIC_TYPE, FIP_BASIC_LENGTH,
                     Msg->base.type.type, Msg->base.type.length,
                     FIP_ADVERTISE_TYPE_1, FIP_ADVERTISE_LENGTH_1,
                     Msg->FipGwInfo.type.type, Msg->FipGwInfo.type.length,
                     FIP_ADVERTISE_GW_TYPE, FIP_ADVERTISE_GW_LENGTH,
                     Msg->gw_info.type.type, Msg->gw_info.type.length,
                     FIP_ADVERTISE_KA_TYPE, FIP_ADVERTISE_KA_LENGTH,
                     Msg->ka_info.type.type, Msg->ka_info.type.length);
        } else {
            MsgType = ADV_MSG;
        }
        goto identify_msg_end;
    }

    // Check for Login-Ack Msg
    if (Length == (sizeof(FipLoginAck_t) + GRH_LEN)) {
        FipLoginAck_t *pFipLogin = (FipLoginAck_t*)((UCHAR *)pAdvMsg + GRH_LEN);

        if (pFipLogin->login.fip.type.type != FIP_FIP_HDR_TYPE ||
            pFipLogin->login.fip.type.length != FIP_FIP_HDR_LENGTH||
            pFipLogin->login.base.type.type != FIP_BASIC_TYPE ||
            pFipLogin->login.base.type.length != FIP_BASIC_LENGTH||
            pFipLogin->login.VnicLogin.type.type != FIP_LOGIN_TYPE ||
            pFipLogin->login.VnicLogin.type.length != FIP_LOGIN_LENGTH||
            pFipLogin->partition.type.type != FIP_LOGIN_PARTITION_TYPE ||
            pFipLogin->partition.type.length != FIP_LOGIN_PARTITION_LENGTH) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, 
                     "pFipLogin Msg format err."
                     "(TLV,LNTH,rcvd TLV, rcvd LNTH): "
                    "(%d,%d,%d,%d), (%d,%d,%d,%d), (%d,%d,%d,%d)\n",
                     FIP_FIP_HDR_TYPE, FIP_FIP_HDR_LENGTH,
                     pFipLogin->login.fip.type.type, pFipLogin->login.fip.type.length,
                    FIP_LOGIN_TYPE, FIP_LOGIN_LENGTH,
                    pFipLogin->login.base.type.type, pFipLogin->login.base.type.length,
                    FIP_LOGIN_PARTITION_TYPE, FIP_LOGIN_PARTITION_LENGTH,
                    pFipLogin->partition.type.type, pFipLogin->partition.type.length);
        } else {
            MsgType = LOGIN_ACK_MSG;
        }
        goto identify_msg_end;
    }

identify_msg_end:
    ASSERT(MsgType != UNKNOWN_MSG);
    return MsgType;
}


/*
* Parse Adv Msg to decide whether this is a Vnic-Login-Ack or a GW Avd Msg
* Then we return the relevant params to identify who will get this msg
*/
NTSTATUS
FipPort::GetParamsFromAdvMsg(
    PVOID pAdvMsg,
    ULONG Length,
    UniqeIdentifyParams_t *Params
    )
{
    ASSERT(m_Shutdown == false);
    NTSTATUS Status = STATUS_SUCCESS;
    FipAvd_t *pFipAdv = (FipAvd_t *)((UCHAR *)pAdvMsg + GRH_LEN);
    FipLoginAck_t *pFipLogin = (FipLoginAck_t*)((UCHAR *)pAdvMsg + GRH_LEN);

    memset(Params, 0, sizeof(*Params)); // For debug only
    Params->MsgType = IdentifyMsg(pAdvMsg, Length);
    switch (Params->MsgType) {
    case ADV_MSG:
        Params->PortId = (u16)(cl_ntoh16(pFipAdv->base.sl_port_id) & FIP_ADVERTISE_GW_PORT_ID_MASK);
        memcpy(Params->Guid, pFipAdv->base.guid, sizeof(Params->Guid));
        memcpy(Params->Gw.SystemGuid, pFipAdv->gw_info.system_guid, sizeof(Params->Gw.SystemGuid));
        MemcpyStr((PCHAR)Params->Gw.PortName, (PCHAR)pFipAdv->gw_info.gw_port_name, sizeof(Params->Gw.PortName));
        MemcpyStr((PCHAR)Params->Gw.SystemName, (PCHAR)pFipAdv->gw_info.system_name, sizeof(Params->Gw.SystemName));
        break;

    case LOGIN_ACK_MSG:
        Params->PortId = (u16)(cl_ntoh16(pFipLogin->login.base.sl_port_id) & FIP_ADVERTISE_GW_PORT_ID_MASK);
        memcpy(Params->Guid, pFipLogin->login.base.guid, sizeof(Params->Guid));
        memcpy(Params->Vnic.Mac, pFipLogin->login.VnicLogin.mac, sizeof(Params->Vnic.Mac));
        Params->Vnic.VnicId = cl_ntoh16(pFipLogin->login.VnicLogin.vnic_id);   // Return the actual Vnic name
        memcpy(Params->Vnic.EthGidPrefix, pFipLogin->login.VnicLogin.eth_gid_prefix, sizeof(Params->Vnic.EthGidPrefix));
        MemcpyStr((PCHAR)Params->Vnic.Name, (PCHAR)pFipLogin->login.VnicLogin.vnic_name, sizeof(Params->Vnic.Name));
        break;

    case UNKNOWN_MSG:
        Status = STATUS_NDIS_INVALID_PACKET;
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "Unknown Incoming Msg!!!\n");
        break;
    }

    return Status;
}




VOID 
FipPort::PortSM()
{
    NTSTATUS Status;
    ASSERT(m_Shutdown == false);

    switch(m_State) {
        case PORT_STATE_INIT:
            Init(m_pDeviceObj, NULL);
            break;
        
        case PORT_STATE_JOINING :
            JoinMcasts();
            break;
            
        case PORT_STATE_CONNECTED :
            HandleGWTimeOut();
            HandleRecvMessage();
            SendSolicited();
            break;

        default:
            ASSERT(FALSE);
        }

    for (int i=0; i < FipGws.GetMaxSize(); i++) {
        if (FipGws[i] != NULL) {
            Status = FipGws[i]->RunFSM();
            if (!NT_SUCCESS(Status)) {
                // TODO: How to behave on ERROR ???
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_PORT, "Run Gateway (%s) Failed failed Status = 0x%x\n", FipGws[i]->GetGwUniqeStr(), Status);
            } 
        }
    }
}


int 
FipPort::NextCallTime()
{

    ASSERT(m_Shutdown == false);
    s64 Time;

    switch(m_State) {
        case PORT_STATE_INIT:
            return 1000;
        
        case PORT_STATE_JOINING :
            ASSERT((m_McastSolicitConnected == false ) || (m_McastDiscoverConnected == false));
            Time = m_LastMcastTime + 1000 - GetTickCountInMsec();
            Time = max(0, Time);
            return (int)Time;
            
        case PORT_STATE_CONNECTED :
            
            Time = m_LastSendTime + 8000 - GetTickCountInMsec();
            Time = max(0, Time);
            return (int)Time;
            

        default:
            ASSERT(FALSE);
            break;
            

    }
    // We should never reach here, this is only for the compiler
    ASSERT(FALSE);
    return MAX_SLEEP;

}

VOID 
FipPort::PreInit()
{
    
    m_PortEvent = NULL;
    m_Shutdown = false;
}




VOID PrepareSolicitMsg(
    FipSoloicit_t *Msg,
    u32 Qpn,
    u16 PortId,
    u16 Lid,
    u64 PortGuid
    )
{
    ASSERT(Msg != NULL);
    memset(Msg, 0, sizeof(FipSoloicit_t));

    // Fip Header
    Msg->fip.opcode         = cl_hton16(EOIB_FIP_OPCODE);
    Msg->fip.subcode        = FIP_HOST_SOL_SUB_OPCODE;
    Msg->fip.list_length    = cl_hton16(FIP_CTRL_HDR_MSG_LEGTH); //cl_hton16((sizeof(FipSoloicit_t) >> 2) - 2);
    Msg->fip.type.type      = FIP_FIP_HDR_TYPE;
    Msg->fip.type.length    = FIP_FIP_HDR_LENGTH;
    memcpy(Msg->fip.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(Msg->fip.vendor_id));

    // Base Solicit Msg
    Msg->base.type.type     = FIP_BASIC_TYPE;
    Msg->base.type.length   = FIP_BASIC_LENGTH;
    memcpy(Msg->base.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(Msg->base.vendor_id));

    Msg->base.qpn           = Qpn;  // TODO: 'switch Qpn to be Host to Network
    Msg->base.sl_port_id    = cl_hton16((u16)(PortId & FIP_PORT_ID_MASK)); // SL must be 0 (BX-PRM 2.4.2.3 P.81) ); */
    Msg->base.lid           = cl_hton16(Lid);

    u64 h_port_guid         = cl_hton64(PortGuid);
    memcpy(Msg->base.guid, &h_port_guid, sizeof(Msg->base.guid));
}



NTSTATUS
FipPort::SendSolicited()
{
    if (m_Shutdown) {
        ASSERT(FALSE);
        return STATUS_SHUTDOWN_IN_PROGRESS;
    }

    if (GetTickCountInMsec() - m_LastSendTime < 8000) {
        return STATUS_DEVICE_NOT_READY;
    }

    m_LastSendTime = GetTickCountInMsec() + (GetTickCountInMsec() % RANDOM_TIME_PERIOD);;

    FipSoloicit_t Msg;

    PrepareSolicitMsg(&Msg, m_SenderReciever.GetQpn(),  0, m_LocalLid, cl_ntoh64(m_PortGuid));

    m_SenderReciever.Send(m_McastSolicitAv ,0xffffff00, (char *)&Msg, sizeof (Msg));

    return STATUS_SUCCESS;

}


