/*
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include "l2w.h"
#include "ib_pack.h"

#define STRUCT_FIELD(header, field) \
	.struct_offset_bytes = offsetof(struct ib_unpacked_ ## header, field),      \
	.struct_size_bytes   = sizeof ((struct ib_unpacked_ ## header *) 0)->field, \
	.field_name          = #header ":" #field

#define STRUCT_FIELD_INIT(header, field,ow,ob,sb) \
	offsetof(struct ib_unpacked_ ## header, field),      \
	sizeof ((struct ib_unpacked_ ## header *) 0)->field, \
	ow,ob,sb, \
	#header ":" #field

#define STRUCT_FIELD_INITR(ow,ob,sb) \
		0, 0, ow, ob, sb, "reserved"

static const struct ib_field lrh_table[]  = {
	{ STRUCT_FIELD_INIT(lrh, virtual_lane, 0, 0, 4) },
	{ STRUCT_FIELD_INIT(lrh, link_version, 0, 4, 4) },
	{ STRUCT_FIELD_INIT(lrh, service_level, 0, 8, 4) },
	{ STRUCT_FIELD_INITR(0,12,2) },
	{ STRUCT_FIELD_INIT(lrh, link_next_header, 0, 14, 2) },
	{ STRUCT_FIELD_INIT(lrh, destination_lid, 0, 16, 16) },
	{ STRUCT_FIELD_INITR(1,0,5) },
	{ STRUCT_FIELD_INIT(lrh, packet_length, 1, 5, 11) },
	{ STRUCT_FIELD_INIT(lrh, source_lid, 1, 16, 16) }
};

static const struct ib_field eth_table[]  = {
	{ STRUCT_FIELD_INIT(eth, dmac_h, 0, 0, 32) },
	{ STRUCT_FIELD_INIT(eth, dmac_l, 1, 0, 16) },
	{ STRUCT_FIELD_INIT(eth, smac_h, 1, 16,16) },
	{ STRUCT_FIELD_INIT(eth, smac_l, 2, 0 ,32) },
	{ STRUCT_FIELD_INIT(eth, type, 3, 0, 16)}
};


static const struct ib_field grh_table[]  = {
	{ STRUCT_FIELD_INIT(grh, ip_version, 0, 0, 4) },
	{ STRUCT_FIELD_INIT(grh, traffic_class, 0, 4, 8) },
	{ STRUCT_FIELD_INIT(grh, flow_label, 0, 12, 20) },
	{ STRUCT_FIELD_INIT(grh, payload_length, 1, 0, 16) },
	{ STRUCT_FIELD_INIT(grh, next_header, 1, 16, 8) },
	{ STRUCT_FIELD_INIT(grh, hop_limit, 1, 24, 8) },
	{ STRUCT_FIELD_INIT(grh, source_gid, 2, 0, 128) },
	{ STRUCT_FIELD_INIT(grh, destination_gid, 6, 0, 128) }
};

static const struct ib_field bth_table[]  = {
	{ STRUCT_FIELD_INIT(bth, opcode, 0, 0, 8) },
	{ STRUCT_FIELD_INIT(bth, solicited_event, 0, 8, 1) },
	{ STRUCT_FIELD_INIT(bth, mig_req, 0, 9, 1) },
	{ STRUCT_FIELD_INIT(bth, pad_count, 0, 10, 2) },
	{ STRUCT_FIELD_INIT(bth, transport_header_version, 0, 12, 4) },
	{ STRUCT_FIELD_INIT(bth, pkey, 0, 16, 16) },
	{ STRUCT_FIELD_INITR(1,0,8) },
	{ STRUCT_FIELD_INIT(bth, destination_qpn, 1, 8, 24) },
	{ STRUCT_FIELD_INIT(bth, ack_req, 2, 0, 1) },
	{ STRUCT_FIELD_INITR(2,1,7) },
	{ STRUCT_FIELD_INIT(bth, psn, 2, 8, 24) }
};

static const struct ib_field deth_table[] = {
	{ STRUCT_FIELD_INIT(deth, qkey, 0, 0, 32) },
	{ STRUCT_FIELD_INITR(1,0,8) },
	{ STRUCT_FIELD_INIT(deth, source_qpn, 1, 8, 24) }
};

/**
 * ib_ud_header_init - Initialize UD header structure
 * @payload_bytes:Length of packet payload
 * @grh_present:GRH flag (if non-zero, GRH will be included)
 * @header:Structure to initialize
 *
 * ib_ud_header_init() initializes the lrh.link_version, lrh.link_next_header,
 * lrh.packet_length, grh.ip_version, grh.payload_length,
 * grh.next_header, bth.opcode, bth.pad_count and
 * bth.transport_header_version fields of a &struct ib_ud_header given
 * the payload length and whether a GRH will be included.
 */
