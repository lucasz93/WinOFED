/*
* Copyright (c) 2011 Intel Corporation.  All rights reserved.
* Copyright (c) 2005 Mellanox Technologies.  All rights reserved.
* Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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
*
* $Id: PrintIp.c 3379 2012-01-12 23:33:26Z stan.smith@intel.com $
*/


#include <windows.h>
#include <devioctl.h>
#include <stdio.h>
#include <Iphlpapi.h>
#include "iba\ib_types.h"
#include <iba\ibat_ex.h>
#include <iba\ib_at_ioctl.h>

#include <rdma\winverbs.h>

typedef struct _RDMA_DEV_IP
{
	IBAT_PORT_RECORD	pr;
	IP_ADDRESS 			ipa;

}	RDMA_DEV_IP_ADDR;


int
get_rdma_dev_IP_addrs( OUT RDMA_DEV_IP_ADDR  **ipAddrs )
{
	HRESULT hr = S_OK;
	char temp[1024];
	char temp1[1024];
	RDMA_DEV_IP_ADDR *ip_addrs=NULL;
	int ipa_cnt=0;

	IOCTL_IBAT_PORTS_OUT *p_ipoib_ports_out;
	IBAT_PORT_RECORD *ports_records;

	IOCTL_IBAT_IP_ADDRESSES_OUT *addresses_out;
	IP_ADDRESS *ip_addreses;

	BOOL ret;
	int i,j;

	*ipAddrs = NULL;
	p_ipoib_ports_out = (IOCTL_IBAT_PORTS_OUT *)temp;

	ret = IbatexGetPorts( sizeof(temp), p_ipoib_ports_out );
	if (ret != 0) {
		hr = HRESULT_FROM_WIN32(GetLastError());
		printf("IbatexGetPorts() failed hr=0x%x\n", hr);
		goto err_out;
	}
	if (p_ipoib_ports_out->Size > sizeof(temp)) {
		printf("Data truncated, IbatexGetPorts() requires a bufsize of %d bytes\n",
				p_ipoib_ports_out->Size);
		goto err_out;
	}

	ports_records = p_ipoib_ports_out->Ports;

	i = (sizeof(RDMA_DEV_IP_ADDR) * 4) * p_ipoib_ports_out->NumPorts;
	ip_addrs = (RDMA_DEV_IP_ADDR*)malloc(i);
	if (!ip_addrs)
	{
		fprintf(stderr,"ERR: malloc(%d)?\n",i);
		goto err_out;
	}

	for (i=ipa_cnt=0; i < p_ipoib_ports_out->NumPorts; i++)
	{
		// collect the ip adresses for the current port
		addresses_out = (IOCTL_IBAT_IP_ADDRESSES_OUT *)temp1;

		//printf("Port[%d] ca guid=0x%I64x port guid=0x%I64x\n", i,
		//		CL_NTOH64(ports_records[i].CaGuid),
		//		CL_NTOH64(ports_records[i].PortGuid));

		ret = IbatexGetIpAddresses( ports_records[i].PortGuid,
									sizeof(temp1),
									addresses_out );
		if (ret != 0)
		{
			hr = HRESULT_FROM_WIN32(GetLastError());
			fprintf(stderr,"IbatexGetIpAddresses failed hr=0x%x, required bufSize %ld "
							"supplied bufSize %ld\n",
							hr, addresses_out->Size,sizeof(temp1));
			goto err_out;
		}

		if (addresses_out->Size > sizeof(temp1) )
		{
			printf("Data truncated, IbatexGetIpAddresses() requires %d bytes\n",
					addresses_out->Size);
			goto err_out;
		}

		if (addresses_out->AddressCount > 0) {
			ip_addreses = addresses_out->Address;
			for (j=0; j < addresses_out->AddressCount; j++)
			{
				ip_addrs[ipa_cnt].pr.CaGuid = ports_records[i].CaGuid;
				ip_addrs[ipa_cnt].pr.PortGuid = ports_records[i].PortGuid;
				ip_addrs[ipa_cnt].pr.PKey = ports_records[i].PKey;
				ip_addrs[ipa_cnt].pr.PortNum = ports_records[i].PortNum;

				memcpy( (void*)&ip_addrs[ipa_cnt].ipa.Address,
						(void*)ip_addreses[j].Address,
						sizeof(IP_ADDRESS) );
				ipa_cnt++;
			}
		}
	}

	*ipAddrs = ip_addrs;
	return ipa_cnt;

err_out:
	if (ip_addrs)
		free((void*)ip_addrs);

	return 0;
}


