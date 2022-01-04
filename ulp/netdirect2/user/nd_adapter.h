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

#ifndef _ND_ADAPTER_H_
#define _ND_ADAPTER_H_

#include <ndspi.h>
#include "nd_base.h"
#include "nd_provider.h"


class CNDAdapter : public INDAdapter, public CNDBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// INDAdapter methods
	STDMETHODIMP_(HANDLE) GetFileHandle();
	STDMETHODIMP Query(ND_ADAPTER_INFO* pInfo, SIZE_T* pcbInfo);
	STDMETHODIMP QueryAddressList(SOCKET_ADDRESS_LIST* pAddressList, SIZE_T* pcbAddressList);
	STDMETHODIMP CreateCompletionQueue(REFIID iid, DWORD queueDepth, USHORT group,
									   KAFFINITY affinity, VOID** ppCompletionQueue);
	STDMETHODIMP CreateMemoryRegion(REFIID iid, VOID** ppMemoryRegion);
	STDMETHODIMP CreateMemoryWindow(REFIID iid, VOID** ppMemoryWindow);
	STDMETHODIMP CreateSharedReceiveQueue(REFIID iid, DWORD queueDepth, DWORD maxSge,
										  DWORD notifyThreshold, USHORT group,
										  KAFFINITY affinity, VOID** ppSharedReceiveQueue);
	STDMETHODIMP CreateQueuePair(REFIID iid, IUnknown* pReceiveCompletionQueue,
								 IUnknown* pInitiatorCompletionQueue, VOID* context,
								 DWORD receiveQueueDepth, DWORD initiatorQueueDepth,
								 DWORD maxReceiveRequestSge, DWORD maxInitiatorRequestSge,
								 VOID** ppQueuePair);
	STDMETHODIMP CreateQueuePairWithSrq(REFIID, IUnknown* pReceiveCompletionQueue,
										IUnknown* pInitiatorCompletionQueue,
										IUnknown* pSharedReceiveQueue, VOID* context,
										DWORD initiatorQueueDepth, DWORD maxInitiatorRequestSge,
										VOID** ppQueuePair);
	STDMETHODIMP CreateSharedEndpoint(REFIID iid, VOID** ppSharedEndpoint);
	STDMETHODIMP CreateConnector(REFIID iid, VOID** ppConnector);
	STDMETHODIMP CreateListen(REFIID iid, VOID** ppListen);

	CNDAdapter(CNDProvider *pProvider);
	~CNDAdapter();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDProvider *pProvider, UINT64 adapterId, VOID** ppAdapter)
	{
		HRESULT hr;
		CNDAdapter *adapter;

		adapter = new CNDAdapter(pProvider);
		if (adapter == NULL) {
			hr = ND_NO_MEMORY;
			goto err1;
		}

		hr = adapter->Init(adapterId);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppAdapter = adapter;
		return ND_SUCCESS;

	err2:
		adapter->Release();
	err1:
		*ppAdapter = NULL;
		return hr;
	}

	CNDProvider			*m_pProvider;
	IWVDevice			*m_pWvDevice;
	IWVProtectionDomain	*m_pWvPd;
	DWORD				m_MaxInlineSend;

protected:
	UINT64				m_DeviceGuid;

	STDMETHODIMP		Init(UINT64 adapterId);
};

#endif // _ND_ADAPTER_H_
