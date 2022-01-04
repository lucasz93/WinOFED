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


#ifndef SRP_CRED_REQ_H_INCLUDED
#define SRP_CRED_REQ_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_information_unit.h"

/* set_srp_credit_request_tag */
/*!
Sets the tag field of a credit request information unit

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value of IU

@return - none
*/
static inline
void
set_srp_credit_request_tag(
	IN OUT  srp_cred_req_t  *p_information_unit,
	IN      uint64_t        iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_credit_request */
/*!
Initializes the credit request IU to zeroes
and sets the IU type to Srp credit request
and sets the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_credit_request(
	IN OUT  srp_cred_req_t  *p_information_unit,
	IN      uint64_t        iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_CRED_REQ ) ;
	set_srp_credit_request_tag( p_information_unit, iu_tag );
}

/* set_srp_credit_request_request_limit_delta */
/*!
Sets the request limit delta for the credit request

@param p_information_unit  - pointer to the IU structure
@param request_limit_delta - flow control request limit delta

@return - none
*/
static inline
void
set_srp_credit_request_request_limit_delta(
	IN OUT  srp_cred_req_t  *p_information_unit,
	IN      int32_t         request_limit_delta )
{
	p_information_unit->request_limit_delta = request_limit_delta;
}

/* setup_srp_credit_request */
/*!
Initializes and sets the Srp credit request IU to the values supplied

@param p_information_unit  - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair
@param request_limit_delta - flow control request limit delta

@return - none
*/
static inline
void
setup_srp_credit_request(
	IN OUT  srp_cred_req_t  *p_information_unit,
	IN      uint64_t        iu_tag,
	IN      int32_t         request_limit_delta )
{
	init_srp_credit_request( p_information_unit, iu_tag );
	set_srp_credit_request_request_limit_delta( p_information_unit, request_limit_delta );
}

/* get_srp_credit_request_tag */
/*!
Returns the value of the tag field of a credit request

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_credit_request_tag(
	IN  srp_cred_req_t  *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_credit_request_request_limit_delta */
/*!
Returns the value of the request limit delta field of a credit request

@param p_information_unit - pointer to the IU structure

@return - request limit delta value
*/
static inline
int32_t
get_srp_credit_request_request_limit_delta(
	IN  srp_cred_req_t  *p_information_unit )
{
	return( p_information_unit->request_limit_delta );
}

/* get_srp_credit_request_length */
/*!
Returns the size in bytes of the Srp credit request IU

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint32_t
get_srp_credit_request_length(
	IN  srp_cred_req_t  *p_information_unit )
{
	return( sizeof( *p_information_unit ) );
}

/* set_srp_credit_request_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_credit_request_from_host_to_network(
	IN OUT  srp_cred_req_t  *p_information_unit )
{
	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
	p_information_unit->request_limit_delta = cl_hton32( p_information_unit->request_limit_delta );
}

/* set_srp_credit_request_from_network_to_host */
/*!
Swaps the IU fields from Network to Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_credit_request_from_network_to_host(
	IN OUT  srp_cred_req_t  *p_information_unit )
{
	set_srp_credit_request_from_host_to_network ( p_information_unit );
}

#endif /* SRP_CRED_REQ_H_INCLUDED */
