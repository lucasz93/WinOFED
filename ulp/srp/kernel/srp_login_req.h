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


#ifndef SRP_LOGIN_REQ_H_INCLUDED
#define SRP_LOGIN_REQ_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_information_unit.h"
#include <complib/cl_byteswap.h>

/* set_srp_login_request_tag */
/*!
Sets the tag field of the login request IU to the supplied value

@param p_information_unit - pointer to the IU structure
@param iu_tag            - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
set_srp_login_request_tag(
	IN OUT  srp_login_req_t *p_information_unit,
	IN      uint64_t        iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_login_request */
/*!
Initializes the login request IU to zeroes
and sets the IU type to Srp Login Request
and set the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag            - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_login_request(
	IN OUT  srp_login_req_t *p_information_unit,
	IN      uint64_t        iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_LOGIN_REQ ) ;
	set_srp_login_request_tag( p_information_unit, iu_tag );
}

/* set_srp_login_request_req_max_init_to_targ_iu */
/*!
Sets the maximum sized IU to be sent on this channel from initiator to target

@param p_information_unit   - pointer to the IU structure
@param req_max_init_to_targ_iu - max initiator to target IU size (64 or greater)

@return - none
*/
static inline
void
set_srp_login_request_req_max_init_to_targ_iu(
	IN OUT  srp_login_req_t *p_information_unit,
	IN      uint32_t        req_max_init_to_targ_iu )
{
	p_information_unit->req_max_init_to_targ_iu = req_max_init_to_targ_iu;
}

/* set_srp_login_request_required_data_buffer_formats */
/*!
Sets the flags indicating whether or not the initiator will use
target support of direct/indirect data buffer descriptors on this channel

@param p_information_unit             - pointer to the IU structure
@param data_buffer_descriptor_formats - usage indicator values

@return - none
*/
static inline
void
set_srp_login_request_required_data_buffer_formats(
	IN OUT  srp_login_req_t                 *p_information_unit,
	IN      DATA_BUFFER_DESCRIPTOR_FORMAT   data_buffer_descriptor_formats )
{
	p_information_unit->req_buffer_fmts.flags = 0;

	if ( data_buffer_descriptor_formats & DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR )
	{
		p_information_unit->req_buffer_fmts.flags |= DIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED;
	}

	if ( data_buffer_descriptor_formats & DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS )
	{
		p_information_unit->req_buffer_fmts.flags |= INDIRECT_DATA_BUFFER_DESCRIPTOR_REQUESTED;
	}
}

/* set_srp_login_request_multi_channel_action */
/*!
Sets the value indicating how existing RDMA channels associated with the
same I_T nexus specified by the Initiator Port Identifier and Target Port
Identifier fields are to be treated. They can either be terminated or allowed
to continue processing.

@param p_information_unit   - pointer to the IU structure
@param multi_channel_action - value indicating action to be applied to
							  existing RDMA channels

@return - none
*/
static inline
void
set_srp_login_request_multi_channel_action(
	IN OUT  srp_login_req_t         *p_information_unit,
	IN      MULTI_CHANNEL_ACTION    multi_channel_action )
{
	p_information_unit->flags |= multi_channel_action;
}

/* setSrpLoginRequestITNexus */
/*!
Sets the I_T nexus value

@param p_information_unit  - pointer to the IU structure
@param p_initiator_port_id - initiator's port id value
@param p_target_port_id    - target's port id value

@return - none
*/
static inline
void
set_srp_login_request_it_nexus(
	IN OUT  srp_login_req_t         *p_information_unit,
	IN      srp_ib_port_id_t		*p_initiator_port_id,
	IN      srp_ib_port_id_t		*p_target_port_id )
{
	RtlCopyMemory( &p_information_unit->initiator_port_id,
		p_initiator_port_id, sizeof(srp_ib_port_id_t) );
	RtlCopyMemory( &p_information_unit->target_port_id,
		p_target_port_id, sizeof(srp_ib_port_id_t) );
}

