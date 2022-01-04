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

#include "nd_listen.h"
#include "nd_adapter.h"
#include "nd_connect.h"


CNDListen::CNDListen(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
	m_pWvConnEp = NULL;
}

STDMETHODIMP CNDListen::
Init(SIZE_T Backlog, INT Protocol, USHORT *pPort)
{
	WV_CONNECT_ATTRIBUTES attr;
	WV_SOCKADDR addr;
	HRESULT hr;

	/* All connection-oriented protocols map to IPPROTO_TCP */
	if (Protocol == IPPROTO_UDP) {
		return ND_NOT_SUPPORTED;
	}

	hr = m_pAdapter->m_pWvProvider->CreateConnectEndpoint(&m_pWvConnEp);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	if (m_pAdapter->m_Address.ss_family == AF_INET) {
		RtlCopyMemory(&addr.Sin, &m_pAdapter->m_Address, sizeof(addr.Sin));
		addr.Sin.sin_port = *pPort;
	} else {
		RtlCopyMemory(&addr.Sin6, &m_pAdapter->m_Address, sizeof(addr.Sin6));
		addr.Sin6.sin6_port = *pPort;
	}

	hr = m_pWvConnEp->BindAddress(&addr.Sa);
	if (FAILED(hr)) {
		goto err;
	}

	hr = m_pWvConnEp->Listen(Backlog);
	if (FAILED(hr)) {
		goto err;
	}

	if (*pPort == 0) {
		hr = m_pWvConnEp->Query(&attr);
		if (FAILED(hr)) {
			goto err;
		}
		*pPort = (addr.Sa.sa_family == AF_INET) ?
				 attr.LocalAddress.Sin.sin_port : attr.LocalAddress.Sin6.sin6_port;
	}

	return ND_SUCCESS;
err:
	m_pWvConnEp->Release();
	return NDConvertWVStatus(hr);
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
GetOverlappedResult(OVERLAPPED *pOverlapped,
					SIZE_T *pNumberOfBytesTransferred, BOOL bWait)
{
	HRESULT hr;

	hr = m_pWvConnEp->GetOverlappedResult(pOverlapped,
										  (DWORD *) pNumberOfBytesTransferred,
										  bWait);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDListen::
GetConnectionRequest(INDConnector* pConnector, OVERLAPPED* pOverlapped)
{
	CNDConnector *conn = (CNDConnector *) pConnector;
	HRESULT hr;

	hr = m_pWvConnEp->GetRequest(conn->m_pWvConnEp, pOverlapped);
	return NDConvertWVStatus(hr);
}
