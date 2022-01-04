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

namespace ND
{

template<
    typename ThisT,
    typename InterfaceT
    >
class __declspec(novtable) Unknown
    : public InterfaceT
{
    friend typename ThisT;
protected:
    volatile LONG   m_references;

public:
    Unknown()
        : m_references(1)
    {
    }

    STDMETHOD(QueryInterface)(
        REFIID riid,
        LPVOID FAR* ppvObj
        )
    {
        ThisT* pThis = static_cast<ThisT*>( this );
        if( ppvObj == NULL )
        {
            return E_POINTER;
        }
        *ppvObj = pThis->GetInterface(riid);
        if( *ppvObj == NULL )
        {
            return E_NOINTERFACE;
        }
        InterlockedIncrement(&pThis->m_references);
        return S_OK;
    }

    STDMETHOD_(ULONG,AddRef)()
    {
        ThisT* pThis = static_cast<ThisT*>( this );
        return InterlockedIncrement(&pThis->m_references);
    }

    STDMETHOD_(ULONG,Release)()
    {
        ThisT* pThis = static_cast<ThisT*>( this );
        ULONG refs = InterlockedDecrement(&pThis->m_references);
        if( 0 == refs )
        {
            pThis->FinalRelease();
        }
        return refs;
    }

public:
    void* GetInterface(REFIID riid)
    {
        ThisT* pThis = static_cast<ThisT*>( this );
        if( IID_IUnknown == riid )
        {
            return static_cast<IUnknown*>( pThis );
        }
        return NULL;
    }

    void FinalRelease()
    {
        ThisT* pThis = static_cast<ThisT*>( this );
        delete pThis;
    }
};

} // namespace ND
