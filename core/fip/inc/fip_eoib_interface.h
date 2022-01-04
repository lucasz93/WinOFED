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
    fip_eoib_interface.h

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/


#pragma once

class InterfaceVnic {
public:
    InterfaceVnic() : 
        m_LocalDataQpn(0),
        m_RefCount(1),
        m_pBusIbInterface(NULL),
        m_VHubId(0),
        m_Rss(0)
            {
                memset(m_EthGidPrefix, 0, sizeof(m_EthGidPrefix));
            };

    ~InterfaceVnic() { 
        ASSERT(m_RefCount == 0);
        RemoveAllEntries();
        ReleaseQPRange();
    }


    // Called by the fip driver

    NTSTATUS Init(
        char                    *UniqeStr,
        int                     LocalDataQpn, // Only 24 Bit valid, Host-Order
        PMLX4_BUS_IB_INTERFACE  pBusIbInterface
        );


    VOID NotifyLinkStateChange();

    VOID AddEntry(
        InterfaceVhubUpdate_t *pInterfaceVhubUpdate
        );

    VOID NotifyEoibDriver() {

        if(m_VhubContext == NULL) {
            // so far nothing to do
            return;
        }
        // notify that a change exists
        m_VhubUpdateFunction(m_VhubContext);
        m_VhubContext = NULL;

    }

    // Called by the eoib driver

    VOID UpdateCheckForHang(boolean_t AllOK) {

        m_LastCheckForHangTime = GetTickCountInMsec();
        m_CheckForHangOk = !!AllOK;
    }


    InterfaceVhubUpdate_t* GetEntry( VOID );


    VOID RemoveAllEntries( VOID );


    NTSTATUS RegisterEoibNotification(void *port, FIP_CHANGE_CALLBACK FipChangeCallback) {
        ASSERT (m_VhubContext == NULL);
        m_VhubContext = port;
        m_VhubUpdateFunction = FipChangeCallback;
        return STATUS_SUCCESS;

    }

    NTSTATUS RemoveGetVhubTableUpdate( VOID ) {
        if (m_VhubContext == NULL) {
            return STATUS_PENDING;
        }

        m_VhubContext = NULL;
        return STATUS_SUCCESS;

    }

    int AddRef( VOID );

    LONG Release( VOID );

    VOID UpdateMgidParams(
        u8  EthGidPrefix[GID_PREFIX_LEN],
        u32 VHubId,
        u8  Rss
        ) {
        memcpy(m_EthGidPrefix, EthGidPrefix, sizeof(m_EthGidPrefix));
            m_VHubId = VHubId;
            m_Rss = Rss;
        }

    VOID UpdateMac( 
        u8 Mac[HW_ADDR_LEN]
        ) {
        memcpy(m_Mac, Mac, sizeof(m_Mac));
        }

    u64                     m_UniqueId;
    char                    m_UniqeStr[UNIQE_GW_LEN + 1]; // For DBG only

    u8                      m_Mac[HW_ADDR_LEN];
    u8                      m_EthGidPrefix[GID_PREFIX_LEN];
    u32                     m_VHubId;
    u8                      m_Rss;

private:

    VOID ConfigChanged();

    VOID ReleaseQPRange( VOID );

    bool                    m_CheckForHangOk;
    u64                     m_LastCheckForHangTime;

    InterfaceVhubUpdate_t   m_InterfaceVhubUpdate;

    void                    *m_VhubContext;
    FIP_CHANGE_CALLBACK     m_VhubUpdateFunction;

    // Ref Counter for Local Data QPN
    int                     m_LocalDataQpn; // Only 24 Bit valid, Host-Order
    LONG                    m_RefCount;
    PMLX4_BUS_IB_INTERFACE  m_pBusIbInterface;

    // Params for UpdateVhubEntry
    LinkedList              m_VhubListHead;
};

class FipInterface {

public:
    FipInterface() :
        m_Shutdown(false) 
            {}

    ~FipInterface() { ASSERT(m_Shutdown == true); }

