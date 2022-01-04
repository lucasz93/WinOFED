/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#include "al.h"
#include "al_common.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_mgr_shared.tmh"
#endif
#include "al_ci_ca.h"
#include "ib_common.h"
#include "al_mgr.h"
#include "al_pnp.h"

ib_al_handle_t			gh_al = NULL;
ib_pool_handle_t		gh_mad_pool = NULL;
al_mgr_t				*gp_al_mgr = NULL;
cl_async_proc_t			*gp_async_proc_mgr = NULL;
cl_async_proc_t			*gp_async_pnp_mgr = NULL;



void
print_al_obj(
	IN				al_obj_t * const			p_obj )
{
	CL_ASSERT( p_obj );

	UNUSED_PARAM( p_obj );

	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
		("AL object %016Ix(%s), parent: %016Ix ref_cnt: %d\n",
		(LONG_PTR)p_obj, ib_get_obj_type( p_obj ),
		(LONG_PTR)p_obj->p_parent_obj, p_obj->ref_cnt) );
}


void
print_al_objs(
	IN		const	ib_al_handle_t				h_al )
{
	al_obj_t		*p_obj;
	cl_list_item_t	*p_list_item;

	if( !gp_al_mgr )
		return;

	/* Display all access layer objects. */
	for( p_list_item = cl_qlist_head( &gp_al_mgr->al_obj_list );
		 p_list_item != cl_qlist_end( &gp_al_mgr->al_obj_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_obj = PARENT_STRUCT( p_list_item, al_obj_t, list_item );
		if( !h_al || p_obj->h_al == h_al )
			print_al_obj( p_obj );
	}
}



void
print_tail_al_objs()
{
	al_obj_t		*p_obj;
	cl_list_item_t	*p_list_item;
	int				count = 3;

	if( !gp_al_mgr )
		return;

	/* Display all access layer objects. */
	for( p_list_item = cl_qlist_tail( &gp_al_mgr->al_obj_list );
		 p_list_item != cl_qlist_end( &gp_al_mgr->al_obj_list ) && count;
		 p_list_item = cl_qlist_prev( p_list_item ) )
	{
		p_obj = PARENT_STRUCT( p_list_item, al_obj_t, list_item );
		print_al_obj( p_obj );
		count--;
	}
}



/*
 * Search all available CI CAs in the system to see if one exists with the
 * given GUID.
 */
al_ci_ca_t*
find_ci_ca(
	IN		const	ib_net64_t					ci_ca_guid )
{
	cl_list_item_t		*p_list_item;
	al_ci_ca_t			*p_ci_ca;

	AL_ENTER( AL_DBG_MGR );

	for( p_list_item = cl_qlist_head( &gp_al_mgr->ci_ca_list );
		 p_list_item != cl_qlist_end( &gp_al_mgr->ci_ca_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_ci_ca = PARENT_STRUCT( p_list_item, al_ci_ca_t, list_item );
		if( p_ci_ca->verbs.guid == ci_ca_guid &&
			p_ci_ca->obj.state == CL_INITIALIZED )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MGR,
				("find_ci_ca:CA guid %I64x.\n", ci_ca_guid) );
			AL_EXIT( AL_DBG_MGR );
			return p_ci_ca;
		}
	}

	AL_EXIT( AL_DBG_MGR );
	return NULL;
}



al_ci_ca_t*
acquire_ci_ca(
	IN		const	ib_net64_t					ci_ca_guid,
	IN		const	ib_ca_handle_t				h_ca )
{
	al_ci_ca_t			*p_ci_ca;

	cl_spinlock_acquire( &gp_al_mgr->obj.lock );
	p_ci_ca = find_ci_ca( ci_ca_guid );
	if( !p_ci_ca )
	{
		cl_spinlock_release( &gp_al_mgr->obj.lock );
		return NULL;
	}

	add_ca( p_ci_ca, h_ca );
	cl_spinlock_release( &gp_al_mgr->obj.lock );
	return p_ci_ca;
}



void
release_ci_ca(
	IN		const	ib_ca_handle_t				h_ca )
{
	AL_ENTER( AL_DBG_MGR );
	remove_ca( h_ca );
	AL_EXIT( AL_DBG_MGR );
}



