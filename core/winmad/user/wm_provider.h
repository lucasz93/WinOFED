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

#include <iba\winmad.h>

extern volatile LONG WmRef;

class CWMProvider : IWMProvider
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IWVProvider methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped,
									 DWORD *pNumberOfBytesTransferred, BOOL bWait);

	STDMETHODIMP_(HANDLE) GetFileHandle();
	STDMETHODIMP Register(WM_REGISTER *pAttributes, UINT64 *pId);
	STDMETHODIMP Deregister(UINT64 Id);
	STDMETHODIMP Send(WM_MAD *pMad, OVERLAPPED *pOverlapped);
	STDMETHODIMP Receive(WM_MAD *pMad, SIZE_T BufferSize, OVERLAPPED *pOverlapped);

	CWMProvider();
	STDMETHODIMP Init();
	~CWMProvider();
	void Delete() {delete this;}
	static STDMETHODIMP CreateInstance(IWMProvider** ppProvider)
	{
		HRESULT hr;
		CWMProvider *wm;

		wm = new CWMProvider;
		if (wm == NULL) {
			hr = E_OUTOFMEMORY;
			goto err1;
		}

		hr = wm->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		wm->m_hFile = CreateFileW(L"\\\\.\\WinMad", GENERIC_READ | GENERIC_WRITE,
								  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
								  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (wm->m_hFile == INVALID_HANDLE_VALUE) {
			hr = HRESULT_FROM_WIN32(GetLastError());
			goto err2;
		}
		*ppProvider = wm;
		return NOERROR;

	err2:
		wm->Release();
	err1:
		*ppProvider = NULL;
		return hr;
	}

	HANDLE					m_hFile;
	volatile LONG			m_nRef;
protected:
	STDMETHODIMP_(BOOL) WmDeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode,
										  LPVOID lpInBuffer, DWORD nInBufferSize,
										  LPVOID lpOutBuffer, DWORD nOutBufferSize,
										  LPDWORD lpBytesReturned,
										  LPOVERLAPPED lpOverlapped);

	OVERLAPPED				m_OverlapWrite;
	OVERLAPPED				m_OverlapRead;
	CRITICAL_SECTION		m_CritSecWrite;
	CRITICAL_SECTION		m_CritSecRead;
};

#endif // _WM_PROVIDER_H_