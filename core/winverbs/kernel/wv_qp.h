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

#ifndef _WV_QP_H_
#define _WV_QP_H_

#include <ntddk.h>
#include <wdm.h>
#include <iba\ib_types.h>
#include <iba\ib_ci.h>

#include "wv_pd.h"
#include "wv_provider.h"
#include "wv_cq.h"
#include "wv_srq.h"

typedef struct _WV_QUEUE_PAIR
{
	WV_PROVIDER				*pProvider;
	WV_PROTECTION_DOMAIN	*pPd;
	WV_COMPLETION_QUEUE		*pSendCq;
	WV_COMPLETION_QUEUE		*pReceiveCq;
	WV_SHARED_RECEIVE_QUEUE	*pSrq;
	ci_interface_t			*pVerbs;
	ib_qp_handle_t			hVerbsQp;
	UINT32					Qpn;
	LIST_ENTRY				Entry;
	LIST_ENTRY				McList;

	KGUARDED_MUTEX			Lock;
	KEVENT					Event;
	LONG					Ref;

}	WV_QUEUE_PAIR;

void WvQpCreate(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvQpDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvQpFree(WV_QUEUE_PAIR *pQp);
void WvQpRemoveHandler(WV_QUEUE_PAIR *pQp);

WV_QUEUE_PAIR *WvQpAcquire(WV_PROVIDER *pProvider, UINT64 Id);
void WvQpRelease(WV_QUEUE_PAIR *pQp);

void WvQpModify(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvQpQuery(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvQpAttach(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvQpDetach(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvQpCancel(WV_PROVIDER *pProvider, WDFREQUEST Request);

#endif // _WV_QP_H_
