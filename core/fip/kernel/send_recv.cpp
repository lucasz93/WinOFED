/*++

Copyright (c) 2005-2012 Mellanox Technologies. All rights reserved.

Module Name:
    send_recv

Abstract:
    This Class is used to Send and Recieve IB messages using UD QP.
    Please note that this class is support single-threaded.
    To change it to support multi-threaded you have to add some locks.
    TO use this class you have to use this way:
        Init(...)
        Send(...) & Recv(...)
        ShutDown(...)

Revision History:

Author:
    Sharon Cohen

Notes:

--*/

#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif

#include "send_recv.tmh"
#endif


char*
RcvMsg::RcvMsgStateToStr( 
    RcvMsgState State )
{
    switch (State)
    {
        case AtInit: return "AtInit";
        case AtPostRecv: return "AtPostRecv";
        case WaitingForUser: return "WaitingForUser";
        case AtUser: return "AtUser";
        default: return "Unknown";
    }
}


/*
 * Print all relevant data a user might want to print.
 * Mostly will be used for debug.
 */
VOID
RcvMsg::PrintDbgData( VOID )
{
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "===========================================================\n");
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Len            %d\n", this->m_Len);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Data           %s\n", (this->m_AlignedData + GRH_LEN));
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "RecvOpt        %d\n", this->m_RecvOpt);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ImmediateData  %d\n", this->m_ImmediateData);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "RemoteQp       %d\n", this->m_RemoteQp);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "PkeyIndex      %d\n", this->m_PkeyIndex);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "RemoteLid      %d\n", this->m_RemoteLid);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "RemoteSl       %d\n", this->m_RemoteSl);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "PathBits       %d\n", this->m_PathBits);
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "m_MsgState     %s\n", RcvMsgStateToStr(this->m_MsgState));
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "===========================================================\n");
}


/*
 * Return the QP state as string
 */
static char* 
QPModifyStateStr(
    ib_qp_state_t qp_state
    )
{
    switch (qp_state)
    {
        case IB_QPS_RESET:  return "Reset";
        case IB_QPS_INIT:   return "Init";
        case IB_QPS_RTR:    return "RTR";
        case IB_QPS_RTS:    return "RTS";
        case IB_QPS_ERROR:  return "Error";
        default:            return "Unknown";
    }
}


/*************************************************************
 * * Function: CbSendCompHandle
 * *************************************************************/
static VOID CbSendCompHandle(
    const ib_cq_handle_t h_cq,
    VOID* CqContext
    )
{
    /* empty function */
    ASSERT(FALSE);
    UNUSED_PARAM(h_cq);
    UNUSED_PARAM(CqContext);
}


NTSTATUS
SendRecv::InitSendBuffers(
    ULONG SendRingSize
    )
{
    ULONG i;
    
    m_SendBuff.m_RingSize = SendRingSize;
    m_SendBuff.m_MsgBuffer = NEW SndMsg[SendRingSize];
    if (m_SendBuff.m_MsgBuffer == NULL)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Failed to alloc Send MsgBuffer\n");
        goto buff_err;
    }

    // For each Data buffer we save it's Physical Address for later use
    for (i = 0; i < SendRingSize; i++)
    {
        SndMsg* pCurrDesc = &m_SendBuff.m_MsgBuffer[i];

        // Allocation is bounded by page size
        pCurrDesc->m_Data = NEW CHAR[PAGE_SIZE + MAX_UD_MSG];
        if(pCurrDesc->m_Data == NULL)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Alloc Send PKT #%d failed.\n",i);
            goto buff_err;
        }
        pCurrDesc->m_AlignedData = (PCHAR)PAGE_ALIGN(pCurrDesc->m_Data);

        // BUGBUG: Replace with DMA Allocation
        pCurrDesc->m_PhyAddr = MmGetPhysicalAddress(pCurrDesc->m_AlignedData).QuadPart;
        
        // Insert All (free) Buffers to the list In FIFO
        m_SendBuff.m_FreeBuffListHead.InsertTailList(&pCurrDesc->m_Entry);
    }

    return STATUS_SUCCESS;

buff_err:
    DestroySendBuffers();
    return STATUS_NO_MEMORY;
}


/* 
 * 'Send' a buffer to post_recv
 */
NTSTATUS
SendRecv::MoveRcvMsgToPostRecv(
    RcvMsg *pRcvMsg
    )
{
    ib_api_status_t  IBStatus = IB_SUCCESS;

    ASSERT(pRcvMsg != NULL);
    ASSERT((pRcvMsg->m_MsgState == AtInit) || (pRcvMsg->m_MsgState == AtUser));

    m_WaitForRcvShutdownLock.Lock();
    if (m_IsQpStateAtError || m_WasShutDownCalled)
    {
        m_WaitForRcvShutdownLock.Unlock();
        pRcvMsg->m_MsgState = AtInit;
        return STATUS_SUCCESS;
    }
    
    IBStatus = ib_post_recv(m_IBRes.m_h_qp, &pRcvMsg->m_Wr, NULL);
    if (IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Failed to Post recv (0x%x)\n", IBStatus);
        ASSERT(FALSE);  // Should not occur (maybe buffer overflow - return-buffer was called twice)
        m_WaitForRcvShutdownLock.Unlock();
        goto post_recv_err;
    }

    pRcvMsg->m_MsgState = AtPostRecv;
    m_RecvBuff.m_NumAtPostRecv++;
    m_WaitForRcvShutdownLock.Unlock();

return STATUS_SUCCESS;

post_recv_err:
    return STATUS_ADAPTER_HARDWARE_ERROR;
}


