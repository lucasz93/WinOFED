/*
 * Copyright (c) 2010 Intel Corporation.  All rights reserved.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <windows.h>
#include <winsock2.h>

#include "cma.h"
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

static DWORD addr_ref;

static void ucma_startup(void)
{
	WSADATA wsadata;

	fastlock_acquire(&lock);
	if (addr_ref++) {
		goto out;
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsadata)) {
		addr_ref--;
	}

out:
	fastlock_release(&lock);
}

static void ucma_shutdown(void)
{
	fastlock_acquire(&lock);
	if (--addr_ref == 0) {
		WSACleanup();
	}
	fastlock_release(&lock);
}

static void ucma_convert_to_ai(struct addrinfo *ai, struct rdma_addrinfo *rai)
{
	memset(ai, 0, sizeof *ai);
	ai->ai_flags = rai->ai_flags;
	ai->ai_family = rai->ai_family;

	switch (rai->ai_qp_type) {
	case IBV_QPT_RC:
		ai->ai_socktype = SOCK_STREAM;
		break;
	case IBV_QPT_UD:
		ai->ai_socktype = SOCK_DGRAM;
		break;
	}

	switch (rai->ai_port_space) {
	case RDMA_PS_TCP:
		ai->ai_protocol = IPPROTO_TCP;
		break;
	case RDMA_PS_IPOIB:
	case RDMA_PS_UDP:
		ai->ai_protocol = IPPROTO_UDP;
		break;
	}

	if (rai->ai_flags & RAI_PASSIVE) {
		ai->ai_addrlen = rai->ai_src_len;
		ai->ai_addr = rai->ai_src_addr;
	} else {
		ai->ai_addrlen = rai->ai_dst_len;
		ai->ai_addr = rai->ai_dst_addr;
	}
	ai->ai_canonname = rai->ai_dst_canonname;
	ai->ai_next = NULL;
}

static int ucma_convert_to_rai(struct rdma_addrinfo *rai, struct addrinfo *ai)
{
	struct sockaddr *addr;
	char *canonname;

	memset(rai, 0, sizeof *rai);
	rai->ai_flags = ai->ai_flags;
	rai->ai_family = ai->ai_family;

	switch (ai->ai_socktype) {
	case SOCK_STREAM:
		rai->ai_qp_type = IBV_QPT_RC;
		break;
	case SOCK_DGRAM:
		rai->ai_qp_type = IBV_QPT_UD;
		break;
	}

	switch (ai->ai_protocol) {
	case IPPROTO_TCP:
		rai->ai_port_space = RDMA_PS_TCP;
		break;
	case IPPROTO_UDP:
		rai->ai_port_space = RDMA_PS_UDP;
		break;
	}

	addr = (struct sockaddr *) malloc(ai->ai_addrlen);
	if (!addr)
		return rdma_seterrno(ENOMEM);

	canonname = (char *) (ai->ai_canonname ? malloc(strlen(ai->ai_canonname) + 1) : NULL);
	if (canonname)
		strcpy(canonname, ai->ai_canonname);

	memcpy(addr, ai->ai_addr, ai->ai_addrlen);
	if (ai->ai_flags & RAI_PASSIVE) {
		rai->ai_src_addr = addr;
		rai->ai_src_len = ai->ai_addrlen;
		rai->ai_src_canonname = canonname;
	} else {
		rai->ai_dst_addr = addr;
		rai->ai_dst_len = ai->ai_addrlen;
		rai->ai_dst_canonname = canonname;
	}

	return 0;
}

__declspec(dllexport)
int rdma_getaddrinfo(char *node, char *service,
					 struct rdma_addrinfo *hints,
					 struct rdma_addrinfo **res)
{
	struct rdma_addrinfo *rai;
	struct addrinfo ai_hints;
	struct addrinfo *ai;
	int ret;

	ucma_startup();
	if (hints)
		ucma_convert_to_ai(&ai_hints, hints);

	ret = getaddrinfo(node, service, &ai_hints, &ai);
	if (ret)
		return rdmaw_wsa_errno(ret);

	rai = (struct rdma_addrinfo *) malloc(sizeof(*rai));
	if (!rai) {
		ret = rdma_seterrno(ENOMEM);
		goto err1;
	}

	// Windows does not set AI_PASSIVE on output
	ai->ai_flags |= hints ? hints->ai_flags : 0;
	ret = ucma_convert_to_rai(rai, ai);
	if (ret)
		goto err2;

	if (!rai->ai_src_len && hints && hints->ai_src_len) {
		rai->ai_src_addr = (struct sockaddr *) calloc(1, hints->ai_src_len);
		if (!rai->ai_src_addr) {
			ret = rdma_seterrno(ENOMEM);
			goto err2;
		}
		memcpy(rai->ai_src_addr, hints->ai_src_addr,
		       hints->ai_src_len);
		rai->ai_src_len = hints->ai_src_len;
	}

	// requires ib acm support --
	//if (!(rai->ai_flags & RAI_PASSIVE))
	//	ucma_ib_resolve(rai);

	freeaddrinfo(ai);
	*res = rai;
	return 0;

err2:
	rdma_freeaddrinfo(rai);
err1:
	freeaddrinfo(ai);
	return ret;
}

__declspec(dllexport)
void rdma_freeaddrinfo(struct rdma_addrinfo *res)
{
	struct rdma_addrinfo *rai;

	while (res) {
		rai = res;
		res = res->ai_next;

		if (rai->ai_connect)
			free(rai->ai_connect);

		if (rai->ai_route)
			free(rai->ai_route);

		if (rai->ai_src_canonname)
			free(rai->ai_src_canonname);

		if (rai->ai_dst_canonname)
			free(rai->ai_dst_canonname);

		if (rai->ai_src_addr)
			free(rai->ai_src_addr);

		if (rai->ai_dst_addr)
			free(rai->ai_dst_addr);

		free(rai);
	}
	ucma_shutdown();
}
