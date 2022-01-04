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
 *	This header file defines data structures for the user-mode proxy
 *	and UAL support
 *
 * Environment:
 *	Kernel and User Mode.
 */


#ifndef _ALDEV_H_
#define _ALDEV_H_

#include <complib/comp_lib.h>
#include <complib/cl_waitobj.h>
#include <complib/cl_ioctl.h>
#include <iba/ib_al.h>
#include <iba/ib_al_ioctl.h>
#include <iba/ib_at_ioctl.h>

#include "al_common.h"


#define AL_DEVICE_NAME	L"\\Device\\ibal"
#define	ALDEV_KEY		(0x3B)	/* Matches FILE_DEVICE_INFINIBAND from wdm.h */

#define AL_IOCTL_VERSION			(13)

/* max number of devices with non-default pkey */
#define	MAX_NUM_PKEY	16
/* defined in several places */
#define	MAX_USER_NAME_SIZE	17

typedef enum port_type {
	PORT_TYPE_IPOIB,
	PORT_TYPE_EOIB,
} port_type_t;

typedef struct _pkey_array
{
	ib_net64_t		port_guid;
	ib_net16_t		pkey_num;
	uint64_t		UniqueId[MAX_NUM_PKEY];
	uint32_t		Location[MAX_NUM_PKEY];
	
	port_type_t		port_type[MAX_NUM_PKEY];
	ib_net16_t		pkey_array[MAX_NUM_PKEY];
	char			name[MAX_NUM_PKEY][MAX_USER_NAME_SIZE];
}	pkey_array_t;



cl_status_t port_mgr_pkey_add(pkey_array_t *pkeys);
cl_status_t port_mgr_pkey_rem(pkey_array_t *pkeys);


#ifdef	CL_KERNEL

/* Function prototypes for al device framework */

AL_EXPORT cl_status_t AL_API
al_dev_open(
	IN				cl_ioctl_handle_t			h_ioctl );

AL_EXPORT cl_status_t AL_API
al_dev_close(
	IN				cl_ioctl_handle_t			h_ioctl );

AL_EXPORT cl_status_t AL_API
al_dev_ioctl(
	IN				cl_ioctl_handle_t			h_ioctl );

cl_status_t
bus_add_pkey(
	IN				cl_ioctl_handle_t			h_ioctl );
cl_status_t
bus_rem_pkey(
	IN				cl_ioctl_handle_t			h_ioctl );

void
cleanup_cb_misc_list(
	IN		const	ib_al_handle_t				h_al,
	IN				al_obj_t					*p_obj);

/* Define data structures for user-mode proxy */
#else		/* CL_KERNEL */


/* Prototype for the ioctl support function */
cl_status_t
do_al_dev_ioctl(
	IN				uint32_t					command,
	IN				void						*p_buf,
	IN				uintn_t						buf_size,
	IN				void						*p_out_buf,
	IN				uintn_t						out_buf_size,
		OUT			uintn_t						*p_bytes_ret );


#endif		/* CL_KERNEL */

/*
 * Shared between user and kernel mode.
 */


/****d* AL device Helper/al_proxy_ops_t
* NAME
*	al_proxy_ops_t
*
* DESCRIPTION
*
*	Define the enumeration for UAL/proxy excluding AL specific
*	These are intended to be the support ioctls such as the
*	notification from the proxy to the UAL in user-mode
*
* SYNOPSIS
*/
typedef enum al_proxy_ops
{
	al_proxy_ops_start = 0,

	ual_get_comp_cb_info = al_proxy_ops_start+1,
	ual_get_misc_cb_info,
	ual_bind,
	ual_bind_sa,
	ual_bind_pnp,
	ual_bind_misc,
	ual_bind_cm,
	ual_bind_cq,
	ual_bind_destroy,
	ual_bind_nd,
	al_proxy_maxops

}	al_proxy_ops_t;
/**********/
/*
 * Various Opration Allowable on the System Helper
 */

/* IOCTL to specify what notification wait objects are used by UAL
 * for asynchronous event notifications from proxy
 */

