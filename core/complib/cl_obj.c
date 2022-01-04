/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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


#include <complib/cl_obj.h>
#include <complib/cl_memory.h>
#include <complib/cl_debug.h>


/* Number of relation objects to add to the global pool when growing. */
#define CL_REL_POOL_SIZE	( 4096 / sizeof( cl_obj_rel_t ) )



/* The global object manager. */
cl_obj_mgr_t				*gp_obj_mgr = NULL;



/********************************************************************
 * Global Object Manager
 *******************************************************************/

cl_status_t
cl_obj_mgr_create()
{
	cl_status_t			status;

	/* See if the object manager has already been created. */
	if( gp_obj_mgr )
		return CL_SUCCESS;

	/* Allocate the object manager. */
	gp_obj_mgr = cl_zalloc( sizeof( cl_obj_mgr_t ) );
	if( !gp_obj_mgr )
		return CL_INSUFFICIENT_MEMORY;

	/* Construct the object manager. */
	cl_qlist_init( &gp_obj_mgr->obj_list );
	cl_spinlock_construct( &gp_obj_mgr->lock );
	cl_async_proc_construct( &gp_obj_mgr->async_proc_mgr );
	cl_qpool_construct( &gp_obj_mgr->rel_pool );

	/* Initialize the spinlock. */
	status = cl_spinlock_init( &gp_obj_mgr->lock );
	if( status != CL_SUCCESS )
	{
		cl_obj_mgr_destroy();
		return status;
	}

	/* Initialize the asynchronous processing manager. */
	status = cl_async_proc_init( &gp_obj_mgr->async_proc_mgr, 0, "obj_mgr" );
	if( status != CL_SUCCESS )
	{
		cl_obj_mgr_destroy();
		return status;
	}

	/* Initialize the relationship pool. */
	status = cl_qpool_init( &gp_obj_mgr->rel_pool, 0, 0, CL_REL_POOL_SIZE,
		sizeof( cl_obj_rel_t ), NULL, NULL, gp_obj_mgr );
	if( status != CL_SUCCESS )
	{
		cl_obj_mgr_destroy();
		return status;
	}

	return CL_SUCCESS;
}



