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
#include "srp_session.tmh"
#endif
#include "srp_session.h"
#include <stdlib.h>

/* __srp_destroying_session */
/*!
Called when session has been marked for destruction

@param p_obj - pointer to a session object

@return - none
*/
static void
__srp_destroying_session(
	IN  cl_obj_t    *p_obj )
{
	srp_session_t   *p_srp_session;

	SRP_ENTER( SRP_DBG_SESSION );

	p_srp_session = PARENT_STRUCT( p_obj, srp_session_t, obj );

	cl_obj_lock( &p_srp_session->obj );
	if( p_srp_session->connection.state != SRP_CONNECT_FAILURE )
	{
		cl_obj_unlock( &p_srp_session->obj );
		return;
	}

	p_srp_session->connection.state = SRP_CONNECTION_CLOSING;
	cl_obj_unlock( &p_srp_session->obj );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("Session Object ref_cnt = %d\n", p_srp_session->obj.ref_cnt) );

	SRP_EXIT( SRP_DBG_SESSION );
}


/* __srp_cleanup_session */
/*!
Called when session is being destroyed
in order to perform resource deallocation

@param p_obj - pointer to a session object

@return - none
*/
void
__srp_cleanup_session(
	IN  cl_obj_t    *p_obj )
{
	srp_session_t   *p_srp_session;

	SRP_ENTER( SRP_DBG_SESSION );

	p_srp_session = PARENT_STRUCT( p_obj, srp_session_t, obj );

	srp_close_ca( &p_srp_session->hca );

	if ( p_srp_session->p_shutdown_srb != NULL )
	{
		p_srp_session->p_shutdown_srb->SrbStatus = SRB_STATUS_SUCCESS;
		SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
			("Returning SrbStatus %s(0x%x) for Function = %s(0x%x), "
			"Path = 0x%x, Target = 0x%x, Lun = 0x%x\n",
			g_srb_status_name[p_srp_session->p_shutdown_srb->SrbStatus],
			p_srp_session->p_shutdown_srb->SrbStatus,
			g_srb_function_name[p_srp_session->p_shutdown_srb->Function],
			p_srp_session->p_shutdown_srb->Function,
			p_srp_session->p_shutdown_srb->PathId,
			p_srp_session->p_shutdown_srb->TargetId,
			p_srp_session->p_shutdown_srb->Lun) );
		StorPortNotification( RequestComplete, p_srp_session->p_hba->p_ext,
			p_srp_session->p_shutdown_srb );
	}

	srp_free_connection( &p_srp_session->connection );
	srp_destroy_descriptors( &p_srp_session->descriptors );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("Session Object ref_cnt = %d\n", p_srp_session->obj.ref_cnt) );

	SRP_EXIT( SRP_DBG_SESSION );
}

/* __srp_free_session */
/*!
Called when session has been destroyed
and is ready for deallocation

@param p_obj - pointer to a session object

@return - none
*/
static void
__srp_free_session(
	IN  cl_obj_t    *p_obj )
{
	srp_session_t   *p_srp_session;

	SRP_ENTER( SRP_DBG_SESSION );

	p_srp_session = PARENT_STRUCT( p_obj, srp_session_t, obj );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("Before DeInit Session Object ref_cnt = %d\n",
		p_srp_session->obj.ref_cnt) );

	cl_obj_deinit( p_obj );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG, 
		("After DeInit Session Object ref_cnt = %d\n",
		p_srp_session->obj.ref_cnt) );

	cl_free( p_srp_session );

	SRP_EXIT( SRP_DBG_SESSION );
}

