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


#include <iba/ib_al.h>
#include "al_pnp.h"
#include "al_ioc_pnp.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_ioc_pnp.tmh"
#endif
#include "ib_common.h"
#include "al_mgr.h"
#include "al_ca.h"
#include <complib/cl_timer.h>
#include <complib/cl_qpool.h>
#include <complib/cl_qmap.h>
#include <complib/cl_fleximap.h>
#include <complib/cl_math.h>


/* Basic sweep operation flow:
 *
 * NOTE: Empty lines indicate asynchronous decoupling.
 *	1. Timer expires
 *	2. Issue SA query for all CA nodes
 *	3. Issue SA query for all paths
 *
 *	4. Query callback for first query - store results.
 *	5. Query callback for second query - process results.
 *	6. Associate paths to nodes.
 *	7. For each node, use the first path to send a IOU Info query.
 *
 *	8a. Recv callback (success) - record IOU info, decrement ref count.
 *	8b. Recv callback (failure) - decrement ref count.
 *	8c. Send failure - decrement ref count.
 *	8d. Send timeout - pick next path and repeate IOU info query.
 *	9. Queue results to async proc thread once ref count hits zero
 *
 *	10. Discard any nodes that failed IOU info query, or reported no IOCs.
 *	11. For each node scanned that is already known, compare change ID
 *	12a. Change ID identical - report any path changes.
 *	12b. Change ID different - for each active IOC slot, query IOC profile.
 *
 *	13a. Recv callback (success) - associate IOC with IOU, decrement ref count.
 *	13b. Recv callback (failure) - decrement ref count.
 *	13c. Send failure - decrement ref count.
 *	14. Queue results to async proc thread once ref count hits zero.
 *
 *	15. Discard any nodes that have no IOCs.
 *	16. For each IOC of each node, query all service entries.
 *
 *	17a. Recv callback (success) - copy service entries, decrement ref count.
 *	17b. Recv callback (failure) - Remove IOC from IOU, decrement ref count.
 *	17c. Send failure - Remove IOC from IOU, decrement ref count.
 *	18. Queue results to async proc thread once ref count hits zero.
 *
 *	19. Discard any nodes that have no IOCs.
 *	20. Compare new node map to known nodes and report changes.
 *	21. Compare IOCs for any duplicates and report changes.
 *	22. Compare paths for any duplicates and report changes.
 *	23. Reset sweep timer.
 *
 * Note: the sweep timer is reset at any point where there can be no further
 * progress towards.
 */


/* Number of entries in the various pools to grow by. */
#define IOC_PNP_POOL_GROW	(10)

#define IOC_RESWEEPS 3
#define IOC_RESWEEP_WAIT (10 * 1000) // time to wait between resweeps in milliseconds

/* IOC PnP Manager structure. */
typedef struct _ioc_pnp_mgr
{
	al_obj_t				obj;

	cl_qlist_t				iou_reg_list;
	cl_qlist_t				ioc_reg_list;

	ib_pnp_handle_t			h_pnp;

	cl_async_proc_item_t	async_item;
	boolean_t				async_item_is_busy;

	cl_spinlock_t			iou_pool_lock;
	cl_qpool_t				iou_pool;
	cl_spinlock_t			ioc_pool_lock;
	cl_qpool_t				ioc_pool;
	cl_spinlock_t			path_pool_lock;
	cl_qpool_t				path_pool;

	cl_fmap_t				iou_map;	/* Map of currently known IOUs */
	cl_fmap_t				sweep_map;	/* Map of IOUs from sweep results. */
	cl_timer_t				sweep_timer;/* Timer to trigger sweep. */
	atomic32_t				query_cnt;	/* Number of sweep results outstanding. */
	atomic32_t				reSweep;	/* Number of IOC resweeps per batch. */

}	ioc_pnp_mgr_t;


/* Per-port IOC PnP agent. */
typedef struct _ioc_pnp_svc
{
	al_obj_t				obj;

	net64_t					ca_guid;
	net64_t					port_guid;

	ib_qp_handle_t			h_qp;
	ib_pool_key_t			pool_key;
	ib_mad_svc_handle_t		h_mad_svc;

	atomic32_t				query_cnt;
	ib_query_handle_t		h_node_query;
	ib_query_handle_t		h_path_query;
	ib_mad_element_t		*p_node_element;
	ib_mad_element_t		*p_path_element;
	uint32_t				num_nodes;
	uint32_t				num_paths;

}	ioc_pnp_svc_t;


/****d* Access Layer:IOC PnP/iou_path_t
* NAME
*	iou_path_t
*
* DESCRIPTION
*	Describes a path to an IOU node.
*
* SYNOPSIS
*/
typedef struct _iou_path
{
	cl_fmap_item_t			map_item;
	net64_t					ca_guid;
	net64_t					port_guid;
	ib_path_rec_t			rec;

}	iou_path_t;
/*
* FIELDS
*	map_item
*		Map item for storing paths in a map.
*
*	path_rec
*		Path record.
*
* SEE ALSO
*	IOC PnP
*********/


/****d* Access Layer:IOC PnP/iou_node_t
* NAME
*	iou_node_t
*
* DESCRIPTION
*	Describes an IOU node on the fabric.
*
* SYNOPSIS
*/
typedef struct _iou_node
{
	cl_fmap_item_t			map_item;
	cl_fmap_t				path_map;
	cl_qmap_t				ioc_map;
	cl_spinlock_t			lock;

	iou_path_t				*p_config_path;

	net64_t					ca_guid;
	net64_t					guid;
	net64_t					chassis_guid;
	uint8_t					slot;
	net32_t					vend_id;
	net16_t					dev_id;
	net32_t					revision;
	ib_iou_info_t			info;

	char					desc[IB_NODE_DESCRIPTION_SIZE + 1];

}	iou_node_t;
/*
* FIELDS
*	map_item
*		Map item for storing IOUs in a map.
*
*	path_map
*		Map of paths to the IOU.
*
*	ioc_map
*		Map of IOCs on the IOU.
*
*	p_config_path
*		Path used to get configuration information from the IOU.
*
*	ca_guid
*		CA GUID through which the IOU is accessible.
*
*	guid
*		Node GUID used as key when storing IOUs in the map.
*
*	chassis_guid
*		GUID of the chassis in which the IOU is installed.
*
*	slot
*		Slot number in the chassis in which the IOU is installed.
*
*	vend_id
*		Vendor ID of the IOU.
*
*	dev_id
*		Device ID of the IOU.
*
*	revision
*		Device revision of the IOU.
*
*	info
*		I/O unit info structure.
*
*	desc
*		Node description as provided in ib_node_record_t, along with space for
*		terminating NULL.
*
* NOTES
*	The guid member must follow the ca_guid member to allow both guids to
*	be compared in single call to cl_memcmp.
*
* SEE ALSO
*	IOC PnP
*********/


#pragma warning(disable:4324)
typedef struct _iou_ioc
{
	cl_map_item_t			map_item;
	iou_node_t				*p_iou;
	uint8_t					slot;
	ib_ioc_profile_t		profile;
	uint8_t					num_valid_entries;
	ib_svc_entry_t			*p_svc_entries;
	atomic32_t				ref_cnt;

}	iou_ioc_t;
#pragma warning(default:4324)


typedef enum _sweep_state
{
	SWEEP_IOU_INFO,
	SWEEP_IOC_PROFILE,
	SWEEP_SVC_ENTRIES,
	SWEEP_COMPLETE

}	sweep_state_t;


typedef struct _ioc_sweep_results
{
	cl_async_proc_item_t	async_item;
	sweep_state_t			state;
	ioc_pnp_svc_t			*p_svc;
	cl_fmap_t				iou_map;

}	ioc_sweep_results_t;


typedef struct _al_pnp_ioc_event
{
	size_t					rec_size;
	ib_pnp_rec_t			*p_rec;
	ib_pnp_rec_t			*p_user_rec;

}	al_pnp_ioc_event_t;


/* Global instance of the IOC PnP manager. */
ioc_pnp_mgr_t	*gp_ioc_pnp = NULL;
uint32_t		g_ioc_query_timeout = 250;
uint32_t		g_ioc_query_retries = 4;
uint32_t		g_ioc_poll_interval = 1;
					/* 0 == no IOC polling
					 * 1 == IOC poll on demand (IB_PNP_SM_CHANGE, IB_PNP_PORT_ACTIVE,
					 *			QUERY_DEVICE_RELATIONS for device 'IB Bus')
					 * > 1 == poll interval in millisecond units.
					 */


/******************************************************************************
*
* IOC PnP Manager functions - global object.
*
******************************************************************************/
static void
__construct_ioc_pnp(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr );

static ib_api_status_t
__init_ioc_pnp(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr );

static void
__destroying_ioc_pnp(
	IN				al_obj_t					*p_obj );

static void
__free_ioc_pnp(
	IN				al_obj_t					*p_obj );

static ib_api_status_t
__ioc_pnp_cb(
	IN				ib_pnp_rec_t				*p_pnp_rec );

static cl_status_t
__init_iou(
	IN				void* const					p_obj,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item );

/******************************************************************************
*
* IOC PnP manager sweep-related functions.
*
******************************************************************************/
static iou_node_t*
__get_iou(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN		const	net64_t						ca_guid,
	IN		const	ib_node_record_t* const		p_node_rec );

static void
__put_iou(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				iou_node_t* const			p_iou );

static void
__put_iou_map(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				cl_fmap_t* const			p_iou_map );

static iou_path_t*
__get_path(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN		const	net64_t						ca_guid,
	IN		const	net64_t						port_guid,
	IN		const	ib_path_rec_t* const		p_path_rec );

static void
__put_path(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				iou_path_t* const			p_path );

static void
__put_path_map(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				cl_fmap_t* const			p_path_map );

static iou_ioc_t*
__get_ioc(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN		const	uint32_t					ioc_slot,
	IN		const	ib_ioc_profile_t* const		p_profile );

static void
__put_ioc(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				iou_ioc_t* const			p_ioc );

static void
__put_ioc_map(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				cl_qmap_t* const			p_ioc_map );

static int
__iou_cmp(
	IN		const	void* const					p_key1,
	IN		const	void* const					p_key2 );

static int
__path_cmp(
	IN		const	void* const					p_key1,
	IN		const	void* const					p_key2 );

static void
__ioc_pnp_timer_cb(
	IN				void						*context );

static void
__ioc_async_cb(
	IN				cl_async_proc_item_t		*p_async_item );

/******************************************************************************
*
* IOC PnP service - per local port child of IOC PnP manager.
*
******************************************************************************/
static ib_api_status_t
__create_ioc_pnp_svc(
	IN				ib_pnp_rec_t				*p_pnp_rec );

static ib_api_status_t
__init_ioc_pnp_svc(
	IN				ioc_pnp_svc_t* const		p_ioc_pnp_svc,
	IN		const	ib_pnp_rec_t* const			p_pnp_rec );

static void
__destroying_ioc_pnp_svc(
	IN				al_obj_t					*p_obj );

static void
__free_ioc_pnp_svc(
	IN				al_obj_t					*p_obj );

/******************************************************************************
*
* IOC PnP service sweep functions.
*
******************************************************************************/
static void
__ioc_pnp_recv_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_request_mad );

static void
__ioc_pnp_send_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_response );

static void
__node_rec_cb(
	IN				ib_query_rec_t				*p_query_rec );

static void
__path_rec_cb(
	IN				ib_query_rec_t				*p_query_rec );

static void
__process_sweep(
	IN				cl_async_proc_item_t		*p_async_item );

static void
__process_query(
	IN				ioc_pnp_svc_t* const		p_svc );

static void
__process_nodes(
	IN				ioc_pnp_svc_t* const		p_svc,
	IN				cl_qmap_t* const			p_iou_map );

static void
__process_paths(
	IN				ioc_pnp_svc_t* const		p_svc,
	IN				cl_qmap_t* const			p_iou_map );

static void
__build_iou_map(
	IN				cl_qmap_t* const			p_port_map,
	IN	OUT			cl_fmap_t* const			p_iou_map );

static void
__format_dm_get(
	IN		const	void* const					context1,
	IN		const	void* const					context2,
	IN		const	iou_path_t* const			p_path,
	IN		const	net16_t						attr_id,
	IN		const	net32_t						attr_mod,
	IN	OUT			ib_mad_element_t* const		p_mad_element );

static ib_api_status_t
__ioc_query_sa(
	IN				ioc_pnp_svc_t* const		p_svc );

static ib_api_status_t
__query_ious(
	IN				ioc_sweep_results_t* const	p_results );

static ib_api_status_t
__query_ioc_profiles(
	IN				ioc_sweep_results_t* const	p_results );

static ib_api_status_t
__query_svc_entries(
	IN				ioc_sweep_results_t* const	p_results );

static void
__update_results(
	IN				ioc_sweep_results_t* const	p_results );

static void
__iou_info_resp(
	IN	OUT			iou_node_t* const			p_iou,
	IN		const	ib_dm_mad_t* const			p_mad );

static void
__ioc_profile_resp(
	IN	OUT			iou_node_t* const			p_iou,
	IN		const	ib_dm_mad_t* const			p_mad );

static void
__svc_entry_resp(
	IN	OUT			iou_ioc_t* const			p_ioc,
	IN		const	ib_dm_mad_t* const			p_mad );

/******************************************************************************
*
* Client registration and notification management
*
******************************************************************************/
static void
__change_ious(
	IN				cl_fmap_t* const			p_cur_ious,
	IN				cl_fmap_t* const			p_dup_ious );

static void
__add_ious(
	IN				cl_fmap_t* const			p_cur_ious,
	IN				cl_fmap_t* const			p_new_ious,
	IN				al_pnp_t* const				p_reg OPTIONAL );

