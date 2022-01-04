/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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
 * Device and complib support for UAL
 */

#if !defined(__UAL_SUPPORT_H__)
#define __UAL_SUPPORT_H__

#include <complib/comp_lib.h>
#include <iba/ib_uvp.h>
#include <iba/ib_al.h>
#include <iba/ib_al_ioctl.h>
#include "al_dev.h"
#include "al_debug.h"



#include <strsafe.h>


/* File handle to kernel transport. */
extern HANDLE		g_al_device;

/*
 * Mutex to serialize open/close AL calls so that we can properly synchronize
 * initializing and cleaning up AL internally.
 */
extern cl_mutex_t	g_open_close_mutex;


/* Helper functions for UVP library access. */
static inline void*
al_load_uvp(
	__in				char* const					uvp_lib_name )
{
	StringCbCatA( uvp_lib_name, 32, ".dll" );
	return LoadLibrary( uvp_lib_name );
}

static inline void
al_unload_uvp(
	IN				void						*h_uvp_lib )
{
	FreeLibrary( h_uvp_lib );
}

static inline void
al_uvp_lib_err(
	__in				uint32_t					dbg_lvl,
	__in 				char* const					msg,
	 				... )
{
	char	buffer[256];
	char*	error;
	va_list	args;

	/* Make free build warning go away. */
	UNUSED_PARAM( dbg_lvl );

	va_start( args, msg );

	if( StringCbVPrintfA( buffer, 256, msg, args ) ==
		STRSAFE_E_INSUFFICIENT_BUFFER )
	{
		/* Overflow... */
		buffer[252] = '.';
		buffer[253] = '.';
		buffer[254] = '.';
	}
	va_end(args);

	if( !FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&error, 0, NULL ) )
	{
		//AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("%s (%d)\n", buffer, GetLastError()) );
	}
	else
	{
		//AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("%s (%s)\n", buffer, error) );
		LocalFree( error );
	}
}

#endif // __UAL_SUPPORT_H__
