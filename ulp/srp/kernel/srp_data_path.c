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


#include <complib/cl_math.h>
#include <complib/cl_obj.h>

#include "srp_cmd.h"
#include "srp_data_path.h"
#include "srp_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srp_data_path.tmh"
#endif
#include "srp_descriptors.h"
#include "srp_rsp.h"
#include "srp_session.h"
#include "srp_tsk_mgmt.h"

//#include "srp_aer_req.h"
//#include "srp_aer_rsp.h"

//#include "srp_cred_req.h"
//#include "srp_cred_rsp.h"

//#include "srp_i_logout.h"
//#include "srp_t_logout.h"

// Final address is of the form 0b00ttttttllllllll
#define BUILD_SCSI_ADDRESS(lun) ((uint64_t)lun << 48)

#define SRP_REQUEST_LIMIT_THRESHOLD		2

static ib_api_status_t
__srp_map_fmr(
	IN			PVOID							p_dev_ext,
	IN			PSTOR_SCATTER_GATHER_LIST		p_scatter_gather_list,
	IN			srp_send_descriptor_t				*p_send_descriptor,
	IN	OUT		srp_memory_descriptor_t			*p_memory_descriptor)
{
	srp_hba_t               *p_hba = ((srp_ext_t *)p_dev_ext)->p_hba;
	PSTOR_SCATTER_GATHER_ELEMENT    p_sg_element;
	uint32_t						total_len = 0;
	uint32_t						i,j,list_len = 0;
	uint64_t						*p_addr_list;
	uint64_t						vaddr=0;
	ib_api_status_t				status;
	srp_hca_t					hca;
	uint64_t						fmr_page_mask;
	net32_t						lkey;
	net32_t						rkey;
	srp_session_t					*p_srp_session;
	mlnx_fmr_pool_el_t			p_fmr_el;

	SRP_ENTER( SRP_DBG_DATA );

	if (g_srp_mode_flags & SRP_MODE_NO_FMR_POOL)
		return IB_UNSUPPORTED;

	p_srp_session = p_hba->session_list[p_send_descriptor->p_srb->TargetId];
	if ( p_srp_session == NULL )
		return IB_INVALID_STATE;
	
	hca = p_srp_session->hca;
	fmr_page_mask = ~(hca.fmr_page_size-1);
	
	for ( i = 0, p_sg_element = p_scatter_gather_list->List;
		  i < p_scatter_gather_list->NumberOfElements;
		  i++,  p_sg_element++ )
	{
		uint32_t		dma_len = p_sg_element->Length;
		
		if (p_sg_element->PhysicalAddress.QuadPart  & ~fmr_page_mask) {
			if (i > 0)
			{	// buffer start not from the beginning of the page is allowed only for the first SG element
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("Unaligned address at the begin of the list\n") );
				return IB_INVALID_PARAMETER;
			}
		}

		if ((p_sg_element->PhysicalAddress.QuadPart + dma_len)  & ~fmr_page_mask) {
			if (i < (uint32_t)p_scatter_gather_list->NumberOfElements -1)
			{	// buffer end not on the beginning of the page is allowed only for the last SG element
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("Unaligned address at the end of the list\n") );
				return IB_INVALID_PARAMETER;
			}
		}

		total_len += p_sg_element->Length;
		list_len += (p_sg_element->Length + (hca.fmr_page_size-1)) >> hca.fmr_page_shift;
	}

	
	p_addr_list = cl_zalloc(sizeof(uint64_t)*list_len);
	if(!p_addr_list)
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,("Failed to allocate page list\n"));
		return IB_INSUFFICIENT_MEMORY;
	}
	
	list_len = 0;
	for ( i = 0,  p_sg_element = p_scatter_gather_list->List;
		  i < p_scatter_gather_list->NumberOfElements;
		  i++, p_sg_element++ )
	{
		uint32_t		dma_len = p_sg_element->Length;
		for( j = 0; j < dma_len; j+=PAGE_SIZE)
		{
			p_addr_list[list_len++] = (p_sg_element->PhysicalAddress.QuadPart & fmr_page_mask) + j;
		}
	}

	p_send_descriptor->p_fmr_el = NULL;
	status = p_hba->ifc.map_phys_mlnx_fmr_pool
					(hca.h_fmr_pool, p_addr_list, list_len, &vaddr, &lkey, &rkey, &p_fmr_el );

	cl_free( p_addr_list );
	
	if(status != IB_SUCCESS)
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,("Failed to map fmr\n"));
		return status;
	}

	p_send_descriptor->p_fmr_el = p_fmr_el;
	p_sg_element = p_scatter_gather_list->List;

	p_memory_descriptor->virtual_address = cl_hton64( p_sg_element->PhysicalAddress.QuadPart & ~fmr_page_mask);
	p_memory_descriptor->memory_handle = rkey;
	p_memory_descriptor->data_length = cl_hton32( total_len);

#if DBG
	/* statistics */
	p_srp_session->x_pkt_fmr++;
#endif

	SRP_EXIT( SRP_DBG_DATA );
	return IB_SUCCESS;
}



static inline
void
__srp_dump_srb_info(srp_send_descriptor_t*		p_send_descriptor)
{
	
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("Srb Address              = %p\n",
		p_send_descriptor->p_srb) );
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("Srb DataBuffer Address   = %p\n",
		p_send_descriptor->p_srb->DataBuffer) );
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("Srb DataTransferLength   = %d\n",
		 p_send_descriptor->p_srb->DataTransferLength) );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			   ("Returning SrbStatus %s(0x%x) for "
			   "Function = %s(0x%x), Path = 0x%x, Target = 0x%x, "
			   "Lun = 0x%x, tag 0x%I64xn",
			   g_srb_status_name[p_send_descriptor->p_srb->SrbStatus],
			   p_send_descriptor->p_srb->SrbStatus,
			   g_srb_function_name[p_send_descriptor->p_srb->Function],
			   p_send_descriptor->p_srb->Function,
			   p_send_descriptor->p_srb->PathId,
			   p_send_descriptor->p_srb->TargetId,
			   p_send_descriptor->p_srb->Lun,
			   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );
}


