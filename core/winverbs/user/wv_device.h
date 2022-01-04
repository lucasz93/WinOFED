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

#ifndef _WV_DEVICE_H_
#define _WV_DEVICE_H_

#include <iba\ib_types.h>
#include <iba\ib_uvp.h>
#include <rdma\winverbs.h>
#include "wv_provider.h"
#include "wv_base.h"

#define WV_MAX_PKEYS 16

typedef struct _WV_PORT
{
	OVERLAPPED	m_Overlap;
	DWORD		m_Flags;
	UINT16		m_PkeyCount;
	NET16		m_PkeyTable[WV_MAX_PKEYS];

}	WV_PORT;


class CWVDevice : IWVDevice, public CWVBase
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

	// IWVDevice methods
	STDMETHODIMP Query(WV_DEVICE_ATTRIBUTES* pAttributes);
	STDMETHODIMP QueryPort(UINT8 PortNumber, WV_PORT_ATTRIBUTES* pAttributes);
	STDMETHODIMP QueryGid(UINT8 PortNumber, DWORD Index, WV_GID* pGid);
	STDMETHODIMP FindGid(UINT8 PortNumber, WV_GID *pGid, DWORD *pIndex);
	STDMETHODIMP QueryPkey(UINT8 PortNumber, UINT16 Index, NET16* pPkey);
	STDMETHODIMP FindPkey(UINT8 PortNumber, NET16 Pkey, UINT16 *pIndex);
	STDMETHODIMP CreateCompletionQueue(SIZE_T *pEntries, IWVCompletionQueue** ppCq);
	STDMETHODIMP AllocateProtectionDomain(IWVProtectionDomain** ppPd);
	STDMETHODIMP Notify(UINT8 PortNumber, OVERLAPPED* pOverlapped, DWORD* pFlags);

	CWVDevice(CWVProvider *pProvider);
	~CWVDevice();
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVProvider *pProvider, NET64 Guid, IWVDevice** ppDevice)
	{
		HRESULT hr;
		CWVDevice *dev;

		dev = new CWVDevice(pProvider);
		if (dev == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = dev->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = dev->Open(Guid);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppDevice = dev;
		return WV_SUCCESS;

	err2:
		dev->Release();
	err1:
		*ppDevice = NULL;
		return hr;
	}

	CWVProvider		*m_pProvider;
	uvp_interface_t	m_Verbs;

	ib_ca_handle_t	m_hVerbsDevice;
	NET64			m_Guid;

protected:
	HMODULE			m_hLib;
	STDMETHODIMP	Open(NET64 Guid);

	WV_PORT			*m_pPorts;
	UINT8			m_PortCount;
	STDMETHODIMP	InitPorts();
	STDMETHODIMP	UpdatePort(UINT8 PortNumber);
	STDMETHODIMP	UpdatePkeys(UINT8 PortNumber);
};

#endif // __WV_DEVICE_H_
