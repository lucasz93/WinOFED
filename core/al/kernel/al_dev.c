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

#include <iba/ib_al.h>
#include <complib/cl_qmap.h>
#include <complib/cl_memory.h>
#include <complib/cl_qpool.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_vector.h>
#include <complib/cl_spinlock.h>

#include "al.h"
#include "al_ca.h"
#include "al_cq.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_dev.tmh"
#endif
#include "al_dev.h"
#include "al_qp.h"
#include "al_mgr.h"
#include "al_proxy.h"



static cl_status_t
__proxy_reg_pnp(
	IN		al_dev_open_context_t			*p_context );

static void
__proxy_cancel_cblists(
	IN		al_dev_open_context_t			*p_context );




static void
__construct_open_context(
	IN		al_dev_open_context_t			*p_context )
{
	cl_event_construct( &p_context->close_event );

	cl_qpool_construct( &p_context->cb_pool );
	cl_spinlock_construct( &p_context->cb_pool_lock );

	cl_qlist_init( &p_context->cm_cb_list );
	cl_qlist_init( &p_context->comp_cb_list );
	cl_qlist_init( &p_context->misc_cb_list );
	cl_spinlock_construct( &p_context->cb_lock );
	cl_mutex_construct( &p_context->pnp_mutex );
}



/*
 * Initialize all objects used by the per client open context.
 */
static cl_status_t
__init_open_context(
	IN		al_dev_open_context_t			*p_context )
{
	cl_status_t		cl_status;
	NTSTATUS		status;

	cl_status = cl_event_init( &p_context->close_event, FALSE );
	if( cl_status != CL_SUCCESS )
		return cl_status;

	/* Allocate pool for storing callback info or requests. */
	cl_status = cl_qpool_init( &p_context->cb_pool,
		AL_CB_POOL_START_SIZE, 0, AL_CB_POOL_GROW_SIZE,
		sizeof(al_proxy_cb_info_t), NULL, NULL, NULL );
	if( cl_status != CL_SUCCESS )
		return cl_status;

	cl_status = cl_spinlock_init( &p_context->cb_pool_lock );
	if( cl_status != CL_SUCCESS )
		return cl_status;

	cl_status = cl_spinlock_init( &p_context->cb_lock );
	if( cl_status != CL_SUCCESS )
		return cl_status;

	cl_status = cl_mutex_init( &p_context->pnp_mutex );
	if( cl_status != CL_SUCCESS )
		return cl_status;

	status = al_csq_init( p_context, &p_context->al_csq );
	if ( !NT_SUCCESS( status ) )
		return (cl_status_t)status;
	
	return CL_SUCCESS;
}



static void
__destroy_open_context(
	IN		al_dev_open_context_t			*p_context )
{
	cl_event_destroy( &p_context->close_event );

	cl_qpool_destroy( &p_context->cb_pool );
	cl_spinlock_destroy( &p_context->cb_pool_lock );
	cl_spinlock_destroy( &p_context->cb_lock );
	cl_mutex_destroy( &p_context->pnp_mutex );
}

typedef NTSTATUS (*QUERY_INFO_PROCESS) (
    __in HANDLE ProcessHandle,
    __in PROCESSINFOCLASS ProcessInformationClass,
    __out_bcount(ProcessInformationLength) PVOID ProcessInformation,
    __in ULONG ProcessInformationLength,
    __out_opt PULONG ReturnLength
    );

QUERY_INFO_PROCESS ZwQueryInformationProcess = NULL;

#pragma warning(disable:4055)