static void
__remove_ious(
	IN				cl_fmap_t* const			p_old_ious );

static void
__add_iocs(
	IN				iou_node_t* const			p_iou,
	IN				cl_qmap_t* const			p_new_iocs,
	IN				al_pnp_t* const				p_reg OPTIONAL );

static void
__remove_iocs(
	IN				iou_node_t* const			p_iou,
	IN				cl_qmap_t* const			p_old_iocs );

static void
__add_paths(
	IN				iou_node_t* const			p_iou,
	IN				cl_qmap_t* const			p_ioc_map,
	IN				cl_fmap_t* const			p_new_paths,
	IN				al_pnp_t* const				p_reg OPTIONAL );

static void
__add_ioc_paths(
	IN				iou_ioc_t* const			p_ioc,
	IN				cl_fmap_t* const			p_new_paths,
	IN				al_pnp_t* const				p_reg OPTIONAL );

static void
__remove_paths(
	IN				cl_qmap_t* const			p_ioc_map,
	IN				cl_fmap_t* const			p_old_paths );

static void
__report_iou_add(
	IN				iou_node_t* const			p_iou,
	IN				al_pnp_t* const				p_reg OPTIONAL );

static void
__report_iou_remove(
	IN				iou_node_t* const			p_iou );

static void
__report_ioc_add(
	IN				iou_node_t* const			p_iou,
	IN				iou_ioc_t* const			p_ioc,
	IN				al_pnp_t* const				p_reg OPTIONAL );

static void
__report_ioc_remove(
	IN				iou_node_t* const			p_iou,
	IN				iou_ioc_t* const			p_ioc );

static void
__report_path(
	IN				iou_ioc_t* const			p_ioc,
	IN				iou_path_t* const			p_path,
	IN				ib_pnp_event_t				pnp_event,
	IN				al_pnp_t* const				p_reg OPTIONAL );


/******************************************************************************
*
* Implementation
*
******************************************************************************/
ib_api_status_t
create_ioc_pnp(
	IN				al_obj_t* const				p_parent_obj )
{
	ib_api_status_t		status;
	ib_pnp_req_t		pnp_req;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( !gp_ioc_pnp );

	gp_ioc_pnp = (ioc_pnp_mgr_t*)cl_zalloc( sizeof(ioc_pnp_mgr_t) );
	if( !gp_ioc_pnp )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to allocate IOC PnP manager.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	__construct_ioc_pnp( gp_ioc_pnp );

	status = __init_ioc_pnp( gp_ioc_pnp );
	if( status != IB_SUCCESS )
	{
		__free_ioc_pnp( &gp_ioc_pnp->obj );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("__construct_ioc_pnp returned %s\n", ib_get_err_str( status )) );
		return status;
	}

	/* Attach to the parent object. */
	status = attach_al_obj( p_parent_obj, &gp_ioc_pnp->obj );
	if( status != IB_SUCCESS )
	{
		gp_ioc_pnp->obj.pfn_destroy( &gp_ioc_pnp->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Register for port PnP notifications. */
	cl_memclr( &pnp_req, sizeof(pnp_req) );
	pnp_req.pnp_class = IB_PNP_PORT;
	pnp_req.pnp_context = gp_ioc_pnp;
	pnp_req.pfn_pnp_cb = __ioc_pnp_cb;
	status = ib_reg_pnp( gh_al, &pnp_req, &gp_ioc_pnp->h_pnp );
	if( status != IB_SUCCESS )
	{
		gp_ioc_pnp->obj.pfn_destroy( &gp_ioc_pnp->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_reg_pnp failed with status %s.\n",
			ib_get_err_str( status )) );
		return status;
	}
	/*
	 * We don't release the reference taken in init_al_obj
	 * since PnP deregistration is asynchronous.
	 */
	
	AL_EXIT( AL_DBG_PNP );
	return IB_SUCCESS;
}


static void
__construct_ioc_pnp(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr )
{
	AL_ENTER( AL_DBG_PNP );

	cl_qlist_init( &p_ioc_mgr->iou_reg_list );
	cl_qlist_init( &p_ioc_mgr->ioc_reg_list );
	cl_fmap_init( &p_ioc_mgr->iou_map, __iou_cmp );
	construct_al_obj( &p_ioc_mgr->obj, AL_OBJ_TYPE_IOC_PNP_MGR );
	cl_spinlock_construct( &p_ioc_mgr->iou_pool_lock );
	cl_spinlock_construct( &p_ioc_mgr->path_pool_lock );
	cl_spinlock_construct( &p_ioc_mgr->ioc_pool_lock );
	cl_qpool_construct( &p_ioc_mgr->iou_pool );
	cl_qpool_construct( &p_ioc_mgr->path_pool );
	cl_qpool_construct( &p_ioc_mgr->ioc_pool );
	cl_fmap_init( &p_ioc_mgr->sweep_map, __iou_cmp );
	cl_timer_construct( &p_ioc_mgr->sweep_timer );
	p_ioc_mgr->async_item.pfn_callback = __ioc_async_cb;

	AL_EXIT( AL_DBG_PNP );
}


static ib_api_status_t
__init_ioc_pnp(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;

	AL_ENTER( AL_DBG_PNP );

	/* Initialize the pool locks. */
	cl_status = cl_spinlock_init( &p_ioc_mgr->iou_pool_lock );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	cl_status = cl_spinlock_init( &p_ioc_mgr->path_pool_lock );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	cl_status = cl_spinlock_init( &p_ioc_mgr->ioc_pool_lock );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	/* Initialize the pools */
	cl_status = cl_qpool_init( &p_ioc_mgr->iou_pool, 0, 0, IOC_PNP_POOL_GROW,
		sizeof(iou_node_t), __init_iou, NULL, p_ioc_mgr );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_qpool_init returned %#x\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	cl_status = cl_qpool_init( &p_ioc_mgr->path_pool, 0, 0, IOC_PNP_POOL_GROW,
		sizeof(iou_path_t), NULL, NULL, NULL );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_qpool_init returned %#x\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	cl_status = cl_qpool_init( &p_ioc_mgr->ioc_pool, 0, 0, IOC_PNP_POOL_GROW,
		sizeof(iou_ioc_t), NULL, NULL, NULL );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_qpool_init returned %#x\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	/* Initialize the sweep timer. */
	cl_status = cl_timer_init( &p_ioc_mgr->sweep_timer,
		__ioc_pnp_timer_cb, p_ioc_mgr );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_timer_init failed with %#x\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}
	if ( g_ioc_poll_interval == 1 )
		p_ioc_mgr->reSweep = 0;

	status = init_al_obj( &p_ioc_mgr->obj, p_ioc_mgr, TRUE,
		__destroying_ioc_pnp, NULL, __free_ioc_pnp );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj returned %s\n", ib_get_err_str( status )) );
		return status;
	}

	AL_EXIT( AL_DBG_PNP );
	return IB_SUCCESS;
}


static void
__destroying_ioc_pnp(
	IN				al_obj_t					*p_obj )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_PNP );

	UNUSED_PARAM( p_obj );
	CL_ASSERT( &gp_ioc_pnp->obj == p_obj );

	/* Stop the timer. */
	cl_timer_stop( &gp_ioc_pnp->sweep_timer );
	gp_ioc_pnp->reSweep = 0;

	if( gp_ioc_pnp->h_pnp )
	{
		status = ib_dereg_pnp( gp_ioc_pnp->h_pnp,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}

	AL_EXIT( AL_DBG_PNP );
}


static void
__free_ioc_pnp(
	IN				al_obj_t					*p_obj )
{
	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( &gp_ioc_pnp->obj == p_obj );

	/*
	 * Return all items from the maps to their pools before
	 * destroying the pools
	 */
	__put_iou_map( gp_ioc_pnp, &gp_ioc_pnp->iou_map );
	cl_timer_destroy( &gp_ioc_pnp->sweep_timer );
	cl_qpool_destroy( &gp_ioc_pnp->ioc_pool );
	cl_qpool_destroy( &gp_ioc_pnp->path_pool );
	cl_qpool_destroy( &gp_ioc_pnp->iou_pool );
	cl_spinlock_destroy( &gp_ioc_pnp->ioc_pool_lock );
	cl_spinlock_destroy( &gp_ioc_pnp->path_pool_lock );
	cl_spinlock_destroy( &gp_ioc_pnp->iou_pool_lock );
	destroy_al_obj( p_obj );
	cl_free( gp_ioc_pnp );
	gp_ioc_pnp = NULL;

	AL_EXIT( AL_DBG_PNP );
}


static cl_status_t
__init_iou(
	IN				void* const					p_obj,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	iou_node_t	*p_iou;

	UNUSED_PARAM( context );

	p_iou = (iou_node_t*)p_obj;
	
	cl_spinlock_construct( &p_iou->lock );
	cl_qmap_init( &p_iou->ioc_map );
	cl_fmap_init( &p_iou->path_map, __path_cmp );

	*pp_pool_item = &p_iou->map_item.pool_item;
	return cl_spinlock_init( &p_iou->lock );
}


static iou_node_t*
__get_iou(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN		const	net64_t						ca_guid,
	IN		const	ib_node_record_t* const		p_node_rec )
{
	iou_node_t		*p_iou;
	cl_pool_item_t	*p_item;

	cl_spinlock_acquire( &p_ioc_mgr->iou_pool_lock );
	p_item = cl_qpool_get( &p_ioc_mgr->iou_pool );
	cl_spinlock_release( &p_ioc_mgr->iou_pool_lock );
	if( !p_item )
		return NULL;

	p_iou = PARENT_STRUCT( PARENT_STRUCT( p_item, cl_map_item_t, pool_item ),
		iou_node_t, map_item );

	p_iou->ca_guid = ca_guid;
	p_iou->guid = p_node_rec->node_info.node_guid;
	p_iou->chassis_guid = p_node_rec->node_info.sys_guid;
	p_iou->vend_id = ib_node_info_get_vendor_id( &p_node_rec->node_info );
	p_iou->dev_id = p_node_rec->node_info.device_id;
	p_iou->revision = p_node_rec->node_info.revision;

	cl_memclr( &p_iou->info, sizeof(ib_iou_info_t) );

	cl_memcpy( p_iou->desc, p_node_rec->node_desc.description,
		IB_NODE_DESCRIPTION_SIZE );

	/* The terminating NULL should never get overwritten. */
	CL_ASSERT( p_iou->desc[IB_NODE_DESCRIPTION_SIZE] == '\0' );

	return p_iou;
}


static void
__put_iou(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				iou_node_t* const			p_iou )
{
	__put_path_map( p_ioc_mgr, &p_iou->path_map );
	__put_ioc_map( p_ioc_mgr, &p_iou->ioc_map );

	cl_spinlock_acquire( &p_ioc_mgr->iou_pool_lock );
	cl_qpool_put( &p_ioc_mgr->iou_pool, &p_iou->map_item.pool_item );
	cl_spinlock_release( &p_ioc_mgr->iou_pool_lock );
}


static void
__put_iou_map(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				cl_fmap_t* const			p_iou_map )
{
	cl_qlist_t		list;
	cl_fmap_item_t	*p_item;
	iou_node_t		*p_iou;

	cl_qlist_init( &list );

	p_item = cl_fmap_head( p_iou_map );
	while( p_item != cl_fmap_end( p_iou_map ) )
	{
		cl_fmap_remove_item( p_iou_map, p_item );

		p_iou = PARENT_STRUCT(
			PARENT_STRUCT( p_item, cl_map_item_t, pool_item ),
			iou_node_t, map_item );

		__put_path_map( p_ioc_mgr, &p_iou->path_map );
		__put_ioc_map( p_ioc_mgr, &p_iou->ioc_map );
		cl_qlist_insert_head( &list, &p_item->pool_item.list_item );
		p_item = cl_fmap_head( p_iou_map );
	}
	cl_spinlock_acquire( &p_ioc_mgr->iou_pool_lock );
	cl_qpool_put_list( &p_ioc_mgr->iou_pool, &list );
	cl_spinlock_release( &p_ioc_mgr->iou_pool_lock );
}


static iou_path_t*
__get_path(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN		const	net64_t						ca_guid,
	IN		const	net64_t						port_guid,
	IN		const	ib_path_rec_t* const		p_path_rec )
{
	cl_pool_item_t	*p_item;
	iou_path_t		*p_path;

	cl_spinlock_acquire( &p_ioc_mgr->path_pool_lock );
	p_item = cl_qpool_get( &p_ioc_mgr->path_pool );
	cl_spinlock_release( &p_ioc_mgr->path_pool_lock );
	if( !p_item )
		return NULL;

	p_path = PARENT_STRUCT( PARENT_STRUCT( p_item, cl_fmap_item_t, pool_item ),
		iou_path_t, map_item );

	/*
	 * Store the local CA and port GUID for this path to let recipients
	 * of a PATH_ADD event avoid a CA lookup based on GID.
	 */
	p_path->ca_guid = ca_guid;
	p_path->port_guid = port_guid;

	p_path->rec = *p_path_rec;
	/* Clear the num_path field since it is just "undefined". */
	p_path->rec.num_path = 0;
	/*
	 * Clear reserved fields in case they were set to prevent undue path
	 * thrashing.
	 */
	memset(p_path->rec.resv2, 0, sizeof(p_path->rec.resv2));

	return p_path;
}


static void
__put_path(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				iou_path_t* const			p_path )
{
	cl_spinlock_acquire( &p_ioc_mgr->path_pool_lock );
	cl_qpool_put( &p_ioc_mgr->path_pool, &p_path->map_item.pool_item );
	cl_spinlock_release( &p_ioc_mgr->path_pool_lock );
}


