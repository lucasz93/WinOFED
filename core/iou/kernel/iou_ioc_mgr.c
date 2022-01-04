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



#include <iba/ib_types.h>
#include <complib/cl_async_proc.h>
#include <complib/cl_bus_ifc.h>
#include "iou_driver.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "iou_ioc_mgr.tmh"
#endif
#include "iou_pnp.h"
#include "iou_ioc_mgr.h"
#include <initguid.h>
#include <wdmguid.h>
#include "iba/ioc_ifc.h"


/* {5A9649F4-0101-4a7c-8337-796C48082DA2} */
DEFINE_GUID(GUID_BUS_TYPE_IBA,
0x5a9649f4, 0x101, 0x4a7c, 0x83, 0x37, 0x79, 0x6c, 0x48, 0x8, 0x2d, 0xa2);


/*
 * Size of device descriptions, as defined in
 *	A1.2.3.1.1 - Creating Compatibility Strings for an I/O Controller
 */
#define IOC_DEV_ID_SIZE		\
	sizeof(L"IBA\\VxxxxxxPxxxxxxxxSxxxxxxsxxxxxxxxvxxxx")
#define IOC_HW_ID_SIZE		\
	sizeof(L"IBA\\VxxxxxxPxxxxxxxxSxxxxxxsxxxxxxxxvxxxx") + \
	sizeof(L"IBA\\VxxxxxxPxxxxxxxxSxxxxxxsxxxxxxxx") + \
	sizeof(L"IBA\\VxxxxxxPxxxxxxxxvxxxx") + \
	sizeof(L"IBA\\VxxxxxxPxxxxxxxx\0\0")
#define IOC_COMPAT_ID_SIZE	\
	sizeof(L"IBA\\Cxxxxcxxxxpxxxxrxxxx") + \
	sizeof(L"IBA\\Cxxxxcxxxxpxxxx\0\0")
#define IOC_LOCATION_SIZE	\
	sizeof(L"Chassis 0xxxxxxxxxxxxxxxxx, Slot xx, IOC xx")

/*
 * Device extension for IOU PDOs.
 */
typedef struct _ioc_ext
{
	iou_pdo_ext_t			pdo;

	ib_ioc_info_t			info;
	ib_svc_entry_t			svc_entries[1];

}	ioc_ext_t;


/*
 * Function prototypes.
 */
void
destroying_ioc_mgr(
	IN				cl_obj_t*					p_obj );

void
free_ioc_mgr(
	IN				cl_obj_t*					p_obj );

ib_api_status_t
ioc_mgr_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec );

ib_api_status_t
ioc_mgr_ioc_add(
	IN				ib_pnp_ioc_rec_t*			p_pnp_rec );

void
ioc_mgr_ioc_remove(
	IN				ib_pnp_ioc_rec_t*			p_pnp_rec );

