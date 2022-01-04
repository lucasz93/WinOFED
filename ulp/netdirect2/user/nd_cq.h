/*
 * Copyright (c) 2009-2010 Intel Corporation. All rights reserved.
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

#ifndef _ND_CQ_H_
#define _ND_CQ_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"
#include "nd_adapter.h"

class CNDCompletionQueue : public INDCompletionQueue, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDOverlapped methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped, BOOL bWait);

	// INDCompletionQueue methods
	STDMETHODIMP GetNotifyAffinity(USHORT* pGroup, KAFFINITY* pAffinity);
	STDMETHODIMP Resize(DWORD queueDepth);
	STDMETHODIMP Notify(DWORD type, OVERLAPPED* pOverlapped);
	STDMETHODIMP_(DWORD) GetResults(ND_RESULT results[], DWORD nResults);

	CNDCompletionQueue(CNDAdapter *pAdapter);
	~CNDCompletionQueue();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter, DWORD queueDepth, USHORT group,
				   KAFFINITY affinity, VOID** ppCq)
	{
		HRESULT hr;
		CNDCompletionQueue *cq;

		cq = new CNDCompletionQueue(pAdapter);
		if (cq == NULL) {
			hr = ND_NO_MEMORY;
			goto err1;
		}

		hr = cq->Init(queueDepth, group, affinity);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppCq = cq;
		return ND_SUCCESS;

	err2:
		cq->Release();
	err1:
		*ppCq = NULL;
		return hr;
	}

	IWVCompletionQueue	*m_pWvCq;

protected:
	CNDAdapter			*m_pAdapter;
	STDMETHODIMP		Init(DWORD queueDepth, USHORT group, KAFFINITY affinity);
	STDMETHODIMP_(HRESULT) ConvertStatus(WV_WC_STATUS Status);
};

#endif // _ND_CQ_H_
