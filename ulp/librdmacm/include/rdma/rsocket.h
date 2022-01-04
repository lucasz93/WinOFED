/*
 * Copyright (c) 2011 Intel Corporation.  All rights reserved.
 * Copyright (c) 2013 Oce Printing Systems GmbH.  All rights reserved.
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

#if !defined(RSOCKET_H)
#define RSOCKET_H

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <ws2spi.h>

typedef unsigned int nfds_t; // Under Linux from poll.h

#ifdef __cplusplus
extern "C" {
#endif

int rsocket(int domain, int type, int protocol);
int rbind(int socket, const struct sockaddr *addr, socklen_t addrlen);
int rlisten(int socket, int backlog);
#if 0 // Direct Call variant:
int raccept(int socket, struct sockaddr *addr, socklen_t *addrlen);
#else // Winsock Provider variant:
SOCKET WSPAPI WSPAccept(
	SOCKET			 socket,
	struct sockaddr	*addr,
	LPINT			 addrlen,
	LPCONDITIONPROC	 lpfnCondition,
	DWORD_PTR		 dwCallbackData,
	LPINT			 lpErrno
);
#endif
int rconnect(int socket, const struct sockaddr *addr, socklen_t addrlen);
int rshutdown(int socket, int how);
int rclose(int socket);
ssize_t rrecv(int socket, void *buf, size_t len, int flags);
ssize_t rrecvfrom(int socket, void *buf, size_t len, int flags,
		  struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t rrecvmsg(int socket, struct msghdr *msg, int flags);
ssize_t rsend(int socket, const void *buf, size_t len, int flags);
ssize_t rsendto(int socket, const void *buf, size_t len, int flags,
		const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t rsendmsg(int socket, const struct msghdr *msg, int flags);
ssize_t rread(int socket, void *buf, size_t count);
ssize_t rreadv(int socket, const struct iovec *iov, int iovcnt);
ssize_t rwrite(int socket, const void *buf, size_t count);
ssize_t rwritev(int socket, const struct iovec *iov, int iovcnt);
int rpoll(struct pollfd *fds, nfds_t nfds, int timeout);
int rselect(int nfds, fd_set *readfds, fd_set *writefds,
	    fd_set *exceptfds, struct timeval *timeout);
int rgetpeername(int socket, struct sockaddr *addr, socklen_t *addrlen);
int rgetsockname(int socket, struct sockaddr *addr, socklen_t *addrlen);

#ifndef SOL_RDMA
#define SOL_RDMA 0x10000
enum {
	RDMA_SQSIZE,
	RDMA_RQSIZE,
	RDMA_INLINE,
	RDMA_IOMAPSIZE 
};

enum {
	PROT_NONE  = 0x0, /* page can not be accessed */
	PROT_READ  = 0x1, /* page can be read */
	PROT_WRITE = 0x2, /* page can be written */
	PROT_EXEC  = 0x4  /* page can be executed */
};
#endif

int rsetsockopt(int socket, int level, int optname,
		const void *optval, socklen_t optlen);
int rgetsockopt(int socket, int level, int optname,
		void *optval, socklen_t *optlen);
off_t riomap(int socket, void *buf, size_t len, int prot, int flags, off_t offset);
int riounmap(int socket, void *buf, size_t len);
size_t riowrite(int socket, const void *buf, size_t count, off_t offset, int flags); 
int rioctlsocket(int socket, long cmd, u_long* argp);

#ifdef __cplusplus
}
#endif

#endif /* RSOCKET_H */
