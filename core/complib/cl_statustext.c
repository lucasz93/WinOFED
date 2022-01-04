/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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


/*
 * Abstract:
 *	Defines string to decode cl_status_t return values.
 *
 * Environment:
 *	All
 */


#include <complib/cl_types.h>


/* Status values above converted to text for easier printing. */
const char* cl_status_text[] =
{
	"CL_SUCCESS",
	"CL_ERROR",
	"CL_INVALID_STATE",
	"CL_INVALID_OPERATION",
	"CL_INVALID_SETTING",
	"CL_INVALID_PARAMETER",
	"CL_INSUFFICIENT_RESOURCES",
	"CL_INSUFFICIENT_MEMORY",
	"CL_INVALID_PERMISSION",
	"CL_COMPLETED",
	"CL_NOT_DONE",
	"CL_PENDING",
	"CL_TIMEOUT",
	"CL_CANCELED",
	"CL_REJECT",
	"CL_OVERRUN",
	"CL_NOT_FOUND",
	"CL_UNAVAILABLE",
	"CL_BUSY",
	"CL_DISCONNECT",
	"CL_DUPLICATE",
	"CL_INVALID_REQUEST"
};
