#ifndef MT_L2W_H
#define MT_L2W_H

// ===========================================
// INCLUDES
// ===========================================

// OS
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

// ours - the order is important
#include <mt_types.h>
#include <mt_bitmap.h>
#include <mt_memory.h>
#include <mt_list.h>
#include <mt_spinlock.h>
#include <mt_atomic.h>
#include <mt_sync.h>
#include <mt_pci.h>
#include <mt_pcipool.h>
//#include <mt_byteorder.h>
#include <complib/cl_timer.h>
#include <complib/cl_qlist.h>
#include <hca_debug.h>


// ===========================================
// SUBSTITUTIONS
// ===========================================

#define BUG_ON(exp)		ASSERT(!(exp)) /* in Linux follows here panic() !*/ 
#define WARN_ON(exp)		ASSERT(!(exp)) /* in Linux follows here panic() !*/ 
#define snprintf	_snprintf

// memory barriers
#define wmb KeMemoryBarrier
#define rmb KeMemoryBarrier
#define mb KeMemoryBarrier

// ===========================================
// LITERALS
// ===========================================




// ===========================================
// TYPES
// ===========================================

// rw_lock
typedef spinlock_t		rwlock_t;

// dummy function
typedef void (*MT_EMPTY_FUNC)();

// ===========================================
// MACROS
// ===========================================

// ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// ALIGN
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))

// there is a bug in Microsoft compiler, that when _byteswap_uint64() gets an expression
// it executes the expression but doesn't swap tte dwords
// So, there's a workaround
#ifdef BYTESWAP_UINT64_BUG_FIXED
#define CPU_2_BE64_PREP		
#define CPU_2_BE64(x)			cl_hton64(x)
#else
#define CPU_2_BE64_PREP	unsigned __int64 __tmp__	
#define CPU_2_BE64(x)			( __tmp__ = x, cl_hton64(__tmp__) )
#endif


SIZE_T strlcpy(char *dest, const char *src, SIZE_T size);
void MT_time_calibrate();

#define ERR_PTR(error)		((void*)(LONG_PTR)(error))
#define PTR_ERR(ptr)			((long)(LONG_PTR)(void*)(ptr))
//TODO: there are 2 assumptions here:
// - pointer can't be too big (around -1)
// - error can't be bigger than 1000
#define IS_ERR(ptr)				((ULONG_PTR)ptr > (ULONG_PTR)-1000L)

#endif
