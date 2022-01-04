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

#include "nd_listen.h"
#include "nd_adapter.h"
#include "nd_connect.h"


CNDListen::CNDListen(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
	m_pWvConnEp = NULL;
}

CNDListen::~CNDListen()
{
	if (m_pWvConnEp != NULL) {
		m_pWvConnEp->Release();
	}
	m_pAdapter->Release();
}

STDMETHODIMP CNDListen::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDListen) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDListen::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDListen::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDListen::
CancelOverlappedRequests(void)
{
	HRESULT hr;

	hr = m_pWvConnEp->CancelOverlappedRequests();
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDListen::
GetOverlappedResult(OVERLAPPED *pOverlapped, BOOL bWait)
{
	DWORD bytes;
	HRESULT hr;

	hr = m_pWvConnEp->GetOverlappedResult(pOverlapped, &bytes, bWait);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDListen::
Listen(const struct sockaddr* pAddress, SIZE_T cbAddress, SIZE_T backlog)
{
	WV_SOCKADDR addr;
	HRESULT hr;

	hr = m_pAdapter->m_pProvider->m_pWvProvider->CreateConnectEndpoint(&m_pWvConnEp);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	hr = m_pWvConnEp->BindAddress((SOCKADDR *) pAddress);
	if (FAILED(hr)) {
		goto err;
	}

	hr = m_pWvConnEp->Listen(backlog);
	if (FAILED(hr)) {
		goto err;
	}

	return ND_SUCCESS;

err:
	m_pWvConnEp->Release();
	m_pWvConnEp = NULL;
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDListen::
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
		RtlCopyMemory(pAddress, &attr.LocalAddress.Sa, size);
	} else {
		hr = ND_BUFFER_OVERFLOW;
	}

	*pcbAddress = size;
	return hr;
}

STDMETHODIMP CNDListen::
GetConnectionRequest(IUnknown* pConnector, OVERLAPPED* pOverlapped)
{
	CNDConnector *conn = (CNDConnector *) pConnector;
	HRESULT hr;

	hr = m_pWvConnEp->GetRequest(conn->m_pWvConnEp, pOverlapped);
	return NDConvertWVStatus(hr);
}