static void
__put_path_map(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				cl_fmap_t* const			p_path_map )
{
	cl_qlist_t		list;
	cl_fmap_item_t	*p_item;
	iou_path_t		*p_path;

	cl_qlist_init( &list );

	p_item = cl_fmap_head( p_path_map );
	while( p_item != cl_fmap_end( p_path_map ) )
	{
		cl_fmap_remove_item( p_path_map, p_item );

		p_path = PARENT_STRUCT( PARENT_STRUCT( p_item, cl_fmap_item_t, pool_item ),
			iou_path_t, map_item );

		cl_qlist_insert_head( &list, &p_item->pool_item.list_item );
		p_item = cl_fmap_head( p_path_map );
	}
	cl_spinlock_acquire( &p_ioc_mgr->path_pool_lock );
	cl_qpool_put_list( &p_ioc_mgr->path_pool, &list );
	cl_spinlock_release( &p_ioc_mgr->path_pool_lock );
}


static iou_ioc_t*
__get_ioc(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN		const	uint32_t					ioc_slot,
	IN		const	ib_ioc_profile_t* const		p_profile )
{
	cl_pool_item_t		*p_item;
	iou_ioc_t			*p_ioc;
	ib_svc_entry_t		*p_svc_entries;

	if( !p_profile->num_svc_entries )
		return NULL;

	p_svc_entries =
		cl_zalloc( sizeof(ib_svc_entry_t) * p_profile->num_svc_entries );
	if( !p_svc_entries )
		return NULL;

	cl_spinlock_acquire( &p_ioc_mgr->ioc_pool_lock );
	p_item = cl_qpool_get( &p_ioc_mgr->ioc_pool );
	cl_spinlock_release( &p_ioc_mgr->ioc_pool_lock );
	if( !p_item )
	{
		cl_free( p_svc_entries );
		return NULL;
	}

	p_ioc = PARENT_STRUCT( PARENT_STRUCT( p_item, cl_map_item_t, pool_item ),
		iou_ioc_t, map_item );
	
	CL_ASSERT( !p_ioc->ref_cnt );

	CL_ASSERT( !(ioc_slot >> 8) );
	p_ioc->slot = (uint8_t)ioc_slot;
	p_ioc->profile = *p_profile;
	p_ioc->num_valid_entries = 0;
	p_ioc->p_svc_entries = p_svc_entries;
	cl_atomic_inc( &p_ioc->ref_cnt );
	return p_ioc;
}


static void
__put_ioc(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				iou_ioc_t* const			p_ioc )
{
	if( cl_atomic_dec( &p_ioc->ref_cnt ) == 0 )
	{
		cl_free( p_ioc->p_svc_entries );

		cl_spinlock_acquire( &p_ioc_mgr->ioc_pool_lock );
		cl_qpool_put( &p_ioc_mgr->ioc_pool, &p_ioc->map_item.pool_item );
		cl_spinlock_release( &p_ioc_mgr->ioc_pool_lock );
	}
}


static void
__put_ioc_map(
	IN				ioc_pnp_mgr_t* const		p_ioc_mgr,
	IN				cl_qmap_t* const			p_ioc_map )
{
	cl_qlist_t		list;
	cl_map_item_t	*p_item;
	iou_ioc_t		*p_ioc;

	cl_qlist_init( &list );

	p_item = cl_qmap_head( p_ioc_map );
	while( p_item != cl_qmap_end( p_ioc_map ) )
	{
		cl_qmap_remove_item( p_ioc_map, p_item );

		p_ioc = PARENT_STRUCT(
			PARENT_STRUCT( p_item, cl_map_item_t, pool_item ),
			iou_ioc_t, map_item );
		
		if( cl_atomic_dec( &p_ioc->ref_cnt ) == 0 )
		{
			cl_free( p_ioc->p_svc_entries );
			cl_qlist_insert_head( &list, &p_item->pool_item.list_item );
		}
		p_item = cl_qmap_head( p_ioc_map );
	}
	cl_spinlock_acquire( &p_ioc_mgr->ioc_pool_lock );
	cl_qpool_put_list( &p_ioc_mgr->ioc_pool, &list );
	cl_spinlock_release( &p_ioc_mgr->ioc_pool_lock );
}


/*
 * Compares two IOUs for inserts/lookups in a flexi map.  Keys are the
 * address of the ca_guid, which is adjacent to the node GUID of the IOU.
 * This allows for a single call to cl_memcmp.
 */
static int
__iou_cmp(
	IN		const	void* const					p_key1,
	IN		const	void* const					p_key2 )
{
	return cl_memcmp( p_key1, p_key2, sizeof(uint64_t) * 2 );
}


/*
 * Compares two paths for inserts/lookups in a flexi map.
 */
static int
__path_cmp(
	IN		const	void* const					p_key1,
	IN		const	void* const					p_key2 )
{
	return cl_memcmp( p_key1, p_key2, sizeof(ib_path_rec_t) );
}


/*
 * Removes all paths and orphaned IOC/IOUs upon a port DOWN event.
 */
static void
__process_port_down(
	IN		const	net64_t						port_guid )
{
	cl_fmap_item_t		*p_path_item;
	cl_fmap_item_t		*p_iou_item;
	iou_node_t			*p_iou;
	iou_path_t			*p_path;
	cl_fmap_t			old_paths;
	cl_fmap_t			old_ious;

	AL_ENTER( AL_DBG_PNP );

	cl_fmap_init( &old_paths, __path_cmp );
	cl_fmap_init( &old_ious, __iou_cmp );

	p_iou_item = cl_fmap_head( &gp_ioc_pnp->iou_map );
	while( p_iou_item != cl_fmap_end( &gp_ioc_pnp->iou_map ) )
	{
		p_iou = PARENT_STRUCT( p_iou_item, iou_node_t, map_item );
		/*
		 * Note that it is safe to move to the next item even if we remove
		 * the IOU from the map since the map effectively maintains an ordered
		 * list of its contents.
		 */
		p_iou_item = cl_fmap_next( p_iou_item );

		p_path_item = cl_fmap_head( &p_iou->path_map );
		while( p_path_item != cl_fmap_end( &p_iou->path_map ) )
		{
			p_path = PARENT_STRUCT( p_path_item, iou_path_t, map_item );
			p_path_item = cl_fmap_next( p_path_item );
			if( p_path->rec.sgid.unicast.interface_id == port_guid )
			{
				cl_fmap_remove_item( &p_iou->path_map, &p_path->map_item );
				cl_fmap_insert( &old_paths, &p_path->rec, &p_path->map_item );
			}
		}

		if( !cl_fmap_count( &p_iou->path_map ) )
		{
			/* Move the paths back to the IOU so that they get freed. */
			cl_fmap_merge( &p_iou->path_map, &old_paths );
			cl_fmap_remove_item( &gp_ioc_pnp->iou_map, &p_iou->map_item );
			cl_fmap_insert( &old_ious, &p_iou->ca_guid, &p_iou->map_item );
		}
		else
		{
			/* Report the removed paths. */
			__remove_paths( &p_iou->ioc_map, &old_paths );
		}
	}

	/* Report any removed IOUs. */
	__remove_ious( &old_ious );

	AL_EXIT( AL_DBG_PNP );
}


void
ioc_pnp_request_ioc_rescan(void)
{
	ib_api_status_t	status;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( g_ioc_poll_interval == 1 );
	CL_ASSERT( gp_ioc_pnp );

	/* continue IOC sweeping or start a new series of sweeps? */
	cl_atomic_add( &gp_ioc_pnp->reSweep, IOC_RESWEEPS );
	if ( !gp_ioc_pnp->query_cnt )
	{
		status = cl_timer_start( &gp_ioc_pnp->sweep_timer, 3 );
		CL_ASSERT( status == CL_SUCCESS );
	}
	AL_EXIT( AL_DBG_PNP );
}


static const char *
__ib_get_pnp_event_str( ib_pnp_event_t event )
{
	return ib_get_pnp_event_str( event );
}


/*
 * PnP callback for port event notifications.
 */
static ib_api_status_t
__ioc_pnp_cb(
	IN				ib_pnp_rec_t				*p_pnp_rec )
{
	ib_api_status_t	status = IB_SUCCESS;
	cl_status_t		cl_status;

	AL_ENTER( AL_DBG_PNP );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
			p_pnp_rec->pnp_event, __ib_get_pnp_event_str(p_pnp_rec->pnp_event)) );

	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_PORT_ADD:
		/* Create the port service. */
		CL_ASSERT( !p_pnp_rec->context );
		status = __create_ioc_pnp_svc( p_pnp_rec );
		break;

	case IB_PNP_SM_CHANGE:
	case IB_PNP_PORT_ACTIVE:
		/* Initiate a sweep - delay a bit to allow the ports to come up. */
		if( g_ioc_poll_interval && !gp_ioc_pnp->query_cnt)
		{
			cl_status = cl_timer_start( &gp_ioc_pnp->sweep_timer, 250 );
			CL_ASSERT( cl_status == CL_SUCCESS );
		}
		break;

	case IB_PNP_PORT_DOWN:
	case IB_PNP_PORT_INIT:
	case IB_PNP_PORT_ARMED:
		CL_ASSERT( p_pnp_rec->context );

		/*
		 * Report IOC and IOU remove events for any IOU/IOCs that only have
		 * paths through this port.  Note, no need to synchronize with a
		 * sweep since synchronization is provided by the PnP thread.
		 */
		__process_port_down( p_pnp_rec->guid );
		break;

	case IB_PNP_PORT_REMOVE:
		/* Destroy the port service. */
		ref_al_obj( &((ioc_pnp_svc_t*)p_pnp_rec->context)->obj );
		((ioc_pnp_svc_t*)p_pnp_rec->context)->obj.pfn_destroy(
			&((ioc_pnp_svc_t*)p_pnp_rec->context)->obj, NULL );
		p_pnp_rec->context = NULL;
		break;

	case IB_PNP_IOU_ADD:
	case IB_PNP_IOU_REMOVE:
	case IB_PNP_IOC_ADD:
	case IB_PNP_IOC_REMOVE:
	case IB_PNP_IOC_PATH_ADD:
	case IB_PNP_IOC_PATH_REMOVE:
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_PNP, ("!Handled PNP Event %s\n",
			__ib_get_pnp_event_str(p_pnp_rec->pnp_event)) );
		break;

	default:
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Ignored PNP Event %s\n",
			__ib_get_pnp_event_str(p_pnp_rec->pnp_event)) );
		break;	/* Ignore other PNP events. */
	}

	AL_EXIT( AL_DBG_PNP );
	return status;
}


static ib_api_status_t
__init_ioc_pnp_svc(
	IN				ioc_pnp_svc_t* const		p_ioc_pnp_svc,
	IN		const	ib_pnp_rec_t* const			p_pnp_rec )
{
	ib_api_status_t		status;
	ib_ca_handle_t		h_ca;
	ib_qp_create_t		qp_create;
	ib_mad_svc_t		mad_svc;
	ib_pnp_port_rec_t	*p_pnp_port_rec;

	AL_ENTER( AL_DBG_PNP );

	p_pnp_port_rec = PARENT_STRUCT( p_pnp_rec, ib_pnp_port_rec_t, pnp_rec );

	/* Store the CA and port GUID so we can issue SA queries. */
	p_ioc_pnp_svc->ca_guid = p_pnp_port_rec->p_ca_attr->ca_guid;
	p_ioc_pnp_svc->port_guid = p_pnp_rec->guid;

	/* Acquire the correct CI CA for this port. */
	h_ca = acquire_ca( p_pnp_port_rec->p_ca_attr->ca_guid );
	if( !h_ca )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("acquire_ca failed.\n") );
		return IB_INVALID_GUID;
	}
	p_ioc_pnp_svc->obj.p_ci_ca = h_ca->obj.p_ci_ca;

	/* Create the MAD QP. */
	cl_memclr( &qp_create, sizeof( ib_qp_create_t ) );
	qp_create.qp_type = IB_QPT_QP1_ALIAS;
	qp_create.sq_depth = p_pnp_port_rec->p_ca_attr->max_wrs;
	qp_create.sq_sge = 1;
	qp_create.sq_signaled = TRUE;
	/*
	 * We use the IOC PnP service's al_obj_t as the context to allow using
	 * deref_al_obj as the destroy callback.
	 */
	status = ib_get_spl_qp( h_ca->obj.p_ci_ca->h_pd_alias,
		p_pnp_port_rec->p_port_attr->port_guid, &qp_create,
		&p_ioc_pnp_svc->obj, NULL, &p_ioc_pnp_svc->pool_key,
		&p_ioc_pnp_svc->h_qp );

	/*
	 * Release the CI CA once we've allocated the QP.  The CI CA will not
	 * go away while we hold the QP.
	 */
	deref_al_obj( &h_ca->obj );

	/* Check for failure allocating the QP. */
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_get_spl_qp failed with status %s\n",
			ib_get_err_str( status )) );
		return status;
	}
	/* Reference the port object on behalf of the QP. */
	ref_al_obj( &p_ioc_pnp_svc->obj );

	/* Create the MAD service. */
	cl_memclr( &mad_svc, sizeof(ib_mad_svc_t) );
	mad_svc.mad_svc_context = p_ioc_pnp_svc;
	mad_svc.pfn_mad_recv_cb = __ioc_pnp_recv_cb;
	mad_svc.pfn_mad_send_cb = __ioc_pnp_send_cb;
	status =
		ib_reg_mad_svc( p_ioc_pnp_svc->h_qp, &mad_svc,
		&p_ioc_pnp_svc->h_mad_svc );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_reg_mad_svc failed with status %s\n",
			ib_get_err_str( status )) );
		return status;
	}

	AL_EXIT( AL_DBG_PNP );
	return IB_SUCCESS;
}


