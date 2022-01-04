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

#pragma warning(disable:4206) //nonstandard extension used : translation unit is empty

#if DBG
#include "al_ref_trace.h"
#include "al_common.h"
#include "al_debug.h"

#ifdef CL_KERNEL
#include <wdm.h>

typedef enum {
	AL_DATA_NODE,
	AL_DIRTY_CHAIN_NODE 
} al_node_type_t;


#define REF_TRACE_MAX_ENTRIES			0x2000
#define REF_TRACE_DB_ENTRIES			0xF0000

typedef struct _ref_cnt_data
{
	char*					file_name;
	LONG					prev_ref_val;
	uint32_t				line_num;
	uint8_t					purge_type;
	uint16_t				repeat_cnt;
	uint8_t					ref_change; 	//al_ref_change_type_t
	uint8_t					ref_ctx; 		//enum e_ref_type

} ref_cnt_data_t;

typedef struct _ref_cnt_dirty_list
{
	void*					p_al_obj;
	uint32_t				type;
	long					obj_list_size;
	LIST_ENTRY				dirty_list;
} ref_cnt_dirty_list_t;


typedef union _ref_db_entry
{
	ref_cnt_data_t 			ref_cnt_data;
	ref_cnt_dirty_list_t	ref_dirty_list;

} ref_db_entry_t;

typedef struct _ref_db_node
{
	uint8_t					type;		//al_node_type_t
	ref_db_entry_t 			entry;
	LIST_ENTRY				list_entry;
} ref_db_node_t;

typedef struct _ref_cnt_dbg_db
{
	LIST_ENTRY 					free_chain;
	cl_spinlock_t				free_chain_lock;
	
	LIST_ENTRY 					dirty_chain;
	cl_spinlock_t				dirty_chain_lock;

	ULONG						policy;
	LONG 						max_chain_size;
	uint32_t					max_chain_obj_type;
	int32_t						needed_size;
	volatile long				objects_num;
	ref_db_node_t				ref_cnt_db[REF_TRACE_DB_ENTRIES];
} ref_cnt_dbg_db_t;

ref_cnt_dbg_db_t g_ref_cnt_dbg_db;

#define IS_IN_DB(address) \
    (((ULONG_PTR)address >= (ULONG_PTR)(g_ref_cnt_dbg_db.ref_cnt_db)) && \
     ((ULONG_PTR)address < (ULONG_PTR)(g_ref_cnt_dbg_db.ref_cnt_db)+REF_TRACE_DB_ENTRIES*sizeof(ref_db_node_t)))


#endif  // CL_KERNEL

void
ref_trace_db_init(ULONG policy)
{	
#ifdef CL_KERNEL
	int i = 0;

	cl_memclr(&g_ref_cnt_dbg_db, sizeof(g_ref_cnt_dbg_db));
	
	InitializeListHead(&g_ref_cnt_dbg_db.free_chain);
	InitializeListHead(&g_ref_cnt_dbg_db.dirty_chain);

	for ( i = 0; i < REF_TRACE_DB_ENTRIES; i++)
	{
		InsertTailList(&g_ref_cnt_dbg_db.free_chain, &g_ref_cnt_dbg_db.ref_cnt_db[i].list_entry);
	}

	cl_spinlock_construct(&g_ref_cnt_dbg_db.free_chain_lock);
	cl_spinlock_init( &g_ref_cnt_dbg_db.free_chain_lock );
	
	cl_spinlock_construct(&g_ref_cnt_dbg_db.dirty_chain_lock);
	cl_spinlock_init( &g_ref_cnt_dbg_db.dirty_chain_lock );

	g_ref_cnt_dbg_db.policy = policy;
	g_ref_cnt_dbg_db.needed_size = REF_TRACE_DB_ENTRIES;
#else   // CL_KERNEL
    UNREFERENCED_PARAMETER(policy);
#endif  // CL_KERNEL
}

void
ref_trace_init(
	IN				void * const			p_obj)
{
#ifdef CL_KERNEL
	al_obj_t* p_al_obj = (al_obj_t*)p_obj;
		
	InitializeListHead(&p_al_obj->ref_trace.ref_chain);

	cl_spinlock_construct(&p_al_obj->ref_trace.lock);
	cl_spinlock_init( &p_al_obj->ref_trace.lock );

	p_al_obj->ref_trace.list_size = 0;
#else   // CL_KERNEL
    UNREFERENCED_PARAMETER(p_obj);
#endif  // CL_KERNEL
}

