/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2011 Mellanox Technologies LTD.  All rights reserved.
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

#define _GNU_SOURCE

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <netinet/in.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include <ibdiag_common.h>

static char *node_type_str[] = {
	"???",
	"CA",
	"Switch",
	"Router",
	"iWARP RNIC"
};

static void ca_dump(umad_ca_t * ca)
{
	if (!ca->node_type)
		return;
	printf("%s '%s'\n",
	       ((unsigned)ca->node_type <=
		IB_NODE_MAX ? node_type_str[ca->node_type] : "???"),
	       ca->ca_name);
	printf("\t%s type: %s\n",
	       ((unsigned)ca->node_type <=
		IB_NODE_MAX ? node_type_str[ca->node_type] : "???"),
	       ca->ca_type);
	printf("\tNumber of ports: %d\n", ca->numports);
	printf("\tFirmware version: %s\n", ca->fw_ver);
	printf("\tHardware version: %s\n", ca->hw_ver);
	printf("\tNode GUID: 0x%016" PRIx64 "\n", ntohll(ca->node_guid));
	printf("\tSystem image GUID: 0x%016" PRIx64 "\n",
	       ntohll(ca->system_guid));
}

static char *port_state_str[] = {
	"???",
	"Down",
	"Initializing",
	"Armed",
	"Active"
};

static char *port_phy_state_str[] = {
	"No state change",
	"Sleep",
	"Polling",
	"Disabled",
	"PortConfigurationTraining",
	"LinkUp",
	"LinkErrorRecovery",
	"PhyTest"
};


static int port_dump(umad_port_t * port, int alone)
{
	char *pre = "";
	char *hdrpre = "";

	if (!port)
		return -1;

	if (!alone) {
		pre = "		";
		hdrpre = "	";
	}

	printf("%sPort %d:\n", hdrpre, port->portnum);
	printf("%sState: %s\n", pre,
	       (unsigned)port->state <=
	       4 ? port_state_str[port->state] : "???");
	printf("%sPhysical state: %s\n", pre,
	       (unsigned)port->phys_state <=
	       7 ? port_phy_state_str[port->phys_state] : "???");
	printf("%sRate: %d\n", pre, port->rate);

	if ( (port->state == 4) && (port->rate == 40) ) {
		float rspeed;
		char* str;
		if ( port->link_encoding ) {
			rspeed = ((float)40.0 * 54) / 56;
			str = "FDR10";
		}
		else {
			rspeed = ((float)40.0 * 8) / 10;
			str = "QDR";
		}
		printf("%sReal rate: %2.2f (%s)\n", pre, rspeed, str);
	}

	printf("%sBase lid: %d\n", pre, port->base_lid);
	printf("%sLMC: %d\n", pre, port->lmc);
	printf("%sSM lid: %d\n", pre, port->sm_lid);
	printf("%sCapability mask: 0x%08x\n", pre, ntohl(port->capmask));
	printf("%sPort GUID: 0x%016" PRIx64 "\n", pre, ntohll(port->port_guid));
#ifdef HAVE_UMAD_PORT_LINK_LAYER
	printf("%sLink layer: %s\n", pre, port->link_layer);
#endif
	return 0;
}

static int ca_stat(char *ca_name, int portnum, int no_ports)
{
	umad_ca_t ca;
	int r;

	if ((r = umad_get_ca(ca_name, &ca)) < 0)
		return r;

	if (!ca.node_type)
		return 0;

	if (!no_ports && portnum >= 0) {
		if (portnum > ca.numports || !ca.ports[portnum]) {
			IBWARN("%s: '%s' has no port number %d - max (%d)",
			       ((unsigned)ca.node_type <=
				IB_NODE_MAX ? node_type_str[ca.node_type] :
				"???"), ca_name, portnum, ca.numports);
			return -1;
		}
		printf("%s: '%s'\n",
		       ((unsigned)ca.node_type <=
			IB_NODE_MAX ? node_type_str[ca.node_type] : "???"),
		       ca.ca_name);
		port_dump(ca.ports[portnum], 1);
		return 0;
	}

	/* print ca header */
	ca_dump(&ca);

	if (no_ports)
		return 0;

	for (portnum = 0; portnum <= ca.numports; portnum++)
		port_dump(ca.ports[portnum], 0);

	return 0;
}

static int ports_list(char names[][UMAD_CA_NAME_LEN], int n)
{
	uint64_t guids[64];
	int found, ports, i;

	for (i = 0, found = 0; i < n && found < 64; i++) {
		if ((ports =
		     umad_get_ca_portguids(names[i], guids + found,
					   64 - found)) < 0)
			return -1;
		found += ports;
	}

	for (i = 0; i < found; i++)
		if (guids[i])
			printf("0x%016" PRIx64 "\n", ntohll(guids[i]));
	return found;
}

static int list_only, short_format, list_ports;

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 'l':
		list_only++;
		break;
	case 's':
		short_format++;
		break;
	case 'p':
		list_ports++;
		break;
	default:
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char names[UMAD_MAX_DEVICES][UMAD_CA_NAME_LEN];
	int dev_port = -1;
	int n, i;

	const struct ibdiag_opt opts[] = {
		{"list_of_cas", 'l', 0, NULL, "list all IB devices"},
		{"short", 's', 0, NULL, "short output"},
		{"port_list", 'p', 0, NULL, "show port list"},
		{0}
	};
	char usage_args[] = "<ca_name> [portnum]";
	const char *usage_examples[] = {
		"-l       # list all IB devices",
		"mthca0 2 # stat port 2 of 'mthca0'",
		NULL
	};

	ibdiag_process_opts(argc, argv, NULL, "sDGLCPte", opts, process_opt,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (argc > 1)
		dev_port = strtol(argv[1], 0, 0);

	if (umad_init() < 0)
		IBPANIC("can't init UMAD library");

	if ((n = umad_get_cas_names(names, UMAD_MAX_DEVICES)) < 0)
		IBPANIC("can't list IB device names");

	if (argc) {
		for (i = 0; i < n; i++)
			if (!strncmp(names[i], argv[0], sizeof names[i]))
				break;
		if (i >= n)
			IBPANIC("'%s' IB device can't be found", argv[0]);

		strncpy(names[i], argv[0], sizeof names[i]);
		n = 1;
	}

	if (list_ports) {
		if (ports_list(names, n) < 0)
			IBPANIC("can't list ports");
		return 0;
	}

	if (!list_only && argc) {
		if (ca_stat(argv[0], dev_port, short_format) < 0)
			IBPANIC("stat of IB device '%s' failed", argv[0]);
		return 0;
	}

	for (i = 0; i < n; i++) {
		if (list_only)
			printf("%s\n", names[i]);
		else if (ca_stat(names[i], -1, short_format) < 0)
			IBPANIC("stat of IB device '%s' failed", names[i]);
	}

	return 0;
}