// Print all ips (IP addresses) that are related to infiniband on this computer
int print_ips()
{
	HRESULT hr = S_OK;
	char temp [1024];
	char temp1 [1024];
	IOCTL_IBAT_PORTS_OUT *p_ipoib_ports_out;
	IBAT_PORT_RECORD *ports_records;

	IOCTL_IBAT_IP_ADDRESSES_OUT *addresses_out;
	IP_ADDRESS       *ip_addreses;

	BOOL ret;
	int i,j;

	p_ipoib_ports_out = (IOCTL_IBAT_PORTS_OUT *)temp;
	ret = IbatexGetPorts( sizeof(temp), p_ipoib_ports_out );
	if (ret != 0) {
		hr = HRESULT_FROM_WIN32(GetLastError());
		printf("IbatexGetPorts failed hr=0x%x\n", hr);
		return 1;
	}

	if (p_ipoib_ports_out->Size > sizeof(temp)) {
		printf("Data truncated, IbatexGetPorts() requires a buffer of %d bytes\n",
				p_ipoib_ports_out->Size);
		return 1;
	}

	ports_records = p_ipoib_ports_out->Ports;
	printf("IPoIB interfaces: %d\n", p_ipoib_ports_out->NumPorts);
	for (i = 0 ; i < p_ipoib_ports_out->NumPorts; i++)
	{
		printf("%d: ca guid=0x%I64x port guid=0x%I64x\n", i,
				CL_NTOH64(ports_records[i].CaGuid),
				CL_NTOH64(ports_records[i].PortGuid));

		// print the ip adresses of this port
		addresses_out = (IOCTL_IBAT_IP_ADDRESSES_OUT *)temp1;

		//printf("Port[%d] ca guid=0x%I64x port guid=0x%I64x\n", i,
		//		CL_NTOH64(ports_records[i].CaGuid),
		//		CL_NTOH64(ports_records[i].PortGuid));

		ret = IbatexGetIpAddresses( ports_records[i].PortGuid,
									sizeof(temp1),
									addresses_out );
		if (ret != 0)
		{
			hr = HRESULT_FROM_WIN32(GetLastError());
			fprintf(stderr,"IbatexGetIpAddresses failed hr=0x%x\n", hr);
			return 1;
		}
		if (addresses_out->Size > sizeof(temp1) )
		{
			printf("Data truncated, IbatexGetIpAddresses requires %d bytes\n",
					p_ipoib_ports_out->Size);
			return 1;
		}

		printf("   found %d ips:", addresses_out->AddressCount);
		ip_addreses = addresses_out->Address;
		for (j = 0; j < addresses_out->AddressCount; j++)
		{
			printf("    %d.%d.%d.%d   ",
				ip_addreses[j].Address[12],
				ip_addreses[j].Address[13],
				ip_addreses[j].Address[14],
				ip_addreses[j].Address[15]);
		}
		printf("\n");
	}

	return 0;
}

void print_usage(char *argv[])
{
	char *base = strrchr(argv[0],'\\');

	if (base)
		base++;
	else
		base = argv[0];

	printf("\nPrint IPoIB IPv4 addresses or ARP for IPv4 address(es)\n\n");
	printf("Usage: %s <print_ips|ips>\n",base);
	printf("Usage: %s <localip|lip>\n",base);
	printf("       %s <remoteip|rip> <ip ...> (example %s rip 1.2.3.4)\n",
		base, base);
}

