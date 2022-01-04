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
    mp_rcv.cpp

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/

#pragma once

#define FIP_ALLOCATION_TAG 'FpiF'



#ifdef __cplusplus
extern "C" {
#endif
NTSTATUS FipDriverEntry(DRIVER_OBJECT *p_driver_obj, UNICODE_STRING *p_registry_path);

void
FipDrvUnload(
	IN				DRIVER_OBJECT				*p_driver_obj );



NTSTATUS
fip_bus_add_device(
    IN              PDRIVER_OBJECT              p_driver_obj,
    IN              PDEVICE_OBJECT              p_pdo );


NTSTATUS
fip_fdo_start(
    IN                  DEVICE_OBJECT* const    p_dev_obj,
    IN                  const   ib_net64_t      ci_ca_guid
    );

void
fip_fdo_stop_device(
    IN                  DEVICE_OBJECT* const    p_dev_obj,
    IN                  const   ib_net64_t      ci_ca_guid);

#ifdef __cplusplus
} // extern C
#endif


typedef struct {
    // Registry Parameters
    BOOLEAN         RunFip;
    ULONG           SendRingSize;
    ULONG           RecvRingSize;
    ULONG           NumHostVnics;

    // Other Global Params
    LONG            TotalDevice;
    KEVENT          TotalDeviceEvent;
    ib_al_handle_t  AlHandle;    
    ib_pnp_handle_t pnp_handle;
    KEVENT          RegCompleteEvent;
    KEVENT          UnregCompleteEvent;
} FipGlobals;

extern FipGlobals *g_pFipGlobals;


