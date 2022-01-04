/*
 * Copyright (c) 2005 Mellanox Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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



#include "stdio.h"
#include "string.h"
#include "stdlib.h"


#include <iba/ib_types.h>
#include <iba/ib_al.h>
#ifndef WIN32
#include <complib/cl_device.h>
#endif
#include <mthca/mthca_vc.h>


#define VEND_ID_MELLNOX	0x02c9
#define VEND_ID_VOLTAIRE	0x08f1

enum link_speed_type_e {
	LINK_SPEED_SUPPORTED = 0,
	LINK_SPEED_ENABLED,
	LINK_SPEED_ACTIVE
};

/*******************************************************************
*******************************************************************/


void print64bit(ib_net64_t u64, BOOLEAN hexFormat){
	ib_net64_t mask = (1<<16)-1;
	ib_net16_t tmp;
	int i;
	for(i=0;i<4;i++){
		tmp = (uint16_t)((u64>>(i*16))& mask);
		if(hexFormat){
			printf("%04x",cl_hton16(tmp));
			if(i<3){
				printf(":");
			}
		}else{
			
			if((tmp>>8)<100){
				printf("%02d", tmp>>8);
			}else{
				printf("%03d", tmp>>8);
			}
			printf(".");
			if((tmp&(mask<<8)) <100){
				printf("%02d", tmp&(mask<<8));
			}else{
				printf("%03d", tmp&(mask<<8));
			}
			
		}
	}
}	

void printGUID(char *title, ib_net64_t guid){
	printf(title);
	print64bit(guid, TRUE);
	printf("\n");
}

void printPortGID(ib_net64_t subnetPrefix, ib_net64_t portGuid){
	printf("\t\tGID[0]=");
	print64bit(subnetPrefix, TRUE);
	printf(":");
	print64bit(portGuid, TRUE);
	printf("\n");
}


void printPortLinkState(int portState){ //TODO: check that these are all the options and that they are correct
	switch(portState){
		case 1:
			printf("\t\tport_state=PORT_DOWN (%d)\n",portState);
			break;
		case 2:
			printf("\t\tport_state=PORT_INITIALIZE (%d)\n",portState);
			break;
		case 3:
			printf("\t\tport_state=PORT_ARMED (%d)\n",portState);
			break;
		case 4:
			printf("\t\tport_state=PORT_ACTIVE (%d)\n",portState);
			break;
		case 5:
			printf("\t\tport_state=PORT_ACTDEFER (%d)\n",portState);
			break;
		default:
			printf("\t\tport_state=UNKNOWN (%d)\n",portState); 
	}
}

float __get_speed(uint8_t active_speed, uint8_t ext_speed)
{
	float speed = 0;
	
	if ( ext_speed ) {
		switch ( ext_speed ) {
			case 1:
				speed = 14.06f;
				break;
			case 2:
				speed = 25.78f;
				break;
		}
	}
	else {
		switch ( active_speed ) {
			case 1:
				speed = 2.5;
				break;
			case 2:
				speed = 5.0;
				break;
			case 4:
				speed = 10.0;
				break;
		}
	}
	return speed;
}

void printPortPhysState(uint8_t physState){ 
	switch(physState){
		case 1:
			printf("\t\tport_phys_state=SLEEP (%d)\n",physState);
			break;
		case 2:
			printf("\t\tport_phys_state=POLLING (%d)\n",physState);
			break;
		case 3:
			printf("\t\tport_phys_state=DISABLED (%d)\n",physState);
			break;
		case 4:
			printf("\t\tport_phys_state=CFG_TRAINING (%d)\n",physState);
			break;
		case 5:
			printf("\t\tport_phys_state=LINK_UP (%d)\n",physState);
			break;
		case 6:
			printf("\t\tport_phys_state=LINK_ERROR_RECOVERY (%d)\n",physState);
			break;
		case 7:
			printf("\t\tport_phys_state=PHY_TEST (%d)\n",physState);
			break;
		default:
			printf("\t\tport_phys_state=UNKNOWN (%d)\n",physState); 
	}
}

