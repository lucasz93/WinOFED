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

#include "nd_ep.h"
#include "nd_adapter.h"
#include <netinet/in.h>


CNDSharedEndpoint::CNDSharedEndpoint(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
}

CNDSharedEndpoint::~CNDSharedEndpoint()
{
	m_pAdapter->Release();
}

STDMETHODIMP CNDSharedEndpoint::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDSharedEndpoint) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDSharedEndpoint::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDSharedEndpoint::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDSharedEndpoint::
Bind(const struct sockaddr* pAddress, SIZE_T cbAddress)
{
	//???
	return ND_NOT_SUPPORTED;
}

STDMETHODIMP CNDSharedEndpoint::
GetLocalAddress(struct sockaddr* pAddress, SIZE_T* pcbAddress)
{
	HRESULT hr;
	
	if (*pcbAddress >= m_AddressSize) {
		RtlCopyMemory(pAddress, &m_Address, m_AddressSize);
		hr = ND_SUCCESS;
	} else {
		hr = ND_BUFFER_OVERFLOW;
	}

	*pcbAddress = m_AddressSize;
	return NDConvertWVStatus(hr);
}
