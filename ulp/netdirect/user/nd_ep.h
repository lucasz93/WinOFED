/*
 * Copyright (c) 2009 Intel Corporation. All rights reserved.
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

#ifndef _ND_ENDPOINT_H_
#define _ND_ENDPOINT_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"
#include "nd_connect.h"
#include "nd_cq.h"
#include "nd_adapter.h"


#define ND_MAX_SGE	8


class CNDEndpoint : public INDEndpoint, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDEndpoint methods
	STDMETHODIMP Flush();
	STDMETHODIMP_(void) StartRequestBatch();
	STDMETHODIMP_(void) SubmitRequestBatch();
	STDMETHODIMP Send(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge, DWORD Flags);
	STDMETHODIMP SendAndInvalidate(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge,
								   const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
								   DWORD Flags);
	STDMETHODIMP Receive(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge);
	STDMETHODIMP Bind(ND_RESULT* pResult, ND_MR_HANDLE hMr, INDMemoryWindow* pMw,
					  const void* pBuffer, SIZE_T BufferSize, DWORD Flags,
					  ND_MW_DESCRIPTOR* pMwDescriptor);
	STDMETHODIMP Invalidate(ND_RESULT* pResult, INDMemoryWindow* pMw, DWORD Flags);
	STDMETHODIMP Read(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge,
					  const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
					  ULONGLONG Offset, DWORD Flags);
	STDMETHODIMP Write(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge,
					   const ND_MW_DESCRIPTOR* pRemoteMwDescriptor,
					   ULONGLONG Offset, DWORD Flags);
	
	CNDEndpoint(CNDConnector *pConnector);
	~CNDEndpoint();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDConnector *pConnector,
				   CNDCompletionQueue* pInboundCq, CNDCompletionQueue* pOutboundCq,
				   SIZE_T nInboundEntries, SIZE_T nOutboundEntries,
				   SIZE_T nInboundSge, SIZE_T nOutboundSge,
				   SIZE_T InboundReadLimit, SIZE_T OutboundReadLimit,
				   SIZE_T* pMaxInlineData, INDEndpoint** ppEndpoint)
	{
		HRESULT hr;
		CNDEndpoint *ep;

		ep = new CNDEndpoint(pConnector);
		if (ep == NULL) {
			hr = ND_NO_MEMORY;
			goto err1;
		}

		hr = ep->Init(pInboundCq, pOutboundCq, nInboundEntries, nOutboundEntries,
					  nInboundSge, nOutboundSge, InboundReadLimit, OutboundReadLimit,
					  pMaxInlineData);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppEndpoint = ep;
		return ND_SUCCESS;

	err2:
		ep->Release();
	err1:
		*ppEndpoint = NULL;
		return hr;
	}

	IWVConnectQueuePair	*m_pWvQp;
	SIZE_T				m_InitiatorDepth;
	SIZE_T				m_ResponderResources;
	SIZE_T				m_MaxInlineSend;

protected:
	CNDConnector		*m_pConnector;
	CNDCompletionQueue	*m_pInboundCq;
	CNDCompletionQueue	*m_pOutboundCq;

	STDMETHODIMP Init(CNDCompletionQueue* pInboundCq, CNDCompletionQueue* pOutboundCq,
					  SIZE_T nInboundEntries, SIZE_T nOutboundEntries,
					  SIZE_T nInboundSge, SIZE_T nOutboundSge,
					  SIZE_T InboundReadLimit, SIZE_T OutboundReadLimit,
					  SIZE_T* pMaxInlineData);
	STDMETHODIMP_(void) InitMaxInline();
	STDMETHODIMP_(SIZE_T) ConvertSgl(const ND_SGE* pSgl, SIZE_T nSge, WV_SGE *pWvSgl);
	STDMETHODIMP_(DWORD) ConvertSendFlags(DWORD Flags);
	STDMETHODIMP_(DWORD) ConvertAccessFlags(DWORD Flags);
};

#endif // _ND_ENDPOINT_H_
