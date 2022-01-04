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
    fip_gw.cpp

Abstract:
    This module contains miniport gateway routines

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

#include "fip_gw.tmh"
#endif



/*
 * Create SendRecv Class
 * Save necesary data 
 * and return event to the port for it to wait on them
 */
NTSTATUS
Gateway::Init(
    SendRecv *pSendRecv,
    ib_net64_t LocalPortGuid,
    u8 LocalEnumMTU,
    ib_net32_t Qkey,
    uint16_t LocalPkeyIndex,
    ib_net16_t LocalLid,
    u8 Guid[GUID_LEN],
    u16 PortId,
    u8 PortName[VNIC_GW_PORT_NAME_LEN],
    u8 SystemGuid[GUID_LEN],
    u8 SystemName[VNIC_SYSTEM_NAME_LEN],
    PMLX4_BUS_IB_INTERFACE pBusIbInterface,
    KEVENT *MsgArrivedEvent,
    KEVENT *CqErrorEvent
    )
{
    NTSTATUS Status;

    ASSERT(m_Shutdown == false);
    ASSERT(pSendRecv != NULL);
    ASSERT(MsgArrivedEvent != NULL);
    ASSERT(CqErrorEvent != NULL);


    m_SendRecv          = pSendRecv;
    m_LocalLid          = LocalLid;
    m_LocalPortGuid     = LocalPortGuid;
    m_Qkey              = Qkey;
    m_LocalEnumMTU      = LocalEnumMTU;
    m_LocalPkeyIndex    = LocalPkeyIndex;
    m_FipGwState        = FIP_GW_INIT;
    m_MsgArrivedEvent   = MsgArrivedEvent;
    m_CqErrorEvent      = CqErrorEvent;
    m_pBusIbInterface   = pBusIbInterface;

    // These are Uniqe GW Params
    m_PortId = PortId;
    memcpy(m_Guid, Guid, sizeof(m_Guid));
    MemcpyStr((PCHAR)m_PortName, (PCHAR)PortName, sizeof(m_PortName));
    memcpy(m_SystemGuid, SystemGuid, sizeof(m_SystemGuid));
    MemcpyStr((PCHAR)m_SystemName, (PCHAR)SystemName, sizeof(m_SystemName));

    Status = Vnics.Init(INITIAL_ARRAY_SIZE, true);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Init Vnics Array\n");
    }

    for (int i = 0; i < MAXDEBUGVNICS; i++) {
        FipVnics[i] = NULL;
    }

    UpdateGwUniqeStr();

    return Status;
}


/* 
 * returns true if the GW last Adv msg has Time out
 */
int
Gateway::NextGwTimeOut( VOID )
{
    s64 NextTime;

    NextTime = m_LastAdvPeriod + Mult2_5(m_AdvPeriod) - GetTickCountInMsec();
    NextTime = max(0, NextTime);
    return (int)NextTime;
}


/* 
 * returns true if the GW last Adv msg has Time out
 */
bool
Gateway::IsGwTimeOut( VOID )
{
    ASSERT(m_FipGwState != FIP_GW_PREINIT);
    bool IsDisconnected = (m_FipGwState == FIP_GW_CONNECTED) && (NextGwTimeOut() == 0);

    if (IsDisconnected) {   // ShutDown will be called soon
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Gateway %s has disconnected due to time out!!!\n", m_GwUniqeStr);
    }
    return IsDisconnected;
}


int
Gateway::NextKA( VOID )
{
    s64 NextTime;

    NextTime   = m_LastKAPeriod + m_KAPeriod - GetTickCountInMsec();
    NextTime = max(0, NextTime); // Time must be positive
    return (int)NextTime;
}


int
Gateway::NextVnicKA( VOID )
{
    s64 NextTime = m_KAPeriod;  // Max KA

    for (int i = 0; i < Vnics.GetMaxSize(); i++) {
        NextTime = min(NextTime, (s64)Vnics[i]->CallNextTime());
        NextTime = max(0, NextTime); // Time must be positive
        if (NextTime == 0)
        {   // Can't be lower then 0 Usec. let's save time..
            break;
        }
    }

    return (int)NextTime;
}


