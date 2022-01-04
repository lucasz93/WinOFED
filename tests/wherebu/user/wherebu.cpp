/*
 * Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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


#include "stdlib.h"
#include "stdio.h"
#include "iba/ib_types.h"
#include "iba/ibat.h"


inline LONGLONG GetElapsedTime()
{
    LARGE_INTEGER elapsed;
    QueryPerformanceCounter(&elapsed);
    return elapsed.QuadPart;
}

inline LONGLONG GetFrequency()
{
    LARGE_INTEGER Frequency;
    QueryPerformanceFrequency(&Frequency);
    return Frequency.QuadPart;
}

int PrintUsage( int argc, char *argv[] )
{
    UNREFERENCED_PARAMETER( argc );
    printf( "%s <source IP> <destination IP>\n", argv[0] );

    return __LINE__;
}

int __cdecl main(int argc, char *argv[])
{
    if( argc < 3 )
        return PrintUsage( argc, argv );

    struct sockaddr_in srcAddr = {0};
    srcAddr.sin_family = AF_INET;
    srcAddr.sin_addr.s_addr = inet_addr( argv[1] );

    struct sockaddr_in destAddr = {0};
    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = inet_addr( argv[2] );

    ib_path_rec_t path;
    HRESULT hr = IBAT::QueryPath(
        (struct sockaddr*)&srcAddr,
        (struct sockaddr*)&destAddr,
        (IBAT_PATH_BLOB*)&path
        );
    if( FAILED( hr ) )
    {
        printf( "Resolve returned %08x.\n", hr );
        return hr;
    }

    printf(
        "I B at:\n"
        "partition %x\n"
        "source GID %x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x\n"
        "destination GID %x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x\n",
        path.pkey,
        path.sgid.raw[0], path.sgid.raw[1], path.sgid.raw[2], path.sgid.raw[3],
        path.sgid.raw[4], path.sgid.raw[5], path.sgid.raw[6], path.sgid.raw[7],
        path.sgid.raw[8], path.sgid.raw[9], path.sgid.raw[10], path.sgid.raw[11],
        path.sgid.raw[12], path.sgid.raw[13], path.sgid.raw[14], path.sgid.raw[15],
        path.dgid.raw[0], path.dgid.raw[1], path.dgid.raw[2], path.dgid.raw[3],
        path.dgid.raw[4], path.dgid.raw[5], path.dgid.raw[6], path.dgid.raw[7],
        path.dgid.raw[8], path.dgid.raw[9], path.dgid.raw[10], path.dgid.raw[11],
        path.dgid.raw[12], path.dgid.raw[13], path.dgid.raw[14], path.dgid.raw[15]
    );

    LONGLONG StartTime = GetElapsedTime();
    for( int i = 0; i < 2000; i++ )
    {
        HRESULT hr = IBAT::QueryPath(
            (struct sockaddr*)&srcAddr,
            (struct sockaddr*)&destAddr,
            (IBAT_PATH_BLOB*)&path
            );
        if( FAILED( hr ) )
        {
            printf( "Resolve returned %08x.\n", hr );
            return hr;
        }
    }
    LONGLONG RunTime = GetElapsedTime() - StartTime;
    double Rate = 2000.0 / ((double)RunTime / (double)GetFrequency());
    printf( "%7.2f lookups per second\n", Rate );

    return 0;
}
