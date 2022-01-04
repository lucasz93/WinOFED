#include "ipoib_adapter.h"
#include "ipoib_debug.h"

#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_stat.tmh"
#endif 

IPOIB_ST_STAT g_stat;

void ipoib_st_dev_rmv( PIPOIB_ST_DEVICE p_stat )
{
	if ( p_stat )
		p_stat->valid = FALSE;
}

PIPOIB_ST_DEVICE ipoib_st_dev_add()
{
	int i;

	for ( i = 0; i < IPOIB_ST_MAX_DEVICES; ++i ) {
		if ( g_stat.dev[i].valid == FALSE ) {
			g_stat.dev[i].valid = TRUE;
			return &g_stat.dev[i];
		}
	}

	return NULL;
}

void ipoib_st_init()
{
	memset( &g_stat, 0, sizeof(g_stat) );
}

