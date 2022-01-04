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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE ANd
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#ifndef _NDFLTR_H_
#define _NDFLTR_H_

#include <ndioctl.h>


#pragma warning(push)
#pragma warning(disable:4201)

struct NDFLTR_QUERY_ADDRESS_LIST {
    ND_HANDLE   Header;
    GUID        DriverId;
};

struct NDFLTR_RESOLVE_ADDRESS {
    ND_RESOLVE_ADDRESS  Header;
    GUID                DriverId;
};

struct NDFLTR_MR_KEYS {
    UINT32 LKey;
    UINT32 RKey;
};

struct NDFLTR_EP_CREATE {
    ND_HANDLE   Header;
    BOOLEAN     Ndv1TimeoutSemantics;
};

struct NDFLTR_CONNECT {
    ND_CONNECT  Header;
    UINT8       RetryCount;
    UINT8       RnrRetryCount;
    BYTE        PrivateData[56];
};

struct NDFLTR_COMPLETE_CONNECT {
    ND_HANDLE   Header;
    UINT8       RnrNakTimeout;
};

struct NDFLTR_ACCEPT {
    ND_ACCEPT   Header;
    UINT8       RnrRetryCount;
    UINT8       RnrNakTimeout;
    BYTE        PrivateData[148];
};

struct NDFLTR_REJECT {
    ND_REJECT   Header;
    BYTE        PrivateData[148];
};

#pragma warning(pop)

#endif // _NDFLTR_H_
