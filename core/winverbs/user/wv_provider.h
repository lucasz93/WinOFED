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

#ifndef _WV_PROVIDER_H_
#define _WV_PROVIDER_H_

#include <rdma\winverbs.h>
#include "wv_base.h"

extern volatile LONG WvRef;

class CWVProvider : IWVProvider, public CWVBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IWVProvider methods
	STDMETHODIMP_(HANDLE) GetFileHandle();
	STDMETHODIMP QueryDeviceList(NET64* pGuidList, SIZE_T* pBufferSize);
	STDMETHODIMP QueryDevice(NET64 Guid, WV_DEVICE_ATTRIBUTES* pAttributes);
	STDMETHODIMP TranslateAddress(const SOCKADDR* pAddress,
								  WV_DEVICE_ADDRESS* pDeviceAddress);

	STDMETHODIMP OpenDevice(NET64 Guid, IWVDevice** ppDevice);

	STDMETHODIMP CreateConnectEndpoint(IWVConnectEndpoint** ppConnectEndpoint);
	STDMETHODIMP CreateDatagramEndpoint(IWVDatagramEndpoint** ppDatagramEndpoint);

	CWVProvider();
	~CWVProvider();
	void Delete() {delete this;}
	static STDMETHODIMP CreateInstance(IWVProvider** ppProvider)
	{
		WSADATA wsadata;
		HRESULT hr;
		CWVProvider *wv;

		if (WvRef == 0) {
			hr = WSAStartup(MAKEWORD(2, 2), &wsadata);
			if (FAILED(hr)) {
				return hr;
			}
		}

		wv = new CWVProvider;
		if (wv == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = wv->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		wv->m_hFile = CreateFileW(L"\\\\.\\WinVerbs", GENERIC_READ | GENERIC_WRITE,
								  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
								  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (wv->m_hFile == INVALID_HANDLE_VALUE) {
			hr = HRESULT_FROM_WIN32(GetLastError());
			goto err2;
		}
		*ppProvider = wv;
		return WV_SUCCESS;

	err2:
		wv->Release();
	err1:
		*ppProvider = NULL;
		return hr;
	}
};

#endif // _WV_PROVIDER_H_