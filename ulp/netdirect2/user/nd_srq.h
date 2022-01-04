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

#ifndef _ND_SRQ_H_
#define _ND_SRQ_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"
#include "nd_adapter.h"


class CNDSharedReceiveQueue : public INDSharedReceiveQueue, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDOverlapped methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped, BOOL bWait);

	// INDSharedReceiveQueue methods
	STDMETHODIMP GetNotifyAffinity(USHORT *pGroup, KAFFINITY* pAffinity);
	STDMETHODIMP Modify(DWORD queueDepth, DWORD notifyThreshold);
	STDMETHODIMP Notify(OVERLAPPED* pOverlapped);
	STDMETHODIMP Receive(VOID* requestContext, const ND_SGE* pSge, DWORD nSge);

	CNDSharedReceiveQueue(CNDAdapter *pAdapter);
	~CNDSharedReceiveQueue();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter, DWORD queueDepth, DWORD maxSge,
				   DWORD notifyThreshold, USHORT group, KAFFINITY affinity,
				   VOID** ppSharedReceiveQueue)
	{
		HRESULT hr;
		CNDSharedReceiveQueue *srq;

		srq = new CNDSharedReceiveQueue(pAdapter);
		if (srq == NULL) {
			hr = ND_NO_MEMORY;
			goto err1;
		}

		hr = srq->Init(queueDepth, maxSge, notifyThreshold, group, affinity);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppSharedReceiveQueue = srq;
		return ND_SUCCESS;

	err2:
		srq->Release();
	err1:
		*ppSharedReceiveQueue = NULL;
		return hr;
	}

	IWVSharedReceiveQueue	*m_pWvSrq;

protected:
	CNDAdapter				*m_pAdapter;

	STDMETHODIMP Init(DWORD queueDepth, DWORD maxSge, DWORD notifyThreshold,
					  USHORT group, KAFFINITY affinity);
};

#endif // _ND_SRQ_H_
