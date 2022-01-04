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


#include "srp_connection.h"
#include "srp_cmd.h"
#include "srp_data_path.h"
#include "srp_descriptors.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srp_descriptors.tmh"
#endif
#include "srp_rsp.h"
#include "srp_session.h"
#include "srp_tsk_mgmt.h"

/* __srp_create_recv_descriptors */
/*!
Creates the receive descriptors and posts them to the receive queue

@param p_descriptors    - pointer to the work requests structure
@param h_pd             - protection domain used for registration of memory
@param h_qp             - queue pair used to post work requests
@param lkey             - lkey for all physical memory

@return - result of operations
*/
static ib_api_status_t
__srp_create_recv_descriptors(
	IN	OUT			srp_descriptors_t			*p_descriptors,
	IN				ib_al_ifc_t* const			p_ifc,
	IN				ib_pd_handle_t				h_pd,
	IN				ib_qp_handle_t				h_qp,
	IN				net32_t						lkey)
{
	ib_api_status_t         status = IB_SUCCESS;
	srp_recv_descriptor_t   *p_descriptor;
	uint8_t                 *p_data_segment;
	uint32_t                i;

	SRP_ENTER( SRP_DBG_PNP );
	UNUSED_PARAM( h_pd );
	/* Create the array of recv descriptors */
	p_descriptors->p_recv_descriptors_array =
		(srp_recv_descriptor_t *)cl_zalloc( p_descriptors->recv_descriptor_count * sizeof(srp_recv_descriptor_t) );
	if ( p_descriptors->p_recv_descriptors_array == NULL )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Failed to allocate %d recv descriptors.\n",  p_descriptors->recv_descriptor_count) );
		status = IB_INSUFFICIENT_MEMORY;
		goto exit;
	}

	/* Create the array of recv data segments */
	p_descriptors->p_recv_data_segments_array =
		cl_zalloc( p_descriptors->recv_descriptor_count * p_descriptors->recv_data_segment_size );
	if ( p_descriptors->p_recv_data_segments_array == NULL )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Failed to allocate %d recv data segments of %d length.\n",
			p_descriptors->recv_descriptor_count,
			p_descriptors->recv_data_segment_size) );

		cl_free( p_descriptors->p_recv_descriptors_array );
		p_descriptors->p_recv_descriptors_array = NULL;
		status = IB_INSUFFICIENT_MEMORY;
		goto exit;
	}

	/* Initialize them and post to receive queue */
	p_descriptor = p_descriptors->p_recv_descriptors_array;
	p_data_segment = p_descriptors->p_recv_data_segments_array;

	for ( i = 0; i < p_descriptors->recv_descriptor_count; i++ )
	{
		p_descriptor->wr.p_next = NULL;
		p_descriptor->wr.wr_id = (ULONG_PTR)p_descriptor;
		p_descriptor->wr.num_ds = 1;
		p_descriptor->wr.ds_array = p_descriptor->ds;

		p_descriptor->ds[0].vaddr = cl_get_physaddr(p_data_segment);
		p_descriptor->ds[0].length = p_descriptors->recv_data_segment_size;
		p_descriptor->ds[0].lkey = lkey;

		p_descriptors->p_recv_descriptors_array[i].p_data_segment = p_data_segment;

		status = p_ifc->post_recv( h_qp, &p_descriptor->wr, NULL );
		if ( status != IB_SUCCESS )
		{
			SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				("Failed to post recv descriptor. Status = %d.\n", status) );
			goto exit;
		}

		p_descriptor++;
		p_data_segment += p_descriptors->recv_data_segment_size;
	}

exit:
	SRP_EXIT( SRP_DBG_PNP );

	return ( status );
}

