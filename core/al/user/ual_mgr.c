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


#include "ual_support.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_mgr.tmh"
#endif

#include "al_mgr.h"
#include "al_init.h"
#include "al_res_mgr.h"
#include "al_proxy_ioctl.h"
#include "al.h"
#include "al_ci_ca.h"
#include "al_pnp.h"
#include "al_ioc_pnp.h"
#include "al_cq.h"
#include "ual_ca.h"
#include "ual_qp.h"
#include "ual_mad.h"
#include "ib_common.h"
#include "al_cm_cep.h"


/* Global AL manager handle is defined in al_mgr_shared.c */
extern	ib_al_handle_t		gh_al;
extern	al_mgr_t*			gp_al_mgr;
extern	ib_pool_handle_t	gh_mad_pool;


atomic32_t					g_open_cnt = 0;

/* Define the thread names to handle various notifications */
#define CM_THREAD_NAME			"CM_Thread"
#define COMP_THREAD_NAME		"Comp_Thread"
#define MISC_THREAD_NAME		"Misc_Thread"

static DWORD WINAPI
__cb_thread_routine(
	IN				void						*context );

static void
__process_misc_cb(
	IN		misc_cb_ioctl_info_t*		p_misc_cb_info );


static void
__cleanup_ual_mgr(
	IN				al_obj_t					*p_obj )
{
	AL_ENTER(AL_DBG_MGR);

	UNUSED_PARAM( p_obj );

	/* Set the callback thread state to exit. */
	gp_al_mgr->ual_mgr.exit_thread = TRUE;

	/* Closing the file handles cancels any pending I/O requests. */
	//CloseHandle( gp_al_mgr->ual_mgr.h_cm_file );
	CloseHandle( gp_al_mgr->ual_mgr.h_cq_file );
	CloseHandle( gp_al_mgr->ual_mgr.h_misc_file );
	CloseHandle( g_al_device );
	g_al_device = INVALID_HANDLE_VALUE;
}


static void
__free_ual_mgr(
	IN				al_obj_t					*p_obj )
{
	size_t			i;
	HANDLE			h_thread;

	UNUSED_PARAM( p_obj );

	/* Verify that the object list is empty. */
	print_al_objs( NULL );

	if( gp_al_mgr->ual_mgr.h_cb_port )
	{
		/* Post a notification to the completion port to make threads exit. */
		for( i = 0;
			i < cl_ptr_vector_get_size( &gp_al_mgr->ual_mgr.cb_threads );
			i++ )
		{
			if( !PostQueuedCompletionStatus( gp_al_mgr->ual_mgr.h_cb_port,
				0, 0, NULL ) )
			{
				AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
					("PostQueuedCompletionStatus returned %d\n",
					GetLastError()) );
			}
		}

		while( cl_ptr_vector_get_size( &gp_al_mgr->ual_mgr.cb_threads ) )
		{
			h_thread = cl_ptr_vector_get( &gp_al_mgr->ual_mgr.cb_threads, 0 );
			WaitForSingleObject( h_thread, INFINITE );
			CloseHandle( h_thread );
			cl_ptr_vector_remove( &gp_al_mgr->ual_mgr.cb_threads, 0 );
		}

		CloseHandle( gp_al_mgr->ual_mgr.h_cb_port );
	}

	cl_ptr_vector_destroy( &gp_al_mgr->ual_mgr.cb_threads );

	/*
	 * We need to destroy the AL object before the spinlock, since
	 * destroying the AL object will try to acquire the spinlock.
	 */
	destroy_al_obj( &gp_al_mgr->obj );
	
	cl_spinlock_destroy( &gp_al_mgr->lock );

	cl_free( gp_al_mgr );
	gp_al_mgr = NULL;
}