static inline
void
__srp_process_session_send_completions(
	IN  srp_session_t   *p_srp_session )
{
	ib_api_status_t status;
	ib_wc_t         *p_wc_done_list = NULL;
	ib_wc_t         *p_wc;
	BOOLEAN         to_recover = FALSE;

	SRP_ENTER( SRP_DBG_DATA );

	cl_obj_lock( &p_srp_session->obj );

	if ( p_srp_session->connection.state != SRP_CONNECTED )
	{
		cl_obj_unlock( &p_srp_session->obj );
		SRP_EXIT( SRP_DBG_DATA );
		return;
	}

	status = p_srp_session->p_hba->ifc.poll_cq(
		p_srp_session->connection.h_send_cq,
		&p_srp_session->connection.p_wc_free_list,
		&p_wc_done_list );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("ib_poll_cq() failed!, status 0x%x\n", status) );

		p_srp_session->connection.state = SRP_CONNECT_FAILURE;
		cl_obj_unlock( &p_srp_session->obj );
		return;
	}

	cl_obj_ref( &p_srp_session->obj );
	cl_obj_unlock( &p_srp_session->obj );

	while ( (p_wc = p_wc_done_list) != NULL )
	{
		srp_send_descriptor_t   *p_send_descriptor;

		p_send_descriptor = (srp_send_descriptor_t *)((uintn_t)p_wc->wr_id);

		/* Remove head from list */
		p_wc_done_list = p_wc->p_next;
		p_wc->p_next = NULL;

		switch ( p_wc->status)
		{
			case IB_WCS_SUCCESS:
				break;
			case IB_WCS_WR_FLUSHED_ERR:
				if( !to_recover )
					to_recover = TRUE;
				SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
					("Send Completion Status %s Vendore Status = 0x%x, \n",
					p_srp_session->p_hba->ifc.get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific));

				SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
						   ("Send Completion Received for Function = %s(0x%x), "
						   "Path = 0x%x, Target = 0x%x, Lun = 0x%x, tag 0x%I64xn",
						   g_srb_function_name[p_send_descriptor->p_srb->Function],
						   p_send_descriptor->p_srb->Function,
						   p_send_descriptor->p_srb->PathId,
						   p_send_descriptor->p_srb->TargetId,
						   p_send_descriptor->p_srb->Lun,
						   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );
				break;
			default:
				to_recover = TRUE;
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("Send Completion Status %s Vendore Status = 0x%x, \n",
					p_srp_session->p_hba->ifc.get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific));

				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
						   ("Send Completion Received for Function = %s(0x%x), "
						   "Path = 0x%x, Target = 0x%x, Lun = 0x%x, tag 0x%I64xn",
						   g_srb_function_name[p_send_descriptor->p_srb->Function],
						   p_send_descriptor->p_srb->Function,
						   p_send_descriptor->p_srb->PathId,
						   p_send_descriptor->p_srb->TargetId,
						   p_send_descriptor->p_srb->Lun,
						   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );
				break;
		}

		/* Put onto head of free list */
		cl_obj_lock( &p_srp_session->obj );
		p_wc->p_next = p_srp_session->connection.p_wc_free_list;
		p_srp_session->connection.p_wc_free_list = p_wc;
		cl_obj_unlock( &p_srp_session->obj );

		/* Get next completion */
		p_wc = p_wc_done_list;
	}

	if( !to_recover )
	{
	/* Re-arm the CQ for more completions */
	status = p_srp_session->p_hba->ifc.rearm_cq(
		p_srp_session->connection.h_send_cq, FALSE );
	if ( status != IB_SUCCESS)
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("ib_rearm_cq() failed!, status 0x%x\n", status) );
			p_srp_session->connection.state = SRP_CONNECT_FAILURE;
			to_recover = TRUE;
	}
	}
	if( to_recover == TRUE )
	{
		p_srp_session->connection.state = SRP_CONNECT_FAILURE;
	}

	cl_obj_deref( &p_srp_session->obj );

	SRP_EXIT( SRP_DBG_DATA );
}

/* srp_send_completion_cb */
/*!
Set Timer to Process Send Completions - Usually bad if we get here

@param p_context - context pointer to the owning session

@return - none
*/
void
srp_send_completion_cb(
	IN	const	ib_cq_handle_t		h_cq,
	IN			void				*p_context )
{
	srp_session_t	*p_srp_session = (srp_session_t *)p_context;

	SRP_ENTER( SRP_DBG_DATA );

	UNUSED_PARAM( h_cq );

	__srp_process_session_send_completions( p_srp_session );

	if( p_srp_session->connection.state == SRP_CONNECT_FAILURE )
	{
		if( !p_srp_session->p_hba->adapter_stopped )
		{
			srp_session_failed( p_srp_session );
		}
	}

	SRP_EXIT( SRP_DBG_DATA );
}


static inline ib_api_status_t
__srp_clean_send_descriptor(
	IN			srp_send_descriptor_t		*p_send_descriptor,
	IN  srp_session_t						*p_srp_session )
{
	ib_api_status_t         status = IB_SUCCESS;

	if(p_srp_session && p_send_descriptor && p_send_descriptor->p_fmr_el)
	{
		status = p_srp_session->p_hba->ifc.unmap_mlnx_fmr_pool(p_send_descriptor->p_fmr_el);
		p_send_descriptor->p_fmr_el = NULL;
	}
	return status;
}

ib_api_status_t
__srp_post_io_request(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb, 
	srp_session_t           *p_srp_session )
{
	ib_api_status_t			status;
	srp_send_descriptor_t	*p_send_descriptor = (srp_send_descriptor_t *)p_srb->SrbExtension;

	SRP_ENTER( SRP_DBG_DATA );

	status = srp_post_send_descriptor( &p_srp_session->descriptors,
									   p_send_descriptor,
									   p_srp_session );

	if ( status == IB_SUCCESS )
	{
		cl_atomic_dec( &p_srp_session->connection.request_limit );
#if DBG	
		{	/* statistics */
			uint32_t size = (uint32_t)cl_qlist_count(&p_srp_session->descriptors.sent_descriptors);
			p_srp_session->x_sent_num++;
			p_srp_session->x_sent_total += size;
			if ( p_srp_session->x_sent_max < size )
				p_srp_session->x_sent_max = size;
		}
#endif	
		goto exit;
	}
	else 
	{
		p_srb->SrbStatus = SRB_STATUS_NO_HBA;

		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				   ("Returning SrbStatus %s(0x%x) for Function = %s(0x%x), Path = 0x%x, "
				   "Target = 0x%x, Lun = 0x%x, tag 0x%I64x\n",
				   g_srb_status_name[p_srb->SrbStatus],
				   p_srb->SrbStatus,
				   g_srb_function_name[p_srb->Function],
				   p_srb->Function,
				   p_srb->PathId,
				   p_srb->TargetId,
				   p_srb->Lun,
				   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );

		status = __srp_clean_send_descriptor( p_send_descriptor, p_srp_session );
		if ( status != IB_SUCCESS )
		{
			SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				("Failed to unmap FMR  Status = %d.\n", status) );
			// TODO: Kill session and inform port driver link down storportnotification
		}
		
		StorPortNotification( RequestComplete, p_dev_ext, p_srb );
	}

exit:
	SRP_EXIT( SRP_DBG_DATA );
	return status;
}

static 
ib_api_status_t
__srp_repost_io_request(
	IN	srp_session_t		*p_srp_session )
{
	srp_hba_t               *p_hba;
	srp_send_descriptor_t   *p_send_descriptor = NULL;
	srp_descriptors_t       *p_descriptors = &p_srp_session->descriptors;
	ib_api_status_t ib_status = IB_SUCCESS;

	SRP_ENTER( SRP_DBG_DATA );

	if ( !cl_qlist_count(&p_descriptors->pending_descriptors) ||
		(p_srp_session->connection.request_limit <= p_srp_session->connection.request_threashold) )
		goto exit;

#if DBG	
	{	/* statistics */
		uint32_t size = (uint32_t)cl_qlist_count(&p_descriptors->pending_descriptors);
		p_srp_session->x_pend_num++;
		p_srp_session->x_pend_total += size;
		if ( p_srp_session->x_pend_max < size )
			p_srp_session->x_pend_max = size;
	}
#endif	

	/* in case when the follows loop will release the last pending request for sending it,
		there will be race between it and StorPort, that can call srp_post_io_request
		just at that moment. In the "worst" case it will cause changing order between 2 posting.
		The flag 'repost_is_on' is intended for preventing ths case */
	cl_atomic_inc( &p_srp_session->repost_is_on );

	while (p_srp_session->connection.request_limit > p_srp_session->connection.request_threashold)
	{
		cl_list_item_t			*p_list_item;

		/* extract a pending descriptor, if any */
		cl_spinlock_acquire ( &p_descriptors->pending_list_lock );
		p_list_item = cl_qlist_remove_head( &p_descriptors->pending_descriptors );
		if ( p_list_item == cl_qlist_end( &p_descriptors->pending_descriptors ) )
		{
			cl_spinlock_release ( &p_descriptors->pending_list_lock );
			break;
		}
		cl_spinlock_release ( &p_descriptors->pending_list_lock );

		/* post the request */
		p_hba = p_srp_session->p_hba;
		p_send_descriptor = PARENT_STRUCT(p_list_item, srp_send_descriptor_t,list_item);
		ib_status = __srp_post_io_request( p_hba->p_ext, p_send_descriptor->p_srb, p_srp_session );
	}

	cl_atomic_dec( &p_srp_session->repost_is_on );

exit:
	SRP_EXIT( SRP_DBG_DATA );
	return ib_status;
}
	
