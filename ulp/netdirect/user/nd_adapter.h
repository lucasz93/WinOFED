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

#ifndef _ND_ADAPTER_H_
#define _ND_ADAPTER_H_

#include <ndspi.h>
#include "nd_base.h"
#include "nd_provider.h"
#include <dlist.h>


typedef struct _ND_MR
{
	DLIST_ENTRY			Entry;
	WV_MEMORY_KEYS		Keys;
	void				*Context;

}	ND_MR;


class CNDAdapter : public INDAdapter, public CNDBase
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

	// INDDevice methods
	STDMETHODIMP_(HANDLE) GetFileHandle();
	STDMETHODIMP Query(DWORD VersionRequested, ND_ADAPTER_INFO* pInfo,
					   SIZE_T* pBufferSize);
	STDMETHODIMP Control(DWORD IoControlCode,
						 const void* pInBuffer, SIZE_T InBufferSize,
						 void* pOutBuffer, SIZE_T OutBufferSize,
						 SIZE_T* pBytesReturned, OVERLAPPED* pOverlapped);
	STDMETHODIMP CreateCompletionQueue(SIZE_T nEntries, INDCompletionQueue** ppCq);
	STDMETHODIMP RegisterMemory(const void* pBuffer, SIZE_T BufferSize,
								OVERLAPPED* pOverlapped, ND_MR_HANDLE* phMr);
	STDMETHODIMP DeregisterMemory(ND_MR_HANDLE hMr, OVERLAPPED* pOverlapped);
	STDMETHODIMP CreateMemoryWindow(ND_RESULT* pInvalidateResult,
									INDMemoryWindow** ppMw);
	STDMETHODIMP CreateConnector(INDConnector** ppConnector);
	STDMETHODIMP Listen(SIZE_T Backlog, INT Protocol, USHORT Port,
						USHORT* pAssignedPort, INDListen** ppListen);

	CNDAdapter(CNDProvider *pProvider);
	~CNDAdapter();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CNDProvider *pProvider, const struct sockaddr *pAddress,
				   SIZE_T AddressLength, INDAdapter** ppAdapter)
	{
		HRESULT hr;
		CNDAdapter *adapter;

		adapter = new CNDAdapter(pProvider);
		if (adapter == NULL) {
			hr = ND_NO_MEMORY;
			goto err1;
		}

		hr = adapter->Init(pAddress, AddressLength);
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

	IWVProvider			*m_pWvProvider;
	IWVDevice			*m_pWvDevice;
	IWVProtectionDomain	*m_pWvPd;
	SOCKADDR_STORAGE	m_Address;
	WV_DEVICE_ADDRESS	m_DevAddress;

protected:
	CNDProvider			*m_pProvider;
	DLIST_ENTRY			m_MrList;
	CRITICAL_SECTION	m_Lock;

	STDMETHODIMP		Init(const struct sockaddr *pAddress, SIZE_T AddressLength);
	void				CleanupMr(ND_MR *pMr);
};

#endif // _ND_ADAPTER_H_
