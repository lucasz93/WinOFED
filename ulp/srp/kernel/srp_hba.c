/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corp.  All rights reserved.
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




#include "srp_hba.h"
#include "srp_data.h"
#include "srp_data_path.h"
#include "srp_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srp_hba.tmh"
#endif
#include "srp_session.h"

#include <complib/cl_byteswap.h>
#include <complib/cl_bus_ifc.h>
#include <initguid.h>
#include <iba/ioc_ifc.h>


static void
__srp_destroying_hba(
	IN				cl_obj_t					*p_obj );

static void
__srp_cleanup_hba(
	IN				cl_obj_t					*p_obj );

static void
__srp_free_hba(
	IN				cl_obj_t					*p_obj );

static ib_api_status_t
__srp_pnp_cb(
	IN				ib_pnp_rec_t				*p_pnp_rec );

void
__srp_dump_ioc_info( const ib_ioc_info_t *p_ioc_info )
{
	UNUSED_PARAM( p_ioc_info );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("Dumping IOC Info\n") );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tchassis_guid\t= 0x%I64x\n",
		cl_ntoh64( p_ioc_info->chassis_guid )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tchassis_slot\t= %d\n",
		p_ioc_info->chassis_slot) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tiou_guid\t= 0x%I64x\n",
		cl_ntoh64( p_ioc_info->iou_guid )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tiou_slot\t= %d\n",
		p_ioc_info->iou_slot) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP, ("\n") );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("Dumping IOC Info Profile\n") );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tioc_guid\t= 0x%I64x\n",
		cl_ntoh64( p_ioc_info->profile.ioc_guid )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tvend_id\t= 0x%x\n",
		cl_ntoh32( p_ioc_info->profile.vend_id )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tdev_id\t= 0x%x\n",
		cl_ntoh32( p_ioc_info->profile.dev_id )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tdev_ver\t= 0x%x\n",
		cl_ntoh16( p_ioc_info->profile.dev_ver )) );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tsubsys_vend_id\t= 0x%x\n",
		cl_ntoh32( p_ioc_info->profile.subsys_vend_id )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tsubsys_id\t= 0x%x\n",
		cl_ntoh32( p_ioc_info->profile.subsys_id )) );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tio_class\t= 0x%x\n",
		cl_ntoh16( p_ioc_info->profile.io_class )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tio_subclass\t= 0x%x\n",
		cl_ntoh16( p_ioc_info->profile.io_subclass )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tprotocol\t= 0x%x\n",
		cl_ntoh16( p_ioc_info->profile.protocol )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tprotocol_ver\t= 0x%x\n",
		cl_ntoh16( p_ioc_info->profile.protocol_ver )) );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tsend_msg_depth\t= %d\n",
		cl_ntoh16( p_ioc_info->profile.send_msg_depth )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\trdma_read_depth\t= %d\n",
		p_ioc_info->profile.rdma_read_depth) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tsend_msg_size\t= %d\n",
		cl_ntoh32( p_ioc_info->profile.send_msg_size )) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\trdma_size\t = %d\n",
		cl_ntoh32( p_ioc_info->profile.rdma_size )) );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tctrl_ops_cap\t= 0x%X\n",
		p_ioc_info->profile.ctrl_ops_cap) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tnum_svc_entries\t= 0x%X\n",
		p_ioc_info->profile.num_svc_entries) );
	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("\tid_string\t= %s\n",
		p_ioc_info->profile.id_string) );
}