static NTSTATUS
ioc_start(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static void
ioc_release_resources(
	IN				DEVICE_OBJECT* const		p_dev_obj );

static NTSTATUS
ioc_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
ioc_surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
ioc_query_capabilities(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
ioc_query_target_relations(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
ioc_query_device_id(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
ioc_query_hardware_ids(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
ioc_query_compatible_ids(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
ioc_query_unique_id(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
ioc_query_description(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
ioc_query_location(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
ioc_query_bus_info(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
ioc_query_interface(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
ioc_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );



/*
 * Global virtual function pointer tables shared between all
 * instances of Port PDOs.
 */
static const cl_vfptr_pnp_po_t		vfptr_ioc_pnp = {
	"IB IOC",
	ioc_start,
	cl_irp_succeed,
	cl_irp_succeed,
	cl_irp_succeed,
	cl_irp_succeed,
	ioc_release_resources,
	ioc_remove,
	cl_irp_succeed,
	ioc_surprise_remove,
	ioc_query_capabilities,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_succeed,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	ioc_query_target_relations,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	ioc_query_bus_info,
	ioc_query_interface,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_succeed,				// QueryPower
	ioc_set_power,				// SetPower
	cl_irp_unsupported,			// PowerSequence
	cl_irp_unsupported			// WaitWake
};


static const cl_vfptr_query_txt_t		vfptr_iou_query_txt = {
	ioc_query_device_id,
	ioc_query_hardware_ids,
	ioc_query_compatible_ids,
	ioc_query_unique_id,
	ioc_query_description,
	ioc_query_location
};


void
ioc_mgr_construct(
	IN	OUT			ioc_mgr_t* const			p_ioc_mgr )
{
	IOU_ENTER( IOU_DBG_PNP );

	/* Construct the IOC manager service. */
	cl_obj_construct( &p_ioc_mgr->obj, 0 );
	cl_mutex_construct( &p_ioc_mgr->pdo_mutex );
	cl_qlist_init( &p_ioc_mgr->pdo_list );

	IOU_EXIT( IOU_DBG_PNP );
}


ib_api_status_t
ioc_mgr_init(
	IN	OUT			ioc_mgr_t* const			p_ioc_mgr )
{
	ib_pnp_req_t		pnp_req;
	ib_api_status_t		status;
	cl_status_t			cl_status;

	IOU_ENTER( IOU_DBG_PNP );

	cl_status = cl_mutex_init( &p_ioc_mgr->pdo_mutex );
	if( cl_status != CL_SUCCESS )
	{
		free_ioc_mgr( &p_ioc_mgr->obj );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("cl_mutex_init returned %#x.\n", cl_status) );
		return IB_ERROR;
	}

	cl_status = cl_mutex_init( &p_ioc_mgr->pdo_mutex );
	if( cl_status != CL_SUCCESS )
	{
		free_ioc_mgr( &p_ioc_mgr->obj );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("cl_mutex_init returned %#x.\n", cl_status) );
		return IB_ERROR;
	}	

	/* Initialize the load service object. */
	cl_status = cl_obj_init( &p_ioc_mgr->obj, CL_DESTROY_SYNC,
		destroying_ioc_mgr, NULL, free_ioc_mgr );
	if( cl_status != CL_SUCCESS )
	{
		free_ioc_mgr( &p_ioc_mgr->obj );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("cl_obj_init returned %#x.\n", cl_status) );
		return IB_ERROR;
	}

	status = p_ioc_mgr->ifc.open_al_trk( AL_WLOCATION, &p_ioc_mgr->h_al );
	if( status != IB_SUCCESS )
	{
		cl_obj_destroy( &p_ioc_mgr->obj );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("open_al returned %s.\n",
			p_ioc_mgr->ifc.get_err_str(status)) );
		return status;
	}

	/* Register for IOC PnP events. */
	cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
	pnp_req.pnp_class	= IB_PNP_IOC;
	pnp_req.pnp_context = p_ioc_mgr;
	pnp_req.pfn_pnp_cb	= ioc_mgr_pnp_cb;

	status = p_ioc_mgr->ifc.reg_pnp(
		p_ioc_mgr->h_al, &pnp_req, &p_ioc_mgr->h_pnp );
	if( status != IB_SUCCESS )
	{
		cl_obj_destroy( &p_ioc_mgr->obj );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("ib_reg_pnp returned %s.\n",
			p_ioc_mgr->ifc.get_err_str(status)) );
		return status;
	}

	/* Reference the load service on behalf of the ib_reg_pnp call. */
	cl_obj_ref( &p_ioc_mgr->obj );

	IOU_EXIT( IOU_DBG_PNP );
	return IB_SUCCESS;
}


/*
 * Pre-destroy the load service.
 */
void
destroying_ioc_mgr(
	IN				cl_obj_t*					p_obj )
{
	ioc_mgr_t				*p_ioc_mgr;
	ib_api_status_t			status;

	IOU_ENTER( IOU_DBG_PNP );

	CL_ASSERT( p_obj );

	p_ioc_mgr = PARENT_STRUCT( p_obj, ioc_mgr_t, obj );

	/* Deregister for port PnP events. */
	if( p_ioc_mgr->h_pnp )
	{
		status = p_ioc_mgr->ifc.dereg_pnp(
			p_ioc_mgr->h_pnp, (ib_pfn_destroy_cb_t)cl_obj_deref );
		CL_ASSERT( status == IB_SUCCESS );
	}
	IOU_EXIT( IOU_DBG_PNP );
}


/*
 * Free the load service.
 */
void
free_ioc_mgr(
	IN				cl_obj_t*					p_obj )
{
	ioc_mgr_t		*p_ioc_mgr;
	ioc_ext_t		*p_iou_ext;
	cl_list_item_t	*p_list_item;

	IOU_ENTER( IOU_DBG_PNP );

	CL_ASSERT( p_obj );
	p_ioc_mgr = PARENT_STRUCT( p_obj, ioc_mgr_t, obj );

	/*
	 * Mark all IOCs as no longer present.  This will cause them
	 * to be removed when they process the IRP_MN_REMOVE_DEVICE.
	 */
	p_list_item = cl_qlist_remove_head( &p_ioc_mgr->pdo_list );
	while( p_list_item != cl_qlist_end( &p_ioc_mgr->pdo_list ) )
	{
		p_iou_ext = PARENT_STRUCT(
			PARENT_STRUCT( p_list_item, iou_pdo_ext_t, list_item ),
			ioc_ext_t, pdo );
		p_list_item = cl_qlist_remove_head( &p_ioc_mgr->pdo_list );
		if( p_iou_ext->pdo.cl_ext.pnp_state == SurpriseRemoved )
		{
			CL_ASSERT( !p_iou_ext->pdo.b_present );
			p_iou_ext->pdo.b_reported_missing = TRUE;
			continue;
		}
		IoDeleteDevice( p_iou_ext->pdo.cl_ext.p_self_do );
	}

	cl_mutex_destroy( &p_ioc_mgr->pdo_mutex );
	cl_obj_deinit( p_obj );
	IOU_EXIT( IOU_DBG_PNP );
}


/*
 * Load service PnP event callback.
 */
ib_api_status_t
ioc_mgr_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec )
{
	ib_api_status_t		status;
	ioc_mgr_t			*p_ioc_mgr;

	IOU_ENTER( IOU_DBG_PNP );

	CL_ASSERT( p_pnp_rec );
	p_ioc_mgr = (ioc_mgr_t*)p_pnp_rec->pnp_context;

	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_IOC_ADD:
		status = ioc_mgr_ioc_add( (ib_pnp_ioc_rec_t*)p_pnp_rec );
		break;

	case IB_PNP_IOC_REMOVE:
		ioc_mgr_ioc_remove( (ib_pnp_ioc_rec_t*)p_pnp_rec );

	default:
		status = IB_SUCCESS;
		break;
	}
	IOU_EXIT( IOU_DBG_PNP );
	return status;
}


/*
 * Called to get child relations for the bus root.
 */
NTSTATUS
ioc_mgr_get_iou_relations(
	IN				ioc_mgr_t* const			p_ioc_mgr,
	IN				IRP* const					p_irp )
{
	NTSTATUS			status;
	size_t				n_devs;
	DEVICE_RELATIONS	*p_rel;

	IOU_ENTER( IOU_DBG_PNP );

	/* If there are already relations, copy them. */
	cl_mutex_acquire( &p_ioc_mgr->pdo_mutex );
	n_devs = cl_qlist_count( &p_ioc_mgr->pdo_list );	
	if( !n_devs )
	{
		cl_mutex_release( &p_ioc_mgr->pdo_mutex );
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
			("No child PDOs.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	/* Add space for our child IOUs. */
	status = cl_alloc_relations( p_irp, n_devs );
	if( !NT_SUCCESS( status ) )
	{
		cl_mutex_release( &p_ioc_mgr->pdo_mutex );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("cl_alloc_relations returned %08x.\n", status) );
		return status;
	}

	p_rel = (DEVICE_RELATIONS*)p_irp->IoStatus.Information;
	update_relations( &p_ioc_mgr->pdo_list, p_rel );
	cl_mutex_release( &p_ioc_mgr->pdo_mutex );
	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}

BOOLEAN is_vnic_profile_ioc(ib_ioc_profile_t	*p_profile)
{
	BOOLEAN b_vnic = FALSE;

	if( ib_ioc_profile_get_vend_id( p_profile ) == 0x00066a &&
		p_profile->dev_id == CL_HTON32(0x00000030) ) 
	{

		b_vnic = TRUE;
	}

	return b_vnic;
	
}

ib_api_status_t
ioc_mgr_ioc_add(
	IN				ib_pnp_ioc_rec_t*			p_pnp_rec )
{
	NTSTATUS		status;
	DEVICE_OBJECT	*p_pdo = NULL;
	iou_fdo_ext_t	*p_ext;
	ioc_mgr_t		*p_ioc_mgr;
	ioc_ext_t		*p_ioc_ext;
	uint32_t		ext_size;
	child_device_info_list_t *pCurList;
	BOOLEAN			b_vnic_ioc = FALSE;
	ca_ioc_map_t		*p_map = NULL;

	IOU_ENTER( IOU_DBG_PNP );

	p_ioc_mgr = PARENT_STRUCT( p_pnp_rec->pnp_rec.pnp_context, ioc_mgr_t, obj );
	p_ext = PARENT_STRUCT( p_ioc_mgr, iou_fdo_ext_t, ioc_mgr );

	if( p_pnp_rec->ca_guid != p_ioc_mgr->info.ca_guid ||
		p_pnp_rec->info.chassis_guid != p_ioc_mgr->info.chassis_guid ||
		p_pnp_rec->info.chassis_slot != p_ioc_mgr->info.slot||
		p_pnp_rec->info.iou_guid != p_ioc_mgr->info.guid )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
			("IOC not in this IOU.\n") );
		return IB_NOT_DONE;
	}

	p_map = cl_zalloc(sizeof(ca_ioc_map_t));

	if ( !p_map ) 
	{
		IOU_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IOU_DBG_PNP, 
				(" Insufficient Memory.\n" ));
		return IB_INSUFFICIENT_MEMORY;
	}

	p_map->ca_guid = p_pnp_rec->ca_guid;
	p_map->info = p_pnp_rec->info;
	p_map->p_ioc_mgr = p_ioc_mgr;
	cl_memcpy( p_map->svc_entry_array, p_pnp_rec->svc_entry_array,
			p_pnp_rec->info.profile.num_svc_entries );

	cl_mutex_acquire(&iou_globals.list_mutex);
	cl_qlist_insert_tail( &iou_globals.ca_ioc_map_list, &p_map->ioc_list);
	cl_mutex_release( &iou_globals.list_mutex );

	b_vnic_ioc = is_vnic_profile_ioc( &p_pnp_rec->info.profile );
	pCurList = iou_globals.p_device_list;

	if ( b_vnic_ioc && !pCurList ) 
	{
		IOU_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IOU_DBG_PNP, 
				("There is no device list for VNIC IOCs\n"));
		return IB_NOT_DONE;
	}

	ext_size = sizeof(ioc_ext_t) +
		(sizeof(ib_svc_entry_t) * p_pnp_rec->info.profile.num_svc_entries);

	do
	{
		if ( b_vnic_ioc ) 
		{
			if ( p_pnp_rec->ca_guid != 
				pCurList->io_device_info.ca_ioc_path.ca_guid ||
				p_pnp_rec->info.profile.ioc_guid != 
				pCurList->io_device_info.ca_ioc_path.info.profile.ioc_guid)
			{
				pCurList = pCurList->next_device_info;
				continue;
			}
		}
	/* Create the PDO for the new port device. */
	status = IoCreateDevice( iou_globals.p_driver_obj, ext_size,
		NULL, FILE_DEVICE_CONTROLLER,
		FILE_DEVICE_SECURE_OPEN | FILE_AUTOGENERATED_DEVICE_NAME,
		FALSE, &p_pdo );
	if( !NT_SUCCESS( status ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("IoCreateDevice returned %08x.\n", status) );
		return IB_ERROR;
	}

	/* Initialize the device extension. */
	cl_init_pnp_po_ext( p_pdo, NULL, p_pdo, g_iou_dbg_flags,
		&vfptr_ioc_pnp, &vfptr_iou_query_txt );

	/* Set the DO_BUS_ENUMERATED_DEVICE flag to mark it as a PDO. */
	p_pdo->Flags |= DO_BUS_ENUMERATED_DEVICE;

	p_ioc_ext = p_pdo->DeviceExtension;
	p_ioc_ext->pdo.dev_po_state.DeviceState = PowerDeviceD0;
	p_ioc_ext->pdo.p_parent_ext = p_ext;
	p_ioc_ext->pdo.b_present = TRUE;
	p_ioc_ext->pdo.b_reported_missing = FALSE;
	p_ioc_ext->pdo.ca_guid = p_pnp_rec->ca_guid;
		

		if ( b_vnic_ioc ) 
		{
			p_ioc_ext->pdo.p_pdo_device_info = &pCurList->io_device_info;
		}

		
	/* Copy the IOC profile and service entries. */
	p_ioc_ext->info = p_pnp_rec->info;
	cl_memcpy( p_ioc_ext->svc_entries, p_pnp_rec->svc_entry_array,
		p_pnp_rec->info.profile.num_svc_entries );
	/* Make sure the IOC string is null terminated. */
	p_ioc_ext->info.profile.id_string[CTRL_ID_STRING_LEN-1] = '\0';

	/* Store the device extension in the PDO list for future queries. */
	cl_mutex_acquire( &p_ioc_mgr->pdo_mutex );
		cl_qlist_insert_tail( &p_ioc_mgr->pdo_list,
		&p_ioc_ext->pdo.list_item );
	cl_mutex_release( &p_ioc_mgr->pdo_mutex );

		IoInvalidateDeviceRelations( p_ext->cl_ext.p_pdo, BusRelations );

		if ( b_vnic_ioc ) 
		{
			pCurList = pCurList->next_device_info;
		} 
		else 
		{
			break;
		}


	} while( pCurList );

	/*
	 * Set the context of the PNP event.  The context is passed in for future
	 * events on the same port.
	 */

	//p_pnp_rec->pnp_rec.context = p_ioc_ext;
	/* Tell the PnP Manager to rescan for bus relations. */

	IOU_EXIT( IOU_DBG_PNP );
	return IB_SUCCESS;
}