void
cl_obj_mgr_destroy()
{
	cl_list_item_t			*p_list_item;
	cl_obj_t				*p_obj;

	/* See if the object manager had been created. */
	if( !gp_obj_mgr )
		return;

	/* Verify that all object's have been destroyed. */
	for( p_list_item = cl_qlist_head( &gp_obj_mgr->obj_list );
		 p_list_item != cl_qlist_end( &gp_obj_mgr->obj_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_obj = PARENT_STRUCT( p_list_item, cl_obj_t, pool_item );
#if defined( _DEBUG_ )
			cl_dbg_out( "object not destroyed %p(%i), ref_cnt: %d\n",
				p_obj, p_obj->type, p_obj->ref_cnt );
#endif
	}

	/* Destroy all object manager resources. */
	cl_spinlock_destroy( &gp_obj_mgr->lock );
	cl_async_proc_destroy( &gp_obj_mgr->async_proc_mgr );
	cl_qpool_destroy( &gp_obj_mgr->rel_pool );

	/* Free the object manager and clear the global pointer. */
	cl_free( gp_obj_mgr );
	gp_obj_mgr = NULL;
}



/*
 * Get an item to track object relationships.
 */
cl_obj_rel_t*
cl_rel_alloc()
{
	cl_obj_rel_t	*p_rel;

	CL_ASSERT( gp_obj_mgr );

	cl_spinlock_acquire( &gp_obj_mgr->lock );
	p_rel = (cl_obj_rel_t*)cl_qpool_get( &gp_obj_mgr->rel_pool );
	cl_spinlock_release( &gp_obj_mgr->lock );

	return p_rel;
}



/*
 * Return an item used to track relationships back to the pool.
 */
void
cl_rel_free(
	IN				cl_obj_rel_t * const		p_rel )
{
	CL_ASSERT( gp_obj_mgr && p_rel );

	cl_spinlock_acquire( &gp_obj_mgr->lock );
	cl_qpool_put( &gp_obj_mgr->rel_pool, &p_rel->pool_item );
	cl_spinlock_release( &gp_obj_mgr->lock );
}



/*
 * Insert an object into the global object manager's list.
 */
static void
__track_obj(
	IN				cl_obj_t					*p_obj )
{
	CL_ASSERT( gp_obj_mgr && p_obj );

	cl_spinlock_acquire( &gp_obj_mgr->lock );
	cl_qlist_insert_tail( &gp_obj_mgr->obj_list,
		(cl_list_item_t*)&p_obj->pool_item );
	cl_spinlock_release( &gp_obj_mgr->lock );
}



/*
 * Remove an object from the global object manager's list.
 */
static void
__remove_obj(
	IN				cl_obj_t					*p_obj )
{
	CL_ASSERT( gp_obj_mgr && p_obj );

	cl_spinlock_acquire( &gp_obj_mgr->lock );
	cl_qlist_remove_item( &gp_obj_mgr->obj_list,
		(cl_list_item_t*)&p_obj->pool_item );
	cl_spinlock_release( &gp_obj_mgr->lock );
}



/********************************************************************
 * Generic Object Class
 *******************************************************************/

/* Function prototypes. */
static void
__destroy_obj(
	IN				cl_obj_t					*p_obj );

static void
__destroy_cb(
	IN				cl_async_proc_item_t		*p_item );

/* Sets the state of an object and returns the old state. */
static cl_state_t
__obj_set_state(
	IN				cl_obj_t * const			p_obj,
	IN		const	cl_state_t					new_state );




void
cl_obj_construct(
	IN				cl_obj_t * const			p_obj,
	IN		const	uint32_t					obj_type )
{
	CL_ASSERT( p_obj );
	cl_memclr( p_obj, sizeof( cl_obj_t ) );

	cl_spinlock_construct( &p_obj->lock );
	p_obj->state = CL_UNINITIALIZED;
	p_obj->type = obj_type;
	cl_event_construct( &p_obj->event );

	cl_qlist_init( &p_obj->parent_list );
	cl_qlist_init( &p_obj->child_list );

	/* Insert the object into the global tracking list. */
	__track_obj( p_obj );
}



cl_status_t
cl_obj_init(
	IN				cl_obj_t * const			p_obj,
	IN				cl_destroy_type_t			destroy_type,
	IN		const	cl_pfn_obj_call_t			pfn_destroying OPTIONAL,
	IN		const	cl_pfn_obj_call_t			pfn_cleanup OPTIONAL,
	IN		const	cl_pfn_obj_call_t			pfn_free )
{
	cl_status_t				status;

	CL_ASSERT( p_obj && pfn_free );
	CL_ASSERT( p_obj->state == CL_UNINITIALIZED );

	/* The object references itself until it is destroyed. */
	p_obj->ref_cnt = 1;

	/* Record destruction callbacks. */
	p_obj->pfn_destroying = pfn_destroying;
	p_obj->pfn_cleanup = pfn_cleanup;
	p_obj->pfn_free = pfn_free;

	/* Set the destroy function pointer based on the destruction type. */
	p_obj->destroy_type = destroy_type;
	p_obj->async_item.pfn_callback = __destroy_cb;

	/* Initialize the spinlock. */
	status = cl_spinlock_init( &p_obj->lock );
	if( status != CL_SUCCESS )
		return status;

	/* Initialize the synchronous cleanup event. */
	status = cl_event_init( &p_obj->event, FALSE );
	if( status != CL_SUCCESS )
		return status;

	p_obj->state = CL_INITIALIZED;

	return CL_SUCCESS;
}



void
cl_obj_destroy(
	IN				cl_obj_t *					p_obj )
{
	cl_state_t		old_state;

	CL_ASSERT( p_obj );

	/* Mark that we're destroying the object. */
	old_state = __obj_set_state( p_obj, CL_DESTROYING );

	/*
	 * Only a single thread can actually destroy the object.  Multiple
	 * threads can initiate destruction as long as the callers can ensure
	 * their object reference is valid.
	 */
	if( old_state == CL_DESTROYING )
		return;

	/* Destroy the object. */
	__destroy_obj( p_obj );
}



void
cl_obj_reset(
	IN				cl_obj_t * const			p_obj )
{
	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->ref_cnt == 0 );
	CL_ASSERT( p_obj->state == CL_DESTROYING );

	p_obj->ref_cnt = 1;
	p_obj->state = CL_INITIALIZED;

	cl_qlist_remove_all( &p_obj->parent_list );
	cl_qlist_remove_all( &p_obj->child_list );
}



