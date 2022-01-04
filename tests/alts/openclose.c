/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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


#include <iba/ib_types.h>
#include <iba/ib_al.h>
#include <complib/cl_thread.h>
#include <alts_debug.h>

ib_api_status_t
al_test_openclose(void)
{
	ib_al_handle_t	ph_al;
	ib_api_status_t ib_status = IB_SUCCESS;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	while(1)
	{

		ib_status = ib_open_al(&ph_al);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_TRACE( ALTS_DBG_ERROR,
				("ib_open_al failed status = %s\n", ib_get_err_str(ib_status)) );
			break;
		}

		ALTS_PRINT( ALTS_DBG_INFO, ("ib_open_al PASSED!!!\n") );

		cl_thread_suspend( 1000 );

		ib_status = ib_close_al(ph_al);

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_close_al failed status = %s\n",ib_get_err_str(ib_status)));

			break;
		}
		ALTS_PRINT( ALTS_DBG_INFO, ("ib_close_al PASSED!!!") );

		break; //Break from while
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE );
	return ib_status;
}


