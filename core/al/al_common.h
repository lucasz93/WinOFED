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

#if !defined(__AL_COMMON_H__)
#define __AL_COMMON_H__

#include <iba/ib_al.h>

#include <complib/cl_atomic.h>
#include <complib/cl_async_proc.h>
#include <complib/cl_event.h>
#include <complib/cl_qlist.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_obj.h>
#include <complib/cl_vector.h>

#if DBG
#include "al_ref_trace.h"
#endif


/* Driver parameters */
extern uint32_t		g_smi_poll_interval;
extern uint32_t		g_ioc_query_timeout;
extern uint32_t		g_ioc_query_retries;
extern uint32_t		g_ioc_poll_interval;


/* Wait operations performed in user-mode must be alertable. */
#ifdef CL_KERNEL
#define AL_WAIT_ALERTABLE	FALSE
#else	/* CL_KERNEL */
#define AL_WAIT_ALERTABLE	TRUE
#endif	/* CL_KERNEL */

/*
 * Controls whether the al_objects use their own private
 * thread pool for destruction.
 */
#define AL_OBJ_PRIVATE_ASYNC_PROC	1

#if AL_OBJ_PRIVATE_ASYNC_PROC
extern cl_async_proc_t		*gp_async_obj_mgr;
#endif


/*
 * Macro to verify a AL object handle.  We ignore the upper byte of the type
 * when making the type comparison.  The upper byte specifies a subtype.
 */
#define AL_BASE_TYPE( t )		( (t) & 0x00FFFFFF )
#define AL_SUBTYPE( t )			( (t) & 0xFF000000 )

#define AL_OBJ_BASE_TYPE( h )	( AL_BASE_TYPE( (h)->obj.type ) )
#define AL_OBJ_SUBTYPE( h )		( AL_SUBTYPE( (h)->obj.type ) )

#define AL_OBJ_IS_TYPE( h, t ) \
	( AL_OBJ_BASE_TYPE( h ) == AL_BASE_TYPE( t ) )

#define AL_OBJ_IS_SUBTYPE( h, t ) \
	( AL_OBJ_SUBTYPE( h ) == AL_SUBTYPE( t ) )

#define AL_OBJ_INVALID_HANDLE( h, t )	\
	( !(h) || !AL_OBJ_IS_TYPE( h, t ) || ((h)->obj.state != CL_INITIALIZED) )


#if DBG

// Has to be a degree of 2
#define POOL_TRACE_ENTRIES			0x20000

typedef enum { POOL_GET, POOL_PUT } change_type_t;

typedef struct _trace_node
{
	void* 				p_pool_item;
	change_type_t		change;
	uint16_t 			trace_ctx;
#ifdef CL_KERNEL
	PETHREAD 			p_thread;
#endif
} trace_node_t;


typedef struct _trace_db
{
	trace_node_t 		trace[POOL_TRACE_ENTRIES];
	LONG 				trace_index;
} trace_db_t;


void
insert_pool_trace(
	IN				trace_db_t*			trace_db,
	IN				void*				p_pool_item,
	IN				change_type_t		change_type,
	IN				uint16_t			ctx);


extern trace_db_t		g_av_trace;
extern trace_db_t		g_mad_trace;

#endif



typedef struct _al_obj __p_al_obj_t;


/* Function used to release AL items created by the object. */
typedef void
(*al_pfn_destroying_t)(
	IN				struct _al_obj				*p_obj );


/* Function used to cleanup any HW resources used by the object. */
typedef void
(*al_pfn_cleanup_t)(
	IN				struct _al_obj				*p_obj );


/* Function to all resources used by the object. */
typedef void
(*al_pfn_free_t)(
	IN				struct _al_obj				*p_obj );


/* Function invoked to release HW resources. */
typedef void
(*al_pfn_destroy_t)(
	IN				struct _al_obj				*p_obj,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb );



/*
 * Different types of AL object's.  Note that the upper byte signifies
 * a subtype.
 */
