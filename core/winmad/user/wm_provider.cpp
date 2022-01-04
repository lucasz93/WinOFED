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

#include <windows.h>
#include <winioctl.h>

#include "wm_memory.h"
#include "wm_provider.h"
#include "wm_ioctl.h"

CWMProvider::CWMProvider()
{
	InitializeCriticalSection(&m_CritSecRead);
	InitializeCriticalSection(&m_CritSecWrite);
	RtlZeroMemory(&m_OverlapRead, sizeof m_OverlapRead);
	RtlZeroMemory(&m_OverlapWrite, sizeof m_OverlapWrite);
	m_nRef = 1;
	m_hFile = INVALID_HANDLE_VALUE;
	InterlockedIncrement(&WmRef);
}

STDMETHODIMP CWMProvider::
Init(void)
{
	m_OverlapRead.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_OverlapWrite.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	return (m_OverlapRead.hEvent != NULL && m_OverlapWrite.hEvent != NULL) ?
			NOERROR : E_OUTOFMEMORY;
}

CWMProvider::~CWMProvider()
{
	if (m_OverlapRead.hEvent != NULL) {
		CloseHandle(m_OverlapRead.hEvent);
	}
	if (m_OverlapWrite.hEvent != NULL) {
		CloseHandle(m_OverlapWrite.hEvent);
	}
	CloseHandle(m_hFile);
	DeleteCriticalSection(&m_CritSecRead);
	DeleteCriticalSection(&m_CritSecWrite);
	InterlockedDecrement(&WmRef);
}

STDMETHODIMP CWMProvider::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWMProvider) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return NOERROR;
}

STDMETHODIMP_(ULONG) CWMProvider::
AddRef(void)
{
	return InterlockedIncrement(&m_nRef);
}

STDMETHODIMP_(ULONG) CWMProvider::
Release(void)
{
	ULONG ref;

	ref = (ULONG) InterlockedDecrement(&m_nRef);
	if (ref == 0) {
		Delete();
	}
	return ref;
}

STDMETHODIMP CWMProvider::
CancelOverlappedRequests(void)
{
	DWORD	bytes;

	return WmDeviceIoControl(m_hFile, WM_IOCTL_CANCEL, NULL, 0,
							 NULL, 0, &bytes, NULL) ?
							 NOERROR : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CWMProvider::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP_(HANDLE) CWMProvider::
GetFileHandle(void)
{
	return m_hFile;
}

STDMETHODIMP_(BOOL) CWMProvider::
WmDeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode,
				  LPVOID lpInBuffer, DWORD nInBufferSize,
				  LPVOID lpOutBuffer, DWORD nOutBufferSize,
				  LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
	BOOL ret;

	if (lpOverlapped == NULL) {
		EnterCriticalSection(&m_CritSecWrite);
		ret = DeviceIoControl(hDevice, dwIoControlCode,
						lpInBuffer, nInBufferSize,
						lpOutBuffer, nOutBufferSize,
						lpBytesReturned, &m_OverlapWrite);
		if (!ret && GetLastError() == ERROR_IO_PENDING) {
			ret = ::GetOverlappedResult(m_hFile, &m_OverlapWrite,
										lpBytesReturned, TRUE);
		}
		LeaveCriticalSection(&m_CritSecWrite);
	} else {
		ret = DeviceIoControl(hDevice, dwIoControlCode,
							  lpInBuffer, nInBufferSize,
							  lpOutBuffer, nOutBufferSize,
							  lpBytesReturned, lpOverlapped);
	}

	return ret;
}

STDMETHODIMP CWMProvider::
Register(WM_REGISTER *pAttributes, UINT64 *pId)
{
	DWORD bytes;

	if (WmDeviceIoControl(m_hFile, WM_IOCTL_REGISTER,
						  pAttributes, sizeof WM_REGISTER,
						  pId, sizeof UINT64, &bytes, NULL)) {
		return NOERROR;
	} else {
		return HRESULT_FROM_WIN32(GetLastError());
	}
}

STDMETHODIMP CWMProvider::
Deregister(UINT64 Id)
{
	DWORD bytes;

	if (WmDeviceIoControl(m_hFile, WM_IOCTL_DEREGISTER,
						  &Id, sizeof UINT64,
						  NULL, 0, &bytes, NULL)) {
		return NOERROR;
	} else {
		return HRESULT_FROM_WIN32(GetLastError());
	}
}

STDMETHODIMP CWMProvider::
Send(WM_MAD *pMad, OVERLAPPED *pOverlapped)
{
	DWORD bytes;
	HRESULT hr;

	bytes = (DWORD) sizeof(WM_MAD) + pMad->Length;
	if (pOverlapped == NULL) {
		EnterCriticalSection(&m_CritSecWrite);
		if (WriteFile(m_hFile, pMad, bytes, NULL, &m_OverlapWrite) ||
			(GetLastError() == ERROR_IO_PENDING)) {
			hr = GetOverlappedResult(&m_OverlapWrite, &bytes, TRUE);
		} else {
			hr = HRESULT_FROM_WIN32(GetLastError());
		}
		LeaveCriticalSection(&m_CritSecWrite);
	} else {
		if (WriteFile(m_hFile, pMad, bytes, &bytes, pOverlapped)) {
			hr = NOERROR;
		} else {
			hr = HRESULT_FROM_WIN32(GetLastError());
		}
	}

	return hr;
}

STDMETHODIMP CWMProvider::
Receive(WM_MAD *pMad, SIZE_T BufferSize, OVERLAPPED *pOverlapped)
{
	DWORD bytes;
	HRESULT hr;

	if (pOverlapped == NULL) {
		EnterCriticalSection(&m_CritSecRead);
		if (ReadFile(m_hFile, pMad, (DWORD) BufferSize, NULL, &m_OverlapRead) ||
			(GetLastError() == ERROR_IO_PENDING)) {
			hr = GetOverlappedResult(&m_OverlapRead, &bytes, TRUE);
		} else {
			hr = HRESULT_FROM_WIN32(GetLastError());
		}
		LeaveCriticalSection(&m_CritSecRead);
	} else {
		if (ReadFile(m_hFile, pMad, (DWORD) BufferSize, &bytes, pOverlapped)) {
			hr = NOERROR;
		} else {
			hr = HRESULT_FROM_WIN32(GetLastError());
		}
	}

	return hr;
}
