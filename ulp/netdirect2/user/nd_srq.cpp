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

#include "nd_srq.h"


CNDSharedReceiveQueue::CNDSharedReceiveQueue(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
	m_pWvSrq = NULL;
}

STDMETHODIMP CNDSharedReceiveQueue::
Init(DWORD queueDepth, DWORD maxSGE, DWORD notifyThreshold,
	 USHORT group, KAFFINITY affinity)
{
	HRESULT hr;

	hr = m_pAdapter->m_pWvPd->CreateSharedReceiveQueue(queueDepth, maxSGE,
													   notifyThreshold, &m_pWvSrq);
	return NDConvertWVStatus(hr);
}

CNDSharedReceiveQueue::~CNDSharedReceiveQueue()
{
	if (m_pWvSrq != NULL) {
		m_pWvSrq->Release();
	}
	m_pAdapter->Release();
}

STDMETHODIMP CNDSharedReceiveQueue::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDSharedReceiveQueue) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDSharedReceiveQueue::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDSharedReceiveQueue::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDSharedReceiveQueue::
CancelOverlappedRequests(void)
{
	HRESULT hr;

	hr = m_pWvSrq->CancelOverlappedRequests();
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDSharedReceiveQueue::
GetOverlappedResult(OVERLAPPED *pOverlapped, BOOL bWait)
{
	DWORD bytes;
	HRESULT hr;

	hr = m_pWvSrq->GetOverlappedResult(pOverlapped, &bytes, bWait);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDSharedReceiveQueue::
GetNotifyAffinity(USHORT *pGroup, KAFFINITY* pAffinity)
{
	return ND_NOT_SUPPORTED;
}

STDMETHODIMP CNDSharedReceiveQueue::
Modify(DWORD queueDepth, DWORD notifyThreshold)
{
	HRESULT hr;

	hr = m_pWvSrq->Modify(queueDepth, notifyThreshold);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDSharedReceiveQueue::
Notify(OVERLAPPED* pOverlapped)
{
	HRESULT hr;

	hr = m_pWvSrq->Notify(pOverlapped);
	return NDConvertWVStatus(hr);
}

#ifdef _WIN64

C_ASSERT(sizeof(WV_SGE) == sizeof(ND_SGE));
C_ASSERT(FIELD_OFFSET(WV_SGE, pAddress)   == FIELD_OFFSET(ND_SGE, Buffer));
C_ASSERT(RTL_FIELD_SIZE(WV_SGE, pAddress) == RTL_FIELD_SIZE(ND_SGE, Buffer));
C_ASSERT(FIELD_OFFSET(WV_SGE, Length)     == FIELD_OFFSET(ND_SGE, BufferLength));
C_ASSERT(RTL_FIELD_SIZE(WV_SGE, Length)   == RTL_FIELD_SIZE(ND_SGE, BufferLength));
C_ASSERT(FIELD_OFFSET(WV_SGE, Lkey)       == FIELD_OFFSET(ND_SGE, MemoryRegionToken));
C_ASSERT(RTL_FIELD_SIZE(WV_SGE, Lkey)     == RTL_FIELD_SIZE(ND_SGE, MemoryRegionToken));

STDMETHODIMP CNDSharedReceiveQueue::
Receive(VOID* requestContext, const ND_SGE* pSge, DWORD nSge)
{
	HRESULT hr;

	hr = m_pWvSrq->PostReceive((UINT64) (ULONG_PTR) requestContext, (WV_SGE *) pSge, nSge);
	return NDConvertWVStatus(hr);
}

#else

STDMETHODIMP CNDSharedReceiveQueue::
Receive(VOID* requestContext, const ND_SGE* pSge, DWORD nSge)
{
	WV_SGE sgl[4];
	WV_SGE *sge;
	DWORD i;
	HRESULT hr;

	if (nSge > _countof(sgl)) {
		sge = new WV_SGE[nSge];
		if (sge == NULL) {
			return ND_NO_MEMORY;
		}
	} else {
		sge = sgl;
	}

	for (i = 0; i < nSge; i++) {
		sge->pAddress = pSge[i].Buffer;
		sge->Length = pSge[i].BufferLength;
		sge->Lkey = pSge[i].MemoryRegionToken;
	}

	hr = m_pWvSrq->PostReceive((UINT64) (ULONG_PTR) requestContext, sge, nSge);
	if (nSge > _countof(sgl)) {
		delete[] sge;
	}
	return NDConvertWVStatus(hr);
}

#endif