int
Gateway::CallNextTime( VOID )
{
    ASSERT(m_Shutdown == false);

    int NextTime = NextVnicKA();

    if (m_IncomingMsgHead.Size() != 0) {
        NextTime = 0; // If we have Msg to parse let's ask the Thread to wake us imidiatelly
        return NextTime;
    }

    NextTime = min(NextTime, NextKA());
    NextTime = min(NextTime, NextGwTimeOut());
    return NextTime;
}


VOID
Gateway::Shutdown()
{
    ASSERT(m_Shutdown == false);

    m_Shutdown = true;
    RemoveAllVnics();
    Vnics.Shutdown();
    RemoveAllMessage();

    if (m_SendRecv->IsAvValid()) {  // Create AV only one time
        m_SendRecv->DestroyAv();
    }
}


bool
Gateway::IsMatch(
    UniqeIdentifyParams_t *UniqeParams
    )
{
    ASSERT(m_Shutdown == false);
    bool fIsMatch = true;

    switch (UniqeParams->MsgType) {
    case ADV_MSG:
        fIsMatch &= (UniqeParams->PortId == m_PortId);
        fIsMatch &= (memcmp(m_PortName, UniqeParams->Gw.PortName, VNIC_GW_PORT_NAME_LEN) == 0);
        fIsMatch &= (memcmp(m_Guid, UniqeParams->Guid, GUID_LEN) == 0);
        fIsMatch &= (memcmp(m_SystemGuid, UniqeParams->Gw.SystemGuid, GUID_LEN) == 0);
        fIsMatch &= (memcmp(m_SystemName, UniqeParams->Gw.SystemName, VNIC_SYSTEM_NAME_LEN) == 0);
        break;
    case LOGIN_ACK_MSG:
        // Run on all Vnics to find match
        fIsMatch = false;
        for (int i = 0; i < Vnics.GetMaxSize(); i++) {
            if ((Vnics[i] != NULL) &&
                (Vnics[i]->IsMatch(UniqeParams))) {
                fIsMatch = true;
                break;
            }
        }
        break;
    default:
        ASSERT(FALSE);
        break;
    }

    return fIsMatch;
}


VOID
Gateway::UpdateGwUniqeStr ( VOID )
{
    NTSTATUS Status;

    Status = RtlStringCbPrintfA(
        m_GwUniqeStr,
        UNIQE_GW_LEN,
        "Qpn %d, PortName %s, SystemName %s",
        m_Qpn,
        m_PortName,
        m_SystemName);
/*    Status = RtlStringCbPrintfA(
        m_GwUniqeStr,
        UNIQE_GW_LEN,
        "Qpn %d, PortId %d, PortName %s, Guid 0x%I64X, SystemGuid 0x%I64X, SystemName %s",
        m_Qpn,
        m_PortId,
        m_PortName,
        cl_ntoh64(U8_TO_U64(m_Guid)),
        cl_ntoh64(U8_TO_U64(m_SystemGuid)),
        m_SystemName);*/
    if (!NT_SUCCESS(Status)) {
        ASSERT(FALSE);
        Status = RtlStringCbCopyA(m_GwUniqeStr, sizeof(m_GwUniqeStr), "ERROR" );
        ASSERT(Status == STATUS_SUCCESS);
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "RtlStringCbPrintfA Failed\n");
    }
}



NTSTATUS
Gateway::ParseMcastMsg( 
    PVOID pAdvMsg,
    ULONG Length
    )
{
    NTSTATUS Status;

    ASSERT((pAdvMsg != NULL) && (Length != 0));
    Status = ParseAdvMsg(pAdvMsg, Length);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to ParseAdvMsg\n");
        goto mcast_end;
    }

    // Update timer
    m_LastAdvPeriod = GetTickCountInMsec();

