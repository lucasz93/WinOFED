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

/* Registers a memory region */
#include "ibspdebug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ibsp_mem.tmh"
#endif

#include "ibspdll.h"


__forceinline boolean_t
__check_mr(
	IN				struct memory_reg			*p_reg,
	IN				ib_access_t					acl_mask,
	IN				void						*start,
	IN				size_t						len )
{
	return( (p_reg->type.access_ctrl & acl_mask) == acl_mask &&
		start >= p_reg->type.vaddr &&
		((ULONG_PTR)start) + len <=
		((ULONG_PTR)p_reg->type.vaddr) + p_reg->type.length );
}


/* Find the first registered mr that matches the given region. 
 * mem_list is either socket_info->buf_mem_list or socket_info->rdma_mem_list.
 */
struct memory_node *
lookup_partial_mr(
	IN				struct ibsp_socket_info		*s,
	IN				ib_access_t					acl_mask,
	IN				void						*start,
	IN				size_t						len )
{
	struct memory_node	*p_node;
	cl_list_item_t		*p_item;

	IBSP_ENTER( IBSP_DBG_MEM );

	cl_spinlock_acquire( &s->port->hca->rdma_mem_list.mutex );

	for( p_item = cl_qlist_head( &s->mr_list );
		p_item != cl_qlist_end( &s->mr_list );
		p_item = cl_qlist_next( p_item ) )
	{
		p_node = PARENT_STRUCT( p_item, struct memory_node, socket_item );
		if(p_node->p_reg1 &&
			__check_mr( p_node->p_reg1, acl_mask, start, len ) )
		{
			cl_spinlock_release( &s->port->hca->rdma_mem_list.mutex );
			IBSP_EXIT( IBSP_DBG_MEM );
			return p_node;
		}
	}

	cl_spinlock_release( &s->port->hca->rdma_mem_list.mutex );

	IBSP_PRINT_EXIT(TRACE_LEVEL_INFORMATION, IBSP_DBG_MEM, ("mr not found\n") );
	return NULL;
}


/* Registers a memory region. The memory region might be cached.
 * mem_list is either socket_info->buf_mem_list or hca->rdma_mem_list.
 */
