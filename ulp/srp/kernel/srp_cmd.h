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


#ifndef SRP_CMD_H_INCLUDED
#define SRP_CMD_H_INCLUDED

#include "srp.h"
#include "srp_iu_buffer.h"
#include "srp_information_unit.h"

/* set_srp_command_tag */
/*!
Sets the tag field of a command information unit

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value of IU

@return - none
*/
static inline
void
set_srp_command_tag(
	IN OUT  srp_cmd_t   *p_information_unit,
	IN      uint64_t    iu_tag )
{
	set_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit, iu_tag );
}

/* init_srp_command */
/*!
Initializes the command IU to zeroes
and sets the IU type to command
and sets the tag to the value supplied

@param p_information_unit - pointer to the IU structure
@param iu_tag             - tag value to be used for the req/rsp pair

@return - none
*/
static inline
void
init_srp_command(
	IN OUT  srp_cmd_t   *p_information_unit,
	IN      uint64_t    iu_tag )
{
	init_srp_iu_buffer( ( srp_iu_buffer_t* ) p_information_unit, SRP_CMD ) ;
	set_srp_command_tag( p_information_unit, iu_tag );
}

/* set_srp_command_data_out_buffer_desc_fmt */
/*!
sets the data out buffer descriptor format value

@param p_information_unit       - pointer to the IU structure
@param data_out_buffer_desc_fmt - buffer descriptor format value

@return - none
*/
static inline
void
set_srp_command_data_out_buffer_desc_fmt(
	IN OUT  srp_cmd_t                       *p_information_unit,
	IN      DATA_BUFFER_DESCRIPTOR_FORMAT   data_out_buffer_desc_fmt )
{
	p_information_unit->data_out_in_buffer_desc_fmt =
		( p_information_unit->data_out_in_buffer_desc_fmt & 0x0F ) | ( (uint8_t)data_out_buffer_desc_fmt << 4 );
}

/* set_srp_command_data_in_buffer_desc_fmt */
/*!
sets the data in buffer descriptor format value

@param p_information_unit      - pointer to the IU structure
@param data_in_buffer_desc_fmt - buffer descriptor format value

@return - none
*/
static inline
void
set_srp_command_data_in_buffer_desc_fmt(
	IN OUT  srp_cmd_t                       *p_information_unit,
	IN      DATA_BUFFER_DESCRIPTOR_FORMAT   data_in_buffer_desc_fmt )
{
	p_information_unit->data_out_in_buffer_desc_fmt = ( p_information_unit->data_out_in_buffer_desc_fmt & 0xF0 ) | (uint8_t)data_in_buffer_desc_fmt;
}

/* set_srp_command_data_out_buffer_desc_count */
/*!
sets the data out buffer descriptor count value

@param p_information_unit       - pointer to the IU structure
@param data_out_buffer_desc_count - buffer descriptor count value

@return - none
*/
static inline
void
set_srp_command_data_out_buffer_desc_count(
	IN OUT  srp_cmd_t   *p_information_unit,
	IN      uint8_t     data_out_buffer_desc_count )
{
	p_information_unit->data_out_buffer_desc_count = data_out_buffer_desc_count;
}

/* set_srp_command_data_in_buffer_desc_count */
/*!
sets the data in buffer descriptor count value

@param p_information_unit        - pointer to the IU structure
@param data_in_buffer_desc_count - buffer descriptor count value

@return - none
*/
static inline
void
set_srp_command_data_in_buffer_desc_count(
	IN OUT  srp_cmd_t   *p_information_unit,
	IN      uint8_t     data_in_buffer_desc_count )
{
	p_information_unit->data_in_buffer_desc_count = data_in_buffer_desc_count;
}

/* set_srp_command_logical_unit_number */
/*!
Sets the logical unit number for the command IU

@param p_information_unit  - pointer to the IU structure
@param logical_unit_number - logical unit number for request

@return - none
*/
static inline
void
set_srp_command_logical_unit_number(
	IN OUT  srp_cmd_t   *p_information_unit,
	IN      uint64_t    logical_unit_number )
{
	p_information_unit->logical_unit_number = logical_unit_number;
}

