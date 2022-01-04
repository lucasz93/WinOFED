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
#include "al_ci_ca.h"
#include "al_common.h"
#include "al_debug.h"
#include "al_ca.h"
#include "al_dev.h"
#if defined(CL_KERNEL)
#include "bus_stat.h"
#endif

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_common.tmh"
#endif

#include "al_mgr.h"
#include <complib/cl_math.h>
#include "ib_common.h"

#if defined(CL_KERNEL) && defined (NTDDI_WIN8) 
#include <ntstrsafe.h>
#else
#include <strsafe.h>
#endif

#include <winerror.h>


#if AL_OBJ_PRIVATE_ASYNC_PROC
cl_async_proc_t		*gp_async_obj_mgr = NULL;
#endif

#if DBG
trace_db_t		g_av_trace;
trace_db_t		g_mad_trace;
#endif


boolean_t
destroy_obj(
	IN				struct _al_obj				*p_obj,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb );


void
async_destroy_cb(
	IN				cl_async_proc_item_t		*p_item );


void
async_destroy_obj(
	IN				struct _al_obj				*p_obj,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb );


void
sync_destroy_obj(
	IN				struct _al_obj				*p_obj,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb );



const char* ib_obj_type_str[] =
{
	"AL_OBJ_TYPE_UNKNOWN",
	"AL_OBJ_TYPE_H_AL",
	"AL_OBJ_TYPE_H_QP",
	"AL_OBJ_TYPE_H_AV",
	"AL_OBJ_TYPE_H_MR",
	"AL_OBJ_TYPE_H_MW",
	"AL_OBJ_TYPE_H_PD",
	"AL_OBJ_TYPE_H_CA",
	"AL_OBJ_TYPE_H_CQ",
	"AL_OBJ_TYPE_H_CONN",
	"AL_OBJ_TYPE_H_LISTEN",
	"AL_OBJ_TYPE_H_IOC",
	"AL_OBJ_TYPE_H_SVC_ENTRY",
	"AL_OBJ_TYPE_H_PNP",
	"AL_OBJ_TYPE_H_SA_REQ",
	"AL_OBJ_TYPE_H_MCAST",
	"AL_OBJ_TYPE_H_ATTACH",
	"AL_OBJ_TYPE_H_MAD",
	"AL_OBJ_TYPE_H_MAD_POOL",
	"AL_OBJ_TYPE_H_POOL_KEY",
	"AL_OBJ_TYPE_H_MAD_SVC",
	"AL_OBJ_TYPE_CI_CA",
	"AL_OBJ_TYPE_CM",
	"AL_OBJ_TYPE_SMI",
	"AL_OBJ_TYPE_DM",
	"AL_OBJ_TYPE_IOU",
	"AL_OBJ_TYPE_LOADER",
	"AL_OBJ_TYPE_MAD_POOL",
	"AL_OBJ_TYPE_MAD_DISP",
	"AL_OBJ_TYPE_AL_MGR",
	"AL_OBJ_TYPE_PNP_MGR",
	"AL_OBJ_TYPE_IOC_PNP_MGR",
	"AL_OBJ_TYPE_IOC_PNP_SVC",
	"AL_OBJ_TYPE_QUERY_SVC",
	"AL_OBJ_TYPE_MCAST_SVC",
	"AL_OBJ_TYPE_SA_REQ_SVC",
	"AL_OBJ_TYPE_RES_MGR",
	"AL_OBJ_TYPE_H_CA_ATTR",
	"AL_OBJ_TYPE_H_PNP_EVENT",
	"AL_OBJ_TYPE_H_SA_REG",
	"AL_OBJ_TYPE_H_FMR",
	"AL_OBJ_TYPE_H_SRQ",
	"AL_OBJ_TYPE_H_FMR_POOL",
	"AL_OBJ_TYPE_NDI"
};


/*
 * Used to force synchronous destruction of AL objects.
 */
void
__sync_destroy_cb(
	IN				void						*context )
{
	UNUSED_PARAM( context );
}