/* 
 * After we polled CQ we update all field here and
 * add it to the Waiting list for the user to receive it
 * Please note that we expect the WR status to be OK.
 */
VOID
SendRecv::MoveRcvMsgToWaitForUser(
    ib_wc_t *pWc
    )
{
    ASSERT(pWc != NULL);
    RcvMsg* pRcvMsg = (RcvMsg*)pWc->wr_id;

    ASSERT(pRcvMsg != NULL);
    ASSERT(pRcvMsg->m_MsgState == AtPostRecv);

    // Copy all the WR relevant data to internal buffer
    pRcvMsg->m_Len = pWc->length;
    pRcvMsg->m_RecvOpt = pWc->recv.ud.recv_opt;
    pRcvMsg->m_ImmediateData = pWc->recv.ud.immediate_data;
    pRcvMsg->m_RemoteQp = pWc->recv.ud.remote_qp;
    pRcvMsg->m_PkeyIndex = pWc->recv.ud.pkey_index;
    pRcvMsg->m_RemoteLid = pWc->recv.ud.remote_lid;
    pRcvMsg->m_RemoteSl = pWc->recv.ud.remote_sl;
    pRcvMsg->m_PathBits = pWc->recv.ud.path_bits;
    pRcvMsg->m_MsgState = WaitingForUser;

    // Add it to the list until the user pop it out (using Recv function)
    m_RcvListLock.Lock();
    m_RecvBuff.m_WaitForUserBuffListHead.InsertTailList(&pRcvMsg->m_Entry);
    m_RcvListLock.Unlock();


}


/*
 * Move some waiting msg to the user, delete it form the list
 * and return it so hte user can use it.
 */
RcvMsg *
SendRecv::MoveRcvMsgToUser( VOID )
{
    m_RcvListLock.Lock();
    LIST_ENTRY *pListEntry = m_RecvBuff.m_WaitForUserBuffListHead.RemoveHeadList();
    m_RcvListLock.Unlock();
    RcvMsg * pRcvMsg = CONTAINING_RECORD(pListEntry,RcvMsg,m_Entry);

    ASSERT(pRcvMsg != NULL);
    ASSERT(pRcvMsg->m_MsgState == WaitingForUser);

    pRcvMsg->m_MsgState = AtUser;
    m_RecvBuff.m_NumAtUser++;

    return pRcvMsg;
}


/*
 * After the user processed the msg we clean the buffer
 * and move it to post-recv
 */
VOID
SendRecv::MoveRcvMsgFromUser(
    RcvMsg *pRcvMsg
    )
{
    ASSERT(pRcvMsg != NULL);
    ASSERT(pRcvMsg->m_MsgState == AtUser);
    
    //Clean the data
    pRcvMsg->m_Len = 0;
    memset(pRcvMsg->m_AlignedData, 0, sizeof(MAX_UD_MSG));
    pRcvMsg->m_ImmediateData = 0;
    pRcvMsg->m_RemoteQp = 0;
    pRcvMsg->m_PkeyIndex = 0;
    pRcvMsg->m_RemoteLid = 0;
    pRcvMsg->m_RemoteSl = 0;
    pRcvMsg->m_PathBits = 0;
    memset(&pRcvMsg->m_RecvOpt, 0, sizeof(pRcvMsg->m_RecvOpt)); // Init to invalid value

    m_RecvBuff.m_NumAtUser--;

    MoveRcvMsgToPostRecv(pRcvMsg);
}


NTSTATUS 
SendRecv::InitRecvBuffers( 
    ULONG RecvRingSize
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    ib_api_status_t  IBStatus = IB_SUCCESS;
    
    m_RecvBuff.m_RingSize = RecvRingSize;

    m_RecvBuff.m_MsgBuffer = NEW RcvMsg[RecvRingSize]; // Allocate a buffer to hold all the msg we receive
    if (m_RecvBuff.m_MsgBuffer == NULL)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Failed to alloc Recv MsgBuffer\n");
        status = STATUS_NO_MEMORY;
        goto init_rcv_err;
    }

    // Prepare each Recv buffer
    for(ULONG i =0; i<RecvRingSize; i++)
    {
        RcvMsg* pCurrDesc = &m_RecvBuff.m_MsgBuffer[i];

        // Allocation is bounded by page size, we add the GRH to UD packet
        pCurrDesc->m_Data = NEW CHAR[PAGE_SIZE + MAX_UD_MSG + GRH_LEN];
        if(pCurrDesc->m_Data == NULL)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Alloc Recv PKT #%d failed.\n",i);
            status = STATUS_NO_MEMORY;
            goto init_rcv_err;
        }
        pCurrDesc->m_AlignedData = (PCHAR)PAGE_ALIGN(pCurrDesc->m_Data);

        pCurrDesc->m_Wr.wr_id = (uintn_t)pCurrDesc;   // When I PollSndCQ I will use this ptr
        pCurrDesc->m_Wr.num_ds = 1;

        // BUGBUG: Replace with DMA Allocation
        pCurrDesc->m_LocalDs.vaddr = MmGetPhysicalAddress(pCurrDesc->m_AlignedData).QuadPart; // For each Data buffer we save it's Physical Address for later use
        pCurrDesc->m_LocalDs.lkey  = m_IBRes.m_lkey;
        pCurrDesc->m_LocalDs.length = MAX_UD_MSG; // The whole buffer is within a single page
        pCurrDesc->m_Wr.ds_array = &pCurrDesc->m_LocalDs;

        // 'Send' each buffer to post_recv
        status = MoveRcvMsgToPostRecv(pCurrDesc);
        if(status != STATUS_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "MoveRcvMsgToPostRecv failed.\n");
            goto init_rcv_err;
        }
    }

    IBStatus = ib_rearm_cq(m_IBRes.m_h_recv_cq, false);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_rearm_cq failed. Status=%d\n",IBStatus);
        m_CqError = true;
        KeSetEvent(m_CqErrorEvent, IO_NO_INCREMENT, FALSE);
        status = STATUS_ADAPTER_HARDWARE_ERROR;
        ASSERT(FALSE);
        goto init_rcv_err;
    }


    return status;
    