static void __get_process_name(PUNICODE_STRING ProcessImageName)
{
    NTSTATUS status;
    ULONG returnedLength;
    ULONG bufferLength;
    PVOID pBuffer = NULL;
    PUNICODE_STRING imageName;
   
    PAGED_CODE(); // this eliminates the possibility of the IDLE Thread/Process

	__try
	{
		// find the function
	    if (NULL == ZwQueryInformationProcess) 
		{
		    UNICODE_STRING routineName;

	        RtlInitUnicodeString(&routineName, L"ZwQueryInformationProcess");

	        ZwQueryInformationProcess =
	               (QUERY_INFO_PROCESS) MmGetSystemRoutineAddress(&routineName);

	        if (NULL == ZwQueryInformationProcess) 
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("Cannot resolve ZwQueryInformationProcess\n") );
				goto exit;
	        }
	    }

		// find the size of process name
	    status = ZwQueryInformationProcess( ZwCurrentProcess(),
			ProcessImageFileName, NULL, 0,&returnedLength);

	    if (STATUS_INFO_LENGTH_MISMATCH != status) 
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ZwQueryInformationProcess failed with status %#x\n", status) );
			goto exit;
	    }

		// Sanity check:
	    // Is the passed-in buffer going to be big enough for us? 
	    // This function returns a single contguous buffer model...
	    //
	    bufferLength = returnedLength - sizeof(UNICODE_STRING);
	    if (ProcessImageName->MaximumLength < bufferLength) 
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Buffer is too small: %d, needed %d\n", ProcessImageName->MaximumLength, bufferLength) );
			goto exit;
	    }

	    // allocate temp buffer
	    pBuffer = ExAllocatePoolWithTag(PagedPool, returnedLength, 'ipgD');
	    if (NULL == pBuffer) 
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Allocation of buffer for process name failed\n") );
			goto exit;
	    }

	    // get process name
	    status = ZwQueryInformationProcess( ZwCurrentProcess(),
			ProcessImageFileName, pBuffer, returnedLength, &returnedLength);
	    if (!NT_SUCCESS(status)) 
			goto exit;

		imageName = (PUNICODE_STRING) pBuffer;
		RtlCopyUnicodeString(ProcessImageName, imageName);
		ExFreePool(pBuffer);
	}

	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Getting of process name crashed with %#x\n", GetExceptionCode()) );
		goto exit;
	}
	
	return;

exit:
	if (pBuffer)
		ExFreePool(pBuffer);
	RtlInitUnicodeString(ProcessImageName, L"Unknown process");
}
#pragma warning(default:4055)

cl_status_t
al_dev_open(
	IN				cl_ioctl_handle_t			h_ioctl )
{
	al_dev_open_context_t	*p_context;
	ib_api_status_t			status;
	cl_status_t				cl_status;
	IO_STACK_LOCATION		*p_io_stack;
	ULONG					*p_ver;

	AL_ENTER( AL_DBG_DEV );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );

	p_ver = cl_ioctl_in_buf( h_ioctl );

	if( p_io_stack->FileObject->FsContext ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(ULONG) ||
		!p_ver ||
		cl_ioctl_out_size( h_ioctl ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("context already exists or bad parameters.\n") );
		return CL_INVALID_PARAMETER;
	}

	if( *p_ver != AL_IOCTL_VERSION )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Unsupported client version: %d\n", *p_ver) );
		return CL_INVALID_PARAMETER;
	}

	/* Allocate the client's context structure. */
	p_context = (al_dev_open_context_t*)
		cl_zalloc( sizeof(al_dev_open_context_t) );
	if( !p_context )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_malloc( %d ) failed.\n", sizeof(al_dev_open_context_t)) );
		return CL_INSUFFICIENT_MEMORY;
	}

	/* get process name */
	p_context->pcs_name.MaximumLength = sizeof(p_context->pcs_name_buffer);
	p_context->pcs_name.Length = sizeof(p_context->pcs_name_buffer);
	p_context->pcs_name.Buffer = p_context->pcs_name_buffer;
	__get_process_name( &p_context->pcs_name );
	
	/* Construct the open context to allow destruction. */
	__construct_open_context( p_context );

	/* Initialize the open context elements. */
	cl_status = __init_open_context( p_context );
	if( cl_status != CL_SUCCESS )
	{
		__destroy_open_context( p_context );
		cl_free( p_context );
		return cl_status;
	}

	/* Open an internal AL instance for this process. */
	status = ib_open_al_trk( p_context->pcs_name.Buffer, &p_context->h_al );
	if( status == IB_SUCCESS )
	{
		/* Register for PnP events. */
		status = __proxy_reg_pnp( p_context );
	}

	/* Make sure that we were able to open AL and register for PnP. */
	if( status == IB_SUCCESS )
	{
		/*
		 * Store the reference from the AL instance back to this
		 * open context.  This allows using the user-mode context
		 * for resource creation.
		 */
		p_context->h_al->p_context = p_context;
		/* We successfully opened the device. */
		p_io_stack->FileObject->FsContext = p_context;
	}
	else
	{
		__destroy_open_context( p_context );
		cl_free( p_context );
		cl_status = CL_INSUFFICIENT_RESOURCES;
	}

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}