/*
 * Create a port agent for a given port.
 */
static ib_api_status_t
__create_ioc_pnp_svc(
	IN				ib_pnp_rec_t				*p_pnp_rec )
{
	ioc_pnp_svc_t		*p_ioc_pnp_svc;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_PNP );

	/* calculate size of port_cm struct */
	p_ioc_pnp_svc = (ioc_pnp_svc_t*)cl_zalloc( sizeof(ioc_pnp_svc_t) );
	if( !p_ioc_pnp_svc )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to cl_zalloc port CM agent.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	construct_al_obj( &p_ioc_pnp_svc->obj, AL_OBJ_TYPE_IOC_PNP_SVC );

	status = init_al_obj( &p_ioc_pnp_svc->obj, p_ioc_pnp_svc, TRUE,
		__destroying_ioc_pnp_svc, NULL, __free_ioc_pnp_svc );
	if( status != IB_SUCCESS )
	{
		__free_ioc_pnp_svc( &p_ioc_pnp_svc->obj );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed with status %s.\n",
			ib_get_err_str( status )) );
		return status;
	}

	/* Attach to the global CM object. */
	status = attach_al_obj( &gp_ioc_pnp->obj, &p_ioc_pnp_svc->obj );
	if( status != IB_SUCCESS )
	{
		p_ioc_pnp_svc->obj.pfn_destroy( &p_ioc_pnp_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	status = __init_ioc_pnp_svc( p_ioc_pnp_svc, p_pnp_rec );
	if( status != IB_SUCCESS )
	{
		p_ioc_pnp_svc->obj.pfn_destroy( &p_ioc_pnp_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("__init_data_svc failed with status %s.\n",
			ib_get_err_str( status )) );
		return status;
	}

	/* Set the PnP context to reference this service. */
	p_pnp_rec->context = p_ioc_pnp_svc;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_ioc_pnp_svc->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_PNP );
	return status;
}


static void
__destroying_ioc_pnp_svc(
	IN				al_obj_t					*p_obj )
{
	ib_api_status_t		status;
	ioc_pnp_svc_t		*p_svc;

	CL_ASSERT( p_obj );
	p_svc = PARENT_STRUCT( p_obj, ioc_pnp_svc_t, obj );

	if( p_svc->h_node_query )
		ib_cancel_query( gh_al, p_svc->h_node_query );

	if( p_svc->h_path_query )
		ib_cancel_query( gh_al, p_svc->h_path_query );

	/* Destroy the QP. */
	if( p_svc->h_qp )
	{
		status =
			ib_destroy_qp( p_svc->h_qp, (ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}
}


static void
__free_ioc_pnp_svc(
	IN				al_obj_t					*p_obj )
{
	ioc_pnp_svc_t*		p_svc;

	CL_ASSERT( p_obj );
	p_svc = PARENT_STRUCT( p_obj, ioc_pnp_svc_t, obj );

	CL_ASSERT( !p_svc->query_cnt );

	destroy_al_obj( p_obj );
	cl_free( p_svc );
}


static void
__ioc_pnp_timer_cb(
	IN				void						*context )
{
	ib_api_status_t		status;
	ioc_pnp_mgr_t		*p_mgr;
	cl_list_item_t		*p_item;
	ioc_pnp_svc_t		*p_svc;

	AL_ENTER( AL_DBG_PNP );

	p_mgr = (ioc_pnp_mgr_t*)context;

	cl_spinlock_acquire( &p_mgr->obj.lock );
	if( p_mgr->obj.state == CL_DESTROYING )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
			("Destroying - not resetting timer.\n") );
		cl_spinlock_release( &p_mgr->obj.lock );
		return;
	}

	CL_ASSERT( !cl_fmap_count( &p_mgr->sweep_map ) );

	/* Pre-charge the ref count so that we don't toggle between 0 and 1. */
	cl_atomic_inc( &p_mgr->query_cnt );
	/* Take a reference on the object for the duration of the sweep process. */
	ref_al_obj( &p_mgr->obj );
	for( p_item = cl_qlist_head( &p_mgr->obj.obj_list );
		p_item != cl_qlist_end( &p_mgr->obj.obj_list );
		p_item = cl_qlist_next( p_item ) )
	{
		p_svc = PARENT_STRUCT( PARENT_STRUCT( p_item, al_obj_t, pool_item ),
			ioc_pnp_svc_t, obj );
		cl_atomic_inc( &p_mgr->query_cnt );
		status = __ioc_query_sa( p_svc );
		if( status != IB_SUCCESS )
			cl_atomic_dec( &p_mgr->query_cnt );
	}

	if ( g_ioc_poll_interval == 1 && p_mgr->reSweep > 0 )
		cl_atomic_dec( &p_mgr->reSweep );

	/* Release the reference we took and see if we're done sweeping. */
	if( !cl_atomic_dec( &p_mgr->query_cnt ) )
		cl_async_proc_queue( gp_async_pnp_mgr, &p_mgr->async_item );

	cl_spinlock_release( &p_mgr->obj.lock );

	AL_EXIT( AL_DBG_PNP );
}


static ib_api_status_t
__ioc_query_sa(
	IN				ioc_pnp_svc_t* const		p_svc )
{
	ib_api_status_t		status = IB_NOT_DONE;
	ib_query_req_t		query;
	ib_user_query_t		info;
	union _ioc_pnp_timer_cb_u
	{
		ib_node_record_t	node_rec;
		ib_path_rec_t		path_rec;

	}	u;

	AL_ENTER( AL_DBG_PNP );

	if( p_svc->h_node_query )
		return IB_NOT_DONE;
	if( p_svc->h_path_query )
		return IB_NOT_DONE;

	if( p_svc->obj.state == CL_DESTROYING )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
			("Destroying - not resetting timer.\n") );
		return IB_NOT_DONE;
	}

	info.method = IB_MAD_METHOD_GETTABLE;
	info.attr_id = IB_MAD_ATTR_NODE_RECORD;
	info.attr_size = sizeof(ib_node_record_t);
	info.comp_mask = IB_NR_COMPMASK_NODETYPE;
	info.p_attr = &u.node_rec;

	cl_memclr( &u.node_rec, sizeof(ib_node_record_t) );
	u.node_rec.node_info.node_type = IB_NODE_TYPE_CA;

	cl_memclr( &query, sizeof(ib_query_req_t) );
	query.query_type = IB_QUERY_USER_DEFINED;
	query.p_query_input = &info;
	query.port_guid = p_svc->port_guid;
	query.timeout_ms = g_ioc_query_timeout;
	query.retry_cnt = g_ioc_query_retries;
	query.query_context = p_svc;
	query.pfn_query_cb = __node_rec_cb;

	/* Reference the service for the node record query. */
	ref_al_obj( &p_svc->obj );
	cl_atomic_inc( &p_svc->query_cnt );

	status = ib_query( gh_al, &query, &p_svc->h_node_query );
	if( status != IB_SUCCESS )
	{
		cl_atomic_dec( &p_svc->query_cnt );
		deref_al_obj( &p_svc->obj );
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_PNP,
			("ib_query returned %s\n", ib_get_err_str( status )) );
		return status;
	}

	/* Setup the path query. */
	info.method = IB_MAD_METHOD_GETTABLE;
	info.attr_id = IB_MAD_ATTR_PATH_RECORD;
	info.attr_size = sizeof(ib_path_rec_t);
	info.comp_mask = IB_PR_COMPMASK_SGID | IB_PR_COMPMASK_NUMBPATH | 
		IB_PR_COMPMASK_PKEY;
	info.p_attr = &u.path_rec;

	cl_memclr( &u.path_rec, sizeof(ib_path_rec_t) );
	ib_gid_set_default( &u.path_rec.sgid, p_svc->port_guid );
	/* Request all the paths available, setting the reversible bit. */
	u.path_rec.num_path = 0xFF;
	/* Request only paths from the default partition */
	u.path_rec.pkey = cl_hton16(IB_DEFAULT_PKEY);

	query.pfn_query_cb = __path_rec_cb;

	/* Reference the service for the node record query. */
	ref_al_obj( &p_svc->obj );
	cl_atomic_inc( &p_svc->query_cnt );

	status = ib_query( gh_al, &query, &p_svc->h_path_query );
	if( status != IB_SUCCESS )
	{
		cl_atomic_dec( &p_svc->query_cnt );
		deref_al_obj( &p_svc->obj );
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_PNP,
			("ib_query returned %s\n", ib_get_err_str( status )) );
	}

	AL_EXIT( AL_DBG_PNP );
	return IB_SUCCESS;
}


static void
__node_rec_cb(
	IN				ib_query_rec_t				*p_query_rec )
{
	ioc_pnp_svc_t		*p_svc;

	AL_ENTER( AL_DBG_PNP );

	p_svc = (ioc_pnp_svc_t*)p_query_rec->query_context;

	if( p_svc->obj.state != CL_DESTROYING &&
		p_query_rec->status == IB_SUCCESS && p_query_rec->result_cnt )
	{
		CL_ASSERT( p_query_rec->p_result_mad );
		CL_ASSERT( !p_svc->p_node_element );
		CL_ASSERT( p_query_rec->p_result_mad->p_next == NULL );
		p_svc->p_node_element = p_query_rec->p_result_mad;
		p_svc->num_nodes = p_query_rec->result_cnt;
	}
	else if( p_query_rec->p_result_mad )
	{
		ib_put_mad( p_query_rec->p_result_mad );
	}

	p_svc->h_node_query = NULL;
	if( !cl_atomic_dec( &p_svc->query_cnt ) )
	{
		/* The path query has already completed.  Process the results. */
		__process_query( p_svc );
	}

	/* Release the reference taken for the query. */
	deref_al_obj( &p_svc->obj );

	AL_EXIT( AL_DBG_PNP );
}


static void
__path_rec_cb(
	IN				ib_query_rec_t				*p_query_rec )
{
	ioc_pnp_svc_t		*p_svc;

	AL_ENTER( AL_DBG_PNP );

	p_svc = (ioc_pnp_svc_t*)p_query_rec->query_context;

	if( p_svc->obj.state != CL_DESTROYING &&
		p_query_rec->status == IB_SUCCESS && p_query_rec->result_cnt )
	{
		CL_ASSERT( p_query_rec->p_result_mad );
		CL_ASSERT( !p_svc->p_path_element );
		CL_ASSERT( p_query_rec->p_result_mad->p_next == NULL );
		p_svc->p_path_element = p_query_rec->p_result_mad;
		p_svc->num_paths = p_query_rec->result_cnt;
	}
	else if( p_query_rec->p_result_mad )
	{
		ib_put_mad( p_query_rec->p_result_mad );
	}

	p_svc->h_path_query = NULL;
	if( !cl_atomic_dec( &p_svc->query_cnt ) )
	{
		/* The node query has already completed.  Process the results. */
		__process_query( p_svc );
	}

	/* Release the reference taken for the query. */
	deref_al_obj( &p_svc->obj );

	AL_EXIT( AL_DBG_PNP );
}

static void
__process_query(
	IN				ioc_pnp_svc_t* const		p_svc )
{
	ib_api_status_t			status;
	ioc_sweep_results_t		*p_results;
	cl_qmap_t				port_map;

	AL_ENTER( AL_DBG_PNP );

	cl_qmap_init( &port_map );

	if( !p_svc->p_node_element || !p_svc->p_path_element )
	{
		/* One of the queries failed.  Release the MADs and reset the timer. */
		if( p_svc->p_node_element )
		{
			ib_put_mad( p_svc->p_node_element );
			p_svc->p_node_element = NULL;
		}

		if( p_svc->p_path_element )
		{
			ib_put_mad( p_svc->p_path_element );
			p_svc->p_path_element = NULL;
		}

		/* Decrement the IOC PnP manager's query count. */
		if( !cl_atomic_dec( &gp_ioc_pnp->query_cnt ) )
			cl_async_proc_queue( gp_async_pnp_mgr, &gp_ioc_pnp->async_item );
		AL_EXIT( AL_DBG_PNP );
		return;
	}

	/*
	 * Allocate the sweep results structure to allow processing
	 * asynchronously.
	 */
	p_results = cl_zalloc( sizeof(ioc_sweep_results_t) );
	if( p_results )
	{
		p_results->async_item.pfn_callback = __process_sweep;
		p_results->p_svc = p_svc;
		cl_fmap_init( &p_results->iou_map, __iou_cmp );

		/* Reference the service till the end of sweep processing */
		ref_al_obj( &p_results->p_svc->obj );

		/* Build the map of nodes by port GUID. */
		__process_nodes( p_svc, &port_map );

		/* Build the map of paths for each node. */
		__process_paths( p_svc, &port_map );

		/* Collapse the map of nodes to be keyed by node GUID. */
		__build_iou_map( &port_map, &p_results->iou_map );

		/* Send the IOU Info queries to the nodes. */
		status = __query_ious( p_results );
	}
	else
	{
		status = IB_INSUFFICIENT_MEMORY;
	}

	/* Release the query result MADs now that we're done with them. */
	ib_put_mad( p_svc->p_node_element );
	ib_put_mad( p_svc->p_path_element );
	p_svc->p_node_element = NULL;
	p_svc->p_path_element = NULL;

	switch( status )
	{
	case IB_SUCCESS:
		break;
	default:
		CL_ASSERT( p_results );
		/* Release the reference taken for the sweep. */
		deref_al_obj( &p_results->p_svc->obj );
		cl_free( p_results );
		/* Fall through */
	case IB_INSUFFICIENT_MEMORY:
		/* Decrement the IOC PnP manager's query count. */
		if( !cl_atomic_dec( &gp_ioc_pnp->query_cnt ) )
			cl_async_proc_queue( gp_async_pnp_mgr, &gp_ioc_pnp->async_item );
	}
	AL_EXIT( AL_DBG_PNP );
}