static inline void
__srp_fix_request_limit(
	IN	srp_session_t		*p_srp_session,
	IN	srp_rsp_t 		*p_srp_rsp )
{
	int32_t rld = get_srp_response_request_limit_delta( p_srp_rsp );
	cl_atomic_add( &p_srp_session->connection.request_limit, rld );
#if DBG	
	/* statistics */
	p_srp_session->x_rld_num++;
	p_srp_session->x_rld_total += rld;
	if ( p_srp_session->x_rld_max < rld )
		p_srp_session->x_rld_max = rld;
	if ( p_srp_session->x_rld_min > rld )
		p_srp_session->x_rld_min = rld;
#endif	
}

static inline
ib_api_status_t
__srp_process_recv_completion(
	IN  srp_recv_descriptor_t   *p_recv_descriptor,
	IN  srp_session_t           *p_srp_session )
{
	ib_api_status_t         status = IB_SUCCESS;
	srp_rsp_t               *p_srp_rsp;
	uint8_t                 response_status;
	srp_send_descriptor_t   *p_send_descriptor;
	uint64_t				response_tag;
	BOOLEAN					session_recover = FALSE;

	SRP_ENTER( SRP_DBG_DATA );

	p_srp_rsp = (srp_rsp_t *)p_recv_descriptor->p_data_segment;

	set_srp_response_from_network_to_host( p_srp_rsp );

	response_status = get_srp_response_status( p_srp_rsp );
	response_tag = get_srp_response_tag( (srp_rsp_t *)p_recv_descriptor->p_data_segment );

	p_send_descriptor = srp_find_matching_send_descriptor(
		&p_srp_session->descriptors, response_tag );
	if ( p_send_descriptor == NULL )
	{
		/* Repost the recv descriptor */
		status = p_srp_session->p_hba->ifc.post_recv(
			p_srp_session->connection.h_qp, &p_recv_descriptor->wr, NULL );
		if ( status != IB_SUCCESS )
		{
			SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				("Failed to post send descriptor. Status = %d.\n", status) );
		}
		else
		{
		__srp_fix_request_limit( p_srp_session, p_srp_rsp );
		}
		SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
			("Matching Send Descriptor not Found: tag %#I64x\n", response_tag ) );

		if( status == IB_SUCCESS &&
			!cl_qlist_count( &p_srp_session->descriptors.sent_descriptors ) )
		{
			/* Seem all commands from sent queue were aborted by timeout already */
			/*	most likely Target get stuck. schedule session recovery */
			status = IB_ERROR;
	}
		return ( status );
	}

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			   ("Recv Completion Received for Function = %s(0x%x), "
			   "Path = 0x%x, Target = 0x%x, Lun = 0x%x, tag 0x%I64x\n",
			   g_srb_function_name[p_send_descriptor->p_srb->Function],
			   p_send_descriptor->p_srb->Function,
			   p_send_descriptor->p_srb->PathId,
			   p_send_descriptor->p_srb->TargetId,
			   p_send_descriptor->p_srb->Lun,
			   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );

	switch ( get_srp_iu_buffer_type( (srp_iu_buffer_t *)p_send_descriptor->data_segment ) )
	{
		case SRP_TSK_MGMT:
		{
			srp_tsk_mgmt_t  *p_srp_tsk_mgmt = (srp_tsk_mgmt_t *)p_send_descriptor->data_segment;

			set_srp_tsk_mgmt_from_network_to_host( p_srp_tsk_mgmt );

			if(response_status == SCSISTAT_GOOD)
			{
				p_send_descriptor->p_srb->SrbStatus = SRB_STATUS_SUCCESS;
			}
			else
			{
				p_send_descriptor->p_srb->SrbStatus = SRB_STATUS_ABORT_FAILED;
				SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
					   ("Scsi Error  %s (%#x)  Received for Function = %s(0x%x), "
					   "Path = 0x%x, Target = 0x%x, Lun = 0x%x, tag 0x%I64x\n",
					   g_srb_scsi_status_name[response_status],
					   response_status,
					   g_srb_function_name[p_send_descriptor->p_srb->Function],
					   p_send_descriptor->p_srb->Function,
					   p_send_descriptor->p_srb->PathId,
					   p_send_descriptor->p_srb->TargetId,
					   p_send_descriptor->p_srb->Lun,
					   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );
			}

			if ( get_srp_tsk_mgmt_task_management_flags( p_srp_tsk_mgmt ) == TMF_ABORT_TASK )
			{
				/* Repost the recv descriptor */
				status = p_srp_session->p_hba->ifc.post_recv(
					p_srp_session->connection.h_qp, &p_recv_descriptor->wr, NULL );
				if ( status != IB_SUCCESS )
				{
					session_recover = TRUE;
					SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
						("Failed to post recv descriptor. Status = %d.\n", status) );
				}

				}

			break;
		}

		case SRP_CMD:
			p_send_descriptor->p_srb->ScsiStatus = response_status;
			if(response_status == SCSISTAT_GOOD)
			{
				p_send_descriptor->p_srb->SrbStatus = SRB_STATUS_SUCCESS;
			}
			else
			{
				p_send_descriptor->p_srb->SrbStatus = SRB_STATUS_ABORT_FAILED;
				SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
					   ("Scsi Error  %s (%#x)  Received for Function = %s(0x%x), "
					   "Path = 0x%x, Target = 0x%x, Lun = 0x%x, tag 0x%I64x\n",
					   g_srb_scsi_status_name[response_status],
					   response_status,
					   g_srb_function_name[p_send_descriptor->p_srb->Function],
					   p_send_descriptor->p_srb->Function,
					   p_send_descriptor->p_srb->PathId,
					   p_send_descriptor->p_srb->TargetId,
					   p_send_descriptor->p_srb->Lun,
					   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );
			}

			if ( get_srp_response_flags( p_srp_rsp ) != 0 )
			{
				uint32_t    resid;

				if ( (response_status != SCSISTAT_CHECK_CONDITION) && get_srp_response_di_under( p_srp_rsp ) )
				{
					resid = get_srp_response_data_in_residual_count( p_srp_rsp );

					SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
						("DI Underflow in response: expected %d got %d.\n",
						p_send_descriptor->p_srb->DataTransferLength,
						p_send_descriptor->p_srb->DataTransferLength - resid) );

					p_send_descriptor->p_srb->DataTransferLength -= resid;

					if ( p_send_descriptor->p_srb->SrbStatus == SRB_STATUS_SUCCESS )
					{
						p_send_descriptor->p_srb->SrbStatus = SRB_STATUS_DATA_OVERRUN; /* Also for underrun see DDK */
					}
				}

				if ( (response_status != SCSISTAT_CHECK_CONDITION) && get_srp_response_do_under( p_srp_rsp ) )
				{
					resid = get_srp_response_data_out_residual_count( p_srp_rsp );

					SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
						("DI Underflow in response: expected %d got %d.\n",
						p_send_descriptor->p_srb->DataTransferLength,
						p_send_descriptor->p_srb->DataTransferLength - resid) );

					p_send_descriptor->p_srb->DataTransferLength -= resid;

					if ( p_send_descriptor->p_srb->SrbStatus == SRB_STATUS_SUCCESS )
					{
						p_send_descriptor->p_srb->SrbStatus = SRB_STATUS_DATA_OVERRUN; /* Also for underrun see DDK */
					}
				}

				if ( get_srp_response_sns_valid( p_srp_rsp ) )
				{
					uint8_t *p_sense_data = get_srp_response_sense_data( p_srp_rsp );

					/* Copy only as much of the sense data as we can hold. */
					cl_memcpy( p_send_descriptor->p_srb->SenseInfoBuffer,
							   p_sense_data,
							   MIN( get_srp_response_sense_data_list_length( p_srp_rsp ),
									p_send_descriptor->p_srb->SenseInfoBufferLength ) );
					SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
							("Sense Data  SENSE_KEY 0x%02x ADDITIONAL_SENSE_CODE"
							"0x%02x ADDITIONAL_SENSE_QUALIFIER 0x%02x.\n",
							p_sense_data[2],p_sense_data[12],p_sense_data[13]) );
					
					if ( ((p_sense_data[2]&0xf) == 0x0b /*ABORTED_COMMAND*/) &&
						(p_sense_data[12] == 0x08) &&
						(p_sense_data[13] == 0x00) )

					{
						/* probably a problem with the Vfx FC san like wire pull*/
						/* initiate session recovery */
						session_recover = TRUE;
						if( p_srp_session->p_hba->session_paused[p_srp_session->target_id] == FALSE )
						{
							p_srp_session->p_hba->session_paused[p_srp_session->target_id] = TRUE;
						SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
							("Sense Data indicates FC link connectivity has been lost.\n") );
							StorPortDeviceBusy( p_srp_session->p_hba->p_ext,
											SP_UNTAGGED,
											p_srp_session->target_id,
											SP_UNTAGGED,
											(ULONG)-1 );
					}
				}
				}

				if ( get_srp_response_di_over( p_srp_rsp ) || get_srp_response_do_over( p_srp_rsp ) )
				{
					SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
						("Overflow error in response.\n") );
					if ( p_send_descriptor->p_srb->SrbStatus == SRB_STATUS_SUCCESS )
					{
						p_send_descriptor->p_srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
					}
				}
			}

			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
				("DataBuffer = 0x%I64x.\n", MmGetPhysicalAddress(
				p_send_descriptor->p_srb->DataBuffer ).QuadPart) );

			/* Repost the recv descriptor */
			status = p_srp_session->p_hba->ifc.post_recv(
				p_srp_session->connection.h_qp, &p_recv_descriptor->wr, NULL );
			if ( status != IB_SUCCESS )
			{
				session_recover = TRUE;
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("Failed to post recv descriptor. Status = %d.\n", status) );
			}

			break;

		case SRP_LOGIN_REQ:
		case SRP_I_LOGOUT:
		case SRP_CRED_RSP:
		case SRP_AER_RSP:
		default:
			SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				("Illegal SRP IU CMD/RSP %#x received\n", 
				get_srp_iu_buffer_type( (srp_iu_buffer_t *)p_send_descriptor->data_segment ) ) );
			session_recover = TRUE;
			break;
	}

	status =  __srp_clean_send_descriptor( p_send_descriptor, p_srp_session );
	if ( status != IB_SUCCESS )
	{
		session_recover = TRUE;
	}

	if( session_recover == TRUE )
	{
		status = IB_ERROR;
	}
	else 
	{
		__srp_fix_request_limit( p_srp_session, p_srp_rsp );
		status = __srp_repost_io_request( p_srp_session );
	}

	__srp_dump_srb_info( p_send_descriptor);

	StorPortNotification( RequestComplete, p_srp_session->p_hba->p_ext, p_send_descriptor->p_srb );

	SRP_EXIT( SRP_DBG_DATA );

	return ( status );
}

