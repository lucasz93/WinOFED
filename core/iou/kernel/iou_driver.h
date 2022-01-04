/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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



#if !defined _IOU_DRIVER_H_
#define _IOU_DRIVER_H_

#include "complib/cl_types.h"
#include "complib/cl_atomic.h"
#include "complib/cl_debug.h"
#include "complib/cl_mutex.h"
#include "complib/cl_qlist.h"
#include "complib/cl_ptr_vector.h"
#include "complib/cl_pnp_po.h"
#include "iba/ib_al.h"
#include "iou_ioc_mgr.h"

/* Safe string functions. */
#if WINVER == 0x500
/*
 * Windows 2000 doesn't support the inline version of safe strings.
 * Force the use of the library version of safe strings.
 */
#define NTSTRSAFE_LIB
#endif
#include <ntstrsafe.h>

extern uint32_t		g_iou_dbg_level;
extern uint32_t		g_iou_dbg_flags;

#if defined(EVENT_TRACING)
//
// Software Tracing Definitions 
//

#define WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(IOUCtlGuid,(A0090FEF,01BB,4617,AF1E,FD02FD5B24ED),  \
	WPP_DEFINE_BIT( IOU_DBG_ERROR) \
	WPP_DEFINE_BIT( IOU_DBG_DRV) \
	WPP_DEFINE_BIT( IOU_DBG_PNP) \
	WPP_DEFINE_BIT( IOU_DBG_POWER) \
	WPP_DEFINE_BIT( IOU_DBG_PORT) \
	WPP_DEFINE_BIT( IOU_DBG_IOU))



#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)
#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_FLAG_ENABLED(flags)(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= TRACE_LEVEL_VERBOSE)
#define WPP_FLAG_LOGGER(flags) WPP_LEVEL_LOGGER(flags)


// begin_wpp config
// IOU_ENTER(FLAG);
// IOU_EXIT(FLAG);
// USEPREFIX(IOU_PRINT, "%!STDPREFIX! [IOU] :%!FUNC!() :");
// USESUFFIX(IOU_ENTER, " [IOU] :%!FUNC!():[");
// USESUFFIX(IOU_EXIT, " [IOU] :%!FUNC!():]");
// end_wpp


#else


#include <evntrace.h>

/*
 * Debug macros
 */


#define IOU_DBG_ERR		(1 << 0)
#define IOU_DBG_DRV		(1 << 1)
#define IOU_DBG_PNP		(1 << 2)
#define IOU_DBG_POWER	(1 << 3)
#define IOU_DBG_PORT	(1 << 4)
#define IOU_DBG_IOU		(1 << 5)

#define IOU_DBG_ERROR	(CL_DBG_ERROR | IOU_DBG_ERR)
#define IOU_DBG_ALL		CL_DBG_ALL

#if DBG

// assignment of _level_ is need to to overcome warning C4127
#define IOU_PRINT(_level_,_flag_,_msg_) \
	{ \
		if( g_iou_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_iou_dbg_flags, _msg_ ); \
	}

#define IOU_PRINT_EXIT(_level_,_flag_,_msg_) \
	{ \
		if( g_iou_dbg_level >= (_level_) ) \
			CL_TRACE( _flag_, g_iou_dbg_flags, _msg_ );\
		IOU_EXIT(_flag_);\
	}

#define IOU_ENTER(_flag_) \
	{ \
		if( g_iou_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_ENTER( _flag_, g_iou_dbg_flags ); \
	}

#define IOU_EXIT(_flag_)\
	{ \
		if( g_iou_dbg_level >= TRACE_LEVEL_VERBOSE ) \
			CL_EXIT( _flag_, g_iou_dbg_flags ); \
	}


#else

#define IOU_PRINT(lvl, flags, msg)

#define IOU_PRINT_EXIT(_level_,_flag_,_msg_)

#define IOU_ENTER(_flag_)

#define IOU_EXIT(_flag_)


#endif


#endif //EVENT_TRACING

/*
 * Main header for IB Bus driver.
 */



/*
 * ALLOC_PRAGMA sections:
 *	PAGE
 *		Default pagable code.  Won't be locked in memory.
 *
 *	PAGE_PNP
 *		Code that needs to be locked in memory when the device is
 *		in the paging, crash dump, or hibernation path.
 */


/*
 * Device extension for the device object that serves as entry point for 
 * the interface and IOCTL requests.
 */
typedef struct _iou_fdo_ext
{
	cl_pnp_po_ext_t			cl_ext;

	/*
	 * Device power map returned by the bus driver for the device, used 
	 * when sending IRP_MN_SET_POWER for device state in response to 
	 * IRP_MN_SET_POWER for system state.
	 */
	DEVICE_POWER_STATE		po_state[PowerSystemMaximum];

	ioc_mgr_t				ioc_mgr;

}	iou_fdo_ext_t;

/*
 * Device extension for bus driver PDOs.
 */
typedef struct _iou_pdo_ext
{
	cl_pnp_po_ext_t			cl_ext;

	cl_list_item_t			list_item;

	/* All reported PDOs are children of an HCA. */
	ib_ca_handle_t			h_ca;

	/*
	 * CA GUID copy - in case we get IRPs after the CA
	 * handle has been released.
	 */
	net64_t					ca_guid;
	POWER_STATE				dev_po_state;

	/*
	 * Pointer to the bus root device extension.  Used to manage access to
	 * child PDO pointer vector when a child is removed politely.
	 */
	iou_fdo_ext_t			*p_parent_ext;

	/*
	 * The following two flags are exclusively set, but can both be FALSE.
	 * Flag that indicates whether the device is present in the system or not.
	 * This affects how a IRP_MN_REMOVE_DEVICE IRP is handled for a child PDO.
	 * This flag is cleared when:
	 *	- an HCA (for IPoIB devices) is removed from the system for all port
	 *	devices loaded for that HCA
	 *	- an IOU is reported as removed by the CIA.
	 */
	boolean_t				b_present;

	/*
	 * Flag that indicates whether the device has been reported to the PnP
	 * manager as having been removed.  That is, the device was reported
	 * in a previous BusRelations query and not in a subsequent one.
	 * This flag is set when
	 *	- the device is in the surprise remove state when the parent bus
	 *	device is removed
	 *	- the device is found to be not present during a BusRelations query
	 *	and thus not reported.
	 */
	boolean_t				b_reported_missing;
	struct _child_device_info			*p_pdo_device_info;
}	iou_pdo_ext_t;


/*
 * Global Driver parameters.
 */
typedef struct _iou_globals
{
	/* Driver object.  Used for creating child devices. */
	DRIVER_OBJECT			*p_driver_obj;
	struct _child_device_info_list *p_device_list;
	cl_mutex_t			list_mutex;
	cl_qlist_t			ca_ioc_map_list;
	PDEVICE_OBJECT			p_config_device;
}	iou_globals_t;


extern iou_globals_t	iou_globals;




#endif	/* !defined _IOU_DRIVER_H_ */
