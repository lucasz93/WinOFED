#ifndef OPENIB_OSD_H
#define OPENIB_OSD_H

#if defined(FD_SETSIZE) && FD_SETSIZE != 1024
#undef FD_SETSIZE
#endif
#define FD_SETSIZE 1024 /* Set before including winsock2 - see select help */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>

#define container_of	CONTAINING_RECORD
#define offsetof		FIELD_OFFSET
#define ssize_t			SSIZE_T

#define __thread		/*???*/

#define ntohll			_byteswap_uint64
#define htonll			_byteswap_uint64

#define SHUT_RD				SD_RECEIVE
#define SHUT_WR				SD_SEND
#define SHUT_RDWR			SD_BOTH

#define O_NONBLOCK		0x4000

/* allow casting to WSABUF */
struct iovec {
       u_long iov_len;
       char FAR* iov_base;
};

struct msghdr {
	void         *msg_name;       // optional address
	socklen_t     msg_namelen;    // size of address
	struct iovec *msg_iov;        // scatter/gather array
	int           msg_iovlen;     // members in msg_iov
	void         *msg_control;    // ancillary data, see below
	socklen_t     msg_controllen; // ancillary data buffer len
	int           msg_flags;      // flags on received message
};

#if(_WIN32_WINNT < 0x0600)

/* Event flag definitions for WSAPoll(). */

#define POLLRDNORM  0x0100
#define POLLRDBAND  0x0200
#define POLLIN      (POLLRDNORM | POLLRDBAND)
#define POLLPRI     0x0400

#define POLLWRNORM  0x0010
#define POLLOUT     (POLLWRNORM)
#define POLLWRBAND  0x0020

#define POLLERR     0x0001
#define POLLHUP     0x0002
#define POLLNVAL    0x0004

typedef struct pollfd {

    SOCKET  fd;
    SHORT   events;
    SHORT   revents;

} WSAPOLLFD, *PWSAPOLLFD, FAR *LPWSAPOLLFD;

#endif // (_WIN32_WINNT < 0x0600)

#endif // OPENIB_OSD_H
