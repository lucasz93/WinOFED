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


#ifndef _SRP_H_INCLUDED_
#define _SRP_H_INCLUDED_

#include <iba/ib_types.h>
#include <complib/cl_types.h>
#include <complib/cl_packon.h>

/*
  SRP Service Definitions
 */
#define SRP_IO_CLASS			CL_HTON16(0x0100)  /* T10 changed */
#define SRP_IO_CLASS_R10		CL_HTON16(0xff00)  /* FF + high 8 bits of NCITS OUI */
#define SRP_IO_SUBCLASS			CL_HTON16(0x609e)  /* Low 16 bits of NCITS OUI */
#define SRP_IO_SUBCLASS_SUN		CL_HTON16(0x690e)  /* Low 16 bits of NCITS OUI erroneously sent by SUN */
#define SRP_PROTOCOL			0x0108  /* T10 administered identifier */
#define SRP_PROTOCOL_VER		0x0001  /* Approved standard version */
#define SRP_SERVICE_NAME_PREFIX	"SRP.T10:"
#define SRP_EXTENSION_ID_LENGTH	16      /* Service name extension ID length */

#define SRP_MIN_IU_SIZE     64
#define SRP_MAX_SG_IN_INDIRECT_DATA_BUFFER     257	/* it was 16 */
#define SRP_MAX_IU_SIZE     (SRP_MIN_IU_SIZE + 20 + 16*SRP_MAX_SG_IN_INDIRECT_DATA_BUFFER)

#define SRP_MIN_INI_TO_TGT_IU       64      // Minimum initiator message size
#define SRP_MIN_TGT_TO_INI_IU       56      // Minimum target message size
#define SRP_MIN_TGT_TO_INI_DMA      512     // At least one sector!

/* Requests sent from SRP initiator ports to SRP target ports */
#define SRP_LOGIN_REQ   0x00
#define SRP_TSK_MGMT    0x01
#define SRP_CMD         0x02
#define SRP_I_LOGOUT    0x03

/* Responses sent from SRP target ports to SRP initiator ports */
#define SRP_LOGIN_RSP   0xC0
#define SRP_RSP         0xC1
#define SRP_LOGIN_REJ   0xC2

/* Requests sent from SRP target ports to SRP initiator ports */
#define SRP_T_LOGOUT    0x80
#define SRP_CRED_REQ    0x81
#define SRP_AER_REQ     0x82

/* Responses sent from SRP initiator ports to SRP target ports */
#define SRP_CRED_RSP    0x41
#define SRP_AER_RSP     0x42

typedef struct _srp_information_unit
{
	uint8_t     type;
	uint8_t     reserved[7];
	uint64_t    tag;
} PACK_SUFFIX srp_information_unit_t;

/* Mask values applied to bit fields for access */
#define DATA_BUFFER_DESCRIPTOR_FORMAT_MASK  0x06
#define MULTI_CHANNEL_ACTION_MASK           0x03
#define MULTI_CHANNEL_RESULT_MASK           0x03

/* Allowable values for the Data Buffer Descriptor Formats */
typedef enum data_buffer_descriptor_format_enum
{
	DBDF_NO_DATA_BUFFER_DESCRIPTOR_PRESENT  = 0x00,
	DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR      = 0x01,
	DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS   = 0x02
} DATA_BUFFER_DESCRIPTOR_FORMAT;

/* Requested Supportted Data Buffer Format flag values */
#define DIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED     0x02
#define INDIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED   0x04

typedef struct _srp_req_sup_db_fmt
{
	uint8_t     reserved;
	uint8_t     flags; /* IDBD/DDBD */
} PACK_SUFFIX srp_req_sup_db_fmt_t;

/*
 * The SRP spec r10 defines the port identifiers as
 * GUID:ExtensionID, while the SRP 2.0 spec defines them
 * as ExtensionID:GUID.  Lucky for us the IO_CLASS in the
 * IOC profile changed from 0xFF to 0x100.
 */
typedef struct _srp_ib_port_id
{
	net64_t		field1;
	net64_t		field2;

} PACK_SUFFIX srp_ib_port_id_t;

/* Allowable values for the MultiChannel Action field */
typedef enum multi_channel_action_enum
{
	MCA_TERMINATE_EXISTING      = 0x00,
	MCA_INDEPENDENT_OPERATION   = 0x01
} MULTI_CHANNEL_ACTION;

typedef struct _srp_login_req
{
	uint8_t                 type;
	uint8_t                 reserved1[7];
	uint64_t                tag;
	uint32_t                req_max_init_to_targ_iu;
	uint8_t                 reserved2[4];
	srp_req_sup_db_fmt_t    req_buffer_fmts;
	uint8_t                 flags; /* MULTI-CHANNEL ACTION */
	uint8_t                 reserved3;
	uint8_t                 reserved4[4];
	srp_ib_port_id_t		initiator_port_id;
	srp_ib_port_id_t		target_port_id;
} PACK_SUFFIX srp_login_req_t;

