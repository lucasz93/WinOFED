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

#include "fip_vhub_table.tmh"
#endif


NTSTATUS
VhubTable::Init(
    u32 VhubId,
    u8  Mac[ETH_ALEN],
    char *UniqeStr,
    InterfaceVnic *pDataInterfaceVnic,
    SendRecv *pSendRecv
    )
{
    NTSTATUS Status;

    ASSERT(pDataInterfaceVnic);

    m_FirstTimeUpdateTusn = true;
    m_VhubResult = VHUB_NEED_RETRY; // Vnic should find out that a new Vhub-table is required
    m_IsVnicExist = false;
    m_TableSize = 0;

    m_VhubId = VhubId;
    memcpy(m_Mac, Mac, sizeof(m_Mac));
    m_pDataInterfaceVnic = pDataInterfaceVnic;
    m_VnicSendRecv = pSendRecv;

    Status = RtlStringCbCopyA(
            m_UniqeStr,
            sizeof(m_UniqeStr),
            UniqeStr);
    if (!NT_SUCCESS(Status)) {
        ASSERT(FALSE);
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "RtlStringCbCopyA Failed\n");
        goto init_err;
    }
    m_Shutdown = false;

    return Status;

init_err:
    return Status;
}

VOID
VhubTable::Shutdown( VOID )
{
    NTSTATUS Status;
    ASSERT(m_Shutdown == false);

    m_Shutdown = true;
    CleanVhubTable();
    RemoveAllMessage();
    m_IsVnicExist = false;
    Status = UpdateInterfaceForAllVhubEntries();
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s UpdateInterfaceForAllVhubEntries Failed\n", m_UniqeStr);
    }
}


u32
VhubTable::GetLastTusn( VOID )
{ 
    ASSERT(m_Shutdown == false);
    return m_Tusn;
}


bool
VhubTable::IsUp2date( VOID )
{
    ASSERT(m_Shutdown == false);
    return (m_VhubResult == VHUB_SUCCESS);
}


bool
VhubTable::NeedUpdate( VOID )
{
    ASSERT(m_Shutdown == false);
    return (m_VhubResult == VHUB_NEED_RETRY);
}


bool
VhubTable::IsVnicExist( VOID )
{
    ASSERT(m_Shutdown == false);
    return m_IsVnicExist;
}


inline
VOID
VhubTable::CleanVhubTable( VOID )
{
    if (m_VnicCtxArr != NULL) {
        delete[] m_VnicCtxArr;
        m_VnicCtxArr = NULL;
        m_CurrTableSize = 0;
        m_TableSize = 0;
    }
}

/*
 * Parse the incoming msg to identify whether it's a Vnic-Login-Ack or Adv Msg
 */
