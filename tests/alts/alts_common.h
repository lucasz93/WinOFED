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
#include <stdio.h>

#if !defined(__ALTS_COMMON_H__)
#define __ALTS_COMMON_H__


typedef struct _mem_region
{
	void		*buffer;
	struct _ib_mr* mr_h;
	uint32_t	lkey;
	uint32_t	rkey;

	uint16_t	my_lid;

} mem_region_t;


#ifndef __MODULE__
#define __MODULE__	"ALTS:"
#endif


typedef struct cmd_line_struct_t_
{
	/*
	 * TBD
	 */
	uint32_t	pgm_to_run;
	boolean_t um;
	uint32_t iteration;
	ib_api_status_t status;

} cmd_line_arg_t;


/*
 * Device Name of this driver
 */
#define ALTS_DEVICE_NAME "/dev/al_test"

/*
 * Define a magic number
 */
#define ALTS_DEV_KEY 'T'

#define ALTS_MAX_CA 4

/*
 * List all the supported test cases here.
 */
typedef enum alts_dev_ops
{
	OpenClose = 1,
	QueryCAAttribute,
	ModifyCAAttribute,
	AllocDeallocPD,
	CreateDestroyAV,
	QueryAndModifyAV,
	CreateDestroyQP,
	QueryAndModifyQP,
	CreateAndDestroyCQ,
	QueryAndModifyCQ,
	AttachMultiCast,
	RegisterMemRegion,
	RegisterVarMemRegions,
	ReregisterHca,
	RegisterPhyMemRegion,
	CreateMemWindow,
	RegisterSharedMemRegion,
	MultiSend,
	RegisterPnP,
	MadTests,
	MadQuery,
	CmTests,
	MaxTestCase
} alts_dev_ops_t;

/*
 * Define all the IOCTL CMD CODES Here
 */
#define ALTS_OpenClose						\
					IOCTL_CODE(ALDEV_KEY, OpenClose)
#define ALTS_QueryCAAttribute					\
					IOCTL_CODE(ALDEV_KEY, QueryCAAttribute)
#define ALTS_ModifyCAAttribute					\
					IOCTL_CODE(ALDEV_KEY, ModifyCAAttribute)
#define ALTS_AllocDeallocPD					\
					IOCTL_CODE(ALDEV_KEY, AllocDeallocPD)
#define ALTS_AllocDeallocRDD					\
					IOCTL_CODE(ALDEV_KEY, AllocDeallocRDD)
#define ALTS_CreateDestroyAV					\
					IOCTL_CODE(ALDEV_KEY, CreateDestroyAV)
#define ALTS_QueryAndModifyAV					\
					IOCTL_CODE(ALDEV_KEY, QueryAndModifyAV)
#define ALTS_CreateDestroyQP					\
					IOCTL_CODE(ALDEV_KEY, CreateDestroyQP)
#define ALTS_QueryAndModifyQP					\
					IOCTL_CODE(ALDEV_KEY, QueryAndModifyQP)
#define ALTS_CreateAndDestroyCQ					\
					IOCTL_CODE(ALDEV_KEY, CreateAndDestroyCQ)
#define ALTS_QueryAndModifyCQ					\
					IOCTL_CODE(ALDEV_KEY, QueryAndModifyCQ)
#define ALTS_CreateAndDestroyEEC				\
					IOCTL_CODE(ALDEV_KEY, CreateAndDestroyEEC)
#define ALTS_QueryAndModifyEEC				\
					IOCTL_CODE(ALDEV_KEY, QueryAndModifyEEC)
#define ALTS_AttachMultiCast				\
					IOCTL_CODE(ALDEV_KEY, AttachMultiCast)
#define ALTS_RegisterMemRegion				\
					IOCTL_CODE(ALDEV_KEY, RegisterMemRegion)
#define ALTS_RegisterPhyMemRegion					\
					IOCTL_CODE(ALDEV_KEY, RegisterPhyMemRegion)
#define ALTS_CreateMemWindow				\
					IOCTL_CODE(ALDEV_KEY, CreateMemWindow)
#define ALTS_RegisterSharedMemRegion					\
					IOCTL_CODE(ALDEV_KEY, RegisterSharedMemRegion)
#define ALTS_MultiSend						\
					IOCTL_CODE(ALDEV_KEY, MultiSend)
#define ALTS_MadTests						\
					IOCTL_CODE(ALDEV_KEY, MadTests)

#define ALTS_CmTests						\
					IOCTL_CODE(ALDEV_KEY, CmTests)


#define ALTS_CQ_SIZE 0x50


/*
 * Function Prototypes for the above test cases
 */
ib_api_status_t
al_test_openclose( void );

ib_api_status_t
al_test_modifycaattr( void );

ib_api_status_t
al_test_querycaattr( void );

ib_api_status_t
al_test_alloc_dealloc_pd( void );

ib_api_status_t
al_test_alloc_dealloc_rdd( void );

ib_api_status_t
al_test_create_destroy_av( void );

ib_api_status_t
al_test_query_modify_av( void );

ib_api_status_t
al_test_create_destroy_cq( void );

ib_api_status_t
al_test_query_modify_cq( void );


ib_api_status_t
al_test_create_destroy_qp( void );

ib_api_status_t
al_test_query_modify_qp( void );


ib_api_status_t
al_test_create_destroy_eec( void );

ib_api_status_t
al_test_query_modify_eec( void );

ib_api_status_t
al_test_register_mem( void );

ib_api_status_t
al_test_create_mem_window( void );

ib_api_status_t
al_test_register_var_mem( void );

ib_api_status_t
al_test_multi_send_recv( void );

ib_api_status_t
al_test_register_pnp( void );

ib_api_status_t
al_test_mad( void );

ib_api_status_t
al_test_query( void );

ib_api_status_t
al_test_cm( void );

ib_api_status_t
al_test_register_phys_mem( void );

ib_api_status_t
al_test_register_shared_mem( void );


/*
 * Misc function prototypes
 */

ib_api_status_t
alts_open_al( ib_al_handle_t	*ph_al );

ib_api_status_t
alts_close_al( ib_al_handle_t	ph_al );

ib_api_status_t
alts_open_ca(
	IN ib_al_handle_t	h_al,
	OUT ib_ca_handle_t	*p_alts_ca_h
	);

ib_api_status_t
alts_close_ca( IN ib_ca_handle_t alts_ca_h );

void
alts_ca_err_cb( ib_async_event_rec_t	*p_err_rec);

void
alts_ca_destroy_cb( void *context);

void
alts_pd_destroy_cb( void *context );

void
alts_print_ca_attr( ib_ca_attr_t *alts_ca_attr );


void
alts_qp_err_cb(
	ib_async_event_rec_t				*p_err_rec );

void
alts_qp_destroy_cb(
	void	*context );



void
alts_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

void
alts_cq_err_cb(
	ib_async_event_rec_t				*p_err_rec );

void
alts_cq_destroy_cb(
	void	*context );




#endif // __ALTS_COMMON_H__