void
construct_al_obj(
	IN				al_obj_t * const			p_obj,
	IN		const	al_obj_type_t				obj_type )
{	
	CL_ASSERT( p_obj );
	cl_memclr( p_obj, sizeof( al_obj_t ) );

	cl_spinlock_construct( &p_obj->lock );
	p_obj->state = CL_UNINITIALIZED;
	p_obj->type = obj_type;
	p_obj->timeout_ms = AL_DEFAULT_TIMEOUT_MS;
	p_obj->ref_cnt = 1;
#if DBG
	ref_trace_init(p_obj);
#endif
	cl_event_construct( &p_obj->event );

	/* Insert the object into the global tracking list. */
	if( p_obj != &gp_al_mgr->obj )
	{
		cl_spinlock_acquire( &gp_al_mgr->lock );
		cl_qlist_insert_tail( &gp_al_mgr->al_obj_list, &p_obj->list_item );
		cl_spinlock_release( &gp_al_mgr->lock );
		ref_ctx_al_obj( &gp_al_mgr->obj, E_REF_CONSTRUCT_CHILD );
	}
}



ib_api_status_t
init_al_obj(
	IN				al_obj_t * const			p_obj,
	IN		const	void* const					context,
	IN				boolean_t					async_destroy,
	IN		const	al_pfn_destroying_t			pfn_destroying,
	IN		const	al_pfn_cleanup_t			pfn_cleanup,
	IN		const	al_pfn_free_t				pfn_free )
{
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_AL_OBJ );
	CL_ASSERT( p_obj && pfn_free );
	CL_ASSERT( p_obj->state == CL_UNINITIALIZED );
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ,
		("%p\n", p_obj ) );

	if ( p_obj->type == AL_OBJ_TYPE_H_CA )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("init_al_obj: init CA %s, obj state %d\n", 
			( async_destroy ) ? "ASYNCHRONICALLY" :"SYNCHRONICALLY", p_obj->state  ));
	}

	/* Initialize the object. */
	p_obj->async_item.pfn_callback = async_destroy_cb;
	p_obj->pfn_free = pfn_free;

	p_obj->context = context;

	if( async_destroy )
		p_obj->pfn_destroy = async_destroy_obj;
	else
		p_obj->pfn_destroy = sync_destroy_obj;

	p_obj->pfn_destroying = pfn_destroying;

	p_obj->pfn_cleanup = pfn_cleanup;
	p_obj->user_destroy_cb = NULL;

	cl_qlist_init( &p_obj->obj_list );
	cl_status = cl_spinlock_init( &p_obj->lock );
	if( cl_status != CL_SUCCESS )
	{
		return ib_convert_cl_status( cl_status );
	}

	cl_status = cl_event_init( &p_obj->event, FALSE );
	if( cl_status != CL_SUCCESS )
	{
		return ib_convert_cl_status( cl_status );
	}

	p_obj->state = CL_INITIALIZED;

	/*
	 * Hold an extra reference on the object until creation is complete.
	 * This prevents a client's destruction of the object during asynchronous
	 * event callback processing from deallocating the object before the
	 * creation is complete.
	 */
	ref_ctx_al_obj( p_obj, E_REF_INIT);

	AL_EXIT( AL_DBG_AL_OBJ );
	return IB_SUCCESS;
}


void
reset_al_obj(
	IN				al_obj_t * const			p_obj )
{
	CL_ASSERT( p_obj && (p_obj->ref_cnt == 0) );
	CL_ASSERT( p_obj->state == CL_DESTROYING );

	p_obj->ref_cnt = 1;
	p_obj->desc_cnt = 0;
	p_obj->state = CL_INITIALIZED;
	p_obj->h_al = NULL;
	p_obj->hdl = AL_INVALID_HANDLE;
	cl_event_reset( &p_obj->event );
	
#if DBG
	ref_trace_destroy(p_obj);
	ref_trace_init(p_obj);
#endif
}



void
set_al_obj_timeout(
	IN				al_obj_t * const			p_obj,
	IN		const	uint32_t					timeout_ms )
{
	CL_ASSERT( p_obj );

	/* Only increase timeout values. */
	p_obj->timeout_ms = MAX( p_obj->timeout_ms, timeout_ms );
}



void
inc_al_obj_desc(
	IN				al_obj_t * const			p_obj,
	IN		const	uint32_t					desc_cnt )
{
	CL_ASSERT( p_obj );

	/* Increment the number of descendants. */
	p_obj->desc_cnt += desc_cnt;
}



