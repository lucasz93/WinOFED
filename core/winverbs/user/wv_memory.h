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

#ifndef _WV_MEMORY_H_
#define _WV_MEMORY_H_

#include <windows.h>

extern HANDLE heap;

__inline void* __cdecl operator new(size_t size)
{
	return HeapAlloc(heap, 0, size);
}

__inline void __cdecl operator delete(void *pObj)
{
	HeapFree(heap, 0, pObj);
}

const int WvDefaultBufferSize = 128;

class CWVBuffer
{
public:
	void* Get(size_t size)
	{
		if (size <= WvDefaultBufferSize) {
			m_pBuf = m_Buffer;
		} else {
			m_pBuf = new UINT8[size];
		}
		return m_pBuf;
	}

	void Put()
	{
		if (m_pBuf != m_Buffer) {
			delete []m_pBuf;
		}
	}
protected:
	UINT8 m_Buffer[WvDefaultBufferSize];
	void *m_pBuf;
};

#endif // _WV_MEMORY_H_