void ib_ud_header_init(int     		    payload_bytes,
		       int    		    grh_present,
		       struct ib_ud_header *header)
{
	int header_len;
	u16 packet_length;

	memset(header, 0, sizeof *header);

	header_len =
		IB_LRH_BYTES  +
		IB_BTH_BYTES  +
		IB_DETH_BYTES;
	if (grh_present) {
		header_len += IB_GRH_BYTES;
	}

	header->lrh.link_version     = 0;
	header->lrh.link_next_header =
		grh_present ? IB_LNH_IBA_GLOBAL : IB_LNH_IBA_LOCAL;
	packet_length		     = (u16)((IB_LRH_BYTES     +
					IB_BTH_BYTES     +
					IB_DETH_BYTES    +
					payload_bytes    +
					4                + /* ICRC     */
					3) / 4);            /* round up */

	header->grh_present          = grh_present;
	if (grh_present) {
		packet_length		   += IB_GRH_BYTES / 4;
		header->grh.ip_version      = 6;
		header->grh.payload_length  =
			cpu_to_be16((IB_BTH_BYTES     +
				     IB_DETH_BYTES    +
				     payload_bytes    +
				     4                + /* ICRC     */
				     3) & ~3);          /* round up */
		header->grh.next_header     = 0x1b;
	}

	header->lrh.packet_length = cpu_to_be16(packet_length);

	if (header->immediate_present)
		header->bth.opcode           = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
	else
		header->bth.opcode           = IB_OPCODE_UD_SEND_ONLY;
	header->bth.pad_count                = (u8)((4 - payload_bytes) & 3);
	header->bth.transport_header_version = 0;
}
EXPORT_SYMBOL(ib_ud_header_init);

/**
 * ib_ud_header_pack - Pack UD header struct into wire format
 * @header:UD header struct
 * @buf:Buffer to pack into
 *
 * ib_ud_header_pack() packs the UD header structure @header into wire
 * format in the buffer @buf.
 */
int ib_ud_header_pack(struct ib_ud_header *header,
		      u8                *buf)
{
	int len = 0;

	ib_pack(lrh_table, ARRAY_SIZE(lrh_table),
		&header->lrh, buf);
	len += IB_LRH_BYTES;

	if (header->grh_present) {
		ib_pack(grh_table, ARRAY_SIZE(grh_table),
			&header->grh, buf + len);
		len += IB_GRH_BYTES;
	}

	ib_pack(bth_table, ARRAY_SIZE(bth_table),
		&header->bth, buf + len);
	len += IB_BTH_BYTES;

	ib_pack(deth_table, ARRAY_SIZE(deth_table),
		&header->deth, buf + len);
	len += IB_DETH_BYTES;

	if (header->immediate_present) {
		memcpy(buf + len, &header->immediate_data, sizeof header->immediate_data);
		len += sizeof header->immediate_data;
	}

	return len;
}
EXPORT_SYMBOL(ib_ud_header_pack);

/**
 * ib_ud_header_unpack - Unpack UD header struct from wire format
 * @header:UD header struct
 * @buf:Buffer to pack into
 *
 * ib_ud_header_pack() unpacks the UD header structure @header from wire
 * format in the buffer @buf.
 */
int ib_ud_header_unpack(u8                *buf,
			struct ib_ud_header *header)
{
	ib_unpack(lrh_table, ARRAY_SIZE(lrh_table),
		  buf, &header->lrh);
	buf += IB_LRH_BYTES;

	if (header->lrh.link_version != 0) {
		printk(KERN_WARNING "Invalid LRH.link_version %d\n",
		       header->lrh.link_version);
		return -EINVAL;
	}

