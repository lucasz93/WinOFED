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



/*
 * Abstract:
 *	Implementation of memory allocation tracking functions.
 *
 * Environment:
 *	All
 */


#include "cl_memtrack.h"
#include "kernel\complib\cl_init.h"


cl_mem_tracker_t		*gp_mem_tracker = NULL;


cl_status_t _cl_init()
{
    cl_state_t status;

    status = __cl_mem_track(TRUE);
    if(status != CL_SUCCESS)
    {
        return status;
    }

    status = cl_obj_mgr_create();
    if(status != CL_SUCCESS)
    {
        __cl_mem_track(FALSE);
    }

    return status;
}

void _cl_deinit()
{
    cl_obj_mgr_destroy();
    __cl_mem_track(FALSE);
}


/*
 * Allocates memory.
 */
void*
__cl_malloc_priv(
	IN	const size_t	size,
	IN	const boolean_t	pageable );


/*
 * Deallocates memory.
 */
void
__cl_free_priv(
	IN	void* const	p_memory );


/*
 * Allocate and initialize the memory tracker object.
 */
static inline cl_status_t
__cl_mem_track_start( void )
{
	cl_status_t			status;

	if( gp_mem_tracker )
		return CL_SUCCESS;

	/* Allocate the memory tracker object. */
	gp_mem_tracker = (cl_mem_tracker_t*)
		__cl_malloc_priv( sizeof(cl_mem_tracker_t), FALSE );

	if( !gp_mem_tracker )
		return CL_INSUFFICIENT_MEMORY;

	/* Initialize the free list. */
	cl_qlist_init( &gp_mem_tracker->free_hdr_list );
	/* Initialize the allocation list. */
	cl_qmap_init( &gp_mem_tracker->alloc_map );

	/* Initialize the spin lock to protect list operations. */
	status = cl_spinlock_init( &gp_mem_tracker->lock );
	if( status != CL_SUCCESS )
	{
		__cl_free_priv( gp_mem_tracker );
		gp_mem_tracker = NULL;
	}
    return status;
}


/*
 * Clean up memory tracking.
 */
static inline cl_status_t
__cl_mem_track_stop( void )
{
	cl_map_item_t	*p_map_item;
	cl_list_item_t	*p_list_item;

	if( !gp_mem_tracker )
		return CL_SUCCESS;

	if( cl_qmap_count( &gp_mem_tracker->alloc_map ) )
	{
#ifdef CL_KERNEL
		CL_ASSERT(FALSE);
#endif
		/* There are still items in the list.  Print them out. */
		cl_mem_display();
	}

	/* Free all allocated headers. */
	cl_spinlock_acquire( &gp_mem_tracker->lock );
	while( cl_qmap_count( &gp_mem_tracker->alloc_map ) )
	{
		p_map_item = cl_qmap_head( &gp_mem_tracker->alloc_map );
		cl_qmap_remove_item( &gp_mem_tracker->alloc_map, p_map_item );
		__cl_free_priv(
			PARENT_STRUCT( p_map_item, cl_malloc_hdr_t, map_item ) );
	}

	while( cl_qlist_count( &gp_mem_tracker->free_hdr_list ) )
	{
		p_list_item = cl_qlist_remove_head( &gp_mem_tracker->free_hdr_list );
		__cl_free_priv( PARENT_STRUCT(
			p_list_item, cl_malloc_hdr_t, map_item.pool_item.list_item ) );
	}
	cl_spinlock_release( &gp_mem_tracker->lock );

	/* Destory all objects in the memory tracker object. */
	cl_spinlock_destroy( &gp_mem_tracker->lock );

	/* Free the memory allocated for the memory tracker object. */
	__cl_free_priv( gp_mem_tracker );
	gp_mem_tracker = NULL;
    
       return CL_SUCCESS;
}


/*
 * Enables memory allocation tracking.
 */
cl_status_t
__cl_mem_track(
	IN	const boolean_t	start )
{
	if( start )
		return __cl_mem_track_start();
	else
		return __cl_mem_track_stop();
}


/*
 * Display memory usage.
 */