static cl_state_t
__obj_set_state(
	IN				cl_obj_t * const			p_obj,
	IN		const	cl_state_t					new_state )
{
	cl_state_t		old_state;

	cl_spinlock_acquire( &p_obj->lock );
	old_state = p_obj->state;
	p_obj->state = new_state;
	cl_spinlock_release( &p_obj->lock );

	return old_state;
}



/*
 * Add a dependent relationship between two objects.
 */
cl_status_t
cl_obj_insert_rel(
	IN				cl_obj_rel_t * const		p_rel,
	IN				cl_obj_t * const			p_parent_obj,
	IN				cl_obj_t * const			p_child_obj )
{
	cl_status_t	status;
	CL_ASSERT( p_rel && p_parent_obj && p_child_obj );

	cl_spinlock_acquire( &p_parent_obj->lock );
	status = cl_obj_insert_rel_parent_locked( p_rel, p_parent_obj, p_child_obj );
	cl_spinlock_release( &p_parent_obj->lock );
	return status;
}



/*
 * Add a dependent relationship between two objects.
 */
cl_status_t
cl_obj_insert_rel_parent_locked(
	IN				cl_obj_rel_t * const		p_rel,
	IN				cl_obj_t * const			p_parent_obj,
	IN				cl_obj_t * const			p_child_obj )
{
	CL_ASSERT( p_rel && p_parent_obj && p_child_obj );

	if(p_parent_obj->state != CL_INITIALIZED)
		return CL_INVALID_STATE;
	/* The child object needs to maintain a reference on the parent. */
	cl_obj_ref( p_parent_obj );
	cl_obj_ref( p_child_obj );

	/* Save the relationship details. */
	p_rel->p_child_obj = p_child_obj;
	p_rel->p_parent_obj = p_parent_obj;

	/*
	 * Track the object - hold both locks to ensure that the relationship is
	 * viewable in the child and parent lists at the same time.
	 */
	cl_spinlock_acquire( &p_child_obj->lock );

	cl_qlist_insert_tail( &p_child_obj->parent_list, &p_rel->list_item );
	cl_qlist_insert_tail( &p_parent_obj->child_list,
		(cl_list_item_t*)&p_rel->pool_item );

	cl_spinlock_release( &p_child_obj->lock );
	return CL_SUCCESS;
}



/*
 * Remove an existing relationship.
 */
void
cl_obj_remove_rel(
	IN				cl_obj_rel_t * const		p_rel )
{
	cl_obj_t		*p_child_obj;
	cl_obj_t		*p_parent_obj;

	CL_ASSERT( p_rel );
	CL_ASSERT( p_rel->p_child_obj && p_rel->p_parent_obj );

	p_child_obj = p_rel->p_child_obj;
	p_parent_obj = p_rel->p_parent_obj;

	/*
	 * Release the objects - hold both locks to ensure that the relationship is
	 * removed from the child and parent lists at the same time.
	 */
	cl_spinlock_acquire( &p_parent_obj->lock );
	cl_spinlock_acquire( &p_child_obj->lock );

	cl_qlist_remove_item( &p_child_obj->parent_list, &p_rel->list_item );
	cl_qlist_remove_item( &p_parent_obj->child_list,
		(cl_list_item_t*)&p_rel->pool_item );

	cl_spinlock_release( &p_child_obj->lock );
	cl_spinlock_release( &p_parent_obj->lock );

	/* Dereference the objects. */
	cl_obj_deref( p_parent_obj );
	cl_obj_deref( p_child_obj );

	p_rel->p_child_obj = NULL;
	p_rel->p_parent_obj = NULL;
}



/*
 * Increment a reference count on an object.
 */
