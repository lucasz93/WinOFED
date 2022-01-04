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

#ifndef _ND_QUEUEPAIR_H_
#define _ND_QUEUEPAIR_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"
#include "nd_cq.h"
#include "nd_srq.h"
#include "nd_adapter.h"


class CNDQueuePair : public INDQueuePair, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDQueuePair methods
	STDMETHODIMP Flush();
	STDMETHODIMP Send(VOID* requestContext, const ND_SGE* pSge, DWORD nSge, DWORD flags);
	STDMETHODIMP Receive(VOID* requestContext, const ND_SGE* pSge, DWORD nSge);
	STDMETHODIMP Bind(VOID* requestContext, IUnknown* pMemoryRegion,
					  IUnknown* pMemoryWindow, const VOID* pBuffer, SIZE_T cbBuffer,
					  DWORD flags);
	STDMETHODIMP Invalidate(VOID* requestContext, IUnknown* pMemoryWindow, DWORD flags);
	STDMETHODIMP Read(VOID* requestContext, const ND_SGE* pSge, DWORD nSge,
					  UINT64 remoteAddress, UINT32 remoteToken, DWORD flags);
	STDMETHODIMP Write(VOID* requestContext, const ND_SGE* pSge, DWORD nSge,
					   UINT64 remoteAddress, UINT32 remoteToken, DWORD flags);
	
	CNDQueuePair(CNDAdapter *pAdapter);
	~CNDQueuePair();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter,
				   CNDCompletionQueue* pReceiveCompletionQueue,
				   CNDCompletionQueue* pInitiatorCompletionQueue,
				   CNDSharedReceiveQueue *pSharedReceiveQueue,
				   VOID* context, DWORD receiveQueueDepth, DWORD initiatorQueueDepth,
				   DWORD maxReceiveRequestSge, DWORD maxInitiatorRequestSge,
				   VOID** ppQueuePair)
	{
		HRESULT hr;
		CNDQueuePair *qp;

		qp = new CNDQueuePair(pAdapter);
		if (qp == NULL) {
			hr = ND_NO_MEMORY;
			goto err1;
		}

		hr = qp->Init(pReceiveCompletionQueue, pInitiatorCompletionQueue,
					  pSharedReceiveQueue, context, receiveQueueDepth,
					  initiatorQueueDepth, maxReceiveRequestSge, maxInitiatorRequestSge);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppQueuePair = qp;
		return ND_SUCCESS;

	err2:
		qp->Release();
	err1:
		*ppQueuePair = NULL;
		return hr;
	}

	IWVConnectQueuePair	*m_pWvQp;

protected:
	CNDAdapter			*m_pAdapter;
	CNDCompletionQueue	*m_pReceiveCq;
	CNDCompletionQueue	*m_pSendCq;
	CNDSharedReceiveQueue *m_pSrq;

	STDMETHODIMP Init(CNDCompletionQueue* pReceiveCompletionQueue,
					  CNDCompletionQueue* pInitiatorCompletionQueue,
					  CNDSharedReceiveQueue *pSharedReceiveQueue,
					  VOID* context, DWORD receiveQueueDepth, DWORD initiatorQueueDepth,
					  DWORD maxReceiveRequestSge, DWORD maxInitiatorRequestSge);
	STDMETHODIMP_(DWORD) ConvertSendFlags(DWORD Flags);
};

#endif // _ND_QUEUEPAIR_H_
