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

#include <iba\ib_al.h>
#include <iba\ib_at_ioctl.h>

#include "wv_memory.h"
#include "wv_provider.h"
#include "wv_device.h"
#include "wv_ep.h"
#include "wv_ioctl.h"

CWVProvider::CWVProvider()
{
	InterlockedIncrement(&WvRef);
	m_hFile = INVALID_HANDLE_VALUE;
}

CWVProvider::~CWVProvider()
{
	CloseHandle(m_hFile);
	if (InterlockedDecrement(&WvRef) == 0) {
		WSACleanup();
	}
}

STDMETHODIMP CWVProvider::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVProvider) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVProvider::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVProvider::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP_(HANDLE) CWVProvider::
GetFileHandle(void)
{
	return m_hFile;
}

STDMETHODIMP CWVProvider::
QueryDeviceList(NET64* pGuidList, SIZE_T* pBufferSize)
{
	CWVBuffer		buf;
	WV_IO_GUID_LIST	*list;
	DWORD			bytes;
	HRESULT			hr;

	bytes = sizeof(WV_IO_GUID_LIST) + (DWORD) *pBufferSize;
	list = (WV_IO_GUID_LIST *) buf.Get(bytes);
	if (list == NULL) {
		return WV_NO_MEMORY;
	}

	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_GUID_QUERY, NULL, 0,
						   list, bytes, &bytes, NULL)) {
		hr = HRESULT_FROM_WIN32(GetLastError());
		goto out;
	}

	bytes = (DWORD) list->Count * sizeof NET64;
	if (*pBufferSize >= bytes) {
		RtlCopyMemory(pGuidList, list->Guid, (size_t) bytes);
	} else if (*pBufferSize > 0) {
		RtlCopyMemory(pGuidList, list->Guid, *pBufferSize);
	}

	*pBufferSize = (size_t) bytes;
	hr = WV_SUCCESS;

out:
	buf.Put();
	return hr;
}

STDMETHODIMP CWVProvider::
QueryDevice(NET64 Guid, WV_DEVICE_ATTRIBUTES* pAttributes)
{
	IWVDevice *dev;
	HRESULT hr;

	hr = OpenDevice(Guid, &dev);
	if (FAILED(hr)) {
		goto out;
	}
	
	hr = dev->Query(pAttributes);
	dev->Release();
out:
	return hr;
}

STDMETHODIMP CWVProvider::
TranslateAddress(const SOCKADDR* pAddress, WV_DEVICE_ADDRESS* pDeviceAddress)
{
	HANDLE hIbat;
	IOCTL_IBAT_IP_TO_PORT_IN addr;
	IBAT_PORT_RECORD port;
	DWORD bytes;
	HRESULT hr;

	hIbat = CreateFileW(IBAT_WIN32_NAME, GENERIC_READ | GENERIC_WRITE,
						FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
						OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hIbat == INVALID_HANDLE_VALUE) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	addr.Version = IBAT_IOCTL_VERSION;
	if (pAddress->sa_family == AF_INET) {
        addr.Address.Ipv4 = *reinterpret_cast<const SOCKADDR_IN*>(pAddress);
	} else {
        addr.Address.Ipv6 = *reinterpret_cast<const SOCKADDR_IN6*>(pAddress);
	}

	if (DeviceIoControl(hIbat, IOCTL_IBAT_IP_TO_PORT,
						&addr, sizeof addr, &port, sizeof port, &bytes, NULL)) {
		hr = WV_SUCCESS;
	pDeviceAddress->DeviceGuid = port.CaGuid;
	pDeviceAddress->Pkey = port.PKey;
	pDeviceAddress->PortNumber = port.PortNum;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	CloseHandle(hIbat);
	return hr;
}

STDMETHODIMP CWVProvider::
OpenDevice(NET64 Guid, IWVDevice** ppDevice)
{
	return CWVDevice::CreateInstance(this, Guid, ppDevice);
}

STDMETHODIMP CWVProvider::
CreateConnectEndpoint(IWVConnectEndpoint** ppConnectEndpoint)
{
	return CWVConnectEndpoint::CreateInstance(this, ppConnectEndpoint);
}

STDMETHODIMP CWVProvider::
CreateDatagramEndpoint(IWVDatagramEndpoint** ppDatagramEndpoint)
{
	return CWVDatagramEndpoint::CreateInstance(this, ppDatagramEndpoint);
}