/* set_srp_command_task_attribute */
/*!
Sets the task attribute for the command IU

@param p_information_unit - pointer to the IU structure
@param task_attribute     - task attribute for the request

@return - none
*/
static inline
void
set_srp_command_task_attribute(
	IN OUT  srp_cmd_t               *p_information_unit,
	IN      TASK_ATTRIBUTE_VALUE    task_attribute )
{
	p_information_unit->flags1 = ( p_information_unit->flags1 & 0xF8 ) | ( (uint8_t)task_attribute );
}

/* set_srp_command_additional_cdb_length */
/*!
Sets the additional CDB length for the command IU

@param p_information_unit    - pointer to the IU structure
@param additional_cdb_length - additional CDB length for the request

@return - none
*/
static inline
void
set_srp_command_additional_cdb_length(
	IN OUT  srp_cmd_t   *p_information_unit,
	IN      uint8_t     additional_cdb_length )
{
	p_information_unit->flags2 = ( p_information_unit->flags2 & 0x03 ) | ( additional_cdb_length << 2 );
}

/* setup_srp_command */
/*!
Initializes and sets the Srp command IU to the values supplied

@param p_information_unit         - pointer to the IU structure
@param iu_tag                     - tag value to be used for the req/rsp pair
@param data_out_buffer_desc_fmt   - buffer descriptor format value
@param data_in_buffer_desc_fmt    - buffer descriptor format value
@param data_out_buffer_desc_count - buffer descriptor count value
@param data_in_buffer_desc_count  - buffer descriptor count value
@param logical_unit_number        - logical unit number for request
@param task_attribute             - task attribute for the request
@param additional_cdb_length      - additional CDB length for the request

@return - pointer to the CDB
*/
static inline
uint8_t*
setup_srp_command(
	IN OUT  srp_cmd_t                   *p_information_unit,
	IN  uint64_t                        iu_tag,
	IN  DATA_BUFFER_DESCRIPTOR_FORMAT   data_out_buffer_desc_fmt,
	IN  DATA_BUFFER_DESCRIPTOR_FORMAT   data_in_buffer_desc_fmt,
	IN  uint8_t                         data_out_buffer_desc_count,
	IN  uint8_t                         data_in_buffer_desc_count,
	IN  uint64_t                        logical_unit_number,
	IN  TASK_ATTRIBUTE_VALUE            task_attribute,
	IN  uint8_t                         additional_cdb_length )
{
	init_srp_command( p_information_unit, iu_tag );
	set_srp_command_data_out_buffer_desc_fmt( p_information_unit, data_out_buffer_desc_fmt );
	set_srp_command_data_in_buffer_desc_fmt( p_information_unit, data_in_buffer_desc_fmt );
	set_srp_command_data_out_buffer_desc_count( p_information_unit, data_out_buffer_desc_count );
	set_srp_command_data_in_buffer_desc_count( p_information_unit, data_in_buffer_desc_count );
	set_srp_command_logical_unit_number( p_information_unit, logical_unit_number );
	set_srp_command_task_attribute( p_information_unit, task_attribute );
	set_srp_command_additional_cdb_length( p_information_unit, additional_cdb_length );
	return( p_information_unit->cdb );
}

/* get_srp_command_tag */
/*!
Returns the value of the tag field of a AsyncEvent request

@param p_information_unit - pointer to the IU structure

@return - tag value
*/
static inline
uint64_t
get_srp_command_tag(
	IN  srp_cmd_t   *p_information_unit )
{
	return( get_srp_information_unit_tag( ( srp_information_unit_t* ) p_information_unit ) );
}

/* get_srp_command_data_out_buffer_desc_fmt */
/*!
Returns the value of the data out buffer descriptor format field of a command

@param p_information_unit - pointer to the IU structure

@return - data out buffer descriptor format value
*/
static inline
DATA_BUFFER_DESCRIPTOR_FORMAT
get_srp_command_data_out_buffer_desc_fmt(
	IN  srp_cmd_t   *p_information_unit )
{
	return( ( DATA_BUFFER_DESCRIPTOR_FORMAT ) ( p_information_unit->data_out_in_buffer_desc_fmt >> 4 ) );
}

