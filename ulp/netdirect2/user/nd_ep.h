/*
 * Copyright (c) 2009-2010 Intel Corporation. All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#ifndef _ND_ENDPOINT_H_
#define _ND_ENDPOINT_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"
#include "nd_adapter.h"


class CNDSharedEndpoint : public INDSharedEndpoint, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDEndpoint methods
	STDMETHODIMP Bind(const struct sockaddr* pAddress, SIZE_T cbAddress);
	STDMETHODIMP GetLocalAddress(struct sockaddr* pAddress, SIZE_T* pcbAddress);
	
	CNDSharedEndpoint(CNDAdapter *pAdapter);
	~CNDSharedEndpoint();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter, VOID** ppSharedEndpoint)
	{
		HRESULT hr;
		CNDSharedEndpoint *ep;

		ep = new CNDSharedEndpoint(pAdapter);
		if (ep == NULL) {
			hr = ND_NO_MEMORY;
			goto err;
		}

		*ppSharedEndpoint = ep;
		return ND_SUCCESS;

	err:
		*ppSharedEndpoint = NULL;
		return hr;
	}

	SOCKADDR_STORAGE	m_Address;
	SIZE_T				m_AddressSize;
	CNDAdapter			*m_pAdapter;
};

#endif // _ND_ENDPOINT_H_
