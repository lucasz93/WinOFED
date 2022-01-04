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
#include <complib/cl_timer.h>
#include <alts_debug.h>
#include <alts_common.h>


/* Test case PARAMETERS */

#define MEM_ALLIGN 32
#define MEM_SIZE 1024


/*
 * Function prototypes
 */


/*
 * Test Case RegisterMemRegion
 */
ib_api_status_t
al_test_register_mem(
	void
	)
{
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_al_handle_t h_al = NULL;
	ib_ca_handle_t h_ca = NULL;
	ib_pd_handle_t h_pd = NULL;

	ib_mr_create_t virt_mem;
	char *ptr = NULL, *ptr_align;
	size_t mask; 
	uint32_t	lkey;
	uint32_t	rkey;
	ib_mr_handle_t	h_mr = NULL;
	ib_mr_attr_t	alts_mr_attr;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	while(1)
	{
		/* Open AL */
		ib_status = alts_open_al(&h_al);

		if(ib_status != IB_SUCCESS)
			break;

		CL_ASSERT(h_al);

		/* Open CA */
		ib_status = alts_open_ca(h_al,&h_ca);
		if(ib_status != IB_SUCCESS)
			break;

		CL_ASSERT(h_ca);

		/*
		 * Allocate a PD here
		 */
		ib_status = ib_alloc_pd(h_ca, IB_PDT_NORMAL, NULL, &h_pd); //passing null context

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_alloc_pd failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Allocate the virtual memory which needs to be registered
		 */

		mask = MEM_ALLIGN - 1;

		ptr = cl_malloc(MEM_SIZE + MEM_ALLIGN - 1);

		CL_ASSERT(ptr);

		ptr_align = ptr;

		if(((size_t)ptr & mask) != 0)
			ptr_align = (char *)(((size_t)ptr+mask)& ~mask);

		virt_mem.vaddr = ptr_align;
		virt_mem.length = MEM_SIZE;
		virt_mem.access_ctrl = (IB_AC_LOCAL_WRITE | IB_AC_MW_BIND);

		/*
		 * Register the memory region
		 */

		ib_status = ib_reg_mem(h_pd, &virt_mem, &lkey, &rkey, &h_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_reg_mem failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Query the memory region
		 */
		ib_status = ib_query_mr(h_mr, &alts_mr_attr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_mr failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		if(alts_mr_attr.lkey !=  lkey || alts_mr_attr.rkey !=  rkey)
		{
			
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_mr failed lkey rkey different from reg\n"));
			ALTS_PRINT( ALTS_DBG_ERROR,
				("\t\t reg-lkey = %x  query-lkey %x reg-rkey%x query-rkey%x\n" ,
				alts_mr_attr.lkey , lkey , alts_mr_attr.rkey ,  rkey));
			alts_close_ca(h_ca);
			ib_status = IB_INVALID_LKEY;
			break;
			
		}

		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_query_mr passed\n"
			"\t\t lkey = %x    rkey%x query-rkey%x\n" ,
			 lkey, rkey, alts_mr_attr.rkey) );
		/*
		 * Re-register the memeory region
		 */
		virt_mem.access_ctrl |= (IB_AC_RDMA_WRITE );

		ib_status = ib_rereg_mem(h_mr,IB_MR_MOD_ACCESS,
			&virt_mem,&lkey,&rkey,NULL);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_rereg_mem failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_rereg_mr passed with status = %s\n",ib_get_err_str(ib_status)));

		/*
		 * De-register the memory region
		 */
		ib_status = ib_dereg_mr(h_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dereg_mr failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Deallocate the PD
		 */

		ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dealloc_pd failed status = %s\n",ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		break; //End of while
	}

	if ( ptr )
		cl_free ( ptr );
	/* Close AL */
	if(h_al)
		alts_close_al(h_al);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}



/*
 * Test Case RegisterVarMemRegions
 */

#define MIN_MEM_SIZE	1	// size of the first region
#define N_SIZES 			27	// number of regions, each next one is twice the size of the previous one
#define ITER_NUM		3	// each region will be re-/deregistered ITER_NUM times

ib_api_status_t
al_test_register_var_mem(
	void
	)
{
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_al_handle_t h_al = NULL;
	ib_ca_handle_t h_ca = NULL;
	ib_pd_handle_t h_pd = NULL;

	ib_mr_create_t virt_mem;
	char *ptr = NULL;
	uint32_t	lkey;
	uint32_t	rkey;
	ib_mr_handle_t	h_mr = NULL;
	ib_mr_attr_t	alts_mr_attr;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	/* Open AL */
	ib_status = alts_open_al(&h_al);

	if(ib_status != IB_SUCCESS)
		goto done;

	CL_ASSERT(h_al);

	/* Open CA */
	ib_status = alts_open_ca(h_al,&h_ca);
	if(ib_status != IB_SUCCESS)
		goto err_open_ca;

	CL_ASSERT(h_ca);

	/*
	 * Allocate a PD here
	 */
	ib_status = ib_alloc_pd(h_ca, IB_PDT_NORMAL, NULL, &h_pd); //passing null context

	if(ib_status != IB_SUCCESS) 
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_alloc_pd failed status = %s\n", ib_get_err_str(ib_status)) );
		goto err_alloc_pd;;
	}

	/*
	 * Register the memory region
	 */
	{
		#define MAX_MEM_SIZE	(MIN_MEM_SIZE << N_SIZES)	// 1GB
		#define MEM_OFFSET		1
		#define PAGE_SIZE		4096
		#define PAGE_MASK		(PAGE_SIZE - 1)
		unsigned i, j, offset;
		unsigned __int64 size;
		unsigned __int64 sizea;
		int reg_time[N_SIZES], dereg_time[N_SIZES];
		int reg_tries[N_SIZES], dereg_tries[N_SIZES];
		unsigned __int64  Start, End;

		ALTS_PRINT( ALTS_DBG_ERROR, ("***** min_size %#x, max_size %#x, n_sizes %d \n",
			MIN_MEM_SIZE, MAX_MEM_SIZE, N_SIZES ));
		

		for (size = MIN_MEM_SIZE, j=0; size < MAX_MEM_SIZE; size <<= 1, ++j) 
		{

			/* Allocate the virtual memory which needs to be registered */
			sizea = size + MEM_OFFSET - 1;
			ptr = cl_malloc((size_t)sizea);
			if (!ptr) {
				ALTS_PRINT( ALTS_DBG_ERROR,
					("cl_malloc failed on %#x bytes\n", sizea) );
				continue;
			}
			offset = (int)((ULONG_PTR)ptr & PAGE_MASK);
			virt_mem.vaddr = ptr - offset;
			virt_mem.length = sizea + offset;
			virt_mem.access_ctrl = IB_AC_LOCAL_WRITE;
			
			reg_time[j] =dereg_time[j] =reg_tries[j] =dereg_tries[j] =0;
			for (i=0; i<ITER_NUM; ++i) {

				/* Register the memory region */
				Start = cl_get_time_stamp();
				ib_status = ib_reg_mem(h_pd, &virt_mem, &lkey, &rkey, &h_mr);
				End = cl_get_time_stamp();
				if(ib_status != IB_SUCCESS) {
					ALTS_PRINT( ALTS_DBG_ERROR,
						("ib_reg_mem failed status = %s on size %#x\n", 
						ib_get_err_str(ib_status), virt_mem.length) );
					break;
				}
				else {
				#if 0
					ALTS_PRINT( ALTS_DBG_ERROR, ("ib_reg_mr: sz %#x in %d usec \n",
						virt_mem.length, (int) (End - Start)));
				#endif
					reg_time[j] += (int) (End - Start);
					++reg_tries[j];
				}

				/* De-register the memory region */
				Start = cl_get_time_stamp();
				ib_status = ib_dereg_mr(h_mr);
				End = cl_get_time_stamp();
				if(ib_status != IB_SUCCESS) {
					ALTS_PRINT( ALTS_DBG_ERROR,
						("ib_dereg_mr failed status = %s\n", 
						ib_get_err_str(ib_status)) );
				}
				else {
				#if 0
					ALTS_PRINT( ALTS_DBG_ERROR, ("ib_dereg_mr: sz %#x in %d usec \n",
						virt_mem.length, (int) (End - Start)));
				#endif
					dereg_time[j] += (int) (End - Start);
					++dereg_tries[j];
				}
			}

			if ( ptr )
				cl_free ( ptr );
		}

		/* results */
		for (size = MIN_MEM_SIZE, j=0; j<N_SIZES; size <<= 1, ++j) 
		{
			ALTS_PRINT( ALTS_DBG_ERROR, ("sz %#x \treg %d \tdereg %d \t \n",
				size, 
				(reg_tries[j]) ? reg_time[j]/reg_tries[j] : 0,
				(dereg_tries[j]) ? dereg_time[j]/dereg_tries[j] : 0
				));
		}
		
	}

	/*
	 * Deallocate the PD
	 */

	ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
	if(ib_status != IB_SUCCESS) 
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_dealloc_pd failed status = %s\n",ib_get_err_str(ib_status)) );
	}

