/*++
Copyright (c) 1990-2000    Microsoft Corporation All Rights Reserved

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define a WMI GUID to get MLX4_HCA info.
// (used in hca\wmi.c)
//

// {2C4C8445-E4A6-45bc-889B-E5E93551DDAF}
DEFINE_GUID(MLX4_HCA_WMI_STD_DATA_GUID, 
0x2c4c8445, 0xe4a6, 0x45bc, 0x88, 0x9b, 0xe5, 0xe9, 0x35, 0x51, 0xdd, 0xaf);



//
// Define a WMI GUID to get MLX4_BUS info.
// (used in bus\wmi.c)
//

// {3337968C-F117-4289-84C2-04EF74CBAD77}
DEFINE_GUID(MLX4_BUS_WMI_STD_DATA_GUID, 
0x3337968c, 0xf117, 0x4289, 0x84, 0xc2, 0x4, 0xef, 0x74, 0xcb, 0xad, 0x77);



//
// Define a GUID for MLX4_BUS upper (IB) interface.
// (used in hca\drv.c)
//

// {48AC3404-269E-4ab0-B5F3-9EF15AA79D0C}
DEFINE_GUID(MLX4_BUS_IB_INTERFACE_GUID, 
0x48ac3404, 0x269e, 0x4ab0, 0xb5, 0xf3, 0x9e, 0xf1, 0x5a, 0xa7, 0x9d, 0xc);


// {B8D50000-21B8-4636-A0EE-00E27C772548}
DEFINE_GUID(MLX4_BUS_FC_INTERFACE_GUID, 
0xb8d50000, 0x21b8, 0x4636, 0xa0, 0xee, 0x0, 0xe2, 0x7c, 0x77, 0x25, 0x48);


//
// Define the MLX4_BUS type GUID.
// (used in bus\drv.c for responding to the IRP_MN_QUERY_BUS_INFORMATION)
//

// {CF9E3C49-48D1-45b5-ABD7-CBCA7D954DF4}
DEFINE_GUID(MLX4_BUS_TYPE_GUID, 
0xcf9e3c49, 0x48d1, 0x45b5, 0xab, 0xd7, 0xcb, 0xca, 0x7d, 0x95, 0x4d, 0xf4);



//
// Installation Class for MLX4 BUS driver 
// (used in bus\mlx4_bus.inf)
//

// {714995B2-CD65-4a47-BCFE-95AC73A0D780}



//
// Installation Class for MLX4 HCA driver 
// (used in hca\mlx4_hca.inf)
//

// {31B0B28A-26FF-4dca-A6FA-E767C7DFBA20}


#if 0

//
// Define an Interface Guid for mxe device class.
// This GUID is used to register (IoRegisterDeviceInterface) 
// an instance of an interface so that user application 
// can control the mxe device.
//

DEFINE_GUID (GUID_DEVINTERFACE_MXE, 
        0x781EF630, 0x72B2, 0x11d2, 0xB8, 0x52, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
//{781EF630-72B2-11d2-B852-00C04FAD5171}

//
// Define a Setup Class GUID for Mxe Class. This is same
// as the TOASTSER CLASS guid in the INF files.
//
//leo
DEFINE_GUID (GUID_DEVCLASS_MXEETHER, 
        0x4d36e972, 0xe325, 0x11ce, 0xBF, 0xC1, 0x08, 0x00, 0x2b, 0xE1, 0x03, 0x18);
//{4d36e972-e325-11ce-bfc1-08002be10318}

//
// Define a WMI GUID to get mxe device info.
//

DEFINE_GUID (MXE_WMI_STD_DATA_GUID, 
        0xBBA21300L, 0x6DD3, 0x11d2, 0xB8, 0x44, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);

//
// Define a WMI GUID to represent device arrival notification WMIEvent class.
//

DEFINE_GUID (MXE_NOTIFY_DEVICE_ARRIVAL_EVENT, 
        0x1cdaff1, 0xc901, 0x45b4, 0xb3, 0x59, 0xb5, 0x54, 0x27, 0x25, 0xe2, 0x9c);
// {01CDAFF1-C901-45b4-B359-B5542725E29C}


//leo The Guid was taken from devguid.h
//DEFINE_GUID( GUID_DEVCLASS_INFINIBAND,          0x30ef7132L, 0xd858, 0x4a0c, 0xac, 0x24, 0xb9, 0x02, 0x8a, 0x5c, 0xca, 0x3f );


#endif

//
// GUID definition are required to be outside of header inclusion pragma to avoid
// error during precompiled headers.
//

#ifndef __PUBLIC_H
#define __PUBLIC_H

#define BUS_HARDWARE_IDS   L"MLX4\\ConnectX_Hca\0"
#define ETH_HARDWARE_IDS   L"MLX4\\ConnectX_Eth\0"
#define ETH_COMPATIBLE_IDS L"MLX4\\ConnectX_Eth\0"



#define BUS_HARDWARE_DESCRIPTION L"Mellanox ConnectX Virtual Infiniband Adapter (#%02d)"

#ifndef HP_PROD
#define ETH_HARDWARE_DESCRIPTION L"Mellanox ConnectX Virtual Ethernet Adapter (#%02d)"
#else
#define ETH_HARDWARE_DESCRIPTION L"HP NC542m Dual Port Flex-10 10GbE BL-c Adapter (#%02d)"
#endif

#define FCOE_HARDWARE_IDS L"MLX4\\ConnectX_FCoE\0"
#define FCOE_HARDWARE_DESCRIPTION L"Mellanox ConnectX FCoE Adapter (#%02d)"

#endif