// Inserts the dest_list into the beginning of the src_list
// Should be called with a lock on both lists
void 
ref_trace_insert_head_list(
	PLIST_ENTRY src_list,
	PLIST_ENTRY dest_list)
{
#ifdef CL_KERNEL
	PLIST_ENTRY  src_first = src_list->Flink;
	PLIST_ENTRY  src_last = src_list->Blink;
	PLIST_ENTRY  dest_first = dest_list->Flink;

	src_first->Blink = dest_list;
	dest_list->Flink = src_first;
	
	src_last->Flink = dest_first;
	dest_first->Blink = src_last;
#else   // CL_KERNEL
    UNREFERENCED_PARAMETER(src_list);
    UNREFERENCED_PARAMETER(dest_list);
#endif  // CL_KERNEL
}


#ifdef CL_KERNEL
static PLIST_ENTRY
ref_trace_alloc_entry()
{
	PLIST_ENTRY 			free_entry;
	
	// Allocate new free entry
	cl_spinlock_acquire( &g_ref_cnt_dbg_db.free_chain_lock );
	free_entry = RemoveHeadList(&g_ref_cnt_dbg_db.free_chain);
	if (free_entry == &g_ref_cnt_dbg_db.free_chain)
	{
		ref_cnt_dirty_list_t* 	ref_dirty;
		
		// Free one dirty entry
		cl_spinlock_acquire( &g_ref_cnt_dbg_db.dirty_chain_lock );
		free_entry = RemoveHeadList(&g_ref_cnt_dbg_db.dirty_chain);

		if (free_entry == &g_ref_cnt_dbg_db.dirty_chain)
		{
			if (g_ref_cnt_dbg_db.needed_size == REF_TRACE_DB_ENTRIES)
			{
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("All %d entries of the ref trace memory are used !!!\n", REF_TRACE_DB_ENTRIES) );
			}
			g_ref_cnt_dbg_db.needed_size++;
			free_entry = NULL;
		}
		else 
		{
			ref_db_node_t* ref_node = CONTAINING_RECORD(free_entry, ref_db_node_t, list_entry);
			ref_dirty = &ref_node->entry.ref_dirty_list;
			
			ref_trace_insert_head_list(&ref_dirty->dirty_list, &g_ref_cnt_dbg_db.free_chain);
		}
		cl_spinlock_release( &g_ref_cnt_dbg_db.dirty_chain_lock );
	}
		
	cl_spinlock_release( &g_ref_cnt_dbg_db.free_chain_lock );

	CL_ASSERT((IS_IN_DB(free_entry) || (free_entry == NULL)));

	return free_entry;
}


static PLIST_ENTRY
ref_trace_alloc_dirty_list(
	IN				al_obj_t * const		p_obj,
	OUT 			ref_db_node_t** 		ref_node)
{
	PLIST_ENTRY 			free_entry;
	ref_cnt_dirty_list_t* 	ref_entry;

	free_entry = ref_trace_alloc_entry();
	if (!free_entry)
		return NULL;

	*ref_node = CONTAINING_RECORD(free_entry, ref_db_node_t, list_entry);
	(*ref_node)->type = AL_DIRTY_CHAIN_NODE;
	ref_entry = &(*ref_node)->entry.ref_dirty_list;

	CL_ASSERT(IS_IN_DB(ref_entry));

	ref_entry->p_al_obj = p_obj;
	ref_entry->type = p_obj->type;
	ref_entry->obj_list_size = p_obj->ref_trace.list_size;

	InitializeListHead(&ref_entry->dirty_list);
	return &ref_entry->dirty_list;
}


static boolean_t
ref_trace_is_logged_obj(
	IN				al_obj_t * const			p_obj )
{
	return (p_obj->type == AL_OBJ_TYPE_H_AL);
}
#endif  // CL_KERNEL

