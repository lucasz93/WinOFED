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
 */

#pragma once

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <tchar.h>
#include <limits.h>
#include <unknwn.h>
#include <winioctl.h>
#include <winternl.h>
#include <ws2tcpip.h>
#include <ws2spi.h>
#include <ndstatus.h>
#include <string.h>
#include <process.h>

#include <InitGuid.h>
#include <ndspi.h>

#include "ndfltr.h"

#include <iba\ib_types.h>
#include <iba\ib_ci.h>
#include "mx_abi.h"
#include "verbs.h"

#include "main.h"
#include "unknown.h"
#include "overlapped.h"
#include "factory.h"
#include "provider.h"
#include "adapter.h"
#include "cq.h"
#include "mr.h"
#include "mw.h"
#include "srq.h"
#include "qp.h"
#include "connector.h"
#include "listener.h"