static void
__process_nodes(
	IN				ioc_pnp_svc_t* const		p_svc,
	IN				cl_qmap_t* const			p_port_map )
{
	iou_node_t			*p_iou;
	ib_node_record_t	*p_node_rec;
	uint32_t			i;
	void				*p_item;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_svc );
	CL_ASSERT( p_svc->p_node_element );
	CL_ASSERT( p_port_map );

	for( i = 0; i < p_svc->num_nodes; i++ )
	{
		p_node_rec = ib_get_query_node_rec( p_svc->p_node_element, i );

		p_iou = __get_iou( gp_ioc_pnp, p_svc->ca_guid, p_node_rec );
		if( !p_iou )
			break;

		/*
		 * We insert by port GUID, not node GUID so that we can match
		 * to paths using DGID.  Note that it is safe to cast between
		 * a flexi-map item and a map item since the pointer to the key
		 * in a flexi-map item is always a 64-bit pointer.
		 */
		p_item = cl_qmap_insert(
			p_port_map, p_node_rec->node_info.port_guid,
			(cl_map_item_t*)&p_iou->map_item );
		if( p_item != &p_iou->map_item )
		{
			/* Duplicate node - discard. */
			__put_iou( gp_ioc_pnp, p_iou );
		}
	}

	AL_EXIT( AL_DBG_PNP );
}


static void
__process_paths(
	IN				ioc_pnp_svc_t* const		p_svc,
	IN				cl_qmap_t* const			p_port_map )
{
	iou_node_t			*p_iou;
	iou_path_t			*p_path;
	ib_path_rec_t		*p_path_rec;
	uint32_t			i;
	cl_map_item_t		*p_iou_item;
	cl_fmap_item_t		*p_item;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_svc );
	CL_ASSERT( p_svc->p_node_element );
	CL_ASSERT( p_port_map );

	for( i = 0; i < p_svc->num_paths; i++ )
	{
		p_path_rec = ib_get_query_path_rec( p_svc->p_path_element, i );

		p_iou_item =
			cl_qmap_get( p_port_map, p_path_rec->dgid.unicast.interface_id );
		if( p_iou_item == cl_qmap_end( p_port_map ) )
			continue;

		p_iou = PARENT_STRUCT( p_iou_item, iou_node_t, map_item );

		p_path = __get_path( gp_ioc_pnp, p_svc->ca_guid,
			p_svc->port_guid, p_path_rec );
		if( !p_path )
			break;

		p_item = cl_fmap_insert( &p_iou->path_map, &p_path->rec,
			&p_path->map_item );
		if( p_item != &p_path->map_item )
		{
			/* Duplicate path - discard. */
			__put_path( gp_ioc_pnp, p_path );
		}
	}

	AL_EXIT( AL_DBG_PNP );
}


static void
__build_iou_map(
	IN				cl_qmap_t* const			p_port_map,
	IN	OUT			cl_fmap_t* const			p_iou_map )
{
	cl_fmap_t		map1, map2;
	void			*p_item;
	iou_node_t		*p_iou, *p_dup;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( !cl_fmap_count( p_iou_map ) );

	cl_fmap_init( &map1, __path_cmp );
	cl_fmap_init( &map2, __path_cmp );

	/*
	 * Now collapse the map so that IOUs aren't repeated.
	 * This is needed because the IOU map is keyed by port GUID, and thus
	 * a multi-port IOU could be listed twice.
	 */
	/* Merge the port map into a map of IOUs. */
	for( p_item = cl_qmap_head( p_port_map );
		p_item != cl_qmap_end( p_port_map );
		p_item = cl_qmap_head( p_port_map ) )
	{
		cl_qmap_remove_item( p_port_map, (cl_map_item_t*)p_item );
		p_iou = PARENT_STRUCT( p_item, iou_node_t, map_item );

		p_item = cl_fmap_insert( p_iou_map, &p_iou->ca_guid, p_item );
		if( p_item != &p_iou->map_item )
		{
			/* Duplicate IOU information - merge the paths. */
			p_dup = PARENT_STRUCT( p_item, iou_node_t, map_item );
			CL_ASSERT( p_dup != p_iou );
			cl_fmap_delta( &p_dup->path_map, &p_iou->path_map, &map1, &map2 );
			/*
			 * The path map in p_iou->path_map is duplicate paths.
			 * map1 contains paths unique to p_iou->path_map, map2 contains
			 * paths unique to p_dup->path_map.  Add the unique paths back to
			 * p_dup->path_map since that IOU is already in the IOU map.
			 * Note that we are keeping the p_dup IOU node.
			 */
			cl_fmap_merge( &p_dup->path_map, &map1 );
			cl_fmap_merge( &p_dup->path_map, &map2 );
			/* All unique items should have merged without duplicates. */
			CL_ASSERT( !cl_fmap_count( &map1 ) );
			CL_ASSERT( !cl_fmap_count( &map2 ) );

			__put_iou( gp_ioc_pnp, p_iou );
		}
	}

	AL_EXIT( AL_DBG_PNP );
}


static void
__format_dm_get(
	IN		const	void* const					context1,
	IN		const	void* const					context2,
	IN		const	iou_path_t* const			p_path,
	IN		const	net16_t						attr_id,
	IN		const	net32_t						attr_mod,
	IN	OUT			ib_mad_element_t* const		p_mad_element )
{
	static uint64_t		tid = 0;

	AL_ENTER( AL_DBG_PNP );

	/*
	 * Context information so that we can continue processing when
	 * the query completes.
	 */
	p_mad_element->context1 = context1;
	p_mad_element->context2 = context2;

	/*
	 * Set the addressing bits necessary for the mad service to
	 * create the address vector
	 */
	p_mad_element->h_av = NULL;
	p_mad_element->remote_sl = ib_path_rec_sl( &p_path->rec );
	p_mad_element->remote_lid = p_path->rec.dlid;
	p_mad_element->grh_valid = FALSE;
	p_mad_element->path_bits = p_path->rec.num_path;

	/* Request response processing. */
	p_mad_element->resp_expected = TRUE;
	p_mad_element->retry_cnt = g_ioc_query_retries;
	p_mad_element->timeout_ms = g_ioc_query_timeout;

	/* Set the destination information for the send. */
	p_mad_element->remote_qp = IB_QP1;
	p_mad_element->remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;

	/* Format the MAD payload. */
	cl_memclr( p_mad_element->p_mad_buf, sizeof(ib_dm_mad_t) );
	ib_mad_init_new( p_mad_element->p_mad_buf, IB_MCLASS_DEV_MGMT, 1,
		IB_MAD_METHOD_GET, cl_ntoh64( tid++ ), attr_id, attr_mod );

	AL_EXIT( AL_DBG_PNP );
}


static ib_api_status_t
__query_ious(
	IN				ioc_sweep_results_t* const	p_results )
{
	ib_api_status_t		status;
	iou_node_t			*p_iou;
	iou_path_t			*p_path;
	cl_fmap_item_t		*p_iou_item;
	cl_fmap_item_t		*p_path_item;
	ib_mad_element_t	*p_mad, *p_mad_list = NULL;

	AL_ENTER( AL_DBG_PNP );

	p_results->state = SWEEP_IOU_INFO;

	/* Send a IOU Info query on the first path to every IOU. */
	p_iou_item = cl_fmap_head( &p_results->iou_map );
	while( p_iou_item != cl_fmap_end( &p_results->iou_map ) )
	{
		p_iou = PARENT_STRUCT( p_iou_item, iou_node_t, map_item );
		p_iou_item = cl_fmap_next( p_iou_item );
		if( !cl_fmap_count( &p_iou->path_map ) )
		{
			/* No paths for this node.  Discard it. */
			cl_fmap_remove_item( &p_results->iou_map, &p_iou->map_item );
			__put_iou( gp_ioc_pnp, p_iou );
			continue;
		}

		p_path_item = cl_fmap_head( &p_iou->path_map );

		p_path = PARENT_STRUCT( p_path_item, iou_path_t, map_item );

		status = ib_get_mad( p_results->p_svc->pool_key,
			MAD_BLOCK_SIZE, &p_mad );
		if( status != IB_SUCCESS )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_get_mad for IOU Info query returned %s.\n",
				ib_get_err_str( status )) );
			break;
		}

		p_iou->p_config_path = p_path;
		__format_dm_get( p_results, p_iou, p_path,
			IB_MAD_ATTR_IO_UNIT_INFO, 0, p_mad );

		/* Link the elements together. */
		p_mad->p_next = p_mad_list;
		p_mad_list = p_mad;

		cl_atomic_inc( &p_results->p_svc->query_cnt );
	}

	if( !p_mad_list )
	{
		AL_EXIT( AL_DBG_PNP );
		return IB_ERROR;
	}

	status = ib_send_mad( p_results->p_svc->h_mad_svc, p_mad_list, &p_mad );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_send_mad returned %s\n", ib_get_err_str( status )) );

		/* If some sends succeeded, change the status. */
		if( p_mad_list != p_mad )
			status = IB_SUCCESS;

		while( p_mad )
		{
			p_mad_list = p_mad->p_next;
			p_mad->p_next = NULL;
			ib_put_mad( p_mad );
			if( !cl_atomic_dec( &p_results->p_svc->query_cnt ) &&
				status == IB_SUCCESS )
			{
				cl_async_proc_queue( gp_async_pnp_mgr,
					&p_results->async_item );
			}
			p_mad = p_mad_list;
		}
	}
	AL_EXIT( AL_DBG_PNP );
	return status;
}


static void
__ioc_pnp_recv_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_response )
{
	ioc_sweep_results_t	*p_results;
	iou_node_t			*p_iou;
	iou_ioc_t			*p_ioc;

	AL_ENTER( AL_DBG_PNP );

	UNUSED_PARAM( h_mad_svc );
	UNUSED_PARAM( mad_svc_context );
	CL_ASSERT( !p_mad_response->p_next );

	p_results = (ioc_sweep_results_t*)p_mad_response->send_context1;
	if( !p_mad_response->p_mad_buf->status )
	{
		/* Query was successful */
		switch( p_mad_response->p_mad_buf->attr_id )
		{
		case IB_MAD_ATTR_IO_UNIT_INFO:
			p_iou = (iou_node_t*)p_mad_response->send_context2;
			__iou_info_resp( p_iou,
				(ib_dm_mad_t*)p_mad_response->p_mad_buf );
			break;

		case IB_MAD_ATTR_IO_CONTROLLER_PROFILE:
			p_iou = (iou_node_t*)p_mad_response->send_context2;
			__ioc_profile_resp( p_iou,
				(ib_dm_mad_t*)p_mad_response->p_mad_buf );
			break;

		case IB_MAD_ATTR_SERVICE_ENTRIES:
			p_ioc = (iou_ioc_t*)p_mad_response->send_context2;
			__svc_entry_resp( p_ioc,
				(ib_dm_mad_t*)p_mad_response->p_mad_buf );
			break;

		default:
			break;
		}
	}

	ib_put_mad( p_mad_response );
	AL_EXIT( AL_DBG_PNP );
}


static void
__iou_info_resp(
	IN	OUT			iou_node_t* const			p_iou,
	IN		const	ib_dm_mad_t* const			p_mad )
{
	AL_ENTER( AL_DBG_PNP );
	/* Copy the IOU info for post-processing. */
	p_iou->info = *((ib_iou_info_t*)p_mad->data);
	AL_EXIT( AL_DBG_PNP );
}


static void
__ioc_profile_resp(
	IN	OUT			iou_node_t* const			p_iou,
	IN		const	ib_dm_mad_t* const			p_mad )
{
	iou_ioc_t		*p_ioc;
	cl_map_item_t	*p_item;

	AL_ENTER( AL_DBG_PNP );
	p_ioc = __get_ioc( gp_ioc_pnp, cl_ntoh32(p_mad->hdr.attr_mod),
		(ib_ioc_profile_t*)p_mad->data );
	if( p_ioc )
	{
		/* Need back link to process service entry failures. */
		p_ioc->p_iou = p_iou;
		cl_spinlock_acquire( &p_iou->lock );
		p_item = cl_qmap_insert( &p_iou->ioc_map,
			p_ioc->profile.ioc_guid, &p_ioc->map_item );
		cl_spinlock_release( &p_iou->lock );
		/* Return the IOC if it's a duplicate. */
		if( p_item != &p_ioc->map_item )
			__put_ioc( gp_ioc_pnp, p_ioc );
	}
	AL_EXIT( AL_DBG_PNP );
}


static void
__svc_entry_resp(
	IN	OUT			iou_ioc_t* const			p_ioc,
	IN		const	ib_dm_mad_t* const			p_mad )
{
	uint16_t			idx;
	uint8_t				lo, hi;
	ib_svc_entries_t	*p_svc_entries;

	AL_ENTER( AL_DBG_PNP );

	ib_dm_get_slot_lo_hi( p_mad->hdr.attr_mod, NULL, &lo, &hi );
	CL_ASSERT( (hi - lo) < SVC_ENTRY_COUNT );
	p_svc_entries = (ib_svc_entries_t*)p_mad->data;

	/* Copy the entries. */
	for( idx = lo; idx <= hi; idx++ )
		p_ioc->p_svc_entries[idx] = p_svc_entries->service_entry[idx - lo];

	/* Update the number of entries received so far. */
	p_ioc->num_valid_entries += (hi - lo) + 1;
	cl_atomic_dec(&p_ioc->ref_cnt);
	AL_EXIT( AL_DBG_PNP );
}