mcast_end:
    return Status;
}


NTSTATUS
Gateway::ParseUCMsg( 
    RcvMsg *pRcvMsg
    )
{
    NTSTATUS Status;

    Status = ParseAdvMsg(pRcvMsg->m_AlignedData, pRcvMsg->m_Len);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Parse UC Adv Msg\n");
        goto recv_end;
    }

    // Update timer
    m_LastAdvPeriod = GetTickCountInMsec();

recv_end:
    return Status;
}


/*
 * Run on all Vnics and search for the next availiable VnicId
 */
u32
Gateway::FindNextAvailVnicId( VOID )
{
    // TODO: Impl. this function
    return FIRST_VNIC_SN;
}


/*
 * Create new Vnic and add it to the list
 */
NTSTATUS
Gateway::AddVnic(
    VnicType_t VnicType,
    bool ValidMac,
    u8 Mac[HW_ADDR_LEN],
    bool ValidVlan,
    u16 Vlan
    )
{
    NTSTATUS Status;
    u16 VnicId = 0;   // A uniqe ID (along with GW-Guid)
    u8 Name[VNIC_NAME_LEN + 1];
    int FreeLocation;

    Vnic *NewVnic = NEW Vnic;
    if (NewVnic == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Allocate memory for New Vnic\n");
        Status = STATUS_NO_MEMORY;
        goto new_vnic_err;
    }

    /* set bit 16 for hadmin vNics (by spec) */
    VnicId |= ((VnicType == HOST_VNIC) << (VNIC_ID_LEN - 1));
    VnicId |= m_VnicSerialNumber;
    m_VnicSerialNumber++;
    // For Performance: Check if we overlap the max Vnic Serial Number
    m_VnicSerialNumber = (m_VnicSerialNumber < 0x8000) ? m_VnicSerialNumber :FindNextAvailVnicId();

    // Determine the Vnic Name
    Status = RtlStringCbPrintfA((char*)Name, VNIC_NAME_LEN, "Vnic-%d", VnicId); /* will be overwritten by BX*/
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "RtlStringCbPrintfA Failed\n");
        ASSERT(FALSE);
        goto init_vnic_err;
    }

    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Going to create new Vnic %d (%s)\n", VnicId, m_GwUniqeStr);

    Status = NewVnic->Init(
        VnicType,
        m_VnicKAPeriod,
        m_LocalLid,
        m_LocalPortGuid,
        m_Qkey,
        m_LocalPkeyIndex,
        m_Lid,
        m_Qpn,
        m_PortId,
        m_RssQpn,
        m_LocalEnumMTU,
        ValidMac,
        Mac,
        ValidVlan,
        Vlan,
        VnicId,
        m_Guid,
        Name,
        m_Sl,
        m_PortName,
        m_pBusIbInterface,
        m_SendRecv,
        m_MsgArrivedEvent,
        m_CqErrorEvent
        );
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Init New Vnic\n");
        goto init_vnic_err;
    }

    if (VnicType == HOST_VNIC) {
        m_HostAdminVnicsCounter++;
    } else {
        m_NetworkAdminVnicsCounter++;
    }

    FreeLocation = Vnics.GetFreeLocation();
    if (FreeLocation == NO_FREE_LOCATION) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Find Free Location for New Vnic\n");
        Status = STATUS_NO_MEMORY;
        goto add_vnic_err;
    }
    Vnics[FreeLocation] = NewVnic;
    if (FreeLocation < MAXDEBUGVNICS) {
        FipVnics[FreeLocation] = NewVnic;
    }

    Status = NewVnic->RunFSM();
    if (!NT_SUCCESS(Status)) {   // TODO: What to do on failure
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Start New Vnic\n");
        goto add_vnic_err;
    }

    return Status;

add_vnic_err:
    NewVnic->Shutdown();
init_vnic_err:
    if (NewVnic != NULL) {   // No need to Shutdown because Init failed
        delete NewVnic;
    }
