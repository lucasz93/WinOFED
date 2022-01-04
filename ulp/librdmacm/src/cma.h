/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2008-2009 Intel Corp.  All rights reserved.
 * Copyright (c) 2012 Oce Printing Systems GmbH.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
 *      - Neither the name Oce Printing Systems GmbH nor the names
 *        of the authors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 * THIS SOFTWARE IS PROVIDED  “AS IS” AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
 * OR CONTRIBUTOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE. 
 */

#ifndef CMA_H
#define CMA_H

#include <complib/cl_spinlock.h>
#include <rdma/rdma_verbs.h>
#include <rdma/rsocksvc.h>

/*
 * Fast synchronization for low contention locking.
 */
#define fastlock_t				cl_spinlock_t
#define fastlock_init(lock)		cl_spinlock_init(lock)
#define fastlock_destroy(lock)	cl_spinlock_destroy(lock)
#define fastlock_acquire(lock)	cl_spinlock_acquire(lock)
#define fastlock_release(lock)	cl_spinlock_release(lock)

extern fastlock_t lock;
extern HANDLE     heap;

#define TRACE(fmt, ...) Trace(__FUNCTION__": " fmt "\n", __VA_ARGS__)

void Trace(const char* fmt, ...);
void ucma_cleanup();
int ucma_max_qpsize(struct rdma_cm_id *id);
int ucma_complete(struct rdma_cm_id *id);
void wsa_setlasterror(void);
RS_NETSTAT_ENTRY* rsNetstatEntryCreate(int rs, int *lpErrno);
RS_NETSTAT_ENTRY* rsNetstatEntryGet   (int rs);

static __inline int ERR(int err)
{
    int ret = rdma_seterrno(err);
    if (ret)
        wsa_setlasterror();

    return ret;
}
 
__inline void* __cdecl operator new(size_t size)
{
	return HeapAlloc(heap, 0, size);
}

__inline void __cdecl operator delete(void *pObj)
{
	HeapFree(heap, 0, pObj);
}

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif
#ifndef RDMADIR
#define RDMADIR "rdma"
#endif
#define RDMA_CONF_DIR  SYSCONFDIR "/" RDMADIR
#define RS_CONF_DIR RDMA_CONF_DIR "/rsocket"

#endif /* CMA_H */