static inline
void
__srp_process_session_recv_completions(
	IN  srp_session_t   *p_srp_session )
{
	ib_api_status_t status;
	ib_wc_t         *p_wc_done_list;
	ib_wc_t         *p_wc;

	SRP_ENTER( SRP_DBG_DATA );

	cl_obj_lock( &p_srp_session->obj );

	if ( p_srp_session->connection.state != SRP_CONNECTED )
	{
		cl_obj_unlock( &p_srp_session->obj );
		SRP_EXIT( SRP_DBG_DATA );
		return;
	}

	status = p_srp_session->p_hba->ifc.poll_cq(
		p_srp_session->connection.h_recv_cq,
		&p_srp_session->connection.p_wc_free_list,
		&p_wc_done_list );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("ib_poll_cq() failed!, status 0x%x\n", status) );

		p_srp_session->connection.state = SRP_CONNECT_FAILURE;

		SRP_EXIT( SRP_DBG_DATA );
		cl_obj_unlock( &p_srp_session->obj );
		return;
	}

	cl_obj_ref( &p_srp_session->obj );
	cl_obj_unlock( &p_srp_session->obj );

	while ( (p_wc = p_wc_done_list) != NULL )
	{
		srp_recv_descriptor_t   *p_recv_descriptor;

		/* Remove head from list */
		p_wc_done_list = p_wc->p_next;
		p_wc->p_next = NULL;

		p_recv_descriptor = (srp_recv_descriptor_t *)((uintn_t)p_wc->wr_id);

		if ( p_wc->status == IB_WCS_SUCCESS )
		{
			status = __srp_process_recv_completion( p_recv_descriptor, p_srp_session );
			if ( status != IB_SUCCESS )
			{
				p_srp_session->connection.state = SRP_CONNECT_FAILURE;
				cl_obj_deref( &p_srp_session->obj );
				return;
			}
		}
		else
		{
			if( p_wc->status != IB_WCS_WR_FLUSHED_ERR )
			{
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("Recv Completion with Error Status %s (vendore specific %#x)\n",
					p_srp_session->p_hba->ifc.get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific) );
			}
			else
			{
				SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
					("Recv Completion Flushed in Error Status: %s\n",
					p_srp_session->p_hba->ifc.get_wc_status_str( p_wc->status )));

			}

			p_srp_session->connection.state = SRP_CONNECT_FAILURE;
			cl_obj_deref( &p_srp_session->obj );
			return;
		}

		/* Put onto head of free list */
		cl_obj_lock( &p_srp_session->obj );
		p_wc->p_next = p_srp_session->connection.p_wc_free_list;
		p_srp_session->connection.p_wc_free_list = p_wc;
		cl_obj_unlock( &p_srp_session->obj );

		/* Get next completion */
		p_wc = p_wc_done_list;
	}

	/* Re-arm the CQ for more completions */
	status = p_srp_session->p_hba->ifc.rearm_cq(
		p_srp_session->connection.h_recv_cq, FALSE );
	if ( status != IB_SUCCESS)
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("ib_rearm_cq() failed!, status 0x%x\n", status) );
		p_srp_session->connection.state = SRP_CONNECT_FAILURE;

		cl_obj_deref( &p_srp_session->obj );
		return;
	}

	cl_obj_deref( &p_srp_session->obj );

	SRP_EXIT( SRP_DBG_DATA );
}

