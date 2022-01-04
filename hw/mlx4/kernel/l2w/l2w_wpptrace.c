#include "l2w_wpptrace.h"

u32 ROUNDUP_LOG2(u32 arg)
{
    if (arg <= 1)    return 0;
    if (arg <= 2)    return 1;
    if (arg <= 4)    return 2;
    if (arg <= 8)    return 3;
    if (arg <= 16)   return 4;
    if (arg <= 32)   return 5;
    if (arg <= 64)   return 6;
    if (arg <= 128)  return 7;
    if (arg <= 256)  return 8;
    if (arg <= 512)  return 9;
    if (arg <= 1024) return 10;
    if (arg <= 2048) return 11;
    if (arg <= 4096) return 12;
    if (arg <= 8192) return 13;
    if (arg <= 16384) return 14;
    if (arg <= 32768) return 15;
    if (arg <= 65536) return 16;
    ASSERT(FALSE);
    return 32;
}