HANDLE
ual_create_async_file(
	IN				uint32_t					type )
{
	cl_status_t				cl_status;
	ual_bind_file_ioctl_t	ioctl;
	uintn_t					bytes_ret;

	AL_ENTER( AL_DBG_MGR );

	/* Create a file object on which to issue all SA requests. */
	ioctl.h_file = HandleToHandle64( CreateFileW( L"\\\\.\\ibal",
		GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL ) );
	if( ioctl.h_file == INVALID_HANDLE_VALUE )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("CreateFile returned %d.\n", GetLastError()) );
		return INVALID_HANDLE_VALUE;
	}

	/* Bind this file object to the completion port. */
	if( !CreateIoCompletionPort(
		ioctl.h_file, gp_al_mgr->ual_mgr.h_cb_port, type, 0 ) )
	{
		CloseHandle( ioctl.h_file );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("CreateIoCompletionPort for file of type %d returned %d.\n",
			type, GetLastError()) );
		return INVALID_HANDLE_VALUE;
	}

	/*
	 * Send an IOCTL down on the main file handle to bind this file
	 * handle with our proxy context.
	 */
	cl_status = do_al_dev_ioctl(
		type, &ioctl, sizeof(ioctl), NULL, 0, &bytes_ret );
	if( cl_status != CL_SUCCESS )
	{
		CloseHandle( ioctl.h_file );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("Bind IOCTL for type %d returned %s.\n",
			type,CL_STATUS_MSG(cl_status)) );
		return INVALID_HANDLE_VALUE;
	}

	AL_EXIT( AL_DBG_MGR );
	return ioctl.h_file;
}


ib_api_status_t
ual_create_cb_threads( void )
{
	cl_status_t		cl_status;
	uint32_t		i;
	HANDLE			h_thread;

	AL_ENTER( AL_DBG_MGR );

	cl_status = cl_ptr_vector_init(
		&gp_al_mgr->ual_mgr.cb_threads, cl_proc_count(), 0 );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("cl_ptr_vector_init returned %s.\n", CL_STATUS_MSG( cl_status )) );
		return IB_ERROR;
	}

	for( i = 0; i < cl_proc_count(); i++ )
	{
		h_thread = CreateThread( NULL, 0, __cb_thread_routine, NULL, 0, NULL );
		if( !h_thread )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("CreateThread returned %d.\n", GetLastError()) );
			return IB_ERROR;
		}

		/* We already sized the vector, so insertion should work. */
		cl_status = cl_ptr_vector_insert( &gp_al_mgr->ual_mgr.cb_threads,
			h_thread, NULL );
		CL_ASSERT( cl_status == CL_SUCCESS );
	}

	AL_EXIT( AL_DBG_MGR );
	return IB_SUCCESS;
}


/*
 * Create the ual manager for the process
 */