void printPortRate(enum link_speed_type_e link_speed_type, float speed, int width, int portState, BOOLEAN moreVerbose, BOOLEAN fdr10){
	char *link_speed_type_str[] = { "supported", "enabled", "active" };

	if ((portState == 1) && (link_speed_type == LINK_SPEED_ACTIVE)){ /* In case the port is in Down state */
		printf("\t\tlink_speed=NA\n");
		printf("\t\tlink_width=NA \n\t\trate=NA\n");
	}else{
		if (moreVerbose) {
			int link_width_arr[4] = { 1, 4, 8, 12 };
			int i, link_flag;

			printf("\t\t%s_link_speed=%2.2f\n", link_speed_type_str[link_speed_type], speed);

			link_flag = 0;
			printf("\t\t%s_link_width=", link_speed_type_str[link_speed_type]);
			for (i=0; i < 4; i++)
				if ((width >> i) & 1) {
					printf("%dx (%d), ", link_width_arr[i], 1<<i);
					link_flag = 1;
				}
			if (!link_flag)
				printf("UNKNOWN width (%d)",width);
			printf("\n");

			if (link_speed_type == LINK_SPEED_ACTIVE) {
				link_flag = 0;
				printf("\t\t%s_rate=", link_speed_type_str[link_speed_type]);
				for (i=0; i < 4; i++)
					if ((width >> i) & 1) {
						printf("%2.2f Gbps, ", link_width_arr[i]*speed);
						link_flag = 1;
					}
				if (!link_flag)
					printf("UNKNOWN width (%d)",width);
				printf("\n");

				// only for QDR
				if ( (speed == 10.0) && ((width >> 1) & 1) ) {
					float rspeed;
					char* str;
					if ( fdr10 ) {
						rspeed = ((float)40.0 * 54) / 56;
						str = "FDR10";
					}
					else {
						rspeed = ((float)40.0 * 8) / 10;
						str = "QDR";
					}
					printf("\t\treal_rate=%2.2f Gbps (%s)\n", rspeed, str);
				}
			}
		}
		else {
			printf("\t\tlink_speed=%2.2f Gbps \n",speed);

			switch (width){
			case 1:
				printf("\t\tlink_width=1x (%d) \n\t\trate=%2.2f Gbps\n",width,speed);
				break;
			case 2:
				printf("\t\tlink_width=4x (%d) \n\t\trate=%2.2f Gbps\n",width,4*speed);
				// only for QDR
				if ( speed == 10.0 ) {
					float rspeed;
					char* str;
					if ( fdr10 ) {
						rspeed = ((float)40.0 * 54) / 56;
						str = "FDR10";
					}
					else {
						rspeed = ((float)40.0 * 8) / 10;
						str = "QDR";
					}
					printf("\t\treal_rate=%2.2f Gbps (%s)\n", rspeed, str);
				}
				break;
			case 4:
				printf("\t\tlink_width=8x (%d) \n\t\trate=%2.2f Gbps\n",width,8*speed);
				break;
			case 8:
				printf("\t\tlink_width=12x (%d) \n\t\trate=%2.2f Gbps\n",width,12*speed);
				break;
			default:
				printf("\t\tlink_width=UNKNOWN (%d)\n",width);
		}

		}
	}
}


void printPortMTU(int mtu){ //TODO: check that these are all the options and that they are correct
	switch(mtu){
		case 1:
			printf("\t\tmax_mtu=256 (%d)\n",mtu);
			break;
		case 2:
			printf("\t\tmax_mtu=512 (%d)\n",mtu);
			break;
		case 3:
			printf("\t\tmax_mtu=1024 (%d)\n",mtu);
			break;
		case 4:
			printf("\t\tmax_mtu=2048 (%d)\n",mtu);
			break;
		case 5:
			printf("\t\tmax_mtu=4096 (%d)\n",mtu);
			break;
		default:
			printf("\t\tmax_mtu=UNKNOWN (%d)\n",mtu); 
	}
}

void printPortCaps(ib_port_cap_t *ibal_port_cap_p)
{
#define PRINT_CAP(cap, name) 	if (ibal_port_cap_p->cap) printf( #name "," )
	
	printf("\t\tcapabilities: ");
	PRINT_CAP(cm, CM);
	PRINT_CAP(snmp, SNMP_TUNNEL);
	PRINT_CAP(dev_mgmt, DEVICE_MGMT);
	PRINT_CAP(sm_disable, SM_DISABLED);
	PRINT_CAP(sm, SM);
	PRINT_CAP(vend, VENDOR_CLASS);
	PRINT_CAP(notice, NOTICE);
	PRINT_CAP(trap, TRAP);
	PRINT_CAP(apm, APM);
	PRINT_CAP(slmap, SL_MAP);
	PRINT_CAP(ledinfo, LED_INFO);
	PRINT_CAP(client_reregister, CLIENT_REG);
	PRINT_CAP(sysguid, SYSGUID);
	PRINT_CAP(boot_mgmt, BOOT_MGMT);
	PRINT_CAP(pkey_switch_ext_port, PKEY_SW_EXT_PORT_TRAP);
	PRINT_CAP(link_rtl, LINK_LATENCY);
	PRINT_CAP(reinit, REINIT);
	PRINT_CAP(ipd, OPT_IPD);
	PRINT_CAP(mkey_nvram, MKEY_NVRAM);
	PRINT_CAP(pkey_nvram, PKEY_NVRAM);
	printf("\n");
}

