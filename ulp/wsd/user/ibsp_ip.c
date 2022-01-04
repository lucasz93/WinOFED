/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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

/* Builds and returns the list of IP addresses available from all
 * adapters. */

#include "ibspdebug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ibsp_ip.tmh"
#endif

#include "ibspdll.h"
#include "iba/ibat.h"

/*--------------------------------------------------------------------------*/

/*
 * Query an IP address from a GUID
 */

struct ip_query_context
{
	cl_fmap_t			*p_ip_map;
	struct ibsp_port	*p_port;
};


int CL_API
ip_cmp(
	IN	const void* const		p_key1,
	IN	const void*	const		p_key2 )
{
	struct ibsp_ip_addr	*p_ip1, *p_ip2;

	p_ip1 = (struct ibsp_ip_addr*)p_key1;
	p_ip2 = (struct ibsp_ip_addr*)p_key2;

	if( p_ip1->ip_addr.S_un.S_addr < p_ip2->ip_addr.S_un.S_addr )
		return -1;
	else if( p_ip1->ip_addr.S_un.S_addr > p_ip2->ip_addr.S_un.S_addr )
		return 1;

	/* IP addresses match.  See if we need a port match too. */
	if( !p_ip1->p_port || !p_ip2->p_port )
		return 0;

	/* We need a port match too. */
	return cl_memcmp(
		&p_ip1->p_port->guid, &p_ip2->p_port->guid, sizeof(net64_t) );
}


/* Synchronously query the SA for an IP address. */
int
query_ip_address(
	IN				struct ibsp_port			*p_port,
	IN	OUT			cl_fmap_t					*p_ip_map )
{
	IOCTL_IBAT_IP_ADDRESSES_IN	in;
	IOCTL_IBAT_IP_ADDRESSES_OUT	*p_out;
	DWORD						size;
    ULONG                       i;
	cl_fmap_item_t				*p_item;

	IBSP_ENTER( IBSP_DBG_HW );

	/* The list must be initialized and empty */
	CL_ASSERT( !cl_fmap_count( p_ip_map ) );
	in.Version = IBAT_IOCTL_VERSION;
	in.PortGuid = p_port->guid;

	cl_spinlock_acquire( &g_ibsp.ip_mutex );
	if( g_ibsp.h_ibat_dev == INVALID_HANDLE_VALUE )
	{
		g_ibsp.h_ibat_dev = CreateFileW( IBAT_WIN32_NAME,
			MAXIMUM_ALLOWED, 0, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	}
	cl_spinlock_release( &g_ibsp.ip_mutex );

	size = sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT);

	do
	{
		p_out = HeapAlloc( g_ibsp.heap, 0, size );

		if( !p_out )
		{
			IBSP_ERROR_EXIT( ("Failed to allocate output buffer.\n") );
			return -1;
		}

		if( !DeviceIoControl( g_ibsp.h_ibat_dev, IOCTL_IBAT_IP_ADDRESSES,
			&in, sizeof(in), p_out, size, &size, NULL ) )
		{
			HeapFree( g_ibsp.heap, 0, p_out );
			IBSP_ERROR_EXIT( (
				"IOCTL_IBAT_IP_ADDRESSES for port %I64x failed (%x).\n",
				p_port->guid, GetLastError()) );
			return -1;
		}

		if( p_out->Size > size )
		{
			size = p_out->Size;
			HeapFree( g_ibsp.heap, 0, p_out );
			p_out = NULL;
		}

	} while( !p_out );

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW, ("Port %I64x has %d IP addresses.\n",
		p_port->guid, p_out->AddressCount) );

	for( i = 0; i < p_out->AddressCount; i++ )
	{
		struct ibsp_ip_addr *ip_addr;

        if( p_out->Address[i].si_family != AF_INET )
        {
            continue;
        }

		ip_addr = HeapAlloc(
			g_ibsp.heap, 0, sizeof(struct ibsp_ip_addr) );
		if( !ip_addr )
		{
			IBSP_ERROR_EXIT( ("no memory\n") );
			break;
		}
		/* Copy the IP address being ia64 friendly */
        memcpy( (void*)&ip_addr->ip_addr,
                (void*)&p_out->Address[i].Ipv4.sin_addr,
                sizeof(ip_addr->ip_addr) );

		ip_addr->p_port = p_port;

		p_item = cl_fmap_insert( p_ip_map, ip_addr, &ip_addr->item );
		if( p_item != &ip_addr->item )
		{
			/* Duplicate!  Should never happen. */
			IBSP_ERROR( (
				"Got duplicate addr %s\n", inet_ntoa( ip_addr->ip_addr )) );
			HeapFree( g_ibsp.heap, 0, ip_addr );
			continue;
		}

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW,
			("Got addr %s\n", inet_ntoa( ip_addr->ip_addr )) );
	}

	HeapFree( g_ibsp.heap, 0, p_out );

	IBSP_EXIT( IBSP_DBG_HW );
	return 0;
}


