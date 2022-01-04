/*++

Copyright (c) 2005-2012 Mellanox Technologies. All rights reserved.

Module Name:
    event_trace.h

Abstract:
    This modulde contains all related API definitions
Notes:

--*/

#pragma once

typedef VOID (*ET_POST_EVENT)( IN const char * const  Caller, IN const char * const  Format, ...);
extern ET_POST_EVENT g_post_event_func;
#if defined(CL_KERNEL)
#define g_post_event(_Format, ...)	\
	if (g_post_event_func) {	\
		g_post_event_func( __FUNCTION__, _Format, __VA_ARGS__ );	\
	}
#else
#define g_post_event(_Format, ...)	
#endif

