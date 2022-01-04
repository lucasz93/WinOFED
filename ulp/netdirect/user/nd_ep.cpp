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

#include "nd_ep.h"
#include "nd_adapter.h"
#include "nd_connect.h"
#include "nd_cq.h"
#include "nd_mw.h"
#include <netinet/in.h>


CNDEndpoint::CNDEndpoint(CNDConnector *pConnector)
{
	pConnector->AddRef();
	m_pConnector = pConnector;
	m_pWvQp = NULL;
	m_pInboundCq = NULL;
	m_pOutboundCq = NULL;
}

STDMETHODIMP_(void) CNDEndpoint::
InitMaxInline(void)
{
	TCHAR val[16];
	DWORD ret;

	ret = GetEnvironmentVariable("IBNDPROV_MAX_INLINE_SIZE", val, 16);
	m_MaxInlineSend = (ret > 0 && ret <= 16) ? (SIZE_T) strtoul(val, NULL, 16) : 160;
}

STDMETHODIMP CNDEndpoint::
Init(CNDCompletionQueue* pInboundCq, CNDCompletionQueue* pOutboundCq,
	 SIZE_T nInboundEntries, SIZE_T nOutboundEntries,
	 SIZE_T nInboundSge, SIZE_T nOutboundSge,
	 SIZE_T InboundReadLimit, SIZE_T OutboundReadLimit,
	 SIZE_T* pMaxInlineData)
{
	WV_QP_CREATE create;
	WV_QP_ATTRIBUTES attr;
	WV_DEVICE_ADDRESS *addr;
	DWORD opts;
	HRESULT hr;

	m_pInboundCq = pInboundCq;
	m_pOutboundCq = pOutboundCq;
	m_pInboundCq->AddRef();
	m_pOutboundCq->AddRef();
	m_InitiatorDepth = OutboundReadLimit;
	m_ResponderResources = InboundReadLimit;
	InitMaxInline();

	RtlZeroMemory(&create, sizeof create);
	create.pSendCq = pOutboundCq->m_pWvCq;
	create.pReceiveCq = pInboundCq->m_pWvCq;
	create.Context = this;
	create.SendDepth = nOutboundEntries;
	create.SendSge = nOutboundSge;
	create.ReceiveDepth = nInboundEntries;
	create.ReceiveSge = nInboundSge;
	create.InitiatorDepth = OutboundReadLimit;
	create.ResponderResources = InboundReadLimit;
	create.MaxInlineSend = m_MaxInlineSend;
	create.QpType = WvQpTypeRc;
	
	hr = m_pConnector->m_pAdapter->m_pWvPd->CreateConnectQueuePair(&create, &m_pWvQp);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	opts = WV_QP_ATTR_STATE | WV_QP_ATTR_PORT_NUMBER | WV_QP_ATTR_PKEY_INDEX;
	attr.QpState = WvQpStateInit;
	addr = &m_pConnector->m_pAdapter->m_DevAddress;
	attr.AddressVector.PortNumber = addr->PortNumber;
	hr = m_pConnector->m_pAdapter->m_pWvDevice->FindPkey(addr->PortNumber, addr->Pkey,
														 &attr.PkeyIndex);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	hr = m_pWvQp->Modify(&attr, opts, NULL);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	if (pMaxInlineData) {
		*pMaxInlineData = m_MaxInlineSend;
	}
	return ND_SUCCESS;
}

CNDEndpoint::~CNDEndpoint()
{
	if (m_pWvQp != NULL) {
		m_pWvQp->Release();
	}
	if (m_pInboundCq != NULL) {
		m_pInboundCq->Release();
	}
	if (m_pOutboundCq != NULL) {
		m_pOutboundCq->Release();
	}
	m_pConnector->Release();
}

STDMETHODIMP CNDEndpoint::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDEndpoint) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDEndpoint::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDEndpoint::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDEndpoint::
Flush(void)
{
	return ND_SUCCESS;
}

STDMETHODIMP_(void) CNDEndpoint::
StartRequestBatch(void)
{
	// no-op
}

STDMETHODIMP_(void) CNDEndpoint::
SubmitRequestBatch(void)
{
	// no-op
}

STDMETHODIMP_(SIZE_T) CNDEndpoint::
ConvertSgl(const ND_SGE* pSgl, SIZE_T nSge, WV_SGE* pWvSgl)
{
	SIZE_T i, len = 0;

	for (i = 0; i < nSge; i++) {
		pWvSgl[i].pAddress = pSgl[i].pAddr;
		pWvSgl[i].Length = (UINT32) pSgl[i].Length;
		len += pWvSgl[i].Length;
		pWvSgl[i].Lkey = pSgl[i].hMr ? ((ND_MR *) pSgl[i].hMr)->Keys.Lkey : 0;
	}
	return len;
}

