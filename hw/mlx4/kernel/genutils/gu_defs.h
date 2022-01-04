/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    gu_defs.h

Abstract:

Notes:

--*/

#pragma once

// basic types
typedef unsigned char			u8, __u8;
typedef unsigned short int	u16, __u16;
typedef unsigned int				u32, __u32;
typedef unsigned __int64		u64, __u64;
typedef char			s8, __s8;
typedef short int	s16, __s16;
typedef int				s32, __s32;
typedef __int64		s64, __s64;
typedef u16 __be16 ;
typedef u32 __be32 ;
typedef u64 __be64 ;

#ifdef _WIN64
typedef unsigned __int64 uintn_t;
#else
typedef unsigned int uintn_t;
#endif

typedef unsigned __int64        uint64_t;

#define be16_to_cpu(a)		_byteswap_ushort((USHORT)(a))
#define be32_to_cpu(a)		_byteswap_ulong((ULONG)(a))

#define __be16_to_cpu       be16_to_cpu
#define __be32_to_cpu       be32_to_cpu


u32 inline CL_NTOH32( u32 x ) {
    return (u32)(          
        (((u32)(x) & 0x000000FF) << 24) |
        (((u32)(x) & 0x0000FF00) << 8) |
        (((u32)(x) & 0x00FF0000) >> 8) |
        (((u32)(x) & 0xFF000000) >> 24) );
}
#define CL_HTON32				CL_NTOH32

#ifdef _WIN64
#define __cpu_to_be32(x) ((((x) >> 24)&0x000000ff) | (((x) >> 8)&0x0000ff00) | (((x) << 8)&0x00ff0000) | (((x) << 24)&0xff000000))
#elif defined(_WIN32)
__inline __int32 __cpu_to_be32( __int32 dwX ) 
{ 
    _asm    mov     eax, dwX     
    _asm    bswap   eax     
    _asm    mov     dwX, eax   
        
    return dwX; 
}
#else
#error unsupported platform
#endif

//#define __cpu_to_be32(x) cpu_to_be32(x)
#define __cpu_to_be16(x) cpu_to_be16(x)
u16 inline cpu_to_be16(u16 in) {
    return in >> 8 | in << 8;
}

inline u64 ALIGN64(u64 pAddr, u64 a) {return ((pAddr)+(a)-1)&~((a)-1);}

#define XOR(x,y)		(!(x) != !(y))
#define XNOR(x,y)		(!(x) == !(y))
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) > (b) ? (a) : (b))


// Convert the mac from the way that windows gives it to the way we want it.
inline void mac_to_be64(u64 *dst, u64 *src)
{
    char *csrc = (char *)src;
    char *cdst = (char *)dst;
    cdst[0] = csrc[5];
    cdst[1] = csrc[4];
    cdst[2] = csrc[3];
    cdst[3] = csrc[2];
    cdst[4] = csrc[1];
    cdst[5] = csrc[0];
    cdst[6] = 0;
    cdst[7] = 0;
}

inline u64 be64_to_mac( UCHAR *src)
{
    u64 dst;
    mac_to_be64(&dst,(u64 *)src);
    return dst;
}

#define IS_BIT_SET(val, mask) \
    (((val) & (mask)) ? 1 : 0)