/*
 * To be called by al_dev_open(). This will register for PnP events
 * on behalf of user process (UAL). It uses the implicit global
 * al instance created by AL manager. PnP events are propagated
 * to UAL automatically from the time AL device is open till the
 * process exits.
 */
static ib_api_status_t
__proxy_reg_pnp(
	IN		al_dev_open_context_t			*p_context )
{
	ib_pnp_req_t			pnp_req;
	ib_pnp_handle_t			h_pnp;
	ib_api_status_t			status;
	
	/* Register for PnP events. */
	cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
	pnp_req.pnp_class = IB_PNP_CA | IB_PNP_FLAG_REG_COMPLETE;
	pnp_req.pnp_context = p_context;
	pnp_req.pfn_pnp_cb = proxy_pnp_ca_cb;

	/* No need to track the registration.  We'll deregister when closing AL. */
	status = ib_reg_pnp( p_context->h_al, &pnp_req, &h_pnp );
	if( status != IB_SUCCESS )
		return status;
	
	/* Register for port events. */
	pnp_req.pfn_pnp_cb = proxy_pnp_port_cb;
	pnp_req.pnp_class = IB_PNP_PORT | IB_PNP_FLAG_REG_COMPLETE;
	status = ib_reg_pnp( p_context->h_al, &pnp_req, &h_pnp );
	
	return status;
}



/*
 * Cleanup the handle map.  Remove all mappings.  Perform all necessary
 * operations.
 */
static void
__proxy_cleanup_map(
	IN		al_dev_open_context_t	*p_context )
{
	al_handle_t				*p_h;
	size_t					i;

	AL_ENTER( AL_DBG_DEV );

	cl_spinlock_acquire( &p_context->h_al->obj.lock );
	for( i = 0; i < cl_vector_get_size( &p_context->h_al->hdl_vector ); i++ )
	{
		p_h = (al_handle_t*)
			cl_vector_get_ptr( &p_context->h_al->hdl_vector, i );

		switch( AL_BASE_TYPE( p_h->type ) )
		{
		/* Return any MADs not reported to the user. */
		case AL_OBJ_TYPE_H_MAD:
			ib_put_mad( (ib_mad_element_t*)p_h->p_obj );
			al_hdl_free( p_context->h_al, i );
			break;

		case AL_OBJ_TYPE_H_CA_ATTR:
			/* Release a saved CA attribute. */
			cl_free( p_h->p_obj );
			al_hdl_free( p_context->h_al, i );
			break;

		case AL_OBJ_TYPE_H_SA_REQ:
			al_cancel_sa_req( (al_sa_req_t*)p_h->p_obj );
			break;

		case AL_OBJ_TYPE_H_PNP_EVENT:
			cl_event_signal( &((proxy_pnp_evt_t*)p_h->p_obj)->event );
			break;

		default:
			/* Nothing else to do for other handle types. */
			break;
		}
	}
	cl_spinlock_release( &p_context->h_al->obj.lock );

	AL_EXIT( AL_DBG_DEV );
}


