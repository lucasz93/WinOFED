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

#ifndef _ND_LISTEN_H_
#define _ND_LISTEN_H_

#include <initguid.h>
#include <ndspi.h>
#include "nd_base.h"
#include "nd_adapter.h"

class CNDListen : public INDListen, public CNDBase
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

	// INDListen methods
	STDMETHODIMP GetConnectionRequest(INDConnector* pConnector,
									  OVERLAPPED* pOverlapped);

	CNDListen(CNDAdapter *pAdapter);
	~CNDListen();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDAdapter *pAdapter, SIZE_T Backlog, INT Protocol, USHORT Port,
				   USHORT* pAssignedPort, INDListen** ppListen)
	{
		HRESULT hr;
		CNDListen *listener;

		listener = new CNDListen(pAdapter);
		if (listener == NULL) {
			hr = ND_NO_MEMORY;
			goto err1;
		}

		hr = listener->Init(Backlog, Protocol, &Port);
		if (FAILED(hr)) {
			goto err2;
		}

		if (pAssignedPort) {
			*pAssignedPort = Port;
		}
		*ppListen = listener;
		return ND_SUCCESS;

	err2:
		listener->Release();
	err1:
		*ppListen = NULL;
		return hr;
	}

protected:
	CNDAdapter			*m_pAdapter;
	IWVConnectEndpoint	*m_pWvConnEp;

	STDMETHODIMP		Init(SIZE_T Backlog, INT Protocol, USHORT *pPort);
};

#endif // _ND_LISTEN_H_
