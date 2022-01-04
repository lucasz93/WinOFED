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

#include "al_mcast_mgr.h"
#include "al_debug.h"
#include "ib_common.h"
#include "al_ci_ca.h"
#include "al_mgr.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_mcast_mgr.tmh"
#endif

typedef struct _join_req
{
	ib_mcast_req_t  		mcast_req;
	ib_mcast_req_t			mcast_mgr_req;
	mcast_mgr_request_t*	p_leave_request;
} join_req_t;

typedef struct _leave_req
{
	mcast_mgr_request_t*	p_join_request;
	ib_pfn_destroy_cb_t		leave_cb;
} leave_req_t;

typedef struct _mcast_mgr
{
	ib_net64_t				port_guid;
	
	LIST_ENTRY 				mcast_list;
	LIST_ENTRY 				request_list;
	
	cl_spinlock_t			mgr_lock;
	
	cl_event_t				requests_list_empty;
	boolean_t				close_pending;
}	mcast_mgr_t;

typedef struct _mcast_mgr_node
{
	ib_gid_t				mgid;
	mcast_state_t			mcast_state;
	uint32_t				connections;
	
	ib_mcast_rec_t			mcast_rec;
	ib_mcast_handle_t		h_mcast;

	LIST_ENTRY 				mcast_entry;
	mcast_mgr_t* 			p_mcast_mgr;

	boolean_t				cb_running;
	cl_event_t				cb_finished;

	LIST_ENTRY 				connected_req_list;

}	mcast_mgr_node_t;


typedef struct _mcast_mgr_port_info
{
	ib_net64_t				port_guid;
	mcast_mgr_t 			mcast_mgr;
	LIST_ENTRY 				port_info_entry;

}	mcast_mgr_port_info_t;


typedef struct _mcast_mgr_db
{
	LIST_ENTRY 				mgr_list;
	cl_spinlock_t			mgr_list_lock;
}	mcast_mgr_db_t;


#if DBG
typedef enum {
	REQ_REF_ADD,
	REQ_REF_RELEASE
} req_ref_t;
#endif

#define ref_request(_p_req_) 				\
    mcast_mgr_request_add_ref(_p_req_, __FILE__, __LINE__)
#define deref_request(_p_req_) 				\
    mcast_mgr_request_release(_p_req_, __FILE__, __LINE__)


mcast_mgr_db_t g_mcast_mgr_db;

void mcast_mgr_join_mcast_cb( ib_mcast_rec_t *p_mcast_rec );
void mcast_mgr_leave_mcast_cb(void* 		context);

void mcast_mgr_print_node_msg(
	IN		uint16_t 				dbg_level, 
	IN 		mcast_mgr_request_t* 	p_mcast_request, 
	IN 		const char* 			msg)
{
    mcast_mgr_node_t* p_mcast_node = p_mcast_request->p_mcast_node;
    ib_gid_t* p_mgid = &(p_mcast_node->mgid);
    AL_PRINT(dbg_level, AL_DBG_ERROR, 
              ("Port 0x%I64x MGID %02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X node %p request %p: %s\n", 
              p_mcast_node->p_mcast_mgr->port_guid,
              p_mgid->raw[0],p_mgid->raw[1],p_mgid->raw[2],p_mgid->raw[3],
              p_mgid->raw[4],p_mgid->raw[5],p_mgid->raw[6],p_mgid->raw[7],
              p_mgid->raw[8],p_mgid->raw[9],p_mgid->raw[10],p_mgid->raw[11],
              p_mgid->raw[12],p_mgid->raw[13],p_mgid->raw[14],p_mgid->raw[15], 
              p_mcast_node, p_mcast_request, msg));
}

void mcast_mgr_print_node_handle_msg(
	IN		uint16_t 				dbg_level, 
	IN 		mcast_mgr_request_t* 	p_mcast_request,
	IN 		const char* 			msg)
{
    mcast_mgr_node_t* p_mcast_node = p_mcast_request->p_mcast_node;
    ib_gid_t* p_mgid = &(p_mcast_node->mgid);
    AL_PRINT(dbg_level, AL_DBG_ERROR, 
              ("Port 0x%I64x MGID %02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X node %p request %p handle %p: %s\n", 
              p_mcast_node->p_mcast_mgr->port_guid,
              p_mgid->raw[0],p_mgid->raw[1],p_mgid->raw[2],p_mgid->raw[3],
              p_mgid->raw[4],p_mgid->raw[5],p_mgid->raw[6],p_mgid->raw[7],
              p_mgid->raw[8],p_mgid->raw[9],p_mgid->raw[10],p_mgid->raw[11],
              p_mgid->raw[12],p_mgid->raw[13],p_mgid->raw[14],p_mgid->raw[15], 
              p_mcast_node, p_mcast_request, p_mcast_node->h_mcast, msg));
}