NTSTATUS
VhubTable::ParseVhubHeaders(
    RcvMsg *pRcvMsg,
    VhubMsgParams_t *pVhubMsgParams
    )
{
    PVOID pVhubMsg = pRcvMsg->m_AlignedData;
    ULONG Length = pRcvMsg->m_Len;

    memset(pVhubMsgParams, 0, sizeof(*pVhubMsgParams));
    pVhubMsgParams->MsgType = UNKNOWN_MSG;

    // Check for Vhub-Update Msg
    if (Length >= (sizeof(FipVhubUpdate_t) + GRH_LEN)) {
        FipVhubUpdate_t *FipVhubUpdate = (FipVhubUpdate_t*)((UCHAR *)pVhubMsg + GRH_LEN);

        if (cl_ntoh16(FipVhubUpdate->fip.opcode) == EOIB_FIP_OPCODE &&
            FipVhubUpdate->fip.subcode == FIP_GW_UPDATE_SUB_OPCODE &&
            FipVhubUpdate->fip.type.type == FIP_FIP_HDR_TYPE &&
            FipVhubUpdate->fip.type.length == FIP_FIP_HDR_LENGTH &&
            FipVhubUpdate->update.type.type == FIP_VHUB_UP_TYPE) {

            pVhubMsgParams->MsgType = VHUB_UPDATE_MSG;
            pVhubMsgParams->Len = FipVhubUpdate->update.type.length;
            pVhubMsgParams->EportState = (u8)((FipVhubUpdate->update.vhub_id.flags.flags & FIP_VHUB_UP_EPORT_MASK) >> FIP_VHUB_UP_EPORT_SHIFT);
            pVhubMsgParams->IsVlanPresent = (bool)(FipVhubUpdate->update.vhub_id.flags.flags & FIP_VHUB_UP_VP_FLAG);
            pVhubMsgParams->VhubId = (u32)(cl_ntoh32(FipVhubUpdate->update.vhub_id.vhub_id) & FIP_VHUB_ID_MASK);
            pVhubMsgParams->Tusn = cl_ntoh32(FipVhubUpdate->update.tusn);
        }
    }

    // Check for Vhub-Table Msg
    if ((pVhubMsgParams->MsgType == UNKNOWN_MSG) &&
        (Length >= (sizeof(FipVhubTable_t) + GRH_LEN))) {
        FipVhubTable_t *FipVhubTable = (FipVhubTable_t*)((UCHAR *)pVhubMsg + GRH_LEN);

        if (cl_ntoh16(FipVhubTable->fip.opcode) == EOIB_FIP_OPCODE &&
            FipVhubTable->fip.subcode== FIP_GW_TABLE_SUB_OPCODE &&
            FipVhubTable->fip.type.type == FIP_FIP_HDR_TYPE &&
            FipVhubTable->fip.type.length == FIP_FIP_HDR_LENGTH &&
            FipVhubTable->table.type.type == FIP_VHUB_TBL_TYPE) {

            pVhubMsgParams->MsgType = VHUB_TABLE_MSG;
            pVhubMsgParams->Len = FipVhubTable->table.type.length;
            pVhubMsgParams->IsVlanPresent = (bool)(FipVhubTable->table.vhub_id.flags.flags & FIP_VHUB_UP_VP_FLAG);
            pVhubMsgParams->VhubId = (u32)(cl_ntoh32(FipVhubTable->table.vhub_id.vhub_id) & FIP_VHUB_ID_MASK);
            pVhubMsgParams->Tusn = cl_ntoh32(FipVhubTable->table.tusn);
            pVhubMsgParams->Hdr = (VhubHDR_t)(FipVhubTable->table.flags >> FIP_VHUB_TBL_HDR_SHIFT);
            pVhubMsgParams->TableSize = cl_ntoh16(FipVhubTable->table.table_size);
        }
    }
    
    if (pVhubMsgParams->MsgType == UNKNOWN_MSG) {
        ASSERT(FALSE);
        return STATUS_NDIS_INVALID_PACKET;
    }

    return STATUS_SUCCESS;
}


inline
VOID
VhubTable::UpdateCheckSum(
    FipContextEntry_t *Ctx
    )
{
    for (int i = 0; i < (sizeof(*Ctx) >> 2); i++) {
        m_CheckSum += ((u32 *)Ctx)[i];
    }
}


