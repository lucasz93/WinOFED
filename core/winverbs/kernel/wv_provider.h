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

#ifndef _WV_PROVIDER_H_
#define _WV_PROVIDER_H_

#include <ntddk.h>
#include <wdf.h>
#include <wdm.h>

#include "work_queue.h"
#include <complib\cl_qmap.h>
#include "wv_driver.h"
#include "index_list.h"

struct _WV_DEVICE;
struct _WV_PROTECTION_DOMAIN;

typedef struct _WV_WORK_ENTRY
{
	WORK_ENTRY		Work;
	UINT64			Id;

}	WV_WORK_ENTRY;

static void WvWorkEntryInit(WV_WORK_ENTRY *pWork, UINT64 Id,
							void (*WorkHandler)(WORK_ENTRY *Work), void *Context)
{
	pWork->Id = Id;
	WorkEntryInit(&pWork->Work, WorkHandler, Context);
}

typedef struct _WV_PROVIDER
{
	LIST_ENTRY		Entry;
	INDEX_LIST		DevIndex;
	INDEX_LIST		CqIndex;
	INDEX_LIST		PdIndex;
	INDEX_LIST		SrqIndex;
	INDEX_LIST		QpIndex;
	INDEX_LIST		MwIndex;
	INDEX_LIST		AhIndex;
	INDEX_LIST		EpIndex;

	KGUARDED_MUTEX	Lock;
	LONG			Ref;
	KEVENT			Event;
	LONG			Pending;
	LONG			Active;
	KEVENT			SharedEvent;
	LONG			Exclusive;
	KEVENT			ExclusiveEvent;

	WORK_QUEUE		WorkQueue;
	KSPIN_LOCK		SpinLock;

}	WV_PROVIDER;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WV_PROVIDER, WvProviderGetContext)

LONG WvProviderGet(WV_PROVIDER *pProvider);
void WvProviderPut(WV_PROVIDER *pProvider);
NTSTATUS WvProviderInit(WDFDEVICE Device, WV_PROVIDER *pProvider);
void WvProviderCleanup(WV_PROVIDER *pProvider);

void WvProviderRemoveHandler(WV_PROVIDER *pProvider, WV_RDMA_DEVICE *pDevice);
void WvProviderDisableRemove(WV_PROVIDER *pProvider);
void WvProviderEnableRemove(WV_PROVIDER *pProvider);

#endif // _WV_PROVIDER_H_
