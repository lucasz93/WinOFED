/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
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
#include "vnic_adapter.h"
#include "vnic_util.h"

#ifndef max_t
#define max_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })
#endif


static int inicIbInited = 0;

extern struct _vnic_globals g_vnic;

void
ib_asyncEvent(
			ib_async_event_rec_t	*pEventRecord );

static void
_ib_qpCompletion(
	IN		IbQp_t			*pQp);
static void
_ibqp_connect_cb(
	IN		ib_cm_rep_rec_t     *p_cm_rep );

static void
_ibqp_detach_cb(
	IN		ib_cm_drep_rec_t	*p_drep_rec );

static void
_ibqp_dreq_cb(
	IN		ib_cm_dreq_rec_t		*p_dreq_rec );

static void
_ibqp_mra_cb(
	IN		ib_cm_mra_rec_t			*p_mra_rec );

static void
_ibqp_rej_cb(
	IN		ib_cm_rej_rec_t		*p_rej_rec );
static
void ib_workCompletion(
		IN			ib_cq_handle_t  h_cq,
		IN			void			*cqContext );


uint8_t
ibca_findPortNum(
		struct _viport *p_viport,
		uint64_t		guid )
{
	uint8_t port;

	for (port = 0; port < p_viport->p_adapter->ca.numPorts; port++ )
	{
			if (p_viport->p_adapter->ca.portGuids[port] == guid )
			{
				return port+1;
			}
	}
	return 0;
}

ib_api_status_t
ibregion_physInit(
	IN		struct _vnic_adapter*		p_adapter,
	IN		IbRegion_t		*pRegion,
	IN		ib_pd_handle_t	hPd,
	IN		uint64_t		*p_vaddr,
	IN		uint64_t		len )
{
	ib_phys_create_t	phys_mem;
	ib_phys_range_t		phys_range;
	ib_api_status_t		ib_status = IB_SUCCESS;
	uint64_t			vaddr = 0;
	VNIC_ENTER ( VNIC_DBG_IB );

	UNUSED_PARAM( p_vaddr );

	phys_range.base_addr = *(p_vaddr);
	phys_range.size =  len;
	phys_mem.access_ctrl = ( IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE );
	phys_mem.buf_offset = 0;
	phys_mem.hca_page_size = PAGE_SIZE;
	phys_mem.length = len;
	phys_mem.num_ranges = 1;
	phys_mem.range_array = &phys_range;

	ib_status = p_adapter->ifc.reg_phys( hPd,
									&phys_mem,
									&vaddr,
									&pRegion->lkey,
									&pRegion->rkey,
									&pRegion->h_mr );

	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("ib_reg_phys failed status %s(%d)\n",
						p_adapter->ifc.get_err_str(ib_status), ib_status ) );
		pRegion->h_mr = NULL;
	}
	else
	{
		pRegion->virtAddress = vaddr;
		pRegion->len = len;
	}

	VNIC_EXIT( VNIC_DBG_IB );
	return ib_status;
}

ib_api_status_t
ibregion_init (
	IN		viport_t		*p_viport,
	IN	OUT	IbRegion_t		*pRegion,
	IN		ib_pd_handle_t	hPd,
	IN		void*			vaddr,
	IN		uint64_t		len,
	IN		ib_access_t		access_ctrl )
{
	ib_api_status_t	ib_status;
	ib_mr_create_t	create_mem;

	VNIC_ENTER ( VNIC_DBG_IB );

	create_mem.length  = len;
	create_mem.vaddr  = vaddr;
	create_mem.access_ctrl = access_ctrl;

	ib_status = p_viport->p_adapter->ifc.reg_mem( hPd,
									&create_mem,
									&pRegion->lkey,
									&pRegion->rkey,
									&pRegion->h_mr );
	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("ib_reg_mem failed status %s(%d)\n",
			p_viport->p_adapter->ifc.get_err_str( ib_status ), ib_status ));
		pRegion->h_mr = NULL;
	}
	else
	{
		pRegion->len = len;
		pRegion->virtAddress = (ULONG_PTR)vaddr;
	}
	VNIC_EXIT ( VNIC_DBG_IB );
	return ib_status;
}

