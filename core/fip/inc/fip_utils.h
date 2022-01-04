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
    mp_rcv.cpp

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/

#pragma once

const int GUID_LEN          = 8;
const int GID_PREFIX_LEN    = 5;
const int HW_ADDR_LEN       = 6;

#define deref_request(_p_req_)\
    mcast_mgr_request_release(_p_req_, __FILE__, __LINE__)

typedef enum ClassType_t {
    PORT_CLASS = 0,
    GW_CLASS,
    VNIC_CLASS
};


typedef union {
	struct mgid {
		u8 mgid_prefix[GID_PREFIX_LEN];
		u8 type;
		u8 dmac[HW_ADDR_LEN];
		u8 rss_hash;
		u8 vhub_id[3];
	} mgid;
	ib_gid_t gid;
} VhubMgid_t;


char *
GetClassTypeStr(
    ClassType_t ClassType
    );

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
    );

NTSTATUS 
CreateMcastAv(
    ib_pd_handle_t              h_pd,
    uint8_t                     port_num,
    ib_member_rec_t* const      p_member_rec,
    ib_av_handle_t* const       ph_av
    );

inline u64
U8_TO_U64(
    u8 Guid[GUID_LEN]
    )
{
    return (u64)(((u64)Guid[0] << 0)  |
                 ((u64)Guid[1] << 8)  |
                 ((u64)Guid[2] << 16) |
                 ((u64)Guid[3] << 24) |
                 ((u64)Guid[4] << 32) |
                 ((u64)Guid[5] << 40) |
                 ((u64)Guid[6] << 48) |
                 ((u64)Guid[7] << 56));
}


// Multiple the paramter in 2.5
inline u64
Mult2_5(
    u64 KAPeriod
    )
{
    return (5 * KAPeriod / 2);
}


/*
 * copy string b to string a and NULL termination.
 * length a must be >= length b+1.
 */
inline VOID
MemcpyStr(
    PCHAR a,
    const PCHAR b,
    const size_t size)
{
    memcpy(a, b, size - 1);
    a[size - 1] = '\0';
}

ib_api_status_t
fip_mcast_mgr_leave_mcast(
    mcast_mgr_request_t *McastReq
    );


bool
GenericJoinMcastCallBack(
    bool *pMcastConnected,
    struct McastMessageData *pMcastMessageData,
    VhubMgid_t *pVhubMgid,
    mcast_mgr_request_t **ppMcastRequest,
    al_attach_handle_t *pMcastHandleAattach,
    ib_av_handle_t *pMcastAv,
    class SendRecv *pSendRecv
    );


