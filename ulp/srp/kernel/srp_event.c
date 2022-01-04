/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corp.  All rights reserved.
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


#include "srp_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "srp_event.tmh"
#endif
#include "srp_event.h"
#include "srp_session.h"

/* srp_async_event_handler_cb */
/*!
Handles asynchronous events from ib

@param p_event_rec - pointer to the async event

@return - none
*/
void
srp_async_event_handler_cb(
	IN  ib_async_event_rec_t    *p_event_rec )
{
	srp_session_t   *p_srp_session = (srp_session_t *)p_event_rec->context;

	SRP_ENTER( SRP_DBG_PNP );

	switch ( p_event_rec->code )
	{
		case IB_AE_PORT_ACTIVE:
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
				("Async Event IB_AE_PORT_ACTIVE (%d) received for %s.\n",
				 p_event_rec->code,
				 p_srp_session->p_hba->ioc_info.profile.id_string) );
			break;

		case IB_AE_PORT_DOWN:
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
				("Async Event IB_AE_PORT_DOWN (%d) received for %s.\n",
				 p_event_rec->code,
				 p_srp_session->p_hba->ioc_info.profile.id_string) );
			break;

		default:
			SRP_PRINT( TRACE_LEVEL_INFORMATION, SRP_DBG_DEBUG,
				("Async Event %d received.\n", p_event_rec->code) );
			break;
	}

	SRP_EXIT( SRP_DBG_PNP );
}
