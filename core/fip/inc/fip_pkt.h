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
    fip_pkt.h

Abstract:
    This module contains miniport Gateway routines

Revision History:

Notes:

Author:
    Sharon Cohen

--*/

#pragma once

const int VNIC_VENDOR_LEN        = 8;
const int VNIC_SYSTEM_NAME_LEN   = 32;
const int VNIC_GW_PORT_NAME_LEN  = 8;
const int VNIC_NAME_LEN          = 16;
const int RANDOM_TIME_PERIOD     = 100;
const int GID_LEN                = 16;
const int UNIQE_GW_LEN           = 256;

const UCHAR x_FIP_VENDOR_ID_MELLANOX[VNIC_VENDOR_LEN] = {0x4d, 0x65, 0x6c, 0x6c, 0x61, 0x6e, 0x6f, 0x78};

/**************************************************************************/
/*                           packet format typedef structs                        */
/**************************************************************************/
typedef struct {
    u8 version;
    u8 reserved[3];
} FipVer_t;

typedef struct {
    u8 type;
    u8 length;
    u8 reserved[2];
} FipType_t;

typedef struct {
    u16 opcode;
    u8 reserved;
    u8 subcode;
    u16 list_length;
    u16 flags;
    FipType_t type;
    u8 vendor_id[VNIC_VENDOR_LEN];
} FipHeader_t;

typedef struct {
    FipType_t type;
    u8 vendor_id[VNIC_VENDOR_LEN];
    u32 qpn;
    u16 sl_port_id;
    u16 lid;
    u8 guid[GUID_LEN];
} FipDiscoverBase_t;

typedef struct {
    FipVer_t version;
    FipHeader_t fip;
    FipDiscoverBase_t base;
} FipSoloicit_t;


typedef struct {
    FipType_t type;
    u8 vendor_id[VNIC_VENDOR_LEN];
    u8 flags;
    u8 n_rss_mgid__n_tss_qpn;   // We don't care for these params
    u16 n_rss_qpn__num_net_vnics;
} FipGwInfo_t;

typedef struct {
    FipType_t type; 
    u8 vendor_id[VNIC_VENDOR_LEN];
    u8 system_guid[GUID_LEN];
    u8 system_name[VNIC_SYSTEM_NAME_LEN];
    u8 gw_port_name[VNIC_GW_PORT_NAME_LEN];
} FipAdvGwInfo_t;

typedef struct {
    FipType_t type; 
    u8 vendor_id[VNIC_VENDOR_LEN];
    u32 gw_adv_period;
    u32 gw_period;
    u32 vnic_ka_period;
} FipAdvKAInfo_t;

typedef struct {
    FipVer_t version;
    FipHeader_t fip;
    FipDiscoverBase_t base;
    FipGwInfo_t FipGwInfo;
    FipAdvGwInfo_t gw_info;
    FipAdvKAInfo_t ka_info;
    // There are more - Ext. Desc, Ext. Boot, Ext. LAG & Ext. Power-Cycle
} FipAvd_t;

typedef struct {
    FipType_t type;
    u8 vendor_id[VNIC_VENDOR_LEN];
    u16 mtu;
    u16 vnic_id;
    u16 flags_vlan;
    u8 mac[ETH_ALEN];
    u8 eth_gid_prefix[GID_PREFIX_LEN];
    u8 antispoofing;
    u16 vfields; //ilp, il, rss, n_mac_mcgid
    u32 syn_ctrl_qpn;   // 8 bits for Syndron, 24 for ctrl-qpn
    u8 vnic_name[VNIC_NAME_LEN];
} VnicLogin_t;

typedef struct {
    FipVer_t version;
    FipHeader_t fip;
    FipDiscoverBase_t base;
    VnicLogin_t VnicLogin;
} FipLogin_t;

typedef struct {
    FipType_t type;
    u8 vendor_id[VNIC_VENDOR_LEN];
    u16 reserved;
    u16 pkey;
} FipLoginAckPartition_t;