static boolean_t
__get_ioc_ifc(
	IN				srp_hba_t* const			p_hba )
{
	NTSTATUS			status;
	ib_al_ifc_data_t	data;
	IO_STACK_LOCATION	io_stack;

	SRP_ENTER( SRP_DBG_PNP );

	/* Query for our interface. */
	data.size = sizeof(ioc_ifc_data_t);
	data.version = IOC_INTERFACE_DATA_VERSION;
	data.type = &GUID_IOC_INTERFACE_DATA;
	data.p_data = &p_hba->info;

	io_stack.MinorFunction = IRP_MN_QUERY_INTERFACE;
	io_stack.Parameters.QueryInterface.Version = AL_INTERFACE_VERSION;
	io_stack.Parameters.QueryInterface.Size = sizeof(ib_al_ifc_t);
	io_stack.Parameters.QueryInterface.Interface = (INTERFACE*)&p_hba->ifc;
	io_stack.Parameters.QueryInterface.InterfaceSpecificData = &data;
	io_stack.Parameters.QueryInterface.InterfaceType = &GUID_IB_AL_INTERFACE;

	status = cl_fwd_query_ifc( gp_self_do, &io_stack );
	if( !NT_SUCCESS( status ) )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Query interface for IOU parameters returned %08x.\n", status) );
		return FALSE;
	}
	else
	{
		/*
		 * Dereference the interface now so that the bus driver doesn't fail a
		 * query remove IRP.  We will always get unloaded before the bus driver
		 * since we're a child device.
		 */
		p_hba->ifc.wdm.InterfaceDereference( p_hba->ifc.wdm.Context );
		SRP_EXIT( SRP_DBG_PNP );
		return TRUE;
	}
}


ib_api_status_t
srp_hba_create(
	IN				cl_obj_t* const				p_drv_obj,
		OUT			srp_ext_t* const			p_ext )
{
	srp_hba_t			*p_hba;
	cl_status_t			cl_status;
	ib_api_status_t		ib_status;
	ib_pnp_req_t		pnp_req;
	uint32_t			i;

	SRP_ENTER( SRP_DBG_PNP );

	p_hba = (srp_hba_t*)cl_zalloc( sizeof(srp_hba_t) );
	if( !p_hba )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Failed to allocate srp_hba_t structure.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	cl_qlist_init( &p_hba->path_record_list );
	cl_spinlock_init( &p_hba->path_record_list_lock );

	/* Store instance parameters. */
	p_hba->p_ext = p_ext;
	p_hba->max_sg = 0xFFFFFFFF;
	p_hba->max_srb_ext_sz = 0xFFFFFFFF;

	if( !__get_ioc_ifc( p_hba ) )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("__get_ioc_ifc failed.\n") );
		return IB_ERROR;
	}

	for ( i = 0; i < SRP_MAX_SERVICE_ENTRIES; i++ )
	{
		p_hba->session_list[i] = NULL;
	}

	cl_obj_construct( &p_hba->obj, SRP_OBJ_TYPE_HBA );
	cl_status = cl_obj_init( &p_hba->obj, CL_DESTROY_ASYNC,
		__srp_destroying_hba, __srp_cleanup_hba, __srp_free_hba );
	if( cl_status != CL_SUCCESS )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("cl_obj_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	ib_status = p_hba->ifc.open_al( &p_hba->h_al );
	if( ib_status != IB_SUCCESS )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("ib_open_al returned %s\n", p_hba->ifc.get_err_str( ib_status )) );
		goto err;
	}

	/* Register for IOC events */
	pnp_req.pfn_pnp_cb = __srp_pnp_cb;
	pnp_req.pnp_class = IB_PNP_IOC | IB_PNP_FLAG_REG_SYNC;
	pnp_req.pnp_context = p_hba;
	/* Reference the HBA object before registering for PnP notifications. */
	cl_obj_ref( &p_hba->obj );

	cl_obj_insert_rel( &p_hba->rel, p_drv_obj, &p_hba->obj );

	ib_status = p_hba->ifc.reg_pnp( p_hba->h_al, &pnp_req, &p_hba->h_pnp );
	if( ib_status != IB_SUCCESS )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("ib_reg_pnp returned %s\n", p_hba->ifc.get_err_str( ib_status )) );
		goto err;
	}
	ib_status = IB_ERROR;
	for ( i = 0; i < p_hba->ioc_info.profile.num_svc_entries; i++ )
	{
		if ( p_hba->session_list[i] != NULL )
			ib_status = IB_SUCCESS;
	}
	
	if( ib_status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Session Connection Failure.\n") );

err:
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
			("HBA Object ref_cnt = %d\n", p_hba->obj.ref_cnt) );
		cl_obj_destroy( &p_hba->obj );

		return ib_status;
	}

	/*
	 * Add the HBA to the driver object's child list.  This will cause
	 * everything to clean up properly in case we miss an unload notification.
	 */
	p_ext->p_hba = p_hba;

	SRP_EXIT( SRP_DBG_PNP );
	return ib_status;
}


