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

const int VNIC_KA_PERIOD = 1000;
const int VNIC_ID_LEN    = 16;
const int VNIC_HOST_BIT  = 0x8000;
const int VNIC_HOST_MASK = 0x7FFF;
const u8 MAX_LOGIN_RETRY = 3;

#define CREATE_VHUB_ID(be_vlan, port_id) \
    (((be_vlan) & 0xFFF) | (((port_id) & 0xFFF) << 12))

#define cpu_to_be32(a)		_byteswap_ulong((ULONG)(a))

typedef struct {
    u8 PortName[VNIC_GW_PORT_NAME_LEN + 1];
    u8 SystemGuid[GUID_LEN];
    u8 SystemName[VNIC_SYSTEM_NAME_LEN + 1];
} UniqeGwParams_t;

typedef struct {
    u8 Mac[HW_ADDR_LEN];
    u16 VnicId;
    u8 EthGidPrefix[GID_PREFIX_LEN];
    u8 Name[VNIC_NAME_LEN + 1];
} UniqeVnicParams_t;

typedef struct {
    MsgType_t MsgType;  // According to Msg Type we'll know which Struct to read
    u16 PortId;
    u8 Guid[GUID_LEN];

    UniqeGwParams_t Gw; // We could have used union but we want an easy debugging
    UniqeVnicParams_t Vnic;
} UniqeIdentifyParams_t; 

typedef enum VnicType_t {
    UNDEFINED_VNIC = 0,
    HOST_VNIC,
    NETWORK_VNIC
};

typedef enum VnicState_t {
    HADMIN_IDLE = 0,
    LOGIN,
    WAIT_FOR_ACK,
    LOGIN_REJECT,
    JOIN_VHUB_MCASTS,
    REQUEST_VHUB_TABLE,
    VHUB_TABLE_INIT,
    CONNECTED,
    WAIT_TO_BE_KILLED
};

typedef enum VnicMsgType_t {
    LOGOUT = 0,
    KEEP_ALIVE,
    ASK_FOR_VHUB_TABLE
};

const u8 EmptyMac[HW_ADDR_LEN] = { 0 };

VOID
mcast_cb(
    ib_mcast_rec_t *p_mcast_rect
    );

class Vnic
{
public:
    Vnic( VOID ) :
        /* Local Params */
        m_VnicState(HADMIN_IDLE),
        m_Shutdown(false),
        m_VnicType(UNDEFINED_VNIC),
        m_VnicKAPeriod(0),
        m_LastKAPeriod(0),
        m_LastMcastTime(0),
        m_LoginRetry(0),
        m_LocalLid(0),
        m_LocalPortGuid(0),
        m_Qkey(0),
        m_LocalPkeyIndex(0),
        m_pBusIbInterface(NULL),
        m_DidWeInitVhubTable(false),
        m_IsVnicAlive(true),    // Until Vhub-Table tell us different
        /* GW Params */
        m_Lid(0),
        m_PortId(0),
        m_QpsNum(0),
        m_LocalEnumMTU(0),

        /* Vnic Params */
        m_ValidMac(false),
        m_ValidVlan(false),
        m_Vlan(0),
        m_VnicId(0),
        m_LocalCtrlQpn(0),
        m_CtrlQpn(0),
        m_DataQpn(0),
        m_LocalDataQpn(0),
        m_Sl(0),
        m_VHubId(0),
        m_NMacMcgid(0),
        m_NRssMgid(0),
        m_Pkey(0),
        m_PkeyIndex(0),
        m_Mtu(0),
        m_Tusn(0),
        m_UniqueId(0),
        m_NeedVhubTable(true),
        m_Syndrome(FIP_SYNDROM_SUCCESS),
        m_PortSendRecv(NULL),
        m_McastTable(NULL),
        m_McastTableConnected(false),
        m_McastUpdate(NULL),
        m_McastUpdateConnected(false),
        m_WasEoIBInterfaceCreated(false),
        m_DataLocation(0),
        m_pDataInterfaceVnic(NULL)
        {
            memset(m_VendorId, 0, sizeof(*m_VendorId));
            memset(m_Mac, 0, sizeof(*m_Mac));
            memset(m_GwGuid, 0, sizeof(*m_GwGuid));
            memset(m_Name, 0, sizeof(*m_Name));
            memset(m_EthGidPrefix, 0, sizeof(*m_EthGidPrefix));
            memset(m_GwPortName, 0, sizeof(m_GwPortName));
            memset(m_UniqeIFName, 0, sizeof(m_UniqeIFName));

            memset(&m_McastTableData, 0, sizeof(m_McastTableData));
            memset(&m_McastTableAattach, 0, sizeof(m_McastTableAattach));
            memset(&m_TableVhubMgid, 0, sizeof(m_TableVhubMgid));

            memset(&m_McastUpdateData, 0, sizeof(m_McastUpdateData));
            memset(&m_McastUpdateAattach, 0, sizeof(m_McastUpdateAattach));
            memset(&m_UpdateVhubMgid, 0, sizeof(m_UpdateVhubMgid));

            m_IncomingMsgHead.Init();
        }

    ~Vnic( VOID ) { ASSERT(m_Shutdown == true); }

    NTSTATUS Init( 
        VnicType_t VnicType,
        u64 VnicKAPeriod,
        u16 LocalLid,
        u64 LocalPortGuid,
        u32 Qkey,
        u16 LocalPkeyIndex,
        u16 Lid,
        u32 Qpn,
        u16 PortId,
        u16 m_QpsNum, // Holds the n_rss_qps from the GW
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
        );

    VOID Shutdown( bool CallImplicitLogout  = true);