new_vnic_err:
    return Status;
}


/*
 * Destroy Vnic instance and remove it from the list
 */
VOID
Gateway::RemoveVnic( 
    Vnic *VnicToRemove
    )
{
    bool Found = false;
    ASSERT(VnicToRemove != NULL);

    if (VnicToRemove->GetVnicType() == HOST_VNIC) {
        m_HostAdminVnicsCounter--;
    } else {
        m_NetworkAdminVnicsCounter--;
    }

    for (int i = 0; i < Vnics.GetMaxSize(); i++) {
        if (Vnics[i] == VnicToRemove) {
            Vnics[i] = NULL;
            Found = true;
            if (i < MAXDEBUGVNICS) {
                FipVnics[i] = NULL;
            }
            break;
        }
    }
    ASSERT(Found == true);

    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "%s Going to destroy Vnic %d\n", m_GwUniqeStr, VnicToRemove->GetVnicId());
    VnicToRemove->Shutdown();
    delete VnicToRemove;
}


VOID
Gateway::RemoveAllVnics( VOID )
{
    for (int i = 0; i < Vnics.GetMaxSize(); i++) {
        if (Vnics[i] != NULL) {
            RemoveVnic(Vnics[i]);
        }
    }
}


NTSTATUS
Gateway::CreateVnics( VOID )
{
    NTSTATUS Status, TotalStatus = STATUS_SUCCESS;
    ULONG vnic_num;

    //Create Network-Admin-Vnics
    for (vnic_num = m_NetworkAdminVnicsCounter; vnic_num < m_NumNetVnics; vnic_num++) {
        Status = AddVnic(NETWORK_VNIC, false, NULL, false, 0);   // TODO: Check for relevant params
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Add Network Vnic\n");
            TotalStatus = Status;
        }
    }
    

    // TODO: Support Host Admin Vnics
#if 0
    // Create Host-Admin-Vnics
    for (vnic_num = m_HostAdminVnicsCounter; vnic_num < g_pFipGlobals->NumHostVnics; vnic_num++) {
        u8 Mac[HW_ADDR_LEN];
        u16 Vlan = 0;   // TODO: Use real Vlan

        /* random_ether_addr(mac); */
        memcpy(Mac, m_Guid + sizeof(Mac) - sizeof(m_Guid), HW_ADDR_LEN);
        Mac[0] += (u8)(m_VnicSerialNumber* 0x10);
        /* mcast bit must be zero */
        Mac[0] &= 0xfe;

        Status = AddVnic(HOST_VNIC, true, Mac, true, Vlan);
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Add Host Vnic\n");
            TotalStatus = Status;
        }
    }

#endif

    return TotalStatus;
}


VOID
Gateway::DestroyVnics( VOID )
{
    ULONG vnic_num;
    ASSERT(FALSE); // TODO: Not impl. yet.......
    //Destroy Network-Admin-Vnics
    // TODO: Which Vnic to destroy
    for (vnic_num = m_NumNetVnics; vnic_num < m_NetworkAdminVnicsCounter; vnic_num++) {
        //RemoveVnic();
    }
    
    // Destroy Host-Admin-Vnics
    // TODO: Which Vnic to destroy
    for (vnic_num = g_pFipGlobals->NumHostVnics; vnic_num < m_HostAdminVnicsCounter; vnic_num++) {
        //RemoveVnic(&tmp);
    }

}


NTSTATUS
Gateway::RunAllVnics( VOID )
{
    NTSTATUS Status, TotalStatus = STATUS_SUCCESS;
    int i = -1;

    for (i = 0; i < Vnics.GetMaxSize(); i++) {
        if (Vnics[i] != NULL) {
            Status = Vnics[i]->RunFSM();
            if (!NT_SUCCESS(Status))
            {
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to Run Vnic %d.\n it's going to be destroyed\n", Vnics[i]->GetVnicId());
                RemoveVnic(Vnics[i]);   // TODO: Handle RunFSM Err
                TotalStatus = Status;
                continue;
            }

            if ((Vnics[i]->IsVnicTimeOut()) ||  // If a TOUT occuered
                (!Vnics[i]->IsVnicAlive())) {  // If Vnic was kiiled we have to destroy it
                RemoveVnic(Vnics[i]);
            }
        }
    }

    return TotalStatus;
}


