/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef _VNIC_CONTROLPKT_H_
#define _VNIC_CONTROLPKT_H_

#include <complib/cl_packon.h>
#define MAX_HOST_NAME_SIZE	64

typedef struct Inic_ConnectionData {
	uint64_t	pathId;
	uint8_t		inicInstance;
	uint8_t		pathNum;
	uint8_t		nodename[MAX_HOST_NAME_SIZE+1];
	uint8_t		reserved;
	uint32_t	featuresSupported;
} Inic_ConnectionData_t;
	
typedef struct Inic_ControlHeader {
	uint8_t		pktType;
	uint8_t		pktCmd;
	uint8_t		pktSeqNum;
	uint8_t		pktRetryCount;
	uint32_t	reserved;	/* for 64-bit alignmnet */
} Inic_ControlHeader_t;

/* ptkType values */
#define TYPE_INFO	0
#define TYPE_REQ	1
#define TYPE_RSP	2
#define TYPE_ERR	3

/* ptkCmd values */
#define CMD_INIT_INIC			1
#define CMD_CONFIG_DATA_PATH	2
#define CMD_EXCHANGE_POOLS		3
#define CMD_CONFIG_ADDRESSES	4
#define CMD_CONFIG_LINK			5
#define CMD_REPORT_STATISTICS	6
#define CMD_CLEAR_STATISTICS	7
#define CMD_REPORT_STATUS		8
#define CMD_RESET				9
#define CMD_HEARTBEAT			10

#define MAC_ADDR_LEN			HW_ADDR_LEN

/* pktCmd CMD_INIT_INIC, pktType TYPE_REQ data format */
typedef struct Inic_CmdInitInicReq {
	uint16_t	inicMajorVersion;
	uint16_t	inicMinorVersion;
	uint8_t		inicInstance;
	uint8_t		numDataPaths;
	uint16_t	numAddressEntries;
} Inic_CmdInitInicReq_t;

/* pktCmd CMD_INIT_INIC, pktType TYPE_RSP subdata format */
typedef struct Inic_LanSwitchAttributes {
	uint8_t		lanSwitchNum;
	uint8_t		numEnetPorts;
	uint16_t	defaultVlan;
	uint8_t		hwMacAddress[MAC_ADDR_LEN];
} Inic_LanSwitchAttributes_t;

/* pktCmd CMD_INIT_INIC, pktType TYPE_RSP data format */
typedef struct Inic_CmdInitInicRsp {
	uint16_t	inicMajorVersion;
	uint16_t	inicMinorVersion;
	uint8_t		numLanSwitches;
	uint8_t		numDataPaths;
	uint16_t	numAddressEntries;
	uint32_t	featuresSupported;
	Inic_LanSwitchAttributes_t lanSwitch[1];
} Inic_CmdInitInicRsp_t;

/* featuresSupported values */
#define INIC_FEAT_IPV4_HEADERS			0x00000001
#define INIC_FEAT_IPV6_HEADERS			0x00000002
#define INIC_FEAT_IPV4_CSUM_RX			0x00000004
#define INIC_FEAT_IPV4_CSUM_TX			0x00000008
#define INIC_FEAT_TCP_CSUM_RX			0x00000010
#define INIC_FEAT_TCP_CSUM_TX			0x00000020
#define INIC_FEAT_UDP_CSUM_RX			0x00000040
#define INIC_FEAT_UDP_CSUM_TX			0x00000080
#define INIC_FEAT_TCP_SEGMENT			0x00000100
#define INIC_FEAT_IPV4_IPSEC_OFFLOAD	0x00000200
#define INIC_FEAT_IPV6_IPSEC_OFFLOAD	0x00000400
#define INIC_FEAT_FCS_PROPAGATE			0x00000800
#define INIC_FEAT_PF_KICK				0x00001000
#define INIC_FEAT_PF_FORCE_ROUTE		0x00002000
#define INIC_FEAT_CHASH_OFFLOAD			0x00004000
#define INIC_FEAT_RDMA_IMMED			0x00008000 
#define INIC_FEAT_IGNORE_VLAN			0x00010000
#define INIC_FEAT_INBOUND_IB_MC			0x00200000