/* __srp_validate_service_entry */
/*!
Validates the format of the Service Name and
Converts and returns the  id extension encoded
within the service name string

@param p_svc_entry           - pointer to the service entry
@param p_target_id_extension - pointer value to hold the returned id extension

@return - result of operation
*/
static ib_api_status_t
__srp_validate_service_entry(
	IN      ib_svc_entry_t  *p_svc_entry,
	OUT     uint64_t        *p_target_id_extension )
{
	ib_api_status_t status = IB_SUCCESS;
	char            target_id_extension[SRP_EXTENSION_ID_LENGTH + 1];
	size_t          target_id_extension_size;
	uint64_t        multiplier = 1;
	ULONG           id_extension;

	SRP_ENTER( SRP_DBG_SESSION );

	if ( cl_memcmp( p_svc_entry->name, SRP_SERVICE_NAME_PREFIX, strlen(SRP_SERVICE_NAME_PREFIX)) != 0 )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Service Name Not Properly Formatted.\n") );
		status = IB_INVALID_SERVICE_TYPE;
		goto exit;
	}

	*p_target_id_extension = 0;

	cl_memclr( target_id_extension, sizeof(target_id_extension) );
	cl_memcpy( target_id_extension, &p_svc_entry->name[strlen(SRP_SERVICE_NAME_PREFIX)], 16 );

	target_id_extension_size = strlen( target_id_extension );

	while ( target_id_extension_size != 0 )
	{
		char        current_digit[2] = {'\0', '\0'};
		NTSTATUS    ntstatus;

		target_id_extension_size--;

		current_digit[0] = target_id_extension[target_id_extension_size];

		ntstatus = RtlCharToInteger( current_digit, 16, &id_extension );
		if ( ntstatus != STATUS_SUCCESS )
		{
			SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
				("Target Id Extension INVALID.\n") );
			status = IB_INVALID_PARAMETER;
			break;
		}

		(*p_target_id_extension) += ( id_extension * multiplier );

		multiplier <<= 4;
	}

	/* Swap to network order now. */
	*p_target_id_extension = cl_hton64( *p_target_id_extension );

exit:
	SRP_EXIT( SRP_DBG_SESSION );

	return ( status );
}

/* srp_new_session */
/*!
Allocates and initializes a session structure and it's sub-structures

@param p_hba       - pointer to the hba associated with the new session
@param ioc_guid    - pointer to the target's ioc guid
@param p_svc_entry - pointer to the service entry
@param p_path_rec  - pointer to path record to use.

@param p_status    - pointer to the reason code

@return - Pointer to new session or NULL if failure. See p_status for reason code.
*/
srp_session_t*
srp_new_session(
	IN      srp_hba_t       *p_hba,
	IN      ib_svc_entry_t  *p_svc_entry,
	IN      ib_path_rec_t   *p_path_rec,
	OUT     ib_api_status_t *p_status )
{
	uint64_t				target_id_extension;
	srp_session_t			*p_srp_session = NULL;
	cl_status_t				cl_status;

	SRP_ENTER( SRP_DBG_SESSION );

	*p_status = __srp_validate_service_entry( p_svc_entry, &target_id_extension );
	if ( *p_status != IB_SUCCESS )
	{
		goto exit;
	}

	if( p_path_rec == NULL )
	{
		goto exit;
	}

	p_srp_session = (srp_session_t*)cl_zalloc( sizeof(srp_session_t) );
	if ( p_srp_session == NULL )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("Failed to allocate srp_session_t structure.\n") );
		*p_status = IB_INSUFFICIENT_MEMORY;
		goto exit;
	}

	p_srp_session->p_hba = p_hba;

	*p_status = srp_init_connection( &p_srp_session->connection,
									&p_hba->ioc_info.profile,
									p_hba->info.ca_guid,
									target_id_extension,
									p_path_rec,
									p_svc_entry->id );
	if ( *p_status != IB_SUCCESS )
	{
		cl_free( p_srp_session );
		p_srp_session = NULL;
		goto exit;
	}

	cl_obj_construct( &p_srp_session->obj, SRP_OBJ_TYPE_SESSION );
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("After Construct Session Object ref_cnt = %d\n",
		 p_srp_session->obj.ref_cnt) );
	cl_status = cl_obj_init( &p_srp_session->obj,
							 CL_DESTROY_ASYNC,
							 __srp_destroying_session,
							 __srp_cleanup_session,
							 __srp_free_session );
	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("After Init Session Object ref_cnt = %d\n",
		p_srp_session->obj.ref_cnt) );
	if( cl_status != CL_SUCCESS )
	{
		SRP_PRINT_EXIT( TRACE_LEVEL_ERROR, SRP_DBG_ERROR,
			("cl_obj_init returned %#x\n", cl_status) );

		cl_free( p_srp_session );
		p_srp_session = NULL;
		*p_status = IB_ERROR;
		goto exit;
	}

	cl_obj_insert_rel( &p_srp_session->rel,
					   &p_srp_session->p_hba->obj,
					   &p_srp_session->obj );

	SRP_PRINT( TRACE_LEVEL_VERBOSE, SRP_DBG_DEBUG,
		("After Insert Rel Session Object ref_cnt = %d\n",
		p_srp_session->obj.ref_cnt) );