init_rcv_err:
    // Will be handled on shutdown
    return status;
}


/*
 * Modify QP from Reset to RTS state
 */
NTSTATUS
SendRecv::ModifyQPResetToRTS( VOID )
{
    ib_api_status_t IBStatus = IB_SUCCESS;
    ib_dgrm_info_t dgrm_info;

    /* Move the QP to RTS. */
    dgrm_info.port_guid = m_IBRes.m_PortGuid;
    dgrm_info.qkey = m_IBRes.m_qkey;
    dgrm_info.pkey_index = m_IBRes.m_PkeyIndex;
    IBStatus = ib_init_dgrm_svc( m_IBRes.m_h_qp, &dgrm_info );
    if( IBStatus != IB_SUCCESS )
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "modify_qp failed, statue=0x%x\n", IBStatus);
        m_CqError = true; // Also send a event to the user about this error
        KeSetEvent(m_CqErrorEvent, IO_NO_INCREMENT, FALSE);
        goto modify_err;
    }

    return STATUS_SUCCESS;

modify_err:
    if (ModifyQPToError() != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ModifyQPToError failed");
    }
    return STATUS_ADAPTER_HARDWARE_ERROR; // or any other error
}


/*
 * Modify QP from any state to Reset state
 */
NTSTATUS 
SendRecv::ModifyQPToError( VOID )
{
    ib_api_status_t IBStatus = IB_SUCCESS;
    ib_qp_mod_t qp_mod;

    ASSERT(m_IBRes.m_h_qp != NULL);
    memset(&qp_mod, 0, sizeof(qp_mod));
    qp_mod.req_state = IB_QPS_ERROR;

    m_QPStateLock.Lock();
    m_IsQpStateAtError = true;
    m_QPStateLock.Unlock();

    IBStatus = ib_modify_qp(m_IBRes.m_h_qp, &qp_mod);
    if(IBStatus != IB_SUCCESS)
    {   // If we fail to change QP state we won't change 'm_IsQpStateAtError' flag because we don't know its state
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV,
            "modify_qp failed. req_state %s, statue=0x%x\n",
            QPModifyStateStr(qp_mod.req_state), IBStatus);
        m_CqError = true; // Also send a event to the user about this error
        KeSetEvent(m_CqErrorEvent, IO_NO_INCREMENT, FALSE);
        goto modify_reset_err;
    }

    return STATUS_SUCCESS;

modify_reset_err:
    return STATUS_ADAPTER_HARDWARE_ERROR; // or any other error
}


/* 
 * This function is called when a receive msg has arrived
 */
VOID 
SendRecv::CbReceive(
    const ib_cq_handle_t h_cq,
    PVOID CqContext
    )
{
    UNUSED_PARAM(h_cq);
    VERIFY_DISPATCH_LEVEL(DISPATCH_LEVEL);
    FIP_PRINT(TRACE_LEVEL_VERBOSE, FIP_SEND_RECV, "CbReceive is called\n");

    SendRecv* pSendRecv = (SendRecv*)CqContext;
    ASSERT(pSendRecv->m_IBRes.m_h_recv_cq == h_cq);
    pSendRecv->ProcessRecvPacket();
}