// Destroys the object's ref trace chain
void
ref_trace_destroy(
	IN				void * const			p_obj )
{
#ifdef CL_KERNEL
	al_obj_t* p_al_obj = (al_obj_t*)p_obj;
	
	cl_spinlock_acquire(&p_al_obj->ref_trace.lock);
	
	if (!IsListEmpty(&p_al_obj->ref_trace.ref_chain)) 
	{
		if (ref_trace_is_logged_obj(p_al_obj))
		{
			//Create new dirty list for the released elements
			ref_db_node_t* ref_node = NULL;
			PLIST_ENTRY dest_list = ref_trace_alloc_dirty_list(p_al_obj, &ref_node);
			if (dest_list)
			{
				cl_spinlock_acquire( &g_ref_cnt_dbg_db.dirty_chain_lock );
				ref_trace_insert_head_list(&p_al_obj->ref_trace.ref_chain, dest_list);
				InsertTailList(&g_ref_cnt_dbg_db.dirty_chain, &ref_node->list_entry);
				cl_spinlock_release( &g_ref_cnt_dbg_db.dirty_chain_lock );
			}
			else
			{
				cl_spinlock_acquire( &g_ref_cnt_dbg_db.free_chain_lock );
				ref_trace_insert_head_list(&p_al_obj->ref_trace.ref_chain, &g_ref_cnt_dbg_db.free_chain);
				cl_spinlock_release( &g_ref_cnt_dbg_db.free_chain_lock );
			}
		}
		else
		{
			cl_spinlock_acquire( &g_ref_cnt_dbg_db.free_chain_lock );
			ref_trace_insert_head_list(&p_al_obj->ref_trace.ref_chain, &g_ref_cnt_dbg_db.free_chain);
			cl_spinlock_release( &g_ref_cnt_dbg_db.free_chain_lock );
		}
		
		InitializeListHead(&p_al_obj->ref_trace.ref_chain);
	}
	
	cl_spinlock_release(&p_al_obj->ref_trace.lock);
	
	cl_spinlock_destroy(&p_al_obj->ref_trace.lock);

	InterlockedDecrement(&g_ref_cnt_dbg_db.objects_num);
#else   // CL_KERNEL
    UNREFERENCED_PARAMETER(p_obj);
#endif  // CL_KERNEL
}


#ifdef CL_KERNEL
static ref_cnt_data_t*
ref_trace_get_trace_data(
	IN PLIST_ENTRY 			list_entry)
{
	ref_db_node_t* 		ref_node;

	ref_node = CONTAINING_RECORD(list_entry, ref_db_node_t, list_entry);

	CL_ASSERT(IS_IN_DB(ref_node));
	
	ref_node->type = AL_DATA_NODE;
	return &ref_node->entry.ref_cnt_data;
}


static boolean_t
ref_trace_is_equal_entry(
	IN PLIST_ENTRY 			first_entry,
	IN PLIST_ENTRY 			second_entry )
{
	ref_cnt_data_t* 	first_ref_data = ref_trace_get_trace_data(first_entry);
	ref_cnt_data_t* 	second_ref_data = ref_trace_get_trace_data(second_entry);

	return (first_ref_data->ref_change == second_ref_data->ref_change &&
			first_ref_data->line_num == second_ref_data->line_num &&
			first_ref_data->prev_ref_val == second_ref_data->prev_ref_val &&
			first_ref_data->file_name == second_ref_data->file_name);
}


static boolean_t
ref_trace_is_same_action_entry(
	IN PLIST_ENTRY 			first_entry,
	IN PLIST_ENTRY 			second_entry )
{
	ref_cnt_data_t* 	first_ref_data = ref_trace_get_trace_data(first_entry);
	ref_cnt_data_t* 	second_ref_data = ref_trace_get_trace_data(second_entry);

	return (first_ref_data->ref_change == second_ref_data->ref_change &&
			first_ref_data->line_num == second_ref_data->line_num &&
			first_ref_data->file_name == second_ref_data->file_name);
}


// Should be called with a lock on the purged list
static boolean_t
ref_trace_purge_ref_deref_pairs(
	IN PLIST_ENTRY 			last_deref_entry,
	IN PLIST_ENTRY 			list_head)
{
	PLIST_ENTRY 		last_ref_entry = last_deref_entry->Blink;
	PLIST_ENTRY 		prev_deref_entry = last_ref_entry->Blink;
	PLIST_ENTRY 		prev_ref_entry = prev_deref_entry->Blink;

	if ( last_ref_entry == list_head   || 
		 prev_deref_entry == list_head || 
		 prev_ref_entry == list_head )
		return FALSE;

	
	if (ref_trace_is_equal_entry(last_deref_entry, prev_deref_entry) &&
		ref_trace_is_equal_entry(last_ref_entry, prev_ref_entry))
	{
		ref_cnt_data_t* prev_ref_data = ref_trace_get_trace_data(prev_ref_entry);
		ref_cnt_data_t* prev_deref_data = ref_trace_get_trace_data(prev_deref_entry);
		if ( (prev_ref_data->repeat_cnt >= 0xFFFF) ||
			 ((prev_ref_data->purge_type != EQUAL_REF_DEREF_PAIRS) &&
			  (prev_ref_data->purge_type != 0)) )
			return FALSE;

		prev_ref_data->repeat_cnt++;
		prev_ref_data->purge_type = EQUAL_REF_DEREF_PAIRS;
		prev_deref_data->repeat_cnt++;
		prev_deref_data->purge_type = EQUAL_REF_DEREF_PAIRS;
		
		prev_deref_entry->Flink = last_deref_entry->Flink;
		last_deref_entry->Flink->Blink = prev_deref_entry;

		cl_spinlock_acquire( &g_ref_cnt_dbg_db.free_chain_lock );
		InsertTailList(&g_ref_cnt_dbg_db.free_chain, last_ref_entry);
		InsertTailList(&g_ref_cnt_dbg_db.free_chain, last_deref_entry);
		cl_spinlock_release( &g_ref_cnt_dbg_db.free_chain_lock );

		return TRUE;
	}

	return FALSE;
}