/* setup_srp_login_request */
/*!
Initializes and sets the Srp Login Request IU to the values supplied

@param p_information_unit             - pointer to the IU structure
@param iu_tag                         - tag value to be used for the req/rsp pair
@param req_max_init_to_targ_iu        - max initiator to target IU size (64 or greater)
@param data_buffer_descriptor_formats - usage indicator values
@param multi_channel_action           - value indicating action to be applied to existing RDMA channels
@param p_initiator_port_id            - initiator's port id value (for I_T nexus)
@param p_target_port_id               - target's port id value (for I_T nexus)

@return - none
*/
static inline
void
setup_srp_login_request(
	IN OUT  srp_login_req_t                 *p_information_unit,
	IN      uint64_t                        iu_tag,
	IN      uint32_t                        req_max_init_to_targ_iu,
	IN      DATA_BUFFER_DESCRIPTOR_FORMAT   data_buffer_descriptor_formats,
	IN      MULTI_CHANNEL_ACTION            multi_channel_action,
	IN      srp_ib_port_id_t				*p_initiator_port_id,
	IN      srp_ib_port_id_t				*p_target_port_id )
{
	init_srp_login_request( p_information_unit, iu_tag );
	set_srp_login_request_req_max_init_to_targ_iu( p_information_unit, req_max_init_to_targ_iu );
	set_srp_login_request_required_data_buffer_formats( p_information_unit, data_buffer_descriptor_formats );
	set_srp_login_request_multi_channel_action( p_information_unit, multi_channel_action );
	set_srp_login_request_it_nexus( p_information_unit, p_initiator_port_id, p_target_port_id );
}

/* get_srp_login_request_tag */
/*!
Returns the value of the tag field of a login request

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_login_request_tag(
	IN  srp_login_req_t *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_login_request_req_max_init_to_targ_iu */
/*!
Returns the requested max initiator to target information unit size

@param p_information_unit - pointer to the IU structure

@return - requested max initiator to target information unit size value
*/
static inline
uint32_t
get_srp_login_request_req_max_init_to_targ_iu(
	IN  srp_login_req_t *p_information_unit )
{
	return( p_information_unit->req_max_init_to_targ_iu );
}

/* get_srp_login_request_required_data_buffer_formats */
/*!
Returns the required data buffer formats to be used on the channel

@param p_information_unit - pointer to the IU structure

@return - required data buffer formats settings
*/
static inline
DATA_BUFFER_DESCRIPTOR_FORMAT
get_srp_login_request_required_data_buffer_formats(
	IN  srp_login_req_t *p_information_unit )
{
	switch ( p_information_unit->req_buffer_fmts.flags & DATA_BUFFER_DESCRIPTOR_FORMAT_MASK )
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

/* get_srp_login_request_multi_channel_action */
/*!
Returns the multi channel action setting

@param p_information_unit - pointer to the IU structure

@return - multi channel action setting
*/
static inline
MULTI_CHANNEL_ACTION
get_srp_login_request_multi_channel_action(
	IN  srp_login_req_t *p_information_unit )
{
	return( ( MULTI_CHANNEL_ACTION ) ( p_information_unit->flags & MULTI_CHANNEL_ACTION_MASK ) );
}

/* get_srp_login_request_initiator_port_id */
/*!
Returns the initiator port identifier

@param p_information_unit - pointer to the IU structure

@return - pointer to initiator port id value
*/
static inline
srp_ib_port_id_t*
get_srp_login_request_initiator_port_id(
	IN  srp_login_req_t *p_information_unit )
{
	return( &p_information_unit->initiator_port_id );
}

/* get_srp_login_request_target_port_id */
/*!
Returns the target port identifier

@param p_information_unit - pointer to the IU structure

@return - pointer to target port id value
*/
static inline
srp_ib_port_id_t*
get_srp_login_request_target_port_id(
	IN  srp_login_req_t *p_information_unit )
{
	return( &p_information_unit->target_port_id );
}

/* get_srp_login_request_length */
/*!
Returns the size in bytes of the Srp Login Request IU

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint32_t
get_srp_login_request_length(
	IN  srp_login_req_t *p_information_unit )
{
	return( sizeof( *p_information_unit ) );
}

/* set_srp_login_request_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_login_request_from_host_to_network(
	IN OUT  srp_login_req_t *p_information_unit )
{
	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
	p_information_unit->req_max_init_to_targ_iu        = cl_hton32( p_information_unit->req_max_init_to_targ_iu );
}

/* set_srp_login_request_from_network_to_host */
/*!
Swaps the IU fields from Network to Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_login_request_from_network_to_host(
	IN OUT  srp_login_req_t *p_information_unit )
{
	set_srp_login_request_from_host_to_network ( p_information_unit );
}

#endif /* SRP_LOGIN_REQ_H_INCLUDED */