NTSTATUS
SendRecv::InitIBResources(
    ib_al_handle_t h_al, 
    ib_net64_t CAGuid, 
    ib_net64_t PortGuid, 
    uint8_t PortNumber, 
    ULONG SendRingSize, 
    ULONG RecvRingSize, 
    ib_net32_t qkey,
    uint16_t PkeyIndex
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    ib_api_status_t  IBStatus = IB_SUCCESS;
    ib_cq_create_t   RcvCqCreate = {0};
    ib_cq_create_t   SndCqCreate = {0};
    ib_qp_attr_t     qp_attr = {0};
    ib_phys_create_t phys_create = {0};
    ib_phys_range_t  phys_range = {0};

    m_IBRes.m_PkeyIndex = PkeyIndex;
    m_IBRes.m_PortNumber = PortNumber;
    m_IBRes.m_qkey = qkey;
    m_IBRes.m_PortGuid = PortGuid;

    //
    //  Allocate CA
    //    
    IBStatus = ib_open_ca(h_al,
                           CAGuid,
                           NULL,
                           this,
                           &m_IBRes.m_h_ca);
    if(IBStatus != IB_SUCCESS)
    {        
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "open_ca failed. statue=0x%x\n", IBStatus);
        goto ca_err;
    }

    //
    //  Allocate PD
    //
    IBStatus = ib_alloc_pd( m_IBRes.m_h_ca,
                             IB_PDT_UD,
                             NULL,
                             &m_IBRes.m_h_pd );
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "alloc_pd failed. statue=0x%x\n", IBStatus);
        goto pd_err;
    }

    //
    // Allocate receive CQ.
    //
    RcvCqCreate.pfn_comp_cb = CbReceive;
    RcvCqCreate.size = RecvRingSize;
    RcvCqCreate.h_wait_obj = NULL;

    IBStatus = ib_create_cq(m_IBRes.m_h_ca,
                             &RcvCqCreate,
                             this,
                             NULL,
                             &m_IBRes.m_h_recv_cq
                             );
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "create rcv cq failed. statue=0x%x\n", IBStatus);
        goto recv_cq_err;
    }

    //
    // Allocate send CQ.
    //
    SndCqCreate.pfn_comp_cb = CbSendCompHandle;
    SndCqCreate.size = SendRingSize;
    SndCqCreate.h_wait_obj = NULL;

    IBStatus = ib_create_cq(m_IBRes.m_h_ca,
                             &SndCqCreate,
                             NULL,
                             NULL,
                             &m_IBRes.m_h_send_cq
                             );
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "create send cq failed. statue=0x%x\n", IBStatus);
        goto send_cq_err;
    }

    //
    // Allocate CPort QP.
    //
    ib_qp_create_t qp_create;
    memset(&qp_create,0,sizeof(qp_create));
    
    qp_create.qp_type  = IB_QPT_UNRELIABLE_DGRM;
    qp_create.rq_depth = RecvRingSize;
    qp_create.rq_sge   = 1;
    qp_create.h_rq_cq  = m_IBRes.m_h_recv_cq;
    
    qp_create.sq_depth = SendRingSize;
    qp_create.sq_sge   = 1;
    qp_create.h_sq_cq  = m_IBRes.m_h_send_cq;

    qp_create.sq_signaled = TRUE;
    
    IBStatus = ib_create_qp(m_IBRes.m_h_pd,
                             &qp_create,
                             NULL,
                             NULL,
                             &m_IBRes.m_h_qp);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "create_qp failed. statue=0x%x\n", IBStatus);
        goto qp_err;
    }

    //
    //  Query CPort QP attributes and get qp number
    //
    IBStatus = ib_query_qp(m_IBRes.m_h_qp,&qp_attr);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "query_qp failed. statue=0x%x\n", IBStatus);
        goto all_ib_res_err;
    }
    
    m_IBRes.m_qpn = qp_attr.num;  /* For JoMcast Recv */

    FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_SEND_RECV, "create_qp: QP number=%d\n",cl_ntoh32(m_IBRes.m_qpn));

    //
    //  Register all of physical memory (0..2^64)
    //
    uint64_t    vaddr   = 0;
    net32_t     rkey    = 0;

    phys_create.length = 0xFFFFFFFFFFFFFFFF;
    phys_create.num_ranges = 1;
    phys_create.range_array = &phys_range;
    phys_create.buf_offset = 0;
    phys_create.hca_page_size = PAGE_SIZE;
    phys_create.access_ctrl = IB_AC_LOCAL_WRITE;
    phys_range.base_addr = 0;
    phys_range.size = 0xFFFFFFFFFFFFFFFF;

    IBStatus = ib_reg_phys(m_IBRes.m_h_pd,
                            &phys_create,
                            &vaddr,
                            &m_IBRes.m_lkey,
                            &rkey,
                            &m_IBRes.m_h_mr);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "reg_phys failed. statue=0x%x\n", IBStatus);
        goto mr_err;
    }

    status = ModifyQPResetToRTS();
    if(status != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ModifyQPResetToRTS failed. statue=0x%x\n", IBStatus);
        goto all_ib_res_err;
    }

    FIP_PRINT(TRACE_LEVEL_VERBOSE, FIP_SEND_RECV, "InitIBResources Done.\n");
    return status;

all_ib_res_err:
    IBStatus = ib_dereg_mr(m_IBRes.m_h_mr);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_dereg_mr failed. statue=0x%x\n", IBStatus);
    }

mr_err:
    IBStatus = ib_destroy_qp(m_IBRes.m_h_qp, NULL);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "destroy_qp failed. statue=0x%x\n", IBStatus);
    }

qp_err:
    IBStatus = ib_destroy_cq(m_IBRes.m_h_send_cq, NULL);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "destroy_cq send failed. statue=0x%x\n", IBStatus);
    }

send_cq_err:
    IBStatus = ib_destroy_cq(m_IBRes.m_h_recv_cq, NULL);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "destroy_cq rcv failed. statue=0x%x\n", IBStatus);
    }

recv_cq_err:
    IBStatus = ib_dealloc_pd(m_IBRes.m_h_pd, NULL);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "dealloc_pd failed. statue=0x%x\n", IBStatus);
    }

pd_err:
    IBStatus = ib_close_ca(m_IBRes.m_h_ca, NULL);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "close_ca failed. statue=0x%x\n", IBStatus);
    }
ca_err:
    return STATUS_ADAPTER_HARDWARE_ERROR;
}


VOID
SendRecv::WaitForRcvToEnd( VOID )
{
    bool ShouldWaitForRcvToEnd;
    
    m_WaitForRcvShutdownLock.Lock();
    m_WaitForRcvToEnd = true; // From now on no event will be sent when we poll recv cq
    ShouldWaitForRcvToEnd = (m_RecvBuff.m_NumAtPostRecv == 0) ? false : true;
    m_WaitForRcvShutdownLock.Unlock();

    
    if (ShouldWaitForRcvToEnd== true)
    {   // Wait  for clean all recv
        NTSTATUS status = KeWaitForSingleObject(&m_WaitForRcvEvent, Executive, KernelMode , FALSE, NULL);
        if (status != STATUS_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "KeWaitForSingleObject failed. statue=0x%x\n", status);
        }
    }
}