#define	UAL_GET_COMP_CB_INFO	IOCTL_CODE(ALDEV_KEY, ual_get_comp_cb_info)
#define	UAL_GET_MISC_CB_INFO	IOCTL_CODE(ALDEV_KEY, ual_get_misc_cb_info)
#define UAL_BIND			IOCTL_CODE(ALDEV_KEY, ual_bind)
#define UAL_BIND_SA			IOCTL_CODE(ALDEV_KEY, ual_bind_sa)
#define UAL_BIND_PNP		IOCTL_CODE(ALDEV_KEY, ual_bind_pnp)
#define UAL_BIND_MISC		IOCTL_CODE(ALDEV_KEY, ual_bind_misc)
#define UAL_BIND_CM			IOCTL_CODE(ALDEV_KEY, ual_bind_cm)
#define UAL_BIND_CQ			IOCTL_CODE(ALDEV_KEY, ual_bind_cq)
#define UAL_BIND_DESTROY	IOCTL_CODE(ALDEV_KEY, ual_bind_destroy)
#define UAL_BIND_ND			IOCTL_CODE(ALDEV_KEY, ual_bind_nd)

#define AL_PROXY_OPS_START	IOCTL_CODE(ALDEV_KEY, al_proxy_ops_start)
#define AL_PROXY_MAXOPS		IOCTL_CODE(ALDEV_KEY, al_proxy_maxops)

#define IS_AL_PROXY_IOCTL(cmd)	\
			((cmd) > AL_PROXY_OPS_START && (cmd) < AL_PROXY_MAXOPS)



/****d* AL device Helper/al_dev_ops_t
* NAME
*	al_dev_ops_t
*
* DESCRIPTION
*	AL device supports the following ioctls
*		1. those meant strictly for the consumption of the proxy
*		2. those meant to denote an AL api and hence the call
*		is further dispatched AL api and/or KAL internal interface
*
* SYNOPSIS
*/
/* All verbs related ioctls */
typedef enum _al_verbs_ops
{
	al_verbs_ops_start = al_proxy_maxops,

	ual_get_uvp_name_cmd = al_verbs_ops_start + 1,
	ual_open_ca_ioctl_cmd,
	ual_query_ca_ioctl_cmd,
	ual_modify_ca_ioctl_cmd,
	ual_close_ca_ioctl_cmd,
	ual_ci_call_ioctl_cmd,
	ual_alloc_pd_ioctl_cmd,
	ual_dealloc_pd_ioctl_cmd,
	ual_create_av_ioctl_cmd,
	ual_query_av_ioctl_cmd,
	ual_modify_av_ioctl_cmd,
	ual_destroy_av_ioctl_cmd,
	ual_create_srq_ioctl_cmd,
	ual_query_srq_ioctl_cmd,
	ual_modify_srq_ioctl_cmd,
	ual_destroy_srq_ioctl_cmd,
	ual_create_qp_ioctl_cmd,
	ual_query_qp_ioctl_cmd,
	ual_modify_qp_ioctl_cmd,
	ual_destroy_qp_ioctl_cmd,
	ual_create_cq_ioctl_cmd,
	ual_query_cq_ioctl_cmd,
	ual_modify_cq_ioctl_cmd,
	ual_destroy_cq_ioctl_cmd,
	ual_reg_mr_ioctl_cmd,
	ual_query_mr_ioctl_cmd,
	ual_rereg_mem_ioctl_cmd,
	ual_reg_shared_ioctl_cmd,
	ual_dereg_mr_ioctl_cmd,
	ual_create_mw_ioctl_cmd,
	ual_query_mw_ioctl_cmd,
	ual_bind_mw_ioctl_cmd,
	ual_destroy_mw_ioctl_cmd,
	ual_post_send_ioctl_cmd,
	ual_post_recv_ioctl_cmd,
	ual_post_srq_recv_ioctl_cmd,
	ual_peek_cq_ioctl_cmd,
	ual_poll_cq_ioctl_cmd,
	ual_rearm_cq_ioctl_cmd,
	ual_rearm_n_cq_ioctl_cmd,
	ual_attach_mcast_ioctl_cmd,
	ual_detach_mcast_ioctl_cmd,
	ual_get_spl_qp_cmd,

	al_verbs_maxops

}	al_verbs_ops_t;

