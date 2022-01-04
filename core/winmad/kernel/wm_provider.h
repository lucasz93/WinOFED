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

#ifndef _WM_PROVIDER_H_
#define _WM_PROVIDER_H_

#include <ntddk.h>
#include <wdf.h>
#include <wdm.h>

#include <iba\ib_al.h>
#include "wm_driver.h"
#include "index_list.h"

typedef struct _WM_PROVIDER
{
	LIST_ENTRY			Entry;
	INDEX_LIST			RegIndex;
	WDFQUEUE			ReadQueue;

	ib_mad_element_t	*MadHead;
	ib_mad_element_t	*MadTail;

	KGUARDED_MUTEX		Lock;
	LONG				Ref;
	KEVENT				Event;
	LONG				Pending;
	LONG				Active;
	KEVENT				SharedEvent;
	LONG				Exclusive;
	KEVENT				ExclusiveEvent;
	KSPIN_LOCK			SpinLock;

}	WM_PROVIDER;

LONG WmProviderGet(WM_PROVIDER *pProvider);
void WmProviderPut(WM_PROVIDER *pProvider);
NTSTATUS WmProviderInit(WM_PROVIDER *pProvider);
void WmProviderCleanup(WM_PROVIDER *pProvider);

void WmProviderRemoveHandler(WM_PROVIDER *pProvider, WM_IB_DEVICE *pDevice);
void WmProviderDisableRemove(WM_PROVIDER *pProvider);
void WmProviderEnableRemove(WM_PROVIDER *pProvider);

void WmProviderRead(WM_PROVIDER *pProvider, WDFREQUEST Request);
void WmProviderWrite(WM_PROVIDER *pProvider, WDFREQUEST Request);
void WmReceiveHandler(ib_mad_svc_handle_t hService, void *Context,
					  ib_mad_element_t *pMad);
void WmSendHandler(ib_mad_svc_handle_t hService, void *Context,
				   ib_mad_element_t *pMad);
void WmProviderDeregister(WM_PROVIDER *pProvider,
						  struct _WM_REGISTRATION *pRegistration);
void WmProviderCancel(WM_PROVIDER *pProvider, WDFREQUEST Request);

#endif // _WM_PROVIDER_H_
