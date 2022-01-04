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
    fip_vnic.cpp

Abstract:
    This module contains miniport vnic routines

Revision History:

Notes:

Author:
    Sharon Cohen

--*/
#include "precomp.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif

#include "fip_vnic.tmh"
#endif


VOID
mcast_cb(
    ib_mcast_rec_t *p_mcast_rect
    )
{
    // Call the class mcast

    VERIFY_DISPATCH_LEVEL(PASSIVE_LEVEL);

    FipThreadMessage *pFipThreadMessage = (FipThreadMessage *)p_mcast_rect->mcast_context;

    McastMessageData* pMcastMessageData = &pFipThreadMessage->Data.Mcast;

    pMcastMessageData->status = p_mcast_rect->status;
    pMcastMessageData->error_status = p_mcast_rect->error_status;
    pMcastMessageData->member_rec = *p_mcast_rect->p_member_rec;

    g_pFipWorkerThread->AddMulticastMessage(pFipThreadMessage);
}


VOID
FillPkeyArray(
    u64 UniqueId,
    ib_net64_t PortGuid,
    ib_net16_t pkey,
    u32 Location,
    pkey_array_t *pPkeyArray
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    
    pPkeyArray->pkey_num = 1;
    pPkeyArray->port_guid = PortGuid;
    pPkeyArray->pkey_array[0] = pkey;
    pPkeyArray->port_type[0] = PORT_TYPE_EOIB;
    
    pPkeyArray->UniqueId[0] = UniqueId;
    pPkeyArray->Location[0] = Location;

    Status = RtlStringCbPrintfA(pPkeyArray->name[0], sizeof(pPkeyArray->name[0]), "EoIB-%010I64x", UniqueId); // Can be EoIB-UniqeId
    ASSERT(Status == STATUS_SUCCESS);
}


NTSTATUS 
CreateEoibInterface(
    ib_net64_t PortGuid,
    ib_net16_t pkey,
    char *UniqeIFName,
    u64 UniqueId,
    u32 Location
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    pkey_array_t PkeyArray;

    FillPkeyArray(
        UniqueId,
        PortGuid,
        pkey,
        Location,
        &PkeyArray);

    Status = RtlStringCbCopyA(UniqeIFName, (sizeof(char) * (MAX_USER_NAME_SIZE + 1)), PkeyArray.name[0]); //Return to the Vnic - For debugging only
    ASSERT(Status == STATUS_SUCCESS);

    cl_status_t cl_Status = port_mgr_pkey_add(&PkeyArray);
    if (cl_Status != CL_SUCCESS) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "port_mgr_pkey_add failed Status = 0x%d\n", cl_Status);
        Status = STATUS_ADAPTER_HARDWARE_ERROR;
    }

    return Status;
}


VOID 
DestroyEoibInterface(
    ib_net64_t PortGuid,
    ib_net16_t pkey,
    u64 UniqueId,
    u32 Location
    )
{
    pkey_array_t PkeyArray;

    FillPkeyArray(
        UniqueId,
        PortGuid,
        pkey,
        Location,
        &PkeyArray);

    cl_status_t cl_Status = port_mgr_pkey_rem(&PkeyArray);
    if (cl_Status != CL_SUCCESS) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "port_mgr_pkey_rem failed Status = 0x%d\n", cl_Status);
    }
}



static
const char *SyndromToStr(
    LoginSyndrom_t Syndrome
    )
{
    switch (Syndrome)
    {
        case FIP_SYNDROM_SUCCESS:        return "SYNDROM_SUCCESS";
        case FIP_SYNDROM_HADMIN_REJECT:  return "SYNDROM_HADMIN_REJECT";
        case FIP_SYNDROM_GW_RESRC:       return "SYNDROM_GW_RESRC";
        case FIP_SYNDROM_NO_NADMIN:      return "SYNDROM_NO_NADMIN";
        case FIP_SYNDROM_UNRECOGNISED:   return "SYNDROM_UNRECOGNISED";
        case FIP_SYNDROM_UNSUPPORTED:    return "SYNDROM_UNSUPPORTED";
        case FIP_SYNDROM_DUPLICATE_ADDR: return "SYNDROM_DUPLICATE_ADDR";
        default:                         return "Unknown";
    }
}


static
const char *StateToStr(
    VnicState_t VnicState
    )
{
    switch (VnicState)
    {
        case HADMIN_IDLE:           return "HADMIN_IDLE";
        case LOGIN:                 return "LOGIN";
        case WAIT_FOR_ACK:          return "WAIT_FOR_ACK";
        case LOGIN_REJECT:          return "LOGIN_REJECT";
        case JOIN_VHUB_MCASTS:      return "JOIN_VHUB_MCASTS";
        case REQUEST_VHUB_TABLE:    return "REQUEST_VHUB_TABLE";
        case VHUB_TABLE_INIT:       return "VHUB_TABLE_INIT";
        case CONNECTED:             return "CONNECTED";
        case WAIT_TO_BE_KILLED:     return "WAIT_TO_BE_KILLED";
        default:                    return "Unknown";
    }
}


static
const char *VnicTypeToStr(
    VnicType_t VnicType
    )
{
    switch (VnicType)
    {
        case UNDEFINED_VNIC:        return "UNDEFINED";
        case HOST_VNIC:             return "Host";
        case NETWORK_VNIC:          return "Network";
        default:                    return "Unknown";
    }
}


NTSTATUS
Vnic::PreLogin( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    int status;

    m_VnicState = LOGIN;

    // Request One QP for Data
    status = m_pBusIbInterface->mlx4_interface.mlx4_qp_reserve_range(
        m_pBusIbInterface->pmlx4_dev,
        1,
        1,
        &m_LocalDataQpn);
    if (status != 0) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s mlx4_qp_reserve_range Failed\n", m_UniqeStr);
        Status = STATUS_ADAPTER_HARDWARE_ERROR;
    }

    return Status;
}


