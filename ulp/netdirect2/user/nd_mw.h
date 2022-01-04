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

#pragma once

#ifndef _ND_MW_H_
#define _ND_MW_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"
#include "nd_adapter.h"

class CNDMemoryRegion : public INDMemoryRegion, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDOverlapped methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped, BOOL bWait);

	// INDMemoryRegion methods
	STDMETHODIMP Register(const VOID* pBuffer, SIZE_T cbBuffer, DWORD flags,
						  OVERLAPPED* pOverlapped);
    STDMETHODIMP Deregister(OVERLAPPED* pOverlapped);
	STDMETHODIMP_(UINT32) GetLocalToken();
	STDMETHODIMP_(UINT32) GetRemoteToken();

	CNDMemoryRegion(CNDAdapter *m_pAdapter);
	~CNDMemoryRegion();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter, VOID** ppMemoryRegion)
	{
		HRESULT hr;
		CNDMemoryRegion *mr;

		mr = new CNDMemoryRegion(pAdapter);
		if (mr == NULL) {
			hr = ND_NO_MEMORY;
			goto err;
		}

		*ppMemoryRegion = mr;
		return ND_SUCCESS;

	err:
		*ppMemoryRegion = NULL;
		return hr;
	}

	WV_MEMORY_KEYS		m_Keys;

protected:
	CNDAdapter			*m_pAdapter;
	OVERLAPPED			*m_pOverlapped;

	DWORD ConvertAccessFlags(DWORD Flags);
};


class CNDMemoryWindow : public INDMemoryWindow, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDMemoryWindow methods
	STDMETHODIMP_(UINT32) GetRemoteToken();
	
	CNDMemoryWindow(CNDAdapter *m_pAdapter);
	~CNDMemoryWindow();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter, VOID** ppMemoryWindow)
	{
		HRESULT hr;
		CNDMemoryWindow *mw;

		mw = new CNDMemoryWindow(pAdapter);
		if (mw == NULL) {
			hr = ND_NO_MEMORY;
			goto err;
		}

		*ppMemoryWindow = mw;
		return ND_SUCCESS;

	err:
		*ppMemoryWindow = NULL;
		return hr;
	}

	CNDMemoryRegion		*m_pMr;

protected:
	CNDAdapter			*m_pAdapter;
};

#endif // _ND_MW_H_
