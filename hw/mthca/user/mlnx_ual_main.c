/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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
#include <mt_l2w.h>
#include "mlnx_ual_main.h"

#if defined(EVENT_TRACING)
#include "mlnx_ual_main.tmh"
#endif


uint32_t	mlnx_dbg_lvl = 0; // MLNX_TRACE_LVL_8;

static void uvp_init();

BOOL APIENTRY
DllMain(
	IN				HINSTANCE					h_module,
	IN				DWORD						ul_reason_for_call, 
	IN				LPVOID						lp_reserved )
{
	switch( ul_reason_for_call )
	{
	case DLL_PROCESS_ATTACH:
#if defined(EVENT_TRACING)
    WPP_INIT_TRACING(L"mthcau.dll");
#endif
		fill_bit_tbls();
		uvp_init();
		break;

	 case DLL_PROCESS_DETACH:
		// The calling process is detaching
		// the DLL from its address space.
		//
		// Note that lpvReserved will be NULL if the detach is due to
		// a FreeLibrary() call, and non-NULL if the detach is due to
		// process cleanup.
		//
#if defined(EVENT_TRACING)
		WPP_CLEANUP();
#endif

	default:
		return TRUE;
	}
	return TRUE;
}


/*
 *     UVP Shared Library Init routine
*/

static void
uvp_init()
{

#if !defined(EVENT_TRACING)
#if DBG 
#define ENV_BUFSIZE 16
	TCHAR  dbg_lvl_str[ENV_BUFSIZE];
	DWORD	i;


	i = GetEnvironmentVariable( "UVP_DBG_LEVEL", dbg_lvl_str, ENV_BUFSIZE );
	if( i && i <= 16 )
	{
		g_mlnx_dbg_level = _tcstoul( dbg_lvl_str, NULL, ENV_BUFSIZE );
	}

	i = GetEnvironmentVariable( "UVP_DBG_FLAGS", dbg_lvl_str, ENV_BUFSIZE );
	if( i && i <= 16 )
	{
		g_mlnx_dbg_flags = _tcstoul( dbg_lvl_str, NULL, ENV_BUFSIZE );
	}


	UVP_PRINT(TRACE_LEVEL_INFORMATION ,UVP_DBG_DEV ,
		("Given UVP_DBG debug level:%d  debug flags 0x%x\n",
		g_mlnx_dbg_level ,g_mlnx_dbg_flags) );

#endif
#endif
}

__declspec(dllexport) ib_api_status_t
uvp_get_interface (
	IN		GUID	iid,
	IN		void*	pifc)
{
	ib_api_status_t	status = IB_SUCCESS;

    UVP_ENTER(UVP_DBG_SHIM);

	if (IsEqualGUID(&iid, &IID_UVP))
	{
		mlnx_get_ca_interface((uvp_interface_t *) pifc);
		mlnx_get_pd_interface((uvp_interface_t *) pifc);
		mlnx_get_srq_interface((uvp_interface_t *) pifc);
		mlnx_get_qp_interface((uvp_interface_t *) pifc);
		mlnx_get_cq_interface((uvp_interface_t *) pifc);
		mlnx_get_av_interface((uvp_interface_t *) pifc);
		mlnx_get_mrw_interface((uvp_interface_t *) pifc);
		mlnx_get_mcast_interface((uvp_interface_t *) pifc);
		mlnx_get_osbypass_interface((uvp_interface_t *) pifc);
	}
	else
	{
		status = IB_UNSUPPORTED;
	}

	UVP_EXIT(UVP_DBG_SHIM);
	return status;
}
