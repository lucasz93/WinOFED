/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions Copyright (c) 2009 Microsoft Corporation.  All rights reserved.
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

#include <windows.h>
#include <iba\ib_ci.h>
#include "wv_memory.h"
#include "wv_device.h"
#include "wv_cq.h"
#include "wv_ioctl.h"
#include "wv_pd.h"

static char *WV_LIB_EXTENSION = ".dll";

CWVDevice::CWVDevice(CWVProvider *pProvider)
{
	pProvider->AddRef();
	m_pProvider = pProvider;
	m_hFile = pProvider->m_hFile;

	m_PortCount = 0;
	m_pPorts = NULL;
	m_hVerbsDevice = NULL;
	m_hLib = NULL;
}

// Destructor will do necessary cleanup.
STDMETHODIMP CWVDevice::
Open(NET64 Guid)
{
	char				libname[WV_MAX_LIB_NAME];
	WV_IO_ID			*pId;
	DWORD				bytes;
	ib_api_status_t		stat;
	HRESULT				hr;
	ci_umv_buf_t		verbsData;
	CWVBuffer			buf;

	m_Guid = Guid;
	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_LIBRARY_QUERY, &m_Guid,
						   (DWORD) sizeof m_Guid, libname,
						   WV_MAX_LIB_NAME - strlen(WV_LIB_EXTENSION),
						   &bytes, NULL)) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	errno_t err = strcpy_s(libname + bytes - 1, WV_MAX_LIB_NAME- (bytes - 1), WV_LIB_EXTENSION	);
	if(err != 0) {
		return WV_NO_MEMORY;
	}
	m_hLib = LoadLibrary(libname);
	if (m_hLib == NULL) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	hr = WvGetUserVerbs(m_hLib, &m_Verbs);
	if (FAILED(hr)) {
		return hr;
	}

	stat = m_Verbs.pre_open_ca(m_Guid, &verbsData, &m_hVerbsDevice);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_ID + max(verbsData.input_size, verbsData.output_size);
	pId = (WV_IO_ID *) buf.Get(bytes);
	if (pId == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pId->Id = m_Guid;
	pId->VerbInfo = verbsData.command;
	RtlCopyMemory(pId + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_DEVICE_OPEN,
						  pId, sizeof WV_IO_ID + verbsData.input_size,
						  pId, sizeof WV_IO_ID + verbsData.output_size,
						  &bytes, NULL)) {
		m_Id = pId->Id;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pId->VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pId + 1,
				  verbsData.output_size);
	buf.Put();

post:
	stat = m_Verbs.post_open_ca(m_Guid, (ib_api_status_t) hr,
								&m_hVerbsDevice, &verbsData);
	if (SUCCEEDED(hr) && stat != IB_SUCCESS) {
		hr = WvConvertIbStatus(stat);
	}

	if (SUCCEEDED(hr)) {
		hr = InitPorts();
	}

	return hr;
}

STDMETHODIMP CWVDevice::
InitPorts()
{
	WV_DEVICE_ATTRIBUTES attr;
	WV_PORT *port;
	HRESULT	hr;

	hr = Query(&attr);
	if (FAILED(hr)) {
		return hr;
	}

	m_pPorts = new WV_PORT[attr.PhysPortCount];
	if (m_pPorts == NULL) {
		return WV_NO_MEMORY;
	}

	RtlZeroMemory(m_pPorts, sizeof(WV_PORT) * attr.PhysPortCount);
	for (m_PortCount = 0; m_PortCount < attr.PhysPortCount; m_PortCount++) {
		port = &m_pPorts[m_PortCount];

		port->m_Overlap.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (port->m_Overlap.hEvent == NULL) {
			hr = WV_INSUFFICIENT_RESOURCES;
			break;
		}

		port->m_Overlap.hEvent = (HANDLE) ((ULONG_PTR) port->m_Overlap.hEvent | 1);

		port->m_Flags = 0xFFFFFFFF;
		hr = UpdatePort(m_PortCount + 1);
		if (FAILED(hr)) {
			CloseHandle(port->m_Overlap.hEvent);
			break;
		}
	}

	return hr;
}

CWVDevice::~CWVDevice()
{
	WV_PORT *port;
	DWORD	bytes;
	HRESULT	hr;

	if (m_hVerbsDevice != NULL) {
		m_Verbs.pre_close_ca(m_hVerbsDevice);
		hr = WvDeviceIoControl(m_hFile, WV_IOCTL_DEVICE_CLOSE, &m_Id, sizeof m_Id,
							   NULL, 0, &bytes, NULL) ?
							   WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
		m_Verbs.post_close_ca(m_hVerbsDevice, (ib_api_status_t) hr);
	}

	while (m_PortCount--) {
		port = &m_pPorts[m_PortCount];
		GetOverlappedResult(&port->m_Overlap, &bytes, TRUE);
		CloseHandle(port->m_Overlap.hEvent);
	}

	if (m_pPorts != NULL) {
		delete m_pPorts;
	}

	if (m_hLib != NULL) {
		FreeLibrary(m_hLib);
	}
	m_pProvider->Release();
}

