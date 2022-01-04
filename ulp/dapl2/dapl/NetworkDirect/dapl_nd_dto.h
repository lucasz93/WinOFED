/*
 * Copyright (c) 2012 Intel Corporation.  All rights reserved.
 *
 * This Software is licensed under one of the following licenses:
 *
 * 1) under the terms of the "Common Public License 1.0" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/cpl.php.
 *
 * 2) under the terms of the "The BSD License" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/bsd-license.php.
 *
 * 3) under the terms of the "GNU General Public License (GPL) Version 2" a
 *    copy of which is available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/gpl-license.php.
 *
 * Licensee has the right to choose one of the above licenses.
 *
 * Redistributions of source code must retain the above copyright
 * notice and one of the license notices.
 *
 * Redistributions in binary form must reproduce both the above copyright
 * notice, one of the license notices in the documentation
 * and/or other materials provided with the distribution.
 */
#ifndef _DAPL_ND_DTO_H_
#define _DAPL_ND_DTO_H_

#include "dapl_nd.h"

STATIC _INLINE_ int dapls_cqe_opcode(ib_work_completion_t *cqe_p);

#define CQE_WR_TYPE_UD(id) \
	(((DAPL_COOKIE *)(uintptr_t)id)->ep->qp_handle->qp_type == IBV_QPT_UD)

#if 0
STATIC _INLINE_ void
dump_sg_list(ND2_SGE *sg_list, SIZE_T segs)
{
	int j;

	printf("%s() entries %d\n",__FUNCTION__,segs);
	for(j=0; j < segs; j++)
		printf("   SGE[%d] Buffer %p len %d mr_token %#x\n",
			j, sg_list->Buffer, sg_list->BufferLength,
			sg_list->MemoryRegionToken);
}
#endif

/*
 * dapls_ib_post_recv
 *
 * Provider specific Post RECV function
 */
STATIC _INLINE_ DAT_RETURN 
dapls_ib_post_recv (
	IN  DAPL_EP		*ep_ptr,
	IN  DAPL_COOKIE		*cookie,
	IN  DAT_COUNT		segments,
	IN  DAT_LMR_TRIPLET	*local_iov )
{
	DAT_COUNT		i, total_len;
	HRESULT			hr;
	DAT_RETURN		status;
	ND2_SGE			*sg_list = (ND2_SGE*) local_iov;
	DAPL_ND_QP		*h_qp;
	SIZE_T			nSge = (SIZE_T) segments;
	
	dapl_dbg_log(DAPL_DBG_TYPE_EP,
	     "%s(ep %lx)-->post_recv: h_QP %lx cookie %lx nSge %d\n",
		     __FUNCTION__,ep_ptr,ep_ptr->qp_handle, cookie, segments);

	dapl_os_assert(ep_ptr->qp_handle);
	if (!ep_ptr->qp_handle) {
		return DAT_ERROR(DAT_INVALID_HANDLE,DAT_INVALID_HANDLE_CNO);
	}
	h_qp = ep_ptr->qp_handle;

	/* Support posting Rx before QP is created */
	if (h_qp->ND_QP == NULL) {
		status = dapli_queue_recv( ep_ptr, cookie, segments, local_iov );
		if (status != DAT_SUCCESS) {
			dapl_dbg_log(DAPL_DBG_TYPE_ERR/*XXXEP*/,
		     		"%s(%lx) Failed to Queue recv QP, ND_QP not created"
				" cookie " F64x "nSge %d\n",
		     		__FUNCTION__,ep_ptr,ep_ptr->qp_handle, cookie,
				segments);
		}
		return status;
	}

	if (segments > h_qp->recv_max_sge) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() segments(%d) > SG entries(%d) abort.\n",
			__FUNCTION__,segments,h_qp->recv_max_sge);
		return DAT_ERROR(DAT_INSUFFICIENT_RESOURCES, DAT_RESOURCE_TEP);
	}

	if (cookie != NULL) {
		for (i=0,total_len=0; i < segments; i++) {
			total_len += local_iov->segment_length;
			dapl_dbg_log(DAPL_DBG_TYPE_EP, 
				" post_rcv: lkey %#x VA %p len %lld\n",
					local_iov->lmr_context,
					local_iov->virtual_address,
					local_iov->segment_length );
			local_iov++;
		}
		cookie->val.dto.size = total_len;
	}
	else {
		dapl_os_assert( segments == 1 );
		total_len = local_iov->segment_length;
	}

	hr = h_qp->ND_QP->lpVtbl->Receive( h_qp->ND_QP,
					   (VOID*) (DAT_UINT64) cookie,
					   sg_list,
					   nSge );

	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
		    "%s(h_qp %lx) post Receive(nSge %d) ERR %s @ line #%d\n",
			 __FUNCTION__, h_qp, segments, ND_error_str(hr), __LINE__);
		return dapl_cvt_ND_to_DAT_status(hr);
	}
	dapl_os_atomic_inc(&h_qp->posted_recvs);
	DAPL_CNTR(ep_ptr, DCNT_EP_POST_RECV);
	DAPL_CNTR_DATA(ep_ptr, DCNT_EP_POST_RECV_DATA, total_len);

	return DAT_SUCCESS;
}

