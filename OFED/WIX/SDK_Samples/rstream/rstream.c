/*
 * Copyright (c) 2011-2012 Intel Corporation.  All rights reserved.
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
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <_errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include <rdma/rdma_cma.h>
#include <rdma/rwinsock.h>

#define MSG_DONTWAIT 0x80

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

#include "..\..\Inc\etc\gtod.c" // gettimeofday()
#include <getopt.h>
#include "..\..\Inc\etc\getopt.c"

struct test_size_param {
	int size;
	int option;
};

static struct test_size_param test_size[] = {
	{ 1 <<  6, 0 },
	{ 1 <<  7, 1 }, { (1 <<  7) + (1 <<  6), 1},
	{ 1 <<  8, 1 }, { (1 <<  8) + (1 <<  7), 1},
	{ 1 <<  9, 1 }, { (1 <<  9) + (1 <<  8), 1},
	{ 1 << 10, 1 }, { (1 << 10) + (1 <<  9), 1},
	{ 1 << 11, 1 }, { (1 << 11) + (1 << 10), 1},
	{ 1 << 12, 0 }, { (1 << 12) + (1 << 11), 1},
	{ 1 << 13, 1 }, { (1 << 13) + (1 << 12), 1},
	{ 1 << 14, 1 }, { (1 << 14) + (1 << 13), 1},
	{ 1 << 15, 1 }, { (1 << 15) + (1 << 14), 1},
	{ 1 << 16, 0 }, { (1 << 16) + (1 << 15), 1},
	{ 1 << 17, 1 }, { (1 << 17) + (1 << 16), 1},
	{ 1 << 18, 1 }, { (1 << 18) + (1 << 17), 1},
	{ 1 << 19, 1 }, { (1 << 19) + (1 << 18), 1},
	{ 1 << 20, 0 }, { (1 << 20) + (1 << 19), 1},
	{ 1 << 21, 1 }, { (1 << 21) + (1 << 20), 1},
	{ 1 << 22, 1 }, { (1 << 22) + (1 << 21), 1},
};

#define TEST_CNT (sizeof test_size / sizeof test_size[0])

enum rs_optimization {
	opt_mixed,
	opt_latency,
	opt_bandwidth
};

static int rs, lrs;
static int use_rs = 1;
static int use_async = 0;
static int verify = 0;
static int flags = 0; //MSG_DONTWAIT;
static int poll_timeout = 0;
static int custom;
static enum rs_optimization optimization;
static int size_option = 0;
static int iterations = 1;
static int transfer_size = 1000;
static int transfer_count = 1000;
static int buffer_size;
static char test_name[10] = "custom";
static char *port = "7471";
static char *dst_addr;
static char *src_addr;
static struct timeval start, end;
static void *buf;

#define rs_socket(f,t,p)          use_rs ? WSASocket(f,t,p,rsGetProtocolInfo(NULL),0,0) : socket(f,t,p)
#define rs_bind(s,a,l)            bind(s,a,l)
#define rs_listen(s,b)            listen(s,b)
#define rs_connect(s,a,l)         connect(s,a,l)
#define rs_accept(s,a,l)          accept(s,a,l)
#define rs_shutdown(s,h)          shutdown(s,h)
#define rs_close(s)               closesocket(s)
#define rs_recv(s,b,l,f)          recv(s,b,l,f)
#define rs_send(s,b,l,f)          send(s,b,l,f)
#define rs_recvfrom(s,b,l,f,a,al) recvfrom(s,b,l,f,a,al)
#define rs_sendto(s,b,l,f,a,al)   sendto(s,b,l,f,a,al)
#define rs_select(n,rf,wf,ef,t)	  select(n,rf,wf,ef,t)
#define rs_ioctlsocket(s,c,p)     ioctlsocket(s,c,p)
#define rs_setsockopt(s,l,n,v,ol) setsockopt(s,l,n,v,ol)
#define rs_getsockopt(s,l,n,v,ol) getsockopt(s,l,n,v,ol)

static void size_str (char *str, size_t ssize, long long size)
{
	long long base, fraction = 0;
	char mag;

	if (size >= (1 << 30)) {
		base = 1 << 30;
		mag = 'g';
	} else if (size >= (1 << 20)) {
		base = 1 << 20;
		mag = 'm';
	} else if (size >= (1 << 10)) {
		base = 1 << 10;
		mag = 'k';
	} else {
		base = 1;
		mag = '\0';
	}

	if (size / base < 10) {
		fraction = (size % base) * 10 / base;
	}

	if (fraction) {
		_snprintf(str, ssize, "%lld.%lld%c", size / base, fraction, mag);
	} else {
		_snprintf(str, ssize, "%lld%c", size / base, mag);
	}
}

static void cnt_str (char *str, size_t ssize, long long cnt)
{
	if (cnt >= 1000000000) {
		_snprintf(str, ssize, "%lldb", cnt / 1000000000);
	} else if (cnt >= 1000000) {
		_snprintf(str, ssize, "%lldm", cnt / 1000000);
	} else if (cnt >= 1000) {
		_snprintf(str, ssize, "%lldk", cnt / 1000);
	} else {
		_snprintf(str, ssize, "%lld", cnt);
	}
}

static void show_perf (void)
{
	char str[32];
	float usec;
	long long bytes;

	usec  = (float)((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec));
	bytes = (long long) iterations * transfer_count * transfer_size * 2;

	/* name size transfers iterations bytes seconds Gb/sec usec/xfer */
	printf("%-10s", test_name);
	size_str(str, sizeof str, transfer_size);
	printf("%-8s", str);
	cnt_str(str, sizeof str, transfer_count);
	printf("%-8s", str);
	cnt_str(str, sizeof str, iterations);
	printf("%-8s", str);
	size_str(str, sizeof str, bytes);
	printf("%-8s", str);
	printf("%8.2fs%10.2f%11.2f\n",
		usec / 1000000., (bytes * 8) / (1000. * usec),
		(usec / iterations) / (transfer_count * 2));
}

