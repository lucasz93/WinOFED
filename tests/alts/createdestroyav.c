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
#include <complib/cl_memory.h>
#include <alts_debug.h>
#include <alts_common.h>

/*
 * Function prototypes
 */
ib_api_status_t
alts_av(
	boolean_t modify_av_attr
	);


/*
 * Test Case CrateDestroyAV
 */

ib_api_status_t
al_test_create_destroy_av(void)
{
	boolean_t modify_av_attr = FALSE;

	return alts_av(modify_av_attr);
}

ib_api_status_t
al_test_query_modify_av(void)
{
	boolean_t modify_av_attr = TRUE;

	return alts_av(modify_av_attr);
}


/* Internal Functions */

ib_api_status_t
alts_av(
	boolean_t modify_av_attr
	)
{
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_al_handle_t h_al = NULL;
	size_t guid_count;
	ib_port_attr_t *p_alts_port_attr;
	ib_av_attr_t alts_av_attr, query_av_attr;
	ib_ca_handle_t h_ca;
	ib_pd_handle_t h_pd, h_pd1;
	ib_av_handle_t h_av;
	ib_net64_t ca_guid_array[ALTS_MAX_CA];
	uint32_t bsize;
	ib_ca_attr_t *alts_ca_attr = NULL;
	uint8_t i;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	while(1)
	{
		/* Step 1: Open AL */
		ib_status = alts_open_al(&h_al);

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR, \
				("Open AL failed\n"));
				break;
		}

		CL_ASSERT(h_al);

		/*
		 * Step 2: Open the first available CA
		 */

		ib_status = ib_get_ca_guids(h_al, NULL, &guid_count);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_get_ca_guids failed status = %s\n",ib_get_err_str(ib_status)) );
			break;

		}

		ALTS_PRINT(ALTS_DBG_INFO, \
			("Total number of CA in the sytem is %d\n",(uint32_t)guid_count));

		if(guid_count == 0)
		{
			ib_status = IB_ERROR;
			break;
		}

		ib_status = ib_get_ca_guids(h_al, ca_guid_array, &guid_count);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_get_ca_guids failed with status = %s\n", ib_get_err_str(ib_status)) );
			break;
		}

		ib_status = ib_open_ca(h_al,
			ca_guid_array[0],	//Default open the first CA
			alts_ca_err_cb,
			(void *)1234,	//ca_context
			&h_ca);

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_open_ca failed with status = %s\n", ib_get_err_str(ib_status)) );
			break;

		}
		CL_ASSERT(h_ca);

		/*
		 * Step 3: Query for the CA Attributes
		 */

		/* Query the CA */
		bsize = 0;
		ib_status = ib_query_ca(h_ca, NULL, &bsize);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %s\n",ib_get_err_str(ib_status)) );

			ib_close_ca(h_ca, alts_ca_destroy_cb);
			break;
		}
		CL_ASSERT(bsize);

		/* Allocate the memory needed for query_ca */

		alts_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		CL_ASSERT(alts_ca_attr);

		ib_status = ib_query_ca(h_ca, alts_ca_attr, &bsize);

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %s\n", ib_get_err_str(ib_status)) );
			ib_close_ca(h_ca, alts_ca_destroy_cb);
			break;
		}

		p_alts_port_attr = alts_ca_attr->p_port_attr;

		/*
		 * Step 4: Get the active port
		 */
		ALTS_PRINT( ALTS_DBG_INFO,	\
			("Get the active Port\n"));
//#if 0
		for(i=0;i < alts_ca_attr->num_ports; i++)
		{
			p_alts_port_attr = &alts_ca_attr->p_port_attr[i];
			if(p_alts_port_attr->link_state == IB_LINK_ACTIVE)
				break;
		}

		if(p_alts_port_attr->link_state != IB_LINK_ACTIVE)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("port attribute link state is not active\n") );
			ib_close_ca(h_ca, alts_ca_destroy_cb);
			break;
		}


		ALTS_PRINT(ALTS_DBG_INFO, \
			("Active port number is %d\n",p_alts_port_attr->port_num));
//#endif

		/*
		 * Step 5: Construct the AV structure
		 */

		alts_av_attr.port_num = p_alts_port_attr->port_num;
		//DLID is SM LID
		alts_av_attr.dlid = p_alts_port_attr->sm_lid;

		alts_av_attr.sl = 0;
		alts_av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
		alts_av_attr.path_bits = 0;
		alts_av_attr.grh_valid = TRUE;

		alts_av_attr.grh.dest_gid.unicast.interface_id= ca_guid_array[0];
		alts_av_attr.grh.src_gid.unicast.interface_id = ca_guid_array[0];
		alts_av_attr.grh.hop_limit = 0;
		alts_av_attr.grh.ver_class_flow = 0;
		/*
		 * step 6: Create a PD
		 */

		/* NOTE Try creating PD for IB_PDT_ALIAS type */

		ib_status = ib_alloc_pd(h_ca, IB_PDT_NORMAL, (void *)1234, &h_pd);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_alloc_pd failed with status = %s\n",ib_get_err_str(ib_status)) );
			ib_close_ca(h_ca, alts_ca_destroy_cb);
			break;
		}



		/*
		 * Step 7: Create the Address Vector
		 */
		ib_status =	ib_create_av(h_pd, &alts_av_attr, &h_av);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_create_av failed with status = %s\n",ib_get_err_str(ib_status)) );
			ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
			ib_close_ca(h_ca, alts_ca_destroy_cb);
			break;
		}
		if(modify_av_attr == TRUE)
		{
			/*
			 * Query the AV fromt the handle
			 */
			ib_status = ib_query_av(h_av, &query_av_attr, &h_pd1);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_query_av failed with status = %s\n",ib_get_err_str(ib_status)) );

				ib_destroy_av(h_av);
				ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
				ib_close_ca(h_ca, alts_ca_destroy_cb);
				break;
			}

			query_av_attr.dlid = p_alts_port_attr->lid; //DLID is local lid;

			ib_status = ib_modify_av(h_av, &query_av_attr);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_modify_av failed with status = %s\n",ib_get_err_str(ib_status)) );
				ib_destroy_av(h_av);
				ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
				ib_close_ca(h_ca, alts_ca_destroy_cb);
				break;
			}
			/* Again query the AV to verify the modified value*/
			ib_status = ib_query_av(h_av, &query_av_attr, &h_pd1);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_query_av failed with status = %s\n", ib_get_err_str(ib_status)) );

				ib_destroy_av(h_av);
				ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
				ib_close_ca(h_ca, alts_ca_destroy_cb);
				break;
			}
			CL_ASSERT(query_av_attr.dlid == p_alts_port_attr->lid);
			ALTS_PRINT( ALTS_DBG_INFO,
					("ib_modify_av PASSED\n") );



		}


		/*
		 * Destroy the address Vector
		 */
		ib_status = ib_destroy_av(h_av);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_destroy_av failed with status = %s\n", ib_get_err_str(ib_status)) );
			ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
			ib_close_ca(h_ca, alts_ca_destroy_cb);
			break;
		}

		ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
		ib_close_ca(h_ca, alts_ca_destroy_cb);
		break; //End of while
	}

	/* Close AL */
	if(h_al)
		alts_close_al(h_al);
	if(alts_ca_attr)
		cl_free(alts_ca_attr);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