ca_ioc_map_t	*find_ca_ioc_map(net64_t	ca_guid,
				 ib_net64_t	ioc_guid/*ib_pnp_ioc_rec_t	*p_pnp_rec*/)
{
	cl_list_item_t	*list_item;
	ca_ioc_map_t	*p_map = NULL;

	cl_mutex_acquire( &iou_globals.list_mutex );

	list_item = cl_qlist_head( &iou_globals.ca_ioc_map_list );

	while( list_item != cl_qlist_end( &iou_globals.ca_ioc_map_list ) ) 
	{
		p_map = ( ca_ioc_map_t * ) list_item;
		if ( p_map->ca_guid != ca_guid || 
		    p_map->info.profile.ioc_guid != ioc_guid )
		{

			list_item = cl_qlist_next( list_item );
			continue;
		}
		break;
	}

	if ( list_item == cl_qlist_end( &iou_globals.ca_ioc_map_list ) )
		p_map = NULL;

	cl_mutex_release( &iou_globals.list_mutex );
	return p_map;
}

void
ioc_mgr_ioc_remove(
	IN				ib_pnp_ioc_rec_t*			p_pnp_rec )
{
	ioc_mgr_t	*p_ioc_mgr;
	ioc_ext_t	*p_ioc_ext;
	iou_pdo_ext_t	*p_iou_pdo_ext;

	ca_ioc_map_t	*p_map;
	cl_list_item_t	*list_item;

	IOU_ENTER( IOU_DBG_PNP );

	p_ioc_mgr = PARENT_STRUCT( p_pnp_rec->pnp_rec.pnp_context, ioc_mgr_t, obj );

	p_map = find_ca_ioc_map( p_pnp_rec->ca_guid, 
				p_pnp_rec->info.profile.ioc_guid );

	if ( p_map ) 
	{
		cl_mutex_acquire( &iou_globals.list_mutex );
		cl_qlist_remove_item( &iou_globals.ca_ioc_map_list, &p_map->ioc_list );
		cl_mutex_release( &iou_globals.list_mutex );
		cl_free(p_map);
	} 

	list_item = cl_qlist_head(&p_ioc_mgr->pdo_list); 

	while ( list_item != cl_qlist_end(&p_ioc_mgr->pdo_list) )
	{
		p_iou_pdo_ext = PARENT_STRUCT(list_item, iou_pdo_ext_t, list_item);
		p_ioc_ext = PARENT_STRUCT(p_iou_pdo_ext, ioc_ext_t, pdo);

		if ( p_pnp_rec->ca_guid != p_ioc_ext->pdo.ca_guid ||
		    p_pnp_rec->info.profile.ioc_guid != 
		    p_ioc_ext->info.profile.ioc_guid ) 
		{
			list_item = cl_qlist_next( list_item );
			continue;
		}
	cl_mutex_acquire( &p_ioc_mgr->pdo_mutex );
	p_ioc_ext->pdo.b_present = FALSE;
		cl_mutex_release( &p_ioc_mgr->pdo_mutex );

		list_item = cl_qlist_next( list_item );
	/* Invalidate bus relations for the bus root. */
	IoInvalidateDeviceRelations(
		p_ioc_ext->pdo.p_parent_ext->cl_ext.p_pdo, BusRelations );


	}

	IOU_EXIT( IOU_DBG_PNP );
}


