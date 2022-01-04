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
    fip_main.cpp

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/
#include "precomp.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif

#include "fip_utils.tmh"
#endif


char *
GetClassTypeStr(
    ClassType_t ClassType
    )
{
    switch (ClassType) {
    case PORT_CLASS:
        return "Port Class";
        break;

    case GW_CLASS:
        return "Gateway Class";
        break;

    case VNIC_CLASS:
        return "Vnic Class";
        break;

    default:
        return "Unknown Class";
        break;
    }
}


NTSTATUS
GetCaGuidAndPortNumFromPortGuid(
    IN ib_al_handle_t h_al,
    IN ib_net64_t PortGuid,
    IN uint16_t PkeyIndex,
    OUT ib_net64_t *CaGuid,
    OUT uint8_t *PortNumber,
    OUT u16 *pPkey,
    OUT u16 *pLid,    
    OUT UCHAR      *pEnumMTU
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ib_api_status_t IbStatus = IB_SUCCESS;
    uint32_t CaSize = 0;
    size_t GuidCount = 0;
    ib_net64_t *pCaGuidArray = NULL;
    ib_ca_attr_t *pCaAttr = NULL;
    bool Found = false;

    IbStatus = ib_get_ca_guids(h_al, NULL, &GuidCount);
    if (IbStatus != IB_INSUFFICIENT_MEMORY) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_get_ca_guids failed. statue=0x%x\n",IbStatus);
        return STATUS_ADAPTER_HARDWARE_ERROR;
    }
 
    pCaGuidArray = NEW ib_net64_t[GuidCount];
    if (IbStatus != IB_INSUFFICIENT_MEMORY) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_get_ca_guids failed. statue=0x%x\n", IbStatus);
        return STATUS_NO_MEMORY;
    }
    
    IbStatus = ib_get_ca_guids(h_al, pCaGuidArray, &GuidCount);
    if (IbStatus != IB_SUCCESS) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_get_ca_guids failed. statue=0x%x\n", IbStatus);
        Status = STATUS_ADAPTER_HARDWARE_ERROR;
        goto guid_err;
    }

    for (ULONG i = 0; i < GuidCount; i++) {
        IbStatus = ib_query_ca_by_guid(h_al, pCaGuidArray[i], NULL, &CaSize);
        if (IbStatus != IB_INSUFFICIENT_MEMORY) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Error, expected to get IB_INSUFFICIENT_MEMORY after query CA\n");
            Status = STATUS_ADAPTER_HARDWARE_ERROR;
            goto guid_err;
        }
        
        /* allocate enough memory for the query CA attributes */
        pCaAttr = NEW ib_ca_attr_t[CaSize];
        if (pCaAttr == NULL) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Failed to allocate %d bytes for CA attributes\n", CaSize);
            Status = STATUS_NO_MEMORY;
            goto guid_err;
        }
        
        IbStatus = ib_query_ca_by_guid(h_al, pCaGuidArray[i], pCaAttr, &CaSize);
        if (IbStatus != IB_SUCCESS) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_query_ca_by_guid  failed. statue=0x%x\n", IbStatus);
            Status = STATUS_ADAPTER_HARDWARE_ERROR;
            goto guid_err;
        }

        for ( u8 PortNum = 0; PortNum < pCaAttr->num_ports; PortNum++) {
            ib_port_attr_t *PortAttr = &pCaAttr->p_port_attr[PortNum];

            if (PortAttr->port_guid == PortGuid) {
                Found = true;
                if (CaGuid) {
                    *CaGuid = pCaGuidArray[i];
                }
                if(PortNumber) {
                    *PortNumber = PortNum + 1;  // Port Number are 1..2
                }
                if (pPkey) {
                    *pPkey = cl_ntoh16(pCaAttr->p_port_attr[PortNum].p_pkey_table[PkeyIndex]);
                }
                if (pLid) {
                    *pLid = cl_ntoh16(pCaAttr->p_port_attr[PortNum].lid);
                }
                if (pEnumMTU) {
                    *pEnumMTU = pCaAttr->p_port_attr[PortNum].mtu;
                }
                break;
                }
        }

        delete[] pCaAttr;
        pCaAttr = NULL;

        if (Found == true) {
            break;
        }
    }

    delete[] pCaGuidArray;
    pCaGuidArray = NULL;

    return Status;

guid_err:
    if (pCaAttr != NULL) {
        delete[] pCaAttr;
    }

    if (pCaGuidArray != NULL) {
        delete[] pCaGuidArray;
    }

    return Status;
}