/* pktCmd CMD_CONFIG_DATA_PATH subdata format */
typedef struct  Inic_RecvPoolConfig {
	uint32_t	sizeRecvPoolEntry;
	uint32_t	numRecvPoolEntries;
	uint32_t	timeoutBeforeKick;
	uint32_t	numRecvPoolEntriesBeforeKick;
	uint32_t	numRecvPoolBytesBeforeKick;
	uint32_t	freeRecvPoolEntriesPerUpdate;
} Inic_RecvPoolConfig_t;

/* pktCmd CMD_CONFIG_DATA_PATH data format */
typedef struct Inic_CmdConfigDataPath {
	uint64_t	pathIdentifier;
	uint8_t		dataPath;
	uint8_t		reserved[3];
	Inic_RecvPoolConfig_t  hostRecvPoolConfig;
	Inic_RecvPoolConfig_t  eiocRecvPoolConfig;
} Inic_CmdConfigDataPath_t;

/* pktCmd CMD_EXCHANGE_POOLS data format */
typedef struct Inic_CmdExchangePools {
	uint8_t	dataPath;
	uint8_t	reserved[3];
	uint32_t	poolRKey;
	uint64_t	poolAddr;
} Inic_CmdExchangePools_t;

/* pktCmd CMD_CONFIG_ADDRESSES subdata format */
typedef struct Inic_AddressOp {
	uint16_t	index;
	uint8_t	operation;
	uint8_t	valid;
	uint8_t	address[6];
	uint16_t	vlan;
} Inic_AddressOp_t;

/* operation values */
#define INIC_OP_SET_ENTRY	0x01
#define INIC_OP_GET_ENTRY	0x02

/* pktCmd CMD_CONFIG_ADDRESSES data format */
typedef struct Inic_CmdConfigAddresses {
	uint8_t	numAddressOps;
	uint8_t	lanSwitchNum;
	Inic_AddressOp_t	listAddressOps[1];
} Inic_CmdConfigAddresses_t;

/* CMD_CONFIG_LINK data format */
typedef struct Inic_CmdConfigLink {
	uint8_t	cmdFlags;
	uint8_t	lanSwitchNum;
	uint16_t   mtuSize;
	uint16_t	defaultVlan;
	uint8_t	hwMacAddress[6];
} Inic_CmdConfigLink_t;

/* cmdFlags values */
#define INIC_FLAG_ENABLE_NIC		0x01
#define INIC_FLAG_DISABLE_NIC		0x02
#define INIC_FLAG_ENABLE_MCAST_ALL	0x04
#define INIC_FLAG_DISABLE_MCAST_ALL	0x08
#define INIC_FLAG_ENABLE_PROMISC	0x10
#define INIC_FLAG_DISABLE_PROMISC	0x20
#define INIC_FLAG_SET_MTU			0x40

/* pktCmd CMD_REPORT_STATISTICS, pktType TYPE_REQ data format */
typedef struct Inic_CmdReportStatisticsReq {
	uint8_t	lanSwitchNum;
} Inic_CmdReportStatisticsReq_t;

/* pktCmd CMD_REPORT_STATISTICS, pktType TYPE_RSP data format */
typedef struct Inic_CmdReportStatisticsRsp {
	uint8_t	lanSwitchNum;
	uint8_t	reserved[7]; /* for 64-bit alignment */
	uint64_t	ifInBroadcastPkts;
	uint64_t	ifInMulticastPkts;
	uint64_t	ifInOctets;
	uint64_t	ifInUcastPkts;
	uint64_t	ifInNUcastPkts; /* ifInBroadcastPkts + ifInMulticastPkts */
	uint64_t	ifInUnderrun; /* (OID_GEN_RCV_NO_BUFFER) */
	uint64_t	ifInErrors; /* (OID_GEN_RCV_ERROR) */
	uint64_t	ifOutErrors; /* (OID_GEN_XMIT_ERROR) */
	uint64_t	ifOutOctets;
	uint64_t	ifOutUcastPkts;
	uint64_t	ifOutMulticastPkts;
	uint64_t	ifOutBroadcastPkts;
	uint64_t	ifOutNUcastPkts; /* ifOutBroadcastPkts + ifOutMulticastPkts */
	uint64_t	ifOutOk; /* ifOutNUcastPkts + ifOutUcastPkts (OID_GEN_XMIT_OK)*/
	uint64_t	ifInOk; /* ifInNUcastPkts + ifInUcastPkts (OID_GEN_RCV_OK) */
	uint64_t	ifOutUcastBytes; /* (OID_GEN_DIRECTED_BYTES_XMT) */
	uint64_t	ifOutMulticastBytes; /* (OID_GEN_MULTICAST_BYTES_XMT) */
	uint64_t	ifOutBroadcastBytes; /* (OID_GEN_BROADCAST_BYTES_XMT) */
	uint64_t	ifInUcastBytes; /* (OID_GEN_DIRECTED_BYTES_RCV) */
	uint64_t	ifInMulticastBytes; /* (OID_GEN_MULTICAST_BYTES_RCV) */
	uint64_t	ifInBroadcastBytes; /* (OID_GEN_BROADCAST_BYTES_RCV) */
	uint64_t	ethernetStatus; /* OID_GEN_MEDIA_CONNECT_STATUS) */
} Inic_CmdReportStatisticsRsp_t;

