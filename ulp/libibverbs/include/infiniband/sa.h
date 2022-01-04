/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2010 Intel.  All rights reserved.
 *
 * This software is available to you under the OpenFabrics.org BSD license
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef INFINIBAND_SA_H
#define INFINIBAND_SA_H

#include <infiniband/verbs.h>

struct ibv_sa_path_rec {
	/* reserved */
	/* reserved */
	union ibv_gid dgid;
	union ibv_gid sgid;
	uint16_t      dlid;
	uint16_t      slid;
	int           raw_traffic;
	/* reserved */
	uint32_t      flow_label;
	uint8_t       hop_limit;
	uint8_t       traffic_class;
	int           reversible;
	uint8_t       numb_path;
	uint16_t      pkey;
	/* reserved */
	uint8_t       sl;
	uint8_t       mtu_selector;
	uint8_t	      mtu;
	uint8_t       rate_selector;
	uint8_t       rate;
	uint8_t       packet_life_time_selector;
	uint8_t       packet_life_time;
	uint8_t       preference;
};

#define IBV_PATH_RECORD_REVERSIBLE 0x80

struct ibv_path_record {
	uint64_t		service_id;
	union ibv_gid	dgid;
	union ibv_gid	sgid;
	uint16_t		dlid;
	uint16_t		slid;
	uint32_t		flowlabel_hoplimit; /* resv-31:28 flow label-27:8 hop limit-7:0*/
	uint8_t			tclass;
	uint8_t			reversible_numpath; /* reversible-7:7 num path-6:0 */
	uint16_t		pkey;
	uint16_t		qosclass_sl;	    /* qos class-15:4 sl-3:0 */
	uint8_t			mtu;		    /* mtu selector-7:6 mtu-5:0 */
	uint8_t			rate;		    /* rate selector-7:6 rate-5:0 */
	uint8_t			packetlifetime;	    /* lifetime selector-7:6 lifetime-5:0 */
	uint8_t			preference;
	uint8_t			reserved[6];
};

#define IBV_PATH_FLAG_GMP	          (1<<0)
#define IBV_PATH_FLAG_PRIMARY	      (1<<1)
#define IBV_PATH_FLAG_ALTERNATE       (1<<2)
#define IBV_PATH_FLAG_OUTBOUND	      (1<<3)
#define IBV_PATH_FLAG_INBOUND	      (1<<4)
#define IBV_PATH_FLAG_INBOUND_REVERSE (1<<5)
#define IBV_PATH_FLAG_BIDIRECTIONAL   (IBV_PATH_FLAG_OUTBOUND |     \
									   IBV_PATH_FLAG_INBOUND_REVERSE)

struct ibv_path_data {
	uint32_t				flags;
	uint32_t				reserved;
	struct ibv_path_record	path;
};

#endif /* INFINIBAND_SA_H */
