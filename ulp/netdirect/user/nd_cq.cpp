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

#include "nd_cq.h"

CNDCompletionQueue::CNDCompletionQueue(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
	m_pWvCq = NULL;
}

STDMETHODIMP CNDCompletionQueue::
Init(SIZE_T nEntries)
{
	HRESULT hr;

	hr = m_pAdapter->m_pWvDevice->CreateCompletionQueue(&nEntries, &m_pWvCq);
	return NDConvertWVStatus(hr);
}

CNDCompletionQueue::~CNDCompletionQueue()
{
	if (m_pWvCq != NULL) {
		m_pWvCq->Release();
	}
	m_pAdapter->Release();
}

STDMETHODIMP CNDCompletionQueue::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDCompletionQueue) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDCompletionQueue::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDCompletionQueue::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDCompletionQueue::
CancelOverlappedRequests(void)
{
	HRESULT hr;

	hr = m_pWvCq->CancelOverlappedRequests();
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDCompletionQueue::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					SIZE_T *pNumberOfBytesTransferred, BOOL bWait)
{
	HRESULT hr;

	hr = m_pWvCq->GetOverlappedResult(pOverlapped,
									  (DWORD *) pNumberOfBytesTransferred, bWait);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDCompletionQueue::
Resize(SIZE_T nEntries)
{
	HRESULT hr;

	hr = m_pWvCq->Resize(&nEntries);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDCompletionQueue::
Notify(DWORD Type, OVERLAPPED* pOverlapped)
{
	HRESULT hr;

	hr = m_pWvCq->Notify((WV_CQ_NOTIFY_TYPE) Type, pOverlapped);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP_(HRESULT) CNDCompletionQueue::
ConvertStatus(WV_WC_STATUS Status)
{
	switch (Status) {
	case WvWcSuccess:
		return ND_SUCCESS;
	case WvWcFlushed:
		return ND_CANCELED;
	case WvWcLocalLengthError:
		return ND_LOCAL_LENGTH;
	case WvWcRnrRetryError:
	case WvWcTimeoutRetryError:
		return ND_TIMEOUT;
	case WvWcLocalAccessError:
	case WvWcLocalOpError:
	case WvWcLocalProtectionError:
	case WvWcMwBindError:
		return ND_ACCESS_VIOLATION;
	case WvWcRemoteAccessError:
	case WvWcRemoteOpError:
	case WvWcRemoteInvalidRequest:
	case WvWcBadResponse:
		return ND_REMOTE_ERROR;
	default:
		return ND_INTERNAL_ERROR;
	}
}

STDMETHODIMP_(SIZE_T) CNDCompletionQueue::
GetResults(ND_RESULT* pResults[], SIZE_T nResults)
{
	WV_COMPLETION	comp[8];
	SIZE_T			cnt, total, i;

	for (total = 0; nResults; nResults -= cnt) {
		cnt = min(8, nResults);
		cnt = m_pWvCq->Poll(comp, cnt);
		if (cnt == 0) {
			break;
		}

		for (i = 0; i < cnt; i++) {
			pResults[total] = (ND_RESULT *) comp[i].WrId;
			if (comp[i].Opcode & WvReceive) {
				pResults[total]->BytesTransferred = comp[i].Length;
			}
			pResults[total++]->Status = ConvertStatus(comp[i].Status);
		}
	}
	return total;
}