typedef struct{
    FipLogin_t login;
    FipLoginAckPartition_t partition;
} FipLoginAck_t ;

typedef struct {
    u8 flags;
    u8 reserved;
    u8 mac[ETH_ALEN];
    u32 qpn;
    u8 reserved1;
    u8 sl;
    u16 lid;
} FipContextEntry_t;

union FipVhubId_t {
    struct _flags {
        u8 flags;
        u8 reserved[3];
    } flags;
    u32 vhub_id;
};

typedef struct {
    FipType_t type;
    u8 vendor_id[VNIC_VENDOR_LEN];
    FipVhubId_t vhub_id;
    u32 tusn;
} FipVhubUpdateParam_t;

typedef struct {
    FipVer_t version;
    FipHeader_t fip;
    FipVhubUpdateParam_t update;
} FipVhubUpdate_t;

typedef struct {
    FipType_t type;
    u8 vendor_id[VNIC_VENDOR_LEN];
    FipVhubId_t vhub_id;
    u32 tusn;
    u8 flags;
    u8 reserved;
    u16 table_size;
} FipVhubTableParam_t;

typedef struct {
    FipVer_t version;
    FipHeader_t fip;
    FipVhubTableParam_t table;
    // here come the context entries 
} FipVhubTable_t;


/* this is the number of DWORDS to subtract from type_1->length
 * to get the size of the entries / 4. (size in dwords from start
 * of vendor_id field until the first context entry + 1 for checksum
 */
//const int  FIP_TABLE_SUB_LENGTH 6

typedef struct {
    FipType_t type;
    u8 vendor_id[VNIC_VENDOR_LEN];
    u8 flags;
    u8 vhub_id[3];
    u32 tusn;
    u16 vnic_id;
    u8 mac[ETH_ALEN];
    u8 port_guid[GUID_LEN];
    u8 vnic_name[VNIC_NAME_LEN];
} FipVnicKA_t;

/*
 * FipHostUpdate_t will be used for vHub context requests,
 * keep alives and logouts
 */
typedef struct {
    FipVer_t version;
    FipHeader_t fip;
    FipVnicKA_t fip_vnic_ka;
} FipHostUpdate_t;


enum FipPacketFields {
    EOIB_FIP_OPCODE = 0xFFF9,
    FIP_CTRL_HDR_MSG_LEGTH = 7,
    FIP_FIP_HDR_LENGTH = 3,
    FIP_FIP_HDR_TYPE = 13,

    FIP_HOST_SOL_SUB_OPCODE = 0x1,
    FIP_GW_ADV_SUB_OPCODE = 0x2,
    FIP_HOST_LOGIN_SUB_OPCODE = 0x3,
    FIP_GW_LOGIN_SUB_OPCODE = 0x4,
    FIP_HOST_LOGOUT_SUB_OPCODE = 0x5,
    FIP_GW_UPDATE_SUB_OPCODE = 0x6,
    FIP_GW_TABLE_SUB_OPCODE = 0x7,
    FIP_HOST_ALIVE_SUB_OPCODE = 0x8,

    FIP_FIP_FCF_FLAG = 0x1,
    FIP_FIP_SOLICITED_FLAG = 0x2,
    FIP_FIP_ADVRTS_FLAG = 0x4,
    FIP_FIP_FP_FLAG = 0x80,
    FIP_FIP_SP_FLAG = 0x40,

    FIP_BASIC_LENGTH = 7,
    FIP_BASIC_TYPE = 240,

    FIP_ADVERTISE_LENGTH_1 = 4,
    FIP_ADVERTISE_TYPE_1 = 241,
    FIP_ADVERTISE_HOST_VLANS = 0x80,
    FIP_ADVERTISE_NUM_VNICS_MASK = 0x0FFF,
    FIP_ADVERTISE_N_RSS_SHIFT = 12,
    FIP_ADVERTISE_HOST_EN_MASK = 0x80,
    FIP_ADVERTISE_GW_PORT_ID_MASK = 0x0FFF,
    FIP_ADVERTISE_SL_SHIFT = 12,