cl_status_t
al_dev_close(
	IN				cl_ioctl_handle_t			h_ioctl )
{
	al_dev_open_context_t	*p_context;
	IO_STACK_LOCATION		*p_io_stack;

	AL_ENTER( AL_DBG_DEV );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );

	/* Determine if the client closed the al_handle. */
	if (!p_io_stack->FileObject)
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Client closed with a null open context .\n") );
		return CL_SUCCESS;
	}
	p_context = (al_dev_open_context_t*)p_io_stack->FileObject->FsContext;
	if( !p_context )
	{
		AL_EXIT( AL_DBG_DEV );
		return CL_SUCCESS;
	}
	if( p_io_stack->FileObject->FsContext2 )
	{
		/* Not the main file object - ignore. */
		AL_EXIT( AL_DBG_DEV );
		return CL_SUCCESS;
	}

	/* Mark that we're closing this device. */
	p_context->closing = TRUE;

	/* Flush any pending IOCTLs in case user-mode threads died on us. */
	al_csq_flush_que( &p_context->al_csq, STATUS_CANCELLED );

	while( p_context->ref_cnt )
	{
#ifdef _DEBUG_
		cl_status_t		cl_status;

		cl_status = cl_event_wait_on( &p_context->close_event, 1000, FALSE );
		ASSERT( cl_status == IB_SUCCESS );
		if( cl_status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Waiting on ref_cnt timed out!\n") );
			break;
		}
#else
		cl_event_wait_on( &p_context->close_event, EVENT_NO_TIMEOUT, FALSE );
#endif
	}

	/* Cleanup any leftover callback resources. */
	__proxy_cancel_cblists( p_context );

	/* Close the AL instance for this process. */
	if( p_context->h_al )
	{
		/* Cleanup all user to kernel handle mappings. */
		__proxy_cleanup_map( p_context );

		ib_close_al( p_context->h_al );
		p_context->h_al = NULL;
	}

	/* Destroy the open context now. */
	__destroy_open_context( p_context );
	cl_free( p_context );

    p_io_stack->FileObject->FsContext = NULL;

	AL_EXIT( AL_DBG_DEV );
	return CL_SUCCESS;
}



/*
 * Remove all callbacks on the given callback queue and return them to
 * the callback pool.
 */
static void
__proxy_dq_cblist(
	IN		al_dev_open_context_t		*p_context,
	IN		cl_qlist_t					*p_cblist )
{
	cl_list_item_t				*p_list_item;
	al_proxy_cb_info_t			*p_cb_info;

	cl_spinlock_acquire( &p_context->cb_lock );
	for( p_list_item = cl_qlist_remove_head( p_cblist );
		 p_list_item != cl_qlist_end( p_cblist );
		 p_list_item = cl_qlist_remove_head( p_cblist ) )
	{
		p_cb_info = (al_proxy_cb_info_t*)p_list_item;
		if( p_cb_info->p_al_obj )
			deref_al_obj( p_cb_info->p_al_obj );
		proxy_cb_put( p_cb_info );
	}
	cl_spinlock_release( &p_context->cb_lock );
}



/*
 * Remove all queued callbacks from all callback lists.
 */
static void
__proxy_cancel_cblists(
	IN		al_dev_open_context_t			*p_context )
{
	__proxy_dq_cblist( p_context, &p_context->cm_cb_list );
	__proxy_dq_cblist( p_context, &p_context->comp_cb_list );
	__proxy_dq_cblist( p_context, &p_context->misc_cb_list );
}

LPCSTR IoctlCode2StatusStr(uint32_t ioctl_code)
{
    switch (ioctl_code)
    {
        case UAL_REG_SVC:     return "UAL_REG_SVC";
        case UAL_DEREG_SVC: return "UAL_DEREG_SVC";              
        case UAL_SEND_SA_REQ: return "UAL_SEND_SA_REQ";
        case UAL_CANCEL_SA_REQ:     return "UAL_CANCEL_SA_REQ";
        case UAL_MAD_SEND:    return "UAL_MAD_SEND";
        case UAL_INIT_DGRM_SVC:  return "UAL_INIT_DGRM_SVC";

        case UAL_REG_MAD_SVC:  return "UAL_REG_MAD_SVC";
        case UAL_DEREG_MAD_SVC:  return "UAL_DEREG_MAD_SVC";
        case UAL_CANCEL_MAD:  return "UAL_CANCEL_MAD";
        case UAL_GET_SPL_QP_ALIAS:  return "UAL_GET_SPL_QP_ALIAS";
        case UAL_MAD_RECV_COMP:  return "UAL_MAD_RECV_COMP";
        case UAL_LOCAL_MAD:  return "UAL_LOCAL_MAD";

        default:
            break;
    }
    return "";
}

