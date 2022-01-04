
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <stdio.h>

#include "contest.h"

static int init_winsock(void)
{
	// initialization of the winsock DLL first
	WORD wVersionRequested;
	WSADATA wsaData;
	int ret;
 
	wVersionRequested = MAKEWORD( 2, 2 );
	ret = WSAStartup( wVersionRequested, &wsaData );
	if ( ret != 0 ) {
		printf("WSAStartup Error!\n");
		return 1;
	}
 
	if ( LOBYTE( wsaData.wVersion ) != 2 ||
			HIBYTE( wsaData.wVersion ) != 2) {
		WSACleanup( );
		printf("Bad winsock version!\n");
	}

	return 0;
}


static struct {
	SOCKET s;
} conn[MAX_CONNS];


int client(const char *host)
{
	struct sockaddr_in to_addr;
	int i;
	char message[100];
	int recd, sent, len;

	if (init_winsock()) {
		return 1;
	}

	to_addr.sin_family = AF_INET;
    to_addr.sin_addr.s_addr = inet_addr(host);
	to_addr.sin_port = htons(CONTEST_PORT);

	printf("Creating sockets\n");

	for (i=0; i<MAX_CONNS; i++) {
		conn[i].s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (conn[i].s == INVALID_SOCKET) {
			printf("failed at connection %d\n", i);
			return -1;
		}
	}

	printf("Opening connections\n");

	for (i=0; i<MAX_CONNS; i++) {
		if (connect(conn[i].s, (struct sockaddr*) &to_addr, sizeof(to_addr)) == SOCKET_ERROR) {
			printf("failed at connection %d\n", i);
			return -1;
		}
	}

	printf("send/receive data\n");

	for (i=0; i<MAX_CONNS; i++) {
		len = sizeof(message);
	
		/* Hope that sent==len with such a short message */
		sent = send(conn[i].s, message, len, 0 );
		if (sent != len) {
			printf("bah! connection %d\n", i);
			return 1;
		}

		recd = recv(conn[i].s, message, len, 0 );
		if (recd != len) {
			printf("bah! connection %d\n", i);
			return 1;
		}
    }

	printf("Closing all connections\n");
	Sleep(5000);
	for (i=0; i<MAX_CONNS; i++) {
		if (closesocket(conn[i].s) == INVALID_SOCKET) {
			printf("failed at connection %d\n", i);
			return -1;
		}
	}

	printf("All connections closed\n");
	Sleep(5000);
	WSACleanup();

	return 0;
}

int server(void)
{
	SOCKET listen_socket = INVALID_SOCKET;
	int i;
	struct sockaddr_in addr;
	int addrlen;
	int recd, sent, len;
	char message[100];

	if (init_winsock()) {
		return 1;
	}

	listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket == INVALID_SOCKET) {
		printf("WSASocketfailed\n");
		return 1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONTEST_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(listen_socket, (const struct sockaddr*)&addr, 
		sizeof(addr)) == SOCKET_ERROR) {
		printf("bind error!");
		return 1;
	}

	if (SOCKET_ERROR == listen(listen_socket, LISTEN_BACKLOG)) {
		printf("listen failed\n");
		return 1;
	}

	printf("Accepting connections\n");

	for (i=0; i<MAX_CONNS; i++) {
		addrlen = sizeof(addr);
		conn[i].s = accept(listen_socket, (struct sockaddr*) &addr, &addrlen);
		if (conn[i].s == INVALID_SOCKET) {
			printf("failed at connection %d, error %d\n", i, WSAGetLastError());
			return -1;
		}
	}

	printf("got all connections\n");

	for (i=0; i<MAX_CONNS; i++) {
		len = sizeof(message);

		recd = recv(conn[i].s, message, len, 0 );
		if (recd != len) {
			printf("bah! connection %d\n", i);
			return 1;
		}
		/* Hope that sent==len with such a short message */
		sent = send(conn[i].s, message, len, 0 );
		if (sent != len) {
			printf("bah! connection %d\n", i);
			return 1;
		}
	}

	printf("Closing all connections\n");

	Sleep(5000);

	for (i=0; i<MAX_CONNS; i++) {
		if (closesocket(conn[i].s) == INVALID_SOCKET) {
			printf("failed at connection %d\n", i);
			return -1;
		}
	}

	printf("All connections closed\n");

	Sleep(5000);

	WSACleanup();

	return 0;
}

int main(int argc, void *argv[])
{
	if (argc == 1) {
		printf("Server\n");
		server();
	} 
	else if (argc == 2) {
		printf("Client\n");
		client(argv[1]);
	}
}