NTSTATUS
VhubTable::ParseVhubMsg(
    RcvMsg *pRcvMsg,
    VhubMsgParams_t *VhubMsgParams
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    FipContextEntry_t *Ctx = NULL;
    u16 TableSize, Idx;

    // Check for 1st\only msg - we have to save Tusn & TableSize
    if ((VhubMsgParams->MsgType == VHUB_TABLE_MSG) &&
        ((VhubMsgParams->Hdr == VHUB_ONLY) ||
        (VhubMsgParams->Hdr == VHUB_FIRST))) {
        m_TableSize = VhubMsgParams->TableSize;
        m_CurrTableSize = 0;
        m_Tusn = VhubMsgParams->Tusn;

        // Allocate Memory only one per Vhub-Table
        m_VnicCtxArr = NEW VnicCtxEntry_t[m_TableSize];
        if (m_VnicCtxArr == NULL) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Fail to allocate memory for m_VnicCtxArr[%d]!!!\n", m_UniqeStr, m_TableSize);
            goto parse_err;
        }
        memset(m_VnicCtxArr, 0, sizeof(VnicCtxEntry_t) *m_TableSize); // For Debug only
    }

    if (VhubMsgParams->MsgType == VHUB_UPDATE_MSG) {
        Ctx = (FipContextEntry_t*)(pRcvMsg->m_AlignedData + GRH_LEN + sizeof(FipVhubUpdate_t));
        m_Tusn = VhubMsgParams->Tusn;
        TableSize = ((VhubMsgParams->Len << 2) - sizeof(FipVhubUpdateParam_t)) / sizeof(FipContextEntry_t);
    } else {    // Vhub-Table Msg
        Ctx = (FipContextEntry_t*)(pRcvMsg->m_AlignedData + GRH_LEN + sizeof(FipVhubTable_t));
        TableSize = ((VhubMsgParams->Len << 2) - sizeof(FipVhubTableParam_t) - sizeof(m_CheckSum)) / sizeof(FipContextEntry_t);
    }

    if (VhubMsgParams->MsgType == VHUB_UPDATE_MSG) {
        m_TableSize = TableSize;
        m_CurrTableSize = 0;

        // Allocate Memory only one per Vhub-Table
        m_VnicCtxArr = NEW VnicCtxEntry_t[m_TableSize];
        if (m_VnicCtxArr == NULL) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Fail to allocate memory for m_VnicCtxArr[%d]!!!\n", m_UniqeStr, m_TableSize);
            goto parse_err;
        }
        memset(m_VnicCtxArr, 0, sizeof(VnicCtxEntry_t) *m_TableSize); // For Debug only
    }


    for (Idx = 0; Idx < TableSize; Idx++, Ctx++) {
#if 0        // CheckSum is not supported by BX Yet
        UpdateCheckSum(Ctx);
#endif
        m_VnicCtxArr[m_CurrTableSize].Valid = !!(Ctx->flags & FIP_VHUB_V_FLAG);
        m_VnicCtxArr[m_CurrTableSize].Rss = !!(Ctx->flags & FIP_VHUB_RSS_FLAG);
        m_VnicCtxArr[m_CurrTableSize].Lid = cl_ntoh16(Ctx->lid);
        m_VnicCtxArr[m_CurrTableSize].Qpn = (u32)(cl_ntoh32(Ctx->qpn) & FIP_LOGIN_QPN_MASK);
        m_VnicCtxArr[m_CurrTableSize].Sl = (u8)(Ctx->sl & FIP_VHUB_SL_MASK);
        memcpy(m_VnicCtxArr[m_CurrTableSize].Mac, Ctx->mac, sizeof(m_VnicCtxArr[m_CurrTableSize].Mac));

        if ((!m_VnicCtxArr[m_CurrTableSize].Valid) &&
            (VhubMsgParams->MsgType == VHUB_TABLE_MSG)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Valid Bit is 0, must be 1 at VhubTable!!!\n", m_UniqeStr);
            ASSERT(FALSE);
            goto parse_err;
        }

        m_CurrTableSize++;
    }

    // Some General Checks
    if ((VhubMsgParams->MsgType == VHUB_TABLE_MSG) &&
        ((VhubMsgParams->Hdr == VHUB_ONLY) ||
        (VhubMsgParams->Hdr == VHUB_LAST))) {

        m_ClearTable = true;

#if 0        // CheckSum is not supported by BX Yet
        // Compare CheckSum
        u32 CheckSum = cl_ntoh32(((u32*)Ctx)[0]);
        if (m_CheckSum != CheckSum) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "Checksum (%d) is not as expected (%d) VhubId %d\n", CheckSum, m_CheckSum, m_VhubId);
            Status = STATUS_NDIS_INVALID_PACKET;
            goto parse_err;
        }
#endif

        // Check for Valid Table Size
        if (m_TableSize != m_CurrTableSize) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Table Size (%d) is not as expected (%d)\n", m_UniqeStr, m_TableSize, m_CurrTableSize);
            Status = STATUS_NDIS_INVALID_PACKET;
            goto parse_err;
        }
    }

    return Status;

parse_err:
    CleanVhubTable();
    return Status;
}


/*
 * This function runs on all Vhub Table Entries
 * and verify whether the relevant Vnic exist there.
 */
VOID
VhubTable::CheckVnicExistance( 
    MsgType_t MsgType
    )
{
    // For Vhub-Update we change the current state only if we got remove\add our Vnic
    //At Vhub-Table we assume it to be false and search it at the table
    if (MsgType == VHUB_TABLE_MSG) {
        m_IsVnicExist = false;
    }

    for (int i = 0; i < m_TableSize; i++) {
        if (memcmp(m_VnicCtxArr[i].Mac, m_Mac, sizeof(m_VnicCtxArr[i].Mac)) == 0) {
            if (MsgType == VHUB_TABLE_MSG) {
                m_IsVnicExist = true;
            } else {    // Vhub-Update
                m_IsVnicExist = (m_VnicCtxArr[i].Valid) ? true : false;
            }
            break;
        }
    }
}