void mcast_mgr_print_gid_msg(
	IN		uint16_t 				dbg_level, 
	IN 		mcast_mgr_node_t* 		p_mcast_node, 
	IN 		void* 					mcast_handle,
	IN 		const char* 			msg)
{
    ib_gid_t* p_mgid = &(p_mcast_node->mgid);
    AL_PRINT(dbg_level, AL_DBG_ERROR, 
              ("Port 0x%I64x MGID %02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X node %p handle %p: %s\n", 
              p_mcast_node->p_mcast_mgr->port_guid,
              p_mgid->raw[0],p_mgid->raw[1],p_mgid->raw[2],p_mgid->raw[3],
              p_mgid->raw[4],p_mgid->raw[5],p_mgid->raw[6],p_mgid->raw[7],
              p_mgid->raw[8],p_mgid->raw[9],p_mgid->raw[10],p_mgid->raw[11],
              p_mgid->raw[12],p_mgid->raw[13],p_mgid->raw[14],p_mgid->raw[15], p_mcast_node, mcast_handle, msg));
}


ib_api_status_t 
mcast_mgr_init_request(
	IN		mcast_mgr_node_t* 		p_mcast_node,
	OUT		mcast_mgr_request_t** 	pp_mcast_request)
{
	mcast_mgr_request_t*	p_mcast_request;

	ib_api_status_t 		status = IB_SUCCESS;
	cl_status_t				cl_status;
	
	p_mcast_request = (mcast_mgr_request_t*)cl_zalloc( sizeof( mcast_mgr_request_t ) );
	if (!p_mcast_request)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Allocation of mcast request failed\n") );
		status = IB_INSUFFICIENT_MEMORY;
		goto exit;
	}

	p_mcast_request->p_mcast_node = p_mcast_node;

	cl_status = cl_event_init( &p_mcast_request->cb_finished, FALSE );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Initiation of mcast request event failed\n") );
		status = ib_convert_cl_status( cl_status );
		goto cb_event_init_error;
	}

	p_mcast_request->p_mcast_node = p_mcast_node;
	p_mcast_request->ref_cnt = 1;

	*pp_mcast_request = p_mcast_request;
	
	goto exit;

cb_event_init_error:
	cl_free(p_mcast_request);
exit:
	return status;

}


void 
mcast_mgr_destroy_request(
	IN		mcast_mgr_request_t* 	p_mcast_request )
{
	if (p_mcast_request->p_join_req)
	{
		mcast_mgr_request_t* p_leave_request = p_mcast_request->p_join_req->p_leave_request;
		if (p_leave_request)
		{
			mcast_mgr_request_release(p_leave_request, __FILE__, __LINE__);
			p_leave_request = NULL;
		}
		
		cl_free(p_mcast_request->p_join_req);
		
	}
	else
		cl_free(p_mcast_request->p_leave_req);
	
	cl_event_destroy( &p_mcast_request->cb_finished );
	cl_free(p_mcast_request);
}

void 
mcast_mgr_request_add_ref(
	IN		mcast_mgr_request_t* 	p_mcast_request,
	IN		const char*				file_name,
	IN		uint32_t				line)
{
#if DBG
	LONG index = InterlockedIncrement(&p_mcast_request->ref_cnt_dbg_index);
	req_ref_cnt_t* p_ref_dbg = &(p_mcast_request->ref_cnt_dbg[index - 1]);
	LONG ref_cnt = InterlockedIncrement(&p_mcast_request->ref_cnt);
	CL_ASSERT(ref_cnt > 1 && index < REF_DBG_TOTAL);

	p_ref_dbg->ref_change = REQ_REF_ADD;
	p_ref_dbg->prev_ref_val = ref_cnt - 1;
	p_ref_dbg->file_name = file_name;
	p_ref_dbg->line_num = line;
#else
	UNUSED_PARAM(file_name);
	UNUSED_PARAM(line);
	InterlockedIncrement(&p_mcast_request->ref_cnt);
#endif
}

void 
mcast_mgr_request_release(
	IN		mcast_mgr_request_t* 	p_mcast_request,
	IN		const char*				file_name,
	IN		uint32_t				line )
{
	LONG ref_cnt;
	
#if DBG
	LONG index = InterlockedIncrement(&p_mcast_request->ref_cnt_dbg_index);
	req_ref_cnt_t* p_ref_dbg = &(p_mcast_request->ref_cnt_dbg[index - 1]);
	CL_ASSERT(index < REF_DBG_TOTAL);

	p_ref_dbg->ref_change = REQ_REF_RELEASE;
	p_ref_dbg->prev_ref_val = p_mcast_request->ref_cnt;
	p_ref_dbg->file_name = file_name;
	p_ref_dbg->line_num = line;
#else
	UNUSED_PARAM(file_name);
	UNUSED_PARAM(line);
#endif

	ref_cnt = InterlockedDecrement(&p_mcast_request->ref_cnt);
	CL_ASSERT(ref_cnt >= 0);
	if (!ref_cnt)
		mcast_mgr_destroy_request(p_mcast_request);
	
}


void
mcast_mgr_init()
{
	InitializeListHead(&g_mcast_mgr_db.mgr_list);
	cl_spinlock_construct(&g_mcast_mgr_db.mgr_list_lock);
	cl_spinlock_init( &g_mcast_mgr_db.mgr_list_lock );
}

