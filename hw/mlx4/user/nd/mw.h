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
namespace v2
{

class Mw
    : public Unknown<Mw, IND2MemoryWindow>
{
    typedef Unknown<Mw, IND2MemoryWindow> _Base;

public:
    static const GUID _Guid;

private:
    Adapter& m_Adapter;

    UINT64 m_hMw;
    ib_mw_handle_t m_uMw;

    //
    // The following members are to work around lack of MW support in the ConnectX HW/drivers.
    //
    Mr& m_Mr;
    Mr* m_pBoundMr;

private:
    Mw& operator =(Mw& rhs);

public:
    Mw(Adapter& adapter, Mr& mr);
    ~Mw(void);

    HRESULT Initialize();

    void* GetInterface(REFIID riid);

    HRESULT Bind(Mr* pMr, __in_bcount(cbBuffer) const void* pBuffer, SIZE_T cbBuffer, ULONG Flags);
    HRESULT Invalidate();

public:
    // *** IND2MemoryWindow methods ***
    STDMETHODIMP_(UINT32) GetRemoteToken(){ return m_Mr.GetRemoteToken(); }
};

} // namespace ND::v2

namespace v1
{

class Mw
    : public Unknown<Mw, INDMemoryWindow>
{
    typedef Unknown<Mw, INDMemoryWindow> _Base;

    INDAdapter& m_Adapter;
    IND2MemoryWindow& m_Mw;

    ND_RESULT* m_pInvalidateResult;

private:
    Mw& operator =(Mw& rhs);

public:
    Mw(INDAdapter& adapter, IND2MemoryWindow& mw, ND_RESULT* pInvalidateResult);
    ~Mw(void);

    void* GetInterface(REFIID riid);

    IND2MemoryWindow* GetMw() const { return &m_Mw; }
};

} // namespace ND::v1
} // namespace ND
