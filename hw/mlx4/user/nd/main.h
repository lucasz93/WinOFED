/*
 * Copyright (c) Microsoft Corporation.  All rights reserved.
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

#if DBG
extern LONG g_nRef[ND_RESOURCE_TYPE_COUNT];

#ifndef ASSERT
#define ASSERT( expr ) ((!!(expr))? 0 : __debugbreak())
#endif  // ASSERT

#else
extern LONG g_nRef[1];

#ifndef ASSERT
#define ASSERT( expr )
#endif  // ASSERT
#endif

class ListEntry
{
    ListEntry* m_Flink;
    ListEntry* m_Blink;

public:
    ListEntry()
    {
        m_Flink = this;
        m_Blink = this;
    }

    ~ListEntry()
    {
    }

    void InsertHead(ListEntry* pEntry)
    {
        pEntry->m_Flink = m_Flink;
        pEntry->m_Blink = this;
        m_Flink->m_Blink = pEntry;
        m_Flink = pEntry;
    }

    void InsertTail(ListEntry* pEntry)
    {
        pEntry->m_Flink = this;
        pEntry->m_Blink = m_Blink;
        m_Blink->m_Flink = pEntry;
        m_Blink = pEntry;
    }

    void RemoveFromList()
    {
        m_Flink->m_Blink = m_Blink;
        m_Blink->m_Flink = m_Flink;
    }

    ListEntry* Next()
    {
        return m_Flink;
    }

    ListEntry* Prev()
    {
        return m_Blink;
    }

    const ListEntry* End()
    {
        return this;
    }
};


void* __cdecl operator new(
    size_t count
    );


void __cdecl operator delete(
    void* object
    );


HRESULT Ioctl(
    __in HANDLE hFile,
    __in ULONG IoControlCode,
    __in_bcount(cbInput) void* pInput,
    __in ULONG cbInput,
    __out_bcount_part_opt(*pcbOutput,*pcbOutput) void* pOutput,
    __inout ULONG* pcbOutput
    );


HRESULT IoctlAsync(
    __in HANDLE hFile,
    __in ULONG IoControlCode,
    __in_bcount(cbInput) void* pInput,
    __in ULONG cbInput,
    __out_bcount_part_opt(*pcbOutput,*pcbOutput) void* pOutput,
    __in ULONG cbOutput,
    __inout OVERLAPPED* pOverlapped
    );


HRESULT ConvertIbStatus( __in ib_api_status_t status );