void
ibregion_cleanup(
		IN		viport_t	*p_viport,
		IN		IbRegion_t	*pRegion )
{
	ib_api_status_t ib_status;

	VNIC_ENTER( VNIC_DBG_IB );

	if( pRegion->h_mr != NULL )
	{
		ib_status = p_viport->p_adapter->ifc.dereg_mr( pRegion->h_mr );
		if ( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Dereg MR failed status (%d)\n", ib_status ));
		}
		pRegion->h_mr = NULL;
	}

	VNIC_EXIT( VNIC_DBG_IB );
}


void
ibqp_construct(
	IN	OUT		IbQp_t			*pQp,
	IN			viport_t		*pViport )
{
	VNIC_ENTER ( VNIC_DBG_IB );

	ASSERT( pQp->qpState == IB_UNINITTED );

	pQp->qp      = NULL;
	pQp->cq      = NULL;
	pQp->pViport = pViport;
	pQp->pCa     = &pViport->p_adapter->ca;

	NdisAllocateSpinLock( &pQp->qpLock );

	InitializeListHead( &pQp->listPtrs);
}


ib_api_status_t
ibqp_init(
	IN	OUT		IbQp_t			*pQp,
	IN			uint64_t		guid,
	IN			IbConfig_t		*p_conf )
{
	ib_qp_create_t		attribCreate;
	ib_qp_mod_t			attribMod;
	ib_qp_attr_t		qpAttribs;

	ib_cq_create_t		cq_create;
	ib_cq_handle_t		h_cq;

	ib_api_status_t		ib_status = IB_SUCCESS;

	VNIC_ENTER ( VNIC_DBG_IB );

	if (pQp->qpState != IB_UNINITTED && pQp->qpState != IB_DETACHED )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("ibqp_init: out of state (%d)\n",	pQp->qpState) );
		return IB_ERROR;
	}

	InterlockedExchange( &pQp->qpState, IB_UNINITTED );

	pQp->p_conf = p_conf;

	cq_create.size = p_conf->numSends + p_conf->numRecvs;
	cq_create.pfn_comp_cb = ib_workCompletion;
	cq_create.h_wait_obj = NULL;

	ib_status = pQp->pViport->p_adapter->ifc.create_cq(
		pQp->pViport->p_adapter->h_ca, &cq_create, pQp, ib_asyncEvent, &h_cq );

	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Failed allocating completion queue\n") );
		goto err1;
	}

	pQp->cq = h_cq;

	cl_memclr( &attribCreate, sizeof(attribCreate) );

	attribCreate.qp_type = IB_QPT_RELIABLE_CONN;
	attribCreate.sq_depth = p_conf->numSends;
	attribCreate.rq_depth = p_conf->numRecvs;
	attribCreate.sq_sge = p_conf->sendGather;
	attribCreate.rq_sge = p_conf->recvScatter;
	attribCreate.h_sq_cq = pQp->cq;
	attribCreate.h_rq_cq = pQp->cq;
	attribCreate.sq_signaled = FALSE;

	ib_status = pQp->pViport->p_adapter->ifc.create_qp(
								pQp->pViport->p_adapter->ca.hPd, 
								&attribCreate, 
								pQp,
								ib_asyncEvent,
								&pQp->qp );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_IB, ("Create QP failed %s\n",
			pQp->pViport->p_adapter->ifc.get_err_str( ib_status )) );
		goto err2;
	}
	ib_status = pQp->pViport->p_adapter->ifc.query_qp(pQp->qp, &qpAttribs);
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR, ("Query QP failed %s\n",
			pQp->pViport->p_adapter->ifc.get_err_str( ib_status )) );
		goto err3;
	}
	pQp->qpNumber = qpAttribs.num;
	pQp->portGuid = guid;
	cl_memclr( &attribMod, sizeof(attribMod) );

	attribMod.req_state = IB_QPS_INIT;
	attribMod.state.init.primary_port = ibca_findPortNum( pQp->pViport, guid );
	attribMod.state.init.pkey_index = 0;
	attribMod.state.init.access_ctrl = IB_AC_LOCAL_WRITE;
	attribMod.state.init.access_ctrl |= (p_conf->sendGather > 1) ? IB_AC_RDMA_WRITE : 0;

	ib_status = pQp->pViport->p_adapter->ifc.modify_qp( pQp->qp, &attribMod );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_IB, ("Init QP failed %s\n",
			pQp->pViport->p_adapter->ifc.get_err_str( ib_status )) );