void printPortInfo(ib_port_attr_t* portPtr, ib_port_info_t portInfo, BOOLEAN fullPrint, BOOLEAN moreVerbose){
	float speed;
	BOOLEAN fdr10 = (BOOLEAN)portPtr->link_encoding;
	printf("\t\tport=%d\n", portPtr->port_num);
	printGUID("\t\tport_guid=", portPtr->port_guid);
	printPortLinkState(portPtr->link_state);
	if (moreVerbose) {
		speed = __get_speed(portInfo.state_info1>>4, portPtr->ext_active_speed >> 4);
		printPortRate(LINK_SPEED_SUPPORTED, speed, portInfo.link_width_supported, portPtr->link_state, moreVerbose, fdr10);
		speed = __get_speed(portInfo.link_speed & 0xF, portPtr->ext_active_speed >> 4);
		printPortRate(LINK_SPEED_ENABLED, speed, portInfo.link_width_enabled, portPtr->link_state, moreVerbose, fdr10);
		printf("\t\text_speed_supported=%d\n",portInfo.link_speed_ext & 0x0f);
		printf("\t\text_speed_enabled=%d\n",portInfo.link_speed_ext_enabled & 0x1f);
		printf("\t\tfdr10_supported=%d\n", fdr10);
	}
	speed = __get_speed(portInfo.link_speed>>4, portPtr->ext_active_speed >> 4);
	printPortRate(LINK_SPEED_ACTIVE, speed, portInfo.link_width_active, portPtr->link_state, moreVerbose, fdr10);
	printPortPhysState(portPtr->phys_state);
	printf("\t\tactive_speed=%2.2f Gbps\n",__get_speed(portPtr->active_speed, portPtr->ext_active_speed >> 4));
	printf("\t\tsm_lid=0x%04x\n", cl_ntoh16(portPtr->sm_lid));
	printf("\t\tport_lid=0x%04x\n", cl_ntoh16(portPtr->lid));
	printf("\t\tport_lmc=0x%x\n", portPtr->lmc);
	printf("\t\ttransport=%s\n", portPtr->transport == RDMA_TRANSPORT_RDMAOE ? "RoCE" : 
		portPtr->transport == RDMA_TRANSPORT_IB ? "IB" : "IWarp");
	printPortMTU(portPtr->mtu);
	if(fullPrint){
		printf("\t\tmax_msg_sz=0x%x	(Max message size)\n", portPtr->max_msg_size);
		printPortCaps( &portPtr->cap );
		printf("\t\tmax_vl_num=0x%x		(Maximum number of VL supported by this port)\n", portPtr->max_vls);
		printf("\t\tbad_pkey_counter=0x%x	(Bad PKey counter)\n", portPtr->pkey_ctr);
		printf("\t\tqkey_viol_counter=0x%x	(QKey violation counter)\n", portPtr->qkey_ctr);
		printf("\t\tsm_sl=0x%x		(IB_SL to be used in communication with subnet manager)\n", portPtr->sm_sl);
		printf("\t\tpkey_tbl_len=0x%x	(Current size of pkey table)\n", portPtr->num_pkeys);
		printf("\t\tgid_tbl_len=0x%x	(Current size of GID table)\n", portPtr->num_gids);
		printf("\t\tsubnet_timeout=0x%x	(Subnet Timeout for this port (see PortInfo))\n", portPtr->subnet_timeout);
		printf("\t\tinitTypeReply=0x%x	(optional InitTypeReply value. 0 if not supported)\n", portPtr->init_type_reply);
		printPortGID(portPtr->p_gid_table->unicast.prefix, portPtr->p_gid_table->unicast.interface_id);
	}
	printf("\n");
}

