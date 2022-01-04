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

#pragma once

#define MAX_UD_MSG      4096
#define GRH_LEN         40
#define DEF_HOP_LIMIT   63

/* These parameters decide the maximun PollSndCQ each iteration */
const ULONG x_MaxPollRcvAtOnce = 16;
const ULONG x_MaxPollSndAtOnce = 2;

// Define the current state of the Recv Msg
/*
    The state can be chage in this way:
        Init --> PostRecv <--> PollSndCQ --> RecvBuffer <--> ReturnBuffer --> PostRecv
 */
typedef enum RcvMsgState
{
    AtInit          = 0,    // This state is only valid at the init state 
    AtPostRecv      = 1,
    WaitingForUser  = 2,
    AtUser          = 3
};

// This is the class the user will get when calling Recv function
class RcvMsg
{
public:
    friend class SendRecv; // Only this class is permitted to change index & state

    RcvMsg() :
        m_Len(0),
        m_AlignedData(NULL),
        m_Data(NULL),
        m_ImmediateData(0),
        m_RemoteQp(0),
        m_PkeyIndex(0),
        m_RemoteLid(0),
        m_RemoteSl(0),
        m_PathBits(0),
        m_MsgState(AtInit)
    {
        memset(&m_RecvOpt, 0, sizeof(m_RecvOpt)); // Init to invalid value
        memset(&m_Wr, 0, sizeof(m_Wr));
        memset(&m_LocalDs, 0, sizeof(m_LocalDs));
        InitializeListHead(&m_ExtenalEntry);
        InitializeListHead(&m_Entry);
    }

    VOID PrintDbgData( VOID );

    // The msg itself
    UINT            m_Len;
    PCHAR           m_AlignedData;  // This parameter holds the msg for recv
    PCHAR           m_Data;         // We use this parameter to alloc the memory for the msg

    // More paramters for the user handling
    ib_recv_opt_t   m_RecvOpt;
    ib_net32_t      m_ImmediateData;
    ib_net32_t      m_RemoteQp;
    uint16_t        m_PkeyIndex;
    ib_net16_t      m_RemoteLid;
    uint8_t         m_RemoteSl;
    uint8_t         m_PathBits;
    LIST_ENTRY      m_ExtenalEntry;

private:
    static char* RcvMsgStateToStr( 
        RcvMsgState State );
    
    // These paramters are used for handling Work-Request
    ib_recv_wr_t    m_Wr;
    ib_local_ds_t   m_LocalDs;

    // This parameter is used for internal buffer handling
    RcvMsgState     m_MsgState; // For debugging

    LIST_ENTRY      m_Entry;
};


class SendRecv
{
    class SndMsg
    {
    public:
        SndMsg() : 
            m_PhyAddr(0),
            m_AlignedData(NULL),
            m_Data(NULL)
        {
            InitializeListHead(&m_Entry);
        }
    
        PCHAR       m_AlignedData;  // This parameter holds the msg to send
        PCHAR       m_Data;         // We use this parameter to alloc the memory for the msg
        uint64_t    m_PhyAddr;      // Save the physical address of data for the CX2 access
        LIST_ENTRY  m_Entry;        // we'll handle a list for all the waiting
    };

    // send msg 
    class SendBuff
    {
    public:
        SendBuff() :
            m_RingSize(0),
            m_MsgBuffer(NULL)
            { m_FreeBuffListHead.Init(); }

        ULONG       m_RingSize;   // Max buffer that can wait at the CQ (Min 1)
        SndMsg*     m_MsgBuffer; // Buffer to hold all messeges until we poll-cq them 
        LinkedList  m_FreeBuffListHead; // we'll handle a list for all the waiting
    };

    // receive msg 
    class RecvBuff {
    public:
        RecvBuff() :
            m_RingSize(0),
            m_NumAtPostRecv(0),
            m_NumAtUser(0),
            m_MsgBuffer(NULL)
            { m_WaitForUserBuffListHead.Init(); }

        ULONG       m_RingSize;                 // Max buffer that can wait at the CQ (Min 1)
        LONG        m_NumAtPostRecv;            // Nuber of buffers that are in use at the qp post recv (init to 0)
        ULONG       m_NumAtUser;                // Nuber of buffers that are in use at the user (init to 0)
        RcvMsg*     m_MsgBuffer;                // Buffer to hold all messeges until we poll-cq them
        LinkedList  m_WaitForUserBuffListHead;  // we'll handle a list for all the waiting to process (to the user)
    };
 
    // IB Resources Parameters
    class IBResources 
    {
    public:
        IBResources() :
            m_h_ca(NULL),
            m_h_pd(NULL),
            m_h_recv_cq(NULL),
            m_h_send_cq(NULL),
            m_h_qp(NULL),
            m_h_av(NULL),
            m_h_mr(NULL),
            m_qpn(0),
            m_PortGuid(0),
            m_lkey(0),
            m_PkeyIndex(0),
            m_qkey(0),
            m_PortNumber(0)
        {
        }