struct memory_node *
ibsp_reg_mem(
	IN				struct ibsp_socket_info		*s,
	IN				ib_pd_handle_t				pd,
	IN				void						*start,
	IN				size_t						len,
	IN				ib_access_t					access_ctrl,
		OUT			LPINT						lpErrno )
{
	struct memory_node	*p_node;
	struct memory_reg	*p_reg;
	cl_list_item_t		*p_item;
	ib_api_status_t		status;

	IBSP_ENTER( IBSP_DBG_MEM );

	CL_ASSERT( start != NULL );
	CL_ASSERT( len != 0 );
	CL_ASSERT( (access_ctrl & ~(IB_AC_RDMA_READ | IB_AC_RDMA_WRITE | IB_AC_LOCAL_WRITE)) ==
			  0 );

	/* Optimistically allocate a tracking structure. */
	p_node = HeapAlloc( g_ibsp.heap, 0, sizeof(struct memory_node) );
	if( !p_node )
	{
		IBSP_ERROR_EXIT( ("AllocateOverlappedBuf:HeapAlloc() failed: %d\n",
			GetLastError()) );
		*lpErrno = WSAENOBUFS;
		return NULL;
	}

	/* First, try to find a suitable MR */
	cl_spinlock_acquire( &s->port->hca->rdma_mem_list.mutex );

	/* Find the first registered mr that matches the given region. */
	for( p_item = cl_qlist_head( &s->port->hca->rdma_mem_list.list );
		p_item != cl_qlist_end( &s->port->hca->rdma_mem_list.list );
		p_item = cl_qlist_next( p_item ) )
	{
		p_reg = PARENT_STRUCT(p_item, struct memory_reg, item);

		if( __check_mr( p_reg, access_ctrl, start, len ) )
		{
			p_node->p_reg1 = p_reg;
			p_node->s = s;
			cl_qlist_insert_tail( &p_reg->node_list, &p_node->mr_item );
			cl_qlist_insert_head(
				&s->mr_list, &p_node->socket_item );
			cl_spinlock_release( &s->port->hca->rdma_mem_list.mutex );
			IBSP_EXIT( IBSP_DBG_MEM );
			return p_node;
		}
	}

	/* No corresponding MR has been found. Create a new one. */
	p_reg = HeapAlloc( g_ibsp.heap, 0, sizeof(struct memory_reg) );

	if( !p_reg )
	{
		IBSP_ERROR_EXIT( ("AllocateOverlappedBuf:HeapAlloc() failed: %d\n",
			GetLastError()) );
		cl_spinlock_release( &s->port->hca->rdma_mem_list.mutex );
		HeapFree( g_ibsp.heap, 0, p_node );
		*lpErrno = WSAENOBUFS;
		return NULL;
	}

	/* The node is not initialized yet. All the parameters given are
	 * supposed to be valid so we don't check them. */
	cl_qlist_init( &p_reg->node_list );
	p_reg->type.vaddr = start;
	p_reg->type.length = len;
	p_reg->type.access_ctrl = access_ctrl;

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_MEM, ("pinning memory node %p\n", p_node) );
	status = ib_reg_mem(
		pd, &p_reg->type, &p_reg->lkey, &p_reg->rkey, &p_reg->mr_handle );

	if( status )
	{
		cl_spinlock_release( &s->port->hca->rdma_mem_list.mutex );
		HeapFree( g_ibsp.heap, 0, p_reg );
		HeapFree( g_ibsp.heap, 0, p_node );

		IBSP_ERROR_EXIT( ("ib_reg_mem returned %s\n", ib_get_err_str(status)) );

		*lpErrno = WSAEFAULT;
		return NULL;
	}

	STAT_INC( mr_num );

	p_node->p_reg1 = p_reg;
	p_node->s = s;

	/* Link to the list of nodes. */
	cl_qlist_insert_head( &s->port->hca->rdma_mem_list.list, &p_reg->item );
	cl_qlist_insert_head( &s->mr_list, &p_node->socket_item );
	cl_qlist_insert_tail( &p_reg->node_list, &p_node->mr_item );
	cl_spinlock_release( &s->port->hca->rdma_mem_list.mutex );

	IBSP_EXIT( IBSP_DBG_MEM );

	*lpErrno = 0;
	return p_node;
}


static inline int __ibsp_dereg_mem_mr(
	IN				struct memory_node			*node )
{
	IBSP_ENTER( IBSP_DBG_MEM );

	// Underlying registration could be freed before the node.
	if( node->p_reg1 )
		cl_qlist_remove_item( &node->p_reg1->node_list, &node->mr_item );

	cl_qlist_remove_item( &node->s->mr_list, &node->socket_item );


	memset(node,0x45,sizeof node);
	HeapFree( g_ibsp.heap, 0, node );

	IBSP_EXIT( IBSP_DBG_MEM );
	return 0;
}


/* Deregisters a memory region */
int
ibsp_dereg_mem(
	IN				struct ibsp_socket_info		*s,
	IN				struct memory_node			*node,
		OUT			LPINT						lpErrno )
{
	IBSP_ENTER( IBSP_DBG_MEM );

	cl_spinlock_acquire( &s->port->hca->rdma_mem_list.mutex );
	*lpErrno = __ibsp_dereg_mem_mr( node );
	cl_spinlock_release( &s->port->hca->rdma_mem_list.mutex );

	IBSP_EXIT( IBSP_DBG_MEM );
	return (*lpErrno? SOCKET_ERROR : 0);
}


/*
 * Deregister the remaining memory regions on an HCA. This function should
 * only be called before destroying the PD. In normal case, the list should
 * be empty because the switch should have done it.
 */