void mcast_mgr_shutdown()
{
	CL_ASSERT(IsListEmpty(&g_mcast_mgr_db.mgr_list));
	cl_spinlock_destroy( &g_mcast_mgr_db.mgr_list_lock );
}


void 
mcast_mgr_close_port_mgr(
	IN		void* 		mcast_mgr) 
{
	mcast_mgr_node_t* 		p_mcast_node;
	mcast_mgr_request_t* 	p_mcast_request;
	PLIST_ENTRY 			p_entry;
	PLIST_ENTRY 			p_req_entry;

	mcast_mgr_t*			p_mcast_mgr = (mcast_mgr_t*)mcast_mgr;

	cl_status_t				cl_status;
	boolean_t 				leave_requests_pending = FALSE;

	cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

	p_entry = p_mcast_mgr->request_list.Flink;
	while (p_entry != &p_mcast_mgr->request_list)
	{
		boolean_t join_req_executed = FALSE;
		mcast_mgr_node_t* p_mcast_node;

		p_mcast_request = CONTAINING_RECORD(p_entry, mcast_mgr_request_t, request_entry);
		p_mcast_node = p_mcast_request->p_mcast_node;
		p_entry = p_entry->Flink;

		if (p_mcast_request->req_type == LEAVE_REQUEST)
		{
			leave_requests_pending = TRUE;
			continue; // Just wait paitiantly for it to finish
		}

		//Remove join request
		switch( p_mcast_request->req_state )	  
		{
			case SCHEDULED:
				// Go on and remove it
				break;
			case RUNNING:
			case RUNNING_CB:
				CL_ASSERT(FALSE);
				cl_spinlock_release(&p_mcast_mgr->mgr_lock);
				cl_status = cl_event_wait_on(&p_mcast_request->cb_finished, EVENT_NO_TIMEOUT, TRUE );
				ASSERT(cl_status == CL_SUCCESS);
				cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);
				join_req_executed = TRUE;
				break;
			default:
				// Done requests should be in the node list
				// And there is no other value
				CL_ASSERT(FALSE);
				break;
		}

		if (join_req_executed)
		{
			mcast_mgr_print_node_msg(TRACE_LEVEL_ERROR, p_mcast_request, " close mgr moving join request into mcast node");
			RemoveEntryList(&p_mcast_request->request_entry);
			InsertTailList(&p_mcast_node->connected_req_list, &p_mcast_request->request_entry);
			p_mcast_request->req_state = DONE;
		}
		else
		{
			mcast_mgr_print_node_msg(TRACE_LEVEL_ERROR, p_mcast_request, " close mgr removing join request");
			RemoveEntryList(&p_mcast_request->request_entry);
			deref_al_obj(&p_mcast_request->p_mcast_node->h_mcast->obj);
			p_mcast_node->h_mcast = NULL;
			deref_request(p_mcast_request);
		}
	}

	if (leave_requests_pending)
	{
		p_mcast_mgr->close_pending = TRUE;
		cl_spinlock_release(&p_mcast_mgr->mgr_lock);
		cl_status = cl_event_wait_on(&p_mcast_mgr->requests_list_empty, EVENT_NO_TIMEOUT, TRUE );
		ASSERT(cl_status == CL_SUCCESS);
		cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);
	}
	
	p_entry = RemoveHeadList(&p_mcast_mgr->mcast_list);
	while (p_entry != &p_mcast_mgr->mcast_list)
	{
		p_mcast_node = CONTAINING_RECORD(p_entry, mcast_mgr_node_t, mcast_entry);

		p_req_entry = RemoveHeadList(&p_mcast_node->connected_req_list);
		while (p_req_entry != &p_mcast_node->connected_req_list)
		{
			p_mcast_request = CONTAINING_RECORD(p_req_entry, mcast_mgr_request_t, request_entry);
			CL_ASSERT(p_mcast_request->req_state == DONE && (p_mcast_request->req_type == JOIN_REQUEST));
			
			mcast_mgr_print_node_msg(TRACE_LEVEL_ERROR, p_mcast_request, " close mgr removing connected request");
			CL_ASSERT(FALSE); // We should not have nodes like these
			deref_request(p_mcast_request);
			deref_request(p_mcast_request); // deref the ref taken for the user
			
			p_req_entry = RemoveHeadList(&p_mcast_node->connected_req_list);
		}
		
		if (p_mcast_node->mcast_state == MCAST_CONNECTED)
		{
			mcast_mgr_print_gid_msg(TRACE_LEVEL_INFORMATION, p_mcast_node, p_mcast_node->h_mcast, " shutdown leaving request");
			ib_leave_mcast(p_mcast_node->h_mcast, NULL);
			deref_al_obj(&p_mcast_node->h_mcast->obj);
		}
		
		cl_free(p_mcast_node);
		p_entry = RemoveHeadList(&p_mcast_mgr->mcast_list);
	}

	cl_spinlock_release(&p_mcast_mgr->mgr_lock);

	cl_spinlock_destroy(&p_mcast_mgr->mgr_lock);
	cl_event_destroy( &p_mcast_mgr->requests_list_empty);
}