static int size_to_count (int size)
{
	if (size >= 1000000) {
		return 100;
	} else if (size >= 100000) {
		return 1000;
	} else if (size >= 10000) {
		return 10000;
	} else if (size >= 1000) {
		return 100000;
	} else {
		return 1000000;
	}
}

static void init_latency_test (int size)
{
	char sstr[5];

	size_str(sstr, sizeof sstr, size);
	_snprintf(test_name, sizeof test_name, "%s_lat", sstr);
	transfer_count = 1;
	transfer_size = size;
	iterations = size_to_count(transfer_size);
}

static void init_bandwidth_test (int size)
{
	char sstr[5];

	size_str(sstr, sizeof sstr, size);
	_snprintf(test_name, sizeof test_name, "%s_bw", sstr);
	iterations = 1;
	transfer_size = size;
	transfer_count = size_to_count(transfer_size);
}

static void format_buf (void *buf, int size)
{
	uint8_t *array = buf;
	static uint8_t data = 0;
	int i;

	for (i = 0; i < size; i++) {
		array[i] = data++;
	}
}

static int verify_buf (void *buf, int size)
{
	static long long total_bytes;
	uint8_t *array = buf;
	static uint8_t data = 0;
	int i;

	for (i = 0; i < size; i++, total_bytes++) {
		if (array[i] != data++) {
			printf("data verification failed byte %lld\n", total_bytes);
			return -1;
		}
	}
	return 0;
}

