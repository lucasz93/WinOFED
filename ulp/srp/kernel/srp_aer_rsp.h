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


#ifndef SRP_AER_RSP_H_INCLUDED
#define SRP_AER_RSP_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_Information_unit.h"

/* set_srp_async_event_response_tag */
/*!
Sets the tag field of a Async Event response information unit

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value of IU

@return - none
*/
static inline
void
set_srp_async_event_response_tag(
	IN OUT  srp_aer_rsp_t   *p_information_unit,
	IN      uint64_t        iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_async_event_response */
/*!
Initializes the Async Event response IU to zeroes
and sets the IU type to Srp Async Event Response
and sets the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_async_event_response(
	IN OUT srp_aer_rsp_t    *p_information_unit,
	IN      uint64_t        iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_AER_RSP ) ;
	set_srp_async_event_response_tag( p_information_unit, iu_tag );
}

/* setup_srp_async_event_response */
/*!
Initializes and sets the Srp Async Event Response IU to the values supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
setup_srp_async_event_response(
	IN OUT  srp_aer_rsp_t   *p_information_unit,
	IN      uint64_t        iu_tag )
{
	init_srp_async_event_response( p_information_unit, iu_tag );
}

/* get_srp_async_event_response_tag */
/*!
Returns the value of the tag field of a Async Event response

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_async_event_response_tag(
	IN  srp_aer_rsp_t   *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_async_event_response_length */
/*!
Returns the size in bytes of the Srp AsyncEvent Response IU

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint32_t
get_srp_async_event_response_length(
	IN  srp_aer_rsp_t   *p_information_unit )
{
	return( sizeof( *p_information_unit ) );
}

/* set_srp_async_event_response_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_async_event_response_from_host_to_network(
	IN OUT  srp_aer_rsp_t   *p_information_unit )
{
	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
}

/* set_srp_async_event_response_from_network_to_host */
/*!
Swaps the IU fields from Network to Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_async_event_response_from_network_to_host(
	IN OUT  srp_aer_rsp_t   *p_information_unit )
{
	set_srp_async_event_response_from_host_to_network ( p_information_unit );
}

#endif /* SRP_AER_RSP_H_INCLUDED */