#define AL_OBJ_TYPE_UNKNOWN		0
#define AL_OBJ_TYPE_H_AL			1
#define AL_OBJ_TYPE_H_QP			2
#define AL_OBJ_TYPE_H_AV			3
#define AL_OBJ_TYPE_H_MR			4
#define AL_OBJ_TYPE_H_MW			5
#define AL_OBJ_TYPE_H_PD			6
#define AL_OBJ_TYPE_H_CA			7
#define AL_OBJ_TYPE_H_CQ			8
#define AL_OBJ_TYPE_H_CONN			9
#define AL_OBJ_TYPE_H_LISTEN		10
#define AL_OBJ_TYPE_H_IOC			11
#define AL_OBJ_TYPE_H_SVC_ENTRY	12
#define AL_OBJ_TYPE_H_PNP			13
#define AL_OBJ_TYPE_H_SA_REQ		14
#define AL_OBJ_TYPE_H_MCAST		15
#define AL_OBJ_TYPE_H_ATTACH		16
#define AL_OBJ_TYPE_H_MAD			17
#define AL_OBJ_TYPE_H_MAD_POOL	18
#define AL_OBJ_TYPE_H_POOL_KEY	19
#define AL_OBJ_TYPE_H_MAD_SVC		20
#define AL_OBJ_TYPE_CI_CA			21
#define AL_OBJ_TYPE_CM				22
#define AL_OBJ_TYPE_SMI				23
#define AL_OBJ_TYPE_DM				24
#define AL_OBJ_TYPE_IOU				25
#define AL_OBJ_TYPE_LOADER			26
#define AL_OBJ_TYPE_MAD_POOL		27
#define AL_OBJ_TYPE_MAD_DISP		28
#define AL_OBJ_TYPE_AL_MGR			29
#define AL_OBJ_TYPE_PNP_MGR		30
#define AL_OBJ_TYPE_IOC_PNP_MGR	31
#define AL_OBJ_TYPE_IOC_PNP_SVC	32
#define AL_OBJ_TYPE_QUERY_SVC		33
#define AL_OBJ_TYPE_MCAST_SVC		34
#define AL_OBJ_TYPE_SA_REQ_SVC	35
#define AL_OBJ_TYPE_RES_MGR		36
#define AL_OBJ_TYPE_H_CA_ATTR		37
#define AL_OBJ_TYPE_H_PNP_EVENT	38
#define AL_OBJ_TYPE_H_SA_REG		39
#define AL_OBJ_TYPE_H_FMR			40
#define AL_OBJ_TYPE_H_SRQ			41
#define AL_OBJ_TYPE_H_FMR_POOL		42
#define AL_OBJ_TYPE_NDI				43
#define AL_OBJ_TYPE_INVALID		44	/* Must be last type. */

/* Kernel object for a user-mode app. */
#define AL_OBJ_SUBTYPE_UM_EXPORT	0x80000000

/* CM related subtypes, used by the CM proxy. */
#define AL_OBJ_SUBTYPE_REQ			0x01000000
#define AL_OBJ_SUBTYPE_REP			0x02000000
#define AL_OBJ_SUBTYPE_DREQ			0x04000000
#define AL_OBJ_SUBTYPE_LAP			0x08000000

typedef uint32_t	al_obj_type_t;


#ifdef _WIN64
#define AL_DEFAULT_TIMEOUT_MS	10000		/* 10 seconds */
#else
#define AL_DEFAULT_TIMEOUT_MS	20000		/* 20 seconds */
#endif
#define AL_DEFAULT_TIMEOUT_US	(AL_DEFAULT_TIMEOUT_MS * 1000)
#define AL_TIMEOUT_PER_DESC_US	10000
#define AL_MAX_TIMEOUT_MS		(AL_DEFAULT_TIMEOUT_MS * 10)
#define AL_MAX_TIMEOUT_US		(AL_MAX_TIMEOUT_MS * 1000)


#if defined( _DEBUG_ )
const char* ib_obj_type_str[];
#endif


/*
 * Base object for AL resources.  This must be the first element of
 * AL resources.
 */
typedef struct _al_obj
{
	cl_pool_item_t				pool_item;

	struct _al_obj				*p_parent_obj;
	struct _al_ci_ca			*p_ci_ca;

	const void					*context;

	/* Asynchronous item used when destroying the object asynchronously. */
	cl_async_proc_item_t		async_item;

	/* Event used when destroying the object synchronously. */
	cl_event_t					event;
	uint32_t					timeout_ms;
	uint32_t					desc_cnt;

	al_pfn_destroy_t			pfn_destroy;
	al_pfn_destroying_t			pfn_destroying;
	al_pfn_cleanup_t			pfn_cleanup;
	al_pfn_free_t				pfn_free;
	ib_pfn_destroy_cb_t			user_destroy_cb;

	cl_spinlock_t				lock;
	cl_qlist_t					obj_list;
	atomic32_t					ref_cnt;
#if defined(CL_KERNEL) && DBG
	obj_ref_trace_t				ref_trace;
#endif
	cl_list_item_t				list_item;
	al_obj_type_t				type;
	cl_state_t					state;

	uint64_t					hdl;	/* User Handle. */
	ib_al_handle_t				h_al;	/* Owning AL instance. */

#ifdef CL_KERNEL
	/*
	 * Flag to indicate that UM calls may proceed on the given object.
	 * Set by the proxy when creation completes successfully.
	 */
	boolean_t					hdl_valid;
	wchar_t						*p_caller_id;	// object owner's name or location
#endif
}	al_obj_t;


