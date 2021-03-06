/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2010,2011 Mellanox Technologies LTD.  All rights reserved.
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
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include "ibdiag_common.h"

enum port_ops {
	QUERY,
	ENABLE,
	RESET,
	DISABLE,
	SPEED,
	ESPEED,
	FDR10SPEED,
	WIDTH,
	DOWN,
	ARM,
	ACTIVE,
	VLS,
	MTU,
	LID,
	SMLID,
	LMC,
};

struct ibmad_port *srcport;
int speed = 0; /* no state change */
int espeed = 0; /* no state change */
int fdr10 = 0; /* no state change */
int width = 0; /* no state change */
int lid;
int smlid;
int lmc;
int mtu;
int vls = 0; /* no state change */

struct {
	const char *name;
	int *val;
	int set;
} port_args[] = {
	{"query", NULL, 0},	/* QUERY */
	{"enable", NULL, 0},	/* ENABLE */
	{"reset", NULL, 0},	/* RESET */
	{"disable", NULL, 0},	/* DISABLE */
	{"speed", &speed, 0},	/* SPEED */
	{"espeed", &espeed, 0},	/* EXTENDED SPEED */
	{"fdr10", &fdr10, 0},	/* FDR10 SPEED */
	{"width", &width, 0},	/* WIDTH */
	{"down", NULL, 0},	/* DOWN */
	{"arm", NULL, 0},	/* ARM */
	{"active", NULL, 0},	/* ACTIVE */
	{"vls", &vls, 0},	/* VLS */
	{"mtu", &mtu, 0},	/* MTU */
	{"lid", &lid, 0},	/* LID */
	{"smlid", &smlid, 0},	/* SMLID */
	{"lmc", &lmc, 0},	/* LMC */
};

#define NPORT_ARGS (sizeof(port_args) / sizeof(port_args[0]))

/*******************************************/

/*
 * Return 1 if node is a switch, else zero.
 */
static int get_node_info(ib_portid_t * dest, uint8_t * data)
{
	int node_type;

	if (!smp_query_via(data, dest, IB_ATTR_NODE_INFO, 0, 0, srcport))
		IBERROR("smp query nodeinfo failed");

	node_type = mad_get_field(data, 0, IB_NODE_TYPE_F);
	if (node_type == IB_NODE_SWITCH)	/* Switch NodeType ? */
		return 1;
	else
		return 0;
}

static int is_mlnx_ext_port_info_supported(uint32_t devid)
{
	if (devid == 0xc738)
		return 1;
	if (devid >= 0x1003 && devid <= 0x1010)
		return 1;
	return 0;
}

static int get_port_info(ib_portid_t * dest, uint8_t * data, int portnum,
			 int is_switch)
{
	uint8_t smp[IB_SMP_DATA_SIZE];
	uint8_t *info;
	int cap_mask;

	if (is_switch) {
		if (!smp_query_via(smp, dest, IB_ATTR_PORT_INFO, 0, 0, srcport))
			IBERROR("smp query port 0 portinfo failed");
		info = smp;
	} else
		info = data;

	if (!smp_query_via(data, dest, IB_ATTR_PORT_INFO, portnum, 0, srcport))
		IBERROR("smp query portinfo failed");
	cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);
	return (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS));
}