    u64 CallNextTime( VOID );

    bool IsVnicTimeOut( VOID );

    bool IsMatch( UniqeIdentifyParams_t *UniqeParams );

    NTSTATUS RunFSM( VOID );

    u16 GetVnicId( VOID ) { return m_VnicId; }

    VnicType_t GetVnicType( VOID ) { return m_VnicType; }

    VOID JoinMcastCallBack(
        McastMessageData *pMcastMessageData
        );

    bool IsVnicAlive( VOID );

    VOID AddMessage( RcvMsg *pRcvMsg );

private:

    VOID UpdateUniqeStr( VOID );

    NTSTATUS AddVnicInterface( VOID );

    VOID RemoveVnicInterface( VOID );

    VOID LeaveAndDestroyMcast( VOID );

    NTSTATUS PreLogin( VOID );

    NTSTATUS JoinMcastForVhubMsg( VOID );

    NTSTATUS RecvVhubUpdate( VOID );

    VOID PrepareLoginMsg(
        FipLogin_t *pFipLogin
        );
    
    NTSTATUS Login( VOID );

    NTSTATUS ParseLoginMsg( 
        PVOID LoginMsg,
        UINT Length
        );

    NTSTATUS RecvAck( VOID );

    VOID PrepareVnicMsg(
        VnicMsgType_t MsgType,
        FipHostUpdate_t *FipHostUpdate
    );

    NTSTATUS SendGenericMsg(VnicMsgType_t MsgType);

    VOID Logout( VOID );

    NTSTATUS SendKA( VOID );

    NTSTATUS SendMsg( VOID );

    VOID CreateVhubMgid(
        VhubMgidType_t VhubMgidType,
        VhubMgid_t *VhubMgid
        );

    VOID FillPortMcastReq(
        ib_mcast_req_t* pMcastReq,
        void* mgid,
        bool fCreate,
        void* context
        );

    NTSTATUS RequestVHubTable( VOID );

    RcvMsg* PeekMessage( VOID );

    VOID
    RemoveMessage(
        RcvMsg *pRcvMsg
        );

    VOID RemoveAllMessage( VOID );

    /* Local Params */
    VnicState_t         m_VnicState;
    bool                m_Shutdown;
    VnicType_t          m_VnicType;
    SendRecv            m_SendRecv;    // For Ctrl only
    VhubTable           m_VhubTable;
    bool                m_DidWeInitVhubTable;   // To prevent init Vhub Table more than one time
    bool                m_IsVnicAlive;

    u64                 m_VnicKAPeriod;
    u64                 m_LastKAPeriod;     /* in msec */
    u64                 m_LastMcastTime;
    u8                  m_LoginRetry;   // After Num of Retry we'll close this vnic

    u16                 m_LocalLid;
    u64                 m_LocalPortGuid;
    u32                 m_Qkey;
    u16                 m_LocalPkeyIndex;

    PMLX4_BUS_IB_INTERFACE m_pBusIbInterface;

    /* GW Params */
    u16                 m_Lid;
    u32                 m_CtrlQpn;
    u32                 m_DataQpn;
    u16                 m_Guid;
    u16                 m_PortId;
    u16                 m_QpsNum; // Holds the n_rss_qps from the GW
    u8                  m_LocalEnumMTU; 
    SendRecv            *m_PortSendRecv;
    u8                  m_GwPortName[VNIC_GW_PORT_NAME_LEN + 1]; // For DBG only
    char                m_UniqeStr[UNIQE_GW_LEN + 1]; // For DBG only

    /* Vnic Params */
    LoginSyndrom_t      m_Syndrome;
    u8                  m_VendorId[VNIC_VENDOR_LEN + 1];
    bool                m_ValidMac;
    u8                  m_Mac[ETH_ALEN];
    bool                m_ValidVlan;
    u16                 m_Vlan; // Only 12 Bit
    u16                 m_VnicId;   // A uniqe ID (along with GW-Guid)
    u8                  m_GwGuid[GUID_LEN];
    u8                  m_Name[VNIC_NAME_LEN + 1];  // Vnic Name
    u8                  m_EthGidPrefix[GID_PREFIX_LEN];
    u32                 m_LocalCtrlQpn;
    int                 m_LocalDataQpn;
    u8                  m_Sl;
    u32                 m_VHubId;
    u8                  m_NMacMcgid;
    u8                  m_NRssMgid;
    u16                 m_Pkey;
    u16                 m_PkeyIndex; // Need to find Index from the Pkey itself in the pkey-table
    u16                 m_Mtu;
    u32                 m_Tusn;

    //  Eoib vnic intrface
    bool                m_WasEoIBInterfaceCreated;
    u32                 m_DataLocation;
    InterfaceVnic *     m_pDataInterfaceVnic;
    u64                 m_UniqueId;
    char                m_UniqeIFName[MAX_USER_NAME_SIZE + 1];

    // Vhub handling paramters
    bool                m_NeedVhubTable;   // In case we get a VHub table with some missing parts we'll request new table

    bool                m_McastTableConnected;
    al_attach_handle_t  m_McastTableAattach;
    FipThreadMessage    m_McastTableData;
    VhubMgid_t          m_TableVhubMgid;
    mcast_mgr_request_t *m_McastTable;

    bool                m_McastUpdateConnected;
    al_attach_handle_t  m_McastUpdateAattach;
    FipThreadMessage    m_McastUpdateData;
    VhubMgid_t          m_UpdateVhubMgid;
    mcast_mgr_request_t *m_McastUpdate;

    LinkedList          m_IncomingMsgHead;  // we'll handle a list for all the waiting to process (to the user)
};