#define AL_VERBS_OPS_START	IOCTL_CODE(ALDEV_KEY, al_verbs_ops_start)
#define AL_VERBS_MAXOPS		IOCTL_CODE(ALDEV_KEY, al_verbs_maxops)
#define IS_VERBS_IOCTL(cmd)		\
	((cmd) > AL_VERBS_OPS_START && (cmd) < AL_VERBS_MAXOPS)

/* All subnet management related ioctls */

typedef enum _al_subnet_ops
{
	al_subnet_ops_start = al_verbs_maxops,

	ual_reg_svc_cmd = al_subnet_ops_start + 1,
	ual_dereg_svc_cmd,
	ual_send_sa_req_cmd,
	ual_cancel_sa_req_cmd,
	ual_mad_send_cmd,
	ual_mad_recv_cmd,
	ual_init_dgram_svc_cmd,
	ual_reg_mad_svc_cmd,
	ual_dereg_mad_svc_cmd,
	ual_reg_mad_pool_cmd,
	ual_dereg_mad_pool_cmd,
	ual_cancel_mad_cmd,
	ual_mad_recv_comp_cmd,
	ual_local_mad_cmd,
	
	al_subnet_maxops

}	al_subnet_ops_t;

#define AL_SUBNET_OPS_START	IOCTL_CODE(ALDEV_KEY, al_subnet_ops_start)
#define AL_SUBNET_MAXOPS	IOCTL_CODE(ALDEV_KEY, al_subnet_maxops)
#define IS_SUBNET_IOCTL(cmd)	\
	((cmd) > AL_SUBNET_OPS_START && (cmd) < AL_SUBNET_MAXOPS)

/* All ioc related ioctls */

typedef enum _al_ioc_ops
{
	al_ioc_ops_start = al_subnet_maxops,

	ual_create_ioc_cmd = al_ioc_ops_start + 1,
	ual_destroy_ioc_cmd,
	ual_reg_ioc_cmd,
	ual_reject_ioc_cmd,
	ual_add_svc_entry_cmd,
	ual_remove_svc_entry_cmd,
	ual_req_create_pdo,
	ual_req_remove_pdo,
	al_ioc_maxops,

}	al_ioc_ops_t;

#define AL_IOC_OPS_START	IOCTL_CODE(ALDEV_KEY, al_ioc_ops_start)
#define AL_IOC_MAXOPS		IOCTL_CODE(ALDEV_KEY, al_ioc_maxops)
#define IS_IOC_IOCTL(cmd)		\
	((cmd) > AL_IOC_OPS_START && (cmd) < AL_IOC_MAXOPS)

typedef enum _al_cm_sidr_ops
{
	al_cm_ops_start = al_ioc_maxops,
	ual_cm_req_cmd = al_cm_ops_start + 1,
	ual_cm_rep_cmd,
	ual_cm_dreq_cmd,
	ual_cm_drep_cmd,
	ual_cm_listen_cmd,
	ual_cm_cancel_cmd,
	ual_cm_rtu_cmd,
	ual_cm_rej_cmd,
	ual_cm_handoff_cmd,
	ual_cm_mra_cmd,
	ual_cm_lap_cmd,
	ual_cm_apr_cmd,
	ual_force_apm_cmd,
	ual_reg_sidr_cmd,
	ual_sidr_req_cmd,
	ual_sidr_rep_cmd,

	al_cm_maxops

} al_cm_sidr_ops_t;

#define AL_CM_OPS_START		IOCTL_CODE(ALDEV_KEY, al_cm_ops_start)
#define AL_CM_MAXOPS		IOCTL_CODE(ALDEV_KEY, al_cm_maxops)
#define IS_CM_IOCTL(cmd)		\
	((cmd) > AL_CM_OPS_START && (cmd) < AL_CM_MAXOPS)