NTSTATUS
SendRecv::Init(
    ib_al_handle_t h_al,
    ib_net64_t PortGuid,
    ULONG SendRingSize,
    ULONG RecvRingSize,
    ib_net32_t qkey,
    uint16_t PkeyIndex,
    KEVENT *MsgArrivedEvent,
    KEVENT *CqErrorEvent
    )
{
    NTSTATUS status;
    ib_net64_t CaGuid;
    uint8_t PortNumber;

    m_WasShutDownCalled = false;

    ASSERT((SendRingSize != 0) && (RecvRingSize != 0));
    ASSERT(h_al != NULL);

    // The first one to wait on this events will have to wait for my signal
    KeInitializeEvent(&m_WaitForRcvEvent, SynchronizationEvent, FALSE);

    m_MsgArrivedEvent = MsgArrivedEvent;
    m_CqErrorEvent = CqErrorEvent;

    status =GetCaGuidAndPortNumFromPortGuid(h_al, PortGuid, 0, &CaGuid, &PortNumber, NULL, NULL, NULL);
    if (status != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "GetCaGuidAndPortNumFromPortGuid Failed\n");
        goto init_err;
    }

    status = InitIBResources(h_al, CaGuid, PortGuid, PortNumber, SendRingSize, RecvRingSize, qkey, PkeyIndex);
    if (status != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Failed to Init IB Resources\n");
        goto init_err;
    }

    status = InitSendBuffers(SendRingSize);
    if (status != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Failed to Init IB Send-Recv Buffers\n");
        goto snd_err;
    }

    status = InitRecvBuffers(RecvRingSize);
    if (status != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "InitRecvBuffers Failed\n");
        goto rcv_err;
    }

    return status;

rcv_err:
    if (ModifyQPToError() != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ModifyQPToError failed");
    }
    WaitForRcvToEnd();
    DestroyRecvBuffers();
snd_err:
    DestroySendBuffers();
    DestroyIBResources();
init_err:
    m_WasShutDownCalled = true;
    return status;
}


VOID
SendRecv::DestroySendBuffers( VOID )
{
    PollAllSndCQ();

    while (m_SendBuff.m_FreeBuffListHead.Size() > 0)
    {
        m_SendBuff.m_FreeBuffListHead.RemoveTailList();
    }

    if (m_SendBuff.m_MsgBuffer != NULL)
    {
        for (ULONG i = 0; i < m_SendBuff.m_RingSize; i++)
        {
            if (m_SendBuff.m_MsgBuffer[i].m_Data != NULL)
            {
                delete []m_SendBuff.m_MsgBuffer[i].m_Data;
            }
        }
        delete []m_SendBuff.m_MsgBuffer;
        m_SendBuff.m_MsgBuffer = NULL;
    }
    FIP_PRINT(TRACE_LEVEL_VERBOSE, FIP_SEND_RECV, "DestroySendBuffers Done.\n");
}


/*
 * Free all memory and list we allocated so far.
* The user must return every buffer 
* before calling shutdown.
 */
VOID
SendRecv::DestroyRecvBuffers( VOID )
{
    ASSERT(m_RecvBuff.m_NumAtUser == 0);

    while (m_RecvBuff.m_WaitForUserBuffListHead.Size() > 0)
    {
        m_RecvBuff.m_WaitForUserBuffListHead.RemoveTailList();
    }

    if (m_RecvBuff.m_MsgBuffer != NULL)
    {
        for(ULONG i =0; i<m_RecvBuff.m_RingSize; i++)
        {
            if (m_RecvBuff.m_MsgBuffer[i].m_Data!= NULL)
            {
                delete []m_RecvBuff.m_MsgBuffer[i].m_Data;
            }
        }
        delete []m_RecvBuff.m_MsgBuffer;
        m_RecvBuff.m_MsgBuffer = NULL;
    }
    FIP_PRINT(TRACE_LEVEL_VERBOSE, FIP_SEND_RECV, "DestroyRecvBuffers Done.\n");
}


VOID
SendRecv::DestroyIBResources( VOID )
{
    ib_api_status_t IBStatus = IB_SUCCESS;

    if (m_IBRes.m_h_av != NULL) 
    {
        DestroyAv();
    }

    //
    //  Destroy MR
    //
    if (m_IBRes.m_h_mr != NULL) {
        IBStatus = ib_dereg_mr(m_IBRes.m_h_mr);
        if(IBStatus != IB_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_dereg_mr failed. statue=0x%x\n", IBStatus);
        }
    }

    //
    //  Destroy QP
    //
    if (m_IBRes.m_h_qp != NULL) {
        IBStatus = ib_destroy_qp(m_IBRes.m_h_qp, NULL);
        if(IBStatus != IB_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "destroy_qp failed. statue=0x%x\n", IBStatus);
        }
    }

    //
    //  Destroy receve cq
    //
    if (m_IBRes.m_h_recv_cq != NULL) {
        IBStatus = ib_destroy_cq(m_IBRes.m_h_recv_cq, NULL);
        if(IBStatus != IB_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "destroy_cq rcv failed. statue=0x%x\n", IBStatus);
        }
    }

    //
    //  Destroy send cq
    //
    if (m_IBRes.m_h_send_cq != NULL) {
        IBStatus = ib_destroy_cq(m_IBRes.m_h_send_cq, NULL);
        if(IBStatus != IB_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "destroy_cq send failed. statue=0x%x\n", IBStatus);
        }
    }

    //
    //  Dealloc PD
    //
    if (m_IBRes.m_h_pd != NULL) {
        IBStatus = ib_dealloc_pd(m_IBRes.m_h_pd, NULL);
        if(IBStatus != IB_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "dealloc_pd failed. statue=0x%x\n", IBStatus);
        }
    }

    //
    //  Dealloc CA
    //
    if (m_IBRes.m_h_ca != NULL) {
        IBStatus = ib_close_ca(m_IBRes.m_h_ca, NULL);
        if(IBStatus != IB_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "close_ca failed. statue=0x%x\n", IBStatus);
        }
    }

    FIP_PRINT(TRACE_LEVEL_VERBOSE, FIP_SEND_RECV, "DestroyIBResources Done.\n");
}

