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

#include "nd_qp.h"
#include "nd_adapter.h"
#include "nd_connect.h"
#include "nd_cq.h"
#include "nd_mw.h"
#include <netinet/in.h>


CNDQueuePair::CNDQueuePair(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
	m_pWvQp = NULL;
	m_pReceiveCq = NULL;
	m_pSendCq = NULL;
}

STDMETHODIMP CNDQueuePair::
Init(CNDCompletionQueue* pReceiveCompletionQueue,
	 CNDCompletionQueue* pInitiatorCompletionQueue,
	 CNDSharedReceiveQueue *pSharedReceiveQueue,
	 VOID* context, DWORD receiveQueueDepth, DWORD initiatorQueueDepth,
	 DWORD maxReceiveRequestSGE, DWORD maxInitiatorRequestSGE)
{
	WV_QP_CREATE create;
	WV_QP_ATTRIBUTES attr;
	DWORD opts;
	HRESULT hr;

	RtlZeroMemory(&create, sizeof create);
	m_pReceiveCq = pReceiveCompletionQueue;
	m_pSendCq = pInitiatorCompletionQueue;
	m_pReceiveCq->AddRef();
	m_pSendCq->AddRef();
	if ((m_pSrq = pSharedReceiveQueue)) {
		m_pSrq->AddRef();
		create.pSharedReceiveQueue = m_pSrq->m_pWvSrq;
	}

	create.pSendCq = m_pSendCq->m_pWvCq;
	create.pReceiveCq = m_pReceiveCq->m_pWvCq;
	create.Context = context;
	create.SendDepth = initiatorQueueDepth;
	create.SendSge = maxInitiatorRequestSGE;
	create.ReceiveDepth = receiveQueueDepth;
	create.ReceiveSge = maxReceiveRequestSGE;
	create.MaxInlineSend = m_pAdapter->m_MaxInlineSend;
	create.QpType = WvQpTypeRc;
	
	hr = m_pAdapter->m_pWvPd->CreateConnectQueuePair(&create, &m_pWvQp);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	opts = WV_QP_ATTR_STATE | WV_QP_ATTR_PORT_NUMBER | WV_QP_ATTR_PKEY_INDEX;
	attr.QpState = WvQpStateInit;
	attr.PkeyIndex = 0;
	attr.AddressVector.PortNumber = 1;

	hr = m_pWvQp->Modify(&attr, opts, NULL);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	return ND_SUCCESS;
}

CNDQueuePair::~CNDQueuePair()
{
	if (m_pWvQp != NULL) {
		m_pWvQp->Release();
	}
	if (m_pReceiveCq != NULL) {
		m_pReceiveCq->Release();
	}
	if (m_pSendCq != NULL) {
		m_pSendCq->Release();
	}
	if (m_pSrq != NULL) {
		m_pSrq->Release();
	}
	m_pAdapter->Release();
}

STDMETHODIMP CNDQueuePair::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDQueuePair) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDQueuePair::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDQueuePair::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDQueuePair::
Flush(void)
{
	WV_QP_ATTRIBUTES attr;
	HRESULT hr;

	attr.QpState = WvQpStateError;
	hr = m_pWvQp->Modify(&attr, WV_QP_ATTR_STATE, NULL);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP_(DWORD) CNDQueuePair::
ConvertSendFlags(DWORD Flags)
{
	DWORD opts = 0;

	if (!(Flags & ND_OP_FLAG_SILENT_SUCCESS)) {
		opts |= WV_SEND_SIGNALED;
	}
	if (Flags & ND_OP_FLAG_READ_FENCE) {
		opts |= WV_SEND_FENCE;
	}
	if (Flags & ND_OP_FLAG_SEND_AND_SOLICIT_EVENT) {
		opts |= WV_SEND_SOLICITED;
	}
	if (Flags & ND_OP_FLAG_INLINE) {
		opts |= WV_SEND_INLINE;
	}
	return opts;
}

STDMETHODIMP CNDQueuePair::
Send(VOID* requestContext, const ND_SGE* pSge, DWORD nSge, DWORD flags)
{
	HRESULT hr;

	hr = m_pWvQp->Send((UINT64) (ULONG_PTR) requestContext, (WV_SGE *) pSge, nSge,
					   ConvertSendFlags(flags), 0);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDQueuePair::
Receive(VOID* requestContext, const ND_SGE* pSge, DWORD nSge)
{
	HRESULT hr;

	hr = m_pWvQp->PostReceive((UINT64) (ULONG_PTR) requestContext, (WV_SGE *) pSge, nSge);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDQueuePair::
Bind(VOID* requestContext, IUnknown* pMemoryRegion,
	 IUnknown* pMemoryWindow, const VOID* pBuffer, SIZE_T cbBuffer,
	 DWORD flags)
{
	CNDMemoryRegion *mr = (CNDMemoryRegion *) pMemoryRegion;
	CNDMemoryWindow *mw = (CNDMemoryWindow *) pMemoryWindow;
	HRESULT hr;

	if (mw->m_pMr != NULL) {
		mw->m_pMr->Release();
	}
	mw->m_pMr = mr;
	mr->AddRef();
	hr = m_pWvQp->Write((UINT64) (ULONG_PTR) requestContext, NULL, 0,
						ConvertSendFlags(flags), 0, 0, 0);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDQueuePair::
Invalidate(VOID* requestContext, IUnknown* pMemoryWindow, DWORD flags)
{
	CNDMemoryWindow *mw = (CNDMemoryWindow *) pMemoryWindow;
	HRESULT hr;

	mw->m_pMr->Release();
	mw->m_pMr = NULL;
	hr = m_pWvQp->Write((UINT64) (ULONG_PTR) requestContext, NULL, 0,
						ConvertSendFlags(flags), 0, 0, 0);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDQueuePair::
Read(VOID* requestContext, const ND_SGE* pSge, DWORD nSge,
	 UINT64 remoteAddress, UINT32 remoteToken, DWORD flags)
{
	DWORD opts;
	HRESULT hr;

	hr = m_pWvQp->Read((UINT64) (ULONG_PTR) requestContext, (WV_SGE *) pSge, nSge,
					   ConvertSendFlags(flags), htonll(remoteAddress), remoteToken);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDQueuePair::
Write(VOID* requestContext, const ND_SGE* pSge, DWORD nSge,
	  UINT64 remoteAddress, UINT32 remoteToken, DWORD flags)
{
	DWORD opts;
	HRESULT hr;

	hr = m_pWvQp->Write((UINT64) (ULONG_PTR) requestContext, (WV_SGE *) pSge, nSge,
					    ConvertSendFlags(flags), 0, htonll(remoteAddress), remoteToken);
	return NDConvertWVStatus(hr);
}