// Should be called with a lock on the purged list
static boolean_t
ref_trace_purge_same_entry(
	IN PLIST_ENTRY 			last_entry,
	IN PLIST_ENTRY 			list_head)
{
	PLIST_ENTRY 		prev_entry = last_entry->Blink;

	if ( prev_entry == list_head )
		return FALSE;
	
	if (ref_trace_is_same_action_entry(last_entry, prev_entry))
	{
		ref_cnt_data_t* prev_ref_data = ref_trace_get_trace_data(prev_entry);
		if ( (prev_ref_data->repeat_cnt >= 0xFFFF) || 
			 ((prev_ref_data->purge_type != SAME_REF_OR_DEREF_ENTRY) &&
			  (prev_ref_data->purge_type != 0)) )
			return FALSE;

		prev_ref_data->repeat_cnt++;
		prev_ref_data->purge_type = SAME_REF_OR_DEREF_ENTRY;
		
		RemoveEntryList(last_entry);

		cl_spinlock_acquire( &g_ref_cnt_dbg_db.free_chain_lock );
		InsertTailList(&g_ref_cnt_dbg_db.free_chain, last_entry);
		cl_spinlock_release( &g_ref_cnt_dbg_db.free_chain_lock );

		return TRUE;
	}

	return FALSE;
}


static void
ref_trace_purge(
	IN PLIST_ENTRY 			last_entry,
	IN al_obj_t *			p_obj)
{
	cl_spinlock_acquire(&p_obj->ref_trace.lock);

	if (g_ref_cnt_dbg_db.policy & EQUAL_REF_DEREF_PAIRS)
	{
		if (ref_trace_purge_ref_deref_pairs(last_entry, &p_obj->ref_trace.ref_chain))
		{
			p_obj->ref_trace.list_size -= 2;
			cl_spinlock_release(&p_obj->ref_trace.lock);
			return;
		}
	}

	if (g_ref_cnt_dbg_db.policy & SAME_REF_OR_DEREF_ENTRY)
	{
		if (ref_trace_purge_same_entry(last_entry, &p_obj->ref_trace.ref_chain))
			p_obj->ref_trace.list_size--;
	}
		
	cl_spinlock_release(&p_obj->ref_trace.lock);
}
#endif  // CL_KERNEL


int32_t
ref_trace_insert(
    IN char* 				 file,
    IN LONG 				 line,
    IN void * const			 p_obj,
    IN al_ref_change_type_t	 change_type,
    IN uint8_t 			 	 ref_ctx
    )
 {
#ifdef CL_KERNEL
	al_obj_t* p_al_obj = (al_obj_t*)p_obj; 
	PLIST_ENTRY 		free_entry;
	ref_cnt_data_t* 	ref_entry;

	cl_spinlock_acquire(&p_al_obj->ref_trace.lock);

	if (p_al_obj->ref_trace.list_size >= REF_TRACE_MAX_ENTRIES)
	{
		// Reuse the first entry instead of allocating a new one 
		
		free_entry = RemoveHeadList(&p_al_obj->ref_trace.ref_chain);
		cl_spinlock_release(&p_al_obj->ref_trace.lock);
	}
	else
	{
		cl_spinlock_release(&p_al_obj->ref_trace.lock);
		free_entry = ref_trace_alloc_entry();
	}

	if (free_entry)
	{
		ref_entry = ref_trace_get_trace_data(free_entry);

		ref_entry->ref_change = change_type;
		ref_entry->line_num = line;
		ref_entry->prev_ref_val = p_al_obj->ref_cnt;
		ref_entry->file_name = file;
		ref_entry->ref_ctx = ref_ctx;
		ref_entry->repeat_cnt = 1;
		ref_entry->purge_type = 0;

		cl_spinlock_acquire(&p_al_obj->ref_trace.lock);
		InsertTailList(&p_al_obj->ref_trace.ref_chain, free_entry);
		if (p_al_obj->ref_trace.list_size < REF_TRACE_MAX_ENTRIES)
		{
			if (p_al_obj->ref_trace.list_size == 0)
			{
				InterlockedIncrement(&g_ref_cnt_dbg_db.objects_num);
			}
			
			//If we allocated a new node and not reused the first from the list
			p_al_obj->ref_trace.list_size++;
		}
		cl_spinlock_release(&p_al_obj->ref_trace.lock);

		ref_trace_purge(free_entry, p_al_obj);

		if (p_al_obj->ref_trace.list_size >= g_ref_cnt_dbg_db.max_chain_size)
		{
			g_ref_cnt_dbg_db.max_chain_size = p_al_obj->ref_trace.list_size;
			g_ref_cnt_dbg_db.max_chain_obj_type = p_al_obj->type;
		}

	}

	return ((change_type == AL_REF) ? ref_al_obj_inner(p_al_obj) : deref_al_obj_inner(p_al_obj));
#else   // CL_KERNEL
    UNREFERENCED_PARAMETER(file);
    UNREFERENCED_PARAMETER(line);
    UNREFERENCED_PARAMETER(p_obj);
    UNREFERENCED_PARAMETER(change_type);
    UNREFERENCED_PARAMETER(ref_ctx);
    return 0;
#endif  // CL_KERNEL
}