static NTSTATUS
ioc_start(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp,
		OUT				cl_irp_action_t* const	p_action )
{
	iou_pdo_ext_t	*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	UNUSED_PARAM( p_irp );

	p_ext = p_dev_obj->DeviceExtension;

	/* Notify the Power Manager that the device is started. */
	PoSetPowerState( p_dev_obj, DevicePowerState, p_ext->dev_po_state );

	*p_action = IrpComplete;
	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static void
ioc_release_resources(
	IN					DEVICE_OBJECT* const	p_dev_obj )
{
	ioc_mgr_t		*p_ioc_mgr;
	ioc_ext_t		*p_ext;
	POWER_STATE		po_state;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;
	p_ioc_mgr = &p_ext->pdo.p_parent_ext->ioc_mgr;

	/* Remove this PDO from its list. */
	cl_mutex_acquire( &p_ioc_mgr->pdo_mutex );
	IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
		("Removing IOC from list.\n") );
	cl_qlist_remove_item( &p_ioc_mgr->pdo_list, &p_ext->pdo.list_item );
	cl_mutex_release( &p_ioc_mgr->pdo_mutex );
	po_state.DeviceState = PowerDeviceD3;
	PoSetPowerState( p_ext->pdo.cl_ext.p_pdo, DevicePowerState, po_state );

	IOU_EXIT( IOU_DBG_PNP );
}


