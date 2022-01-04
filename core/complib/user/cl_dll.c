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

#include <complib/comp_lib.h>
#include <process.h>

static BOOL
_DllMain(
	IN				HINSTANCE					h_module,
	IN				DWORD						ul_reason_for_call, 
	IN				LPVOID						lp_reserved )
{
	UNUSED_PARAM( lp_reserved );

	switch( ul_reason_for_call )
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls( h_module );
		__cl_mem_track( TRUE );
		break;

	case DLL_PROCESS_DETACH:
		__cl_mem_track( FALSE );
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