err_alloc_pd:
	alts_close_ca(h_ca);
err_open_ca:
	/* Close AL */
	if(h_al)
		alts_close_al(h_al);
done:
	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}


/*
 * Test Case RegisterPhyMemRegion
 */

#ifdef CL_KERNEL

ib_api_status_t
al_test_register_phys_mem(
	void
	)
{
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_al_handle_t h_al = NULL;
	ib_ca_handle_t h_ca = NULL;
	ib_pd_handle_t h_pd = NULL;

	ib_phys_range_t	phys_range;
	ib_phys_create_t phys_mem;
	uint64_t		reg_va;
	void			*virt_addr = NULL;
	uint32_t		lkey;
	uint32_t		rkey;
	ib_mr_handle_t	h_mr = NULL;
	ib_mr_attr_t	alts_mr_attr;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	do
	{
		/* Open AL */
		ib_status = alts_open_al(&h_al);

		if(ib_status != IB_SUCCESS)
			break;

		CL_ASSERT(h_al);

		/* Open CA */
		ib_status = alts_open_ca(h_al,&h_ca);
		if(ib_status != IB_SUCCESS)
			break;

		CL_ASSERT(h_ca);

		/*
		 * Allocate a PD here
		 */
		ib_status = ib_alloc_pd(h_ca, IB_PDT_NORMAL, NULL, &h_pd); //passing null context

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_alloc_pd failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Allocate the virtual memory which needs to be registered
		 */

		virt_addr = cl_zalloc( cl_get_pagesize() );
		phys_range.base_addr = cl_get_physaddr( virt_addr );
		phys_range.size = PAGE_SIZE;
		phys_mem.length = PAGE_SIZE;
		phys_mem.num_ranges = 1;
		phys_mem.range_array = &phys_range;
		phys_mem.buf_offset = 0;
		phys_mem.hca_page_size = cl_get_pagesize();
		phys_mem.access_ctrl = (IB_AC_LOCAL_WRITE | IB_AC_RDMA_READ);
		reg_va = (uintn_t)virt_addr;

		/*
		 * Register the memory region
		 */
		ib_status = ib_reg_phys(h_pd, &phys_mem, &reg_va, &lkey, &rkey, &h_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_reg_phys failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Query the memory region
		 */
		ib_status = ib_query_mr(h_mr, &alts_mr_attr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_mr failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Re-register the memeory region
		 */
		phys_mem.access_ctrl |= (IB_AC_RDMA_WRITE );

		ib_status = ib_rereg_phys(h_mr,IB_MR_MOD_ACCESS,
			&phys_mem, &reg_va, &lkey,&rkey,NULL);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_rereg_phys failed status = %s\n", ib_get_err_str(ib_status)) );
		}

		ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_rereg_phys passed with status = %s\n",ib_get_err_str(ib_status)));

		/*
		 * De-register the memory region
		 */
		ib_status = ib_dereg_mr(h_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dereg_mr failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Deallocate the PD
		 */

		ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dealloc_pd failed status = %s\n",ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

	} while (0);

	if ( virt_addr )
	{
		cl_free ( virt_addr );
	}
	/* Close AL */
	if(h_al)
		alts_close_al(h_al);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
#endif

/*
 * Test Case RegisterSharedMemRegion
 */

ib_api_status_t
al_test_register_shared_mem(
	void
	)
{
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_al_handle_t h_al = NULL;
	ib_ca_handle_t h_ca = NULL;
	ib_pd_handle_t h_pd = NULL;

	ib_mr_create_t virt_mem;
	uint64_t virt_ptr = 0;
	char *ptr = NULL, *ptr_align;
	size_t mask; 
	uint32_t	lkey;
	uint32_t	rkey;
	ib_mr_handle_t	h_base_mr = NULL;
	ib_mr_handle_t	h_mr = NULL;
	ib_mr_attr_t	alts_mr_attr;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	do
	{
		/* Open AL */
		ib_status = alts_open_al(&h_al);

		if(ib_status != IB_SUCCESS)
			break;

		CL_ASSERT(h_al);

		/* Open CA */
		ib_status = alts_open_ca(h_al,&h_ca);
		if(ib_status != IB_SUCCESS)
			break;

		CL_ASSERT(h_ca);

		/*
		 * Allocate a PD here
		 */
		ib_status = ib_alloc_pd(h_ca, IB_PDT_NORMAL, NULL, &h_pd); //passing null context

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_alloc_pd failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Allocate the virtual memory which needs to be registered
		 */

		mask = MEM_ALLIGN - 1;

		ptr = cl_malloc(MEM_SIZE + MEM_ALLIGN - 1);

		CL_ASSERT(ptr);

		ptr_align = ptr;

		if(((size_t)ptr & mask) != 0)
			ptr_align = (char *)(((size_t)ptr+mask)& ~mask);

		virt_mem.vaddr = ptr_align;
		virt_mem.length = MEM_SIZE;
		virt_mem.access_ctrl = (IB_AC_LOCAL_WRITE | IB_AC_MW_BIND);

		/*
		 * Register the memory region
		 */

		ib_status = ib_reg_mem(h_pd, &virt_mem, &lkey, &rkey, &h_base_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_reg_mem failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Register the same region as shared
		 */
		virt_ptr = (uintn_t)ptr_align;
		ib_status = ib_reg_shared(h_base_mr, h_pd, virt_mem.access_ctrl, &virt_ptr, &lkey, &rkey, &h_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_reg_shared failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * De-register the base memory region
		 */
		ib_status = ib_dereg_mr(h_base_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dereg_mr (base) failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Query the memory region
		 */
		ib_status = ib_query_mr(h_mr, &alts_mr_attr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_mr failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Re-register the memeory region
		 */
		virt_mem.access_ctrl |= (IB_AC_RDMA_WRITE );

		ib_status = ib_rereg_mem(h_mr,IB_MR_MOD_ACCESS,
			&virt_mem,&lkey,&rkey,NULL);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_rereg_shared failed status = %s\n", ib_get_err_str(ib_status)) );
		}

		ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_rereg_mr passed with status = %s\n",ib_get_err_str(ib_status)));

		/*
		 * De-register the shared memory region
		 */
		ib_status = ib_dereg_mr(h_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dereg_mr (shared) failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Deallocate the PD
		 */

		ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dealloc_pd failed status = %s\n",ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

	} while(0);

	if( ptr )
		cl_free( ptr );

	/* Close AL */
	if(h_al)
		alts_close_al(h_al);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
