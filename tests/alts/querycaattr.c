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
alts_ca_attr(
	boolean_t modify_attr
	);

/*
 * Test Case QueryCaAttributes
 */

ib_api_status_t
al_test_modifycaattr(void)
{
	boolean_t modify_ca_attr = TRUE;

	return alts_ca_attr(modify_ca_attr);
}

ib_api_status_t
al_test_querycaattr(void)
{
	boolean_t modify_ca_attr = FALSE;

	return alts_ca_attr(modify_ca_attr);
}


/* Internal Functions */

ib_api_status_t
alts_ca_attr(
	boolean_t modify_attr
	)
{
	ib_al_handle_t	h_al = NULL;
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_api_status_t ret_status = IB_SUCCESS;
	size_t guid_count;
	ib_net64_t ca_guid_array[ALTS_MAX_CA];
	ib_ca_attr_t *alts_ca_attr;
	uintn_t i;
	ib_ca_handle_t h_ca = NULL;
	uint32_t bsize;
	ib_port_attr_mod_t port_attr_mod;


	ALTS_ENTER( ALTS_DBG_VERBOSE );

	while(1)
	{
		/*
		 * Open the AL instance
		 */
		ib_status = ib_open_al(&h_al);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_open_al failed status = %d", ib_status) );
			ret_status = ib_status;
			break;
		}

		ALTS_PRINT( ALTS_DBG_INFO, ("ib_open_al PASSED.\n") );
		CL_ASSERT(h_al);

		/*
		 * Get the Local CA Guids
		 */
		ib_status = ib_get_ca_guids(h_al, NULL, &guid_count);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_get_ca_guids failed status = %d\n", (uint32_t)ib_status) );
			ret_status = ib_status;
			goto Cleanup1;
		}

		ALTS_PRINT(ALTS_DBG_INFO,
			("Total number of CA in the sytem is %d\n",(uint32_t)guid_count));

		/*
		 * If no CA's Present then return
		 */

		if(guid_count == 0)
			goto Cleanup1;

		// ca_guid_array holds ALTS_MAX_CA
		ib_status = ib_get_ca_guids(h_al, ca_guid_array, &guid_count);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_get_ca_guids failed with status = %d\n", ib_status) );
			ret_status = ib_status;
			goto Cleanup1;
		}

		

		/*
		 * For Each CA Guid found Open the CA,
		 * Query the CA Attribute and close the CA
		 */
		for(i=0; i < guid_count; i++)
		{
			ALTS_PRINT(ALTS_DBG_INFO, 
				("CA[%d] GUID IS 0x%" PRIx64 "\n",i,_byteswap_uint64(ca_guid_array[i])) );

			/* Open the CA */
			ib_status = ib_open_ca(h_al,
				ca_guid_array[i],
				alts_ca_err_cb,
				NULL,	//ca_context
				&h_ca);

			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,	("ib_open_ca failed with status = %d\n", ib_status) );
				ret_status = ib_status;
				goto Cleanup1;
			}
			ALTS_PRINT(ALTS_DBG_INFO,
			("ib_open_ca passed\n"));


			/* Query the CA */
			bsize = 0;
			ib_status = ib_query_ca(h_ca, NULL, &bsize);
			if(ib_status != IB_INSUFFICIENT_MEMORY)
			{
				ALTS_PRINT(ALTS_DBG_ERROR, ("ib_query_ca failed with status = %d\n", ib_status) );
				ret_status = ib_status;
				goto Cleanup2;
			}
			CL_ASSERT(bsize);

			/* Allocate the memory needed for query_ca */

			alts_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
			CL_ASSERT(alts_ca_attr);

			ib_status = ib_query_ca(h_ca, alts_ca_attr, &bsize);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_query_ca failed with status = %d\n", ib_status) );
				ret_status = ib_status;
				goto Cleanup2;
			}

			/* Print_ca_attributes */

			alts_print_ca_attr(alts_ca_attr);

			if(modify_attr)
			{
				port_attr_mod.pkey_ctr = 10;
				port_attr_mod.qkey_ctr = alts_ca_attr->p_port_attr->qkey_ctr+10;

				ib_status = ib_modify_ca(h_ca,alts_ca_attr->p_port_attr->port_num,
					IB_CA_MOD_QKEY_CTR | IB_CA_MOD_PKEY_CTR ,
					&port_attr_mod);

				if(ib_status != IB_SUCCESS)
				{
					ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_modify_ca failed with status = %d\n", ib_status) );
					ret_status = ib_status;
				}

				ib_status = ib_query_ca(h_ca, alts_ca_attr, &bsize);

				if(ib_status != IB_SUCCESS)
				{
					ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_query_ca failed with status = %d\n", ib_status) );
					goto Cleanup2;
				}

				CL_ASSERT(port_attr_mod.pkey_ctr != \
					alts_ca_attr->p_port_attr->pkey_ctr);
				CL_ASSERT(port_attr_mod.qkey_ctr != \
					alts_ca_attr->p_port_attr->qkey_ctr);

			}

			/* Free the memory */
			cl_free(alts_ca_attr);
			alts_ca_attr = NULL;
			/* Close the current open CA */
			ib_status = ib_close_ca(h_ca, alts_ca_destroy_cb);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_close_ca failed status = %d", ib_status));
			}
			h_ca = NULL;

		}

Cleanup2:
		if(h_ca != NULL)
		{
			ib_status = ib_close_ca(h_ca, alts_ca_destroy_cb);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_close_ca failed status = %d", ib_status));
			}
		}

Cleanup1:
		ib_status = ib_close_al(h_al);

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_close_al failed status = %d", ib_status));
		}

		break;

	} //End of while(1)

	ALTS_EXIT( ALTS_DBG_VERBOSE );
	return ret_status;
}

