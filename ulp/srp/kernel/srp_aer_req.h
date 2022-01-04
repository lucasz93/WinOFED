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


#ifndef SRP_AER_REQ_H_INCLUDED
#define SRP_AER_REQ_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_Information_unit.h"

/* set_srp_async_event_request_tag */
/*!
Sets the tag field of a AsyncEvent request information unit

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value of IU

@return - none
*/
static inline
void
set_srp_async_event_request_tag(
	IN OUT  srp_aer_req_t   *p_information_unit,
	IN      uint64_t        iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_async_event_request */
/*!
Initializes the AsyncEvent request IU to zeroes
and sets the IU type to Srp AsyncEvent request
and sets the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_async_event_request(
	IN OUT  srp_aer_req_t   *p_information_unit,
	IN      uint64_t        iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_AER_REQ ) ;
	set_srp_async_event_request_tag( p_information_unit, iu_tag );
}

/* set_srp_async_event_request_request_limit_delta */
/*!
Sets the request limit delta for the AsyncEvent request

@param p_information_unit  - pointer to the IU structure
@param request_limit_delta - flow control request limit delta

@return - none
*/
static inline
void
set_srp_async_event_request_request_limit_delta(
	IN OUT  srp_aer_req_t   *p_information_unit,
	IN      int32_t         request_limit_delta )
{
	p_information_unit->request_limit_delta = request_limit_delta;
}

/* set_srp_async_event_request_logical_unit_number */
/*!
Sets the logical unit number for the AsyncEvent request

@param p_information_unit  - pointer to the IU structure
@param logical_unit_number - logical unit number for request

@return - none
*/
static inline
void
set_srp_async_event_request_logical_unit_number(
	IN OUT  srp_aer_req_t   *p_information_unit,
	IN      uint64_t        logical_unit_number )
{
	p_information_unit->logical_unit_number = logical_unit_number;
}

/* set_srp_async_event_request_sense_data_list_length */
/*!
Sets the sense data list length for the AsyncEvent request

@param p_information_unit     - pointer to the IU structure
@param sense_data_list_length - length of sense data

@return - none
*/
static inline
void
set_srp_async_event_request_sense_data_list_length(
	IN OUT  srp_aer_req_t   *p_information_unit,
	IN      uint32_t        sense_data_list_length )
{
	p_information_unit->sense_data_list_length = sense_data_list_length;
}

/* setup_srp_async_event_request */
/*!
Initializes and sets the Srp AsyncEvent request IU to the values supplied

@param p_information_unit     - pointer to the IU structure
@param iu_tag                 - tag value to be used for the req/rsp pair
@param request_limit_delta    - flow control request limit delta
@param logical_unit_number    - logical unit number for request
@param sense_data_list_length - length of sense data

@return - pointer to the sense data area
*/
static inline
uint8_t*
setup_srp_async_event_request(
	IN OUT  srp_aer_req_t   *p_information_unit,
	IN      uint64_t        iu_tag,
	IN      int32_t         request_limit_delta,
	IN      uint64_t        logical_unit_number,
	IN      uint32_t        sense_data_list_length )
{
	init_srp_async_event_request( p_information_unit, iu_tag );
	set_srp_async_event_request_request_limit_delta( p_information_unit, request_limit_delta );
	set_srp_async_event_request_logical_unit_number( p_information_unit, logical_unit_number );
	set_srp_async_event_request_sense_data_list_length( p_information_unit, sense_data_list_length );
	return( p_information_unit->sense_data );
}

/* get_srp_async_event_request_tag */
/*!
Returns the value of the tag field of a AsyncEvent request

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_async_event_request_tag(
	IN  srp_aer_req_t   *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_async_event_request_request_limit_delta */
/*!
Returns the value of the request limit delta field of a AsyncEvent request

@param p_information_unit - pointer to the IU structure

@return - request limit delta value
*/
static inline
int32_t
get_srp_async_event_request_request_limit_delta(
	IN  srp_aer_req_t   *p_information_unit )
{
	return( p_information_unit->request_limit_delta );
}

/* get_srp_async_event_request_logical_unit_number */
/*!
Returns the value of the logical unit number field of a AsyncEvent request

@param p_information_unit - pointer to the IU structure

@return - logical unit number value
*/
static inline
uint64_t
get_srp_async_event_request_logical_unit_number(
	IN  srp_aer_req_t   *p_information_unit )
{
	return( p_information_unit->logical_unit_number );
}

/* get_srp_async_event_request_sense_data_list_length */
/*!
Returns the value of the sense data list length field of a AsyncEvent request

@param p_information_unit - pointer to the IU structure

@return - sense data list length value
*/
static inline
uint32_t
get_srp_async_event_request_sense_data_list_length(
	IN  srp_aer_req_t   *p_information_unit )
{
	return( p_information_unit->sense_data_list_length );
}

/* get_srp_async_event_request_sense_data */
/*!
Returns a pointer to the sense data field of a AsyncEvent request

@param p_information_unit - pointer to the IU structure

@return - pointer to the sense data
*/
static inline
uint8_t*
get_srp_async_event_request_sense_data(
	IN  srp_aer_req_t   *p_information_unit )
{
	return( p_information_unit->sense_data );
}

/* get_srp_async_event_request_length */
/*!
Returns the size in bytes of the Srp AsyncEvent request IU

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint32_t
get_srp_async_event_request_length(
	IN  srp_aer_req_t   *p_information_unit )
{
	/* do not include sense data field in the sizeof the IU. add it's length to the structure size */
	return( ( sizeof( *p_information_unit ) - sizeof( p_information_unit->sense_data ) ) + p_information_unit->sense_data_list_length );
}

/* set_srp_async_event_request_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_async_event_request_from_host_to_network(
	IN OUT  srp_aer_req_t   *p_information_unit )
{
	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
	p_information_unit->request_limit_delta   = cl_hton32( p_information_unit->request_limit_delta );
	p_information_unit->logical_unit_number   = cl_hton64( p_information_unit->logical_unit_number );
	p_information_unit->sense_data_list_length = cl_hton32( p_information_unit->sense_data_list_length );
}

/* set_srp_async_event_request_from_network_to_host */
/*!
Swaps the IU fields from Network to Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_async_event_request_from_network_to_host(
	IN OUT  srp_aer_req_t   *p_information_unit )
{
	set_srp_async_event_request_from_host_to_network ( p_information_unit );
}

#endif /* SRP_AER_REQ_H_INCLUDED */
