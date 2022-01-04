/*
 * Copyright (c) 2009-2010 Intel Corporation. All rights reserved.
 * Copyright (c) 2010 Microsoft Corporation.  All rights reserved.
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

extern volatile LONG g_nRef;


CNDProvider::CNDProvider()
{
	m_pWvProvider = NULL;
	InterlockedIncrement(&g_nRef);
}

STDMETHODIMP CNDProvider::
Init()
{
	HRESULT hr;

	hr = WvGetObject(IID_IWVProvider, (LPVOID *) &m_pWvProvider);
	if (FAILED(hr)) {
		return hr;
	}

	if (!SetFileCompletionNotificationModes(m_pWvProvider->GetFileHandle(),
											FILE_SKIP_COMPLETION_PORT_ON_SUCCESS |
											FILE_SKIP_SET_EVENT_ON_HANDLE)) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	return ND_SUCCESS;
}

CNDProvider::~CNDProvider()
{
	if (m_pWvProvider) {
		m_pWvProvider->Release();
	}
	InterlockedDecrement(&g_nRef);
}

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
QueryAdapterAddressList(SOCKET_ADDRESS_LIST* pAddressList,
						SIZE_T* pcbAddressList, UINT64 adapterId)
{
	WV_DEVICE_ADDRESS devaddr;
	struct addrinfo *res, *ai;
	HRESULT hr;
	int cnt = 0;
	size_t addrlen = 0, size;
	UINT8 *offset;

	hr = getaddrinfo("..localmachine", NULL, NULL, &res);
	if (hr) {
		goto out;
	}

	for (ai = res; ai; ai = ai->ai_next) {
		ai->ai_flags = m_pWvProvider->TranslateAddress(ai->ai_addr, &devaddr);
		if (SUCCEEDED(ai->ai_flags) &&
			((adapterId == 0) || (adapterId == devaddr.DeviceGuid))) {
			cnt++;
			addrlen += ai->ai_addrlen;
		}
	}

	if (cnt == 0) {
		*pcbAddressList = 0;
		goto free;
	}

	size = sizeof(SOCKET_ADDRESS_LIST) + sizeof(SOCKET_ADDRESS) * (cnt - 1);
	if (size + addrlen > *pcbAddressList) {
		*pcbAddressList = size + addrlen;
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
out:
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDProvider::
QueryAddressList(SOCKET_ADDRESS_LIST* pAddressList, SIZE_T* pcbAddressList)
{
	return QueryAdapterAddressList(pAddressList, pcbAddressList, 0);
}

STDMETHODIMP CNDProvider::
ResolveAddress(const struct sockaddr* pAddress, SIZE_T cbAddress, UINT64* pAdapterId)
{
	WV_DEVICE_ADDRESS devaddr;
	HRESULT hr;

	hr = m_pWvProvider->TranslateAddress(pAddress, &devaddr);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	*pAdapterId = devaddr.DeviceGuid;
	return ND_SUCCESS;
}

STDMETHODIMP CNDProvider::
OpenAdapter(REFIID iid, UINT64 adapterId, VOID** ppAdapter)
{
	if (iid != IID_INDAdapter) {
		return E_NOINTERFACE;
	}

	return CNDAdapter::CreateInstance(this, adapterId, ppAdapter);
}