int local_ip(void)
{
	RDMA_DEV_IP_ADDR *ip_addrs;
	int j,ip_addrs_cnt=0;

	/* get Port Records for ports which have an IPv4 address assigned */
	ip_addrs_cnt = get_rdma_dev_IP_addrs( &ip_addrs );
	if (ip_addrs_cnt == 0) {
		printf("get_rdma_dev_IP_addrs() failed?\n");
		return 1;
	}
	printf("Found %d IP active ports\n",ip_addrs_cnt);
	for(j=0; j < ip_addrs_cnt; j++) {
		printf("%d: ca guid 0x%I64x port guid 0x%I64x  [%d.%d.%d.%d]\n",
				j,
				CL_NTOH64(ip_addrs[j].pr.CaGuid),
				CL_NTOH64(ip_addrs[j].pr.PortGuid),
				ip_addrs[j].ipa.Address[12],
				ip_addrs[j].ipa.Address[13],
				ip_addrs[j].ipa.Address[14],
				ip_addrs[j].ipa.Address[15]);
	}
	free((void*)ip_addrs);
	return 0;
}


int remote_ip(char *remote_ip)
{
	HRESULT hr = S_OK;
	IPAddr ip;
	char *pIp = (char *)&ip;
	int b1,b2,b3,b4;
	DWORD  ret;
	IOCTL_IBAT_MAC_TO_GID_OUT gid;

	ULONG pMacAddr[2]={0,0}, PhyAddrLen ;
	unsigned char *pMac = (unsigned char *)&pMacAddr[0];

	RDMA_DEV_IP_ADDR *ip_addrs;
	int ip_addrs_cnt=0;

	PhyAddrLen = sizeof(pMacAddr);
	sscanf(remote_ip, "%d.%d.%d.%d", &b1, &b2, &b3, &b4);
	printf("ARP'ing for address %d.%d.%d.%d\n", b1, b2, b3, b4);

	pIp[0] = (char)b1;
	pIp[1] = (char)b2;
	pIp[2] = (char)b3;
	pIp[3] = (char)b4;

	ret = SendARP(ip ,0 ,pMacAddr, &PhyAddrLen );
	if (ret != NO_ERROR)
	{
		printf("Error in SendARP\n");
		return 1;
	}

	printf("  Mac addr %x-%x-%x-%x-%x-%x\n",
		pMac[0], pMac[1], pMac[2], pMac[3], pMac[4], pMac[5] );

	/* get Port Records for ports which have an IPv4 address assigned */
	ip_addrs_cnt = get_rdma_dev_IP_addrs( &ip_addrs );
	if (ip_addrs_cnt == 0) {
		printf("get_rdma_dev_IP_addrs() failed?\n");
		return 1;
	}

	ret = IbatexMacToGid( (ULONGLONG)ip_addrs[0].pr.PortGuid, pMac, &gid.DestGid );
	if (ret != 0)
	{
		hr = HRESULT_FROM_WIN32(GetLastError());
		printf("DeviceIoControl failed for IOCTL_IBAT_MAC_TO_GID hr=0x%x\n",hr);
		return 1;
	}
	else {
		printf("  GID %x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x\n", 
			gid.DestGid.raw[0], gid.DestGid.raw[1], gid.DestGid.raw[2],
			gid.DestGid.raw[3], gid.DestGid.raw[4], gid.DestGid.raw[5],
			gid.DestGid.raw[6], gid.DestGid.raw[7], gid.DestGid.raw[8],
			gid.DestGid.raw[9], gid.DestGid.raw[10], gid.DestGid.raw[11],
			gid.DestGid.raw[12], gid.DestGid.raw[13], gid.DestGid.raw[14],
			gid.DestGid.raw[15]); 
	}
	return 0;
}


int __cdecl main(int argc, char *argv[])
{
	if ( argc < 2 || !strcmp(argv[1],"/?") || !strcmp(argv[1],"-h") ) {
		print_usage(argv);
		return 1;
	}
	if ( !strcmp(argv[1], "print_ips") || !strcmp(argv[1], "ips") ) {
		return print_ips();
	}
	if ( !strcmp(argv[1], "remoteip") ||  !strcmp(argv[1], "rip") ) {
		int j;
		for(j=2; argv[j]; j++) {
			remote_ip(argv[j]);
		}
		return 0;
	}
	if ( !strcmp(argv[1], "localip") ||  !strcmp(argv[1], "lip") ) {
		return local_ip();
	}
	print_usage(argv);
	return 1;
}