VOID
Vnic::UpdateUniqeStr( VOID )
{
    NTSTATUS Status;

    Status = RtlStringCbPrintfA(
        m_UniqeStr,
        UNIQE_GW_LEN,
        "GW %s, %s VnicId %d -",
        m_GwPortName,
        VnicTypeToStr(m_VnicType),
        m_VnicId);
    if (!NT_SUCCESS(Status)) {
        ASSERT(FALSE);
        Status = RtlStringCbCopyA(m_UniqeStr, sizeof(m_UniqeStr), "ERROR" );
        ASSERT(Status == STATUS_SUCCESS);
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s RtlStringCbPrintfA Failed\n", m_UniqeStr);
    }
}


NTSTATUS
Vnic::Init( 
    VnicType_t VnicType,
    u64 VnicKAPeriod,
    u16 LocalLid,
    u64 LocalPortGuid,
    u32 Qkey,
    u16 LocalPkeyIndex,
    u16 Lid,
    u32 Qpn,
    u16 PortId,
    u16 QpsNum, // Holds the n_rss_qps from the GW
    u8  LocalEnumMTU,
    bool ValidMac,
    u8 Mac[HW_ADDR_LEN],
    bool ValidVlan,
    u16 Vlan,
    u16 VnicId,   // A uniqe ID (along with GW-Guid)
    u8 GwGuid[GUID_LEN],
    u8 Name[VNIC_NAME_LEN + 1],
    u8 Sl,
    u8 GwPortName[VNIC_GW_PORT_NAME_LEN + 1],
    PMLX4_BUS_IB_INTERFACE pBusIbInterface,
    SendRecv *PortSendRecv,
    KEVENT * MsgArrivedEvent,
    KEVENT * CqErrorEvent
    )
{
    NTSTATUS Status;

    ASSERT(m_VnicState == HADMIN_IDLE);

    m_VnicType          = VnicType;
    m_LocalEnumMTU      = LocalEnumMTU;
    m_VnicKAPeriod      = VnicKAPeriod;
    m_LocalLid          = LocalLid;
    m_LocalPortGuid     = LocalPortGuid;
    m_Qkey              = Qkey;
    m_LocalPkeyIndex    = LocalPkeyIndex;
    m_Lid               = Lid;
    m_CtrlQpn           = Qpn;
    m_PortId            = PortId;
    m_QpsNum            = QpsNum;
    m_pBusIbInterface   = pBusIbInterface;
    m_PortSendRecv      = PortSendRecv; // This will used to return Port msg to the correct SendRecv class

    m_ValidMac          = ValidMac;
    if (ValidMac) {
        memcpy(m_Mac, Mac, sizeof(m_Mac));
    }

    m_ValidVlan         = ValidVlan;
    if (ValidVlan) {
        m_Vlan          = Vlan;
    }

    m_VnicId = VnicId;
    memcpy(m_GwGuid, GwGuid, sizeof(m_GwGuid));
    memcpy(m_GwPortName, GwPortName, sizeof(GwPortName));
    MemcpyStr((PCHAR)m_Name, (PCHAR)Name, sizeof(m_Name));
    m_Sl = Sl;

    UpdateUniqeStr();

     Status = m_SendRecv.Init(
        g_pFipGlobals->AlHandle, 
        m_LocalPortGuid,
        g_pFipGlobals->SendRingSize,
        g_pFipGlobals->RecvRingSize,
        cl_hton32(m_Qkey),
        m_LocalPkeyIndex,
        MsgArrivedEvent,
        CqErrorEvent);
     if (!NT_SUCCESS(Status)) {
         FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Init SendRecv Failed\n", m_UniqeStr);
         goto init_snd_rcv_err;
     }
     m_LocalCtrlQpn = m_SendRecv.GetQpn();

     Status = PreLogin();
     if (!NT_SUCCESS(Status)) {
         FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s PreLogin Failed\n", m_UniqeStr);
         goto init_err;
     }

     // Must be after UpdateUniqeStr & Data Qpn
     Status = AddVnicInterface();
     if (!NT_SUCCESS(Status)) {
         FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s AddVnicInterface Failed\n", m_UniqeStr);
         goto interface_err;
     }

     return Status;

interface_err:
init_err:
    m_SendRecv.ShutDown();
init_snd_rcv_err:
    return Status;
}


NTSTATUS
Vnic::AddVnicInterface( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(m_pDataInterfaceVnic == NULL);
    Status = g_pFipInterface->AddVnic(
        &m_UniqueId,
        &m_DataLocation,
        m_UniqeStr,
        m_LocalDataQpn,
        m_pBusIbInterface,
        &m_pDataInterfaceVnic);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "g_pFipInterface->AddVnic Failed Status=0x%x\n", Status);
    }

    return Status;
}


VOID
Vnic::RemoveVnicInterface( VOID )
{
    if (m_pDataInterfaceVnic != NULL) {
        g_pFipInterface->RemoveVnic(m_pDataInterfaceVnic, m_DataLocation);
        m_pDataInterfaceVnic = NULL;
    }
}


VOID
Vnic::LeaveAndDestroyMcast( VOID )
{
    DetachMcast(m_McastTableConnected, &m_SendRecv, &m_McastTableAattach);
    DestroyMcast(
        &m_McastTableConnected,
        &m_McastTableData,
        &m_McastTable,
        g_pFipWorkerThread
        );

    DetachMcast(m_McastUpdateConnected, &m_SendRecv, &m_McastUpdateAattach);
    DestroyMcast(
        &m_McastUpdateConnected,
        &m_McastUpdateData,
        &m_McastUpdate,
        g_pFipWorkerThread
        );
}