        ib_ca_handle_t  m_h_ca;
        ib_pd_handle_t  m_h_pd;
        ib_cq_handle_t  m_h_recv_cq;
        ib_cq_handle_t  m_h_send_cq;
        ib_qp_handle_t  m_h_qp;
        ib_av_handle_t  m_h_av; // For Create & set AV
        net32_t         m_qpn;
        ib_net64_t      m_PortGuid;

        ib_mr_handle_t  m_h_mr;
        uint32_t        m_lkey;

        uint16_t        m_PkeyIndex;
        ib_net32_t      m_qkey;

        uint8_t         m_PortNumber;
    };


class Statistics 
{
};


public:
    SendRecv( VOID ) :
        m_CqError(false),
        m_IsQpStateAtError(false),
        m_WasShutDownCalled(true),
        m_WaitForRcvToEnd(false)
        { }
    
    ~SendRecv( VOID )
    {
        ASSERT(m_WasShutDownCalled == true); 
    }
    
    NTSTATUS
    Init(
        ib_al_handle_t h_al,
        ib_net64_t PortGuid,
        ULONG SendRingSize,
        ULONG RecvRingSize,
        ib_net32_t qkey,
        uint16_t PkeyIndex,
        KEVENT *MsgArrivedEvent,
        KEVENT *CqErrorEvent
        );
    
    NTSTATUS Send( 
        ib_av_handle_t av, 
        ib_net32_t remote_qp, 
        PCHAR pSndMsg, 
        ULONG MsgSize 
        );
    
    VOID ReturnBuffer( RcvMsg *pRcvMsg );
    
    NTSTATUS Recv( RcvMsg **pRcvMsg );
    
    ib_pd_handle_t GetHandlePD( VOID )
    { 
        return m_IBRes.m_h_pd; 
    }

    net32_t GetQpn( VOID ) { return m_IBRes.m_qpn; }

    NTSTATUS SetAndCreateAV(
        ib_net16_t dlid
         );

    VOID DestroyAv( VOID );

    bool IsAvValid( VOID ) { return (m_IBRes.m_h_av != NULL); }

    NTSTATUS AttachMcast(
        const   ib_gid_t *p_mcast_gid,
        const   ib_net16_t  mcast_lid,
        al_attach_handle_t   *ph_attach
        );


    VOID DetachMcast(
        al_attach_handle_t   ph_attach
        );


    NTSTATUS CreateMcastAv(
        ib_member_rec_t* const      p_member_rec,
        ib_av_handle_t* const       ph_av );


    VOID DestroyMcastAv(ib_av_handle_t const       ph_av) {
        ib_destroy_av(ph_av);
    }

    VOID ShutDown( VOID ); 

    KEVENT          *m_MsgArrivedEvent;
    KEVENT          *m_CqErrorEvent;

private:
    // C'tor & D'tor private Functions
    NTSTATUS ModifyQPResetToRTS( VOID );
    
    NTSTATUS ModifyQPToError( VOID );
    
    NTSTATUS InitIBResources( 
        ib_al_handle_t h_al, 
        ib_net64_t CAGuid, 
        ib_net64_t PortGuid, 
        uint8_t PortNumber, 
        ULONG SendRingSize, 
        ULONG RecvRingSize, 
        ib_net32_t qkey, 
        uint16_t PkeyIndex 
        );
    
    NTSTATUS InitSendBuffers( ULONG SendRingSize );
    
    NTSTATUS InitRecvBuffers( ULONG RecvRingSize );
    
    VOID DestroyIBResources( VOID );
    
    VOID DestroySendBuffers( VOID );
    
    VOID WaitForRcvToEnd( VOID );

    VOID DestroyRecvBuffers( VOID );

    // Send private Functions
    VOID PollAllSndCQ( VOID );

    NTSTATUS PollSndCQ( VOID );

    inline SendRecv::SndMsg* PrepareToSendMassege( 
        PCHAR pSndMsg, 
        ULONG MsgSize 
        );

    inline VOID ReturnSendMassegeToFreeBuff( 
        SndMsg* pBuffEntry,
        ULONG MsgSize = MAX_UD_MSG
        );

    // Receive private Functions
    NTSTATUS MoveRcvMsgToPostRecv(
        RcvMsg *pRcvMsg
        );

    VOID MoveRcvMsgToWaitForUser(
        ib_wc_t *pWc
        );

    RcvMsg* MoveRcvMsgToUser( VOID );

    VOID MoveRcvMsgFromUser(
        RcvMsg *pRcvMsg
        );

    VOID ProcessRecvPacket( VOID );

    static VOID CbReceive(
        const ib_cq_handle_t h_cq,
        PVOID CqContext
        );

private:
    bool        m_CqError;
    bool        m_IsQpStateAtError;
    bool        m_WasShutDownCalled;
    bool        m_WaitForRcvToEnd;

    CSpinLock   m_RcvListLock;
    CSpinLock   m_WaitForRcvShutdownLock;
    CSpinLock   m_QPStateLock;

    KEVENT      m_WaitForRcvEvent;

    IBResources m_IBRes;    // All IB Resources
    SendBuff    m_SendBuff;  
    RecvBuff    m_RecvBuff; 
    Statistics  m_Statistics; // For Statistics use only
};

