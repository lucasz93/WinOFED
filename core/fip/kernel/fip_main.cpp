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
    fip_main.cpp

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

#include "fip_main.tmh"
#endif

FipGlobals g_FipGlobals;
FipGlobals *g_pFipGlobals = &g_FipGlobals;


REG_ENTRY FipRegTable[] = 
{
//    reg value name                                             Offset                                                                     Field size                                                            Default   Min      Max
    {REG_STRING_CONST("RunFip"),           REG_OFFSET(g_FipGlobals, RunFip),           REG_SIZE(g_FipGlobals, RunFip),           FALSE, FALSE, TRUE},
    {REG_STRING_CONST("SendRingSize"),     REG_OFFSET(g_FipGlobals, SendRingSize),     REG_SIZE(g_FipGlobals, SendRingSize),     1024,  128,   64000},
    {REG_STRING_CONST("RecvRingSize"),     REG_OFFSET(g_FipGlobals, RecvRingSize),     REG_SIZE(g_FipGlobals, RecvRingSize),     1024,  128,   64000},
    {REG_STRING_CONST("NumHostVnics"),     REG_OFFSET(g_FipGlobals, NumHostVnics),     REG_SIZE(g_FipGlobals, NumHostVnics),     0,     0,     4096/*MAX_VNIC_PER_PORT*/},
};


#define FIP_NUM_REG_PARAMS (sizeof (FipRegTable) / sizeof(REG_ENTRY))



extern "C" 
NTSTATUS
FipDriverEntry(
    IN PDRIVER_OBJECT /*DriverObject */,
    IN PUNICODE_STRING pRegistryPath
    )

{

    NTSTATUS Status = STATUS_SUCCESS;

    RtlFillMemory(&g_FipGlobals, sizeof g_FipGlobals, 0);

    Status = GenUtilsInit(FIP_ALLOCATION_TAG);
    ASSERT(Status == STATUS_SUCCESS);

    // Read the parameters from the registry.
    Status = ReadRegistry(pRegistryPath->Buffer, L"\\Parameters\\Fip", FipRegTable, FIP_NUM_REG_PARAMS);
    if (!NT_SUCCESS(Status)) {
          FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,"ReadRegistry failed Status = 0x%x\n", Status);
          goto error_new;
    }

#ifndef FIP_COMPILE
        g_FipGlobals.RunFip = false;
#endif

    if (g_FipGlobals.RunFip == FALSE) {
        return Status;
    }

    KeInitializeEvent(&g_FipGlobals.TotalDeviceEvent, SynchronizationEvent, TRUE );
    KeInitializeEvent(&g_FipGlobals.RegCompleteEvent, SynchronizationEvent, FALSE );
    
    KeInitializeEvent(&g_FipGlobals.UnregCompleteEvent, SynchronizationEvent, FALSE );


    g_pFipWorkerThread = NEW FipWorkerThread;
    if (g_pFipWorkerThread == NULL) {
        Status = STATUS_NO_MEMORY;
        goto error_new;
    }


    g_pFipInterface = NEW FipInterface;
    if (g_pFipInterface == NULL) {
        Status = STATUS_NO_MEMORY;
        goto error_FipInterface;
    }
    Status = g_pFipInterface->Init();
    if (!NT_SUCCESS(Status)) {
          FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,"g_pFipInterface->Init failed Status = 0x%x\n", Status);
          goto error_FipInterface_Init;
    }

    return Status;

error_FipInterface_Init:
    delete g_pFipInterface;
    g_pFipInterface = NULL;

error_FipInterface:
    delete g_pFipWorkerThread;
    g_pFipWorkerThread = NULL;

error_new:
    return Status;
   
}


void
FipDrvUnload(
	IN				DRIVER_OBJECT			*	/*p_driver_obj */)
{


    if (g_FipGlobals.RunFip == FALSE) {
        return ;
    }

    g_pFipInterface->Shutdown();
    delete g_pFipInterface;

    delete g_pFipWorkerThread;
    g_pFipWorkerThread = NULL;
}