void print_uplink_info(ib_ca_attr_t* ca_attr)
{
	uplink_info_t*p_uplink_info = mthca_get_uplink_info(ca_attr);
	char *bus_type, *link_speed, cap;
	int freq;

	switch (p_uplink_info->bus_type) {
		case UPLINK_BUS_PCI: bus_type = "PCI"; break;
		case UPLINK_BUS_PCIX: bus_type = "PCI_X"; break;
		case UPLINK_BUS_PCIE: bus_type = "PCI_E"; break;
		default: printf("\tuplink={BUS=UNRECOGNIZED (%d)}\n", p_uplink_info->bus_type); return;
	}

	switch (p_uplink_info->bus_type) {
		case UPLINK_BUS_PCI: 
		case UPLINK_BUS_PCIX:
			if (p_uplink_info->u.pci_x.capabilities == UPLINK_BUS_PCIX_133)
				freq = 133;
			else
				freq = 66;
			printf("\tuplink={BUS=%s, CAPS=%d MHz}\n", bus_type, freq ); 
			break;

		case UPLINK_BUS_PCIE:
			cap = p_uplink_info->u.pci_e.capabilities;
			if (p_uplink_info->u.pci_e.link_speed == UPLINK_BUS_PCIE_SDR)
				link_speed = "2.5 Gbps";
			else
			if (p_uplink_info->u.pci_e.link_speed == UPLINK_BUS_PCIE_DDR)
				link_speed = "5.0 Gbps";
			else
				link_speed = "unknown";
			printf("\tuplink={BUS=%s, SPEED=%s, WIDTH=x%d, CAPS=%s*x%d}\n",
				bus_type, link_speed, p_uplink_info->u.pci_e.link_width,
				(cap&1) ? "2.5" : "5", cap>>2 ); 
			break;
	}

	/* MSI-X */
	if (p_uplink_info->x.valid)
		printf("\tMSI-X={ENABLED=%d, SUPPORTED=%d, GRANTED=%d, ALL_MASKED=%s}\n",
			p_uplink_info->x.enabled, p_uplink_info->x.requested,
			p_uplink_info->x.granted, p_uplink_info->x.masked ? "Y" : "N"); 
}