void
add_ci_ca(
	IN				al_ci_ca_t* const			p_ci_ca )
{
	AL_ENTER( AL_DBG_MGR );
	cl_spinlock_acquire( &gp_al_mgr->obj.lock );
	cl_qlist_insert_tail( &gp_al_mgr->ci_ca_list, &p_ci_ca->list_item );
	ref_al_obj( &gp_al_mgr->obj );
	cl_spinlock_release( &gp_al_mgr->obj.lock );
	AL_EXIT( AL_DBG_MGR );
}


void
remove_ci_ca(
	IN				al_ci_ca_t* const			p_ci_ca )
{
	AL_ENTER( AL_DBG_MGR );
	cl_spinlock_acquire( &gp_al_mgr->obj.lock );
	cl_qlist_remove_item( &gp_al_mgr->ci_ca_list, &p_ci_ca->list_item );
	cl_spinlock_release( &gp_al_mgr->obj.lock );
	deref_al_obj( &gp_al_mgr->obj );
	AL_EXIT( AL_DBG_MGR );
}



ib_ca_handle_t
acquire_ca(
	IN		const	ib_net64_t					ci_ca_guid )
{
	al_ci_ca_t			*p_ci_ca;

	cl_spinlock_acquire( &gp_al_mgr->obj.lock );
	p_ci_ca = find_ci_ca( ci_ca_guid );
	if( !p_ci_ca )
	{
		cl_spinlock_release( &gp_al_mgr->obj.lock );
		return NULL;
	}

	ref_al_obj( &p_ci_ca->h_ca->obj );
	cl_spinlock_release( &gp_al_mgr->obj.lock );
	return p_ci_ca->h_ca;
}



#define SEARCH_CA_GUID		(1)
#define SEARCH_PORT_GUID	(2)

/*
 * Return the GUID of CA with the given port GID.
 */

static ib_api_status_t
__get_guid_by_gid (
	IN				ib_al_handle_t				h_al,
	IN		const	ib_gid_t* const				p_gid,
	IN		const	uintn_t						type,
		OUT			ib_net64_t* const			p_guid )
{
	ib_net64_t			*p_guid_array = NULL;
	uint32_t			size;
	uintn_t				ca_ind, port_ind, gid_ind, ca_cnt;
	ib_api_status_t		status = IB_SUCCESS;
	ib_ca_attr_t		*p_ca_attr = NULL;
	ib_port_attr_t		*p_port_attr = NULL;

	AL_ENTER( AL_DBG_MGR );

	CL_ASSERT( h_al && p_gid && p_guid );

	/* Get the number of CA GUIDs. */
	ca_cnt = 0;
	p_guid_array = NULL;
	status = ib_get_ca_guids( h_al, p_guid_array, &ca_cnt );
	if( status != IB_INSUFFICIENT_MEMORY )
	{
		if( status == IB_SUCCESS )
		{
			status = IB_NOT_FOUND;	/* No CAs in the system */
		}
		goto end;
	}

	/* Allocate an array to store the CA GUIDs. */
	p_guid_array = cl_malloc( sizeof( ib_net64_t ) * ca_cnt );
	if( !p_guid_array )
	{
		status = IB_INSUFFICIENT_MEMORY;
		goto end;
	}

	/* Get the list of CA GUIDs in the system. */
	status = ib_get_ca_guids( h_al, p_guid_array, &ca_cnt );
	if( status != IB_SUCCESS )
		goto end;

	/* Query each CA. */
	size = 0;
	p_ca_attr = NULL;
	for( ca_ind = 0; ca_ind < ca_cnt; ca_ind++ )
	{
		/* Query the CA and port information. */
		status = ib_query_ca_by_guid( h_al, p_guid_array[ca_ind],
			p_ca_attr, &size );

		if( status == IB_INSUFFICIENT_MEMORY )
		{
			/* Allocate a larger buffer and requery. */
			if( p_ca_attr )
				cl_free( p_ca_attr );

			p_ca_attr = cl_malloc( size );
			if( !p_ca_attr )
			{
				status = IB_INSUFFICIENT_MEMORY;
				goto end;
			}

			status = ib_query_ca_by_guid( h_al, p_guid_array[ca_ind],
				p_ca_attr, &size );
		}

		if( status != IB_SUCCESS )
			goto end;

		/* Try to match the GID with one of the port's GIDs. */
        CL_ASSERT(p_ca_attr != NULL);
		status = IB_NOT_FOUND;
		for( port_ind = 0; port_ind < p_ca_attr->num_ports; port_ind++ )
		{
			p_port_attr = &p_ca_attr->p_port_attr[port_ind];

			for( gid_ind = 0; gid_ind < p_port_attr->num_gids; gid_ind++ )
			{
				if( !cl_memcmp( &p_port_attr->p_gid_table[gid_ind], p_gid,
					sizeof( ib_gid_t ) ) )
				{
					if ( type == SEARCH_CA_GUID )
						*p_guid = p_guid_array[ca_ind];
					else
						*p_guid = p_port_attr->port_guid;
					status = IB_SUCCESS;
					goto end;
				}
			}
		}
	}

end:
	if ( p_ca_attr )
		cl_free ( p_ca_attr );
	if ( p_guid_array )
		cl_free( p_guid_array );

	AL_EXIT( AL_DBG_MGR );
	return status;
}