bool
Gateway::IsMcastMsg(
    RcvMsg *pRcvMsg
    )
{
    bool IsMcast = false;

    if ((pRcvMsg->m_RecvOpt & IB_RECV_OPT_GRH_VALID) != 0) {
        ib_grh_t *grh = (ib_grh_t*)(pRcvMsg->m_AlignedData);
        IsMcast = (grh->dest_gid.multicast.header[0] == 0xFF) && (grh->dest_gid.multicast.header[1] == 0x12);
    }
    return IsMcast;
}


/*
 * ************ Mcast Avd Msg Handling ***************
 */
NTSTATUS
Gateway::HandleMcastMsg( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (m_IncomingMsgHead.Size() != 0) {
        RcvMsg *pRcvMsg;

        pRcvMsg = PeekMessage(); // Get next msg in the list

        if (!IsMcastMsg(pRcvMsg)) { // If we get UC Msg we will drop it
            // TODO: What to do with redundant UC msg - Should we update Timers
                RemoveMessage(pRcvMsg);
                m_LastAdvPeriod = GetTickCountInMsec(); // Update timer
                goto handle_mcast_end;
        }
        
        Status = ParseMcastMsg(pRcvMsg->m_AlignedData, pRcvMsg->m_Len);
        RemoveMessage(pRcvMsg); // This is a redundant Mcast msg - will be silently dropped
        if (!NT_SUCCESS(Status)) {  // In case of msg err the can be 2 way to handle it
            // Expecting another Avd Mcast Msg
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to ParseMcastMsg\n");
            goto handle_mcast_end;
        }
        m_FipGwState = FIP_GW_ACK;
    }

handle_mcast_end:
    return Status;
}


/*
 * ************ UC Avd Msg Handling ***************
 */
NTSTATUS
Gateway::HandleUCMsg( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    RcvMsg *pRcvMsg = NULL;

    while (m_IncomingMsgHead.Size() != 0) {
        pRcvMsg = PeekMessage(); // Get next msg in the list
        if (IsMcastMsg(pRcvMsg)) { // If we get Mcast Msg we will drop it
            // TODO: What to do with redundant Mcast msg - Should we update Timers
                RemoveMessage(pRcvMsg);
                m_LastAdvPeriod = GetTickCountInMsec(); // Update timer
                goto handle_uc_end;
        }

        Status = ParseUCMsg(pRcvMsg);
        RemoveMessage(pRcvMsg); // This is a redundant Mcast msg - will be silently dropped
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to ParseUCMsg\n");
            goto handle_uc_end;
        }

        if (m_AvailForLogin) {   // The GW accept our solicit request
            if (m_FipGwState == FIP_GW_CONNECTED) {
                // TODO: Compare whether any param have changed
                m_FipGwState = FIP_GW_MODIFY;
            } else {
                m_FipGwState = FIP_GW_CONNECTED;
                UpdateGwUniqeStr();
                if (m_IsStateNotificationRequired) {
                    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Gateway %s is now connected !!!\n", m_GwUniqeStr);
                    m_IsStateNotificationRequired = false;
                }
            }
        } else {   // The GW reject our solicit request
            m_FipGwState = FIP_GW_INIT;
            RemoveAllVnics();
            if (!m_IsStateNotificationRequired) {
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Gateway %s is back to Init State due to rejected UC Msg !!!\n", m_GwUniqeStr);
                m_IsStateNotificationRequired = true;
            }
        }
    }

handle_uc_end:
    return Status;
}