ib_api_status_t
create_al_mgr()
{
	ib_api_status_t			ib_status;
	cl_status_t				cl_status;
	uintn_t					bytes_ret;
	ULONG					ver;

	AL_ENTER(AL_DBG_MGR);

	CL_ASSERT( !gp_al_mgr );

	/* First open the kernel device. */
	CL_ASSERT( g_al_device == INVALID_HANDLE_VALUE );
	g_al_device = CreateFileW( L"\\\\.\\ibal",
		GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL );
	if( g_al_device == INVALID_HANDLE_VALUE )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("CreateFile returned %d.\n", GetLastError()) );
		return IB_ERROR;
	}


	ver = AL_IOCTL_VERSION;

	cl_status =
		do_al_dev_ioctl( UAL_BIND, &ver, sizeof(ver), NULL, 0, &bytes_ret );
	if( cl_status != CL_SUCCESS )
		return IB_ERROR;

	gp_al_mgr = cl_zalloc( sizeof( al_mgr_t ) );
	if( !gp_al_mgr )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("Failed to cl_zalloc ual_mgr_t.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the AL manager. */
	cl_event_construct( &gp_al_mgr->ual_mgr.sync_event );
	cl_ptr_vector_construct( &gp_al_mgr->ual_mgr.cb_threads );
	cl_qlist_init( &gp_al_mgr->al_obj_list );
	cl_qlist_init( &gp_al_mgr->ci_ca_list );
	cl_spinlock_construct( &gp_al_mgr->lock );
	gp_al_mgr->ual_mgr.h_cb_port = NULL;

	/* Init the al object in the ual manager */
	construct_al_obj(&gp_al_mgr->obj, AL_OBJ_TYPE_AL_MGR);
	ib_status = init_al_obj( &gp_al_mgr->obj, gp_al_mgr, FALSE,
		NULL, __cleanup_ual_mgr, __free_ual_mgr );
	if( ib_status != IB_SUCCESS )
	{
		__free_ual_mgr( &gp_al_mgr->obj );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("init_al_obj failed, status = 0x%x.\n", ib_status) );
		return ib_status;
	}

	/* Allocate the I/O completion port for async operations. */
	gp_al_mgr->ual_mgr.h_cb_port = CreateIoCompletionPort(
		INVALID_HANDLE_VALUE, NULL, 0, 0 );
	if( !gp_al_mgr->ual_mgr.h_cb_port )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("Failed to create I/O completion port.\n") );
		return IB_ERROR;
	}

	/* Create the threads to process completion callbacks. */
	ib_status = ual_create_cb_threads();
	if( ib_status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("ual_create_cb_threads failed.\n") );
		return ib_status;
	}

	/* Create CM callback file handle. */
	//gp_al_mgr->ual_mgr.h_cm_file = ual_create_async_file( UAL_BIND_CM );
	//if( gp_al_mgr->ual_mgr.h_cq_file == INVALID_HANDLE_VALUE )
	//{
	//	gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
	//	AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
	//		("ual_create_async_file for UAL_BIND_CM returned %d.\n",
	//		GetLastError()) );
	//	return IB_ERROR;
	//}

	/* Create the CQ completion callback file handle. */
	gp_al_mgr->ual_mgr.h_cq_file = ual_create_async_file( UAL_BIND_CQ );
	if( gp_al_mgr->ual_mgr.h_cq_file == INVALID_HANDLE_VALUE )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ual_create_async_file for UAL_BIND_CQ returned %d.\n",
			GetLastError()) );
		return IB_ERROR;
	}

	/* Create the miscelaneous callback file handle. */
	gp_al_mgr->ual_mgr.h_misc_file = ual_create_async_file( UAL_BIND_MISC );
	if( gp_al_mgr->ual_mgr.h_misc_file == INVALID_HANDLE_VALUE )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ual_create_async_file for UAL_BIND_CQ returned %d.\n",
			GetLastError()) );
		return IB_ERROR;
	}

	cl_status = cl_spinlock_init( &gp_al_mgr->lock );
	if( cl_status != CL_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		return ib_convert_cl_status( cl_status );
	}

	/* With PnP support, open the AL instance before the threads
	 * get a chance to process async events
	 */

	/* Open an implicit al instance for UAL's internal usage.  This call will
	 * automatically create the gh_al.
	 */
	gh_al = NULL;
	if ((ib_status = do_open_al(&gp_al_mgr->ual_mgr.h_al)) != IB_SUCCESS)
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("do_open_al() failed, status = 0x%x.\n", ib_status) );
		return ( ib_status );
	}

	/* Create the global AL MAD pool. */
	ib_status = ib_create_mad_pool( gh_al, 0, 0, 64, &gh_mad_pool );
	if( ib_status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("ib_create_mad_pool failed with %s.\n", ib_get_err_str(ib_status)) );
		return ib_status;
	}

	/*
	 * Create a global pool key for internal MADs - they are never
	 * registered on any CA.
	 */
	ib_status = ual_reg_global_mad_pool( gh_mad_pool, &g_pool_key );
	if( ib_status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("ual_reg_global_mad_pool failed with %s.\n", ib_get_err_str(ib_status)) );
		return ib_status;
	}

	/* Create the pnp manager before the thread initialize.  This makes
	 * sure that the pnp manager is ready to process pnp callbacks as
	 * soon as the callback threads start running
	 */
	ib_status = create_pnp( &gp_al_mgr->obj );
	if( ib_status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("al_pnp_create failed with %s.\n", ib_get_err_str(ib_status)) );
		return ib_status;
	}

	/* Initialize the AL resource manager. */
	ib_status = create_res_mgr( &gp_al_mgr->obj );
	if( ib_status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("create_res_mgr failed with %s.\n", ib_get_err_str(ib_status)) );
		return ib_status;
	}

	/* Initialize the AL SA request manager. */
	ib_status = create_sa_req_mgr( &gp_al_mgr->obj );
	if( ib_status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("create_sa_req_mgr failed with %s.\n", ib_get_err_str(ib_status)) );
		return ib_status;
	}

	/* Initialize CM */
	ib_status = create_cep_mgr( &gp_al_mgr->obj );
	if( ib_status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("create_cm_mgr failed, status = 0x%x.\n", ib_status) );
		return ib_status;
	}

	cl_status = cl_event_init( &gp_al_mgr->ual_mgr.sync_event, FALSE );
	if( cl_status != CL_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		return ib_convert_cl_status( cl_status );
	}

	/* Everything is ready now.  Issue the first callback requests. */
	if( !DeviceIoControl( gp_al_mgr->ual_mgr.h_misc_file, UAL_GET_MISC_CB_INFO,
		NULL, 0,
		&gp_al_mgr->ual_mgr.misc_cb_info, sizeof(misc_cb_ioctl_info_t),
		NULL, &gp_al_mgr->ual_mgr.misc_ov ) )
	{
		if( GetLastError() != ERROR_IO_PENDING )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("DeviceIoControl for misc callback request returned %d.\n",
				GetLastError()) );
			gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
			return IB_ERROR;
		}
	}

	if( !DeviceIoControl( gp_al_mgr->ual_mgr.h_cq_file, UAL_GET_COMP_CB_INFO,
		NULL, 0,
		&gp_al_mgr->ual_mgr.comp_cb_info, sizeof(comp_cb_ioctl_info_t),
		NULL, &gp_al_mgr->ual_mgr.cq_ov ) )
	{
		if( GetLastError() != ERROR_IO_PENDING )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("DeviceIoControl for CM callback request returned %d.\n",
				GetLastError()) );
			gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
			return IB_ERROR;
		}
	}

	/*
	 * Wait until the associated kernel PnP registration completes.  This
	 * indicates that all known CAs have been reported to user-space
	 * and are being processed by the PnP manager.
	 */
