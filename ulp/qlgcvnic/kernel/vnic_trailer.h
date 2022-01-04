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

#ifndef _VNIC_TRAILER_H_
#define _VNIC_TRAILER_H_

/* pktFlags values */
#define PF_CHASH_VALID			0x01
#define PF_IPSEC_VALID			0x02
#define PF_TCP_SEGMENT			0x04
#define PF_KICK					0x08
#define PF_VLAN_INSERT			0x10
#define PF_PVID_OVERRIDDEN		0x20
#define PF_FCS_INCLUDED			0x40
#define PF_FORCE_ROUTE			0x80

/* txChksumFlags values */
#define TX_CHKSUM_FLAGS_CHECKSUM_V4		0x01
#define TX_CHKSUM_FLAGS_CHECKSUM_V6		0x02
#define TX_CHKSUM_FLAGS_TCP_CHECKSUM	0x04
#define TX_CHKSUM_FLAGS_UDP_CHECKSUM	0x08
#define TX_CHKSUM_FLAGS_IP_CHECKSUM		0x10

/* rxChksumFlags values */
#define RX_CHKSUM_FLAGS_TCP_CHECKSUM_FAILED	0x01
#define RX_CHKSUM_FLAGS_UDP_CHECKSUM_FAILED	0x02
#define RX_CHKSUM_FLAGS_IP_CHECKSUM_FAILED	0x04
#define RX_CHKSUM_FLAGS_TCP_CHECKSUM_SUCCEEDED	0x08
#define RX_CHKSUM_FLAGS_UDP_CHECKSUM_SUCCEEDED	0x10
#define RX_CHKSUM_FLAGS_IP_CHECKSUM_SUCCEEDED	0x20
#define RX_CHKSUM_FLAGS_LOOPBACK		0x40
#define RX_CHKSUM_FLAGS_RESERVED		0x80

/* connectionHashAndValid values */
#define CHV_VALID		0x80
#define CHV_HASH_MASH	0x7f

/* round down value to align, align must be a power of 2 */
#ifndef ROUNDDOWNP2
#define ROUNDDOWNP2(val, align) \
        (((ULONG)(val)) & (~((ULONG)(align)-1)))
#endif

/* round up value to align, align must be a power of 2 */
#ifndef ROUNDUPP2
#define ROUNDUPP2(val, align)   \
        (((ULONG)(val) + (ULONG)(align) - 1) & (~((ULONG)(align)-1)))
#endif

#define VIPORT_TRAILER_ALIGNMENT	32
#define BUFFER_SIZE(len)			(sizeof(ViportTrailer_t) + ROUNDUPP2((len), VIPORT_TRAILER_ALIGNMENT))
#define MAX_PAYLOAD(len)			ROUNDDOWNP2((len) - sizeof(ViportTrailer_t), VIPORT_TRAILER_ALIGNMENT)
#define MIN_PACKET_LEN				64
#include <complib/cl_packon.h>

typedef struct ViportTrailer {
	int8_t dataAlignmentOffset;
	uint8_t rndisHeaderLength; /* reserved for use by Edp */
	uint16_t dataLength;
	uint8_t  pktFlags;

	uint8_t txChksumFlags;

	uint8_t rxChksumFlags;

	uint8_t ipSecFlags;
	uint32_t tcpSeqNo;
	uint32_t ipSecOffloadHandle;
	uint32_t ipSecNextOffloadHandle;
	uint8_t destMacAddr[6];
	uint16_t vLan;
	uint16_t timeStamp;
	uint8_t origin;
	uint8_t connectionHashAndValid;
} ViportTrailer_t;

#include <complib/cl_packoff.h>

#endif /* _VNIC_TRAILER_H_ */