err3:
		pQp->pViport->p_adapter->ifc.destroy_qp( pQp->qp, NULL );
err2:
		pQp->pViport->p_adapter->ifc.destroy_cq( pQp->cq, NULL );
	}
	else
	{
		InterlockedExchange( &pQp->qpState, IB_INITTED );
	}

err1:
	VNIC_EXIT ( VNIC_DBG_IB );
	return ib_status;
}


ib_api_status_t
ibqp_connect(
	IN		IbQp_t	 *pQp)
{
	IbCa_t          *pCa;
	viport_t		*p_viport;
	IbConfig_t      *p_conf;
	ib_api_status_t  ib_status = IB_SUCCESS;
	ib_cm_req_t			conn_req;

	VNIC_ENTER( VNIC_DBG_IB );

	if ( pQp->qpState != IB_INITTED )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_IB,
			("ibqp_connect: out of state (%d)\n",pQp->qpState) );
		return IB_INVALID_STATE;
	}

	p_viport = pQp->pViport;
	pCa = pQp->pCa;
	p_conf = pQp->p_conf;

#ifdef VNIC_STATISTIC
	pQp->statistics.connectionTime = cl_get_time_stamp();
#endif

	cl_memclr( &conn_req, sizeof(conn_req));

	conn_req.h_al			= p_viport->p_adapter->h_al;
	conn_req.qp_type		= IB_QPT_RELIABLE_CONN;
	conn_req.h_qp			= pQp->qp;
	conn_req.p_req_pdata	= (uint8_t*)&pQp->p_conf->connData;
	conn_req.req_length		= sizeof( Inic_ConnectionData_t );
	conn_req.svc_id			= pQp->p_conf->sid;
	conn_req.pkey			= pQp->p_conf->pathInfo.pkey;

	conn_req.p_primary_path = &pQp->p_conf->pathInfo;

	conn_req.retry_cnt		= (uint8_t)pQp->p_conf->retryCount;
	conn_req.rnr_nak_timeout = pQp->p_conf->minRnrTimer;
	conn_req.rnr_retry_cnt	= (uint8_t)pQp->p_conf->rnrRetryCount;
	conn_req.max_cm_retries	= 5;
	conn_req.remote_resp_timeout = ib_path_rec_pkt_life( &pQp->p_conf->pathInfo ) + 1;
	conn_req.local_resp_timeout  = ib_path_rec_pkt_life( &pQp->p_conf->pathInfo ) + 1;

	conn_req.compare_length	=	0;
	conn_req.resp_res		=	0;

	conn_req.flow_ctrl		=	TRUE;
	
	conn_req.pfn_cm_req_cb	=	NULL;
	conn_req.pfn_cm_rep_cb	=	_ibqp_connect_cb;
	conn_req.pfn_cm_mra_cb	=	_ibqp_mra_cb;
	conn_req.pfn_cm_rej_cb	=	_ibqp_rej_cb;

	InterlockedExchange( &pQp->qpState, IB_ATTACHING );

	ib_status = p_viport->p_adapter->ifc.cm_req ( &conn_req );
	if ( ib_status != IB_SUCCESS && ib_status != IB_PENDING )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Connect request return status %s\n",
			p_viport->p_adapter->ifc.get_err_str( ib_status )) );
		InterlockedExchange( &pQp->qpState, IB_DETACHED );
		return ib_status;
	}

	if( cl_event_wait_on(
		&p_viport->sync_event, EVENT_NO_TIMEOUT, FALSE ) != CL_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR, ("conn event timeout!\n") );
		return IB_TIMEOUT;
	}

	if( pQp->qpState != IB_ATTACHED )
	{
		VNIC_TRACE( VNIC_DBG_ERROR, ("QP connect failed\n") );
		ib_status = IB_ERROR;
	}

	VNIC_EXIT( VNIC_DBG_IB );
	return ib_status;
}