static int do_poll (struct pollfd *fds)
{
	int		ret;
	int		nfds = 0;
	fd_set	readfds, writefds, exceptfds;
	struct timeval timeout;
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	
	if (fds->events & (POLLIN | POLLHUP)) {
		FD_SET(fds->fd, &readfds);
		nfds++;
	}
		
	if (fds->events & POLLOUT) {
		FD_SET(fds->fd, &writefds);
		nfds++;
	}

	if (fds->events & ~(POLLIN | POLLOUT)) {
		FD_SET(fds->fd, &exceptfds);
		nfds++;
	}

	timeout.tv_sec  = poll_timeout / 1000;
	timeout.tv_usec = timeout.tv_sec ? 0 : poll_timeout * 1000;
	
	do {	
		ret = rs_select(
			nfds,
			FD_ISSET(fds->fd, &readfds  ) ? &readfds   : NULL,
			FD_ISSET(fds->fd, &writefds ) ? &writefds  : NULL,
			FD_ISSET(fds->fd, &exceptfds) ? &exceptfds : NULL,
			poll_timeout < 0 ? NULL : &timeout
		);
	} while (!ret);
	
	return (ret != SOCKET_ERROR ? 0 : ret);
}

static int send_xfer (int size)
{
	struct pollfd fds;
	int offset, ret;
	
	if (verify) {
		format_buf(buf, size);
	}

	if (use_async) {
		fds.fd = rs;
		fds.events = POLLOUT;
	}

	for (offset = 0; offset < size; ) {
		if (use_async) {
			ret = do_poll(&fds);
			if (ret) {
				return ret;
			}
		}

		ret = (int)rs_send(rs, (char *)buf + offset, size - offset, flags);
		if (ret > 0) {
			offset += ret;
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("rsend");
			return ret;
		}
	}
	return 0;
}