ib_api_status_t
attach_al_obj(
	IN				al_obj_t * const			p_parent_obj,
	IN				al_obj_t * const			p_child_obj )
{
	AL_ENTER( AL_DBG_AL_OBJ );
	
	CL_ASSERT( p_child_obj->state == CL_INITIALIZED );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ,
		("%p(%s) to %p(%s)\n",
		p_child_obj, ib_get_obj_type( p_child_obj ),
		p_parent_obj, ib_get_obj_type( p_parent_obj ) ) );

	/* Insert the object into the parent's object tracking list. */
	p_child_obj->p_ci_ca = p_parent_obj->p_ci_ca;
	cl_spinlock_acquire( &p_parent_obj->lock );
	if( p_parent_obj->state != CL_INITIALIZED )
	{
		cl_spinlock_release( &p_parent_obj->lock );
		return IB_INVALID_STATE;
	}
	cl_qlist_insert_tail( &p_parent_obj->obj_list,
		(cl_list_item_t*)&p_child_obj->pool_item );
	p_child_obj->p_parent_obj = p_parent_obj;
	cl_spinlock_release( &p_parent_obj->lock );

	if( p_parent_obj->h_al )
	{
		if( !p_child_obj->h_al )
		{
			p_child_obj->h_al = p_parent_obj->h_al;
#ifdef CL_KERNEL
			p_child_obj->hdl = al_hdl_insert_obj( p_child_obj );
			if( p_child_obj->hdl == AL_INVALID_HANDLE )
			{
				cl_spinlock_acquire( &p_parent_obj->lock );
				cl_qlist_remove_item( &p_parent_obj->obj_list,
					(cl_list_item_t*)&p_child_obj->pool_item );
				p_child_obj->p_parent_obj = NULL;
				cl_spinlock_release( &p_parent_obj->lock );
				return IB_INSUFFICIENT_MEMORY;
			}
#endif
		}
		else
		{
			CL_ASSERT( p_child_obj->h_al == p_parent_obj->h_al );
		}
	}

	/* Reference the parent. */
	ref_al_obj( p_parent_obj );
	AL_EXIT( AL_DBG_AL_OBJ );
	return IB_SUCCESS;
}



/*
 * Called to release a child object from its parent.
 */
void
detach_al_obj(
	IN				al_obj_t * const			p_obj )
{
	al_obj_t				*p_parent_obj;

	AL_ENTER( AL_DBG_AL_OBJ );
	
	p_parent_obj = p_obj->p_parent_obj;
	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->state == CL_INITIALIZED ||
		p_obj->state == CL_DESTROYING );
	CL_ASSERT( p_parent_obj );
	CL_ASSERT( p_parent_obj->state == CL_INITIALIZED ||
		p_parent_obj->state == CL_DESTROYING );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ,
		("%p(%s) from %p(%s)\n",
		p_obj, ib_get_obj_type( p_obj ),
		p_parent_obj, ib_get_obj_type( p_parent_obj ) ) );

	/* Remove the object from the parent's list. */
	cl_spinlock_acquire( &p_parent_obj->lock );
	cl_qlist_remove_item( &p_parent_obj->obj_list,
		(cl_list_item_t*)&p_obj->pool_item );
	cl_spinlock_release( &p_parent_obj->lock );
	AL_EXIT( AL_DBG_AL_OBJ );
}


/*
 * Increment a reference count on an object.  This object should not be
 * an object's parent.
 */
int32_t
ref_al_obj_inner(
	IN				al_obj_t * const			p_obj )
{
	uint32_t	ref_cnt;

	AL_ENTER( AL_DBG_AL_OBJ );
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ,
		("%p(%s)\n", p_obj, ib_get_obj_type( p_obj ) ) );
	ref_cnt = cl_atomic_inc( &p_obj->ref_cnt );
	CL_ASSERT( ref_cnt != 1 || p_obj->type == AL_OBJ_TYPE_H_CQ );

	AL_EXIT( AL_DBG_AL_OBJ );
	return ref_cnt;
}


/*
 * Decrement the reference count on an AL object.  Destroy the object if
 * it is no longer referenced.  This object should not be an object's parent.
 */