void
cl_mem_display( void )
{
	cl_map_item_t		*p_map_item;
	cl_malloc_hdr_t		*p_hdr;
#define MAX_LINES_TO_PRINT	40
	int n_lines = 0;

	if( !gp_mem_tracker )
		return;

	cl_spinlock_acquire( &gp_mem_tracker->lock );

#ifdef _DEBUG_

#ifdef CL_KERNEL

	DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, 
		"\n\n\n*** Memory Usage - %d allocations left, max %d lines will be printed ***\n", 
		gp_mem_tracker->alloc_map.count, MAX_LINES_TO_PRINT );
#else
	cl_msg_out( "\n\n\n*** Memory Usage ***\n" );
#endif // CL_KERNEL
#endif //  _DEBUG_
	p_map_item = cl_qmap_head( &gp_mem_tracker->alloc_map );
	while( p_map_item != cl_qmap_end( &gp_mem_tracker->alloc_map ) )
	{
		if ( n_lines++ >= MAX_LINES_TO_PRINT )
			break;
		
		/*
		 * Get the pointer to the header.  Note that the object member of the
		 * list item will be used to store the pointer to the user's memory.
		 */
		p_hdr = PARENT_STRUCT( p_map_item, cl_malloc_hdr_t, map_item );

#ifdef _DEBUG_
#ifdef CL_KERNEL

		DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, 
			"\tMemory block for '%s' at %p of size %#x allocated in file %s line %d\n",
			(p_hdr->tag == NULL) ? "Unknown" : p_hdr->tag,
			p_hdr->p_mem, p_hdr->size, p_hdr->file_name, p_hdr->line_num );
#else
		cl_msg_out( "\tMemory block at %p of size %#x allocated in file %s line %d\n",
			p_hdr->p_mem, p_hdr->size, p_hdr->file_name, p_hdr->line_num );
#endif // CL_KERNEL
#endif // _DEBUG_
        __cl_free_priv( p_hdr->p_mem );

		p_map_item = cl_qmap_next( p_map_item );
	}
	cl_msg_out( "*** End of Memory Usage ***\n\n" );
	cl_spinlock_release( &gp_mem_tracker->lock );
}



/*
 * Allocates memory and stores information about the allocation in a list.
 * The contents of the list can be printed out by calling the function
 * "MemoryReportUsage".  Memory allocation will succeed even if the list
 * cannot be created.
 */
static 
void*
__cl_malloc_trk_internal(
	IN	const char* const	p_file_name,
	IN	const int32_t		line_num,
	IN	const size_t		size,
	IN	const boolean_t		pageable,
	IN	const char*			tag )
{
	cl_malloc_hdr_t	*p_hdr;
	cl_list_item_t	*p_list_item;
	void			*p_mem;
	uint64_t		temp_buf[FILE_NAME_LENGTH/sizeof(uint64_t)];
	int32_t			temp_line;

	/*
	 * Allocate the memory first, so that we give the user's allocation
	 * priority over the the header allocation.
	 */
	p_mem = __cl_malloc_priv( size, pageable );

	if( !p_mem )
		return( NULL );

	if( !gp_mem_tracker )
		return( p_mem );

	/*
	 * Make copies of the file name and line number in case those
	 * parameters are in paged pool.
	 */
	temp_line = line_num;
#ifdef NTDDI_WIN8
	strncpy_s( (char*)temp_buf, sizeof(temp_buf), p_file_name, FILE_NAME_LENGTH );
#else 
	//TODO to use ntrsafe.dll and remove this call
	strncpy( (char*)temp_buf, p_file_name, FILE_NAME_LENGTH );
#endif

	/* Make sure the string is null terminated. */
	((char*)temp_buf)[FILE_NAME_LENGTH - 1] = '\0';

	cl_spinlock_acquire( &gp_mem_tracker->lock );

	/* Get a header from the free header list. */
	p_list_item = cl_qlist_remove_head( &gp_mem_tracker->free_hdr_list );
	if( p_list_item != cl_qlist_end( &gp_mem_tracker->free_hdr_list ) )
	{
		/* Set the header pointer to the header retrieved from the list. */
		p_hdr = PARENT_STRUCT( p_list_item, cl_malloc_hdr_t,
			map_item.pool_item.list_item );
	}
	else
	{
		/* We failed to get a free header.  Allocate one. */
		p_hdr = __cl_malloc_priv( sizeof(cl_malloc_hdr_t), FALSE );
		if( !p_hdr )
		{
			/* We failed to allocate the header.  Return the user's memory. */
			cl_spinlock_release( &gp_mem_tracker->lock );
			return( p_mem );
		}
	}
	cl_memcpy( p_hdr->file_name, temp_buf, FILE_NAME_LENGTH );
	p_hdr->line_num = temp_line;
	/*
	 * We store the pointer to the memory returned to the user.  This allows
	 * searching the list of allocated memory even if the buffer allocated is
	 * not in the list without dereferencing memory we do not own.
	 */
	p_hdr->p_mem = p_mem;
	p_hdr->size = (uint32_t)size;
	p_hdr->tag = (char*)tag;

	/* Insert the header structure into our allocation list. */
	cl_qmap_insert( &gp_mem_tracker->alloc_map, (uintptr_t)p_mem, &p_hdr->map_item );
	cl_spinlock_release( &gp_mem_tracker->lock );

	return( p_mem );
}