typedef enum _ual_cep_ops
{
	al_cep_ops_start = al_ioc_maxops,
	ual_create_cep,
	ual_destroy_cep,
	ual_cep_listen,
	ual_cep_pre_req,
	ual_cep_send_req,
	ual_cep_pre_rep,
	ual_cep_send_rep,
	ual_cep_get_rtr,
	ual_cep_get_rts,
	ual_cep_rtu,
	ual_cep_rej,
	ual_cep_mra,
	ual_cep_lap,
	ual_cep_pre_apr,
	ual_cep_send_apr,
	ual_cep_dreq,
	ual_cep_drep,
	ual_cep_get_timewait,
	ual_cep_get_event,
	ual_cep_poll,
	ual_cep_get_pdata,

	al_cep_maxops

} ual_cep_ops_t;

#define UAL_CEP_OPS_START	IOCTL_CODE(ALDEV_KEY, al_cep_ops_start)
#define UAL_CEP_MAXOPS		IOCTL_CODE(ALDEV_KEY, al_cep_maxops)
#define IS_CEP_IOCTL(cmd)		\
	((cmd) > UAL_CEP_OPS_START && (cmd) < UAL_CEP_MAXOPS)


/* AL ioctls */

typedef enum _al_dev_ops
{
	al_ops_start = al_cep_maxops,

	ual_reg_shmid_cmd,
	ual_get_ca_attr,
	ual_reg_pnp_cmd,
	ual_poll_pnp_cmd,
	ual_rearm_pnp_cmd,
	ual_dereg_pnp_cmd,
	
	ual_access_flash,

	al_maxops

}	al_dev_ops_t;

#define AL_OPS_START			IOCTL_CODE(ALDEV_KEY, al_ops_start)
#define AL_MAXOPS				IOCTL_CODE(ALDEV_KEY, al_maxops)

#define IS_AL_IOCTL(cmd)		\
	((cmd) > AL_OPS_START && (cmd) < AL_MAXOPS)

/* NDI ioctls */

typedef enum _al_ndi_ops
{
	al_ndi_ops_start = al_maxops,

	ual_ndi_create_cq_ioctl_cmd,
	ual_ndi_notify_cq_ioctl_cmd,
	ual_ndi_cancel_cq_ioctl_cmd,
	ual_ndi_modify_qp_ioctl_cmd,
	ual_ndi_req_cm_ioctl_cmd,
	ual_ndi_rep_cm_ioctl_cmd,
	ual_ndi_rtu_cm_ioctl_cmd,
	ual_ndi_rej_cm_ioctl_cmd,
	ual_ndi_dreq_cm_ioctl_cmd,
    ual_ndi_noop,
    ual_ndi_notify_dreq_cmd,
	ual_ndi_cancel_cm_irps,
	ual_ndi_listen_cm_cmd,
	ual_ndi_get_req_cm_cmd,

	al_ndi_maxops

}	al_ndi_ops_t;

typedef enum _al_ioc_device_config
{
	al_ioc_device_config_start = al_ndi_maxops,
	ual_ioc_device_create,
	ual_ioc_list,
	al_ioc_device_config_maxops

}	al_ioc_device_config_t;

#define AL_NDI_OPS_START			IOCTL_CODE(ALDEV_KEY, al_ndi_ops_start)
#define AL_NDI_MAXOPS				IOCTL_CODE(ALDEV_KEY, al_ndi_maxops)

#define IS_NDI_IOCTL(cmd)		\
	((cmd) > AL_NDI_OPS_START && (cmd) < AL_NDI_MAXOPS)

// Note that the IOCTL definitions come from ib_at_ioctl.h
#define IS_IBAT_IOCTL(cmd)      \
    ((cmd) >= IOCTL_IBAT_IP_ADDRESSES && ((cmd) <= IOCTL_IBAT_QUERY_PATH))