int32_t
deref_al_obj_inner(
	IN				al_obj_t * const			p_obj )
{
	int32_t			ref_cnt;

	AL_ENTER( AL_DBG_AL_OBJ );

	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->state == CL_INITIALIZED ||
		p_obj->state == CL_DESTROYING );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ,
		("%p(%s)\n", p_obj, ib_get_obj_type( p_obj ) ) );

	CL_ASSERT( p_obj->ref_cnt > 0 );
	ref_cnt = cl_atomic_dec( &p_obj->ref_cnt );

	/* If the reference count went to 0, the object should be destroyed. */
	if( ref_cnt <= 0 )
	{
		if( ref_cnt == 0 )
		{
			if( p_obj->pfn_destroy == async_destroy_obj &&
				p_obj->user_destroy_cb != __sync_destroy_cb )
			{
				/* Queue the object for asynchronous destruction. */
#if AL_OBJ_PRIVATE_ASYNC_PROC
				cl_async_proc_queue( gp_async_obj_mgr, &p_obj->async_item );
#else
				cl_async_proc_queue( gp_async_proc_mgr, &p_obj->async_item );
#endif
			}
			else
			{
				/* Signal an event for synchronous destruction. */
				cl_event_signal( &p_obj->event );
			}
		}
		else
			CL_ASSERT( FALSE );
	}

	AL_EXIT( AL_DBG_AL_OBJ );
	return ref_cnt;
}

int32_t
deref_al_obj_cb(
	IN				al_obj_t * const			p_obj )
{
	return deref_al_obj(p_obj);
}


/*
 * Called to cleanup all resources allocated by an object.
 */
void
destroy_al_obj(
	IN				al_obj_t * const			p_obj )
{
	AL_ENTER( AL_DBG_AL_OBJ );

	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->state == CL_DESTROYING ||
		p_obj->state == CL_UNINITIALIZED );
	CL_ASSERT( cl_is_qlist_empty( &p_obj->obj_list ) );

	/* Remove the object from the global tracking list. */
	if( p_obj != &gp_al_mgr->obj )
	{
		cl_spinlock_acquire( &gp_al_mgr->lock );
		cl_qlist_remove_item( &gp_al_mgr->al_obj_list, &p_obj->list_item );
		cl_spinlock_release( &gp_al_mgr->lock );
		deref_ctx_al_obj( &gp_al_mgr->obj, E_REF_CONSTRUCT_CHILD );
	}

	cl_event_destroy( &p_obj->event );
	cl_spinlock_destroy( &p_obj->lock );
	
#if DBG
	ref_trace_destroy(p_obj);
#endif

	p_obj->state = CL_DESTROYED;

	AL_EXIT( AL_DBG_AL_OBJ );
}



void
async_destroy_obj(
	IN				struct _al_obj				*p_obj,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb )
{
	AL_ENTER( AL_DBG_AL_OBJ );

	if( pfn_destroy_cb == ib_sync_destroy )
		sync_destroy_obj( p_obj, pfn_destroy_cb );
	else if( destroy_obj( p_obj, pfn_destroy_cb ) )
		deref_al_obj( p_obj );	/* Only destroy the object once. */

	AL_EXIT( AL_DBG_AL_OBJ );
}