exit:
	SRP_EXIT( SRP_DBG_SESSION );

	return ( p_srp_session );
}

/* srp_new_session */
/*!
Orchestrates the connection process for a session to a target

@param p_srp_session - pointer to the session to connect to the target

@return - result of operation
*/
ib_api_status_t
srp_session_login(
	IN  srp_session_t   *p_srp_session )
{
	ib_api_status_t status;

	SRP_ENTER( SRP_DBG_SESSION );

	status = srp_init_hca( &p_srp_session->hca, p_srp_session->p_hba );
	if ( status != IB_SUCCESS )
	{
		goto exit;
	}

	status = srp_open_ca( &p_srp_session->hca, p_srp_session );
	if ( status != IB_SUCCESS )
		goto exit;

	status = srp_connect( &p_srp_session->connection,
						  &p_srp_session->hca,
						  (uint8_t)p_srp_session->p_hba->ioc_info.profile.send_msg_depth,
						  p_srp_session );

exit:
	SRP_EXIT( SRP_DBG_SESSION );
	return ( status );
}

void
srp_session_failed(
IN		srp_session_t*	p_srp_session )
{

	SRP_ENTER( SRP_DBG_SESSION );
	
	if( !p_srp_session )
		return;

	cl_obj_lock( &p_srp_session->obj );
	
	if( p_srp_session->obj.state != CL_INITIALIZED )
	{
		cl_obj_unlock( &p_srp_session->obj );
		return;
	}
	
	if( p_srp_session->connection.state != SRP_CONNECT_FAILURE )
	{
		cl_obj_unlock( &p_srp_session->obj );
		return;
	}
	p_srp_session->connection.state = SRP_CONNECTION_CLOSING;
	
	cl_obj_unlock( &p_srp_session->obj );

	SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
		("Session Idx %d failed\n", p_srp_session->target_id ) );

	cl_event_signal( &p_srp_session->offload_event );
}

ib_api_status_t
srp_session_connect( 
  IN		srp_hba_t			*p_hba,
  IN OUT	p_srp_session_t		*pp_srp_session,
  IN		UCHAR				svc_idx )
{
	ib_api_status_t		ib_status;
	srp_path_record_t	*p_srp_path_rec;
	srp_session_t		*p_session;
	uint64_t				target_id_extension;

	SRP_ENTER( SRP_DBG_SESSION );
	
	if( *pp_srp_session != NULL )
	{
		return IB_ERROR;
	}

	cl_spinlock_acquire( &p_hba->path_record_list_lock );
	if( !cl_qlist_count( &p_hba->path_record_list ) )
	{
		cl_spinlock_release( &p_hba->path_record_list_lock );
		return IB_NOT_FOUND;
	}

	p_srp_path_rec = (srp_path_record_t *)cl_qlist_head( &p_hba->path_record_list );
	
	cl_spinlock_release( &p_hba->path_record_list_lock );

	if( p_srp_path_rec == (srp_path_record_t *)cl_qlist_end( &p_hba->path_record_list ) )
	{
		return IB_NOT_FOUND;
	}

	ib_status = __srp_validate_service_entry( &p_hba->p_svc_entries[svc_idx], &target_id_extension );
	if( ib_status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_SESSION,
			("Failed validate service entry status %x\n", ib_status ));
		return ib_status;
	}
	
	p_session = srp_new_session( p_hba,
					&p_hba->p_svc_entries[svc_idx],
					&p_srp_path_rec->path_rec,
					&ib_status );
	
	if( ib_status != IB_SUCCESS || p_session == NULL )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_SESSION,
			("Failed create Session for SVC idx %d status %x\n", svc_idx, ib_status ));
		
		*pp_srp_session = NULL;
		
		return ib_status;
	}

	ib_status = srp_session_login( p_session );
	if( ib_status != IB_SUCCESS )
	{
		SRP_PRINT( TRACE_LEVEL_ERROR, SRP_DBG_SESSION,
			("Failed Session Login status %x\n", ib_status ));

		*pp_srp_session = NULL;
		
		cl_obj_destroy( &p_session->obj );
		
		return ib_status;
	}
	srp_session_adjust_params( p_session );

	cl_obj_lock( &p_hba->obj );

	p_session->target_id = svc_idx;

	*pp_srp_session = p_session;

	cl_obj_unlock( &p_hba->obj );


	SRP_EXIT( SRP_DBG_SESSION );
	return ib_status;
}