#ifdef _DEBUG_
	cl_status = cl_event_wait_on( &gp_al_mgr->ual_mgr.sync_event,
		EVENT_NO_TIMEOUT, TRUE );
	CL_ASSERT ( cl_status == CL_SUCCESS);
#else
	cl_status = cl_event_wait_on( &gp_al_mgr->ual_mgr.sync_event,
		EVENT_NO_TIMEOUT, TRUE );
#endif
	
	if( cl_status != CL_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy(&gp_al_mgr->obj, NULL);
		return ib_convert_cl_status( cl_status );
	}
	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &gp_al_mgr->obj, E_REF_INIT );
	
	AL_EXIT(AL_DBG_MGR);
	return IB_SUCCESS;
}



/*
 * UAL thread start routines.
 */
static void
__process_comp_cb(
	IN		comp_cb_ioctl_info_t*			p_comp_cb_info )
{
	ib_cq_handle_t	h_cq;
	CL_ASSERT( p_comp_cb_info->cq_context );
	h_cq = (ib_cq_handle_t)(ULONG_PTR)p_comp_cb_info->cq_context;

	if( ref_al_obj( &h_cq->obj ) > 1 )
	{
		CL_ASSERT( h_cq->pfn_user_comp_cb );
		h_cq->pfn_user_comp_cb( h_cq, (void*)h_cq->obj.context );
	}
	deref_al_obj( &h_cq->obj );
}



