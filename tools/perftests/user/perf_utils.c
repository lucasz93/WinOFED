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
 *
 */

#include "perf_defs.h"

const char *sock_get_error_str(void)
{
    switch (WSAGetLastError()) {
	case WSANOTINITIALISED:
		return "WSANOTINITIALISED"; /* A successful WSAStartup call must occur before using this function */
	case WSAENETDOWN:
		return "WSAENETDOWN"; /* The network subsystem has failed */
	case WSAEFAULT:
		return "WSAEFAULT"; /* The buf parameter is not completely contained in a valid part of the user address space */
	case WSAENOTCONN:
		return "WSAENOTCONN"; /* The socket is not connected */
	case WSAEINTR:
		return "WSAEINTR"; /* The (blocking) call was canceled through WSACancelBlockingCall */
	case WSAEINPROGRESS:
		return "WSAEINPROGRESS"; /* A blocking Windows Sockets 1.1 call is in progress, or the service provider is still processing a callback function */
	case WSAENETRESET:
		return "WSAENETRESET"; /* The connection has been broken due to the keep-alive activity detecting a failure while the operation was in progress */
	case WSAENOTSOCK:
		return "WSAENOTSOCK"; /* The descriptor is not a socket */
	case WSAEOPNOTSUPP:
		return "WSAEOPNOTSUPP"; /* MSG_OOB was specified, but the socket is not stream-style such as type SOCK_STREAM, OOB data is not supported in the communication domain associated with this socket, or the socket is unidirectional and supports only send operations */
	case WSAESHUTDOWN:
		return "WSAESHUTDOWN"; /* The socket has been shut down; it is not possible to receive on a socket after shutdown has been invoked with how set to SD_RECEIVE or SD_BOTH */
	case WSAEWOULDBLOCK:
		return "WSAEWOULDBLOCK"; /* The socket is marked as nonblocking and the receive operation would block */
	case WSAEMSGSIZE:
		return "WSAEMSGSIZE"; /* The message was too large to fit into the specified buffer and was truncated */
	case WSAEINVAL:
		return "WSAEINVAL"; /* The socket has not been bound with bind, or an unknown flag was specified, or MSG_OOB was specified for a socket with SO_OOBINLINE enabled or (for byte stream sockets only) len was zero or negative */
	case WSAECONNABORTED:
		return "WSAECONNABORTED"; /* The virtual circuit was terminated due to a time-out or other failure. The application should close the socket as it is no longer usable */
	case WSAETIMEDOUT:
		return "WSAETIMEDOUT"; /* The connection has been dropped because of a network failure or because the peer system failed to respond */
	case WSAECONNRESET:
		return "WSAECONNRESET"; /* The virtual circuit was reset by the remote side executing a hard or abortive close. The application should close the socket as it is no longer usable. On a UPD-datagram socket this error would indicate that a previous send operation resulted in an ICMP "Port Unreachable" message */
	default:
		return "Unknown error";
	}
}

static int pp_write_keys(SOCKET sockfd, const struct pingpong_dest *my_dest)
{
	char msg[KEY_MSG_SIZE_GID];
	PERF_ENTER;
	sprintf_s(msg, KEY_MSG_SIZE_GID, KEY_PRINT_FMT_GID,cl_hton16(my_dest->lid), cl_hton32(my_dest->qpn), 
		cl_hton32(my_dest->psn), cl_hton32(my_dest->rkey), my_dest->vaddr, 
		my_dest->gid.unicast.prefix, my_dest->gid.unicast.interface_id);

	if (send(sockfd, msg, sizeof msg,0) != sizeof msg) {
		perror("pp_write_keys");
		fprintf(stderr, "Couldn't send local address %s (%x)\n",sock_get_error_str(), WSAGetLastError());
		return -1;
	}
	PERF_EXIT;
	return 0;
}

static int pp_read_keys(SOCKET sockfd,
			struct pingpong_dest *rem_dest)
{
	int parsed;

	char msg[KEY_MSG_SIZE_GID];
	PERF_ENTER;
	if (recv(sockfd, msg, sizeof msg, 0) != sizeof msg) {
		perror("pp_read_keys");
		fprintf(stderr, "Couldn't read remote address %s (%x)\n",sock_get_error_str(), WSAGetLastError());
		return -1;
	}
	
