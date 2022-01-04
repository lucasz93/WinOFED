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


#include "complib/comp_lib.h"
#include <stdarg.h>


CL_EXPORT NTSTATUS
cl_to_ntstatus(
	IN	cl_status_t	status )
{
	return (NTSTATUS)status;
}


CL_EXPORT cl_status_t
cl_from_ntstatus(
	IN	NTSTATUS status )
{
	switch( status )
	{
	case STATUS_SUCCESS:				case STATUS_DRIVER_INTERNAL_ERROR:
	case STATUS_INVALID_DEVICE_STATE:	case STATUS_NOT_SUPPORTED:
	case STATUS_INVALID_PARAMETER_1:	case STATUS_INVALID_PARAMETER:
	case STATUS_INSUFFICIENT_RESOURCES:	case STATUS_NO_MEMORY:
	case STATUS_ACCESS_DENIED:			case STATUS_EVENT_DONE:
	case STATUS_ABANDONED:				case STATUS_PENDING:
	case STATUS_TIMEOUT:				case STATUS_CANCELLED:
	case STATUS_REQUEST_NOT_ACCEPTED:	case STATUS_DATA_OVERRUN:
	case STATUS_NOT_FOUND:				case STATUS_DEVICE_NOT_READY:
	case STATUS_DEVICE_BUSY:			case STATUS_LOCAL_DISCONNECT:
	case STATUS_DUPLICATE_NAME: 		case STATUS_INVALID_DEVICE_REQUEST:
	case STATUS_INVALID_HANDLE: 		case STATUS_CONNECTION_INVALID:
		return (cl_status_t)status;
	default:
		return CL_ERROR;
	}
}


#if defined( _DEBUG_ )

VOID cl_dbg_out( IN const char* const format, ...)
{
	va_list  list;
	va_start(list, format);
	vDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, format, list);
	va_end(list);
}
#endif

