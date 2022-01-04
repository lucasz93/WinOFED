/*++

Copyright (c) 2005-2010 Mellanox Technologies. All rights reserved.

Module Name:
    gu_dbg.h

Abstract:
    This modulde contains all related dubug code
Notes:

--*/

#pragma once

#ifdef _PREFAST_
#define CONDITION_ASSUMED(X) __analysis_assume((X))
#else
#define CONDITION_ASSUMED(X) 
#endif // _PREFAST_

#if DBG

#undef ASSERT
#define ASSERT(x) if(!(x)) { \
	DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, "Assertion failed: %s:%d %s\n", __FILE__, __LINE__, #x);\
    DbgBreakPoint(); }\
    CONDITION_ASSUMED(x);

#define ASSERT_ALWAYS(x) ASSERT(x)

#else   // !DBG

#undef ASSERT
#define ASSERT(x)

#define ASSERT_ALWAYS(x) if(!(x)) { \
    DbgBreakPoint(); }

#endif  // DBG
