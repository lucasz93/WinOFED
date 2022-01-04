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

#ifndef _WV_SRQ_H_
#define _WV_SRQ_H_

#include <ntddk.h>
#include <wdm.h>
#include <iba\ib_types.h>
#include <iba\ib_ci.h>

#include "wv_pd.h"
#include "wv_provider.h"

typedef struct _WV_SHARED_RECEIVE_QUEUE
{
	WV_PROVIDER				*pProvider;
	WV_PROTECTION_DOMAIN	*pPd;
	ci_interface_t			*pVerbs;
	ib_srq_handle_t			hVerbsSrq;
	LIST_ENTRY				Entry;

	KEVENT					Event;
	LONG					Ref;
	WDFQUEUE				Queue;

}	WV_SHARED_RECEIVE_QUEUE;

void WvSrqCreate(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvSrqDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvSrqFree(WV_SHARED_RECEIVE_QUEUE *pSrq);

WV_SHARED_RECEIVE_QUEUE *WvSrqAcquire(WV_PROVIDER *pProvider, UINT64 Id);
void WvSrqRelease(WV_SHARED_RECEIVE_QUEUE *pSrq);
void WvSrqGet(WV_SHARED_RECEIVE_QUEUE *pSrq);
void WvSrqPut(WV_SHARED_RECEIVE_QUEUE *pSrq);

void WvSrqModify(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvSrqQuery(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvSrqNotify(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvSrqCancel(WV_PROVIDER *pProvider, WDFREQUEST Request);

#endif // _WV_SRQ_H_
