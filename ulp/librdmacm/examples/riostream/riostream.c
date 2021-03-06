/*
 * Copyright (c) 2011-2012 Intel Corporation.  All rights reserved.
 * Copyright (c) 2013 Oce Printing Systems GmbH.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include "../../../../etc/user/gtod.c" // gettimeofday()
#include "getopt.c"

#include "..\src\openib_osd.h"
#include <rdma/rdma_cma.h>
#include <rdma/rwinsock.h>

#define MSG_DONTWAIT 0x80

#define EWOULDBLOCK WSAEWOULDBLOCK
#define EAGAIN      WSAEWOULDBLOCK
#define EINPROGRESS WSAEINPROGRESS
#undef  errno
#define errno (WSAGetLastError())
#define perror(s) printf("%s: WSAError=%d", s, errno)

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
static int use_async = 0;
static int verify = 0;
static int flags = 0/*MSG_DONTWAIT*/;
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
static char *buf;
static volatile uint8_t *poll_byte;

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
	static long long total_bytes = 0;
	uint8_t *array = buf;
	static uint8_t data = 0;
	int i;

	for (i = 0; i < size; i++, total_bytes++) {
		if (array[i] != data++) {
			printf("data verification failed data=0x%02X  total_bytes=%lld\n", data, total_bytes);
			_flushall();
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
		ret = select(
			nfds,
			FD_ISSET(fds->fd, &readfds  ) ? &readfds   : NULL,
			FD_ISSET(fds->fd, &writefds ) ? &writefds  : NULL,
			FD_ISSET(fds->fd, &exceptfds) ? &exceptfds : NULL,
			poll_timeout < 0 ? NULL : &timeout
		);
	} while (!ret);
	
	return (ret != SOCKET_ERROR ? 0 : ret);
}

static int send_msg (int size)
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

		ret = send(rs, buf + offset, size - offset, flags);
		if (ret > 0) {
			offset += ret;
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("send");
			return ret;
		}
	}

	return 0;
}

static int send_xfer (int size)
{
	struct pollfd fds;
	int offset, ret;

	if (verify) {
		format_buf(buf, size - 1);
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

		ret = rsIoWrite(rs, buf + offset, size - offset, offset, flags);
		if (ret > 0) {
			offset += ret;
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("rsIoWrite");
			return ret;
		}
	}

	return 0;
}

