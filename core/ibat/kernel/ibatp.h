/*
 * Copyright (c) Microsoft Corporation.  All rights reserved.
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
 *
 */


#include <ntddk.h>
#include "iba\ibat.h"
#include "iba\ibat_ifc.h"
#include "iba\ib_al_ifc.h"


#define IBAT_POOL_TAG 'tabi'

EXTERN_C ib_al_handle_t gh_al;


namespace IbatUtil
{

    inline
    bool
    IsEqual(const SOCKADDR_IN6& lhs, const SOCKADDR_IN6& rhs )
    {
        if (IN6_IS_ADDR_LOOPBACK(&lhs.sin6_addr) ||
            IN6_IS_ADDR_LOOPBACK(&rhs.sin6_addr))
        {
            return true;
        }
        return IN6_ADDR_EQUAL( &lhs.sin6_addr, &rhs.sin6_addr ) == TRUE;
    }


    inline void MapTo6(__in const SOCKADDR_INET& in, __out SOCKADDR_IN6* out)
    {
        if (in.si_family == AF_INET)
        {
            if (in.Ipv4.sin_addr.s_addr == _byteswap_ulong(INADDR_LOOPBACK))
            {
                IN6ADDR_SETLOOPBACK(out);
            }
            else
            {
                out->sin6_family = AF_INET6;
                out->sin6_port = 0;
                out->sin6_flowinfo = 0;
                out->sin6_addr = in6addr_v4mappedprefix;
                out->sin6_addr.u.Word[6] = in.Ipv4.sin_addr.S_un.S_un_w.s_w1;
                out->sin6_addr.u.Word[7] = in.Ipv4.sin_addr.S_un.S_un_w.s_w2;
                out->sin6_scope_id = 0;
            }
        }
        else
        {
            *out = in.Ipv6;
        }
    }

    inline
    bool
    IsEqual(const SOCKADDR_INET& lhs, const SOCKADDR_INET& rhs)
    {
        SOCKADDR_IN6 lhs6;
        SOCKADDR_IN6 rhs6;

        MapTo6(lhs, &lhs6);
        MapTo6(rhs, &rhs6);

        return IsEqual(lhs6, rhs6);
    }

};