/* NDI Related ioctl commands */
#define UAL_NDI_CREATE_CQ		IOCTL_CODE(ALDEV_KEY, ual_ndi_create_cq_ioctl_cmd)
#define UAL_NDI_NOTIFY_CQ		IOCTL_CODE(ALDEV_KEY, ual_ndi_notify_cq_ioctl_cmd)
#define UAL_NDI_CANCEL_CQ		IOCTL_CODE(ALDEV_KEY, ual_ndi_cancel_cq_ioctl_cmd)
#define UAL_NDI_MODIFY_QP		IOCTL_CODE(ALDEV_KEY, ual_ndi_modify_qp_ioctl_cmd)
#define UAL_NDI_REQ_CM			IOCTL_CODE(ALDEV_KEY, ual_ndi_req_cm_ioctl_cmd)
#define UAL_NDI_REP_CM			IOCTL_CODE(ALDEV_KEY, ual_ndi_rep_cm_ioctl_cmd)
#define UAL_NDI_RTU_CM			IOCTL_CODE(ALDEV_KEY, ual_ndi_rtu_cm_ioctl_cmd)
#define UAL_NDI_REJ_CM			IOCTL_CODE(ALDEV_KEY, ual_ndi_rej_cm_ioctl_cmd)
#define UAL_NDI_DREQ_CM			IOCTL_CODE(ALDEV_KEY, ual_ndi_dreq_cm_ioctl_cmd)
#define UAL_NDI_NOOP            IOCTL_CODE(ALDEV_KEY, ual_ndi_noop)
#define UAL_NDI_NOTIFY_DREQ     IOCTL_CODE(ALDEV_KEY, ual_ndi_notify_dreq_cmd)
#define UAL_NDI_CANCEL_CM_IRPS	IOCTL_CODE(ALDEV_KEY, ual_ndi_cancel_cm_irps)
#define UAL_NDI_LISTEN_CM		IOCTL_CODE(ALDEV_KEY, ual_ndi_listen_cm_cmd)
#define UAL_NDI_GET_REQ_CM		IOCTL_CODE(ALDEV_KEY, ual_ndi_get_req_cm_cmd)

/*
 * Various Operation Allowable on the System Helper
 */

