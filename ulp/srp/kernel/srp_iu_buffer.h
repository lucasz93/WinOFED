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


#ifndef SRP_IU_BUFFER_H_INCLUDED
#define SRP_IU_BUFFER_H_INCLUDED

#include "srp.h"

/* set_srp_iu_buffer_type */
/*!
Set the Information Unit type for the Srp IU buffer

@param p_buffer - pointer to the IU buffer
@param iu_type  - IU structure type
*/
static inline
void
set_srp_iu_buffer_type(
	IN OUT  srp_iu_buffer_t *p_buffer,
	IN      uint8_t         iu_type )
{
	p_buffer->information_unit.type = iu_type;
}

/* init_srp_iu_buffer */
/*!
Initialize the Srp IU buffer to 0 and set it's type

@param p_buffer - pointer to the IU buffer
@param iu_type  - IU structure type
*/
static inline
void
init_srp_iu_buffer(
	IN OUT  srp_iu_buffer_t *p_buffer,
	IN      uint8_t         iu_type )
{
	size_t  iu_size = 0;

	switch( iu_type )
	{
		case SRP_RSP:
			/* don't dirty the second cache line */
			iu_size = offsetof( srp_rsp_t, data_out_residual_count );
			break;

		case SRP_LOGIN_REQ:
			iu_size = sizeof( srp_login_req_t );
			break;

		case SRP_TSK_MGMT:
			iu_size = sizeof( srp_tsk_mgmt_t );
			break;

		case SRP_CMD:
			iu_size = sizeof( srp_cmd_t );
			break;

		case SRP_I_LOGOUT:
			iu_size = sizeof( srp_i_logout_t );
			break;

		case SRP_LOGIN_RSP:
			iu_size = sizeof( srp_login_rsp_t );
			break;

		case SRP_LOGIN_REJ:
			iu_size = sizeof( srp_login_rej_t );
			break;

		case SRP_T_LOGOUT:
			iu_size = sizeof( srp_t_logout_t );
			break;

		case SRP_CRED_REQ:
			iu_size = sizeof( srp_cred_req_t );
			break;

		case SRP_AER_REQ:
			iu_size = sizeof( srp_aer_req_t );
			break;

		case SRP_CRED_RSP:
			iu_size = sizeof( srp_cred_rsp_t );
			break;

		case SRP_AER_RSP:
			iu_size = sizeof( srp_aer_rsp_t );
			break;
	}

	memset( p_buffer, 0, iu_size );

	set_srp_iu_buffer_type( p_buffer, iu_type );
}

/* get_srp_iu_buffer_type */
/*!
Returns the Information Unit type for the Srp buffer

@param p_buffer - pointer to the IU structure

@return - IU type value
*/
static inline
uint8_t
get_srp_iu_buffer_type(
	IN  srp_iu_buffer_t *p_buffer )
{
	return( p_buffer->information_unit.type );
}

#endif /* SRP_IU_BUFFER_H_INCLUDED */
