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


#ifndef SRP_TSK_MGMT_H_INCLUDED
#define SRP_TSK_MGMT_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_information_unit.h"

/* set_srp_tsk_mgmt_tag */
/*!
Sets the tag field of a task management information unit

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value of IU

@return - none
*/
static inline
void
set_srp_tsk_mgmt_tag(
	IN OUT  srp_tsk_mgmt_t  *p_information_unit,
	IN      uint64_t        iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_tsk_mgmt */
/*!
Initializes the task management IU to zeroes
and sets the IU type to Srp Target Logout
and sets the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_tsk_mgmt(
	IN OUT  srp_tsk_mgmt_t  *p_information_unit,
	IN      uint64_t        iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_TSK_MGMT ) ;
	set_srp_tsk_mgmt_tag( p_information_unit, iu_tag );
}

/* set_srp_tsk_mgmt_logical_unit_number */
/*!
Sets the logical unit number for the task management request IU

@param p_information_unit  - pointer to the IU structure
@param logical_unit_number - logical unit number

@return - none
*/
static inline
void
set_srp_tsk_mgmt_logical_unit_number(
	IN OUT  srp_tsk_mgmt_t  *p_information_unit,
	IN      uint64_t        logical_unit_number )
{
	p_information_unit->logical_unit_number = logical_unit_number;
}

/* set_srp_tsk_mgmt_task_management_flags */
/*!
Sets the task management flags for a task management IU

@param p_information_unit    - pointer to the IU structure
@param task_management_flags - logical unit number

@return - none
*/
static inline
void
set_srp_tsk_mgmt_task_management_flags(
	IN OUT  srp_tsk_mgmt_t  *p_information_unit,
	IN      uint8_t         task_management_flags )
{
	p_information_unit->task_management_flags = task_management_flags;
}

/* set_srp_tsk_mgmt_managed_task_tag */
/*!
Sets the task management flags for a task management IU

@param p_information_unit - pointer to the IU structure
@param managed_task_tag   - id of task to be managed

@return - none
*/
static inline
void
set_srp_tsk_mgmt_managed_task_tag(
	IN OUT  srp_tsk_mgmt_t  *p_information_unit,
	IN      uint64_t        managed_task_tag )
{
	p_information_unit->managed_task_tag = managed_task_tag;
}

/* setup_srp_tsk_mgmt */
/*!
Initializes and sets the Srp Task Management IU to the values supplied

@param p_information_unit    - pointer to the IU structure
@param iu_tag                - tag value to be used for the req/rsp pair
@param logical_unit_number   - logical unit number
@param task_management_flags - logical unit number
@param managed_task_tag      - id of task to be managed

@return - none
*/
static inline
void
setup_srp_tsk_mgmt(
	IN OUT  srp_tsk_mgmt_t *p_information_unit,
	IN      uint64_t        iu_tag,
	IN      uint64_t        logical_unit_number,
	IN      uint8_t         task_management_flags,
	IN      uint64_t        managed_task_tag )
{
	init_srp_tsk_mgmt( p_information_unit, iu_tag );
	set_srp_tsk_mgmt_logical_unit_number( p_information_unit, logical_unit_number );
	set_srp_tsk_mgmt_task_management_flags( p_information_unit, task_management_flags );
	set_srp_tsk_mgmt_managed_task_tag( p_information_unit, managed_task_tag );
}

/* get_srp_tsk_mgmt_tag */
/*!
Returns the value of the tag field of a task management iu

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_tsk_mgmt_tag(
	IN  srp_tsk_mgmt_t  *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_tsk_mgmt_logical_unit_number */
/*!
Returns the value of the logical unit number field of a task management iu

@param p_information_unit - pointer to the IU structure

@return - logical unit number
*/
static inline
uint64_t
get_srp_tsk_mgmt_logical_unit_number(
	IN  srp_tsk_mgmt_t  *p_information_unit )
{
	return( p_information_unit->logical_unit_number );
}

/* get_srp_tsk_mgmt_task_management_flags */
/*!
Returns the value of the task management flags field of a task management iu

@param p_information_unit - pointer to the IU structure

@return - task management flags
*/
static inline
uint8_t
get_srp_tsk_mgmt_task_management_flags(
	IN  srp_tsk_mgmt_t  *p_information_unit )
{
	return( p_information_unit->task_management_flags );
}

/* get_srp_tsk_mgmt_managed_task_tag */
/*!
Returns the value of the managed task tag field of a task management iu

@param p_information_unit - pointer to the IU structure

@return - managed task tag
*/
static inline
uint64_t
get_srp_tsk_mgmt_managed_task_tag(
	IN  srp_tsk_mgmt_t  *p_information_unit )
{
	return( p_information_unit->managed_task_tag );
}

/* get_srp_tsk_mgmt_length */
/*!
Returns the size in bytes of the Srp Task Management IU

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint32_t
get_srp_tsk_mgmt_length(
	IN  srp_tsk_mgmt_t  *p_information_unit )
{
	return( sizeof( *p_information_unit ) );
}

/* set_srp_tsk_mgmt_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_tsk_mgmt_from_host_to_network(
	IN OUT  srp_tsk_mgmt_t  *p_information_unit )
{
	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
	p_information_unit->logical_unit_number = cl_hton64( p_information_unit->logical_unit_number );
	p_information_unit->managed_task_tag    = cl_hton64( p_information_unit->managed_task_tag );
}

/* set_srp_tsk_mgmt_from_network_to_host */
/*!
Swaps the IU fields from Network To Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_tsk_mgmt_from_network_to_host(
	IN OUT  srp_tsk_mgmt_t  *p_information_unit )
{
	set_srp_tsk_mgmt_from_host_to_network ( p_information_unit );
}

#endif /* SRP_TSK_MGMT_H_INCLUDED */