/* Thread to process the asynchronous completion notifications */
void
cq_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov )
{
	AL_ENTER( AL_DBG_CQ );

	UNUSED_PARAM( p_ov );

	if( !error_code && ret_bytes )
	{
		/* Check the record type and adjust the pointers */
		/*	TBD	*/
		__process_comp_cb( &gp_al_mgr->ual_mgr.comp_cb_info );
	}
	
	if( error_code != ERROR_OPERATION_ABORTED )
	{
		if( !DeviceIoControl( gp_al_mgr->ual_mgr.h_cq_file, UAL_GET_COMP_CB_INFO,
			NULL, 0,
			&gp_al_mgr->ual_mgr.comp_cb_info, sizeof(comp_cb_ioctl_info_t),
			NULL, &gp_al_mgr->ual_mgr.cq_ov ) )
		{
			if( GetLastError() != ERROR_IO_PENDING )
			{
				AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
					("DeviceIoControl for CM callback request returned %d.\n",
					GetLastError()) );
			}
		}
	}

	AL_EXIT( AL_DBG_CQ );
}



/* Thread to process miscellaneous asynchronous events */
void
misc_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov )
{
	AL_ENTER( AL_DBG_MGR );

	UNUSED_PARAM( p_ov );

	if( !error_code && ret_bytes )
	{
		/* Check the record type and adjust the pointers */
		/*	TBD	*/
		__process_misc_cb( &gp_al_mgr->ual_mgr.misc_cb_info );
	}
	
	if( error_code != ERROR_OPERATION_ABORTED )
	{
		/* Issue the next request. */
		if( !DeviceIoControl( gp_al_mgr->ual_mgr.h_misc_file, UAL_GET_MISC_CB_INFO,
			NULL, 0,
			&gp_al_mgr->ual_mgr.misc_cb_info, sizeof(misc_cb_ioctl_info_t),
			NULL, &gp_al_mgr->ual_mgr.misc_ov ) )
		{
			if( GetLastError() != ERROR_IO_PENDING )
			{
				AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
					("DeviceIoControl for misc callback request returned %d.\n",
					GetLastError()) );
			}
		}
	}

	AL_EXIT( AL_DBG_MGR );
}



