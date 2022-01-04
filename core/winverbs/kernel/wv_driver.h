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

#ifndef _WV_DRIVER_H_
#define _WV_DRIVER_H_

#include <ntddk.h>
#include <wdm.h>
#include <wdf.h>

#include <iba\ib_types.h>
#include <iba\ib_ci.h>
#include <iba\ib_al_ifc.h>
#include <iba\ib_cm_ifc.h>
#include <rdma\verbs.h>
#include "wv_ioctl.h"

#if WINVER <= _WIN32_WINNT_WINXP
#define KGUARDED_MUTEX			 FAST_MUTEX
#define KeInitializeGuardedMutex ExInitializeFastMutex
#define KeAcquireGuardedMutex	 ExAcquireFastMutex
#define KeReleaseGuardedMutex	 ExReleaseFastMutex
#endif

extern WDFDEVICE				ControlDevice;
extern INFINIBAND_INTERFACE_CM	IbCmInterface;
extern ULONG					RandomSeed;

typedef struct _WV_RDMA_DEVICE
{
	LIST_ENTRY				Entry;
	LONG					Ref;
	KEVENT					Event;
	ib_ca_handle_t			hDevice;
	RDMA_INTERFACE_VERBS	Interface;
	BOOLEAN					InterfaceTaken;
	INFINIBAND_INTERFACE_CM IbCmInterface;
	BOOLEAN					IbCmInterfaceTaken;

}	WV_RDMA_DEVICE;

WV_RDMA_DEVICE *WvRdmaDeviceGet(NET64 Guid);
void WvRdmaDevicePut(WV_RDMA_DEVICE *pDevice);

static inline void WvInitVerbsData(ci_umv_buf_t *pVerbsData, UINT32 Command,
								   SIZE_T InputLength, SIZE_T OutputLength,
								   void *pBuffer)
{
	pVerbsData->command = Command;
	pVerbsData->input_size = (UINT32) InputLength;
	pVerbsData->output_size = (UINT32) OutputLength;
	pVerbsData->p_inout_buf = (ULONG_PTR) pBuffer;
	pVerbsData->status = 0;
}

void WvCompleteRequests(WDFQUEUE Queue, NTSTATUS ReqStatus);
void WvFlushQueue(WDFQUEUE Queue, NTSTATUS ReqStatus);
void WvCompleteRequestsWithInformation(WDFQUEUE Queue, NTSTATUS ReqStatus);

#endif // _WV_DRIVER_H_
