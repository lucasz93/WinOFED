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



#include "complib/cl_log.h"


void
cl_log_event(
	IN	const char* const	name,
	IN	const cl_log_type_t	type,
	IN	const char* const	p_message,
	IN	const void* const	p_data,
	IN	const uint32_t		data_len )
{
    HANDLE	h;
	WORD	log_type;
	WORD	num_str = 0;
#pragma prefast(suppress: 28735 , "Ignore Banned Crimson API Usage")
    h = RegisterEventSource( NULL, name );

    if( !h )
        return;

	switch( type )
	{
	case CL_LOG_ERROR:
		log_type = EVENTLOG_ERROR_TYPE;
		break;

	case CL_LOG_WARN:
		log_type = EVENTLOG_WARNING_TYPE;
		break;

	default:
	case CL_LOG_INFO:
		log_type = EVENTLOG_INFORMATION_TYPE;
		break;
	}

	if( p_message )
		num_str = 1;

	/* User the ASCII version of ReportEvent. */
    ReportEventA( h, log_type, 0, 0, NULL, num_str, data_len, 
		(LPCTSTR*)&p_message, (LPVOID)p_data );
 
    DeregisterEventSource( h ); 
}
