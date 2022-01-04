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


#ifndef SRP_INFORMATION_UNIT_H_INCLUDED
#define SRP_INFORMATION_UNIT_H_INCLUDED

#include "srp_iu_buffer.h"
#include <complib/cl_byteswap.h>

/* set_srp_information_unit_tag */
/*!
Set the Information Unit tag for the Srp buffer

@param p_information_unit - pointer to the IU structure
@param iuTag              - IU tag value
*/
static inline
void
set_srp_information_unit_tag(
	IN OUT  srp_information_unit_t  *p_information_unit,
			uint64_t                iu_tag )
{
	p_information_unit->tag = iu_tag;
}

/* get_srp_information_unit_tag */
/*!
Returns the Information Unit tag for the Srp buffer

@param p_information_unit - pointer to the IU structure

@return - IU tag field value
*/
static inline
uint64_t
get_srp_information_unit_tag(
	IN  srp_information_unit_t  *p_information_unit )
{
	return( p_information_unit->tag );
}

/* set_srp_information_unit_from_host_to_network */
/*!
Swaps the tag field bytes from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - NONE
*/

static inline
void
set_srp_information_unit_from_host_to_network(
	IN OUT  srp_information_unit_t  *p_information_unit )
{
	UNUSED_PARAM( p_information_unit );
//  p_information_unit->tag = cl_hton64( p_information_unit->tag );
}

/* set_srp_information_unit_from_network_to_host */
/*!
Swaps the tag field bytes from Network To Host ordering

@param p_information_unit - pointer to the IU structure

@return - NONE
*/

static inline
void
set_srp_information_unit_from_network_to_host(
	IN OUT  srp_information_unit_t  *p_information_unit )
{
	set_srp_information_unit_from_host_to_network ( p_information_unit );
}

#endif /* SRP_INFORMATION_UNIT_H_INCLUDED */