static void
_ibqp_detach_cb(
		IN		ib_cm_drep_rec_t	*p_drep_rec )
{
	IbQp_t	*pQp = (IbQp_t *)p_drep_rec->qp_context;
	VNIC_ENTER( VNIC_DBG_IB );
	CL_ASSERT( p_drep_rec );

	InterlockedExchange( &pQp->qpState, IB_DETACHED );
	viport_failure( pQp->pViport );

	VNIC_EXIT( VNIC_DBG_IB );
}

static void
_ibqp_rej_cb(
	 IN		ib_cm_rej_rec_t		*p_rej_rec )
{
	IbQp_t	*pQp = (IbQp_t *)p_rej_rec->qp_context;
	CL_ASSERT(p_rej_rec );

	InterlockedExchange( &pQp->qpState, IB_DETACHED );
	switch ( p_rej_rec->rej_status )
	{
		case IB_REJ_USER_DEFINED:
		
			VNIC_TRACE ( VNIC_DBG_IB | VNIC_DBG_ERROR,
				("Conn req user reject status %d\nARI: %s\n",
						cl_ntoh16( p_rej_rec->rej_status ),
						p_rej_rec->p_ari ));
			break;
		default:
			VNIC_TRACE( VNIC_DBG_IB | VNIC_DBG_ERROR,
				("Conn req reject status %d\n",
						cl_ntoh16( p_rej_rec->rej_status )) );
	}

	pQp->pViport->p_adapter->hung++;
	cl_event_signal( &pQp->pViport->sync_event );
}

static void
_ibqp_mra_cb(
	 IN		ib_cm_mra_rec_t			*p_mra_rec )
{
	VNIC_ENTER( VNIC_DBG_IB );
	CL_ASSERT( p_mra_rec );
	UNREFERENCED_PARAMETER( p_mra_rec );
	VNIC_EXIT( VNIC_DBG_IB );
}

static void
_ibqp_dreq_cb(
	  IN	ib_cm_dreq_rec_t		*p_dreq_rec )
{
	ib_api_status_t	ib_status = IB_SUCCESS;
	ib_cm_drep_t	cm_drep;
	IbQp_t	*pQp	= (IbQp_t *)p_dreq_rec->qp_context;

	VNIC_ENTER( VNIC_DBG_IB );
	CL_ASSERT( p_dreq_rec );

	cm_drep.drep_length = 0;
	cm_drep.p_drep_pdata = NULL;

	ib_status = pQp->pViport->p_adapter->ifc.cm_drep(
		p_dreq_rec->h_cm_dreq, &cm_drep );
	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("dreq_cb failed status %s(%d)\n",
			pQp->pViport->p_adapter->ifc.get_err_str( ib_status ), ib_status));
		return;
	}

	InterlockedExchange (&pQp->qpState,  IB_DETACHED );
	pQp->pViport->p_adapter->hung++;
	viport_failure( pQp->pViport );

	VNIC_EXIT( VNIC_DBG_IB );
}

void
ibqp_detach(
		IN			IbQp_t		*pQp )
{
	ib_cm_dreq_t	cm_dreq;
	ib_api_status_t		ib_status = IB_SUCCESS;

	VNIC_ENTER( VNIC_DBG_IB );

	if( InterlockedCompareExchange( &pQp->qpState, 
			IB_DETACHING, IB_ATTACHED ) == IB_ATTACHED )
	{
		NdisAcquireSpinLock( &pQp->qpLock );

		cm_dreq.h_qp = pQp->qp;
		cm_dreq.qp_type = IB_QPT_RELIABLE_CONN;
		cm_dreq.p_dreq_pdata = NULL;
		cm_dreq.dreq_length = 0;
		cm_dreq.pfn_cm_drep_cb = _ibqp_detach_cb;
		ib_status = pQp->pViport->p_adapter->ifc.cm_dreq( &cm_dreq );
		if ( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				(" cm_dreq failed status %s\n",
				pQp->pViport->p_adapter->ifc.get_err_str( ib_status )));
		}

		NdisReleaseSpinLock( &pQp->qpLock );

		ib_status = pQp->pViport->p_adapter->ifc.destroy_qp( pQp->qp, NULL );
		if ( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				(" QP destroy failed status %s\n",
				pQp->pViport->p_adapter->ifc.get_err_str( ib_status )));
		}
	}

	VNIC_EXIT( VNIC_DBG_IB );
	return;
}