void
construct_al_obj(
	IN				al_obj_t * const			p_obj,
	IN		const	al_obj_type_t				obj_type );


ib_api_status_t
init_al_obj(
	IN				al_obj_t * const			p_obj,
	IN		const	void* const					context,
	IN				boolean_t					async_destroy,
	IN		const	al_pfn_destroying_t			pfn_destroying,
	IN		const	al_pfn_cleanup_t			pfn_cleanup,
	IN		const	al_pfn_free_t				pfn_free );

/*
 * Reset an object's state.  This is called after pfn_destroy() has
 * been called on a object, but before destroy_al_obj() has been invoked.
 * It allows an object to be initialized once, then returned to a pool
 * on destruction, and later reused after being removed from the pool.
 */
void
reset_al_obj(
	IN				al_obj_t * const			p_obj );

void
set_al_obj_timeout(
	IN				al_obj_t * const			p_obj,
	IN		const	uint32_t					timeout_ms );

void
inc_al_obj_desc(
	IN				al_obj_t * const			p_obj,
	IN		const	uint32_t					desc_cnt );


/*
 * Attach to our parent object.  The parent will destroy the child when
 * it is destroyed.  Attaching a child to the parent automatically
 * increments the parent's reference count.
 */
ib_api_status_t
attach_al_obj(
	IN				al_obj_t * const			p_parent_obj,
	IN				al_obj_t * const			p_child_obj );


/*
 * Increment the reference count on an AL object.
 */
int32_t
ref_al_obj_inner(
	IN				al_obj_t * const			p_obj );

#if DBG
#define ref_ctx_al_obj(_p_obj_, ref_inx) 				\
	ref_trace_insert(__FILE__, __LINE__,_p_obj_, AL_REF, ref_inx)
#define ref_al_obj(_p_obj_) 	ref_ctx_al_obj(_p_obj_, 0)
#else
#define ref_al_obj(_p_obj_) 				ref_al_obj_inner(_p_obj_)
#define ref_ctx_al_obj(_p_obj_, ref_inx)    ref_al_obj_inner(_p_obj_)
#endif

/*
 * Called to release a child object from its parent.  The child's
 * reference to its parent is still held.
 */
void
detach_al_obj(
	IN				al_obj_t * const			p_obj );

/*
 * Decrement the reference count on an AL object.
 */
AL_EXPORT int32_t AL_API
deref_al_obj_inner(
	IN				al_obj_t * const			p_obj );

#if DBG
#define deref_ctx_al_obj(_p_obj_, ref_inx)						\
	ref_trace_insert(__FILE__, __LINE__,_p_obj_, AL_DEREF, ref_inx) 
	
#define deref_al_obj(_p_obj_)	deref_ctx_al_obj(_p_obj_, 0) 
#else
#define deref_al_obj(_p_obj_) 				deref_al_obj_inner(_p_obj_)
#define deref_ctx_al_obj(_p_obj_, ref_inx) 	deref_al_obj_inner(_p_obj_)
#endif

AL_EXPORT int32_t AL_API
deref_al_obj_cb(
	IN				al_obj_t * const			p_obj );


/*
 * Called to cleanup all resources allocated by an object.
 */
void
destroy_al_obj(
	IN				al_obj_t * const			p_obj );




extern const char* ib_obj_type_str[];

static inline const char*
ib_get_obj_type(
	IN				al_obj_t * const			p_obj )
{
	if( AL_BASE_TYPE( p_obj->type ) >= AL_OBJ_TYPE_INVALID )
		return( ib_obj_type_str[AL_OBJ_TYPE_UNKNOWN] );

	return( ib_obj_type_str[ AL_BASE_TYPE( p_obj->type ) ] );
}




#endif /* __AL_COMMON_H__ */