    NTSTATUS Init();

    VOID Shutdown();


    //
    // Called by the eoib driver
    //

    NTSTATUS UpdateCheckForHang(
        u64 UniqueId,
        u32 Location,
        boolean_t AllOK
        );

    NTSTATUS RegisterNotifyInterface( VOID );

    NTSTATUS GetVhubTableEntry(
        u64 UniqueId,
        u32 Location,
        InterfaceVhubUpdate_t *pInterfaceVhubUpdate
        );

    NTSTATUS GetLinkStatus(
        u64 UniqueId,
        u32 Location
        );
   
    NTSTATUS FipRegisterEoibNotification(
        uint64_t UniqueId ,
        uint32_t Location,
        void *port,
        FIP_CHANGE_CALLBACK FipChangeCallback
        );
    
    NTSTATUS FipRemoveEoibNotification(
        uint64_t UniqueId ,
        uint32_t Location
        );

    NTSTATUS GetVhubTableUpdate(
        uint64_t UniqueId ,
        uint32_t Location,
        InterfaceVhubUpdate_t** ppInterfaceVhubUpdate
        );

    NTSTATUS FipAcquireDataQpn(
        uint64_t UniqueId ,
        uint32_t Location,
        int *DataQpn
        );


    NTSTATUS FipReleaseDataQpn(
        uint64_t UniqueId ,
        uint32_t Location
        );

    NTSTATUS FipGetBroadcastMgidParams(
        uint64_t UniqueId ,
        uint32_t Location,
        uint8_t  *pEthGidPrefix,
        uint32_t EthGidPrefixSize,
        uint8_t  *pRssHash,
        uint32_t *pVHubId
        );

    NTSTATUS FipGetMac(
        uint64_t UniqueId ,
        uint32_t Location,
        uint8_t  *pMac,
        uint32_t  MacSize
        );


    //
    // Called by the fip code
    //

    // When A new Vnic is created and became connected the Fip Vnic will call this function to create it's IF
    NTSTATUS AddVnic(
        u64                     *pUniqueId,
        u32                     *pLocation,
        char                    *UniqeStr,
        int                     LocalDataQpn,
        PMLX4_BUS_IB_INTERFACE  pBusIbInterface,
        InterfaceVnic           **ppInterfaceVnic
        );

    // Once the Fip vnic is destroyed we have to call this function to destroy it's IF.
    VOID RemoveVnic(
        InterfaceVnic *pInterfaceVnic,
        u32 Location
        );

    // Each Vnic will update for it's known neighbours (add or remove them)
    VOID UpdateVhubEntry(
        InterfaceVnic *pInterfaceVnic,
        InterfaceVhubUpdate_t *pInterfaceVhubUpdate
        );

    NTSTATUS UpdateLinkStatus(InterfaceVnic *pInterfaceVnic);

    NTSTATUS GetEoibStatus(
        InterfaceVnic *pInterfaceVnic,
        boolean_t *pAllOK
        );

private:

    VOID Lock( VOID ) {
        m_FipInterfaceLock.Lock();
    }

    VOID Unlock( VOID ) {
        m_FipInterfaceLock.Unlock();
    }

    InterfaceVnic *FindVnic(u64 UniqueId, u32 Location) {
        ASSERT(Location < (u32)m_FipInterfaceVnics.GetMaxSize());
        if(m_FipInterfaceVnics[Location] && 
           m_FipInterfaceVnics[Location]->m_UniqueId == UniqueId) {
            return m_FipInterfaceVnics[Location];           
        }
        return NULL;
    }

    bool m_Shutdown;
    u64 m_UniqueIdCounter;

    CSpinLock               m_FipInterfaceLock;

    static const int MAXDEBUGVNICS = 8;

    Array <InterfaceVnic *>   m_FipInterfaceVnics;
    InterfaceVnic *FipInterfaceVnics[MAXDEBUGVNICS]; // For debug only
};

extern FipInterface* g_pFipInterface;