int32_t
cl_obj_ref(
	IN				cl_obj_t * const			p_obj )
{
	CL_ASSERT( p_obj );

	/*
	 * We need to allow referencing the object during destruction in order
	 * to properly synchronize destruction between parent and child objects.
	 */
	CL_ASSERT( p_obj->state == CL_INITIALIZED ||
		p_obj->state == CL_DESTROYING );

	return cl_atomic_inc( &p_obj->ref_cnt );
}



/*
 * Decrement the reference count on an AL object.  Destroy the object if
 * it is no longer referenced.  This object should not be an object's parent.
 */
int32_t
cl_obj_deref(
	IN				cl_obj_t * const			p_obj )
{
	int32_t			ref_cnt;
	cl_destroy_type_t destroy_type;

	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->state == CL_INITIALIZED ||
		p_obj->state == CL_DESTROYING );

	ref_cnt = cl_atomic_dec( &p_obj->ref_cnt );
	/*
	 * Cache the destroy_type because the object could be freed by the time
	 * 'if' below completes.
	 */
	destroy_type = p_obj->destroy_type;

	/* If the reference count went to 0, the object should be destroyed. */
	if( ref_cnt == 0 )
	{
		CL_ASSERT( p_obj->state == CL_DESTROYING );
		if( destroy_type == CL_DESTROY_ASYNC ||
			destroy_type == CL_DESTROY_ASYNC_KEEP_CHILDREN )
		{
			/* Queue the object for asynchronous destruction. */
			CL_ASSERT( gp_obj_mgr );
			cl_async_proc_queue( &gp_obj_mgr->async_proc_mgr,
				&p_obj->async_item );
		}
		else if( destroy_type == CL_DESTROY_SYNC ||
				 destroy_type == CL_DESTROY_SYNC_KEEP_CHILDREN )
		{
			/* Signal an event for synchronous destruction. */
			cl_event_signal( &p_obj->event );
		}
	}

	/* In CL_DESTROY_ASYNC_KEEP_CHILDREN_STAY_IN_PARENT_LIST_TILL_FREE mode, if only
	   references remained are from parents to the object, it is time to destroy the object. */
	if( destroy_type == CL_DESTROY_ASYNC_KEEP_CHILDREN_STAY_IN_PARENT_LIST_TILL_FREE &&
	    ref_cnt == (int32_t) p_obj->parent_list.count )
	{
		CL_ASSERT( p_obj->state == CL_DESTROYING );

		/* Queue the object for asynchronous destruction. */
		CL_ASSERT( gp_obj_mgr );
		cl_async_proc_queue( &gp_obj_mgr->async_proc_mgr,
			&p_obj->async_item );
	}
	
	return ref_cnt;
}



/*
 * Called to cleanup all resources allocated by an object.
 */
void
cl_obj_deinit(
	IN				cl_obj_t * const			p_obj )
{
	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->state == CL_UNINITIALIZED ||
		p_obj->state == CL_DESTROYING );
#if defined( _DEBUG_ )
	{
		cl_list_item_t	*p_list_item;
		cl_obj_rel_t	*p_rel;

		/*
		 * Check that we didn't leave any list items in the parent list
		 * that came from the global pool.  Ignore list items allocated by
		 * the user to simplify their usage model.
		 */
		for( p_list_item = cl_qlist_head( &p_obj->parent_list );
			 p_list_item != cl_qlist_end( &p_obj->parent_list );
			 p_list_item = cl_qlist_next( p_list_item ) )
		{
			p_rel = (cl_obj_rel_t*)PARENT_STRUCT( p_list_item,
				cl_obj_rel_t, list_item );
			CL_ASSERT( p_rel->pool_item.p_pool !=
				&gp_obj_mgr->rel_pool.qcpool );
		}
	}
#endif
	CL_ASSERT( cl_is_qlist_empty( &p_obj->child_list ) );

	/* Remove the object from the global tracking list. */
	__remove_obj( p_obj );

	cl_event_destroy( &p_obj->event );
	cl_spinlock_destroy( &p_obj->lock );

	/* Mark the object as destroyed for debugging purposes. */
	p_obj->state = CL_DESTROYED;
}



/*
 * Remove the given object from its relationships with all its parents.
 * This call requires synchronization to the given object.
 */