ib_api_status_t
mcast_mgr_add_ca_ports(
	IN		const	ib_net64_t					ci_ca_guid)
{
	ib_api_status_t status;
	int i;
	
	/* Initialize mcast mgr */
	al_ci_ca_t* p_ci_ca = find_ci_ca( ci_ca_guid );
	if (!p_ci_ca)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("ci_ca guid not found\n") );
		return IB_NOT_FOUND;
	}

	for ( i = 0; i < p_ci_ca->num_ports; i++)
	{
		mcast_mgr_t*			p_mcast_mgr;
		cl_status_t				cl_status;
		
		mcast_mgr_port_info_t* p_port_info = (mcast_mgr_port_info_t*)cl_zalloc( sizeof(mcast_mgr_port_info_t));
		if (!p_port_info)
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Allocation of mcast manager failed\n") );
			status = IB_INSUFFICIENT_MEMORY;
			goto error;
		}

		p_port_info->port_guid = p_ci_ca->port_array[i];
		
		p_mcast_mgr = &p_port_info->mcast_mgr;
		p_mcast_mgr->port_guid = p_port_info->port_guid;
		
		InitializeListHead(&p_mcast_mgr->mcast_list);
		InitializeListHead(&p_mcast_mgr->request_list);
		
		cl_spinlock_construct(&p_mcast_mgr->mgr_lock);
		cl_spinlock_init( &p_mcast_mgr->mgr_lock );

		cl_status = cl_event_init( &p_mcast_mgr->requests_list_empty, TRUE );
		if( cl_status != CL_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Initiation of mcast mgr event failed\n") );
			status = ib_convert_cl_status( cl_status );
			goto error;
		}

		p_mcast_mgr->close_pending = FALSE;

        cl_spinlock_acquire( &g_mcast_mgr_db.mgr_list_lock );
		InsertTailList(&g_mcast_mgr_db.mgr_list, &p_port_info->port_info_entry);
        cl_spinlock_release( &g_mcast_mgr_db.mgr_list_lock );

		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_ERROR, ("Add CA port 0x%I64x\n", p_port_info->port_guid) );
	}

	return IB_SUCCESS;

error:
	mcast_mgr_remove_ca_ports(ci_ca_guid);
	return status;
}


ib_api_status_t
mcast_mgr_remove_ca_ports(
	IN		const	ib_net64_t					ci_ca_guid)
{
	int i;
	
	/* Initialize mcast mgr */
	al_ci_ca_t* p_ci_ca = find_ci_ca( ci_ca_guid );
	if (!p_ci_ca)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("ci_ca guid not found\n") );
		return IB_NOT_FOUND;
	}

	for ( i = 0; i < p_ci_ca->num_ports; i++)
	{
		mcast_mgr_port_info_t* 		p_port_info;
		mcast_mgr_port_info_t* 		p_found_port_info = NULL;
		
		PLIST_ENTRY 				p_entry;
		PLIST_ENTRY 				p_next_entry;

		cl_spinlock_acquire( &g_mcast_mgr_db.mgr_list_lock );
		
		p_entry = g_mcast_mgr_db.mgr_list.Flink;
		while (p_entry != &g_mcast_mgr_db.mgr_list)
		{
			p_port_info = CONTAINING_RECORD(p_entry, mcast_mgr_port_info_t, port_info_entry);
			p_next_entry = p_entry->Flink;
			if (p_port_info->port_guid == p_ci_ca->port_array[i])
			{
				AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_ERROR, ("Remove CA port 0x%I64x\n", p_port_info->port_guid) );
				RemoveEntryList(p_entry);
				p_found_port_info = p_port_info;
				break;
			}
			p_entry = p_next_entry;
		}
		
		cl_spinlock_release( &g_mcast_mgr_db.mgr_list_lock );

		if (p_found_port_info)
		{
			mcast_mgr_close_port_mgr(&p_found_port_info->mcast_mgr);
			cl_free(p_found_port_info);
		}
	}
	
	return IB_SUCCESS;
}


mcast_mgr_t*
mcast_mgr_get_port_mgr(
	IN		ib_net64_t					port_guid)
{
	mcast_mgr_port_info_t* 		p_port_info;
	PLIST_ENTRY 				p_entry;
	mcast_mgr_t*				p_mcast_mgr = NULL;
	
	cl_spinlock_acquire( &g_mcast_mgr_db.mgr_list_lock );

	p_entry = g_mcast_mgr_db.mgr_list.Flink;
	while (p_entry != &g_mcast_mgr_db.mgr_list)
	{
		p_port_info = CONTAINING_RECORD(p_entry, mcast_mgr_port_info_t, port_info_entry);
		p_entry = p_entry->Flink;
		if (p_port_info->port_guid == port_guid)
		{
			p_mcast_mgr = &p_port_info->mcast_mgr;
			goto exit;
		}
	}

exit:
	cl_spinlock_release( &g_mcast_mgr_db.mgr_list_lock );
	return p_mcast_mgr;
}


