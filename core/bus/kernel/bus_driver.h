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



#if !defined _BUS_DRIVER_H_
#define _BUS_DRIVER_H_

#include "complib/cl_types.h"
#include "complib/cl_atomic.h"
#include "complib/cl_debug.h"
#include "complib/cl_mutex.h"
#include "complib/cl_qlist.h"
#include "complib/cl_ptr_vector.h"
#include "complib/cl_pnp_po.h"
#include "iba/ib_al.h"
#include "bus_port_mgr.h"
#include "bus_iou_mgr.h"
#include "al_dev.h"
#include <rdma/verbs.h>
#include "bus_stat.h"

/* Safe string functions. */
#if WINVER == 0x500
/*
 * Windows 2000 doesn't support the inline version of safe strings.
 * Force the use of the library version of safe strings.
 */
#define NTSTRSAFE_LIB
#endif
#include <ntstrsafe.h>


/*
 * Main header for IB Bus driver.
 */

#define BUS_ENTER( lvl )			\
	CL_ENTER( lvl, bus_globals.dbg_lvl )

#define BUS_EXIT( lvl )				\
	CL_EXIT( lvl, bus_globals.dbg_lvl )

#define BUS_TRACE( lvl, msg )		\
	CL_TRACE( lvl, bus_globals.dbg_lvl, msg )

#define BUS_TRACE_EXIT( lvl, msg )	\
	CL_TRACE_EXIT( lvl, bus_globals.dbg_lvl, msg )

#define BUS_PRINT( lvl, msg )		\
	CL_PRINT( lvl, bus_globals.dbg_lvl, msg )

/* single character Macro elemination */
#define XBUS_PRINT( lvl, msg )
#define XBUS_ENTER( lvl )
#define XBUS_EXIT( lvl )

#define BUS_DBG_ERROR	CL_DBG_ERROR
#define BUS_DBG_DRV		(1 << 0)
#define BUS_DBG_PNP		(1 << 1)
#define BUS_DBG_POWER	(1 << 2)
#define BUS_DBG_PORT	(1 << 3)
#define BUS_DBG_IOU		(1 << 4)

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
typedef struct _bus_fdo_ext
{
	cl_pnp_po_ext_t			cl_ext;

	/*
	 * Device power map returned by the bus driver for the device, used 
	 * when sending IRP_MN_SET_POWER for device state in response to 
	 * IRP_MN_SET_POWER for system state.
	 */
	DEVICE_POWER_STATE		po_state[PowerSystemMaximum];

	/* Number of references on the upper interface. */
	atomic32_t				n_al_ifc_ref;
	/* Number of references on the CI interface. */
	atomic32_t				n_ci_ifc_ref;
	/* Number of references on the CM interface. */
	atomic32_t				n_cm_ifc_ref;
	/* HCA interface stuff */
	RDMA_INTERFACE_VERBS	hca_ifc;
	boolean_t				hca_ifc_taken;
	/* notify interface */
	MLX4_BUS_NOTIFY_INTERFACE	notify_ifc;
	boolean_t				notify_ifc_taken;
	/* Number of references on all interfaces. */
	atomic32_t				n_ifc_ref;

	struct _bus_filter_instance *bus_filter;

	boolean_t				fip_started;
	boolean_t				fdo_started;

	/* flag, indicating that FDO resources have been released */
	boolean_t				ca_registered;
	/* current device power state */
	DEVICE_POWER_STATE		device_power_state;
	/* current system  power state */
	SYSTEM_POWER_STATE		system_power_state;
	/* work item for power operations */
	PIO_WORKITEM			p_po_work_item;
	/* current PnP state */
	PNP_DEVICE_STATE		pnp_state; /* state for PnP Manager */
	/* statistics */
	PBUS_ST_DEVICE			p_stat;
	/* driver object */
	DRIVER_OBJECT			*p_driver_obj;
	/* exit event */
	KEVENT					exit_event;
	/* flag, indicating that "mcast_mgr_add_ca_ports succeeded  */
	boolean_t				mcast_mgr_created;

}	bus_fdo_ext_t;


