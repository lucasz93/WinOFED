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

#include "nd_connect.h"
#include "nd_ep.h"
#include "nd_qp.h"
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

	hr = m_pAdapter->m_pProvider->m_pWvProvider->CreateConnectEndpoint(&ep);
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
GetOverlappedResult(OVERLAPPED *pOverlapped, BOOL bWait)
{
	DWORD bytes;
	HRESULT hr;

	hr = m_pWvConnEp->GetOverlappedResult(pOverlapped, &bytes, bWait);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
ConnectQp(IUnknown* pQueuePair, BOOL SharedAddress,
		  const struct sockaddr* pSrcAddress, SIZE_T cbSrcAddress,
		  const struct sockaddr* pDestAddress, SIZE_T cbDestAddress,
		  DWORD inboundReadLimit, DWORD outboundReadLimit,
		  const VOID* pPrivateData, DWORD cbPrivateData, OVERLAPPED* pOverlapped)
{
	CNDQueuePair *qp = (CNDQueuePair *) pQueuePair;
	WV_CONNECT_PARAM attr;
	IBAT_PATH_BLOB path;
	HRESULT hr;

	if (m_Connects++ > 0) {
		hr = Init();
		if (FAILED(hr)) {
			goto out;
		}
	}

	hr = IBAT::ResolvePath(pSrcAddress, pDestAddress, &path, INFINITE);
	if (FAILED(hr)) {
		goto out;
	}

	if (SharedAddress) {
		//hr = m_pWvConnEp->BindAddress((SOCKADDR *) pSrcAddress);
		if (FAILED(hr)) {
			goto out;
		}
	} else {
		hr = m_pWvConnEp->BindAddress((SOCKADDR *) pSrcAddress);
		if (FAILED(hr)) {
			goto out;
		}
	}

	hr = m_pWvConnEp->Modify(WV_EP_OPTION_ROUTE, &path, sizeof path);
	if (FAILED(hr)) {
		goto out;
	}

	RtlZeroMemory(&attr, sizeof attr);
	if ((attr.DataLength = cbPrivateData)) {
		RtlCopyMemory(attr.Data, pPrivateData, cbPrivateData);
	}
	attr.ResponderResources = inboundReadLimit;
	attr.InitiatorDepth = outboundReadLimit;
	attr.RetryCount = 7;

	hr = m_pWvConnEp->Connect(qp->m_pWvQp, pDestAddress, &attr, pOverlapped);
out:
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
Connect(IUnknown* pQueuePair,
		const struct sockaddr* pSrcAddress, SIZE_T cbSrcAddress,
		const struct sockaddr* pDestAddress, SIZE_T cbDestAddress,
		DWORD inboundReadLimit, DWORD outboundReadLimit,
		const VOID* pPrivateData, DWORD cbPrivateData, OVERLAPPED* pOverlapped)
{
	return ConnectQp(pQueuePair, FALSE, pSrcAddress, cbSrcAddress,
					 pDestAddress, cbDestAddress, inboundReadLimit,
					 outboundReadLimit, pPrivateData, cbPrivateData, pOverlapped);
}

STDMETHODIMP CNDConnector::
ConnectSharedEndpoint(IUnknown* pQueuePair, IUnknown* pSharedEndpoint,
					  const struct sockaddr* pDestAddress, SIZE_T cbDestAddress,
					  DWORD inboundReadLimit, DWORD outboundReadLimit,
					  const VOID* pPrivateData, DWORD cbPrivateData,
					  OVERLAPPED* pOverlapped)
{
	CNDSharedEndpoint *sep = (CNDSharedEndpoint *) pSharedEndpoint;

	return ConnectQp(pQueuePair, TRUE, (SOCKADDR *) &sep->m_Address, sep->m_AddressSize,
					 pDestAddress, cbDestAddress, inboundReadLimit,
					 outboundReadLimit, pPrivateData, cbPrivateData, pOverlapped);
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
Accept(IUnknown* pQueuePair, DWORD inboundReadLimit, DWORD outboundReadLimit,
	   const VOID* pPrivateData, DWORD cbPrivateData, OVERLAPPED* pOverlapped)
{
	CNDQueuePair *qp = (CNDQueuePair *) pQueuePair;
	WV_CONNECT_PARAM attr;
	HRESULT hr;

	RtlZeroMemory(&attr, sizeof attr);
	if ((attr.DataLength = cbPrivateData)) {
		RtlCopyMemory(attr.Data, pPrivateData, cbPrivateData);
	}
	attr.ResponderResources = inboundReadLimit;
	attr.InitiatorDepth = outboundReadLimit;

	hr = m_pWvConnEp->Accept(qp->m_pWvQp, &attr, pOverlapped);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
Reject(const void* pPrivateData, DWORD cbPrivateData)
{
	HRESULT hr;

	hr = m_pWvConnEp->Reject(pPrivateData, cbPrivateData);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
GetConnectionData(DWORD* pInboundReadLimit, DWORD* pOutboundReadLimit,
				  void* pPrivateData, DWORD* pcbPrivateData)
{
	WV_CONNECT_ATTRIBUTES attr;
	HRESULT hr;
	
	hr = m_pWvConnEp->Query(&attr);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	if (pInboundReadLimit) {
		*pInboundReadLimit = (DWORD) attr.Param.ResponderResources;
	}
	if (pOutboundReadLimit) {
		*pOutboundReadLimit = (DWORD) attr.Param.InitiatorDepth;
	}
	if (pcbPrivateData) {
		if (*pcbPrivateData < ND_PRIVATE_DATA_SIZE) {
			hr = ND_BUFFER_OVERFLOW;
		}

		RtlCopyMemory(pPrivateData, attr.Param.Data,
					  min(*pcbPrivateData, ND_PRIVATE_DATA_SIZE));
		*pcbPrivateData = ND_PRIVATE_DATA_SIZE;
	}

	return hr;
}

SIZE_T GetAddressSize(WV_SOCKADDR *addr)
{
	return (addr->Sa.sa_family == AF_INET) ? sizeof(addr->Sin) : sizeof(addr->Sin6);
}

STDMETHODIMP CNDConnector::
GetLocalAddress(struct sockaddr* pAddress, SIZE_T* pcbAddress)
{
	WV_CONNECT_ATTRIBUTES attr;
	HRESULT hr;
	SIZE_T size;
	
	hr = m_pWvConnEp->Query(&attr);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	size = GetAddressSize(&attr.LocalAddress);
	if (*pcbAddress >= size) {
		RtlCopyMemory(pAddress, &attr.LocalAddress, size);
	} else {
		hr = ND_BUFFER_OVERFLOW;
	}

	*pcbAddress = size;
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDConnector::
GetPeerAddress(struct sockaddr* pAddress, SIZE_T* pcbAddress)
{
	WV_CONNECT_ATTRIBUTES attr;
	HRESULT hr;
	SIZE_T size;
	
	hr = m_pWvConnEp->Query(&attr);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	size = GetAddressSize(&attr.PeerAddress);
	if (*pcbAddress >= size) {
		RtlCopyMemory(pAddress, &attr.PeerAddress, size);
	} else {
		hr = ND_BUFFER_OVERFLOW;
	}

	*pcbAddress = size;
	return hr;
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
