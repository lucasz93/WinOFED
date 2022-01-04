/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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

#ifndef _VNIC_UTIL_H_
#define _VNIC_UTIL_H_

#include "vnic_debug.h"

#define MAXU32 MAXULONG
#define MAXU64 ((uint64_t)(~0))

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

#define hton16(x)		_byteswap_ushort(x)
#define hton32(x)		_byteswap_ulong(x)
#define hton64(x)		_byteswap_uint64(x)

#define ntoh16(x)		hton16(x)
#define ntoh32(x)		hton32(x)
#define ntoh64(x)		hton64(x)

#define IsPowerOf2(value) (((value) & (value - 1)) == 0)
/* round down to closest power of 2 value */

#define SetMinPowerOf2(_val) ((_val) & ( 1 << RtlFindMostSignificantBit( (uint64_t)(_val) )))


#endif /* _VNIC_UTIL_H_ */