VOID
Vnic::Shutdown(bool CallImplicitLogout)
{
    ASSERT(m_Shutdown == false);

    if (CallImplicitLogout) {
        Logout();
    }

    m_Shutdown = true;

    LeaveAndDestroyMcast();

    if (m_DidWeInitVhubTable == true) {
        m_VhubTable.Shutdown();
    }

    RemoveAllMessage();

    if (m_SendRecv.IsAvValid()) {  // Create AV only one time
        m_SendRecv.DestroyAv();
    }

    m_SendRecv.ShutDown();

    RemoveVnicInterface();

    if (m_WasEoIBInterfaceCreated == true) {
        DestroyEoibInterface(m_LocalPortGuid, m_Pkey, m_UniqueId, m_DataLocation);
        m_WasEoIBInterfaceCreated = false;
    }

}


u64
Vnic::CallNextTime( VOID )
{
    s64 NextTime;

    ASSERT(m_Shutdown == false);

    // Check last time we sent some KA Msg.
    NextTime   = m_LastKAPeriod + (s64)m_VnicKAPeriod - GetTickCountInMsec();
    NextTime = max(0, NextTime); // Time must be positive
    return (int)NextTime;
}


/* 
 * returns true if the GW last Adv msg has Time out
 */
bool
Vnic::IsVnicTimeOut( VOID )
{
    s64 NextTime;

    ASSERT(m_Shutdown == false);

    NextTime   = m_LastKAPeriod + Mult2_5((s64)m_VnicKAPeriod) - GetTickCountInMsec();
    NextTime = max(0, NextTime );
    return (NextTime == 0);
}


bool
Vnic::IsMatch(
    UniqeIdentifyParams_t *UniqeParams
    )
{
    ASSERT(m_Shutdown == false);
    ASSERT(UniqeParams->MsgType == LOGIN_ACK_MSG);
    bool fIsMatch = true;

    fIsMatch &= (UniqeParams->PortId == m_PortId);
    fIsMatch &= (m_VnicId == UniqeParams->Vnic.VnicId);
    fIsMatch &= (memcmp(m_GwGuid, UniqeParams->Guid, GUID_LEN) == 0);
    // These parameters we will know only after receiving the 1st msg
    if (memcmp(m_Mac, EmptyMac, HW_ADDR_LEN) != 0) {    // Check whether we already initialize the Vnic's params
        fIsMatch &= (memcmp(m_Name, UniqeParams->Vnic.Name, VNIC_NAME_LEN) == 0);
        fIsMatch &= (memcmp(m_EthGidPrefix, UniqeParams->Vnic.EthGidPrefix, GID_PREFIX_LEN) == 0);
        fIsMatch &= (memcmp(m_Mac, UniqeParams->Vnic.Mac, HW_ADDR_LEN) == 0);
    }

    return fIsMatch;
}



/*
 * construct an mgid address based on vnic login information and the type
 * variable (data mcast / vhub update / vhub table). The resulting mgid
 * is returned in *mgid.
 */
VOID
Vnic::CreateVhubMgid(
    VhubMgidType_t VhubMgidType,
    VhubMgid_t *VhubMgid
    )
{
    memset(VhubMgid, 0, sizeof(*VhubMgid)); // For debugging only

    memcpy(VhubMgid->mgid.mgid_prefix, m_EthGidPrefix,
           sizeof(VhubMgid->mgid.mgid_prefix));
    VhubMgid->mgid.type = (u8)VhubMgidType;
    memcpy(VhubMgid->mgid.dmac, EmptyMac, sizeof(VhubMgid->mgid.dmac)); // Must be zero at non-data mcast
    VhubMgid->mgid.rss_hash = 0; // Must be zero at non-data mcast
    u32 VHubId = cl_hton32(m_VHubId);
    memcpy(VhubMgid->mgid.vhub_id, ((u8 *) &VHubId) + 1,    // Convert 32 bits to 24 bits
    sizeof(VhubMgid->mgid.vhub_id));
};


// This function is being called by the normal thread
VOID
Vnic::JoinMcastCallBack(
    McastMessageData *pMcastMessageData
    )
{
    bool WasFound = false;

    ASSERT(m_VnicState == JOIN_VHUB_MCASTS);
    ASSERT(pMcastMessageData->ClassType == VNIC_CLASS);

    WasFound |= GenericJoinMcastCallBack(
        &m_McastTableConnected,
        pMcastMessageData,
        &m_TableVhubMgid,
        &m_McastTable,
        &m_McastTableAattach,
        NULL,
        &m_SendRecv
        );

    WasFound |= GenericJoinMcastCallBack(
        &m_McastUpdateConnected,
        pMcastMessageData,
        &m_UpdateVhubMgid,
        &m_McastUpdate,
        &m_McastUpdateAattach,
        NULL,
        &m_SendRecv
        );

    ASSERT(WasFound == true);
    if(m_McastUpdateConnected && m_McastTableConnected) {
        m_VnicState = REQUEST_VHUB_TABLE;
    }

}


VOID
Vnic::FillPortMcastReq(
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

    pMcastReq->member_rec.qkey = cl_hton32(m_Qkey);
    pMcastReq->member_rec.mtu = (u8)((IB_PATH_SELECTOR_EXACTLY << 6) | m_LocalEnumMTU);
    pMcastReq->member_rec.pkey = m_Pkey;
    pMcastReq->member_rec.sl_flow_hop = ib_member_set_sl_flow_hop(0, 0, 0);
    pMcastReq->member_rec.scope_state = ib_member_set_scope_state(2, IB_MC_REC_STATE_FULL_MEMBER);
    
    pMcastReq->mcast_context = context;
    pMcastReq->pfn_mcast_cb = mcast_cb;
    pMcastReq->timeout_ms = 1000;
    pMcastReq->retry_cnt  = 10;
    pMcastReq->port_guid  = m_LocalPortGuid;
    pMcastReq->pkey_index = m_PkeyIndex;    // TODO: Find real Pkey index
}