VOID
VhubTable::RequestNewVhubTable( VOID )
{
    RemoveAllMessage(); // All current Msg are irrelevant
    CleanVhubTable();   // The table is not-up2date and we are about to request a new one
    m_FirstTimeUpdateTusn = true;
    m_IsVnicExist = false;
    m_VhubResult = VHUB_NEED_RETRY; // Vnic should find out that a new Vhub-table is required
    m_VhubTableState = VHUB_INIT;
}


NTSTATUS
VhubTable::UpdateInterfaceForAllVhubEntries( VOID )
{
    ASSERT(m_pDataInterfaceVnic != NULL);

    if (m_IsVnicExist == false) {        // Our Vnic is not exist, no need to Update IF
        InterfaceVhubUpdate_t *pInterfaceVhubUpdate = NEW InterfaceVhubUpdate_t;
        if (pInterfaceVhubUpdate == NULL) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Fail to allocate memory for InterfaceVhubUpdate!!!\n", m_UniqeStr);
            return STATUS_NO_MEMORY;

        }

        pInterfaceVhubUpdate->Cmd = CMD_REMOVE_ALL;
        memset(&pInterfaceVhubUpdate->VnicCtxEntry, 0, sizeof(pInterfaceVhubUpdate->VnicCtxEntry));

        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Going to send '%s' to EoIB\n", m_UniqeStr, CmdToStr(pInterfaceVhubUpdate->Cmd));
        g_pFipInterface->UpdateVhubEntry(m_pDataInterfaceVnic, pInterfaceVhubUpdate);   // Will send one msg to remove all
        return STATUS_SUCCESS;
    }

    for (u16 Idx = 0; Idx < m_TableSize; Idx++) {
        InterfaceVhubUpdate_t *pInterfaceVhubUpdate = NEW InterfaceVhubUpdate_t;
        if (pInterfaceVhubUpdate == NULL) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Fail to allocate memory for InterfaceVhubUpdate!!!\n", m_UniqeStr);
            return STATUS_NO_MEMORY;
        }

        pInterfaceVhubUpdate->Cmd = (m_VnicCtxArr[Idx].Valid == 1) ? CMD_ADD : CMD_REMOVE;
        memcpy(&pInterfaceVhubUpdate->VnicCtxEntry, &m_VnicCtxArr[Idx], sizeof(pInterfaceVhubUpdate->VnicCtxEntry));

        u64 Mac = 0;
        memcpy(&Mac, pInterfaceVhubUpdate->VnicCtxEntry.Mac, sizeof(pInterfaceVhubUpdate->VnicCtxEntry.Mac));
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Going to send '%s' mac = %I64x to EoIB\n", m_UniqeStr, CmdToStr(pInterfaceVhubUpdate->Cmd), cl_ntoh64(Mac) >> 16);
        g_pFipInterface->UpdateVhubEntry(m_pDataInterfaceVnic, pInterfaceVhubUpdate);
    }

    return STATUS_SUCCESS;
}


/*
 * This function is called only when in Init State.
 * On any other state we'll drop this msg.
 * This fucntion is handling Vhub Table Msg.
 * It will build a temporary list, update IBAL
 * and remove it to save memory.
  * In case we have a Msg with Tusn +2 we throw all Msgs
  * and request for New Vhub-Table.
 */
