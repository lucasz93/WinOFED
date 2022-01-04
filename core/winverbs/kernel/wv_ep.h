/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
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

#pragma once

#ifndef _WV_EP_H_
#define _WV_EP_H_

#include <ntddk.h>
#include <wdm.h>

#include "work_queue.h"
#include <iba\ib_types.h>
#include <iba\ib_ci.h>
#include "wv_provider.h"
#include "wv_ioctl.h"

typedef enum _WV_ENDPOINT_STATE
{
	WvEpIdle,
	WvEpAddressBound,
	WvEpListening,
	WvEpQueued,
	WvEpRouteResolved,
	WvEpPassiveConnect,
	WvEpActiveConnect,
	WvEpConnected,
	WvEpActiveDisconnect,
	WvEpPassiveDisconnect,
	WvEpDisconnected,
	WvEpDestroying

}	WV_ENDPOINT_STATE;

typedef struct _WV_ENDPOINT
{
	WV_PROVIDER			*pProvider;
	LIST_ENTRY			Entry;
	WV_ENDPOINT_STATE	State;

	iba_cm_id			*pIbCmId;
	WV_IO_EP_ATTRIBUTES	Attributes;
	ib_path_rec_t		Route;

	UINT16				EpType;
	KEVENT				Event;
	LONG				Ref;
	WDFQUEUE			Queue;
	WV_WORK_ENTRY		*pWork;

}	WV_ENDPOINT;

void WvEpCreate(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpFree(WV_ENDPOINT *pEndpoint);
void WvEpCancelListen(WV_ENDPOINT *pEndpoint);

void WvEpQuery(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpModify(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpBind(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpCancel(WV_PROVIDER *pProvider, WDFREQUEST Request);

void WvEpConnect(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpAccept(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpReject(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpDisconnect(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpDisconnectNotify(WV_PROVIDER *pProvider, WDFREQUEST Request);

void WvEpListen(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpGetRequest(WV_PROVIDER *pProvider, WDFREQUEST Request);

void WvEpLookup(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpMulticastJoin(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvEpMulticastLeave(WV_PROVIDER *pProvider, WDFREQUEST Request);

#endif // _WV_EP_H_