//
// This function is called with the global mcast_mgr lock held.
//
ib_api_status_t
mcast_mgr_process_next_request(
	IN		mcast_mgr_t* 		p_mcast_mgr,
	IN		boolean_t			is_from_queue)
{
	ib_api_status_t 		status = IB_SUCCESS;
	boolean_t 				is_request_scheduled = FALSE;

	mcast_mgr_request_t* 	p_mcast_request;
	mcast_mgr_node_t* 		p_mcast_node;

	PLIST_ENTRY				p_entry = p_mcast_mgr->request_list.Flink;
	
	while (!is_request_scheduled && (p_entry != &p_mcast_mgr->request_list)) 
	{
		p_mcast_request = CONTAINING_RECORD(p_entry, mcast_mgr_request_t, request_entry);
		p_mcast_node = p_mcast_request->p_mcast_node;
		
		switch( p_mcast_node->mcast_state )      
		{
			case MCAST_IDLE:
				if (p_mcast_request->req_type == JOIN_REQUEST)
				{
					join_req_t* p_join_req = p_mcast_request->p_join_req;
					status = al_join_mcast_no_qp(&p_join_req->mcast_mgr_req, &p_mcast_node->h_mcast);
					if (status != IB_SUCCESS)
					{
						ib_mcast_rec_t mcast_rec;
						
						if (!is_from_queue)
							return status;
						
						// Indicate error to user in a call back
						cl_memset(&mcast_rec, 0, sizeof(ib_mcast_rec_t));
						mcast_rec.p_member_rec = &p_join_req->mcast_req.member_rec;
						mcast_mgr_print_node_msg(TRACE_LEVEL_ERROR, p_mcast_request, " running join request failed - inform user by callback");
						
						p_mcast_request->req_state = RUNNING_CB;
                        //
                        // Release the lock before invoking the callback.  Note that we remove the
                        // request from the list before invoking the callback, so as to allow the
                        // list to change during the callback without worry.
                        //
						RemoveEntryList(&p_mcast_request->request_entry);
						cl_spinlock_release(&p_mcast_mgr->mgr_lock);
						mcast_rec.mcast_context = p_join_req->mcast_req.mcast_context;
						mcast_rec.status = status;
		   	 			p_mcast_request->p_join_req->mcast_req.pfn_mcast_cb(&mcast_rec);
						cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);
						
						cl_event_signal( &p_mcast_request->cb_finished );
						p_mcast_request->req_state = DONE;
						p_mcast_node->h_mcast = NULL;
						deref_request(p_mcast_request);
					}
					else
					{
						mcast_mgr_print_node_handle_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " running join request");
						p_mcast_request->req_state = RUNNING;
						is_request_scheduled = TRUE;
					}
				}
				else	// LEAVE_REQUEST
				{
					//Should not get leave requests when no mcasts are connected
					ASSERT(FALSE);
				}
				break;

			case MCAST_CONNECTED:
				CL_ASSERT(p_mcast_node->connections > 0);
				if (p_mcast_request->req_type == JOIN_REQUEST)
				{
					//Call user cb on existing record
					ib_mcast_rec_t mcast_rec;
					cl_memcpy(&mcast_rec, &p_mcast_node->mcast_rec, sizeof(ib_mcast_rec_t));
					mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " already connected");
					p_mcast_node->connections++;

					p_mcast_request->req_state = RUNNING_CB;
                    //
                    // Release the lock before invoking the callback.  Note that we remove the
                    // request from the list before invoking the callback, so as to allow the
                    // list to change during the callback without worry.
                    //
					RemoveEntryList(&p_mcast_request->request_entry);
					cl_spinlock_release(&p_mcast_mgr->mgr_lock);
					mcast_rec.mcast_context = p_mcast_request->p_join_req->mcast_req.mcast_context;
					p_mcast_request->p_join_req->mcast_req.pfn_mcast_cb(&mcast_rec);
					cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

					InsertTailList(&p_mcast_node->connected_req_list, &p_mcast_request->request_entry);
					p_mcast_request->req_state = DONE;
					cl_event_signal( &p_mcast_request->cb_finished );
				}
				else 	// LEAVE_REQUEST
				{
					if (p_mcast_node->connections == 1)
					{
						ib_mcast_handle_t h_mcast = p_mcast_node->h_mcast;

						mcast_mgr_print_node_handle_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " running leave request");
						status = ib_leave_mcast(h_mcast, mcast_mgr_leave_mcast_cb);
						CL_ASSERT(status == IB_SUCCESS);
						p_mcast_request->req_state = RUNNING;
						is_request_scheduled = TRUE;

						deref_al_obj(&h_mcast->obj);
					}
					else
					{
						void* user_context;
						mcast_mgr_request_t* p_join_request = p_mcast_request->p_leave_req->p_join_request;
						CL_ASSERT(p_join_request->req_state == DONE);

						mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " reducing connections");
						p_mcast_node->connections--;

						p_mcast_request->req_state = RUNNING_CB;
                        //
                        // Release the lock before invoking the callback.  Note that we remove the
                        // request from the list before invoking the callback, so as to allow the
                        // list to change during the callback without worry.
                        //
						RemoveEntryList(&p_mcast_request->request_entry);
						RemoveEntryList(&p_join_request->request_entry);
						cl_spinlock_release(&p_mcast_mgr->mgr_lock);
						user_context = (void*)p_join_request->p_join_req->mcast_req.mcast_context;
						p_join_request->req_dereg_status = IB_SUCCESS;
						if (p_mcast_request->p_leave_req->leave_cb)
							p_mcast_request->p_leave_req->leave_cb(user_context);
						cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

						deref_request(p_join_request);
						deref_request(p_mcast_request);
					}
				}
				break;

			case MCAST_CONNECTING:
			case MCAST_LEAVING:
			default:
				//We can not be in the middle of another request by design
				// And there is no other value
				CL_ASSERT(FALSE);
				break;
		}

		p_entry = p_mcast_mgr->request_list.Flink;
	}

	if (p_mcast_mgr->close_pending && !is_request_scheduled) // We exited because there are no more requests
		cl_event_signal( &p_mcast_mgr->requests_list_empty );

	return IB_SUCCESS;
}