/* srp_init_descriptors */
/*!
Orchestrates creation of receive descriptor buffers and sent list

@param p_descriptors          - pointer to the descriptors structure
@param recv descriptor_count  - number of receive descriptors to create
@param recv data_segment_size - size of each receive descriptor's data area
@param h_pd                   - protection domain used for registration of memory
@param h_qp                   - queue pair used to post work requests
@param lkey                   - lkey for all physical memory

@return - result of operations
*/
ib_api_status_t
srp_init_descriptors(
	IN	OUT			srp_descriptors_t			*p_descriptors,
	IN				uint32_t					recv_descriptor_count,
	IN				uint32_t					recv_data_segment_size,
	IN				ib_al_ifc_t* const			p_ifc,
	IN				ib_pd_handle_t				h_pd,
	IN				ib_qp_handle_t				h_qp,
	IN				net32_t						lkey)
{
	ib_api_status_t status;

	SRP_ENTER( SRP_DBG_PNP );

	CL_ASSERT( p_descriptors != NULL );

	cl_memclr( p_descriptors, sizeof(*p_descriptors) );

	cl_spinlock_init ( &p_descriptors->sent_list_lock );
	cl_spinlock_init ( &p_descriptors->pending_list_lock );
	cl_qlist_init( &p_descriptors->sent_descriptors );
	cl_qlist_init( &p_descriptors->pending_descriptors );

	p_descriptors->initialized = TRUE;

	p_descriptors->recv_descriptor_count = recv_descriptor_count;
	p_descriptors->recv_data_segment_size = recv_data_segment_size;

	status = __srp_create_recv_descriptors( p_descriptors, p_ifc, h_pd, h_qp, lkey );
	if ( status != IB_SUCCESS )
	{
		srp_destroy_descriptors( p_descriptors );
	}

	SRP_EXIT( SRP_DBG_PNP );

	return ( status );
}

/* srp_destroy_descriptors */
/*!
Destroys the receive work request buffers

@param p_descriptors - pointer to the descriptors structure

@return - result of operations
*/
ib_api_status_t
srp_destroy_descriptors(
	IN OUT  srp_descriptors_t   *p_descriptors )
{
	SRP_ENTER( SRP_DBG_PNP );

	if ( p_descriptors->initialized == TRUE )
	{
		cl_spinlock_destroy ( &p_descriptors->sent_list_lock );
		cl_spinlock_destroy ( &p_descriptors->pending_list_lock );

		if ( p_descriptors->p_recv_data_segments_array != NULL )
		{
			cl_free( p_descriptors->p_recv_data_segments_array );
		}

		if ( p_descriptors->p_recv_descriptors_array != NULL )
		{
			cl_free( p_descriptors->p_recv_descriptors_array );
		}

		cl_memclr( p_descriptors, sizeof( *p_descriptors ) );
	}

	SRP_EXIT( SRP_DBG_PNP );

	return IB_SUCCESS;
}

/* __srp_add_descriptor */
/*!
Puts descriptor at tail of the list

@param p_descriptors - pointer to the descriptors structure
@param p_descriptor  - pointer to the descriptor to add
@param descriptors_list  - pointer to the list

@return - none
*/
inline
void
__srp_add_descriptor(
	IN  srp_send_descriptor_t   *p_descriptor,
	IN  cl_qlist_t                      *descriptors_list,
	IN  cl_spinlock_t             *p_lock)
{
	SRP_ENTER( SRP_DBG_DATA );

	cl_spinlock_acquire ( p_lock );

	cl_qlist_insert_tail( descriptors_list, &p_descriptor->list_item );
	CL_ASSERT( descriptors_list == p_descriptor->list_item.p_list );

	cl_spinlock_release ( p_lock );

	SRP_EXIT( SRP_DBG_DATA );
}

/* srp_add_send_descriptor */
/*!
Puts send descriptor at tail of the sent list

@param p_descriptors - pointer to the descriptors structure
@param p_descriptor  - pointer to the descriptor to add

@return - none
*/
inline
void
srp_add_send_descriptor(
	IN  srp_descriptors_t       *p_descriptors,
	IN  srp_send_descriptor_t   *p_descriptor )
{
	SRP_ENTER( SRP_DBG_DATA );
	__srp_add_descriptor( p_descriptor, 
		&p_descriptors->sent_descriptors, &p_descriptors->sent_list_lock );
	SRP_EXIT( SRP_DBG_DATA );
}

/* srp_add_pending_descriptor */
/*!
Puts pending send descriptor at tail of the pending list

@param p_descriptors - pointer to the descriptors structure
@param p_descriptor  - pointer to the descriptor to add

@return - none
*/
void
srp_add_pending_descriptor(
	IN  srp_descriptors_t       *p_descriptors,
	IN  srp_send_descriptor_t   *p_descriptor )
{
	SRP_ENTER( SRP_DBG_DATA );
	__srp_add_descriptor( p_descriptor, 
		&p_descriptors->pending_descriptors, &p_descriptors->pending_list_lock );
	SRP_EXIT( SRP_DBG_DATA );
}

