/*++

Copyright (c) 2005-2009 Mellanox Technologies. All rights reserved.

Module Name:
	bus_stat.h

Abstract:
	Statistics Collector header file

Revision History:

Notes:

--*/

#pragma once

#include <ntddk.h>
#include "al_mgr.h"
#include "event_trace.h"

//
// restrictions
//

#define BUS_ST_MAX_DEVICES			8
#define BUS_ST_MAX_THREADS			4

//
// enums
// 

//
// structures
//

// device

typedef struct _bus_fdo_ext 	bus_fdo_ext_t;
typedef struct _bus_globals		bus_globals_t;
typedef struct _al_mgr			al_mgr_t;

#define BUS_ST_THREAD_POWER_DOWN	0
#define BUS_ST_THREAD_POWER_UP		1

typedef struct _BUS_ST_THREAD
{
	PKTHREAD			p_thread;
	char				thread_name[16];
	
} BUS_ST_THREAD, *PBUS_ST_THREAD;

typedef struct _BUS_ST_DEVICE
{
	boolean_t			valid;
	bus_fdo_ext_t		*p_fdo;
	BUS_ST_THREAD		thread[BUS_ST_MAX_THREADS];
	
} BUS_ST_DEVICE, *PBUS_ST_DEVICE;

// driver
typedef struct _BUS_ST_DRIVER
{
	bus_globals_t		*p_globals;
	cl_obj_mgr_t		*gp_obj_mgr;
	cl_async_proc_t 	*gp_async_proc_mgr;
	cl_async_proc_t 	*gp_async_pnp_mgr;
	cl_async_proc_t 	*gp_async_obj_mgr;
	al_mgr_t			*gp_al_mgr;
	
} BUS_ST_DRIVER, *PBUS_ST_DRIVER;

// driver stack

typedef struct _BUS_ST_STAT
{
	BUS_ST_DRIVER		drv;
	BUS_ST_DEVICE		dev[BUS_ST_MAX_DEVICES];
	
} BUS_ST_STAT, *PBUS_ST_STAT;

extern BUS_ST_STAT g_stat;

//
// functions 
//

void bus_st_dev_rmv( PBUS_ST_DEVICE p_stat );

PBUS_ST_DEVICE bus_st_dev_add();

void bus_st_init();

