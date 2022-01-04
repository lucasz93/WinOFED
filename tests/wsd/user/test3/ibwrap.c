#include "ibwrap.h"

/* CQ completion handler */
static void ib_cq_comp(void *cq_context)
{
	ib_api_status_t status;
	ib_wc_t wclist;
	ib_wc_t *free_wclist;
	ib_wc_t *done_wclist;
	BOOL need_rearm;
	struct qp_pack *qp = cq_context;

	need_rearm = TRUE;

	  again:
	while(1) {

		wclist.p_next = NULL;

		free_wclist = &wclist;

		status = ib_poll_cq(qp->cq_handle, &free_wclist, &done_wclist);

		switch (status) {
		case IB_NOT_FOUND:
			goto done;

		case IB_SUCCESS:
			//printf("got a completion\n");
			break;

		default:
			printf("ib_poll_cq failed badly (%d)\n", status);
			need_rearm = FALSE;
			goto done;
		}

#if 0
		if (done_wclist->status != IB_WCS_SUCCESS) {
			printf("operation failed - status %d\n", done_wclist->status);
		}
#endif

		need_rearm = TRUE;

		/* We have some completions. */
		while(done_wclist) {

			cl_atomic_dec(&qp->wq_posted);

			if (qp->comp) qp->comp(qp, done_wclist);

			done_wclist = done_wclist->p_next;
		};

		if (free_wclist != NULL) {
			/* No more completions */
			goto done;
		}
	};

  done:
	if (need_rearm) {

		need_rearm = FALSE;

		status = ib_rearm_cq(qp->cq_handle, FALSE);
		if (status != IB_SUCCESS) {
			printf("ib_poll_cq failed badly (%d)\n", status);
		}

		goto again;
	}
}

/* Enable or disable completion. */
int control_qp_completion(struct qp_pack *qp, int enable)
{
	ib_api_status_t status;

	if (enable) {
		
		status = ib_rearm_cq(qp->cq_handle, FALSE);
		if (status) {
			printf("ib_rearm_cq failed (%d)\n", status);
			goto fail;
		}

	} 

	return 0;

 fail:
	return -1;
}