void 
mcast_mgr_join_mcast_cb(
    ib_mcast_rec_t *p_mcast_rec )
{
	mcast_mgr_request_t* 	p_mcast_request;
	mcast_mgr_node_t* 		p_mcast_node;
	join_req_t*				p_join_req;
	mcast_mgr_t* 			p_mcast_mgr;
	
	CL_ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);

	p_mcast_node = (mcast_mgr_node_t*)p_mcast_rec->mcast_context;
	p_mcast_mgr = p_mcast_node->p_mcast_mgr;

	cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

	// The first request should be the one we are handling
	p_mcast_request = CONTAINING_RECORD(p_mcast_mgr->request_list.Flink, 
										   mcast_mgr_request_t, 
										   request_entry);
	CL_ASSERT(p_mcast_request->p_mcast_node == p_mcast_node && 
		      p_mcast_request->req_type == JOIN_REQUEST);

	p_mcast_request->req_state = RUNNING_CB;
	p_mcast_request->req_reg_status = p_mcast_rec->status;

	if (p_mcast_rec->status == IB_SUCCESS)
	{
		p_mcast_node->mcast_state = MCAST_CONNECTED;
		CL_ASSERT(p_mcast_node->connections == 0);
		p_mcast_node->connections++;
		mcast_mgr_print_node_handle_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " connected");
	}
	
	cl_spinlock_release(&p_mcast_mgr->mgr_lock);

	p_join_req = p_mcast_request->p_join_req;
	p_mcast_rec->mcast_context = p_join_req->mcast_req.mcast_context;
	cl_memcpy(&p_mcast_node->mcast_rec, p_mcast_rec, sizeof(ib_mcast_rec_t));
    p_join_req->mcast_req.pfn_mcast_cb(p_mcast_rec);

	cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

	if (p_mcast_rec->status == IB_SUCCESS)
	{
		mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " moving join request into mcast node");
		RemoveEntryList(&p_mcast_request->request_entry);
		InsertTailList(&p_mcast_node->connected_req_list, &p_mcast_request->request_entry);
		p_mcast_request->req_state = DONE;
		cl_event_signal( &p_mcast_request->cb_finished );
	}
	else
	{
		mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " removing failed join request");
		RemoveEntryList(&p_mcast_request->request_entry);
		cl_event_signal( &p_mcast_request->cb_finished );
		deref_al_obj(&p_mcast_node->h_mcast->obj);
		p_mcast_node->h_mcast = NULL;
		deref_request(p_mcast_request);
	}

	mcast_mgr_process_next_request(p_mcast_mgr, TRUE);
		
	cl_spinlock_release(&p_mcast_mgr->mgr_lock);
}


void 
mcast_mgr_leave_mcast_cb(
	void* 		context)
{
	mcast_mgr_request_t* 	p_mcast_request;
	mcast_mgr_request_t* 	p_join_request;
	mcast_mgr_node_t* 		p_mcast_node;
	mcast_mgr_t* 			p_mcast_mgr;
	void*					user_context;
	
	CL_ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);

	p_mcast_node = (mcast_mgr_node_t*)context;
	p_mcast_mgr = p_mcast_node->p_mcast_mgr;

	cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);
	
	p_mcast_node->mcast_state = MCAST_IDLE;
	CL_ASSERT(p_mcast_node->connections > 0);
	p_mcast_node->connections--;

	// The first request should be the one we are handling
	p_mcast_request = CONTAINING_RECORD(p_mcast_mgr->request_list.Flink, 
										   mcast_mgr_request_t, 
										   request_entry);
	CL_ASSERT(p_mcast_request->p_mcast_node == p_mcast_node && 
		      p_mcast_request->req_type == LEAVE_REQUEST);

	p_mcast_request->req_state = RUNNING_CB;
	mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " idle");

	p_join_request = p_mcast_request->p_leave_req->p_join_request;
	CL_ASSERT(p_join_request->req_state == DONE);
		
	cl_spinlock_release(&p_mcast_mgr->mgr_lock);

	user_context = (void*)p_mcast_node->mcast_rec.mcast_context;
	p_join_request->req_dereg_status = p_mcast_node->h_mcast->sa_dereg_req.status;
	if (p_mcast_request->p_leave_req->leave_cb)
		p_mcast_request->p_leave_req->leave_cb(user_context);

	cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

	mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " removing leave request");
	
	RemoveEntryList(&p_mcast_request->request_entry);
	RemoveEntryList(&p_join_request->request_entry);
	
	deref_request(p_join_request);
	deref_request(p_mcast_request);

	mcast_mgr_process_next_request(p_mcast_mgr, TRUE);
	
	cl_spinlock_release(&p_mcast_mgr->mgr_lock);
}