static void
__srp_destroying_hba(
	IN              cl_obj_t                    *p_obj )
{
	srp_hba_t       *p_hba;

	SRP_ENTER( SRP_DBG_PNP );

	p_hba = PARENT_STRUCT( p_obj, srp_hba_t, obj );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("Before dereg pnp HBA Object ref_cnt = %d\n", p_hba->obj.ref_cnt) );

	if( p_hba->h_pnp )
	{
		p_hba->ifc.dereg_pnp( p_hba->h_pnp, cl_obj_deref );
	}

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("After dereg pnp HBA Object ref_cnt = %d\n", p_hba->obj.ref_cnt) );

	SRP_EXIT( SRP_DBG_PNP );
}

static void
__srp_remove_path_records(
	IN              srp_hba_t                   *p_hba )
{
	srp_path_record_t   *p_srp_path_record;

	SRP_ENTER( SRP_DBG_PNP );

	cl_spinlock_acquire( &p_hba->path_record_list_lock );
	p_srp_path_record = (srp_path_record_t *)cl_qlist_remove_head( &p_hba->path_record_list );

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
		("Removing any remaining path records.\n") );

	while ( p_srp_path_record != (srp_path_record_t *)cl_qlist_end( &p_hba->path_record_list ) )
	{
		cl_free( p_srp_path_record );
		p_srp_path_record = (srp_path_record_t *)cl_qlist_remove_head( &p_hba->path_record_list );
	}

	cl_spinlock_release( &p_hba->path_record_list_lock );

	SRP_EXIT( SRP_DBG_PNP );
}

static void
__srp_cleanup_hba(
	IN              cl_obj_t                    *p_obj )
{
	srp_hba_t   *p_hba;

	SRP_ENTER( SRP_DBG_PNP );

	p_hba = PARENT_STRUCT( p_obj, srp_hba_t, obj );

	if( p_hba->h_al )
		p_hba->ifc.close_al( p_hba->h_al );

	__srp_remove_path_records( p_hba );

	cl_spinlock_destroy( &p_hba->path_record_list_lock );

	if ( p_hba->p_svc_entries )
		cl_free( p_hba->p_svc_entries );

	SRP_EXIT( SRP_DBG_PNP );
}


static void
__srp_free_hba(
	IN              cl_obj_t                    *p_obj )
{
	srp_hba_t       *p_hba;

	SRP_ENTER( SRP_DBG_PNP );

	p_hba = PARENT_STRUCT( p_obj, srp_hba_t, obj );

	cl_obj_deinit( p_obj );
	cl_free( p_hba );

	SRP_EXIT( SRP_DBG_PNP );
}

static BOOLEAN
__srp_validate_ioc(
	IN  ib_pnp_ioc_rec_t    *p_ioc_rec )
{
	SRP_ENTER( SRP_DBG_PNP );

	// Is this really an SRP device?
	if ( ( p_ioc_rec->info.profile.io_class != SRP_IO_CLASS &&
		   p_ioc_rec->info.profile.io_class != SRP_IO_CLASS_R10 ) ||
		 p_ioc_rec->info.profile.io_subclass != SRP_IO_SUBCLASS )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
						("Not an SRP CLASS(0x%x)/SUBCLASS(0x%x).\n",
						cl_ntoh16( p_ioc_rec->info.profile.io_class ),
						cl_ntoh16( p_ioc_rec->info.profile.io_subclass )) );
#if defined(SRP_IO_SUBCLASS_SUN)
		if ( p_ioc_rec->info.profile.io_subclass == SRP_IO_SUBCLASS_SUN )
			__srp_dump_ioc_info( &p_ioc_rec->info );
		if ( p_ioc_rec->info.profile.io_subclass != SRP_IO_SUBCLASS_SUN )
