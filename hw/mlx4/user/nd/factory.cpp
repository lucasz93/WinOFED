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

#include "precomp.h"

namespace ND
{

ClassFactory::ClassFactory(void)
    : _Base()
{
    //
    // We need DllCanUnloadNow to report whether there are any objects left.  We normally
    // only count top level provider objects, as all children hold a reference on their parent.
    // For NDv1, however, the provider object is created via a class factory object.
    // For simplicity, reuse the provider counter for the class factory.
    //
    InterlockedIncrement(&g_nRef[NdProvider]);
}


ClassFactory::~ClassFactory(void)
{
    InterlockedDecrement(&g_nRef[NdProvider]);
}


void* ClassFactory::GetInterface(
    REFIID riid
    )
{
    if( IsEqualIID( riid, IID_IClassFactory ) )
    {
        return static_cast<IClassFactory*>(this);
    }

    return _Base::GetInterface(riid);
}


HRESULT ClassFactory::CreateInstance(
    IUnknown* pUnkOuter,
    REFIID riid,
    void** ppObject )
{
    if( pUnkOuter != NULL )
    {
        return CLASS_E_NOAGGREGATION;
    }

    if( IsEqualIID( riid, IID_INDProvider ) )
    {
        return v1::Provider::Create(ppObject);
    }

    return v2::Provider::Create(riid, ppObject);
}


HRESULT ClassFactory::LockServer( BOOL )
{
    return S_OK;
}

} // namespace ND