void
ibqp_cleanup(
		IN		IbQp_t		*pQp )
{
	ib_api_status_t		ib_status = IB_SUCCESS;
	LONG				qp_state;

	VNIC_ENTER( VNIC_DBG_IB );

	qp_state = InterlockedExchange( &pQp->qpState, IB_UNINITTED );
	if ( qp_state != IB_UNINITTED &&
		 qp_state != IB_DETACHED &&
		 qp_state != IB_DETACHING )
	{
		ib_status = pQp->pViport->p_adapter->ifc.destroy_qp( pQp->qp, NULL );
		if ( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
				("destroy_qp failed status %s\n",
				pQp->pViport->p_adapter->ifc.get_err_str( ib_status )) );
		}
	}

	VNIC_EXIT( VNIC_DBG_IB );
	return;
}

ib_api_status_t ibqp_postSend(IbQp_t *pQp, Io_t *pIo)
{
	ib_api_status_t       ib_status;

#ifdef VNIC_STATISTIC
	int64_t		postTime;
#endif /* VNIC_STATISTIC */

	VNIC_ENTER( VNIC_DBG_IB );

	NdisAcquireSpinLock( &pQp->qpLock );

	if( pQp->qpState != IB_ATTACHED )
	{
		ib_status = IB_INVALID_STATE;
		goto failure;
	}

#ifdef VNIC_STATISTIC
	pIo->time = postTime = cl_get_time_stamp();
#endif /* VNIC_STATISTIC */
	if (pIo->wrq.wr_type == WR_RDMA_WRITE )
		pIo->type = RDMA;
	else
		pIo->type = SEND;

	ib_status =
		pQp->pViport->p_adapter->ifc.post_send( pQp->qp, &pIo->wrq, NULL );

#ifdef VNIC_STATISTIC
	postTime = cl_get_time_stamp() - postTime;
#endif /* VNIC_STATISTIC */

	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR, ("ib_post_send returned %s\n",
			pQp->pViport->p_adapter->ifc.get_err_str( ib_status )) );
	}
	else
	{
#ifdef VNIC_STATISTIC
		if (pIo->wrq.wr_type == WR_RDMA_WRITE)
		{
			pQp->statistics.rdmaPostTime += postTime;
			pQp->statistics.rdmaPostIos++;
		}
		else
		{
			pQp->statistics.sendPostTime += postTime;
			pQp->statistics.sendPostIos++;
		}
#endif /* VNIC_STATISTIC */
	}

failure:
	NdisReleaseSpinLock( &pQp->qpLock );
	VNIC_EXIT( VNIC_DBG_IB );
	return ib_status;
}


ib_api_status_t
ibqp_postRecv(IbQp_t *pQp, Io_t *pIo)
{
	ib_api_status_t  ib_status = IB_SUCCESS;
#ifdef VNIC_STATISTIC
	int64_t      postTime;
#endif /* VNIC_STATISTIC */

	NdisAcquireSpinLock( &pQp->qpLock );
	/* can post recvieves before connecting queue pair */
	if( pQp->qpState != IB_INITTED && pQp->qpState != IB_ATTACHED )
	{
		NdisReleaseSpinLock( &pQp->qpLock );
		return IB_INVALID_STATE;
	}
	pIo->type = RECV;

#ifdef VNIC_STATISTIC
	postTime = cl_get_time_stamp();
	if (pIo->time != 0)
	{
		pQp->statistics.recvCompTime += postTime - pIo->time;
		pQp->statistics.recvCompIos++;
	}
#endif /* VNIC_STATISTIC */

	ib_status =
		pQp->pViport->p_adapter->ifc.post_recv(pQp->qp, &pIo->r_wrq, NULL );

#ifdef VNIC_STATISTIC
	postTime = (cl_get_time_stamp()) - postTime;
#endif /* VNIC_STATISTIC */

	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("Post Recv failed status %s(%d)\n",
			pQp->pViport->p_adapter->ifc.get_err_str(ib_status), ib_status ));

		NdisReleaseSpinLock( &pQp->qpLock );
		return ib_status;
	}
	else
	{
#ifdef VNIC_STATISTIC
		pQp->statistics.recvPostTime += postTime;
		pQp->statistics.recvPostIos++;
#endif /* VNIC_STATISTIC */
	}

	NdisReleaseSpinLock( &pQp->qpLock );
	return ib_status;
}