void
sync_destroy_obj(
	IN				struct _al_obj				*p_obj,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb )
{
	cl_status_t		cl_status;

	uint64_t t1;
	uint64_t t2;
	uint64_t t3 = 0;

	size_t async_size;
	uint32_t wait_index = 0;
	
	AL_ENTER( AL_DBG_AL_OBJ );

	if( !destroy_obj( p_obj, pfn_destroy_cb ) )
	{
		/* Object is already being destroyed... */
		AL_EXIT( AL_DBG_AL_OBJ );
		return;
	}
#ifdef CL_KERNEL
	cleanup_cb_misc_list(p_obj->h_al, p_obj);
#endif

	if( deref_al_obj( p_obj ) )
	{
		uint32_t		wait_us;
		/*
		 * Wait for all other references to go away.  We wait as long as the
		 * longest child will take, plus an additional amount based on the
		 * number of descendants.
		 */
		wait_us = (p_obj->timeout_ms * 1000) +
			(AL_TIMEOUT_PER_DESC_US * p_obj->desc_cnt);
		wait_us = MIN( wait_us, AL_MAX_TIMEOUT_US );
		t1 = cl_get_time_stamp()/1000;

		do
		{
			cl_status = CL_NOT_DONE;
			
			for (wait_index = 0; wait_index < 5; wait_index++)
			{
				cl_status = cl_event_wait_on(
					&p_obj->event, wait_us/5, AL_WAIT_ALERTABLE );
				
				if ( cl_status != CL_NOT_DONE )
					break;

				async_size = gp_async_obj_mgr->item_queue.count;
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("async_size is = 0x%x\n", (unsigned int)async_size ) );
			}
		} while( cl_status == CL_NOT_DONE );
		t2 = cl_get_time_stamp()/1000;

		

		if( cl_status != CL_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Error waiting for references to be released - delaying.\n") );
			print_al_obj( p_obj );
			/*
			 * Wait some more to handle really long timeouts by referencing
			 * objects that are not descendants.
			 */
			do
			{
				cl_status = cl_event_wait_on(
					&p_obj->event, AL_MAX_TIMEOUT_US, AL_WAIT_ALERTABLE );
			} while( cl_status == CL_NOT_DONE );
			t3 = cl_get_time_stamp()/1000;

#ifdef CL_KERNEL
			/* Free outstanding mads that are still running */
			if (( cl_status != CL_SUCCESS ) &&
				(p_obj->type == AL_OBJ_TYPE_CI_CA))
			{
				destroy_outstanding_mads(p_obj);
				do
				{
					cl_status = cl_event_wait_on(
						&p_obj->event, AL_DEFAULT_TIMEOUT_US, AL_WAIT_ALERTABLE );
				} while( cl_status == CL_NOT_DONE );
			}
#endif

			if ( p_obj->p_ci_ca && p_obj->p_ci_ca->h_ca )
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("IBAL stuck: AL object %s, ref_cnt: %d. Forcing object destruction.\n",
					ib_get_obj_type( p_obj ), p_obj->ref_cnt));
		}
#if DBG

		/* Print the ref count tracking */
		if ( cl_status != CL_SUCCESS )
		{
			if (t3)
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("Error waiting for references to be released: t2 - t1 = 0x%x, t3 - t2 = 0x%x\n", t2-t1, t3-t2));
			}
			else
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
					("Error waiting for references to be released: t2 - t1 = 0x%x\n", t2-t1));
			}
			
			//Temporary assert to enable saving the screen output 
			//before filling it with those prints
			CL_ASSERT( cl_status == CL_SUCCESS );
			ref_trace_print(p_obj);
		}
#endif
		CL_ASSERT( cl_status == CL_SUCCESS );
		if( cl_status != CL_SUCCESS )
		{
#if defined(CL_KERNEL)		
			g_post_event("IBAL: sync_destroy_obj: the references on object of type %#x was not released (ref_cnt %d). Forcing object destruction.\n", 
				p_obj->type, p_obj->ref_cnt);
#endif
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Forcing object destruction.\n") );
			print_al_obj( p_obj );
			//print_tail_al_objs();
			print_al_objs( p_obj->h_al );
			p_obj->ref_cnt = 0;
		}
	}

	async_destroy_cb( &p_obj->async_item );

	AL_EXIT( AL_DBG_AL_OBJ );
}



boolean_t
destroy_obj(
	IN				struct _al_obj				*p_obj,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb )
{
	cl_list_item_t			*p_list_item;
	al_obj_t				*p_child_obj;

	AL_ENTER( AL_DBG_AL_OBJ );

	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->state == CL_INITIALIZED ||
		p_obj->state == CL_DESTROYING );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ,
		("%p(%s)\n", p_obj, ib_get_obj_type( p_obj ) ) );

	if ( p_obj->type == AL_OBJ_TYPE_H_CA )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("destroy_obj: destroy CA %s, obj state %d\n", 
			( pfn_destroy_cb == ib_sync_destroy ) ? "SYNCHRONICALLY" :"ASYNCHRONICALLY", p_obj->state  ));
	}

	/*
	 * Lock to synchronize with asynchronous event processing.
	 * See ci_ca_async_event_cb for more information.
	 */
	cl_spinlock_acquire( &p_obj->lock );
	if( p_obj->state == CL_DESTROYING )
	{
		cl_spinlock_release( &p_obj->lock );
		deref_al_obj( p_obj );
		AL_EXIT( AL_DBG_AL_OBJ );
		return FALSE;
	}
	p_obj->state = CL_DESTROYING;
	CL_ASSERT( p_obj->ref_cnt >= 2 );
	cl_spinlock_release( &p_obj->lock );
	deref_al_obj( p_obj );

