#ifndef MT_TYPES_H
#define MT_TYPES_H

//#include <complib/comp_lib.h>
#pragma warning( push )
#include <wdmwarn4.h>
 #include <ntddk.h>
#pragma warning( pop )

// ===========================================
// SUBSTITUTES
// ===========================================

// gcc compiler attributes
#define __iomem
#define likely(x)			(x)
#define unlikely(x)			(x)

// container_of
#define container_of		CONTAINING_RECORD

// inline 
#define inline	__inline

// ===========================================
// TYPES
// ===========================================

// basic types
typedef unsigned char			u8, __u8;
typedef unsigned short int	u16, __u16;
typedef unsigned int				u32, __u32;
typedef unsigned __int64		u64, __u64;
typedef char			s8, __s8;
typedef short int	s16, __s16;
typedef int				s32, __s32;
typedef __int64		s64, __s64;

// inherited
typedef u16  __le16;
typedef u16  __be16;
typedef u32  __le32;
typedef u32  __be32;
typedef u64  __le64;
typedef u64  __be64;
typedef u64 dma_addr_t;
typedef u64 io_addr_t;

// ===========================================
// MACROS
// ===========================================

// assert
#ifdef _DEBUG_
#define MT_ASSERT( exp )	(void)(!(exp)?cl_dbg_out("Assertion Failed:" #exp "\n"),DbgBreakPoint(),FALSE:TRUE)
#else
#define MT_ASSERT( exp )
#endif	/* _DEBUG_ */

#endif
