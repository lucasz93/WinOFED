/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


/* Message sizes. */
/* TODO: these sizes are arbitrary */
#define IB_MAX_MSG_SIZE    0xFFFFFFFF //(32*1024*1024)
#define IB_MAX_RDMA_SIZE   0xFFFFFFFF //(16*1024*1024)
#define IB_RDMA_THRESHOLD_SIZE (4*1024)
/* TODO: Change back */
//#define IB_RDMA_THRESHOLD_SIZE (16*1024)

/* Number of work completion to poll at a time */
#define WC_LIST_SIZE 8 

/* QP creation parameters */
#define QP_ATTRIB_RESPONDER_RESOURCES	4
#define QP_ATTRIB_INITIATOR_DEPTH		4
#define QP_ATTRIB_RETRY_COUNT			6
#define QP_ATTRIB_RNR_RETRY				7
#define QP_ATTRIB_RNR_NAK_TIMEOUT		8 /* 16 ms */

#define QP_ATTRIB_SQ_DEPTH				16

/*
 * Only the RDMA calls can have more than one SGE - the send and receive always
 * have just one.  For send and receives, the switch always uses its internal
 * buffers.  For RDMAs the switch will issue requests with at most 4 SGEs.
 * We support twice that for good measure.
 */
#define QP_ATTRIB_SQ_SGE				8

/* Our indexes are single-byte, so make sure we don't screw up. */
C_ASSERT( QP_ATTRIB_SQ_DEPTH <= 256 );

/* 
 * TODO: During testing, the switch has been observed to post
 * 12 receive buffers.  It would be nice to know what the max is.
 */
#define QP_ATTRIB_RQ_DEPTH				16
#define QP_ATTRIB_RQ_SGE				1

/* Our indexes are single-byte, so make sure we don't screw up. */
C_ASSERT( QP_ATTRIB_RQ_DEPTH <= 256 );

/* Number of entries in a CQ */
/*
 * TODO: Workaround until MTHCA driver supports resize CQ, pre-allocate
 * for 100 QPs per CQ.
 */
#define IB_CQ_SIZE			(QP_ATTRIB_SQ_DEPTH + QP_ATTRIB_RQ_DEPTH)
#define IB_INIT_CQ_SIZE		(IB_CQ_SIZE * 500)

/* CM timeouts */
#define CM_MIN_LOCAL_TIMEOUT	(18)
#define CM_LOCAL_TIMEOUT		(1)
#define CM_MIN_REMOTE_TIMEOUT	(18)
#define CM_REMOTE_TIMEOUT		(2)
#define CM_RETRIES 4

/* Base service ID for listen */
#define BASE_LISTEN_ID (CL_CONST64(0xb6e36efb8eda0000))