void 
ref_trace_print(
	IN void * const 	 p_obj
)
{
#ifdef CL_KERNEL
	al_obj_t* p_al_obj = (al_obj_t*)p_obj;
	ref_cnt_data_t* 	ref_entry;
	PLIST_ENTRY 		p_entry;
	
	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("******** Ref count dbg entries:*********\n") );

	cl_spinlock_acquire(&p_al_obj->ref_trace.lock);

	p_entry = p_al_obj->ref_trace.ref_chain.Flink;
	while (p_entry != &p_al_obj->ref_trace.ref_chain)
	{
		ref_entry = ref_trace_get_trace_data(p_entry);
		
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("change: %d prev_ref: %d file: %s line: %d ctx: %d repeatitions: %d purge type: %d\n", 
			ref_entry->ref_change,
			ref_entry->prev_ref_val,
			ref_entry->file_name,
			ref_entry->line_num,
			ref_entry->ref_ctx,
			ref_entry->repeat_cnt,
			ref_entry->purge_type) );

		p_entry = p_entry->Flink;
	}
	
	cl_spinlock_release(&p_al_obj->ref_trace.lock);
#else   // CL_KERNEL
    UNREFERENCED_PARAMETER(p_obj);
#endif  // CL_KERNEL
}

void 
ref_trace_dirty_print(
	IN void * const 	 p_obj
)
{
#ifdef CL_KERNEL
	al_obj_t* p_al_obj = (al_obj_t*)p_obj;
	ref_db_node_t* 			ref_node;
	ref_cnt_dirty_list_t* 	ref_dirty;
	ref_cnt_data_t* 		ref_entry;
	PLIST_ENTRY 			p_chain_entry;
	
	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("******** Ref count dirty dbg entries:*********\n") );

	cl_spinlock_acquire( &g_ref_cnt_dbg_db.dirty_chain_lock );

	p_chain_entry = g_ref_cnt_dbg_db.dirty_chain.Flink;
	while (p_chain_entry != &g_ref_cnt_dbg_db.dirty_chain)
	{
		ref_node = CONTAINING_RECORD(p_chain_entry, ref_db_node_t, list_entry);
		ref_dirty = &ref_node->entry.ref_dirty_list;
		if (ref_dirty->p_al_obj == p_al_obj)
		{
			LIST_ENTRY* p_entry = ref_dirty->dirty_list.Flink;
			while (p_entry != &ref_dirty->dirty_list)
			{
				ref_entry = ref_trace_get_trace_data(p_entry);
				
				AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
					("change: %d prev_ref: %d file: %s line: %d ctx: %d repeatitions: %d purge type: %d\n", 
					ref_entry->ref_change,
					ref_entry->prev_ref_val,
					ref_entry->file_name,
					ref_entry->line_num,
					ref_entry->ref_ctx,
					ref_entry->repeat_cnt,
					ref_entry->purge_type) );

				p_entry = p_entry->Flink;
			}

			break;
		}
		
		p_chain_entry = p_chain_entry->Flink;
	}
	
	cl_spinlock_release( &g_ref_cnt_dbg_db.dirty_chain_lock );
#else   // CL_KERNEL
    UNREFERENCED_PARAMETER(p_obj);
#endif  // CL_KERNEL
}

#endif
