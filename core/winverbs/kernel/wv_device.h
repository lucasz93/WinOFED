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

#ifndef _WV_DEVICE_H_
#define _WV_DEVICE_H_

#include <ntddk.h>
#include <wdm.h>
#include <iba\ib_types.h>
#include <iba\ib_ci.h>

#include "wv_driver.h"
#include "wv_provider.h"

typedef struct _WV_PORT
{
	WDFQUEUE			Queue;
	UINT32				Flags;

}	WV_PORT;

typedef struct _WV_DEVICE
{
	WV_PROVIDER			*pProvider;
	WV_RDMA_DEVICE		*pDevice;
	ci_interface_t		*pVerbs;
	LIST_ENTRY			Entry;
	ib_ca_handle_t		hVerbsDevice;
	ci_event_handler_t	EventHandler;

	LIST_ENTRY			PdList;
	LIST_ENTRY			CqList;

	KEVENT				Event;
	LONG				Ref;
	WV_PORT				*pPorts;
	UINT8				PortCount;

}	WV_DEVICE;

struct _WV_DEVICE *WvDeviceAcquire(WV_PROVIDER *pProvider, UINT64 Id);
void WvDeviceRelease(WV_DEVICE *pDevice);
void WvDeviceGet(WV_DEVICE *pDevice);
void WvDevicePut(WV_DEVICE *pDevice);

void WvDeviceOpen(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvDeviceClose(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvDeviceFree(WV_DEVICE *pDevice);
void WvDeviceRemoveHandler(WV_DEVICE *pDevice);

void WvDeviceQuery(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvDevicePortQuery(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvDeviceGidQuery(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvDevicePkeyQuery(WV_PROVIDER *pProvider, WDFREQUEST Request);

void WvDeviceNotify(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvDeviceCancel(WV_PROVIDER *pProvider, WDFREQUEST Request);

#endif // __WV_DEVICE_H_
