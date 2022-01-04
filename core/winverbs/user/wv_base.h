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

#ifndef _WV_BASE_H_
#define _WV_BASE_H_

#include <windows.h>
#include <rdma\winverbs.h>
#include <winioctl.h>

#include <iba\ib_types.h>
#include <iba\ib_uvp.h>

HRESULT WvConvertIbStatus(ib_api_status_t status);
HRESULT WvConvertWSAStatus(int status);
HRESULT WvGetUserVerbs(HMODULE hLib, uvp_interface_t *pVerbs);


class CWVBase
{
public:
	CWVBase();
	~CWVBase();
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	STDMETHODIMP Init();
	virtual void Delete() {};
	BOOL WvDeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode,
						   LPVOID lpInBuffer, DWORD nInBufferSize,
						   LPVOID lpOutBuffer, DWORD nOutBufferSize,
						   LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);

	HANDLE					m_hFile;
	volatile LONG			m_nRef;
	UINT64					m_Id;

protected:
	OVERLAPPED				m_Overlap;
	CRITICAL_SECTION		m_CritSec;
};


#if defined(_WIN64)
	#define WvConvertSgl(pSgl, nSge)	((ib_local_ds_t *) pSgl)
#else
	static inline ib_local_ds_t *WvConvertSgl(WV_SGE* pSgl, SIZE_T nSge)
	{
		SIZE_T n;

		for (n = 0; n < nSge; n++) {
			pSgl[n].Reserved = 0;
		}
		return (ib_local_ds_t *) pSgl;
	}
#endif (_WIN64)

#endif // _WV_BASE_H_
