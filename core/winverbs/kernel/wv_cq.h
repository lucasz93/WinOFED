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

#ifndef _WV_CQ_H_
#define _WV_CQ_H_

#include <ntddk.h>
#include <wdm.h>
#include <iba\ib_types.h>
#include <iba\ib_ci.h>

#include "wv_device.h"
#include "wv_provider.h"

typedef struct _WV_COMPLETION_QUEUE
{
	WV_DEVICE			*pDevice;
	ci_interface_t		*pVerbs;
	ib_cq_handle_t		hVerbsCq;
	LIST_ENTRY			Entry;

	KEVENT				Event;
	LONG				Ref;
	WDFQUEUE			Queue;
	WDFQUEUE			ErrorQueue;

}	WV_COMPLETION_QUEUE;


void WvCqCreate(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvCqDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvCqFree(WV_COMPLETION_QUEUE *pCq);

WV_COMPLETION_QUEUE *WvCqAcquire(WV_PROVIDER *pProvider, UINT64 Id);
void WvCqRelease(WV_COMPLETION_QUEUE *pCq);
void WvCqGet(WV_COMPLETION_QUEUE *pCq);
void WvCqPut(WV_COMPLETION_QUEUE *pCq);

void WvCqResize(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvCqNotify(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvCqBatchNotify(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvCqCancel(WV_PROVIDER *pProvider, WDFREQUEST Request);

#endif //_WV_CQ_H_
