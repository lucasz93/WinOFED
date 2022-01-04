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

#ifndef _WV_PD_H_
#define _WV_PD_H_

#include <rdma\winverbs.h>
#include "wv_device.h"
#include "wv_ioctl.h"
#include "wv_base.h"

class CWVProtectionDomain : IWVProtectionDomain, public CWVBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IWVOverlapped methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped,
									 DWORD *pNumberOfBytesTransferred, BOOL bWait);

	// IWVProtectionDomain methods
	STDMETHODIMP CreateSharedReceiveQueue(SIZE_T MaxWr, SIZE_T MaxSge, SIZE_T SrqLimit,
										  IWVSharedReceiveQueue** ppSrq);
	STDMETHODIMP CreateConnectQueuePair(WV_QP_CREATE* pAttributes,
										IWVConnectQueuePair** ppQp);
	STDMETHODIMP CreateDatagramQueuePair(WV_QP_CREATE* pAttributes,
										 IWVDatagramQueuePair** ppQp);
	STDMETHODIMP RegisterMemory(const VOID* pBuffer, SIZE_T BufferLength,
								DWORD AccessFlags, OVERLAPPED* pOverlapped,
								WV_MEMORY_KEYS *pKeys);
	STDMETHODIMP DeregisterMemory(UINT32 Lkey, OVERLAPPED* pOverlapped);
	STDMETHODIMP AllocateMemoryWindow(IWVMemoryWindow** ppMw);
	STDMETHODIMP CreateAddressHandle(WV_ADDRESS_VECTOR* pAddress,
									 IWVAddressHandle** ppAh, ULONG_PTR *pAhKey);

	CWVProtectionDomain(CWVDevice *pDevice);
	~CWVProtectionDomain();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVDevice *pDevice, IWVProtectionDomain** ppPd)
	{
		HRESULT hr;
		CWVProtectionDomain *pd;

		pd = new CWVProtectionDomain(pDevice);
		if (pd == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = pd->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = pd->Allocate();
		if (FAILED(hr)) {
			goto err2;
		}

		*ppPd = pd;
		return WV_SUCCESS;

	err2:
		pd->Release();
	err1:
		*ppPd = NULL;
		return hr;
	}

	CWVDevice		*m_pDevice;
	uvp_interface_t	*m_pVerbs;
	ib_pd_handle_t	m_hVerbsPd;

protected:
	STDMETHODIMP Allocate();
};


class CWVMemoryWindow : IWVMemoryWindow, public CWVBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	CWVMemoryWindow(CWVProtectionDomain *pPd);
	~CWVMemoryWindow();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVProtectionDomain *pPd, IWVMemoryWindow** ppMw)
	{
		HRESULT hr;
		CWVMemoryWindow *mw;

		mw = new CWVMemoryWindow(pPd);
		if (mw == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = mw->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = mw->Allocate();
		if (FAILED(hr)) {
			goto err2;
		}

		*ppMw = mw;
		return WV_SUCCESS;

	err2:
		mw->Release();
	err1:
		*ppMw = NULL;
		return hr;
	}

	CWVProtectionDomain	*m_pPd;
	uvp_interface_t		*m_pVerbs;
	ib_mw_handle_t		m_hVerbsMw;
	NET32				m_Rkey;

protected:
	STDMETHODIMP Allocate();
};


class CWVAddressHandle : IWVAddressHandle, public CWVBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	CWVAddressHandle(CWVProtectionDomain *pPd);
	~CWVAddressHandle();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVProtectionDomain *pPd, WV_ADDRESS_VECTOR* pAddress,
				   IWVAddressHandle** ppAh, ULONG_PTR* pAhKey)
	{
		HRESULT hr;
		CWVAddressHandle *ah;

		ah = new CWVAddressHandle(pPd);
		if (ah == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = ah->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = ah->Create(pAddress);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppAh = ah;
		*pAhKey = (ULONG_PTR) ah->m_hVerbsAh;
		return WV_SUCCESS;

	err2:
		ah->Release();
	err1:
		*ppAh = NULL;
		return hr;
	}

	CWVProtectionDomain	*m_pPd;
	uvp_interface_t		*m_pVerbs;
	ib_av_handle_t		m_hVerbsAh;

protected:
	STDMETHODIMP Create(WV_ADDRESS_VECTOR* pAddress);
	STDMETHODIMP ConvertAv(ib_av_attr_t *pVerbsAv, WV_ADDRESS_VECTOR *pAv);
};

#endif // _WV_PD_H_