void vstat_print_ca_attr(int idx,  ib_ca_attr_t* ca_attr, ib_port_info_t* vstat_port_info, 
			 BOOLEAN fullPrint, BOOLEAN moreVerbose){
	int i;

	printf("\n\thca_idx=%d\n", idx);
	print_uplink_info(ca_attr);
	printf("\tvendor_id=0x%04x\n", ca_attr->vend_id);
	if(moreVerbose)
		printf("\tvendor_part_id=0x%04x (%d)\n", ca_attr->dev_id, ca_attr->dev_id);
	else
		printf("\tvendor_part_id=%hu\n", ca_attr->dev_id);

	if (!ca_attr->num_ports) {
		printf("\n\tATTENTION! \n\t    The driver has reported zero physical ports. "
			"\n\t    It can be a result of incompatibility of user and kernel components."
			"\n\t    It can be also caused by a driver failure on startup. "
			"\n\t    Look into System Event Log for driver reports. "
			"\n\t    Use firmware tools to solve the problem (in second case)."
			"\n\t    The work with IB stack is impossible. \n");
		return;
	}
	
	printf("\thw_ver=0x%x\n", ca_attr->revision); //TODO: ???
	if(ca_attr->vend_id == VEND_ID_MELLNOX || ca_attr->vend_id == VEND_ID_VOLTAIRE) {
		printf("\tfw_ver=%d.%.2d.%.4d\n",
		(uint16_t)(ca_attr->fw_ver>>32),
		(uint16_t)(ca_attr->fw_ver>>16),
		(uint16_t)(ca_attr->fw_ver));
		printf("\tPSID=%s\n",mthca_get_board_id(ca_attr));
	}else{
		printf("\tfw_ver=0x%I64x\n",ca_attr->fw_ver);
	}
	printGUID("\tnode_guid=", ca_attr->ca_guid);
	if(fullPrint){
		printGUID("\tsys_image_guid=", ca_attr->system_image_guid);
		printf("\tnum_phys_ports = %d\n",ca_attr->num_ports);
		printf("\tmax_num_qp = 0x%x		(Maximum Number of QPs supported)\n", ca_attr->max_qps);
		printf("\tmax_qp_ous_wr = 0x%x		(Maximum Number of outstanding WR on any WQ)\n", ca_attr->max_wrs);
		printf("\tmax_num_sg_ent = 0x%x		(Max num of scatter/gather entries for WQE other than RD)\n", ca_attr->max_sges);
		printf("\tmax_num_sg_ent_rd = 0x%x		(Max num of scatter/gather entries for RD WQE)\n",  ca_attr->max_rd_sges);
		printf("\tmax_num_srq = 0x%x		(Maximum Number of SRQs supported)\n", ca_attr->max_srq);
		printf("\tmax_wqe_per_srq = 0x%x	(Maximum Number of outstanding WR on any SRQ)\n", ca_attr->max_srq_wrs);
		printf("\tmax_srq_sentries = 0x%x		(Maximum Number of scatter/gather entries for SRQ WQE)\n", ca_attr->max_srq_sges);
		printf("\tsrq_resize_supported = %d	(SRQ resize supported)\n", ca_attr->modify_srq_depth);
		printf("\tmax_num_cq = 0x%x		(Max num of supported CQs)\n", ca_attr->max_cqs);
		printf("\tmax_num_ent_cq = 0x%x	(Max num of supported entries per CQ)\n", ca_attr->max_cqes);
		printf("\tmax_num_mr = 0x%x		(Maximum number of memory region supported)\n", ca_attr->init_regions);
		printf("\tmax_mr_size = 0x%x	(Largest contiguous block of memory region in bytes)\n", ca_attr->init_region_size);
		printf("\tmax_pd_num = 0x%x		(Maximum number of protection domains supported)\n", ca_attr->max_pds);
		printf("\tpage_size_cap = 0x%x		(Largest page size supported by this HCA)\n",ca_attr->p_page_size[ca_attr->num_page_sizes-1]);

		printf("\tlocal_ca_ack_delay = 0x%x	(Log2 4.096usec Max. RX to ACK or NAK delay)\n", ca_attr->local_ack_delay);
		printf("\tmax_qp_ous_rd_atom = 0x%x	(Maximum number of oust. RDMA read/atomic as target)\n",ca_attr->max_qp_resp_res);
		printf("\tmax_ee_ous_rd_atom = 0		(EE Maximum number of outs. RDMA read/atomic as target)\n");
		printf("\tmax_res_rd_atom = 0x%x		(Max. Num. of resources used for RDMA read/atomic as target)\n",ca_attr->max_resp_res);
		printf("\tmax_qp_init_rd_atom = 0x%x	(Max. Num. of outs. RDMA read/atomic as initiator)\n",ca_attr->max_qp_init_depth);
		printf("\tmax_ee_init_rd_atom = 0		(EE Max. Num. of outs. RDMA read/atomic as initiator)\n");
		printf("\tatomic_cap = %s		(Level of Atomicity supported)\n",ca_attr->atomicity == IB_ATOMIC_GLOBAL?"GLOBAL":
																	ca_attr->atomicity == IB_ATOMIC_LOCAL?"LOCAL":"NORMAL");
		printf("\tmax_ee_num = 0x0		(Maximum number of EEC supported)\n");
		printf("\tmax_rdd_num = 0x0		(Maximum number of IB_RDD supported)\n");
		printf("\tmax_mw_num = 0x%x		(Maximum Number of memory windows supported)\n", ca_attr->init_windows);
		printf("\tmax_raw_ipv6_qp = 0x%x		(Maximum number of Raw IPV6 QPs supported)\n", ca_attr->max_ipv6_qps);
		printf("\tmax_raw_ethy_qp = 0x%x		(Maximum number of Raw Ethertypes QPs supported)\n", ca_attr->max_ether_qps);
		printf("\tmax_mcast_grp_num = 0x%x	(Maximum Number of multicast groups)\n", ca_attr->max_mcast_grps);
		printf("\tmax_mcast_qp_attach_num = 0x%x	(Maximum number of QP per multicast group)\n", ca_attr->max_qps_per_mcast_grp);
		printf("\tmax_ah_num = 0x%I64x		(Maximum number of address handles)\n", ca_attr->max_addr_handles);
		printf("\tmax_num_fmr = 0x%x		(Maximum number FMRs)\n", ca_attr->max_fmr);
		printf("\tmax_num_map_per_fmr = 0x%x	(Maximum number of (re)maps per FMR before an unmap operation in required)\n", ca_attr->max_map_per_fmr);
		printf("\tmodify_wr_depth = %d		(Capabilities: change QP depth during a modify QP)\n", !!ca_attr->modify_wr_depth);
		printf("\tmodify_srq_depth = %d 		(Capabilities: change SRQ depth - Not supported by driver!)\n", !!ca_attr->modify_srq_depth);
		printf("\tchange_primary_port = %d		(Capabilities: change primary port for a QP during a SQD->RTS transition)\n", !!ca_attr->change_primary_port);
		printf("\tav_port_check = %d		(Capabilities: check port number in address handles)\n", !!ca_attr->av_port_check);
		printf("\tinit_type_support = %d		(Capabilities: set init_type)\n", !!ca_attr->init_type_support);
		printf("\tshutdown_port = %d		(Capabilities: shutdown port support)\n", !!ca_attr->shutdown_port_capability);
	}else{
		printf("\tnum_phys_ports=%d\n", 	ca_attr->num_ports);
	}
	for (i = 0; i<ca_attr->num_ports; i++){
		printPortInfo(ca_attr->p_port_attr+i, vstat_port_info[i], fullPrint, moreVerbose);
	}	
}
/* Internal Functions */