    FIP_ADVERTISE_GW_LENGTH = 15,
    FIP_ADVERTISE_GW_TYPE = 248,

    FIP_ADVERTISE_KA_LENGTH = 6,
    FIP_ADVERTISE_KA_TYPE = 249,

    FIP_HADMINED_VLAN = 1 << 3, /* H bit set in advertise pkt */

    FIP_LOGIN_LENGTH = 13,
    FIP_LOGIN_TYPE = 242,
    FIP_LOGIN_PARTITION_LENGTH = 4,
    FIP_LOGIN_PARTITION_TYPE = 246,

    FIP_LOGIN_V_FLAG = 0x8000,
    FIP_LOGIN_M_FLAG = 0x4000,
    FIP_LOGIN_VP_FLAG = 0x2000,
    FIP_LOGIN_H_FLAG = 0x1000,
    FIP_PORT_ID_MASK = 0x0FFF,
    FIP_LOGIN_VLAN_MASK = 0x0FFF,
    FIP_LOGIN_DMAC_MGID_MASK = 0x3F,
    FIP_LOGIN_RSS_MGID_MASK = 0x0F,
    FIP_LOGIN_RSS_MASK = 0x10,
    FIP_LOGIN_RSS_SHIFT = 8,
    FIP_LOGIN_CTRL_QPN_MASK = 0xFFFFFF,
    FIP_LOGIN_VNIC_ID_BITS = 16,
    FIP_LOGIN_IPL_SHIFT = 14,
    FIP_LOGIN_IP_SHIFT = 13,
    FIP_LOGIN_RSS_BIT_SHIFT = 12,
    FIP_LOGIN_SYNDROM_SHIFT = 24,
    FIP_LOGIN_QPN_MASK = 0xFFFFFF,

    FIP_LOGOUT_LENGTH = 13,
    FIP_LOGOUT_TYPE = 245,

    FIP_HOST_UPDATE_LENGTH = 13,
    FIP_HOST_UPDATE_TYPE = 245,
    FIP_HOST_VP_FLAG = 0x01,
    FIP_HOST_U_FLAG = 0x80,
    FIP_HOST_R_FLAG = 0x40,

    FIP_VHUB_ID_MASK = 0xFFFFFF,
    FIP_VHUB_V_FLAG = 0x80, // Valid Bit
    FIP_VHUB_RSS_FLAG = 0x40,
    FIP_VHUB_TYPE_MASK = 0x0F,
    FIP_VHUB_SL_MASK = 0x0F,

    FIP_VHUB_UP_LENGTH = 9,
    FIP_VHUB_UP_TYPE = 243,
    FIP_VHUB_UP_EPORT_MASK = 0x30,
    FIP_VHUB_UP_EPORT_SHIFT = 4,
    FIP_VHUB_UP_VP_FLAG = 0x01,

    FIP_VHUB_TBL_TYPE = 244,
    FIP_VHUB_TBL_HDR_SHIFT = 6,
    FIP_VHUB_TBL_SEQ_MASK = 0xC0,
    FIP_VHUB_TBL_SEQ_FIRST = 0x40,
    FIP_VHUB_TBL_SEQ_LAST = 0x80,

    FKA_ADV_PERIOD = 8000,  /* in mSecs */
    FKA_ADV_MISSES = 3
};

typedef enum LoginSyndrom_t {
    FIP_SYNDROM_SUCCESS = 0,
    FIP_SYNDROM_HADMIN_REJECT = 1,
    FIP_SYNDROM_GW_RESRC = 2,
    FIP_SYNDROM_NO_NADMIN = 3,
    FIP_SYNDROM_UNRECOGNISED = 4,
    FIP_SYNDROM_UNSUPPORTED = 5,
    FIP_SYNDROM_DUPLICATE_ADDR = 7
};


