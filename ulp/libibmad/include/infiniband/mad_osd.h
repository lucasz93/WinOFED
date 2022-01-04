/*
 * Copyright (c) 2009 Intel Corporation. All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _MAD_OSD_H_
#define _MAD_OSD_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <_errno.h>

typedef unsigned __int8		uint8_t;
typedef unsigned __int16	uint16_t;
typedef unsigned __int32	uint32_t;
typedef unsigned __int64	uint64_t;

#define PRId64	"I64d"
#define PRIx64	"I64x"
#define PRIo64	"I64o"
#define PRIu64	"I64u"

#ifndef MAD_EXPORT
#define MAD_EXPORT	__declspec(dllimport)
#endif
#ifndef IBND_EXPORT
#define IBND_EXPORT	__declspec(dllimport)
#endif

#define DEPRECATED

#if !defined( __cplusplus )
#define inline	__inline
#endif

#define bswap_64	_byteswap_uint64
#if !defined(getpid)
  #define getpid()	GetCurrentProcessId()
#endif
#define snprintf	_snprintf
#if !defined(strtoull)
#define strtoull	_strtoui64
#endif
#define __func__	__FUNCTION__
#define random		rand
#define srandom		srand

#endif /* _MAD_OSD_H_ */
