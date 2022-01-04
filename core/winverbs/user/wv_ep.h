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

#ifndef _WV_EP_H_
#define _WV_EP_H_

#include <rdma\winverbs.h>
#include "wv_provider.h"
#include "wv_base.h"
#include "wv_qp.h"

class CWVConnectEndpoint : IWVConnectEndpoint, public CWVBase
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

	// IWVEndpoint methods
	STDMETHODIMP Modify(DWORD Option, const VOID* pOptionData, SIZE_T OptionLength);
	STDMETHODIMP BindAddress(SOCKADDR* pAddress);
	STDMETHODIMP Listen(SIZE_T Backlog);
	STDMETHODIMP Reject(const VOID* pUserData, SIZE_T UserDataLength);

	// IWVConnectEndpoint methods
	STDMETHODIMP GetRequest(IWVConnectEndpoint* pEndpoint, OVERLAPPED* pOverlapped);
	STDMETHODIMP Connect(IWVConnectQueuePair* pQp, const SOCKADDR* pAddress,
						 WV_CONNECT_PARAM* pParam, OVERLAPPED* pOverlapped);
	STDMETHODIMP Accept(IWVConnectQueuePair* pQp, WV_CONNECT_PARAM* pParam,
						OVERLAPPED* pOverlapped);
	STDMETHODIMP Disconnect(OVERLAPPED* pOverlapped);
	STDMETHODIMP NotifyDisconnect(OVERLAPPED* pOverlapped);
	STDMETHODIMP Query(WV_CONNECT_ATTRIBUTES* pAttributes);

	CWVConnectEndpoint(CWVProvider *pProvider);
	~CWVConnectEndpoint();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVProvider *pProvider, IWVConnectEndpoint** ppConnectEndpoint)
	{
		HRESULT hr;
		CWVConnectEndpoint *ep;

		ep = new CWVConnectEndpoint(pProvider);
		if (ep == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = ep->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = ep->Allocate();
		if (FAILED(hr)) {
			goto err2;
		}

		*ppConnectEndpoint = ep;
		return WV_SUCCESS;

	err2:
		ep->Release();
	err1:
		*ppConnectEndpoint = NULL;
		return hr;
	}

	CWVProvider		*m_pProvider;

protected:
	SOCKET				m_Socket;
	CWVConnectQueuePair	*m_pQp;

	STDMETHODIMP Allocate();
};

class CWVDatagramEndpoint : IWVDatagramEndpoint, public CWVBase
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

	// IWVEndpoint methods
	STDMETHODIMP Modify(DWORD Option, const VOID* pOptionData, SIZE_T OptionLength);
	STDMETHODIMP BindAddress(SOCKADDR* pAddress);
	STDMETHODIMP Listen(SIZE_T Backlog);
	STDMETHODIMP Reject(const VOID* pUserData, SIZE_T UserDataLength);

	// IWVDatagramEndpoint methods
	STDMETHODIMP GetRequest(IWVDatagramEndpoint* pEndpoint, OVERLAPPED* pOverlapped);
	STDMETHODIMP Lookup(const SOCKADDR* pAddress, const VOID* pUserData,
						SIZE_T UserDataLength, OVERLAPPED* pOverlapped);
	STDMETHODIMP Accept(WV_DATAGRAM_PARAM* pParam, OVERLAPPED* pOverlapped);
	STDMETHODIMP JoinMulticast(const SOCKADDR* pAddress,
							   OVERLAPPED* pOverlapped);
	STDMETHODIMP LeaveMulticast(const SOCKADDR* pAddress,
								OVERLAPPED* pOverlapped);
	STDMETHODIMP Query(WV_DATAGRAM_ATTRIBUTES* pAttributes);

	CWVDatagramEndpoint(CWVProvider *pProvider);
	~CWVDatagramEndpoint();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVProvider *pProvider, IWVDatagramEndpoint** ppDatagramEndpoint)
	{
		HRESULT hr;
		CWVDatagramEndpoint *ep;

		ep = new CWVDatagramEndpoint(pProvider);
		if (ep == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = ep->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = ep->Allocate();
		if (FAILED(hr)) {
			goto err2;
		}

		*ppDatagramEndpoint = ep;
		return WV_SUCCESS;

	err2:
		ep->Release();
	err1:
		*ppDatagramEndpoint = NULL;
		return hr;
	}

	CWVProvider		*m_pProvider;

protected:
	STDMETHODIMP Allocate();
};

#endif // _WV_EP_H_