#define UAL_REG_SHMID		IOCTL_CODE(ALDEV_KEY, ual_reg_shmid_cmd)
#define UAL_GET_VENDOR_LIBCFG IOCTL_CODE(ALDEV_KEY, ual_get_uvp_name_cmd)
#define UAL_OPEN_CA			IOCTL_CODE(ALDEV_KEY, ual_open_ca_ioctl_cmd)
#define UAL_QUERY_CA		IOCTL_CODE(ALDEV_KEY, ual_query_ca_ioctl_cmd)
#define UAL_MODIFY_CA		IOCTL_CODE(ALDEV_KEY, ual_modify_ca_ioctl_cmd)
#define UAL_CLOSE_CA		IOCTL_CODE(ALDEV_KEY, ual_close_ca_ioctl_cmd)
#define UAL_CI_CALL			IOCTL_CODE(ALDEV_KEY, ual_ci_call_ioctl_cmd)
#define UAL_ALLOC_PD		IOCTL_CODE(ALDEV_KEY, ual_alloc_pd_ioctl_cmd)
#define UAL_DEALLOC_PD		IOCTL_CODE(ALDEV_KEY, ual_dealloc_pd_ioctl_cmd)
#define UAL_CREATE_AV		IOCTL_CODE(ALDEV_KEY, ual_create_av_ioctl_cmd)
#define UAL_QUERY_AV		IOCTL_CODE(ALDEV_KEY, ual_query_av_ioctl_cmd)
#define UAL_MODIFY_AV		IOCTL_CODE(ALDEV_KEY, ual_modify_av_ioctl_cmd)
#define UAL_DESTROY_AV		IOCTL_CODE(ALDEV_KEY, ual_destroy_av_ioctl_cmd)
#define UAL_CREATE_SRQ		IOCTL_CODE(ALDEV_KEY, ual_create_srq_ioctl_cmd)
#define UAL_QUERY_SRQ		IOCTL_CODE(ALDEV_KEY, ual_query_srq_ioctl_cmd)
#define UAL_MODIFY_SRQ		IOCTL_CODE(ALDEV_KEY, ual_modify_srq_ioctl_cmd)
#define UAL_DESTROY_SRQ	IOCTL_CODE(ALDEV_KEY, ual_destroy_srq_ioctl_cmd)
#define UAL_CREATE_QP		IOCTL_CODE(ALDEV_KEY, ual_create_qp_ioctl_cmd)
#define UAL_QUERY_QP		IOCTL_CODE(ALDEV_KEY, ual_query_qp_ioctl_cmd)
#define UAL_MODIFY_QP		IOCTL_CODE(ALDEV_KEY, ual_modify_qp_ioctl_cmd)
#define UAL_DESTROY_QP		IOCTL_CODE(ALDEV_KEY, ual_destroy_qp_ioctl_cmd)
#define UAL_CREATE_CQ		IOCTL_CODE(ALDEV_KEY, ual_create_cq_ioctl_cmd)
#define UAL_QUERY_CQ		IOCTL_CODE(ALDEV_KEY, ual_query_cq_ioctl_cmd)
#define UAL_MODIFY_CQ		IOCTL_CODE(ALDEV_KEY, ual_modify_cq_ioctl_cmd)
#define UAL_DESTROY_CQ		IOCTL_CODE(ALDEV_KEY, ual_destroy_cq_ioctl_cmd)
#define UAL_REG_MR			IOCTL_CODE(ALDEV_KEY, ual_reg_mr_ioctl_cmd)
#define UAL_QUERY_MR		IOCTL_CODE(ALDEV_KEY, ual_query_mr_ioctl_cmd)
#define UAL_MODIFY_MR		IOCTL_CODE(ALDEV_KEY, ual_rereg_mem_ioctl_cmd)
#define UAL_REG_SHARED		IOCTL_CODE(ALDEV_KEY, ual_reg_shared_ioctl_cmd)
#define UAL_DEREG_MR		IOCTL_CODE(ALDEV_KEY, ual_dereg_mr_ioctl_cmd)
#define UAL_CREATE_MW		IOCTL_CODE(ALDEV_KEY, ual_create_mw_ioctl_cmd)
#define UAL_QUERY_MW		IOCTL_CODE(ALDEV_KEY, ual_query_mw_ioctl_cmd)
#define UAL_BIND_MW			IOCTL_CODE(ALDEV_KEY, ual_bind_mw_ioctl_cmd)
#define UAL_DESTROY_MW		IOCTL_CODE(ALDEV_KEY, ual_destroy_mw_ioctl_cmd)
#define UAL_POST_SEND		IOCTL_CODE(ALDEV_KEY, ual_post_send_ioctl_cmd)
#define UAL_POST_RECV		IOCTL_CODE(ALDEV_KEY, ual_post_recv_ioctl_cmd)
#define UAL_POST_SRQ_RECV	IOCTL_CODE(ALDEV_KEY, ual_post_srq_recv_ioctl_cmd)
#define UAL_PEEK_CQ			IOCTL_CODE(ALDEV_KEY, ual_peek_cq_ioctl_cmd)
#define UAL_POLL_CQ			IOCTL_CODE(ALDEV_KEY, ual_poll_cq_ioctl_cmd)
#define UAL_REARM_CQ		IOCTL_CODE(ALDEV_KEY, ual_rearm_cq_ioctl_cmd)
#define UAL_REARM_N_CQ		IOCTL_CODE(ALDEV_KEY, ual_rearm_n_cq_ioctl_cmd)
#define UAL_ATTACH_MCAST	IOCTL_CODE(ALDEV_KEY, ual_attach_mcast_ioctl_cmd)
#define UAL_DETACH_MCAST	IOCTL_CODE(ALDEV_KEY, ual_detach_mcast_ioctl_cmd)