/* __srp_remove_send_descriptor */
/*!
Removes send descriptor from the sent list

@param p_descriptors - pointer to the descriptors structure
@param p_descriptor  - pointer to the descriptor to add
@param descriptors_list  - pointer to the list

@return - none
*/
inline
void
__srp_remove_send_descriptor(
	IN  srp_send_descriptor_t   *p_descriptor,
	IN  cl_qlist_t                      *descriptors_list,
	IN  cl_spinlock_t             *p_lock)
{
	SRP_ENTER( SRP_DBG_DATA );

	cl_spinlock_acquire ( p_lock );

	CL_ASSERT( descriptors_list == p_descriptor->list_item.p_list );
	cl_qlist_remove_item( descriptors_list, &p_descriptor->list_item );

	cl_spinlock_release ( p_lock );

	SRP_EXIT( SRP_DBG_DATA );
}


/* srp_remove_send_descriptor */
/*!
Removes send descriptor from the sent list

@param p_descriptors - pointer to the descriptors structure
@param p_descriptor  - pointer to the descriptor to add

@return - none
*/
inline
void
srp_remove_send_descriptor(
	IN  srp_descriptors_t       *p_descriptors,
	IN  srp_send_descriptor_t   *p_descriptor )
{
	SRP_ENTER( SRP_DBG_DATA );
	__srp_remove_send_descriptor( p_descriptor, 
		&p_descriptors->sent_descriptors, &p_descriptors->sent_list_lock );
	SRP_EXIT( SRP_DBG_DATA );
}

/* srp_remove_pending_descriptor */
/*!
Removes pending send descriptor from the sent list

@param p_descriptors - pointer to the descriptors structure
@param p_descriptor  - pointer to the descriptor to add

@return - none
*/
inline
void
srp_remove_pending_descriptor(
	IN  srp_descriptors_t       *p_descriptors,
	IN  srp_send_descriptor_t   *p_descriptor )
{
	SRP_ENTER( SRP_DBG_DATA );
	__srp_remove_send_descriptor( p_descriptor, 
		&p_descriptors->pending_descriptors, &p_descriptors->pending_list_lock );
	SRP_EXIT( SRP_DBG_DATA );
}

/* __srp_remove_lun_head_send_descriptor */
/*!
Removes and returns the send descriptor from the head of the a list for the lun specified

@param p_descriptors - pointer to the descriptors structure
@param lun           - lun for which to remove head send descriptor
@param descriptors_list  - pointer to the list

@return - srp_send_descriptor at head of sent list or NULL if empty
*/
srp_send_descriptor_t*
__srp_remove_lun_head_send_descriptor(
	IN      UCHAR                   lun,
	IN  cl_qlist_t                      *descriptors_list,
	IN  cl_spinlock_t             *p_lock)
{
	srp_send_descriptor_t   *p_descriptor;

	SRP_ENTER( SRP_DBG_DATA );

	cl_spinlock_acquire ( p_lock );

	p_descriptor = (srp_send_descriptor_t *)cl_qlist_head( descriptors_list );
	CL_ASSERT( descriptors_list == p_descriptor->list_item.p_list );

	while ( p_descriptor != (srp_send_descriptor_t *)cl_qlist_end( descriptors_list ) )
	{
		if ( p_descriptor->p_srb->Lun == lun )
		{
			CL_ASSERT( descriptors_list == p_descriptor->list_item.p_list );
			cl_qlist_remove_item( descriptors_list, &p_descriptor->list_item );
			break;
		}

		p_descriptor = (srp_send_descriptor_t *)cl_qlist_next( &p_descriptor->list_item );
		CL_ASSERT( descriptors_list == p_descriptor->list_item.p_list );
	}

	if ( p_descriptor == (srp_send_descriptor_t *)cl_qlist_end( descriptors_list ) )
	{
		p_descriptor = NULL;
	}

	cl_spinlock_release ( p_lock );

	SRP_EXIT( SRP_DBG_DATA );

	return ( p_descriptor );
}


/* srp_remove_lun_head_send_descriptor */
/*!
Removes and returns the send descriptor from the head of the sent list for the lun specified

@param p_descriptors - pointer to the descriptors structure
@param lun           - lun for which to remove head send descriptor

@return - srp_send_descriptor at head of sent list or NULL if empty
*/
srp_send_descriptor_t*
srp_remove_lun_head_send_descriptor(
	IN      srp_descriptors_t       *p_descriptors,
	IN      UCHAR                   lun )
{
	srp_send_descriptor_t   *p_descriptor;

	SRP_ENTER( SRP_DBG_DATA );
	p_descriptor = __srp_remove_lun_head_send_descriptor( 
		lun, &p_descriptors->sent_descriptors, &p_descriptors->sent_list_lock );
	SRP_EXIT( SRP_DBG_DATA );

	return ( p_descriptor );
}

