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


/*
 * Abstract:
 *	This module defines the basic AL test suite function
 *
 * Environment:
 *	Kernel Mode and User Mode
 */


#include <iba/ib_types.h>
#include <iba/ib_al.h>
#include <complib/cl_memory.h>
#include <complib/cl_byteswap.h>
#include <alts_debug.h>
#include <alts_common.h>


ib_api_status_t
alts_open_al(
	ib_al_handle_t	*ph_al )
{
	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	ib_status = ib_open_al(ph_al);
	if(ib_status != IB_SUCCESS)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_open_al failed status = %d\n", ib_status) );

	}
	else
	{
		ALTS_PRINT( ALTS_DBG_INFO, ("ib_open_al PASSED.\n") );
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_close_al(
	ib_al_handle_t	h_al )
{
	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	ib_status = ib_close_al(h_al);
	if(ib_status != IB_SUCCESS)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_close_al failed status = %d\n", ib_status));
	}
	else
	{
		ALTS_PRINT( ALTS_DBG_INFO, ("ib_close_al PASSED.\n") );
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

ib_api_status_t
alts_open_ca(
	IN	ib_al_handle_t	h_al,
	OUT ib_ca_handle_t	*p_alts_ca_h )
{
	ib_api_status_t ib_status = IB_SUCCESS;
	size_t guid_count;
	ib_net64_t ca_guid_array[ALTS_MAX_CA];

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	do
	{
		ib_status = ib_get_ca_guids(h_al, NULL, &guid_count);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_get_ca_guids failed status = %d\n", (uint32_t)ib_status) );
			break;

		}

		ALTS_PRINT(ALTS_DBG_INFO, \
			("Total number of CA in the sytem is %d\n",(uint32_t)guid_count));

		if(guid_count == 0)
		{
			ib_status = IB_ERROR;
			break;
		}

		if (guid_count > ALTS_MAX_CA)
		{
			guid_count = ALTS_MAX_CA;

			ALTS_PRINT(ALTS_DBG_INFO, \
				("Resetting guid_count to %d\n",ALTS_MAX_CA));
		}

		ib_status = ib_get_ca_guids(h_al, ca_guid_array, &guid_count);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_get_ca_guids failed with status = %d\n", ib_status) );
			break;
		}

		ib_status = ib_open_ca(
			h_al,
			ca_guid_array[0],	//Default open the first CA
			alts_ca_err_cb,
			NULL,	//ca_context
			p_alts_ca_h);

		ALTS_PRINT(ALTS_DBG_INFO,
			("GUID = %" PRIx64"\n", ca_guid_array[0]));

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_open_ca failed with status = %d\n", ib_status) );
			break;
		}

	} while (0);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


ib_api_status_t
alts_close_ca(
	IN ib_ca_handle_t alts_ca_h
	)
{
	ib_api_status_t ib_status;
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	ib_status = ib_close_ca(alts_ca_h, alts_ca_destroy_cb);
	if(ib_status != IB_SUCCESS)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
		("ib_close_ca failed status = %d\n", ib_status));
	}
	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

void
alts_ca_destroy_cb(
	void *context)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( context );

	ALTS_EXIT( ALTS_DBG_VERBOSE );
}

void
alts_pd_destroy_cb(
	void *context
	)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	if(context != NULL)
		ALTS_PRINT(ALTS_DBG_INFO,
		("Context is %"PRIdSIZE_T"\n", (size_t)context)); 


	ALTS_EXIT( ALTS_DBG_VERBOSE );
}



void
alts_ca_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_PRINT( ALTS_DBG_INFO,
		("p_err_rec->code is %d\n",p_err_rec->code));

	ALTS_EXIT( ALTS_DBG_VERBOSE );
}



void
alts_print_ca_attr(
	ib_ca_attr_t *alts_ca_attr
	)
{
	uint32_t i;
	ib_port_attr_t *p_port;
	UNUSED_PARAM( alts_ca_attr );
	ALTS_PRINT( ALTS_DBG_VERBOSE,
		("\tdev_id is <0x%x>\n",alts_ca_attr->dev_id));

	for (i = 0; i < alts_ca_attr->num_ports; i ++)
	{
		p_port = (alts_ca_attr->p_port_attr +i);
		ALTS_PRINT( ALTS_DBG_INFO,
			("Port %d\tPrefix: %#16I64x \tGUID: %#16I64x\nLink Status: %s\tLID %#x \n",i,
			cl_ntoh64(p_port->p_gid_table->unicast.prefix),
			cl_ntoh64(p_port->p_gid_table->unicast.interface_id),
			ib_get_port_state_str(p_port->link_state), 
			cl_ntoh16(p_port->lid)) );
	}
}