/* Subnet management related ioctl commands */
#define UAL_REG_SVC			IOCTL_CODE(ALDEV_KEY, ual_reg_svc_cmd)
#define UAL_DEREG_SVC		IOCTL_CODE(ALDEV_KEY, ual_dereg_svc_cmd)
#define UAL_SEND_SA_REQ		IOCTL_CODE(ALDEV_KEY, ual_send_sa_req_cmd)
#define UAL_CANCEL_SA_REQ	IOCTL_CODE(ALDEV_KEY, ual_cancel_sa_req_cmd)
#define UAL_MAD_SEND		IOCTL_CODE(ALDEV_KEY, ual_mad_send_cmd)
#define UAL_INIT_DGRM_SVC	IOCTL_CODE(ALDEV_KEY, ual_init_dgram_svc_cmd)
#define UAL_REG_MAD_SVC		IOCTL_CODE(ALDEV_KEY, ual_reg_mad_svc_cmd)
#define UAL_DEREG_MAD_SVC	IOCTL_CODE(ALDEV_KEY, ual_dereg_mad_svc_cmd)
#define UAL_REG_MAD_POOL	IOCTL_CODE(ALDEV_KEY, ual_reg_mad_pool_cmd)
#define UAL_DEREG_MAD_POOL	IOCTL_CODE(ALDEV_KEY, ual_dereg_mad_pool_cmd)
#define UAL_CANCEL_MAD		IOCTL_CODE(ALDEV_KEY, ual_cancel_mad_cmd)
#define UAL_GET_SPL_QP_ALIAS IOCTL_CODE(ALDEV_KEY, ual_get_spl_qp_cmd)
#define UAL_MAD_RECV_COMP	IOCTL_CODE(ALDEV_KEY, ual_mad_recv_comp_cmd)
#define UAL_LOCAL_MAD		IOCTL_CODE(ALDEV_KEY, ual_local_mad_cmd)

/* CM Related ioctl commands */
#define UAL_CM_LISTEN		IOCTL_CODE(ALDEV_KEY, ual_cm_listen_cmd)
#define UAL_CM_CANCEL		IOCTL_CODE(ALDEV_KEY, ual_cm_cancel_cmd)
#define UAL_CM_REQ			IOCTL_CODE(ALDEV_KEY, ual_cm_req_cmd)
#define UAL_CM_REP			IOCTL_CODE(ALDEV_KEY, ual_cm_rep_cmd)
#define UAL_CM_RTU			IOCTL_CODE(ALDEV_KEY, ual_cm_rtu_cmd)
#define UAL_CM_REJ			IOCTL_CODE(ALDEV_KEY, ual_cm_rej_cmd)
#define UAL_CM_HANDOFF		IOCTL_CODE(ALDEV_KEY, ual_cm_handoff_cmd)
#define UAL_CM_DREQ			IOCTL_CODE(ALDEV_KEY, ual_cm_dreq_cmd)
#define UAL_CM_DREP			IOCTL_CODE(ALDEV_KEY, ual_cm_drep_cmd)
#define UAL_CM_MRA			IOCTL_CODE(ALDEV_KEY, ual_cm_mra_cmd)
#define UAL_CM_LAP			IOCTL_CODE(ALDEV_KEY, ual_cm_lap_cmd)
#define UAL_CM_APR			IOCTL_CODE(ALDEV_KEY, ual_cm_apr_cmd)
#define UAL_CM_FORCE_APM	IOCTL_CODE(ALDEV_KEY, ual_force_apm_cmd)

