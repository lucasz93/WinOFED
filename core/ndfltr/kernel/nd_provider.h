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

#ifndef _ND_PROVIDER_H_
#define _ND_PROVIDER_H_


#define ND_PROVIDER_POOL_TAG 'rpdn'

class ND_PROVIDER : public ObjChild<ND_PARTITION>
{
    typedef ObjChild<ND_PARTITION> _Base;

    HANDLE_TABLE<ND_ADAPTER>                m_AdapterTable;
    HANDLE_TABLE<ND_COMPLETION_QUEUE>       m_CqTable;
    HANDLE_TABLE<ND_PROTECTION_DOMAIN>      m_PdTable;
    HANDLE_TABLE<ND_MEMORY_REGION>          m_MrTable;
    HANDLE_TABLE<ND_SHARED_RECEIVE_QUEUE>   m_SrqTable;
    HANDLE_TABLE<ND_QUEUE_PAIR>             m_QpTable;
    HANDLE_TABLE<ND_MEMORY_WINDOW>          m_MwTable;
    HANDLE_TABLE<ND_ENDPOINT>               m_EpTable;

    ERESOURCE                               m_Lock;
    LONG                                    m_nFiles;

private:
    ND_PROVIDER(__in ND_PARTITION* pPartition);
    ~ND_PROVIDER();

    template <typename T> NTSTATUS Get(HANDLE_TABLE<T>& Table, UINT64 Handle, T** ppObj)
    {
        NTSTATUS status;

        Table.LockShared();
        T* obj = Table.At(Handle);
        if (obj == NULL) {
            status = STATUS_INVALID_HANDLE;
        } else if (obj->Removed()) {
            status = STATUS_DEVICE_REMOVED;
        } else {
            obj->AddRef();
            *ppObj = obj;
            status = STATUS_SUCCESS;
        }
        Table.Unlock();
        return status;
    }

    template <typename T> UINT64 Add(HANDLE_TABLE<T>& Table, T* pObj)
    {
        Table.LockExclusive();
        UINT64 handle = Table.Insert(pObj);
        Table.Unlock();
        return handle;
    }

    template <typename T> NTSTATUS Free(HANDLE_TABLE<T>& Table, UINT64 Handle)
    {
        NTSTATUS status;
        Table.LockExclusive();
        T* obj = Table.At(Handle);
        if (obj == NULL) {
            status = STATUS_INVALID_HANDLE;
        } else if (obj->Busy()) {
            status = STATUS_DEVICE_BUSY;
        } else {
            Table.Erase(Handle);
            status = STATUS_SUCCESS;
        }
        Table.Unlock();

        if (NT_SUCCESS(status)) {
            obj->Dispose();
        }
        return status;
    }

public:
    static NTSTATUS Create(__in ND_PARTITION* pPartition, __out ND_PROVIDER** ppProvider);

    void Dispose();

    NTSTATUS Bind(UINT64 Handle, FILE_OBJECT* File)
    {
        InterlockedIncrement(&m_nFiles);
        VOID* prov = InterlockedCompareExchangePointer(
                &File->FsContext,
                reinterpret_cast<VOID*>(Handle),
                NULL
                );

        if (prov != NULL) {
            InterlockedDecrement(&m_nFiles);
            return STATUS_INVALID_PARAMETER;
        }

        return STATUS_SUCCESS;
    }

    NTSTATUS Bind(UINT64 Handle, WDFFILEOBJECT File)
    {
        return Bind(Handle, WdfFileObjectWdmGetFileObject(File));
    }

    LONG Unbind(FILE_OBJECT* File)
    {
        InterlockedExchangePointer(&File->FsContext, NULL);
        return InterlockedDecrement(&m_nFiles);
    }

    LONG Unbind(WDFFILEOBJECT File)
    {
        return Unbind(WdfFileObjectWdmGetFileObject(File));
    }

    static __inline UINT64 HandleFromFile(WDFFILEOBJECT File)
    {
        DEVICE_OBJECT* pDevObj = IoGetRelatedDeviceObject(WdfFileObjectWdmGetFileObject(File));
        if (ControlDevice == NULL || pDevObj != WdfDeviceWdmGetDeviceObject(ControlDevice)) {
            return 0;
        }
        return reinterpret_cast<ULONG_PTR>(WdfFileObjectWdmGetFileObject(File)->FsContext);
    }

    void RemoveHandler(ND_RDMA_DEVICE* pDevice);

    void LockExclusive();

    void LockShared();

    void Unlock();

    ND_PARTITION* GetPartition() const { return m_pParent; }

    inline UINT64 AddAdapter(__in ND_ADAPTER* pAdapter)
    {
        return Add<ND_ADAPTER>(m_AdapterTable, pAdapter);
    }