void
srp_session_adjust_params( 
	IN	srp_session_t	*p_session )
{

	if ( ( p_session->p_hba->max_sg > p_session->connection.max_scatter_gather_entries )
		&& !( p_session->connection.descriptor_format &	DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS) )
	{
		p_session->p_hba->max_sg = p_session->connection.max_scatter_gather_entries;
	}

	if ( p_session->p_hba->max_srb_ext_sz > p_session->connection.init_to_targ_iu_sz )
	{
		p_session->p_hba->max_srb_ext_sz =
			sizeof( srp_send_descriptor_t ) -
			SRP_MAX_IU_SIZE +
			p_session->connection.init_to_targ_iu_sz;
	}
}

static void
__srp_session_recovery(
IN		srp_session_t*		p_failed_session , 
IN		BOOLEAN				reconnect_request )
{
	ib_api_status_t	ib_status;
	srp_hba_t*		p_hba;
	srp_session_t*	p_new;
	srp_session_t*	p_old;
	UCHAR			target_id;
	int				retry_count;

	SRP_ENTER( SRP_DBG_SESSION );
	
	if( !p_failed_session )
		return;
	if ( ( p_hba = p_failed_session->p_hba ) == NULL )
		return;

	p_old = p_failed_session;
	target_id = p_old->target_id;
	p_hba->session_list[target_id] = NULL;

	if( !reconnect_request )
	{
		/* we're done here */
		SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
			("Session Id: %d won't recover\n", p_old->target_id ) );
		cl_obj_destroy( &p_old->obj );
		return;
	}

	if( !p_hba->session_paused[target_id] )
	{
		p_hba->session_paused[target_id] = TRUE;

		StorPortDeviceBusy( p_hba->p_ext,
					SP_UNTAGGED,
					target_id,
					SP_UNTAGGED, 
					(ULONG)-1 );
	}

	SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
		("Pausing Adapter Session %d\n", target_id ) );

	cl_obj_destroy( &p_old->obj );

	for( retry_count=0; retry_count < 3 ; retry_count++ )
	{
		ib_status = srp_session_connect( p_hba, &p_new, target_id );

		if( ib_status != IB_SUCCESS )
		{
			SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
				("Failed session idx %d connect\n", target_id ) );
		
			continue;
		}

		p_hba->session_list[target_id] = p_new;

		SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
				("Session idx %d connected. Resuming\n", target_id ) );

		StorPortDeviceReady( p_hba->p_ext,
						SP_UNTAGGED,
						target_id,
						SP_UNTAGGED );

		p_hba->session_paused[target_id] = FALSE;
		
		return;
	}

	/* what do we do now ? */
	SRP_PRINT( TRACE_LEVEL_WARNING, SRP_DBG_DATA,
			("Session idx %d recovery failed\n", target_id) );

	return;
}

void
srp_session_recovery_thread(
	IN		void*	 const		context )
{
	srp_session_t*	p_session = (srp_session_t *)context;
	cl_status_t		status;

	if( p_session == NULL )
		return;

	cl_event_init( &p_session->offload_event, FALSE);

	status = cl_event_wait_on( &p_session->offload_event, EVENT_NO_TIMEOUT, FALSE );

	__srp_session_recovery( p_session,  !p_session->p_hba->adapter_stopped );
}