/* CEP Related IOCTL commands */
#define UAL_CREATE_CEP		IOCTL_CODE(ALDEV_KEY, ual_create_cep)
#define UAL_DESTROY_CEP		IOCTL_CODE(ALDEV_KEY, ual_destroy_cep)
#define UAL_CEP_LISTEN		IOCTL_CODE(ALDEV_KEY, ual_cep_listen)
#define UAL_CEP_PRE_REQ		IOCTL_CODE(ALDEV_KEY, ual_cep_pre_req)
#define UAL_CEP_SEND_REQ	IOCTL_CODE(ALDEV_KEY, ual_cep_send_req)
#define UAL_CEP_PRE_REP		IOCTL_CODE(ALDEV_KEY, ual_cep_pre_rep)
#define UAL_CEP_SEND_REP	IOCTL_CODE(ALDEV_KEY, ual_cep_send_rep)
#define UAL_CEP_GET_RTR		IOCTL_CODE(ALDEV_KEY, ual_cep_get_rtr)
#define UAL_CEP_GET_RTS		IOCTL_CODE(ALDEV_KEY, ual_cep_get_rts)
#define UAL_CEP_RTU			IOCTL_CODE(ALDEV_KEY, ual_cep_rtu)
#define UAL_CEP_REJ			IOCTL_CODE(ALDEV_KEY, ual_cep_rej)
#define UAL_CEP_MRA			IOCTL_CODE(ALDEV_KEY, ual_cep_mra)
#define UAL_CEP_LAP			IOCTL_CODE(ALDEV_KEY, ual_cep_lap)
#define UAL_CEP_PRE_APR		IOCTL_CODE(ALDEV_KEY, ual_cep_pre_apr)
#define UAL_CEP_SEND_APR	IOCTL_CODE(ALDEV_KEY, ual_cep_send_apr)
#define UAL_CEP_DREQ		IOCTL_CODE(ALDEV_KEY, ual_cep_dreq)
#define UAL_CEP_DREP		IOCTL_CODE(ALDEV_KEY, ual_cep_drep)
#define UAL_CEP_GET_TIMEWAIT	IOCTL_CODE(ALDEV_KEY, ual_cep_get_timewait)
#define UAL_CEP_GET_EVENT	IOCTL_CODE(ALDEV_KEY, ual_cep_get_event)
#define UAL_CEP_POLL		IOCTL_CODE(ALDEV_KEY, ual_cep_poll)
#define UAL_CEP_GET_PDATA	IOCTL_CODE(ALDEV_KEY, ual_cep_get_pdata)


#define UAL_GET_CA_ATTR_INFO	IOCTL_CODE(ALDEV_KEY, ual_get_ca_attr)
#define UAL_REQ_CREATE_PDO		IOCTL_CODE(ALDEV_KEY, ual_req_create_pdo)
#define UAL_REQ_REMOVE_PDO		IOCTL_CODE(ALDEV_KEY, ual_req_remove_pdo)

/* PnP related ioctl commands. */
#define UAL_REG_PNP			IOCTL_CODE(ALDEV_KEY, ual_reg_pnp_cmd)
#define UAL_POLL_PNP		IOCTL_CODE(ALDEV_KEY, ual_poll_pnp_cmd)
#define UAL_REARM_PNP		IOCTL_CODE(ALDEV_KEY, ual_rearm_pnp_cmd)
#define UAL_DEREG_PNP		IOCTL_CODE(ALDEV_KEY, ual_dereg_pnp_cmd)
#define UAL_ACCESS_FLASH	IOCTL_CODE(ALDEV_KEY, ual_access_flash)

#define AL_IOC_DEVICE_CONFIG_START		IOCTL_CODE(ALDEV_KEY, al_ioc_device_config_start)
#define AL_IOC_DEVICE_CONFIG_MAXOPS		IOCTL_CODE(ALDEV_KEY, al_ioc_device_config_maxops)

#define IS_IOC_DEVICE_CONFIG_IOCTL(cmd)		\
	((cmd) > AL_IOC_DEVICE_CONFIG_START && (cmd) < AL_IOC_DEVICE_CONFIG_MAXOPS)

#define UAL_IOC_DEVICE_CREATE			IOCTL_CODE(ALDEV_KEY, ual_ioc_device_create)
#define UAL_IOC_LIST				IOCTL_CODE(ALDEV_KEY, ual_ioc_list)

#endif	/* _AL_DEV_H_ */
