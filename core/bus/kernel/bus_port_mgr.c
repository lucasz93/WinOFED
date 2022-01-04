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
#include "iba/ipoib_ifc.h"


/* {5A9649F4-0101-4a7c-8337-796C48082DA2} */
DEFINE_GUID(GUID_BUS_TYPE_IBA,
0x5a9649f4, 0x101, 0x4a7c, 0x83, 0x37, 0x79, 0x6c, 0x48, 0x8, 0x2d, 0xa2);


/*
 * Device extension for IPoIB port PDOs.
 */
typedef struct _bus_port_ext
{
	bus_pdo_ext_t			pdo;

	port_guid_pkey_t		port_guid;
	uint32_t				n_port;

	/* Number of references on the upper interface. */
	atomic32_t				n_ifc_ref;

}	bus_port_ext_t;


typedef struct _port_pnp_context
{
	bus_filter_t	*p_bus_filter;
	void			*p_pdo_ext;
	int				port_num;

}	port_pnp_ctx_t;


extern pkey_array_t  g_pkeys;

/*
 * Function prototypes.
 */
void
destroying_port_mgr(
	IN				cl_obj_t*					p_obj );

void
free_port_mgr(
	IN				cl_obj_t*					p_obj );

ib_api_status_t
bus_reg_port_pnp(
	IN				bus_filter_t*				p_bfi );

ib_api_status_t
port_mgr_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec );

ib_api_status_t
port_mgr_port_add(
	IN				ib_pnp_port_rec_t*			p_pnp_rec );

void
port_mgr_port_remove(
	IN				ib_pnp_port_rec_t*			p_pnp_rec );

