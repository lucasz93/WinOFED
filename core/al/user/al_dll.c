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


#include <tchar.h>
#include <stdlib.h>
#include "ual_support.h"
#include "al_mgr.h"
#include "ib_common.h"
#include "al_init.h"
#include <process.h>
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_dll.tmh"
#endif



HANDLE		g_al_device = INVALID_HANDLE_VALUE;
cl_mutex_t	g_open_close_mutex;
cl_perf_t	g_perf;

extern		al_mgr_t*		gp_al_mgr;
extern		cl_async_proc_t	*gp_async_proc_mgr;


static BOOL
_DllMain(
	IN				HINSTANCE					h_module,
	IN				DWORD						ul_reason_for_call, 
	IN				LPVOID						lp_reserved )
{

#if !defined(EVENT_TRACING)
#if DBG 

#define ENV_BUFSIZE 16

	TCHAR  dbg_lvl_str[ENV_BUFSIZE];
	DWORD	i;
#endif
#endif

	UNUSED_PARAM( lp_reserved );

	switch( ul_reason_for_call )
	{
	case DLL_PROCESS_ATTACH:

#if defined(EVENT_TRACING)
    WPP_INIT_TRACING(L"ibal.dll");
#endif		
		cl_perf_init( &g_perf, AlMaxPerf );
		DisableThreadLibraryCalls( h_module );

		cl_mutex_construct( &g_open_close_mutex );
		if( cl_mutex_init( &g_open_close_mutex ) != CL_SUCCESS )
			return FALSE;

#if !defined(EVENT_TRACING)
#if DBG 

	i = GetEnvironmentVariable( "IBAL_UAL_DBG_LEVEL", dbg_lvl_str, ENV_BUFSIZE );
	if( i && i <= 16 )
	{
		g_al_dbg_level = _tcstoul( dbg_lvl_str, NULL, ENV_BUFSIZE );
	}

	i = GetEnvironmentVariable( "IBAL_UAL_DBG_FLAGS", dbg_lvl_str, ENV_BUFSIZE );
	if( i && i <= 16 )
	{
		g_al_dbg_flags = _tcstoul( dbg_lvl_str, NULL, ENV_BUFSIZE );
	}

	if( g_al_dbg_flags & AL_DBG_ERR )
		g_al_dbg_flags |= CL_DBG_ERROR;

	AL_PRINT(TRACE_LEVEL_INFORMATION ,AL_DBG_DEV ,
		("Given IBAL_UAL_DBG debug level:%d  debug flags 0x%x\n",
		g_al_dbg_level ,g_al_dbg_flags) );

#endif
#endif

		break;

	case DLL_PROCESS_DETACH:

		cl_mutex_destroy( &g_open_close_mutex );
		cl_perf_destroy( &g_perf, TRUE );

#if defined(EVENT_TRACING)
		WPP_CLEANUP();
#endif
		break;
	}
	return TRUE;
}


BOOL APIENTRY
DllMain(
	IN				HINSTANCE					h_module,
	IN				DWORD						ul_reason_for_call, 
	IN				LPVOID						lp_reserved )
{
	__security_init_cookie();
	switch( ul_reason_for_call )
	{
	case DLL_PROCESS_ATTACH:
		return _DllMain( h_module, ul_reason_for_call, lp_reserved );

	case DLL_PROCESS_DETACH:
		return _DllMain( h_module, ul_reason_for_call, lp_reserved );
	}
	return TRUE;
}


cl_status_t
do_al_dev_ioctl(
	IN				uint32_t					command,
	IN				void						*p_in_buf,
	IN				uintn_t						in_buf_size,
	IN				void						*p_out_buf,
	IN				uintn_t						out_buf_size,
		OUT			uintn_t						*p_bytes_ret )
{
	cl_status_t cl_status;

	AL_ENTER( AL_DBG_DEV );

	CL_ASSERT( g_al_device != INVALID_HANDLE_VALUE );

	cl_status = cl_ioctl_request(	g_al_device,
									command,
									p_in_buf,
									in_buf_size,
									p_out_buf,
									out_buf_size,
									p_bytes_ret,
									NULL );

	if( cl_status != CL_SUCCESS )
	{
		CL_ASSERT( cl_status != CL_PENDING );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("Error performing IOCTL 0x%08x to AL driver (%s)\n",
			 command, CL_STATUS_MSG(cl_status)) );
		return CL_ERROR;
	}

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}