/*
 * This function will create Update & Table Vhub Mcast
 * and will join this mcast group
 * in order to recv all Vhub msg related to this vnic
 */
NTSTATUS 
Vnic::JoinMcastForVhubMsg( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ib_api_status_t     ib_status = IB_SUCCESS;
    ib_mcast_req_t      McastReq;
    s64 NextTime = (s64)(m_LastMcastTime + (s64)(m_VnicKAPeriod / 10) - GetTickCountInMsec());  // Can send Join request 10 times before going back to Login

    if (NextTime > 0) { // Prevent from 'bombing' the network
        return Status;
    }
    m_LastMcastTime = GetTickCountInMsec() + (GetTickCountInMsec() % RANDOM_TIME_PERIOD);

    // Join Table mcast
    if (!m_McastTableConnected && m_McastTable == NULL) {
        CreateVhubMgid(VHUB_MGID_TABLE, &m_TableVhubMgid);
        m_McastTableData.Data.Mcast.pFipClass = this;
        m_McastTableData.Data.Mcast.ClassType = VNIC_CLASS;
        FillPortMcastReq(&McastReq, &m_TableVhubMgid, false, &m_McastTableData);

        ib_status = mcast_mgr_join_mcast(&McastReq, &m_McastTable);
        if (ib_status != IB_SUCCESS) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s mcast_mgr_join_mcast_no_qp failed ib_status = %d\n", m_UniqeStr, ib_status);
            ASSERT(m_McastTable == NULL);
            Status = STATUS_INVALID_CONNECTION;
            // We can continue and try to connect the second mcast
        }
    }

    // Join Update mcast
    if (!m_McastUpdateConnected && m_McastUpdate == NULL) {
        CreateVhubMgid(VHUB_MGID_UPDATE, &m_UpdateVhubMgid);
        m_McastUpdateData.Data.Mcast.pFipClass = this;
        m_McastUpdateData.Data.Mcast.ClassType = VNIC_CLASS;
        FillPortMcastReq(&McastReq, &m_UpdateVhubMgid, false, &m_McastUpdateData);

        ib_status = mcast_mgr_join_mcast(&McastReq, &m_McastUpdate);
        if (ib_status != IB_SUCCESS) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s mcast_mgr_join_mcast_no_qp failed ib_status = %d\n", m_UniqeStr, ib_status);
            ASSERT(m_McastUpdate == NULL);
            Status = STATUS_INVALID_CONNECTION;
        }
    }

    return Status;
}


/*
 * This Function will try to recv and incoming Vhub-Table Msg
 * Then send it to parse at VhubTable class.
 * Then we verify whether we exist in this Vhub Msg.
 * If yes we'll move to connected state and create and EoIB IF.
 */
NTSTATUS
Vnic::RecvVhubUpdate( VOID )
{
    NTSTATUS Status;
    RcvMsg *pRcvMsg = NULL;

    ASSERT(m_pDataInterfaceVnic != NULL);
    Status = m_SendRecv.Recv(&pRcvMsg);
    if ((!NT_SUCCESS(Status)) &&
        (Status != STATUS_PIPE_EMPTY)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to Recv Vhub Msg Status=0x%x\n", m_UniqeStr, Status);
        m_VnicState = LOGIN_REJECT;
        goto recv_vhub_end;
    }
    if(pRcvMsg == NULL) {
        return STATUS_SUCCESS;
    }

    if (m_DidWeInitVhubTable == false) {
        Status = m_VhubTable.Init(
            m_VHubId,
            m_Mac,
            m_UniqeStr,
            m_pDataInterfaceVnic,
            &m_SendRecv);
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed Init m_VhubTable\n", m_UniqeStr);
            m_VnicState = LOGIN_REJECT;
            goto recv_vhub_end;
        }
        m_DidWeInitVhubTable = true;
    }

    // Move Vhub Msg to parse
    m_VhubTable.AddMessage(pRcvMsg);
    m_VhubTable.RunFSM();

    if (m_VhubTable.NeedUpdate()) {
        Status = RequestVHubTable();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to RequestVHubTable Status=0x%x\n", m_UniqeStr, Status);
            m_VnicState = LOGIN_REJECT;
            goto recv_vhub_end;
        }
        goto recv_vhub_end;
    }

    if ((m_VhubTable.IsUp2date()) &&
        (!m_VhubTable.IsVnicExist())) {   // Vnic was removed
        m_IsVnicAlive = false;
        m_VnicState = WAIT_TO_BE_KILLED;
    }

    if ((m_VhubTable.IsVnicExist()) &&
        (m_VnicState != CONNECTED)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s is now connected !!!\n", m_UniqeStr);
        m_VnicState = CONNECTED;

        ASSERT(memcmp(m_Mac, EmptyMac, sizeof(m_Mac)) != 0);
        m_pDataInterfaceVnic->UpdateMac(m_Mac);

        m_pDataInterfaceVnic->UpdateMgidParams(
            m_EthGidPrefix,
            m_VHubId,
            m_NRssMgid);
    }

    // Update to Current Tusn
    if (m_VhubTable.IsUp2date()) {
        m_Tusn = m_VhubTable.GetLastTusn();
    }

    if ((m_WasEoIBInterfaceCreated == false) &&
        (m_VnicState == CONNECTED)) {
        Status = CreateEoibInterface(
            m_LocalPortGuid,
            m_Pkey,
            m_UniqeIFName,
            m_UniqueId,
            m_DataLocation);
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to CreateEoibInterface Status=0x%x\n", m_UniqeStr, Status);
            m_VnicState = LOGIN_REJECT;
            goto recv_vhub_end;
        }
        m_WasEoIBInterfaceCreated = true;
    }