/* Allowable values for the MultiChannel Result field */
typedef enum multi_channel_result_enum
{
	MCR_NO_EXISTING_TERMINATED  = 0x00,
	MCR_EXISTING_TERMINATED     = 0x01,
	MCR_EXISTING_CONTINUED      = 0x02
} MULTI_CHANNEL_RESULT;

typedef struct _srp_login_rsp
{
	uint8_t                 type;
	uint8_t                 reserved1[3];
	int32_t                 request_limit_delta;
	uint64_t                tag;
	uint32_t                max_init_to_targ_iu;
	uint32_t                max_targ_to_init_iu;
	srp_req_sup_db_fmt_t    sup_buffer_fmts;
	uint8_t                 flags; /* MULTI-CHANNEL RESULT */
	uint8_t                 reserved2;
	uint8_t                 reserved3[24];
} PACK_SUFFIX srp_login_rsp_t;

/* Allowable values for SRP LOGIN REJ Reason Codes */
typedef enum login_reject_code_enum
{
	LIREJ_UNABLE_TO_ESTABLISH_RDMA_CHANNEL                              = 0x00010000,
	LIREJ_INSUFFICIENT_RDMA_CHANNEL_RESOURCES                           = 0x00010001,
	LIREJ_INIT_TO_TARG_IU_LENGTH_TOO_LARGE                              = 0x00010002,
	LIREJ_UNABLE_TO_ASSOCIATE_RDMA_CHANNEL_WITH_I_T_NEXUS               = 0x00010003,
	LIREJ_UNSUPPORTED_DATA_BUFFER_DESCRIPTOR_FORMAT                     = 0x00010004,
	LIREJ_NO_TARGET_SUPPORT_FOR_MULTIPLE_RDMA_CHANNELS_PER_I_T_NEXUS    = 0x00010005
} LOGIN_REJECT_CODE;

typedef struct _srp_login_rej
{
	uint8_t                 type;
	uint8_t                 reserved1[3];
	uint32_t                reason;
	uint64_t                tag;
	uint8_t                 reserved2[8];
	srp_req_sup_db_fmt_t    sup_buffer_fmts;
	uint8_t                 reserved3[6];
} PACK_SUFFIX srp_login_rej_t;

typedef struct _srp_i_logout
{
	uint8_t     type;
	uint8_t     reserved[7];
	uint64_t    tag;
} PACK_SUFFIX srp_i_logout_t;

/* Srp Target Logout Reason Codes */
typedef enum target_logout_reason_code_enum
{
	TLO_NO_REASON                                               = 0x0000,
	TLO_INACTIVE_RDMA_CHANNEL                                   = 0x0001,
	TLO_INVALID_IU_TYPE_RECEIVED_BY_TARGET                      = 0x0002,
	TLO_RESPONSE_WITH_NO_OUTSTANDING_TARGET_PORT_REQUEST        = 0x0003,
	TLO_DISCONNECT_DUE_TO_MULTI_CHANNEL_ACTION_ON_NEW_LOGIN     = 0x0004,
	TLO_UNSUPPORTED_FORMAT_FOR_DATA_OUT_BUFFER_DESCRIPTOR       = 0x0006,
	TLO_UNSUPPORTED_FORMAT_FOR_DATA_IN_BUFFER_DESCRIPTOR        = 0x0007,
	TLO_INVALID_COUNT_VALUE_IN_DATA_OUT_BUFFER_DESCRIPTOR_COUNT = 0x0008,
	TLO_INVALID_COUNT_VALUE_IN_DATA_IN_BUFFER_DESCRIPTOR_COUNT  = 0x0009
} TARGET_LOGOUT_REASON_CODE;

typedef struct _srp_t_logout
{
	uint8_t     type;
	uint8_t     reserved[3];
	uint32_t    reason;
	uint64_t    m_tag;
} PACK_SUFFIX srp_t_logout_t;

/* Srp Task Management Flags */
#define TMF_ABORT_TASK          0x01
#define TMF_ABORT_TASK_SET      0x02
#define TMF_CLEAR_TASK_SET      0x04
#define TMF_LOGICAL_UNIT_RESET  0x08
#define TMF_RESTRICTED          0x20
#define TMF_CLEAR_ACA           0x40

typedef struct _srp_tsk_mgmt
{
	uint8_t     type;
	uint8_t     reserved1[7];
	uint64_t    tag;
	uint8_t     reserved2[4];
	uint64_t    logical_unit_number;
	uint8_t     reserved3;
	uint8_t     reserved4;
	uint8_t     task_management_flags;
	uint8_t     reserved5;
	uint64_t    managed_task_tag;
	uint8_t     reserved6[8];
} PACK_SUFFIX srp_tsk_mgmt_t;