/* srp_recv_completion_cb */
/*!
Set Timer to Process Receive Completions - Responses to our requests

@param p_context - context pointer to the owning session

@return - none
*/
void
srp_recv_completion_cb(
	IN	const	ib_cq_handle_t		h_cq,
	IN			void				*p_context )
{
	srp_session_t	*p_srp_session = (srp_session_t *)p_context;

	SRP_ENTER( SRP_DBG_DATA );

	UNUSED_PARAM( h_cq );

	__srp_process_session_recv_completions( p_srp_session );
	if( p_srp_session->connection.state == SRP_CONNECT_FAILURE )
	{
		srp_session_failed( p_srp_session );
	}
	SRP_EXIT( SRP_DBG_DATA );
}

/* __srp_build_cmd */
/*!
Build the SRP Cmd to be sent to the VFx target

@param p_dev_ext - our context pointer
@param p_srb     - scsi request to send to target

@return - none
*/
static inline
BOOLEAN
__srp_build_cmd(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb,
	IN      srp_conn_info_t     *p_srp_conn_info )
{
	srp_send_descriptor_t           *p_send_descriptor = (srp_send_descriptor_t *)p_srb->SrbExtension;
	srp_cmd_t                       *p_srp_cmd = (srp_cmd_t *)p_send_descriptor->data_segment;
	UCHAR                           *p_cdb;
	PSTOR_SCATTER_GATHER_LIST       p_scatter_gather_list = NULL;
	uint8_t                         scatter_gather_count = 0;
	srp_memory_descriptor_t         *p_memory_descriptor = NULL;
	srp_memory_table_descriptor_t	*p_table_descriptor = NULL;
	uint32_t                        i;
	ULONG                           scsi_direction = p_srb->SrbFlags & ( SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT );
	DATA_BUFFER_DESCRIPTOR_FORMAT   format = p_srp_conn_info->descriptor_format & DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS;
	ULONG							length;
#if DBG
	srp_hba_t               *p_hba = ((srp_ext_t *)p_dev_ext)->p_hba;
	srp_session_t					*p_srp_session;
#endif	
	
	SRP_ENTER( SRP_DBG_DATA );

#if DBG
	/* statistics */
	p_srp_session = p_hba->session_list[p_send_descriptor->p_srb->TargetId];
	p_srp_session->x_pkt_built++;
#endif	

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			   ("Sending I/O to Path = 0x%x, Target = 0x%x, Lun = 0x%x\n",
			   p_srb->PathId,
			   p_srb->TargetId,
			   p_srb->Lun) );

	setup_srp_command( p_srp_cmd,
					   p_send_descriptor->tag,
					   DBDF_NO_DATA_BUFFER_DESCRIPTOR_PRESENT,
					   DBDF_NO_DATA_BUFFER_DESCRIPTOR_PRESENT,
					   0,
					   0,
					   BUILD_SCSI_ADDRESS( p_srb->Lun ),
					   TAV_SIMPLE_TASK,
					   0 );

	p_cdb = get_srp_command_cdb( p_srp_cmd );
	cl_memcpy( p_cdb, p_srb->Cdb, p_srb->CdbLength );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("CDB Length = %d.\n", p_srb->CdbLength) );
#if DBG
	{
		char*	cmd;
		cmd = cl_zalloc(p_srb->CdbLength +1);
		if(cmd)
		{	
			for ( i = 0; i < p_srb->CdbLength; i++ )
			{
				cmd[i] = p_srb->Cdb[i];
			}
			cmd[i] = '\0';
			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA, ("CDB = 0x%s\n",cmd) );

			cl_free(cmd);
		}
		
	}
#endif
	
	if ( !format )
	{
		format = DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR;
	}

	if ( scsi_direction )
	{
		p_scatter_gather_list = StorPortGetScatterGatherList( p_dev_ext, p_srb );
		CL_ASSERT( p_scatter_gather_list != NULL );
		scatter_gather_count = (uint8_t)p_scatter_gather_list->NumberOfElements;
	}

	/* Cap the length of the inline descriptors to the maximum IU size. */
	if( p_srp_conn_info->max_scatter_gather_entries < scatter_gather_count )
	{
		scatter_gather_count =
			(uint8_t)p_srp_conn_info->max_scatter_gather_entries;
	}

	if ( scsi_direction == SRB_FLAGS_DATA_IN )
	{
		set_srp_command_data_in_buffer_desc_fmt( p_srp_cmd, format );
		set_srp_command_data_in_buffer_desc_count( p_srp_cmd, scatter_gather_count );
		p_memory_descriptor = get_srp_command_data_in_buffer_desc( p_srp_cmd );
	}

	else if ( scsi_direction == SRB_FLAGS_DATA_OUT )
	{
		set_srp_command_data_out_buffer_desc_fmt( p_srp_cmd, format );
		set_srp_command_data_out_buffer_desc_count( p_srp_cmd, scatter_gather_count );
		p_memory_descriptor = get_srp_command_data_out_buffer_desc( p_srp_cmd );
	}

#if DBG
	{ /* print max SG list, gotten from the StorPort */
		static ULONG s_sg_max = 0;
		if ( p_scatter_gather_list && s_sg_max < p_scatter_gather_list->NumberOfElements )
		{
			uint32_t				total = 0;
			PSTOR_SCATTER_GATHER_ELEMENT    p_sg_el;
			for ( i = 0, p_sg_el = p_scatter_gather_list->List;
				  i < scatter_gather_count; i++, p_sg_el++ )
			{
				total += p_sg_el->Length;
			}
			s_sg_max = p_scatter_gather_list->NumberOfElements;
			SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA, 
				( "StorPort sg_cnt %d, total %#x, max sg_cnt %d, direction %s\n",
				s_sg_max, total, p_srp_conn_info->max_scatter_gather_entries,
				( scsi_direction == SRB_FLAGS_DATA_IN ) ? "IN" : "OUT" ));
		}
	}
