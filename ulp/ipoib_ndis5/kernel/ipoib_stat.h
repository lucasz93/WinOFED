/*++

Copyright (c) 2005-2009 Mellanox Technologies. All rights reserved.

Module Name:
	ipoib_stat.h

Abstract:
	Statistics Collector header file

Revision History:

Notes:

--*/

#pragma once

#include <ntddk.h>

//
// restrictions
//

#define IPOIB_ST_MAX_DEVICES			16

//
// enums
// 

//
// structures
//

// device
typedef struct _ipoib_adapter ipoib_adapter_t;
typedef struct _ipoib_port ipoib_port_t;


typedef struct _IPOIB_ST_DEVICE
{
	boolean_t			valid;				// all the structure is valid
	ipoib_adapter_t		*p_adapter;			// current
	ipoib_port_t		*p_prev_port;		// previous value of p_port in p_adapter
	PRKTHREAD			p_halt_thread;		// thread, calling ipoib_halt
	int					n_power_irps;		// NdisDevicePnPEventPowerProfileChanged 
	int					n_pnp_irps;			// NdisDevicePnPEventSurpriseRemoved 
	
} IPOIB_ST_DEVICE, *PIPOIB_ST_DEVICE;

// driver
typedef struct _IPOIB_ST_DRIVER
{
	PDRIVER_OBJECT		obj;
	
} IPOIB_ST_DRIVER, *PIPOIB_ST_DRIVER;

// driver stack

typedef struct _IPOIB_ST_STAT
{
	IPOIB_ST_DRIVER		drv;
	IPOIB_ST_DEVICE		dev[IPOIB_ST_MAX_DEVICES];
	
} IPOIB_ST_STAT, *PIPOIB_ST_STAT;

extern IPOIB_ST_STAT g_stat;

//
// functions 
//

void ipoib_st_dev_rmv( PIPOIB_ST_DEVICE p_stat );

PIPOIB_ST_DEVICE ipoib_st_dev_add();

void ipoib_st_init();