static void
__remove_parent_rel(
	IN				cl_obj_t * const			p_obj )
{
	cl_list_item_t		*p_list_item;
	cl_obj_rel_t		*p_rel;

	/* Remove this child object from all its parents. */
	for( p_list_item = cl_qlist_tail( &p_obj->parent_list );
		 p_list_item != cl_qlist_end( &p_obj->parent_list );
		 p_list_item = cl_qlist_prev( p_list_item ) )
	{
		p_rel = (cl_obj_rel_t*)PARENT_STRUCT( p_list_item,
			cl_obj_rel_t, list_item );

		/*
		 * Remove the child from the parent's list, but do not dereference
		 * the parent.  This lets the user access the parent in the callback
		 * routines, but allows destruction to proceed.
		 */
		cl_spinlock_acquire( &p_rel->p_parent_obj->lock );
		cl_qlist_remove_item( &p_rel->p_parent_obj->child_list,
			(cl_list_item_t*)&p_rel->pool_item );

		/*
		 * Remove the relationship's reference to the child.  Use an atomic
		 * decrement rather than cl_obj_deref, since we're already holding the
		 * child object's lock.
		 */
		cl_atomic_dec( &p_obj->ref_cnt );
		CL_ASSERT( p_obj->ref_cnt > 0 || p_obj->destroy_type == CL_DESTROY_ASYNC_KEEP_CHILDREN_STAY_IN_PARENT_LIST_TILL_FREE );

		cl_spinlock_release( &p_rel->p_parent_obj->lock );

		/*
		 * Mark that the child is no longer related to the parent.  We still
		 * hold a reference on the parent object, so we don't clear the parent
		 * pointer until that reference is released.
		 */
		p_rel->p_child_obj = NULL;
	}
}



static void
__destroy_child_obj(
	IN				cl_obj_t *					p_obj )
{
	cl_list_item_t			*p_list_item;
	cl_obj_rel_t			*p_rel;
	cl_obj_t				*p_child_obj;
	cl_state_t				old_state;

	/*	Destroy all child objects. */
	cl_spinlock_acquire( &p_obj->lock );
	for( p_list_item = cl_qlist_tail( &p_obj->child_list );
		 p_list_item != cl_qlist_end( &p_obj->child_list );
		 p_list_item = cl_qlist_tail( &p_obj->child_list ) )
	{
		p_rel = (cl_obj_rel_t*)PARENT_STRUCT( p_list_item,
			cl_obj_rel_t, pool_item );

		/*
		 * Take a reference on the child to protect against another parent
		 * of the object destroying it while we are trying to access it.
		 * If the child object is being destroyed, it will try to remove
		 * this relationship from this parent.
		 */
		p_child_obj = p_rel->p_child_obj;
		cl_obj_ref( p_child_obj );

		/*
		 * We cannot hold the parent lock when acquiring the child's lock, or
		 * a deadlock can occur if the child is in the process of destroying
		 * itself and its parent relationships.
		 */
		cl_spinlock_release( &p_obj->lock );

		/*
		 * Mark that we wish to destroy the object.  If the old state indicates
		 * that we should destroy the object, continue with the destruction.
		 * Note that there is a reference held on the child object from its
		 * creation.  We no longer need the prior reference taken above.
		 */
		old_state = __obj_set_state( p_child_obj, CL_DESTROYING );
		cl_obj_deref( p_child_obj );

		if( old_state != CL_DESTROYING )
			__destroy_obj( p_child_obj );

		/* Continue processing the relationship list. */
		cl_spinlock_acquire( &p_obj->lock );
	}
	cl_spinlock_release( &p_obj->lock );
}



/*
 * Destroys an object.  This call returns TRUE if the destruction process
 * should proceed, or FALSE if destruction is already in progress.
 */