	parsed = sscanf_s(msg, KEY_PRINT_FMT_GID, &rem_dest->lid, &rem_dest->qpn,
			&rem_dest->psn,&rem_dest->rkey, &rem_dest->vaddr,
			&rem_dest->gid.unicast.prefix, &rem_dest->gid.unicast.interface_id);
				
	rem_dest->lid = cl_ntoh16(rem_dest->lid);
	rem_dest->qpn = cl_ntoh32(rem_dest->qpn);
	rem_dest->psn = cl_ntoh32(rem_dest->psn);
	rem_dest->rkey = cl_ntoh32(rem_dest->rkey);

	if (parsed != 7) {
		fprintf(stderr, "Couldn't parse line <%.*s > parsed = %d %s (%x)\n",
			(int)sizeof msg, msg,parsed,sock_get_error_str(), WSAGetLastError());
		return -1;
	}
	rem_dest->vaddr = (uintptr_t) rem_dest->vaddr;
	PERF_EXIT;
	return 0;
}

SOCKET pp_client_connect(const char *servername, int port)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = { 
		0,				//ai_flags
		AF_UNSPEC,		// ai_family
		SOCK_STREAM	//ai_socktype
	};
	char service[8];
	int n;
	SOCKET sockfd = INVALID_SOCKET;
	PERF_ENTER;
	sprintf_s(service, 8, "%d", port);
	n = getaddrinfo(servername, service, &hints, &res);

	if (n) {
		fprintf(stderr, "%s (%x) for %s:%d\n", sock_get_error_str(), WSAGetLastError(), servername, port);
		return sockfd;
	}

	for (t = res; t; t = t->ai_next) {
		if (t->ai_family != AF_INET)
			continue;
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd != INVALID_SOCKET) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			closesocket(sockfd);
			sockfd = INVALID_SOCKET;
		}
	}

	freeaddrinfo(res);

	if (sockfd == INVALID_SOCKET) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		return sockfd;
	}
	PERF_EXIT;
	return sockfd;
}

int pp_client_exch_dest(SOCKET sockfd, const struct pingpong_dest *my_dest,
			       struct pingpong_dest *rem_dest)
{
	PERF_ENTER;
	if (pp_write_keys(sockfd, my_dest))
		return -1;
	PERF_EXIT;
	return pp_read_keys(sockfd,rem_dest);
}

SOCKET pp_server_connect(int port)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		AI_PASSIVE,		//ai_flags
		AF_UNSPEC,		// ai_family
		SOCK_STREAM //ai_socktype
	};
	char service[8];
	SOCKET sockfd = INVALID_SOCKET, connfd;
	int n;
	PERF_ENTER;
	sprintf_s(service, 8, "%d", port);
	n = getaddrinfo(NULL, service, &hints, &res);

	if (n) {
		fprintf(stderr, "%s (%x) for port %d\n", sock_get_error_str(), WSAGetLastError(), port);
		return n;
	}

	for (t = res; t; t = t->ai_next) {
		if (t->ai_family != AF_INET)
			continue;
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd != INVALID_SOCKET) {
			n = 1;

			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&n, sizeof n);

			if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			closesocket(sockfd);
			sockfd = INVALID_SOCKET;
		}
	}

	freeaddrinfo(res);

	if (sockfd == INVALID_SOCKET) {
		fprintf(stderr, "Couldn't listen to port %d\n", port);
		return sockfd;
	}

	listen(sockfd, 1);
	connfd = accept(sockfd, NULL, 0);
	if (connfd == INVALID_SOCKET) {
		perror("server accept");
		fprintf(stderr, "accept() failed\n");
		closesocket(sockfd);
		return connfd;
	}

	closesocket(sockfd);
	PERF_EXIT;
	return connfd;
}

int pp_server_exch_dest(SOCKET sockfd, const struct pingpong_dest *my_dest,
			       struct pingpong_dest* rem_dest)
{
	PERF_ENTER;
	if (pp_read_keys(sockfd, rem_dest))
		return -1;

	PERF_EXIT;
	return pp_write_keys(sockfd, my_dest);
}