static void
__ioc_pnp_send_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_request_mad )
{
	ib_api_status_t		status;
	ioc_sweep_results_t	*p_results;
	iou_node_t			*p_iou;
	iou_ioc_t			*p_ioc;
	cl_fmap_item_t		*p_item;

	AL_ENTER( AL_DBG_PNP );

	UNUSED_PARAM( h_mad_svc );
	UNUSED_PARAM( mad_svc_context );

	CL_ASSERT( p_request_mad->p_next == NULL );

	p_results = (ioc_sweep_results_t*)p_request_mad->context1;

	if( p_request_mad->status != IB_WCS_SUCCESS )
	{
		switch( p_request_mad->p_mad_buf->attr_id )
		{
		case IB_MAD_ATTR_IO_UNIT_INFO:
			p_iou = (iou_node_t*)p_request_mad->context2;
			if( p_request_mad->status == IB_WCS_TIMEOUT_RETRY_ERR )
			{
				/* Move to the next path for the node and try the query again. */
				p_item = cl_fmap_next( &p_iou->p_config_path->map_item );
				if( p_item != cl_fmap_end( &p_iou->path_map ) )
				{
					p_iou->p_config_path =
						PARENT_STRUCT( p_item, iou_path_t, map_item );
					__format_dm_get( p_results, p_iou, p_iou->p_config_path,
						IB_MAD_ATTR_IO_UNIT_INFO, 0, p_request_mad );

					status = ib_send_mad( p_results->p_svc->h_mad_svc,
						p_request_mad, &p_request_mad );
					if( status == IB_SUCCESS )
					{
						AL_EXIT( AL_DBG_PNP );
						return;
					}
				}
			}
			break;

		case IB_MAD_ATTR_SERVICE_ENTRIES:
			p_ioc = (iou_ioc_t*)p_request_mad->context2;
			cl_spinlock_acquire( &p_ioc->p_iou->lock );
			cl_qmap_remove_item( &p_ioc->p_iou->ioc_map, &p_ioc->map_item );
			cl_spinlock_release( &p_ioc->p_iou->lock );
			__put_ioc( gp_ioc_pnp, p_ioc );
			break;

		default:
			break;
		}
	}

	/* Cleanup. */
	ib_put_mad( p_request_mad );

	/*
	 * If this is the last MAD, finish processing the IOU queries
	 * in the PnP thread.
	 */
	if( !cl_atomic_dec( &p_results->p_svc->query_cnt ) )
		cl_async_proc_queue( gp_async_pnp_mgr, &p_results->async_item );

	AL_EXIT( AL_DBG_PNP );
}


static void
__flush_duds(
	IN	OUT			ioc_sweep_results_t			*p_results )
{
	cl_fmap_item_t			*p_item;
	cl_map_item_t			*p_ioc_item;
	iou_node_t				*p_iou;
	iou_ioc_t				*p_ioc;

	AL_ENTER( AL_DBG_PNP );

	/* Walk the map of IOUs and discard any that didn't respond to IOU info. */
	p_item = cl_fmap_head( &p_results->iou_map );
	/*
	 * No locking required since we're protected by the serialization of the
	 * PnP thread.
	 */
	while( p_item != cl_fmap_end( &p_results->iou_map ) )
	{
		p_iou = PARENT_STRUCT( p_item, iou_node_t, map_item );

		p_item = cl_fmap_next( p_item );
		switch( p_results->state )
		{
		case SWEEP_IOU_INFO:
			if( p_iou->info.max_controllers )
				continue;
			break;

		case SWEEP_SVC_ENTRIES:
			CL_ASSERT( cl_qmap_count( &p_iou->ioc_map ) );
			p_ioc_item = cl_qmap_head( &p_iou->ioc_map );
			while( p_ioc_item != cl_qmap_end( &p_iou->ioc_map ) )
			{
				p_ioc = PARENT_STRUCT( p_ioc_item, iou_ioc_t, map_item );
				p_ioc_item = cl_qmap_next( p_ioc_item );

				if( !p_ioc->num_valid_entries ||
					p_ioc->num_valid_entries != p_ioc->profile.num_svc_entries )
				{
					cl_qmap_remove_item( &p_iou->ioc_map, &p_ioc->map_item );
					__put_ioc( gp_ioc_pnp, p_ioc );
				}
			}
			/* Fall through. */
		case SWEEP_IOC_PROFILE:
			if( cl_qmap_count( &p_iou->ioc_map ) )
				continue;
			break;

		default:
			CL_ASSERT( p_results->state != SWEEP_COMPLETE );
			break;
		}

		cl_fmap_remove_item( &p_results->iou_map, &p_iou->map_item );
		__put_iou( gp_ioc_pnp, p_iou );
	}

	AL_EXIT( AL_DBG_PNP );
}

static void
__process_sweep(
	IN				cl_async_proc_item_t		*p_async_item )
{
	ib_api_status_t			status;
	ioc_sweep_results_t		*p_results;

	AL_ENTER( AL_DBG_PNP );

	p_results = PARENT_STRUCT( p_async_item, ioc_sweep_results_t, async_item );
	CL_ASSERT( !p_results->p_svc->query_cnt );

	if( p_results->p_svc->obj.state == CL_DESTROYING )
	{
		__put_iou_map( gp_ioc_pnp, &p_results->iou_map );
		goto err;
	}

	/* Walk the map of IOUs and discard any that didn't respond to IOU info. */
	__flush_duds( p_results );
	switch( p_results->state )
	{
	case SWEEP_IOU_INFO:
		/* Next step, query IOC profiles for all IOUs. */
		p_results->state = SWEEP_IOC_PROFILE;
		status = __query_ioc_profiles( p_results );
		break;

	case SWEEP_IOC_PROFILE:
		/* Next step: query service entries for all IOCs. */
		p_results->state = SWEEP_SVC_ENTRIES;
		status = __query_svc_entries( p_results );
		break;

	case SWEEP_SVC_ENTRIES:
		/* Filter results and report changes. */
		p_results->state = SWEEP_COMPLETE;
		__update_results( p_results );
		status = IB_SUCCESS;
		break;

	default:
		CL_ASSERT( p_results->state == SWEEP_IOU_INFO ||
			p_results->state == SWEEP_IOC_PROFILE ||
			p_results->state == SWEEP_SVC_ENTRIES );
		status = IB_ERROR;
	}

	if( p_results->state == SWEEP_COMPLETE || status != IB_SUCCESS )
	{
err:
		if( !cl_atomic_dec( &gp_ioc_pnp->query_cnt ) )
			cl_async_proc_queue( gp_async_pnp_mgr, &gp_ioc_pnp->async_item );
		/* Release the reference taken for the sweep. */
		deref_al_obj( &p_results->p_svc->obj );
		cl_free( p_results );
	}

	AL_EXIT( AL_DBG_PNP );
}


static ib_api_status_t
__query_ioc_profiles(
	IN				ioc_sweep_results_t* const	p_results )
{
	ib_api_status_t		status;
	cl_fmap_item_t		*p_item;
	iou_node_t			*p_iou;
	uint8_t				slot;
	ib_mad_element_t	*p_mad, *p_mad_list = NULL;

	AL_ENTER( AL_DBG_PNP );

	p_item = cl_fmap_head( &p_results->iou_map );
	while( p_item != cl_fmap_end( &p_results->iou_map ) )
	{
		p_iou = PARENT_STRUCT( p_item, iou_node_t, map_item );
		CL_ASSERT( p_iou->info.max_controllers );
		CL_ASSERT( cl_fmap_count( &p_iou->path_map ) );
		CL_ASSERT( p_iou->p_config_path );
		p_item = cl_fmap_next( p_item );

		p_mad = NULL;
		for( slot = 1; slot <= p_iou->info.max_controllers; slot++ )
		{
			if( ioc_at_slot( &p_iou->info, slot ) == IOC_INSTALLED )
			{
				status = ib_get_mad( p_results->p_svc->pool_key,
					MAD_BLOCK_SIZE, &p_mad );
				if( status != IB_SUCCESS )
					break;

				__format_dm_get( p_results, p_iou, p_iou->p_config_path,
					IB_MAD_ATTR_IO_CONTROLLER_PROFILE, cl_hton32( slot ), p_mad );

				/* Chain the MAD up. */
				p_mad->p_next = p_mad_list;
				p_mad_list = p_mad;

				cl_atomic_inc( &p_results->p_svc->query_cnt );
			}
		}
		if( !p_mad )
		{
			/* No IOCs installed in this IOU, or failed to get MAD. */
			cl_fmap_remove_item( &p_results->iou_map, &p_iou->map_item );
			__put_iou( gp_ioc_pnp, p_iou );
		}
	}

	/* Trap the case where there are no queries to send. */
	if( !p_mad_list )
	{
		AL_EXIT( AL_DBG_PNP );
		return IB_NOT_DONE;
	}

	status = ib_send_mad( p_results->p_svc->h_mad_svc, p_mad_list, &p_mad );
	if( status != IB_SUCCESS )
	{
		/* If some of the MADs were sent wait for their completion. */
		if( p_mad_list != p_mad )
			status = IB_SUCCESS;

		while( p_mad )
		{
			p_mad_list = p_mad->p_next;
			p_mad->p_next = NULL;
			ib_put_mad( p_mad );
			if( !cl_atomic_dec( &p_results->p_svc->query_cnt ) &&
				status == IB_SUCCESS )
			{
				cl_async_proc_queue( gp_async_pnp_mgr,
					&p_results->async_item );
			}
			p_mad = p_mad_list;
		}
	}
	AL_EXIT( AL_DBG_PNP );
	return status;
}


static ib_api_status_t
__query_svc_entries(
	IN				ioc_sweep_results_t* const	p_results )
{
	ib_api_status_t		status;
	cl_fmap_item_t		*p_iou_item;
	cl_map_item_t		*p_ioc_item;
	iou_node_t			*p_iou;
	iou_ioc_t			*p_ioc;
	uint8_t				i;
	uint32_t			attr_mod;
	ib_mad_element_t	*p_mad, *p_mad_list = NULL;

	AL_ENTER( AL_DBG_PNP );

	for( p_iou_item = cl_fmap_head( &p_results->iou_map );
		p_iou_item != cl_fmap_end( &p_results->iou_map );
		p_iou_item = cl_fmap_next( p_iou_item ) )
	{
		p_iou = PARENT_STRUCT( p_iou_item, iou_node_t, map_item );
		CL_ASSERT( cl_qmap_count( &p_iou->ioc_map ) );
		CL_ASSERT( cl_fmap_count( &p_iou->path_map ) );
		CL_ASSERT( p_iou->p_config_path );

		for( p_ioc_item = cl_qmap_head( &p_iou->ioc_map );
			p_ioc_item != cl_qmap_end( &p_iou->ioc_map );
			p_ioc_item = cl_qmap_next( p_ioc_item ) )
		{
			p_ioc = PARENT_STRUCT( p_ioc_item, iou_ioc_t, map_item );
			CL_ASSERT( p_ioc->p_iou == p_iou );

			for( i = 0; i < p_ioc->profile.num_svc_entries; i += 4 )
			{
				status = ib_get_mad( p_results->p_svc->pool_key,
					MAD_BLOCK_SIZE, &p_mad );
				if( status != IB_SUCCESS )
					break;

				attr_mod = (((uint32_t)p_ioc->slot) << 16) | i;
				if( (i + 3) > p_ioc->profile.num_svc_entries )
					attr_mod |= ((p_ioc->profile.num_svc_entries - 1) << 8);
				else
					attr_mod |= ((i + 3) << 8);

				__format_dm_get( p_results, p_ioc, p_iou->p_config_path,
					IB_MAD_ATTR_SERVICE_ENTRIES, cl_hton32( attr_mod ),
					p_mad );

				/* Chain the MAD up. */
				p_mad->p_next = p_mad_list;
				p_mad_list = p_mad;

				cl_atomic_inc( &p_ioc->ref_cnt );
				cl_atomic_inc( &p_results->p_svc->query_cnt );
			}
		}
	}

	/* Trap the case where there are no queries to send. */
	if( !p_mad_list )
	{
		AL_EXIT( AL_DBG_PNP );
		return IB_NOT_DONE;
	}

	status = ib_send_mad( p_results->p_svc->h_mad_svc, p_mad_list, &p_mad );
	if( status != IB_SUCCESS )
	{
		/* If some of the MADs were sent wait for their completion. */
		if( p_mad_list != p_mad )
			status = IB_SUCCESS;

		while( p_mad )
		{
			p_mad_list = p_mad->p_next;
			p_mad->p_next = NULL;
			p_ioc = (iou_ioc_t*)p_mad->context2;
			cl_atomic_dec( &p_ioc->ref_cnt );
			ib_put_mad( p_mad );
			if( !cl_atomic_dec( &p_results->p_svc->query_cnt ) &&
				status == IB_SUCCESS )
			{
				cl_async_proc_queue( gp_async_pnp_mgr,
					&p_results->async_item );
			}
			p_mad = p_mad_list;
		}
	}
	AL_EXIT( AL_DBG_PNP );
	return status;
}