static void
__destroy_obj(
	IN				cl_obj_t					*p_obj )
{
	uint32_t			ref_cnt;
	cl_destroy_type_t	destroy_type;

	CL_ASSERT( p_obj );
	CL_ASSERT( p_obj->state == CL_DESTROYING );

	if( p_obj->destroy_type != CL_DESTROY_ASYNC_KEEP_CHILDREN_STAY_IN_PARENT_LIST_TILL_FREE )
	{
		/* Remove this child object from all its parents. */
		__remove_parent_rel( p_obj );
	}

	/* Notify the user that the object is being destroyed. */
	if( p_obj->pfn_destroying )
		p_obj->pfn_destroying( p_obj );

	if(p_obj->destroy_type != CL_DESTROY_SYNC_KEEP_CHILDREN &&
	   p_obj->destroy_type != CL_DESTROY_ASYNC_KEEP_CHILDREN &&
	   p_obj->destroy_type != CL_DESTROY_ASYNC_KEEP_CHILDREN_STAY_IN_PARENT_LIST_TILL_FREE)
	{
		/*	Destroy all child objects. */
		__destroy_child_obj( p_obj );
	}
	
	/*
	 * Cache the destroy_type because the object could be freed by the time
	 * cl_obj_deref below returns.
	 */
	destroy_type = p_obj->destroy_type;

	CL_ASSERT( p_obj->ref_cnt > 0 );

	/* Dereference this object as it is being destroyed. */
	ref_cnt = cl_obj_deref( p_obj );

	if( destroy_type == CL_DESTROY_SYNC || destroy_type == CL_DESTROY_SYNC_KEEP_CHILDREN )
	{
		if( ref_cnt )
		{
			/* Wait for all other references to go away. */
#if DBG
			/*
			 * In debug builds, we assert every 10 seconds - a synchronous
			 * destruction should not take that long.
			 */
			while( cl_event_wait_on( &p_obj->event, 10000000, FALSE ) ==
				CL_TIMEOUT )
			{
				//CL_ASSERT( !p_obj->ref_cnt );
			}
#else	/* DBG */
			cl_event_wait_on( &p_obj->event, EVENT_NO_TIMEOUT, FALSE );
#endif	/* DBG */
		}
		__destroy_cb( &p_obj->async_item );
	}
}



/*
 * Dereference all parents the object was related to.
 */
static cl_obj_t*
__deref_parents(
	IN				cl_obj_t * const			p_obj )
{
	cl_list_item_t		*p_list_item;
	cl_obj_rel_t		*p_rel;
	cl_obj_t			*p_parent_obj;

	/* Destruction of the object is already serialized - no need to lock. */

	/*
	 * Dereference all parents.  Keep the relationship items in the child's
	 * list, so that they can be returned to the user through the free callback.
	 */
	for( p_list_item = cl_qlist_head( &p_obj->parent_list );
		 p_list_item != cl_qlist_end( &p_obj->parent_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_rel = (cl_obj_rel_t*)PARENT_STRUCT( p_list_item,
			cl_obj_rel_t, list_item );

		p_parent_obj = p_rel->p_parent_obj;
		p_rel->p_parent_obj = NULL;
		CL_ASSERT( !p_rel->p_child_obj );
		if( cl_qlist_next( p_list_item ) ==
			cl_qlist_end( &p_obj->parent_list ) )
		{
			/* Last parent - don't dereference it until after the "free" cb. */
			return p_parent_obj;
		}
		else
		{
			cl_obj_deref( p_parent_obj );
		}
	}
	return NULL;
}



static void
__destroy_cb(
	IN				cl_async_proc_item_t		*p_item )
{
	cl_obj_t				*p_obj, *p_last_parent;

	CL_ASSERT( p_item );

	p_obj = PARENT_STRUCT( p_item, cl_obj_t, async_item );
	CL_ASSERT( p_obj->state == CL_DESTROYING );

	/* Cleanup any hardware related resources. */
	if( p_obj->pfn_cleanup )
		p_obj->pfn_cleanup( p_obj );

	if( p_obj->destroy_type == CL_DESTROY_ASYNC_KEEP_CHILDREN_STAY_IN_PARENT_LIST_TILL_FREE )
	{
		/* Remove this child object from all its parents. */
		__remove_parent_rel( p_obj );
	}

	CL_ASSERT( !p_obj->ref_cnt );

	/* We can now safely dereference all but the last parent. */
	p_last_parent = __deref_parents( p_obj );

	/* Free the resources associated with the object. */
	CL_ASSERT( p_obj->pfn_free );
	p_obj->pfn_free( p_obj );

	if( p_last_parent )
		cl_obj_deref( p_last_parent );
}
