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


#ifndef SRP_LOGIN_REJ_H_INCLUDED
#define SRP_LOGIN_REJ_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_Information_unit.h"

/* set_srp_login_reject_tag */
/*!
Sets the tag field of a login reject information unit

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value of IU

@return - none
*/
static inline
void
set_srp_login_reject_tag(
	IN OUT  srp_login_rej_t *p_information_unit,
	IN      uint64_t        iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_login_reject */
/*!
Initializes the login reject IU to zeroes
and sets the IU type to Srp Login Reject
and sets the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_login_reject(
	IN OUT  srp_login_rej_t *p_information_unit,
	IN      uint64_t        iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_LOGIN_REJ ) ;
	set_srp_login_reject_tag( p_information_unit, iu_tag ) ;
}

/* set_srp_login_reject_reason */
/*!
Sets the reason for the rejection

@param p_information_unit  - pointer to the IU structure
@param reason              - rejection reason code

@return - none
*/
static inline
void
set_srp_login_reject_reason(
	IN OUT  srp_login_rej_t     *p_information_unit,
	IN      LOGIN_REJECT_CODE   reason )
{
	p_information_unit->reason = reason;
}

/* set_srp_login_reject_supported_data_buffer_formats */
/*!
Sets the flags indicating the type of data buffer descriptors
which are supported by the target on this channel

@param p_information_unit             - pointer to the IU structure
@param data_buffer_descriptor_formats - usage indicator values

@return - none
*/
static inline
void
set_srp_login_reject_supported_data_buffer_formats(
	IN OUT  srp_login_rej_t                 *p_information_unit,
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

/* setup_srp_login_reject */
/*!
Initializes and sets the Srp Login Reject IU to the values supplied

@param p_information_unit             - pointer to the IU structure
@param iu_tag                         - tag value to be used for the req/rsp pair
@param reason                         - reason code for login rejection
@param data_buffer_descriptor_formats - usage indicator values

@return - none
*/
static inline
void
setup_srp_login_reject(
	IN OUT  srp_login_rej_t                 *p_information_unit,
	IN      uint64_t                        iu_tag,
	IN      LOGIN_REJECT_CODE               reason,
	IN      DATA_BUFFER_DESCRIPTOR_FORMAT   data_buffer_descriptor_formats )
{
	init_srp_login_reject( p_information_unit, iu_tag );
	set_srp_login_reject_reason( p_information_unit, reason );
	set_srp_login_reject_supported_data_buffer_formats( p_information_unit, data_buffer_descriptor_formats );
}

/* get_srp_login_reject_tag */
/*!
Returns the value of the tag field of a login reject

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_login_reject_tag(
	IN  srp_login_rej_t *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_login_reject_reason */
/*!
Returns the value of the reason code field of a login reject

@param p_information_unit - pointer to the IU structure

@return - reason code value
*/
static inline
LOGIN_REJECT_CODE
get_srp_login_reject_reason(
	IN  srp_login_rej_t *p_information_unit )
{
	return( ( LOGIN_REJECT_CODE ) p_information_unit->reason );
}

/* get_srp_login_reject_supported_data_buffer_formats */
/*!
Returns the supported data buffer formats that can be used on the channel

@param p_information_unit - pointer to the IU structure

@return - supported data buffer formats settings
*/
static inline
DATA_BUFFER_DESCRIPTOR_FORMAT
get_srp_login_reject_supported_data_buffer_formats(
	IN  srp_login_rej_t *p_information_unit )
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

/* get_srp_login_reject_length */
/*!
Returns the size in bytes of the Srp Login Reject IU

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint32_t
get_srp_login_reject_length(
	IN  srp_login_rej_t *p_information_unit )
{
	return( sizeof( *p_information_unit ) );
}

/* set_srp_login_reject_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_login_reject_from_host_to_network(
	IN OUT  srp_login_rej_t *p_information_unit )
{
	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
	p_information_unit->reason = cl_hton32( p_information_unit->reason );
}

/* set_srp_login_reject_from_network_to_host */
/*!
Swaps the IU fields from Network to Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_login_reject_from_network_to_host(
	IN OUT  srp_login_rej_t *p_information_unit )
{
	set_srp_login_reject_from_host_to_network ( p_information_unit );
}

#endif /* SRP_LOGIN_REJ_H_INCLUDED */