static void
__update_results(
	IN				ioc_sweep_results_t* const	p_results )
{
	cl_fmap_t			iou_map1, iou_map2;
	cl_fmap_item_t		*p_item1, *p_item2;
	iou_node_t			*p_iou1, *p_iou2;

	AL_ENTER( AL_DBG_PNP );

	cl_fmap_init( &iou_map1, __iou_cmp );
	cl_fmap_init( &iou_map2, __iou_cmp );

	/*
	 * No need to lock on the sweep map since all accesses are serialized
	 * by the PnP thread.
	 */
	cl_fmap_delta( &gp_ioc_pnp->sweep_map, &p_results->iou_map,
		&iou_map1, &iou_map2 );
	/* sweep_map and iou_map now contain exactly the same items. */
	p_item1 = cl_fmap_head( &gp_ioc_pnp->sweep_map );
	p_item2 = cl_fmap_head( &p_results->iou_map );
	while( p_item1 != cl_fmap_end( &gp_ioc_pnp->sweep_map ) )
	{
		CL_ASSERT( p_item2 != cl_fmap_end( &p_results->iou_map ) );
		p_iou1 = PARENT_STRUCT( p_item1, iou_node_t, map_item );
		p_iou2 = PARENT_STRUCT( p_item2, iou_node_t, map_item );
		CL_ASSERT( p_iou1->guid == p_iou2->guid );

		/*
		 * Merge the IOC maps - this leaves all duplicates in
		 * p_iou2->ioc_map.
		 */
		cl_qmap_merge( &p_iou1->ioc_map, &p_iou2->ioc_map );

		/*
		 * Merge the path maps - this leaves all duplicates in
		 * p_iou2->path_map
		 */
		cl_fmap_merge( &p_iou1->path_map, &p_iou2->path_map );

		/* Return the duplicate IOU (and whatever duplicate paths and IOCs) */
		cl_fmap_remove_item( &p_results->iou_map, p_item2 );
		__put_iou( gp_ioc_pnp, p_iou2 );

		p_item1 = cl_fmap_next( p_item1 );
		p_item2 = cl_fmap_head( &p_results->iou_map );
	}
	CL_ASSERT( !cl_fmap_count( &p_results->iou_map ) );

	/* Merge in the unique items. */
	cl_fmap_merge( &gp_ioc_pnp->sweep_map, &iou_map1 );
	CL_ASSERT( !cl_fmap_count( &iou_map1 ) );
	cl_fmap_merge( &gp_ioc_pnp->sweep_map, &iou_map2 );
	CL_ASSERT( !cl_fmap_count( &iou_map2 ) );

	AL_EXIT( AL_DBG_PNP );
	return;
}


static void
__ioc_async_cb(
	IN				cl_async_proc_item_t		*p_item )
{
	cl_status_t		status;
	cl_fmap_t		old_ious, new_ious;
	uint32_t		interval=0;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_item == &gp_ioc_pnp->async_item );
	UNUSED_PARAM( p_item );

	CL_ASSERT( !gp_ioc_pnp->query_cnt );

	cl_fmap_init( &old_ious, __iou_cmp );
	cl_fmap_init( &new_ious, __iou_cmp );
	cl_fmap_delta(
		&gp_ioc_pnp->iou_map, &gp_ioc_pnp->sweep_map, &new_ious, &old_ious );

	/* For each duplicate IOU, report changes in IOCs or paths. */
	__change_ious( &gp_ioc_pnp->iou_map, &gp_ioc_pnp->sweep_map );

	/* Report all new IOUs. */
	__add_ious( &gp_ioc_pnp->iou_map, &new_ious, NULL );
	CL_ASSERT( !cl_fmap_count( &new_ious ) );

	/* Report all removed IOUs. */
	__remove_ious( &old_ious );
	CL_ASSERT( !cl_fmap_count( &old_ious ) );

	/* Reset the sweep timer.
	 * 0 == No IOC polling.
	 * 1 == IOC poll on demand.
	 * > 1 == IOC poll every g_ioc_poll_interval milliseconds.
	 */
	if( g_ioc_poll_interval == 1 && gp_ioc_pnp->reSweep > 0 )
		interval = IOC_RESWEEP_WAIT;
	else if( g_ioc_poll_interval > 1 )
		interval = g_ioc_poll_interval;

	if( interval > 0 )
	{
		status = cl_timer_start( &gp_ioc_pnp->sweep_timer, g_ioc_poll_interval );
		CL_ASSERT( status == CL_SUCCESS );
	}

	/* Release the reference we took in the timer callback. */
	deref_al_obj( &gp_ioc_pnp->obj );

	AL_EXIT( AL_DBG_PNP );
}


static void
__change_ious(
	IN				cl_fmap_t* const			p_cur_ious,
	IN				cl_fmap_t* const			p_dup_ious )
{
	cl_fmap_t		new_paths, old_paths;
	cl_qmap_t		new_iocs, old_iocs;
	cl_fmap_item_t	*p_item1, *p_item2;
	iou_node_t		*p_iou1, *p_iou2;

	AL_ENTER( AL_DBG_PNP );

	cl_fmap_init( &new_paths, __path_cmp );
	cl_fmap_init( &old_paths, __path_cmp );
	cl_qmap_init( &new_iocs );
	cl_qmap_init( &old_iocs );

	p_item1 = cl_fmap_head( p_cur_ious );
	p_item2 = cl_fmap_head( p_dup_ious );
	while( p_item1 != cl_fmap_end( p_cur_ious ) )
	{
		p_iou1 = PARENT_STRUCT( p_item1, iou_node_t, map_item );
		p_iou2 = PARENT_STRUCT( p_item2, iou_node_t, map_item );
		CL_ASSERT( p_iou1->guid == p_iou2->guid );

		/* Figure out what changed. */
		cl_fmap_delta(
			&p_iou1->path_map, &p_iou2->path_map, &new_paths, &old_paths );
		cl_qmap_delta(
			&p_iou1->ioc_map, &p_iou2->ioc_map, &new_iocs, &old_iocs );

		/*
		 * Report path changes before IOC changes so that new IOCs
		 * report up-to-date paths.  Report new paths before removing
		 * old ones to minimize the chance of disruption of service - 
		 * i.e. the last path being removed before an alternate is available.
		 */
		__add_paths( p_iou1, &p_iou1->ioc_map, &new_paths, NULL );
		CL_ASSERT( !cl_fmap_count( &new_paths ) );

		__remove_paths( &p_iou1->ioc_map, &old_paths );
		CL_ASSERT( !cl_fmap_count( &old_paths ) );

		/* Report IOCs. */
		__add_iocs( p_iou1, &new_iocs, NULL );
		CL_ASSERT( !cl_qmap_count( &new_iocs ) );

		__remove_iocs( p_iou1, &old_iocs );
		CL_ASSERT( !cl_qmap_count( &old_iocs ) );

		/* Done with the duplicate IOU.  Return it to the pool */
		cl_fmap_remove_item( p_dup_ious, p_item2 );
		__put_iou( gp_ioc_pnp, p_iou2 );

		p_item1 = cl_fmap_next( p_item1 );
		p_item2 = cl_fmap_head( p_dup_ious );
	}
	CL_ASSERT( !cl_fmap_count( p_dup_ious ) );

	AL_EXIT( AL_DBG_PNP );
}


static void
__add_ious(
	IN				cl_fmap_t* const			p_cur_ious,
	IN				cl_fmap_t* const			p_new_ious,
	IN				al_pnp_t* const				p_reg OPTIONAL )
{
	cl_fmap_item_t	*p_item;
	iou_node_t		*p_iou;

	AL_ENTER( AL_DBG_PNP );

	p_item = cl_fmap_head( p_new_ious );
	while( p_item != cl_fmap_end( p_new_ious ) )
	{
		p_iou = PARENT_STRUCT( p_item, iou_node_t, map_item );

		/* Report the IOU addition. */
		__report_iou_add( p_iou, p_reg );

		p_item = cl_fmap_next( p_item );
	}

	if( p_cur_ious != p_new_ious )
	{
		cl_fmap_merge( p_cur_ious, p_new_ious );
		CL_ASSERT( !cl_fmap_count( p_new_ious ) );
	}

	AL_EXIT( AL_DBG_PNP );
}


static void
__remove_ious(
	IN				cl_fmap_t* const			p_old_ious )
{
	cl_fmap_item_t	*p_item;
	iou_node_t		*p_iou;

	AL_ENTER( AL_DBG_PNP );

	p_item = cl_fmap_head( p_old_ious );
	while( p_item != cl_fmap_end( p_old_ious ) )
	{
		p_iou = PARENT_STRUCT( p_item, iou_node_t, map_item );

		/* Report the IOU removal. */
		__report_iou_remove( p_iou );

		cl_fmap_remove_item( p_old_ious, p_item );
		__put_iou( gp_ioc_pnp, p_iou );
		p_item = cl_fmap_head( p_old_ious );
	}
	CL_ASSERT( !cl_fmap_count( p_old_ious ) );

	AL_EXIT( AL_DBG_PNP );
}


static void
__add_iocs(
	IN				iou_node_t* const			p_iou,
	IN				cl_qmap_t* const			p_new_iocs,
	IN				al_pnp_t* const				p_reg OPTIONAL )
{
	cl_map_item_t	*p_item;
	iou_ioc_t		*p_ioc;

	AL_ENTER( AL_DBG_PNP );

	p_item = cl_qmap_head( p_new_iocs );
	while( p_item != cl_qmap_end( p_new_iocs ) )
	{
		p_ioc = PARENT_STRUCT( p_item, iou_ioc_t, map_item );

		/* Report the IOU addition. */
		__report_ioc_add( p_iou, p_ioc, p_reg );

		p_item = cl_qmap_next( p_item );
	}

	if( p_new_iocs != &p_iou->ioc_map )
	{
		cl_qmap_merge( &p_iou->ioc_map, p_new_iocs );
		CL_ASSERT( !cl_qmap_count( p_new_iocs ) );
	}
	AL_EXIT( AL_DBG_PNP );
}


static void
__remove_iocs(
	IN				iou_node_t* const			p_iou,
	IN				cl_qmap_t* const			p_old_iocs )
{
	cl_map_item_t	*p_item;
	iou_ioc_t		*p_ioc;

	AL_ENTER( AL_DBG_PNP );

	p_item = cl_qmap_tail( p_old_iocs );
	while( p_item != cl_qmap_end( p_old_iocs ) )
	{
		p_ioc = PARENT_STRUCT( p_item, iou_ioc_t, map_item );

		/* Report the IOC removal. */
		__report_ioc_remove( p_iou, p_ioc );

		cl_qmap_remove_item( p_old_iocs, p_item );
		__put_ioc( gp_ioc_pnp, p_ioc );
		p_item = cl_qmap_tail( p_old_iocs );
	}
	CL_ASSERT( !cl_qmap_count( p_old_iocs ) );

	AL_EXIT( AL_DBG_PNP );
}


static void
__add_paths(
	IN				iou_node_t* const			p_iou,
	IN				cl_qmap_t* const			p_ioc_map,
	IN				cl_fmap_t* const			p_new_paths,
	IN				al_pnp_t* const				p_reg OPTIONAL )
{
	cl_map_item_t	*p_ioc_item;
	cl_fmap_item_t	*p_item;
	iou_ioc_t		*p_ioc;
	iou_path_t		*p_path;

	AL_ENTER( AL_DBG_PNP );

	p_item = cl_fmap_head( p_new_paths );
	while( p_item != cl_fmap_end( p_new_paths ) )
	{
		p_path = PARENT_STRUCT( p_item, iou_path_t, map_item );

		/* Report the path to all IOCs. */
		for( p_ioc_item = cl_qmap_head( p_ioc_map );
			p_ioc_item != cl_qmap_end( p_ioc_map );
			p_ioc_item = cl_qmap_next( p_ioc_item ) )
		{
			p_ioc = PARENT_STRUCT( p_ioc_item, iou_ioc_t, map_item );
			__report_path( p_ioc, p_path, IB_PNP_IOC_PATH_ADD, p_reg );
		}

		p_item = cl_fmap_next( p_item );
	}

	ASSERT( &p_iou->path_map != p_new_paths );

	cl_fmap_merge( &p_iou->path_map, p_new_paths );
	CL_ASSERT( !cl_fmap_count( p_new_paths ) );

	AL_EXIT( AL_DBG_PNP );
}


static void
__add_ioc_paths(
	IN				iou_ioc_t* const			p_ioc,
	IN				cl_fmap_t* const			p_new_paths,
	IN				al_pnp_t* const				p_reg OPTIONAL )
{
	cl_fmap_item_t	*p_item;
	iou_path_t		*p_path;

	AL_ENTER( AL_DBG_PNP );

	p_item = cl_fmap_head( p_new_paths );
	while( p_item != cl_fmap_end( p_new_paths ) )
	{
		p_path = PARENT_STRUCT( p_item, iou_path_t, map_item );

		__report_path( p_ioc, p_path, IB_PNP_IOC_PATH_ADD, p_reg );

		p_item = cl_fmap_next( p_item );
	}

	AL_EXIT( AL_DBG_PNP );
}


static void
__remove_paths(
	IN				cl_qmap_t* const			p_ioc_map,
	IN				cl_fmap_t* const			p_old_paths )
{
	cl_map_item_t	*p_ioc_item;
	cl_fmap_item_t	*p_item;
	iou_ioc_t		*p_ioc;
	iou_path_t		*p_path;

	AL_ENTER( AL_DBG_PNP );

	p_item = cl_fmap_tail( p_old_paths );
	while( p_item != cl_fmap_end( p_old_paths ) )
	{
		p_path = PARENT_STRUCT( p_item, iou_path_t, map_item );

		for( p_ioc_item = cl_qmap_tail( p_ioc_map );
			p_ioc_item != cl_qmap_end( p_ioc_map );
			p_ioc_item = cl_qmap_prev( p_ioc_item ) )
		{
			p_ioc = PARENT_STRUCT( p_ioc_item, iou_ioc_t, map_item );
			__report_path( p_ioc, p_path, IB_PNP_IOC_PATH_REMOVE, NULL );
		}

		cl_fmap_remove_item( p_old_paths, p_item );
		__put_path( gp_ioc_pnp, p_path );
		p_item = cl_fmap_tail( p_old_paths );
	}
	CL_ASSERT( !cl_fmap_count( p_old_paths ) );

	AL_EXIT( AL_DBG_PNP );
}