/*
 * dapls_ib_post_send
 *
 * Provider specific Post SEND function
 */
STATIC _INLINE_ DAT_RETURN 
dapls_ib_post_send (
	IN  DAPL_EP			*ep_ptr,
	IN  ib_send_op_type_t		op_type,
	IN  DAPL_COOKIE			*cookie,
	IN  DAT_COUNT			segments,
	IN  DAT_LMR_TRIPLET		*local_iov,
	IN  const DAT_RMR_TRIPLET	*remote_iov,
	IN  DAT_COMPLETION_FLAGS	completion_flags)
{
	DAT_COUNT		i, total_len;
	HRESULT			hr;
	ND2_SGE			*sg_list = (ND2_SGE*) local_iov;
	ND_MW_DESCRIPTOR	rdma;
	DAPL_ND_QP		*h_qp;
	ULONG			segs = (ULONG) segments;
	ULONG			send_flags=0;
	char			*op_name;
#if TRACK_RDMA_WRITES
	int			outstanding=0;
#endif
	dapl_dbg_log(DAPL_DBG_TYPE_EP,
		     " post_snd: ep %p op %d ck %p sgs",
		     "%d l_iov %p r_iov %p f %d\n",
		     ep_ptr, op_type, cookie, segments, local_iov, 
		     remote_iov, completion_flags);

#if DAT_EXTENSIONS	
	if (ep_ptr->qp_handle->qp_type != IB_QPT_RC)
		return(DAT_ERROR(DAT_INVALID_HANDLE, DAT_INVALID_HANDLE_EP));
#endif
	if (ep_ptr->param.ep_state != DAT_EP_STATE_CONNECTED) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,"%s() EP_STATE_NOT_CONNECTED?\n",
			__FUNCTION__);
		return DAT_ERROR(DAT_INVALID_STATE,
					DAT_INVALID_STATE_EP_DISCONNECTED);
	}

	/* setup the work request */
	h_qp = ep_ptr->qp_handle;

	if (cookie != NULL) {
		for (i=0,total_len=0; i < segments; i++ ) {
			total_len += local_iov->segment_length;
			dapl_dbg_log(DAPL_DBG_TYPE_EP, 
				" post_snd: lkey 0x%x va %p len %lld\n",
					local_iov->lmr_context,
					local_iov->virtual_address,
					local_iov->segment_length );
			local_iov++;
		}
		cookie->val.dto.size = total_len;
	}
	else {
		dapl_os_assert( segments == 1 );
		total_len = local_iov->segment_length;
	}

	/* set completion flags in work request */
	if ( (DAT_COMPLETION_SUPPRESS_FLAG & completion_flags) )
		send_flags |= ND_OP_FLAG_SILENT_SUCCESS;

	send_flags |= (DAT_COMPLETION_BARRIER_FENCE_FLAG & completion_flags)
			? ND_OP_FLAG_READ_FENCE : 0;

	send_flags |= (DAT_COMPLETION_SOLICITED_WAIT_FLAG & completion_flags)
			? ND_OP_FLAG_SEND_AND_SOLICIT_EVENT : 0;

	switch(op_type)
	{
	    case OP_RDMA_WRITE:
#if TRACK_RDMA_WRITES
		if ( (DAT_COMPLETION_SUPPRESS_FLAG & completion_flags) ) {
			outstanding++;
			dapl_os_atomic_inc(&h_qp->pSendCQ->outstanding_sends);
		}
#endif
		op_name = "RDMA_WRITE";
		rdma.Length = (UINT64) remote_iov->segment_length;
		hr = h_qp->ND_QP->lpVtbl->Write( h_qp->ND_QP,
						(VOID*) (DAT_UINT64) cookie,
						 sg_list,
						 segs, 
						 (UINT64)remote_iov->virtual_address,
						 remote_iov->rmr_context, // Token
						 send_flags );
		break;

	    case OP_RDMA_READ:
		op_name = "RDMA_READ";
		rdma.Base = (UINT64) remote_iov->virtual_address;
		rdma.Length = (UINT64) remote_iov->segment_length;
		rdma.Token = remote_iov->rmr_context;
		hr = h_qp->ND_QP->lpVtbl->Read( h_qp->ND_QP,
						(VOID*) (DAT_UINT64) cookie,
						sg_list,
						segs, 
						(UINT64) remote_iov->virtual_address,
						remote_iov->rmr_context, // Token
						send_flags );
		break;

	    default:
		op_name = "Send";
		hr = h_qp->ND_QP->lpVtbl->Send( h_qp->ND_QP,
						(VOID*) (DAT_UINT64) cookie,
						sg_list,
						segs, 
						send_flags );
		break;
	}

	if ( FAILED(hr) ) {
#if TRACK_RDMA_WRITES
		if ( outstanding )
			dapl_os_atomic_dec(&h_qp->pSendCQ->outstanding_sends);
#endif
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s(h_qp %lx) post %s(segs %d) ERR %s @ line #%d\n",
				 __FUNCTION__, h_qp, op_name, segments,
				ND_error_str(hr), __LINE__);
		ep_ptr->qp_state = IB_QP_STATE_ERROR;
		dapl_os_assert( hr != ND_ACCESS_VIOLATION );
		return dapl_cvt_ND_to_DAT_status(hr);
	}
