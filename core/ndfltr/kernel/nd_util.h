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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE ANd
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#ifndef _ND_UTIL_H_
#define _ND_UTIL_H_


template<typename T> void* operator new(size_t, T* instance) { return instance; }

#define HANDLE_TABLE_POOL_TAG 'thdn'

template<typename T> class HANDLE_TABLE
{
    union ENTRY {
            T*      object;
            SSIZE_T nextFree;
    };

    ENTRY*          Table;
    SSIZE_T         FirstFree;
    SSIZE_T         Size;
    ERESOURCE       Lock;

    static const SSIZE_T GROW_SIZE = PAGE_SIZE / sizeof(ENTRY);

private:
    bool Valid(UINT64 index)
    {
        if (index >= static_cast<UINT64>(Size)) {
            return false;
        }

        if (Table[index].nextFree >= 0) {
            return false;
        }
        return true;
    }

    bool Grow()
    {
        if (Size > (LONG_MAX - GROW_SIZE)) {
            return false;
        }

        ENTRY* newTable = reinterpret_cast<ENTRY*>(ExAllocatePoolWithTag(
            NonPagedPool,
            sizeof(ENTRY) * (Size + GROW_SIZE),
            HANDLE_TABLE_POOL_TAG)
            );
        if (newTable == NULL) {
            return false;
        }

        if (Table != NULL) {
            RtlCopyMemory(newTable, Table, Size * sizeof(ENTRY));
            ExFreePoolWithTag(Table, HANDLE_TABLE_POOL_TAG);
        }
        Table = newTable;

        Size += GROW_SIZE;
        for (SSIZE_T i = FirstFree; i < Size; i++) {
            Table[i].nextFree = (i + 1);
        }
        return true;
    }

    static inline SSIZE_T HandleToIndex(UINT64 handle)
    {
        return static_cast<SSIZE_T>(handle - 1);
    }

    static inline UINT64 IndexToHandle(SSIZE_T index)
    {
        return static_cast<UINT64>(index + 1);
    }

    inline void FreeIndex(SSIZE_T index)
    {
        Table[index].nextFree = FirstFree;
        FirstFree = index;
    }

public:
    HANDLE_TABLE<T>()
        : Table(NULL),
        FirstFree(0),
        Size(0)
    {
        ExInitializeResourceLite(&Lock);
    }
    
    ~HANDLE_TABLE<T>()
    {
        if (Table != NULL) {
            ExFreePoolWithTag(Table, HANDLE_TABLE_POOL_TAG);
        }
        ExDeleteResourceLite(&Lock);
    }

    void LockShared()
    {
        KeEnterCriticalRegion();
        ExAcquireResourceSharedLite(&Lock, TRUE);
    }

    void LockExclusive()
    {
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&Lock, TRUE);
    }

    void Unlock()
    {
        ExReleaseResourceLite(&Lock);
        KeLeaveCriticalRegion();
    }

    T* At(UINT64 handle)
    {
        SSIZE_T index = HandleToIndex(handle);
        if (Valid(index) == false) {
            return NULL;
        }

        return Table[index].object;
    }

    UINT64 Reserve()
    {
        if (FirstFree == Size && Grow() == false) {
            return 0;
        }

        SSIZE_T index = FirstFree;
        FirstFree = Table[FirstFree].nextFree;
        return IndexToHandle(index);
    }

    void Free(UINT64 handle)
    {
        FreeIndex(HandleToIndex(handle));
    }

    void Set(UINT64 handle, T* object)
    {
        SSIZE_T index = HandleToIndex(handle);
        Table[index].object = object;
    }

    void Erase(UINT64 handle)
    {
        SSIZE_T index = HandleToIndex(handle);
        NT_ASSERT(Valid(index));
        FreeIndex(index);
    }

    UINT64 Insert(T* object)
    {
        UINT64 handle = Reserve();
        if (handle != 0) {
            Set(handle, object);
        }

        return handle;
    }

    template<typename F> void Iterate(F functor)
    {
        for (SSIZE_T index = 0; index < Size; index++) {
            if (Valid(index) == true) {
                if (functor(Table[index].object) == true) {
                    FreeIndex(index);
                }
            }
        }
    }
};


class ObjBase
{
private:
    KEVENT m_Event;
    LONG m_nRef;
    LONG m_UseCount;

public:
    ObjBase()
        : m_nRef(1),
        m_UseCount(0)
    {
        KeInitializeEvent(&m_Event, NotificationEvent, FALSE);
    }

    ~ObjBase()
    {
        NT_ASSERT(m_nRef == 0);
    }

    void Reference()
    {
        InterlockedIncrement(&m_nRef);
    }

    void Dereference()
    {
        if (InterlockedDecrement(&m_nRef) == 0) {
            KeSetEvent(&m_Event, 0, FALSE);
        }
    }

    void AddRef()
    {
        InterlockedIncrement(&m_UseCount);
        Reference();
    }

    void Release()
    {
        InterlockedDecrement(&m_UseCount);
        Dereference();
    }

    bool Busy() { return m_UseCount != 0; }

    void RunDown()
    {
        if (InterlockedDecrement(&m_nRef) > 0) {
            NT_ASSERT(m_UseCount == 0);
            KeWaitForSingleObject(&m_Event, Executive, KernelMode, FALSE, NULL);
        }
    }
};


template <typename P> class ObjChild : public ObjBase
{
    typedef ObjBase _Base;

protected:
    P* m_pParent;
    LIST_ENTRY m_Entry;

private:
    ObjChild() {};

public:
    ObjChild(P* pParent)
        : m_pParent(pParent)
    {
        pParent->AddRef();
    }

    ~ObjChild()
    {
        m_pParent->Release();
    }

    static ObjChild* FromEntry(LIST_ENTRY* entry)
    {
        return CONTAINING_RECORD(entry, ObjChild, m_Entry);
    }
};


template <typename P, typename T, typename I> class RdmaResource : public ObjChild<P>
{
    typedef ObjChild<P> _Base;

protected:
    T m_hIfc;
    I* m_pIfc;

private:
    RdmaResource() {};

public:
    RdmaResource(P* pParent, I* pVerbs)
        : _Base(pParent),
        m_hIfc(NULL),
        m_pIfc(pVerbs)
    {
    }

    T GetIfcHandle() const { return m_hIfc; }
    I* GetInterface() const { return m_pIfc; }

    bool Removed() const { return m_pIfc == NULL; }

    void RemoveHandler() { m_hIfc = NULL; m_pIfc = NULL; }
};


template <typename P, typename T, typename I> class RdmaParent : public RdmaResource<P, T, I>
{
    typedef RdmaResource<P, T, I> _Base;

protected:
    KGUARDED_MUTEX m_Lock;

public:
    RdmaParent(P* pParent, ci_interface_t* pVerbs)
        : _Base(pParent, pVerbs)
    {
       KeInitializeGuardedMutex(&m_Lock);
    }

    void AddResource(__in LIST_ENTRY* list, LIST_ENTRY* entry)
    {
        KeAcquireGuardedMutex(&m_Lock);
        InsertTailList(list, entry);
        KeReleaseGuardedMutex(&m_Lock);
    }

    void RemoveResource(__in LIST_ENTRY* entry)
    {
        KeAcquireGuardedMutex(&m_Lock);
        RemoveEntryList(entry);
        KeReleaseGuardedMutex(&m_Lock);
    }
};


template<typename T> class DisposeFunctor
{
public:
    bool operator () (T* obj)
    {
        obj->Dispose();
        return true;
    }
};



#endif // _ND_UTIL_H_