recv_vhub_end:
    return Status;
}


bool
Vnic::IsVnicAlive( VOID )
{
    return m_IsVnicAlive;
}


NTSTATUS
Vnic::RunFSM( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(m_Shutdown == false);
    ASSERT(m_VnicState != HADMIN_IDLE);

    FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_VNIC, "%s Starting at State %s\n", m_UniqeStr, StateToStr(m_VnicState));

    switch (m_VnicState) {
    case LOGIN:    //Can be in login state only if we failed the last time
        if (m_pDataInterfaceVnic == NULL) { // Will be called only if we came from reject
            Status = AddVnicInterface();
            if (!NT_SUCCESS(Status)) {
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s AddVnicInterface Failed\n", m_UniqeStr);
                goto fsm_end;
            }
        }

        Status = Login();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to Login\n", m_UniqeStr);
            goto fsm_end;
        }
        break;
    case WAIT_FOR_ACK:
        if (CallNextTime() == 0) {  // If a TimeOut has passed since the last Login Msg we'll retry to login
            m_VnicState = LOGIN;
            break;
        }

        Status = RecvAck();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to Recv Ack\n", m_UniqeStr);
            goto fsm_end;
        }
        break;

    case LOGIN_REJECT:
        // TODO: Add support on Reject
        m_VnicState = LOGIN;
        if (m_DidWeInitVhubTable == true) {
            m_VhubTable.Shutdown();
            m_DidWeInitVhubTable = false;
        }

        RemoveVnicInterface();
        if (m_WasEoIBInterfaceCreated == true) {
            DestroyEoibInterface(m_LocalPortGuid, m_Pkey, m_UniqueId, m_DataLocation);
            m_WasEoIBInterfaceCreated = false;
        }

        break;
    case JOIN_VHUB_MCASTS:
        Status = JoinMcastForVhubMsg();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to Join Mcast For Vhub Update\n", m_UniqeStr);
            goto fsm_end;
        }

        if (CallNextTime() == 0) {  // If a TimeOut has passed since the last Login Msg we'll retry to login
            LeaveAndDestroyMcast(); // In case we succeeded to join one of these mcast..
            m_VnicState = LOGIN;
        }
        break;

    case REQUEST_VHUB_TABLE:
        Status = RequestVHubTable();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to RequestVHubTable\n", m_UniqeStr);
            goto fsm_end;
        }

        m_VnicState = VHUB_TABLE_INIT;
        break;
    case VHUB_TABLE_INIT:
        Status = RecvVhubUpdate();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to Recv Vhub Update\n", m_UniqeStr);
            goto fsm_end;
        }

        // TODO: Should we check this ????
        if (CallNextTime() == 0) {  // If a TimeOut has passed since the last Login Msg we'll retry to login
            LeaveAndDestroyMcast(); // In case we succeeded to join one of these mcast..
            m_VnicState = LOGIN_REJECT;
        }
        break;

    case CONNECTED:
        Status = RecvVhubUpdate();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to Recv Vhub Update\n", m_UniqeStr);
            goto fsm_end;
        }

        if (CallNextTime() == 0) {
            Status = SendMsg();
            if (!NT_SUCCESS(Status)) {
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to Send Keep Alive\n", m_UniqeStr);
                goto fsm_end;
            }
        }
        break;

    case WAIT_TO_BE_KILLED:
        break;  // Nothing to do but wait for the GW to destroy me
    }

fsm_end:
    return Status;
}