/* Srp TASK ATTRIBUTE VALUES */
typedef enum task_attribute_value_enum
{
	TAV_SIMPLE_TASK                         = 0x00,
	TAV_HEAD_OF_QUEUE_TASK                  = 0x01,
	TAV_ORDERED                             = 0x02,
	TAV_AUTOMATIC_CONTINGENT_ALLIANCE_TASK  = 0x04
} TASK_ATTRIBUTE_VALUE;

typedef struct _srp_memory_descriptor
{
	uint64_t    virtual_address;
	uint32_t    memory_handle;
	uint32_t    data_length;
} PACK_SUFFIX srp_memory_descriptor_t;

typedef struct _srp_memory_table_descriptor
{
	srp_memory_descriptor_t	descriptor;
	uint32_t    total_length;
} PACK_SUFFIX srp_memory_table_descriptor_t;

typedef struct _srp_cmd
{
	uint8_t     type;
	uint8_t     reserved1[4];
	uint8_t     data_out_in_buffer_desc_fmt;
	uint8_t     data_out_buffer_desc_count;
	uint8_t     data_in_buffer_desc_count;
	uint64_t    tag;
	uint8_t     reserved2[4];
	uint64_t    logical_unit_number;
	uint8_t     reserved3;
	uint8_t     flags1;      /* TASK ATTRIBUTE */
	uint8_t     reserved4;
	uint8_t     flags2;      /* ADDITIONAL CDB LENGTH in 4 byte words */
	uint8_t     cdb[16];
	uint8_t     additional_cdb[1]; /* place holder, may not be present */
 /* srp_memory_descriptor_t     data_out_buffer_desc[] */
 /* srp_memory_descriptor_t     data_in_buffer_desc[] */
} PACK_SUFFIX srp_cmd_t;

/* Srp Response Code values */
typedef enum response_code_value_enum
{
	RC_NO_FAILURE_OR_TSK_MGMT_FUNC_COMPLETE = 0x00,
	RC_REQUEST_FIELDS_INVALID               = 0x02,
	RC_TSK_MGMT_FUNCTION_NOT_SUPPORTED      = 0x04,
	RC_TSK_MGMT_FUNCTION_FAILED             = 0x05
} RESPONSE_CODE_VALUE;

typedef struct _srp_response_data
{
	uint8_t     reserved[3];
	uint8_t     response_code;
} PACK_SUFFIX srp_response_data_t;

typedef struct _srp_rsp
{
	uint8_t                 type;
	uint8_t                 reserved1[3];
	int32_t                 request_limit_delta;
	uint64_t                tag;
	uint8_t                 reserved2[2];
	uint8_t                 flags;    /* DIUNDER DIOVER DOUNDER DOOVER SNSVALID RSPVALID */
	uint8_t                 status;
	uint32_t                data_out_residual_count;
	uint32_t                data_in_residual_count;
	uint32_t                sense_data_list_length;
	uint32_t                response_data_list_length;
	srp_response_data_t     response_data[1]; /* place holder. may not be present */
	/* uint8_t              sense_data[] */
} PACK_SUFFIX srp_rsp_t;

typedef struct _srp_cred_req
{
	uint8_t     type;
	uint8_t     reserved[3];
	int32_t     request_limit_delta;
	uint64_t    tag;
} PACK_SUFFIX srp_cred_req_t;

typedef struct _srp_cred_rsp
{
	uint8_t     type;
	uint8_t     reserved[7];
	uint64_t    tag;
} PACK_SUFFIX srp_cred_rsp_t;

typedef struct _srp_aer_req
{
	uint8_t     type;
	uint8_t     reserved1[3];
	int32_t     request_limit_delta;
	uint64_t    tag;
	uint8_t     reserved2[4];
	uint64_t    logical_unit_number;
	uint32_t    sense_data_list_length;
	uint8_t     reserved3[4];
	uint8_t     sense_data[1];   /* actually a place holder may not be present */
} PACK_SUFFIX srp_aer_req_t;

typedef struct _srp_aer_rsp
{
	uint8_t     type;
	uint8_t     reserved[7];
	uint64_t    tag;
} PACK_SUFFIX srp_aer_rsp_t;

typedef union _srp_iu_buffer
{
	uint64_t                alignment_dummy;
	uint8_t                 iu_buffer[SRP_MAX_IU_SIZE];
	srp_information_unit_t  information_unit;
	srp_login_req_t         login_request;
	srp_login_rsp_t         login_response;
	srp_login_rej_t         login_reject;
	srp_i_logout_t          initiator_logout;
	srp_t_logout_t          target_logout;
	srp_tsk_mgmt_t          task_management;
	srp_cmd_t               command;
	srp_rsp_t               response;
	srp_cred_req_t          credit_request;
	srp_cred_rsp_t          credit_response;
	srp_aer_req_t           async_event_request;
	srp_aer_rsp_t           async_event_response;
} PACK_SUFFIX srp_iu_buffer_t;

#include <complib/cl_packoff.h>

#endif /* SRP_H_INCLUDED */