NTSTATUS 
SendRecv::AttachMcast(
    const   ib_gid_t *p_mcast_gid,
    const   ib_net16_t  mcast_lid,
    al_attach_handle_t   *ph_attach
    )
{
    ib_api_status_t  IBStatus = IB_SUCCESS;
    IBStatus = al_attach_mcast(m_IBRes.m_h_qp, p_mcast_gid, mcast_lid, ph_attach, NULL);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "al_attach_mcast failed. statue=0x%x\n", IBStatus);
        return STATUS_ADAPTER_HARDWARE_ERROR;
    }

    return STATUS_SUCCESS;
}


VOID 
SendRecv::DetachMcast(
    al_attach_handle_t   ph_attach
    )
{
    ph_attach->obj.pfn_destroy( &ph_attach->obj, ib_sync_destroy );

}


NTSTATUS 
SendRecv::CreateMcastAv(
    ib_member_rec_t* const      p_member_rec,
    ib_av_handle_t* const       ph_av )
{
    NTSTATUS Status = ::CreateMcastAv(m_IBRes.m_h_pd, m_IBRes.m_PortNumber, p_member_rec, ph_av);
    if (!NT_SUCCESS(Status)) 
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "CreateMcastAv failed Status = 0x%x\n", Status);
    }
    return Status;
}



VOID
SendRecv::ShutDown( VOID )
{
    m_WasShutDownCalled = true;

    if (ModifyQPToError() != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Failed to Modify QP To Reset\n");
    }

    WaitForRcvToEnd();
    DestroyRecvBuffers();
    DestroySendBuffers();
    DestroyIBResources();
    FIP_PRINT(TRACE_LEVEL_VERBOSE, FIP_SEND_RECV, "ShutDown Done.\n");
}


VOID
SendRecv::PollAllSndCQ( VOID )
{
    NTSTATUS Status;
    LARGE_INTEGER SleepTime;
    
    while ((m_SendBuff.m_RingSize - m_SendBuff.m_FreeBuffListHead.Size()) != 0)
    {
        Status = PollSndCQ();
        if ((Status != STATUS_SUCCESS) && (Status != STATUS_PIPE_EMPTY))
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "PollSndCQ failed.\n");
            break;
        }
        SleepTime =  TimeFromLong(1000);    // To reduce CPU utilization while cleaning all Snd CQ
        KeDelayExecutionThread(KernelMode, FALSE, &SleepTime);
    }

    ASSERT((m_SendBuff.m_RingSize - m_SendBuff.m_FreeBuffListHead.Size()) == 0);
}


/*
 * Poll Max 'x_MaxPollSndAtOnce' and return the number of polled CQs to 'NumOfPolledCQ'.
 */
NTSTATUS
SendRecv::PollSndCQ( VOID )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ib_api_status_t IBStatus = IB_SUCCESS;
    ib_wc_t wc[x_MaxPollSndAtOnce], *pFree, *pWc;
    ULONG NumToPollSndCQ = min(x_MaxPollSndAtOnce, (m_SendBuff.m_RingSize - m_SendBuff.m_FreeBuffListHead.Size()));
    ULONG NumOfPolledCQ = 0;

    if (NumToPollSndCQ == 0)    // Nothing to Poll
        return Status; // Should I return STATUS_PIPE_EMPTY ?

    for(ULONG i = 0; i < NumToPollSndCQ; i++ )
    {
        wc[i].p_next = &wc[i + 1];
    }
    wc[NumToPollSndCQ - 1].p_next = NULL;

    pFree = wc;

    IBStatus = ib_poll_cq(m_IBRes.m_h_send_cq, &pFree, &pWc);
    if(IBStatus != IB_SUCCESS)
    {
        if (IBStatus == IB_NOT_FOUND)
        {
            FIP_PRINT(TRACE_LEVEL_WARNING, FIP_SEND_RECV, 
                "Nothing to poll althogh we tough otherwise, NumToPollSndCQ (%d)\n", NumToPollSndCQ);
            return STATUS_PIPE_EMPTY;
            
        }

        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "poll_cq failed. statue=0x%x\n", IBStatus);
        m_CqError = true; // Also send a event to the user about this error
        KeSetEvent(m_CqErrorEvent, IO_NO_INCREMENT, FALSE);
        Status = STATUS_ADAPTER_HARDWARE_ERROR;
        goto poll_cq_err;
    }

    while (pWc != NULL)
    {
        if (pWc->status != IB_WCS_SUCCESS)
        {
            m_QPStateLock.Lock();
            if ((pWc->status == IB_WCS_WR_FLUSHED_ERR) && (m_IsQpStateAtError))
            {   // wr_id should be valid, we still want to handle it
                FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_SEND_RECV, "Got send complition with flush error\n");
            }
            else
            {
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "send completion, pWc->status = %d\n", pWc->status);
                m_CqError = true; // Also send a event to the user ab this error
                KeSetEvent(m_CqErrorEvent, IO_NO_INCREMENT, FALSE);
            }
            m_QPStateLock.Unlock();
        }

        // Add the buffers to the Free list
        ReturnSendMassegeToFreeBuff((SndMsg*)pWc->wr_id);

        NumOfPolledCQ++;
        pWc = pWc->p_next;
    }
    
    if(m_CqError == true)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "poll_cq failed.\n");
        goto poll_cq_err;
    }
    return Status;