NTSTATUS
Gateway::HandleConnectedState( VOID )
{
    NTSTATUS Status;

    Status = HandleUCMsg();
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to HandleUCMsg\n");
        goto handle_connected_end;
    }

    if (NextKA() == 0) {
        Status = SendSolicitMsg();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to SendSolicitMsg\n");
            goto handle_connected_end;
        }
    }


    // ************ Vnic Handling ***************
    // After recv msg some paramters might be changed
    if ((m_HostAdminVnicsCounter < g_pFipGlobals->NumHostVnics) ||
        (m_NetworkAdminVnicsCounter < m_NumNetVnics)) {
        Status = CreateVnics();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to SendSolicitMsg\n");
            goto handle_connected_end;
        }
    }
/*
    // TODO: Hadle destroy Vnics
    if ((m_HostAdminVnicsCounter > g_pFipGlobals->NumHostVnics) ||
        (m_NetworkAdminVnicsCounter > m_NumNetVnics)) {
        DestroyVnics();
    }
*/

    Status = RunAllVnics();
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to RunAllVnics\n");
        goto handle_connected_end;
    }

handle_connected_end:
    return Status;
}


/*
 * This Function we be called from port class.
 * If it was called then we have to understand the reason
 * and behave accordingly.
 * We parse only one msg at a time.
 * This is the main function to hadle GW that handle those cases:
 *      1. Wait for Mcast adv
 *      2. Send Soloicit (UC)
 *      3. Wait for response (UC).
 *      4. Wait for UC adv for configuration changes
 *      5. Send KA.
 *      6 . Create Vnics 
 *      7. Destory Vnics
 *      8. Run Vnic FSM
 */
NTSTATUS
Gateway::RunFSM( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(m_Shutdown == false);
    ASSERT(m_FipGwState != FIP_GW_PREINIT); // Init method was not called

    switch (m_FipGwState) {
    case FIP_GW_INIT:
        Status = HandleMcastMsg();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to HandleUCMsg\n");
        }
        break;
    case FIP_GW_ACK:
        Status = HandleUCMsg();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to HandleUCMsg\n");
        }
        break;
    case FIP_GW_MODIFY:
        // TODO: We have to decide what to do..
//        break;
    case FIP_GW_CONNECTED:
        Status = HandleConnectedState();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to HandleConnectedState\n");
        }
        break;
    }


    return Status;
}


/*
 * Receive a pointer to Advertise message and parse it
 */