/* srp_remove_lun_head_pending_descriptor */
/*!
Removes and returns the send descriptor from the head of the sent list for the lun specified

@param p_descriptors - pointer to the descriptors structure
@param lun           - lun for which to remove head send descriptor

@return - srp_send_descriptor at head of sent list or NULL if empty
*/
srp_send_descriptor_t*
srp_remove_lun_head_pending_descriptor(
	IN      srp_descriptors_t       *p_descriptors,
	IN      UCHAR                   lun )
{
	srp_send_descriptor_t   *p_descriptor;

	SRP_ENTER( SRP_DBG_DATA );
	p_descriptor = __srp_remove_lun_head_send_descriptor( 
		lun, &p_descriptors->pending_descriptors, &p_descriptors->pending_list_lock );
	SRP_EXIT( SRP_DBG_DATA );

	return ( p_descriptor );
}

/* srp_post_send_descriptor */
/*!
Posts send descriptor across the connection specified and
if successful add it to the sent descriptors list

@param p_descriptors - pointer to the descriptors structure
@param p_descriptor  - pointer to the descriptor to send
@param p_session     - pointer to the session used to send

@return - result of post operation, or IB_ERROR if not connected
*/
ib_api_status_t
srp_post_send_descriptor(
	IN				srp_descriptors_t			*p_descriptors,
	IN				srp_send_descriptor_t		*p_descriptor,
	IN				srp_session_t				*p_session )
{
	ib_api_status_t		status = IB_ERROR;
	srp_connection_t	*p_connection;
	ib_al_ifc_t			*p_ifc;

	SRP_ENTER( SRP_DBG_DATA );

	p_connection = &p_session->connection;
	p_ifc = &p_session->hca.p_hba->ifc;

	if ( p_connection->state == SRP_CONNECTED )
	{
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			("wr_id    = 0x%I64x.\n", p_descriptor->wr.wr_id) );
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			("wr_type  = 0x%x.\n", p_descriptor->wr.wr_type) );
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			("send_opt = 0x%x.\n", p_descriptor->wr.send_opt) );
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			("num_ds   = 0x%x.\n", p_descriptor->wr.num_ds) );

		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
				   ("Posting  I/O for Function = %s(0x%x), Path = 0x%x, "
				   "Target = 0x%x, Lun = 0x%x, tag 0x%I64x\n",
				   g_srb_function_name[p_descriptor->p_srb->Function],
				   p_descriptor->p_srb->Function,
				   p_descriptor->p_srb->PathId,
				   p_descriptor->p_srb->TargetId,
				   p_descriptor->p_srb->Lun,
				   get_srp_command_tag( (srp_cmd_t *)p_descriptor->data_segment )) );

		if ( get_srp_iu_buffer_type( (srp_iu_buffer_t *)p_descriptor->data_segment ) == SRP_CMD )
		{
			p_descriptor->ds[0].length = get_srp_command_length( (srp_cmd_t *)p_descriptor->data_segment );
		}
		else /* task type */
		{
			p_descriptor->ds[0].length = get_srp_tsk_mgmt_length( (srp_tsk_mgmt_t *)p_descriptor->data_segment );
		}

		ASSERT( p_descriptor->ds[0].length <= p_connection->init_to_targ_iu_sz );

		srp_add_send_descriptor( p_descriptors, p_descriptor );

		status = p_ifc->post_send(
			p_connection->h_qp, &p_descriptor->wr, NULL );
		if ( status != IB_SUCCESS )
		{
			/* Remove From Sent List */
			srp_remove_send_descriptor( p_descriptors, p_descriptor );
			SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
						("Failed to post send descriptor. "
						"ib_post_send status = 0x%x tag = 0x%I64x\n",
						status,
						get_srp_command_tag( (srp_cmd_t *)p_descriptor->data_segment )) );
		}
	}
	else
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Attempting to post to an unconnected session.\n") );
	}

	SRP_EXIT( SRP_DBG_DATA );

	return ( status );
}

static srp_send_descriptor_t*
__srp_find_matching_send_descriptor(
	IN		cl_qlist_t			*p_descriptors_list,
	IN		uint64_t			tag )
{
	srp_send_descriptor_t	*p_send_descriptor;

	SRP_ENTER( SRP_DBG_DATA );
	/* assumed list lock is taken outside */
	p_send_descriptor = (srp_send_descriptor_t *)cl_qlist_head( p_descriptors_list );
	CL_ASSERT( p_descriptors_list == p_send_descriptor->list_item.p_list );

	while ( p_send_descriptor != (srp_send_descriptor_t *)cl_qlist_end( p_descriptors_list ) )
	{
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA, ("cmd tag = 0x%I64x.\n",
			get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );

		if ( get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment ) == tag )
		{
			CL_ASSERT( p_descriptors_list == p_send_descriptor->list_item.p_list );
			cl_qlist_remove_item( p_descriptors_list, &p_send_descriptor->list_item );
			goto exit;
		}

		p_send_descriptor = (srp_send_descriptor_t *)cl_qlist_next( &p_send_descriptor->list_item );
	}

	/* This is not an error. The request may have been aborted */
	p_send_descriptor = NULL;