/* get_srp_command_data_in_buffer_desc_fmt */
/*!
Returns the value of the data in buffer descriptor format field of a command

@param p_information_unit - pointer to the IU structure

@return - data in buffer descriptor format value
*/
static inline
DATA_BUFFER_DESCRIPTOR_FORMAT
get_srp_command_data_in_buffer_desc_fmt(
	IN  srp_cmd_t   *p_information_unit )
{
	return( ( DATA_BUFFER_DESCRIPTOR_FORMAT ) ( p_information_unit->data_out_in_buffer_desc_fmt & 0x0F ) );
}

/* get_srp_command_data_out_buffer_desc_count */
/*!
Returns the value of the data out buffer descriptor count field of a command

@param p_information_unit - pointer to the IU structure

@return - data out buffer descriptor count value
*/
static inline
uint8_t
get_srp_command_data_out_buffer_desc_count(
	IN  srp_cmd_t   *p_information_unit )
{
	return( p_information_unit->data_out_buffer_desc_count );
}

/* get_srp_command_data_in_buffer_desc_count */
/*!
Returns the value of the data in buffer descriptor count field of a command

@param p_information_unit - pointer to the IU structure

@return - data in buffer descriptor count value
*/
static inline
uint8_t
get_srp_command_data_in_buffer_desc_count(
	IN  srp_cmd_t   *p_information_unit )
{
	return( p_information_unit->data_in_buffer_desc_count );
}

/* get_srp_command_logical_unit_number */
/*!
Returns the value of the logical unit number field of a command IU

@param p_information_unit - pointer to the IU structure

@return - logical unit number value
*/
static inline
uint64_t
get_srp_command_logical_unit_number(
	IN  srp_cmd_t   *p_information_unit )
{
	return( p_information_unit->logical_unit_number );
}

/* get_srp_command_task_attribute */
/*!
Returns the value of the task attribute field of a command

@param p_information_unit - pointer to the IU structure

@return - task attribute value
*/
static inline
TASK_ATTRIBUTE_VALUE
get_srp_command_task_attribute(
	IN  srp_cmd_t   *p_information_unit )
{
	return( ( TASK_ATTRIBUTE_VALUE ) ( p_information_unit->flags1 & 0x07 ) );
}

/* get_srp_command_additional_cdb_length */
/*!
Returns the value of the additional CDB length field of a command

@param p_information_unit - pointer to the IU structure

@return - additional CDB length value
*/
static inline
uint8_t
get_srp_command_additional_cdb_length(
	IN  srp_cmd_t   *p_information_unit )
{
	return( ( uint8_t ) ( p_information_unit->flags2 & 0xFC ) >> 2 );
}

/* get_srp_command_cdb */
/*!
Returns a pointer to the CDB field of a command

@param p_information_unit - pointer to the IU structure

@return - pointer to the CDB
*/
static inline
uint8_t*
get_srp_command_cdb(
	IN  srp_cmd_t   *p_information_unit )
{
	return( p_information_unit->cdb );
}

/* get_srp_command_additional_cdb */
/*!
Returns a pointer to the additional CDB field of a command

@param p_information_unit - pointer to the IU structure

@return - pointer to the additional CDB
*/
static inline
uint8_t*
get_srp_command_additional_cdb(
	IN  srp_cmd_t   *p_information_unit )
{
	if( get_srp_command_additional_cdb_length( p_information_unit ) == 0 )
	{
		return( NULL );
	}

	return( p_information_unit->additional_cdb );
}

/* get_srp_command_data_out_buffer_desc */
/*!
Returns a pointer to the data out buffer desc field of a command

WARNING!!!! Set the additional CDB length before this call so the
			offset can be correctly calculated

@param p_information_unit - pointer to the IU structure

@return - pointer to data out buffer desc
*/
static inline
srp_memory_descriptor_t*
get_srp_command_data_out_buffer_desc(
	IN  srp_cmd_t   *p_information_unit )
{
	if( get_srp_command_data_out_buffer_desc_fmt( p_information_unit ) == DBDF_NO_DATA_BUFFER_DESCRIPTOR_PRESENT )
	{
		return( NULL );
	}

	return( ( srp_memory_descriptor_t* ) ( p_information_unit->additional_cdb + ( get_srp_command_additional_cdb_length( p_information_unit ) * 4 ) ) );
}