VOID
Vnic::PrepareLoginMsg(
    FipLogin_t *pFipLogin
    )
{
    memset(pFipLogin, 0, sizeof(*pFipLogin));

    pFipLogin->version.version = 0;

    pFipLogin->fip.opcode = cl_ntoh16(EOIB_FIP_OPCODE);
    pFipLogin->fip.subcode = FIP_HOST_LOGIN_SUB_OPCODE;
    pFipLogin->fip.list_length = cl_hton16(FIP_CTRL_HDR_MSG_LEGTH);
    pFipLogin->fip.list_length = cl_hton16((sizeof(*pFipLogin) - sizeof(FipVer_t)) >> 2);// should be 20
    pFipLogin->fip.type.type = FIP_FIP_HDR_TYPE;
    pFipLogin->fip.type.length = FIP_FIP_HDR_LENGTH;//sizeof(pFipLogin->fvend) / 4; 
    memcpy(pFipLogin->fip.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(pFipLogin->fip.vendor_id));

    pFipLogin->base.type.type = FIP_BASIC_TYPE;
    pFipLogin->base.type.length = FIP_BASIC_LENGTH;
    memcpy(pFipLogin->base.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(pFipLogin->base.vendor_id));
    pFipLogin->base.qpn = cl_hton32((u32)(m_LocalDataQpn));
    /* sl in vnic_login is 0. BXM will provide SL in login ack */
    pFipLogin->base.sl_port_id = cl_hton16(m_PortId);
    pFipLogin->base.lid = cl_hton16(m_LocalLid);
    memcpy(pFipLogin->base.guid, &m_LocalPortGuid, sizeof(pFipLogin->base.guid));
    
    pFipLogin->VnicLogin.type.type = FIP_LOGIN_TYPE;
    pFipLogin->VnicLogin.type.length = FIP_LOGIN_LENGTH;
    memcpy(pFipLogin->VnicLogin.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(pFipLogin->VnicLogin.vendor_id));
    pFipLogin->VnicLogin.vnic_id = cl_hton16(m_VnicId);

    if (m_VnicType == HOST_VNIC) { // TODO: WHY LINUX BEHAVE THIS WAY
        u16 flags = (u16)(FIP_LOGIN_H_FLAG |
                (m_ValidMac ? FIP_LOGIN_M_FLAG : 0) |
                (m_ValidVlan ? (FIP_LOGIN_VP_FLAG | FIP_LOGIN_V_FLAG) : 0));
        pFipLogin->VnicLogin.flags_vlan = cl_hton16(flags | m_Vlan);
        memcpy(pFipLogin->VnicLogin.mac, m_Mac, sizeof(pFipLogin->VnicLogin.mac));
        memcpy(pFipLogin->VnicLogin.vnic_name, m_Name, sizeof(pFipLogin->VnicLogin.vnic_name));

        // TODO remove this when BXM handles 0 addresses
        if (!m_ValidMac)
        {
            pFipLogin->VnicLogin.mac[ETH_ALEN-1] = 1;
        }
    }

    /* all_vlan mode must be enforced between the host and GW side. - LINUX CODE
       For host admin vnic with VLAN we let the host choose the work mode.
       If the GW isn't working in that same mode, the login will fail
       and the host will enter a login-retry loop
       For net admin vnic or host admin without a vlan, we work in the mode
       published by the GW */
/*    if (all_gw_vlan &&
        ((m_VnicType == NETWORK_VNIC) ||
         ((m_VnicType == HOST_VNIC) && !m_ValidVlan)))
        pFipLogin->VnicLogin.vfields |= FIP_LOGIN_ALL_VLAN_GW_FLAG;

    // for child vNics, allow implicit logout  - LINUX CODE
    if (vnic->parent_used) {
        pFipLogin->VnicLogin.vfields |= cl_ntoh16(1 << FIP_LOGIN_IPL_SHIFT);   //Implicit Logout Policy
        pFipLogin->VnicLogin.vfields |= cl_ntoh16(1 << FIP_LOGIN_IP_SHIFT);   //Implicit Logout
    }*/

    // Syndrom must be 0
    // Also we don't SW-RSS, HW does it instead so this bit is 0.
    pFipLogin->VnicLogin.syn_ctrl_qpn = m_LocalCtrlQpn;
    pFipLogin->VnicLogin.vfields |= cl_ntoh16(1 << FIP_LOGIN_IPL_SHIFT);   //Implicit Logout Policy
    pFipLogin->VnicLogin.vfields |= cl_ntoh16(1 << FIP_LOGIN_IP_SHIFT);   //Implicit Logout
}


/*
 * Send a unicast login packet. This function supports both host and
 * network admined logins.
 */
NTSTATUS
Vnic::Login( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    FipLogin_t FipLogin;

    m_LoginRetry++;
    if (m_LoginRetry > MAX_LOGIN_RETRY) {
        m_IsVnicAlive = false;  // If we are trying too much will ask the GW to recreate us.. (maybe we were removed and never notived that)
        m_VnicState = WAIT_TO_BE_KILLED;
        return Status;
    }

    if (!m_SendRecv.IsAvValid()) {  // Create AV only one time
        Status = m_SendRecv.SetAndCreateAV(m_Lid);
        if (!NT_SUCCESS(Status))
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to SetAndCreateAV\n", m_UniqeStr);
            goto end_login;
        }
    }

    PrepareLoginMsg(&FipLogin);

    Status = m_SendRecv.Send(NULL, cl_hton32(m_CtrlQpn), (PCHAR)&FipLogin, sizeof(FipLogin));
    if (!NT_SUCCESS(Status))
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed Sending Login Message\n", m_UniqeStr);
        goto end_login;
    }

    // 0..100ms random time bofore sending next period msg, BX-PRM 2.7.2 P.113
    m_LastKAPeriod = GetTickCountInMsec() + (GetTickCountInMsec() % RANDOM_TIME_PERIOD);
    m_VnicState = WAIT_FOR_ACK;

end_login:
    return Status;
}


/*
 * parse a packet that is suspected of being an login ack packet. The packet
 * returns 0 for a valid login ack packet and an error code otherwise. The
 * packets "interesting" details are returned in data.
 */
