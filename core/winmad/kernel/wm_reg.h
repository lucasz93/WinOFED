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

#ifndef _WM_REG_H_
#define _WM_REG_H_

#include <ntddk.h>
#include <wdm.h>
#include <iba\ib_types.h>

#include "wm_driver.h"
#include "wm_provider.h"

typedef struct _WM_REGISTRATION
{
	WM_PROVIDER				*pProvider;
	WM_IB_DEVICE			*pDevice;

	ib_al_handle_t			hIbal;
	ib_ca_handle_t			hCa;
	UINT8					PortNum;
	ib_pd_handle_t			hPd;
	ib_qp_handle_t			hQp;
	ib_pool_key_t			hMadPool;
	ib_mad_svc_handle_t		hService;
	ib_ca_mod_t				PortCapMask;

	SIZE_T					Id;
	LONG					Ref;

}	WM_REGISTRATION;

void WmRegister(WM_PROVIDER *pProvider, WDFREQUEST Request);
void WmDeregister(WM_PROVIDER *pProvider, WDFREQUEST Request);
void WmRegFree(WM_REGISTRATION *pRegistration);
void WmRegRemoveHandler(WM_REGISTRATION *pRegistration);

WM_REGISTRATION *WmRegAcquire(WM_PROVIDER *pProvider, UINT64 Id);
void WmRegRelease(WM_REGISTRATION *pRegistration);

#endif // __WM_REG_H_
