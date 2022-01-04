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
    fip_gw.h

Abstract:
    This module contains miniport Gateway routines

Revision History:

Notes:

Author:
    Sharon Cohen

--*/

#pragma once

const int MAX_VNIC_PER_PORT    = 4096;
const int FIRST_VNIC_SN        = 1;

typedef enum FipGwState_t
{
    FIP_GW_PREINIT = 0, // Class was created
    FIP_GW_INIT,        // MC advertise received
    FIP_GW_ACK,         // UC advertise received
    FIP_GW_MODIFY,      // UC advertise received - Modify some parameters
    FIP_GW_CONNECTED    // UC advertise accept
};


class Gateway
{
public:
    Gateway() : // Zeros all parameters
        m_FipGwState(FIP_GW_PREINIT),
        m_IsStateNotificationRequired(true),
        m_Shutdown(false),
        m_NetworkAdminVnicsCounter(0),
        m_HostAdminVnicsCounter(0),
        m_AdvPeriod(0),
        m_KAPeriod(0),
        m_VnicKAPeriod(0),
        m_LastAdvPeriod(0),
        m_LastKAPeriod(0),
        m_Flags(0),
        m_Qpn(0),
        m_Lid(0),
        m_PortId(0),
        m_NumNetVnics(0),
        m_RssQpn(0),
        m_LocalEnumMTU(0),
        m_Sl(0),
        m_AcceptHostAdminVnic(false),
        m_AvailForLogin(false),
        m_LocalLid(0),
        m_LocalPortGuid(0),
        m_LocalPortNumber(0),
        m_Qkey(0),
        m_LocalPkeyIndex(0),
        m_VnicSerialNumber(FIRST_VNIC_SN),  // Don't want to start from Zero
        m_SendRecv(NULL),
        m_pBusIbInterface(NULL),
        m_MsgArrivedEvent(NULL),
        m_CqErrorEvent(NULL)
        {
            memset(m_SystemGuid, 0, sizeof(m_SystemGuid));
            memset(m_SystemName, 0, sizeof(m_SystemName));
            memset(m_PortName, 0, sizeof(m_PortName));
            memset(m_Guid, 0, sizeof(m_Guid));
            memset(m_VendorId, 0, sizeof(m_VendorId));
            memset(m_GwUniqeStr, 0, sizeof(m_GwUniqeStr));
            m_IncomingMsgHead.Init();
        }

    ~Gateway() { ASSERT(m_Shutdown); }

    NTSTATUS
    Init(
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
        );

    bool IsGwTimeOut( VOID );

    int CallNextTime( VOID );

    NTSTATUS RunFSM( VOID );

    VOID Shutdown();

    char* GetGwUniqeStr( VOID ) { return m_GwUniqeStr; }

    bool IsMatch(UniqeIdentifyParams_t *UniqeParams);


    VOID AddMessage( 
        RcvMsg *pRcvMsg,
        UniqeIdentifyParams_t *UniqeParams
        );

private:
    int NextKA( VOID );

    int NextGwTimeOut( VOID );

    int NextVnicKA( VOID );

    bool IsMcastMsg(
        RcvMsg *pRcvMsg
        );

    VOID UpdateGwUniqeStr ( VOID );

    NTSTATUS HandleMcastMsg( VOID );

    NTSTATUS HandleUCMsg( VOID );

    NTSTATUS HandleConnectedState( VOID );

    NTSTATUS ParseMcastMsg( 
        PVOID pAdvMsg,
        ULONG Length
        );

    NTSTATUS ParseUCMsg( 
        RcvMsg *pRcvMsg
        );

    NTSTATUS CreateVnics( VOID );

    VOID DestroyVnics( VOID );

    NTSTATUS
    ParseAdvMsg(
        PVOID pAdvMsg,
        ULONG length
        );

    VOID
    PrepareSolicitMsg(
        FipSoloicit_t *Msg
        );

    NTSTATUS 
    SetAndCreateAV(
        ib_av_handle_t *ph_av
        );
        
    NTSTATUS SendSolicitMsg( VOID );

    u32 FindNextAvailVnicId( VOID );

    u32 FindNextVnicId( VOID );

    NTSTATUS
    AddVnic(
        VnicType_t VnicType,
        bool ValidMac,
        u8 Mac[HW_ADDR_LEN],
        bool ValidVlan,
        u16 Vlan
        );

    VOID
    RemoveVnic( 
        Vnic *VnicToRemove
        );

    VOID RemoveAllVnics( VOID );

    NTSTATUS RunAllVnics( VOID );

    RcvMsg* PeekMessage( VOID );

    VOID
    RemoveMessage(
        RcvMsg *pRcvMsg
        );

    VOID RemoveAllMessage( VOID );

    /*** Gateway Params ***/
    /*
     * These 3 Params hold informative info about the GW that can change without
     * implications on GW or vnic logic (only reported to user)
     */
    u8 m_SystemGuid[GUID_LEN];
    u8 m_SystemName[VNIC_SYSTEM_NAME_LEN + 1];
    u8 m_PortName[VNIC_GW_PORT_NAME_LEN + 1];

    u64 m_AdvPeriod;      /* timeout in msec */
    u64 m_KAPeriod;       /* timeout in msec */
    u64 m_VnicKAPeriod;
    u64 m_LastAdvPeriod;    /* timeout in msec */
    u64 m_LastKAPeriod;     /* in msec */
    int m_Flags;
    u32 m_Qpn;
    u16 m_Lid;
    u16 m_PortId;
    u16 m_NumNetVnics;
    u16 m_RssQpn;
    u8 m_LocalEnumMTU;
    u8 m_Sl;
    bool m_AcceptHostAdminVnic; // Whether the GW Accept Host-Admin Vnic
    bool m_AvailForLogin; // Whether the GW reject or accept my solicitation
    u8 m_VendorId[VNIC_VENDOR_LEN + 1];
    u8 m_Guid[GUID_LEN];

    /*** Local Params ***/
    FipGwState_t m_FipGwState;
    bool m_IsStateNotificationRequired;
    bool m_Shutdown;
    ULONG m_NetworkAdminVnicsCounter;
    ULONG m_HostAdminVnicsCounter;
    char m_GwUniqeStr[UNIQE_GW_LEN + 1];

    u8 m_LocalPortNumber;
    ib_net16_t m_LocalLid;
    ib_net64_t m_LocalPortGuid;
    ib_net32_t m_Qkey;
    uint16_t m_LocalPkeyIndex;
    u32 m_VnicSerialNumber; // only 15 LSB is valid
    PMLX4_BUS_IB_INTERFACE m_pBusIbInterface;

    // SendRecv Parameters
    SendRecv *m_SendRecv;
    KEVENT *m_MsgArrivedEvent;
    KEVENT *m_CqErrorEvent;

    // we will hold a list of all Vnic for this GW
    Array <Vnic *> Vnics;
    static const int MAXDEBUGVNICS = 4;
    Vnic *FipVnics[MAXDEBUGVNICS]; // For debug only

    LinkedList  m_IncomingMsgHead;  // we'll handle a list for all the waiting to process (to the user)

};
