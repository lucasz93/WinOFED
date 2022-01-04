/*
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2008 Intel Corporation.  All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "..\..\..\..\etc\user\getopt.c"
#include <infiniband/verbs.h>
#include <netinet/in.h>

static int verbose = 0;

static int null_gid(union ibv_gid *gid)
{
	return !(gid->raw[8] | gid->raw[9] | gid->raw[10] | gid->raw[11] |
			 gid->raw[12] | gid->raw[13] | gid->raw[14] | gid->raw[15]);
}

static const char *guid_str(uint64_t node_guid, char *str)
{
	node_guid = ntohll(node_guid);
	sprintf(str, "%04x:%04x:%04x:%04x",
		(unsigned) (node_guid >> 48) & 0xffff,
		(unsigned) (node_guid >> 32) & 0xffff,
		(unsigned) (node_guid >> 16) & 0xffff,
		(unsigned) (node_guid >>  0) & 0xffff);
	return str;
}

static const char *port_phy_state_str(uint8_t phys_state)
{
	switch (phys_state) {
	case 1:  return "SLEEP";
	case 2:  return "POLLING";
	case 3:  return "DISABLED";
	case 4:  return "PORT_CONFIGURATION TRAINNING";
	case 5:  return "LINK_UP";
	case 6:  return "LINK_ERROR_RECOVERY";
	case 7:  return "PHY TEST";
	default: return "invalid physical state";
	}
}

static const char *atomic_cap_str(enum ibv_atomic_cap atom_cap)
{
	switch (atom_cap) {
	case IBV_ATOMIC_NONE: return "ATOMIC_NONE";
	case IBV_ATOMIC_HCA:  return "ATOMIC_HCA";
	case IBV_ATOMIC_GLOB: return "ATOMIC_GLOB";
	default:              return "invalid atomic capability";
	}
}

static const char *mtu_str(enum ibv_mtu max_mtu)
{
	switch (max_mtu) {
	case IBV_MTU_256:  return "256";
	case IBV_MTU_512:  return "512";
	case IBV_MTU_1024: return "1024";
	case IBV_MTU_2048: return "2048";
	case IBV_MTU_4096: return "4096";
	default:           return "invalid MTU";
	}
}

static const char *width_str(uint8_t width)
{
	switch (width) {
	case 1:  return "1";
	case 2:  return "4";
	case 4:  return "8";
	case 8:  return "12";
	default: return "invalid width";
	}
}

static const char *speed_str(uint8_t speed, uint8_t ext_speed)
{
	if ( !ext_speed )
	{
		switch (speed) {
		case 1:  return "2.5 Gbps";
		case 2:  return "5.0 Gbps";
		case 4:  return "10.0 Gbps";
		case 5:  return "20.0 Gbps";
		case 6:  return "40.0 Gbps";
		default: return "invalid speed";
		}
	}
	else
	{
		switch (ext_speed) {
		case 1:  return "14.06 Gbps";
		case 2:  return "25.78 Gbps";
		default: return "invalid speed";
		}
	}
}

static const char *vl_str(uint8_t vl_num)
{
	switch (vl_num) {
	case 1:  return "1";
	case 2:  return "2";
	case 3:  return "4";
	case 4:  return "8";
	case 5:  return "15";
	default: return "invalid value";
	}
}

static int print_all_port_gids(struct ibv_context *ctx, uint8_t port_num, int tbl_len)
{
	union ibv_gid gid;
	int rc = 0;
	int i;

	for (i = 0; i < tbl_len; i++) {
		rc = ibv_query_gid(ctx, port_num, i, &gid);
		if (rc) {
			fprintf(stderr, "Failed to query gid to port %d, index %d\n",
			       port_num, i);
			return rc;
		}
		if (!null_gid(&gid))
			printf("\t\t\tGID[%3d]:\t\t%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
			       i,
			       ntohs(*((uint16_t *) gid.raw + 0)),
			       ntohs(*((uint16_t *) gid.raw + 1)),
			       ntohs(*((uint16_t *) gid.raw + 2)),
			       ntohs(*((uint16_t *) gid.raw + 3)),
			       ntohs(*((uint16_t *) gid.raw + 4)),
			       ntohs(*((uint16_t *) gid.raw + 5)),
			       ntohs(*((uint16_t *) gid.raw + 6)),
			       ntohs(*((uint16_t *) gid.raw + 7)));
	}
	return rc;
}

static int print_hca_cap(struct ibv_device *ib_dev, uint8_t ib_port)
{
	struct ibv_context *ctx;
	struct ibv_device_attr device_attr;
	struct ibv_port_attr port_attr;
	int rc = 0;
	uint8_t port;
	char buf[256];

	ctx = ibv_open_device(ib_dev);
	if (!ctx) {
		fprintf(stderr, "Failed to open device\n");
		rc = 1;
		goto cleanup;
	}
	if (ibv_query_device(ctx, &device_attr)) {
		fprintf(stderr, "Failed to query device props");
		rc = 2;
		goto cleanup;
	}

	printf("hca_id:\t%s\n", ibv_get_device_name(ib_dev));
	if (strlen(device_attr.fw_ver))
		printf("\tfw_ver:\t\t\t\t%s\n", device_attr.fw_ver);
	printf("\tnode_guid:\t\t\t%s\n", guid_str(device_attr.node_guid, buf));
	printf("\tsys_image_guid:\t\t\t%s\n", guid_str(device_attr.sys_image_guid, buf));
	printf("\tvendor_id:\t\t\t0x%04x\n", device_attr.vendor_id);
	printf("\tvendor_part_id:\t\t\t%d\n", device_attr.vendor_part_id);
	printf("\thw_ver:\t\t\t\t0x%X\n", device_attr.hw_ver);

	//if (ibv_read_sysfs_file(ib_dev->ibdev_path, "board_id", buf, sizeof buf) > 0)
	//	printf("\tboard_id:\t\t\t%s\n", buf);

	printf("\tphys_port_cnt:\t\t\t%d\n", device_attr.phys_port_cnt);

	if (verbose) {
		printf("\tmax_mr_size:\t\t\t0x%llx\n",
		       (unsigned long long) device_attr.max_mr_size);
		printf("\tpage_size_cap:\t\t\t0x%llx\n",
		       (unsigned long long) device_attr.page_size_cap);
		printf("\tmax_qp:\t\t\t\t%d\n", device_attr.max_qp);
		printf("\tmax_qp_wr:\t\t\t%d\n", device_attr.max_qp_wr);
		printf("\tdevice_cap_flags:\t\t0x%08x\n", device_attr.device_cap_flags);
		printf("\tmax_sge:\t\t\t%d\n", device_attr.max_sge);
		printf("\tmax_sge_rd:\t\t\t%d\n", device_attr.max_sge_rd);
		printf("\tmax_cq:\t\t\t\t%d\n", device_attr.max_cq);
		printf("\tmax_cqe:\t\t\t%d\n", device_attr.max_cqe);
		printf("\tmax_mr:\t\t\t\t%d\n", device_attr.max_mr);
		printf("\tmax_pd:\t\t\t\t%d\n", device_attr.max_pd);
		printf("\tmax_qp_rd_atom:\t\t\t%d\n", device_attr.max_qp_rd_atom);
		printf("\tmax_ee_rd_atom:\t\t\t%d\n", device_attr.max_ee_rd_atom);
		printf("\tmax_res_rd_atom:\t\t%d\n", device_attr.max_res_rd_atom);
		printf("\tmax_qp_init_rd_atom:\t\t%d\n", device_attr.max_qp_init_rd_atom);
		printf("\tmax_ee_init_rd_atom:\t\t%d\n", device_attr.max_ee_init_rd_atom);
		printf("\tatomic_cap:\t\t\t%s (%d)\n",
		       atomic_cap_str(device_attr.atomic_cap), device_attr.atomic_cap);
		printf("\tmax_ee:\t\t\t\t%d\n", device_attr.max_ee);
		printf("\tmax_rdd:\t\t\t%d\n", device_attr.max_rdd);
		printf("\tmax_mw:\t\t\t\t%d\n", device_attr.max_mw);
		printf("\tmax_raw_ipv6_qp:\t\t%d\n", device_attr.max_raw_ipv6_qp);
		printf("\tmax_raw_ethy_qp:\t\t%d\n", device_attr.max_raw_ethy_qp);
		printf("\tmax_mcast_grp:\t\t\t%d\n", device_attr.max_mcast_grp);
		printf("\tmax_mcast_qp_attach:\t\t%d\n", device_attr.max_mcast_qp_attach);
		printf("\tmax_total_mcast_qp_attach:\t%d\n",
		       device_attr.max_total_mcast_qp_attach);
		printf("\tmax_ah:\t\t\t\t%d\n", device_attr.max_ah);
		printf("\tmax_fmr:\t\t\t%d\n", device_attr.max_fmr);
		if (device_attr.max_fmr) {
			printf("\tmax_map_per_fmr:\t\t%d\n", device_attr.max_map_per_fmr);
		}
		printf("\tmax_srq:\t\t\t%d\n", device_attr.max_srq);
		if (device_attr.max_srq) {
			printf("\tmax_srq_wr:\t\t\t%d\n", device_attr.max_srq_wr);
			printf("\tmax_srq_sge:\t\t\t%d\n", device_attr.max_srq_sge);
		}
		printf("\tmax_pkeys:\t\t\t%d\n", device_attr.max_pkeys);
		printf("\tlocal_ca_ack_delay:\t\t%d\n", device_attr.local_ca_ack_delay);
	}

	for (port = 1; port <= device_attr.phys_port_cnt; ++port) {
		/* if in the command line the user didn't ask for info about this port */
		if ((ib_port) && (port != ib_port))
			continue;

		rc = ibv_query_port(ctx, port, &port_attr);
		if (rc) {
			fprintf(stderr, "Failed to query port %u props\n", port);
			goto cleanup;
		}
		printf("\t\tport:\t%d\n", port);
		printf("\t\t\tstate:\t\t\t%s (%d)\n",
		       ibv_port_state_str(port_attr.state), port_attr.state);
		printf("\t\t\tmax_mtu:\t\t%s (%d)\n",
		       mtu_str(port_attr.max_mtu), port_attr.max_mtu);
		printf("\t\t\tactive_mtu:\t\t%s (%d)\n",
		       mtu_str(port_attr.active_mtu), port_attr.active_mtu);
		printf("\t\t\tsm_lid:\t\t\t%d\n", port_attr.sm_lid);
		printf("\t\t\tport_lid:\t\t%d\n", port_attr.lid);
		printf("\t\t\tport_lmc:\t\t0x%02x\n", port_attr.lmc);
		printf("\t\t\ttransport:\t\t%s\n", port_attr.transport == WvPortTransportRdmaoe ? "RoCE" : 
			port_attr.transport == WvPortTransportIb ? "IB" : "IWarp");

		if (verbose) {
			printf("\t\t\tmax_msg_sz:\t\t0x%x\n", port_attr.max_msg_sz);
			printf("\t\t\tport_cap_flags:\t\t0x%08x\n", port_attr.port_cap_flags);
			printf("\t\t\tmax_vl_num:\t\t%s (%d)\n",
			       vl_str(port_attr.max_vl_num), port_attr.max_vl_num);
			printf("\t\t\tbad_pkey_cntr:\t\t0x%x\n", port_attr.bad_pkey_cntr);
			printf("\t\t\tqkey_viol_cntr:\t\t0x%x\n", port_attr.qkey_viol_cntr);
			printf("\t\t\tsm_sl:\t\t\t%d\n", port_attr.sm_sl);
			printf("\t\t\tpkey_tbl_len:\t\t%d\n", port_attr.pkey_tbl_len);
			printf("\t\t\tgid_tbl_len:\t\t%d\n", port_attr.gid_tbl_len);
			printf("\t\t\tsubnet_timeout:\t\t%d\n", port_attr.subnet_timeout);
			printf("\t\t\tinit_type_reply:\t%d\n", port_attr.init_type_reply);
			printf("\t\t\tactive_width:\t\t%sX (%d)\n",
			       width_str(port_attr.active_width), port_attr.active_width);
			printf("\t\t\tactive_speed:\t\t%s (%d)\n",
			       speed_str(port_attr.active_speed, port_attr.ext_active_speed >> 4), port_attr.active_speed);
			printf("\t\t\tphys_state:\t\t%s (%d)\n",
			       port_phy_state_str(port_attr.phys_state), port_attr.phys_state);

			if (print_all_port_gids(ctx, port, port_attr.gid_tbl_len))
				goto cleanup;
		}
		printf("\n");
	}