/* Windows pnp device information */
#define MAX_DEVICE_ID_LEN     200
#define MAX_DEVICE_STRING_LEN 		MAX_DEVICE_ID_LEN + 2	//add extra 4 bytes in case we need double NULL ending
typedef struct _child_device_info {
	wchar_t		device_id[MAX_DEVICE_STRING_LEN];  
	uint32_t    device_id_size;
	wchar_t     compatible_id[MAX_DEVICE_STRING_LEN];
	uint32_t    compatible_id_size;
	wchar_t     hardware_id[MAX_DEVICE_STRING_LEN];
	uint32_t    hardware_id_size;
	wchar_t     description[MAX_DEVICE_STRING_LEN];
	uint32_t    description_size;
	wchar_t     pkey[8 + MAX_USER_NAME_SIZE];
	boolean_t	is_eoib;
}  child_device_info_t;

typedef struct _child_device_info_list{
	child_device_info_t io_device_info;
	struct _child_device_info_list *next_device_info;
}child_device_info_list_t;


/*
 * Device extension for bus driver PDOs.
 */
typedef struct _bus_pdo_ext
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
	bus_fdo_ext_t			*p_parent_ext;

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

	/* Flag to control the behaviour of the driver during hibernation */
	uint32_t				b_hibernating;

	/* work item for handling Power Management request */
	PIO_WORKITEM			p_po_work_item;
	boolean_t				hca_acquired;
	child_device_info_t		*p_pdo_device_info;
}	bus_pdo_ext_t;

/* pkey configuration */
typedef struct _pkey_conf_t
{
	pkey_array_t    pkeys_per_port;
	struct _pkey_conf_t *next_conf;
}pkey_conf_t;


/*
 * Global Driver parameters.
 */
typedef struct _bus_globals
{
	/* Debug level. */
	uint32_t				dbg_lvl;

	/* Flag to control loading of Ip Over Ib driver for each HCA port. */
	uint32_t				b_report_port_nic;

	/* Driver object.  Used for registering of Plug and Play notifications. */
	DRIVER_OBJECT			*p_driver_obj;

	/* IBAL PNP event register handles */
	ib_pnp_handle_t			h_pnp_port;
	ib_pnp_handle_t			h_pnp_iou;

	/* pkey array to be read */
	pkey_conf_t				*p_pkey_conf;

	/* saved devices info*/
	child_device_info_list_t *p_device_list;

	/* IBAL started flag */
	ULONG					started;

}	bus_globals_t;


extern bus_globals_t	bus_globals;

/*
 * Each instance of a bus filter on an HCA device stack (InfiniBandController)
 * populates a bus_filter_t slot in g_bus_filters[MAX_BUS_FILTERS]; see
 * bus_add_device(). Therefore MAX_BUS_FILTERS represents the MAX number of
 * HCA's supported in a single system.
 */
#define MAX_BUS_FILTERS	64
#define BFI_MAGIC		0xcafebabe

#define BFI_PORT_MGR_OBJ	1
#define BFI_IOU_MGR_OBJ		2


typedef struct _bus_filter_instance
{
	/* Pointer to the bus filter instance FDO extention.
	 * if p_bus_ext is NULL, then it's an empty slot available for allocation
	 */
	bus_fdo_ext_t			*p_bus_ext;

	/* HCA guid for which this bus filter is servicing */
	ib_net64_t				ca_guid;

	/* PORT management - on a per HCA basis */
	port_mgr_t				*p_port_mgr;
	cl_obj_t				*p_port_mgr_obj;

	/* IOU management - on a per HCA basis */
	iou_mgr_t				*p_iou_mgr;
	cl_obj_t				*p_iou_mgr_obj;
#if DBG
	ULONG					magic; // initial/temp debug
	char					whoami[8];
#endif

}	bus_filter_t;

extern bus_filter_t	g_bus_filters[MAX_BUS_FILTERS];
extern ULONG g_bfi_InstanceCount;
extern KEVENT g_ControlEvent;	// serializes InstanceCount & g_bus_filters

extern bus_filter_t *alloc_bfi( IN DRIVER_OBJECT *, OUT int * );
extern int free_bfi( IN bus_filter_t *p_bfi );
extern int get_bfi_count( void );
extern bus_filter_t *get_bfi_by_obj( IN int obj_type, IN cl_obj_t *p_obj );
extern bus_filter_t *get_bfi_by_ca_guid( IN net64_t ca_guid );
extern char *get_obj_state_str(cl_state_t state);

inline VOID lock_control_event() {
	KeWaitForSingleObject(&g_ControlEvent, Executive, KernelMode , FALSE, NULL);
}

inline VOID unlock_control_event() {
	KeSetEvent(&g_ControlEvent, 0, FALSE);
}

#endif	/* !defined _BUS_DRIVER_H_ */