cl_status_t
al_dev_ioctl(
	IN				cl_ioctl_handle_t			h_ioctl )
{
	cl_status_t			cl_status;
	size_t				ret_bytes = 0;
	void				*p_open_context;
	IO_STACK_LOCATION	*p_io_stack;
    uint32_t            ctl_code;

	AL_ENTER( AL_DBG_DEV );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	p_open_context = p_io_stack->FileObject->FsContext;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_DEV,
		("al_dev_ioctl: buf_size (%d) p_buf (%p).\n",
		cl_ioctl_in_size( h_ioctl ), cl_ioctl_in_buf( h_ioctl )) );

	/* Process the ioctl command. */
    ctl_code = cl_ioctl_ctl_code( h_ioctl );

	if( IS_AL_PROXY_IOCTL(ctl_code) )
    {
		cl_status = proxy_ioctl( h_ioctl, &ret_bytes );
    }
	else if( IS_VERBS_IOCTL(ctl_code) )
    {
		cl_status = verbs_ioctl( h_ioctl, &ret_bytes );
    }
	else if( IS_CEP_IOCTL(ctl_code) )
    {
		cl_status = cep_ioctl( h_ioctl, &ret_bytes );
    }
	else if( IS_AL_IOCTL(ctl_code) )
    {
		cl_status = al_ioctl( h_ioctl, &ret_bytes );
    }
	else if( IS_SUBNET_IOCTL(ctl_code) )
    {
		cl_status = subnet_ioctl( h_ioctl, &ret_bytes );
    }
	else if( IS_IOC_IOCTL(ctl_code) )
    {
		cl_status = ioc_ioctl( h_ioctl, &ret_bytes );
    }
	else if( IS_NDI_IOCTL(ctl_code) )
    {
		cl_status = ndi_ioctl( h_ioctl, &ret_bytes );
    }
    else if( IS_IBAT_IOCTL(ctl_code) )
    {
        // IBAT fully handles the IRP.
        cl_status = ibat_ioctl( h_ioctl );
        AL_EXIT( AL_DBG_DEV );
        return cl_status;
    }
	else
    {
		cl_status = CL_INVALID_REQUEST;
    }

	switch( cl_status )
	{
	case CL_COMPLETED:
		/* Flip the status since the IOCTL was completed. */
		cl_status = CL_SUCCESS;
		__fallthrough;
	case CL_PENDING:
		break;
	case CL_INVALID_REQUEST:
		/*
		 * In Windows, Driver Verifier sends bogus IOCTLs to the device.
		 * These must be passed down the device stack, and so cannot be
		 * completed in the IOCTL handler.  They are properly cleaned up,
		 * though no data is returned to the user.
		 */
		break;
	default:
		if ( cl_status != CL_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_DEV,
				("al_dev_ioctl: cl_status is 0x%x ioctl 0x%x %s\n",
				cl_status, cl_ioctl_ctl_code( h_ioctl ), 
				IoctlCode2StatusStr(cl_ioctl_ctl_code( h_ioctl ))));
		}
		
		cl_ioctl_complete( h_ioctl, cl_status, ret_bytes );
	}

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}

/* 
* Cleanup callbacks associated with the given object 
*/
void
cleanup_cb_misc_list(
	IN		const	ib_al_handle_t				h_al,
	IN				al_obj_t					*p_obj)
{	
	al_dev_open_context_t		*p_context = NULL;
	cl_qlist_t					*p_cblist = NULL;
	
	cl_list_item_t				*p_list_item, *p_next_item;
	al_proxy_cb_info_t			*p_cb_info;
	
	if (h_al && h_al->p_context)
	{
		/*
		 * Remove all callbacks associated with a specific al_obj 
		 * on the given callback queue and return them to
		 * the callback pool.
		 */
		p_context = h_al->p_context;
		p_cblist = &p_context->misc_cb_list;

		cl_spinlock_acquire( &p_context->cb_lock );
		for( p_list_item = cl_qlist_head( p_cblist );
			 p_list_item != cl_qlist_end( p_cblist );
			 p_list_item = p_next_item )
		{
			/* Cache the next item in case we remove this one. */
			p_next_item = cl_qlist_next( p_list_item );

			p_cb_info = (al_proxy_cb_info_t*)p_list_item;
			if ( p_cb_info->p_al_obj && ( p_cb_info->p_al_obj == p_obj ))
			{
				cl_qlist_remove_item(p_cblist, p_list_item);
				deref_al_obj( p_cb_info->p_al_obj );
				proxy_cb_put( p_cb_info );
			}
		}
		cl_spinlock_release( &p_context->cb_lock );
	}		
}

