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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <windows.h>
#include <ws2spi.h>
#include <stdio.h>
#include "nd_provider.h"


extern "C" {

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason) {
	case DLL_PROCESS_ATTACH:
	case DLL_PROCESS_DETACH:
		return TRUE;
	default:
		return TRUE;
	}
}

STDAPI DllCanUnloadNow(void)
{
	return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
	UNREFERENCED_PARAMETER(rclsid);

	if (riid != IID_IClassFactory) {
		*ppv = NULL;
		return E_NOINTERFACE;
	}

	*ppv = new CNDClassFactory();
	if (*ppv == NULL) {
		return E_OUTOFMEMORY;
	}

	return S_OK;
}

int WSPStartup(WORD wVersionRequested, LPWSPDATA lpWSPData,
			   LPWSAPROTOCOL_INFOW lpProtocolInfo,
			   WSPUPCALLTABLE UpcallTable, LPWSPPROC_TABLE lpProcTable)
{
	UNREFERENCED_PARAMETER(wVersionRequested);
	UNREFERENCED_PARAMETER(lpWSPData);
	UNREFERENCED_PARAMETER(lpProtocolInfo);
	UNREFERENCED_PARAMETER(UpcallTable);
	UNREFERENCED_PARAMETER(lpProcTable);
	return WSASYSNOTREADY;
}

} // extern "C"