void vstat_get_counters(ib_ca_handle_t h_ca,uint8_t port_num)
{
	ib_mad_t			*mad_in = NULL;
	ib_mad_t			*mad_out = NULL;
	ib_port_counters_t	*port_counters;
	ib_api_status_t 	ib_status = IB_SUCCESS;
	int i;
	
	mad_out = (ib_mad_t*)cl_zalloc(256);
	CL_ASSERT(mad_out);

	mad_in = (ib_mad_t*)cl_zalloc(256);
	CL_ASSERT(mad_in);


	mad_in->attr_id = IB_MAD_ATTR_PORT_CNTRS;
	mad_in->method = IB_MAD_METHOD_GET;
	mad_in->base_ver = 1;
	mad_in->class_ver =1;
	mad_in->mgmt_class = IB_MCLASS_PERF;

	port_counters = (ib_port_counters_t*)(((ib_gmp_t*)mad_in)->data);

	port_counters->port_select= port_num;
	port_counters->counter_select= 0xff;

	ib_status = ib_local_mad(h_ca ,port_num ,mad_in ,mad_out);
	if(ib_status != IB_SUCCESS)
	{
		printf("ib_local_mad failed with status = %d\n", ib_status);
		return;
	}
	
	port_counters = (ib_port_counters_t*)(((ib_gmp_t*)mad_out)->data);

	printf("\n\tport counters for port %d\n",port_num);
	printf("\t\tlink_error_recovery_counter\t0x%x \n",port_counters->link_error_recovery_counter);
	printf("\t\tlink_down_counter\t\t0x%x \n",port_counters->link_down_counter);
	printf("\t\tport_rcv_errors\t\t\t0x%x \n",CL_NTOH16(port_counters->port_rcv_errors));
	printf("\t\tport_rcv_remote_physical_errors\t0x%x \n",CL_NTOH16(port_counters->port_rcv_remote_physical_errors));
	printf("\t\tport_rcv_switch_relay_errors\t0x%x \n",CL_NTOH16(port_counters->port_rcv_switch_relay_errors));
	printf("\t\tport_xmit_discard\t\t0x%x \n",CL_NTOH16(port_counters->port_xmit_discard));
	printf("\t\tport_xmit_constraint_errors\t0x%x \n",port_counters->port_xmit_constraint_errors);
	printf("\t\tport_rcv_constraint_errors\t0x%x \n",port_counters->port_rcv_constraint_errors);
	printf("\t\tvl15_dropped\t\t\t0x%x \n",CL_NTOH16(port_counters->vl15_dropped));
	printf("\t\tport_rcv_data\t\t\t0x%x \n",CL_NTOH32(port_counters->port_rcv_data));
	printf("\t\tport_xmit_data\t\t\t0x%x \n",CL_NTOH32(port_counters->port_xmit_data));
	printf("\t\tport_rcv_pkts\t\t\t0x%x \n",CL_NTOH32(port_counters->port_rcv_pkts));
	printf("\t\tport_xmit_pkts\t\t\t0x%x \n\n",CL_NTOH32(port_counters->port_xmit_pkts));
	
}