    inline NTSTATUS GetAdapter(UINT64 Id, __out ND_ADAPTER** ppAdapter)
    {
        return Get<ND_ADAPTER>(m_AdapterTable, Id, ppAdapter);
    }

    inline NTSTATUS FreeAdapter(__in UINT64 Handle)
    {
        return Free<ND_ADAPTER>(m_AdapterTable, Handle);
    }

    inline UINT64 AddCq(__in ND_COMPLETION_QUEUE* pCq)
    {
        return Add<ND_COMPLETION_QUEUE>(m_CqTable, pCq);
    }

    inline NTSTATUS GetCq(UINT64 Id, __out ND_COMPLETION_QUEUE** ppCq)
    {
        return Get<ND_COMPLETION_QUEUE>(m_CqTable, Id, ppCq);
    }

    inline NTSTATUS FreeCq(__in UINT64 Handle)
    {
        return Free<ND_COMPLETION_QUEUE>(m_CqTable, Handle);
    }

    inline UINT64 AddPd(__in ND_PROTECTION_DOMAIN* pPd)
    {
        return Add<ND_PROTECTION_DOMAIN>(m_PdTable, pPd);
    }

    inline NTSTATUS GetPd(UINT64 Id, __out ND_PROTECTION_DOMAIN** ppPd)
    {
        return Get<ND_PROTECTION_DOMAIN>(m_PdTable, Id, ppPd);
    }

    inline NTSTATUS FreePd(__in UINT64 Handle)
    {
        return Free<ND_PROTECTION_DOMAIN>(m_PdTable, Handle);
    }

    inline UINT64 AddMr(__in ND_MEMORY_REGION* pMr)
    {
        return Add<ND_MEMORY_REGION>(m_MrTable, pMr);
    }

    inline NTSTATUS GetMr(UINT64 Id, __out ND_MEMORY_REGION** ppMr)
    {
        return Get<ND_MEMORY_REGION>(m_MrTable, Id, ppMr);
    }

    inline NTSTATUS FreeMr(__in UINT64 Handle)
    {
        return Free<ND_MEMORY_REGION>(m_MrTable, Handle);
    }

    inline UINT64 AddSrq(__in ND_SHARED_RECEIVE_QUEUE* pSrq)
    {
        return Add<ND_SHARED_RECEIVE_QUEUE>(m_SrqTable, pSrq);
    }

    inline NTSTATUS GetSrq(UINT64 Id, __out ND_SHARED_RECEIVE_QUEUE** ppSrq)
    {
        return Get<ND_SHARED_RECEIVE_QUEUE>(m_SrqTable, Id, ppSrq);
    }

    inline NTSTATUS FreeSrq(__in UINT64 Handle)
    {
        return Free<ND_SHARED_RECEIVE_QUEUE>(m_SrqTable, Handle);
    }

    inline UINT64 AddQp(__in ND_QUEUE_PAIR* pQp)
    {
        return Add<ND_QUEUE_PAIR>(m_QpTable, pQp);
    }

    inline NTSTATUS GetQp(UINT64 Id, __out ND_QUEUE_PAIR** ppQp)
    {
        return Get<ND_QUEUE_PAIR>(m_QpTable, Id, ppQp);
    }

    inline NTSTATUS FreeQp(__in UINT64 Handle)
    {
        return Free<ND_QUEUE_PAIR>(m_QpTable, Handle);
    }

    inline UINT64 AddMw(__in ND_MEMORY_WINDOW* pMw)
    {
        return Add<ND_MEMORY_WINDOW>(m_MwTable, pMw);
    }

    inline NTSTATUS GetMw(UINT64 Id, __out ND_MEMORY_WINDOW** ppMw)
    {
        return Get<ND_MEMORY_WINDOW>(m_MwTable, Id, ppMw);
    }

    inline NTSTATUS FreeMw(__in UINT64 Handle)
    {
        return Free<ND_MEMORY_WINDOW>(m_MwTable, Handle);
    }

    inline UINT64 AddEp(__in ND_ENDPOINT* pEp)
    {
        return Add<ND_ENDPOINT>(m_EpTable, pEp);
    }

    inline NTSTATUS GetEp(UINT64 Id, __out ND_ENDPOINT** ppEp)
    {
        return Get<ND_ENDPOINT>(m_EpTable, Id, ppEp);
    }

    inline NTSTATUS FreeEp(__in UINT64 Handle)
    {
        return Free<ND_ENDPOINT>(m_EpTable, Handle);
    }

public:
    static FN_REQUEST_HANDLER ResolveAddress;
    static FN_REQUEST_HANDLER QueryAddressList;
};

FN_REQUEST_HANDLER NdProviderBindFile;

#endif // _ND_PROVIDER_H_
