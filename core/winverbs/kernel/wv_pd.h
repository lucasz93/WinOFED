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

#ifndef _WV_PD_H_
#define _WV_PD_H_

#include <ntddk.h>
#include <wdm.h>
#include <iba\ib_types.h>
#include <iba\ib_ci.h>

#include "wv_device.h"
#include "wv_provider.h"

typedef struct _WV_PROTECTION_DOMAIN
{
	WV_DEVICE			*pDevice;
	ci_interface_t		*pVerbs;
	ib_pd_handle_t		hVerbsPd;
	LIST_ENTRY			Entry;

	LIST_ENTRY			QpList;
	LIST_ENTRY			SrqList;
	LIST_ENTRY			MwList;
	LIST_ENTRY			AhList;
	KGUARDED_MUTEX		Lock;
	cl_qmap_t			MrMap;

	KEVENT				Event;
	LONG				Ref;

}	WV_PROTECTION_DOMAIN;

WV_PROTECTION_DOMAIN *WvPdAcquire(WV_PROVIDER *pProvider, UINT64 Id);
void WvPdRelease(WV_PROTECTION_DOMAIN *pPd);
void WvPdGet(WV_PROTECTION_DOMAIN *pPd);
void WvPdPut(WV_PROTECTION_DOMAIN *pPd);

void WvPdAllocate(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvPdDeallocate(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvPdCancel(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvPdFree(WV_PROTECTION_DOMAIN *pPd);
void WvPdRemoveHandler(WV_PROTECTION_DOMAIN *pPd);


typedef struct _WV_MEMORY_REGION
{
	ib_mr_handle_t			hVerbsMr;
	cl_map_item_t			Item;

}	WV_MEMORY_REGION;

void WvMrRegister(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvMrDeregister(WV_PROVIDER *pProvider, WDFREQUEST Request);


typedef struct _WV_MEMORY_WINDOW
{
	WV_PROTECTION_DOMAIN	*pPd;
	ib_mw_handle_t			hVerbsMw;
	LIST_ENTRY				Entry;

}	WV_MEMORY_WINDOW;

#ifdef NTDDI_WIN8
    _IRQL_requires_same_
    _Function_class_(ALLOCATE_FUNCTION)
#endif
void WvMwAllocate(WV_PROVIDER *pProvider, WDFREQUEST Request);

void WvMwDeallocate(WV_PROVIDER *pProvider, WDFREQUEST Request);

#ifdef NTDDI_WIN8
_IRQL_requires_same_
_Function_class_(FREE_FUNCTION)
#endif
void WvMwFree(WV_MEMORY_WINDOW *pMw);


typedef struct _WV_ADDRESS_HANDLE
{
	WV_PROTECTION_DOMAIN	*pPd;
	ib_av_handle_t			hVerbsAh;
	LIST_ENTRY				Entry;

}	WV_ADDRESS_HANDLE;

#ifdef NTDDI_WIN8
_IRQL_requires_same_
_Function_class_(ALLOCATE_FUNCTION)
#endif
void WvAhCreate(WV_PROVIDER *pProvider, WDFREQUEST Request);
void WvAhDestroy(WV_PROVIDER *pProvider, WDFREQUEST Request);

#ifdef NTDDI_WIN8
_IRQL_requires_same_
_Function_class_(FREE_FUNCTION)
#endif
void WvAhFree(WV_ADDRESS_HANDLE *pAh);

#endif // _WV_PD_H_
