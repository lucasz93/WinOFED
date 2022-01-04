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

typedef enum {
    VHUB_INIT = 0,
    VHUB_UP2DATE,
} VhubTableState_t;

typedef enum {
    VHUB_SUCCESS = 0,
    VHUB_NOT_UPDATE_YET,
    VHUB_NEED_RETRY
} VhubResult_t;

typedef enum {
    VHUB_MGID_DATA = 0,
    VHUB_MGID_UPDATE = 2,
    VHUB_MGID_TABLE = 3
} VhubMgidType_t;

typedef enum {
    VHUB_MIDDLE = 0,
    VHUB_FIRST,
    VHUB_LAST,
    VHUB_ONLY
} VhubHDR_t;

typedef enum MsgType_t {
    UNKNOWN_MSG = 0,
    ADV_MSG,
    LOGIN_ACK_MSG,
    VHUB_UPDATE_MSG,
    VHUB_TABLE_MSG
};

typedef struct {    // These are Vhub-Ctx-Table-Entry after parsing
    MsgType_t   MsgType;    // Can be Update or Table Msg
    u8          Len;
    u8          EportState;
    bool        IsVlanPresent;
    u32         VhubId;
    u32         Tusn;
    VhubHDR_t   Hdr;
    u16         TableSize;
} VhubMsgParams_t;

/* 
 * This class does not have to save any copy of the Vnic-list
 * that came from the Vhub-Table.
 * Also this is a 1 to 1 'Vnic - Vhub Table'.
 */
class VhubTable {
public:
    VhubTable( VOID ) :
        // General Params
        m_Shutdown(true),
        m_VhubResult(VHUB_NEED_RETRY),
        m_VhubTableState(VHUB_INIT),
        m_ClearTable(false),
        m_FirstTimeUpdateTusn(true),

        // Vnic Params
        m_pDataInterfaceVnic(NULL),
        m_VnicSendRecv(NULL),
        m_VhubId(0),
        m_IsVnicExist(false),

        // Context Vhub Table Params
        m_Tusn(0),
        m_TableSize(0),
        m_CurrTableSize(0),
        m_VnicCtxArr(NULL),
        m_CheckSum(0)

        {
            m_IncomingMsgHead.Init();  // we'll handle a list for all incoming vhub-msg
            memset(m_Mac, 0, sizeof(*m_Mac));
            memset(m_UniqeStr, 0, sizeof(m_UniqeStr));
        }

    ~VhubTable() { ASSERT(m_Shutdown == true); }

    NTSTATUS Init(
        u32 VhubId,
        u8  Mac[ETH_ALEN],
        char *UniqeStr,
        InterfaceVnic *pDataInterfaceVnic,
        SendRecv *pSendRecv
        );

    VOID Shutdown();

    NTSTATUS RunFSM( VOID );

    u32 GetLastTusn( VOID );

    bool IsUp2date( VOID );

    bool NeedUpdate( VOID );

    bool IsVnicExist( VOID );

    VOID AddMessage( RcvMsg *pRcvMsg );

private:

    VOID RequestNewVhubTable( VOID );

    VOID CleanVhubTable( VOID );

    NTSTATUS ParseVhubHeaders(
        RcvMsg *pRcvMsg,
        VhubMsgParams_t *pVhubMsgParams
        );

    VOID UpdateCheckSum(
        FipContextEntry_t *Ctx
        );

    NTSTATUS ParseVhubMsg(
        RcvMsg *pRcvMsg,
        VhubMsgParams_t *VhubMsgParams
        );

    VOID CheckVnicExistance(
        MsgType_t MsgType
        );

    NTSTATUS UpdateInterfaceForAllVhubEntries( VOID );

    NTSTATUS HandleVhubTable( VOID );

    NTSTATUS HandleVhubUpdate( VOID );

    RcvMsg* PeekNextMessage(
        RcvMsg *pInRcvMsg
        );

    RcvMsg* RemoveCurrMsgAndPeekNextMsg(
        RcvMsg *pInRcvMsg
        );

    VOID
    RemoveMessage(
        RcvMsg *pRcvMsg
        );

    VOID RemoveAllMessage( VOID );

    // General Params
    bool                m_Shutdown;
    VhubResult_t        m_VhubResult;
    VhubTableState_t    m_VhubTableState;
    bool                m_ClearTable;   // After Parsing the whole table we have to clear it (after using it).
    bool                m_FirstTimeUpdateTusn;

    // Incoming Msg list
    LinkedList          m_IncomingMsgHead;  // we'll handle a list for all incoming vhub-msg

    // Vnic Params
    InterfaceVnic       *m_pDataInterfaceVnic;
    SendRecv            *m_VnicSendRecv;
    u32                 m_VhubId;    // The VnicId of the Vnic who created this Class
    u8                  m_Mac[ETH_ALEN];
    bool                m_IsVnicExist; // Once we get new Update\table we verify whether the VnicId exist
    char                m_UniqeStr[UNIQE_GW_LEN + 1]; // For DBG only

    // Context Vhub Table Params
    u32                     m_Tusn;
    u16                     m_TableSize;    // Total Expected Size
    u16                     m_CurrTableSize;    // Acual Table Size
    VnicCtxEntry_t          *m_VnicCtxArr;
    u32                     m_CheckSum;
};