static int recv_msg (int size)
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

		ret = recv(rs, buf + offset, size - offset, flags);
		if (ret > 0) {
			offset += ret;
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("recv");
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

static int recv_xfer (int size, uint8_t marker)
{
	int ret;

	while (*poll_byte != marker) {
		;
	}

	if (verify) {
		ret = verify_buf(buf, size - 1);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

static int sync_test (void)
{
	int ret;

	ret = dst_addr ? send_msg(16) : recv_msg(16);
	if (ret) {
		return ret;
	}

	return dst_addr ? recv_msg(16) : send_msg(16);
}

static int run_test (void)
{
	int ret, i, t;
	off_t offset = -1;
	uint8_t marker = 0;

	 poll_byte = buf + transfer_size - 1;
	*poll_byte = -1;
	offset = rsIoMap(rs, buf, transfer_size, PROT_WRITE, 0, 0);
	if (offset ==  -1) {
		perror("rsIoMap");
		ret = -1;
		goto out;
	}
	ret = sync_test();
	if (ret) {
		goto out;
	}

	gettimeofday(&start, NULL);
	for (i = 0; i < iterations; i++) {
		if (dst_addr) {
			for (t = 0; t < transfer_count - 1; t++) {
				ret = send_xfer(transfer_size);
				if (ret) {
					goto out;
				}
			}

			*poll_byte = (uint8_t) marker++;
			ret = send_xfer(transfer_size);
			if (ret) {
				goto out;
			}

			ret = recv_xfer(transfer_size, marker++);
		} else {
			ret = recv_xfer(transfer_size, marker++);
			if (ret) {
				goto out;
			}

			for (t = 0; t < transfer_count - 1; t++) {
				ret = send_xfer(transfer_size);
				if (ret) {
					goto out;
				}
			}

			*poll_byte = (uint8_t) marker++;
			ret = send_xfer(transfer_size);
		}

		if (ret) {
			goto out;
		}
	}

	gettimeofday(&end, NULL);
	show_perf();

out:
	if (offset != -1) {
		rsIoUnmap(rs, buf, transfer_size);
	}

	return ret;
}

static void set_options (SOCKET rs)
{
	int val;

	if (buffer_size) {
		setsockopt(rs, SOL_SOCKET, SO_SNDBUF, (void *) &buffer_size,
			    sizeof buffer_size);
		setsockopt(rs, SOL_SOCKET, SO_RCVBUF, (void *) &buffer_size,
			    sizeof buffer_size);
	} else {
		val = 1 << 19;
		setsockopt(rs, SOL_SOCKET, SO_SNDBUF, (void *) &val, sizeof val);
		setsockopt(rs, SOL_SOCKET, SO_RCVBUF, (void *) &val, sizeof val);
	}

	val = 1;
	setsockopt(rs, IPPROTO_TCP, TCP_NODELAY, (void *) &val, sizeof(val));
	setsockopt(rs, SOL_RDMA, RDMA_IOMAPSIZE, (void *) &val, sizeof val);
	
	val = 1;
	if (flags & MSG_DONTWAIT) {
		ioctlsocket(rs, FIONBIO, (u_long *)&val);
	}

	/* Inline size based on experimental data */
	if (optimization == opt_latency) {
		val = 384;
		setsockopt(rs, SOL_RDMA, RDMA_INLINE, (char *)&val, sizeof val);
	} else if (optimization == opt_bandwidth) {
		val = 0;
		setsockopt(rs, SOL_RDMA, RDMA_INLINE, (char *)&val, sizeof val);
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

	lrs = (int)WSASocket(res->ai_family, res->ai_socktype, res->ai_protocol, rsGetProtocolInfo(NULL), 0, 0);
	if (lrs == INVALID_SOCKET) {
		perror("WSASocket");
		ret = (int)lrs;
		goto free;
	}

	val = 1;
	ret = setsockopt(lrs, SOL_SOCKET, SO_REUSEADDR, (char *)&val, sizeof val);
	if (ret) {
		perror("setsockopt SO_REUSEADDR");
		goto close;
	}

	ret = bind(lrs, res->ai_addr, res->ai_addrlen);
	if (ret) {
		perror("bind");
		goto close;
	}

	ret = listen(lrs, 1);
	if (ret) {
		perror("listen");
	}

close:
	if (ret) {
		closesocket(lrs);
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

		rs = (int)accept(lrs, NULL, 0);
	} while (rs == INVALID_SOCKET && (errno == EAGAIN || errno == EWOULDBLOCK));
	if (rs == INVALID_SOCKET) {
		perror("accept");
		return (int)rs;
	}

	set_options(rs);
	return ret;
}

static int client_connect (void)
{
	struct addrinfo hints, *res;
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

	rs = (int)WSASocket(res->ai_family, res->ai_socktype, res->ai_protocol, rsGetProtocolInfo(NULL), 0, 0);
	if (rs == INVALID_SOCKET) {
		perror("WSASocket");
		ret = (int)rs;
		goto free;
	}

	set_options(rs);
	/* TODO: bind client to src_addr */

	ret = connect(rs, res->ai_addr, res->ai_addrlen);
	if (ret && (errno != EINPROGRESS)) {
		perror("connect");
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
		ret = getsockopt(rs, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
		if (ret) {
			goto close;
		}

		if (err) {
			ret = -1;
			perror("async connect");
		}
	}

close:
	if (ret) {
		closesocket(rs);
	}

free:
	freeaddrinfo(res);
	return ret;
}

static int run (void)
{
	int i, ret = 0;

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

		if (SOCKET_ERROR == shutdown(rs, SHUT_RDWR)) {
			perror("shutdown");
		}

		if (SOCKET_ERROR == closesocket(rs)) {
			perror("closesocket");
		}

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

	if (SOCKET_ERROR == shutdown(rs, SHUT_RDWR)) {
		perror("shutdown");
	}

	if (SOCKET_ERROR == closesocket(rs)) {
		perror("closesocket");
	}

free:
	free(buf);
	return ret;
}

static int set_test_opt (char *optarg)
{
	if (strlen(optarg) == 1) {
		switch (optarg[0]) {
		case 'b':
			flags &= ~MSG_DONTWAIT;
			break;
		case 'n':
			flags |=  MSG_DONTWAIT;
			break;
		case 'v':
			verify = 1;
			break;
		default:
			return -1;
		}
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