int create_qp(struct qp_pack *qp)
{
	ib_api_status_t status;
	ib_cq_create_t cq_create;
	ib_qp_create_t qp_create;
	ib_qp_mod_t qp_mod;
	ib_net64_t *guid_list = NULL;
	size_t adapter_count;
	size_t ca_attr_size = 0;

	qp->wq_posted = 0;

	/* Open AL */
	status = ib_open_al(&qp->al_handle);
	if (status != IB_SUCCESS) {
		printf("ib_open_al failed (%d)\n", status);
		goto fail;
	}

	/* Find a CA */
	adapter_count = 10;
	guid_list = malloc(sizeof(ib_net64_t) * adapter_count);
	if (guid_list == NULL) {
		printf("can't get enough memory (%d, %d)\n", sizeof(ib_net64_t), adapter_count);
		goto fail;
	}

	status = ib_get_ca_guids(qp->al_handle, guid_list, &adapter_count);
	if (status != IB_SUCCESS) {
		printf("second ib_get_ca_guids failed (%d)\n", status);
		goto fail;
	}

	if (adapter_count < 1) {
		printf("not enough CA (%d)\n", adapter_count);
		goto fail;
	}

	/* Open the hca */
	status = ib_open_ca(qp->al_handle, guid_list[0], NULL,	/* event handler */
						NULL,	/* context */
						&qp->hca_handle);

	if (status != IB_SUCCESS) {
		printf("ib_open_ca failed (%d)\n", status); 
		goto fail;
	}

	
	/* Get the HCA attributes */
	ca_attr_size = 0;
 query_ca_again:
	status = ib_query_ca(qp->hca_handle, qp->ca_attr, &ca_attr_size);

	if (status == IB_INSUFFICIENT_MEMORY) {

		printf("ib_query_ca needs %d bytes\n", ca_attr_size);

		/* Allocate more memory */
		qp->ca_attr = malloc(ca_attr_size);

		if (qp->ca_attr)
			goto query_ca_again;
		else {
			printf("HeapAlloc failed\n");
			goto fail;
		}
	} else if (status != IB_SUCCESS) {
		printf("ib_query_ca failed (%d)\n", status);
		goto fail;
	}

	/* Find a port */
	if (qp->ca_attr->num_ports < 1) {
		printf("not enough ports (%d)\n", qp->ca_attr->num_ports);
		goto fail;
	}

	qp->hca_port = &qp->ca_attr->p_port_attr[0];

	/* Create a PD */
	status = ib_alloc_pd(qp->hca_handle, IB_PDT_NORMAL, qp,	/* context */
						 &qp->pd_handle);
	if (status) {
		printf("ib_alloc_pd failed (%d)\n", status);
		goto fail;
	}

	/* Create a CQ */
	cq_create.size = 50;
	cq_create.pfn_comp_cb = ib_cq_comp;
	cq_create.h_wait_obj = NULL;	/* we use signaled completion instead */

	status = ib_create_cq(qp->hca_handle, &cq_create, qp,	/* context */
						  NULL,	/* async handler */
						  &qp->cq_handle);
	if (status) {
		printf("ib_create_cq failed (%d)\n", status);
		goto fail;
	}

	status = ib_rearm_cq(qp->cq_handle, FALSE);
	if (status) {
		printf("ib_rearm_cq failed (%d)\n", status);
		goto fail;
	}

	/* Arm the CQ handler */
	if (control_qp_completion(qp, 1) != 0) {
		printf("control_qp_completion failed\n");
		goto fail;
	}

	/* Create a qp */
	cl_memclr(&qp_create, sizeof(ib_qp_create_t));
	qp_create.qp_type = IB_QPT_RELIABLE_CONN;
	qp_create.h_rdd = NULL;
	qp_create.sq_depth = 255;
	qp_create.rq_depth = 255;
	qp_create.sq_sge = 1;
	qp_create.rq_sge = 1;
	qp_create.h_rq_cq = qp->cq_handle;
	qp_create.h_sq_cq = qp->cq_handle;
	qp_create.sq_signaled = TRUE;

	status = ib_create_qp(qp->pd_handle, &qp_create, qp,	/* context */
						  NULL,	/* async handler */
						  &qp->qp_handle);
	if (status) {
		printf("ib_create_qp failed (%d)\n", status);
		goto fail;
	}

	/* Modify QP to INIT state */
	memset(&qp_mod, 0, sizeof(qp_mod));

	qp_mod.req_state = IB_QPS_INIT;
	qp_mod.state.init.pkey_index = 0;
	qp_mod.state.init.primary_port = qp->hca_port->port_num;
	qp_mod.state.init.access_ctrl = IB_AC_RDMA_READ | IB_AC_RDMA_WRITE | IB_AC_LOCAL_WRITE;
	qp_mod.state.init.opts = IB_MOD_QP_PRIMARY_PORT | IB_MOD_QP_ACCESS_CTRL | IB_MOD_QP_PKEY;

	status = ib_modify_qp(qp->qp_handle, &qp_mod);
	if (status) {
		printf("ib_modify_qp failed (%d)\n", status);
		goto fail;
	}

	if (query_qp(qp)) {
		printf("query failed\n");
		goto fail;
	}

	return 0;

 fail:
	return -1;
}