NTSTATUS
Vnic::ParseLoginMsg( 
    PVOID LoginMsg,
    UINT Length
    )
{
    if (Length != (sizeof(FipLoginAck_t) + GRH_LEN)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Dump packet:"
            "unexpected size. length %d expected %d\n",
             m_UniqeStr,
             (int)Length,
             (int)(sizeof(FipLoginAck_t) + GRH_LEN));
        return STATUS_INVALID_BUFFER_SIZE;
    }

    FipLoginAck_t *pFipLogin = (FipLoginAck_t*)((UCHAR *)LoginMsg + GRH_LEN);

    if (pFipLogin->login.fip.type.type != FIP_FIP_HDR_TYPE ||
        pFipLogin->login.fip.type.length != FIP_FIP_HDR_LENGTH||
        pFipLogin->login.base.type.type != FIP_BASIC_TYPE ||
        pFipLogin->login.base.type.length != FIP_BASIC_LENGTH||
        pFipLogin->login.VnicLogin.type.type != FIP_LOGIN_TYPE ||
        pFipLogin->login.VnicLogin.type.length != FIP_LOGIN_LENGTH||
        pFipLogin->partition.type.type != FIP_LOGIN_PARTITION_TYPE ||
        pFipLogin->partition.type.length != FIP_LOGIN_PARTITION_LENGTH) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, 
                 "%s pFipLogin Msg format err."
                 "(TLV,LNTH,rcvd TLV, rcvd LNTH): "
                "(%d,%d,%d,%d), (%d,%d,%d,%d), (%d,%d,%d,%d)\n",
                m_UniqeStr,
                FIP_FIP_HDR_TYPE, FIP_FIP_HDR_LENGTH,
                pFipLogin->login.fip.type.type, pFipLogin->login.fip.type.length,
                FIP_LOGIN_TYPE, FIP_LOGIN_LENGTH,
                pFipLogin->login.base.type.type, pFipLogin->login.base.type.length,
                FIP_LOGIN_PARTITION_TYPE, FIP_LOGIN_PARTITION_LENGTH,
                pFipLogin->partition.type.type, pFipLogin->partition.type.length);
        return STATUS_NDIS_INVALID_PACKET;
    }

    m_Syndrome = (LoginSyndrom_t)(cl_ntoh32(pFipLogin->login.VnicLogin.syn_ctrl_qpn) >> FIP_LOGIN_SYNDROM_SHIFT);
    if (m_Syndrome != FIP_SYNDROM_SUCCESS) {
        // Once we got Reject we stop parsing and the caller function will have to check the syndrom
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Got Login Ack with Syndrom %s\n", m_UniqeStr, SyndromToStr(m_Syndrome));
        return STATUS_SUCCESS;  // We have to retry again.. P.103 - 2.5.2.4
    }

    m_VnicId = cl_ntoh16(pFipLogin->login.VnicLogin.vnic_id);   // Return the actual Vnic name
    m_Lid = cl_ntoh16(pFipLogin->login.base.lid);
    m_PortId = (u16)(cl_ntoh16(pFipLogin->login.base.sl_port_id) & FIP_ADVERTISE_GW_PORT_ID_MASK);
    m_Sl = (u8)(cl_ntoh16(pFipLogin->login.base.sl_port_id) >> FIP_ADVERTISE_SL_SHIFT);
    m_DataQpn = (u32)(cl_ntoh32(pFipLogin->login.base.qpn) & FIP_LOGIN_QPN_MASK);
    memcpy(m_GwGuid, pFipLogin->login.base.guid, sizeof(m_GwGuid));

    if (cl_ntoh16(pFipLogin->login.VnicLogin.flags_vlan) & FIP_LOGIN_VP_FLAG) {
        m_ValidVlan = 1;
        m_Vlan = (u16)(cl_ntoh16(pFipLogin->login.VnicLogin.flags_vlan) & FIP_LOGIN_VLAN_MASK);
    }
//    m_all_vlan_gw = !!(cl_ntoh16(fc->fl->vfields) & FIP_LOGIN_ALL_VLAN_GW_FLAG);

    m_VHubId = CREATE_VHUB_ID(m_Vlan, m_PortId);

    m_DataQpn = cl_ntoh32(pFipLogin->login.VnicLogin.syn_ctrl_qpn) & FIP_LOGIN_QPN_MASK;
    u16 vfields = cl_ntoh16(pFipLogin->login.VnicLogin.vfields);
    m_NMacMcgid = (u8)(vfields & FIP_LOGIN_DMAC_MGID_MASK);
    m_NRssMgid = (u8)((vfields >> FIP_LOGIN_RSS_SHIFT) & FIP_LOGIN_RSS_MGID_MASK);
    /* m_Rss = FipHostUpdate->rss & FIP_LOGIN_RSS_MASK; it's redundant in login ack */
    m_Pkey = cl_ntoh16(pFipLogin->partition.pkey);
    m_Mtu = cl_ntoh16(pFipLogin->login.VnicLogin.mtu);

    memcpy(m_Mac, pFipLogin->login.VnicLogin.mac, sizeof(m_Mac));
    memcpy(m_EthGidPrefix, pFipLogin->login.VnicLogin.eth_gid_prefix, sizeof(m_EthGidPrefix));
    MemcpyStr((PCHAR)m_Name, (PCHAR)pFipLogin->login.VnicLogin.vnic_name, sizeof(m_Name));
    MemcpyStr((PCHAR)m_VendorId, (PCHAR)pFipLogin->login.VnicLogin.vendor_id, sizeof(m_VendorId));

    return STATUS_SUCCESS;
}


NTSTATUS
Vnic::RecvAck( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    RcvMsg *pRcvMsg = NULL;

    if (m_IncomingMsgHead.Size() == 0) {
        goto recv_end;
    }

    pRcvMsg = PeekMessage();
    Status = ParseLoginMsg(pRcvMsg->m_AlignedData, pRcvMsg->m_Len);
    RemoveMessage(pRcvMsg);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed to Parse Login Ack Msg\n", m_UniqeStr);
        goto recv_end;
    }

    // Check whether vnic login was accepted 
    if (m_Syndrome == FIP_SYNDROM_SUCCESS) {
        m_VnicState = JOIN_VHUB_MCASTS;
    } else {
        m_VnicState = LOGIN_REJECT;
    }

recv_end:
    return Status;
}


/*
 * Prepare LogOut & Keep-Alive Msg
 */