static NTSTATUS
ioc_remove(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp,
		OUT				cl_irp_action_t* const	p_action )
{
	ioc_ext_t	*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;

	if( p_ext->pdo.b_present )
	{
		CL_ASSERT( p_ext->pdo.cl_ext.pnp_state != NotStarted );
		CL_ASSERT( !p_ext->pdo.b_reported_missing );
		/* Reset the state to NotStarted.  CompLib set it to Deleted. */
		cl_set_pnp_state( &p_ext->pdo.cl_ext, NotStarted );
		/* Don't delete the device.  It may simply be disabled. */
		*p_action = IrpComplete;
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
			("Device still present.\n") );
		return STATUS_SUCCESS;
	}

	if( !p_ext->pdo.b_reported_missing )
	{
		/* Reset the state to RemovePending.  Complib set it to Deleted. */
		cl_rollback_pnp_state( &p_ext->pdo.cl_ext );
		*p_action = IrpComplete;
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
			("Device not reported missing yet.\n") );
		return STATUS_SUCCESS;
	}

	/* Wait for all I/O operations to complete. */
	IoReleaseRemoveLockAndWait( &p_ext->pdo.cl_ext.remove_lock, p_irp );

	/* Release resources if it was not done yet. */
	if( p_ext->pdo.cl_ext.last_pnp_state != SurpriseRemoved )
		p_ext->pdo.cl_ext.vfptr_pnp_po->pfn_release_resources( p_dev_obj );

	p_irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	IoDeleteDevice( p_dev_obj );

	*p_action = IrpDoNothing;
	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	ioc_ext_t	*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	UNUSED_PARAM( p_irp );

	p_ext = p_dev_obj->DeviceExtension;
	p_ext->pdo.b_present = FALSE;
	p_ext->pdo.b_reported_missing = TRUE;

	*p_action = IrpComplete;

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_query_capabilities(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp,
		OUT				cl_irp_action_t* const	p_action )
{
	DEVICE_CAPABILITIES		*p_caps;
	IO_STACK_LOCATION		*p_io_stack;

	IOU_ENTER( IOU_DBG_PNP );

	UNUSED_PARAM( p_dev_obj );

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	p_caps = p_io_stack->Parameters.DeviceCapabilities.Capabilities;

	p_caps->DeviceD1 = FALSE;
	p_caps->DeviceD2 = FALSE;
	p_caps->LockSupported = FALSE;
	p_caps->EjectSupported = FALSE;
	p_caps->Removable = TRUE;
	p_caps->DockDevice = FALSE;
	p_caps->UniqueID = TRUE;
	p_caps->SilentInstall = TRUE;
	p_caps->RawDeviceOK = FALSE;
	p_caps->SurpriseRemovalOK = FALSE;
	p_caps->WakeFromD0 = FALSE;
	p_caps->WakeFromD1 = FALSE;
	p_caps->WakeFromD2 = FALSE;
	p_caps->WakeFromD3 = FALSE;
	p_caps->HardwareDisabled = FALSE;
	p_caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
	p_caps->DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
	p_caps->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
	p_caps->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
	p_caps->DeviceState[PowerSystemHibernate] = PowerDeviceD3;
	p_caps->DeviceState[PowerSystemShutdown] = PowerDeviceD3;
	p_caps->SystemWake = PowerSystemUnspecified;
	p_caps->DeviceWake = PowerDeviceUnspecified;
	p_caps->D1Latency = 0;
	p_caps->D2Latency = 0;
	p_caps->D3Latency = 0;

	*p_action = IrpComplete;
	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_query_target_relations(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp,
		OUT				cl_irp_action_t* const	p_action )
{
	NTSTATUS			status;
	DEVICE_RELATIONS	*p_rel;

	IOU_ENTER( IOU_DBG_PNP );

	*p_action = IrpComplete;

	status = cl_alloc_relations( p_irp, 1 );
	if( !NT_SUCCESS( status ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("cl_alloc_relations returned 0x%08x.\n", status) );
		return status;
	}

	p_rel = (DEVICE_RELATIONS*)p_irp->IoStatus.Information;
	p_rel->Count = 1;
	p_rel->Objects[0] = p_dev_obj;

	ObReferenceObject( p_dev_obj );

	IOU_EXIT( IOU_DBG_PNP );
	return status;
}


static NTSTATUS
ioc_query_device_id(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	ioc_ext_t			*p_ext;
	WCHAR				*p_string;
	uint32_t			dev_id_size;
	NTSTATUS			status;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = (ioc_ext_t*)p_dev_obj->DeviceExtension;

	if( !p_ext->pdo.b_present )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	if (p_ext->pdo.p_pdo_device_info) 
	{

		dev_id_size = (p_ext->pdo.p_pdo_device_info)->device_id_size;
		p_string = ExAllocatePoolWithTag( NonPagedPool, dev_id_size, 'didq' );

		if( !p_string )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
				( "Failed to allocate device ID buffer (%u bytes).\n",
				dev_id_size ) );
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		RtlZeroMemory( p_string, dev_id_size );

		cl_memcpy( p_string, p_ext->pdo.p_pdo_device_info->device_id, dev_id_size );
	} 
	else 
	{
	p_string = ExAllocatePoolWithTag( NonPagedPool, IOC_DEV_ID_SIZE, 'didq' );
	if( !p_string )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to allocate device ID buffer (%d bytes).\n",
			IOC_DEV_ID_SIZE) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = RtlStringCbPrintfW( p_string, IOC_DEV_ID_SIZE,
		L"IBA\\V%06xP%08xS%06xs%08xv%04x",
		ib_ioc_profile_get_vend_id( &p_ext->info.profile ),
		cl_ntoh32( p_ext->info.profile.dev_id ),
		ib_ioc_profile_get_subsys_vend_id( &p_ext->info.profile ),
		cl_ntoh32( p_ext->info.profile.subsys_id ),
		cl_ntoh16( p_ext->info.profile.dev_ver ) );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to format device ID string.\n") );
		return status;
	}
		
	}

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_query_hardware_ids(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	ioc_ext_t			*p_ext;
	WCHAR				*p_string, *p_start;
	size_t				size;
	uint32_t			V,P,S,s, hw_id_size;
	uint16_t			v;
	NTSTATUS			status;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = (ioc_ext_t*)p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	if (p_ext->pdo.p_pdo_device_info) 
	{
		hw_id_size = p_ext->pdo.p_pdo_device_info->hardware_id_size;

		p_string = ExAllocatePoolWithTag( NonPagedPool, hw_id_size, 'ihqi' );
		if( !p_string )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
				( "Failed to allocate hardware ID buffer (%d bytes).\n",
				hw_id_size ) );
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		RtlZeroMemory( p_string, hw_id_size );

		cl_memcpy( p_string, p_ext->pdo.p_pdo_device_info->hardware_id, hw_id_size );
	} 
	else 
	{
	p_string = ExAllocatePoolWithTag( NonPagedPool, IOC_HW_ID_SIZE, 'ihqi' );
	if( !p_string )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to allocate hardware ID buffer (%d bytes).\n",
			IOC_HW_ID_SIZE) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	V = ib_ioc_profile_get_vend_id( &p_ext->info.profile );
	P = cl_ntoh32( p_ext->info.profile.dev_id );
	S = ib_ioc_profile_get_subsys_vend_id( &p_ext->info.profile );
	s = cl_ntoh32( p_ext->info.profile.subsys_id );
	v = cl_ntoh16( p_ext->info.profile.dev_ver );

	/* Fill in the first hardware ID. */
	p_start = p_string;
	size = IOC_HW_ID_SIZE;
	status = RtlStringCbPrintfExW( p_start, size, &p_start, &size,
		STRSAFE_FILL_BEHIND_NULL, L"IBA\\V%06xP%08xS%06xs%08xv%04x",
		V, P, S, s, v );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to format hardware ID string.\n") );
		return status;
	}
	/* Fill in the second hardware ID. */
	p_start++;
	size -= sizeof(WCHAR);
	status = RtlStringCbPrintfExW( p_start, size, &p_start, &size,
		STRSAFE_FILL_BEHIND_NULL, L"IBA\\V%06xP%08xS%06xs%08x", V, P, S, s );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to format hardware ID string.\n") );
		return status;
	}
	/* Fill in the third hardware ID. */
	p_start++;
	size -= sizeof(WCHAR);
	status = RtlStringCbPrintfExW( p_start, size, &p_start, &size,
		STRSAFE_FILL_BEHIND_NULL, L"IBA\\V%06xP%08xv%04x", V, P, v );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to format hardware ID string.\n") );
		return status;
	}
	/* Fill in the fourth hardware ID. */
	p_start++;
	size -= sizeof(WCHAR);
	status = RtlStringCbPrintfExW( p_start, size, &p_start, &size,
		STRSAFE_FILL_BEHIND_NULL, L"IBA\\V%06xP%08x", V, P );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to format hardware ID string.\n") );
		return status;
	}
	}

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_query_compatible_ids(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	ioc_ext_t			*p_ext;
	WCHAR				*p_string, *p_start;
	uint32_t			compat_id_size;
	size_t				size;
	uint16_t			C, c, p, r;
	NTSTATUS			status;


	IOU_ENTER( IOU_DBG_PNP );

	p_ext = (ioc_ext_t*)p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	if ( p_ext->pdo.p_pdo_device_info )
	{

		compat_id_size = p_ext->pdo.p_pdo_device_info->compatible_id_size;

		p_string = ExAllocatePoolWithTag( NonPagedPool, compat_id_size, 'icqi' );

		if( !p_string )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
				("Failed to allocate compatible ID buffer (%d bytes).\n",
				compat_id_size) );
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		RtlZeroMemory( p_string, compat_id_size );

		cl_memcpy( p_string, p_ext->pdo.p_pdo_device_info->compatible_id, compat_id_size );
	} 
	else 
	{
	p_string = ExAllocatePoolWithTag( NonPagedPool, IOC_COMPAT_ID_SIZE, 'icqi' );
	if( !p_string )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to allocate compatible ID buffer (%d bytes).\n",
			IOC_HW_ID_SIZE) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	C = cl_ntoh16( p_ext->info.profile.io_class );
	c = cl_ntoh16( p_ext->info.profile.io_subclass );
	p = cl_ntoh16( p_ext->info.profile.protocol );
	r = cl_ntoh16( p_ext->info.profile.protocol_ver );

	p_start = p_string;
	size = IOC_COMPAT_ID_SIZE;
	/* Fill in the first compatible ID. */
	status = RtlStringCbPrintfExW( p_start, size, &p_start, &size,
		STRSAFE_FILL_BEHIND_NULL, L"IBA\\C%04xc%04xp%04xr%04x", C, c, p, r );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to format device ID string.\n") );
		return status;
	}
	/* Fill in the second compatible ID. */
	p_start++;
	size -= sizeof(WCHAR);
	status = RtlStringCbPrintfExW( p_start, size, NULL, NULL,
		STRSAFE_FILL_BEHIND_NULL, L"IBA\\C%04xc%04xp%04x", C, c, p );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to format device ID string.\n") );
		return status;
	}
	}

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_query_unique_id(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	WCHAR				*p_string;
	ioc_ext_t			*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	/* The instance ID is the port GUID. */
	if ( p_ext->pdo.p_pdo_device_info ) 
	{

		p_string = ExAllocatePoolWithTag( NonPagedPool, sizeof(WCHAR) * 41, 'iuqi' );
		if( !p_string )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
				("Failed to allocate instance ID buffer (%d bytes).\n",
				sizeof(WCHAR) * 41) );
			return STATUS_NO_MEMORY;
		}

		status = RtlStringCchPrintfW ( p_string, 41, L"%016I64x%016I64x%08x",
					 p_ext->info.profile.ioc_guid, p_ext->pdo.ca_guid,
					 p_ext->pdo.p_pdo_device_info->uniqueinstanceid);
		if( !NT_SUCCESS( status ) )
		{
			CL_ASSERT( NT_SUCCESS( status ) );
			ExFreePool( p_string );
			IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
				("RtlStringCchPrintfW returned %08x.\n", status) );
			return status;
		}
	} 
	else 
	{
	p_string = ExAllocatePoolWithTag( NonPagedPool, sizeof(WCHAR) * 33, 'iuqi' );
	if( !p_string )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to allocate instance ID buffer (%d bytes).\n",
			sizeof(WCHAR) * 17) );
		return STATUS_NO_MEMORY;
	}

	status = RtlStringCchPrintfW(p_string, 33, L"%016I64x%016I64x",
		 p_ext->info.profile.ioc_guid,p_ext->pdo.ca_guid);
	if( !NT_SUCCESS( status ) )
	{
		CL_ASSERT( NT_SUCCESS( status ) );
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("RtlStringCchPrintfW returned %08x.\n", status) );
		return status;
	}

	}

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_query_description(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	WCHAR				*p_string;
	ioc_ext_t			*p_ext;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	if ( p_ext->pdo.p_pdo_device_info ) 
	{
		p_string = ExAllocatePoolWithTag( NonPagedPool, p_ext->pdo.p_pdo_device_info->description_size, 
						'edqi' );
		if( !p_string )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
				( "Failed to allocate description buffer (%d bytes).\n",
				p_ext->pdo.p_pdo_device_info->description_size ) );
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		RtlZeroMemory( p_string, p_ext->pdo.p_pdo_device_info->description_size );

		cl_memcpy( p_string, p_ext->pdo.p_pdo_device_info->description,
				p_ext->pdo.p_pdo_device_info->description_size );

	} 
	else 
	{
	p_string = ExAllocatePoolWithTag( NonPagedPool,
									  sizeof(WCHAR) * sizeof(p_ext->info.profile.id_string),
									  'edqi');
	if( !p_string )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to allocate device description buffer (%d bytes).\n",
			sizeof(WCHAR) * sizeof(p_ext->info.profile.id_string)) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if( ib_ioc_profile_get_vend_id( &p_ext->info.profile ) == 0x00066a &&
		p_ext->info.profile.dev_id == CL_HTON32(0x00000030) )
	{
		status = RtlStringCchPrintfW(
			p_string, sizeof(p_ext->info.profile.id_string),
			L"SilverStorm Technologies VEx I/O Controller" );
	}
	else if( ib_ioc_profile_get_vend_id( &p_ext->info.profile ) == 0x00066a &&
		p_ext->info.profile.dev_id == CL_HTON32(0x00000038) )
	{
		status = RtlStringCchPrintfW(
			p_string, sizeof(p_ext->info.profile.id_string),
			L"SilverStorm Technologies VFx I/O Controller" );
	}
	else
	{
		status = RtlStringCchPrintfW(
			p_string, sizeof(p_ext->info.profile.id_string),
			L"%S", p_ext->info.profile.id_string );
	}
	if( !NT_SUCCESS( status ) )
	{
		CL_ASSERT( NT_SUCCESS( status ) );
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("RtlStringCchPrintfW returned %08x.\n", status) );
		return status;
	}
	}

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_query_location(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	ioc_ext_t			*p_ext;
	WCHAR				*p_string;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = (ioc_ext_t*)p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	p_string = ExAllocatePoolWithTag( NonPagedPool, 
									  max( IOC_LOCATION_SIZE, sizeof( WCHAR ) *
									       ( sizeof( p_ext->info.profile.id_string ) + 1 )),
									  'olqi');
	if( !p_string )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to allocate location buffer (%d bytes).\n",
			IOC_LOCATION_SIZE) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if( ib_ioc_profile_get_vend_id( &p_ext->info.profile ) == 0x00066a )
	{
		status = RtlStringCchPrintfW(
			p_string, sizeof(p_ext->info.profile.id_string),
			L"%S", p_ext->info.profile.id_string );
	}
	else
	{
		status = RtlStringCbPrintfW( p_string, IOC_LOCATION_SIZE,
			L"Chassis 0x%016I64x, Slot %d, IOC %d",
			cl_ntoh64( p_ext->info.chassis_guid ),
			p_ext->info.chassis_slot, p_ext->info.iou_slot );
	}
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to format device ID string.\n") );
		return status;
	}
	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