#endif
		return FALSE;
	}

	// Does it have the required features?
	if ( cl_ntoh16( p_ioc_rec->info.profile.protocol )     != SRP_PROTOCOL     ||
		 cl_ntoh16( p_ioc_rec->info.profile.protocol_ver ) != SRP_PROTOCOL_VER ||
		 !(p_ioc_rec->info.profile.ctrl_ops_cap & CTRL_OPS_CAP_ST) ||
		 !(p_ioc_rec->info.profile.ctrl_ops_cap & CTRL_OPS_CAP_SF) ||
		 !(p_ioc_rec->info.profile.ctrl_ops_cap & CTRL_OPS_CAP_RF) ||
		 !(p_ioc_rec->info.profile.ctrl_ops_cap & CTRL_OPS_CAP_WF) )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Not an SRP PROTOCOL/PROTOCOL_VER.\n") );
#if defined(SRP_IO_SUBCLASS_SUN)
		if ( p_ioc_rec->info.profile.io_subclass != SRP_IO_SUBCLASS_SUN )
#endif
		return FALSE;
	}

	// Can it handle our IO requirements?
	if ( cl_ntoh32( p_ioc_rec->info.profile.send_msg_size )  <  SRP_MIN_TGT_TO_INI_IU ||
		 cl_ntoh16( p_ioc_rec->info.profile.send_msg_depth ) == 0 ||
		 cl_ntoh32( p_ioc_rec->info.profile.rdma_size )      <  SRP_MIN_TGT_TO_INI_DMA )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Device Not Capable.\n") );
		return FALSE;
	}

	SRP_EXIT( SRP_DBG_PNP );

	return TRUE;
}

static BOOLEAN
__srp_path_rec_equal(
	IN	const ib_path_rec_t	*p_path_rec_1,
	IN	const ib_path_rec_t	*p_path_rec_2,
	IN	BOOLEAN				check_num_path,
	IN	BOOLEAN				check_preference )
{
	SRP_ENTER( SRP_DBG_PNP );

	if ( p_path_rec_1->dgid.unicast.prefix != p_path_rec_2->dgid.unicast.prefix )
		return ( FALSE );

	if ( p_path_rec_1->dgid.unicast.interface_id != p_path_rec_2->dgid.unicast.interface_id )
		return ( FALSE );

	if ( p_path_rec_1->sgid.unicast.prefix != p_path_rec_2->sgid.unicast.prefix )
		return ( FALSE );

	if ( p_path_rec_1->sgid.unicast.interface_id != p_path_rec_2->sgid.unicast.interface_id )
		return ( FALSE );

	if ( p_path_rec_1->dlid != p_path_rec_2->dlid )
		return ( FALSE );

	if ( p_path_rec_1->slid != p_path_rec_2->slid )
		return ( FALSE );

	if ( p_path_rec_1->hop_flow_raw != p_path_rec_2->hop_flow_raw )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("hop_flow_raw.val does not match.\n") );
		return ( FALSE );
	}

	if ( p_path_rec_1->tclass != p_path_rec_2->tclass )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("tclass does not match.\n") );
		return ( FALSE );
	}

	if ( p_path_rec_1->num_path != p_path_rec_2->num_path )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("num_path does not match.\n") );
		if ( check_num_path == TRUE )
		{
			return ( FALSE );
		}
	}

	if ( p_path_rec_1->pkey != p_path_rec_2->pkey )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("pkey does not match.\n") );
		return ( FALSE );
	}

	if ( ib_path_rec_sl(p_path_rec_1) != ib_path_rec_sl(p_path_rec_2) )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("sl does not match.\n") );
		return ( FALSE );
	}

	if ( p_path_rec_1->mtu != p_path_rec_2->mtu )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("mtu does not match.\n") );
		return ( FALSE );
	}

	if ( p_path_rec_1->rate != p_path_rec_2->rate )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("rate does not match.\n") );
		return ( FALSE );
	}

	if ( p_path_rec_1->pkt_life != p_path_rec_2->pkt_life )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("pkt_life does not match.\n") );
		return ( FALSE );
	}

	if ( p_path_rec_1->preference != p_path_rec_2->preference )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("preference does not match.\n") );
		if ( check_preference == TRUE )
		{
			return ( FALSE );
		}
	}