mcast_mgr_node_t*
mcast_mgr_get_mcast_node(
	IN		mcast_mgr_t* 				p_mcast_mgr,
	IN		const ib_gid_t* const		p_gid)
{
	mcast_mgr_node_t* 	p_mcast_node = NULL;
	PLIST_ENTRY 		p_entry;

	p_entry = p_mcast_mgr->mcast_list.Flink;
	while (p_entry != &p_mcast_mgr->mcast_list)
	{
		p_mcast_node = CONTAINING_RECORD(p_entry, mcast_mgr_node_t, mcast_entry);
		if (cl_memcmp(&(p_mcast_node->mgid), p_gid, sizeof(ib_gid_t)) == 0)
		{
			return p_mcast_node;
		}
		
		p_entry = p_entry->Flink;
	}

	return NULL;
}

ib_api_status_t 
mcast_mgr_join_mcast(
	IN		const ib_mcast_req_t* const 	mcast_req,
	OUT		mcast_mgr_request_t**			pp_mcast_request)
{
	mcast_mgr_node_t* 		p_mcast_node;	
	mcast_mgr_request_t*	p_mcast_request;
	mcast_mgr_request_t*	p_mcast_leave_request;
	join_req_t*				p_join_req;
	leave_req_t*			p_leave_req;

	ib_api_status_t 		status = IB_SUCCESS;
	boolean_t				node_allocated = FALSE;

	const ib_gid_t* const	p_gid = &(mcast_req->member_rec.mgid);
	mcast_mgr_t*			p_mcast_mgr = mcast_mgr_get_port_mgr(mcast_req->port_guid);

	*pp_mcast_request = NULL;

	if (!p_mcast_mgr)
		return IB_NOT_FOUND;

	cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

	p_mcast_node = mcast_mgr_get_mcast_node(p_mcast_mgr, p_gid);
	if (!p_mcast_node)
	{
		p_mcast_node = (mcast_mgr_node_t*)cl_zalloc( sizeof( mcast_mgr_node_t ) );
		if( !p_mcast_node )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("zalloc of p_mcast_node failed\n") );
			status = IB_INSUFFICIENT_MEMORY;
			goto exit;
		}

		cl_memcpy(&p_mcast_node->mgid, p_gid, sizeof(ib_gid_t));
		InitializeListHead(&p_mcast_node->connected_req_list);

		InsertTailList(&p_mcast_mgr->mcast_list, &p_mcast_node->mcast_entry);
		p_mcast_node->p_mcast_mgr = p_mcast_mgr;

		node_allocated = TRUE;
	}

	//
	// Init join request
	//
	
	p_join_req = (join_req_t*)cl_zalloc( sizeof( join_req_t ) );
	if (!p_join_req)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Allocation of join request info failed\n") );
		status = IB_INSUFFICIENT_MEMORY;
		goto request_info_alloc_error; 
	}

	cl_memcpy(&p_join_req->mcast_req, mcast_req, sizeof(ib_mcast_req_t));

	cl_memcpy(&p_join_req->mcast_mgr_req, mcast_req, sizeof(ib_mcast_req_t));
	p_join_req->mcast_mgr_req.mcast_context = p_mcast_node;
	p_join_req->mcast_mgr_req.pfn_mcast_cb = mcast_mgr_join_mcast_cb;

	status = mcast_mgr_init_request(p_mcast_node, &p_mcast_request);
	if (status != IB_SUCCESS)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Allocation of mcast request failed\n") );
		goto request_init_error;
	}

	p_mcast_request->req_type = JOIN_REQUEST;
	p_mcast_request->p_join_req = p_join_req;
	p_mcast_request->p_leave_req = NULL;

	//
	// Init empty leave request
	//
	
	p_leave_req = (leave_req_t*)cl_zalloc( sizeof( leave_req_t ) );
	if (!p_leave_req)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Allocation of mcast request info failed\n") );
		status = IB_INSUFFICIENT_MEMORY;
		goto leave_request_info_alloc_error; 
	}

	p_leave_req->p_join_request = p_mcast_request;
	p_leave_req->leave_cb = NULL;

	status = mcast_mgr_init_request(p_mcast_node, &p_mcast_leave_request);
	if (status != IB_SUCCESS)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Allocation of mcast request failed\n") );
		goto leave_request_init_error; 
	}

	p_mcast_leave_request->req_type = LEAVE_REQUEST;
	p_mcast_leave_request->p_join_req = NULL;
	p_mcast_leave_request->p_leave_req = p_leave_req;

	p_join_req->p_leave_request = p_mcast_leave_request;

	//
	// Schedule request
	//
	
	if (IsListEmpty(&p_mcast_mgr->request_list))
	{
		InsertTailList(&p_mcast_mgr->request_list, &p_mcast_request->request_entry);
		p_mcast_request->req_state = RUNNING;
		status = mcast_mgr_process_next_request(p_mcast_mgr, FALSE);
		if (status != IB_SUCCESS)
			goto handle_error;
	}
	else
	{
		mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " schedualing join request");
		InsertTailList(&p_mcast_mgr->request_list, &p_mcast_request->request_entry);
		p_mcast_request->req_state = SCHEDULED;
	}

	p_mcast_request->ref_cnt++;
	*pp_mcast_request = p_mcast_request;
	goto exit;