	switch (header->lrh.link_next_header) {
	case IB_LNH_IBA_LOCAL:
		header->grh_present = 0;
		break;

	case IB_LNH_IBA_GLOBAL:
		header->grh_present = 1;
		ib_unpack(grh_table, ARRAY_SIZE(grh_table),
			  buf, &header->grh);
		buf += IB_GRH_BYTES;

		if (header->grh.ip_version != 6) {
			printk(KERN_WARNING "Invalid GRH.ip_version %d\n",
			       header->grh.ip_version);
			return -EINVAL;
		}
		if (header->grh.next_header != 0x1b) {
			printk(KERN_WARNING "Invalid GRH.next_header 0x%02x\n",
			       header->grh.next_header);
			return -EINVAL;
		}
		break;

	default:
		printk(KERN_WARNING "Invalid LRH.link_next_header %d\n",
		       header->lrh.link_next_header);
		return -EINVAL;
	}

	ib_unpack(bth_table, ARRAY_SIZE(bth_table),
		  buf, &header->bth);
	buf += IB_BTH_BYTES;

	switch (header->bth.opcode) {
	case IB_OPCODE_UD_SEND_ONLY:
		header->immediate_present = 0;
		break;
	case IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE:
		header->immediate_present = 1;
		break;
	default:
		printk(KERN_WARNING "Invalid BTH.opcode 0x%02x\n",
		       header->bth.opcode);
		return -EINVAL;
	}

	if (header->bth.transport_header_version != 0) {
		printk(KERN_WARNING "Invalid BTH.transport_header_version %d\n",
		       header->bth.transport_header_version);
		return -EINVAL;
	}

	ib_unpack(deth_table, ARRAY_SIZE(deth_table),
		  buf, &header->deth);
	buf += IB_DETH_BYTES;

	if (header->immediate_present)
		memcpy(&header->immediate_data, buf, sizeof header->immediate_data);

	return 0;
}
EXPORT_SYMBOL(ib_ud_header_unpack);

/**
 * ib_rdmaoe_ud_header_init - Initialize UD header structure
 * @payload_bytes:Length of packet payload
 * @grh_present:GRH flag (if non-zero, GRH will be included)
 * @header:Structure to initialize
 *
 * ib_rdmaoe_ud_header_init() initializes the grh.ip_version, grh.payload_length,
 * grh.next_header, bth.opcode, bth.pad_count and
 * bth.transport_header_version fields of a &struct eth_ud_header given
 * the payload length and whether a GRH will be included.
 */
void ib_rdmaoe_ud_header_init(int     		    payload_bytes,
			   int    		    grh_present,
			   struct eth_ud_header    *header)
{
	int header_len;

	memset(header, 0, sizeof *header);

	header_len =
		sizeof header->eth  +
		IB_BTH_BYTES  +
		IB_DETH_BYTES;
	if (grh_present)
		header_len += IB_GRH_BYTES;

	header->grh_present          = grh_present;
	if (grh_present) {
		header->grh.ip_version      = 6;
		header->grh.payload_length  =
			cpu_to_be16((IB_BTH_BYTES     +
				     IB_DETH_BYTES    +
				     payload_bytes    +
				     4                + /* ICRC     */
				     3) & ~3);          /* round up */
		header->grh.next_header     = 0x1b;
	}

	if (header->immediate_present)
		header->bth.opcode           = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
	else
		header->bth.opcode           = IB_OPCODE_UD_SEND_ONLY;
	header->bth.pad_count                =(u8) ((4 - payload_bytes) & 3);
	header->bth.transport_header_version = 0;
}



/**
 * rdmaoe_ud_header_pack - Pack UD header struct into eth wire format
 * @header:UD header struct
 * @buf:Buffer to pack into
 *
 * ib_ud_header_pack() packs the UD header structure @header into wire
 * format in the buffer @buf.
 */
int rdmaoe_ud_header_pack(struct eth_ud_header *header,
		       void                 *buf)
{
	int len = 0;

	ib_pack(eth_table, ARRAY_SIZE(eth_table),
		&header->eth, buf);
	len += IB_ETH_BYTES;

	if (header->grh_present) {
		ib_pack(grh_table, ARRAY_SIZE(grh_table),
			&header->grh, (u8*)buf + len);
		len += IB_GRH_BYTES;
	}

	ib_pack(bth_table, ARRAY_SIZE(bth_table),
		&header->bth, (u8*)buf + len);
	len += IB_BTH_BYTES;

	ib_pack(deth_table, ARRAY_SIZE(deth_table),
		&header->deth, (u8*)buf + len);
	len += IB_DETH_BYTES;

	if (header->immediate_present) {
		memcpy((u8*)buf + len, &header->immediate_data,
		       sizeof header->immediate_data);
		len += sizeof header->immediate_data;
	}

	return len;
}