NTSTATUS CreateMcastAv(
    ib_pd_handle_t              h_pd,
    uint8_t                     port_num,
    ib_member_rec_t* const      p_member_rec,
    ib_av_handle_t* const       ph_av )
{
    ib_av_attr_t    av_attr;
    uint32_t        flow_lbl;
    uint8_t         hop_lmt;
    ib_api_status_t IBStatus;
    NTSTATUS Status = STATUS_SUCCESS;

    memset( &av_attr, 0, sizeof(ib_av_attr_t) );
    av_attr.port_num = port_num;
    ib_member_get_sl_flow_hop( p_member_rec->sl_flow_hop,
        &av_attr.sl, &flow_lbl, &hop_lmt );
    av_attr.dlid = p_member_rec->mlid;
    av_attr.grh_valid = TRUE;
    av_attr.grh.hop_limit = hop_lmt;
    av_attr.grh.dest_gid = p_member_rec->mgid;
    av_attr.grh.src_gid = p_member_rec->port_gid;
    av_attr.grh.ver_class_flow =
        ib_grh_set_ver_class_flow( 6, p_member_rec->tclass, flow_lbl );
    av_attr.static_rate = p_member_rec->rate & IB_PATH_REC_BASE_MASK;
    av_attr.path_bits = 0;
    /* port is not attached to endpoint at this point, so use endpt ifc
       reference */
    IBStatus = ib_create_av( h_pd, &av_attr, ph_av );

    if (IBStatus != IB_SUCCESS) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_create_av Failed\n");
        Status = STATUS_ADAPTER_HARDWARE_ERROR;
    }

    return Status;

}


ib_api_status_t
fip_mcast_mgr_leave_mcast(
    mcast_mgr_request_t *McastReq
    )
{
    ib_api_status_t ib_status = mcast_mgr_leave_mcast(McastReq, NULL);
    ASSERT(ib_status == IB_SUCCESS);
    if (ib_status != IB_SUCCESS) 
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "mcast_mgr_leave_mcast failed Status = 0x%x\n", ib_status);
    }
    return ib_status;

}


VOID
DetachMcast(
    bool m_McastTableConnected,
    SendRecv *pSendRecv,
    al_attach_handle_t *m_McastTableAattach
    )
{
    if (m_McastTableConnected) {
        ASSERT(*m_McastTableAattach);
        pSendRecv->DetachMcast(*m_McastTableAattach);
        *m_McastTableAattach = NULL;
    }
}


VOID
DestroyMcast(
    bool                *pfMcastConnected,
    FipThreadMessage    *pMcastData,
    mcast_mgr_request_t **ppMcastRequest,
    class FipWorkerThread *pFipWorkerThread
    )
{
    ib_api_status_t ib_status;
    boolean_t is_mcast_connected = FALSE;

    if (*pfMcastConnected) {
        ASSERT(*ppMcastRequest != NULL);
        fip_mcast_mgr_leave_mcast(*ppMcastRequest);
        *pfMcastConnected = false;
        deref_request(*ppMcastRequest);
        *ppMcastRequest = NULL;
    } else {
        if(*ppMcastRequest) {
            // cancell
            ib_status = mcast_mgr_cancel_join(*ppMcastRequest, &is_mcast_connected);
            if (ib_status == IB_SUCCESS) {
                // mcast was canceled, no side affects
            } else {
                // cancell has failed, but we have not yet recieved it, remove us from the list of operations to do
                ASSERT(!IsListEmpty(&pMcastData->ListEntry));
                pFipWorkerThread->RemoveThreadMessage(pMcastData);

                if (is_mcast_connected) {
                    fip_mcast_mgr_leave_mcast(*ppMcastRequest);
                }
            }
            deref_request(*ppMcastRequest);
            *ppMcastRequest = NULL;
        }
    }
}


bool
GenericJoinMcastCallBack(
    bool *pMcastConnected,
    struct McastMessageData *pMcastMessageData,
    VhubMgid_t *pVhubMgid,
    mcast_mgr_request_t **ppMcastRequest,
    al_attach_handle_t *pMcastHandleAattach,
    ib_av_handle_t *pMcastAv,
    class SendRecv *pSendRecv
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    bool WasFound = false;

    if(memcmp(&pMcastMessageData->member_rec.mgid, pVhubMgid, sizeof(ib_gid_t)) != 0){
        return WasFound;
    }

    WasFound = true;
    ASSERT(*pMcastConnected == false);
    if (pMcastMessageData->status != IB_SUCCESS) {
        deref_request(*ppMcastRequest);
        *ppMcastRequest = NULL;
    } else {
        if (pMcastHandleAattach != NULL) {
            Status = pSendRecv->AttachMcast(
                &pMcastMessageData->member_rec.mgid,
                pMcastMessageData->member_rec.mlid,
                pMcastHandleAattach
                );
        } else {    // Av
            Status = pSendRecv->CreateMcastAv(
                &pMcastMessageData->member_rec,
                pMcastAv
                );
        }
        if (!NT_SUCCESS(Status)) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "SendRecv.AttachMcast/CreateMcastAv failed Status = 0x%x\n", Status);

            fip_mcast_mgr_leave_mcast(*ppMcastRequest);
            deref_request(*ppMcastRequest);
            *ppMcastRequest = NULL;
        } else {
            *pMcastConnected = true;
        }
    }
    return WasFound;
}


