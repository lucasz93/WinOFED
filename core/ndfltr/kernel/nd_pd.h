/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions (c) Microsoft Corporation.  All rights reserved.
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

#ifndef _ND_PD_H_
#define _ND_PD_H_


#define ND_PD_POOL_TAG 'dpdn'

class ND_PROTECTION_DOMAIN
    : public RdmaParent<ND_ADAPTER, ib_pd_handle_t, ci_interface_t>
{
    typedef RdmaParent<ND_ADAPTER, ib_pd_handle_t, ci_interface_t> _Base;

    LIST_ENTRY                                  m_QpList;
    LIST_ENTRY                                  m_SrqList;
    LIST_ENTRY                                  m_MrList;
    LIST_ENTRY                                  m_MwList;

private:
    ND_PROTECTION_DOMAIN(__in ND_ADAPTER* pAdapter);
    ~ND_PROTECTION_DOMAIN();

    NTSTATUS Initialize(
        KPROCESSOR_MODE RequestorMode,
        __inout ci_umv_buf_t* pVerbsData
        );

public:
    static
    NTSTATUS
    Create(
        __in ND_ADAPTER* pAdapter,
        KPROCESSOR_MODE RequestorMode,
        __inout ci_umv_buf_t* pVerbsData,
        __out ND_PROTECTION_DOMAIN** ppPd
        );

    void Dispose();

    void RemoveHandler();

    void AddQp(LIST_ENTRY* entry) { AddResource(&m_QpList, entry); }
    void RemoveQp(LIST_ENTRY* entry) { RemoveResource(entry); }
    void AddSrq(LIST_ENTRY* entry) { AddResource(&m_SrqList, entry); }
    void RemoveSrq(LIST_ENTRY* entry) { RemoveResource(entry); }
    void AddMr(LIST_ENTRY* entry) { AddResource(&m_MrList, entry); }
    void RemoveMr(LIST_ENTRY* entry) { RemoveResource(entry); }
    void AddMw(LIST_ENTRY* entry) { AddResource(&m_MwList, entry); }
    void RemoveMw(LIST_ENTRY* entry) { RemoveResource(entry); }
};

FN_REQUEST_HANDLER NdPdCreate;
FN_REQUEST_HANDLER NdPdFree;

#endif // _ND_PD_H_
