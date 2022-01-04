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

#ifndef _WV_CQ_H_
#define _WV_CQ_H_

#include <rdma\winverbs.h>
#include "wv_device.h"
#include "wv_base.h"

class CWVCompletionQueue : IWVCompletionQueue, public CWVBase
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

	// IWVCompletionQueue methods
	STDMETHODIMP Resize(SIZE_T* pEntries);
	STDMETHODIMP Peek(SIZE_T* pCompletedEntries);
	STDMETHODIMP Notify(WV_CQ_NOTIFY_TYPE Type, OVERLAPPED* pOverlapped);
	STDMETHODIMP BatchNotify(SIZE_T CompletedEntries, OVERLAPPED* pOverlapped);
	STDMETHODIMP_(SIZE_T) Poll(WV_COMPLETION Completions[], SIZE_T Entries);

	CWVCompletionQueue(CWVDevice *pDevice);
	~CWVCompletionQueue();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVDevice *pDevice, SIZE_T *pEntries, IWVCompletionQueue** ppCq)
	{
		HRESULT hr;
		CWVCompletionQueue *cq;

		cq = new CWVCompletionQueue(pDevice);
		if (cq == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = cq->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = cq->Create(pEntries);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppCq = cq;
		return WV_SUCCESS;

	err2:
		cq->Release();
	err1:
		*ppCq = NULL;
		return hr;
	}

	CWVDevice		*m_pDevice;
	uvp_interface_t	*m_pVerbs;

	ib_cq_handle_t	m_hVerbsCq;

protected:
	STDMETHODIMP Create(SIZE_T *pEntries);
};

#endif //_WV_CQ_H_