ioc_query_bus_info(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp,
		OUT				cl_irp_action_t* const	p_action )
{
	ioc_ext_t			*p_ext;
	PNP_BUS_INFORMATION	*p_iou_info;

	IOU_ENTER( IOU_DBG_PNP );

	p_ext = (ioc_ext_t*)p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	*p_action = IrpComplete;

	p_iou_info = ExAllocatePoolWithTag( NonPagedPool, sizeof(PNP_BUS_INFORMATION), 'ibqi' );
	if( !p_iou_info )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to allocate PNP_BUS_INFORMATION (%d bytes).\n",
			sizeof(PNP_BUS_INFORMATION)) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}


	p_iou_info->BusTypeGuid = GUID_BUS_TYPE_IBA;
	//TODO: Memory from Intel - storage miniport would not stay loaded unless
	//TODO: bus type was PCI.  Look here if SRP is having problems staying
	//TODO: loaded.
	p_iou_info->LegacyBusType = PNPBus;
	p_iou_info->BusNumber = p_ext->info.iou_slot;

	p_irp->IoStatus.Information = (ULONG_PTR)p_iou_info;
	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


static __ref_ioc_ifc(
	IN				ioc_ext_t*						p_ext )
{
	IOU_ENTER( IOU_DBG_PNP );

	cl_atomic_inc( &p_ext->pdo.p_parent_ext->cl_ext.n_ifc_ref );
	ObReferenceObject( p_ext->pdo.p_parent_ext->cl_ext.p_self_do );

	IOU_EXIT( IOU_DBG_PNP );
}