STDMETHODIMP CWVDevice::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVDevice) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVDevice::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVDevice::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVDevice::
CancelOverlappedRequests(void)
{
	DWORD	bytes;

	return WvDeviceIoControl(m_hFile, WV_IOCTL_DEVICE_CANCEL, &m_Id, sizeof m_Id,
							 NULL, 0, &bytes, NULL) ?
							 WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CWVDevice::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVDevice::
Query(WV_DEVICE_ATTRIBUTES* pAttributes)
{
	WV_IO_DEVICE_ATTRIBUTES	attr;
	DWORD					bytes;

	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_DEVICE_QUERY, &m_Id, sizeof m_Id,
						   &attr, sizeof attr, &bytes, NULL)) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	pAttributes->FwVersion = attr.FwVersion;
	pAttributes->NodeGuid = attr.NodeGuid;
	pAttributes->SystemImageGuid = attr.SystemImageGuid;
	pAttributes->VendorId = attr.VendorId;
	pAttributes->VendorPartId = attr.VendorPartId;
	pAttributes->HwVersion = attr.HwVersion;
	pAttributes->CapabilityFlags = attr.CapabilityFlags;
	pAttributes->AtomicCapability = (WV_ATOMIC_CAPABILITIES) attr.AtomicCapability;
	pAttributes->PageSizeCapabilityFlags = attr.PageSizeCapabilityFlags;
	pAttributes->MaxMrSize = (SIZE_T) attr.MaxMrSize;
	pAttributes->MaxQp = attr.MaxQp;
	pAttributes->MaxQpWr = attr.MaxQpWr;
	pAttributes->MaxSge = attr.MaxSge;
	pAttributes->MaxCq = attr.MaxCq;
	pAttributes->MaxCqEntries = attr.MaxCqEntries;
	pAttributes->MaxMr = attr.MaxMr;
	pAttributes->MaxPd = attr.MaxPd;
	pAttributes->MaxQpResponderResources = attr.MaxQpResponderResources;
	pAttributes->MaxResponderResources = attr.MaxResponderResources;
	pAttributes->MaxQpInitiatorDepth = attr.MaxQpInitiatorDepth;
	pAttributes->MaxInlineSend = attr.MaxInlineSend;
	pAttributes->MaxMw = attr.MaxMw;
	pAttributes->MaxMulticast = attr.MaxMulticast;
	pAttributes->MaxQpAttach = attr.MaxQpAttach;
	pAttributes->MaxMulticastQp = attr.MaxMulticastQp;
	pAttributes->MaxAh = attr.MaxAh;
	pAttributes->MaxFmr = attr.MaxFmr;
	pAttributes->MaxMapPerFmr = attr.MaxMapPerFmr;
	pAttributes->MaxSrq = attr.MaxSrq;
	pAttributes->MaxSrqWr = attr.MaxSrqWr;
	pAttributes->MaxSrqSge = attr.MaxSrqSge;
	pAttributes->MaxPkeys = attr.MaxPkeys;
	pAttributes->DeviceType = (WV_DEVICE_TYPE) attr.DeviceType;
	pAttributes->LocalAckDelay = attr.LocalAckDelay;
	pAttributes->PhysPortCount = attr.PhysPortCount;

	return WV_SUCCESS;
}

STDMETHODIMP CWVDevice::
QueryPort(UINT8 PortNumber, WV_PORT_ATTRIBUTES* pAttributes)
{
	WV_IO_DEVICE_PORT_QUERY query;
	DWORD					bytes;

	query.Id = m_Id;
	query.PortNumber = PortNumber;
	RtlZeroMemory(&query.Reserved, sizeof query.Reserved);

	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_DEVICE_PORT_QUERY, &query,
						   sizeof query, pAttributes, sizeof *pAttributes,
						   &bytes, NULL)) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	pAttributes->PkeyTableLength = min(pAttributes->PkeyTableLength, WV_MAX_PKEYS);
	return WV_SUCCESS;
}

STDMETHODIMP CWVDevice::
UpdatePort(UINT8 PortNumber)
{
	WV_PORT	*port = &m_pPorts[PortNumber - 1];
	HRESULT hr;

	if (port->m_Flags & WV_EVENT_PARTITION) {
		UpdatePkeys(PortNumber);
	}

	port->m_Flags = 0;
	hr = Notify(PortNumber, &port->m_Overlap, &port->m_Flags);
	if (FAILED(hr) && hr != WV_IO_PENDING) {
		return hr;
	}

	return WV_SUCCESS;
}