void
ibsp_dereg_hca(
	IN				struct mr_list				*mem_list )
{
	cl_list_item_t *item;
	cl_list_item_t *item1;

	IBSP_ENTER( IBSP_DBG_MEM );

	cl_spinlock_acquire( &mem_list->mutex );
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_MEM,
		("%Id registrations.\n", cl_qlist_count( &mem_list->list )) );

	for( item = cl_qlist_remove_head( &mem_list->list );
		item != cl_qlist_end( &mem_list->list );
		item = cl_qlist_remove_head( &mem_list->list ) )
	{
		struct memory_reg *p_reg = PARENT_STRUCT(item, struct memory_reg, item);
		ib_api_status_t status;

		/*
		 * Clear the pointer from the node to this registration.  No need
		 * to remove from the list as we're about to free the registration.
		 */
		for( item1 = cl_qlist_head( &p_reg->node_list );
			item1 != cl_qlist_end( &p_reg->node_list );
			item1 = cl_qlist_next( item1 ) )
		{
			struct memory_node *p_node =
				PARENT_STRUCT( item1, struct memory_node, mr_item );
			p_node->p_reg1 = NULL;
		}

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_MEM, ("unpinning ,memory reg %p\n", p_reg) );
		status = ib_dereg_mr( p_reg->mr_handle );
		if( status )
		{
			IBSP_ERROR( (
				"ib_dereg_mem returned %s\n", ib_get_err_str( status )) );
		}
		else
		{
			STAT_DEC( mr_num );
		}

		HeapFree( g_ibsp.heap, 0, p_reg );
	}

	cl_spinlock_release( &mem_list->mutex );

	IBSP_EXIT( IBSP_DBG_MEM );
}


/* Deregister the remaining memory regions. This function should only 
 * be called when destroying the socket. In normal case, the list should 
 * be empty because the switch should have done it. */
void
ibsp_dereg_socket(
	IN				struct ibsp_socket_info		*s )
{
	IBSP_ENTER( IBSP_DBG_MEM );

	if( !s->port )
	{
		CL_ASSERT( !cl_qlist_count( &s->mr_list ) );
		IBSP_EXIT( IBSP_DBG_MEM );
		return;
	}

	cl_spinlock_acquire( &s->port->hca->rdma_mem_list.mutex );
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_MEM,
		("%Id registrations.\n", cl_qlist_count( &s->mr_list )) );

	while( cl_qlist_count( &s->mr_list ) )
	{
		__ibsp_dereg_mem_mr( PARENT_STRUCT( cl_qlist_head( &s->mr_list ),
			struct memory_node, socket_item) );
	}

	cl_spinlock_release( &s->port->hca->rdma_mem_list.mutex );

	IBSP_EXIT( IBSP_DBG_MEM );
}


/*
 * Loop through all the memory registrations on an HCA and release
 * all that fall within the specified range.
 */
void
ibsp_hca_flush_mr_cache(
	IN				struct ibsp_hca				*p_hca,
	IN				LPVOID						lpvAddress,
	IN				SIZE_T						Size )
{
	struct memory_reg	*p_reg;
	cl_list_item_t		*p_item;
	cl_list_item_t		*p_item1;
	ib_api_status_t		status;

	IBSP_ENTER( IBSP_DBG_MEM );

	cl_spinlock_acquire( &p_hca->rdma_mem_list.mutex );
	p_item = cl_qlist_head( &p_hca->rdma_mem_list.list );
	while( p_item != cl_qlist_end( &p_hca->rdma_mem_list.list ) )
	{
		p_reg = PARENT_STRUCT( p_item, struct memory_reg, item );

		/* Move to the next item now so we can remove the current. */
		p_item = cl_qlist_next( p_item );

		if( lpvAddress > p_reg->type.vaddr ||
			((ULONG_PTR)lpvAddress) + Size <
			((ULONG_PTR)p_reg->type.vaddr) + p_reg->type.length )
		{
			continue;
		}

		/*
		 * Clear the pointer from all sockets' nodes to this registration.
		 * No need to remove from the list as we're about to free the
		 * registration.
		 */
		for( p_item1 = cl_qlist_head( &p_reg->node_list );
			p_item1 != cl_qlist_end( &p_reg->node_list );
			p_item1 = cl_qlist_next( p_item1 ) )
		{
			struct memory_node *p_node =
				PARENT_STRUCT( p_item1, struct memory_node, mr_item );

			p_node->p_reg1 = NULL;
		}

		cl_qlist_remove_item( &p_hca->rdma_mem_list.list, &p_reg->item );

		status = ib_dereg_mr( p_reg->mr_handle );
		if( status != IB_SUCCESS )
		{
			IBSP_ERROR( (
				"ib_dereg_mr returned %s\n", ib_get_err_str(status)) );
		}

		HeapFree( g_ibsp.heap, 0, p_reg );
	}
	cl_spinlock_release( &p_hca->rdma_mem_list.mutex );

	IBSP_EXIT( IBSP_DBG_MEM );
}