/* get_srp_command_data_in_buffer_desc */
/*!
Returns a pointer to the data in buffer desc field of a command

WARNING!!!! Set the additional CDB length and data out buffer descriptor count
			before this call so the offset can be correctly calculated

@param p_information_unit - pointer to the IU structure

@return - pointer to data in buffer desc
*/
static inline
srp_memory_descriptor_t*
get_srp_command_data_in_buffer_desc(
	IN  srp_cmd_t   *p_information_unit )
{
	if( get_srp_command_data_in_buffer_desc_fmt( p_information_unit ) == DBDF_NO_DATA_BUFFER_DESCRIPTOR_PRESENT )
	{
		return( NULL );
	}

	return( ( srp_memory_descriptor_t* ) ( p_information_unit->additional_cdb +
										 ( get_srp_command_additional_cdb_length( p_information_unit ) * 4 ) +
										 ( get_srp_command_data_out_buffer_desc_count( p_information_unit ) * sizeof( srp_memory_descriptor_t ) ) ) );
}

/* get_srp_command_buffer_desc */
/*!
Returns a pointer to the start of the data buffer descs of a command

WARNING!!!! Set the additional CDB length before this call so the
			offset can be correctly calculated

@param p_information_unit - pointer to the IU structure

@return - pointer to start of data buffer descs block
*/
static inline
srp_memory_descriptor_t*
get_srp_command_buffer_desc(
	IN  srp_cmd_t   *p_information_unit )
{
	return( ( srp_memory_descriptor_t* ) ( p_information_unit->additional_cdb + ( get_srp_command_additional_cdb_length( p_information_unit ) * 4 ) ) );
}


/* get_srp_command_Length */
/*!
Returns the size in bytes of the Srp command IU

@param p_information_unit - pointer to the IU structure

@return - used length of command IU buffer
*/
static inline
uint32_t
get_srp_command_length(
	IN  srp_cmd_t *p_information_unit )
{
	int			buffer_desc_count;
	uint32_t	srp_cmd_length = ( sizeof( *p_information_unit ) - sizeof( p_information_unit->additional_cdb ) ) +
							( get_srp_command_additional_cdb_length( p_information_unit ) * 4 );
	
	switch ( get_srp_command_data_out_buffer_desc_fmt ( p_information_unit ))
	{
		case DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR:
			buffer_desc_count = get_srp_command_data_out_buffer_desc_count( p_information_unit );
			srp_cmd_length += ( buffer_desc_count == 0)? sizeof(srp_memory_descriptor_t): 
														 ( buffer_desc_count * sizeof(srp_memory_descriptor_t ));
			break;
		case DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS:
			buffer_desc_count = get_srp_command_data_out_buffer_desc_count( p_information_unit );
			srp_cmd_length += sizeof(srp_memory_table_descriptor_t) + ( buffer_desc_count * sizeof(srp_memory_descriptor_t));
			break;
		default:
			break;
	}

	switch ( get_srp_command_data_in_buffer_desc_fmt ( p_information_unit ))
	{
		case DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR:
			buffer_desc_count = get_srp_command_data_in_buffer_desc_count( p_information_unit );
			srp_cmd_length += ( buffer_desc_count == 0)? sizeof(srp_memory_descriptor_t): 
														 ( buffer_desc_count * sizeof(srp_memory_descriptor_t ));
			break;
		case DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS:
			buffer_desc_count = get_srp_command_data_in_buffer_desc_count( p_information_unit );
			srp_cmd_length += sizeof(srp_memory_table_descriptor_t) + ( buffer_desc_count * sizeof(srp_memory_descriptor_t));
			break;
		default:
			break;
	}
	return ( srp_cmd_length );
}