static void
__deref_ioc_ifc(
	IN				ioc_ext_t*						p_ext )
{
	IOU_ENTER( IOU_DBG_PNP );

	cl_atomic_dec( &p_ext->pdo.p_parent_ext->cl_ext.n_ifc_ref );
	ObDereferenceObject( p_ext->pdo.p_parent_ext->cl_ext.p_self_do );

	IOU_EXIT( IOU_DBG_PNP );
}




static NTSTATUS
ioc_query_interface(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS				status;
	IO_STACK_LOCATION		*p_io_stack;
	ib_al_ifc_t				*p_ifc;
	ib_al_ifc_data_t		*p_ifc_data;
	ioc_ifc_data_t			*p_ioc_data;
	ioc_ext_t				*p_ext;
	const GUID				*p_guid;

	IOU_ENTER( IOU_DBG_PNP );

	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

	p_ext = p_dev_obj->DeviceExtension;
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	p_guid = p_io_stack->Parameters.QueryInterface.InterfaceType;
	/* Bottom of the stack - IRP must be completed. */
	*p_action = IrpComplete;

	/* Compare requested GUID with our supported interface GUIDs. */
	if( IsEqualGUID( p_guid, &GUID_BUS_INTERFACE_STANDARD ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
			("BUS_INTERFACE_STANDARD\n") );
		return cl_fwd_query_ifc(
			p_ext->pdo.p_parent_ext->cl_ext.p_self_do, p_io_stack );
	}

	if( !IsEqualGUID( p_guid, &GUID_IB_AL_INTERFACE ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
			("Unsupported interface: \n\t"
			"0x%08x, 0x%04x, 0x%04x, 0x%02x, 0x%02x, 0x%02x,"
			"0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x.\n",
			p_guid->Data1, p_guid->Data2, p_guid->Data3,
			p_guid->Data4[0], p_guid->Data4[1], p_guid->Data4[2],
			p_guid->Data4[3], p_guid->Data4[4], p_guid->Data4[5],
			p_guid->Data4[6], p_guid->Data4[7]) );
		return p_irp->IoStatus.Status;
	}

	/* Get the interface. */
	status = cl_fwd_query_ifc(
		p_ext->pdo.p_parent_ext->cl_ext.p_self_do, p_io_stack );
	if( !NT_SUCCESS( status ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Failed to forward interface query: %08X\n", status) );
		return status;
	}

	if( !p_io_stack->Parameters.QueryInterface.InterfaceSpecificData )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("No interface specific data!\n") );
		return status;
	}

	p_ifc = (ib_al_ifc_t*)p_io_stack->Parameters.QueryInterface.Interface;

	p_ifc_data = (ib_al_ifc_data_t*)
		p_io_stack->Parameters.QueryInterface.InterfaceSpecificData;
	p_guid = p_ifc_data->type;
	if( !IsEqualGUID( p_guid, &GUID_IOC_INTERFACE_DATA ) ||
		p_ifc_data->version != IOC_INTERFACE_DATA_VERSION )
	{
		p_ifc->wdm.InterfaceDereference( p_ifc->wdm.Context );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("Unsupported interface data: \n\t"
			"0x%08x, 0x%04x, 0x%04x, 0x%02x, 0x%02x, 0x%02x,"
			"0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x.\n",
			p_guid->Data1, p_guid->Data2, p_guid->Data3,
			p_guid->Data4[0], p_guid->Data4[1], p_guid->Data4[2],
			p_guid->Data4[3], p_guid->Data4[4], p_guid->Data4[5],
			p_guid->Data4[6], p_guid->Data4[7]) );
		return STATUS_INVALID_PARAMETER;
	}

	ASSERT( p_ifc_data->p_data );

	if( p_ifc_data->size != sizeof(ioc_ifc_data_t) )
	{
		p_ifc->wdm.InterfaceDereference( p_ifc->wdm.Context );
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
			("Buffer too small (%d given, %d required).\n",
			p_ifc_data->size,
			sizeof(ioc_ifc_data_t)) );
		return STATUS_BUFFER_TOO_SMALL;
	}

	/* Set the interface data. */
	p_ioc_data = (ioc_ifc_data_t*)p_ifc_data->p_data;

	p_ioc_data->ca_guid = p_ext->pdo.p_parent_ext->ioc_mgr.info.ca_guid;
	p_ioc_data->guid = p_ext->info.profile.ioc_guid;

	IOU_EXIT( IOU_DBG_PNP );
	return STATUS_SUCCESS;
}