static void
_ibqp_connect_cb(
	IN  ib_cm_rep_rec_t     *p_cm_rep )
{
	IbQp_t				*pQp;
	ib_api_status_t		ib_status = IB_SUCCESS;

	union
	{
		ib_cm_rtu_t			cm_rtu;
		ib_cm_mra_t			cm_mra;
		ib_cm_rej_t			cm_rej;
	} u_reply;

	viport_t			*p_viport;

	VNIC_ENTER( VNIC_DBG_IB );

	pQp = (IbQp_t *)p_cm_rep->qp_context;
	p_viport = pQp->pViport;

	ASSERT( pQp->qpState == IB_ATTACHING );

	ib_status = p_viport->p_adapter->ifc.rearm_cq( pQp->cq, FALSE );
	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
					("Rearm CQ failed %s\n",
					p_viport->p_adapter->ifc.get_err_str( ib_status )) );

		cl_memclr( &u_reply.cm_rej, sizeof( u_reply.cm_rej ) );
		u_reply.cm_rej.rej_status = IB_REJ_INSUF_RESOURCES;

		p_viport->p_adapter->ifc.cm_rej( p_cm_rep->h_cm_rep, &u_reply.cm_rej );
		goto err;
	}

	cl_memclr( &u_reply.cm_rtu, sizeof( u_reply.cm_rtu ) );
	u_reply.cm_rtu.access_ctrl = IB_AC_LOCAL_WRITE;

	if( pQp->p_conf->sendGather > 1 )
		u_reply.cm_rtu.access_ctrl |= ( IB_AC_RDMA_WRITE );

	u_reply.cm_rtu.pfn_cm_dreq_cb = _ibqp_dreq_cb;

	ib_status = p_viport->p_adapter->ifc.cm_rtu ( p_cm_rep->h_cm_rep, &u_reply.cm_rtu );
	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Send RTU failed: status %#x\n", ib_status ) );
err:
		InterlockedExchange( &pQp->qpState, IB_DETACHED );
		pQp->pViport->p_adapter->hung++;
	}
	else
	{
		InterlockedExchange ( &pQp->qpState,  IB_ATTACHED );
	}

	cl_event_signal( &p_viport->sync_event );
	VNIC_EXIT( VNIC_DBG_IB );
}