void vstat_get_port_info(ib_ca_handle_t h_ca,uint8_t port_num, ib_port_info_t* vstat_port_info)
{
	ib_mad_t			*mad_in = NULL;
	ib_mad_t			*mad_out = NULL;
	ib_api_status_t 	ib_status = IB_SUCCESS;
	int i;
	
	mad_out = (ib_mad_t*)cl_zalloc(256);
	CL_ASSERT(mad_out);

	mad_in = (ib_mad_t*)cl_zalloc(256);
	CL_ASSERT(mad_in);


	mad_in->attr_id = IB_MAD_ATTR_PORT_INFO;
	mad_in->method = IB_MAD_METHOD_GET;
	mad_in->base_ver = 1;
	mad_in->class_ver =1;
	mad_in->mgmt_class = IB_MCLASS_SUBN_LID;
	


	ib_status = ib_local_mad(h_ca ,port_num ,mad_in ,mad_out);
	if(ib_status != IB_SUCCESS &&  0 != mad_in->status )
	{
		printf("ib_local_mad failed with status = %d mad status = %d\n", ib_status,mad_in->status);
		return;
	}

	cl_memcpy(vstat_port_info,(ib_port_info_t*)(((ib_smp_t*)mad_out)->data),sizeof(ib_port_info_t));

	cl_free(mad_out);
	cl_free(mad_in);
	
}

void
ca_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{
	printf( "*******************************************************************************\n");
	printf( "* Gotten async event: port_num %d, code %#x (%s), vendor %#I64x *\n",
		p_err_rec->port_number, p_err_rec->code, 
		ib_get_async_event_str(p_err_rec->code), 
		p_err_rec->vendor_specific );
	printf( "*******************************************************************************\n");
}


ib_api_status_t
vstat_ca_attr(
	boolean_t modify_attr,
	BOOLEAN fullPrint,
	BOOLEAN getCounters,
	BOOLEAN moreVerbose,
	BOOLEAN process,
	ULONG	timeout
	)
{
	ib_al_handle_t		h_al = NULL;
	ib_api_status_t 	ib_status = IB_SUCCESS;
	ib_api_status_t 	ret_status = IB_SUCCESS;
	size_t 			guid_count;
	ib_net64_t		*ca_guid_array = NULL;
	ib_ca_attr_t		*vstat_ca_attr;
	ib_port_info_t		vstat_port_info[2];
	size_t 			i;
	ib_ca_handle_t 	h_ca = NULL;
	uint32_t 			bsize;
	ib_port_attr_mod_t port_attr_mod;
	uint8_t			port_idx;

	while(1)
	{
		/*
		 * Open the AL instance
		 */
		ib_status = ib_open_al(&h_al);
		if(ib_status != IB_SUCCESS)
		{
			printf("ib_open_al failed status = %d\n", ib_status);
			ret_status = ib_status;			
			break;
		}
		//xxxx
		//printf("ib_open_al PASSED.\n");
		//xxx
		CL_ASSERT(h_al);

		/*
		 * Get the Local CA Guids
		 */
		ib_status = ib_get_ca_guids(h_al, NULL, &guid_count);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			printf("ib_get_ca_guids1 failed status = %d\n", (uint32_t)ib_status);
			ret_status = ib_status;			
			goto Cleanup1;
		}

		

		/*
		 * If no CA's Present then return
		 */

		if(guid_count == 0)
			goto Cleanup1;

		
		ca_guid_array = (ib_net64_t*)cl_malloc(sizeof(ib_net64_t) * guid_count);
		CL_ASSERT(ca_guid_array);
		
		ib_status = ib_get_ca_guids(h_al, ca_guid_array, &guid_count);
		if(ib_status != IB_SUCCESS)
		{
			printf("ib_get_ca_guids2 failed with status = %d\n", ib_status);
			ret_status = ib_status;			
			goto Cleanup1;
		}

		

		/*
		 * For Each CA Guid found Open the CA,
		 * Query the CA Attribute and close the CA
		 */
		for(i=0; i < guid_count; i++)
		{

			/* Open the CA */
			ib_status = ib_open_ca(h_al,
				ca_guid_array[i],
				ca_err_cb,
				NULL,	//ca_context
				&h_ca);

			if(ib_status != IB_SUCCESS)
			{
				printf("ib_open_ca failed with status = %d\n", ib_status);
				ret_status = ib_status;				
				goto Cleanup1;
			}

			//xxx
			//printf("ib_open_ca passed i=%d\n",i); 
			//xxx

repeat:
			/* Query the CA */
			bsize = 0;
			ib_status = ib_query_ca(h_ca, NULL, &bsize);
			if(ib_status != IB_INSUFFICIENT_MEMORY)
			{
				printf("ib_query_ca failed with status = %d\n", ib_status);
				ret_status = ib_status;
				goto Cleanup2;
			}
			CL_ASSERT(bsize);
			//xxxx
			//printf("ib_query_ca PASSED bsize = 0x%x.\n",bsize);
			//xxx
			/* Allocate the memory needed for query_ca */

			vstat_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
			CL_ASSERT(vstat_ca_attr);

			ib_status = ib_query_ca(h_ca, vstat_ca_attr, &bsize);
			if(ib_status != IB_SUCCESS)
			{
				printf("ib_query_ca failed with status = %d\n", ib_status);
				ret_status = ib_status;
				goto Cleanup2;
			}

			for(port_idx =0; port_idx< vstat_ca_attr->num_ports;port_idx++){
				vstat_get_port_info(h_ca ,port_idx+1,&vstat_port_info[port_idx]);
			}

			vstat_print_ca_attr((int)i, vstat_ca_attr, vstat_port_info, fullPrint, moreVerbose);
			if(getCounters)
			{
				for(port_idx =0; port_idx< vstat_ca_attr->num_ports;port_idx++){
					vstat_get_counters(h_ca ,port_idx+1);
				}
			}

			if (process) {
				printf( "\nSleeping for %d msec\n\n", timeout);
				Sleep( timeout );
				goto repeat;
			}
			
			/* Free the memory */
			cl_free(vstat_ca_attr);
			vstat_ca_attr = NULL;
			/* Close the current open CA */
			ib_status = ib_close_ca(h_ca, NULL);
			if(ib_status != IB_SUCCESS)
			{
				printf("ib_close_ca failed status = %d", ib_status);
				ret_status = ib_status;
			}
			h_ca = NULL;

		}

Cleanup2:
		if(h_ca != NULL)
		{
			ib_status = ib_close_ca(h_ca, NULL);
			if(ib_status != IB_SUCCESS)
			{
				printf("ib_close_ca failed status = %d", ib_status);
			}
		}

Cleanup1:
		cl_free(ca_guid_array);
		ib_status = ib_close_al(h_al);

		if(ib_status != IB_SUCCESS)
		{
			printf("ib_close_al failed status = %d", ib_status);
		}

		break;

	} //End of while(1)

	
	return ret_status;
}