#endif	

	if ( p_memory_descriptor != NULL )
	{
		PSTOR_SCATTER_GATHER_ELEMENT    p_sg_element;
		uint32_t						totalLength;
		uint64_t						buf_addr;
		if ( format == DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS )
		{
			p_table_descriptor = (srp_memory_table_descriptor_t *)p_memory_descriptor;
			p_memory_descriptor = ( srp_memory_descriptor_t *)(p_table_descriptor + 1 );

			buf_addr = (StorPortGetPhysicalAddress( p_dev_ext,p_srb, p_memory_descriptor, &length)).QuadPart;

			/* we don't swap rkey - it is already in network order*/
			p_table_descriptor->descriptor.virtual_address =  cl_hton64( buf_addr );
			p_table_descriptor->descriptor.memory_handle = p_srp_conn_info->rkey;

			if((p_scatter_gather_list->NumberOfElements > 1) && !__srp_map_fmr(p_dev_ext,p_scatter_gather_list,p_send_descriptor,p_memory_descriptor))
			{
				/* Set the discriptor list len */
				p_table_descriptor->descriptor.data_length =
					cl_hton32( sizeof(srp_memory_descriptor_t) *1);
				p_table_descriptor->total_length = p_memory_descriptor->data_length;
				if ( scsi_direction == SRB_FLAGS_DATA_IN )
					set_srp_command_data_in_buffer_desc_count( p_srp_cmd, 1 );
				else if ( scsi_direction == SRB_FLAGS_DATA_OUT )
					set_srp_command_data_out_buffer_desc_count( p_srp_cmd, 1 );

				SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
					("virtual_address[%d] = 0x%I64x.\n",
					0, cl_ntoh64(p_memory_descriptor->virtual_address) ) );
				SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
					("memory_handle[%d]   = 0x%x.\n",
					0, cl_ntoh32( p_memory_descriptor->memory_handle) ) );
				SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
					("data_length[%d]	  = %d.\n",
					0, cl_ntoh32( p_memory_descriptor->data_length) ) );
			}
			else
			{
				CL_ASSERT( scatter_gather_count ==
					p_scatter_gather_list->NumberOfElements );

				/* Set the descriptor list len */
				p_table_descriptor->descriptor.data_length =
					cl_hton32( sizeof(srp_memory_descriptor_t) *
					p_scatter_gather_list->NumberOfElements );

				for ( i = 0, totalLength = 0, p_sg_element = p_scatter_gather_list->List;
					  i < scatter_gather_count;
					  i++, p_memory_descriptor++, p_sg_element++ )
				{
					buf_addr =  p_srp_conn_info->vaddr + p_sg_element->PhysicalAddress.QuadPart;
				
					p_memory_descriptor->virtual_address = cl_hton64( buf_addr );
					p_memory_descriptor->memory_handle   = p_srp_conn_info->rkey;
					p_memory_descriptor->data_length     =  cl_hton32( p_sg_element->Length );
					totalLength += p_sg_element->Length;
					
					SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
						("virtual_address[%d] = 0x%I64x.\n",
						i, cl_ntoh64(p_memory_descriptor->virtual_address) ) );
					SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
						("memory_handle[%d]   = 0x%x.\n",
						i, cl_ntoh32( p_memory_descriptor->memory_handle) ) );
					SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
						("data_length[%d]     = %d.\n",
						i, cl_ntoh32( p_memory_descriptor->data_length) ) );
				}
				p_table_descriptor->total_length = cl_hton32( totalLength );
			}
		}
		else if ( format == DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR )
		{
			CL_ASSERT( scatter_gather_count ==
				p_scatter_gather_list->NumberOfElements );
			if((p_scatter_gather_list->NumberOfElements > 1) && !__srp_map_fmr(p_dev_ext,p_scatter_gather_list,p_send_descriptor,p_memory_descriptor))
			{
				if ( scsi_direction == SRB_FLAGS_DATA_IN )
					set_srp_command_data_in_buffer_desc_count( p_srp_cmd, 1 );
				else if ( scsi_direction == SRB_FLAGS_DATA_OUT )
					set_srp_command_data_out_buffer_desc_count( p_srp_cmd, 1 );
			}
			else
			{
				for ( i = 0, p_sg_element = p_scatter_gather_list->List;
					  i < scatter_gather_count;	i++, p_memory_descriptor++, p_sg_element++ )
				{
					buf_addr =  p_srp_conn_info->vaddr + p_sg_element->PhysicalAddress.QuadPart;
					p_memory_descriptor->virtual_address = cl_hton64( buf_addr );
					p_memory_descriptor->memory_handle   = p_srp_conn_info->rkey;
					p_memory_descriptor->data_length     = cl_hton32( p_sg_element->Length );

					SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
						("virtual_address[%d] = 0x%I64x.\n",
						i, cl_ntoh64(p_memory_descriptor->virtual_address) ) );
					SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
						("memory_handle[%d]   = 0x%x.\n",
						i, cl_ntoh32( p_memory_descriptor->memory_handle) ) );
					SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
						("data_length[%d]     = %d.\n",
						i, cl_ntoh32( p_memory_descriptor->data_length) ) );
				}
			}
		}
		SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
			("scatter/gather count = %d.\n", scatter_gather_count));
	}

	p_srp_cmd->logical_unit_number = cl_hton64( p_srp_cmd->logical_unit_number );

	//set_srp_command_from_host_to_network( p_srp_cmd );

	SRP_EXIT( SRP_DBG_DATA );
	return TRUE;
}

/* srp_format_io_request */
/*!
Format the SRP Cmd for the VFx target

@param p_dev_ext - our context pointer
@param p_srb     - scsi request to send to target

@return - TRUE for success, FALSE for failure
*/
BOOLEAN
srp_format_io_request(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb  )
{
	srp_hba_t       *p_hba = ((srp_ext_t *)p_dev_ext)->p_hba;
	BOOLEAN         result = TRUE;
	srp_session_t   *p_srp_session;

	SRP_ENTER( SRP_DBG_DATA );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("Device Extension Address = %p\n", p_dev_ext) );
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("Srb Address              = %p\n", p_srb) );
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("Srb DataBuffer Address   = %p\n", p_srb->DataBuffer) );
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
		("Srb DataTransferLength   = %d\n", p_srb->DataTransferLength) );

	cl_obj_lock( &p_hba->obj );

	p_srp_session = p_hba->session_list[p_srb->TargetId];

	if ( p_srp_session != NULL && 
		 p_srp_session->connection.state == SRP_CONNECTED &&
		 !p_hba->session_paused[p_srb->TargetId] )
	{
		srp_conn_info_t srp_conn_info;

		cl_obj_ref( &p_srp_session->obj );
		cl_obj_unlock( &p_hba->obj );

		srp_conn_info.vaddr = p_srp_session->hca.vaddr;
		srp_conn_info.lkey  = p_srp_session->hca.lkey;
		srp_conn_info.rkey  = p_srp_session->hca.rkey;
		srp_conn_info.descriptor_format = p_srp_session->connection.descriptor_format;
		srp_conn_info.init_to_targ_iu_sz         = p_srp_session->connection.init_to_targ_iu_sz;
		srp_conn_info.max_scatter_gather_entries = p_srp_session->connection.max_scatter_gather_entries;
		srp_conn_info.tag                        = cl_atomic_inc( &p_srp_session->connection.tag );
		srp_conn_info.signal_send_completion     =
			((srp_conn_info.tag % p_srp_session->connection.signaled_send_completion_count) == 0) ? TRUE : FALSE;

		cl_obj_deref( &p_srp_session->obj );

		srp_build_send_descriptor( p_dev_ext, p_srb, &srp_conn_info );

		result = __srp_build_cmd( p_dev_ext, p_srb, &srp_conn_info );
		
		if( result != TRUE )
			SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				("BUILD command %#x failed = %#x tag %#I64x\n",
				p_srb->Cdb[0], p_srb->SrbStatus, srp_conn_info.tag ) );
	}
	else
	{
		// Handle the error case here
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Find Session For Target ID = %d\n", p_srb->TargetId) );

		p_srb->SrbStatus = SRB_STATUS_INVALID_TARGET_ID;
		cl_obj_unlock( &p_hba->obj );
		result = FALSE;
	}

	SRP_EXIT( SRP_DBG_DATA );
	return ( result );
}