NTSTATUS
Gateway::ParseAdvMsg(
    PVOID pAdvMsg,
    ULONG Length
    )
{
    ASSERT(m_Shutdown == false);
    // TODO: Shoud we overwrite existing params or compare it
    if (Length != (sizeof(FipAvd_t) + GRH_LEN))
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Dump packet:"
        "unexpected size. length %d expected %d\n",
             (int)Length, (int)(sizeof(FipAvd_t) + GRH_LEN));
        return STATUS_INVALID_BUFFER_SIZE;
    }

    FipAvd_t *Msg = (FipAvd_t *)((UCHAR *)pAdvMsg + GRH_LEN);

    ASSERT(cl_ntoh16(Msg->fip.opcode) == EOIB_FIP_OPCODE);

    if (Msg->fip.type.type != FIP_FIP_HDR_TYPE ||
        Msg->fip.type.length != FIP_FIP_HDR_LENGTH||
        Msg->base.type.type != FIP_BASIC_TYPE ||
        Msg->base.type.length != FIP_BASIC_LENGTH ||
        Msg->FipGwInfo.type.type != FIP_ADVERTISE_TYPE_1 ||
        Msg->FipGwInfo.type.length != FIP_ADVERTISE_LENGTH_1 ||
        Msg->gw_info.type.type != FIP_ADVERTISE_GW_TYPE ||
        Msg->gw_info.type.length != FIP_ADVERTISE_GW_LENGTH ||
        Msg->ka_info.type.type != FIP_ADVERTISE_KA_TYPE ||
        Msg->ka_info.type.length != FIP_ADVERTISE_KA_LENGTH) 
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, 
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
        return STATUS_NDIS_INVALID_PACKET;
    }

    m_Flags |= (Msg->FipGwInfo.flags & FIP_ADVERTISE_HOST_VLANS) ? FIP_HADMINED_VLAN : 0;

    u16 HostFlag = cl_ntoh16(Msg->fip.flags);
    if (HostFlag & FIP_FIP_SOLICITED_FLAG) {
        m_AvailForLogin = (HostFlag & FIP_FIP_ADVRTS_FLAG) ? true : false;
    }

    m_Qpn       = cl_ntoh32(Msg->base.qpn) & FIP_LOGIN_QPN_MASK;
    m_Lid       = cl_ntoh16(Msg->base.lid);
    m_PortId    = (u16)(cl_ntoh16(Msg->base.sl_port_id) & FIP_ADVERTISE_GW_PORT_ID_MASK);
    m_Sl        = (u8)(cl_ntoh16(Msg->base.sl_port_id) >> FIP_ADVERTISE_SL_SHIFT);

    memcpy(m_Guid, Msg->base.guid, sizeof(m_Guid));

    m_NumNetVnics  = (u16)(cl_ntoh16(Msg->FipGwInfo.n_rss_qpn__num_net_vnics) & FIP_ADVERTISE_NUM_VNICS_MASK);

    m_RssQpn    = (u16)(cl_ntoh16(Msg->FipGwInfo.n_rss_qpn__num_net_vnics) >> FIP_ADVERTISE_N_RSS_SHIFT);
    m_AcceptHostAdminVnic= (Msg->FipGwInfo.flags & FIP_ADVERTISE_HOST_EN_MASK)? true: false;

    memcpy(m_SystemGuid, Msg->gw_info.system_guid, sizeof(m_SystemGuid));

    MemcpyStr((PCHAR)m_VendorId, (PCHAR)Msg->FipGwInfo.vendor_id, sizeof(m_VendorId));
    MemcpyStr((PCHAR)m_SystemName, (PCHAR)Msg->gw_info.system_name, sizeof(m_SystemName));
    MemcpyStr((PCHAR)m_PortName, (PCHAR)Msg->gw_info.gw_port_name, sizeof(m_PortName));

    ULONG ka_time;

    ka_time = cl_ntoh32(Msg->ka_info.gw_adv_period);
    ka_time = ka_time ? ka_time : (ULONG)m_AdvPeriod;
    /* do not let KA go under 2 secs */
    ka_time = (ka_time <= 2000) ? 2000 : ka_time;
    m_AdvPeriod = ka_time;

    ka_time = cl_ntoh32(Msg->ka_info.gw_period);
    ka_time = ka_time ? ka_time : FKA_ADV_PERIOD;
    m_KAPeriod = ka_time;

    ka_time = cl_ntoh32(Msg->ka_info.vnic_ka_period);
    ka_time = ka_time ? ka_time : FKA_ADV_PERIOD;
    m_VnicKAPeriod = ka_time;

    FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_GW,
             "FIP-GW Parse Msg: qpn %d, lid %d, "
             "system_guid %016I64x gw_guid %016I64x port_id %d, vendor %s\n", 
             m_Qpn,
             m_Lid,
             *(u64*)m_SystemGuid,
             *(u64*)m_Guid,
             m_PortId,
             (char *)m_VendorId);

return STATUS_SUCCESS;
}


