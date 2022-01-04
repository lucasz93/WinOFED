/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#include "srp_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srp_hca.tmh"
#endif
#include "srp_event.h"
#include "srp_hca.h"
#include "srp_session.h"

/* Amount of physical memory to register. */
#define MEM_REG_SIZE    0xFFFFFFFFFFFFFFFF


/* srp_open_ca */
/*!
Open the channel adapter associated with the SRP initiator
Allocates a protection domain and
Registers all of physical memory

@param p_hca      - pointer to the hca structure
@param p_context  - context pointer passed back to callback functions

@return - result of operation
*/
ib_api_status_t
srp_open_ca(
	IN OUT  srp_hca_t               *p_hca,
	IN      void                    *p_context )
{
	ib_api_status_t     status;
	ib_phys_create_t    phys_create;
	ib_phys_range_t     phys_range;
	mlnx_fmr_pool_create_t	fmr_pool_create;
	
	SRP_ENTER( SRP_DBG_PNP );

	status = p_hca->p_hba->ifc.open_ca( p_hca->p_hba->h_al,
		p_hca->p_hba->info.ca_guid, srp_async_event_handler_cb,
		p_context, &p_hca->h_ca );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Failed to open Channel Adapter. Status = %d\n", status) );
		goto exit;
	}

	status = p_hca->p_hba->ifc.alloc_pd( p_hca->h_ca,
						  IB_PDT_NORMAL,
						  p_context,
						  &p_hca->h_pd );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Failed to create Protection Domain. Status = %d\n", status) );
		goto exit;
	}

	/* Register all of physical memory */
	phys_create.length = MEM_REG_SIZE;
	phys_create.num_ranges = 1;
	phys_create.range_array = &phys_range;
	phys_create.buf_offset = 0;
	phys_create.hca_page_size = PAGE_SIZE;
	phys_create.access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_RDMA_READ | IB_AC_RDMA_WRITE | IB_AC_MW_BIND;

	phys_range.base_addr = 0;
	phys_range.size = MEM_REG_SIZE;

	p_hca->vaddr = 0;

	
	status = p_hca->p_hba->ifc.reg_phys( p_hca->h_pd,
						  &phys_create,
						  &p_hca->vaddr,
						  &p_hca->lkey,
						  &p_hca->rkey,
						  &p_hca->h_mr );

	if( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Physical Memory Registration Failure. Status = %d\n", status) );
		goto exit;
	}

	if ( g_srp_mode_flags & SRP_MODE_NO_FMR_POOL )
	{
		SRP_EXIT( SRP_DBG_PNP );
		return IB_SUCCESS;
	}

	fmr_pool_create.max_pages_per_fmr = SRP_MAX_SG_IN_INDIRECT_DATA_BUFFER;
	fmr_pool_create.page_size = 12;
	fmr_pool_create.access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_RDMA_READ | IB_AC_RDMA_WRITE;
	fmr_pool_create.pool_size = 100;
	fmr_pool_create.dirty_watermark = 2;
	fmr_pool_create.flush_function = NULL;
	fmr_pool_create.flush_arg = NULL;
	fmr_pool_create.cache = TRUE;

	status = p_hca->p_hba->ifc.create_mlnx_fmr_pool(p_hca->h_pd, &fmr_pool_create, &p_hca->h_fmr_pool);

	if( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("FMR pool creation Failure. Status = %d\n", status) );
		goto exit;
	}

	p_hca->fmr_page_size = 1<< fmr_pool_create.page_size;
	p_hca->fmr_page_shift = (uint32_t)fmr_pool_create.page_size;
	
	SRP_EXIT( SRP_DBG_PNP );
	return IB_SUCCESS;
exit:
	srp_close_ca( p_hca );
	
	SRP_EXIT( SRP_DBG_PNP );
	return ( status );
}

/* srp_close_ca */
/*!
Closes the channel adapter

@param p_hca - pointer to the hca structure

@return - none
*/
void
srp_close_ca(
	IN OUT  srp_hca_t	*p_hca )
{
	SRP_ENTER( SRP_DBG_PNP );

	if( p_hca->h_ca )
	{
		p_hca->p_hba->ifc.close_ca( p_hca->h_ca, ib_sync_destroy );
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("Closed Channel Adapter.\n") );
	}

		cl_memclr( p_hca, sizeof( *p_hca ) );

	SRP_EXIT( SRP_DBG_PNP );
}

/* srp_get_responder_resources */
/*!
Queries the channel adapter for the number of
outstanding atomic/rdma operations it supports

@param p_hca                 - pointer to the hca structure
@param p_responder_resources - value to hold responder resource count

@return - result of operation
*/
ib_api_status_t
srp_get_responder_resources(
	IN  srp_hca_t   *p_hca,
	OUT uint8_t     *p_responder_resources )
{
	ib_api_status_t	status;
	ib_ca_attr_t	*p_ca_attr = NULL;
	uint32_t		ca_attr_size = 0;

	SRP_ENTER( SRP_DBG_PNP );

	status = p_hca->p_hba->ifc.query_ca( p_hca->h_ca, NULL, &ca_attr_size );
	if ( status != IB_INSUFFICIENT_MEMORY )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Query Channel Adapter. Status = %d\n", status) );
		goto exit;
	}

	p_ca_attr = cl_zalloc( ca_attr_size );
	if ( p_ca_attr == NULL )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				   ("Memory Allocation Error: Cannot Create CA Attributes.\n") );
		goto exit;
	}

	status = p_hca->p_hba->ifc.query_ca( p_hca->h_ca, p_ca_attr, &ca_attr_size );
	if ( status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Cannot Query Channel Adapter. Status = %d\n", status) );
	}
	else
	{
		*p_responder_resources = p_ca_attr->max_qp_resp_res;
	}

	cl_free ( p_ca_attr );

exit:
	SRP_EXIT( SRP_DBG_PNP );

	return ( status );
}

/* srp_init_hca */
/*!
Initializes hca resources

@param p_hca  - pointer to the hca structure

@return -  result of initialization
*/
ib_api_status_t
srp_init_hca(
	IN	OUT			srp_hca_t					*p_hca,
	IN				srp_hba_t					*p_hba )
{
	SRP_ENTER( SRP_DBG_PNP );

	cl_memclr( p_hca, sizeof( *p_hca ) );

	p_hca->p_hba = p_hba;

	SRP_EXIT( SRP_DBG_PNP );

	return ( IB_SUCCESS );
}



