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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "nd_base.h"
#include <ndstatus.h>

CNDBase::CNDBase()
{
	m_nRef = 1;
}

STDMETHODIMP_(ULONG) CNDBase::
AddRef(void)
{
	return InterlockedIncrement(&m_nRef);
}

STDMETHODIMP_(ULONG) CNDBase::
Release(void)
{
	ULONG ref;

	ref = (ULONG) InterlockedDecrement(&m_nRef);
	if (ref == 0) {
		Delete();
	}
	return ref;
}

HRESULT NDConvertWVStatus(HRESULT hr)
{
	switch (hr) {
	case WV_IO_PENDING:
		return ND_PENDING;
	default:
		return hr;
	}
}
