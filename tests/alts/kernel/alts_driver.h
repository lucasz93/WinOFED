/* BEGIN_ICS_COPYRIGHT ****************************************
** END_ICS_COPYRIGHT   ****************************************/



#if !defined( _ALTS_DRIVER_H_ )
#define _ALTS_DRIVER_H_


#include <complib/cl_types.h>
#include <complib/cl_pnp_po.h>
#include <complib/cl_mutex.h>
#include "alts_debug.h"


typedef struct _alts_dev_ext
{
	cl_pnp_po_ext_t					cl_ext;

}	alts_dev_ext_t;


#endif	/* !defined( _ALTS_DRIVER_H_ ) */
