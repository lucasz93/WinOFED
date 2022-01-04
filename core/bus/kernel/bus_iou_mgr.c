/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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
#include <precomp.h>
#include <initguid.h>
#include <wdmguid.h>
#include "iba/iou_ifc.h"


/* {5A9649F4-0101-4a7c-8337-796C48082DA2} */
DEFINE_GUID(GUID_BUS_TYPE_IBA, 
0x5a9649f4, 0x101, 0x4a7c, 0x83, 0x37, 0x79, 0x6c, 0x48, 0x8, 0x2d, 0xa2);


/*
 * Size of device descriptions, in the format:
 *	IBA\VxxxxxxPxxxxxxxxvxxxxxxxx
 */
#define IOU_DEV_ID_SIZE		sizeof(L"IBA\\VxxxxxxPxxxxvxxxxxxxx")
#define IOU_DEV_ID_STRING1	L"IBA\\V%06xP%04hxv%08x"
#define IOU_DEV_ID_STRING2	L"IBA\\V%06xP%04hx"
#define IOU_HW_ID_SIZE		\
	sizeof(L"IBA\\VxxxxxxPxxxxvxxxxxxxx\0IBA\\VxxxxxxPxxxx\0\0")
#define IOU_COMPAT_ID		L"IBA\\IB_IOU\0\0"
#define IOU_LOCATION_SIZE	\
	sizeof(L"Chassis 0xxxxxxxxxxxxxxxxx, Slot xx")

/*
 * Device extension for IOU PDOs.
 */
typedef struct _bus_iou_ext
{
	bus_pdo_ext_t			pdo;

	net64_t					chassis_guid;
	uint8_t					slot;
	net64_t					guid;
	net32_t					vend_id;
	net16_t					dev_id;
	net32_t					revision;
	char					desc[IB_NODE_DESCRIPTION_SIZE + 1];

	/* Number of references on the upper interface. */
	atomic32_t				n_ifc_ref;

}	bus_iou_ext_t;


typedef struct _iou_pnp_context
{
	bus_filter_t	*p_bus_filter;
	void			*p_pdo_ext;

}	iou_pnp_ctx_t;


/*
 * Function prototypes.
 */
void
destroying_iou_mgr(
	IN				cl_obj_t*					p_obj );

void
free_iou_mgr(
	IN				cl_obj_t*					p_obj );

ib_api_status_t
bus_reg_iou_pnp(
	IN				bus_filter_t* 				p_bfi );

ib_api_status_t
iou_mgr_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec );

ib_api_status_t
iou_mgr_iou_add(
	IN				ib_pnp_iou_rec_t*			p_pnp_rec );

void
iou_mgr_iou_remove(
	IN				ib_pnp_iou_rec_t*			p_pnp_rec );