/*
 * Allocates memory and stores information about the allocation in a list.
 * The contents of the list can be printed out by calling the function
 * "MemoryReportUsage".  Memory allocation will succeed even if the list
 * cannot be created.
 */
void*
__cl_malloc_trk(
	IN	const char* const	p_file_name,
	IN	const int32_t		line_num,
	IN	const size_t		size,
	IN	const boolean_t		pageable )
{
	return __cl_malloc_trk_internal( p_file_name,
		line_num, size, pageable, NULL );
}

void*
__cl_malloc_trk_ex(
	IN	const char* const	p_file_name,
	IN	const int32_t		line_num,
	IN	const size_t		size,
	IN	const boolean_t		pageable,
	IN	const char*			tag )
{
	return __cl_malloc_trk_internal( p_file_name,
		line_num, size, pageable, tag );
}

/*
 * Allocate non-tracked memory.
 */
void*
__cl_malloc_ntrk(
	IN	const size_t	size,
	IN	const boolean_t	pageable )
{
	return( __cl_malloc_priv( size, pageable ) );
}


void*
__cl_zalloc_trk(
	IN	const char* const	p_file_name,
	IN	const int32_t		line_num,
	IN	const size_t		size,
	IN	const boolean_t		pageable )
{
	void	*p_buffer;

	p_buffer = __cl_malloc_trk( p_file_name, line_num, size, pageable );
	if( p_buffer )
		cl_memclr( p_buffer, size );

	return( p_buffer );
}

void*
__cl_zalloc_trk_ex(
	IN	const char* const	p_file_name,
	IN	const int32_t		line_num,
	IN	const size_t		size,
	IN	const boolean_t		pageable,
	IN	const char*			tag )
{
	void	*p_buffer;

	p_buffer = __cl_malloc_trk_ex( p_file_name, line_num, size, pageable, tag );
	if( p_buffer )
		cl_memclr( p_buffer, size );

	return( p_buffer );
}

void*
__cl_zalloc_ntrk(
	IN	const size_t	size,
	IN	const boolean_t	pageable )
{
	void	*p_buffer;

	p_buffer = __cl_malloc_priv( size, pageable );
	if( p_buffer )
		cl_memclr( p_buffer, size );

	return( p_buffer );
}


void
__cl_free_trk(
	IN	void* const	p_memory )
{
	cl_malloc_hdr_t		*p_hdr;
	cl_map_item_t		*p_map_item;

	if( gp_mem_tracker )
	{
		cl_spinlock_acquire( &gp_mem_tracker->lock );

		/*
		 * Removes an item from the allocation tracking list given a pointer
		 * To the user's data and returns the pointer to header referencing the
		 * allocated memory block.
		 */
		p_map_item = cl_qmap_get( &gp_mem_tracker->alloc_map, (uintptr_t)p_memory );
		if( p_map_item != cl_qmap_end( &gp_mem_tracker->alloc_map ) )
		{
			/* Get the pointer to the header. */
			p_hdr = PARENT_STRUCT( p_map_item, cl_malloc_hdr_t, map_item );
			/* Remove the item from the list. */
			cl_qmap_remove_item( &gp_mem_tracker->alloc_map, p_map_item );

			/* Return the header to the free header list. */
			cl_qlist_insert_head( &gp_mem_tracker->free_hdr_list,
				&p_hdr->map_item.pool_item.list_item );
		}
		cl_spinlock_release( &gp_mem_tracker->lock );
	}
	__cl_free_priv( p_memory );
}


void
__cl_free_ntrk(
	IN	void* const	p_memory )
{
	__cl_free_priv( p_memory );
}