int query_qp(struct qp_pack *qp)
{
	ib_api_status_t status;

	status = ib_query_qp(qp->qp_handle, &qp->qp_attr);
	if (status) {
		printf("ib_query_qp failed (%d)\n", status);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}

int connect_qp(struct qp_pack *qp1, struct qp_pack *qp2) 
{
	ib_api_status_t status;
	ib_qp_mod_t qp_mod;

	/* Update attributes */
	if (query_qp(qp1)) {
		printf("query failed\n");
		goto fail;
	}

	if (query_qp(qp2)) {
		printf("query failed\n");
		goto fail;
	}

	/* Modify QP to RTR state */
	memset(&qp_mod, 0, sizeof(qp_mod));

	qp_mod.req_state = IB_QPS_RTR;
	qp_mod.state.rtr.rq_psn = 0x1234;	/* random */
	qp_mod.state.rtr.dest_qp = qp2->qp_attr.num;
	qp_mod.state.rtr.primary_av.dlid = qp2->hca_port->lid;
	qp_mod.state.rtr.primary_av.static_rate = 1;
	qp_mod.state.rtr.primary_av.port_num = qp1->hca_port->port_num;
	
	qp_mod.state.rtr.primary_av.conn.local_ack_timeout = 0;
	qp_mod.state.rtr.primary_av.conn.path_mtu = IB_MTU_LEN_2048;
	qp_mod.state.rtr.primary_av.conn.rnr_retry_cnt = 6;
	qp_mod.state.rtr.primary_av.conn.seq_err_retry_cnt = 6;

	qp_mod.state.rtr.qkey = 0;
	qp_mod.state.rtr.resp_res = 4;
	qp_mod.state.rtr.rnr_nak_timeout = 6;

	qp_mod.state.rtr.opts = IB_MOD_QP_QKEY | IB_MOD_QP_PRIMARY_AV | IB_MOD_QP_RNR_NAK_TIMEOUT | IB_MOD_QP_RESP_RES;

	status = ib_modify_qp(qp1->qp_handle, &qp_mod);
	if (status) {
		printf("ib_modify_qp 1 failed (%d)\n", status);
		goto fail;
	}

	/* Modify QP to RTS state */
	memset(&qp_mod, 0, sizeof(qp_mod));

	qp_mod.req_state = IB_QPS_RTS;
	qp_mod.state.rts.sq_psn = 0x1234;
	qp_mod.state.rts.retry_cnt = 7;
	qp_mod.state.rts.rnr_retry_cnt = 7;
	
	qp_mod.state.rts.opts = IB_MOD_QP_RETRY_CNT | IB_MOD_QP_RNR_RETRY_CNT;

	status = ib_modify_qp(qp1->qp_handle, &qp_mod);
	if (status) {
		printf("ib_modify_qp 2 failed (%d)\n", status);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}

#if 0
/* Move the QP to the error state. Wait for the CQ to flush. */
int move_qp_to_error(struct qp_pack *qp)
{
	ib_api_status_t status;
	VAPI_qp_attr_t       qp_attr;
	VAPI_qp_attr_mask_t  qp_attr_mask;
	VAPI_qp_cap_t        qp_cap;

	/* Modify QP to ERROR state */
	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state        = VAPI_ERR;

	qp_attr_mask            = QP_ATTR_QP_STATE;

	vret = VAPI_modify_qp(qp->hca_handle, qp->qp_handle, &qp_attr, &qp_attr_mask, &qp_cap);

	if (vret != VAPI_OK) {
		VAPI_ERR_LOG("VAPI_modify_qp(ERROR) failed ", vret);
		goto fail;
	}

	while(qp->wq_posted != 0) {
		usleep(10000);
	}

	return 0;

 fail:
	return -1;
}
#endif

#if 0
int move_qp_to_reset(struct qp_pack *qp)
{
	ib_api_status_t status;
	VAPI_qp_attr_t       qp_attr;
	VAPI_qp_attr_mask_t  qp_attr_mask;
	VAPI_qp_cap_t        qp_cap;

	/* Modify QP to RESET state */
	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state        = VAPI_RESET;
	
	qp_attr_mask            = QP_ATTR_QP_STATE;
	
	vret = VAPI_modify_qp(qp->hca_handle, qp->qp_handle, &qp_attr, &qp_attr_mask, &qp_cap);
	
	if (vret != VAPI_OK) {
		VAPI_ERR_LOG("VAPI_modify_qp(ERROR) failed ", vret);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}

int move_qp_to_drain(struct qp_pack *qp)
{
	ib_api_status_t status;
	VAPI_qp_attr_t       qp_attr;
	VAPI_qp_attr_mask_t  qp_attr_mask;
	VAPI_qp_cap_t        qp_cap;

	/* Modify QP to SQD state */
	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state        = VAPI_SQD;
	qp_attr_mask            = QP_ATTR_QP_STATE;
	
	vret = VAPI_modify_qp(qp->hca_handle, qp->qp_handle, &qp_attr, &qp_attr_mask, &qp_cap);
	
	if (vret != VAPI_OK) {
		VAPI_ERR_LOG("VAPI_modify_qp(SQD) failed ", vret);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}
#endif

int destroy_qp(struct qp_pack *qp)
{
	ib_api_status_t status;

	if (qp->qp_handle) {
		status = ib_destroy_qp(qp->qp_handle, NULL);
		if (status) {
			printf("ib_destroy_qp failed (%d)\n", status);
		}
		qp->qp_handle = NULL;
	}

	if (qp->cq_handle) {
		status = ib_destroy_cq(qp->cq_handle, NULL);
		if (status) {
			printf("ib_destroy_cq failed (%d)\n", status);
		}
		qp->cq_handle = NULL;
	}

	if (qp->pd_handle) {
		status = ib_dealloc_pd(qp->pd_handle, NULL);
		if (status) {
			printf("ib_dealloc_pd failed (%d)\n", status);
		}
		qp->pd_handle = NULL;
	}

	if (qp->hca_handle) {
		status = ib_close_ca(qp->hca_handle, NULL);
		if (status != IB_SUCCESS) {
			printf("ib_close_ca failed (%d)\n", status);
		}
		qp->hca_handle = NULL;
	}

	if (qp->al_handle) {
		status = ib_close_al(qp->al_handle);
		if (status != IB_SUCCESS) {
			printf("ib_close_al failed (%d)\n", status);
		}
		qp->al_handle = NULL;
	}
	
	return 0;
}

/* Create and register a memory region which will be used for rdma transfer. */
int create_mr(struct qp_pack *qp, struct mr_pack *mr, size_t len) {

	ib_api_status_t status;
	ib_mr_create_t mr_create;

	mr->size = len;

	mr->buf = malloc(mr->size);
	if (mr->buf == NULL) {
		printf("malloc failed\n");
		goto fail;
	}

	mr_create.access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE | IB_AC_RDMA_READ;
	mr_create.length = mr->size;
	mr_create.vaddr = mr->buf;

	status = ib_reg_mem(qp->pd_handle, &mr_create,
						&mr->lkey, &mr->rkey, &mr->mr_handle);

	if (status) {
		printf("ib_reg_mem failed (%d)\n", status);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}

/* Unregister and free a local memory region. */
int delete_mr(struct mr_pack *mr) {
	ib_api_status_t status;

	status = ib_dereg_mr(mr->mr_handle);
	if (status) {
		printf("ib_dereg_mr failed (%d)\n", status);
		goto fail;
	}

	free(mr->buf);
	memset(mr, 0, sizeof(struct mr_pack));

	return 0;

 fail:
	return -1;
}

int post_receive_buffer(struct qp_pack *qp, struct mr_pack *mr,
						int offset, size_t length)
{
	ib_api_status_t status;
	ib_local_ds_t dataseg;
	ib_recv_wr_t rwr;

	/* Post receive buffer on qp1 */
	memset(&rwr, 0, sizeof(rwr));

	rwr.wr_id = (uint64_t) (uintptr_t)qp;
	
	rwr.num_ds = 1;
	rwr.ds_array = &dataseg;

	dataseg.length = (uint32_t)length;
	dataseg.vaddr = (uint64_t) (uintptr_t) mr->buf + offset;
	dataseg.lkey = mr->lkey;
	
	cl_atomic_inc(&qp->wq_posted);

	status = ib_post_recv(qp->qp_handle, &rwr, NULL);

	if (status != IB_SUCCESS) {
		cl_atomic_dec(&qp->wq_posted);
		printf("ib_post_send failed (%d)\n", status);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}

int post_send_buffer(struct qp_pack *qp, struct mr_pack *local_mr,
	struct mr_pack *remote_mr, ib_wr_type_t opcode, int offset, size_t length)
{
	ib_api_status_t status;
	ib_local_ds_t dataseg;
	ib_send_wr_t swr;
	
	/* Post send buffer on qp2. */
	memset(&swr, 0, sizeof(swr));
	swr.wr_id = (uint64_t) (uintptr_t)qp;
	swr.wr_type = opcode;

	if (length == 0) {
		swr.num_ds = 0;
		swr.ds_array = NULL;
	} else {
		swr.num_ds = 1;
		swr.ds_array = &dataseg;

		dataseg.length = (uint32_t)length;
		dataseg.vaddr = (uint64_t) (uintptr_t) local_mr->buf + offset;
		dataseg.lkey = local_mr->lkey;
	}

	if (opcode == WR_RDMA_WRITE || opcode == WR_RDMA_READ) {
		swr.remote_ops.vaddr = (uint64_t) (uintptr_t) remote_mr->buf;
		swr.remote_ops.rkey = remote_mr->rkey;

		printf("RDMA %d %I64x %x - %I64x %x\n",
			swr.ds_array[0].length,
			swr.ds_array[0].vaddr,
			swr.ds_array[0].lkey,
			swr.remote_ops.vaddr,
			swr.remote_ops.rkey);
		printf("REM RDMA %p %x\n", remote_mr->buf, remote_mr->rkey);
	}

	cl_atomic_inc(&qp->wq_posted);

	status = ib_post_send(qp->qp_handle, &swr, NULL);

	if (status != IB_SUCCESS) {
		cl_atomic_dec(&qp->wq_posted);
		printf("ib_post_send failed (%d)\n", status);
		goto fail;
	}

	return 0;

 fail:
	return -1;	
}
