/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions (c) Microsoft Corporation.  All rights reserved.
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

#include <ntifs.h>
#include <wdm.h>
#include <wdf.h>
#include <WinDef.h>
#include <ntstatus.h>
#include <ntstrsafe.h>
#include <ndstatus.h>
#include <limits.h>
#include <netioapi.h>

#include <iba\ib_types.h>
#include <iba\ib_ci.h>
#include <iba\ib_cm_ifc.h>
#include <iba\ibat_ifc.h>
#include <iba\ib_rdma_cm.h>
#include <rdma\verbs.h>

#include "ndfltr.h"
#include "nd_util.h"
#include "nd_decl.h"
#include "nd_partition.h"
#include "nd_adapter.h"
#include "nd_ep.h"
#include "nd_cq.h"
#include "nd_pd.h"
#include "nd_mr.h"
#include "nd_mw.h"
#include "nd_srq.h"
#include "nd_qp.h"
#include "nd_provider.h"
#include "nd_driver.h"
