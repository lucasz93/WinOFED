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
 *
 */


///////////////////////////////////////////////////////////////////////////////
//
// IbatRoute
//
// The IBAT route tracks the state of the route, performs path queries to the
// subnet administrator, and tracks route resolution requests.
//
class IbatRoute
{
    ib_path_rec_t m_path;

    ib_gid_t m_srcGid;
    ib_gid_t m_destGid;

    ib_query_handle_t m_hQuery;

    struct IBAT_ROUTE_REQUEST* m_requestList;

    KSPIN_LOCK m_requestLock;

    //
    // Only NlnsUnreachable, NlnsIncomplete, and NlnsReachable are used.
    //
    NL_NEIGHBOR_STATE m_state;

    volatile LONG m_nRef;

    UINT16 m_pkey;

public:
    static IbatRoute* Create(
        const ib_gid_t* pSrcGid,
        const ib_gid_t* pDestGid,
        UINT16 pkey
        );

    inline VOID AddRef(){ InterlockedIncrement( &m_nRef ); }
    inline VOID Release();

    inline VOID Shutdown();
    inline IBAT_ROUTE_REQUEST* PopRequestList();

    NTSTATUS Resolve(
        __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
        __in VOID* completionContext,
        __out ib_path_rec_t* pPath
        );

    VOID Resolve(
        __in_opt IBAT_ROUTE_REQUEST* pRequestList
        );

    NTSTATUS CancelResolve(
        __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
        __in VOID* completionContext
        );

private:
    static VOID AL_API PathQueryCallback( __in ib_query_rec_t* pQueryResult );
    VOID
    QueryPathCompletion(
        __in NTSTATUS status,
        __in_opt ib_path_rec_t* const pPath
        );

    NTSTATUS QueryPathUnsafe();
};


inline
VOID
IbatRoute::Release()
{
    NT_ASSERT( m_nRef != 0 );
    if( InterlockedDecrement( &m_nRef ) == 0 )
    {
        ExFreePoolWithTag( this, IBAT_POOL_TAG );
    }
}


inline
VOID
IbatRoute::Shutdown()
{
    ib_query_handle_t hQuery = static_cast<ib_query_handle_t>(
        InterlockedExchangePointer(
            reinterpret_cast<volatile PVOID*>(&m_hQuery),
            NULL
            )
        );
    if( hQuery != NULL )
    {
        ib_cancel_query( gh_al, hQuery );
    }
}


inline
IBAT_ROUTE_REQUEST*
IbatRoute::PopRequestList()
{
    KLOCK_QUEUE_HANDLE hLock;
    KeAcquireInStackQueuedSpinLock( &m_requestLock, &hLock );

    IBAT_ROUTE_REQUEST* pHead = m_requestList;
    m_requestList = NULL;

    KeReleaseInStackQueuedSpinLock( &hLock );

    return pHead;
}
