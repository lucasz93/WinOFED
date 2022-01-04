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

#ifndef _WM_DRIVER_H_
#define _WM_DRIVER_H_

#include <ntddk.h>
#include <wdm.h>
#include <wdf.h>

#include <rdma\verbs.h>
#include <iba\ib_al_ifc.h>
#include "wm_ioctl.h"

#if WINVER <= _WIN32_WINNT_WINXP
#define KGUARDED_MUTEX FAST_MUTEX
#define KeInitializeGuardedMutex ExInitializeFastMutex
#define KeAcquireGuardedMutex ExAcquireFastMutex
#define KeReleaseGuardedMutex ExReleaseFastMutex
#endif



#if DBG
#define WmDbgPrintEx DbgPrintEx
#else
#define WmDbgPrintEx //
#endif


extern WDFDEVICE			ControlDevice;

typedef struct _WM_IB_PORT
{
	NET64					Guid;

}	WM_IB_PORT;

typedef struct _WM_IB_DEVICE
{
	LIST_ENTRY				Entry;
	LONG					Ref;
	KEVENT					Event;
	NET64					Guid;
	union
	{
		RDMA_INTERFACE_VERBS	VerbsInterface;
		ib_al_ifc_t				IbInterface;
	};
	ib_al_handle_t			hIbal;
	ib_pnp_handle_t			hPnp;
	//ib_ca_handle_t			hDevice;
	//ib_pd_handle_t			hPd;
	int						PortCount;
	WM_IB_PORT				*pPortArray;

}	WM_IB_DEVICE;

WM_IB_DEVICE *WmIbDeviceGet(NET64 Guid);
void WmIbDevicePut(WM_IB_DEVICE *pDevice);

#endif // _WM_DRIVER_H_
