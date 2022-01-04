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

#pragma once

#ifndef _ND_CONNECTOR_H_
#define _ND_CONNECTOR_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"
#include "nd_adapter.h"


#define ND_PRIVATE_DATA_SIZE	56


class CNDConnector : public INDConnector, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDOverlapped methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped, BOOL bWait);

	// INDConnector methods
	STDMETHODIMP Connect(IUnknown* pQueuePair,
						 const struct sockaddr* pSrcAddress, SIZE_T cbSrcAddress,
						 const struct sockaddr* pDestAddress, SIZE_T cbDestAddress,
						 DWORD inboundReadLimit, DWORD outboundReadLimit,
						 const VOID* pPrivateData, DWORD cbPrivateData, OVERLAPPED* pOverlapped);
	STDMETHODIMP ConnectSharedEndpoint(IUnknown* pQueuePair, IUnknown* pSharedEndpoint,
									   const struct sockaddr* pDestAddress, SIZE_T cbDestAddress,
									   DWORD inboundReadLimit, DWORD outboundReadLimit,
									   const VOID* pPrivateData, DWORD cbPrivateData,
									   OVERLAPPED* pOverlapped);
	STDMETHODIMP CompleteConnect(OVERLAPPED* pOverlapped);
	STDMETHODIMP Accept(IUnknown* pQueuePair, DWORD inboundReadLimit, DWORD outboundReadLimit,
						const VOID* pPrivateData, DWORD cbPrivateData, OVERLAPPED* pOverlapped);
	STDMETHODIMP Reject(const VOID* pPrivateData, DWORD cbPrivateData);
	STDMETHODIMP GetConnectionData(DWORD* pInboundReadLimit, DWORD* pOutboundReadLimit,
								   VOID* pPrivateData, DWORD* pcbPrivateData);
	STDMETHODIMP GetLocalAddress(struct sockaddr* pAddress, SIZE_T* pcbAddress);
	STDMETHODIMP GetPeerAddress(struct sockaddr* pAddress, SIZE_T* pcbAddress);
	STDMETHODIMP NotifyDisconnect(OVERLAPPED* pOverlapped);
	STDMETHODIMP Disconnect(OVERLAPPED* pOverlapped);

	CNDConnector(CNDAdapter *pAdapter);
	~CNDConnector();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter, VOID** ppConnector)
	{
		HRESULT hr;
		CNDConnector *conn;

		conn = new CNDConnector(pAdapter);
		if (conn == NULL) {
			hr = ND_NO_MEMORY;
			goto err1;
		}

		hr = conn->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		*ppConnector = conn;
		return ND_SUCCESS;

	err2:
		conn->Release();
	err1:
		*ppConnector = NULL;
		return hr;
	}

	IWVConnectEndpoint	*m_pWvConnEp;
	CNDAdapter			*m_pAdapter;

protected:
	STDMETHODIMP		Init();
	STDMETHODIMP		ConnectQp(IUnknown* pQueuePair, BOOL SharedAddress,
								  const struct sockaddr* pSrcAddress, SIZE_T cbSrcAddress,
								  const struct sockaddr* pDestAddress, SIZE_T cbDestAddress,
								  DWORD inboundReadLimit, DWORD outboundReadLimit,
								  const VOID* pPrivateData, DWORD cbPrivateData,
								  OVERLAPPED* pOverlapped);
	int					m_Connects;
};

SIZE_T GetAddressSize(WV_SOCKADDR *addr);

#endif // _ND_CONNECTOR_H_
