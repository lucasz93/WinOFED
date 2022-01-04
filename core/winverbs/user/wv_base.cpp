/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions Copyright (c) 2009 Microsoft Corporation.  All rights reserved.
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

#include <iba\ib_ci.h>
#include "wv_base.h"
#include "wv_cq.h"
#include "wv_qp.h"
#include "wv_ioctl.h"

CWVBase::CWVBase()
{
	InitializeCriticalSection(&m_CritSec);
	m_Overlap.hEvent = NULL;
	m_nRef = 1;
	m_Id = 0;
}

CWVBase::~CWVBase()
{
	if (m_Overlap.hEvent != NULL) {
		CloseHandle(m_Overlap.hEvent);
	}
	DeleteCriticalSection(&m_CritSec);
}

STDMETHODIMP CWVBase::
Init(void)
{
	m_Overlap.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (m_Overlap.hEvent == NULL)
		return WV_INSUFFICIENT_RESOURCES;

	m_Overlap.hEvent = (HANDLE) ((ULONG_PTR) m_Overlap.hEvent | 1);
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVBase::
AddRef(void)
{
	return InterlockedIncrement(&m_nRef);
}

STDMETHODIMP_(ULONG) CWVBase::
Release(void)
{
	ULONG ref;

	ref = (ULONG) InterlockedDecrement(&m_nRef);
	if (ref == 0) {
		Delete();
	}
	return ref;
}

BOOL CWVBase::
WvDeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode,
				  LPVOID lpInBuffer, DWORD nInBufferSize,
				  LPVOID lpOutBuffer, DWORD nOutBufferSize,
				  LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
	BOOL ret;

	if (lpOverlapped == NULL) {
		EnterCriticalSection(&m_CritSec);
		ret = DeviceIoControl(hDevice, dwIoControlCode,
						lpInBuffer, nInBufferSize,
						lpOutBuffer, nOutBufferSize,
						lpBytesReturned, &m_Overlap);
		if (!ret && GetLastError() == ERROR_IO_PENDING) {
		ret = GetOverlappedResult(hDevice, &m_Overlap, lpBytesReturned, TRUE);
		}
		LeaveCriticalSection(&m_CritSec);
	} else {
		ret = DeviceIoControl(hDevice, dwIoControlCode,
							  lpInBuffer, nInBufferSize,
							  lpOutBuffer, nOutBufferSize,
							  lpBytesReturned, lpOverlapped);
	}

	return ret;
}
