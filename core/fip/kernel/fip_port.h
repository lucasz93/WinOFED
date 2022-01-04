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
    fip_port.h

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/


#pragma once

const u32 VNIC_FIP_QKEY =0x80020002;

enum PORT_STATE {
    PORT_STATE_INIT = 0,
    PORT_STATE_JOINING,
    PORT_STATE_CONNECTED
};


class FipPort {

public:

    FipPort();

    ib_net64_t m_PortGuid;

    NTSTATUS Init(
        PDEVICE_OBJECT pDeviceObj,
        KEVENT *PortEvent
        );

    VOID Shutdown();


    int NextCallTime();

    VOID PortSM();

    VOID JoinMcastCallBack(McastMessageData *pMcastMessageData);

    VOID PreInit();

private:

    MsgType_t IdentifyMsg(
        PVOID pAdvMsg,
        ULONG Length
        );

NTSTATUS GetParamsFromAdvMsg(
        PVOID pAdvMsg,
        ULONG Length,
        u16 *PortId,
        u8 PortName[VNIC_GW_PORT_NAME_LEN],
        u8 Guid[GUID_LEN],
        u8 SystemGuid[GUID_LEN],
        u8 SystemName[VNIC_SYSTEM_NAME_LEN]);

    NTSTATUS GetParamsFromAdvMsg(
        PVOID pAdvMsg,
        ULONG Length,
        UniqeIdentifyParams_t *Params
        );


    VOID FillPortMcastReq(
        ib_mcast_req_t* pMcastReq,
        void* mgid,
        bool fCreate,
        VOID* context
        );


    NTSTATUS JoinMcasts();

    NTSTATUS SendSolicited();

    VOID HandleRecvMessage();

    VOID HandleGWTimeOut( VOID );

    VOID DestroyGateway( int GwIdx );

    static const int MAXDEBUGGWS = 4;


    Gateway *FipGateWays[MAXDEBUGGWS]; // For debug only


    PORT_STATE              m_State;
    bool                    m_Shutdown;
    u64                     m_LastMcastTime;
    u64                     m_LastSendTime;
    KEVENT                  *m_PortEvent;

    // Params to get Bus Interface
    bool                    m_BusInterfaceUpdated;
    PDEVICE_OBJECT          m_pDeviceObj;
    MLX4_BUS_IB_INTERFACE   m_BusIbInterface;

    u16                     m_Pkey;
    UCHAR                   m_EnumMTU;
    u16                     m_PkeyIndex;
    

    mcast_mgr_request_t     *m_McastSolicit;
    bool                    m_McastSolicitConnected;
    FipThreadMessage        m_McastSolicitData;    
    ib_av_handle_t          m_McastSolicitAv;

    mcast_mgr_request_t     *m_McastDiscover;
    bool                    m_McastDiscoverConnected;
    FipThreadMessage        m_McastDiscoverData;
    al_attach_handle_t      m_McastDiscoverAattach;

    u16                     m_LocalPkeyIndex;   // TODO: Get Pkey idx
    u16                     m_LocalLid;

    SendRecv                m_SenderReciever;
    Array <Gateway *>       FipGws;

    

};


