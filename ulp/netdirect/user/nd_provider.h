/*
 * Copyright (c) 2009 Intel Corporation. All rights reserved.
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

#ifndef _ND_PROVIDER_H_
#define _ND_PROVIDER_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"

class CNDProvider : public INDProvider, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDProvider methods
	STDMETHODIMP QueryAddressList(SOCKET_ADDRESS_LIST* pAddressList,
								  SIZE_T* pBufferSize);
	STDMETHODIMP OpenAdapter(const struct sockaddr* pAddress,
							 SIZE_T AddressLength, INDAdapter** ppAdapter);

	CNDProvider() {};
	~CNDProvider() {};
	void Delete() {delete this;}
};


class CNDClassFactory : public IClassFactory, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IClassFactory methods
	STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppObject);
	STDMETHODIMP LockServer(BOOL fLock);

	CNDClassFactory() {};
	~CNDClassFactory() {};
	void Delete() {delete this;}
};

#endif // _ND_PROVIDER_H_