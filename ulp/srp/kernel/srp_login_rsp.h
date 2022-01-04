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


#ifndef SRP_LOGIN_RSP_H_INCLUDED
#define SRP_LOGIN_RSP_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_information_unit.h"

#include <complib/cl_byteswap.h>

/* set_srp_login_response_tag */
/*!
Sets the tag field of a login response information unit

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value of IU

@return - none
*/
static inline
void
set_srp_login_response_tag(
	IN OUT  srp_login_rsp_t *p_information_unit,
	IN      uint64_t        iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_login_response */
/*!
Initializes the login response IU to zeroes
and sets the IU type to Srp Login Response
and sets the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag            - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_login_response(
	IN OUT  srp_login_rsp_t *p_information_unit,
	IN      uint64_t        iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_LOGIN_RSP ) ;
	set_srp_login_response_tag( p_information_unit, iu_tag );
}

/* set_srp_login_response_request_limit_delta */
/*!
Sets the request limit delta value for flow control

@param p_information_unit  - pointer to the IU structure
@param request_limit_delta - flow control request limit delta value

@return - none
*/
static inline
void
set_srp_login_response_request_limit_delta(
	IN OUT  srp_login_rsp_t *p_information_unit,
	IN      int32_t         request_limit_delta
									)
{
	p_information_unit->request_limit_delta = request_limit_delta;
}

/* set_srp_login_response_max_init_to_targ_iu */
/*!
Sets the maximum sized IU to be sent on this channel from initiator to target

@param p_information_unit - pointer to the IU structure
@param max_init_to_targ_iu  - max initiator to target IU size (64 or greater)

@return - none
*/
static inline
void
set_srp_login_response_max_init_to_targ_iu(
	IN OUT  srp_login_rsp_t *p_information_unit,
	IN      uint32_t        max_init_to_targ_iu )
{
	p_information_unit->max_init_to_targ_iu = max_init_to_targ_iu;
}

/* set_srp_login_response_max_targ_to_init_iu */
/*!
Sets the maximum sized IU to be sent on this channel from target to initiator

@param p_information_unit - pointer to the IU structure
@param max_targ_to_init_iu  - max initiator to target IU size (64 or greater)

@return - none
*/
static inline
void
set_srp_login_response_max_targ_to_init_iu(
	IN OUT  srp_login_rsp_t *p_information_unit,
	IN      uint32_t        max_targ_to_init_iu )
{
	p_information_unit->max_targ_to_init_iu = max_targ_to_init_iu;
}

/* set_srp_login_response_supported_data_buffer_formats */
/*!
Sets the flags indicating whether or not the target can and will use
direct/indirect data buffer descriptors on this channel

@param p_information_unit            - pointer to the IU structure
@param dataBufferDescriptorFormats - usage indicator values

@return - none
*/
static inline
void
set_srp_login_response_supported_data_buffer_formats(
	IN OUT  srp_login_rsp_t                 *p_information_unit,
	IN      DATA_BUFFER_DESCRIPTOR_FORMAT   data_buffer_descriptor_formats )
{
	p_information_unit->sup_buffer_fmts.flags = 0;

	if ( data_buffer_descriptor_formats & DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR )
	{
		p_information_unit->sup_buffer_fmts.flags |= DIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED;
	}

	if ( data_buffer_descriptor_formats & DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS )
	{
		p_information_unit->sup_buffer_fmts.flags |= INDIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED;
	}
}

/* set_srp_login_response_multi_channel_result */
/*!
Sets the value indicating how existing RDMA channels associated with the
same I_T nexus specified by the Initiator Port Identifier and Target Port
Identifier fields were treated. They can either be terminated or allowed
to continue processing.

@param p_information_unit   - pointer to the IU structure
@param multi_channel_result - value indicating action applied to
							existing RDMA channels

@return - none
*/
static inline
void
set_srp_login_response_multi_channel_result(
	IN OUT  srp_login_rsp_t         *p_information_unit,
	IN      MULTI_CHANNEL_RESULT    multi_channel_result )
{
	p_information_unit->flags |= multi_channel_result;
}

/* setup_srp_login_response */
/*!
Initializes and sets the Srp Login Response IU to the values supplied

@param p_information_unit          - pointer to the IU structure
@param iu_tag                      - tag value to be used for the req/rsp pair
@param request_limit_delta         - flow control request limit delta value
@param max_init_to_targ_iu         - max initiator to target IU size (64 or greater)
@param max_targ_to_init_iu         - max target to initiator IU size (64 or greater)
@param dataBufferDescriptorFormats - usage indicator values
@param multi_channel_result        - value indicating action applied to existing RDMA channels

@return - none
*/
static inline
void
setup_srp_login_response(
	IN OUT  srp_login_rsp_t                 *p_information_unit,
	IN      uint64_t                        iu_tag,
	IN      int32_t                         request_limit_delta,
	IN      uint32_t                        max_init_to_targ_iu,
	IN      uint32_t                        max_targ_to_init_iu,
	IN      DATA_BUFFER_DESCRIPTOR_FORMAT   data_buffer_descriptor_formats,
	IN      MULTI_CHANNEL_RESULT            multi_channel_result )
{
	init_srp_login_response( p_information_unit, iu_tag );
	set_srp_login_response_request_limit_delta( p_information_unit, request_limit_delta );
	set_srp_login_response_max_init_to_targ_iu( p_information_unit, max_init_to_targ_iu );
	set_srp_login_response_max_targ_to_init_iu( p_information_unit, max_targ_to_init_iu );
	set_srp_login_response_supported_data_buffer_formats( p_information_unit, data_buffer_descriptor_formats );
	set_srp_login_response_multi_channel_result( p_information_unit, multi_channel_result );
}

/* get_srp_login_response_tag */
/*!
Returns the value of the tag field of a login response

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_login_response_tag(
	IN  srp_login_rsp_t *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_login_response_request_limit_delta */
/*!
Returns the value of the request limit delta field of a login response

@param p_information_unit - pointer to the IU structure

@return - request limit delta value
*/
static inline
int32_t
get_srp_login_response_request_limit_delta(
	IN  srp_login_rsp_t *p_information_unit )
{
	return( p_information_unit->request_limit_delta );
}

/* get_srp_login_response_max_init_to_targ_iu */
/*!
Returns the value of the max initiator to target IU size value

@param p_information_unit - pointer to the IU structure

@return - max initiator to target IU value
*/
static inline
uint32_t
get_srp_login_response_max_init_to_targ_iu(
	IN  srp_login_rsp_t *p_information_unit )
{
	return( p_information_unit->max_init_to_targ_iu );
}

/* get_srp_login_response_max_targ_to_init_iu */
/*!
Returns the value of the max target to initiator IU size value

@param p_information_unit - pointer to the IU structure

@return - max target to initiator IU value
*/
static inline
uint32_t
get_srp_login_response_max_targ_to_init_iu(
	IN  srp_login_rsp_t *p_information_unit )
{
	return( p_information_unit->max_targ_to_init_iu );
}

/* get_srp_login_response_supported_data_buffer_formats */
/*!
Returns the supported data buffer formats to be used on the channel

@param p_information_unit - pointer to the IU structure

@return - supported data buffer formats settings
*/
static inline
DATA_BUFFER_DESCRIPTOR_FORMAT
get_srp_login_response_supported_data_buffer_formats(
	IN  srp_login_rsp_t *p_information_unit )
{
	switch ( p_information_unit->sup_buffer_fmts.flags & DATA_BUFFER_DESCRIPTOR_FORMAT_MASK )
	{
		case DIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED:
			return( DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR );

		case INDIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED:
			return( DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS );

		case DIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED | INDIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED:
			return( ( DATA_BUFFER_DESCRIPTOR_FORMAT ) ( DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR | DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS ) );

		default:
			return( DBDF_NO_DATA_BUFFER_DESCRIPTOR_PRESENT );
	}
}

/* get_srp_login_response_multi_channel_result */
/*!
Returns the multi channel result setting

@param p_information_unit - pointer to the IU structure

@return - multi channel result setting
*/
static inline
MULTI_CHANNEL_RESULT
get_srp_login_response_multi_channel_result(
	IN  srp_login_rsp_t *p_information_unit )
{
	return( ( MULTI_CHANNEL_RESULT ) ( p_information_unit->flags & MULTI_CHANNEL_RESULT_MASK ) );
}

/* get_srp_login_response_length */
/*!
Returns the size in bytes of the Srp Login Response IU

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint32_t
get_srp_login_response_length(
	IN  srp_login_rsp_t *p_information_unit )
{
	return( sizeof( *p_information_unit ) );
}

/* set_srp_login_response_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_login_response_from_host_to_network(
	IN OUT  srp_login_rsp_t *p_information_unit )
{
	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
	p_information_unit->request_limit_delta = cl_hton32( p_information_unit->request_limit_delta );
	p_information_unit->max_init_to_targ_iu = cl_hton32( p_information_unit->max_init_to_targ_iu );
	p_information_unit->max_targ_to_init_iu = cl_hton32( p_information_unit->max_targ_to_init_iu );
}

/* setSrpLoginResponseFromNetworkToHost */
/*!
Swaps the IU fields from Network to Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_login_response_from_network_to_host(
	IN OUT  srp_login_rsp_t *p_information_unit )
{
	set_srp_login_response_from_host_to_network ( p_information_unit );
}

#endif /* SRP_LOGIN_RSP_H_INCLUDED */
