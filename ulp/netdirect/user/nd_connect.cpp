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

#include "nd_connect.h"
#include "nd_ep.h"
#include <iba/ibat.h>


CNDConnector::CNDConnector(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
	m_pWvConnEp = NULL;
	m_Connects = 0;
}

STDMETHODIMP CNDConnector::
Init(void)
{
	IWVConnectEndpoint *ep;
	HRESULT hr;

	hr = m_pAdapter->m_pWvProvider->CreateConnectEndpoint(&ep);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	if (m_pWvConnEp != NULL) {
		m_pWvConnEp->Release();
	}

	m_pWvConnEp = ep;
	return ND_SUCCESS;
}

CNDConnector::~CNDConnector()
{
	if (m_pWvConnEp != NULL) {
		m_pWvConnEp->Release();
	}
	m_pAdapter->Release();
}

STDMETHODIMP CNDConnector::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDConnector) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDConnector::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDConnector::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDConnector::
CancelOverlappedRequests(void)
{
	HRESULT hr;

	hr = m_pWvConnEp->CancelOverlappedRequests();
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					SIZE_T *pNumberOfBytesTransferred, BOOL bWait)
{
	HRESULT hr;

	hr = m_pWvConnEp->GetOverlappedResult(pOverlapped,
										  (DWORD *) pNumberOfBytesTransferred,
										  bWait);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
CreateEndpoint(INDCompletionQueue* pInboundCq, INDCompletionQueue* pOutboundCq,
			   SIZE_T nInboundEntries, SIZE_T nOutboundEntries,
			   SIZE_T nInboundSge, SIZE_T nOutboundSge,
			   SIZE_T InboundReadLimit, SIZE_T OutboundReadLimit,
			   SIZE_T* pMaxInlineData, INDEndpoint** ppEndpoint)
{
	CNDCompletionQueue *incq = (CNDCompletionQueue *) pInboundCq;
	CNDCompletionQueue *outcq = (CNDCompletionQueue *) pOutboundCq;

	return CNDEndpoint::CreateInstance(this, incq, outcq,
									   nInboundEntries, nOutboundEntries,
									   nInboundSge, nOutboundSge,
									   InboundReadLimit, OutboundReadLimit,
									   pMaxInlineData, ppEndpoint);
}

STDMETHODIMP CNDConnector::
Connect(INDEndpoint* pEndpoint,
		const struct sockaddr* pAddress, SIZE_T AddressLength,
		INT Protocol, USHORT LocalPort,
		const void* pPrivateData, SIZE_T PrivateDataLength,
		OVERLAPPED* pOverlapped)
{
	CNDEndpoint *ep = (CNDEndpoint *) pEndpoint;
	WV_SOCKADDR addr;
	WV_CONNECT_PARAM attr;
	IBAT_PATH_BLOB path;
	HRESULT hr;

	if (m_Connects++ > 0) {
		hr = Init();
		if (FAILED(hr)) {
			goto out;
		}
	}

	RtlCopyMemory(&addr, &m_pAdapter->m_Address, AddressLength);
	if (addr.Sa.sa_family == AF_INET) {
		addr.Sin.sin_port = LocalPort;
	} else {
		addr.Sin6.sin6_port = LocalPort;
	}

    hr = IBAT::QueryPath(&addr.Sa, pAddress, &path);
	if (FAILED(hr)) {
		goto out;
	}

	hr = m_pWvConnEp->BindAddress(&addr.Sa);
	if (FAILED(hr)) {
		goto out;
	}

	hr = m_pWvConnEp->Modify(WV_EP_OPTION_ROUTE, &path, sizeof path);
	if (FAILED(hr)) {
		goto out;
	}

	RtlZeroMemory(&attr, sizeof attr);
	if ((attr.DataLength = PrivateDataLength)) {
		RtlCopyMemory(attr.Data, pPrivateData, PrivateDataLength);
	}
	attr.ResponderResources = ep->m_ResponderResources;
	attr.InitiatorDepth = ep->m_InitiatorDepth;
	attr.RetryCount = 7;

	hr = m_pWvConnEp->Connect(ep->m_pWvQp, pAddress, &attr, pOverlapped);
out:
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
CompleteConnect(OVERLAPPED* pOverlapped)
{
	WV_CONNECT_PARAM attr;
	HRESULT hr;

	RtlZeroMemory(&attr, sizeof attr);
	hr = m_pWvConnEp->Accept(NULL, &attr, pOverlapped);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
Accept(INDEndpoint* pEndpoint,
	   const void* pPrivateData, SIZE_T PrivateDataLength,
	   OVERLAPPED* pOverlapped)
{
	CNDEndpoint *ep = (CNDEndpoint *) pEndpoint;
	WV_CONNECT_PARAM attr;
	HRESULT hr;

	RtlZeroMemory(&attr, sizeof attr);
	if ((attr.DataLength = PrivateDataLength)) {
		RtlCopyMemory(attr.Data, pPrivateData, PrivateDataLength);
	}
	attr.ResponderResources = ep->m_ResponderResources;
	attr.InitiatorDepth = ep->m_InitiatorDepth;

	hr = m_pWvConnEp->Accept(ep->m_pWvQp, &attr, pOverlapped);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
Reject(const void* pPrivateData, SIZE_T PrivateDataLength)
{
	HRESULT hr;

	hr = m_pWvConnEp->Reject(pPrivateData, PrivateDataLength);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
GetConnectionData(SIZE_T* pInboundReadLimit, SIZE_T* pOutboundReadLimit,
				  void* pPrivateData, SIZE_T* pPrivateDataLength)
{
	WV_CONNECT_ATTRIBUTES attr;
	HRESULT hr;
	
	hr = m_pWvConnEp->Query(&attr);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	if (pInboundReadLimit) {
		*pInboundReadLimit = attr.Param.ResponderResources;
	}
	if (pOutboundReadLimit) {
		*pOutboundReadLimit = attr.Param.InitiatorDepth;
	}
	if (pPrivateDataLength) {
		if (*pPrivateDataLength < ND_PRIVATE_DATA_SIZE) {
			hr = ND_BUFFER_OVERFLOW;
		}

		RtlCopyMemory(pPrivateData, attr.Param.Data,
					  min(*pPrivateDataLength, ND_PRIVATE_DATA_SIZE));
		*pPrivateDataLength = ND_PRIVATE_DATA_SIZE;
	}

	return hr;
}

static SIZE_T GetAddressSize(WV_SOCKADDR *addr)
{
	return (addr->Sa.sa_family == AF_INET) ? sizeof(addr->Sin) : sizeof(addr->Sin6);
}

STDMETHODIMP CNDConnector::
GetLocalAddress(struct sockaddr* pAddress, SIZE_T* pAddressLength)
{
	WV_CONNECT_ATTRIBUTES attr;
	HRESULT hr;
	
	hr = m_pWvConnEp->Query(&attr);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	if (*pAddressLength < GetAddressSize(&attr.LocalAddress)) {
		hr = ND_BUFFER_OVERFLOW;
		goto out;
	}

	RtlCopyMemory(pAddress, &attr.LocalAddress, GetAddressSize(&attr.LocalAddress));
out:
	*pAddressLength = GetAddressSize(&attr.LocalAddress);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
GetPeerAddress(struct sockaddr* pAddress, SIZE_T* pAddressLength)
{
	WV_CONNECT_ATTRIBUTES attr;
	HRESULT hr;
	
	hr = m_pWvConnEp->Query(&attr);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	if (*pAddressLength < GetAddressSize(&attr.PeerAddress)) {
		hr = ND_BUFFER_OVERFLOW;
		goto out;
	}

	RtlCopyMemory(pAddress, &attr.PeerAddress, GetAddressSize(&attr.PeerAddress));
out:
	*pAddressLength = GetAddressSize(&attr.PeerAddress);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
NotifyDisconnect(OVERLAPPED* pOverlapped)
{
	HRESULT hr;

	hr = m_pWvConnEp->NotifyDisconnect(pOverlapped);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
Disconnect(OVERLAPPED* pOverlapped)
{
	HRESULT hr;

	hr = m_pWvConnEp->Disconnect(pOverlapped);
	return NDConvertWVStatus(hr);
}