exit:
	SRP_EXIT( SRP_DBG_DATA );

	return ( p_send_descriptor );
}

/* srp_find_matching_send_descriptor */
/*!
Given a received response find the matching send descriptor
which originated the request to the VFx and remove it from
the sent descriptor list

@param p_descriptors - pointer to the descriptors structure
@param tag           - tag of descriptor to find

@return - pointer to send descriptor or NULL if not found
*/
srp_send_descriptor_t*
srp_find_matching_send_descriptor(
	IN      srp_descriptors_t   *p_descriptors,
	IN      uint64_t            tag )
{
	srp_send_descriptor_t   *p_send_descriptor;

	SRP_ENTER( SRP_DBG_DATA );

	cl_spinlock_acquire( &p_descriptors->sent_list_lock );

	p_send_descriptor = __srp_find_matching_send_descriptor( &p_descriptors->sent_descriptors, tag );

	cl_spinlock_release( &p_descriptors->sent_list_lock );

	SRP_EXIT( SRP_DBG_DATA );

	return ( p_send_descriptor );
}

srp_send_descriptor_t*
srp_find_matching_pending_descriptor(
	IN      srp_descriptors_t       *p_descriptors,
	IN      uint64_t                tag )
{
	srp_send_descriptor_t		*p_send_descriptor;
	
	SRP_ENTER( SRP_DBG_DATA );
	
	cl_spinlock_acquire( &p_descriptors->pending_list_lock );

	p_send_descriptor = __srp_find_matching_send_descriptor( &p_descriptors->pending_descriptors, tag );

	cl_spinlock_release( &p_descriptors->pending_list_lock );

	SRP_EXIT( SRP_DBG_DATA );

	return ( p_send_descriptor );
}

/* srp_build_send_descriptor */
/*!
Initializes a send descriptor's fields

@param p_dev_ext       - our context pointer
@param p_srb           - scsi request to send to target
@param p_srp_conn_info - information about our connection to the VFx

@return - none
*/
void
srp_build_send_descriptor(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb,
	IN      p_srp_conn_info_t   p_srp_conn_info )
{
	srp_send_descriptor_t   *p_send_descriptor = (srp_send_descriptor_t *)p_srb->SrbExtension;
	STOR_PHYSICAL_ADDRESS   physical_address;
	ULONG                   length;

	SRP_ENTER( SRP_DBG_DATA );

	cl_memclr( p_send_descriptor, (sizeof ( srp_send_descriptor_t ) - SRP_MAX_IU_SIZE) );

	physical_address = StorPortGetPhysicalAddress( p_dev_ext, p_srb, p_send_descriptor->data_segment, &length );

	p_send_descriptor->wr.wr_id = (uint64_t)((uintn_t)p_send_descriptor);
	p_send_descriptor->wr.wr_type = WR_SEND;
	p_send_descriptor->wr.send_opt = (p_srp_conn_info->signal_send_completion == TRUE) ? IB_SEND_OPT_SIGNALED : 0;
	p_send_descriptor->wr.num_ds = 1;
	p_send_descriptor->wr.ds_array = p_send_descriptor->ds;
	p_send_descriptor->tag = p_srp_conn_info->tag;
	p_send_descriptor->p_srb = p_srb;
	p_send_descriptor->ds[0].vaddr = p_srp_conn_info->vaddr + physical_address.QuadPart;
	p_send_descriptor->ds[0].length =  p_srp_conn_info->init_to_targ_iu_sz;
	p_send_descriptor->ds[0].lkey =   p_srp_conn_info->lkey;
	p_send_descriptor->p_fmr_el = NULL;

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("hca vaddr        = 0x%I64x.\n", p_srp_conn_info->vaddr));
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("physical_address = 0x%I64x.\n", physical_address.QuadPart));
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("IU  vaddr        = 0x%I64x.\n", p_send_descriptor->ds[0].vaddr));
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("length           = %d.\n",          p_send_descriptor->ds[0].length));
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("lkey             = 0x%x.\n",        p_send_descriptor->ds[0].lkey));

	SRP_EXIT( SRP_DBG_DATA );
}
