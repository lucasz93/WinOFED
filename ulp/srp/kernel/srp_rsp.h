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


#ifndef SRP_RSP_H_INCLUDED
#define SRP_RSP_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_information_unit.h"

/* set_srp_response_tag */
/*!
Sets the tag field of a Response information unit

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value of IU

@return - none
*/
static inline
void
set_srp_response_tag(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint64_t    iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_response */
/*!
Initializes the Response IU to zeroes
and sets the IU type to Response
and sets the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_response(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint64_t    iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_RSP ) ;
	set_srp_response_tag( p_information_unit, iu_tag );
}

/* set_srp_response_request_limit_delta */
/*!
sets the request limit delta value

@param p_information_unit  - pointer to the IU structure
@param request_limit_delta - buffer descriptor format value

@return - none
*/
static inline
void
set_srp_response_request_limit_delta(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      int32_t     request_limit_delta )
{
	p_information_unit->request_limit_delta = request_limit_delta;
}

/* set_srp_response_flags */
/*!
sets the flags field

@param p_information_unit - pointer to the IU structure
@param flags              - flags  di_under di_over do_under do_over sns_valid RSPVALID

@return - none
*/
static inline
void
set_srp_response_flags(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint8_t     flags )
{
	p_information_unit->flags = flags & 0x3F;
}

/* set_srp_response_di_under */
/*!
sets the DIUNDER flag

@param p_information_unit - pointer to the IU structure
@param di_under           - DIUNDER flag

this is a boolean flag and therefore any non-zero value is true
while zero is of course then false

@return - none
*/
static inline
void
set_srp_response_di_under(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint8_t     di_under )
{
	p_information_unit->flags = ( p_information_unit->flags & 0xDF ) | ( di_under == 0 ? 0 : 0x20 );
}

/* set_srp_response_di_over */
/*!
sets the DIOVER flag

@param p_information_unit - pointer to the IU structure
@param di_over            - DIOVER flag

this is a boolean flag and therefore any non-zero value is true
while zero is of course then false

@return - none
*/
static inline
void
set_srp_response_di_over(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint8_t     di_over )
{
	p_information_unit->flags = ( p_information_unit->flags & 0xEF ) | ( di_over == 0 ? 0 : 0x10 );
}

/* set_srp_response_do_under */
/*!
sets the DOUNDER flag

@param p_information_unit - pointer to the IU structure
@param do_under           - DOUNDER flag

this is a boolean flag and therefore any non-zero value is true
while zero is of course then false

@return - none
*/
static inline
void
set_srp_response_do_under(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint8_t     do_under )
{
	p_information_unit->flags = ( p_information_unit->flags & 0xF7 ) | ( do_under == 0 ? 0 : 0x08 );
}

/* set_srp_response_do_over */
/*!
sets the DOOVER flag

@param p_information_unit - pointer to the IU structure
@param do_over            - DOOVER flag

this is a boolean flag and therefore any non-zero value is true
while zero is of course then false

@return - none
*/
static inline
void
set_srp_response_do_over(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint8_t     do_over )
{
	p_information_unit->flags = ( p_information_unit->flags & 0xFB ) | ( do_over == 0 ? 0 : 0x04 );
}

/* set_srp_response_sns_valid */
/*!
sets the SNSVALID flag

@param p_information_unit - pointer to the IU structure
@param sns_valid          - SNSVALID flag

this is a boolean flag and therefore any non-zero value is true
while zero is of course then false

@return - none
*/
static inline
void
set_srp_response_sns_valid(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint8_t     sns_valid )
{
	p_information_unit->flags = ( p_information_unit->flags & 0xFD ) | ( sns_valid == 0 ? 0 : 0x02 );
}

/* set_srp_response_rsp_valid */
/*!
sets the RSPVALID flag

@param p_information_unit - pointer to the IU structure
@param rsp_valid          - RSPVALID flag

this is a boolean flag and therefore any non-zero value is true
while zero is of course then false

@return - none
*/
static inline
void
set_srp_response_rsp_valid(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint8_t     rsp_valid )
{
	p_information_unit->flags = ( p_information_unit->flags & 0xFE ) | ( rsp_valid == 0 ? 0 : 0x01 );
}

/* set_srp_response_status */
/*!
sets the Status value

@param p_information_unit - pointer to the IU structure
@param status             - Status value

@return - none
*/
static inline
void
set_srp_response_status(
	IN  OUT srp_rsp_t   *p_information_unit,
	IN      uint8_t     status )
{
	p_information_unit->status = status;
}

/* set_srp_response_data_out_residual_count */
/*!
Sets the data out residual count for the Response IU

@param p_information_unit      - pointer to the IU structure
@param data_out_residual_count - data out residual count for the request

@return - none
*/
static inline
void
set_srp_response_data_out_residual_count(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint32_t    data_out_residual_count )
{
	p_information_unit->data_out_residual_count = data_out_residual_count;
}

/* set_srp_response_data_in_residual_count */
/*!
Sets the data in residual count for the Response IU

@param p_information_unit     - pointer to the IU structure
@param data_in_residual_count - data out residual count for the request

@return - none
*/
static inline
void
set_srp_response_data_in_residual_count(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint32_t    data_in_residual_count )
{
	p_information_unit->data_in_residual_count = data_in_residual_count;
}

/* set_srp_response_sense_data_list_length */
/*!
Sets the sense data list length for the Response IU

@param p_information_unit     - pointer to the IU structure
@param sense_data_list_length - sense data list length for the request

@return - none
*/
static inline
void
set_srp_response_sense_data_list_length(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint32_t    sense_data_list_length )
{
	p_information_unit->sense_data_list_length = sense_data_list_length;
}

/* set_srp_response_response_data_list_length */
/*!
Sets the response data list length for the Response IU

@param p_information_unit        - pointer to the IU structure
@param response_data_list_length - response data list length for the request

@return - none
*/
static inline
void
set_srp_response_response_data_list_length(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint32_t    response_data_list_length )
{
	p_information_unit->response_data_list_length = response_data_list_length;
}

/* setup_srp_response */
/*!
Initializes and sets the Srp Response IU to the values supplied

@param p_information_unit        - pointer to the IU structure
@param iu_tag                    - tag value to be used for the req/rsp pair
@param request_limit_delta       - buffer descriptor format value
@param di_under                  - DIUNDER flag
@param di_over                   - DIOVER flag
@param do_under                  - DOUNDER flag
@param do_over                   - DOOVER flag
@param sns_valid                 - SNSVALID flag
@param rsp_valid                 - RSPVALID flag
@param status                    - Status value
@param data_out_residual_count   - data out residual count for the request
@param data_in_residual_count    - data out residual count for the request
@param sense_data_list_length    - sense data list length for the request
@param response_data_list_length - response data list length for the request

@return - none
*/
static inline
void
setup_srp_response(
	IN OUT  srp_rsp_t   *p_information_unit,
	IN      uint64_t    iu_tag,
	IN      int32_t     request_limit_delta,
	IN      uint8_t     di_under,
	IN      uint8_t     di_over,
	IN      uint8_t     do_under,
	IN      uint8_t     do_over,
	IN      uint8_t     sns_valid,
	IN      uint8_t     rsp_valid,
	IN      uint8_t     status,
	IN      uint32_t    data_out_residual_count,
	IN      uint32_t    data_in_residual_count,
	IN      uint32_t    sense_data_list_length,
	IN      uint32_t    response_data_list_length )
{
	init_srp_response( p_information_unit, iu_tag );
	set_srp_response_request_limit_delta( p_information_unit, request_limit_delta );
	set_srp_response_di_under( p_information_unit, di_under );
	set_srp_response_di_over( p_information_unit, di_over );
	set_srp_response_do_under( p_information_unit, do_under );
	set_srp_response_do_over( p_information_unit, do_over );
	set_srp_response_sns_valid( p_information_unit, sns_valid );
	set_srp_response_rsp_valid( p_information_unit, rsp_valid );
	set_srp_response_status( p_information_unit, status );
	set_srp_response_data_out_residual_count( p_information_unit, data_out_residual_count );
	set_srp_response_data_in_residual_count( p_information_unit, data_in_residual_count );
	set_srp_response_sense_data_list_length( p_information_unit,  sense_data_list_length );
	set_srp_response_response_data_list_length( p_information_unit, response_data_list_length );
}

/* get_srp_response_tag */
/*!
Returns the value of the tag field of a response IU

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_response_tag(
	IN  srp_rsp_t   *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_response_request_limit_delta */
/*!
Returns the value of the request limit delta field of a Response IU

@param p_information_unit - pointer to the IU structure

@return - request limit delta value
*/
static inline
int32_t
get_srp_response_request_limit_delta(
	IN  srp_rsp_t   *p_information_unit )
{
	return( p_information_unit->request_limit_delta );
}

/* get_srp_response_flags */
/*!
Returns flags field of a Response IU

@param p_information_unit  - pointer to the IU structure

@return - value of the flags field
*/
static inline
uint8_t
get_srp_response_flags(
	IN  srp_rsp_t   *p_information_unit )
{
	return( p_information_unit->flags );
}

/* get_srp_response_di_under */
/*!
Returns the DIUNDER flag setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - set/clear setting
		  one indicates set while zero indicates not set
*/
static inline
uint8_t
get_srp_response_di_under(
	IN  srp_rsp_t   *p_information_unit )
{
	return( ( p_information_unit->flags & 0x20 ) != 0 ? 1 : 0 );
}

/* get_srp_response_di_over */
/*!
Returns the DIOVER flag setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - set/clear setting
		  one indicates set while zero indicates not set
*/
static inline
uint8_t
get_srp_response_di_over(
	IN  srp_rsp_t   *p_information_unit )
{
	return( ( p_information_unit->flags & 0x10 ) != 0 ? 1 : 0 );
}

/* get_srp_response_do_under */
/*!
Returns the DOUNDER flag setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - set/clear setting
		  one indicates set while zero indicates not set
*/
static inline
uint8_t
get_srp_response_do_under(
	IN  srp_rsp_t   *p_information_unit )
{
	return( ( p_information_unit->flags & 0x08 ) != 0 ? 1 : 0 );
}

/* get_srp_response_do_over */
/*!
Returns the DOOVER flag setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - set/clear setting
		  one indicates set while zero indicates not set
*/
static inline
uint8_t
get_srp_response_do_over(
	IN  srp_rsp_t   *p_information_unit )
{
	return( ( p_information_unit->flags & 0x04 ) != 0 ? 1 : 0 );
}

/* get_srp_response_sns_valid */
/*!
Returns the SNSVALID flag setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - set/clear setting
		  one indicates set while zero indicates not set
*/
static inline
uint8_t
get_srp_response_sns_valid(
	IN  srp_rsp_t   *p_information_unit )
{
	return( ( p_information_unit->flags & 0x02 ) != 0 ? 1 : 0 );
}

/* get_srp_response_rsp_valid */
/*!
Returns the RSPVALID flag setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - set/clear setting
		  one indicates set while zero indicates not set
*/
static inline
uint8_t
get_srp_response_rsp_valid(
	IN  srp_rsp_t   *p_information_unit )
{
	return( ( p_information_unit->flags & 0x01 ) != 0 ? 1 : 0 );
}

/* get_srp_response_status */
/*!
Returns the Status field setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - Status value setting
*/
static inline
uint8_t
get_srp_response_status(
	IN  srp_rsp_t   *p_information_unit )
{
	return( p_information_unit->status );
}

/* get_srp_response_data_out_residual_count */
/*!
Returns the data out residual count field setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - data out residual count value setting
*/
static inline
uint32_t
get_srp_response_data_out_residual_count(
	IN  srp_rsp_t   *p_information_unit )
{
	return( p_information_unit->data_out_residual_count );
}

/* get_srp_response_data_in_residual_count */
/*!
Returns the data in residual count field setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - data in residual count value setting
*/
static inline
uint32_t
get_srp_response_data_in_residual_count(
	IN  srp_rsp_t   *p_information_unit )
{
	return( p_information_unit->data_in_residual_count );
}

/* get_srp_response_sense_data_list_length */
/*!
Returns the sense data list length field setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - sense data list length value setting
*/
static inline
uint32_t
get_srp_response_sense_data_list_length(
	IN  srp_rsp_t   *p_information_unit )
{
	return( p_information_unit->sense_data_list_length );
}

/* get_srp_response_response_data_list_length */
/*!
Returns the response data list length field setting of a Response IU

@param p_information_unit - pointer to the IU structure

@return - response data list length value setting
*/
static inline
uint32_t
get_srp_response_response_data_list_length(
	IN  srp_rsp_t   *p_information_unit )
{
	return( p_information_unit->response_data_list_length );
}

/* get_srp_response_response_data */
/*!
Returns a pointer to the response data field of a Response

@param p_information_unit - pointer to the IU structure

@return - pointer to the response data
*/
static inline
srp_response_data_t*
get_srp_response_response_data(
	IN  srp_rsp_t   *p_information_unit )
{
	if ( get_srp_response_rsp_valid( p_information_unit ) )
		return( p_information_unit->response_data );

	return( NULL );
}

/* get_srp_response_sense_data */
/*!
Returns a pointer to the sense data field of a Response

WARNING!!!! Set the response data list length before this call so the
			offset can be correctly calculated

@param p_information_unit - pointer to the IU structure

@return - pointer to sense data
*/
static inline
uint8_t*
get_srp_response_sense_data(
	IN  srp_rsp_t   *p_information_unit )
{
	if ( get_srp_response_sns_valid( p_information_unit ) )
	{
		if ( get_srp_response_response_data( p_information_unit ) != NULL )
		{
			return( ( ( uint8_t* ) p_information_unit->response_data ) + get_srp_response_response_data_list_length( p_information_unit ) );
		}
		else
		{
			return( ( uint8_t* ) p_information_unit->response_data );
		}
	}

	return( NULL );
}

/* get_srp_response_length */
/*!
Returns the size in bytes of the Srp Response IU

@param p_information_unit - pointer to the IU structure

@return - used length of Response IU buffer
*/
static inline
uint32_t
get_srp_response_length(
	IN  srp_rsp_t   *p_information_unit )
{
	/* do not include response data field in the sizeof the IU. add it's length and sense data list length to the structure size */
	return( ( sizeof( *p_information_unit ) - sizeof( p_information_unit->response_data ) ) +
			( get_srp_response_rsp_valid( p_information_unit ) ? get_srp_response_response_data_list_length( p_information_unit ) : 0 ) +
			( get_srp_response_sns_valid( p_information_unit ) ? get_srp_response_sense_data_list_length( p_information_unit ) : 0 ) );
}

/* set_srp_response_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_response_from_host_to_network(
	IN OUT  srp_rsp_t   *p_information_unit )
{
	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
	p_information_unit->request_limit_delta      = cl_hton32( p_information_unit->request_limit_delta );
	if ( get_srp_response_flags( p_information_unit ) != 0 )
	{
		p_information_unit->data_out_residual_count   = cl_hton32( p_information_unit->data_out_residual_count );
		p_information_unit->data_in_residual_count    = cl_hton32( p_information_unit->data_in_residual_count );
		p_information_unit->sense_data_list_length    = cl_hton32( p_information_unit->sense_data_list_length );
		p_information_unit->response_data_list_length = cl_hton32( p_information_unit->response_data_list_length );
	}
}

/* set_srp_response_from_network_to_host */
/*!
Swaps the IU fields from Network to Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_response_from_network_to_host(
	IN OUT  srp_rsp_t   *p_information_unit )
{
	set_srp_response_from_host_to_network ( p_information_unit );
}

#endif /* SRP_RSP_H_INCLUDED */