/*
 * The PDOs created by the IB Bus driver are software devices.  As such,
 * all power states are supported.  It is left to the HCA power policy
 * owner to handle which states can be supported by the HCA.
 */
static NTSTATUS
ioc_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	IO_STACK_LOCATION	*p_io_stack;
	iou_pdo_ext_t		*p_ext;

	IOU_ENTER( IOU_DBG_POWER );

	p_ext = p_dev_obj->DeviceExtension;
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	if( p_io_stack->Parameters.Power.Type == DevicePowerState )
	{
		/* Notify the power manager. */
		p_ext->dev_po_state = p_io_stack->Parameters.Power.State;
		PoSetPowerState( p_dev_obj, DevicePowerState, p_ext->dev_po_state );
	}

	*p_action = IrpComplete;
	IOU_EXIT( IOU_DBG_POWER );
	return STATUS_SUCCESS;
}

ib_api_status_t
_create_ioc_pdo(
	IN				child_device_info_t*			p_child_dev,
	IN				ca_ioc_map_t*				p_ca_ioc_map)
{
	NTSTATUS		status;
	DEVICE_OBJECT	*p_pdo = NULL;
	iou_fdo_ext_t	*p_ext;
	ioc_mgr_t		*p_ioc_mgr;
	ioc_ext_t		*p_ioc_ext;
	uint32_t		ext_size;
	BOOLEAN			b_vnic_ioc = FALSE;

	p_ioc_mgr = p_ca_ioc_map->p_ioc_mgr;

	p_ext = PARENT_STRUCT( p_ioc_mgr, iou_fdo_ext_t, ioc_mgr );

	if( p_child_dev->ca_ioc_path.ca_guid != p_ioc_mgr->info.ca_guid ||
		p_child_dev->ca_ioc_path.info.chassis_guid != p_ioc_mgr->info.chassis_guid ||
		p_child_dev->ca_ioc_path.info.chassis_slot != p_ioc_mgr->info.slot||
		p_child_dev->ca_ioc_path.info.iou_guid != p_ioc_mgr->info.guid )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_PNP,
			("Child PDO's IOC not in this IOU.\n") );
		return IB_NOT_DONE;
	}

	ext_size = sizeof(ioc_ext_t) +
		( sizeof(ib_svc_entry_t) * p_ca_ioc_map->info.profile.num_svc_entries );

	status = IoCreateDevice( iou_globals.p_driver_obj, ext_size,
			NULL, FILE_DEVICE_CONTROLLER,
			FILE_DEVICE_SECURE_OPEN | FILE_AUTOGENERATED_DEVICE_NAME,
			FALSE, &p_pdo );
	if( !NT_SUCCESS( status ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			( "IoCreateDevice returned %08x.\n", status ) );
		return IB_ERROR;
	}

	b_vnic_ioc = is_vnic_profile_ioc( &p_ca_ioc_map->info.profile );	

	/* Initialize the device extension. */
	cl_init_pnp_po_ext( p_pdo, NULL, p_pdo, g_iou_dbg_flags,
		&vfptr_ioc_pnp, &vfptr_iou_query_txt );
	/* Set the DO_BUS_ENUMERATED_DEVICE flag to mark it as a PDO. */
	p_pdo->Flags |= DO_BUS_ENUMERATED_DEVICE;

	p_ioc_ext = p_pdo->DeviceExtension;
	p_ioc_ext->pdo.dev_po_state.DeviceState = PowerDeviceD0;
	p_ioc_ext->pdo.p_parent_ext = p_ext;
	p_ioc_ext->pdo.b_present = TRUE;
	p_ioc_ext->pdo.b_reported_missing = FALSE;
	p_ioc_ext->pdo.ca_guid = p_child_dev->ca_ioc_path.ca_guid;

	if ( b_vnic_ioc ) 
	{
		p_ioc_ext->pdo.p_pdo_device_info = p_child_dev;
	}

	/* Copy the IOC profile and service entries. */
	p_ioc_ext->info = p_child_dev->ca_ioc_path.info;
	cl_memcpy( p_ioc_ext->svc_entries, p_child_dev->ca_ioc_path.svc_entry_array,
		p_child_dev->ca_ioc_path.info.profile.num_svc_entries );
	/* Make sure the IOC string is null terminated. */
	p_ioc_ext->info.profile.id_string[CTRL_ID_STRING_LEN-1] = '\0';

	/* Store the device extension in the PDO list for future queries. */
	cl_mutex_acquire( &p_ioc_mgr->pdo_mutex );
	cl_qlist_insert_tail( &p_ioc_mgr->pdo_list,
				&p_ioc_ext->pdo.list_item );
	cl_mutex_release( &p_ioc_mgr->pdo_mutex );

	IoInvalidateDeviceRelations( p_ext->cl_ext.p_pdo, BusRelations );

	IOU_EXIT(IOU_DBG_PNP);

	return IB_SUCCESS;
}