static ib_api_status_t
FipPnpCb(
    IN              ib_pnp_rec_t                *p_pnp_rec )
{
    ib_api_status_t     status = IB_SUCCESS;

    CL_ASSERT( p_pnp_rec );

    FipGlobals *pFipGlobals = (FipGlobals *)p_pnp_rec->pnp_context;

    
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV ,
        "IPOIB-ETH: p_pnp_rec->pnp_event = 0x%x (%s)\n",
        p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event ) );


    switch( p_pnp_rec->pnp_event )
    {
        case IB_PNP_REG_COMPLETE:
            KeSetEvent( &pFipGlobals->RegCompleteEvent, IO_NETWORK_INCREMENT, FALSE );
            status = IB_SUCCESS;
            break;


        case IB_PNP_PORT_ADD:
        case IB_PNP_PORT_REMOVE:
        case IB_PNP_PORT_DOWN :
        case IB_PNP_PORT_ACTIVE :
        case IB_PNP_PKEY_CHANGE :
        case IB_PNP_SM_CHANGE :
        case IB_PNP_GID_CHANGE :
        case IB_PNP_LID_CHANGE :

        break;


    default:
        FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_DBG_DRV ,
            "IPOIB-ETH: Received unhandled PnP event 0x%x (%s)\n",
            p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event ) );

        
    }
//Cleanup:    
    return status;
}


static VOID
FipPnpUnregisterCallback(void *ctx)
{
    FipGlobals *pFipGlobals = (FipGlobals *)ctx;   
    KeSetEvent(&pFipGlobals->UnregCompleteEvent, 0, FALSE);
}

NTSTATUS EnumDevices(
    DEVICE_OBJECT* const p_dev_obj,
    const ib_net64_t ci_ca_guid,
    bool add)
{

    NTSTATUS Status = STATUS_SUCCESS;
    int i;
 
    al_ci_ca_t* p_ci_ca = find_ci_ca( ci_ca_guid );
    if (!p_ci_ca) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,"ci_ca guid not found 0x%I64x\n", ci_ca_guid);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for ( i = 0; i < p_ci_ca->num_ports; i++) {
        
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "Port found - 0x%I64x\n", p_ci_ca->port_array[i]);
        FipThreadMessage PnpThreadMessage;

        PnpThreadMessage.MessageType                = MESSAGE_TYPE_PNP;
        PnpThreadMessage.DeleteAfterUse             = false;
        PnpThreadMessage.Data.ib_pnp_rec.guid       = p_ci_ca->port_array[i];
        PnpThreadMessage.Data.ib_pnp_rec.pnp_event  = add ? IB_PNP_PORT_ADD : IB_PNP_PORT_REMOVE;
        PnpThreadMessage.pDeviceObj               = p_dev_obj;

        g_pFipWorkerThread->AddThreadMessage(&PnpThreadMessage);
        g_pFipWorkerThread->FlushMessages();
    }

    return Status;
}


VOID PnpDerigester()
{
    ib_api_status_t     ib_status;

    KeClearEvent(&g_FipGlobals.UnregCompleteEvent);
    ib_status = ib_dereg_pnp( g_FipGlobals.pnp_handle, FipPnpUnregisterCallback );
    if( ib_status != IB_SUCCESS )
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,
            "ib_dereg_pnp returned %s\n", ib_get_err_str( ib_status ) );
        ASSERT(FALSE);
    } else {
        KeWaitForSingleObject(&g_FipGlobals.UnregCompleteEvent, Executive, KernelMode, FALSE, NULL );
    }


}


NTSTATUS
fip_bus_add_device(
    IN              PDRIVER_OBJECT              /* p_driver_obj */,
    IN              PDEVICE_OBJECT              /* p_pdo */)
{

    NTSTATUS Status = STATUS_SUCCESS;

    if (g_FipGlobals.RunFip == FALSE) {
        return Status;
    }

    return Status;
}

