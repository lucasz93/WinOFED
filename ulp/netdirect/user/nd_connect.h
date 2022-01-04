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
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped,
									 SIZE_T *pNumberOfBytesTransferred, BOOL bWait);

	// INDConnector methods
	STDMETHODIMP CreateEndpoint(INDCompletionQueue* pInboundCq,
								INDCompletionQueue* pOutboundCq,
								SIZE_T nInboundEntries, SIZE_T nOutboundEntries,
								SIZE_T nInboundSge, SIZE_T nOutboundSge,
								SIZE_T InboundReadLimit, SIZE_T OutboundReadLimit,
								SIZE_T* pMaxInlineData, INDEndpoint** ppEndpoint);
	STDMETHODIMP Connect(INDEndpoint* pEndpoint, 
						 const struct sockaddr* pAddress, SIZE_T AddressLength,
						 INT Protocol, USHORT LocalPort,
						 const void* pPrivateData, SIZE_T PrivateDataLength,
						 OVERLAPPED* pOverlapped);
	STDMETHODIMP CompleteConnect(OVERLAPPED* pOverlapped);
	STDMETHODIMP Accept(INDEndpoint* pEndpoint,
						const void* pPrivateData, SIZE_T PrivateDataLength,
						OVERLAPPED* pOverlapped);
	STDMETHODIMP Reject(const void* pPrivateData, SIZE_T PrivateDataLength);
	STDMETHODIMP GetConnectionData(SIZE_T* pInboundReadLimit,
								   SIZE_T* pOutboundReadLimit,
								   void* pPrivateData, SIZE_T* pPrivateDataLength);
	STDMETHODIMP GetLocalAddress(struct sockaddr* pAddress, SIZE_T* pAddressLength);
	STDMETHODIMP GetPeerAddress(struct sockaddr* pAddress, SIZE_T* pAddressLength);
	STDMETHODIMP NotifyDisconnect(OVERLAPPED* pOverlapped);
	STDMETHODIMP Disconnect(OVERLAPPED* pOverlapped);

	CNDConnector(CNDAdapter *pAdapter);
	~CNDConnector();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter, INDConnector** ppConnector)
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
	int					m_Connects;
};

#endif // _ND_CONNECTOR_H_