/* Query a port for it list of supported IP addresses, and update the port and global lists.
 * The port mutex  must be taken. */
static int
update_ip_addresses(
					struct ibsp_port			*port )
{
	cl_fmap_t		new_ip, old_ip, dup_ip;
	cl_fmap_item_t	*p_item;
	int				ret;

	cl_fmap_init( &new_ip, ip_cmp );
	cl_fmap_init( &old_ip, ip_cmp );
	cl_fmap_init( &dup_ip, ip_cmp );

	/* Get the list of new addresses */
	ret = query_ip_address( port, &dup_ip );
	if( ret )
	{
		IBSP_ERROR_EXIT( (
			"query_ip_address failed (%d)\n", ret) );
		return 1;
	}

	cl_spinlock_acquire( &g_ibsp.ip_mutex );

	/* Insert the new list of IP into the global list of IP addresses. */
	cl_fmap_delta( &g_ibsp.ip_map, &dup_ip, &new_ip, &old_ip );
	cl_fmap_merge( &g_ibsp.ip_map, &new_ip );
	CL_ASSERT( !cl_fmap_count( &new_ip ) );

	/*
	 * Note that the map delta operation will have moved all IP addresses
	 * for other ports into the old list.  Move them back.
	 */
	for( p_item = cl_fmap_head( &old_ip );
		p_item != cl_fmap_end( &old_ip );
		p_item = cl_fmap_head( &old_ip ) )
	{
		struct ibsp_ip_addr *p_ip =
			PARENT_STRUCT( p_item, struct ibsp_ip_addr, item );

		cl_fmap_remove_item( &old_ip, p_item );

		if( p_ip->p_port != port )
		{
			p_item = cl_fmap_insert( &g_ibsp.ip_map, p_ip, &p_ip->item );
			CL_ASSERT( p_item == &p_ip->item );
		}
		else
		{
			HeapFree( g_ibsp.heap, 0, p_ip );
		}
	}

	cl_spinlock_release( &g_ibsp.ip_mutex );

	/* Now clean up duplicates entries. */
	for( p_item = cl_fmap_head( &dup_ip );
		p_item != cl_fmap_end( &dup_ip );
		p_item = cl_fmap_head( &dup_ip ) )
	{
		struct ibsp_ip_addr *p_ip =
			PARENT_STRUCT( p_item, struct ibsp_ip_addr, item );

		cl_fmap_remove_item( &dup_ip, p_item );

		HeapFree( g_ibsp.heap, 0, p_ip );
	}

	return 0;
}

/*--------------------------------------------------------------------------*/


/* Synchronously query the SA for a GUID. Return 0 on success. */
int
query_guid_address(
	IN				const struct sockaddr		*p_src_addr,
	IN				const struct sockaddr		*p_dest_addr,
		OUT			ib_net64_t					*port_guid )
{
	ib_path_rec_t path;
	HRESULT hr;

	IBSP_ENTER( IBSP_DBG_HW );
    hr = IbatQueryPath(p_src_addr, p_dest_addr, (IBAT_PATH_BLOB*)&path);

	if( hr == S_OK )
	{
		*port_guid = path.dgid.unicast.interface_id;
	}
	else
	{
		IBSP_ERROR( ("IBAT::Resolve for IP %08x\n",
			((struct sockaddr_in*)p_dest_addr)->sin_addr.s_addr) );
	}
	return hr;
}

/*--------------------------------------------------------------------------*/

/* 
 * Get a path record from a GUID 
 */
struct query_pr_context
{
	ib_api_status_t status;
	ib_path_rec_t *path_rec;
};