NTSTATUS IbalRegister()
{
    ib_pnp_req_t        pnp_req;
    ib_api_status_t     ib_status;
    NTSTATUS Status = STATUS_SUCCESS;   
    

#pragma warning( push )
#pragma warning(disable: 4244 ) 
    ib_status = ib_open_al_trk(AL_WLOCATION, &g_FipGlobals.AlHandle);
#pragma warning( pop )
    if ( ib_status != IB_SUCCESS) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,
            "ib_open_al failed ib_status = 0x%d\n", ib_status );
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,
        "ib_open_al succeeded g_FipGlobals.AlHandle = %p\n", g_FipGlobals.AlHandle );

    Status = g_pFipWorkerThread->Init();
    if (!NT_SUCCESS(Status)) {
          FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,"g_FipGlobals.Init() failed Status = 0x%x\n", Status);
          goto err_FipWorkerThread;
    }


    /* Register for PNP events. */
    cl_memclr( &pnp_req, sizeof(pnp_req) );
    pnp_req.pnp_class = IB_PNP_PORT | IB_PNP_FLAG_REG_SYNC | IB_PNP_FLAG_REG_COMPLETE;
    /*
         * Context is the cl_obj of the adapter to allow passing cl_obj_deref
         * to ib_dereg_pnp.
         */
    pnp_req.pnp_context = &g_FipGlobals;
    pnp_req.pfn_pnp_cb = FipPnpCb;
    
    KeClearEvent(&g_FipGlobals.RegCompleteEvent);
    ib_status = ib_reg_pnp( g_FipGlobals.AlHandle, &pnp_req, &g_FipGlobals.pnp_handle );
    if( ib_status != IB_SUCCESS )
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,
            "ib_reg_pnp returned %s\n", ib_get_err_str( ib_status ) );
        Status = STATUS_INSUFFICIENT_RESOURCES;         
        goto err_ib_reg_pnp;
    }

    KeWaitForSingleObject(&g_FipGlobals.RegCompleteEvent, Executive, KernelMode, FALSE, NULL );
    return Status;

err_ib_reg_pnp:
    g_pFipWorkerThread->Shutdown();


err_FipWorkerThread:

    ASSERT(g_FipGlobals.AlHandle != NULL);
    ib_close_al(g_FipGlobals.AlHandle);
    g_FipGlobals.AlHandle = NULL;

exit:
    return Status;

}


VOID IbalDeregister()
{
    PnpDerigester();
    g_pFipWorkerThread->Shutdown();

    ASSERT(g_FipGlobals.AlHandle != NULL);
    ib_close_al(g_FipGlobals.AlHandle);
    g_FipGlobals.AlHandle = NULL;
}


extern "C" 
NTSTATUS
fip_fdo_start(
    IN                  DEVICE_OBJECT* const    p_dev_obj,
    IN                  const   ib_net64_t      ci_ca_guid)
{
    NTSTATUS Status = STATUS_SUCCESS;   

    if (g_FipGlobals.RunFip == FALSE) {
        return Status;
    }
    
    Status = KeWaitForSingleObject(&g_FipGlobals.TotalDeviceEvent, Executive, KernelMode , FALSE, NULL);
    ASSERT(Status == STATUS_SUCCESS);

    g_FipGlobals.TotalDevice++;
    if(g_FipGlobals.TotalDevice == 1) {

        Status = IbalRegister();
        if (!NT_SUCCESS(Status)) {
              FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,"IbalRegister() failed Status = 0x%x\n", Status);
              goto err_IbalRegister;
        }
    }

    Status = EnumDevices(p_dev_obj, ci_ca_guid, true);
    if (!NT_SUCCESS(Status)) {
          FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,"g_FipGlobals.Init() failed Status = 0x%x\n", Status);
          goto exit;
    }

    KeSetEvent(&g_FipGlobals.TotalDeviceEvent, 0, FALSE);
    return Status;


exit:

    g_FipGlobals.TotalDevice--;
    if(g_FipGlobals.TotalDevice == 0) {
        IbalDeregister();
    }
    

err_IbalRegister:
    KeSetEvent(&g_FipGlobals.TotalDeviceEvent, 0, FALSE);
    return Status;
}

extern "C" 
void
fip_fdo_stop_device(
    IN                  DEVICE_OBJECT* const    p_dev_obj,
    IN                  const   ib_net64_t      ci_ca_guid)
{

    NTSTATUS Status = STATUS_SUCCESS;
    
    if (g_FipGlobals.RunFip == FALSE) {
        return;
    }

    Status = EnumDevices(p_dev_obj, ci_ca_guid, false);
    if (!NT_SUCCESS(Status)) {
          FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV,"g_FipGlobals.Init() failed Status = 0x%x\n", Status);
          ASSERT(FALSE);
    }

    Status = KeWaitForSingleObject(&g_FipGlobals.TotalDeviceEvent, Executive, KernelMode , FALSE, NULL);
    ASSERT(Status == STATUS_SUCCESS);


    g_FipGlobals.TotalDevice--;
    if(g_FipGlobals.TotalDevice == 0) {
        IbalDeregister();
    } else {
        g_pFipWorkerThread->FlushMessages();
    }
    
    KeSetEvent(&g_FipGlobals.TotalDeviceEvent, 0, FALSE);

    
}