#if defined( _DEBUG_ )

	if ( cl_memcmp( p_path_rec_1, p_path_rec_2, sizeof( ib_path_rec_t ) ) != 0 )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("p_path_rec_1 does not match p_path_rec_2.\n") );
	}
	else
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("p_path_rec_1 matches p_path_rec_2.\n") );
	}

#endif

	SRP_EXIT( SRP_DBG_PNP );

	return ( TRUE );
}

static
srp_path_record_t*
__srp_find_path(
	IN  srp_hba_t               *p_hba,
	IN  const ib_path_rec_t     *p_path_rec,
	IN  BOOLEAN                 check_num_path,
	IN  BOOLEAN                 check_preference )
{
	srp_path_record_t   *p_srp_path_record;

	SRP_ENTER( SRP_DBG_PNP );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
		("Finding path record (slid:0x%x dlid:0x%x) for %s.\n",
		cl_ntoh16(p_path_rec->slid),
		cl_ntoh16(p_path_rec->dlid),
		p_hba->ioc_info.profile.id_string) );

	cl_spinlock_acquire( &p_hba->path_record_list_lock );

	p_srp_path_record = (srp_path_record_t *)cl_qlist_head( &p_hba->path_record_list );

	while ( p_srp_path_record != (srp_path_record_t *)cl_qlist_end( &p_hba->path_record_list ) )
	{
		if ( __srp_path_rec_equal( (const ib_path_rec_t *)&p_srp_path_record->path_rec,
									p_path_rec,
									check_num_path,
									check_preference ) == TRUE )
		{
			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
				("Found path record (slid:0x%x dlid:0x%x) for %s.\n",
				cl_ntoh16(p_path_rec->slid),
				cl_ntoh16(p_path_rec->dlid),
				p_hba->ioc_info.profile.id_string) );
			break;
		}

		p_srp_path_record = (srp_path_record_t *)cl_qlist_next( &p_srp_path_record->list_item );
	}

	if ( p_srp_path_record == (srp_path_record_t *)cl_qlist_end( &p_hba->path_record_list ) )
	{
		p_srp_path_record = NULL;
	}

	cl_spinlock_release( &p_hba->path_record_list_lock );

	SRP_EXIT( SRP_DBG_PNP );

	return p_srp_path_record;
}

static
srp_path_record_t*
__srp_remove_path(
	IN  srp_hba_t               *p_hba,
	IN  const ib_path_rec_t     *p_path_rec )
{
	srp_path_record_t   *p_srp_path_record;

	SRP_ENTER( SRP_DBG_PNP );

	p_srp_path_record = __srp_find_path( p_hba, p_path_rec, TRUE, TRUE );
	if ( p_srp_path_record != NULL )
	{
		cl_spinlock_acquire( &p_hba->path_record_list_lock );

		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
			("Removing path record (slid:0x%x dlid:0x%x) for %s.\n",
			cl_ntoh16(p_path_rec->slid),
			cl_ntoh16(p_path_rec->dlid),
			p_hba->ioc_info.profile.id_string) );

		cl_qlist_remove_item( &p_hba->path_record_list, &p_srp_path_record->list_item );

		cl_spinlock_release( &p_hba->path_record_list_lock );
	}

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("Current Path count for %s = %d \n",
		 p_hba->ioc_info.profile.id_string,
		 (int)cl_qlist_count( &p_hba->path_record_list )) );

	SRP_EXIT( SRP_DBG_PNP );

	return p_srp_path_record;
}