STDMETHODIMP_(DWORD) CNDEndpoint::
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
	return opts;
}

STDMETHODIMP_(DWORD) CNDEndpoint::
ConvertAccessFlags(DWORD Flags)
{
	DWORD opts = 0;

	if (!(Flags & ND_OP_FLAG_ALLOW_READ)) {
		opts |= WV_ACCESS_REMOTE_READ;
	}
	if (Flags & ND_OP_FLAG_ALLOW_WRITE) {
		opts |= WV_ACCESS_REMOTE_WRITE;
	}
	return opts;
}

STDMETHODIMP CNDEndpoint::
Send(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge, DWORD Flags)
{
	WV_SGE sgl[ND_MAX_SGE];
	DWORD opts;
	HRESULT hr;

	pResult->BytesTransferred = ConvertSgl(pSgl, nSge, sgl);
	opts = ConvertSendFlags(Flags) |
		   (pResult->BytesTransferred <= m_MaxInlineSend ? WV_SEND_INLINE : 0);
	hr = m_pWvQp->Send((UINT64) pResult, sgl, nSge, opts, 0);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDEndpoint::
SendAndInvalidate(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge,
				  const ND_MW_DESCRIPTOR* pRemoteMwDescriptor, DWORD Flags)
{
	HRESULT hr;
	
	hr = Send(pResult, pSgl, nSge, Flags);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDEndpoint::
Receive(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge)
{
	WV_SGE sgl[ND_MAX_SGE];
	HRESULT hr;

	ConvertSgl(pSgl, nSge, sgl);
	hr = m_pWvQp->PostReceive((UINT64) pResult, sgl, nSge);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDEndpoint::
Bind(ND_RESULT* pResult, ND_MR_HANDLE hMr, INDMemoryWindow* pMw,
	 const void* pBuffer, SIZE_T BufferSize, DWORD Flags,
	 ND_MW_DESCRIPTOR* pMwDescriptor)
{
	CNDMemoryWindow *mw = (CNDMemoryWindow *) pMw;
	ND_MR *mr = (ND_MR *) hMr;
	HRESULT hr;

	pResult->BytesTransferred = 0;
	pMwDescriptor->Base = htonll((UINT64) (ULONG_PTR) pBuffer);
	pMwDescriptor->Length = htonll(BufferSize);
	pMwDescriptor->Token = mr->Keys.Rkey;

	hr = m_pWvQp->Write((UINT64) pResult, NULL, 0, ConvertSendFlags(Flags),
						0, 0, 0);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDEndpoint::
Invalidate(ND_RESULT* pResult, INDMemoryWindow* pMw, DWORD Flags)
{
	HRESULT hr;

	pResult->BytesTransferred = 0;
	hr = m_pWvQp->Write((UINT64) pResult, NULL, 0, ConvertSendFlags(Flags),
						0, 0, 0);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDEndpoint::
Read(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge,
	 const ND_MW_DESCRIPTOR* pRemoteMwDescriptor, ULONGLONG Offset, DWORD Flags)
{
	WV_SGE sgl[ND_MAX_SGE];
	UINT64 addr;
	DWORD opts;
	HRESULT hr;

	pResult->BytesTransferred = ConvertSgl(pSgl, nSge, sgl);
	opts = ConvertSendFlags(Flags) |
		   (pResult->BytesTransferred ? 0 : WV_SEND_INLINE);
	addr = ntohll(pRemoteMwDescriptor->Base) + Offset;
	hr = m_pWvQp->Read((UINT64) pResult, sgl, nSge, opts,
					   htonll(addr), pRemoteMwDescriptor->Token);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDEndpoint::
Write(ND_RESULT* pResult, const ND_SGE* pSgl, SIZE_T nSge,
	  const ND_MW_DESCRIPTOR* pRemoteMwDescriptor, ULONGLONG Offset, DWORD Flags)
{
	WV_SGE sgl[ND_MAX_SGE];
	UINT64 addr;
	DWORD opts;
	HRESULT hr;

	pResult->BytesTransferred = ConvertSgl(pSgl, nSge, sgl);
	opts = ConvertSendFlags(Flags) |
		   (pResult->BytesTransferred <= m_MaxInlineSend ? WV_SEND_INLINE : 0);
	addr = ntohll(pRemoteMwDescriptor->Base) + Offset;
	hr = m_pWvQp->Write((UINT64) pResult, sgl, nSge, opts, 0,
						htonll(addr), pRemoteMwDescriptor->Token);
	return NDConvertWVStatus(hr);
}
