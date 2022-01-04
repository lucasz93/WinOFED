/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2005 Hewlett Packard, Inc (Grant Grundler)
 * Copyright (c) 2008-2009 Intel Corporation.  All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "perftest.h"
#include <stdio.h>

UINT64 get_cycles()
{
	LARGE_INTEGER counter;
	static UINT64 base_adj = 0;
	static UINT64 running_adj = 0;

	if (base_adj == 0) {
		int i;

		QueryPerformanceCounter(&counter);
		base_adj = counter.QuadPart;
		for (i = 0; i < (1 << 16) - 2; i++) {
			QueryPerformanceCounter(&counter);
		}
		QueryPerformanceCounter(&counter);
		base_adj = (counter.QuadPart - base_adj) >> 16;
	}

	QueryPerformanceCounter(&counter);

	running_adj += base_adj;
	return counter.QuadPart - running_adj;
}

UINT64 get_freq()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	return freq.QuadPart;
}

cycles_t get_median(int n, cycles_t delta[])
{
	if ((n - 1) % 2)
		return(delta[n / 2] + delta[n / 2 - 1]) / 2;
	else
		return delta[n / 2];
}

int __cdecl cycles_compare(const void * aptr, const void * bptr)
{
	const cycles_t *a = aptr;
	const cycles_t *b = bptr;
	if (*a < *b) return -1;
	if (*a > *b) return 1;
	return 0;

}

SOCKET pp_client_connect(const char *servername, int port)
{
	struct addrinfo *res, *t;
	struct addrinfo hints;
	char service[6];
	int n;
	SOCKET sockfd = INVALID_SOCKET;

	memset(&hints, 0, sizeof hints);
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
#ifndef _WIN32
	sprintf(service, "%d\0", port);
#else
	sprintf_s(service, sizeof(service), "%d\0", port);
#endif

	n = getaddrinfo(servername, service, &hints, &res);
	if (n != 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
		return INVALID_SOCKET;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd != INVALID_SOCKET) {
			if (!connect(sockfd, t->ai_addr, (int)t->ai_addrlen))
				break;
			closesocket(sockfd);
			sockfd = INVALID_SOCKET;
		}
	}

	freeaddrinfo(res);

	if (sockfd == INVALID_SOCKET) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
	}
	return sockfd;
}

SOCKET pp_server_connect(int port)
{
	struct addrinfo *res, *t;
	struct addrinfo hints;
	char service[6];
	SOCKET sockfd = INVALID_SOCKET, connfd;
	int n;

	memset(&hints, 0, sizeof hints);
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
#ifndef _WIN32
	sprintf(service, "%d\0", port);
#else
	sprintf_s(service, sizeof(service), "%d\0", port);
#endif

	n = getaddrinfo(NULL, service, &hints, &res);
	if (n != 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
		return INVALID_SOCKET;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd != INVALID_SOCKET) {
			n = 0;
			setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &n, sizeof n);
			n = 1;
			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &n, sizeof n);

			if (!bind(sockfd, t->ai_addr, (int)t->ai_addrlen))
				break;
			closesocket(sockfd);
			sockfd = INVALID_SOCKET;
		}
	}

	freeaddrinfo(res);

	if (sockfd == INVALID_SOCKET) {
		fprintf(stderr, "Couldn't listen to port %d\n", port);
		return INVALID_SOCKET;
	}

	listen(sockfd, 1);
	connfd = accept(sockfd, NULL, 0);
	if (connfd == INVALID_SOCKET) {
		perror("server accept");
		fprintf(stderr, "accept() failed\n");
	}

	closesocket(sockfd);
	return connfd;
}