static
srp_path_record_t*
__srp_add_path(
	IN  srp_hba_t               *p_hba,
	IN  const ib_path_rec_t     *p_path_rec )
{
	srp_path_record_t   *p_srp_path_record;

	SRP_ENTER( SRP_DBG_PNP );

	p_srp_path_record = __srp_find_path( p_hba, p_path_rec, FALSE, FALSE );
	if ( p_srp_path_record != NULL )
	{
		cl_spinlock_acquire( &p_hba->path_record_list_lock );
		p_srp_path_record->path_rec = *p_path_rec;
		cl_spinlock_release( &p_hba->path_record_list_lock );

		SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_PNP,
			("Discarding/Updating duplicate path record (slid:0x%x dlid:0x%x) for %s.\n",
			cl_ntoh16(p_path_rec->slid),
			cl_ntoh16(p_path_rec->dlid),
			p_hba->ioc_info.profile.id_string) );

		goto exit;
	}

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
		("Adding path record (slid:0x%x dlid:0x%x) for %s.\n",
		cl_ntoh16(p_path_rec->slid),
		cl_ntoh16(p_path_rec->dlid),
		p_hba->ioc_info.profile.id_string) );


	p_srp_path_record = cl_zalloc( sizeof( srp_path_record_t ) );
	if ( p_srp_path_record == NULL )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Insufficient Memory.\n") );
	}
	else
	{
		p_srp_path_record->path_rec = *p_path_rec;

		cl_spinlock_acquire( &p_hba->path_record_list_lock );
		cl_qlist_insert_tail( &p_hba->path_record_list, &p_srp_path_record->list_item );
		cl_spinlock_release( &p_hba->path_record_list_lock );
	}

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("Current Path count for %s = %d \n",
		 p_hba->ioc_info.profile.id_string,
		 (int)cl_qlist_count( &p_hba->path_record_list )) );

exit:
	SRP_EXIT( SRP_DBG_PNP );

	return p_srp_path_record;
}

static ib_api_status_t
__srp_connect_sessions(
	IN OUT srp_hba_t    *p_hba )
{
	uint32_t        i;
	srp_session_t   *p_session;
	ib_api_status_t status = IB_ERROR;
	BOOLEAN any_ioc_connected = FALSE;

	SRP_ENTER( SRP_DBG_PNP );

	/* Create the session(s). */
	for ( i = 0; i < p_hba->ioc_info.profile.num_svc_entries; i++ )
	{
		int     retry_count = 0;

		do{
			retry_count++;

			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
			("Creating New Session For Service Entry Index %d.\n", i ));

			p_session = srp_new_session(
							p_hba, 
							&p_hba->p_svc_entries[i],
							&p_hba->p_srp_path_record->path_rec,
							&status );
			if( p_session == NULL )
			{
				status = IB_INSUFFICIENT_MEMORY;
				break;
			}

			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
			("New Session For Service Entry Index %d Created.\n", i ));

			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("Attempting to connect %s. Svc Idx %d; Connection Attempt Count = %d.\n", 
				 p_hba->ioc_info.profile.id_string, i,
				 retry_count) );
			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
				("Logging Into Session.\n"));
			status = srp_session_login( p_session );
			if ( status == IB_SUCCESS )
			{
				any_ioc_connected = TRUE;

				srp_session_adjust_params( p_session );

				cl_obj_lock( &p_hba->obj );
				p_session->target_id = (UCHAR)i;
				p_hba->session_list[i] = p_session;
				cl_obj_unlock( &p_hba->obj );

				SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
					("Session Login Issued Successfully.\n"));
			}
			else
			{
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_PNP,
					("Session Login for Service Idx %d Failure Status = %d.\n", i, status));
				cl_obj_destroy( &p_session->obj );
			}

		} while ( (status != IB_SUCCESS) && (retry_count < 3) );

	}

	if ( any_ioc_connected == TRUE )
	{
		status = IB_SUCCESS;
		for( i = 0; i < p_hba->ioc_info.profile.num_svc_entries; i++ )
		{
			p_session = p_hba->session_list[i];
			
			if( p_session != NULL &&
				p_session->connection.state == SRP_CONNECTED && 
				p_hba->session_paused[i] == TRUE )
			{
				SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("Resuming Adapter Session %d for %s.\n", i,
						p_hba->ioc_info.profile.id_string) );

				p_hba->session_paused[i] = FALSE;
				StorPortDeviceReady( p_hba->p_ext, SP_UNTAGGED, (UCHAR)i, SP_UNTAGGED );
			}
		}
	}

	SRP_EXIT( SRP_DBG_PNP );

	return status;
}