/* pktCmd CMD_CLEAR_STATISTICS data format */
typedef struct Inic_CmdClearStatistics {
	uint8_t	lanSwitchNum;
} Inic_CmdClearStatistics_t;

/* pktCmd CMD_REPORT_STATUS data format */
typedef struct Inic_CmdReportStatus {
	uint8_t	lanSwitchNum;
	uint8_t	isFatal;
	uint8_t	reserved[2]; /* for 32-bit alignment */
	uint32_t	statusNumber;
	uint32_t	statusInfo;
	uint8_t 	fileName[32];
	uint8_t	routine[32];
	uint32_t	lineNum;
	uint32_t	errorParameter;
	uint8_t	descText[128];
} Inic_CmdReportStatus_t;

/* pktCmd CMD_HEARTBEAT data format */
typedef struct Inic_CmdHeartbeat {
	uint32_t	hbInterval;
} Inic_CmdHeartbeat_t;

#define INIC_STATUS_LINK_UP				1
#define INIC_STATUS_LINK_DOWN			2
#define INIC_STATUS_ENET_AGGREGATION_CHANGE	3
#define INIC_STATUS_EIOC_SHUTDOWN		4
#define INIC_STATUS_CONTROL_ERROR		5
#define INIC_STATUS_EIOC_ERROR			6

#define INIC_MAX_CONTROLPKTSZ	256
#define INIC_MAX_CONTROLDATASZ \
		(INIC_MAX_CONTROLPKTSZ - sizeof(Inic_ControlHeader_t))

typedef struct Inic_ControlPacket {
	Inic_ControlHeader_t hdr;
	union {
		Inic_CmdInitInicReq_t initInicReq;
		Inic_CmdInitInicRsp_t initInicRsp;
		Inic_CmdConfigDataPath_t configDataPathReq;
		Inic_CmdConfigDataPath_t configDataPathRsp;
		Inic_CmdExchangePools_t exchangePoolsReq;
		Inic_CmdExchangePools_t exchangePoolsRsp;
		Inic_CmdConfigAddresses_t configAddressesReq;
		Inic_CmdConfigAddresses_t configAddressesRsp;
		Inic_CmdConfigLink_t configLinkReq;
		Inic_CmdConfigLink_t configLinkRsp;
		Inic_CmdReportStatisticsReq_t reportStatisticsReq;
		Inic_CmdReportStatisticsRsp_t reportStatisticsRsp;
		Inic_CmdClearStatistics_t clearStatisticsReq;
		Inic_CmdClearStatistics_t clearStatisticsRsp;
		Inic_CmdReportStatus_t reportStatus;
		Inic_CmdHeartbeat_t heartbeatReq;
		Inic_CmdHeartbeat_t heartbeatRsp;
		char cmdData[INIC_MAX_CONTROLDATASZ];
	} cmd;
} Inic_ControlPacket_t;
/*
typedef struct _mac_addr
{
	uint8_t		addr[MAC_ADDR_LEN];
}	PACK_SUFFIX mac_addr_t;
*/
#include <complib/cl_packoff.h>

#endif /* _VNIC_CONTROLPKT_H_ */