static cl_status_t
__notify_users(
	IN		const	cl_list_item_t* const		p_item,
	IN				al_pnp_ioc_event_t* const	p_event )
{
	ib_api_status_t			status;
	al_pnp_t				*p_reg;
	al_pnp_context_t		*p_context;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_item, al_pnp_t, list_item );

	/* Copy the source record into the user's record. */
	cl_memcpy( p_event->p_user_rec, p_event->p_rec, p_event->rec_size );
	p_event->p_user_rec->h_pnp = p_reg;
	p_event->p_user_rec->pnp_context = (void*)p_reg->obj.context;

	switch( p_event->p_rec->pnp_event )
	{
	case IB_PNP_IOU_ADD:
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOU );
		p_context = pnp_create_context( p_reg, &p_event->p_rec->guid);
		break;

	case IB_PNP_IOU_REMOVE:
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOU );
		/* Lookup the context for this IOU. */
		p_context = pnp_get_context( p_reg, &p_event->p_rec->guid );
		break;

	case IB_PNP_IOC_ADD:
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOC );
		p_context = pnp_create_context( p_reg, &p_event->p_rec->guid);
		break;
	case IB_PNP_IOC_REMOVE:
	case IB_PNP_IOC_PATH_ADD:
	case IB_PNP_IOC_PATH_REMOVE:
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOC );
		p_context = pnp_get_context( p_reg, &p_event->p_rec->guid );
		break;
	default:
		AL_PRINT_EXIT(TRACE_LEVEL_WARNING, AL_DBG_PNP,("Invalid PnP event %#x\n",
			p_event->p_rec->pnp_event));
		return CL_NOT_DONE;
		break;
	}
	if( !p_context )
		return CL_NOT_FOUND;

	p_event->p_user_rec->context = (void*)p_context->context;

	/* Notify user. */
	status = p_reg->pfn_pnp_cb( p_event->p_user_rec );

	/* Update contexts */
	if( status != IB_SUCCESS ||
		p_event->p_rec->pnp_event == IB_PNP_IOU_REMOVE ||
		p_event->p_rec->pnp_event == IB_PNP_IOC_REMOVE )
	{
		cl_fmap_remove_item( &p_reg->context_map, &p_context->map_item );
		cl_free( p_context );
	}
	else
	{
		p_context->context = p_event->p_user_rec->context;
	}

	AL_EXIT( AL_DBG_PNP );
	return CL_NOT_FOUND;
}


static void
__report_iou_add(
	IN				iou_node_t* const			p_iou,
	IN				al_pnp_t* const				p_reg OPTIONAL )
{
	al_pnp_ioc_event_t		event;
	ib_pnp_iou_rec_t		*p_rec, *p_user_rec;

	AL_ENTER( AL_DBG_PNP );

	event.rec_size = sizeof(ib_pnp_iou_rec_t);
	event.rec_size = ROUNDUP( event.rec_size, sizeof(void*) );

	p_rec = cl_zalloc( event.rec_size * 2 );
	if( !p_rec )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to allocate user record.\n") );
		return;
	}
	p_rec->pnp_rec.pnp_event = IB_PNP_IOU_ADD;
	p_rec->pnp_rec.guid = p_iou->guid;
	p_rec->pnp_rec.ca_guid = p_iou->ca_guid;
	
	p_rec->ca_guid = p_iou->ca_guid;
	p_rec->guid = p_iou->guid;
	p_rec->chassis_guid = p_iou->chassis_guid;
	p_rec->vend_id = p_iou->vend_id;
	p_rec->dev_id = p_iou->dev_id;
	p_rec->revision = p_iou->revision;
	cl_memcpy( p_rec->desc, p_iou->desc, sizeof(p_rec->desc) );
	p_user_rec = (ib_pnp_iou_rec_t*)(((uint8_t*)p_rec) + event.rec_size);
	
	event.p_rec = (ib_pnp_rec_t*)p_rec;
	event.p_user_rec = (ib_pnp_rec_t*)p_user_rec;

	if( p_reg )
	{
		if( pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOU )
			__notify_users( &p_reg->list_item, &event );
		else
			__add_iocs( p_iou, &p_iou->ioc_map, p_reg );
	}
	else
	{
		/* Report the IOU to all clients registered for IOU events. */
		cl_qlist_find_from_head( &gp_ioc_pnp->iou_reg_list, __notify_users, &event );

		/* Report IOCs - this will in turn report the paths. */
		__add_iocs( p_iou, &p_iou->ioc_map, NULL );
	}

	cl_free( p_rec );
	AL_EXIT( AL_DBG_PNP );
}


static void
__report_iou_remove(
	IN				iou_node_t* const			p_iou )
{
	al_pnp_ioc_event_t		event;
	ib_pnp_iou_rec_t		rec, user_rec;

	AL_ENTER( AL_DBG_PNP );

	/* Report IOCs - this will in turn report the paths. */
	__remove_iocs( p_iou, &p_iou->ioc_map );

	cl_memclr( &rec, sizeof(ib_pnp_iou_rec_t) );
	rec.pnp_rec.pnp_event = IB_PNP_IOU_REMOVE;
	rec.pnp_rec.guid = p_iou->guid;
	rec.pnp_rec.ca_guid = p_iou->ca_guid;

	event.rec_size = sizeof(ib_pnp_iou_rec_t);
	event.p_rec = (ib_pnp_rec_t*)&rec;
	event.p_user_rec = (ib_pnp_rec_t*)&user_rec;

	/*
	 * Report the IOU to all clients registered for IOU events in
	 * reverse order than ADD notifications.
	 */
	cl_qlist_find_from_tail( &gp_ioc_pnp->iou_reg_list,
		__notify_users, &event );

	AL_EXIT( AL_DBG_PNP );
}


static void
__report_ioc_add(
	IN				iou_node_t* const			p_iou,
	IN				iou_ioc_t* const			p_ioc,
	IN				al_pnp_t* const				p_reg OPTIONAL )
{
	al_pnp_ioc_event_t		event;
	ib_pnp_ioc_rec_t		*p_rec;

	AL_ENTER( AL_DBG_PNP );

	event.rec_size = sizeof(ib_pnp_ioc_rec_t) +
		(sizeof(ib_svc_entry_t) * (p_ioc->profile.num_svc_entries - 1));
	event.rec_size = ROUNDUP( event.rec_size, sizeof(void*) );

	/*
	 * The layout of the pnp record is as follows:
	 *	ib_pnp_rec_t
	 *	ib_svc_entry_t
	 *	ib_ioc_info_t
	 *
	 * This is needed to keep the service entries contiguous to the first
	 * entry in the pnp record.
	 */
	p_rec = (ib_pnp_ioc_rec_t*)cl_zalloc( event.rec_size * 2 );
	if( !p_rec )
		return;

	p_rec->pnp_rec.pnp_event = IB_PNP_IOC_ADD;
	p_rec->pnp_rec.guid = p_ioc->profile.ioc_guid;
	p_rec->pnp_rec.ca_guid = p_ioc->p_iou->ca_guid;
	
	p_rec->ca_guid = p_ioc->p_iou->ca_guid;
	cl_memcpy( p_rec->svc_entry_array, p_ioc->p_svc_entries,
		p_ioc->profile.num_svc_entries * sizeof(ib_svc_entry_t) );
	p_rec->info.chassis_guid = p_iou->chassis_guid;
	p_rec->info.chassis_slot = p_iou->slot;
	p_rec->info.iou_guid = p_iou->guid;
	p_rec->info.iou_slot = p_ioc->slot;
	p_rec->info.profile = p_ioc->profile;

	event.p_rec = (ib_pnp_rec_t*)p_rec;
	event.p_user_rec = (ib_pnp_rec_t*)(((uint8_t*)p_rec) + event.rec_size);

	if( p_reg )
	{
		__notify_users( &p_reg->list_item, &event );
	}
	else
	{
		/* Report the IOC to all clients registered for IOC events. */
		cl_qlist_find_from_head( &gp_ioc_pnp->ioc_reg_list,
			__notify_users, &event );
	}
	cl_free( p_rec );

	/* Report the paths for this IOC only. */
	__add_ioc_paths( p_ioc, &p_iou->path_map, p_reg );

	AL_EXIT( AL_DBG_PNP );
}


static void
__report_ioc_remove(
	IN				iou_node_t* const			p_iou,
	IN				iou_ioc_t* const			p_ioc )
{
	al_pnp_ioc_event_t		event;
	ib_pnp_ioc_rec_t		rec, user_rec;

	AL_ENTER( AL_DBG_PNP );

	UNUSED_PARAM( p_iou );

	cl_memclr( &rec, sizeof(ib_pnp_ioc_rec_t) );
	rec.pnp_rec.pnp_event = IB_PNP_IOC_REMOVE;
	rec.pnp_rec.guid = p_ioc->profile.ioc_guid;
	rec.pnp_rec.ca_guid = p_ioc->p_iou->ca_guid;
	
	event.rec_size = sizeof(ib_pnp_ioc_rec_t);
	event.p_rec = (ib_pnp_rec_t*)&rec;
	event.p_user_rec = (ib_pnp_rec_t*)&user_rec;

	/*
	 * Report the IOC removal to all clients registered for IOC events in
	 * reverse order than ADD notifications.
	 */
	cl_qlist_find_from_tail( &gp_ioc_pnp->ioc_reg_list,
		__notify_users, &event );

	AL_EXIT( AL_DBG_PNP );
}


static void
__report_path(
	IN				iou_ioc_t* const			p_ioc,
	IN				iou_path_t* const			p_path,
	IN				ib_pnp_event_t				pnp_event,
	IN				al_pnp_t* const				p_reg OPTIONAL )
{
	al_pnp_ioc_event_t		event;
	ib_pnp_ioc_path_rec_t	*p_rec;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( pnp_event == IB_PNP_IOC_PATH_ADD ||
		pnp_event == IB_PNP_IOC_PATH_REMOVE );

	event.rec_size = sizeof(ib_pnp_ioc_path_rec_t);
	event.rec_size = ROUNDUP( event.rec_size, sizeof(void*) );

	/*
	 * The layout of the pnp record is as follows:
	 *	ib_pnp_rec_t
	 *	ib_svc_entry_t
	 *	ib_ioc_info_t
	 *
	 * This is needed to keep the service entries contiguous to the first
	 * entry in the pnp record.
	 */
	p_rec = (ib_pnp_ioc_path_rec_t*)cl_zalloc( event.rec_size * 2 );
	if( !p_rec )
		return;
	p_rec->pnp_rec.pnp_event = pnp_event;
	p_rec->pnp_rec.guid = p_ioc->profile.ioc_guid;
	p_rec->pnp_rec.ca_guid = p_path->ca_guid;
	
	p_rec->ca_guid = p_path->ca_guid;
	p_rec->port_guid = p_path->port_guid;
	p_rec->path = p_path->rec;

	event.p_rec = (ib_pnp_rec_t*)p_rec;
	event.p_user_rec = (ib_pnp_rec_t*)(((uint8_t*)p_rec) + event.rec_size);

	/* Report the IOC to all clients registered for IOC events. */
	if( p_reg )
	{
		__notify_users( &p_reg->list_item, &event );
	}
	else
	{
		if( pnp_event == IB_PNP_IOC_PATH_ADD )
		{
			cl_qlist_find_from_head( &gp_ioc_pnp->ioc_reg_list,
				__notify_users, &event );
		}
		else
		{
			cl_qlist_find_from_tail( &gp_ioc_pnp->ioc_reg_list,
				__notify_users, &event );
		}
	}

	cl_free( p_rec );

	AL_EXIT( AL_DBG_PNP );
}


void
ioc_pnp_process_reg(
	IN				cl_async_proc_item_t		*p_item )
{
	al_pnp_t	*p_reg;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_item, al_pnp_t, async_item );

	/* Add the registrant to the list. */
	switch( pnp_get_class( p_reg->pnp_class ) )
	{
	case IB_PNP_IOU:
		cl_qlist_insert_tail( &gp_ioc_pnp->iou_reg_list, &p_reg->list_item );
		break;

	case IB_PNP_IOC:
		cl_qlist_insert_tail( &gp_ioc_pnp->ioc_reg_list, &p_reg->list_item );
		break;

	default:
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOU ||
			pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOC );
	}

	/* Generate all relevant events for the registration. */
	__add_ious( &gp_ioc_pnp->iou_map, &gp_ioc_pnp->iou_map, p_reg );

	/* Notify the user that the registration is complete. */
	pnp_reg_complete( p_reg );

	/* Release the reference taken in init_al_obj. */
	deref_al_obj( &p_reg->obj );

	AL_EXIT( AL_DBG_PNP );
}


void
ioc_pnp_process_dereg(
	IN				cl_async_proc_item_t		*p_item )
{
	al_pnp_t	*p_reg;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_item, al_pnp_t, dereg_item );

	/* Remove the registration information from the list. */
	switch( pnp_get_class( p_reg->pnp_class ) )
	{
	case IB_PNP_IOU:
		cl_qlist_remove_item( &gp_ioc_pnp->iou_reg_list, &p_reg->list_item );
		break;

	case IB_PNP_IOC:
		cl_qlist_remove_item( &gp_ioc_pnp->ioc_reg_list, &p_reg->list_item );
		break;

	default:
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOU ||
			pnp_get_class( p_reg->pnp_class ) == IB_PNP_IOC );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid PnP registartion type.\n") );
	}

	/* Release the reference we took for processing the deregistration. */
	deref_al_obj( &p_reg->obj );

	AL_EXIT( AL_DBG_PNP );
}