#ifdef CL_KERNEL
	/*
	 * Release this object's handle.  We must do this before calling the
	 * destroy callback so that any IOCTLs referencing this object will fail
	 */
	if( p_obj->hdl != AL_INVALID_HANDLE )
	{
		CL_ASSERT( p_obj->h_al );
		al_hdl_free_obj( p_obj );
	}
#endif

	/* Notify the object that it is being destroyed. */
	if( p_obj->pfn_destroying )
		p_obj->pfn_destroying( p_obj );

	if( p_obj->p_parent_obj )
		detach_al_obj( p_obj );

	/*	Destroy all child resources.  No need to lock during destruction. */
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ, ("destroying children\n") );
	p_list_item = cl_qlist_tail( &p_obj->obj_list );
	while( p_list_item != cl_qlist_end( &p_obj->obj_list ) )
	{
		p_child_obj = PARENT_STRUCT( p_list_item, al_obj_t, pool_item );
		CL_ASSERT( p_child_obj->pfn_destroy );
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ,
			("bye bye: %p(%s)\n", p_child_obj,
			ib_get_obj_type( p_child_obj ) ) );
		ref_al_obj( p_child_obj );
		p_child_obj->pfn_destroy( p_child_obj, NULL );

		p_list_item = cl_qlist_tail( &p_obj->obj_list );
	}

	/*
	 * Update our parent's timeout value.  Ours could have been increased
	 * when destroying one of our children's.
	 */
	if( p_obj->p_parent_obj )
	{
		set_al_obj_timeout( p_obj->p_parent_obj, p_obj->timeout_ms );
		inc_al_obj_desc( p_obj->p_parent_obj, p_obj->desc_cnt + 1 );
	}

	if( pfn_destroy_cb == ib_sync_destroy )
		p_obj->user_destroy_cb = __sync_destroy_cb;
	else
		p_obj->user_destroy_cb = pfn_destroy_cb;

	AL_EXIT( AL_DBG_AL_OBJ );
	return TRUE;
}



void
async_destroy_cb(
	IN				cl_async_proc_item_t		*p_item )
{
	al_obj_t				*p_obj;
	al_obj_t				*p_parent_obj = NULL;

	AL_ENTER( AL_DBG_AL_OBJ );

	CL_ASSERT( p_item );
	p_obj = PARENT_STRUCT( p_item, al_obj_t, async_item );
	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->state == CL_DESTROYING );
	CL_ASSERT( !p_obj->ref_cnt );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ,
		("%p\n", p_obj ) );

	/* Cleanup any hardware related resources. */
	if( p_obj->pfn_cleanup )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ, ("cleaning up\n" ) );
		p_obj->pfn_cleanup( p_obj );
	}

	/* We can now safely dereference the parent. */
	if( p_obj->p_parent_obj )
	{
		p_parent_obj = p_obj->p_parent_obj;
		p_obj->p_parent_obj = NULL;
	}

	/* Notify the user that we're done. */
	if( p_obj->user_destroy_cb )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ, ("notifying user\n" ) );
		p_obj->user_destroy_cb( (void*)p_obj->context );
	}

	/* Free the resources associated with the object. */
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_AL_OBJ, ("freeing object\n" ) );
	p_obj->pfn_free( p_obj );

	/* Dereference the parent after freeing the child. */
	if( p_parent_obj )
		deref_al_obj( p_parent_obj );
	AL_EXIT( AL_DBG_AL_OBJ );
}

#if DBG

void
insert_pool_trace(
	IN				trace_db_t*			trace_db,
	IN				void*				p_pool_item,
	IN				change_type_t		change_type,
	IN				uint16_t			ctx)
{
	
	uint32_t	entry_num = InterlockedIncrement(&trace_db->trace_index) - 1;
		
	entry_num = entry_num & (POOL_TRACE_ENTRIES-1);

	trace_db->trace[entry_num].p_pool_item = p_pool_item;
	trace_db->trace[entry_num].change = change_type;
	trace_db->trace[entry_num].trace_ctx = ctx;
#ifdef CL_KERNEL
	trace_db->trace[entry_num].p_thread = PsGetCurrentThread();
#endif
}

#endif
