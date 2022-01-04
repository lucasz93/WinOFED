/*
 * Copyright (c) 2008 Lawrence Livermore National Laboratory
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

#include <infiniband/verbs.h>

__declspec(dllexport)
const char *ibv_node_type_str(enum ibv_node_type node_type)
{
	static const char *const node_type_str[] = {
		"unknown",
		"InfiniBand channel adapter",
		"InfiniBand switch",
		"InfiniBand router",
		"iWARP NIC"
	};

	if (node_type < IBV_NODE_CA || node_type > IBV_NODE_RNIC)
		return "unknown";

	return node_type_str[node_type];
}

__declspec(dllexport)
const char *ibv_port_state_str(enum ibv_port_state port_state)
{
	static const char *const port_state_str[] = {
		"no state change (NOP)",
		"PORT_DOWN",
		"PORT_INIT",
		"PORT_ARMED",
		"PORT_ACTIVE",
		"PORt_ACTIVE_DEFER"
	};

	if (port_state < IBV_PORT_NOP || port_state > IBV_PORT_ACTIVE_DEFER)
		return "unknown";

	return port_state_str[port_state];
}

__declspec(dllexport)
const char *ibv_event_type_str(enum ibv_event_type event)
{
	static const char *const event_type_str[] = {
		"CQ error",
		"local work queue catastrophic error",
		"invalid request local work queue error",
		"local access violation work queue error",
		"communication established",
		"send queue drained",
		"path migrated",
		"path migration request error",
		"local catastrophic error",
		"port active",
		"port error",
		"LID change",
		"P_Key change",
		"SM change",
		"SRQ catastrophic error",
		"SRQ limit reached",
		"last WQE reached",
		"client reregistration",
	};

	if (event < IBV_EVENT_CQ_ERR || event > IBV_EVENT_CLIENT_REREGISTER)
		return "unknown";

	return event_type_str[event];
}

__declspec(dllexport)
const char *ibv_wc_status_str(enum ibv_wc_status status)
{
	static const char *const wc_status_str[] = {
		"success",
		"local length error",
		"local QP operation error",
		"local protection error",
		"Work Request Flushed Error",
		"memory management operation error",
		"remote access error",
		"remote operation error",
		"RNR retry counter exceeded",
		"response timeout error",
		"remote invalid request error",
		"bad response error",
		"local access error",
		"general error",
		"fatal error",
		"transport retry counter exceeded",
		"aborted error",
		"local EE context operation error",
		"local RDD violation error",
		"remote invalid RD request",
		"invalid EE context number",
		"invalid EE context state"
	};

	if (status < IBV_WC_SUCCESS || status > IBV_WC_GENERAL_ERR)
		return "unknown";

	return wc_status_str[status];
}
