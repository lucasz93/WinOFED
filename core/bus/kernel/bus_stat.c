/*++

Copyright (c) 2005-2009 Mellanox Technologies. All rights reserved.

Module Name:
	bus_stat.h

Abstract:
	Statistics Collector header file

Revision History:

Notes:

--*/


#include <precomp.h>

#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "bus_stat.tmh"
#endif 

BUS_ST_STAT g_stat;

void bus_st_dev_rmv( PBUS_ST_DEVICE p_stat )
{
	if ( p_stat )
		p_stat->valid = FALSE;
}

PBUS_ST_DEVICE bus_st_dev_add()
{
	int i;

	for ( i = 0; i < BUS_ST_MAX_DEVICES; ++i ) {
		if ( g_stat.dev[i].valid == FALSE ) {
			g_stat.dev[i].valid = TRUE;
			return &g_stat.dev[i];
		}
	}

	return NULL;
}

void bus_st_init()
{
	memset( &g_stat, 0, sizeof(g_stat) );
}

