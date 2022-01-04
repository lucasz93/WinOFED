/*
 * Copyright (c) 2012 Intel Corporation.  All rights reserved.
 *
 * This Software is licensed under one of the following licenses:
 *
 * 1) under the terms of the "Common Public License 1.0" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/cpl.php.
 *
 * 2) under the terms of the "The BSD License" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/bsd-license.php.
 *
 * 3) under the terms of the "GNU General Public License (GPL) Version 2" a
 *    copy of which is available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/gpl-license.php.
 *
 * Licensee has the right to choose one of the above licenses.
 *
 * Redistributions of source code must retain the above copyright
 * notice and one of the license notices.
 *
 * Redistributions in binary form must reproduce both the above copyright
 * notice, one of the license notices in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Definitions specific to the NetworkDirect provider.
 */
#ifndef _DAPL_ND_UTIL_H_
#define _DAPL_ND_UTIL_H_

#include "dapl_nd.h"

ib_cm_events_t dapli_NDerror_2_CME_event(DWORD err);

#ifdef DAPL_COUNTERS
STATIC _INLINE_ void dapls_print_cm_list(IN DAPL_IA * ia_ptr)
{
	return;
}
#endif

STATIC _INLINE_ void
dump_sg_list(ND2_SGE *sg_list, SIZE_T segs)
{
	int j;

	printf("%s() entries %d\n",__FUNCTION__,segs);
	for(j=0; j < segs; j++,sg_list++)
		printf("   SGE[%d] Buffer %p len %d mr_token %#x\n",
			j, sg_list->Buffer, sg_list->BufferLength,
			sg_list->MemoryRegionToken);
}

#endif /*  _DAPL_ND_UTIL_H_ */