/* set_srp_command_from_host_to_network */
/*!
Swaps the IU fields from Host to Network ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_command_from_host_to_network(
	IN OUT  srp_cmd_t   *p_information_unit )
{
	srp_memory_descriptor_t *p_memory_descriptor;
	srp_memory_table_descriptor_t *p_table_descriptor;
	int                     buffer_desc_count;
	int                     i;

	set_srp_information_unit_from_host_to_network( ( srp_information_unit_t* ) p_information_unit );
	p_information_unit->logical_unit_number = cl_hton64( p_information_unit->logical_unit_number );

	p_memory_descriptor = get_srp_command_buffer_desc( p_information_unit );

	switch (get_srp_command_data_out_buffer_desc_fmt(p_information_unit) )
	{
	case DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR:
		buffer_desc_count = get_srp_command_data_out_buffer_desc_count( p_information_unit );
		if ( p_memory_descriptor != NULL )
		{
			for ( i=0; i < buffer_desc_count; i++)
			{
				p_memory_descriptor->virtual_address = cl_hton64( p_memory_descriptor->virtual_address );
				p_memory_descriptor->data_length =  cl_hton32 ( p_memory_descriptor->data_length );
				p_memory_descriptor++;
			}
		}
		break;
	case DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS:
		buffer_desc_count = get_srp_command_data_out_buffer_desc_count( p_information_unit );
		if ( p_memory_descriptor != NULL )
		{
			p_table_descriptor = ( srp_memory_table_descriptor_t *)p_memory_descriptor;
			p_memory_descriptor = ( srp_memory_descriptor_t *)( p_table_descriptor + 1);
			
			p_table_descriptor->descriptor.virtual_address = cl_hton64( p_table_descriptor->descriptor.virtual_address );
			p_table_descriptor->descriptor.data_length = cl_hton32( p_table_descriptor->descriptor.data_length );
			p_table_descriptor->total_length =  cl_hton32( p_table_descriptor->total_length );
			
			for ( i=0; i < buffer_desc_count; i++)
			{
				p_memory_descriptor->virtual_address = cl_hton64( p_memory_descriptor->virtual_address );
				p_memory_descriptor->data_length =  cl_hton32( p_memory_descriptor->data_length );
				p_memory_descriptor++;
			}
		}
		break;
	case DBDF_NO_DATA_BUFFER_DESCRIPTOR_PRESENT:
	default:
		break;
	}

	switch (get_srp_command_data_in_buffer_desc_fmt(p_information_unit) )
	{
	case DBDF_DIRECT_DATA_BUFFER_DESCRIPTOR:
		buffer_desc_count = get_srp_command_data_in_buffer_desc_count( p_information_unit );
		if ( p_memory_descriptor != NULL )
		{
			for ( i=0; i < buffer_desc_count; i++)
			{
				p_memory_descriptor->virtual_address = cl_hton64( p_memory_descriptor->virtual_address );
				p_memory_descriptor->data_length = cl_hton32 ( p_memory_descriptor->data_length );
				p_memory_descriptor++;
			}
		}
		break;
	case DBDF_INDIRECT_DATA_BUFFER_DESCRIPTORS:
		buffer_desc_count = get_srp_command_data_in_buffer_desc_count( p_information_unit );
		if ( p_memory_descriptor != NULL )
		{
			p_table_descriptor = ( srp_memory_table_descriptor_t *)p_memory_descriptor;
			p_memory_descriptor = ( srp_memory_descriptor_t *)( p_table_descriptor + 1);
			
			p_table_descriptor->descriptor.virtual_address = cl_hton64( p_table_descriptor->descriptor.virtual_address );
			p_table_descriptor->descriptor.data_length = cl_hton32( p_table_descriptor->descriptor.data_length );
			p_table_descriptor->total_length = cl_hton32( p_table_descriptor->total_length );
			
			for ( i=0; i < buffer_desc_count; i++)
			{
				p_memory_descriptor->virtual_address = cl_hton64( p_memory_descriptor->virtual_address );
				p_memory_descriptor->data_length =  cl_hton32( p_memory_descriptor->data_length );
				p_memory_descriptor++;
			}
		}
		break;
	case DBDF_NO_DATA_BUFFER_DESCRIPTOR_PRESENT:
	default:
		break;
	}
}

/* set_srp_command_from_network_to_host */
/*!
Swaps the IU fields from Network to Host ordering

@param p_information_unit - pointer to the IU structure

@return - none
*/

static inline
void
set_srp_command_from_network_to_host(
	IN OUT  srp_cmd_t   *p_information_unit )
{
	set_srp_command_from_host_to_network ( p_information_unit );
}

#endif /* SRP_CMD_H_INCLUDED */