NTSTATUS
VhubTable::HandleVhubTable( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    RcvMsg *pRcvMsg = NULL;
    VhubMsgParams_t VhubMsgParams;
    bool fDidWeFoundTable = false;

    memset(&VhubMsgParams, 0, sizeof(VhubMsgParams));
    m_CheckSum = 0;
    pRcvMsg = PeekNextMessage(NULL);

    // Run this loop as long as we have a valid Msg and we haven't recieve the last Vhub-table Msg
    while ((pRcvMsg != NULL) && (m_ClearTable == false)) {

        Status = ParseVhubHeaders(pRcvMsg, &VhubMsgParams);
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Failed to Parse Vhub Headers\n", m_UniqeStr);
            pRcvMsg = RemoveCurrMsgAndPeekNextMsg(pRcvMsg);
            Status = STATUS_SUCCESS;
            continue;
        }

        // Update Tusn when a Table Msg has arrived
        if ((VhubMsgParams.Tusn > m_Tusn) &&
            (VhubMsgParams.MsgType == VHUB_TABLE_MSG) &&
            (m_FirstTimeUpdateTusn == true)) {
            m_Tusn = VhubMsgParams.Tusn;
            m_FirstTimeUpdateTusn = false;
        }

        if (VhubMsgParams.Tusn > (m_Tusn + 1)) {  // We are Out-of-Sync, let's request new Vhub-Table
            RequestNewVhubTable();
            goto handle_vhub_table_end;
        }

        // Check for Tusn - if the Tusn-incoming Msg is lower or equal we'll 'throw' this msg
        if (VhubMsgParams.Tusn < m_Tusn) {  // Irrelevant Old Msg..
            pRcvMsg = RemoveCurrMsgAndPeekNextMsg(pRcvMsg);
            continue;
        }

        if (VhubMsgParams.Tusn > m_Tusn) {  // Irrelevant Future Msg.. will handle it later on
            pRcvMsg = PeekNextMessage(pRcvMsg);
            continue;
        }

        if (VhubMsgParams.MsgType != VHUB_TABLE_MSG) { // HandleVhubUpdate will handle this Msg
            pRcvMsg = PeekNextMessage(pRcvMsg);
            continue;
        }

        // TODO: Handle Last\First Msg - when they got lost!!!
        // If we got here this is a relevant Msg, let's parse it
        Status = ParseVhubMsg(pRcvMsg, &VhubMsgParams);
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Failed to Parse Vhub Table\n", m_UniqeStr);
            RemoveMessage(pRcvMsg);
            goto handle_vhub_table_end;
        }
        fDidWeFoundTable = true;

        pRcvMsg = RemoveCurrMsgAndPeekNextMsg(pRcvMsg);
    }

    if (!fDidWeFoundTable) {   // Nothing to do
        goto handle_vhub_table_end;
    }

    // Now let's check whether the current Vnic is exist as it should
    // It can also silently dropped.
    CheckVnicExistance(VhubMsgParams.MsgType);

    // If we have to clear table it means that we successfullt got all Vhub Table Parts
    // Update current Vnic existance and sent update to IPoIB & IBAL.
    if (m_ClearTable == true) {
        Status = UpdateInterfaceForAllVhubEntries();
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s UpdateInterfaceForAllVhubEntries Failed\n", m_UniqeStr);
            // BUGBUG: Should we ClearTable
            CleanVhubTable();
            m_ClearTable = false; // For next time
            goto handle_vhub_table_end;
        }

        CleanVhubTable();
        m_ClearTable = false; // For next time
        m_VhubTableState = VHUB_UP2DATE;
        m_VhubResult = VHUB_SUCCESS;
    } else {
        m_VhubResult = VHUB_NOT_UPDATE_YET;
    }

handle_vhub_table_end:
    return Status;
}


/*
 * This function scan the Msg list we have for Update Msg
 * In case we have a Msg with Tusn +2 we throw all Msgs
 * and request for New Vhub-Table.
 * Else we scan eack Update Msg and update IBAL & IPoIB
 * For and add\remove Vnic at the net.
 */
