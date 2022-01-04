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

#ifndef _WV_SRQ_H_
#define _WV_SRQ_H_

#include <rdma\winverbs.h>
#include "wv_pd.h"
#include "wv_base.h"

class CWVSharedReceiveQueue : IWVSharedReceiveQueue, public CWVBase
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

	// IWVSharedReceiveQueue methods
	STDMETHODIMP Query(SIZE_T* pMaxWr, SIZE_T* pMaxSge, SIZE_T* pSrqLimit);
	STDMETHODIMP Modify(SIZE_T MaxWr, SIZE_T SrqLimit);
	STDMETHODIMP PostReceive(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge);
	STDMETHODIMP Notify(OVERLAPPED* pOverlapped);

	CWVSharedReceiveQueue(CWVProtectionDomain *pPd);
	~CWVSharedReceiveQueue();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVProtectionDomain *pPd, SIZE_T MaxWr, SIZE_T MaxSge,
				   SIZE_T SrqLimit, IWVSharedReceiveQueue** ppSrq)
	{
		HRESULT hr;
		CWVSharedReceiveQueue *srq;

		srq = new CWVSharedReceiveQueue(pPd);
		if (srq == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = srq->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = srq->Create(MaxWr, MaxSge, SrqLimit);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppSrq = srq;
		return WV_SUCCESS;

	err2:
		srq->Release();
	err1:
		*ppSrq = NULL;
		return hr;
	}

	CWVProtectionDomain	*m_pPd;
	uvp_interface_t		*m_pVerbs;
	ib_srq_handle_t		m_hVerbsSrq;

protected:
	STDMETHODIMP Create(SIZE_T MaxWr, SIZE_T MaxSge, SIZE_T SrqLimit);
};

#endif // _WV_SRQ_H_