void vstat_help()
{
	printf("\n\tUsage: vstat [-v] [-c] [-m] [-p N]\n");
	printf("\t\t -v      - verbose mode\n");
	printf("\t\t -c      - HCA error/statistic counters\n");
	printf("\t\t -m      - more verbose mode\n");
	printf("\t\t -p N    - repeat every N sec\n");
	
}

int32_t __cdecl
main(
	int32_t argc,
	char* argv[])
{
	ib_api_status_t ib_status;
	BOOLEAN fullPrint = FALSE;
	BOOLEAN getCounters = FALSE;
	BOOLEAN showHelp = FALSE;
	BOOLEAN moreVerbose = FALSE;
	BOOLEAN process = FALSE;
	ULONG	period_sec = 5;
	if(argc>1){
		int i = 2;
		while(i<=argc){
			if(!_stricmp(argv[i-1], "-v")){
				fullPrint = TRUE;
				i+=1;
			}else if(!_stricmp(argv[i-1], "-h") || 
				!_stricmp(argv[i-1], "-help")){
				showHelp = TRUE;
				i+=1;
			}else if(!_stricmp(argv[i-1], "-c")){
				getCounters = TRUE;
				i+=1;
			}else if(!_stricmp(argv[i-1], "-p")){
				process = TRUE;
				i+=1;
				if (i>argc)
					break;
				period_sec = strtol(argv[i-1], NULL, 0);
				if (period_sec<1 || period_sec>60)
					period_sec = 5;
				i+=1;
			}else if(!_stricmp(argv[i-1], "-m")){
				fullPrint = TRUE;
				moreVerbose = TRUE;
				i+=1;
			}else{
				i+=2;
			}
		}
	}
	if (showHelp)
		vstat_help();
	else
		ib_status = vstat_ca_attr(FALSE, fullPrint,getCounters, moreVerbose, process, period_sec*1000);

	return 0;
}



