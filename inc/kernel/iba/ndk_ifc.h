/*
 * Copyright (c) 2010 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2009 Intel Corporation. All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <ndis.h>

#ifdef __cplusplus
}
#endif


#define NDK_VERSION_MAJOR		1
#define NDK_VERSION_MINOR		1

//
// NDK FW Version
//
const int x_NDK_FW_REV_MAJOR = 2;
const int x_NDK_FW_REV_SUBMINOR = 9;
const int x_NDK_FW_REV_MINOR = 8350;

typedef VOID *	NDK_HANDLE;

typedef enum _ndk_provider_type {
	NDK_PROVIDER_TYPE_IPOIB,
	NDK_PROVIDER_TYPE_ROCE
	
} ndk_provider_type_t;

typedef struct _ndk_ib_data
{
#define NDK_VALID_CA_GUID		(1 << 0)
#define NDK_VALID_PORT_GUID		(1 << 1)
#define NDK_VALID_PORT_PKEY		(1 << 2)
#define NDK_VALID_ALL			(NDK_VALID_CA_GUID | NDK_VALID_PORT_GUID | NDK_VALID_PORT_PKEY)


	PDEVICE_OBJECT				p_pdo;			// MANDATORY: PDO for requesting GUID_IB_AL_INTERFACE
	net64_t						ca_guid;		// OPTIONAL; HCA guid
	net64_t						port_guid;		// OPTIONAL; port guid
	ib_net16_t					port_pkey;		// OPTIONAL; port pkey
	uint8_t						port_num;		// MANDATORY: port number (1 or 2)
	uint8_t						valid;			// MANDATORY: validity FLAGS
	ndk_provider_type_t			provider_type;	// MANDATORY: provider type

}	ndk_ib_data_t;

VOID
ndk_create(
	IN				PDRIVER_OBJECT				p_drv_obj,
	IN				UNICODE_STRING* const		p_registry_path,
	IN				NDIS_HANDLE					driver_handle
	);

VOID
ndk_delete(
	);

NTSTATUS 
ndk_adapter_init(
	IN				NDIS_HANDLE const		h_adapter,
	IN				uint8_t*				mac,
	IN				ndk_provider_type_t		provider_type,	// MANDATORY: provider type
	IN				PDEVICE_OBJECT			p_pdo,			// MANDATORY: PDO for requesting GUID_IB_AL_INTERFACE
	IN				uint8_t					port_num,		// MANDATORY: port number (1 or 2)
	IN				uint8_t					valid,			// MANDATORY: validity FLAGS
	IN				net64_t					ca_guid,		// OPTIONAL; HCA guid
	IN				net64_t					port_guid,		// OPTIONAL; port guid
	IN				ib_net16_t				port_pkey,		// OPTIONAL; port pkey
	OUT 			NDK_HANDLE* 			ph_ndk
	);

VOID 
ndk_adapter_deinit(
	IN				NDK_HANDLE				h_ndk
	);

NDIS_STATUS
ndk_prov_set_handlers(
	IN				NDIS_HANDLE 			NdisMiniportDriverHandle
	);

VOID
ndk_enable_disable(
	IN			NDK_HANDLE 			h_ndk,
    IN			BOOLEAN 			RequestedState
    );

// Ethernet adapter should provide this function
NDK_HANDLE
ndk_get_ndk_handle(
	IN				NDIS_HANDLE 			adapter_context
	);

NDIS_STATUS
ndk_query_ndk_stat(
	 IN 			 NDK_HANDLE 	h_ndk,
	OUT 			PVOID			p_buf
	);

NDIS_STATUS
ndk_query_general_stat(
	 IN 			 NDK_HANDLE 	h_ndk,
	OUT 			PVOID			p_buf
	);

NDIS_STATUS
ndk_ca_enumerate_connections(
        IN  NDK_HANDLE h_ndk,
        __inout_bcount_part_opt(InformationBufferLength, *pBytesWritten)PVOID InformationBuffer,
        __in ULONG InformationBufferLength, 
        __out PULONG pBytesWritten,
        __out PULONG pBytesNeeded
    );

NDIS_STATUS
ndk_ca_enumerate_local_endpoints(
        IN  NDK_HANDLE h_ndk,
        __inout_bcount_part_opt(InformationBufferLength, *pBytesWritten)PVOID InformationBuffer,
        __in ULONG InformationBufferLength, 
        __out PULONG pBytesWritten,
        __out PULONG pBytesNeeded
    );

VOID
ndk_notify_power_change(
    IN          NDK_HANDLE          h_ndk,
    IN          BOOLEAN             power_up
    );