void
srp_post_io_request(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb  )
{
	ib_api_status_t			status = IB_SUCCESS;
	srp_hba_t               *p_hba = ((srp_ext_t *)p_dev_ext)->p_hba;
	srp_send_descriptor_t	*p_send_descriptor = (srp_send_descriptor_t *)p_srb->SrbExtension;
	srp_session_t           *p_srp_session;
	srp_descriptors_t       *p_descriptors;
	
	SRP_ENTER( SRP_DBG_DATA );

	cl_obj_lock( &p_hba->obj );

	p_srp_session = p_hba->session_list[p_srb->TargetId];

	if( p_hba->session_paused[p_srb->TargetId] == TRUE )
	{
		cl_obj_unlock( &p_hba->obj );
		p_srb->SrbStatus = SRB_STATUS_BUSY;
		goto err;
	}

	if ( p_srp_session != NULL && 
		 p_srp_session->connection.state == SRP_CONNECTED )
	{
		cl_obj_ref( &p_srp_session->obj );
		cl_obj_unlock( &p_hba->obj );

		p_descriptors = &p_srp_session->descriptors;

		cl_spinlock_acquire ( &p_descriptors->pending_list_lock );
		if ( (p_srp_session->connection.request_limit <= p_srp_session->connection.request_threashold) || 
			!cl_is_qlist_empty( &p_descriptors->pending_descriptors ) ||
			p_srp_session->repost_is_on )
		{
			int32_t num_pending_desc = (int32_t)cl_qlist_count( &p_descriptors->pending_descriptors );
			cl_spinlock_release ( &p_descriptors->pending_list_lock );
			srp_add_pending_descriptor( p_descriptors, p_send_descriptor );
			
			/* don't allow pending queue grow indefinitely */
			if( num_pending_desc >= p_srp_session->connection.max_limit )
			{		
				StorPortDeviceBusy( p_dev_ext, 
									p_srb->PathId,
									p_srb->TargetId,
									p_srb->Lun,
									1 );
			}

			cl_obj_deref( &p_srp_session->obj );
			goto exit;
		}
		cl_spinlock_release ( &p_descriptors->pending_list_lock );

		status = __srp_post_io_request( p_dev_ext, p_srb, p_srp_session );
		cl_obj_deref( &p_srp_session->obj );
		goto exit;
	}
	else
	{
		cl_obj_unlock( &p_hba->obj );
		p_srb->SrbStatus = SRB_STATUS_NO_HBA;
		goto err;
	}

err:
	SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			   ("Returning SrbStatus %s(0x%x) for Function = %s(0x%x), Path = 0x%x, "
			   "Target = 0x%x, Lun = 0x%x, tag 0x%I64x\n",
			   g_srb_status_name[p_srb->SrbStatus],
			   p_srb->SrbStatus,
			   g_srb_function_name[p_srb->Function],
			   p_srb->Function,
			   p_srb->PathId,
			   p_srb->TargetId,
			   p_srb->Lun,
			   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );

	status = __srp_clean_send_descriptor( p_send_descriptor, p_srp_session );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Failed to unmap FMR  Status = %d.\n", status) );
		// TODO: Kill session and inform port driver link down storportnotification
	}
	
	StorPortNotification( RequestComplete, p_dev_ext, p_srb );

exit:
	if( status != IB_SUCCESS )
	{
		p_srp_session->connection.state = SRP_CONNECT_FAILURE;
		srp_session_failed( p_srp_session );
	}
	SRP_EXIT( SRP_DBG_DATA );
}

void
srp_abort_command(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb  )
{
	srp_hba_t               *p_hba = ((srp_ext_t *)p_dev_ext)->p_hba;
	srp_session_t           *p_srp_session;
	uint64_t                iu_tag;
	srp_send_descriptor_t   *p_srb_send_descriptor;
	srp_send_descriptor_t   *p_send_descriptor;
	srp_conn_info_t         srp_conn_info;
	srp_tsk_mgmt_t          *p_srp_tsk_mgmt;

	SRP_ENTER( SRP_DBG_DATA );

	cl_obj_lock( &p_hba->obj );

	p_srp_session = p_hba->session_list[p_srb->TargetId];
	if ( p_srp_session == NULL )
	{
		/* If the session is NULL there is no connection and cannot be aborted */
		p_srb->SrbStatus = SRB_STATUS_ABORT_FAILED;
		goto exit;
	}

	p_srb_send_descriptor = (srp_send_descriptor_t *)p_srb->NextSrb->SrbExtension;

	iu_tag = get_srp_information_unit_tag( (srp_information_unit_t *)p_srb_send_descriptor->data_segment );

	p_send_descriptor = srp_find_matching_pending_descriptor(&p_srp_session->descriptors, iu_tag );
	
	/*complete pending locally as failed */
	if( p_send_descriptor != NULL )
	{
		p_srb->SrbStatus = SRB_STATUS_ABORT_FAILED;
		p_srb->ScsiStatus = SCSISTAT_COMMAND_TERMINATED;

		__srp_clean_send_descriptor( p_send_descriptor, p_srp_session );
		goto exit;
	}

	p_send_descriptor = srp_find_matching_send_descriptor( &p_srp_session->descriptors, iu_tag );
	if ( p_send_descriptor == NULL )
	{
		/* Cannot find the command so it must have been completed */
		p_srb->SrbStatus = SRB_STATUS_ABORT_FAILED;
		goto exit;
	}

	CL_ASSERT( p_srb_send_descriptor == p_send_descriptor );

	if( __srp_clean_send_descriptor( p_send_descriptor, p_srp_session ) != IB_SUCCESS )
	{
		p_srb->SrbStatus = SRB_STATUS_ABORT_FAILED;
		goto exit;
	}

	p_srb->NextSrb->SrbStatus = SRB_STATUS_ABORTED;

	/* create and send abort request to the VFx */

	srp_conn_info.vaddr = p_srp_session->hca.vaddr;
	srp_conn_info.lkey  = p_srp_session->hca.lkey;
	srp_conn_info.rkey  = p_srp_session->hca.rkey;

	srp_conn_info.init_to_targ_iu_sz         = p_srp_session->connection.init_to_targ_iu_sz;
	srp_conn_info.max_scatter_gather_entries = p_srp_session->connection.max_scatter_gather_entries;
	srp_conn_info.tag                        = cl_atomic_inc( &p_srp_session->connection.tag );
	srp_conn_info.signal_send_completion     =
		((srp_conn_info.tag % p_srp_session->connection.signaled_send_completion_count) == 0) ? TRUE : FALSE;

	srp_build_send_descriptor( p_dev_ext, p_srb, &srp_conn_info );

	p_srp_tsk_mgmt = (srp_tsk_mgmt_t *)p_send_descriptor->data_segment;

	setup_srp_tsk_mgmt( p_srp_tsk_mgmt,
						p_send_descriptor->tag,
						BUILD_SCSI_ADDRESS( p_srb->Lun ),
						TMF_ABORT_TASK,
						iu_tag );

	set_srp_tsk_mgmt_from_host_to_network( p_srp_tsk_mgmt );

	srp_post_io_request( p_dev_ext, p_srb );

exit:
	cl_obj_unlock( &p_hba->obj );
	if ( p_srb->SrbStatus == SRB_STATUS_ABORT_FAILED )
	{
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
				   ("Returning SrbStatus %s(0x%x) for Function = %s(0x%x), "
				   "Path = 0x%x, Target = 0x%x, Lun = 0x%x\n",
				   g_srb_status_name[p_srb->SrbStatus],
				   p_srb->SrbStatus,
				   g_srb_function_name[p_srb->Function],
				   p_srb->Function,
				   p_srb->PathId,
				   p_srb->TargetId,
				   p_srb->Lun) );
		StorPortNotification( RequestComplete, p_dev_ext, p_srb );
	}

	SRP_EXIT( SRP_DBG_DATA );
}