#define WC_LIST_SIZE_TO_POLL 32
static void
_ib_qpCompletion(
		IN			IbQp_t		*pQp )
{
	Io_t				*pIo;
	ib_wc_t				wc[WC_LIST_SIZE_TO_POLL];
	ib_wc_t				*p_free_wc;
	ib_wc_t				*p_done_wc;
	ib_api_status_t		ib_status = IB_SUCCESS;
	int					i;

#ifdef VNIC_STATISTIC
	int64_t				compTime;
#endif /* VNIC_STATISTIC */

	VNIC_ENTER ( VNIC_DBG_IB );

	if ( pQp->qpState != IB_ATTACHING && pQp->qpState != IB_ATTACHED )
		return;

#ifdef VNIC_STATISTIC
	compTime = cl_get_time_stamp();
	pQp->statistics.numCallbacks++;
#endif /* VNIC_STATISTIC */

	for( i = 0; i < (WC_LIST_SIZE_TO_POLL - 1); i++ )
	{
		wc[i].p_next = &wc[i + 1];
	}
	wc[(WC_LIST_SIZE_TO_POLL - 1)].p_next = NULL;

	p_free_wc = wc;
	p_done_wc = NULL;

	ib_status = pQp->pViport->p_adapter->ifc.poll_cq( pQp->cq, &p_free_wc, &p_done_wc );

	if ( ib_status != IB_SUCCESS && ib_status != IB_NOT_FOUND )
	{
		VNIC_TRACE ( VNIC_DBG_ERROR,
			("ib_poll_cq failed status %d(%s)\n", ib_status,
						pQp->pViport->p_adapter->ifc.get_err_str( ib_status )) );
		return;
	}

	while ( p_done_wc )
	{
		pIo = (Io_t *)(uintn_t)p_done_wc->wr_id;

		/* keep completion status for proper ndis packet return status */
		pIo->wc_status = p_done_wc->status;

		if( p_done_wc->status != IB_WCS_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_IB,
			("Failed Completion: WcType = %d, Status = %d, Length = %d\n",
			p_done_wc->wc_type,
			p_done_wc->status,
			( p_done_wc->wc_type == IB_WC_RECV )? p_done_wc->length : 0 ) );

			if( pIo && pIo->type == RDMA )
			{
				VNIC_TRACE( VNIC_DBG_ERROR,
				("Failed RDMA Op, WC type = %d, WC status = %d IO type %d\n",
					p_done_wc->wc_type,	p_done_wc->status, pIo->type ));

				/* we must complete send packets */
				(*pIo->pRoutine)( pIo );
			}
		}
		else if(pIo)
		{
			if( pIo->pRoutine )
			{
				(*pIo->pRoutine)( pIo );
			}
		}

		p_done_wc = p_done_wc->p_next;
	}

	ib_status = pQp->pViport->p_adapter->ifc.rearm_cq( pQp->cq, FALSE );

	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("Rearm CQ failed status %d(%s)\n", ib_status,
						pQp->pViport->p_adapter->ifc.get_err_str( ib_status )) );
		viport_failure( pQp->pViport );
	}
	return;
}


static
void ib_workCompletion(
		IN			ib_cq_handle_t  h_cq,
		IN			void			*cqContext )
{
	IbQp_t           *pQp;

	VNIC_ENTER ( VNIC_DBG_IB );
	UNREFERENCED_PARAMETER( h_cq );
	pQp = (IbQp_t *)cqContext;
	_ib_qpCompletion(pQp);

	VNIC_EXIT ( VNIC_DBG_IB );
	return;
}

void
ib_asyncEvent(
	IN		ib_async_event_rec_t		*pEventRecord )
{
	vnic_adapter_t	*p_adapter;
	viport_t		*p_viport;

	VNIC_ENTER ( VNIC_DBG_IB );

	if ( pEventRecord )
	{
		
		switch ( pEventRecord->code )
		{
			case IB_AE_PORT_DOWN:
				p_adapter = (vnic_adapter_t *)pEventRecord->context;

				if( p_adapter &&
					p_adapter->p_currentPath->pViport &&
					!p_adapter->p_currentPath->pViport->errored )
				{
					VNIC_TRACE( VNIC_DBG_ERROR,
						("Async Event Port Down for CA GUID: %#016I64x\n", 
						p_adapter->ifc_data.ca_guid ) );
				}
				break;

			case IB_AE_CQ_ERROR:
			case IB_AE_RQ_ERROR:
			case IB_AE_SQ_ERROR:
			case IB_AE_QP_FATAL:
			case IB_AE_WQ_REQ_ERROR:
			case IB_AE_WQ_ACCESS_ERROR:

					p_viport = ((IbQp_t *)pEventRecord->context)->pViport;

					if( p_viport && !p_viport->errored )
					{
						viport_failure( p_viport );

						VNIC_TRACE( VNIC_DBG_ERROR,
							("Async Event %#x for port GUID: %#016I64x\n",
							pEventRecord->code,	
							p_viport->portGuid ) );
					}
			break;

			default:
				VNIC_TRACE( VNIC_DBG_IB,
						("Async Event %d\n", pEventRecord->code ));
					break;
		}
	}

	VNIC_EXIT ( VNIC_DBG_IB );
}







