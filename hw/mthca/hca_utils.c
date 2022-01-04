/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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


#include "mthca_dev.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "hca_data.tmh"
#endif


mthca_qp_access_t
map_qp_ibal_acl(
	IN				ib_access_t					ibal_acl)
{
#define IBAL_ACL(ifl,mfl) if (ibal_acl & ifl)   mthca_acl |= mfl
	mthca_qp_access_t		mthca_acl = 0;

	IBAL_ACL(IB_AC_RDMA_READ,MTHCA_ACCESS_REMOTE_READ);
	IBAL_ACL(IB_AC_RDMA_WRITE,MTHCA_ACCESS_REMOTE_WRITE);
	IBAL_ACL(IB_AC_ATOMIC,MTHCA_ACCESS_REMOTE_ATOMIC);
	IBAL_ACL(IB_AC_LOCAL_WRITE,MTHCA_ACCESS_LOCAL_WRITE);
	IBAL_ACL(IB_AC_MW_BIND,MTHCA_ACCESS_MW_BIND);

	return mthca_acl;
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
ib_access_t
map_qp_mthca_acl(
	IN				mthca_qp_access_t				mthca_acl)
{
#define ACL_IBAL(mfl,ifl) if (mthca_acl & mfl)   ibal_acl |= ifl
	ib_access_t ibal_acl = 0;

	ACL_IBAL(MTHCA_ACCESS_REMOTE_READ,IB_AC_RDMA_READ);
	ACL_IBAL(MTHCA_ACCESS_REMOTE_WRITE,IB_AC_RDMA_WRITE);
	ACL_IBAL(MTHCA_ACCESS_REMOTE_ATOMIC,IB_AC_ATOMIC);
	ACL_IBAL(MTHCA_ACCESS_LOCAL_WRITE,IB_AC_LOCAL_WRITE);
	ACL_IBAL(MTHCA_ACCESS_MW_BIND,IB_AC_MW_BIND);

	return ibal_acl;
}