static void show_port_info(ib_portid_t * dest, uint8_t * data, int portnum,
			   int espeed_cap)
{
	char buf[2300];
	char val[64];

	mad_dump_portstates(buf, sizeof buf, data, sizeof *data);
	mad_decode_field(data, IB_PORT_LID_F, val);
	mad_dump_field(IB_PORT_LID_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	mad_decode_field(data, IB_PORT_SMLID_F, val);
	mad_dump_field(IB_PORT_SMLID_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	mad_decode_field(data, IB_PORT_LMC_F, val);
	mad_dump_field(IB_PORT_LMC_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	mad_decode_field(data, IB_PORT_LINK_WIDTH_SUPPORTED_F, val);
	mad_dump_field(IB_PORT_LINK_WIDTH_SUPPORTED_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	mad_decode_field(data, IB_PORT_LINK_WIDTH_ENABLED_F, val);
	mad_dump_field(IB_PORT_LINK_WIDTH_ENABLED_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	mad_decode_field(data, IB_PORT_LINK_WIDTH_ACTIVE_F, val);
	mad_dump_field(IB_PORT_LINK_WIDTH_ACTIVE_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	mad_decode_field(data, IB_PORT_LINK_SPEED_SUPPORTED_F, val);
	mad_dump_field(IB_PORT_LINK_SPEED_SUPPORTED_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	mad_decode_field(data, IB_PORT_LINK_SPEED_ENABLED_F, val);
	mad_dump_field(IB_PORT_LINK_SPEED_ENABLED_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	mad_decode_field(data, IB_PORT_LINK_SPEED_ACTIVE_F, val);
	mad_dump_field(IB_PORT_LINK_SPEED_ACTIVE_F, buf + strlen(buf),
		       sizeof buf - strlen(buf), val);
	sprintf(buf + strlen(buf), "%s", "\n");
	if (espeed_cap) {
		mad_decode_field(data, IB_PORT_LINK_SPEED_EXT_SUPPORTED_F, val);
		mad_dump_field(IB_PORT_LINK_SPEED_EXT_SUPPORTED_F,
			       buf + strlen(buf), sizeof buf - strlen(buf),
			       val);
		sprintf(buf + strlen(buf), "%s", "\n");
		mad_decode_field(data, IB_PORT_LINK_SPEED_EXT_ENABLED_F, val);
		mad_dump_field(IB_PORT_LINK_SPEED_EXT_ENABLED_F,
			       buf + strlen(buf), sizeof buf - strlen(buf),
			       val);
		sprintf(buf + strlen(buf), "%s", "\n");
		mad_decode_field(data, IB_PORT_LINK_SPEED_EXT_ACTIVE_F, val);
		mad_dump_field(IB_PORT_LINK_SPEED_EXT_ACTIVE_F,
			       buf + strlen(buf), sizeof buf - strlen(buf),
			       val);
		sprintf(buf + strlen(buf), "%s", "\n");
	}

	printf("# Port info: %s port %d\n%s", portid2str(dest), portnum, buf);
}

static void set_port_info(ib_portid_t * dest, uint8_t * data, int portnum,
			  int espeed_cap)
{
	if (!smp_set_via(data, dest, IB_ATTR_PORT_INFO, portnum, 0, srcport))
		IBERROR("smp set portinfo failed");

	printf("\nAfter PortInfo set:\n");
	show_port_info(dest, data, portnum, espeed_cap);
}

static void get_ext_port_info(ib_portid_t * dest, uint8_t * data, int portnum)
{
	if (!smp_query_via(data, dest, IB_ATTR_MLNX_EXT_PORT_INFO,
			   portnum, 0, srcport))
		IBERROR("smp query ext portinfo failed");
}

static void show_ext_port_info(ib_portid_t * dest, uint8_t * data, int portnum)
{
	char buf[256];

	mad_dump_mlnx_ext_port_info(buf, sizeof buf, data, IB_SMP_DATA_SIZE);

	printf("# Extended Port info: %s port %d\n%s", portid2str(dest),
	       portnum, buf);
}

static void set_ext_port_info(ib_portid_t * dest, uint8_t * data, int portnum)
{
	if (!smp_set_via(data, dest, IB_ATTR_MLNX_EXT_PORT_INFO,
			 portnum, 0, srcport))
		IBERROR("smp set ext portinfo failed");

	printf("\nAfter ExtendedPortInfo set:\n");
	show_ext_port_info(dest, data, portnum);
}

static int get_link_width(int lwe, int lws)
{
	if (lwe == 255)
		return lws;
	else
		return lwe;
}

static int get_link_speed(int lse, int lss)
{
	if (lse == 15)
		return lss;
	else
		return lse;
}

static int get_link_speed_ext(int lsee, int lses)
{
	if (lsee == 31)
		return lses;
	else
		return lsee;
}

static void validate_width(int width, int peerwidth, int lwa)
{
	if ((width & peerwidth & 0x8)) {
		if (lwa != 8)
			IBWARN
			    ("Peer ports operating at active width %d rather than 8 (12x)",
			     lwa);
	} else if ((width & peerwidth & 0x4)) {
		if (lwa != 4)
			IBWARN
			    ("Peer ports operating at active width %d rather than 4 (8x)",
			     lwa);
	} else if ((width & peerwidth & 0x2)) {
		if (lwa != 2)
			IBWARN
			    ("Peer ports operating at active width %d rather than 2 (4x)",
			     lwa);
	} else if ((width & peerwidth & 0x1)) {
		if (lwa != 1)
			IBWARN
			    ("Peer ports operating at active width %d rather than 1 (1x)",
			     lwa);
	}
}

static void validate_speed(int speed, int peerspeed, int lsa)
{
	if ((speed & peerspeed & 0x4)) {
		if (lsa != 4)
			IBWARN
			    ("Peer ports operating at active speed %d rather than 4 (10.0 Gbps)",
			     lsa);
	} else if ((speed & peerspeed & 0x2)) {
		if (lsa != 2)
			IBWARN
			    ("Peer ports operating at active speed %d rather than 2 (5.0 Gbps)",
			     lsa);
	} else if ((speed & peerspeed & 0x1)) {
		if (lsa != 1)
			IBWARN
			    ("Peer ports operating at active speed %d rather than 1 (2.5 Gbps)",
			     lsa);
	}
}

static void validate_extended_speed(int espeed, int peerespeed, int lsea)
{
	if ((espeed & peerespeed & 0x2)) {
		if (lsea != 2)
			IBWARN
			    ("Peer ports operating at active extended speed %d rather than 2 (25.78125 Gbps)",
			     lsea);
	} else if ((espeed & peerespeed & 0x1)) {
		if (lsea != 1)
			IBWARN
			    ("Peer ports operating at active extended speed %d rather than 1 (14.0625 Gbps)",
			     lsea);
	}
}

int main(int argc, char **argv)
{
	int mgmt_classes[3] =
	    { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS };
	ib_portid_t portid = { 0 };
	int port_op = -1;
	int is_switch, is_peer_switch, espeed_cap, peer_espeed_cap;
	int state, physstate, lwe, lws, lwa, lse, lss, lsa, lsee, lses, lsea,
	    fdr10s, fdr10e, fdr10a;
	int peerlocalportnum, peerlwe, peerlws, peerlwa, peerlse, peerlss,
	    peerlsa, peerlsee, peerlses, peerlsea, peerfdr10s, peerfdr10e,
	    peerfdr10a;
	int peerwidth, peerspeed, peerespeed;
	uint8_t data[IB_SMP_DATA_SIZE] = { 0 };
	uint8_t data2[IB_SMP_DATA_SIZE] = { 0 };
	ib_portid_t peerportid = { 0 };
	int portnum = 0;
	ib_portid_t selfportid = { 0 };
	int selfport = 0;
	int changed = 0;
	int i;
	uint16_t devid, rem_devid;
	long val;
	char usage_args[] = "<dest dr_path|lid|guid> <portnum> [<op>]\n"
	    "\nSupported ops: enable, disable, reset, speed, width, query,\n"
	    "\tdown, arm, active, vls, mtu, lid, smlid, lmc\n";
	const char *usage_examples[] = {
		"3 1 disable\t\t\t# by lid",
		"-G 0x2C9000100D051 1 enable\t# by guid",
		"-D 0 1\t\t\t# (query) by direct route",
		"3 1 reset\t\t\t# by lid",
		"3 1 speed 1\t\t\t# by lid",
		"3 1 width 1\t\t\t# by lid",
		"-D 0 1 lid 0x1234 arm\t\t# by direct route",
		NULL
	};

	ibdiag_process_opts(argc, argv, NULL, NULL, NULL, NULL,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (argc < 2)
		ibdiag_show_usage();

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!srcport)
		IBERROR("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	if (ib_resolve_portid_str_via(&portid, argv[0], ibd_dest_type,
				      ibd_sm_id, srcport) < 0)
		IBERROR("can't resolve destination port %s", argv[0]);

	if (argc > 1)
		portnum = strtol(argv[1], 0, 0);

	for (i = 2; i < argc; i++) {
		int j;

		for (j = 0; j < NPORT_ARGS; j++) {
			if (strcmp(argv[i], port_args[j].name))
				continue;
			port_args[j].set = 1;
			if (!port_args[j].val) {
				if (port_op >= 0)
					IBERROR("%s only one of: ",
						"query, enable, disable, "
						"reset, down, arm, active, "
						"can be specified",
						port_args[j].name);
				port_op = j;
				break;
			}
			if (++i >= argc)
				IBERROR("%s requires an additional parameter",
					port_args[j].name);
			val = strtol(argv[i], 0, 0);
			switch (j) {
			case SPEED:
				if (val < 0 || val > 15)
					IBERROR("invalid speed value %ld", val);
				break;
			case ESPEED:
				if (val < 0 || val > 31)
					IBERROR("invalid extended speed value %ld", val);
				break;
			case FDR10SPEED:
				if (val < 0 || val > 1)
					IBERROR("invalid fdr10 speed value %ld", val);
				break;
			case WIDTH:
				if (val < 0 || (val > 15 && val != 255))
					IBERROR("invalid width value %ld", val);
				break;
			case VLS:
				if (val <= 0 || val > 5)
					IBERROR("invalid vls value %ld", val);
				break;
			case MTU:
				if (val <= 0 || val > 5)
					IBERROR("invalid mtu value %ld", val);
				break;
			case LID:
				if (val <= 0 || val >= 0xC000)
					IBERROR("invalid lid value 0x%lx", val);
				break;
			case SMLID:
				if (val <= 0 || val >= 0xC000)
					IBERROR("invalid smlid value 0x%lx",
						val);
				break;
			case LMC:
				if (val < 0 || val > 7)
					IBERROR("invalid lmc value %ld", val);
			}
			*port_args[j].val = (int)val;
			changed = 1;
			break;
		}
		if (j == NPORT_ARGS)
			IBERROR("invalid operation: %s", argv[i]);
	}
	if (port_op < 0)
		port_op = QUERY;

	is_switch = get_node_info(&portid, data);
	devid = (uint16_t) mad_get_field(data, 0, IB_NODE_DEVID_F);

	if (port_op != QUERY || changed)
		printf("Initial %s PortInfo:\n", is_switch ? "Switch" : "CA");
	else
		printf("%s PortInfo:\n", is_switch ? "Switch" : "CA");
	espeed_cap = get_port_info(&portid, data, portnum, is_switch);
	show_port_info(&portid, data, portnum, espeed_cap);
	if (is_mlnx_ext_port_info_supported(devid)) {
		get_ext_port_info(&portid, data2, portnum);
		show_ext_port_info(&portid, data2, portnum);
	}

	if (port_op != QUERY || changed) {
		/*
		 * If we aren't setting the LID and the LID is the default,
		 * the SMA command will fail due to an invalid LID.
		 * Set it to something unlikely but valid.
		 */
		val = mad_get_field(data, 0, IB_PORT_LID_F);
		if (!port_args[LID].set && (!val || val == 0xFFFF))
			mad_set_field(data, 0, IB_PORT_LID_F, 0x1234);
		val = mad_get_field(data, 0, IB_PORT_SMLID_F);
		if (!port_args[SMLID].set && (!val || val == 0xFFFF))
			mad_set_field(data, 0, IB_PORT_SMLID_F, 0x1234);
		mad_set_field(data, 0, IB_PORT_STATE_F, 0);	/* NOP */
		mad_set_field(data, 0, IB_PORT_PHYS_STATE_F, 0);	/* NOP */

		switch (port_op) {
		case ENABLE:
		case RESET:
			/* Polling */
			mad_set_field(data, 0, IB_PORT_PHYS_STATE_F, 2);
			break;
		case DISABLE:
			printf("Disable may be irreversible\n");
			mad_set_field(data, 0, IB_PORT_PHYS_STATE_F, 3);
			break;
		case DOWN:
			mad_set_field(data, 0, IB_PORT_STATE_F, 1);
			break;
		case ARM:
			mad_set_field(data, 0, IB_PORT_STATE_F, 3);
			break;
		case ACTIVE:
			mad_set_field(data, 0, IB_PORT_STATE_F, 4);
			break;
		}

		/* always set enabled speeds/width - defaults to NOP */
		mad_set_field(data, 0, IB_PORT_LINK_SPEED_ENABLED_F, speed);
		mad_set_field(data, 0, IB_PORT_LINK_SPEED_EXT_ENABLED_F, espeed);
		mad_set_field(data, 0, IB_PORT_LINK_WIDTH_ENABLED_F, width);

		if (port_args[VLS].set)
			mad_set_field(data, 0, IB_PORT_OPER_VLS_F, vls);
		if (port_args[MTU].set)
			mad_set_field(data, 0, IB_PORT_NEIGHBOR_MTU_F, mtu);
		if (port_args[LID].set)
			mad_set_field(data, 0, IB_PORT_LID_F, lid);
		if (port_args[SMLID].set)
			mad_set_field(data, 0, IB_PORT_SMLID_F, smlid);
		if (port_args[LMC].set)
			mad_set_field(data, 0, IB_PORT_LMC_F, lmc);

		if (port_args[FDR10SPEED].set) {
			mad_set_field(data2, 0,
				      IB_MLNX_EXT_PORT_STATE_CHG_ENABLE_F,
				      FDR10);
			mad_set_field(data2, 0,
				      IB_MLNX_EXT_PORT_LINK_SPEED_ENABLED_F,
				      fdr10);
			set_ext_port_info(&portid, data2, portnum);
		}
		set_port_info(&portid, data, portnum, is_switch);

	} else if (is_switch && portnum) {
		/* Now, make sure PortState is Active */
		/* Or is PortPhysicalState LinkUp sufficient ? */
		mad_decode_field(data, IB_PORT_STATE_F, &state);
		mad_decode_field(data, IB_PORT_PHYS_STATE_F, &physstate);
		if (state == 4) {	/* Active */
			mad_decode_field(data, IB_PORT_LINK_WIDTH_ENABLED_F,
					 &lwe);
			mad_decode_field(data, IB_PORT_LINK_WIDTH_SUPPORTED_F,
					 &lws);
			mad_decode_field(data, IB_PORT_LINK_WIDTH_ACTIVE_F,
					 &lwa);
			mad_decode_field(data, IB_PORT_LINK_SPEED_SUPPORTED_F,
					 &lss);
			mad_decode_field(data, IB_PORT_LINK_SPEED_ACTIVE_F,
					 &lsa);
			mad_decode_field(data, IB_PORT_LINK_SPEED_ENABLED_F,
					 &lse);
			mad_decode_field(data2,
					 IB_MLNX_EXT_PORT_LINK_SPEED_SUPPORTED_F,
					 &fdr10s);
			mad_decode_field(data2,
					 IB_MLNX_EXT_PORT_LINK_SPEED_ENABLED_F,
					 &fdr10e);
			mad_decode_field(data2,
					 IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F,
					 &fdr10a);
			if (espeed_cap) {
				mad_decode_field(data,
						 IB_PORT_LINK_SPEED_EXT_SUPPORTED_F,
						 &lses);
				mad_decode_field(data,
						 IB_PORT_LINK_SPEED_EXT_ACTIVE_F,
						 &lsea);
				mad_decode_field(data,
						 IB_PORT_LINK_SPEED_EXT_ENABLED_F,
						 &lsee);
			}

			/* Setup portid for peer port */
			memcpy(&peerportid, &portid, sizeof(peerportid));
			peerportid.drpath.cnt = 1;
			peerportid.drpath.p[1] = (uint8_t) portnum;

			/* Set DrSLID to local lid */
			if (ib_resolve_self_via(&selfportid,
						&selfport, 0, srcport) < 0)
				IBERROR("could not resolve self");
			peerportid.drpath.drslid = (uint16_t) selfportid.lid;
			peerportid.drpath.drdlid = 0xffff;

			/* Get peer port NodeInfo to obtain peer port number */
			is_peer_switch = get_node_info(&peerportid, data);
			rem_devid = (uint16_t) mad_get_field(data, 0, IB_NODE_DEVID_F);

			mad_decode_field(data, IB_NODE_LOCAL_PORT_F,
					 &peerlocalportnum);

			printf("Peer PortInfo:\n");
			/* Get peer port characteristics */
			peer_espeed_cap = get_port_info(&peerportid, data,
							peerlocalportnum,
							is_peer_switch);
			if (is_mlnx_ext_port_info_supported(rem_devid))
				get_ext_port_info(&peerportid, data2,
						  peerlocalportnum);
			show_port_info(&peerportid, data, peerlocalportnum,
				       peer_espeed_cap);
			if (is_mlnx_ext_port_info_supported(rem_devid))
				show_ext_port_info(&peerportid, data2,
						   peerlocalportnum);

			mad_decode_field(data, IB_PORT_LINK_WIDTH_ENABLED_F,
					 &peerlwe);
			mad_decode_field(data, IB_PORT_LINK_WIDTH_SUPPORTED_F,
					 &peerlws);
			mad_decode_field(data, IB_PORT_LINK_WIDTH_ACTIVE_F,
					 &peerlwa);
			mad_decode_field(data, IB_PORT_LINK_SPEED_SUPPORTED_F,
					 &peerlss);
			mad_decode_field(data, IB_PORT_LINK_SPEED_ACTIVE_F,
					 &peerlsa);
			mad_decode_field(data, IB_PORT_LINK_SPEED_ENABLED_F,
					 &peerlse);
			mad_decode_field(data2,
					 IB_MLNX_EXT_PORT_LINK_SPEED_SUPPORTED_F,
					 &peerfdr10s);
			mad_decode_field(data2,
					 IB_MLNX_EXT_PORT_LINK_SPEED_ENABLED_F,
					 &peerfdr10e);
			mad_decode_field(data2,
					 IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F,
					 &peerfdr10a);
			if (peer_espeed_cap) {
				mad_decode_field(data,
						 IB_PORT_LINK_SPEED_EXT_SUPPORTED_F,
						 &peerlses);
				mad_decode_field(data,
						 IB_PORT_LINK_SPEED_EXT_ACTIVE_F,
						 &peerlsea);
				mad_decode_field(data,
						 IB_PORT_LINK_SPEED_EXT_ENABLED_F,
						 &peerlsee);
			}

			/* Now validate peer port characteristics */
			/* Examine Link Width */
			width = get_link_width(lwe, lws);
			peerwidth = get_link_width(peerlwe, peerlws);
			validate_width(width, peerwidth, lwa);

			/* Examine Link Speeds */
			speed = get_link_speed(lse, lss);
			peerspeed = get_link_speed(peerlse, peerlss);
			validate_speed(speed, peerspeed, lsa);

			if (espeed_cap && peer_espeed_cap) {
				espeed = get_link_speed_ext(lsee, lses);
				peerespeed = get_link_speed_ext(peerlsee,
								peerlses);
				validate_extended_speed(espeed, peerespeed,
							lsea);
			} else {
				if (fdr10e & FDR10 && peerfdr10e & FDR10) {
					if (!(fdr10a & FDR10))
						IBWARN("Peer ports operating at  active speed %d rather than FDR10", lsa);
				}
			}
		}
	}

	mad_rpc_close_port(srcport);
	exit(0);
}
