/*
 * Copyright (c) 2004-2007 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2008 Intel Corp., Inc.  All rights reserved.
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

#include <windows.h>
#include <stdio.h>

#include <infiniband/umad.h>
#include <infiniband/verbs.h>
#include <rdma/wvstatus.h>
#include <_errno.h>
#include "ibumad.h"

#ifdef _PREFAST_
#define CONDITION_ASSUMED(X) __analysis_assume((X))
#else
#define CONDITION_ASSUMED(X) 
#endif // _PREFAST_

#ifndef CL_ASSERT
#ifdef DBG
#define CL_ASSERT( exp )	(void)(!(exp)?OutputDebugString("Assertion Failed:" #exp "\n"),DebugBreak(),FALSE:TRUE);CONDITION_ASSUMED(exp)
#else
#define CL_ASSERT( exp )
#endif	/* _DEBUG_ */
#endif	/* CL_ASSERT */


#define IB_OPENIB_OUI                 (0x001405)

#define UMAD_MAX_PKEYS	16

typedef struct um_port
{
	IWMProvider *prov;
	NET64		dev_guid;
	OVERLAPPED	overlap;
	UINT8		port_num;

}	um_port_t;

CRITICAL_SECTION crit_sec;
um_port_t ports[UMAD_MAX_PORTS];


__declspec(dllexport)
int umad_init(void)
{
	InitializeCriticalSection(&crit_sec);
	return 0;
}

__declspec(dllexport)
int umad_done(void)
{
	DeleteCriticalSection(&crit_sec);
	return 0;
}

__declspec(dllexport)
int umad_get_cas_names(char cas[][UMAD_CA_NAME_LEN], int max)
{
	struct ibv_device	**list;
	int					cnt, i;

	list = ibv_get_device_list(&cnt);
	if (list == NULL) {
		return 0;
	}

	for (i = 0; i < min(cnt, max); i++) {
		strcpy(cas[i], ibv_get_device_name(list[i]));
	}

	ibv_free_device_list(list);
	return i;
}

__declspec(dllexport)
int umad_get_ca_portguids(char *ca_name, uint64_t *portguids, int max)
{
	umad_ca_t	ca;
	int			ports = 0, i;

	if (umad_get_ca(ca_name, &ca) < 0)
		return -1;

	if (ca.numports + 1 > max) {
		umad_release_ca(&ca);
		return -ENOMEM;
	}

	portguids[ports++] = 0;
	for (i = 1; i <= ca.numports; i++)
		portguids[ports++] = ca.ports[i]->port_guid;

	umad_release_ca(&ca);
	return ports;
}

static void umad_convert_ca_attr(umad_ca_t *ca, ibv_device_attr *attr)
{
	ca->node_type = 1;	// HCA
	ca->numports = attr->phys_port_cnt;
	strncpy(ca->fw_ver, attr->fw_ver, 20);
	memset(ca->ca_type, 0, 40);		// TODO: determine what this should be
	sprintf(ca->hw_ver, "0x%x", attr->hw_ver);
	ca->node_guid = attr->node_guid;
	ca->system_guid = attr->sys_image_guid;
}