VOID
Gateway::PrepareSolicitMsg(
    FipSoloicit_t *Msg
    )
{
    ASSERT(Msg != NULL);
//    ib_port_attr_t* pPortAttr = &m_pCaAttributes->p_port_attr[m_PortNumber];
//    FipSoloicit_t *Msg = (FipSoloicit_t *)MsgToPrepare;
    memset(Msg, 0, sizeof(FipSoloicit_t));

    Msg->version.version = 0;

    // Fip Header
    Msg->fip.opcode         = cl_hton16(EOIB_FIP_OPCODE);
    Msg->fip.subcode        = FIP_HOST_SOL_SUB_OPCODE;
    Msg->fip.list_length    = cl_hton16((sizeof(FipDiscoverBase_t) >> 2)); // cl_hton16(FIP_CTRL_HDR_MSG_LEGTH);
    Msg->fip.type.type      = FIP_FIP_HDR_TYPE;
    Msg->fip.type.length    = FIP_FIP_HDR_LENGTH;
    memcpy(Msg->fip.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(Msg->fip.vendor_id));

    // Base Solicit Msg
    Msg->base.type.type     = FIP_BASIC_TYPE;
    Msg->base.type.length   = FIP_BASIC_LENGTH;
    memcpy(Msg->base.vendor_id, x_FIP_VENDOR_ID_MELLANOX, sizeof(Msg->base.vendor_id));

    Msg->base.qpn           = m_SendRecv->GetQpn();
    Msg->base.sl_port_id    = cl_hton16((u16)(m_PortId & FIP_PORT_ID_MASK)); // SL must be 0 (BX-PRM 2.4.2.3 P.81)
    Msg->base.lid           = cl_hton16(m_LocalLid);

    memcpy(Msg->base.guid, &m_LocalPortGuid, sizeof(Msg->base.guid));
}


NTSTATUS
Gateway::SendSolicitMsg( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    FipSoloicit_t FipSolMsg;

    ASSERT(m_Shutdown == false);

    if (!m_SendRecv->IsAvValid()) {  // Create AV only one time
        Status = m_SendRecv->SetAndCreateAV(m_Lid);
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed to SetAndCreateAV\n");
            goto end_send_sol_msg;
        }
    }

    PrepareSolicitMsg(&FipSolMsg);

    Status = m_SendRecv->Send(NULL, cl_hton32(m_Qpn), (PCHAR)&FipSolMsg, sizeof(FipSolMsg));
    if (!NT_SUCCESS(Status))
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_GW, "Failed Sending Solicit Message\n");
        goto end_send_sol_msg;
    }

    // 0..100ms random time bofore sending next period msg, BX-PRM 2.7.2 P.113
    m_LastKAPeriod  = GetTickCountInMsec() + (GetTickCountInMsec() % RANDOM_TIME_PERIOD);

end_send_sol_msg:
    return Status;
}


VOID
Gateway::AddMessage(
    RcvMsg *pRcvMsg,
    UniqeIdentifyParams_t *UniqeParams
    )
{
    bool FoundMatch = false;

    ASSERT(m_Shutdown == false);
    switch (UniqeParams->MsgType) {
    case ADV_MSG:
        m_IncomingMsgHead.InsertTailList(&pRcvMsg->m_ExtenalEntry);
        FoundMatch = true;
        break;
    case LOGIN_ACK_MSG:
        for (int i = 0; i < Vnics.GetMaxSize(); i++) {
            if ((Vnics[i] != NULL) &&
                (Vnics[i]->IsMatch(UniqeParams))) {
                Vnics[i]->AddMessage(pRcvMsg);
                FoundMatch = true;
                break;
            }
        }
        break;
    default:
        break;
    }
    ASSERT(FoundMatch);
}


RcvMsg*
Gateway::PeekMessage( VOID )
{
    ASSERT(m_IncomingMsgHead.Size() != 0);

    RcvMsg * pRcvMsg = CONTAINING_RECORD(m_IncomingMsgHead.Head(),RcvMsg,m_ExtenalEntry);
    return pRcvMsg;
}

VOID
Gateway::RemoveMessage(
    RcvMsg *pRcvMsg
    )
{
    m_IncomingMsgHead.RemoveEntryList(&pRcvMsg->m_ExtenalEntry);
    m_SendRecv->ReturnBuffer(pRcvMsg);
}


VOID
Gateway::RemoveAllMessage( VOID )
{
    RcvMsg *pRcvMsg = NULL;

    while (m_IncomingMsgHead.Size() != 0) {
        RemoveMessage(pRcvMsg);
    }
}



