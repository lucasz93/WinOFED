/*
 * Copyright (c) 2004-2007 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2008 Intel Corporation.  All rights reserved.
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

#pragma once

#ifndef UMAD_H
#define UMAD_H

#include <windows.h>
#include <iba\winmad.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Interfaces based on libibumad 1.2.0
 */

typedef unsigned __int8		uint8_t;
typedef unsigned __int16	uint16_t;
typedef unsigned __int32	uint32_t;
typedef unsigned __int64	uint64_t;

#define UMAD_MAX_DEVICES	20
#define UMAD_ANY_PORT		0

// Allow casting to WM_UMAD_AV
typedef struct ib_mad_addr
{
	uint32_t		qpn;
	uint32_t		qkey;
	uint32_t		flow_label;
	uint16_t		pkey_index;
	uint8_t			hop_limit;
	uint8_t			gid_index;
	uint8_t			gid[16];

	uint8_t			traffic_class;
	uint8_t			reserved_grh;
	uint16_t		lid;
	uint8_t			sl;
	uint8_t			path_bits;
	uint8_t			reserved_rate;
	uint8_t			grh_present;

}	ib_mad_addr_t;

// Allow casting to WM_MAD
#pragma warning(push)
#pragma warning(disable: 4200)
typedef struct ib_user_mad
{
	uint32_t		agent_id;
	uint32_t		reserved_id;
	ib_mad_addr_t	addr;

	uint32_t		status;
	uint32_t		timeout_ms;
	uint32_t		retries;
	uint32_t		length;
	uint8_t			data[0];

}	ib_user_mad_t;
#pragma warning(pop)

#define UMAD_CA_NAME_LEN	64
#define UMAD_CA_MAX_PORTS	10	/* 0 - 9 */
#define UMAD_MAX_PORTS		64

typedef struct umad_port
{
	char			ca_name[UMAD_CA_NAME_LEN];
	int				portnum;
	unsigned		base_lid;
	unsigned		lmc;
	unsigned		sm_lid;
	unsigned		sm_sl;
	unsigned		state;
	unsigned		phys_state;
	unsigned		rate;
	uint32_t		capmask;
	uint64_t		gid_prefix;
	uint64_t		port_guid;
	unsigned		pkeys_size;
	uint16_t		*pkeys;
	char			link_layer[UMAD_CA_NAME_LEN];
	uint8_t			transport;
	uint8_t			ext_active_speed;
	uint8_t			link_encoding;

}	umad_port_t;

typedef struct umad_ca
{
	char			ca_name[UMAD_CA_NAME_LEN];
	unsigned		node_type;
	int				numports;
	char			fw_ver[20];
	char			ca_type[40];
	char			hw_ver[20];
	uint64_t		node_guid;
	uint64_t		system_guid;
	umad_port_t		*ports[UMAD_CA_MAX_PORTS];

}	umad_ca_t;

__declspec(dllexport)
int umad_init(void);
__declspec(dllexport)
int umad_done(void);

__declspec(dllexport)
int umad_get_cas_names(char cas[][UMAD_CA_NAME_LEN], int max);
__declspec(dllexport)
int umad_get_ca_portguids(char *ca_name, uint64_t *portguids, int max);

__declspec(dllexport)
int umad_get_ca(char *ca_name, umad_ca_t *ca);
__declspec(dllexport)
int umad_release_ca(umad_ca_t *ca);

__declspec(dllexport)
int umad_get_port(char *ca_name, int portnum, umad_port_t *port);
__declspec(dllexport)
int umad_release_port(umad_port_t *port);

__declspec(dllexport)
int umad_get_issm_path(char *ca_name, int portnum, char path[], int max);

__declspec(dllexport)
int umad_open_port(char *ca_name, int portnum);
__declspec(dllexport)
int umad_close_port(int portid);

__declspec(dllexport)
void *umad_get_mad(void *umad);

__declspec(dllexport)
size_t umad_size(void);

__declspec(dllexport)
int umad_status(void *umad);

__declspec(dllexport)
ib_mad_addr_t *umad_get_mad_addr(void *umad);
__declspec(dllexport)
int umad_set_grh_net(void *umad, void *mad_addr);
__declspec(dllexport)
int umad_set_grh(void *umad, void *mad_addr);
__declspec(dllexport)
int umad_set_addr_net(void *umad, int dlid, int dqp, int sl, int qkey);
__declspec(dllexport)
int umad_set_addr(void *umad, int dlid, int dqp, int sl, int qkey);
__declspec(dllexport)
int umad_set_pkey(void *umad, int pkey_index);
__declspec(dllexport)
int umad_get_pkey(void *umad);

__declspec(dllexport)
int umad_send(int portid, int agentid, void *umad, int length,
		  int timeout_ms, int retries);
__declspec(dllexport)
int umad_recv(int portid, void *umad, int *length, int timeout_ms);
__declspec(dllexport)
int umad_poll(int portid, int timeout_ms);
HANDLE umad_get_fd(int portid);

__declspec(dllexport)
int umad_register(int portid, int mgmt_class, int mgmt_version,
				  uint8_t rmpp_version, long method_mask[16/sizeof(long)]);
__declspec(dllexport)
int umad_register_oui(int portid, int mgmt_class, uint8_t rmpp_version,
					  uint8_t oui[3], long method_mask[16/sizeof(long)]);
__declspec(dllexport)
int umad_unregister(int portid, int agentid);


__declspec(dllexport)
int umad_debug(int level);
__declspec(dllexport)
void umad_addr_dump(ib_mad_addr_t *addr);
__declspec(dllexport)
void umad_dump(void *umad);

__declspec(dllexport)
void *umad_alloc(int num, size_t size);

__declspec(dllexport)
void umad_free(void *umad);

#ifdef __cplusplus
}
#endif

#endif /* UMAD_H */