VOID
Vnic::PrepareVnicMsg(
    VnicMsgType_t MsgType,
    FipHostUpdate_t *FipHostUpdate
)
{
    memset(FipHostUpdate, 0, sizeof(*FipHostUpdate));


    FipHostUpdate->version.version = 0;

    if (MsgType == LOGOUT) {
        FipHostUpdate->fip.subcode = FIP_HOST_LOGOUT_SUB_OPCODE;

        FipHostUpdate->fip_vnic_ka.type.type = FIP_LOGOUT_TYPE;
        FipHostUpdate->fip_vnic_ka.type.length = FIP_LOGOUT_LENGTH;
    } else {    // KA
        FipHostUpdate->fip.subcode = FIP_HOST_ALIVE_SUB_OPCODE;

        FipHostUpdate->fip_vnic_ka.type.type = FIP_HOST_UPDATE_TYPE;
        FipHostUpdate->fip_vnic_ka.type.length = FIP_HOST_UPDATE_LENGTH;
    }

    FipHostUpdate->fip.opcode = cl_hton16(EOIB_FIP_OPCODE);
    FipHostUpdate->fip.type.type = FIP_FIP_HDR_TYPE;
    FipHostUpdate->fip.type.length = FIP_FIP_HDR_LENGTH;
    memcpy(FipHostUpdate->fip.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(FipHostUpdate->fip.vendor_id));
    FipHostUpdate->fip.list_length = cl_hton16((sizeof(FipHostUpdate_t) >> 2) - 3);


    FipHostUpdate->fip_vnic_ka.vnic_id = cl_hton16(m_VnicId);
    memcpy(FipHostUpdate->fip_vnic_ka.mac, m_Mac, sizeof(FipHostUpdate->fip_vnic_ka.mac));
    memcpy(FipHostUpdate->fip_vnic_ka.vnic_name, m_Name, sizeof(FipHostUpdate->fip_vnic_ka.vnic_name));
    memcpy(FipHostUpdate->fip_vnic_ka.port_guid, &m_LocalPortGuid, sizeof(FipHostUpdate->fip_vnic_ka.port_guid));
    memcpy(FipHostUpdate->fip_vnic_ka.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(FipHostUpdate->fip_vnic_ka.vendor_id));

    u32 VHubId = cl_hton32(m_VHubId);
    memcpy(FipHostUpdate->fip_vnic_ka.vhub_id, ((u8 *) &VHubId) + 1,    // Convert 32 bits to 24 bits
        sizeof(FipHostUpdate->fip_vnic_ka.vhub_id));

    if (MsgType == KEEP_ALIVE) {
        FipHostUpdate->fip_vnic_ka.tusn = cl_hton32(m_Tusn);

        if (m_ValidVlan) {
            FipHostUpdate->fip_vnic_ka.flags |= FIP_HOST_VP_FLAG;
        }

        if (m_NeedVhubTable) {
            FipHostUpdate->fip_vnic_ka.flags |= FIP_HOST_R_FLAG;  // Request vHub Table
        } else {
            FipHostUpdate->fip_vnic_ka.flags |= FIP_HOST_U_FLAG;  // We have the updated table for this Tusn
        }
    }
}


/*
 * This function creates and sends a few types of packets (all ucast):
 *   1. vHub context request - according to "m_NeedVhubTable"
 *   2. vHub context update - according to "m_NeedVhubTable"
 *   3. vnic keep alive - according to "m_NeedVhubTable"
 *   3. vnic logout
*/
NTSTATUS
Vnic::SendGenericMsg(VnicMsgType_t MsgType)
{
    FipHostUpdate_t FipHostUpdate;
    NTSTATUS Status;

    ASSERT(m_Shutdown == false);

    // We don't create AV because we assume Login already created one
    PrepareVnicMsg(MsgType, &FipHostUpdate);

    Status = m_SendRecv.Send(NULL, cl_hton32(m_CtrlQpn), (PCHAR)&FipHostUpdate, sizeof(FipHostUpdate));
    if (!NT_SUCCESS(Status))
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "%s Failed Sending Update Message\n", m_UniqeStr);
        goto end_send;
    }

    // 0..100ms random time bofore sending next period msg, BX-PRM 2.7.2 P.113
    m_LastKAPeriod  = GetTickCountInMsec() + (GetTickCountInMsec() % RANDOM_TIME_PERIOD);

    return Status;

end_send:
    return Status;
}


/*
 * Send Implicit Vnic Logout Msg
 */
VOID
Vnic::Logout( VOID )
{
    if (m_VnicState != LOGIN) {
        SendGenericMsg(LOGOUT);
    }
}


NTSTATUS
Vnic::SendKA( VOID )
{
    return SendGenericMsg(KEEP_ALIVE);
}

NTSTATUS
Vnic::RequestVHubTable( VOID )
{
    m_NeedVhubTable = true;
    NTSTATUS Status = SendGenericMsg(KEEP_ALIVE);
    m_NeedVhubTable = false;
    return Status;
}


NTSTATUS
Vnic::SendMsg( VOID )
{
    if (m_VhubTable.NeedUpdate()) {
        return RequestVHubTable();
    } else {
        return SendKA();
    }
    
}


/*
 * The next 4 functions are handling the incoming msg from the port qp
 * 
 */
VOID
Vnic::AddMessage(
    RcvMsg *pRcvMsg
    )
{
    ASSERT(m_Shutdown == false);
    m_IncomingMsgHead.InsertTailList(&pRcvMsg->m_ExtenalEntry);
}


RcvMsg*
Vnic::PeekMessage( VOID )
{
    ASSERT(m_IncomingMsgHead.Size() != 0);

    RcvMsg * pRcvMsg = CONTAINING_RECORD(m_IncomingMsgHead.Head(), RcvMsg, m_ExtenalEntry);
    return pRcvMsg;
}

VOID
Vnic::RemoveMessage(
    RcvMsg *pRcvMsg
    )
{
    m_IncomingMsgHead.RemoveEntryList(&pRcvMsg->m_ExtenalEntry);
    m_PortSendRecv->ReturnBuffer(pRcvMsg);
}


VOID
Vnic::RemoveAllMessage( VOID )
{
    RcvMsg *pRcvMsg = NULL;

    while (m_IncomingMsgHead.Size() != 0) {
        pRcvMsg = PeekMessage();
        RemoveMessage(pRcvMsg);
    }
}


