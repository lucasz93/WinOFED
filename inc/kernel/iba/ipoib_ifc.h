/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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


#include "iba/ib_al_ifc.h"


#if !defined _IPOIB_IFC_H_
#define _IPOIB_IFC_H_


/****h* Access Layer/IPoIB Interface
* NAME
*	IPoIB Interface
*
* DESCRIPTION
*	Header file for the interface exported to IPoIB client drivers for access
*	to IB resources provided by HCAs.
*
*	The actual interface returned is an contains information about the
*	particular instance of an IPoIB device.
*********/


#define IPOIB_INTERFACE_DATA_VERSION		(10)

/* defined in several places */
#define	MAX_USER_NAME_SIZE	17

/****s* Access Layer: IPoIB Interface/ipoib_ifc_data_t
* NAME
*	ipoib_ifc_data_t
*
* DESCRIPTION
*	IPoIB interface datat.
*
*	The port guid combined from guid + PKEY 
*/


#define ETH_ALEN        6   /* MAC address length in bytes */


typedef struct {    // These are Vhub-Ctx-Table-Entry after parsing
    uint8_t  Valid;
    uint8_t  Rss;
    uint8_t  Mac[ETH_ALEN];
    uint32_t Qpn;
    uint8_t  Sl;
    uint16_t Lid;
} VnicCtxEntry_t;



typedef enum {
    CMD_ADD = 0,
    CMD_REMOVE,
    CMD_REMOVE_ALL
} InterfaceCommand_t;


typedef struct {
    InterfaceCommand_t  Cmd;
    VnicCtxEntry_t      VnicCtxEntry;
    LIST_ENTRY          Entry;
} InterfaceVhubUpdate_t;


static inline
const char *CmdToStr(
    InterfaceCommand_t Cmd
    )
{
    switch (Cmd)
    {
        case CMD_ADD:            return "Add One Vnic";
        case CMD_REMOVE:        return "Romove One Vnic";
        case CMD_REMOVE_ALL:    return "Remove All Vnics";
        default:                return "Unknown";
    }
}


typedef VOID (*FIP_CHANGE_CALLBACK)(VOID *Port);

typedef NTSTATUS (*UPDATE_CHECK_FOR_HANG)(uint64_t UniqueId, uint32_t Location, boolean_t AllOK);
typedef NTSTATUS (*REGISTER_EOIB_NOTIFICATION)(uint64_t UniqueId , uint32_t Location, void *port, FIP_CHANGE_CALLBACK FipChangeCallback);
typedef NTSTATUS (*REMOVE_EOIB_NOTIFICATION)(uint64_t UniqueId , uint32_t Location);
typedef NTSTATUS (*GET_VHUB_TABLE_UPDATE)(uint64_t UniqueId, uint32_t Location, InterfaceVhubUpdate_t **ppInterfaceVhubUpdate);
typedef NTSTATUS(*GET_LINK_STATUS) (uint64_t UniqueId, uint32_t Location);
typedef VOID (*RETURN_VHUB_TABLE_UPDATE) (InterfaceVhubUpdate_t *pInterfaceVhubUpdate);
typedef NTSTATUS(*ACQUIRE_DATA_QPN) (uint64_t UniqueId, uint32_t Location, int *DataQpn);
typedef NTSTATUS(*RELEASE_DATA_QPN) (uint64_t UniqueId, uint32_t Location);
typedef NTSTATUS(*GET_BROADCAST_MGID_PARAMS) (uint64_t UniqueId, uint32_t Location, uint8_t *pEthGidPrefix, uint32_t EthGidPrefixSize, uint8_t  *pRssHash, uint32_t *pVHubId);
typedef NTSTATUS(*GET_EOIB_MAC) (uint64_t UniqueId, uint32_t Location, uint8_t *pMac, uint32_t MacSize);


typedef struct {
	UPDATE_CHECK_FOR_HANG		UpdateCheckForHang;
	REGISTER_EOIB_NOTIFICATION	RegisterEoibNotification;
	REMOVE_EOIB_NOTIFICATION	RemoveEoibNotification;
	GET_VHUB_TABLE_UPDATE		GetVhubTableUpdate;
	RETURN_VHUB_TABLE_UPDATE	ReturnVhubTableUpdate;
	GET_LINK_STATUS 			GetLinkStatus;
	ACQUIRE_DATA_QPN			AcquireDataQpn;
	RELEASE_DATA_QPN			ReleaseDataQpn;
	GET_BROADCAST_MGID_PARAMS	GetBroadcastMgidParams;
	GET_EOIB_MAC				GetEoIBMac;
} ipoib_ifc_func;



NTSTATUS FipUpdateCheckForHang(uint64_t UniqueId, uint32_t Location, boolean_t AllOK);
NTSTATUS FipRegisterEoibNotification(uint64_t UniqueId , uint32_t Location, void *port, FIP_CHANGE_CALLBACK FipChangeCallback);
NTSTATUS FipRemoveEoibNotification(uint64_t UniqueId , uint32_t Location);
NTSTATUS FipGetVhubTableUpdate(uint64_t UniqueId , uint32_t Location, InterfaceVhubUpdate_t **ppInterfaceVhubUpdate );
VOID     FipReturnVhubTableUpdate(InterfaceVhubUpdate_t *pInterfaceVhubUpdate );
NTSTATUS FipGetLinkStatus(uint64_t UniqueId , uint32_t Location );
NTSTATUS FipAcquireDataQpn(uint64_t UniqueId , uint32_t Location, int *DataQpn);
NTSTATUS FipReleaseDataQpn(uint64_t UniqueId , uint32_t Location);
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
    uint32_t MacSize
    );


typedef struct _port_guid_pkey
{
	net64_t		guid;
	ib_net16_t	pkey;
	boolean_t	IsEoib;
	uint64_t	UniqueId;
	uint32_t	Location;
	char		name[MAX_USER_NAME_SIZE];
} port_guid_pkey_t;


/*
*	The ipoib_ifc_data_t structure 
*
* SYNOPSIS
*/
typedef struct _ipoib_ifc_data
{
    GUID                        driver_id;
	net64_t						ca_guid;
	port_guid_pkey_t			port_guid;
	uint8_t						port_num;
	ipoib_ifc_func				ibal_ipoib_ifc;

}	ipoib_ifc_data_t;
/*
* FIELDS
*   driver_id
*       GUID identifying the HCA driver, used to perform per-driver IBAT filtering.
*
*	ca_guid
*		HCA GUID for this IPoIB interface
*
*	port_guid
*		Port GUID for this IPoIB interface
*
*	port_num
*		Port Number GUID for this IPoIB interface
*
* SEE ALSO
*	
*********/



#endif	/* !defined _IPOIB_IFC_H_ */

/*
 * IPOIB interface GUID.  The GUID is defined outside the conditional include
 * on purpose so that it can be instantiated only once where it is actually
 * needed.  See the DDK docs section "Using GUIDs in Drivers" for more info.
 */
/* {B40DDB48-5710-487a-B812-6DAF56C7F423} */
DEFINE_GUID(GUID_IPOIB_INTERFACE_DATA, 
0xb40ddb48, 0x5710, 0x487a, 0xb8, 0x12, 0x6d, 0xaf, 0x56, 0xc7, 0xf4, 0x23);