static NTSTATUS
port_start(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
port_query_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static void
port_release_resources(
	IN				DEVICE_OBJECT* const		p_dev_obj );

static NTSTATUS
port_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
port_surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
port_query_capabilities(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
port_query_target_relations(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
port_query_device_id(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
port_query_hardware_ids(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
port_query_compatible_ids(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
port_query_unique_id(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
port_query_description(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );


static NTSTATUS
port_query_location(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp );

static NTSTATUS
port_query_bus_info(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
port_query_ipoib_ifc(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IO_STACK_LOCATION* const	p_io_stack );

static NTSTATUS
port_query_interface(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );

static NTSTATUS
port_set_power(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action );



/*
 * Global virtual function pointer tables shared between all
 * instances of Port PDOs.
 */
static const cl_vfptr_pnp_po_t		vfptr_port_pnp = {
	"IODEVICE",
	port_start,
	cl_irp_succeed,
	cl_irp_succeed,
	cl_irp_succeed,
	port_query_remove,
	port_release_resources,
	port_remove,
	cl_irp_succeed,
	port_surprise_remove,
	port_query_capabilities,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_succeed,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	port_query_target_relations,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	port_query_bus_info,
	port_query_interface,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_complete,
	cl_irp_succeed,				// QueryPower
	port_set_power,				// SetPower
	cl_irp_unsupported,			// PowerSequence
	cl_irp_unsupported			// WaitWake
};


static const cl_vfptr_query_txt_t		vfptr_port_query_txt = {
	port_query_device_id,
	port_query_hardware_ids,
	port_query_compatible_ids,
	port_query_unique_id,
	port_query_description,
	port_query_location
};



/*
 * Create the AL load service.
 */
ib_api_status_t
create_port_mgr(
		IN			bus_filter_t*				p_bfi )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;
	port_mgr_t			*p_port_mgr;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( p_bfi->p_port_mgr == NULL );

	p_port_mgr = cl_zalloc( sizeof( port_mgr_t ) );
	if( !p_port_mgr )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate port manager.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}
	p_bfi->p_port_mgr = p_port_mgr;

	/* Construct the load service. */
	cl_obj_construct( &p_port_mgr->obj, AL_OBJ_TYPE_LOADER );

	p_bfi->p_port_mgr_obj = &p_port_mgr->obj;

	cl_mutex_construct( &p_port_mgr->pdo_mutex );
	cl_qlist_init( &p_port_mgr->port_list );

	cl_status = cl_mutex_init( &p_port_mgr->pdo_mutex );
	if( cl_status != CL_SUCCESS )
	{
		free_port_mgr( &p_port_mgr->obj );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("cl_mutex_init returned %#x.\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	/* Initialize the load service object. */
	cl_status = cl_obj_init( &p_port_mgr->obj, CL_DESTROY_SYNC,
							 destroying_port_mgr, NULL, free_port_mgr );

	if( cl_status != CL_SUCCESS )
	{
		free_port_mgr( &p_port_mgr->obj );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("cl_obj_init returned %#x.\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	/* Register for port PnP events */
	status = bus_reg_port_pnp( p_bfi );
	if( status != IB_SUCCESS )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("bus_reg_port_pnp returned %s.\n", ib_get_err_str(status)) );
		free_port_mgr( &p_port_mgr->obj );
		return status;
	}

	BUS_EXIT( BUS_DBG_PNP );
	return IB_SUCCESS;
}


/*
 * Pre-destroy the load service.
 */
void
destroying_port_mgr(
	IN				cl_obj_t*					p_obj )
{
	ib_api_status_t			status;
	bus_filter_t			*p_bfi;
	port_mgr_t				*p_port_mgr;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( p_obj );
	p_bfi = get_bfi_by_obj(BFI_PORT_MGR_OBJ, p_obj);
	if (p_bfi == NULL) {
		BUS_PRINT(BUS_DBG_PNP, ("Failed to find p_bfi by obj %p?\n", p_obj));
		return;
	}
	p_port_mgr = p_bfi->p_port_mgr;

	BUS_PRINT(BUS_DBG_PNP, ("%s obj %p port_mgr %p port_mgr_obj %p\n",
			p_bfi->whoami,p_obj,p_port_mgr, p_bfi->p_port_mgr_obj) );

	CL_ASSERT( p_port_mgr == PARENT_STRUCT( p_obj, port_mgr_t, obj ) );

	/* Deregister for port PnP events if this is the last Port manager. */
	if ( get_bfi_count() == 1 && bus_globals.h_pnp_port ) {
		status = ib_dereg_pnp( bus_globals.h_pnp_port, NULL );
		bus_globals.h_pnp_port = NULL;
		CL_ASSERT( status == IB_SUCCESS );
	}
	cl_obj_deref( p_bfi->p_port_mgr_obj );

	BUS_EXIT( BUS_DBG_PNP );
}


/*
 * Free the load service.
 */
void
free_port_mgr(
	IN				cl_obj_t*					p_obj )
{
	bus_pdo_ext_t	*p_ext;
	cl_list_item_t	*p_list_item;
	bus_filter_t	*p_bfi;
	port_mgr_t		*p_port_mgr;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( p_obj );
	p_bfi = get_bfi_by_obj(BFI_PORT_MGR_OBJ, p_obj);
	if (p_bfi == NULL) {
		BUS_PRINT(BUS_DBG_PNP, ("No p_bfi for port obj %p?\n", p_obj));
		return;
	}
	p_port_mgr = p_bfi->p_port_mgr;
	if ( !p_port_mgr ) {
		// if create fails & then free is called, p_bfi->p_port_mgr == NULL
		return;
	}
	CL_ASSERT( p_port_mgr == PARENT_STRUCT( p_obj, port_mgr_t, obj ) );

	BUS_PRINT(BUS_DBG_PNP, ("%s obj %p port_mgr %p port_mgr_obj %p\n",
			p_bfi->whoami, p_obj,p_port_mgr,
			p_bfi->p_port_mgr_obj) );

	BUS_PRINT( BUS_DBG_PNP,
				("%s Mark all IPoIB PDOs no longer present\n", p_bfi->whoami));
	/*
	 * Mark all IPoIB PDOs as no longer present.  This will cause them
	 * to be removed when they process the IRP_MN_REMOVE_DEVICE.
	 */
	p_list_item = cl_qlist_remove_head( &p_port_mgr->port_list );
	while( p_list_item != cl_qlist_end( &p_port_mgr->port_list ) )
	{
		p_ext = PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );
		p_list_item = cl_qlist_remove_head( &p_port_mgr->port_list );
		if( p_ext->cl_ext.pnp_state == SurpriseRemoved )
		{
			CL_ASSERT( !p_ext->b_present );
			p_ext->b_reported_missing = TRUE;
			BUS_TRACE( BUS_DBG_PNP,
				("%s %s: PDO %p, ext %p, present %d, missing %d .\n",
				p_bfi->whoami,
				p_ext->cl_ext.vfptr_pnp_po->identity, p_ext->cl_ext.p_self_do,
				p_ext, p_ext->b_present, p_ext->b_reported_missing ) );
			continue;
		}

		if( p_ext->h_ca && p_ext->hca_acquired )
		{
			/* Invalidate bus relations for the HCA. */
			IoInvalidateDeviceRelations(
				p_ext->h_ca->p_hca_dev, BusRelations );

			/* Release the reference on the CA object. */
			deref_al_obj( &p_ext->h_ca->obj );
		}

		BUS_TRACE( BUS_DBG_PNP, ("%s Deleted device %s: PDO %p, ext %p\n",
						p_bfi->whoami, p_ext->cl_ext.vfptr_pnp_po->identity,
						p_ext->cl_ext.p_self_do, p_ext ) );

		IoDeleteDevice( p_ext->cl_ext.p_self_do );
	}

	cl_mutex_destroy( &p_port_mgr->pdo_mutex );
	cl_obj_deinit( p_obj );
	cl_free( p_port_mgr );

	p_bfi->p_port_mgr = NULL;
	p_bfi->p_port_mgr_obj = NULL;

	BUS_EXIT( BUS_DBG_PNP );
}


/*
 * Register the load service for the given PnP class events.
 */
ib_api_status_t
bus_reg_port_pnp( IN bus_filter_t *p_bfi )
{
	ib_pnp_req_t			pnp_req;
	ib_api_status_t			status = IB_SUCCESS;
	boolean_t				need_pnp_reg = FALSE;

	/* only need to register for port PNP events once.
	 * Do not hold mutex over pnp_reg() call as callback which needs mutex
	 * could occur.
	 */
	if ( !bus_globals.h_pnp_port )
	{
		lock_control_event();
		if ( !bus_globals.h_pnp_port ) {
			bus_globals.h_pnp_port = (ib_pnp_handle_t)1; /* block others */
			need_pnp_reg = TRUE;
		}
		unlock_control_event();

		if ( need_pnp_reg )
		{
			cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
			pnp_req.pnp_class	= IB_PNP_PORT | IB_PNP_FLAG_REG_SYNC;
			pnp_req.pnp_context = NULL;
			pnp_req.pfn_pnp_cb	= port_mgr_pnp_cb;

			status = ib_reg_pnp( gh_al, &pnp_req, &bus_globals.h_pnp_port );
		}
	}

	if ( status == IB_SUCCESS )
	{
		/* Reference this bus filter's port load service */
		cl_obj_ref( p_bfi->p_port_mgr_obj );
	}

	return status;
}


/*
 * Load service PnP event callback.
 */
ib_api_status_t
port_mgr_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec )
{
	ib_api_status_t		status=IB_SUCCESS;
	port_pnp_ctx_t		*p_ctx;
	bus_filter_t		*p_bfi;

	BUS_ENTER( BUS_DBG_PNP );

	BUS_PRINT(BUS_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );

	CL_ASSERT( p_pnp_rec );
	p_ctx = p_pnp_rec->context;

	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_PORT_ADD:
		status = port_mgr_port_add( (ib_pnp_port_rec_t*)p_pnp_rec );
		break;

	case IB_PNP_PORT_REMOVE:
		if (p_ctx)
		{
			p_bfi = p_ctx->p_bus_filter;
			CL_ASSERT( p_bfi );
			if (p_bfi->p_port_mgr->active_ports > 0)
				cl_atomic_dec( &p_bfi->p_port_mgr->active_ports );
			port_mgr_port_remove( (ib_pnp_port_rec_t*)p_pnp_rec );
		}
		break;

	case IB_PNP_PORT_ACTIVE:
		if (p_ctx)
		{
			p_bfi = p_ctx->p_bus_filter;
			CL_ASSERT( p_bfi );
			cl_atomic_inc( &p_bfi->p_port_mgr->active_ports );
		}
		break;

	default:
		XBUS_PRINT( BUS_DBG_PNP, ("Unhandled PNP Event %s\n",
					ib_get_pnp_event_str(p_pnp_rec->pnp_event) ));
		break;
	}
	BUS_EXIT( BUS_DBG_PNP );

	return status;
}


/*
 * Called to get bus relations for an HCA.
 */
#pragma prefast(suppress: 28167, "The irql level is restored here")
NTSTATUS port_mgr_get_bus_relations(
	IN				bus_filter_t*				p_bfi,
	IN				IRP* const					p_irp )
{
	NTSTATUS			status;

	BUS_ENTER( BUS_DBG_PNP );

	BUS_PRINT(BUS_DBG_PNP, ("CA_guid %I64x\n",p_bfi->ca_guid));

	CL_ASSERT( p_bfi->ca_guid );

	BUS_PRINT(BUS_DBG_PNP, ("%s for ca_guid %I64x port_mgr %p\n",
							p_bfi->whoami, p_bfi->ca_guid, p_bfi->p_port_mgr) );
	if (!p_bfi->p_port_mgr)
		return STATUS_NO_SUCH_DEVICE;

	cl_mutex_acquire( &p_bfi->p_port_mgr->pdo_mutex );
	status = bus_get_relations( &p_bfi->p_port_mgr->port_list, p_bfi->ca_guid, p_irp );
	cl_mutex_release( &p_bfi->p_port_mgr->pdo_mutex );

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
static ib_api_status_t __port_was_hibernated(
	IN				ib_pnp_port_rec_t*			p_pnp_rec,
	IN				bus_filter_t*				p_bfi )
{
	ib_api_status_t	status;
	cl_list_item_t	*p_list_item;
	bus_port_ext_t	*p_port_ext;
	bus_pdo_ext_t	*p_shadow_pdo_ext, *p_pdo_ext = NULL;
	size_t			n_devs = 0;
	port_mgr_t		*p_port_mgr = p_bfi->p_port_mgr;
	cl_qlist_t		*p_pdo_list = &p_port_mgr->port_list;
	port_pnp_ctx_t	*p_ctx = p_pnp_rec->pnp_rec.context;

	BUS_ENTER( BUS_DBG_PNP );

	if ( !p_port_mgr ) {
		// if free_port_mgr has been called , p_bfi->p_port_mgr == NULL 
		// this will cause crash on cl_mutex_acquire
		// (leo) i'm not sure when it happens, but i saw it happened
		status = IB_NOT_FOUND;
		goto end;
	}

	cl_mutex_acquire( &p_port_mgr->pdo_mutex );
	
	/* Count the number of child devices. */
	for( p_list_item = cl_qlist_head( p_pdo_list );
		p_list_item != cl_qlist_end( p_pdo_list );
		p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_pdo_ext = PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );
		p_port_ext = (bus_port_ext_t*)p_pdo_ext;
	
		if( p_pdo_ext->b_present && p_pdo_ext->b_hibernating && p_pdo_ext->hca_acquired &&
			(p_port_ext->port_guid.guid == p_pnp_rec->p_port_attr->port_guid) )
		{
			n_devs++;
			break;
		}

		BUS_TRACE( BUS_DBG_PNP, ("%s Skipped acquire hca on PDO for %s: PDO %p, ext %p, "
			"present %d, missing %d, hibernating %d, port_guid %I64x.\n",
			p_bfi->whoami,
			p_pdo_ext->cl_ext.vfptr_pnp_po->identity,
			p_pdo_ext->cl_ext.p_self_do, 
			p_pdo_ext, p_pdo_ext->b_present, p_pdo_ext->b_reported_missing, 
			p_pdo_ext->b_hibernating, p_port_ext->port_guid.guid  ) );
	}

	if (n_devs)
	{
		/* Take a reference on the parent HCA. */
		p_pdo_ext->h_ca = acquire_ca( p_pnp_rec->p_ca_attr->ca_guid );
		if( !p_pdo_ext->h_ca )
		{
			BUS_TRACE( BUS_DBG_ERROR,
				("%s acquire_ca failed to find CA by guid %I64x\n",
				p_bfi->whoami, p_pnp_rec->p_ca_attr->ca_guid ) );
			status = IB_INVALID_GUID;
		}
		else 
		{
			p_pdo_ext->b_hibernating = FALSE;

			CL_ASSERT( p_ctx );
			p_ctx->p_pdo_ext = p_pdo_ext; // save for port_mgr_port_remove

			status = IB_SUCCESS;
			p_port_ext = (bus_port_ext_t*)p_pdo_ext;
			BUS_TRACE( BUS_DBG_PNP, ("%s Found PDO for %s: PDO %p, ext %p, "
				"present %d, missing %d, hibernating %d, port_guid %I64x.\n",
				p_bfi->whoami,
				p_pdo_ext->cl_ext.vfptr_pnp_po->identity,
				p_pdo_ext->cl_ext.p_self_do, 
				p_pdo_ext, p_pdo_ext->b_present, p_pdo_ext->b_reported_missing, 
				p_pdo_ext->b_hibernating, p_port_ext->port_guid.guid ) );

			for( p_list_item = cl_qlist_head( p_pdo_list );
				p_list_item != cl_qlist_end( p_pdo_list );
				p_list_item = cl_qlist_next( p_list_item ) )
			{
				p_shadow_pdo_ext = PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );
				p_port_ext = (bus_port_ext_t*)p_shadow_pdo_ext;

				if( p_shadow_pdo_ext->b_present && p_shadow_pdo_ext->b_hibernating &&
					(p_port_ext->port_guid.guid == p_pnp_rec->p_port_attr->port_guid) )
				{
					p_shadow_pdo_ext->b_hibernating = FALSE;
					p_shadow_pdo_ext->h_ca = p_pdo_ext->h_ca;


					BUS_TRACE( BUS_DBG_PNP, ("%s set shadow h_ca PDO for %s: PDO %p, ext %p, "
											 "present %d, missing %d, hibernating %d, port_guid %I64x.\n",
											 p_bfi->whoami,
											 p_shadow_pdo_ext->cl_ext.vfptr_pnp_po->identity,
											 p_shadow_pdo_ext->cl_ext.p_self_do, 
											 p_shadow_pdo_ext, p_shadow_pdo_ext->b_present, p_shadow_pdo_ext->b_reported_missing, 
											 p_shadow_pdo_ext->b_hibernating, p_port_ext->port_guid.guid  ) );
				}
			}

		}
	}
	else 
	{
		BUS_TRACE( BUS_DBG_PNP, ("%s Failed to find PDO for guid  %I64x .\n",
					p_bfi->whoami, p_pnp_rec->p_ca_attr->ca_guid ) );
		status = IB_NOT_FOUND;
	}

	cl_mutex_release( &p_port_mgr->pdo_mutex );

end:
	BUS_EXIT( BUS_DBG_PNP );
	return status;
}

#if DBG

void
dump_pnp_port_rec( ib_pnp_port_rec_t*	pr )
{
	BUS_TRACE( BUS_DBG_PNP, ("ib_pnp_port_rec_t* @ %p\nib_pnp_rec_t*:\n", pr));


	BUS_PRINT( BUS_DBG_PNP, ("  Event %s\n",
				ib_get_pnp_event_str(pr->pnp_rec.pnp_event) ));

	BUS_PRINT( BUS_DBG_PNP, ("  pnp_context %p\n",pr->pnp_rec.pnp_context));
	BUS_PRINT( BUS_DBG_PNP, ("  context %p\n",pr->pnp_rec.context));
	BUS_PRINT( BUS_DBG_PNP, ("  guid %I64x\n",pr->pnp_rec.guid));
	BUS_PRINT( BUS_DBG_PNP, ("  ca_guid %I64x\n",pr->pnp_rec.ca_guid));

	if ( !pr->p_ca_attr ) {
		BUS_PRINT( BUS_DBG_PNP, ("  NULL *p_ca_attr ?\n"));
	}
	else {
		BUS_PRINT( BUS_DBG_PNP, ("*p_ca_attr\n"));
		BUS_PRINT( BUS_DBG_PNP, ("  ca_guid 0x%I64x\n",
								pr->p_ca_attr->ca_guid ) );
	}
	if ( !pr->p_port_attr ) {
		BUS_PRINT( BUS_DBG_PNP, ("  NULL *p_port_attr?\n"));
	}
	else {
		BUS_PRINT( BUS_DBG_PNP, ("*p_port_attr:\n"));
		BUS_PRINT( BUS_DBG_PNP, ("  port_guid 0x%I64x port_num %d\n",
								pr->p_port_attr->port_guid,
 								pr->p_port_attr->port_num ));
	}
}

void
dump_pnp_iou_rec( ib_pnp_iou_rec_t*	pr )
{
	BUS_TRACE( BUS_DBG_PNP, ("ib_pnp_iou_rec_t* @ %p\nib_pnp_rec_t*:\n", pr));


	BUS_PRINT( BUS_DBG_PNP, ("  Event %s\n",
				ib_get_pnp_event_str(pr->pnp_rec.pnp_event) ));

	BUS_PRINT( BUS_DBG_PNP, ("  pnp_context %p\n",pr->pnp_rec.pnp_context));
	BUS_PRINT( BUS_DBG_PNP, ("  context %p\n",pr->pnp_rec.context));
	BUS_PRINT( BUS_DBG_PNP, ("  guid %I64x\n",pr->pnp_rec.guid));
	BUS_PRINT( BUS_DBG_PNP, ("  ca_guid %I64x\n",pr->pnp_rec.ca_guid));

	BUS_PRINT( BUS_DBG_PNP, ("pnp_iou_rec_t:\n" ));
	BUS_PRINT( BUS_DBG_PNP,
				("  guid 0x%I64x\n  ca_guid %I64x\n  chassis_guid %I64x\n",
				pr->guid, pr->ca_guid, pr->chassis_guid ));
	BUS_PRINT( BUS_DBG_PNP,
				("  slot 0x%x\n  vend_id 0x%x\n  dev_id 0x%x  revision 0x%x\n",
				pr->slot, pr->vend_id, pr->dev_id, pr->revision ));
	if ( pr->desc[0] ) {
		BUS_PRINT( BUS_DBG_PNP, ("  Desc %s\n",pr->desc ));
	}
}
#endif


ib_api_status_t
port_mgr_port_add(
	IN				ib_pnp_port_rec_t*			p_pnp_rec )
{
	NTSTATUS		status;
	DEVICE_OBJECT   *p_pdo; 
	bus_port_ext_t	*p_port_ext;
	bus_filter_t	*p_bfi;
	port_mgr_t		*p_port_mgr;
	port_pnp_ctx_t	*p_ctx = p_pnp_rec->pnp_rec.context;
	child_device_info_list_t *pCurList;
	ib_ca_handle_t	h_ca = NULL;
	ULONG			pKey;
	UNICODE_STRING	uniKey;
	pkey_conf_t *p_cur_conf;
	ANSI_STRING ansi_str;
	int shift = 0;


	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( p_pnp_rec->p_ca_attr->ca_guid );
    g_post_event("Ibbus: port_mgr_port_add started\n");

	p_bfi = get_bfi_by_ca_guid( p_pnp_rec->p_ca_attr->ca_guid );
	if ( !p_bfi ) {
		BUS_TRACE_EXIT( BUS_DBG_PNP,("NULL p_bfi? ca_guid 0x%I64x\n",
								p_pnp_rec->p_ca_attr->ca_guid ) );
		return IB_ERROR;
	}

	/*
	 * Don't create PDO for IPoIB (and start IPoIB) while over a RoCE port.
	 */
	if ( p_pnp_rec->p_port_attr->transport != RDMA_TRANSPORT_IB ){
		BUS_TRACE_EXIT( BUS_DBG_PNP,("IPoIb is not started for RoCE port. %s ca_guid %I64x port(%d)\n",
								p_bfi->whoami, p_bfi->ca_guid, p_pnp_rec->p_port_attr->port_num));
		return IB_SUCCESS;
	}

	/*
	 * Allocate a PNP context for this object. pnp_rec.context is obj unique.
	 */
	if ( !p_ctx ) {
		p_ctx = cl_zalloc( sizeof(*p_ctx) );
		if( !p_ctx )
		{
			BUS_TRACE_EXIT(BUS_DBG_PNP,
					("%s ca_guid %I64x port(%d) BAD alloc PNP context\n",
					p_bfi->whoami, p_bfi->ca_guid, 
					p_pnp_rec->p_port_attr->port_num));
			return IB_ERROR;
		}
		p_ctx->p_bus_filter = p_bfi;
		p_ctx->port_num = p_pnp_rec->p_port_attr->port_num;
		p_pnp_rec->pnp_rec.context = p_ctx;

		BUS_PRINT(BUS_DBG_PNP,
					("%s ca_guid %I64x port %d ALLOC p_ctx @ %p\n",
					p_bfi->whoami, p_bfi->ca_guid, 
					p_pnp_rec->p_port_attr->port_num,p_ctx));
	}

	p_port_mgr = p_bfi->p_port_mgr;

	if( !bus_globals.b_report_port_nic )
	{
		BUS_EXIT( BUS_DBG_PNP );
		return IB_NOT_DONE;
	}

	/* Upon hibernating the computer IB_BUS driver doesn't remove PDO, but
	   marks with a flag. So we first try to find an existing PDO for this port,
	   marked with this flag. If it was found, we turn off the flag and use
	   this PDO */
	status = __port_was_hibernated( p_pnp_rec, p_bfi );
	if( status != IB_NOT_FOUND )
	{
		BUS_EXIT( BUS_DBG_PNP );
		return status;
	}

	pCurList = bus_globals.p_device_list;

	while(pCurList)
	{
		/* Create the PDO for the new port device. */
		status = IoCreateDevice( bus_globals.p_driver_obj,
								 sizeof(bus_port_ext_t),
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

		/* clean the extension (must be before initializing) */
		p_port_ext = p_pdo->DeviceExtension;
		memset( p_port_ext, 0, sizeof(bus_port_ext_t) );

		/* Initialize the device extension. */
		cl_init_pnp_po_ext( p_pdo, NULL, p_pdo,
							bus_globals.dbg_lvl, &vfptr_port_pnp,
							&vfptr_port_query_txt );

		/* Set the DO_BUS_ENUMERATED_DEVICE flag to mark it as a PDO. */
		p_pdo->Flags |= DO_BUS_ENUMERATED_DEVICE;

		p_port_ext->pdo.dev_po_state.DeviceState = PowerDeviceD0;
		p_port_ext->pdo.p_parent_ext = p_bfi->p_bus_ext;
		p_port_ext->pdo.b_present = TRUE;
		p_port_ext->pdo.b_reported_missing = FALSE;
		p_port_ext->pdo.b_hibernating = FALSE;
		p_port_ext->pdo.p_po_work_item = NULL;
		p_port_ext->pdo.p_pdo_device_info = &pCurList->io_device_info;
		BUS_TRACE( BUS_DBG_PNP,
			("Created %s %s: PDO %p,ext %p, present %d, missing %d .\n",
			p_bfi->whoami,
			p_port_ext->pdo.cl_ext.vfptr_pnp_po->identity, p_pdo,
			p_port_ext, p_port_ext->pdo.b_present,
			p_port_ext->pdo.b_reported_missing ) );

		/* Cache the CA GUID. */
		p_port_ext->pdo.ca_guid = p_pnp_rec->p_ca_attr->ca_guid;

		/*Only acquire one hca for each port*/
		if(h_ca)
		{
			p_port_ext->pdo.h_ca = h_ca;
			p_port_ext->pdo.hca_acquired = FALSE;
		}else
		{
			/* Acquire CA for the first child pdo*/
			h_ca = p_port_ext->pdo.h_ca = acquire_ca( p_pnp_rec->p_ca_attr->ca_guid );
			p_port_ext->pdo.hca_acquired = TRUE;
		}

		if( !p_port_ext->pdo.h_ca )
		{
			BUS_TRACE_EXIT( BUS_DBG_ERROR, ("acquire_ca failed to find CA.\n") );
			status = IB_INVALID_GUID;
			goto err;
		}

		p_port_ext->port_guid.guid = p_pnp_rec->p_port_attr->port_guid;
		p_port_ext->n_port = p_pnp_rec->p_port_attr->port_num;

		RtlInitUnicodeString(&uniKey, pCurList->io_device_info.pkey);
		status = RtlUnicodeStringToAnsiString(&ansi_str,&uniKey,TRUE);
		if(! NT_SUCCESS(status))
		{
			BUS_TRACE_EXIT( BUS_DBG_ERROR, ("pkey convetion failed, status %#x\n", status) );
			status = IB_INVALID_PKEY;
			goto err;
		}

		if (ansi_str.Buffer[1] == 'x' || ansi_str.Buffer[1] == 'X')
			shift = 2;
		status = RtlStringCchCopyA(p_port_ext->port_guid.name, sizeof(p_port_ext->port_guid.name), ansi_str.Buffer+5+shift);
		if(! NT_SUCCESS(status))
		{
			BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Failed to copy the name, status %#x\n", status) );
			status = IB_INVALID_PKEY;
			goto err;
		}

		ansi_str.Buffer[4+shift] = '\0';
		status = RtlCharToInteger(&ansi_str.Buffer[shift],16,&pKey);
		if(! NT_SUCCESS(status))
		{
			BUS_TRACE(BUS_DBG_ERROR ,
				("Failed to convert pkey, status = 0x%08X, pkey '%s'\n",status,&ansi_str.Buffer[shift]) );
			status = IB_INVALID_PKEY;
			goto err;
		}
		p_port_ext->port_guid.pkey = (ib_net16_t)pKey;
		p_port_ext->port_guid.IsEoib = pCurList->io_device_info.is_eoib;

		RtlFreeAnsiString(&ansi_str);

		p_port_ext->n_ifc_ref = 0;

		BUS_TRACE( BUS_DBG_ERROR, ("Created PDO %p for IPoIB: port_num %d, guid 0x%I64x, pkey 0x%04hx, name '%s'\n", 
			p_pdo, p_port_ext->n_port, p_port_ext->pdo.ca_guid, p_port_ext->port_guid.pkey, p_port_ext->port_guid.name ));

		/* Store the device extension in the port vector for future queries. */
		cl_mutex_acquire( &p_port_mgr->pdo_mutex );
		cl_qlist_insert_tail( &p_port_mgr->port_list,
							  &p_port_ext->pdo.list_item );
		cl_mutex_release( &p_port_mgr->pdo_mutex );

		/*
		 * Set the context of the PNP event. The context is passed in for future
		 * events on the same port.
		 */
		if(p_port_ext->pdo.hca_acquired)
		{
			p_ctx->p_pdo_ext = p_port_ext;
		}

		pCurList = pCurList->next_device_info;

		/* Tell the PnP Manager to rescan for the HCA's bus relations. */
		IoInvalidateDeviceRelations(
			p_port_ext->pdo.h_ca->p_hca_dev, BusRelations );
	}

	/* Invalidate removal relations for the bus driver. */
	IoInvalidateDeviceRelations(
		p_bfi->p_bus_ext->cl_ext.p_pdo, RemovalRelations );

 
    p_cur_conf = bus_globals.p_pkey_conf;
    while(p_cur_conf)
    {
        if(p_cur_conf->pkeys_per_port.port_guid == p_pnp_rec->p_port_attr->port_guid)
        {
            port_mgr_pkey_add(&p_cur_conf->pkeys_per_port);
        }
        p_cur_conf = p_cur_conf->next_conf;
    }
	
    g_post_event("Ibbus: port_mgr_port_add ended\n");

	BUS_EXIT( BUS_DBG_PNP );
	return IB_SUCCESS;
err:	
	BUS_TRACE( BUS_DBG_PNP, ("Deleted device: PDO %p\n", p_pdo));
	IoDeleteDevice( p_pdo);
    g_post_event("Ibbus: port_mgr_port_add failed\n");
	return status;
}



/************************************************************************************
* name	:	port_mgr_pkey_rem
*           removes pdo for each pkey value in pkey_array 
* input	:	g_pkeys
* output:	none
* return:	cl_status
*************************************************************************************/
#pragma prefast(suppress: 28167, "The irql level is restored here")
cl_status_t _port_mgr_pkey_rem(	IN	pkey_array_t	*pkeys,
								IN	port_mgr_t		*p_port_mgr )
{

	uint16_t 			cnt;
	cl_list_item_t		*p_list_item;
	bus_port_ext_t		*p_port_ext;
	bus_pdo_ext_t		*p_pdo_ext = NULL;
	cl_qlist_t*			p_pdo_list = &p_port_mgr->port_list;

	BUS_ENTER( BUS_DBG_PNP );

	p_port_ext = NULL;
	cl_mutex_acquire( &p_port_mgr->pdo_mutex );
	
	/* Count the number of child devices. */
	for( p_list_item = cl_qlist_head( p_pdo_list );
		p_list_item != cl_qlist_end( p_pdo_list );
		p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_pdo_ext = PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );
		p_port_ext = (bus_port_ext_t*)p_pdo_ext;

		if(p_port_ext->port_guid.guid == pkeys->port_guid)
		{
			for(cnt = 0; cnt < pkeys->pkey_num; cnt++)
			{
				if( (p_port_ext->port_guid.pkey == pkeys->pkey_array[cnt]) &&
					!strcmp(p_port_ext->port_guid.name, pkeys->name[cnt]))
				{
					p_port_ext->pdo.b_present = FALSE;
					BUS_TRACE( BUS_DBG_ERROR, ("Removing PDO %p for IPoIB: port_num %d, guid 0x%I64x, pkey 0x%04hx, name '%s'\n", 
						p_pdo_ext, p_port_ext->n_port, p_port_ext->pdo.ca_guid, p_port_ext->port_guid.pkey, p_port_ext->port_guid.name ));
					break;
				}
			}
		}
	}
	cl_mutex_release( &p_port_mgr->pdo_mutex );

	/* Tell the PnP Manager to rescan for the HCA's bus relations. */
	IoInvalidateDeviceRelations(
		p_port_ext->pdo.h_ca->p_hca_dev, BusRelations );

	BUS_EXIT( BUS_DBG_PNP );
	return CL_SUCCESS;
}


cl_status_t port_mgr_pkey_rem( IN pkey_array_t *pkeys )
{
	bus_filter_t	*p_bfi;
	cl_status_t		status;
	boolean_t		GO;
	int				success_cnt=0;

	for(p_bfi=&g_bus_filters[0]; p_bfi < &g_bus_filters[MAX_BUS_FILTERS]; p_bfi++)
	{
		if ( !p_bfi->p_bus_ext )
			continue;
		GO = FALSE;
		lock_control_event();
		if ( p_bfi->ca_guid && p_bfi->p_port_mgr )
			GO = TRUE;
		unlock_control_event();
		if ( GO == FALSE )
			continue;
		status = _port_mgr_pkey_rem( pkeys, p_bfi->p_port_mgr );
		if ( status == CL_SUCCESS )
			success_cnt++;
	}
	return ( success_cnt ? CL_SUCCESS : CL_ERROR );
}

extern child_device_info_t g_default_device_info;
extern child_device_info_t g_eoib_default_device_info;


/************************************************************************************
* name	:	port_mgr_pkey_add
*           creates pdo for each pkey value in pkey_array 
* input	:	g_pkeys
* output:	none
* return:	cl_status
*************************************************************************************/
#pragma prefast(suppress: 28167, "The irql level is restored here")
cl_status_t _port_mgr_pkey_add( IN	pkey_array_t	*req_pkeys,
								IN	bus_filter_t	*p_bfi,
								IN	port_mgr_t		*p_port_mgr )
{
	uint16_t 			cnt;
	NTSTATUS            status;
	cl_list_item_t		*p_list_item;
	bus_port_ext_t		*p_port_ext, *pkey_port_ext, *pmatched_guid_ext;
	DEVICE_OBJECT       *p_pdo[MAX_NUM_PKEY];
	cl_qlist_t*			p_pdo_list = &p_port_mgr->port_list;

	BUS_ENTER( BUS_DBG_PNP );

	pmatched_guid_ext = NULL;
	p_port_ext = NULL;
	cl_mutex_acquire( &p_port_mgr->pdo_mutex );
	
	/* Count the number of child devices. */
	for( p_list_item = cl_qlist_head( p_pdo_list );
		p_list_item != cl_qlist_end( p_pdo_list );
		p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_port_ext = (bus_port_ext_t*)PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );

		if(p_port_ext->port_guid.guid == req_pkeys->port_guid)
		{
			uint16_t i;
			for(i = 0; i < req_pkeys->pkey_num; i++)
			{
				if( (p_port_ext->port_guid.pkey == req_pkeys->pkey_array[i]) &&
					!strcmp(p_port_ext->port_guid.name, req_pkeys->name[i]) )
				{
					/* was removed previously */
					p_port_ext->pdo.b_present = TRUE;
					p_port_ext->pdo.b_reported_missing = FALSE;
					req_pkeys->pkey_array[i] = 0;  
					req_pkeys->name[i][0] = '\0';
				}
			}
			if(!pmatched_guid_ext)
				pmatched_guid_ext = p_port_ext;
		}
	}
	cl_mutex_release( &p_port_mgr->pdo_mutex );

	if (!pmatched_guid_ext)
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("No existed pdo found.\n") );
		return CL_NOT_FOUND;
	}

    for (cnt = 0; cnt < req_pkeys->pkey_num; cnt++)
    {
		if(! (cl_hton16(req_pkeys->pkey_array[cnt]) & IB_PKEY_BASE_MASK) )
			continue;

		/* Create the PDO for the new port device. */
		status = IoCreateDevice( bus_globals.p_driver_obj, sizeof(bus_port_ext_t),
			NULL, FILE_DEVICE_CONTROLLER,
			FILE_DEVICE_SECURE_OPEN | FILE_AUTOGENERATED_DEVICE_NAME,
			FALSE, &p_pdo[cnt] );
		if( !NT_SUCCESS( status ) )
		{
			BUS_TRACE_EXIT( BUS_DBG_ERROR,
				("IoCreateDevice returned %08x.\n", status) );
			return CL_ERROR;
		}
	
		/* Initialize the device extension. */
		cl_init_pnp_po_ext( p_pdo[cnt], NULL, p_pdo[cnt], bus_globals.dbg_lvl,
			&vfptr_port_pnp, &vfptr_port_query_txt );
	
		/* Set the DO_BUS_ENUMERATED_DEVICE flag to mark it as a PDO. */
		p_pdo[cnt]->Flags |= DO_BUS_ENUMERATED_DEVICE;
	
		pkey_port_ext = p_pdo[cnt]->DeviceExtension;
		pkey_port_ext->pdo.dev_po_state.DeviceState = PowerDeviceD0;
		pkey_port_ext->pdo.p_parent_ext = p_bfi->p_bus_ext;
		pkey_port_ext->pdo.b_present = TRUE;
		pkey_port_ext->pdo.b_reported_missing = FALSE;
		pkey_port_ext->pdo.b_hibernating = FALSE;
		pkey_port_ext->pdo.p_po_work_item = NULL;
		if(req_pkeys->port_type[cnt]==PORT_TYPE_IPOIB) {
			pkey_port_ext->pdo.p_pdo_device_info = &g_default_device_info;
			pkey_port_ext->port_guid.IsEoib = FALSE;
		} else {
			ASSERT(req_pkeys->port_type[cnt]==PORT_TYPE_EOIB);
			pkey_port_ext->pdo.p_pdo_device_info = &g_eoib_default_device_info;
			pkey_port_ext->port_guid.IsEoib = TRUE;
		}
		BUS_TRACE( BUS_DBG_PNP, ("Created device for %s: PDO %p,ext %p, present %d, missing %d .\n",
			pkey_port_ext->pdo.cl_ext.vfptr_pnp_po->identity, p_pdo[cnt], pkey_port_ext, pkey_port_ext->pdo.b_present, 
			pkey_port_ext->pdo.b_reported_missing ) );
	
		/* Cache the CA GUID. */
		pkey_port_ext->pdo.ca_guid = pmatched_guid_ext->pdo.ca_guid;
		pkey_port_ext->pdo.h_ca = pmatched_guid_ext->pdo.h_ca;
		pkey_port_ext->port_guid.guid = pmatched_guid_ext->port_guid.guid;
		pkey_port_ext->n_port = pmatched_guid_ext->n_port;
		pkey_port_ext->port_guid.pkey = req_pkeys->pkey_array[cnt];
		pkey_port_ext->port_guid.UniqueId = req_pkeys->UniqueId[cnt];
		pkey_port_ext->port_guid.Location= req_pkeys->Location[cnt];
		RtlStringCchCopyA(pkey_port_ext->port_guid.name, sizeof(pkey_port_ext->port_guid.name), req_pkeys->name[cnt]);

		BUS_TRACE( BUS_DBG_ERROR, ("Created PDO %p for IPoIB: port_num %d, guid 0x%I64x, pkey 0x%04hx, name '%s'\n", 
			p_pdo[cnt], pkey_port_ext->n_port, pkey_port_ext->pdo.ca_guid, pkey_port_ext->port_guid.pkey, pkey_port_ext->port_guid.name ));

		/* Store the device extension in the port vector for future queries. */
		cl_mutex_acquire( &p_port_mgr->pdo_mutex );
		cl_qlist_insert_tail( &p_port_mgr->port_list,
			&pkey_port_ext->pdo.list_item );
		cl_mutex_release( &p_port_mgr->pdo_mutex );
	}

	/* Tell the PnP Manager to rescan for the HCA's bus relations. */
	IoInvalidateDeviceRelations(
		pmatched_guid_ext->pdo.h_ca->p_hca_dev, BusRelations );

	/* Invalidate removal relations for the bus driver. */
	IoInvalidateDeviceRelations(
		p_bfi->p_bus_ext->cl_ext.p_pdo, RemovalRelations );

	BUS_EXIT( BUS_DBG_PNP );
	return CL_SUCCESS;
}

cl_status_t port_mgr_pkey_add(pkey_array_t *pkeys)
{
	bus_filter_t	*p_bfi;
	cl_status_t		status;
	boolean_t		GO;
	int				success_cnt=0;

	for(p_bfi=&g_bus_filters[0]; p_bfi < &g_bus_filters[MAX_BUS_FILTERS]; p_bfi++)
	{
		if ( !p_bfi->p_bus_ext )
			continue;
		GO = FALSE;
		lock_control_event();
		if ( p_bfi->ca_guid && p_bfi->p_port_mgr )
			GO = TRUE;
		unlock_control_event();
		if ( GO == FALSE )
			continue;
		status = _port_mgr_pkey_add( pkeys, p_bfi, p_bfi->p_port_mgr );
		if ( status == CL_SUCCESS )
			success_cnt++;
	}
	return ( success_cnt ? CL_SUCCESS : CL_ERROR );
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
void port_mgr_port_remove(
	IN				ib_pnp_port_rec_t*		p_pnp_rec )
{
	bus_pdo_ext_t	*p_ext;
	port_mgr_t		*p_port_mgr;
	bus_filter_t	*p_bfi;
	port_pnp_ctx_t	*p_ctx = p_pnp_rec->pnp_rec.context;
	cl_list_item_t		*p_list_item;
	bus_port_ext_t		*p_port_ext;
	bus_pdo_ext_t		*p_pdo_ext;
	cl_qlist_t*	   		p_pdo_list;

	BUS_ENTER( BUS_DBG_PNP );

	if ( !p_ctx ) {
		BUS_EXIT( BUS_DBG_PNP );
		return;
	}

	CL_ASSERT( p_ctx->p_bus_filter->magic == BFI_MAGIC );
	p_bfi = p_ctx->p_bus_filter;
	CL_ASSERT( p_bfi );

	BUS_PRINT(BUS_DBG_PNP,("%s ca_guid 0x%I64x port_num %d port_mgr %p\n",
						p_bfi->whoami, p_bfi->ca_guid,
						p_ctx->port_num, p_bfi->p_port_mgr));

	/* in the process of device remove, port_mgr has been destroyed.
	 * cleanup allocated PNP port context; one per port.
	 * The issue is the PNP PORT_REMOVE event occurs after
	 * fdo_release_resources() completes.
	 */
	if ( p_bfi->ca_guid == 0ULL || !p_bfi->p_port_mgr ) {
		cl_free( p_ctx );
		p_pnp_rec->pnp_rec.context = NULL;
		BUS_EXIT( BUS_DBG_PNP );
		return;
	}

	p_port_mgr = p_bfi->p_port_mgr;

	/* Within the PNP record's context is the port extension ptr;
	 * see port_was_hibernated().
	 */
	p_ext = p_ctx->p_pdo_ext;
	CL_ASSERT( p_ext );

	/*
	 * Flag the port PDO as no longer being present.  We have to wait until
	 * the PnP manager removes it to clean up.  However, we do release the
	 * reference on the CA object in order to allow the removal of the HCA
	 * to proceed should it occur before the port's PDO is cleaned up.
	 */
	if ( !p_ext->h_ca )
	{
		BUS_TRACE_EXIT( BUS_DBG_PNP,
						("%s NULL h_ca? p_ext %p\n", p_bfi->whoami, p_ext ) );
		return;
	}

	// Don't crash if p_ext->p_parent_ext is NULL
	CL_ASSERT((p_ext->p_parent_ext == NULL) || p_bfi == p_ext->p_parent_ext->bus_filter);
	
	cl_mutex_acquire( &p_port_mgr->pdo_mutex );
	CL_ASSERT( p_ext->h_ca );

	if( p_ext->b_hibernating )
	{
		BUS_TRACE( BUS_DBG_PNP, ("Skip port removing for %s: PDO %p, ext %p, "
			"present %d, missing %d, hibernating %d .\n",
			p_ext->cl_ext.vfptr_pnp_po->identity, p_ext->cl_ext.p_self_do,
			p_ext, p_ext->b_present, p_ext->b_reported_missing,
			p_ext->b_hibernating ) );
		goto hca_deref;
	}

	p_ext->b_present = FALSE;

	p_pdo_list = &p_port_mgr->port_list;

	for( p_list_item = cl_qlist_head( p_pdo_list );
		 p_list_item != cl_qlist_end( p_pdo_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
		{
		p_pdo_ext = PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );
		p_port_ext =  (bus_port_ext_t*) p_pdo_ext;

		if( (p_port_ext->port_guid.guid == ((bus_port_ext_t*)p_ext)->port_guid.guid) )
			{
			p_pdo_ext->b_present = FALSE;
			BUS_TRACE( BUS_DBG_ERROR, ("Removing PDO %p for IPoIB: port_num %d, guid 0x%I64x, pkey 0x%04hx, name '%s'\n", 
				p_pdo_ext, p_port_ext->n_port, p_port_ext->pdo.ca_guid, p_port_ext->port_guid.pkey, p_port_ext->port_guid.name ));
		}			
	}

	BUS_TRACE( BUS_DBG_PNP,
		("Mark removing %s: PDO %p, ext %p, present %d, missing %d .\n",
		p_ext->cl_ext.vfptr_pnp_po->identity, p_ext->cl_ext.p_self_do, p_ext,
		p_ext->b_present, p_ext->b_reported_missing ) );

	/* Invalidate removal relations for the bus driver. */
	IoInvalidateDeviceRelations(
		p_bfi->p_bus_ext->cl_ext.p_pdo, RemovalRelations );

	/* Invalidate bus relations for the HCA. */
	IoInvalidateDeviceRelations(
		p_ext->h_ca->p_hca_dev, BusRelations );

hca_deref:
	/* Free PNP context memory */
	cl_free( p_ctx );
	p_pnp_rec->pnp_rec.context = NULL;

	deref_al_obj( &p_ext->h_ca->obj );
	
	/* Setting h_ca to be NULL forces IPoIB to start only after re-acquiring
	 * new CA object. The latter happens in __port_was_hibernated or
	 * port_mgr_port_add functions  after arriving IB_PNP_PORT_ADD event
	 * from IBAL.
	 */
	p_ext->h_ca = NULL;

	p_pdo_list = &p_port_mgr->port_list;

	for( p_list_item = cl_qlist_head( p_pdo_list );
		 p_list_item != cl_qlist_end( p_pdo_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
		{
		p_pdo_ext = PARENT_STRUCT( p_list_item, bus_pdo_ext_t, list_item );
		p_port_ext =  (bus_port_ext_t*) p_pdo_ext;

		if( p_port_ext->port_guid.guid == ((bus_port_ext_t*)p_ext)->port_guid.guid )
		{
			p_pdo_ext->h_ca = NULL;
		}			
	}

	cl_mutex_release( &p_port_mgr->pdo_mutex );

	BUS_EXIT( BUS_DBG_PNP );
}


static NTSTATUS
port_start(
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
port_query_remove(
	IN				DEVICE_OBJECT* const	p_dev_obj,
	IN				IRP* const				p_irp,
		OUT			cl_irp_action_t* const	p_action )
{
	bus_port_ext_t	*p_ext;

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

	CL_ASSERT(p_ext->pdo.p_parent_ext->bus_filter);
	BUS_TRACE_EXIT( BUS_DBG_PNP,
			("OK IRP_MN_QUERY_REMOVE_DEVICE:\n  %s HCA guid %I64x port %d\n",
			p_ext->pdo.p_parent_ext->bus_filter->whoami,
			p_ext->pdo.ca_guid, p_ext->n_port ) );

	return STATUS_SUCCESS;
}

#pragma prefast(suppress: 28167, "The irql level is restored here")
static void port_release_resources(
	IN					DEVICE_OBJECT* const	p_dev_obj )
{
	bus_port_ext_t	*p_ext;
	POWER_STATE		po_state;
	port_mgr_t		*p_port_mgr;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;
	p_port_mgr = p_ext->pdo.p_parent_ext->bus_filter->p_port_mgr;

	/* skip releasing resources if PDO has not been yet reported missing */
	if (!p_ext->pdo.b_reported_missing) {
		BUS_TRACE_EXIT( BUS_DBG_PNP, ("PDO is not yet reported missing - skip the removing port from vector: PDO %p, ext %p\n",
			p_dev_obj, p_ext) );
		return;
	}

	/* Remove this PDO from its list. */
	cl_mutex_acquire( &p_port_mgr->pdo_mutex );
	BUS_TRACE( BUS_DBG_PNP, ("Removing port from vector: PDO %p, ext %p\n",
				p_dev_obj, p_ext) );
	cl_qlist_remove_item( &p_port_mgr->port_list, &p_ext->pdo.list_item );
	cl_mutex_release( &p_port_mgr->pdo_mutex );
	po_state.DeviceState = PowerDeviceD3;
	PoSetPowerState( p_ext->pdo.cl_ext.p_pdo, DevicePowerState, po_state );

	BUS_EXIT( BUS_DBG_PNP );
}


static NTSTATUS
port_remove(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp,
		OUT				cl_irp_action_t* const	p_action )
{
	bus_port_ext_t	*p_ext;

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
		BUS_TRACE_EXIT( BUS_DBG_PNP,
						("Device %s not reported missing yet: PDO %p, ext %p\n",
						p_ext->pdo.cl_ext.vfptr_pnp_po->identity,
						p_dev_obj, p_ext) );
		return STATUS_SUCCESS;
	}

	/* Wait for all I/O operations to complete. */
	IoReleaseRemoveLockAndWait( &p_ext->pdo.cl_ext.remove_lock, p_irp );

	/* Release resources if it was not done yet. */
	if( p_ext->pdo.cl_ext.last_pnp_state != SurpriseRemoved )
		p_ext->pdo.cl_ext.vfptr_pnp_po->pfn_release_resources( p_dev_obj );

	p_irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	BUS_TRACE( BUS_DBG_PNP, ("Deleted device %s: PDO %p(=%p), ext %p\n",
		p_ext->pdo.cl_ext.vfptr_pnp_po->identity, p_ext->pdo.cl_ext.p_self_do,
		p_dev_obj, p_ext ) );

	IoDeleteDevice( p_dev_obj );

	*p_action = IrpDoNothing;
	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
port_surprise_remove(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IRP* const					p_irp,
		OUT			cl_irp_action_t* const		p_action )
{
	bus_port_ext_t	*p_ext;

	BUS_ENTER( BUS_DBG_PNP );

	UNUSED_PARAM( p_irp );

	p_ext = p_dev_obj->DeviceExtension;
	//
	// Setting 2 following flags seems like the right behaviour
	// according to DDK, but it causes 
	// WHQL PnP SurpriseRemoval test to fail
	// So, as a work around, they are disabled for now.
	// The best solution is to rewrite all the drivers
	// to WDF model, hoping it will handle right all PnP/Power issues
	//
//	p_ext->pdo.b_present = FALSE;
//	p_ext->pdo.b_reported_missing = TRUE;
	if (!p_ext->pdo.b_reported_missing) {
		// we have not yet reported the device absence 
		cl_rollback_pnp_state( &p_ext->pdo.cl_ext );
	}
	BUS_TRACE( BUS_DBG_PNP, ("%s: PDO %p, ext %p, present %d, missing %d .\n",
		p_ext->pdo.cl_ext.vfptr_pnp_po->identity, p_ext->pdo.cl_ext.p_self_do, 
		p_ext, p_ext->pdo.b_present, p_ext->pdo.b_reported_missing ) );

	*p_action = IrpComplete;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
port_query_capabilities(
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
	p_caps->Removable = FALSE;
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
port_query_target_relations(
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
port_query_device_id(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	WCHAR				*p_string;
	bus_port_ext_t		*p_ext;
	size_t				dev_id_size;
	
	BUS_ENTER( BUS_DBG_PNP );

	
	p_ext = (bus_port_ext_t*)p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	dev_id_size = p_ext->pdo.p_pdo_device_info->device_id_size;

	/* Device ID is "IBA\SID_<sid> where <sid> is the IO device Service ID. */
	p_string = ExAllocatePoolWithTag( NonPagedPool, dev_id_size, 'vedq' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate device ID buffer (%d bytes).\n",
			dev_id_size) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(p_string, dev_id_size);

	cl_memcpy( p_string, p_ext->pdo.p_pdo_device_info->device_id, dev_id_size );

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
port_query_hardware_ids(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	WCHAR				*p_string;
	bus_port_ext_t		*p_ext;
	size_t				dev_id_size;

	BUS_ENTER( BUS_DBG_PNP );


	p_ext = (bus_port_ext_t*)p_dev_obj->DeviceExtension;

	dev_id_size = p_ext->pdo.p_pdo_device_info->hardware_id_size;

	p_string = ExAllocatePoolWithTag( NonPagedPool, dev_id_size, 'ihqp' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate hardware ID buffer (%d bytes).\n",
			dev_id_size) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(p_string, dev_id_size);

	cl_memcpy( p_string, p_ext->pdo.p_pdo_device_info->hardware_id, dev_id_size );

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
port_query_compatible_ids(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	WCHAR				*p_string;
	bus_port_ext_t		*p_ext;
	size_t				dev_id_size;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = (bus_port_ext_t*)p_dev_obj->DeviceExtension;

	dev_id_size = p_ext->pdo.p_pdo_device_info->compatible_id_size;

	p_string = ExAllocatePoolWithTag( NonPagedPool, dev_id_size, 'ihqp' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate hardware ID buffer (%d bytes).\n",
			dev_id_size) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(p_string, dev_id_size);

	cl_memcpy( p_string, p_ext->pdo.p_pdo_device_info->compatible_id, dev_id_size );

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
port_query_unique_id(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	NTSTATUS			status;
	WCHAR				*p_string;
	bus_port_ext_t		*p_ext;
	int					wsize;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;

	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	/* The instance ID is the port GUID. */
	wsize = 2*sizeof(p_ext->port_guid.guid) + 2*sizeof(p_ext->port_guid.pkey) + sizeof(p_ext->port_guid.name) + 3;
	p_string = ExAllocatePoolWithTag( NonPagedPool, sizeof(WCHAR) * wsize, 'iuqp' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate instance ID buffer (%d bytes).\n",
			sizeof(WCHAR) * wsize) );
		return STATUS_NO_MEMORY;
	}

	status = RtlStringCchPrintfW( p_string, wsize, L"%016I64x-%04x-%S",
		p_ext->port_guid.guid,p_ext->port_guid.pkey,p_ext->port_guid.name );
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
port_query_description(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	WCHAR				*p_string;
	bus_port_ext_t		*p_ext;
	
	BUS_ENTER( BUS_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}


	p_string = ExAllocatePoolWithTag( NonPagedPool, p_ext->pdo.p_pdo_device_info->description_size, 'edqp' );

	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate device description buffer (%d bytes).\n",
			p_ext->pdo.p_pdo_device_info->description_size) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(p_string,p_ext->pdo.p_pdo_device_info->description_size);

	cl_memcpy( p_string, p_ext->pdo.p_pdo_device_info->description, p_ext->pdo.p_pdo_device_info->description_size );

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
port_query_location(
	IN					DEVICE_OBJECT* const	p_dev_obj,
		OUT				IRP* const				p_irp )
{
	WCHAR				*p_string;
	bus_port_ext_t		*p_ext;
	size_t				size;
	ULONG				len;
	NTSTATUS			status;
	DEVICE_OBJECT		*p_hca_dev;

	BUS_ENTER( BUS_DBG_PNP );

	p_ext = p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	if (!p_ext->pdo.h_ca)
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("HCA is still or already not acquired !\n") );
		return STATUS_DEVICE_NOT_READY;
	}
		
	p_hca_dev = p_ext->pdo.h_ca->p_hca_dev;

	/* Get the length of the HCA's location. */
	status = IoGetDeviceProperty( p_hca_dev,
		DevicePropertyLocationInformation, 0, NULL, &len );
	if( status != STATUS_BUFFER_TOO_SMALL )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("IoGetDeviceProperty for device location size returned %08x.\n",
			status) );
		return status;
	}

	/*
	 * Allocate the string buffer to hold the HCA's location along with the
	 * port number.  The port number is 32-bits, so in decimal it can be at
	 * most 10 characters.
	 */
	size = len + sizeof(L", port ") + (sizeof(WCHAR) * 10);
	if( size > (USHORT)-1 )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Length beyond limits.\n") );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	p_string = ExAllocatePoolWithTag( NonPagedPool, size, 'olqp' );
	if( !p_string )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate device location buffer (%d bytes).\n", len) );
		return STATUS_NO_MEMORY;
	}

	/* Get the HCA's location information. */
	status = IoGetDeviceProperty( p_hca_dev,
		DevicePropertyLocationInformation, len, p_string, &len );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_string );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("IoGetDeviceProperty for device location returned %08x.\n",
			status) );
		return status;
	}

	/* Append the port number to the HCA's location. */
	status = RtlStringCbPrintfW( p_string + (len/2) - 1, size - len + 1,
		L", port %d", p_ext->n_port );
	if( !NT_SUCCESS( status ) )
	{
		CL_ASSERT( NT_SUCCESS( status ) );
		ExFreePool( p_string );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("RtlStringCbPrintfW returned %08x.\n", status) );
		return status;
	}

	p_irp->IoStatus.Information = (ULONG_PTR)p_string;

	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
port_query_bus_info(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp,
		OUT				cl_irp_action_t* const	p_action )
{
	PNP_BUS_INFORMATION	*p_bus_info;
	NTSTATUS status;
	bus_port_ext_t		*p_ext;
	DEVICE_OBJECT		*p_hca_dev;
	ULONG BusNumber;
	ULONG len;

	BUS_ENTER( BUS_DBG_PNP );

	*p_action = IrpComplete;

	p_ext = p_dev_obj->DeviceExtension;
	if( !p_ext->pdo.b_present )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("Device not present.\n") );
		return STATUS_NO_SUCH_DEVICE;
	}

	if (!p_ext->pdo.h_ca)
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("HCA is still or already not acquired !\n") );
		return STATUS_DEVICE_NOT_READY;
	}
		
	p_hca_dev = p_ext->pdo.h_ca->p_hca_dev;

	p_bus_info = ExAllocatePoolWithTag( NonPagedPool, sizeof(PNP_BUS_INFORMATION), 'ibqp' );
	if( !p_bus_info )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to allocate PNP_BUS_INFORMATION (%d bytes).\n",
			sizeof(PNP_BUS_INFORMATION)) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	p_bus_info->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
	//TODO: Memory from Intel - storage miniport would not stay loaded unless
	//TODO: bus type was PCI.  Look here if SRP is having problems staying
	//TODO: loaded.
	p_bus_info->LegacyBusType = Internal;

	len = sizeof(ULONG);

	status = IoGetDeviceProperty( p_hca_dev, DevicePropertyBusNumber , sizeof(ULONG), &BusNumber, &len );
	if( !NT_SUCCESS( status ) )
	{
		ExFreePool( p_bus_info );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("IoGetDeviceProperty for bus query information returned %08x.\n",
			status) );
		return status;
	}

	p_bus_info->BusNumber = BusNumber;

	p_irp->IoStatus.Information = (ULONG_PTR)p_bus_info;
	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}


