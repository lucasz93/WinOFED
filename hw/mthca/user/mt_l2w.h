#ifndef UMT_L2W_H
#define UMT_L2W_H

// ===========================================
// INCLUDES
// ===========================================

// OS
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
//#include <stddef.h>
#include <errno.h>
#include <complib/cl_memory.h>
//#include <malloc.h>
#include <mt_utils.h>


// ===========================================
// SUBSTITUTIONS
// ===========================================

#define inline	__inline
#define likely(x)			(x)
#define unlikely(x)			(x)

// ===========================================
// LITERALS
// ===========================================



// ===========================================
// TYPES
// ===========================================


// ===========================================
// MACROS
// ===========================================

// nullifying macros

#define ERR_PTR(error)		((void*)(LONG_PTR)(error))
#define PTR_ERR(ptr)			((long)(LONG_PTR)(void*)(ptr))
//TODO: there are 2 assumptions here:
// - pointer can't be too big (around -1)
// - error can't be bigger than 1000
#define IS_ERR(ptr)				((ULONG_PTR)ptr > (ULONG_PTR)-1000L)

#define ffsl(val)		ffs(val)

extern size_t g_page_size;

static inline BOOLEAN is_power_of_2(uint32_t n)
{
	return (!!n & !(n & (n-1))) ? TRUE : FALSE;
}

// Allocated memory is zeroed !
static inline int posix_memalign(void **memptr, int alignment, int size)
{
	int aligned_size, desc_size = sizeof(int);
	char *real_addr, *aligned_addr;

	// sanity check: alignment should a power of 2 and more then 2
	if ( alignment < desc_size || !is_power_of_2((uint32_t)alignment) )
		return -EINVAL;

	// calculate size, needed for aligned allocation
	aligned_size = size + alignment + desc_size;

	// allocate
	real_addr = cl_zalloc(aligned_size);
	if ( real_addr == NULL )
		return -ENOMEM;

	// calculate aligned address
	aligned_addr = (char *)(((ULONG_PTR)(real_addr + alignment-1)) & ~(alignment - 1));
	if ( aligned_addr < real_addr + desc_size )
		aligned_addr += alignment;

	// store the descriptor
	*(int*)(aligned_addr - desc_size) = (int)(aligned_addr - real_addr);
	
	*memptr = aligned_addr;
	return 0;
}

// there is no such POSIX function. Called so to be similar to the allocation one.
static inline void posix_memfree(void *memptr)
{
	int *desc_addr = (int*)((char*)memptr - sizeof(int));
	char *real_addr = (char*)memptr - *desc_addr;
	cl_free(real_addr);
}

// ===========================================
// FUNCTIONS
// ===========================================


#endif