void
__process_misc_cb(
	IN		misc_cb_ioctl_info_t*		p_misc_cb_info )
{
	switch( p_misc_cb_info->rec_type )
	{
	case CA_ERROR_REC:
	case QP_ERROR_REC:
	case SRQ_ERROR_REC:
	case CQ_ERROR_REC:
	{
		/* Initiate user-mode asynchronous event processing. */
		ci_ca_async_event( &p_misc_cb_info->ioctl_rec.event_rec );
		break;
	}
	case MCAST_REC:
	{
		ib_mcast_rec_t			mcast_rec;
		cl_memcpy((void *)&mcast_rec,
					(void*)&p_misc_cb_info->ioctl_rec.mcast_cb_ioctl_rec,
					sizeof(ib_mcast_rec_t));
		mcast_rec.p_member_rec = 
			&p_misc_cb_info->ioctl_rec.mcast_cb_ioctl_rec.member_rec;
		/******* Call the cb function for app callback *****/
		break;
	}
	case MAD_SEND_REC:
	{
		/* We got a send completion. */
		ib_mad_element_t			*p_element;

		ib_mad_svc_handle_t			h_mad_svc = (ib_mad_svc_handle_t)(ULONG_PTR)
			p_misc_cb_info->ioctl_rec.mad_send_cb_ioctl_rec.mad_svc_context;

		/* Copy the data to the user's element. */
		p_element = (ib_mad_element_t*)(ULONG_PTR)
			p_misc_cb_info->ioctl_rec.mad_send_cb_ioctl_rec.p_um_mad;
		/* Only update the status if a receive wasn't failed. */
		if( p_element->status != IB_WCS_TIMEOUT_RETRY_ERR )
		{
			p_element->status =
				p_misc_cb_info->ioctl_rec.mad_send_cb_ioctl_rec.wc_status;
		}
		p_element->p_next = NULL;

		/* Now the user mad_elements should have the right data
		 * Make the client callback
		 */
		h_mad_svc->pfn_user_send_cb( h_mad_svc,
			(void*)h_mad_svc->obj.context, p_element );
		break;
	}
	case MAD_RECV_REC:
	{
		/*
		 * We've receive a MAD.  We need to get a user-mode MAD of the
		 * correct size, then send it down to retrieve the received MAD.
		 */
		ual_mad_recv_ioctl_t	ioctl_buf;
		uintn_t					bytes_ret;
		cl_status_t				cl_status;
		ib_api_status_t			status;
		ib_mad_svc_handle_t		h_mad_svc;
		ib_mad_element_t		*p_mad = NULL;
		ib_mad_element_t		*p_send_mad;
		ib_mad_t				*p_mad_buf = NULL;
		ib_grh_t				*p_grh = NULL;

		h_mad_svc = (ib_mad_svc_handle_t)(ULONG_PTR)
			p_misc_cb_info->ioctl_rec.mad_recv_cb_ioctl_rec.mad_svc_context;

		p_send_mad = (ib_mad_element_t*)(ULONG_PTR)
			p_misc_cb_info->ioctl_rec.mad_recv_cb_ioctl_rec.p_send_mad;

		cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

		/*
		 * Get a MAD large enough to receive the MAD.  If we can't get a
		 * MAD, we still perform the IOCTL so that the kernel will return
		 * the MAD to its pool, resulting in a dropped MAD.
		 */
		status = ib_get_mad( h_mad_svc->obj.p_ci_ca->pool_key,
			p_misc_cb_info->ioctl_rec.mad_recv_cb_ioctl_rec.elem_size,
			&p_mad );

		/*
		 * Note that we set any associated send MAD's status here
		 * in case of failure.
		 */
		if( status == IB_SUCCESS )
			al_handoff_mad( (ib_al_handle_t)h_mad_svc->obj.h_al, p_mad );
		else if( p_send_mad )
			p_send_mad->status = IB_WCS_TIMEOUT_RETRY_ERR;

		ioctl_buf.in.p_user_mad = (ULONG_PTR)p_mad;

		if( p_mad )
		{
			/* Save off the pointers since the proxy overwrites the element. */
			p_mad_buf = p_mad->p_mad_buf;
			p_grh = p_mad->p_grh;

			ioctl_buf.in.p_mad_buf = (ULONG_PTR)p_mad_buf;
			ioctl_buf.in.p_grh = (ULONG_PTR)p_grh;
		}
		ioctl_buf.in.h_mad = p_misc_cb_info->ioctl_rec.mad_recv_cb_ioctl_rec.h_mad;

		cl_status = do_al_dev_ioctl( UAL_MAD_RECV_COMP,
			&ioctl_buf.in, sizeof(ioctl_buf.in),
			&ioctl_buf.out, sizeof(ioctl_buf.out),
			&bytes_ret );
		if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
		{
			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("UAL_MAD_RECV_COMP IOCTL returned %s.\n",
				CL_STATUS_MSG(cl_status)) );
			status = IB_ERROR;
		}
		else
		{
			status = ioctl_buf.out.status;
		}
		if( p_mad )
		{
			if( status == IB_SUCCESS )
			{
				/* We need to reset MAD data pointers. */
				p_mad->p_mad_buf = p_mad_buf;
				p_mad->p_grh = p_grh;
				/* Restore the client's send context1 */
				if( p_send_mad )
					p_mad->send_context1 = (void*)p_send_mad->context1;
                
				h_mad_svc->pfn_user_recv_cb( h_mad_svc,
					(void*)h_mad_svc->obj.context, p_mad );
			}
			else
			{
				ib_put_mad( p_mad );
			}
		}
		break;
	}
	case SVC_REG_REC:
	{
		break;
	}
	case QUERY_REC:
	{
		break;
	}
	case PNP_REC:
	{
		ib_pnp_event_t					pnp_event;
		ib_net64_t						ca_guid;
		al_ci_ca_t						*p_ci_ca;
		ib_ca_attr_t					*p_old_ca_attr;
		ib_api_status_t					status;

		pnp_event = p_misc_cb_info->ioctl_rec.pnp_cb_ioctl_rec.pnp_event;
		ca_guid = p_misc_cb_info->ioctl_rec.pnp_cb_ioctl_rec.pnp_info.ca.ca_guid;

		switch( pnp_event )
		{
		case IB_PNP_CA_ADD:
			/* Create a new CI CA. */
			create_ci_ca( gh_al, &gp_al_mgr->obj,
				p_misc_cb_info->ioctl_rec.pnp_cb_ioctl_rec.pnp_info.ca.ca_guid );
			break;

		case IB_PNP_CA_REMOVE:
			/* Destroy the CI CA. */
			cl_spinlock_acquire( &gp_al_mgr->obj.lock );
			p_ci_ca = find_ci_ca( ca_guid );
			if( !p_ci_ca )
			{
				cl_spinlock_release( &gp_al_mgr->obj.lock );
				break;
			}
			ref_al_obj( &p_ci_ca->obj );
			cl_spinlock_release( &gp_al_mgr->obj.lock );

			p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
			break;

		case IB_PNP_PORT_ADD:
		case IB_PNP_PORT_REMOVE:
			/* Should never get these. */
			break;

		case IB_PNP_REG_COMPLETE:
			/*
			 * Signal that the kernel PnP registration is done, indicating
			 * that the current system state has been reported to the user.
			 */
			cl_event_signal( &gp_al_mgr->ual_mgr.sync_event );
			break;

		default:
			/* Process the PnP event - most likely a port change event. */
			cl_spinlock_acquire( &gp_al_mgr->obj.lock );
			p_ci_ca = find_ci_ca( ca_guid );
			if( !p_ci_ca )
			{
				cl_spinlock_release( &gp_al_mgr->obj.lock );
				break;
			}
			ref_al_obj( &p_ci_ca->obj );
			cl_spinlock_release( &gp_al_mgr->obj.lock );

			status = ci_ca_update_attr( p_ci_ca, &p_old_ca_attr );
			if( status != IB_SUCCESS) {
				AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
					("update CA attributes returned %#x.\n", status) );
	
				/* Dereference the CA now. */
				deref_al_obj( &p_ci_ca->obj );
				break;
			}
			if ( p_old_ca_attr )
				cl_free( p_old_ca_attr );

			/* Dereference the CA now. */
			deref_al_obj( &p_ci_ca->obj );
			break;
		}

		break;  /* For PNP_EVENT_REC */
	}
	default:
		CL_ASSERT (0);
		break;
	}
}