static void AL_API
query_pr_callback(
					ib_query_rec_t				*p_query_rec )
{
	struct query_pr_context *query_context =
		(struct query_pr_context *)p_query_rec->query_context;
	ib_api_status_t status;

	IBSP_ENTER( IBSP_DBG_HW );
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW, ("status is %d\n", p_query_rec->status) );

	if( p_query_rec->status == IB_SUCCESS && p_query_rec->result_cnt )
	{
		ib_path_rec_t *path_rec;

		query_context->status = IB_SUCCESS;

		path_rec = ib_get_query_path_rec( p_query_rec->p_result_mad, 0 );

		CL_ASSERT( path_rec );

		/* Copy the path record */
		*query_context->path_rec = *path_rec;
	}
	else
	{
		query_context->status = IB_ERROR;
	}

	if( p_query_rec->p_result_mad )
		status = ib_put_mad( p_query_rec->p_result_mad );

	IBSP_EXIT( IBSP_DBG_HW );
}


/* Synchronously query the SA for a GUID. Return 0 on success. */
int
query_pr(
	IN				ib_net64_t					guid,
	IN				ib_net64_t					dest_port_guid,
	IN				uint16_t					dev_id,
		OUT			ib_path_rec_t				*path_rec )
{
	ib_gid_pair_t user_query;
	struct query_pr_context query_context;
	ib_query_handle_t query_handle;
	ib_query_req_t query_req;
	ib_api_status_t status;
	uint8_t pkt_life;

	IBSP_ENTER( IBSP_DBG_HW );

	query_req.query_type = IB_QUERY_PATH_REC_BY_GIDS;
	query_req.p_query_input = &user_query;
	query_req.port_guid = guid;
	query_req.timeout_ms = g_sa_timeout;
	query_req.retry_cnt = g_sa_retries;
	query_req.flags = IB_FLAGS_SYNC;
	query_req.query_context = &query_context;
	query_req.pfn_query_cb = query_pr_callback;

	ib_gid_set_default( &user_query.src_gid, guid );
	ib_gid_set_default( &user_query.dest_gid, dest_port_guid );

	query_context.path_rec = path_rec;

	fzprint(("%s():%d:0x%x:0x%x: Calling ib_query()..\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId()));

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW,
		("Query for path from %I64x to %I64x\n",
		guid , dest_port_guid) );

	status = ib_query( g_ibsp.al_handle, &query_req, &query_handle );

	if( status != IB_SUCCESS )
	{
		IBSP_ERROR( ("ib_query failed (%d)\n", status) );
		goto error;
	}

	fzprint(("%s():%d:0x%x:0x%x: Done calling ib_query()..\n", __FUNCTION__,
			 __LINE__, GetCurrentProcessId(), GetCurrentThreadId()));

	if( query_context.status != IB_SUCCESS )
	{
		IBSP_ERROR( ("query failed (%d)\n", query_context.status) );
		goto error;
	}

	if( (dev_id == 0x5A44) &&
		(ib_path_rec_mtu( path_rec ) > IB_MTU_LEN_1024) )
	{
		/* Local endpoint is Tavor - cap MTU to 1K for extra bandwidth. */
		path_rec->mtu &= IB_PATH_REC_SELECTOR_MASK;
		path_rec->mtu |= IB_MTU_LEN_1024;
	}

	pkt_life = ib_path_rec_pkt_life( path_rec )  + g_pkt_life_modifier;
	if( pkt_life > 0x1F )
		pkt_life = 0x1F;

	path_rec->pkt_life &= IB_PATH_REC_SELECTOR_MASK;
	path_rec->pkt_life |= pkt_life;

	IBSP_EXIT( IBSP_DBG_HW );
	return 0;

error:
	IBSP_ERROR_EXIT( ("query_pr failed\n") );
	return 1;
}

/*--------------------------------------------------------------------------*/

/* Builds the list of all IP addresses supported. */
int
build_ip_list(
	IN	OUT			LPSOCKET_ADDRESS_LIST		ip_list,
	IN	OUT			LPDWORD						ip_list_size,
		OUT			LPINT						lpErrno )
{
	size_t				size_req;
	size_t				num_ip;
	cl_list_item_t		*p_hca_item, *p_port_item;
	cl_fmap_item_t		*p_item;
	struct ibsp_hca		*p_hca;
	struct ibsp_port	*p_port;
	struct sockaddr_in	*addr;
	int					i;

	IBSP_ENTER( IBSP_DBG_HW );

	cl_spinlock_acquire( &g_ibsp.hca_mutex );
	for( p_hca_item = cl_qlist_head( &g_ibsp.hca_list );
		p_hca_item != cl_qlist_end( &g_ibsp.hca_list );
		p_hca_item = cl_qlist_next( p_hca_item ) )
	{
		p_hca = PARENT_STRUCT( p_hca_item, struct ibsp_hca, item );

		cl_spinlock_acquire( &p_hca->port_lock );
		for( p_port_item = cl_qlist_head( &p_hca->port_list );
			p_port_item != cl_qlist_end( &p_hca->port_list );
			p_port_item = cl_qlist_next( p_port_item ) )
		{
			p_port = PARENT_STRUCT( p_port_item, struct ibsp_port, item );
			update_ip_addresses( p_port );
		}
		cl_spinlock_release( &p_hca->port_lock );
	}
	cl_spinlock_release( &g_ibsp.hca_mutex );

	cl_spinlock_acquire( &g_ibsp.ip_mutex );

	num_ip = cl_fmap_count( &g_ibsp.ip_map );

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW, ("	num ip = %Id\n", num_ip) );

	/* Note: the required size computed is a few bytes larger than necessary, 
	 * but that keeps the code clean. */
	size_req = sizeof(SOCKET_ADDRESS_LIST);
	
	switch( num_ip )
	{
	case 0:
		cl_spinlock_acquire( &g_ibsp.ip_mutex );
		if( g_ibsp.h_ibat_dev != INVALID_HANDLE_VALUE )
		{
			CloseHandle( g_ibsp.h_ibat_dev );
			g_ibsp.h_ibat_dev = INVALID_HANDLE_VALUE;
		}
		cl_spinlock_release( &g_ibsp.ip_mutex );
		break;
		
	default:
		size_req +=
			(num_ip - 1) * (sizeof(SOCKET_ADDRESS) + sizeof(SOCKADDR));
		/* Fall through. */

	case 1:
		/* Add the space for the first address. */
		size_req += sizeof(SOCKADDR);
		break;
	}

	if( size_req > *ip_list_size )
	{
		cl_spinlock_release( &g_ibsp.ip_mutex );
		*ip_list_size = (DWORD) size_req;
		*lpErrno = WSAEFAULT;
		IBSP_ERROR_EXIT( (
			"returning default, size %Id (usually not an error)\n", size_req) );
		return SOCKET_ERROR;
	}

	memset( ip_list, 0, *ip_list_size );

	/* We store the array of addresses after the last address pointer. */
	addr = (struct sockaddr_in *)(&(ip_list->Address[num_ip]));
	*ip_list_size = (DWORD) size_req;

	ip_list->iAddressCount = (INT) num_ip;

	for( i = 0, p_item = cl_fmap_head( &g_ibsp.ip_map );
		p_item != cl_fmap_end( &g_ibsp.ip_map );
		i++, p_item = cl_fmap_next( p_item ) )
	{
		struct ibsp_ip_addr *ip_addr =
			PARENT_STRUCT(p_item, struct ibsp_ip_addr, item);

		ip_list->Address[i].iSockaddrLength = sizeof(struct sockaddr_in);
		ip_list->Address[i].lpSockaddr = (LPSOCKADDR) addr;

		addr->sin_family = AF_INET;
		addr->sin_port = 0;
		addr->sin_addr = ip_addr->ip_addr;

		addr++;
	}

	cl_spinlock_release( &g_ibsp.ip_mutex );

	IBSP_EXIT( IBSP_DBG_HW );

	lpErrno = 0;
	return 0;
}


/* Find a port associated with an IP address. */
struct ibsp_port *
get_port_from_ip_address(
	IN		const	struct						in_addr sin_addr )
{
	cl_fmap_item_t			*p_item;
	struct ibsp_ip_addr		ip;
	struct ibsp_port		*p_port = NULL;

	IBSP_ENTER( IBSP_DBG_HW );

	ip.ip_addr = sin_addr;
	ip.p_port = NULL;

	cl_spinlock_acquire( &g_ibsp.ip_mutex );

	p_item = cl_fmap_get( &g_ibsp.ip_map, &ip );
	if( p_item != cl_fmap_end( &g_ibsp.ip_map ) )
		p_port = PARENT_STRUCT(p_item, struct ibsp_ip_addr, item)->p_port;
	else
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW, ("not found\n") );

	cl_spinlock_release( &g_ibsp.ip_mutex );

	IBSP_EXIT( IBSP_DBG_HW );
	return p_port;
}