#if 0
	if ( DAT_COMPLETION_SUPPRESS_FLAG & completion_flags ) {
		fprintf(stderr, "%s() CQ %lx Suppressed Completion %s\n",
				__FUNCTION__, h_qp->pSendCQ, op_name);
		dapl_dbg_log(DAPL_DBG_TYPE_EP,
			"%s()CQ %lx Suppressed Completion %s\n",
				__FUNCTION__, h_qp->pSendCQ, op_name);
	}
#endif
	dapl_dbg_log(DAPL_DBG_TYPE_EP, 
	     "\n'%s' flags %x segs %d bytes %d h_QP %lx send_CQ %lx recv_CQ %lx\n", 
		op_name, send_flags, segments, total_len,
		h_qp, h_qp->pSendCQ, h_qp->pRecvCQ );

#ifdef DAPL_COUNTERS
	switch (op_type) {
	case OP_SEND:
		DAPL_CNTR(ep_ptr, DCNT_EP_POST_SEND);
		DAPL_CNTR_DATA(ep_ptr, DCNT_EP_POST_SEND_DATA,total_len);
		break;
	case OP_RDMA_WRITE:
		DAPL_CNTR(ep_ptr, DCNT_EP_POST_WRITE);
		DAPL_CNTR_DATA(ep_ptr, DCNT_EP_POST_WRITE_DATA,total_len);
		break;	
	case OP_RDMA_READ:
		DAPL_CNTR(ep_ptr, DCNT_EP_POST_READ);
		DAPL_CNTR_DATA(ep_ptr, DCNT_EP_POST_READ_DATA,total_len);
		break;
	default:
		break;
	}
#endif /* DAPL_COUNTERS */

	dapl_dbg_log(DAPL_DBG_TYPE_EP,"-->POST_%s: OK\n",op_name);

	return DAT_SUCCESS;
}