poll_cq_err:
    // If error occur we don't have the pointer to the buffer, 
    // we can't add it to the list
    return Status;
}


/*
 * Clean the data from the prev msg
 * and add it to the free buffer list
 */
inline VOID
SendRecv::ReturnSendMassegeToFreeBuff(
    SndMsg* pBuffEntry,
    ULONG MsgSize
    )
{
    memset(pBuffEntry->m_AlignedData, 0, (sizeof(CHAR) * MsgSize));
    m_SendBuff.m_FreeBuffListHead.InsertTailList(&pBuffEntry->m_Entry);
}


/*
 * Choose one entry from the list (the head),
 * prepare it for send, remove from the free list 
 * Copy the new msg to private buffer
 * and return its pointer.
 */
inline SendRecv::SndMsg*
SendRecv::PrepareToSendMassege(
    PCHAR pSndMsg,
    ULONG MsgSize
    )
{
    LIST_ENTRY * pBuff = m_SendBuff.m_FreeBuffListHead.Head();
    SndMsg* pBuffEntry = CONTAINING_RECORD(pBuff, SndMsg, m_Entry);

    memcpy(pBuffEntry->m_AlignedData, pSndMsg, MsgSize);
    m_SendBuff.m_FreeBuffListHead.RemoveEntryList(pBuff);
    return pBuffEntry;
}

/*
 * Send a single message
 */
    NTSTATUS
SendRecv::Send(
    ib_av_handle_t av,
    ib_net32_t remote_qp,
    PCHAR pSndMsg,
    ULONG MsgSize
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ib_api_status_t IBStatus = IB_SUCCESS;
    ib_send_wr_t p_send_wr;
    ib_local_ds_t ds_array;
    bool AnyError = m_IsQpStateAtError || m_WasShutDownCalled || m_CqError;

    ASSERT(pSndMsg != NULL);
    ASSERT(MsgSize <= MAX_UD_MSG);
    ASSERT(!AnyError);

    if (av == NULL)
    {
        av = m_IBRes.m_h_av;
    }
    ASSERT(av != NULL);

    // Before send the msg we poll some CQ and check for room for the new msg
    Status = PollSndCQ();
    if ((Status != STATUS_SUCCESS) && (Status != STATUS_PIPE_EMPTY))
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "PollSndCQ Failed\n");
        goto send_err;
    }
    Status = STATUS_SUCCESS;

    if (AnyError)
    {
        return STATUS_ADAPTER_HARDWARE_ERROR;
    }

    if (m_SendBuff.m_FreeBuffListHead.Size() == 0)
    {   // If Send Queue is empty we'll exit with error
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Free Send Queue is full\n");
        Status = STATUS_PRINT_QUEUE_FULL;
        goto send_err;
    }

    // Copy the msg to private buffer and update counters
    SndMsg *MsgToSend =  PrepareToSendMassege(pSndMsg, MsgSize);

    // Send msg
    memset(&p_send_wr, 0, sizeof(p_send_wr));
    p_send_wr.wr_type = WR_SEND;
    p_send_wr.wr_id = (uint64_t)(UINT_PTR)MsgToSend; // pointer to the buffer

    ds_array.length = MsgSize;
    ds_array.vaddr = MsgToSend->m_PhyAddr;   // Get Phy Address
    ds_array.lkey = m_IBRes.m_lkey;
    p_send_wr.ds_array = &ds_array;

    p_send_wr.num_ds = 1;   // Single msg each time
    p_send_wr.send_opt = IB_SEND_OPT_SIGNALED; // Completion for each msg
    p_send_wr.dgrm.ud.h_av = av;
    p_send_wr.dgrm.ud.pkey_index = m_IBRes.m_PkeyIndex;
    p_send_wr.dgrm.ud.remote_qkey = m_IBRes.m_qkey;
    p_send_wr.dgrm.ud.remote_qp = remote_qp;

    IBStatus = ib_post_send(m_IBRes.m_h_qp, &p_send_wr, NULL);
    if (IBStatus != IB_SUCCESS)
    {
        ReturnSendMassegeToFreeBuff(MsgToSend, MsgSize);
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "post_send Failed\n");
        Status = STATUS_ADAPTER_HARDWARE_ERROR;
        goto send_err;
    }

    ASSERT(NT_SUCCESS(Status));
    return Status;

send_err:
    return Status;
}


/*
 * This is the main function (CB) to process recv msg.
 * Process Receive Message Function.
 * This function will process maximun recv msg.
 * It will stop process when it all recv msg were 
 * processed or if the buffer is full.
 * Once the process is complete it will raise an 
 * event to that all msg were processed.
 */
