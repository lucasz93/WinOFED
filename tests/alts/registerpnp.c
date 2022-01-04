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
al_test_pnp_callback(
	IN ib_pnp_rec_t* notify );

/*
 * al_test_register_pnp test case.
 * This test case test the ib_reg_pnp and ib_dereg_pnp calls of AL
 */
ib_api_status_t
al_test_register_pnp(void)
{
	ib_al_handle_t	h_al;
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_pnp_req_t	pnp_req;
	ib_pnp_handle_t h_pnp;


	ALTS_ENTER( ALTS_DBG_VERBOSE );

	while(1)
	{
		ib_status = ib_open_al(&h_al);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_open_al failed status = %d", ib_status) );
			break;
		}

		ALTS_PRINT( ALTS_DBG_INFO,
				("ib_open_al PASSED!!!\n") );

		cl_memclr(&pnp_req,sizeof(ib_pnp_req_t));

		pnp_req.pnp_context	= (void*)(uintn_t)0xdeadbeef;
		pnp_req.pfn_pnp_cb	= al_test_pnp_callback;
		pnp_req.pnp_class	= IB_PNP_CA | IB_PNP_FLAG_REG_COMPLETE | IB_PNP_FLAG_REG_SYNC;

		h_pnp = NULL;
		ib_status = ib_reg_pnp(h_al, &pnp_req, &h_pnp );

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_reg_pnp failed status = %s\n", ib_get_err_str(ib_status)));

		}
		else
		{
			ALTS_PRINT( ALTS_DBG_INFO,
				("ib_reg_pnp PASSED!!\n"));
		}

		ALTS_PRINT( ALTS_DBG_INFO, ("h_pnp = (0x%p)\n", h_pnp) );

		if(h_pnp)
		{
			//ib_status = ib_dereg_pnp(h_pnp, al_test_pnp_destroy_cb);
			ib_status = ib_dereg_pnp(h_pnp, NULL);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_dereg_pnp failed status = %s\n", ib_get_err_str(ib_status)));
			}
			else
			{
				ALTS_PRINT( ALTS_DBG_INFO,
					("ib_dereg_pnp PASSED!!\n"));
			}
		}


		ib_status = ib_close_al(h_al);

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_close_al failed status = %s\n", ib_get_err_str(ib_status)));
		}
		else
		{
			ALTS_PRINT( ALTS_DBG_INFO, ("ib_close_al PASSED!!!\n") );
		}

		break; //Break from while
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE );
	return ib_status;
}

/*
 * This is a pnp callback function call by AL
 */

ib_api_status_t
al_test_pnp_callback(
	IN ib_pnp_rec_t* notify )
{
	void	*context = (void*)0x1234;
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	CL_ASSERT(notify);

	ALTS_PRINT( ALTS_DBG_INFO,
				("AL pnp event (0x%x)\n", notify->pnp_event) );

	CL_ASSERT( notify->pnp_context == (void*)(uintn_t)0xdeadbeef );

	switch ( notify->pnp_event )
	{
		/*
		 *  Deal with additions
		 */
		case IB_PNP_CA_ADD:
			CL_ASSERT( ((ib_pnp_ca_rec_t*)notify)->p_ca_attr != NULL );
			notify->context = context;
			break;
		case IB_PNP_PORT_ADD:
			CL_ASSERT( ((ib_pnp_port_rec_t*)notify)->p_ca_attr != NULL );
			CL_ASSERT( ((ib_pnp_port_rec_t*)notify)->p_port_attr != NULL );
			notify->context = context;
			break;
		/*
		 *  Deal with removals
		 */
		case IB_PNP_CA_REMOVE:
			CL_ASSERT( notify->context == context);
			break;
		case IB_PNP_PORT_REMOVE:
			CL_ASSERT( notify->context == context );
			break;
		/*
		 *  Deal with link state
		 */
		case IB_PNP_PORT_ACTIVE:
			CL_ASSERT( ((ib_pnp_port_rec_t*)notify)->p_port_attr != NULL );
			CL_ASSERT( notify->context == context );
			/*
			 *  we treat a port up event like a pkey change event
			 */
			break;
		case IB_PNP_PORT_DOWN:
			CL_ASSERT( notify->context == context );
			break;
		/*
		 *  Deal with PKey changes
		 */
		case IB_PNP_PKEY_CHANGE:
			CL_ASSERT( ((ib_pnp_port_rec_t*)notify)->p_port_attr != NULL );
			CL_ASSERT( notify->context == context );
			break;
		case IB_PNP_REG_COMPLETE:
			break;
		/*
		 *  Deal with unknown/unhandled
		 */
		default:
			ALTS_PRINT( ALTS_DBG_ERROR,
				("Unknown/unhandled AL event (0x%x)\n", notify->pnp_event) );

	}
	ALTS_EXIT( ALTS_DBG_VERBOSE );
	return IB_SUCCESS;
}