STDMETHODIMP CWVDevice::
QueryGid(UINT8 PortNumber, DWORD Index, WV_GID* pGid)
{
	WV_IO_DEVICE_PORT_QUERY query;
	WV_GID					*gidtable;
	DWORD					ngid, bytes;
	HRESULT					hr;
	CWVBuffer				buf;

	bytes = sizeof WV_GID * (Index + 1);
	gidtable = (WV_GID *) buf.Get(bytes);
	if (gidtable == NULL) {
		return WV_NO_MEMORY;
	}

	query.Id = m_Id;
	query.PortNumber = PortNumber;
	RtlZeroMemory(&query.Reserved, sizeof query.Reserved);

	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_DEVICE_GID_QUERY, &query,
						   sizeof query, gidtable, bytes, &bytes, NULL)) {
		hr = HRESULT_FROM_WIN32(GetLastError());
		goto out;
	}

	ngid = bytes / sizeof WV_GID;
	if (Index >= ngid) {
		hr = WV_INVALID_PARAMETER_2;
		goto out;
	}
	*pGid = gidtable[Index];
	hr = WV_SUCCESS;

out:
	buf.Put();
	return hr;
}

STDMETHODIMP CWVDevice::
FindGid(UINT8 PortNumber, WV_GID *pGid, DWORD *pIndex)
{
	WV_GID	gid;
	DWORD	index;
	HRESULT	hr;

	for (index = 0; true; index++) {
		hr = QueryGid(PortNumber, index, &gid);
		if (FAILED(hr)) {
			return hr;
		}

		if (RtlEqualMemory(pGid, &gid, sizeof(gid))) {
			*pIndex = (UINT16) index;
			return WV_SUCCESS;
		}
	}

	return WV_INVALID_ADDRESS;
}

STDMETHODIMP CWVDevice::
UpdatePkeys(UINT8 PortNumber)
{
	WV_IO_DEVICE_PORT_QUERY query;
	WV_PORT	*port;
	DWORD	bytes;

	port = &m_pPorts[PortNumber - 1];
	bytes = sizeof(port->m_PkeyTable);

	query.Id = m_Id;
	query.PortNumber = PortNumber;
	RtlZeroMemory(&query.Reserved, sizeof query.Reserved);

	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_DEVICE_PKEY_QUERY, &query,
						   sizeof query, port->m_PkeyTable, bytes, &bytes, NULL)) {
		port->m_PkeyCount = 0;
		return HRESULT_FROM_WIN32(GetLastError());
	}

	port->m_PkeyCount = (UINT16) (bytes / sizeof NET16);
	return WV_SUCCESS;
}

STDMETHODIMP CWVDevice::
QueryPkey(UINT8 PortNumber, UINT16 Index, NET16* pPkey)
{
	WV_PORT *port = &m_pPorts[PortNumber - 1];
	HRESULT hr;

	EnterCriticalSection(&m_CritSec);
	if (HasOverlappedIoCompleted(&port->m_Overlap)) {
		UpdatePort(PortNumber);
	}

	if (Index < port->m_PkeyCount) {
		*pPkey = port->m_PkeyTable[Index];
		hr = WV_SUCCESS;
	} else {
		hr = WV_INVALID_PARAMETER_2;
	}

	LeaveCriticalSection(&m_CritSec);
	return hr;
}

STDMETHODIMP CWVDevice::
FindPkey(UINT8 PortNumber, NET16 Pkey, UINT16 *pIndex)
{
	WV_PORT *port = &m_pPorts[PortNumber - 1];
	UINT16	index;
	HRESULT hr = WV_INVALID_ADDRESS;

	EnterCriticalSection(&m_CritSec);
	if (HasOverlappedIoCompleted(&port->m_Overlap)) {
		UpdatePort(PortNumber);
	}

	for (index = 0; index < port->m_PkeyCount; index++) {
		if (Pkey == port->m_PkeyTable[index]) {
			*pIndex = index;
			hr = WV_SUCCESS;
			break;
		}
	}

	LeaveCriticalSection(&m_CritSec);
	return hr;
}

STDMETHODIMP CWVDevice::
CreateCompletionQueue(SIZE_T *pEntries, IWVCompletionQueue** ppCq)
{
	return CWVCompletionQueue::CreateInstance(this, pEntries, ppCq);
}

STDMETHODIMP CWVDevice::
AllocateProtectionDomain(IWVProtectionDomain** ppPd)
{
	return CWVProtectionDomain::CreateInstance(this, ppPd);
}

STDMETHODIMP CWVDevice::
Notify(UINT8 PortNumber, OVERLAPPED* pOverlapped, DWORD* pFlags)
{
	WV_IO_ID	id;
	DWORD		bytes;
	HRESULT		hr;

	id.Id = m_Id;
	id.Data = PortNumber;

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_DEVICE_NOTIFY, &id, sizeof id,
						  pFlags, sizeof DWORD, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}