static NTSTATUS
iou_start(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
iou_query_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static void
iou_release_resources(
	IN				DEVICE_OBJECT* const		p_dev_obj );

static NTSTATUS
iou_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
iou_surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
iou_query_capabilities(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
iou_query_target_relations(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
iou_query_device_id(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
iou_query_hardware_ids(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
iou_query_compatible_ids(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
iou_query_unique_id(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
iou_query_description(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
iou_query_location(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
iou_query_bus_info(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
iou_query_interface(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
iou_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );



/*
 * Global virtual function pointer tables shared between all 
 * instances of Port PDOs.
 */
static const cl_vfptr_pnp_po_t		vfptr_iou_pnp = {
	"IB IOU",
	iou_start,
	cl_irp_succeed,
	cl_irp_succeed,
	cl_irp_succeed,
	iou_query_remove,
	iou_release_resources,
	iou_remove,
	cl_irp_succeed,
	iou_surprise_remove,
	iou_query_capabilities,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_succeed,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	iou_query_target_relations,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	iou_query_bus_info,
	iou_query_interface,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_succeed,				// QueryPower
	iou_set_power,				// SetPower
	cl_irp_unsupported,			// PowerSequence
	cl_irp_unsupported			// WaitWake
};


static const cl_vfptr_query_txt_t		vfptr_iou_query_txt = {
	iou_query_device_id,
	iou_query_hardware_ids,
	iou_query_compatible_ids,
	iou_query_unique_id,
	iou_query_description,
	iou_query_location
};


/*
 * Create the AL load service.
 */
ib_api_status_t
create_iou_mgr(
	IN				bus_filter_t*				p_bfi )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;
	iou_mgr_t			*p_iou_mgr;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( p_bfi->p_iou_mgr == NULL );

	p_iou_mgr = cl_zalloc( sizeof(iou_mgr_t) );
	if( !p_iou_mgr )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Failed to allocate IOU manager.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}
	p_bfi->p_iou_mgr = p_iou_mgr;

	/* Construct the load service. */
	cl_obj_construct( &p_iou_mgr->obj, AL_OBJ_TYPE_LOADER );

	p_bfi->p_iou_mgr_obj = &p_bfi->p_iou_mgr->obj; // save for destroy & free

	cl_mutex_construct( &p_iou_mgr->pdo_mutex );
	cl_qlist_init( &p_iou_mgr->iou_list );

	cl_status = cl_mutex_init( &p_iou_mgr->pdo_mutex );
	if( cl_status != CL_SUCCESS )
	{
		free_iou_mgr( &p_iou_mgr->obj );
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("cl_mutex_init returned %#x.\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	/* Initialize the load service object. */
	cl_status = cl_obj_init( &p_iou_mgr->obj, CL_DESTROY_SYNC,
							 destroying_iou_mgr, NULL, free_iou_mgr );
	if( cl_status != CL_SUCCESS )
	{
		free_iou_mgr( &p_iou_mgr->obj );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("cl_obj_init returned %#x.\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	/* Register for IOU PnP events. */
	status = bus_reg_iou_pnp( p_bfi );
	if( status != IB_SUCCESS )
	{
		free_iou_mgr( &p_iou_mgr->obj );
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("bus_reg_iou_pnp returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	BUS_EXIT( BUS_DBG_PNP );
	return IB_SUCCESS;
}


/*
 * Pre-destroy the load service.
 */
void
destroying_iou_mgr(
	IN				cl_obj_t*					p_obj )
{
	ib_api_status_t			status;
	bus_filter_t			*p_bfi;
	iou_mgr_t				*p_iou_mgr;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( p_obj );
	p_bfi = get_bfi_by_obj( BFI_IOU_MGR_OBJ, p_obj );
	if (p_bfi == NULL) {
		BUS_PRINT(BUS_DBG_PNP, ("Failed to find p_bfi by obj %p?\n", p_obj));
		return;
	}
	p_iou_mgr = p_bfi->p_iou_mgr;

	BUS_PRINT(BUS_DBG_PNP, ("%s obj %p iou_mgr %p iou_mgr_obj %p\n",
							p_bfi->whoami, p_obj,p_iou_mgr,&p_iou_mgr->obj));

	CL_ASSERT( p_iou_mgr == PARENT_STRUCT( p_obj, iou_mgr_t, obj ) );

	/* Deregister for iou PnP events. */
	if( get_bfi_count() == 1 && bus_globals.h_pnp_iou )
	{
		status = ib_dereg_pnp( bus_globals.h_pnp_iou, NULL );
		bus_globals.h_pnp_iou = NULL;
		CL_ASSERT( status == IB_SUCCESS );
	}
	cl_obj_deref( p_bfi->p_iou_mgr_obj );

	BUS_EXIT( BUS_DBG_PNP );
}


/*
 * Free the load service.
 */
void
free_iou_mgr(
	IN				cl_obj_t*					p_obj )
{
	bus_pdo_ext_t	*p_ext;
	cl_list_item_t	*p_list_item;
	bus_filter_t	*p_bfi;
	iou_mgr_t		*p_iou_mgr;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( p_obj );
	p_bfi = get_bfi_by_obj( BFI_IOU_MGR_OBJ, p_obj );
	if ( p_bfi == NULL ) {
		BUS_TRACE( BUS_DBG_ERROR, ("Unable to get p_bfi iou_obj %p?\n", p_obj) );
		return;
	}
	p_iou_mgr = p_bfi->p_iou_mgr;
	if ( !p_iou_mgr ) {
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("%s <null> IOU mgr?\n",p_bfi->whoami) );
		// if create fails & then free is called, p_bfi->p_iou_mgr == NULL
		return;
	}

	CL_ASSERT( p_iou_mgr == PARENT_STRUCT( p_obj, iou_mgr_t, obj ) );

	BUS_PRINT( BUS_DBG_PNP, ("%s Mark all IOU PDOs as no longer present\n",
								p_bfi->whoami));
	/*
	 * Mark all IOU PDOs as no longer present.  This will cause them
	 * to be removed when they process the IRP_MN_REMOVE_DEVICE.
	 */
	p_list_item = cl_qlist_remove_head( &p_iou_mgr->iou_list );
	while( p_list_item != cl_qlist_end( &p_iou_mgr->iou_list ) )
	{
		p_ext = PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );
		p_list_item = cl_qlist_remove_head( &p_iou_mgr->iou_list );
		if( p_ext->cl_ext.pnp_state == SurpriseRemoved )
		{
			CL_ASSERT( !p_ext->b_present );
			p_ext->b_reported_missing = TRUE;
			BUS_TRACE( BUS_DBG_PNP,
					("%s Surprise-remove %s: ext %p, present %d, missing %d\n",
						p_bfi->whoami,
						p_ext->cl_ext.vfptr_pnp_po->identity, p_ext,
						p_ext->b_present, p_ext->b_reported_missing ) );
			continue;
		}

		if( p_ext->h_ca )
		{
			/* Invalidate bus relations for the HCA. */
			IoInvalidateDeviceRelations(
				p_ext->h_ca->p_hca_dev, BusRelations );

			/* Release the reference on the CA object. */
			deref_al_obj( &p_ext->h_ca->obj );
		}

		BUS_TRACE( BUS_DBG_PNP, ("Deleted device %s %s: PDO %p, ext %p\n",
					p_bfi->whoami, p_ext->cl_ext.vfptr_pnp_po->identity,
					p_ext->cl_ext.p_self_do, p_ext ) );

		IoDeleteDevice( p_ext->cl_ext.p_self_do );
	}

	cl_mutex_destroy( &p_iou_mgr->pdo_mutex );
	cl_obj_deinit( p_obj );
	cl_free( p_iou_mgr );

	p_bfi->p_iou_mgr = NULL;
	p_bfi->p_iou_mgr_obj = NULL;

	BUS_EXIT( BUS_DBG_PNP );
}


/*
 * Register the load service for the given PnP class events.
 */
ib_api_status_t
bus_reg_iou_pnp( IN bus_filter_t *p_bfi )
{
	ib_pnp_req_t			pnp_req;
	ib_api_status_t			status = IB_SUCCESS;
	boolean_t				need_pnp_reg = FALSE;

	/* only need to register for IOU PNP events once.
	 * do not hold mutex over pnp_reg() call as callback which needs mutex
	 * could occur.
	 */
	if ( !bus_globals.h_pnp_iou )
	{
		lock_control_event();
		if ( !bus_globals.h_pnp_iou )
		{
			bus_globals.h_pnp_iou = (ib_pnp_handle_t)1; /* block others */ 
			need_pnp_reg = TRUE;
		}
		unlock_control_event();

		if ( need_pnp_reg )
		{
			cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
			pnp_req.pnp_class	= IB_PNP_IOU | IB_PNP_FLAG_REG_SYNC;
			pnp_req.pnp_context = NULL;
			pnp_req.pfn_pnp_cb	= iou_mgr_pnp_cb;

			status = ib_reg_pnp( gh_al, &pnp_req, &bus_globals.h_pnp_iou );
		}
	}

	if ( status == IB_SUCCESS )
	{
		/* Reference the load service on behalf of the ib_reg_pnp call. */
		cl_obj_ref( p_bfi->p_iou_mgr_obj );
	}

	return status;
}


/*
 * Load service PnP event callback.
 */
ib_api_status_t
iou_mgr_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec )
{
	ib_api_status_t		status=IB_SUCCESS;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( p_pnp_rec );

	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_IOU_ADD:
		status = iou_mgr_iou_add( (ib_pnp_iou_rec_t*)p_pnp_rec );
		break;

	case IB_PNP_IOU_REMOVE:
		iou_mgr_iou_remove( (ib_pnp_iou_rec_t*)p_pnp_rec );
		break;

	default:
		BUS_PRINT( BUS_DBG_PNP, ("Unhandled PNP Event %s\n",
					ib_get_pnp_event_str(p_pnp_rec->pnp_event) ));
		break;
	}
	BUS_EXIT( BUS_DBG_PNP );
	return status;
}


/*
 * Called to get child relations for the bus root.
 */
#pragma prefast(suppress: 28167, "The irql level is restored here")
NTSTATUS iou_mgr_get_bus_relations(
	IN				bus_filter_t*				p_bfi,
	IN				IRP* const					p_irp )
{
	NTSTATUS			status;

	BUS_ENTER( BUS_DBG_PNP );

	BUS_PRINT(BUS_DBG_PNP, ("CA_guid %I64x\n",p_bfi->ca_guid));

	CL_ASSERT( p_bfi->ca_guid );

	BUS_PRINT(BUS_DBG_PNP, ("%s for ca_guid %I64x iou_mgr %p\n",
							p_bfi->whoami, p_bfi->ca_guid, p_bfi->p_iou_mgr) );
	if (!p_bfi->p_iou_mgr)
		return STATUS_NO_SUCH_DEVICE;

	cl_mutex_acquire( &p_bfi->p_iou_mgr->pdo_mutex );
	status = bus_get_relations( &p_bfi->p_iou_mgr->iou_list, p_bfi->ca_guid, p_irp );
	cl_mutex_release( &p_bfi->p_iou_mgr->pdo_mutex );

	BUS_EXIT( BUS_DBG_PNP );
	return status;
}


#pragma prefast(suppress: 28167, "The irql level is restored here")
static ib_api_status_t __iou_was_hibernated(
	IN				ib_pnp_iou_rec_t*			p_pnp_rec,
	IN				bus_filter_t*				p_bfi )
{
	ib_api_status_t	status;
	cl_list_item_t	*p_list_item;
	bus_iou_ext_t	*p_iou_ext;
	bus_pdo_ext_t	*p_pdo_ext = NULL;
	size_t			n_devs = 0;
	iou_mgr_t		*p_iou_mgr = p_bfi->p_iou_mgr;
	cl_qlist_t		*p_pdo_list = &p_iou_mgr->iou_list;
	iou_pnp_ctx_t	*p_ctx = p_pnp_rec->pnp_rec.context;

	BUS_ENTER( BUS_DBG_PNP );
	
	cl_mutex_acquire( &p_iou_mgr->pdo_mutex );
	
	/* Count the number of child devices. */
	for( p_list_item = cl_qlist_head( p_pdo_list );
		p_list_item != cl_qlist_end( p_pdo_list );
		p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_pdo_ext = PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );
		p_iou_ext = (bus_iou_ext_t*)p_pdo_ext;

		/* TODO: maybe we need more search patterns like vend_id, dev_id ... */
		if( p_pdo_ext->b_present && p_pdo_ext->b_hibernating &&
			(p_iou_ext->guid == p_pnp_rec->pnp_rec.guid) )
		{
			n_devs++;
			break;
		}

		BUS_TRACE( BUS_DBG_PNP, ("%s Skipped PDO for %s: PDO %p, ext %p, "
			"present %d, missing %d, hibernating %d, port_guid %I64x.\n",
			p_bfi->whoami,
			p_pdo_ext->cl_ext.vfptr_pnp_po->identity,
			p_pdo_ext->cl_ext.p_self_do, 
			p_pdo_ext, p_pdo_ext->b_present, p_pdo_ext->b_reported_missing, 
			p_pdo_ext->b_hibernating, p_iou_ext->guid ) );
	}

	if (n_devs)
	{
		/* Take a reference on the parent HCA. */
		p_pdo_ext->h_ca = acquire_ca( p_pnp_rec->ca_guid );
		if( !p_pdo_ext->h_ca )
		{
			BUS_TRACE( BUS_DBG_ERROR,
				("acquire_ca failed to find CA by guid %I64x\n",
				p_pnp_rec->ca_guid ) );
			status = IB_INVALID_GUID;
		}
		else 
		{
			p_pdo_ext->b_hibernating = FALSE;
			p_ctx->p_pdo_ext = p_pdo_ext; // for iou_mgr_iou_remove()

			status = IB_SUCCESS;
			p_iou_ext = (bus_iou_ext_t*)p_pdo_ext;
			BUS_TRACE( BUS_DBG_PNP, ("%s Found PDO for %s: PDO %p, ext %p, "
				"present %d, missing %d, hibernating %d, port_guid %I64x.\n",
				p_bfi->whoami,
				p_pdo_ext->cl_ext.vfptr_pnp_po->identity,
				p_pdo_ext->cl_ext.p_self_do, 
				p_pdo_ext, p_pdo_ext->b_present, p_pdo_ext->b_reported_missing, 
				p_pdo_ext->b_hibernating, p_iou_ext->guid ) );
		}
	}
	else 
	{
		BUS_TRACE( BUS_DBG_PNP, ("%s Failed to find PDO for guid %I64x\n",
								p_bfi->whoami, p_pnp_rec->pnp_rec.guid ) );
		status = IB_NOT_FOUND;
	}

	cl_mutex_release( &p_iou_mgr->pdo_mutex );

	BUS_EXIT( BUS_DBG_PNP );
	return status;
}

ib_api_status_t
iou_mgr_iou_add(
	IN				ib_pnp_iou_rec_t*			p_pnp_rec )
{
	NTSTATUS		status;
	DEVICE_OBJECT	*p_pdo;
	bus_iou_ext_t	*p_iou_ext;
	bus_filter_t	*p_bfi;
	iou_mgr_t		*p_iou_mgr;
	iou_pnp_ctx_t	*p_ctx = p_pnp_rec->pnp_rec.context;

	BUS_ENTER( BUS_DBG_PNP );

	p_bfi = get_bfi_by_ca_guid( p_pnp_rec->ca_guid );
	if ( !p_bfi ) {
		BUS_TRACE_EXIT( BUS_DBG_PNP,("NULL p_bfi? ca_guid 0x%I64x\n",
									p_pnp_rec->ca_guid ) );
		return IB_ERROR;
	}

	if ( !p_ctx ) {
		/*
		 * Allocate a PNP context for this object. pnp_rec.context is object
		 * unique.
		 */
		p_ctx = cl_zalloc( sizeof(*p_ctx) );
		if( !p_ctx )
		{
			BUS_TRACE_EXIT(BUS_DBG_PNP, ("%s ca_guid %I64x iou_guid(%I64x) "
					"BAD alloc for PNP context\n", p_bfi->whoami,
					p_bfi->ca_guid, p_pnp_rec->guid ));

			return IB_ERROR;
		}
		p_ctx->p_bus_filter = p_bfi;
		p_pnp_rec->pnp_rec.context = p_ctx;

		BUS_PRINT(BUS_DBG_PNP,
					("%s ca_guid %I64x iou_guid(%I64x) ALLOC p_ctx @ %p\n",
					p_bfi->whoami, p_bfi->ca_guid, p_pnp_rec->guid,p_ctx));
	}
	p_iou_mgr = p_bfi->p_iou_mgr;

	/* Upon hibernating the computer IB_BUS driver doesn't remove PDO, but
	   marks with a flag. So we first try to find an existing PDO for this port,
	   marked with this flag. If it was found, we turn off the flag and use
	   this PDO */
	status = __iou_was_hibernated( p_pnp_rec, p_bfi );
	if( status != IB_NOT_FOUND )
	{
		BUS_EXIT( BUS_DBG_PNP );
		return status;
	}

	/* Create the PDO for the new port device. */
	status = IoCreateDevice( bus_globals.p_driver_obj, sizeof(bus_iou_ext_t),
							 NULL, FILE_DEVICE_CONTROLLER, 
							 FILE_DEVICE_SECURE_OPEN |
										 	FILE_AUTOGENERATED_DEVICE_NAME,
							 FALSE, &p_pdo );

	if( !NT_SUCCESS( status ) )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("IoCreateDevice returned %08x.\n", status) );
		return IB_ERROR;
	}

	p_iou_ext = p_pdo->DeviceExtension;
	memset( p_iou_ext, 0, sizeof(bus_iou_ext_t) );

	/* Initialize the device extension. */
	cl_init_pnp_po_ext( p_pdo, NULL, p_pdo, bus_globals.dbg_lvl,
						&vfptr_iou_pnp, &vfptr_iou_query_txt );

	/* Set the DO_BUS_ENUMERATED_DEVICE flag to mark it as a PDO. */
	p_pdo->Flags |= DO_BUS_ENUMERATED_DEVICE;
	
	p_iou_ext->pdo.dev_po_state.DeviceState = PowerDeviceD0;
	p_iou_ext->pdo.p_parent_ext = p_bfi->p_bus_ext;
	p_iou_ext->pdo.b_present = TRUE;
	p_iou_ext->pdo.b_reported_missing = FALSE;

	BUS_TRACE( BUS_DBG_PNP, ("%s: ext %p, present %d, missing %d .\n",
		p_iou_ext->pdo.cl_ext.vfptr_pnp_po->identity, p_iou_ext,
		p_iou_ext->pdo.b_present, p_iou_ext->pdo.b_reported_missing ) );

	p_iou_ext->guid = p_pnp_rec->guid;
	p_iou_ext->chassis_guid = p_pnp_rec->chassis_guid;
	p_iou_ext->slot = p_pnp_rec->slot;
	p_iou_ext->vend_id = cl_ntoh32( p_pnp_rec->vend_id );
	if( p_iou_ext->vend_id == 0x00066a )
		p_iou_ext->dev_id = (net16_t)(p_pnp_rec->pnp_rec.guid >> 32) & 0x00FF;
	else
		p_iou_ext->dev_id = cl_ntoh16( p_pnp_rec->dev_id );
	p_iou_ext->revision = cl_ntoh32( p_pnp_rec->revision );
	cl_memcpy( p_iou_ext->desc, p_pnp_rec->desc,
		IB_NODE_DESCRIPTION_SIZE + 1 );
	p_iou_ext->n_ifc_ref = 0;

	/* Cache the CA GUID. */
	p_iou_ext->pdo.ca_guid = p_pnp_rec->ca_guid;

	/* Take a reference on the parent HCA. */
	p_iou_ext->pdo.h_ca = acquire_ca( p_pnp_rec->ca_guid );
	if( !p_iou_ext->pdo.h_ca )
	{
		IoDeleteDevice( p_pdo );
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("acquire_ca failed to find CA.\n") );
		return IB_INVALID_GUID;
	}

	/* Store the device extension in the PDO list for future queries. */
	cl_mutex_acquire( &p_iou_mgr->pdo_mutex );
	cl_qlist_insert_tail( &p_iou_mgr->iou_list, &p_iou_ext->pdo.list_item );
	cl_mutex_release( &p_iou_mgr->pdo_mutex );

	/*
	 * Set the context of the PNP event.  The context is passed in for future
	 * events on the same port.
	 */
	/* if not set in iou_was_hibernated(), set now */
	if ( !p_ctx->p_pdo_ext )
		p_ctx->p_pdo_ext = p_iou_ext;

	/* Tell the PnP Manager to rescan for the HCA's bus relations. */
	IoInvalidateDeviceRelations(
			p_iou_ext->pdo.h_ca->p_hca_dev, BusRelations );

	/* Invalidate removal relations for the bus driver. */
	IoInvalidateDeviceRelations(
		p_bfi->p_bus_ext->cl_ext.p_pdo, RemovalRelations );

	BUS_EXIT( BUS_DBG_PNP );

	return IB_SUCCESS;
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
void iou_mgr_iou_remove(
	IN				ib_pnp_iou_rec_t*			p_pnp_rec )
{
	bus_pdo_ext_t	*p_ext;
	iou_mgr_t		*p_iou_mgr;
	bus_filter_t	*p_bfi;
	iou_pnp_ctx_t	*p_ctx = p_pnp_rec->pnp_rec.context;

	BUS_ENTER( BUS_DBG_PNP );

	if ( !p_ctx ) {
		BUS_EXIT( BUS_DBG_PNP );
		return;
	}

	CL_ASSERT( p_ctx->p_bus_filter->magic == BFI_MAGIC );
	p_bfi = p_ctx->p_bus_filter;
	CL_ASSERT( p_bfi );

	BUS_PRINT(BUS_DBG_PNP,("%s ca_guid 0x%I64x iou_mgr %p\n",
				p_bfi->whoami, p_bfi->ca_guid, p_bfi->p_iou_mgr));

	/* fdo_release_resources() has destroyed the IOU mgr, all that needs to be
	 * done is cleanup the PNP IOU context; one per port.
	 */
	if ( p_bfi->ca_guid == 0ULL || !p_bfi->p_iou_mgr ) {
		cl_free( p_ctx );
		p_pnp_rec->pnp_rec.context = NULL;
		BUS_EXIT( BUS_DBG_PNP );
		return;
	}

	p_iou_mgr = p_bfi->p_iou_mgr;

	/* Within the PNP record's context is the IOU extension; see
	 * was_hibernated().
	 */
	p_ext = p_ctx->p_pdo_ext;
	CL_ASSERT( p_ext );

	if (p_bfi != p_ext->p_parent_ext->bus_filter) {
		BUS_PRINT(BUS_DBG_PNP,
			("p_bfi(%p) != p_ext->bus_filter(%p) line %d file %s\n",
			p_bfi,p_ext->p_parent_ext->bus_filter, __LINE__,__FILE__));
		CL_ASSERT (p_bfi == p_ext->p_parent_ext->bus_filter);
	}

	/*
	 * Flag the port PDO as no longer being present.  We have to wait until
	 * the PnP manager removes it to clean up.  However, we do release the
	 * reference on the CA object in order to allow the removal of the HCA
	 * to proceed should it occur before the port's PDO is cleaned up.
	 */
	cl_mutex_acquire( &p_iou_mgr->pdo_mutex );
	if ( !p_ext->h_ca )
	{
		BUS_TRACE_EXIT( BUS_DBG_PNP, ("NULL h_ca? p_ext %p\n", p_ext ) );
		return;
	}

	if( p_ext->b_hibernating )
	{
		BUS_TRACE( BUS_DBG_PNP, ("%s Skip port removing for %s: PDO %p ext %p "
			"present %d missing %d hibernating %d\n",
			p_bfi->whoami,
			p_ext->cl_ext.vfptr_pnp_po->identity, p_ext->cl_ext.p_self_do,
			p_ext, p_ext->b_present, 
			p_ext->b_reported_missing, p_ext->b_hibernating ) );
		goto hca_deref;
	}

	p_ext->b_present = FALSE;
	p_ext->b_reported_missing = TRUE;

	BUS_TRACE( BUS_DBG_PNP, ("%s %s: ext %p, present %d, missing %d .\n",
				p_bfi->whoami,
				p_ext->cl_ext.vfptr_pnp_po->identity, p_ext, p_ext->b_present,
				p_ext->b_reported_missing ) );

	/* Invalidate removal relations for the bus driver. */
	IoInvalidateDeviceRelations(
		p_bfi->p_bus_ext->cl_ext.p_pdo, RemovalRelations );

	/* Invalidate bus relations for the HCA. */
	IoInvalidateDeviceRelations(
		p_ext->h_ca->p_hca_dev, BusRelations );

hca_deref:
	/* free PNP context */
	cl_free( p_ctx );
	p_pnp_rec->pnp_rec.context = NULL;

	deref_al_obj( &p_ext->h_ca->obj );
	p_ext->h_ca = NULL;

	cl_mutex_release( &p_iou_mgr->pdo_mutex );

	BUS_EXIT( BUS_DBG_PNP );
}


static NTSTATUS
iou_start(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	bus_pdo_ext_t	*p_ext;

	BUS_ENTER( BUS_DBG_PNP );

	UNUSED_PARAM( p_irp );

	p_ext = p_dev_obj->DeviceExtension;

	/* Notify the Power Manager that the device is started. */
	PoSetPowerState( p_dev_obj, DevicePowerState, p_ext->dev_po_state );

	*p_action = IrpComplete;
	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_remove(
	IN				DEVICE_OBJECT* const	p_dev_obj,
	IN				IRP* const				p_irp, 
		OUT			cl_irp_action_t* const	p_action )
{
	bus_iou_ext_t	*p_ext;

	BUS_ENTER( BUS_DBG_PNP );

	UNUSED_PARAM( p_irp );

	p_ext = p_dev_obj->DeviceExtension;

	*p_action = IrpComplete;
	if( p_ext->n_ifc_ref )
	{
		/*
		 * Our interface is still being held by someone.
		 * Rollback the PnP state that was changed in the complib handler.
		 */
		cl_rollback_pnp_state( &p_ext->pdo.cl_ext );

		/* Fail the query. */
		BUS_TRACE_EXIT( BUS_DBG_PNP, ("Failing IRP_MN_QUERY_REMOVE_DEVICE:\n"
			"\tInterface has %d reference\n", p_ext->n_ifc_ref ) );
		return STATUS_UNSUCCESSFUL;
	}

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
static void iou_release_resources(
	IN					DEVICE_OBJECT* const	p_dev_obj )
{
	bus_iou_ext_t	*p_ext;
	POWER_STATE		po_state;
	iou_mgr_t		*p_iou_mgr;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;
	p_iou_mgr = p_ext->pdo.p_parent_ext->bus_filter->p_iou_mgr;

	/* Remove this PDO from its list. */
	cl_mutex_acquire( &p_iou_mgr->pdo_mutex );
	BUS_TRACE( BUS_DBG_PNP, ("Removing IOU from list.\n") );
	cl_qlist_remove_item( &p_iou_mgr->iou_list, &p_ext->pdo.list_item );
	cl_mutex_release( &p_iou_mgr->pdo_mutex );
	po_state.DeviceState = PowerDeviceD3;
	PoSetPowerState( p_ext->pdo.cl_ext.p_pdo, DevicePowerState, po_state );

	BUS_EXIT( BUS_DBG_PNP );
}


static NTSTATUS
iou_remove(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	bus_iou_ext_t	*p_ext;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;

	if( p_ext->pdo.b_present )
	{
		CL_ASSERT( p_ext->pdo.cl_ext.pnp_state != NotStarted );
		CL_ASSERT( !p_ext->pdo.b_reported_missing );
		/* Reset the state to NotStarted.  CompLib set it to Deleted. */
		cl_set_pnp_state( &p_ext->pdo.cl_ext, NotStarted );
		/* Don't delete the device.  It may simply be disabled. */
		*p_action = IrpComplete;
		BUS_TRACE_EXIT( BUS_DBG_PNP,
				("Device %s still present: PDO %p, ext %p\n",
				p_ext->pdo.cl_ext.vfptr_pnp_po->identity, p_dev_obj, p_ext) );
		return STATUS_SUCCESS;
	}

	if( !p_ext->pdo.b_reported_missing )
	{
		/* Reset the state to RemovePending.  Complib set it to Deleted. */
		cl_rollback_pnp_state( &p_ext->pdo.cl_ext );
		*p_action = IrpComplete;
		BUS_TRACE_EXIT( BUS_DBG_PNP, ("Device not reported missing yet.\n") );
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
	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp, 
		OUT			cl_irp_action_t* const		p_action )
{
	bus_iou_ext_t	*p_ext;

	BUS_ENTER( BUS_DBG_PNP );

	UNUSED_PARAM( p_irp );

	p_ext = p_dev_obj->DeviceExtension;
	p_ext->pdo.b_present = FALSE;
	p_ext->pdo.b_reported_missing = TRUE;

	BUS_TRACE( BUS_DBG_PNP, ("%s: ext %p, present %d, missing %d .\n",
				p_ext->pdo.cl_ext.vfptr_pnp_po->identity, p_ext,
				p_ext->pdo.b_present, p_ext->pdo.b_reported_missing ) );

	*p_action = IrpComplete;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_capabilities(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	DEVICE_CAPABILITIES		*p_caps;
	IO_STACK_LOCATION		*p_io_stack;

	BUS_ENTER( BUS_DBG_PNP );

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
	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_target_relations(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	NTSTATUS			status;
	DEVICE_RELATIONS	*p_rel;

	BUS_ENTER( BUS_DBG_PNP );

	*p_action = IrpComplete;

	status = cl_alloc_relations( p_irp, 1 );
	if( !NT_SUCCESS( status ) )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("cl_alloc_relations returned 0x%08x.\n", status) );
		return status;
	}

	p_rel = (DEVICE_RELATIONS*)p_irp->IoStatus.Information;
	p_rel->Count = 1;
	p_rel->Objects[0] = p_dev_obj;

	ObReferenceObject( p_dev_obj );

	BUS_EXIT( BUS_DBG_PNP );
	return status;
}


static NTSTATUS
iou_query_device_id(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	bus_iou_ext_t		*p_ext;
	WCHAR				*p_string;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = (bus_iou_ext_t*)p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	/* Device ID is "IBA\SID_<sid> where <sid> is the IPoIB Service ID. */
	p_string = ExAllocatePoolWithTag( NonPagedPool, IOU_DEV_ID_SIZE, 'didq' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Failed to allocate device ID buffer (%d bytes).\n",
			IOU_DEV_ID_SIZE) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status =
		RtlStringCbPrintfW( p_string, IOU_DEV_ID_SIZE, IOU_DEV_ID_STRING1,
			p_ext->vend_id, p_ext->dev_id, p_ext->revision );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to format device ID string.\n") );
		return status;
	}
	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_hardware_ids(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	bus_iou_ext_t		*p_ext;
	WCHAR				*p_string, *p_start;
	size_t				size;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = (bus_iou_ext_t*)p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	p_string = ExAllocatePoolWithTag( NonPagedPool, IOU_HW_ID_SIZE, 'dihq' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Failed to allocate hardware ID buffer (%d bytes).\n",
			IOU_HW_ID_SIZE) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	p_start = p_string;
	size = IOU_HW_ID_SIZE;
	/* Fill in the first HW ID. */
	status = RtlStringCbPrintfExW( p_start, size, &p_start, &size,
		STRSAFE_FILL_BEHIND_NULL | STRSAFE_NO_TRUNCATION, IOU_DEV_ID_STRING1,
		p_ext->vend_id, p_ext->dev_id, p_ext->revision );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to format device ID string.\n") );
		return status;
	}
	/* Fill in the second HW ID. */
	CL_ASSERT( *p_start == L'\0' );
	p_start++;
	size -= sizeof(WCHAR);
	status = RtlStringCbPrintfExW( p_start, size, NULL, NULL,
		STRSAFE_FILL_BEHIND_NULL | STRSAFE_NO_TRUNCATION, IOU_DEV_ID_STRING2,
		p_ext->vend_id, p_ext->dev_id );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to format device ID string.\n") );
		return status;
	}
	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_compatible_ids(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	WCHAR				*p_string;

	BUS_ENTER( BUS_DBG_PNP );

	UNUSED_PARAM( p_dev_obj );

	p_string = ExAllocatePoolWithTag( NonPagedPool, sizeof(IOU_COMPAT_ID), 'dicq' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Failed to allocate compatible ID buffer (%d bytes).\n",
			IOU_HW_ID_SIZE) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	cl_memcpy( p_string, IOU_COMPAT_ID, sizeof(IOU_COMPAT_ID) );
	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_unique_id(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	WCHAR				*p_string;
	bus_iou_ext_t		*p_ext;

	BUS_ENTER( BUS_DBG_PNP );
	
	p_ext = p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	/* The instance ID is the port GUID. */
	p_string = ExAllocatePoolWithTag( NonPagedPool, sizeof(WCHAR) * 33, 'diuq' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Failed to allocate instance ID buffer (%d bytes).\n",
			sizeof(WCHAR) * 17) );
		return STATUS_NO_MEMORY;
	}

	status = RtlStringCchPrintfW( p_string, 33, L"%016I64x%016I64x",
		p_ext->guid, p_ext->pdo.ca_guid );
	if( !NT_SUCCESS( status ) )
	{
		CL_ASSERT( NT_SUCCESS( status ) );
		ExFreePool( p_string );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("RtlStringCchPrintfW returned %08x.\n", status) );
		return status;
	}

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_description(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	WCHAR				*p_string;
	bus_iou_ext_t		*p_ext;

	BUS_ENTER( BUS_DBG_PNP );
	
	p_ext = p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	/* The instance ID is the port GUID. */
	p_string = ExAllocatePoolWithTag( NonPagedPool,
									  sizeof(WCHAR) *  _countof(p_ext->desc),
									  'sedq' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Failed to allocate device description buffer (%d bytes).\n",
			sizeof(WCHAR) * _countof(p_ext->desc)) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = RtlStringCchPrintfW( p_string, sizeof(p_ext->desc),
		L"%S", p_ext->desc );
	if( !NT_SUCCESS( status ) )
	{
		CL_ASSERT( NT_SUCCESS( status ) );
		ExFreePool( p_string );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("RtlStringCchPrintfW returned %08x.\n", status) );
		return status;
	}
	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_location(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	bus_iou_ext_t		*p_ext;
	WCHAR				*p_string;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = (bus_iou_ext_t*)p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	p_string = ExAllocatePoolWithTag( NonPagedPool, IOU_LOCATION_SIZE, 'colq' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Failed to allocate location buffer (%d bytes).\n",
			IOU_LOCATION_SIZE) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = RtlStringCbPrintfW( p_string, IOU_LOCATION_SIZE,
		L"Chassis 0x%016I64x, Slot %d", p_ext->chassis_guid, p_ext->slot );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to format device ID string.\n") );
		return status;
	}
	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_bus_info(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	PNP_BUS_INFORMATION	*p_bus_info;

	BUS_ENTER( BUS_DBG_PNP );

	UNUSED_PARAM( p_dev_obj );

	*p_action = IrpComplete;

	p_bus_info = ExAllocatePoolWithTag( NonPagedPool, sizeof(PNP_BUS_INFORMATION), 'subq' );
	if( !p_bus_info )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate PNP_BUS_INFORMATION (%d bytes).\n",
			sizeof(PNP_BUS_INFORMATION)) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	p_bus_info->BusTypeGuid = GUID_BUS_TYPE_IBA;
	//TODO: Memory from Intel - storage miniport would not stay loaded unless
	//TODO: bus type was PCI.  Look here if SRP is having problems staying
	//TODO: loaded.
	p_bus_info->LegacyBusType = PNPBus;
	p_bus_info->BusNumber = 0;

	p_irp->IoStatus.Information = (ULONG_PTR)p_bus_info;
	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_iou_ifc(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IO_STACK_LOCATION* const	p_io_stack )
{
	NTSTATUS				status;
	ib_al_ifc_t				*p_ifc;
	ib_al_ifc_data_t		*p_ifc_data;
	iou_ifc_data_t			*p_iou_data;
	bus_iou_ext_t			*p_ext;
	const GUID				*p_guid;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

	p_ext = p_dev_obj->DeviceExtension;

	/* Get the interface. */
	status = cl_fwd_query_ifc(
		p_ext->pdo.p_parent_ext->cl_ext.p_self_do, p_io_stack );
	if( !NT_SUCCESS( status ) )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to forward interface query: %08X\n", status) );
		return status;
	}

	if( !p_io_stack->Parameters.QueryInterface.InterfaceSpecificData )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("No interface specific data!\n") );
		return status;
	}

	p_ifc = (ib_al_ifc_t*)p_io_stack->Parameters.QueryInterface.Interface;

	p_ifc_data = (ib_al_ifc_data_t*)
		p_io_stack->Parameters.QueryInterface.InterfaceSpecificData;
	p_guid = p_ifc_data->type;
	if( !IsEqualGUID( p_guid, &GUID_IOU_INTERFACE_DATA ) )
	{
		BUS_TRACE_EXIT( BUS_DBG_PNP, ("Unsupported interface data: \n\t"
			"0x%08x, 0x%04x, 0x%04x, 0x%02x, 0x%02x, 0x%02x,"
			"0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x.\n",
			p_guid->Data1, p_guid->Data2, p_guid->Data3,
			p_guid->Data4[0], p_guid->Data4[1], p_guid->Data4[2],
			p_guid->Data4[3], p_guid->Data4[4], p_guid->Data4[5],
			p_guid->Data4[6], p_guid->Data4[7]) );
		return status;
	}

	if( p_ifc_data->version != IOU_INTERFACE_DATA_VERSION )
	{
		p_ifc->wdm.InterfaceDereference( p_ifc->wdm.Context );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Unsupported version %d, expected %d\n",
			p_ifc_data->version, IOU_INTERFACE_DATA_VERSION) );
		return STATUS_NOT_SUPPORTED;
	}

	ASSERT( p_ifc_data->p_data );

	if( p_ifc_data->size != sizeof(iou_ifc_data_t) )
	{
		p_ifc->wdm.InterfaceDereference( p_ifc->wdm.Context );
		BUS_TRACE_EXIT( BUS_DBG_PNP,
			("Buffer too small (%d given, %d required).\n",
			p_ifc_data->size,
			sizeof(iou_ifc_data_t)) );
		return STATUS_BUFFER_TOO_SMALL;
	}

	/* Set the interface data. */
	p_iou_data = (iou_ifc_data_t*)p_ifc_data->p_data;

	p_iou_data->ca_guid = p_ext->pdo.ca_guid;
	p_iou_data->chassis_guid = p_ext->chassis_guid;
	p_iou_data->slot = p_ext->slot;
	p_iou_data->guid = p_ext->guid;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
iou_query_interface(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp, 
		OUT				cl_irp_action_t* const	p_action )
{
	bus_pdo_ext_t		*p_ext;
	NTSTATUS			status;
	IO_STACK_LOCATION	*p_io_stack;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	
	/* Bottom of the stack - IRP must be completed. */
	*p_action = IrpComplete;

	/* Compare requested GUID with our supported interface GUIDs. */
	if( IsEqualGUID( p_io_stack->Parameters.QueryInterface.InterfaceType,
		&GUID_IB_AL_INTERFACE ) )
	{
		status = iou_query_iou_ifc( p_dev_obj, p_io_stack );
	}
	else if( IsEqualGUID( p_io_stack->Parameters.QueryInterface.InterfaceType,
		&GUID_BUS_INTERFACE_STANDARD ) )
	{
		p_ext = p_dev_obj->DeviceExtension;
		if( !p_ext->h_ca ||
			!p_ext->b_present ||
			!p_ext->h_ca->p_hca_dev ||
			p_ext->b_reported_missing )
		{
			return STATUS_NO_SUCH_DEVICE;
		}


		status = cl_fwd_query_ifc(
			p_ext->h_ca->p_hca_dev, p_io_stack );
	}
	else
	{
		status = p_irp->IoStatus.Status;
	}

	BUS_EXIT( BUS_DBG_PNP );
	return status;
}



/* Work item callback to handle DevicePowerD3 IRPs at passive level. */
static IO_WORKITEM_ROUTINE __HibernateUpWorkItem;
static void
__HibernateUpWorkItem(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				void*						context )
{
	IO_STACK_LOCATION	*p_io_stack;
	bus_pdo_ext_t		*p_ext;
	IRP					*p_irp;
	POWER_STATE powerState;

	BUS_ENTER( BUS_DBG_POWER );

	p_ext = (bus_pdo_ext_t*)p_dev_obj->DeviceExtension;
	p_irp = (IRP*)context;
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	IoFreeWorkItem( p_ext->p_po_work_item );
	p_ext->p_po_work_item = NULL;

	while (!p_ext->h_ca) {
		BUS_TRACE( BUS_DBG_PNP, ("Waiting for the end of HCA registration ... \n"));
		cl_thread_suspend( 200 );	/* suspend for 200 ms */
	}

	p_ext->dev_po_state = p_io_stack->Parameters.Power.State;
	powerState = PoSetPowerState( p_dev_obj, DevicePowerState, p_ext->dev_po_state );

	BUS_TRACE( BUS_DBG_POWER, 
		("PoSetPowerState: old state %d, new state to %d\n", 
		powerState.DeviceState, p_ext->dev_po_state ));

	p_irp->IoStatus.Status = STATUS_SUCCESS;
	PoStartNextPowerIrp( p_irp );
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	IoReleaseRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );

	BUS_EXIT( BUS_DBG_POWER );
}

/*
 * The PDOs created by the IB Bus driver are software devices.  As such,
 * all power states are supported.  It is left to the HCA power policy 
 * owner to handle which states can be supported by the HCA.
 */
static NTSTATUS
iou_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	NTSTATUS			status = STATUS_SUCCESS;
	IO_STACK_LOCATION	*p_io_stack;
	bus_pdo_ext_t		*p_ext;

	BUS_ENTER( BUS_DBG_POWER );

	p_ext = p_dev_obj->DeviceExtension;
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	BUS_TRACE( BUS_DBG_POWER, 
		("SET_POWER for PDO %p (ext %p): type %s, state %d, action %d \n",
		p_dev_obj, p_ext,
		(p_io_stack->Parameters.Power.Type) ? "DevicePowerState" : "SystemPowerState",
		p_io_stack->Parameters.Power.State.DeviceState, 
		p_io_stack->Parameters.Power.ShutdownType ));

	if ((p_io_stack->Parameters.Power.Type == SystemPowerState) &&
		(p_io_stack->Parameters.Power.State.SystemState ==PowerSystemHibernate ||
		p_io_stack->Parameters.Power.State.SystemState ==PowerSystemSleeping1 || 
		p_io_stack->Parameters.Power.State.SystemState ==PowerSystemSleeping2 || 
		p_io_stack->Parameters.Power.State.SystemState ==PowerSystemSleeping3 ))
	{
		BUS_TRACE( BUS_DBG_POWER, ("Setting b_hibernating flag for PDO %p \n", p_dev_obj));
		p_ext->b_hibernating = TRUE;
	}

	if( p_io_stack->Parameters.Power.Type == DevicePowerState )
	{
		/* after hibernation PDO is not ready for work. we need to wait for finishing of the HCA registration */
		if( p_io_stack->Parameters.Power.State.DeviceState == PowerDeviceD0 && p_ext->b_hibernating)
		{
			/* Process in a work item - deregister_ca and HcaDeinit block. */
			ASSERT( !p_ext->p_po_work_item );
			p_ext->p_po_work_item = IoAllocateWorkItem( p_dev_obj );
			if( !p_ext->p_po_work_item )
				status = STATUS_INSUFFICIENT_RESOURCES;
			else {
				/* Process in work item callback. */
				IoMarkIrpPending( p_irp );
				IoQueueWorkItem(
					p_ext->p_po_work_item, __HibernateUpWorkItem, DelayedWorkQueue, p_irp );
				*p_action = IrpDoNothing;
				BUS_EXIT( BUS_DBG_POWER );
				return STATUS_PENDING;
			}
		}

		/* Notify the power manager. */
		p_ext->dev_po_state = p_io_stack->Parameters.Power.State;
		PoSetPowerState( p_dev_obj, DevicePowerState, p_ext->dev_po_state );
	}

	*p_action = IrpComplete;
	BUS_EXIT( BUS_DBG_POWER );
	return STATUS_SUCCESS;
}