/*
 * Create a new instance of the access layer.
 */
ib_api_status_t
ib_open_al(
		OUT			ib_al_handle_t* const		ph_al )
{
	ib_api_status_t		status;

	cl_mutex_acquire( &g_open_close_mutex );
	status = do_open_al( ph_al );
	if( status == IB_SUCCESS )
	{
		/*
		 * Bump the open count.  Note that we only do this for external
		 * calls, not the internal ib_open_al call.
		 */
		cl_atomic_inc( &g_open_cnt );
	}
	cl_mutex_release( &g_open_close_mutex );
	return status;
}


ib_api_status_t
ib_close_al(
	IN		const	ib_al_handle_t				h_al )
{
	ib_api_status_t		status;

	cl_mutex_acquire( &g_open_close_mutex );
	status = do_close_al( h_al );
	if( status == IB_SUCCESS && !cl_atomic_dec( &g_open_cnt ) )
		al_cleanup();
	cl_mutex_release( &g_open_close_mutex );
	return status;
}


ib_api_status_t
do_open_al(
		OUT			ib_al_handle_t* const		ph_al )
{
	ib_al_handle_t			h_al;
	ib_api_status_t			status;

	AL_ENTER(AL_DBG_MGR);

	if( !ph_al )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/*
	 * Initialize AL if needed.
	 * This should only occur on the first ib_open_al call.
	 */
	if( !gp_al_mgr )
	{
		status = al_initialize();
		if( status != IB_SUCCESS )
		{
			al_cleanup();
			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("ual_init failed, status = %s\n", ib_get_err_str(status) ) );
			return status;
		}
		/*
		* Wait for 50ms before returning. This ensures the pnp events are
		* delivered before any special qp services are invoked.
		*/
		cl_thread_suspend( 50 );
	}

	/* Allocate an access layer instance. */
	h_al = (ib_al_handle_t)cl_zalloc( sizeof( ib_al_t ) );
	if( !h_al )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("cl_malloc failed\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the instance. */
	construct_al_obj( &h_al->obj, AL_OBJ_TYPE_H_AL );
	cl_spinlock_construct( &h_al->mad_lock );
	cl_qlist_init( &h_al->mad_list );
	cl_qlist_init( &h_al->key_list );
	cl_qlist_init( &h_al->query_list );
	cl_qlist_init( &h_al->cep_list );

	if( cl_spinlock_init( &h_al->mad_lock ) != CL_SUCCESS )
	{
		free_al( &h_al->obj );
		AL_EXIT( AL_DBG_ERROR );
		return IB_ERROR;
	}

	/* Initialize the base object. */
	status = init_al_obj( &h_al->obj, NULL, FALSE,
		destroying_al, NULL, free_al );
	if( status != IB_SUCCESS )
	{
		free_al( &h_al->obj );
		AL_EXIT(AL_DBG_MGR);
		return status;
	}
	attach_al_obj( &gp_al_mgr->obj, &h_al->obj );

	/*
	 * Self reference the AL instance so that all attached objects
	 * insert themselve in the instance's handle manager automatically.
	 */
	h_al->obj.h_al = h_al;

	/*
	 * We only maintain a single AL instance in the kernel.  It is created
	 * automatically when the device is opened.
	 */
	if( !gh_al )
	{
		/* Save a copy of the implicit al handle in a global */
		gh_al = h_al;
	}

	/* Return UAL's handle to caller */
	*ph_al = (ib_al_handle_t)h_al;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_al->obj, E_REF_INIT );

	AL_EXIT(AL_DBG_MGR);
	return IB_SUCCESS;
}