void
srp_lun_reset(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb  )
{

	ib_api_status_t         status = IB_SUCCESS;
	srp_hba_t               *p_hba = ((srp_ext_t *)p_dev_ext)->p_hba;
	srp_session_t           *p_srp_session;

	SRP_ENTER( SRP_DBG_DATA );

	cl_obj_lock( &p_hba->obj );

	p_srp_session = p_hba->session_list[p_srb->TargetId];
	if ( p_srp_session != NULL )
	{
		srp_send_descriptor_t   *p_send_descriptor;
		UCHAR                   path_id   = p_srb->PathId;
		UCHAR                   target_id = p_srb->TargetId;
		UCHAR                   lun       = p_srb->Lun;

	    StorPortPauseDevice( p_dev_ext, p_srb->PathId, p_srb->TargetId, p_srb->Lun, 10 );

		/* release this device' descriptors from the pending_list */
		while ( (p_send_descriptor = srp_remove_lun_head_pending_descriptor( &p_srp_session->descriptors, p_srb->Lun )) != NULL )
		{
			status = __srp_clean_send_descriptor( p_send_descriptor, p_srp_session );
			if ( status != IB_SUCCESS )
			{
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("Failed to unmap FMR  Status = %d.\n", status) );
				// TODO: Kill session and inform port driver link down storportnotification
			}
			
			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
					   ("Returning SrbStatus %s(0x%x) for Function = %s(0x%x), "
					   "Path = 0x%x, Target = 0x%x, Lun = 0x%x, tag 0x%I64xn",
					   g_srb_status_name[SRB_STATUS_BUS_RESET],
					   SRB_STATUS_BUS_RESET,
					   g_srb_function_name[p_send_descriptor->p_srb->Function],
					   p_send_descriptor->p_srb->Function,
					   p_send_descriptor->p_srb->PathId,
					   p_send_descriptor->p_srb->TargetId,
					   p_send_descriptor->p_srb->Lun,
					   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );
		}

		/* release this device' descriptors from the sent_list */
		while ( (p_send_descriptor = srp_remove_lun_head_send_descriptor( &p_srp_session->descriptors, p_srb->Lun )) != NULL )
		{
			status = __srp_clean_send_descriptor( p_send_descriptor, p_srp_session );
			if ( status != IB_SUCCESS )
			{
				SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
					("Failed to unmap FMR  Status = %d.\n", status) );
				// TODO: Kill session and inform port driver link down storportnotification
			}
			
			SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DATA,
					   ("Returning SrbStatus %s(0x%x) for Function = %s(0x%x), "
					   "Path = 0x%x, Target = 0x%x, Lun = 0x%x, tag 0x%I64xn",
					   g_srb_status_name[SRB_STATUS_BUS_RESET],
					   SRB_STATUS_BUS_RESET,
					   g_srb_function_name[p_send_descriptor->p_srb->Function],
					   p_send_descriptor->p_srb->Function,
					   p_send_descriptor->p_srb->PathId,
					   p_send_descriptor->p_srb->TargetId,
					   p_send_descriptor->p_srb->Lun,
					   get_srp_command_tag( (srp_cmd_t *)p_send_descriptor->data_segment )) );
		}


		p_srb->SrbStatus = SRB_STATUS_SUCCESS;

		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
				   ("Returning SrbStatus %s(0x%x) for Function = %s(0x%x), "
				   "Path = 0x%x, Target = 0x%x, Lun = 0x%x\n",
				   g_srb_status_name[p_srb->SrbStatus],
				   p_srb->SrbStatus,
				   g_srb_function_name[p_srb->Function],
				   p_srb->Function,
				   p_srb->PathId,
				   p_srb->TargetId,
				   p_srb->Lun) );

		StorPortNotification( RequestComplete, p_dev_ext, p_srb );

		StorPortCompleteRequest( p_dev_ext, path_id, target_id, lun, SRB_STATUS_BUS_RESET );

		StorPortResumeDevice( p_dev_ext, path_id, target_id, lun );
	}
	else
	{
		// Handle the error case here
		p_srb->SrbStatus = SRB_STATUS_INVALID_TARGET_ID;
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DATA,
				   ("Returning SrbStatus %s(0x%x) for Function = %s(0x%x), "
				   "Path = 0x%x, Target = 0x%x, Lun = 0x%x\n",
				   g_srb_status_name[p_srb->SrbStatus],
				   p_srb->SrbStatus,
				   g_srb_function_name[p_srb->Function],
				   p_srb->Function,
				   p_srb->PathId,
				   p_srb->TargetId,
				   p_srb->Lun) );

		StorPortNotification( RequestComplete, p_dev_ext, p_srb );
	}

	cl_obj_unlock( &p_hba->obj );

	SRP_EXIT( SRP_DBG_DATA );
}

#if DBG	

/* statistics */

void
srp_x_clean(
	IN	void		*p_session )
{
	srp_session_t		*p_srp_session = p_session;

	if (p_srp_session == NULL ||
		p_srp_session->connection.state != SRP_CONNECTED )
		return;

	p_srp_session->x_pkt_fmr = 0;
	p_srp_session->x_pkt_built = 0;
	p_srp_session->x_rld_total = 0;
	p_srp_session->x_rld_num = 0;
	p_srp_session->x_rld_max = 0;
	p_srp_session->x_rld_min = p_srp_session->x_req_limit;
	p_srp_session->x_pend_total = 0;
	p_srp_session->x_pend_num = 0;
	p_srp_session->x_pend_max = 0;
	p_srp_session->x_sent_total = 0;
	p_srp_session->x_sent_num = 0;
	p_srp_session->x_sent_max = 0;
}

void
srp_x_print(
	IN	void		*p_session )
{
	srp_session_t		*p_srp_session = p_session;

	if (p_srp_session == NULL || 
		p_srp_session->connection.state != SRP_CONNECTED )
		return;

	SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_DATA,
		("req_limit %d, pkt_built %d, pkt_fmr'ed %d\n",
		p_srp_session->x_req_limit, 
		p_srp_session->x_pkt_built,
		p_srp_session->x_pkt_fmr ));

	SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_DATA,
		("request_limit_delta: max %d, min %d, average %d, num %d\n", 
		p_srp_session->x_rld_max, p_srp_session->x_rld_min, 
		(p_srp_session->x_rld_num) ? p_srp_session->x_rld_total / p_srp_session->x_rld_num : 0,
		p_srp_session->x_rld_num ));

	SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_DATA,
		("pendinq_desc: max %d, average %d, num %d\n", 
		p_srp_session->x_pend_max, 
		(p_srp_session->x_pend_num) ? p_srp_session->x_pend_total / p_srp_session->x_pend_num : 0,
		p_srp_session->x_pend_num ));

	SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_DATA,
		("sent_desc: max %d, average %d, num %d\n", 
		p_srp_session->x_sent_max, 
		(p_srp_session->x_sent_num) ? p_srp_session->x_sent_total / p_srp_session->x_sent_num : 0,
		p_srp_session->x_sent_num ));

}

#endif
