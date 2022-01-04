#ifndef MLX4_L2W_H
#define MLX4_L2W_H

// ===========================================
// INCLUDES
// ===========================================

// OS
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>

#include <errno.h>
#include <cl_spinlock.h>
#include <cl_byteswap.h>
#include <cl_types.h>
#include <cl_memory.h>


// ===========================================
// SUBSTITUTIONS
// ===========================================

// Memory
#define memset		cl_memset
#define memclr		cl_memclr
#define memcpy		cl_memcpy
#define malloc		cl_malloc
#define calloc(x,y)	cl_zalloc((x)*(y))
#define free			cl_free

// ByteSwap
#define htons	cl_hton16
#define htonl		cl_hton32
#define htonll	cl_hton64

#define ntohs	cl_ntoh16
#define ntohl		cl_ntoh32
#define ntohll	cl_ntoh64

// Synchronization
#define pthread_mutex_t		HANDLE
#define pthread_spinlock_t	cl_spinlock_t

#define pthread_spin_init(x,y)		cl_spinlock_init(x)
#define pthread_spin_lock			cl_spinlock_acquire
#define pthread_spin_unlock		cl_spinlock_release


// ===========================================
// LITERALS
// ===========================================


// ===========================================
// TYPES
// ===========================================

typedef uint8_t	__u8;
typedef uint16_t	__u16;
typedef uint32_t	__u32;
typedef uint64_t	__u64;

typedef int32_t	__s32;


// ===========================================
// MACROS
// ===========================================


// ===========================================
// FUNCTIONS
// ===========================================

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

static inline int ffsl(uint32_t x)
{
       int r = 0;

       if (!x)
               return 0;
       if (!(x & 0x0000ffffu)) {
               x >>= 16;
               r += 16;
       }
       if (!(x & 0x000000ffu)) {
               x >>= 8;
               r += 8;
       }
       if (!(x & 0x0000000fu)) {
               x >>= 4;
               r += 4;
       }
       if (!(x & 0x000000003u)) {
               x >>= 2;
               r += 2;
       }
       if (!(x & 0x00000001u)) {
               x >>= 1;
               r += 1;
       }
       return r+1;
}

static inline void pthread_mutex_lock(HANDLE *mutex)
{
	WaitForSingleObject(*mutex, INFINITE);
}

static inline void pthread_mutex_unlock(HANDLE *mutex)
{
	ReleaseMutex(*mutex);
}

// ===========================================
// ARCHITECTURE DEFINITIONS
// ===========================================

/*
 * Architecture-specific defines.  Currently, an architecture is
 * required to implement the following operations:
 *
 * mb() - memory barrier.  No loads or stores may be reordered across
 *     this macro by either the compiler or the CPU.
 * rmb() - read memory barrier.  No loads may be reordered across this
 *     macro by either the compiler or the CPU.
 * wmb() - write memory barrier.  No stores may be reordered across
 *     this macro by either the compiler or the CPU.
 * wc_wmb() - flush write combine buffers.  No write-combined writes
 *     will be reordered across this macro by either the compiler or
 *     the CPU.
 */
 
#define mb			MemoryBarrier
#define rmb			mb
#define wmb			mb
#define wc_wmb		mb

#endif