VOID
SendRecv::ProcessRecvPacket( VOID )
{
    ib_api_status_t IBStatus = IB_SUCCESS;
    ib_wc_t wc[x_MaxPollRcvAtOnce], *pFree, *pWc;
    ULONG NumToPollRcvCQ = min(x_MaxPollRcvAtOnce, (m_RecvBuff.m_NumAtPostRecv));
    ULONG NumPolledFromPostRecv = 0;

    if (NumToPollRcvCQ == 0)    // According to my counters nothing to poll
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "No post recv were made, Aborting.\n");
        ASSERT(FALSE);
    }

    while (IBStatus == IB_SUCCESS)
    {   // Will poll recv CQ untill all msg were polled
        for(ULONG i = 0; i < NumToPollRcvCQ; i++ )
        {
            wc[i].p_next = &wc[i + 1];
        }
        wc[NumToPollRcvCQ - 1].p_next = NULL;

        pFree = wc;
        
        IBStatus = ib_poll_cq(m_IBRes.m_h_recv_cq, &pFree, &pWc);
        if ((IBStatus != IB_SUCCESS) && (IBStatus != IB_NOT_FOUND))
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_poll_cq failed.\n");
            m_CqError = true;
            KeSetEvent(m_CqErrorEvent, IO_NO_INCREMENT, FALSE);
            ASSERT(FALSE);
            goto rcv_err;
        }

        while (pWc != NULL)
        {
            if(pWc->status != IB_WCS_SUCCESS)
            {
                m_QPStateLock.Lock();
                if ((pWc->status == IB_WCS_WR_FLUSHED_ERR) && (m_IsQpStateAtError))
                {   // wr_id should be valid, we still want to handle it
                    FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_SEND_RECV, "Got send complition with flush error\n");
                    IBStatus = IB_SUCCESS;
                }
                else
                {
                    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Received corrupted WC.Status=%d\n",pWc->status);
                    m_CqError = true;
                    KeSetEvent(m_CqErrorEvent, IO_NO_INCREMENT, FALSE);
                    ASSERT(FALSE);
                }
                m_QPStateLock.Unlock();
            }

            if (!m_CqError)
            {
                MoveRcvMsgToWaitForUser( pWc );
            }
            pWc = pWc->p_next;
            NumPolledFromPostRecv++;
        }

    }

    IBStatus = ib_rearm_cq(m_IBRes.m_h_recv_cq, false);
    if(IBStatus != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_rearm_cq failed. Status=%d\n",IBStatus);
        m_CqError = true;
        KeSetEvent(m_CqErrorEvent, IO_NO_INCREMENT, FALSE);
        ASSERT(FALSE);
    }

    m_WaitForRcvShutdownLock.Lock();
    m_RecvBuff.m_NumAtPostRecv -= NumPolledFromPostRecv;

    if (m_WaitForRcvToEnd == false) // At ShutDown this paramter will be true
    {   // The user gets the event
        KeSetEvent(m_MsgArrivedEvent, IO_NO_INCREMENT, FALSE);
    }
    else
    {
        if (m_RecvBuff.m_NumAtPostRecv == 0)
        {   // The 'cleaner' gets the event
            KeSetEvent(&m_WaitForRcvEvent, IO_NO_INCREMENT, FALSE);
        }
    }
    m_WaitForRcvShutdownLock.Unlock();
rcv_err:
    return;
}


/*
 * In this function the user can return 
 * a single msg buffer after using it.
 * The user must return every buffer 
 * before calling shutdown.
 */
VOID
SendRecv::ReturnBuffer(
    RcvMsg *pRcvMsg
    )
{
    MoveRcvMsgFromUser(pRcvMsg);
}

/*
 * Receive Message Function
 * Return to the user a single Msg
 * If no message is availiable we return NULL
 * and STATUS_PIPE_EMPTY.
 */
NTSTATUS
SendRecv::Recv(
    RcvMsg **pRcvMsg
    )
{
    bool AnyError = m_IsQpStateAtError || m_WasShutDownCalled || m_CqError;
    ASSERT(pRcvMsg != NULL);


    *pRcvMsg = NULL;
    
    if (m_RecvBuff.m_WaitForUserBuffListHead.Size() == 0)
    {   // if no msg left we check for Error state before returning status
        FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_SEND_RECV, "No Message is available at the moment\n");
        return (AnyError) ? STATUS_ADAPTER_HARDWARE_ERROR : STATUS_PIPE_EMPTY;
    }

    *pRcvMsg = MoveRcvMsgToUser();
    return STATUS_SUCCESS;
}


/*
 * The user can create it only one time and use it all other time
 * In case the user have to change the AV he have to 
 * destroy and recreate it.
 */
NTSTATUS 
SendRecv::SetAndCreateAV(
    ib_net16_t dlid // Must be given in Host order
     )
{
    ib_api_status_t ib_status;
    ib_av_attr_t    av_attr;

    ASSERT(m_IBRes.m_h_av == NULL); // The user already created one AV and didn't destroy it - Memory leak!!!
    memset(&av_attr, 0, sizeof(av_attr));

    av_attr.port_num = m_IBRes.m_PortNumber;
    av_attr.grh.hop_limit = DEF_HOP_LIMIT;
    av_attr.path_bits = 0;
    // TODO: Use Maximun Speed (0)
    av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
    av_attr.dlid = cl_hton16(dlid);
    av_attr.grh_valid = FALSE;

    ib_status = ib_create_av(m_IBRes.m_h_pd, &av_attr, &m_IBRes.m_h_av);
    if(ib_status != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "ib_create_av failed. statue=0x%x\n", ib_status);
        goto av_err;
    }
    return STATUS_SUCCESS;

av_err:
    return STATUS_ADAPTER_HARDWARE_ERROR;
}


VOID
SendRecv::DestroyAv( VOID )
{
    ASSERT(m_IBRes.m_h_av != NULL);
    ib_destroy_av(m_IBRes.m_h_av);
    m_IBRes.m_h_av = NULL;
}