ib_api_status_t
ib_get_ca_by_gid(
	IN				ib_al_handle_t				h_al,
	IN		const	ib_gid_t* const				p_gid,
		OUT			ib_net64_t* const			p_ca_guid )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MGR );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_gid || !p_ca_guid )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = __get_guid_by_gid( h_al, p_gid, SEARCH_CA_GUID, p_ca_guid );

	AL_EXIT( AL_DBG_MGR );
	return status;
}



ib_api_status_t
ib_get_port_by_gid(
	IN				ib_al_handle_t				h_al,
	IN		const	ib_gid_t* const				p_gid,
		OUT			ib_net64_t* const			p_port_guid )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MGR );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_gid || !p_port_guid )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = __get_guid_by_gid( h_al, p_gid, SEARCH_PORT_GUID, p_port_guid );

	AL_EXIT( AL_DBG_MGR );
	return status;
}



/*
 * Return the GUIDs of all CAs the system.
 */
ib_api_status_t
ib_get_ca_guids(
	IN				ib_al_handle_t				h_al,
		OUT			ib_net64_t* const			p_guid_array OPTIONAL,
	IN	OUT			size_t* const				p_guid_cnt )
{
	cl_list_item_t		*p_list_item;
	al_ci_ca_t			*p_ci_ca;
	uintn_t				guid_cnt;

	AL_ENTER( AL_DBG_MGR );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_guid_cnt )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Prevent CA additions or removals. */
	cl_spinlock_acquire( &gp_al_mgr->obj.lock );

	/*
	 * Count the number of GUIDs available.  Allow CA
	 * additions or removals and maintain the count.
	 */
	guid_cnt = cl_qlist_count( &gp_al_mgr->ci_ca_list );

	/* Check if a GUID array of sufficient size was provided. */
	if( !p_guid_array || (*p_guid_cnt < guid_cnt) )
	{
		/* Array too small. */
		cl_spinlock_release( &gp_al_mgr->obj.lock );

		/* Return the actual count. */
		*p_guid_cnt = guid_cnt;

		AL_EXIT( AL_DBG_MGR );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Return the actual count. */
	*p_guid_cnt = guid_cnt;

	/* Copy the GUIDs into the array. */
	guid_cnt = 0;
	for( p_list_item = cl_qlist_head( &gp_al_mgr->ci_ca_list );
		 p_list_item != cl_qlist_end( &gp_al_mgr->ci_ca_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_ci_ca = PARENT_STRUCT( p_list_item, al_ci_ca_t, list_item );
		p_guid_array[guid_cnt++] = p_ci_ca->verbs.guid;
	}

	/* Allow CA additions or removals. */
	cl_spinlock_release( &gp_al_mgr->obj.lock );

	AL_EXIT( AL_DBG_MGR );
	return IB_SUCCESS;
}



static boolean_t
__match_ca_attr(
	IN				al_ci_ca_t * const			p_ci_ca,
	IN		const	uint64_t					attr_mask )
{
	boolean_t		match;

	ci_ca_lock_attr( p_ci_ca );
	
	/* We don't match any attributes for CA's currently. */
	UNUSED_PARAM( attr_mask );
	match = TRUE;

	ci_ca_unlock_attr( p_ci_ca );

	return match;
}



static ib_api_status_t
__get_ca_guid(
	IN		const	uint32_t					index,
	IN		const	uint64_t					attr_mask,
		OUT			ib_net64_t* const			p_guid )
{
	uint32_t			ca_index;
	cl_list_item_t		*p_list_item;
	al_ci_ca_t			*p_ci_ca;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MGR );

	/* Prevent CA additions or removals. */
	cl_spinlock_acquire( &gp_al_mgr->obj.lock );

	/* Check for a valid index. */
	if( index != IB_ANY_INDEX &&
		index >= cl_qlist_count( &gp_al_mgr->ci_ca_list ) )
	{
		cl_spinlock_release( &gp_al_mgr->obj.lock );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_INDEX\n") );
		return IB_INVALID_INDEX;
	}

	/*
	 * Find the CA at the correct index and check its attributes.  Optimize
	 * for the "any index" case.
	 */
	status = IB_NO_MATCH;
	ca_index = 0;
	for( p_list_item = cl_qlist_head( &gp_al_mgr->ci_ca_list );
		 p_list_item != cl_qlist_end( &gp_al_mgr->ci_ca_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_ci_ca = PARENT_STRUCT( p_list_item, al_ci_ca_t, list_item );
		
		if( (ca_index == index || index == IB_ANY_INDEX) &&
			__match_ca_attr( p_ci_ca, attr_mask ) )
		{
			*p_guid = p_ci_ca->verbs.guid;
			status = IB_SUCCESS;
			break;
		}
		ca_index++;
	}

	cl_spinlock_release( &gp_al_mgr->obj.lock );

	AL_EXIT( AL_DBG_MGR );
	return status;
}



static boolean_t
__match_port_attr(
	IN		const	ib_port_attr_t * const		p_port_attr,
	IN		const	uint64_t					attr_mask )
{
	if( attr_mask & IB_DEV_PORT_ACTIVE )
		return( p_port_attr->link_state == IB_LINK_ACTIVE );

	return TRUE;
}



static ib_api_status_t
__get_port_guid(
	IN		const	uint32_t					index,
	IN		const	uint64_t					attr_mask,
		OUT			ib_net64_t* const			p_guid )
{
	uint32_t			port_index, i;
	cl_list_item_t		*p_list_item;
	al_ci_ca_t			*p_ci_ca;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MGR );

	/* Prevent CA additions or removals. */
	cl_spinlock_acquire( &gp_al_mgr->obj.lock );

	/*
	 * Find the port at the correct index and check its attributes.  Optimize
	 * for the "any index" case.
	 */
	status = IB_NO_MATCH;
	port_index = 0;
	for( p_list_item = cl_qlist_head( &gp_al_mgr->ci_ca_list );
		 p_list_item != cl_qlist_end( &gp_al_mgr->ci_ca_list ) &&
		 status != IB_SUCCESS;
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_ci_ca = PARENT_STRUCT( p_list_item, al_ci_ca_t, list_item );

		/* Check all ports on this CA. */
		ci_ca_lock_attr( p_ci_ca );
		for( i = 0; i < p_ci_ca->p_pnp_attr->num_ports; i++ )
		{
			/* Check the attributes. */
			if( (port_index == index || index == IB_ANY_INDEX) &&
				__match_port_attr( &p_ci_ca->p_pnp_attr->p_port_attr[i],
				attr_mask ) )
			{
				*p_guid = p_ci_ca->verbs.guid;
				status = IB_SUCCESS;
				break;
			}
			port_index++;
		}	
		ci_ca_unlock_attr( p_ci_ca );
	}
	cl_spinlock_release( &gp_al_mgr->obj.lock );

	/*
	 * See if the index was valid.  We need to perform this check at the
	 * end of the routine, since we don't know how many ports we have.
	 */
	if( p_list_item == cl_qlist_end( &gp_al_mgr->ci_ca_list ) &&
		index != IB_ANY_INDEX )
	{
		status = IB_INVALID_INDEX;
	}

	AL_EXIT( AL_DBG_MGR );
	return status;
}



ib_api_status_t
ib_get_guid(
	IN				ib_al_handle_t				h_al,
	IN		const	uint32_t					index,
	IN		const	ib_pnp_class_t				device_type,
	IN		const	uint64_t					attr_mask,
		OUT			ib_net64_t* const			p_guid )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MGR );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_guid )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	switch( device_type )
	{
	case IB_PNP_CA:
		status = __get_ca_guid( index, attr_mask, p_guid );
		break;

	case IB_PNP_PORT:
		status = __get_port_guid( index, attr_mask, p_guid );
		break;

	case IB_PNP_IOC:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("IOC GUIDs not supported at this time\n") );
		return IB_UNSUPPORTED;

	default:
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SETTING\n") );
		return IB_INVALID_SETTING;
	}

	AL_EXIT( AL_DBG_MGR );
	return status;
}