static DWORD WINAPI
__cb_thread_routine(
	IN				void						*context )
{
	DWORD		ret_bytes, err;
	OVERLAPPED	*p_ov;
	ULONG_PTR	key;
	BOOL		ret;

	AL_ENTER( AL_DBG_MGR );

	UNUSED_PARAM( context );

	do
	{
		ret = GetQueuedCompletionStatus( gp_al_mgr->ual_mgr.h_cb_port,
			&ret_bytes, &key, &p_ov, INFINITE );

		if( ret && !p_ov )
			break;

		if( !ret )
			err = GetLastError();
		else
			err = 0;

		CL_ASSERT( p_ov );
		switch( key )
		{
		case UAL_BIND_CM:
			//DebugBreak();
			/* CM callback. */
			cm_cb( err, ret_bytes, p_ov );
			break;

		case UAL_BIND_CQ:
			/* CQ completion callback. */
			cq_cb( err, ret_bytes, p_ov );
			break;

		case UAL_BIND_MISC:
			/* Misc callback. */
			misc_cb( err, ret_bytes, p_ov );
			break;

		case UAL_BIND_PNP:
			/* PnP callback. */
			pnp_cb( err, ret_bytes, p_ov );
			break;

		case UAL_BIND_SA:
			/* SA callback. */
			sa_req_cb( err, ret_bytes, p_ov );
			break;

		case UAL_BIND_DESTROY:
			if( p_ov )
				deref_al_obj( (al_obj_t*)p_ov->Pointer );
			break;

		default:
			CL_ASSERT( key == UAL_BIND_CM || key == UAL_BIND_CQ ||
				key == UAL_BIND_MISC || key == UAL_BIND_PNP ||
				key == UAL_BIND_SA || key == UAL_BIND_DESTROY );
			break;
		}
	} while( !ret || p_ov );

	AL_EXIT( AL_DBG_MGR );
	ExitThread( 0 );
}