handle_error:
	RemoveEntryList(&p_mcast_request->request_entry);
	p_leave_req = NULL;
leave_request_init_error:
	if (p_leave_req)
		cl_free(p_leave_req);
leave_request_info_alloc_error:
	deref_request(p_mcast_request);
	p_join_req = NULL;
request_init_error:
	if (p_join_req)
		cl_free(p_join_req);
request_info_alloc_error:
	if (node_allocated) 
	{
		// We allocated a new mcast node - it has no request entries
		RemoveEntryList(&p_mcast_node->mcast_entry);
		cl_free(p_mcast_node);
	}
exit:
	cl_spinlock_release(&p_mcast_mgr->mgr_lock);
	return status;
}


ib_api_status_t
mcast_mgr_leave_mcast(
	IN		mcast_mgr_request_t*		p_join_request,
	IN		ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	mcast_mgr_node_t* 		p_mcast_node;
	mcast_mgr_request_t*	p_mcast_request;

	ib_api_status_t 		status = IB_SUCCESS;

	mcast_mgr_t*			p_mcast_mgr = p_join_request->p_mcast_node->p_mcast_mgr;

	cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

	p_mcast_node = p_join_request->p_mcast_node;

	CL_ASSERT(p_join_request->req_type == JOIN_REQUEST && p_join_request->p_join_req);
	p_mcast_request = p_join_request->p_join_req->p_leave_request;
	
	CL_ASSERT(p_mcast_request->p_leave_req);
	p_mcast_request->p_leave_req->leave_cb = pfn_destroy_cb;

	mcast_mgr_request_add_ref(p_mcast_request, __FILE__, __LINE__);
	
	if (IsListEmpty(&p_mcast_mgr->request_list))
	{
		InsertTailList(&p_mcast_mgr->request_list, &p_mcast_request->request_entry);
		p_mcast_request->req_state = RUNNING;
		status = mcast_mgr_process_next_request(p_mcast_mgr, FALSE);
		CL_ASSERT(status == IB_SUCCESS);
	}
	else
	{
		mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " schedualing leave request");
		InsertTailList(&p_mcast_mgr->request_list, &p_mcast_request->request_entry);
		p_mcast_request->req_state = SCHEDULED;
	}
	
	cl_spinlock_release(&p_mcast_mgr->mgr_lock);
	return status;
}


ib_api_status_t 
mcast_mgr_cancel_join(
	IN		mcast_mgr_request_t*			p_user_request,
	OUT		boolean_t*						p_mcast_connected)
{
	mcast_mgr_request_t*	p_mcast_request = (mcast_mgr_request_t*)p_user_request;
	mcast_mgr_t*			p_mcast_mgr = p_mcast_request->p_mcast_node->p_mcast_mgr;

	ib_api_status_t			status = IB_SUCCESS;
	cl_status_t				cl_status;
	*p_mcast_connected = FALSE;

	cl_spinlock_acquire(&p_mcast_mgr->mgr_lock);

	if (p_mcast_request == NULL)
	{
		cl_spinlock_release(&p_mcast_mgr->mgr_lock);
		return IB_ERROR; // Request failed and destroyed
	}

	switch( p_mcast_request->req_state )      
	{
		case SCHEDULED:
			CL_ASSERT(p_mcast_request->req_type == JOIN_REQUEST);
			mcast_mgr_print_node_msg(TRACE_LEVEL_INFORMATION, p_mcast_request, " canceling join request");
			RemoveEntryList(&p_mcast_request->request_entry);
			deref_request(p_mcast_request);
			cl_spinlock_release(&p_mcast_mgr->mgr_lock);
			*p_mcast_connected = FALSE;
			status = IB_SUCCESS;
			break;
		case RUNNING:
		case RUNNING_CB:
			cl_spinlock_release(&p_mcast_mgr->mgr_lock);
			cl_status = cl_event_wait_on(&p_mcast_request->cb_finished, EVENT_NO_TIMEOUT, TRUE );
			ASSERT(cl_status == CL_SUCCESS);
			*p_mcast_connected = (p_mcast_request->p_mcast_node->mcast_state == MCAST_CONNECTED);
			status = IB_ERROR;
			break;

		case DONE:
			*p_mcast_connected = (p_mcast_request->p_mcast_node->mcast_state == MCAST_CONNECTED);
			cl_spinlock_release(&p_mcast_mgr->mgr_lock);
			status = IB_ERROR;
			break;
		default:
			// There is no other value
			CL_ASSERT(FALSE);
			cl_spinlock_release(&p_mcast_mgr->mgr_lock);
			status = IB_ERROR;
			break;
	}

	return status;
}