cleanup:
	if (ctx)
		if (ibv_close_device(ctx)) {
			fprintf(stderr, "Failed to close device");
			rc = 3;
		}
	return rc;
}

static void usage(const char *argv0)
{
	printf("Usage: %s             print the ca attributes\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
	printf("  -i, --ib-port=<port>   use port <port> of IB device (default all ports)\n");
	printf("  -l, --list             print only the IB devices names\n");
	printf("  -v, --verbose          print all the attributes of the IB device(s)\n");
}

int __cdecl main(int argc, char *argv[])
{
	char *ib_devname = NULL;
	int ret = 0;
	struct ibv_device **dev_list, **orig_dev_list;
	int num_of_hcas;
	uint8_t ib_port = 0;

	/* parse command line options */
	while (1) {
		int c;

		c = getopt(argc, argv, "d:i:lv");
		if (c == -1)
			break;

		switch (c) {
		case 'd':
			ib_devname = _strdup(optarg);
			break;

		case 'i':
			ib_port = (uint8_t) strtol(optarg, NULL, 0);
			break;

		case 'v':
			verbose = 1;
			break;

		case 'l':
			dev_list = orig_dev_list = ibv_get_device_list(&num_of_hcas);
			if (!dev_list) {
				fprintf(stderr, "Failed to get IB devices list");
				return -1;
			}

			printf("%d HCA%s found:\n", num_of_hcas,
			       num_of_hcas != 1 ? "s" : "");

			while (*dev_list) {
				printf("\t%s\n", ibv_get_device_name(*dev_list));
				++dev_list;
			}

			printf("\n");

			ibv_free_device_list(orig_dev_list);

			return 0;

		default:
			usage(argv[0]);
			return -1;
		}
	}

	dev_list = orig_dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		fprintf(stderr, "Failed to get IB device list\n");
		return -1;
	}

	if (ib_devname) {
		while (*dev_list) {
			if (!strcmp(ibv_get_device_name(*dev_list), ib_devname))
				break;
			++dev_list;
		}

		if (!*dev_list) {
			fprintf(stderr, "IB device '%s' wasn't found\n", ib_devname);
			return -1;
		}

		ret |= print_hca_cap(*dev_list, ib_port);
	} else {
		if (!*dev_list) {
			fprintf(stderr, "No IB devices found\n");
			return -1;
		}

		while (*dev_list) {
			ret |= print_hca_cap(*dev_list, ib_port);
			++dev_list;
		}
	}

	if (ib_devname)
		free(ib_devname);

	ibv_free_device_list(orig_dev_list);

	return ret;
}