void
srp_disconnect_sessions(
	IN				srp_hba_t					*p_hba )
{
	uint32_t		i;
	srp_session_t	*p_session;

	SRP_ENTER( SRP_DBG_PNP );

	cl_obj_lock( &p_hba->obj );

	for ( i = 0; i < p_hba->ioc_info.profile.num_svc_entries; i++ )
	{
		if ( p_hba->session_list[i] != NULL )
		{
			p_session = p_hba->session_list[i];
			p_hba->session_list[i] = NULL;
			p_session->connection.state = SRP_CONNECT_FAILURE;
			srp_session_failed( p_session );
		}
	}

	cl_obj_unlock( &p_hba->obj );

	SRP_EXIT( SRP_DBG_PNP );
}

static ib_api_status_t
__srp_connect_path(
	IN  srp_hba_t   *p_hba )
{
	ib_api_status_t     status = IB_ERROR;
	srp_path_record_t   *p_srp_path_record;

	SRP_ENTER( SRP_DBG_PNP );

	while ( g_srp_system_shutdown == FALSE )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
			("Searching for path to %s.\n",
			 p_hba->ioc_info.profile.id_string) );

		cl_spinlock_acquire( &p_hba->path_record_list_lock );
		p_srp_path_record = (srp_path_record_t *)cl_qlist_head( &p_hba->path_record_list );
		cl_spinlock_release( &p_hba->path_record_list_lock );
		if ( p_srp_path_record == (srp_path_record_t *)cl_qlist_end( &p_hba->path_record_list ) )
		{
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("No paths to %s found.\n",
				p_hba->ioc_info.profile.id_string) );
			break;
		}

		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
			("Connecting path to %s.\n",
			p_hba->ioc_info.profile.id_string) );

		p_hba->p_srp_path_record = p_srp_path_record;
		status = __srp_connect_sessions( p_hba );
		if ( status == IB_SUCCESS )
		{
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("Path to %s has connected.\n",
				p_hba->ioc_info.profile.id_string) );
			break;
		}

		p_hba->p_srp_path_record = NULL;
		cl_spinlock_acquire( &p_hba->path_record_list_lock );
		cl_qlist_remove_item( &p_hba->path_record_list, &p_srp_path_record->list_item );
		cl_spinlock_release( &p_hba->path_record_list_lock );
		cl_free( p_srp_path_record );
	}

	SRP_EXIT( SRP_DBG_PNP );

	return status;
}