static int umad_query_port(struct ibv_context *context, umad_port_t *port)
{
	ibv_port_attr	attr;
	ibv_gid			gid;
	int				i, ret;

	ret = ibv_query_port(context, (uint8_t) port->portnum, &attr);
	if (ret != 0) {
		return ret;
	}

	port->base_lid = attr.lid;
	port->lmc = attr.lmc;
	port->sm_lid = attr.sm_lid;
	port->sm_sl = attr.sm_sl;
	port->state = attr.state;
	port->phys_state = attr.phys_state;
	port->capmask = attr.port_cap_flags;
	port->transport = attr.transport;
	port->ext_active_speed = attr.ext_active_speed;
	port->link_encoding = attr.link_encoding;
	

	if ( !attr.ext_active_speed )
	{
		switch (attr.active_width){
			case 1:
				port->rate = (unsigned) (2.5*attr.active_speed); 
				break;
			case 2:
				port->rate = 10*attr.active_speed; //2.5*4
				break;
			case 4:
				port->rate = 20*attr.active_speed; //2.5*8
				break;
			case 8:
				port->rate = 30*attr.active_speed; //2.5*12
				break;
			default:
				port->rate = 0;	
		}
	}
	else
	{
		unsigned speed = 0;
		switch (attr.ext_active_speed){
			case 1: 
				speed = 14;
				break;
			case 2: 
				speed = 26;
				break;
		}
			
		switch (attr.active_width){
			case 1:
				port->rate = 1*speed; 
				break;
			case 2:
				port->rate = 4*speed; 
				break;
			case 4:
				port->rate = 8*speed;
				break;
			case 8:
				port->rate = 12*speed; 
				break;
			default:
				port->rate = 0;	
		}
	}

	// Assume GID 0 contains port GUID and gid prefix
	ret = ibv_query_gid(context, (uint8_t) port->portnum, 0, &gid);
	if (ret != 0) {
		return ret;
	}

	port->gid_prefix = gid.global.subnet_prefix;
	port->port_guid = gid.global.interface_id;

	port->pkeys_size = min(UMAD_MAX_PKEYS, attr.pkey_tbl_len);
	for (i = 0; i < (int) port->pkeys_size; i++) {
		ret = ibv_query_pkey(context,(uint8_t)  port->portnum, i, &port->pkeys[i]);
		if (ret != 0) {
			return ret;
		}
	}

	sprintf(port->link_layer, "IB");

	return 0;
}

__declspec(dllexport)
int umad_get_ca(char *ca_name, umad_ca_t *ca)
{
	struct ibv_device	**list;
	struct ibv_context	*context;
	ibv_device_attr		dev_attr;
	int					cnt, i, ret = 0;
	uint8_t				*ports;
	size_t				port_size;

	list = ibv_get_device_list(&cnt);
	if (list == NULL) {
		return -ENOMEM;
	}

	for (i = 0; i < cnt; i++) {
		if (!strcmp(ca_name, ibv_get_device_name(list[i]))) {
			break;
		}
	}

	if (i == cnt) {
		ret = -EINVAL;
		goto free;
	}

	context = ibv_open_device(list[i]);
	if (context == NULL) {
		ret = -ENOMEM;
		goto free;
	}

	ret = ibv_query_device(context, &dev_attr);
	if (ret != 0) {
		goto close;
	}

	port_size = sizeof(umad_port_t) + sizeof(uint16_t) * UMAD_MAX_PKEYS;
	ports = new uint8_t[port_size * dev_attr.phys_port_cnt];
	if (ports == NULL) {
		ret = -ENOMEM;
		goto close;
	}

	strcpy(ca->ca_name, ca_name);
	umad_convert_ca_attr(ca, &dev_attr);
	memset(ca->ports, 0, sizeof(ca->ports));

	for (i = 1; i <= dev_attr.phys_port_cnt; i++, ports += port_size) {

		ca->ports[i] = (umad_port_t *) ports;
		strcpy(ca->ports[i]->ca_name, ca_name);
		ca->ports[i]->portnum = i;
		ca->ports[i]->pkeys = (uint16_t *) (ports + sizeof(umad_port_t));

		ret = umad_query_port(context, ca->ports[i]);
		if (ret != 0) {
			delete ports;
			goto close;
		}
	}

close:
	ibv_close_device(context);
free:
	ibv_free_device_list(list);
	return ret;
}

__declspec(dllexport)
int umad_release_ca(umad_ca_t *ca)
{
	delete ca->ports[0];
	return 0;
}

static uint64_t umad_get_ca_guid(char *ca_name)
{
	umad_ca_t	ca;
	uint64_t	guid;
	int			ret;

	ret = umad_get_ca(ca_name, &ca);
	if (ret != 0) {
		return 0;
	}

	guid = ca.node_guid;
	umad_release_ca(&ca);
	return guid;
}