static int recv_xfer (int size)
{
	struct pollfd fds;
	int offset, ret;
	
	if (use_async) {
		fds.fd = rs;
		fds.events = POLLIN;
	}

	for (offset = 0; offset < size; ) {
		if (use_async) {
			ret = do_poll(&fds);
			if (ret) {
				return ret;
			}
		}

		ret = (int)rs_recv(rs, (char *)buf + offset, size - offset, flags);
		if (ret > 0) {
			offset += ret;
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("rrecv");
			return ret;
		}
	}

	if (verify) {
		ret = verify_buf(buf, size);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int sync_test (void)
{
	int ret;

	ret = dst_addr ? send_xfer(16) : recv_xfer(16);
	if (ret) {
		return ret;
	}

	return dst_addr ? recv_xfer(16) : send_xfer(16);
}

static int run_test (void)
{
	int ret, i, t;

	ret = sync_test();
	if (ret) {
		goto out;
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < iterations; i++) {
		for (t = 0; t < transfer_count; t++) {
			ret = dst_addr ? send_xfer(transfer_size) :
					 recv_xfer(transfer_size);
			if (ret) {
				goto out;
			}
		}

		for (t = 0; t < transfer_count; t++) {
			ret = dst_addr ? recv_xfer(transfer_size) :
					 send_xfer(transfer_size);
			if (ret) {
				goto out;
			}
		}
	}

	gettimeofday(&end, NULL);
	show_perf();
	ret = 0;

out:
	return ret;
}

static void set_options (int rs)
{
	int val;

	if (buffer_size) {
		rs_setsockopt(rs, SOL_SOCKET, SO_SNDBUF, (void *) &buffer_size,
			      sizeof buffer_size);
		rs_setsockopt(rs, SOL_SOCKET, SO_RCVBUF, (void *) &buffer_size,
			      sizeof buffer_size);
	} else {
		val = 1 << 19;
		rs_setsockopt(rs, SOL_SOCKET, SO_SNDBUF, (void *) &val, sizeof val);
		rs_setsockopt(rs, SOL_SOCKET, SO_RCVBUF, (void *) &val, sizeof val);
	}

	val = 1;
	rs_setsockopt(rs, IPPROTO_TCP, TCP_NODELAY, (void *) &val, sizeof(val));

	val = 1;
	if (flags & MSG_DONTWAIT) {
		rs_ioctlsocket(rs, FIONBIO, (u_long *)&val);
	}

	if (use_rs) {
		/* Inline size based on experimental data */
		if (optimization == opt_latency) {
			val = 384;
			rs_setsockopt(rs, SOL_RDMA, RDMA_INLINE, (char *)&val, sizeof val);
		} else if (optimization == opt_bandwidth) {
			val = 0;
			rs_setsockopt(rs, SOL_RDMA, RDMA_INLINE, (char *)&val, sizeof val);
		}
	}
}

static int server_listen (void)
{
	struct addrinfo hints, *res;
	int val, ret;

	memset(&hints, 0, sizeof hints);
	hints.ai_flags    = RAI_PASSIVE;
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
 	ret = getaddrinfo(src_addr, port, &hints, &res);
	if (ret) {
		perror("getaddrinfo");
		return ret;
	}

	lrs = (int)(rs_socket(res->ai_family, res->ai_socktype, res->ai_protocol));
	if (lrs < 0) {
		perror("rsocket");
		ret = lrs;
		goto free;
	}

	val = 1;
	ret = rs_setsockopt(lrs, SOL_SOCKET, SO_REUSEADDR, (char *)&val, sizeof val);
	if (ret) {
		perror("rsetsockopt SO_REUSEADDR");
		goto close;
	}

	ret = rs_bind(lrs, res->ai_addr, (int) res->ai_addrlen);
	if (ret) {
		perror("rbind");
		goto close;
	}

	ret = rs_listen(lrs, 1);
	if (ret) {
		perror("rlisten");
	}

close:
	if (ret) {
		rs_close(lrs);
	}

free:
	freeaddrinfo(res);
	return ret;
}

static int server_connect (void)
{
	struct pollfd fds;
	int ret = 0;

	set_options(lrs);
	do {
		if (use_async) {
			fds.fd = lrs;
			fds.events = POLLIN;

			ret = do_poll(&fds);
			if (ret) {
				perror("rpoll");
				return ret;
			}
		}

		rs = (int)(rs_accept(lrs, NULL, 0));
	} while (rs < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

	if (rs < 0) {
		ret = rs;
		perror("raccept");
		return ret;
	}

	set_options(rs);
	return ret;
}

static int client_connect (void)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct pollfd fds;
	int ret, err;
	socklen_t len;
	
	memset(&hints, 0, sizeof hints);
	hints.ai_flags  = RAI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
 	ret = getaddrinfo(dst_addr, port, &hints, &res);
	if (ret) {
		perror("getaddrinfo");
		return ret;
	}

	rs = (int)(rs_socket(res->ai_family, res->ai_socktype, res->ai_protocol));
	if (rs < 0) {
		perror("rsocket");
		ret = rs;
		goto free;
	}

	set_options(rs);
	/* TODO: bind client to src_addr */

	ret = rs_connect(rs, res->ai_addr, (int)res->ai_addrlen);
	if (ret && (errno != EINPROGRESS)) {
		perror("rconnect");
		goto close;
	}

	if (ret && (errno == EINPROGRESS)) {
		fds.fd = rs;
		fds.events = POLLOUT;
		ret = do_poll(&fds);
		if (ret) {
			goto close;
		}

		len = sizeof err;
		ret = rs_getsockopt(rs, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
		if (ret) {
			goto close;
		}

		if (err) {
			ret = -1;
			errno = err;
			perror("async rconnect");
		}
	}

close:
	if (ret) {
		rs_close(rs);
	}

free:
	if (res) {
		freeaddrinfo(res);
	}

	return ret;
}

static int run (void)
{
	int i, ret = 0;
	DWORD dwBytesReturned = 0;
	
	buf = malloc(!custom ? test_size[TEST_CNT - 1].size : transfer_size);
	if (!buf) {
		perror("malloc");
		return -1;
	}
	if (!dst_addr) {
		ret = server_listen();
		if (ret) {
			goto free;
		}
	}

	printf("%-10s%-8s%-8s%-8s%-8s%8s %10s%13s\n",
	       "name", "bytes", "xfers", "iters", "total", "time", "Gb/sec", "usec/xfer");
	if (!custom) {
		optimization = opt_latency;
		ret = dst_addr ? client_connect() : server_connect();
		if (ret) {
			goto free;
		}

		for (i = 0; i < TEST_CNT; i++) {
			if (test_size[i].option > size_option) {
				continue;
			}

			init_latency_test(test_size[i].size);
			run_test();
		}

		rs_shutdown(rs, SHUT_RDWR);
		rs_close(rs);

		optimization = opt_bandwidth;
		ret = dst_addr ? client_connect() : server_connect();
		if (ret) {
			goto free;
		}

		for (i = 0; i < TEST_CNT; i++) {
			if (test_size[i].option > size_option) {
				continue;
			}

			init_bandwidth_test(test_size[i].size);
			run_test();
		}
	} else {
		ret = dst_addr ? client_connect() : server_connect();
		if (ret) {
			goto free;
		}

		ret = run_test();
	}

	rs_shutdown(rs, SHUT_RDWR);
	rs_close(rs);

free:
	free(buf);
	return ret;
}

static int set_test_opt (char *optarg)
{
	if (strlen(optarg) == 1) {
		switch (optarg[0]) {
		case 's':
			use_rs = 0;
			break;
		case 'b':
			flags &= ~MSG_DONTWAIT;
			break;
		case 'n':
			flags |=  MSG_DONTWAIT;
			break;
		case 'v':
			verify = 1;
			break;		default:
			return -1;
		}
	} else if (!_strnicmp("socket",   optarg, 6)) {
		use_rs = 0;
	} else if (!_strnicmp("block",    optarg, 5)) {
		flags &= ~MSG_DONTWAIT;
	} else if (!_strnicmp("nonblock", optarg, 8)) {
		flags |=  MSG_DONTWAIT;
	} else if (!_strnicmp("verify",   optarg, 6)) {
		verify = 1;
	} else {
		return -1;
	}

	return 0;
}

int __cdecl main (int argc, char **argv)
{
	int op, ret;
	WSADATA wsaData;

    if (0 != (ret = WSAStartup(0x202,&wsaData)) ) {
        fprintf(stderr, "WSAStartup failed with error %d\n",ret);
		ret = -1;
        goto out;
    }
	while ((op = getopt(argc, argv, "s:b:B:I:C:S:p:T:")) != -1) {
		switch (op) {
		case 's':
			dst_addr = optarg;
			break;
		case 'b':
			src_addr = optarg;
			break;
		case 'B':
			buffer_size = atoi(optarg);
			break;
		case 'I':
			custom = 1;
			iterations = atoi(optarg);
			break;
		case 'C':
			custom = 1;
			transfer_count = atoi(optarg);
			break;
		case 'S':
			if (!_strnicmp("all", optarg, 3)) {
				size_option = 1;
			} else {
				custom = 1;
				transfer_size = atoi(optarg);
			}
			break;
		case 'p':
			port = optarg;
			break;
		case 'T':
			if (!set_test_opt(optarg)) {
				break;
			}
			/* invalid option - fall through */
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-s server_address]\n");
			printf("\t[-b bind_address]\n");
			printf("\t[-B buffer_size]\n");
			printf("\t[-I iterations]\n");
			printf("\t[-C transfer_count]\n");
			printf("\t[-S transfer_size or all]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-T test_option]\n");
			printf("\t    s|sockets - use standard tcp/ip sockets\n");
			printf("\t    b|blocking - use blocking calls\n");
			printf("\t    n|nonblocking - use nonblocking calls\n");
			printf("\t    v|verify - verify data\n");
			exit(1);
		}
	}
	if (!(flags & MSG_DONTWAIT)) {
		poll_timeout = -1;
	}

	ret = run();

out:
    WSACleanup();

	return ret;
}