/* map Work Completions to DAPL/DAT WR operations */
STATIC _INLINE_ DAT_DTOS dapls_cqe_dtos_opcode(ib_work_completion_t *cqe_p)
{
	DAT_DTOS op;

	switch (cqe_p->opcode) {

	case OP_SEND:
	case OP_SEND_IMM:
		op = DAT_DTO_SEND;
		break;
	case OP_RECEIVE:
	case OP_RDMA_READ:
		op = DAT_DTO_RECEIVE;
		break;
	case OP_BIND_MW:
		op = DAT_DTO_BIND_MW;
		break;
	case OP_RDMA_WRITE:
	case OP_RDMA_WRITE_IMM:
		op = DAT_DTO_RDMA_WRITE;
		break;
#ifdef DAT_EXTENSIONS
#if 0
	case IBV_WC_COMP_SWAP:
		return (DAT_IB_DTO_CMP_SWAP);
	case IBV_WC_FETCH_ADD:
		return (DAT_IB_DTO_FETCH_ADD);
	case IBV_WC_RECV_RDMA_WITH_IMM:
		return (DAT_IB_DTO_RECV_IMMED);
#endif
#else
	case OP_RDMA_WRITE:
		op = DAT_DTO_RDMA_WRITE;
		break;
#endif
	default:
		op = OP_INVALID;
		break;
	}
	return op;
}
#define DAPL_GET_CQE_DTOS_OPTYPE(cqe_p) dapls_cqe_dtos_opcode(cqe_p)


#ifdef DAT_EXTENSIONS
/*
 * dapls_ib_post_ext_send
 *
 * Provider specific extended Post SEND function for atomics
 *	OP_COMP_AND_SWAP and OP_FETCH_AND_ADD
 */
STATIC _INLINE_ DAT_RETURN 
dapls_ib_post_ext_send (
	IN  DAPL_EP			*ep_ptr,
	IN  ib_send_op_type_t		op_type,
	IN  DAPL_COOKIE			*cookie,
	IN  DAT_COUNT			segments,
	IN  DAT_LMR_TRIPLET		*local_iov,
	IN  const DAT_RMR_TRIPLET	*remote_iov,
	IN  DAT_UINT32			immed_data,
	IN  DAT_UINT64			compare_add,
	IN  DAT_UINT64			swap,
	IN  DAT_COMPLETION_FLAGS	completion_flags,
	IN  DAT_IB_ADDR_HANDLE		*remote_ah)
{
	dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		     " post_ext_snd: ep %p op %d ck %p sgs",
		     "%d l_iov %p r_iov %p f %d\n",
		     ep_ptr, op_type, cookie, segments, local_iov, 
		     remote_iov, completion_flags, remote_ah);
        return DAT_DTO_FAILURE;
}
#endif // DAT_EXTENSIONS



#define DAPL_GET_CQE_OPTYPE(cqe_p) ((ib_work_completion_t*)cqe_p)->opcode 
#define DAPL_GET_CQE_WRID(cqe_p) ((ib_work_completion_t*)cqe_p)->wr_id
#define DAPL_GET_CQE_STATUS(cqe_p) ((ib_work_completion_t*)cqe_p)->status
#define DAPL_GET_CQE_VENDOR_ERR(cqe_p) ((ib_work_completion_t*)cqe_p)->vendor_err
#define DAPL_GET_CQE_BYTESNUM(cqe_p) ((ib_work_completion_t*)cqe_p)->byte_len
#define DAPL_GET_CQE_IMMED_DATA(cqe_p) ((ib_work_completion_t*)cqe_p)->imm_data

STATIC _INLINE_ char * dapls_dto_op_str(int op)
{
    static char *optable[] =
    {
        "OP_RDMA_WRITE",
        "OP_RDMA_WRITE_IMM",
        "OP_SEND",
        "OP_SEND_IMM",
        "OP_RDMA_READ",
        "OP_COMP_AND_SWAP",
        "OP_FETCH_AND_ADD",
        "OP_RECEIVE",
        "OP_RECEIVE_MSG_IMM",
	"OP_RECEIVE_RDMA_IMM",
        "OP_BIND_MW"
	"OP_SEND_UD"
	"OP_RECV_UD"
    };
    return ((op < 0 || op > 12) ? "Invalid CQE OP?" : optable[op]);
}

static _INLINE_ char *
dapls_cqe_op_str(IN ib_work_completion_t *cqe_ptr)
{
    return dapls_dto_op_str(DAPL_GET_CQE_OPTYPE(cqe_ptr));
}

#define DAPL_GET_CQE_OP_STR(cqe) dapls_cqe_op_str(cqe)

#endif	/*  _DAPL_ND_DTO_H_ */