__declspec(dllexport)
int umad_get_port(char *ca_name, int portnum, umad_port_t *port)
{
	umad_ca_t	ca;
	int			ret;

	ret = umad_get_ca(ca_name, &ca);
	if (ret != 0) {
		return ret;
	}

	memcpy(port, ca.ports[portnum], sizeof(umad_port_t));

	port->pkeys = new uint16_t[ca.ports[portnum]->pkeys_size];
	if (port->pkeys == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(port->pkeys, ca.ports[portnum]->pkeys,
		   sizeof(uint16_t) * ca.ports[portnum]->pkeys_size);
out:
	umad_release_ca(&ca);
	return ret;
}

__declspec(dllexport)
int umad_release_port(umad_port_t *port)
{
	delete port->pkeys;
	return 0;
}

__declspec(dllexport)
int umad_get_issm_path(char *ca_name, int portnum, char path[], int max)
{
	return -EINVAL;
}

static uint8_t umad_find_port(char *ca_name, enum ibv_port_state state)
{
	umad_ca_t	ca;
	int			i, ret;

	ret = umad_get_ca(ca_name, &ca);
	if (ret != 0) {
		return 0;
	}

	for (i = 1; i <= ca.numports; i++) {
		if (ca.ports[i]->state == state) {
			i = ca.ports[i]->portnum;
			umad_release_ca(&ca);
			return (uint8_t) i;
		}
	}

	umad_release_ca(&ca);
	return 0;
}

static int umad_find_ca(enum ibv_port_state state, char *ca_name, uint8_t port)
{
	char		names[8][UMAD_CA_NAME_LEN];
	umad_ca_t	ca;
	int			cnt, i, ret;

	cnt = umad_get_cas_names(names, 8);

	for (i = 0; i < cnt; i++) {
		ret = umad_get_ca(names[i], &ca);
		if (ret != 0) {
			return -1;
		}

		if ((port <= ca.numports) && (ca.ports[port]->state == state)) {
			strcpy(ca_name, names[i]);
			umad_release_ca(&ca);
			return 0;
		}

		umad_release_ca(&ca);
	}

	return -1;
}

static int umad_find_ca_port(enum ibv_port_state state, char *ca_name, uint8_t *port)
{
	char		names[8][UMAD_CA_NAME_LEN];
	int			cnt, i;

	cnt = umad_get_cas_names(names, 8);

	for (i = 0; i < cnt; i++) {
		*port = umad_find_port(names[i], state);
		if (*port != 0) {
			strcpy(ca_name, names[i]);
			return 0;
		}
	}
	return -1;
}

static int umad_resolve_ca_port(char *ca_name, uint8_t *port)
{
	int ret;

	if (ca_name[0] != NULL) {
		if (*port != 0) {
			return 0;
		}

		*port = umad_find_port(ca_name, IBV_PORT_ACTIVE);
		if (*port != 0) {
			return 0;
		}
		*port = umad_find_port(ca_name, IBV_PORT_INIT);
		if (*port != 0) {
			return 0;
		}
		*port = umad_find_port(ca_name, IBV_PORT_DOWN);
		return (*port == 0);
	}

	if (*port != 0) {
		ret = umad_find_ca(IBV_PORT_ACTIVE, ca_name, *port);
		if (ret == 0) {
			return 0;
		}
		ret = umad_find_ca(IBV_PORT_INIT, ca_name, *port);
		if (ret == 0) {
			return 0;
		}
		return umad_find_ca(IBV_PORT_DOWN, ca_name, *port);
	}

	ret = umad_find_ca_port(IBV_PORT_ACTIVE, ca_name, port);
	if (ret == 0) {
		return 0;
	}
	ret = umad_find_ca_port(IBV_PORT_INIT, ca_name, port);
	if (ret == 0) {
		return 0;
	}
	return umad_find_ca_port(IBV_PORT_DOWN, ca_name, port);
}

__declspec(dllexport)
int umad_open_port(char *ca_name, int portnum)
{
	char	name[UMAD_CA_NAME_LEN];
	uint8_t	port = (uint8_t) portnum;
	HRESULT	hr;
	int		portid;

	if (ca_name != NULL) {
		strcpy(name, ca_name);
	} else {
		name[0] = NULL;
	}

	hr = umad_resolve_ca_port(name, &port);
	if (FAILED(hr)) {
		return hr;
	}

	EnterCriticalSection(&crit_sec);
	for (portid = 0; portid < UMAD_MAX_PORTS; portid++) {
		if (ports[portid].prov == NULL) {
			break;
		}
	}

	if (portid == UMAD_MAX_PORTS) {
		portid = -ENOMEM;
		goto out;
	}

	ports[portid].overlap.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (ports[portid].overlap.hEvent == NULL) {
		portid = -ENOMEM;
		goto out;
	}

	hr = WmGetObject(IID_IWMProvider, (LPVOID*) &ports[portid].prov);
	if (FAILED(hr)) {
		CloseHandle(ports[portid].overlap.hEvent);
		portid = GetLastError() | 0x80000000;
		goto out;
	}
	CL_ASSERT(ports[portid].prov);

	ports[portid].dev_guid = umad_get_ca_guid(name);
	ports[portid].port_num = port;

out:
	LeaveCriticalSection(&crit_sec);
	return portid;
}

__declspec(dllexport)
int umad_close_port(int portid)
{
	CloseHandle(ports[portid].overlap.hEvent);
	ports[portid].prov->Release();

	EnterCriticalSection(&crit_sec);
	ports[portid].prov = NULL;
	LeaveCriticalSection(&crit_sec);

	return 0;
}

__declspec(dllexport)
int umad_set_grh_net(void *umad, void *mad_addr)
{
	struct ib_user_mad *mad = (struct ib_user_mad *) umad;
	struct ib_mad_addr *addr = (struct ib_mad_addr *) mad_addr;

	if (mad_addr) {
		mad->addr.grh_present = 1;
		memcpy(mad->addr.gid, addr->gid, 16);
		mad->addr.flow_label = addr->flow_label;
		mad->addr.hop_limit = addr->hop_limit;
		mad->addr.traffic_class = addr->traffic_class;
	} else
		mad->addr.grh_present = 0;
	return 0;
}

__declspec(dllexport)
int umad_set_grh(void *umad, void *mad_addr)
{
	struct ib_user_mad *mad = (struct ib_user_mad *) umad;
	struct ib_mad_addr *addr = (struct ib_mad_addr *) mad_addr;

	if (mad_addr) {
		mad->addr.grh_present = 1;
		memcpy(mad->addr.gid, addr->gid, 16);
		mad->addr.flow_label = htonl(addr->flow_label);
		mad->addr.hop_limit = addr->hop_limit;
		mad->addr.traffic_class = addr->traffic_class;
	} else
		mad->addr.grh_present = 0;
	return 0;
}

__declspec(dllexport)
int umad_set_pkey(void *umad, int pkey_index)
{
	struct ib_user_mad *mad = (struct ib_user_mad *) umad;

	mad->addr.pkey_index = (uint16_t) pkey_index;
	return 0;
}

__declspec(dllexport)
int umad_get_pkey(void *umad)
{
	struct ib_user_mad *mad = (struct ib_user_mad *) umad;

	return mad->addr.pkey_index;
}

__declspec(dllexport)
int umad_set_addr(void *umad, int dlid, int dqp, int sl, int qkey)
{
	struct ib_user_mad *mad = (struct ib_user_mad *) umad;

	mad->addr.qpn = htonl(dqp);
	mad->addr.lid = htons((uint16_t) dlid);
	mad->addr.qkey = htonl(qkey);
	mad->addr.sl = (uint8_t) sl;
	return 0;
}

__declspec(dllexport)
int umad_set_addr_net(void *umad, int dlid, int dqp, int sl, int qkey)
{
	struct ib_user_mad *mad = (struct ib_user_mad *) umad;

	mad->addr.qpn = dqp;
	mad->addr.lid = (uint16_t) dlid;
	mad->addr.qkey = qkey;
	mad->addr.sl = (uint8_t) sl;

	return 0;
}

__declspec(dllexport)
int umad_status(void *umad)
{
	return ((struct ib_user_mad *) umad)->status;
}

__declspec(dllexport)
ib_mad_addr_t *umad_get_mad_addr(void *umad)
{
	return &((struct ib_user_mad *) umad)->addr;
}

__declspec(dllexport)
void *umad_get_mad(void *umad)
{
	return ((struct ib_user_mad *)umad)->data;
}

__declspec(dllexport)
void *umad_alloc(int num, size_t size)
{
	return calloc(num, size);
}

__declspec(dllexport)
void umad_free(void *umad)
{
	free(umad);
}

__declspec(dllexport)
size_t umad_size(void)
{
	return sizeof(struct ib_user_mad);
}

static void umad_convert_av(WM_MAD_AV *av, struct ib_mad_addr *addr)
{
	uint32_t ver_class_flow;

	ver_class_flow = ntohl(av->VersionClassFlow);
	addr->flow_label = ver_class_flow & 0x000FFFFF;
	addr->traffic_class = (uint8_t) (ver_class_flow >> 20);
}

__declspec(dllexport)
int umad_send(int portid, int agentid, void *umad, int length,
			  int timeout_ms, int retries)
{
	struct ib_user_mad *mad = (struct ib_user_mad *) umad;
	HRESULT hr;

	mad->agent_id = agentid;
	mad->reserved_id = 0;

	mad->timeout_ms = (uint32_t) timeout_ms;
	mad->retries	= (uint32_t) retries;
	mad->length		= (uint32_t) length;

	mad->addr.reserved_grh  = 0;
	mad->addr.reserved_rate = 0;

	hr = ports[portid].prov->Send((WM_MAD *) mad, NULL);
	if (FAILED(hr)) {
		_set_errno(EIO);
		return GetLastError();
	}

	return 0;
}

static HRESULT umad_cancel_recv(um_port_t *port)
{
	DWORD bytes;

	port->prov->CancelOverlappedRequests();
	return port->prov->GetOverlappedResult(&port->overlap, &bytes, TRUE);
}

__declspec(dllexport)
int umad_recv(int portid, void *umad, int *length, int timeout_ms)
{
	WM_MAD		*mad = (WM_MAD *) umad;
	um_port_t	*port;
	HRESULT		hr;

	port = &ports[portid];
	ResetEvent(port->overlap.hEvent);
	hr = port->prov->Receive(mad, sizeof(WM_MAD) + (size_t) *length, &port->overlap);
	if (hr == WV_IO_PENDING) {
		hr = WaitForSingleObject(port->overlap.hEvent, (DWORD) timeout_ms);
		if (hr == WAIT_TIMEOUT) {
			hr = umad_cancel_recv(port);
			if (hr == WV_CANCELLED) {
				_set_errno(EWOULDBLOCK);
				return -EWOULDBLOCK;
			}
		}
	}

	if (FAILED(hr)) {
		_set_errno(EIO);
		return -EIO;
	}

	if (mad->Length <= (UINT32) *length) {
		hr = (HRESULT) mad->Id;
		umad_convert_av(&mad->Address, &((struct ib_user_mad *) mad)->addr);
	} else {
		_set_errno(ENOSPC);
		hr = -ENOSPC;
	}

	*length = mad->Length;
	return hr;
}

__declspec(dllexport)
int umad_poll(int portid, int timeout_ms)
{
	WM_MAD		mad;
	um_port_t	*port;
	HRESULT		hr;

	port = &ports[portid];
	ResetEvent(port->overlap.hEvent);
	hr = port->prov->Receive(&mad, sizeof mad, &port->overlap);
	if (hr == WV_IO_PENDING) {
		hr = WaitForSingleObject(port->overlap.hEvent, (DWORD) timeout_ms);
		if (hr == WAIT_TIMEOUT) {
			hr = umad_cancel_recv(port);
			if (hr == WV_CANCELLED) {
				_set_errno(ETIMEDOUT);
				return -ETIMEDOUT;
			}
		}
	}

	if (FAILED(hr) && hr != ERROR_MORE_DATA) {
		_set_errno(EIO);
		return -EIO;
	}

	return 0;
}

static int umad_reg_oui(int portid, int mgmt_class, int mgmt_version,
						uint8_t rmpp_version, uint8_t oui[3],
						long method_mask[16/sizeof(long)])
{
	WM_REGISTER reg;
	UINT64		id = 0;

	UNREFERENCED_PARAMETER(rmpp_version);

	reg.Guid = ports[portid].dev_guid;
	reg.Qpn = (mgmt_class == 0x01 || mgmt_class == 0x81) ? 0 : htonl(1);
	reg.Port = ports[portid].port_num;
	reg.Class = (uint8_t) mgmt_class;
	reg.Version = (uint8_t) mgmt_version;
	memset(reg.Reserved, 0, sizeof(reg.Reserved));
	memcpy(reg.Oui, oui, sizeof(oui));
	if (method_mask != NULL) {
		memcpy(reg.Methods, method_mask, sizeof(reg.Methods));
	} else {
		memset(reg.Methods, 0, sizeof(reg.Methods));
	}
	ports[portid].prov->Register(&reg, &id);

	return (int) id;
}

__declspec(dllexport)
int umad_register_oui(int portid, int mgmt_class, uint8_t rmpp_version,
					  uint8_t oui[3], long method_mask[16/sizeof(long)])
{
	return umad_reg_oui(portid, mgmt_class, 1,
						rmpp_version, oui, method_mask);
}

__declspec(dllexport)
int umad_register(int portid, int mgmt_class, int mgmt_version,
				  uint8_t rmpp_version, long method_mask[16/sizeof(long)])
{
	uint8_t oui[3];

	memset(oui, 0, 3);
	return umad_reg_oui(portid, mgmt_class, mgmt_version,
						rmpp_version, oui, method_mask);
}

__declspec(dllexport)
int umad_unregister(int portid, int agentid)
{
	return ports[portid].prov->Deregister((UINT64) agentid);
}

HANDLE umad_get_fd(int portid)
{
	return ports[portid].prov->GetFileHandle();
}

__declspec(dllexport)
int umad_debug(int level)
{
	UNREFERENCED_PARAMETER(level);
	return 0;
}

__declspec(dllexport)
void umad_addr_dump(ib_mad_addr_t *addr)
{
	printf("umad_addr:\n");
	printf("\tqpn           0x%x\n", addr->qpn);
	printf("\tqkey          0x%x\n", addr->qkey);
	printf("\tsl            0x%x\n", addr->sl);
	printf("\tlid           0x%x\n", addr->lid);
	printf("\tpkey_index    0x%x\n", addr->pkey_index);
	printf("\tpath_bits     0x%x\n", addr->path_bits);
	printf("\trate          0x%x\n", addr->reserved_rate);

	printf("\tgrh_present   0x%x\n", addr->grh_present);
	if (addr->grh_present) {
		printf("\tgid_index     0x%x\n", addr->gid_index);
		printf("\tgid           0x%x %x\n",
			(uint64_t) addr->gid, (uint64_t) (addr->gid + 8));
		printf("\tflow_lable    0x%x\n", addr->flow_label);
		printf("\thop_limit     0x%x\n", addr->hop_limit);
		printf("\ttraffic_class 0x%x\n", addr->qpn);
	}
}

__declspec(dllexport)
void umad_dump(void *umad)
{
	struct ib_user_mad *mad = (struct ib_user_mad *) umad;
	int i;

	umad_addr_dump(&mad->addr);
	printf("umad_data\n");
	printf("offset: hex data\n");
	for (i = 0; i < 256; i += 4) {
		printf("%03d: ", i);
		printf("%02x %02x %02x %02x\n", mad->data[i], mad->data[i + 1],
			mad->data[i + 2], mad->data[i + 3]);
	}
}