NTSTATUS
VhubTable::HandleVhubUpdate( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    RcvMsg *pRcvMsg = NULL;
    VhubMsgParams_t VhubMsgParams;
    bool fDidWeFoundUpdate = false;

    memset(&VhubMsgParams, 0, sizeof(VhubMsgParams));
    m_CheckSum = 0;
    pRcvMsg = PeekNextMessage(NULL);

    while (pRcvMsg != NULL) {

        Status = ParseVhubHeaders(pRcvMsg, &VhubMsgParams);
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Failed to Parse Vhub Headers\n", m_UniqeStr);
            pRcvMsg = RemoveCurrMsgAndPeekNextMsg(pRcvMsg);
            Status = STATUS_SUCCESS;
            continue;
        }

        if (VhubMsgParams.Tusn > (m_Tusn + 1)) {  // We are Out-of-Sync, let's request new Vhub-Table
            RequestNewVhubTable();
            goto handle_vhub_update_end;
        }

        // Check for Tusn - if the Tusn-incoming Msg is lower or equal we'll 'throw' this msg
        if (VhubMsgParams.Tusn <= m_Tusn) {  // Irrelevant Old Msg..
            pRcvMsg = RemoveCurrMsgAndPeekNextMsg(pRcvMsg);
            continue;
        }

        if (VhubMsgParams.MsgType == VHUB_TABLE_MSG) { // We should be sync and this should not arrive - a redundant msg
            pRcvMsg = RemoveCurrMsgAndPeekNextMsg(pRcvMsg);
            continue;
        }

        ASSERT(VhubMsgParams.Tusn == (m_Tusn + 1));

        // If we got here this is a relevant Msg, let's parse it
        Status = ParseVhubMsg(pRcvMsg, &VhubMsgParams);
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s Failed to Parse Vhub Update\n", m_UniqeStr);
            RemoveMessage(pRcvMsg);
            goto handle_vhub_update_end;
        }
        fDidWeFoundUpdate = true;

        pRcvMsg = RemoveCurrMsgAndPeekNextMsg(pRcvMsg);
    }

    if (!fDidWeFoundUpdate) {   // Nothing to do
        goto handle_vhub_update_end;
    }

    // Check if our Vnic have be removed..
    CheckVnicExistance(VhubMsgParams.MsgType);

    Status = UpdateInterfaceForAllVhubEntries();
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VHUB_TABLE, "%s UpdateInterfaceForAllVhubEntries Failed\n", m_UniqeStr);
        goto handle_vhub_update_end;
    }

    m_VhubTableState = VHUB_UP2DATE;
    m_VhubResult = VHUB_SUCCESS;

handle_vhub_update_end:
    // Vhub Update are Uni-Packet so we can clear it after parsing
    CleanVhubTable();
    return Status;
}


NTSTATUS
VhubTable::RunFSM( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(m_Shutdown == false);

    switch (m_VhubTableState) {
        case VHUB_INIT:
            Status = HandleVhubTable();            
            break;

        case VHUB_UP2DATE:
            Status = HandleVhubUpdate();
            break;
    }

    return Status;
}


/*
 * The next 4 functions are handling the incoming msg from the port qp
 * 
 */
VOID
VhubTable::AddMessage(
    RcvMsg *pRcvMsg
    )
{
    ASSERT(m_Shutdown == false);
    m_IncomingMsgHead.InsertTailList(&pRcvMsg->m_ExtenalEntry);
}


/*
 * This function return the next msg in the incoming list.
 * In case there are no more msg it will return Null.
 * To get the 1st msg you'll have to supply Null.
 */
RcvMsg*
VhubTable::PeekNextMessage(
    RcvMsg *pInRcvMsg
    )
{
    RcvMsg *pOutRcvMsg = NULL;

    if (pInRcvMsg == NULL) {    // Get the 1st Msg
        if (m_IncomingMsgHead.Size() != 0) {
            pOutRcvMsg = CONTAINING_RECORD(m_IncomingMsgHead.Head(), RcvMsg, m_ExtenalEntry);
        }   // Else we return NULL
    } else {    // Return a Msg in the middle of the list
        LIST_ENTRY *pNextMsg = pInRcvMsg->m_ExtenalEntry.Flink;

        if ((pNextMsg != NULL) &&
            (!m_IncomingMsgHead.IsAfterTheLast(pNextMsg))) {
            pOutRcvMsg = CONTAINING_RECORD(pNextMsg, RcvMsg, m_ExtenalEntry);
        }
    }
    return pOutRcvMsg;
}


RcvMsg*
VhubTable::RemoveCurrMsgAndPeekNextMsg(
    RcvMsg *pInRcvMsg
    )
{
    RcvMsg * pOutRcvMsg = NULL;

    pOutRcvMsg = PeekNextMessage(pInRcvMsg);
    RemoveMessage(pInRcvMsg);

    return pOutRcvMsg;
}


VOID
VhubTable::RemoveMessage(
    RcvMsg *pRcvMsg
    )
{
    m_IncomingMsgHead.RemoveEntryList(&pRcvMsg->m_ExtenalEntry);
    m_VnicSendRecv->ReturnBuffer(pRcvMsg);
}


VOID
VhubTable::RemoveAllMessage( VOID )
{
    RcvMsg *pRcvMsg = NULL;

    while (m_IncomingMsgHead.Size() != 0) {
        pRcvMsg = PeekNextMessage(pRcvMsg);
        RemoveMessage(pRcvMsg);
    }
}