static ib_api_status_t
__srp_pnp_cb(
	IN              ib_pnp_rec_t                *p_pnp_rec )
{
	ib_api_status_t			status = IB_SUCCESS;
	ib_pnp_ioc_rec_t		*p_ioc_rec;
	ib_pnp_ioc_path_rec_t	*p_ioc_path;
	srp_hba_t				*p_hba;
	srp_path_record_t		*p_srp_path_record;
	
	SRP_ENTER( SRP_DBG_PNP );

	p_hba = (srp_hba_t*)p_pnp_rec->pnp_context;
	p_ioc_rec = (ib_pnp_ioc_rec_t*)p_pnp_rec;
	p_ioc_path = (ib_pnp_ioc_path_rec_t*)p_pnp_rec;

	SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );


	switch( p_pnp_rec->pnp_event )
	{
		case IB_PNP_IOC_ADD:
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("IB_PNP_IOC_ADD for %s.\n",
				p_ioc_rec->info.profile.id_string) );

			__srp_dump_ioc_info( &p_ioc_rec->info );

			/*
			 * Trap our CA GUID so we filter path notifications
			 * for our bound CA only.
			 */
			if( p_ioc_rec->ca_guid != p_hba->info.ca_guid )
			{
				SRP_PRINT_EXIT( TRACE_LEVEL_WARNING, SRP_DBG_PNP,
					("Ignoring CA GUID.\n") );
				status = IB_INVALID_GUID;
				break;
			}

			/* Trap our IOC GUID so we can get path notification events. */
			if( p_ioc_rec->info.profile.ioc_guid != p_hba->info.guid )
			{
				SRP_PRINT_EXIT( TRACE_LEVEL_WARNING, SRP_DBG_PNP,
					("Ignoring GUID.\n") );
				status = IB_INVALID_GUID;
				break;
			}

			if ( __srp_validate_ioc( p_ioc_rec ) == FALSE )
			{
				status = IB_INVALID_GUID;
				break;
			}

			p_hba->ioc_info = p_ioc_rec->info;
			p_hba->p_svc_entries = cl_zalloc( sizeof(ib_svc_entry_t) * p_hba->ioc_info.profile.num_svc_entries );
			if ( p_hba->p_svc_entries == NULL )
			{
				SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("Insufficient Memory.\n") );
				status = IB_INSUFFICIENT_MEMORY;
				break;
			}

			cl_memcpy ( p_hba->p_svc_entries,
						p_ioc_rec->svc_entry_array,
						sizeof(ib_svc_entry_t) * p_hba->ioc_info.profile.num_svc_entries);

			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
				("Found %d Service Entries.\n",
				p_hba->ioc_info.profile.num_svc_entries));
			break;

		case IB_PNP_IOC_REMOVE:
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("IB_PNP_IOC_REMOVE for %s.\n",
				p_hba->ioc_info.profile.id_string) );

			CL_ASSERT( p_pnp_rec->guid == p_hba->info.guid );

			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("Hey!!! Our IOC went away.\n") );
			
			if( !p_hba->adapter_stopped )
				p_hba->adapter_stopped = TRUE;
			
			srp_disconnect_sessions( p_hba );
			__srp_remove_path_records( p_hba );

			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
				("IB_PNP_IOC_REMOVE HBA Object ref_cnt = %d\n", p_hba->obj.ref_cnt ) );

			break;

		case IB_PNP_IOC_PATH_ADD:
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("IB_PNP_IOC_PATH_ADD (slid:0x%x dlid:0x%x) for %s.\n",
				cl_ntoh16(p_ioc_path->path.slid),
				cl_ntoh16(p_ioc_path->path.dlid),
				p_hba->ioc_info.profile.id_string));

			p_srp_path_record = __srp_add_path( p_hba, &p_ioc_path->path );
			if ( p_srp_path_record == NULL )
			{
				status = IB_INSUFFICIENT_MEMORY;
				break;
			}

			if ( p_hba->p_srp_path_record == NULL )
			{
				SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
					("Connecting new path to %s.\n",
					p_hba->ioc_info.profile.id_string) );
				status = __srp_connect_path( p_hba );
			}

			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
				(" IOC_PATH ADD HBA Object ref_cnt = %d\n", p_hba->obj.ref_cnt ) );
			break;

		case IB_PNP_IOC_PATH_REMOVE:
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_PNP,
				("IB_PNP_IOC_PATH_REMOVE (slid:%x dlid:%x) for %s.\n",
				cl_ntoh16(p_ioc_path->path.slid),
				cl_ntoh16(p_ioc_path->path.dlid),
				p_hba->ioc_info.profile.id_string));

			p_srp_path_record = __srp_remove_path( p_hba, &p_ioc_path->path );
			if ( p_srp_path_record != NULL )
			{
				if ( p_srp_path_record == p_hba->p_srp_path_record )
				{
					SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_PNP,
						("Current path to %s has been lost.\n",
						 p_hba->ioc_info.profile.id_string) );

					if ( g_srp_system_shutdown == FALSE )
					{
						srp_disconnect_sessions( p_hba );
					}
				}

				cl_free( p_srp_path_record );
			}
			break;

		default:
			CL_ASSERT( p_pnp_rec->pnp_event == IB_PNP_IOC_ADD ||
				p_pnp_rec->pnp_event == IB_PNP_IOC_REMOVE ||
				p_pnp_rec->pnp_event == IB_PNP_IOC_PATH_ADD ||
				p_pnp_rec->pnp_event == IB_PNP_IOC_PATH_REMOVE );
			break;
	}

	SRP_EXIT( SRP_DBG_PNP );
	return status;
}
