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

#include "nd_provider.h"
#include "nd_adapter.h"
#include <ws2tcpip.h>

STDMETHODIMP CNDProvider::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDProvider) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDProvider::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDProvider::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDProvider::
QueryAddressList(SOCKET_ADDRESS_LIST* pAddressList, SIZE_T* pBufferSize)
{
	WV_DEVICE_ADDRESS devaddr;
	IWVProvider *prov;
	struct addrinfo *res, *ai;
	HRESULT hr;
	int cnt = 0;
	size_t addrlen = 0, size;
	UINT8 *offset;

	hr = WvGetObject(IID_IWVProvider, (LPVOID *) &prov);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	hr = getaddrinfo("..localmachine", NULL, NULL, &res);
	if (hr) {
		goto release;
	}

	for (ai = res; ai; ai = ai->ai_next) {
		ai->ai_flags = prov->TranslateAddress(ai->ai_addr, &devaddr);
		if (SUCCEEDED(ai->ai_flags)) {
			cnt++;
			addrlen += ai->ai_addrlen;
		}
	}

	if (cnt == 0) {
		*pBufferSize = 0;
		goto free;
	}

	size = sizeof(SOCKET_ADDRESS_LIST) + sizeof(SOCKET_ADDRESS) * (cnt - 1);
	if (size + addrlen > *pBufferSize) {
		*pBufferSize = size + addrlen;
		hr = ND_BUFFER_OVERFLOW;
		goto free;
	}

	pAddressList->iAddressCount = cnt;
	offset = (UINT8 *) pAddressList + size;
	for (cnt = 0, ai = res; ai; ai = ai->ai_next) {
		if (SUCCEEDED(ai->ai_flags)) {
			pAddressList->Address[cnt].iSockaddrLength = ai->ai_addrlen;
			pAddressList->Address[cnt++].lpSockaddr = (LPSOCKADDR) offset;
			RtlCopyMemory(offset, ai->ai_addr, ai->ai_addrlen);
			offset += ai->ai_addrlen;
		}
	}

free:
	freeaddrinfo(res);
release:
	prov->Release();
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDProvider::
OpenAdapter(const struct sockaddr* pAddress, SIZE_T AddressLength,
			INDAdapter** ppAdapter)
{
	return CNDAdapter::CreateInstance(this, pAddress, AddressLength, ppAdapter);
}


//-------------------------
// CNDClassFactory routines
//-------------------------

STDMETHODIMP CNDClassFactory::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IClassFactory) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDClassFactory::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDClassFactory::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDClassFactory::
CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppObject)
{
	if (pUnkOuter != NULL) {
		return CLASS_E_NOAGGREGATION;
	}

	if (riid != IID_INDProvider) {
		*ppObject = NULL;
		return E_NOINTERFACE;
	}

	*ppObject = new CNDProvider();
	if (*ppObject == NULL) {
		return E_OUTOFMEMORY;
	}

	return S_OK;
}

STDMETHODIMP CNDClassFactory::
LockServer(BOOL fLock)
{
	UNREFERENCED_PARAMETER(fLock);
	return S_OK;
}
