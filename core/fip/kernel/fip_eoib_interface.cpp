/*
 * 
 * Copyright (c) 2011-2012 Mellanox Technologies. All rights reserved.
 * 
 * This software is licensed under the OpenFabrics BSD license below:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *  conditions are met:
 *
 *       - Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *       - Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

/*

Module Name:
    fip_eoib_interface.cpp

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/

#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif

#include "fip_eoib_interface.tmh"
#endif


FipInterface* g_pFipInterface;

//
// InterfaceVnic Class Function
//
NTSTATUS 
InterfaceVnic::Init(
    char                    *UniqeStr,
    int                     LocalDataQpn, // Only 24 Bit valid, Host-Order
    PMLX4_BUS_IB_INTERFACE  pBusIbInterface
    )
{
    NTSTATUS Status;

    ASSERT(m_RefCount > 0);
    Status = RtlStringCbCopyA(m_UniqeStr, sizeof(m_UniqeStr), UniqeStr);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_VNIC, "RtlStringCbCopyA Failed\n");
        ASSERT(FALSE);
    }

    m_VhubListHead.Init();
    m_LocalDataQpn = LocalDataQpn;
    m_pBusIbInterface = pBusIbInterface;

    return Status;
}


VOID
InterfaceVnic::AddEntry(
    InterfaceVhubUpdate_t *pInterfaceVhubUpdate
    )
{
    ASSERT(m_RefCount > 0);

    // If we get "Remove All" we'll remove all current Msg and add this one only
    if (pInterfaceVhubUpdate->Cmd == CMD_REMOVE_ALL) {
        RemoveAllEntries();
    }

    m_VhubListHead.InsertTailList(&pInterfaceVhubUpdate->Entry);
    NotifyEoibDriver();
}


InterfaceVhubUpdate_t*
InterfaceVnic::GetEntry( VOID )
{
    LIST_ENTRY *item;
    InterfaceVhubUpdate_t *pInterfaceVhubUpdate = NULL;

    ASSERT(m_RefCount > 0);
    if (m_VhubListHead.Size() == 0) {
        return NULL;
    }
    item = m_VhubListHead.RemoveHeadList();    
    pInterfaceVhubUpdate = CONTAINING_RECORD(item, InterfaceVhubUpdate_t , Entry);

    return pInterfaceVhubUpdate;
}

VOID
InterfaceVnic::RemoveAllEntries( VOID )
{
    LIST_ENTRY *item;
    InterfaceVhubUpdate_t *pInterfaceVhubUpdate = NULL;

    ASSERT(m_RefCount >= 0);
    while (m_VhubListHead.Size() != 0) {
        item = m_VhubListHead.RemoveHeadList();
        pInterfaceVhubUpdate = CONTAINING_RECORD(item, InterfaceVhubUpdate_t , Entry);
        delete pInterfaceVhubUpdate;
    }
}


VOID
InterfaceVnic::ReleaseQPRange( VOID )
{
    ASSERT(m_pBusIbInterface != NULL);
    m_pBusIbInterface->mlx4_interface.mlx4_qp_release_range(
        m_pBusIbInterface->pmlx4_dev,
        m_LocalDataQpn,
        1);
}


int
InterfaceVnic::AddRef( VOID )
{
    ASSERT(m_RefCount > 0);
    m_RefCount = InterlockedIncrement(&m_RefCount);
    return m_LocalDataQpn;
}


LONG
InterfaceVnic::Release( VOID )
{
    LONG RefCount;
    ASSERT(m_RefCount > 0);

    m_RefCount = InterlockedDecrement(&m_RefCount);
    RefCount = m_RefCount;

    if (m_RefCount == 0) {
        delete this;
    }

    return RefCount;
}


//
// FipInterface Class Function
//
NTSTATUS 
FipInterface::Init(
    )
{
    NTSTATUS Status;

    ASSERT(m_Shutdown == false);
    Status = m_FipInterfaceVnics.Init(INITIAL_ARRAY_SIZE, TRUE);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FipGws.Init failed Status = 0x%x\n", Status);
        goto Cleanup;
    }

Cleanup:
    return Status;
}

VOID FipInterface::Shutdown()
{
    ASSERT(m_Shutdown == false);
    m_Shutdown = true;
}


NTSTATUS 
FipInterface::AddVnic(
    u64                     *pUniqueId,
    u32                     *pLocation,
    char                    *UniqeStr,
    int                     LocalDataQpn,
    PMLX4_BUS_IB_INTERFACE  pBusIbInterface,
    InterfaceVnic           **ppInterfaceVnic
    )
{
    int FreeLocation;
    LONG RefCount = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(m_Shutdown == false);
    ASSERT(*ppInterfaceVnic == NULL);

    *pUniqueId = 0;
    *pLocation = 0;
    *ppInterfaceVnic = NULL;

    m_FipInterfaceLock.Lock();

    InterfaceVnic *pInterfaceVnic = NEW InterfaceVnic;
    if (pInterfaceVnic == NULL) {
        Status = STATUS_NO_MEMORY;
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "new InterfaceVnic failed\n");
        goto err_new;
    }

    Status = pInterfaceVnic->Init(
        UniqeStr,
        LocalDataQpn,
        pBusIbInterface
        );
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "%s Init InterfaceVnic failed\n", UniqeStr);
        goto err_init_if;
    }

    FreeLocation = m_FipInterfaceVnics.GetFreeLocation();
    if (FreeLocation == -1) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "Failed to Find Free Location for New Vnic\n");
        Status = STATUS_NO_MEMORY;
        goto err_free_location;
    }

    m_FipInterfaceVnics[FreeLocation] = pInterfaceVnic;
    if (FreeLocation < MAXDEBUGVNICS) {
        FipInterfaceVnics[FreeLocation] = pInterfaceVnic;
    }

    *pLocation = FreeLocation;
    *pUniqueId = m_UniqueIdCounter++;
    pInterfaceVnic->m_UniqueId = *pUniqueId;
    *ppInterfaceVnic = pInterfaceVnic;

    m_FipInterfaceLock.Unlock();
    return Status;

err_free_location:
err_init_if:
    RefCount = pInterfaceVnic->Release();
    ASSERT( RefCount == 0);

err_new:
    m_FipInterfaceLock.Unlock();
    return Status;

}

VOID
FipInterface::RemoveVnic(
    InterfaceVnic *pInterfaceVnic,
    u32 Location
    )
{
    ASSERT(m_Shutdown == false);
    m_FipInterfaceLock.Lock();

    LONG RefCount = pInterfaceVnic->Release();
    if (RefCount == 0) {
        ASSERT(m_FipInterfaceVnics[Location] == pInterfaceVnic);
        m_FipInterfaceVnics[Location] = NULL;
        if (Location < MAXDEBUGVNICS) {
            FipInterfaceVnics[Location] = NULL;
        }
    }
    m_FipInterfaceLock.Unlock();

}


// Each Vnic will update for it's known neighbours (add or remove them)
VOID
FipInterface::UpdateVhubEntry(
    InterfaceVnic *pInterfaceVnic,
    InterfaceVhubUpdate_t *pInterfaceVhubUpdate
    )
{
    ASSERT(m_Shutdown == false);
    ASSERT(pInterfaceVnic != NULL);
    m_FipInterfaceLock.Lock();
    pInterfaceVnic->AddEntry(pInterfaceVhubUpdate);
    m_FipInterfaceLock.Unlock();
}


NTSTATUS
FipInterface::UpdateCheckForHang(
    u64 UniqueId,
    u32 Location,
    boolean_t AllOK
    )
{   
    InterfaceVnic *pInterfaceVnic = NULL;
    NTSTATUS Status = STATUS_SUCCESS;


    m_FipInterfaceLock.Lock();

    pInterfaceVnic = FindVnic(UniqueId, Location);
    if (pInterfaceVnic == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FindVnic failed UniqueId=%I64d ,  Location = %d\n", UniqueId, Location);
        Status = STATUS_SHUTDOWN_IN_PROGRESS;
        goto Exit;
    }

    pInterfaceVnic->UpdateCheckForHang(AllOK);

Exit:
    m_FipInterfaceLock.Unlock();
    return Status;

}


NTSTATUS
FipInterface::FipRegisterEoibNotification(
    uint64_t UniqueId ,
    uint32_t Location,
    void *port,
    FIP_CHANGE_CALLBACK FipChangeCallback
    )
{
    InterfaceVnic *pInterfaceVnic = NULL;
    NTSTATUS Status = STATUS_SUCCESS;


    m_FipInterfaceLock.Lock();

    pInterfaceVnic = FindVnic(UniqueId, Location);
    if (pInterfaceVnic == NULL) {       
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FindVnic failed UniqueId=%I64d ,  Location = %d\n", UniqueId, Location);
        Status = STATUS_SHUTDOWN_IN_PROGRESS;
        goto Exit;
    }

    Status = pInterfaceVnic->RegisterEoibNotification(port, FipChangeCallback);
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "pInterfaceVnic->RegisterEoibNotification failed Status = 0x%x\n", Status);
        goto Exit;
    }

Exit:
    m_FipInterfaceLock.Unlock();
    return Status;

}

NTSTATUS
FipInterface::FipRemoveEoibNotification(
    uint64_t UniqueId,
    uint32_t Location
    )
{
    InterfaceVnic *pInterfaceVnic = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    m_FipInterfaceLock.Lock();

    pInterfaceVnic = FindVnic(UniqueId, Location);
    if (pInterfaceVnic == NULL) {       
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FindVnic failed UniqueId=%I64d ,  Location = %d\n", UniqueId, Location);
        Status = STATUS_SHUTDOWN_IN_PROGRESS;
        goto Exit;
    }

    Status = pInterfaceVnic->RemoveGetVhubTableUpdate();
    if (!NT_SUCCESS(Status)) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "pInterfaceVnic->RemoveGetVhubTableUpdate failed Status = 0x%x\n", Status);
        goto Exit;
    }

Exit:
    m_FipInterfaceLock.Unlock();
    return Status;
}


NTSTATUS
FipInterface::GetVhubTableUpdate(
    uint64_t UniqueId ,
    uint32_t Location,
    InterfaceVhubUpdate_t** ppInterfaceVhubUpdate
    )
{
    InterfaceVnic *pInterfaceVnic = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    m_FipInterfaceLock.Lock();

    pInterfaceVnic = FindVnic(UniqueId, Location);
    if (pInterfaceVnic == NULL) {       
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FindVnic failed UniqueId=%I64d ,  Location = %d\n", UniqueId, Location);
        Status = STATUS_SHUTDOWN_IN_PROGRESS;
        goto Exit;
    }

    *ppInterfaceVhubUpdate = pInterfaceVnic->GetEntry();

Exit:
    m_FipInterfaceLock.Unlock();
    return Status;
}


NTSTATUS
FipInterface::FipAcquireDataQpn(
    uint64_t UniqueId ,
    uint32_t Location,
    int *DataQpn
    )
{
    InterfaceVnic *pInterfaceVnic = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    m_FipInterfaceLock.Lock();

    pInterfaceVnic = FindVnic(UniqueId, Location);
    if (pInterfaceVnic == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FindVnic failed UniqueId=%I64d ,  Location = %d\n", UniqueId, Location);
        Status = STATUS_SHUTDOWN_IN_PROGRESS;
        goto Exit;
    }

    *DataQpn = pInterfaceVnic->AddRef();

Exit:
    m_FipInterfaceLock.Unlock();
    return Status;
}


NTSTATUS
FipInterface::FipReleaseDataQpn(
    uint64_t UniqueId ,
    uint32_t Location
    )
{
    InterfaceVnic *pInterfaceVnic = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    m_FipInterfaceLock.Lock();

    pInterfaceVnic = FindVnic(UniqueId, Location);
    if (pInterfaceVnic == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FindVnic failed UniqueId=%I64d ,  Location = %d\n", UniqueId, Location);
        Status = STATUS_SHUTDOWN_IN_PROGRESS;
        goto Exit;
    }

    LONG RefCount = pInterfaceVnic->Release();
    if (RefCount == 0) {
        ASSERT(m_FipInterfaceVnics[Location] == pInterfaceVnic);
        m_FipInterfaceVnics[Location] = NULL;
        if (Location < MAXDEBUGVNICS) {
            FipInterfaceVnics[Location] = NULL;
        }
    }

Exit:
    m_FipInterfaceLock.Unlock();
    return Status;
}


NTSTATUS
FipInterface::FipGetBroadcastMgidParams(
    uint64_t UniqueId ,
    uint32_t Location,
    uint8_t  *pEthGidPrefix,
    uint32_t EthGidPrefixSize,
    uint8_t  *pRssHash,
    uint32_t *pVHubId
    )
{
    InterfaceVnic *pInterfaceVnic = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    m_FipInterfaceLock.Lock();

    pInterfaceVnic = FindVnic(UniqueId, Location);
    if (pInterfaceVnic == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FindVnic failed UniqueId=%I64d ,  Location = %d\n", UniqueId, Location);
        Status = STATUS_SHUTDOWN_IN_PROGRESS;
        goto Exit;
    }

    memcpy(pEthGidPrefix, pInterfaceVnic->m_EthGidPrefix, EthGidPrefixSize);
    *pRssHash = pInterfaceVnic->m_Rss;
    *pVHubId = pInterfaceVnic->m_VHubId;

    Exit:
        m_FipInterfaceLock.Unlock();
        return Status;
}


NTSTATUS 
FipInterface::FipGetMac(
    uint64_t UniqueId ,
    uint32_t Location,
    uint8_t  *pMac,
    uint32_t MacSize
    )
{
    InterfaceVnic *pInterfaceVnic = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    m_FipInterfaceLock.Lock();

    pInterfaceVnic = FindVnic(UniqueId, Location);
    if (pInterfaceVnic == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "FindVnic failed UniqueId=%I64d ,  Location = %d\n", UniqueId, Location);
        Status = STATUS_SHUTDOWN_IN_PROGRESS;
        goto Exit;
    }

    memcpy(pMac, pInterfaceVnic->m_Mac, MacSize);

    Exit:
        m_FipInterfaceLock.Unlock();
        return Status;
}


extern "C" {
    NTSTATUS FipUpdateCheckForHang(u64 UniqueId, u32 Location, boolean_t AllOK) {
        return g_pFipInterface->UpdateCheckForHang(UniqueId, Location, AllOK);
    }


    NTSTATUS FipGetVhubTableUpdate(uint64_t UniqueId , uint32_t Location, InterfaceVhubUpdate_t** ppInterfaceVhubUpdate ) {
        return g_pFipInterface->GetVhubTableUpdate(UniqueId, Location, ppInterfaceVhubUpdate);
    }


    VOID     FipReturnVhubTableUpdate(InterfaceVhubUpdate_t *pInterfaceVhubUpdate ) {
        delete pInterfaceVhubUpdate;
    }


    NTSTATUS FipGetLinkStatus(uint64_t /*UniqueId */, uint32_t /*Location */)  {
        return 0;//g_pFipInterface->GetLinkStatus(UniqueId, Location);
    }


    NTSTATUS FipRegisterEoibNotification(uint64_t UniqueId , uint32_t Location, void *port, FIP_CHANGE_CALLBACK FipChangeCallback) {
        return g_pFipInterface->FipRegisterEoibNotification(UniqueId , Location, port, FipChangeCallback);
    }


    NTSTATUS FipRemoveEoibNotification(uint64_t UniqueId , uint32_t Location) {
        return g_pFipInterface->FipRemoveEoibNotification(UniqueId , Location);
    }


    NTSTATUS FipAcquireDataQpn(uint64_t UniqueId , uint32_t Location, int *DataQpn) {
        return g_pFipInterface->FipAcquireDataQpn(UniqueId , Location, DataQpn);
    }


    NTSTATUS FipReleaseDataQpn(uint64_t UniqueId , uint32_t Location) {
        return g_pFipInterface->FipReleaseDataQpn(UniqueId , Location);
    }

    NTSTATUS FipGetBroadcastMgidParams(
        uint64_t UniqueId ,
        uint32_t Location,
        uint8_t  *pEthGidPrefix,
        uint32_t EthGidPrefixSize,
        uint8_t  *pRssHash,
        uint32_t *pVHubId
        )
    {
        return g_pFipInterface->FipGetBroadcastMgidParams(UniqueId , Location, pEthGidPrefix, EthGidPrefixSize, pRssHash, pVHubId);
    }


    NTSTATUS FipGetMac(
        uint64_t UniqueId ,
        uint32_t Location,
        uint8_t  *pMac,
        uint32_t MacSize
        )
    {
        return g_pFipInterface->FipGetMac(UniqueId , Location, pMac, MacSize);
    }

}