static NTSTATUS
port_query_ipoib_ifc(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IO_STACK_LOCATION* const	p_io_stack )
{
	NTSTATUS				status;
	ib_al_ifc_t				*p_ifc;
	ib_al_ifc_data_t		*p_ifc_data;
	ipoib_ifc_data_t		*p_ipoib_data;
	bus_port_ext_t			*p_ext;
	const GUID				*p_guid;

	BUS_ENTER( BUS_DBG_PNP );

	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

	p_ext = p_dev_obj->DeviceExtension;

	BUS_TRACE( BUS_DBG_PNP, ("Query i/f for %s: PDO %p (=%p),ext %p, present %d, missing %d, hibernated %d .\n",
		p_ext->pdo.cl_ext.vfptr_pnp_po->identity, p_ext->pdo.cl_ext.p_self_do, 
		p_dev_obj, p_ext, p_ext->pdo.b_present, p_ext->pdo.b_reported_missing,
		p_ext->pdo.b_hibernating ) );

	/* Get the interface. */
	status = cl_fwd_query_ifc(
		p_ext->pdo.p_parent_ext->cl_ext.p_self_do, p_io_stack );
	if( !NT_SUCCESS( status ) )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Failed to forward interface query: %08X\n", status) );
		return status;
	}

	p_ifc = (ib_al_ifc_t*)p_io_stack->Parameters.QueryInterface.Interface;

	p_ifc_data = (ib_al_ifc_data_t*)
		p_io_stack->Parameters.QueryInterface.InterfaceSpecificData;

	if ( !p_ifc_data )
		goto exit;
	
	p_guid = p_ifc_data->type;
	if( !IsEqualGUID( p_guid, &GUID_IPOIB_INTERFACE_DATA ) )
	{
		BUS_TRACE_EXIT( BUS_DBG_PNP, ("Unsupported interface data: \n\t"
			"0x%08x, 0x%04x, 0x%04x, 0x%02x, 0x%02x, 0x%02x,"
			"0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x.\n",
			p_guid->Data1, p_guid->Data2, p_guid->Data3,
			p_guid->Data4[0], p_guid->Data4[1], p_guid->Data4[2],
			p_guid->Data4[3], p_guid->Data4[4], p_guid->Data4[5],
			p_guid->Data4[6], p_guid->Data4[7]) );
		return STATUS_NOT_SUPPORTED;
	}

	if( p_ifc_data->version != IPOIB_INTERFACE_DATA_VERSION )
	{
		p_ifc->wdm.InterfaceDereference( p_ifc->wdm.Context );
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
			("Unsupported version %d, expected %d\n",
			p_ifc_data->version, IPOIB_INTERFACE_DATA_VERSION) );
		return STATUS_NOT_SUPPORTED;
	}

	ASSERT( p_ifc_data->p_data );

	if( p_ifc_data->size != sizeof(ipoib_ifc_data_t) )
	{
		p_ifc->wdm.InterfaceDereference( p_ifc->wdm.Context );
		BUS_TRACE_EXIT( BUS_DBG_PNP,
			("Buffer too small (%d given, %d required).\n",
			p_ifc_data->size,
			sizeof(ipoib_ifc_data_t)) );
		return STATUS_BUFFER_TOO_SMALL;
	}

	/* Set the interface data. */
	p_ipoib_data = (ipoib_ifc_data_t*)p_ifc_data->p_data;

    p_ipoib_data->driver_id = p_ext->pdo.h_ca->obj.p_ci_ca->verbs.driver_id;
	p_ipoib_data->ca_guid = p_ext->pdo.h_ca->obj.p_ci_ca->verbs.guid;
	p_ipoib_data->port_guid = p_ext->port_guid;
	p_ipoib_data->port_num = (uint8_t)p_ext->n_port;
	p_ipoib_data->ibal_ipoib_ifc.UpdateCheckForHang = FipUpdateCheckForHang;
	p_ipoib_data->ibal_ipoib_ifc.RegisterEoibNotification = FipRegisterEoibNotification;
	p_ipoib_data->ibal_ipoib_ifc.RemoveEoibNotification = FipRemoveEoibNotification;
	p_ipoib_data->ibal_ipoib_ifc.GetVhubTableUpdate = FipGetVhubTableUpdate;
	p_ipoib_data->ibal_ipoib_ifc.ReturnVhubTableUpdate = FipReturnVhubTableUpdate;
	p_ipoib_data->ibal_ipoib_ifc.GetLinkStatus = FipGetLinkStatus;
	p_ipoib_data->ibal_ipoib_ifc.AcquireDataQpn = FipAcquireDataQpn;
	p_ipoib_data->ibal_ipoib_ifc.ReleaseDataQpn = FipReleaseDataQpn;
	p_ipoib_data->ibal_ipoib_ifc.GetBroadcastMgidParams = FipGetBroadcastMgidParams;
	p_ipoib_data->ibal_ipoib_ifc.GetEoIBMac = FipGetMac;

exit:
	BUS_EXIT( BUS_DBG_PNP );
	return STATUS_SUCCESS;
}

#pragma alloc_text( PAGED , port_query_interface )
static NTSTATUS
port_query_interface(
	IN					DEVICE_OBJECT* const	p_dev_obj,
	IN					IRP* const				p_irp,
		OUT				cl_irp_action_t* const	p_action )
{
	bus_pdo_ext_t		*p_ext;
	NTSTATUS			status;
	IO_STACK_LOCATION	*p_io_stack;

	BUS_ENTER( BUS_DBG_PNP );

	PAGED_CODE();

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	/* Bottom of the stack - IRP must be completed. */
	*p_action = IrpComplete;

	/* Compare requested GUID with our supported interface GUIDs. */
	if( IsEqualGUID( p_io_stack->Parameters.QueryInterface.InterfaceType,
		&GUID_IB_AL_INTERFACE ) )
	{
		status = port_query_ipoib_ifc( p_dev_obj, p_io_stack );
	}
	else 
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

    //
    // Verifier assertion - if not supported, do not change status.
    //
    if( status == STATUS_NOT_SUPPORTED )
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
port_set_power(
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
	return status;
}



